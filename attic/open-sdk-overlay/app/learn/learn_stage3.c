/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 3: Shadow (SMK-37) / Tape (FM-1). Grow it.
 *
 * Shadow: up to three tracks (LT_SHADOW_A..C) that are described by their
 * relationship to a track that already exists. A shadow is created in two
 * presses, source then rule, and is re-rendered from the source's cells and
 * the song chords whenever the engine's edit revision moves.
 *
 * Tape: four tracks of takes (LT_MELODY + the three shadows) in RAM. A take
 * is a copy of the track's cells; the engine's own recorder captures the
 * notes, this page decides which bars it may touch (punch by bar), stacks
 * the takes for undo/redo, swaps them for A/B replay and dumps them as
 * SysEx so a DAW can keep them. Nothing here writes Flash.
 *
 * Controls (logical, both boards):
 *   keys        play the selected tape track live (recorded while taping)
 *   REC         hold while playing: punch next bar; tap: A/B previous/current take
 *   REC+PLAY    tape a whole pass now        PLAY   play/stop (ends a tape pass)
 *   HOME        stop everything              SAVE   undo take  GLO+SAVE redo take
 *   HOME+REC    export the four tape tracks as LCMD_TRACK frames
 *   SCL         next tape track              ENV    hints on/off
 *   FX          remove the selected shadow   EDIT   next shadow slot
 *   ARP         next lesson / stop
 *   K1 octave  K2 density (FM-1: obedience, GLO+K2 density)  K3 velocity
 *   K4 patch   K5 send pick  K6 send amount  K7 slow %
 *   SMK pads    1-3 source (Chords, Melody, Drums)  4-6 shadow slots
 *               7 next tape track  8 export  9-16 rules; aftertouch on 1-6 = solo
 *   SMK faders  bank 1 mixer (Drums, Bass, Chords, Melody); bank 2 obedience A, B, C + send
 *   FM-1 keys   with GLO held: keys 1-3 source, 4-6 shadow slot, 9-16 rule, 17-20 tape track */
#include "learn_song.h"
#include <string.h>

enum { SHADOWS = 3, TAPE_TRACKS = 4, TAKES = 4 };
enum { SRC_CHORDS, SRC_MELODY, SRC_DRUMS, SRC_COUNT };
enum { RULE_HOLD, RULE_ARP, RULE_THIRD, RULE_SIXTH, RULE_ECHO, RULE_ANSWER, RULE_TOP, RULE_GHOST, RULE_COUNT };
static const uint8_t rule_source[RULE_COUNT] = {SRC_CHORDS, SRC_CHORDS, SRC_MELODY, SRC_MELODY, SRC_MELODY, SRC_MELODY, SRC_CHORDS, SRC_DRUMS};
static const char *const rule_name[RULE_COUNT] = {"HOLD", "ARP", "3RD ABOVE", "6TH BELOW", "ECHO", "ANSWER", "TOP NOTE", "GHOST"};
static const char *const rule_hint[RULE_COUNT] = {
    "HOLD: CHORD TONES, ONE BEAT EACH", "ARP: CHORD TONES IN YOUR PATTERN",
    "3RD ABOVE: MELODY +2 SCALE STEPS", "6TH BELOW: MELODY -5 SCALE STEPS",
    "ECHO: 1 BEAT LATE, 8VA, 60% VEL", "ANSWER: LAST 4 NOTES, BACKWARDS",
    "TOP NOTE: HIGHEST CHORD TONE", "GHOST: QUIET HITS OFF THE HAT"};
static const char *const source_name[SRC_COUNT] = {"CHORDS", "MELODY", "DRUMS"};
static const uint8_t source_track[SRC_COUNT] = {LT_CHORDS, LT_MELODY, LT_DRUMS};

struct shadow { uint8_t used, source, rule, density, velocity, bank, patch, obedience; int8_t octave; };
static struct shadow shadow[SHADOWS];
static uint8_t shadow_sel;                 /* selected shadow slot */
static uint8_t arp_pattern[8] = {0, 1, 2, 1}, arp_len = 4, arp_entry; static uint32_t arp_entry_at;
static uint8_t source_armed;               /* 1 + source, 0 none */
static uint32_t source_armed_at;
static uint8_t rule_hinted;                /* bit per rule: hint already shown */
static uint8_t hints_on = 1;
static uint8_t pad_down[16]; static uint32_t pad_hold_since[16]; static uint8_t pad_hint_done[16];
static uint8_t solo_held;                  /* bit per track: momentary solo on */
static uint32_t seen_revision; static uint32_t revision_changed_at; static uint8_t render_pending;
static unsigned key_root, key_scale;       /* key cached from lctx for rendering */

/* ---- tape ---- */
static struct groove_step takes[TAPE_TRACKS][TAKES][SONG_STEPS];  /* 4 KB: the budget */
static struct groove_step scratch[SONG_STEPS];                    /* pre-punch copy / render output */
static struct groove_step src_cells[SONG_STEPS];                  /* source read buffer */
static uint8_t take_count[TAPE_TRACKS], take_cur[TAPE_TRACKS];    /* cur = index loaded, 255 none */
static uint8_t tape_sel;                   /* 0 melody, 1..3 shadows */
static uint8_t punch_armed, punch_on, punch_bars, rec_full, rec_consumed, rec_slow;
static uint8_t ab_pending, ab_side;        /* A/B replay: swap at the next bar */
static unsigned last_step; static uint8_t bar_boundary;
static uint8_t send_pick;                  /* 0 reverb, 1 delay */
static uint8_t export_track, export_frame, export_on;
static uint8_t from_partner;
static uint8_t demo_phase; static uint32_t demo_until; static uint8_t demo_src_track, demo_shadow_track;
static uint32_t sounding[4];               /* live notes we started, by MIDI note */
static uint32_t hint_until; static char hint_line[40];
static uint8_t follow_pad;

/* ---- drawn-state cache ---- */
static char shown_head[32], shown_big[16], shown_sub[32], shown_prompt[32], shown_hint[40], shown_notice[32];
static uint32_t shown_lane[TAPE_TRACKS]; static unsigned shown_pads = ~0U;

static unsigned slow_ms(unsigned ms, const struct lctx *x) { return ms * 100U / (x->slow_percent ? x->slow_percent : 100); }
static unsigned tape_track(unsigned t) { return t == 0 ? LT_MELODY : LT_SHADOW_A + t - 1; }
static unsigned loop_steps(void) { unsigned s = song.bars * 16; return s < 16 ? 16 : s > SONG_STEPS ? SONG_STEPS : s; }
static void num2(char *p, unsigned v) { p[0] = (char)('0' + (v / 10) % 10); p[1] = (char)('0' + v % 10); }
static char *cat(char *d, const char *s, unsigned max) { unsigned n = 0; while (*d) ++d; while (*s && n < max) { *d++ = *s++; ++n; } *d = 0; return d; }

static void set_step_setting(unsigned track, unsigned step, unsigned setting, unsigned value)
{
    int delta = (int)value - (int)groove_step_setting(track, step, setting);
    if (delta) groove_step_setting_edit(track, step, setting, delta);
}

/* ---- theory helpers ---- */
static unsigned diatonic_shift(unsigned note, int degrees)
{
    if (!theory_in_scale(key_root, key_scale, note)) note = theory_nudge(key_root, key_scale, note);
    int n = (int)note;
    while (degrees > 0 && n < 127) { do { ++n; } while (n < 127 && !theory_in_scale(key_root, key_scale, (unsigned)n)); --degrees; }
    while (degrees < 0 && n > 0) { do { --n; } while (n > 0 && !theory_in_scale(key_root, key_scale, (unsigned)n)); ++degrees; }
    return (unsigned)(n < 0 ? 0 : n > 127 ? 127 : n);
}

/* ---- rendering ---- */
static unsigned jitter_for(const struct shadow *sh, unsigned s, unsigned vel)
{
    unsigned loose = 127U - sh->obedience;                  /* 0 strict .. 127 loose */
    unsigned j = loose * 30U / 127U;
    uint32_t r = (uint32_t)(s * 2654435761U) ^ (uint32_t)(sh->rule * 97U);
    r = (r >> 13) ^ r;
    int v = (int)vel * (int)sh->velocity / 100;
    if (j) v += (int)(r % (2U * j + 1U)) - (int)j;
    return (unsigned)(v < 1 ? 1 : v > 127 ? 127 : v);
}
static unsigned probability_for(const struct shadow *sh)
{
    if (sh->obedience >= 120) return 0;                      /* always */
    return 4U + (unsigned)sh->obedience * 3U / 120U;         /* 4/8 .. 6/8 */
}
static unsigned density_keep(const struct shadow *sh, unsigned s)
{
    if (sh->density >= 7) return 1;
    return ((s * 5U + 3U) % 8U) <= sh->density;
}
static void put_note(unsigned track, const struct shadow *sh, unsigned s, int note, unsigned vel, unsigned gate, unsigned tie)
{
    note += 12 * sh->octave;
    if (note < 0 || note > 127 || !vel) return;
    groove_sequence_edit(track, s, (unsigned)note, jitter_for(sh, s, vel));
    set_step_setting(track, s, 1, gate);
    if (tie) set_step_setting(track, s, 4, 1);
    unsigned p = probability_for(sh); if (p) set_step_setting(track, s, 2, p);
}
static void put_chord(unsigned track, const struct shadow *sh, unsigned s, const uint8_t *notes, unsigned n, unsigned vel, unsigned tie)
{
    uint8_t shifted[4];
    for (unsigned i = 0; i < n; ++i) { int v = (int)notes[i] + 12 * sh->octave; if (v < 0 || v > 127) return; shifted[i] = (uint8_t)v; }
    if (groove_sequence_chord(track, s, shifted, n, jitter_for(sh, s, vel))) return;
    set_step_setting(track, s, 1, 15);
    if (tie) set_step_setting(track, s, 4, 1);
    unsigned p = probability_for(sh); if (p) set_step_setting(track, s, 2, p);
}

static void render_shadow(unsigned id)
{
    const struct shadow *sh = &shadow[id];
    unsigned t = LT_SHADOW_A + id, steps = loop_steps();
    if (!sh->used) return;
    unsigned want = sh->rule == RULE_GHOST ? GROOVE_808 : GROOVE_DX7;
    if (groove_source_type(t) != want) groove_source_set(t, want);
    groove_sequence_length_set(t, steps);
    groove_edit_clear(t, 0, 2);
    groove_sequence_read(source_track[sh->source], 0, src_cells, SONG_STEPS);
    switch (sh->rule) {
    case RULE_HOLD:
        for (unsigned beat = 0; beat < steps / 4; ++beat) {
            uint8_t notes[4]; unsigned n; song_chord_notes(song.chord[beat].degree, song.chord[beat].seventh, notes, &n);
            if (!n || !density_keep(sh, beat)) continue;
            for (unsigned k = 0; k < 4; ++k) put_chord(t, sh, beat * 4 + k, notes, n, 80, k > 0);
        }
        break;
    case RULE_ARP: {
        unsigned rate = sh->density >= 6 ? 1 : sh->density >= 3 ? 2 : 4, k = 0;
        for (unsigned s = 0; s < steps; s += rate, ++k) {
            unsigned seventh; int d = song_degree_at_step(s, &seventh);
            uint8_t notes[4]; unsigned n; song_chord_notes(d > 0 ? (unsigned)d : 0, seventh, notes, &n);
            if (!n) continue;
            unsigned idx = arp_pattern[k % (arp_len ? arp_len : 1)];
            put_note(t, sh, s, (int)notes[idx % n] + 12 * (int)(idx / n), 90, rate == 1 ? 8 : 10, 0);
        }
        break; }
    case RULE_THIRD: case RULE_SIXTH:
        for (unsigned s = 0; s < steps; ++s) {
            if (!src_cells[s].velocity || !density_keep(sh, s)) continue;
            unsigned gate = (src_cells[s].drums >> 5) & 15U;
            put_note(t, sh, s, (int)diatonic_shift(src_cells[s].note, sh->rule == RULE_THIRD ? 2 : -5), src_cells[s].velocity, gate, (src_cells[s].drums >> 12) & 1U);
        }
        break;
    case RULE_ECHO:
        for (unsigned s = 0; s < steps; ++s) {
            if (!src_cells[s].velocity || !density_keep(sh, s)) continue;
            put_note(t, sh, (s + 4) % steps, (int)src_cells[s].note + 12, src_cells[s].velocity * 60U / 100U, (src_cells[s].drums >> 5) & 15U, 0);
        }
        break;
    case RULE_ANSWER: {
        uint8_t notes[SONG_STEPS]; uint8_t at[SONG_STEPS]; unsigned count = 0;
        for (unsigned s = 0; s < steps; ++s) if (src_cells[s].velocity) { notes[count] = src_cells[s].note; at[count] = (uint8_t)s; ++count; }
        if (!count) break;
        for (unsigned bar = 0; bar < steps / 16; ++bar) {
            unsigned silent = 1; for (unsigned s = bar * 16; s < bar * 16 + 16; ++s) if (src_cells[s].velocity) silent = 0;
            if (!silent) continue;
            unsigned last = count - 1; for (unsigned i = 0; i < count; ++i) if (at[i] < bar * 16) last = i;
            for (unsigned k = 0; k < 4 && k < count; ++k) {
                unsigned i = (last + count - k) % count;
                if (density_keep(sh, bar * 16 + k * 4)) put_note(t, sh, bar * 16 + k * 4, notes[i], 85, 10, 0);
            }
        }
        break; }
    case RULE_TOP: {
        int prev = -1;
        for (unsigned s = 0; s < steps; ++s) {
            unsigned seventh; int d = song_degree_at_step(s, &seventh);
            uint8_t notes[4]; unsigned n; song_chord_notes(d > 0 ? (unsigned)d : 0, seventh, notes, &n);
            if (!n) { prev = -1; continue; }
            int top = notes[n - 1];
            put_note(t, sh, s, top, 70, 15, top == prev);
            prev = top;
        }
        break; }
    default: { /* GHOST: quiet hat hits where the hat lane is silent */
        unsigned hat = groove_drum_note(10); int d = groove_pad_index(LT_DRUMS, hat);
        if (d < 0) break;
        for (unsigned s = 0; s < steps; ++s) {
            if ((src_cells[s].drums >> d) & 1U) continue;
            if (!density_keep(sh, s)) continue;
            unsigned p = probability_for(sh);
            groove_sequence_edit(t, s, hat, jitter_for(sh, s, 35));
            if (p) set_step_setting(t, s, 2, p);
        }
        break; }
    }
}
static void render_all(void)
{
    for (unsigned i = 0; i < SHADOWS; ++i) render_shadow(i);
    seen_revision = groove_edit_revision(); render_pending = 0;
}

static void shadow_send(unsigned id)
{
    uint8_t p[4] = {(uint8_t)id, shadow[id].source, shadow[id].rule, shadow[id].obedience};
    lsyx_send(LCMD_SHADOW, p, 4);
}
static void shadow_create(struct lctx *x, unsigned id, unsigned source, unsigned rule, unsigned obedience)
{
    if (id >= SHADOWS || rule >= RULE_COUNT) return;
    struct shadow *sh = &shadow[id];
    memset(sh, 0, sizeof *sh);
    sh->used = 1; sh->source = (uint8_t)source; sh->rule = (uint8_t)rule; sh->density = 7; sh->velocity = 100;
    sh->obedience = (uint8_t)(obedience > 127 ? 127 : obedience); sh->octave = 0;
    sh->bank = rule == RULE_GHOST ? 0 : rule_source[rule] == SRC_CHORDS ? BANK_PAD : BANK_PLUCK; sh->patch = 0;
    if (rule != RULE_GHOST) learn_bank_select(LT_SHADOW_A + id, sh->bank, sh->patch);
    render_shadow(id); seen_revision = groove_edit_revision();
    shadow_sel = (uint8_t)id;
    if (rule == RULE_ARP) { arp_entry = 1; arp_entry_at = x->now; learn_notice("TAP A PATTERN ON PADS 9-16", 1500); }
    if (hints_on && !(rule_hinted & (1U << rule))) { rule_hinted |= (uint8_t)(1U << rule); strncpy(hint_line, rule_hint[rule], sizeof hint_line - 1); hint_until = x->now + 3000U; }
    x->redraw = 1;
}
static void shadow_remove(struct lctx *x, unsigned id)
{
    if (id >= SHADOWS || !shadow[id].used) return;
    unsigned t = LT_SHADOW_A + id;
    if (solo_held & (1U << t)) { groove_mix_toggle(t, 1); solo_held &= (uint8_t)~(1U << t); }
    groove_edit_clear(t, 0, 2);
    if (groove_source_type(t) != GROOVE_DX7) groove_source_set(t, GROOVE_DX7);
    shadow[id].used = 0; seen_revision = groove_edit_revision();
    learn_notice("SHADOW REMOVED", 800); x->redraw = 1;
}

/* ---- momentary solo ---- */
static void solo_set(unsigned track, unsigned on)
{
    unsigned bit = 1U << track;
    if (!!(solo_held & bit) == !!on) return;
    groove_mix_toggle(track, 1);
    if (on) solo_held |= (uint8_t)bit; else solo_held &= (uint8_t)~bit;
    uint8_t p[2] = {(uint8_t)track, (uint8_t)on}; lsyx_send(LCMD_SOLO, p, 2);
}
static void solo_clear(void) { for (unsigned t = 0; t < GROOVE_TRACKS; ++t) if (solo_held & (1U << t)) solo_set(t, 0); }

/* ---- takes ---- */
static unsigned count_notes(const struct groove_step *c, unsigned steps)
{
    unsigned n = 0; for (unsigned s = 0; s < steps; ++s) if (c[s].velocity) ++n; return n;
}
static void take_capture(struct lctx *x, unsigned tt)
{
    unsigned track = tape_track(tt);
    if (take_count[tt] == TAKES) { memmove(takes[tt][0], takes[tt][1], sizeof takes[tt][0] * (TAKES - 1)); take_count[tt] = TAKES - 1; }
    groove_sequence_read(track, 0, takes[tt][take_count[tt]], SONG_STEPS);
    take_cur[tt] = take_count[tt]; ++take_count[tt];
    unsigned notes = count_notes(takes[tt][take_cur[tt]], loop_steps());
    /* Score: notes in the key out of notes played. */
    unsigned in_key = 0;
    for (unsigned s = 0; s < loop_steps(); ++s) if (takes[tt][take_cur[tt]][s].velocity && theory_in_scale(key_root, key_scale, takes[tt][take_cur[tt]][s].note)) ++in_key;
    unsigned score = notes ? in_key * 100U / notes : 0;
    log_add(LOGK_TAKE, in_key, notes - in_key, song.tempo, rec_slow, lesson_running(), score);
    const struct lesson_step *ls = lesson_step();
    if (ls && ls->type == DR_REFTAKE) {
        unsigned need = ls->data[0] ? ls->data[0] : 4;
        unsigned passed = notes >= need && score >= (ls->target ? ls->target : 80) && !rec_slow;
        lesson_step_done(passed, score);
    }
    uint8_t p[3] = {(uint8_t)track, 1, take_count[tt]}; lsyx_send(LCMD_TRACK, p, 3);
    learn_notice(rec_slow ? "TAKE SAVED (SLOW)" : "TAKE SAVED", 900);
    x->redraw = 1;
}
static void take_load(unsigned tt, unsigned index)
{
    if (index >= take_count[tt]) return;
    groove_sequence_replace(tape_track(tt), 0, takes[tt][index], SONG_STEPS);
    take_cur[tt] = (uint8_t)index;
}
static void take_undo(struct lctx *x)
{
    unsigned tt = tape_sel;
    if (take_cur[tt] == 255 || take_cur[tt] == 0 || take_count[tt] < 2) { learn_notice("NO EARLIER TAKE", 800); return; }
    take_load(tt, take_cur[tt] - 1); learn_notice("UNDO: EARLIER TAKE", 800); x->redraw = 1;
}
static void take_redo(struct lctx *x)
{
    unsigned tt = tape_sel;
    if (take_cur[tt] == 255 || take_cur[tt] + 1U >= take_count[tt]) { learn_notice("NO LATER TAKE", 800); return; }
    take_load(tt, take_cur[tt] + 1); learn_notice("REDO: LATER TAKE", 800); x->redraw = 1;
}

/* ---- recording ---- */
static void rec_begin(struct lctx *x, unsigned full)
{
    unsigned track = tape_track(tape_sel);
    if (tape_sel) groove_edit_record(1, track, 0); else song_record(1);
    rec_slow = x->slow_percent < 100; rec_full = (uint8_t)full;
    if (!song_playing()) song_play(1);
    uint8_t p = 2; if (!from_partner) lsyx_send(LCMD_TRANSPORT, &p, 1);
    x->redraw = 1;
}
static void rec_end(struct lctx *x)
{
    unsigned track = tape_track(tape_sel);
    if (!groove_edit_recording() && !punch_on && !rec_full) return;
    if (tape_sel) groove_edit_record(0, track, 0); else song_record(0);
    if (punch_on) {
        /* Only the punched bars keep the new cells; the rest returns to the pre-punch copy. */
        groove_sequence_read(track, 0, src_cells, SONG_STEPS);
        for (unsigned s = 0; s < SONG_STEPS; ++s) if (!((punch_bars >> (s / 16)) & 1U)) src_cells[s] = scratch[s];
        groove_sequence_replace(track, 0, src_cells, SONG_STEPS);
    }
    punch_on = punch_armed = rec_full = 0; punch_bars = 0;
    take_capture(x, tape_sel);
}
static void stop_all(struct lctx *x)
{
    if (groove_edit_recording() || punch_on || rec_full) rec_end(x);
    punch_armed = 0; ab_pending = 0; demo_phase = 0;
    if (song_playing()) song_play(0);
    solo_clear(); lmidi_all_off(); memset(sounding, 0, sizeof sounding);
    uint8_t p = 0; if (!from_partner) lsyx_send(LCMD_TRANSPORT, &p, 1);
    x->redraw = 1;
}

/* ---- export ---- */
static void export_poll(void)
{
    if (!export_on) return;
    unsigned tt = export_track, track = tape_track(tt), first = export_frame * 8U;
    struct groove_step c[8]; groove_sequence_read(track, first, c, 8);
    uint8_t p[3 + 8 * 5]; p[0] = (uint8_t)track; p[1] = (uint8_t)export_frame; p[2] = take_cur[tt] == 255 ? 0 : (uint8_t)(take_cur[tt] + 1);
    for (unsigned i = 0; i < 8; ++i) {
        p[3 + i * 5] = c[i].note & 127; p[4 + i * 5] = c[i].velocity & 127;
        p[5 + i * 5] = c[i].drums & 127; p[6 + i * 5] = (c[i].drums >> 7) & 127; p[7 + i * 5] = (c[i].drums >> 14) & 3;
    }
    lsyx_send(LCMD_TRACK, p, sizeof p);
    if (++export_frame >= loop_steps() / 8) { export_frame = 0; if (++export_track >= TAPE_TRACKS) { export_on = 0; learn_notice("EXPORT DONE", 1000); } }
}

/* ---- why-hint demo: source alone, then shadow alone ---- */
static void demo_start(struct lctx *x, unsigned rule)
{
    strncpy(hint_line, rule_hint[rule], sizeof hint_line - 1); hint_until = x->now + 3000U;
    unsigned id = SHADOWS; for (unsigned i = 0; i < SHADOWS; ++i) if (shadow[i].used && shadow[i].rule == rule) id = i;
    if (id == SHADOWS || !song_playing()) { x->redraw = 1; return; }
    demo_src_track = source_track[shadow[id].source]; demo_shadow_track = (uint8_t)(LT_SHADOW_A + id);
    solo_clear(); solo_set(demo_src_track, 1); demo_phase = 1; demo_until = x->now + slow_ms(2400, x);
    learn_notice("SOURCE ALONE...", 1200); x->redraw = 1;
}
static void demo_poll(struct lctx *x)
{
    if (!demo_phase || (int32_t)(x->now - demo_until) < 0) return;
    if (demo_phase == 1) { solo_set(demo_src_track, 0); solo_set(demo_shadow_track, 1); demo_phase = 2; demo_until = x->now + slow_ms(2400, x); learn_notice("...NOW THE SHADOW", 1200); }
    else { solo_set(demo_shadow_track, 0); demo_phase = 0; }
    x->redraw = 1;
}

/* ---- selection actions shared by pads (SMK) and GLO+keys (FM-1) ---- */
static void select_source(struct lctx *x, unsigned source)
{
    source_armed = (uint8_t)(source + 1); source_armed_at = x->now; arp_entry = 0;
    learn_notice(source == SRC_CHORDS ? "CHORDS: NOW PICK A RULE" : source == SRC_MELODY ? "MELODY: NOW PICK A RULE" : "DRUMS: NOW PICK A RULE", 2000);
    /* Follow-me: the first rule pad of this source. */
    for (unsigned r = 0; r < RULE_COUNT; ++r) if (rule_source[r] == source) { follow_pad = (uint8_t)(9 + r); uint8_t p[2] = {0, (uint8_t)(8 + r)}; lsyx_send(LCMD_PATH, p, 2); break; }
    x->redraw = 1;
}
static void pick_rule(struct lctx *x, unsigned rule)
{
    if (!source_armed) { learn_notice("PICK A SOURCE FIRST (PADS 1-3)", 1200); return; }
    unsigned source = source_armed - 1;
    if (rule_source[rule] != source) {
        char line[32] = ""; cat(line, rule_name[rule], 12); cat(line, " FOLLOWS ", 10); cat(line, source_name[rule_source[rule]], 8);
        learn_notice(line, 1200); return;
    }
    unsigned id = SHADOWS; for (unsigned i = 0; i < SHADOWS; ++i) if (!shadow[i].used) { id = i; break; }
    if (id == SHADOWS) { id = shadow_sel; learn_notice("REPLACING THE SELECTED SHADOW", 1000); }
    shadow_create(x, id, source, rule, shadow[id].obedience ? shadow[id].obedience : 127);
    shadow_send(id);
    source_armed = 0; follow_pad = 0; uint8_t p[2] = {2, 0}; lsyx_send(LCMD_PATH, p, 2);
    char line[32] = ""; cat(line, "SHADOW ", 8); line[7] = (char)('A' + id); line[8] = 0; cat(line, ": ", 2); cat(line, rule_name[rule], 12);
    learn_notice(line, 1200);
}
static void select_tape(struct lctx *x, unsigned tt)
{
    if (tt >= TAPE_TRACKS) return;
    if (groove_edit_recording()) rec_end(x);
    lmidi_all_off(); memset(sounding, 0, sizeof sounding);
    tape_sel = (uint8_t)tt;
    learn_notice(tt == 0 ? "TAPE: MELODY" : tt == 1 ? "TAPE: SHADOW A" : tt == 2 ? "TAPE: SHADOW B" : "TAPE: SHADOW C", 900);
    x->redraw = 1;
}

/* ---- public ---- */
void stage3_init(void)
{
    memset(shadow, 0, sizeof shadow); memset(take_count, 0, sizeof take_count); memset(take_cur, 255, sizeof take_cur);
    shadow_sel = tape_sel = 0; source_armed = 0; arp_entry = 0; rule_hinted = 0; solo_held = 0;
    punch_armed = punch_on = punch_bars = rec_full = rec_consumed = 0; ab_pending = 0; export_on = 0; demo_phase = 0;
    seen_revision = groove_edit_revision(); render_pending = 0; key_root = 0; key_scale = SCALE_MAJOR;
    memset(sounding, 0, sizeof sounding); hint_line[0] = 0; follow_pad = 0; send_pick = 0;
    shown_head[0] = shown_big[0] = shown_sub[0] = shown_prompt[0] = shown_hint[0] = shown_notice[0] = 0;
    memset(shown_lane, 0xff, sizeof shown_lane); shown_pads = ~0U;
    last_step = 0; bar_boundary = 0;
}

void stage3_key(struct lctx *x, unsigned note, unsigned velocity)
{
    if (note > 127) return;
    int k = (int)note - (LEARN_FIRST_NOTE + 12 * x->octave);
    if (velocity && (x->c.buttons & LB(LB_GLO)) && k >= 0 && k < (int)LEARN_KEYS) {
        /* Selection keys while GLO is held (the FM-1's pads). */
        if (k < 3) select_source(x, (unsigned)k);
        else if (k < 6) { shadow_sel = (uint8_t)(k - 3); learn_notice(shadow[shadow_sel].used ? "SHADOW SELECTED" : "EMPTY SLOT: SOURCE THEN RULE", 900); x->redraw = 1; }
        else if (k >= 8 && k < 16) {
            if (arp_entry && arp_len < 8) { arp_pattern[arp_len++] = (uint8_t)(k - 8); arp_entry_at = x->now; render_all(); x->redraw = 1; }
            else pick_rule(x, (unsigned)(k - 8));
        }
        else if (k >= 16 && k < 20) select_tape(x, (unsigned)(k - 16));
        return;
    }
    unsigned ch = learn_track_channel(tape_track(tape_sel));
    if (velocity) {
        /* A TEXT lesson step continues on any key. */
        const struct lesson_step *ls = lesson_step();
        if (ls && ls->type == DR_TEXT) lesson_step_done(1, 100);
        sounding[note >> 5] |= 1U << (note & 31);
        lmidi_note(ch, note, velocity);
    } else if (sounding[note >> 5] & (1U << (note & 31))) {
        sounding[note >> 5] &= ~(1U << (note & 31));
        lmidi_note(ch, note, 0);
    }
    x->redraw = 1;
}

void stage3_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    if (pad >= 16) return;
    if (velocity == 0xFFFF) {
        /* Aftertouch on a track pad: momentary solo. */
        if (pad < 6) {
            unsigned track = pad < 3 ? source_track[pad] : LT_SHADOW_A + pad - 3;
            if (pad >= 3 && !shadow[pad - 3].used) return;
            if (pressure >= 40) solo_set(track, 1); else if (pressure < 20) solo_set(track, 0);
            x->redraw = 1;
        }
        return;
    }
    if (velocity) {
        pad_down[pad] = 1; pad_hold_since[pad] = x->now; pad_hint_done[pad] = 0;
        if (pad < 3) select_source(x, pad);
        else if (pad < 6) { shadow_sel = (uint8_t)(pad - 3); learn_notice(shadow[shadow_sel].used ? "SHADOW SELECTED: K1-K4 EDIT" : "EMPTY SLOT: SOURCE THEN RULE", 900); }
        else if (pad == 6) select_tape(x, (tape_sel + 1) % TAPE_TRACKS);
        else if (pad == 7) { export_on = 1; export_track = export_frame = 0; learn_notice("EXPORTING TAKES...", 1000); }
        else {
            unsigned rule = pad - 8;
            if (arp_entry && arp_len < 8) { arp_pattern[arp_len++] = (uint8_t)rule; arp_entry_at = x->now; render_all(); }
            else pick_rule(x, rule);
        }
    } else {
        pad_down[pad] = 0;
        if (pad < 6) solo_set(pad < 3 ? source_track[pad] : LT_SHADOW_A + pad - 3, 0);
    }
    x->redraw = 1;
}

void stage3_encoder(struct lctx *x, unsigned id, int delta)
{
    struct shadow *sh = &shadow[shadow_sel];
    unsigned st = LT_SHADOW_A + shadow_sel;
    int v;
#ifdef LEARN_BOARD_FM1
    /* The FM-1 has no obedience fader: K2 is obedience, GLO+K2 density. */
    if (id == 1 && !(x->c.buttons & LB(LB_GLO))) {
        v = (int)sh->obedience + delta * 8; sh->obedience = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        if (sh->used) { render_shadow(shadow_sel); seen_revision = groove_edit_revision(); shadow_send(shadow_sel); }
        learn_notice(sh->obedience >= 120 ? "OBEDIENCE: STRICT" : sh->obedience >= 60 ? "OBEDIENCE: HUMAN" : "OBEDIENCE: LOOSE", 800);
        x->redraw = 1; return;
    }
#endif
    switch (id) {
    case 0: v = sh->octave + (delta > 0 ? 1 : -1); sh->octave = (int8_t)(v < -2 ? -2 : v > 2 ? 2 : v); if (sh->used) { render_shadow(shadow_sel); seen_revision = groove_edit_revision(); } break;
    case 1: v = (int)sh->density + (delta > 0 ? 1 : -1); sh->density = (uint8_t)(v < 1 ? 1 : v > 7 ? 7 : v); if (sh->used) { render_shadow(shadow_sel); seen_revision = groove_edit_revision(); } break;
    case 2: v = (int)sh->velocity + delta * 5; sh->velocity = (uint8_t)(v < 20 ? 20 : v > 150 ? 150 : v); if (sh->used) { render_shadow(shadow_sel); seen_revision = groove_edit_revision(); } break;
    case 3: if (sh->used && sh->rule != RULE_GHOST) { v = (int)(sh->bank * 8 + sh->patch) + (delta > 0 ? 1 : -1); v = (v + BANK_COUNT * 8) % (BANK_COUNT * 8); sh->bank = (uint8_t)(v / 8); sh->patch = (uint8_t)(v % 8); learn_bank_select(st, sh->bank, sh->patch); learn_notice(learn_bank_name(sh->bank), 700); } break;
    case 4: send_pick = (uint8_t)(delta > 0 ? 1 : 0); learn_notice(send_pick ? "SEND: DELAY" : "SEND: REVERB", 700); break;
    case 5: groove_sends_edit(tape_track(tape_sel), send_pick, delta); break;
    case 6: { v = (int)x->slow_percent + delta * 5; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); uint8_t p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); } break;
    default: break;
    }
    x->redraw = 1;
}

void stage3_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (id >= 4) return;
    unsigned bank = 0;
#ifdef LEARN_BOARD_SMK37
    extern unsigned smk37_fader_bank(void);
    bank = smk37_fader_bank();
#endif
    if (bank == 0) {
        /* Mixer: Drums, Bass, Chords, Melody. */
        static const uint8_t tracks[4] = {LT_DRUMS, LT_BASS, LT_CHORDS, LT_MELODY};
        struct groove_view v; groove_get_view(tracks[id], 0, &v);
        int delta = (int)(value & 127) - (int)v.volume;
        if (delta) groove_control_edit(tracks[id], 0, delta, 0);
    } else if (id < 3) {
        shadow[id].obedience = (uint8_t)(value & 127);
        if (shadow[id].used) { render_shadow(id); seen_revision = groove_edit_revision(); shadow_send(id); }
    } else {
        uint8_t s[2]; groove_sends_get(tape_track(tape_sel), s);
        int delta = (int)(value * 15U / 127U) - (int)s[send_pick];
        if (delta) groove_sends_edit(tape_track(tape_sel), send_pick, delta);
    }
    x->redraw = 1;
}

void stage3_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_SHADOW:
        if (n >= 4 && p[0] < SHADOWS && p[1] < SRC_COUNT && p[2] < RULE_COUNT) shadow_create(x, p[0], p[1], p[2], p[3]);
        break;
    case LCMD_SOLO:
        if (n >= 2 && p[0] < GROOVE_TRACKS) { solo_set(p[0], p[1]); x->redraw = 1; }
        break;
    case LCMD_TRANSPORT:
        if (n >= 1) {
            from_partner = 1;
            if (p[0] == 0) stop_all(x);
            else if (p[0] == 1) { if (!song_playing()) song_play(1); }
            else if (p[0] == 2) { if (!groove_edit_recording()) rec_begin(x, 1); }
            else if (p[0] == 3) take_undo(x);
            from_partner = 0; x->redraw = 1;
        }
        break;
    case LCMD_PATH:
        if (n >= 2) { follow_pad = p[0] == 0 ? (uint8_t)(p[1] + 1) : 0; x->redraw = 1; }
        break;
    default: break;
    }
}

void stage3_control(struct lctx *x)
{
    unsigned b = x->btn_down, glo = x->c.buttons & LB(LB_GLO), home = x->c.buttons & LB(LB_HOME);
    key_root = x->root; key_scale = x->scale;
    /* Bar boundary detection from the chord track's play head. */
    unsigned step = song_step(); bar_boundary = 0;
    if (song_playing() && (step < last_step || step / 16 != last_step / 16)) bar_boundary = 1;
    last_step = step;

    if (b & LB(LB_REC)) {
        if (home) { export_on = 1; export_track = export_frame = 0; learn_notice("EXPORTING TAKES...", 1000); rec_consumed = 1; }
        else if (groove_edit_recording() || rec_full) { rec_end(x); rec_consumed = 1; }
        else { rec_consumed = 0; if (song_playing()) { punch_armed = 1; learn_notice("PUNCH AT THE NEXT BAR", 1200); } }
    }
    if (b & LB(LB_PLAY)) {
        if (x->c.buttons & LB(LB_REC) && !groove_edit_recording()) { punch_armed = 0; rec_consumed = 1; rec_begin(x, 1); learn_notice("TAPING...", 1000); }
        else if (groove_edit_recording() || rec_full) rec_end(x);
        else if (song_playing()) { song_play(0); uint8_t p = 0; lsyx_send(LCMD_TRANSPORT, &p, 1); }
        else { song_play(1); uint8_t p = 1; lsyx_send(LCMD_TRANSPORT, &p, 1); }
        x->redraw = 1;
    }
    if (x->btn_up & LB(LB_REC)) {
        if (rec_consumed) rec_consumed = 0;
        else if (punch_on) { punch_armed = 2; learn_notice("PUNCH ENDS AT THE BAR", 1000); }
        else if (punch_armed) { punch_armed = 0; goto ab; }
        else {
        ab:
            if (take_count[tape_sel] >= 2) { ab_pending = 1; ab_side ^= 1; learn_notice(ab_side ? "A/B: PREVIOUS TAKE" : "A/B: CURRENT TAKE", 900); uint8_t p = ab_side; lsyx_send(LCMD_REPLAY, &p, 1); }
            else learn_notice("TAPE TWO TAKES TO COMPARE", 900);
        }
        x->redraw = 1;
    }
    if (b & LB(LB_HOME)) stop_all(x);
    if (b & LB(LB_SAVE)) { if (glo) take_redo(x); else { take_undo(x); uint8_t p = 3; lsyx_send(LCMD_TRANSPORT, &p, 1); } }
    if (b & LB(LB_SCL)) select_tape(x, (tape_sel + 1) % TAPE_TRACKS);
    if (b & LB(LB_ENV)) { hints_on = !hints_on; learn_notice(hints_on ? "HINTS ON" : "HINTS OFF", 700); x->redraw = 1; }
    if (b & LB(LB_FX)) shadow_remove(x, shadow_sel);
    if (b & LB(LB_EDIT)) { shadow_sel = (uint8_t)((shadow_sel + 1) % SHADOWS); x->redraw = 1; }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); learn_notice("LESSON STOPPED", 800); }
        else { for (unsigned i = 0; i < lesson_count; ++i) if (lessons[i].stage == 3) { lesson_start(i); break; } if (!lesson_running()) learn_notice("NO STAGE 3 LESSON LOADED", 900); }
        x->redraw = 1;
    }
    if (b & LB(LB_LFO)) { arp_entry = 0; if (shadow[shadow_sel].used) demo_start(x, shadow[shadow_sel].rule); }

    /* Punch state machine on bar boundaries. */
    if (bar_boundary) {
        unsigned track = tape_track(tape_sel);
        if (punch_armed == 1 && !punch_on) {
            groove_sequence_read(track, 0, scratch, SONG_STEPS);
            punch_on = 1; punch_armed = 0; punch_bars = (uint8_t)(1U << (step / 16));
            if (tape_sel) groove_edit_record(1, track, step); else song_record(1);
            rec_slow = x->slow_percent < 100; learn_notice("PUNCH IN", 600); x->redraw = 1;
        } else if (punch_on && punch_armed == 2) { rec_end(x); learn_notice("PUNCH OUT", 600); }
        else if (punch_on) punch_bars |= (uint8_t)(1U << (step / 16));
        if (ab_pending && take_count[tape_sel] >= 2) {
            unsigned cur = take_cur[tape_sel] == 255 ? take_count[tape_sel] - 1 : take_cur[tape_sel];
            unsigned base = ab_side ? (cur ? cur - 1 : cur + 1) : cur;
            groove_sequence_replace(tape_track(tape_sel), 0, takes[tape_sel][base], SONG_STEPS);
            ab_pending = 0; x->redraw = 1;
        }
        if (punch_on) x->redraw = 1;
    }
    if (ab_pending && !song_playing()) {
        /* Stopped: swap at once. */
        unsigned cur = take_cur[tape_sel] == 255 ? take_count[tape_sel] - 1 : take_cur[tape_sel];
        unsigned base = ab_side ? (cur ? cur - 1 : cur + 1) : cur;
        if (base < take_count[tape_sel]) groove_sequence_replace(tape_track(tape_sel), 0, takes[tape_sel][base], SONG_STEPS);
        ab_pending = 0; x->redraw = 1;
    }
    if (punch_armed && !song_playing()) punch_armed = 0;

    /* Why-hints: a rule pad held two seconds. */
    for (unsigned p = 8; p < 16; ++p)
        if (pad_down[p] && !pad_hint_done[p] && x->now - pad_hold_since[p] >= 2000U) { pad_hint_done[p] = 1; demo_start(x, p - 8); }
    for (unsigned p = 3; p < 6; ++p)
        if (pad_down[p] && !pad_hint_done[p] && x->now - pad_hold_since[p] >= 2000U) { pad_hint_done[p] = 1; shadow_remove(x, p - 3); }
    demo_poll(x);
    if (source_armed && x->now - source_armed_at >= 5000U) { source_armed = 0; follow_pad = 0; x->redraw = 1; }
    if (arp_entry && x->now - arp_entry_at >= 4000U) { arp_entry = 0; x->redraw = 1; }

    /* Shadows follow their sources: re-render after the edit revision settles. */
    uint32_t rev = groove_edit_revision();
    if (rev != seen_revision) { if (!render_pending) revision_changed_at = x->now; render_pending = 1; seen_revision = rev; }
    if (render_pending && !groove_edit_recording() && x->now - revision_changed_at >= 300U) {
        unsigned any = 0; for (unsigned i = 0; i < SHADOWS; ++i) any |= shadow[i].used;
        if (any) render_all(); else render_pending = 0;
        x->redraw = 1;
    }
    export_poll();
    if (hint_line[0] && (int32_t)(x->now - hint_until) >= 0) { hint_line[0] = 0; x->redraw = 1; }
    /* External notes from the partner box are shown; the engine records them itself. */
    uint8_t st, d1, d2; unsigned changed = 0;
    while (lmidi_pull_external(&st, &d1, &d2)) changed = 1;
    if (changed) x->redraw = 1;
}

static uint32_t source_colour(unsigned source) { return source == SRC_CHORDS ? P_CYAN : source == SRC_MELODY ? P_GREEN : P_RED; }

uint32_t stage3_pad_colour(unsigned pad)
{
    if (pad >= 16) return P_OFF;
    unsigned blink = (fm1_millis() / 125U) & 1U;
    if (pad < 3) {
        uint32_t c = source_colour(pad);
        /* A held rule pad flashes its source. */
        for (unsigned p = 8; p < 16; ++p) if (pad_down[p] && rule_source[p - 8] == pad) return blink ? P_FULL(c) : P_DIM(c);
        if (source_armed == pad + 1) return blink ? P_FULL(c) : c;
        return (solo_held >> source_track[pad]) & 1U ? P_FULL(c) : c;
    }
    if (pad < 6) {
        unsigned id = pad - 3;
        if (!shadow[id].used) return id == shadow_sel ? P_DIM(P_WHITE) : P_OFF;
        uint32_t c = source_colour(shadow[id].source);
        if ((solo_held >> (LT_SHADOW_A + id)) & 1U) return P_FULL(c);
        return id == shadow_sel ? c : P_DIM(c);
    }
    if (pad == 6) { uint32_t c = tape_sel == 0 ? P_GREEN : source_colour(shadow[tape_sel - 1].source); return groove_edit_recording() ? (blink ? P_RED : P_DIM(P_RED)) : P_DIM(c); }
    if (pad == 7) return export_on ? P_WHITE : P_DIM(P_WHITE);
    unsigned rule = pad - 8; uint32_t c = source_colour(rule_source[rule]);
    if (pad_down[pad]) return P_FULL(c);
    if (follow_pad == pad + 1) return blink ? P_FULL(c) : c;
    if (arp_entry) return rule < arp_len ? P_ORANGE : P_DIM(P_ORANGE);
    if (source_armed && source_armed - 1 == rule_source[rule]) return c;
    return P_DIM(c);
}

/* ---- drawing ---- */
static void draw_if_changed(char *shown, const char *text, int x, int y, unsigned scale, uint16_t fg, int clear_w, unsigned force)
{
    if (!force && !strcmp(shown, text)) return;
    draw_fill(x, y, clear_w, (int)(7 * scale), C_BG);
    draw_text(x, y, text, scale, fg, C_BG);
    strncpy(shown, text, 31); shown[31] = 0;
}
static uint16_t source_rgb(unsigned source) { return source == SRC_CHORDS ? C_CYAN : source == SRC_MELODY ? C_GREEN : C_RED; }

void stage3_draw(struct lctx *x)
{
    unsigned force = x->redraw, steps = loop_steps();
    if (force) draw_clear();
    /* Header: page, bars, tempo, slow. */
    {
        char line[32] = ""; cat(line, learn_is_fm1() ? "TAPE " : "SHADOW ", 8);
        char n[4]; n[0] = (char)('0' + song.bars); n[1] = 'B'; n[2] = 'A'; n[3] = 0; cat(line, n, 3); cat(line, "R ", 2);
        char t[5]; t[0] = (char)('0' + (song.tempo / 100) % 10); num2(t + 1, song.tempo); t[3] = 0; cat(line, t[0] == '0' ? t + 1 : t, 3); cat(line, " BPM", 4);
        if (x->slow_percent < 100) { char s[4]; s[0] = ' '; s[1] = 'S'; s[2] = 0; cat(line, s, 2); char v[3]; num2(v, x->slow_percent); v[2] = 0; cat(line, v, 2); }
        draw_if_changed(shown_head, line, 4, 2, 1, C_DIM, 200, force);
        draw_fill(228, 2, 8, 8, lsyx_partner_present() ? C_GREEN : C_PANEL);
    }
    /* Big line: the selected shadow, or what to do next. */
    {
        char big[16] = "", sub[32] = "";
        const struct shadow *sh = &shadow[shadow_sel];
        if (source_armed) { cat(big, source_name[source_armed - 1], 12); cat(sub, "PICK A RULE", 20); }
        else if (sh->used) {
            big[0] = (char)('A' + shadow_sel); big[1] = ' '; big[2] = 0; cat(big, rule_name[sh->rule], 10);
            cat(sub, source_name[sh->source], 8); cat(sub, " OCT", 4); char o[3]; o[0] = sh->octave < 0 ? '-' : '+'; o[1] = (char)('0' + (sh->octave < 0 ? -sh->octave : sh->octave)); o[2] = 0; cat(sub, o, 2);
            cat(sub, " D", 2); o[0] = (char)('0' + sh->density); o[1] = 0; cat(sub, o, 1);
            cat(sub, sh->obedience >= 120 ? " STRICT" : sh->obedience >= 60 ? " HUMAN" : " LOOSE", 8);
        } else { cat(big, "SHADOW", 12); cat(sub, learn_is_fm1() ? "GLO+KEY: SOURCE, RULE" : "PAD 1-3 THEN PAD 9-16", 30); }
        draw_if_changed(shown_big, big, 4, 18, 4, C_TEXT, 232, force);
        draw_if_changed(shown_sub, sub, 4, 50, 2, sh->used && !source_armed ? source_rgb(sh->source) : C_CYAN, 232, force);
    }
    /* Four tape lanes: name, density bar with play head and punch, take, level. */
    unsigned head = song_playing() ? song_step() % steps : 0xff;
    for (unsigned i = 0; i < TAPE_TRACKS; ++i) {
        unsigned track = tape_track(i);
        struct groove_view v; groove_get_view(track, 0, &v);
        unsigned active = 0; for (unsigned s = 0; s < steps; ++s) if ((v.active_steps >> s) & 1U) ++active;
        unsigned used = i == 0 || shadow[i - 1].used;
        unsigned punch = (punch_on && tape_sel == i) ? punch_bars : 0, rec = (groove_edit_recording() && tape_sel == i);
        uint32_t sig = active | (head << 8) | ((v.level / 8U) << 16) | ((take_cur[i] == 255 ? 0U : take_cur[i] + 1U) << 20)
                     | ((unsigned)(tape_sel == i) << 23) | (punch << 24) | (rec << 28) | (used << 29) | (((solo_held >> track) & 1U) << 30);
        if (!force && sig == shown_lane[i]) continue;
        shown_lane[i] = sig;
        int y = 72 + (int)i * 22;
        draw_fill(0, y, 240, 21, C_BG);
        uint16_t c = i == 0 ? C_GREEN : used ? source_rgb(shadow[i - 1].source) : C_DIM;
        const char *name = i == 0 ? "MEL" : i == 1 ? "SH A" : i == 2 ? "SH B" : "SH C";
        draw_text(4, y + 2, name, 1, tape_sel == i ? C_TEXT : c, C_BG);
        if (tape_sel == i) draw_fill(0, y + 2, 2, 7, rec ? C_RED : C_TEXT);
        if (used) {
            draw_frame(36, y, 130, 12, C_PANEL);
            unsigned w = active * 128U / steps; if (w) draw_fill(37, y + 1, (int)w, 10, C_DIM);
            for (unsigned bar = 0; bar < steps / 16; ++bar) if ((punch >> bar) & 1U) draw_fill(37 + (int)bar * 128 / (int)(steps / 16), y + 13, 128 / (int)(steps / 16), 2, C_RED);
            if (head != 0xff) draw_fill(37 + (int)(head * 128U / steps), y + 1, 2, 10, rec ? C_RED : C_ORANGE);
        } else draw_text(36, y + 2, "EMPTY", 1, C_PANEL, C_BG);
        char tk[3] = "T-"; if (take_cur[i] != 255) tk[1] = (char)('1' + take_cur[i]);
        draw_text(172, y + 2, tk, 1, take_cur[i] != 255 ? C_AMBER : C_PANEL, C_BG);
        unsigned lvl = v.level * 36U / 127U; draw_frame(196, y, 40, 12, C_PANEL);
        if (lvl) draw_fill(198, y + 2, (int)lvl, 8, (solo_held >> track) & 1U ? C_YELLOW : C_GREEN);
    }
    /* Prompt. */
    {
        char line[32] = "";
        const struct lesson_step *ls = lesson_step();
        if (groove_edit_recording()) cat(line, punch_on ? "PUNCHING: RELEASE REC TO END" : "TAPING: PLAY TO FINISH", 30);
        else if (punch_armed) cat(line, "PUNCH WAITS FOR THE BAR", 30);
        else if (arp_entry) cat(line, "TAP PATTERN ON PADS 9-16", 30);
        else if (demo_phase) cat(line, demo_phase == 1 ? "LISTEN: SOURCE ALONE" : "LISTEN: SHADOW ALONE", 30);
        else if (ls) cat(line, ls->prompt ? ls->prompt : "", 30);
        else cat(line, learn_is_fm1() ? "REC+PLAY TAPE  SAVE UNDO" : "REC+PLAY TAPE  PAD7 TRACK", 30);
        draw_if_changed(shown_prompt, line, 4, 164, 1, groove_edit_recording() ? C_RED : C_AMBER, 232, force);
    }
    if (lesson_running()) {
        const struct lesson *l = &lessons[lesson_running() - 1];
        char line[32] = ""; cat(line, l->title, 20); line[strlen(line)] = 0;
        char pr[5] = " "; pr[1] = (char)('0' + lesson_progress() + 1); pr[2] = '/'; pr[3] = (char)('0' + l->step_count); pr[4] = 0; cat(line, pr, 4);
        draw_text(4, 176, line, 1, C_PURPLE, C_BG);
    } else if (force) draw_fill(4, 176, 232, 8, C_BG);
    /* Takes row: one dot per take of the selected track, the loaded one bright. */
    {
        draw_fill(4, 190, 232, 10, C_BG);
        for (unsigned i = 0; i < take_count[tape_sel]; ++i) draw_fill(4 + (int)i * 14, 190, 10, 6, i == take_cur[tape_sel] ? C_GREEN : C_DIM);
        uint8_t s[2]; groove_sends_get(tape_track(tape_sel), s);
        char line[16] = ""; cat(line, send_pick ? "DLY " : "REV ", 4); char v[3]; num2(v, s[send_pick]); v[2] = 0; cat(line, v, 2);
        draw_text(170, 190, line, 1, C_DIM, C_BG);
    }
    draw_if_changed(shown_hint, hint_line, 4, 212, 1, C_YELLOW, 232, force);
    const char *n = learn_notice_text();
    draw_if_changed(shown_notice, n ? n : "", 4, 230, 1, C_TEXT, 232, force);
}
