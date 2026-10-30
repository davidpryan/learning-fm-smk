/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 1: Lantern (SMK-37) / Ember (FM-1). Hear what you play.
 *
 * Both boards run the same page. The SMK-37 is the one with pads, so it
 * carries the chord palette; the FM-1 carries the staff hint and is the
 * default tutor, but every drill also runs on a single box.
 *
 * Controls (logical, both boards):
 *   keys        play; outside-the-scale keys are muted or nudged
 *   SCL         cycle scale type      GLO/SHIFT + key   set the root
 *   OCT -/+     octave                ENV               why-hints on/off
 *   FX          staff hint on/off     EDIT / SAVE       sound bank / patch
 *   PLAY        start or stop a drill REC               replay ref / mine
 *   ARP         next lesson / stop    HOME              stop everything
 *   K1 bank  K2 patch  K3 reverb  K4 delay  K5 slow %  K6 lesson  K7 drill kind
 *   pads (SMK)  1-7 triads of degree 1-7, 8 sus4; 9-15 sevenths, 16 sus2
 *   FM-1 keys   SCL twice = CHORD KEYS: a white key plays its degree's triad */
#include "learn.h"
#include <string.h>

enum { HELD_MAX = 10 };
static uint8_t held[HELD_MAX]; static unsigned held_count;
static uint8_t chord_mode;                 /* FM-1: keys play degree chords */
static uint8_t hints_on = 1, staff_on = 1;
static uint8_t bank_sel, patch_sel, reverb_send = 4, delay_send = 2;
static uint8_t last_roman[4]; static uint8_t last_roman_count; /* degree<<1 | seventh */
static uint8_t last_minor;
static unsigned root_set_armed;
static uint8_t pad_down[16];
static uint32_t hint_until; static char hint_line[40];
static uint32_t pad_hold_since[16];
static unsigned lesson_pick;

/* ---- drill ---- */
enum { DS_IDLE, DS_PLAYING_REF, DS_WAIT_ANSWER, DS_RESULT };
struct event { uint8_t note, vel; uint16_t at; }; /* at in ms/4 from start */
enum { EV_MAX = 24 };
static struct {
    unsigned state, kind;            /* kind: DR_* */
    unsigned index, total, hits, misses;
    uint8_t target[4], target_count; /* expected notes (chord) or one note */
    unsigned target_degree, target_seventh;
    uint32_t started, due;
    struct event ref[EV_MAX]; unsigned ref_count;
    struct event mine[EV_MAX]; unsigned mine_count;
    uint8_t answer[8]; unsigned answer_count; uint32_t answer_first;
    unsigned prog[8], prog_count, prog_pos;
    unsigned tempo;
} drill;
static unsigned replay_which, replay_on, replay_pos; static uint32_t replay_started;
static unsigned follow_key;              /* 1 + key index to light, 0 none */
static unsigned follow_pad;              /* 1 + pad */
static uint8_t drill_kind_pick = DR_ECHO;

/* ---- drawn-state cache ---- */
static char shown_big[16], shown_sub[32], shown_prog[32], shown_prompt[32], shown_hint[40], shown_head[32], shown_notice[32];
static uint64_t shown_lit, shown_bright; static unsigned shown_staff_note = 999; static unsigned shown_pads = ~0U;

static unsigned slow_ms(unsigned ms, const struct lctx *x) { return ms * 100U / (x->slow_percent ? x->slow_percent : 100); }

static void held_add(unsigned note) { for (unsigned i = 0; i < held_count; ++i) if (held[i] == note) return; if (held_count < HELD_MAX) held[held_count++] = (uint8_t)note; }
static void held_remove(unsigned note) { for (unsigned i = 0; i < held_count; ++i) if (held[i] == note) { held[i] = held[held_count - 1]; --held_count; return; } }

static void roman_push(unsigned degree, unsigned seventh)
{
    for (unsigned i = 3; i > 0; --i) last_roman[i] = last_roman[i - 1];
    last_roman[0] = (uint8_t)((degree << 1) | (seventh & 1));
    if (last_roman_count < 4) ++last_roman_count;
}

static unsigned chord_octave(const struct lctx *x) { return (unsigned)(48 + 12 * x->octave); }

static void play_degree(struct lctx *x, unsigned degree, unsigned seventh, unsigned slot, unsigned velocity)
{
    uint8_t notes[4];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, seventh, chord_octave(x), notes);
    if (!n) return;
    lmidi_chord_on(learn_track_channel(LT_CHORDS), notes, n, velocity, slot);
    roman_push(degree, seventh); last_minor = (uint8_t)x->minor;
    if (hints_on) { strncpy(hint_line, theory_why_hint(x->minor, degree), sizeof hint_line - 1); hint_until = x->now + 2500U; }
    uint8_t p[3] = {(uint8_t)degree, (uint8_t)seventh, (uint8_t)x->minor};
    lsyx_send(LCMD_HINT, p, 3);
    x->redraw = 1;
}

static void sus_chord(struct lctx *x, unsigned sus4, unsigned slot, unsigned velocity)
{
    unsigned degree = last_roman_count ? last_roman[0] >> 1 : 1;
    uint8_t notes[4];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, 0, chord_octave(x), notes);
    if (n < 3) return;
    notes[1] = (uint8_t)(notes[0] + (sus4 ? 5 : 2));
    lmidi_chord_on(learn_track_channel(LT_CHORDS), notes, 3, velocity, slot);
    x->redraw = 1;
}

/* ---- drill engine ---- */
static void drill_send_state(void)
{
    uint8_t p[4] = {(uint8_t)drill.state, (uint8_t)drill.kind, (uint8_t)drill.index, (uint8_t)(drill.total ? drill.hits * 100U / (drill.hits + drill.misses + (drill.hits + drill.misses == 0)) : 0)};
    lsyx_send(LCMD_DRILL, p, 4);
}
static unsigned lcg(void) { static uint32_t s = 12345; s = s * 1664525U + 1013904223U; return s >> 8; }

static void drill_pick_target(struct lctx *x)
{
    uint8_t notes[4];
    const struct lesson_step *ls = lesson_step();
    unsigned minor = x->minor;
    if (drill.kind == DR_ECHO || drill.kind == DR_SCALE) {
        unsigned base = (unsigned)(LEARN_FIRST_NOTE + 12 * x->octave);
        unsigned note;
        if (drill.kind == DR_SCALE) {
            /* Ascending scale: next scale note after the previous target. */
            unsigned start = drill.target_count ? drill.target[0] + 1 : base + ((x->root + 12 - base % 12) % 12);
            note = start;
            while (!theory_in_scale(x->root, x->scale, note)) ++note;
            if (note >= base + LEARN_KEYS) { note = base + ((x->root + 12 - base % 12) % 12); }
        } else {
            unsigned span = LEARN_KEYS > 24 ? 24 : LEARN_KEYS;
            unsigned pool[32], pool_n = 0;
            for (unsigned k = 0; k < span; ++k) if (theory_in_scale(x->root, x->scale, base + k)) pool[pool_n++] = base + k;
            if (ls && ls->data[0]) { /* lesson restricts to listed degrees */
                unsigned filtered[32], fn = 0;
                for (unsigned i = 0; i < pool_n; ++i) { int d = theory_degree(x->root, x->scale, pool[i]); for (unsigned j = 0; j < 8 && ls->data[j]; ++j) if ((int)ls->data[j] == d) { filtered[fn++] = pool[i]; break; } }
                if (fn) { memcpy(pool, filtered, fn * sizeof pool[0]); pool_n = fn; }
            }
            note = pool_n ? pool[lcg() % pool_n] : base;
        }
        drill.target[0] = (uint8_t)note; drill.target_count = 1;
        drill.ref_count = 1; drill.ref[0].note = (uint8_t)note; drill.ref[0].vel = 100; drill.ref[0].at = 0;
        drill.target_degree = 0;
    } else if (drill.kind == DR_CHORD || drill.kind == DR_PROGRESSION) {
        unsigned degree;
        if (drill.kind == DR_PROGRESSION) degree = drill.prog[drill.prog_pos % (drill.prog_count ? drill.prog_count : 1)];
        else if (ls && ls->data[0]) { unsigned n = 0; while (n < 8 && ls->data[n]) ++n; degree = ls->data[lcg() % n]; }
        else degree = 1 + lcg() % 7;
        unsigned seventh = ls ? ls->seventh : 0;
        drill.target_count = (uint8_t)theory_degree_chord(x->root, minor, degree, seventh, chord_octave(x), notes);
        memcpy(drill.target, notes, 4);
        drill.target_degree = degree; drill.target_seventh = seventh;
        drill.ref_count = drill.target_count;
        for (unsigned i = 0; i < drill.target_count; ++i) { drill.ref[i].note = notes[i]; drill.ref[i].vel = 100; drill.ref[i].at = 0; }
    }
    drill.answer_count = 0; drill.mine_count = 0;
}

static void drill_play_reference(struct lctx *x)
{
    drill.state = DS_PLAYING_REF; drill.started = x->now;
    if (drill.kind == DR_ECHO || drill.kind == DR_SCALE) lmidi_note(learn_track_channel(LT_KEYS), drill.target[0], 100);
    else lmidi_chord_on(learn_track_channel(LT_CHORDS), drill.target, drill.target_count, 100, 18);
    drill.due = x->now + slow_ms(600, x);
    /* Follow-me: light what to press next. */
    if (drill.kind == DR_ECHO || drill.kind == DR_SCALE) {
        int k = (int)drill.target[0] - (LEARN_FIRST_NOTE + 12 * x->octave);
        follow_key = (k >= 0 && k < (int)LEARN_KEYS) ? (unsigned)k + 1 : 0; follow_pad = 0;
        uint8_t p[2] = {1, (uint8_t)drill.target[0]}; lsyx_send(LCMD_PATH, p, 2);
    } else {
        follow_pad = drill.target_degree + (drill.target_seventh ? 8 : 0); follow_key = 0;
        uint8_t p[2] = {0, (uint8_t)(follow_pad - 1)}; lsyx_send(LCMD_PATH, p, 2);
    }
    drill_send_state(); x->redraw = 1;
}

static void drill_start(struct lctx *x, unsigned kind, unsigned total)
{
    memset(&drill, 0, sizeof drill);
    drill.kind = kind; drill.total = total ? total : 10; drill.tempo = 90;
    if (kind == DR_PROGRESSION) {
        const struct lesson_step *ls = lesson_step();
        drill.prog_count = 0;
        if (ls) for (unsigned i = 0; i < 8 && ls->data[i]; ++i) drill.prog[drill.prog_count++] = ls->data[i];
        if (!drill.prog_count) { drill.prog[0] = 1; drill.prog[1] = 5; drill.prog[2] = 6; drill.prog[3] = 4; drill.prog_count = 4; }
        drill.total = drill.prog_count * (total ? total : 2);
    }
    drill_pick_target(x);
    drill_play_reference(x);
    learn_notice(kind == DR_ECHO ? "ECHO: COPY THE NOTE" : kind == DR_SCALE ? "SCALE: PLAY IT UP" : kind == DR_CHORD ? "FIND THE CHORD" : "PLAY THE PROGRESSION", 1200);
}

static void drill_stop(struct lctx *x)
{
    if (drill.state == DS_PLAYING_REF) { lmidi_chord_off(18); if (drill.kind == DR_ECHO || drill.kind == DR_SCALE) lmidi_note(learn_track_channel(LT_KEYS), drill.target[0], 0); }
    drill.state = DS_IDLE; follow_key = follow_pad = 0;
    uint8_t p[2] = {2, 0}; lsyx_send(LCMD_PATH, p, 2);
    drill_send_state(); x->redraw = 1;
}

static void drill_score_answer(struct lctx *x)
{
    unsigned hit = 0;
    if (drill.kind == DR_ECHO || drill.kind == DR_SCALE) {
        hit = drill.answer_count && drill.answer[0] % 12 == drill.target[0] % 12;
        if (hit && drill.answer[0] != drill.target[0]) learn_notice("RIGHT NOTE, OTHER OCTAVE", 900);
    } else {
        unsigned r1, r2;
        unsigned k1 = theory_identify(drill.answer, drill.answer_count, &r1);
        unsigned k2 = theory_identify(drill.target, drill.target_count, &r2);
        hit = k1 != CHORD_NONE && k1 == k2 && r1 == r2;
        if (!hit && drill.answer_count && drill.answer[0] % 12 == drill.target[0] % 12) learn_notice("RIGHT ROOT, WRONG CHORD", 900);
    }
    if (hit) ++drill.hits; else ++drill.misses;
    ++drill.index;
    if (drill.kind == DR_PROGRESSION) ++drill.prog_pos;
    uint8_t p[2] = {(uint8_t)hit, (uint8_t)(drill.hits * 100U / (drill.hits + drill.misses))}; lsyx_send(LCMD_SCORE, p, 2);
    learn_notice(hit ? "HIT" : "MISS", 500);
    drill.state = DS_RESULT; drill.due = x->now + slow_ms(500, x);
    x->redraw = 1;
}

static void drill_poll(struct lctx *x)
{
    if (drill.state == DS_PLAYING_REF && (int32_t)(x->now - drill.due) >= 0) {
        lmidi_chord_off(18);
        if (drill.kind == DR_ECHO || drill.kind == DR_SCALE) lmidi_note(learn_track_channel(LT_KEYS), drill.target[0], 0);
        drill.state = DS_WAIT_ANSWER; drill.due = x->now + slow_ms(6000, x);
        drill_send_state(); x->redraw = 1;
    } else if (drill.state == DS_WAIT_ANSWER) {
        /* A chord answer is complete 350 ms after its first note; a single note immediately on release. */
        if (drill.answer_count && ((drill.kind == DR_ECHO || drill.kind == DR_SCALE) || x->now - drill.answer_first >= slow_ms(350, x)))
            drill_score_answer(x);
        else if ((int32_t)(x->now - drill.due) >= 0) { drill.answer_count = 0; ++drill.misses; ++drill.index; learn_notice("TOO SLOW - LISTEN AGAIN", 900); drill.state = DS_RESULT; drill.due = x->now + 400; }
    } else if (drill.state == DS_RESULT && (int32_t)(x->now - drill.due) >= 0) {
        if (drill.index >= drill.total) {
            unsigned pct = drill.hits * 100U / (drill.hits + drill.misses);
            unsigned logk = drill.kind == DR_ECHO || drill.kind == DR_SCALE ? LOGK_ECHO : drill.kind == DR_CHORD ? LOGK_CHORD : LOGK_PROGRESSION;
            log_add(logk, drill.hits, drill.misses, drill.tempo, x->slow_percent < 100, lesson_running(), pct);
            if (lesson_running()) lesson_step_done(pct >= 80, pct);
            drill.state = DS_IDLE; follow_key = follow_pad = 0;
            uint8_t p[2] = {2, 0}; lsyx_send(LCMD_PATH, p, 2);
            learn_notice(pct >= 80 ? "PASSED!" : "KEEP GOING", 1500);
            uint8_t lg[3] = {(uint8_t)log_streak(), (uint8_t)(log_unlocked(UNLOCK_STAGE2) ? 2 : 1), (uint8_t)(log_attempts() & 127)}; lsyx_send(LCMD_LOG, lg, 3);
            drill_send_state(); x->redraw = 1;
            /* Lesson: start the next step's drill automatically. */
            const struct lesson_step *ls = lesson_step();
            if (ls && ls->type != DR_TEXT) { x->root = ls->root; x->scale = ls->scale; x->minor = ls->minor; drill_start(x, ls->type, ls->count); }
        } else { drill_pick_target(x); drill_play_reference(x); }
    }
    /* Replay playback of the reference or the attempt. */
    if (replay_on) {
        const struct event *ev = replay_which ? drill.mine : drill.ref; unsigned n = replay_which ? drill.mine_count : drill.ref_count;
        while (replay_pos < n && x->now - replay_started >= slow_ms(ev[replay_pos].at * 4U, x)) {
            lmidi_note(learn_track_channel(replay_which ? LT_KEYS : LT_CHORDS), ev[replay_pos].note, ev[replay_pos].vel);
            ++replay_pos;
        }
        if (replay_pos >= n && x->now - replay_started >= slow_ms(700, x) + (n ? slow_ms(ev[n - 1].at * 4U, x) : 0)) {
            for (unsigned i = 0; i < n; ++i) lmidi_note(learn_track_channel(replay_which ? LT_KEYS : LT_CHORDS), ev[i].note, 0);
            replay_on = 0; x->redraw = 1;
        }
    }
}

static void drill_answer_note(struct lctx *x, unsigned note, unsigned velocity)
{
    if (drill.state != DS_WAIT_ANSWER || !velocity) return;
    if (!drill.answer_count) drill.answer_first = x->now;
    if (drill.answer_count < 8) drill.answer[drill.answer_count++] = (uint8_t)note;
    if (drill.mine_count < EV_MAX) { drill.mine[drill.mine_count].note = (uint8_t)note; drill.mine[drill.mine_count].vel = (uint8_t)velocity; drill.mine[drill.mine_count].at = (uint16_t)((x->now - drill.answer_first) / 4U); ++drill.mine_count; }
}

/* ---- public ---- */
void stage1_init(void)
{
    held_count = 0; chord_mode = 0; memset(&drill, 0, sizeof drill); last_roman_count = 0;
    shown_big[0] = shown_sub[0] = shown_prog[0] = shown_prompt[0] = shown_hint[0] = shown_head[0] = shown_notice[0] = 0;
    shown_lit = shown_bright = 0; shown_staff_note = 999; shown_pads = ~0U;
    groove_sends_edit(LT_KEYS, 0, reverb_send); groove_sends_edit(LT_KEYS, 1, delay_send);
    groove_sends_edit(LT_CHORDS, 0, reverb_send); groove_sends_edit(LT_CHORDS, 1, delay_send);
}

void stage1_key(struct lctx *x, unsigned note, unsigned velocity)
{
    unsigned root_set = x->c.buttons & LB(LB_GLO);
#ifdef LEARN_BOARD_SMK37
    extern unsigned smk37_shift(void);
    root_set |= smk37_shift();
#endif
    if (velocity && root_set) { x->root = note % 12; x->redraw = 1; uint8_t p[2] = {(uint8_t)x->root, (uint8_t)x->scale}; lsyx_send(LCMD_KEY, p, 2); root_set_armed = 1; return; }
    if (velocity && chord_mode) {
        int d = theory_degree(x->root, x->scale, note);
        if (d) { play_degree(x, (unsigned)d, 0, 17, velocity); drill_answer_note(x, note, velocity); for (unsigned i = 0; i < 3; ++i) { uint8_t n[4]; theory_degree_chord(x->root, x->minor, (unsigned)d, 0, chord_octave(x), n); drill_answer_note(x, n[i], velocity); } return; }
    }
    if (!velocity) {
        if (chord_mode) lmidi_chord_off(17);
        held_remove(note); lmidi_note(learn_track_channel(LT_KEYS), note, 0);
        /* The nudged note may differ: release both candidates. */
        unsigned nudged = theory_nudge(x->root, x->scale, note);
        if (nudged != note) lmidi_note(learn_track_channel(LT_KEYS), nudged, 0);
        x->redraw = 1; return;
    }
    unsigned play = note;
    if (!theory_in_scale(x->root, x->scale, note)) {
        if (hints_on) play = theory_nudge(x->root, x->scale, note); /* NUDGE */
        else { learn_notice("NOT IN THE KEY", 400); x->redraw = 1; return; } /* MUTE */
    }
    held_add(play);
    lmidi_note(learn_track_channel(LT_KEYS), play, velocity);
    drill_answer_note(x, play, velocity);
    x->redraw = 1;
}

void stage1_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    if (pad >= 16) return;
    if (velocity == 0xFFFF) { (void)pressure; return; }
    if (velocity) {
        pad_down[pad] = 1; pad_hold_since[pad] = x->now;
        unsigned slot = pad + 1;
        if (pad == 7) sus_chord(x, 1, slot, velocity);
        else if (pad == 15) sus_chord(x, 0, slot, velocity);
        else {
            unsigned degree = (pad & 7) + 1, seventh = pad >= 8;
            play_degree(x, degree, seventh, slot, velocity);
            uint8_t n[4]; unsigned c = theory_degree_chord(x->root, x->minor, degree, seventh, chord_octave(x), n);
            for (unsigned i = 0; i < c; ++i) drill_answer_note(x, n[i], velocity);
        }
    } else {
        pad_down[pad] = 0; lmidi_chord_off(pad + 1);
    }
    x->redraw = 1;
}

void stage1_encoder(struct lctx *x, unsigned id, int delta)
{
    switch (id) {
    case 0: bank_sel = (uint8_t)((bank_sel + BANK_COUNT + (delta > 0 ? 1 : BANK_COUNT - 1)) % BANK_COUNT); learn_bank_select(LT_KEYS, bank_sel, patch_sel); learn_bank_select(LT_CHORDS, bank_sel, patch_sel); break;
    case 1: patch_sel = (uint8_t)((patch_sel + 8 + (delta > 0 ? 1 : 7)) % 8); learn_bank_select(LT_KEYS, bank_sel, patch_sel); learn_bank_select(LT_CHORDS, bank_sel, patch_sel); break;
    case 2: { int v = (int)reverb_send + delta; reverb_send = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v); groove_sends_edit(LT_KEYS, 0, delta); groove_sends_edit(LT_CHORDS, 0, delta); } break;
    case 3: { int v = (int)delay_send + delta; delay_send = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v); groove_sends_edit(LT_KEYS, 1, delta); groove_sends_edit(LT_CHORDS, 1, delta); } break;
    case 4: { int v = (int)x->slow_percent + delta * 5; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); uint8_t p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); } break;
    case 5: { int v = (int)lesson_pick + delta; lesson_pick = (unsigned)(v < 0 ? 0 : v >= (int)lesson_count ? (int)lesson_count - 1 : v); learn_notice(lessons[lesson_pick].title, 1200); } break;
    case 6: { static const uint8_t kinds[4] = {DR_ECHO, DR_CHORD, DR_PROGRESSION, DR_SCALE}; unsigned i = 0; while (i < 4 && kinds[i] != drill_kind_pick) ++i; i = (i + 4 + (delta > 0 ? 1 : 3)) % 4; drill_kind_pick = kinds[i]; learn_notice(drill_kind_pick == DR_ECHO ? "DRILL: ECHO" : drill_kind_pick == DR_CHORD ? "DRILL: CHORD MATCH" : drill_kind_pick == DR_PROGRESSION ? "DRILL: PROGRESSION" : "DRILL: SCALE", 1000); } break;
    default: break;
    }
    x->redraw = 1;
}

void stage1_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_PATH:
        if (n >= 2) {
            if (p[0] == 1) { int k = (int)p[1] - (LEARN_FIRST_NOTE + 12 * x->octave); follow_key = (k >= 0 && k < (int)LEARN_KEYS) ? (unsigned)k + 1 : 0; follow_pad = 0; }
            else if (p[0] == 0) { follow_pad = p[1] + 1; follow_key = 0; }
            else follow_key = follow_pad = 0;
            x->redraw = 1;
        }
        break;
    case LCMD_HINT:
        if (n >= 3 && hints_on) { roman_push(p[0], p[1]); last_minor = p[2]; strncpy(hint_line, theory_why_hint(p[2], p[0]), sizeof hint_line - 1); hint_until = x->now + 2500U; x->redraw = 1; }
        break;
    case LCMD_DRILL:
        if (n >= 4) {
            /* Mirror the partner's drill state so this screen shows the prompt. */
            if (drill.state == DS_IDLE || drill.kind != p[1]) { drill.kind = p[1]; }
            drill.state = p[0] == DS_IDLE ? DS_IDLE : p[0] == DS_WAIT_ANSWER ? DS_WAIT_ANSWER : DS_PLAYING_REF; drill.index = p[2];
            if (p[0] == DS_IDLE) follow_key = follow_pad = 0;
            x->redraw = 1;
        }
        break;
    case LCMD_SCORE:
        if (n >= 2) learn_notice(p[0] ? "HIT" : "MISS", 500);
        break;
    case LCMD_LOG:
        if (n >= 3 && p[1] >= 2) log_unlock(UNLOCK_STAGE2);
        break;
    default: break;
    }
}

void stage1_control(struct lctx *x)
{
    unsigned b = x->btn_down;
    if (b & LB(LB_SCL)) {
#ifdef LEARN_BOARD_FM1
        if (x->c.buttons & LB(LB_GLO)) { chord_mode = !chord_mode; learn_notice(chord_mode ? "CHORD KEYS ON" : "CHORD KEYS OFF", 900); }
        else
#endif
        { x->scale = (x->scale + 1) % SCALE_COUNT; x->minor = x->scale == SCALE_MINOR || x->scale == SCALE_PENT_MIN || x->scale == SCALE_BLUES; uint8_t p[2] = {(uint8_t)x->root, (uint8_t)x->scale}; lsyx_send(LCMD_KEY, p, 2); }
        x->redraw = 1;
    }
    if (b & LB(LB_ENV)) { hints_on = !hints_on; learn_notice(hints_on ? "HINTS + NUDGE ON" : "HINTS OFF, MUTE OFF-KEY", 900); x->redraw = 1; }
    if (b & LB(LB_FX)) { staff_on = !staff_on; x->redraw = 1; }
    if (b & LB(LB_EDIT)) stage1_encoder(x, 0, 1);
    if (b & LB(LB_SAVE)) stage1_encoder(x, 1, 1);
    if (b & LB(LB_PLAY)) { if (drill.state != DS_IDLE) drill_stop(x); else drill_start(x, drill_kind_pick, 10); }
    if (b & LB(LB_REC)) {
        if (drill.ref_count || drill.mine_count) { replay_which = !replay_which; replay_on = 1; replay_pos = 0; replay_started = x->now; learn_notice(replay_which ? "REPLAY: YOU" : "REPLAY: REFERENCE", 800); }
        else learn_notice("NOTHING TO REPLAY YET", 800);
    }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); drill_stop(x); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(lesson_pick); const struct lesson_step *ls = lesson_step(); if (ls && ls->type != DR_TEXT) drill_start(x, ls->type, ls->count); }
    }
    if (b & LB(LB_HOME)) { drill_stop(x); replay_on = 0; lmidi_all_off(); held_count = 0; x->redraw = 1; }
    /* TEXT lesson steps: any key or pad press continues. */
    const struct lesson_step *ls = lesson_step();
    if (ls && ls->type == DR_TEXT && drill.state == DS_IDLE && (x->keys_down || held_count)) { lesson_step_done(1, 100); ls = lesson_step(); if (ls && ls->type != DR_TEXT) { x->root = ls->root; x->scale = ls->scale; x->minor = ls->minor; drill_start(x, ls->type, ls->count); } }
    /* External notes (the other box) count as answers and show on screen. */
    uint8_t st, d1, d2; unsigned changed = 0;
    while (lmidi_pull_external(&st, &d1, &d2)) { changed = 1; if ((st & 0xF0) == 0x90 && d2) drill_answer_note(x, d1, d2); }
    if (changed) x->redraw = 1;
    drill_poll(x);
    if (hint_line[0] && (int32_t)(x->now - hint_until) >= 0) { hint_line[0] = 0; x->redraw = 1; }
}

uint32_t stage1_pad_colour(unsigned pad)
{
    static const uint32_t colours[7] = {P_GREEN, P_ORANGE, P_CYAN, P_BLUE, P_RED, P_PURPLE, P_WHITE};
    if (pad >= 16) return P_OFF;
    if (pad == 7 || pad == 15) return pad_down[pad] ? P_FULL(P_WHITE) : P_DIM(P_WHITE);
    unsigned degree = (pad & 7) + 1;
    uint32_t c = colours[theory_degree_colour(last_minor, degree)];
    if (pad >= 8) c = P_DIM(c);
    if (follow_pad == pad + 1) return P_FULL(c);
    if (pad_down[pad]) return P_FULL(c);
    return c;
}

/* ---- drawing ---- */
static void draw_if_changed(char *shown, const char *text, int x, int y, unsigned scale, uint16_t fg, int clear_w, unsigned force)
{
    if (!force && !strcmp(shown, text)) return;
    draw_fill(x, y, clear_w, (int)(7 * scale), C_BG);
    draw_text(x, y, text, scale, fg, C_BG);
    strncpy(shown, text, 31); shown[31] = 0;
}

void stage1_draw(struct lctx *x)
{
    char nn[4];
    unsigned force = x->redraw;
    if (force) draw_clear();
    /* Header: key, scale, octave, slow, partner dot. */
    theory_note_name(x->root, nn);
    {
        char line[32]; unsigned n = 0;
        const char *s = nn; while (*s) line[n++] = *s++;
        line[n++] = ' ';
        s = theory_scale_name(x->scale); while (*s && n < 18) line[n++] = *s++;
        line[n++] = ' '; line[n++] = 'O'; line[n++] = (char)('0' + (x->octave + 2));
        if (x->slow_percent < 100) { line[n++] = ' '; line[n++] = 'S'; line[n++] = (char)('0' + x->slow_percent / 10); line[n++] = (char)('0' + x->slow_percent % 10); }
        line[n] = 0;
        draw_if_changed(shown_head, line, 4, 2, 1, C_DIM, 200, force);
        draw_fill(228, 2, 8, 8, lsyx_partner_present() ? C_GREEN : C_PANEL);
    }
    /* Big line: chord name if 3+ held, else the last note. */
    char big[16] = ""; char sub[32] = "";
    if (held_count >= 3) {
        unsigned r; unsigned k = theory_identify(held, held_count, &r);
        theory_note_name(r, nn);
        unsigned n = 0; for (const char *s = nn; *s; ++s) big[n++] = *s;
        for (const char *s = theory_chord_suffix(k); *s && n < 12; ++s) big[n++] = *s;
        big[n] = 0;
        int d = theory_degree(x->root, x->scale, r);
        if (d && k != CHORD_NONE) { const char *rm = theory_roman(x->minor, (unsigned)d, k >= CHORD_MAJ7); unsigned m = 0; for (const char *s = rm; *s; ++s) sub[m++] = *s; sub[m] = 0; }
    } else if (held_count) {
        unsigned note = held[held_count - 1];
        theory_note_name(note, nn);
        unsigned n = 0; for (const char *s = nn; *s; ++s) big[n++] = *s; big[n++] = (char)('0' + (note / 12 > 0 ? note / 12 - 1 : 0)); big[n] = 0;
        int d = theory_degree(x->root, x->scale, note);
        static const char *const ord[8] = {"", "ROOT", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH"};
        unsigned m = 0; for (const char *s = d ? ord[d] : "OUTSIDE"; *s; ++s) sub[m++] = *s;
        if (held_count >= 2) { int iv = (int)note - (int)held[held_count - 2]; sub[m++] = ' '; for (const char *s = theory_interval_name(iv); *s && m < 30; ++s) sub[m++] = *s; }
        sub[m] = 0;
    } else if (last_roman_count) {
        unsigned n = 0; for (const char *s = theory_roman(last_minor, last_roman[0] >> 1, last_roman[0] & 1); *s && n < 12; ++s) big[n++] = *s; big[n] = 0;
        strncpy(sub, "LAST CHORD", sizeof sub);
    } else { strncpy(big, "PLAY", sizeof big); strncpy(sub, "KEYS OR PADS", sizeof sub); }
    draw_if_changed(shown_big, big, 4, 18, 4, C_TEXT, 232, force);
    draw_if_changed(shown_sub, sub, 4, 50, 2, C_CYAN, 232, force);
    /* Keymap strip with held keys lit and the follow-me key bright. */
    uint64_t lit = 0, bright = 0;
    for (unsigned i = 0; i < held_count; ++i) { int k = (int)held[i] - (LEARN_FIRST_NOTE + 12 * x->octave); if (k >= 0 && k < (int)LEARN_KEYS) lit |= 1ULL << k; }
    uint32_t ext = lmidi_held_external();
    for (unsigned k = 0; k < LEARN_KEYS; ++k) if ((ext >> ((LEARN_FIRST_NOTE + 12 * x->octave + k) % 12)) & 1U && (ext >> 12)) lit |= 1ULL << k;
    if (follow_key) bright |= 1ULL << (follow_key - 1);
    if (force || lit != shown_lit || bright != shown_bright) { draw_keymap(70, 26, LEARN_KEYS, (unsigned)(LEARN_FIRST_NOTE + 12 * x->octave), lit, bright); shown_lit = lit; shown_bright = bright; }
    /* Progression line. */
    {
        char line[32]; unsigned n = 0;
        for (unsigned i = last_roman_count; i > 0; --i) { const char *s = theory_roman(last_minor, last_roman[i - 1] >> 1, last_roman[i - 1] & 1); while (*s && n < 28) line[n++] = *s++; if (i > 1 && n < 28) { line[n++] = ' '; line[n++] = '-'; line[n++] = ' '; } }
        line[n] = 0;
        draw_if_changed(shown_prog, line, 4, 102, 2, C_ORANGE, 232, force);
    }
#ifdef LEARN_BOARD_SMK37
    /* Pad palette: two rows of eight cells coloured by function. */
    {
        static const uint16_t cols[7] = {C_GREEN, C_ORANGE, C_CYAN, C_BLUE, C_RED, C_PURPLE, C_TEXT};
        unsigned state = 0; for (unsigned i = 0; i < 16; ++i) state |= pad_down[i] << i; state |= follow_pad << 16;
        if (force || state != shown_pads) {
            shown_pads = state;
            for (unsigned p = 0; p < 16; ++p) {
                int cx = 4 + (int)(p & 7) * 29, cy = 122 + (p >= 8 ? 18 : 0);
                uint16_t c = (p == 7 || p == 15) ? C_DIM : cols[theory_degree_colour(last_minor, (p & 7) + 1)];
                unsigned hot = pad_down[p] || follow_pad == p + 1;
                draw_fill(cx, cy, 27, 16, hot ? c : C_PANEL);
                draw_text(cx + 2, cy + 4, p == 7 ? "s4" : p == 15 ? "s2" : theory_roman(last_minor, (p & 7) + 1, 0), 1, hot ? C_BG : c, hot ? c : C_PANEL);
            }
        }
    }
#else
    if (staff_on) {
        unsigned note = held_count ? held[held_count - 1] : (drill.state == DS_WAIT_ANSWER && drill.target_count ? drill.target[0] : 999);
        if (force || note != shown_staff_note) { if (note < 128) draw_staff(4, 128, note); else draw_fill(0, 118, 70, 42, C_BG); shown_staff_note = note; }
    }
    draw_text(80, 122, chord_mode ? "CHORD KEYS" : "SINGLE KEYS", 1, C_DIM, C_BG);
    draw_text(80, 134, "GLO+SCL TOGGLE", 1, C_PANEL, C_BG);
#endif
    /* Drill / lesson prompt. */
    {
        char line[32] = "";
        const struct lesson_step *ls = lesson_step();
        if (drill.state != DS_IDLE) {
            const char *k = drill.kind == DR_ECHO ? "ECHO" : drill.kind == DR_SCALE ? "SCALE" : drill.kind == DR_CHORD ? "CHORD" : "PROG";
            unsigned n = 0; for (const char *s = k; *s; ++s) line[n++] = *s; line[n++] = ' ';
            line[n++] = (char)('0' + (drill.index + 1) / 10); line[n++] = (char)('0' + (drill.index + 1) % 10); line[n++] = '/';
            line[n++] = (char)('0' + drill.total / 10); line[n++] = (char)('0' + drill.total % 10); line[n++] = ' ';
            const char *st = drill.state == DS_PLAYING_REF ? "LISTEN" : drill.state == DS_WAIT_ANSWER ? "YOUR TURN" : "";
            for (const char *s = st; *s && n < 30; ++s) line[n++] = *s; line[n] = 0;
        } else if (ls) { strncpy(line, ls->prompt ? ls->prompt : "", 31); line[31] = 0; }
        else { unsigned n = 0; for (const char *s = "PLAY: DRILL   ARP: LESSON "; *s; ++s) line[n++] = *s; line[n++] = (char)('0' + (lesson_pick + 1) / 10); line[n++] = (char)('0' + (lesson_pick + 1) % 10); line[n] = 0; }
        draw_if_changed(shown_prompt, line, 4, 164, 1, drill.state == DS_WAIT_ANSWER ? C_GREEN : C_AMBER, 232, force);
    }
    /* Lesson title and progress. */
    if (lesson_running()) {
        const struct lesson *l = &lessons[lesson_running() - 1];
        char line[32]; unsigned n = 0;
        for (const char *s = l->title; *s && n < 20; ++s) line[n++] = *s;
        line[n++] = ' '; line[n++] = (char)('0' + lesson_progress() + 1); line[n++] = '/'; line[n++] = (char)('0' + l->step_count); line[n] = 0;
        draw_text(4, 176, line, 1, C_PURPLE, C_BG);
    } else if (force) draw_fill(4, 176, 232, 8, C_BG);
    /* Replay dots: reference above, attempt below, misses in red. */
    if (drill.ref_count || drill.mine_count) {
        draw_fill(4, 188, 232, 18, C_BG);
        for (unsigned i = 0; i < drill.ref_count && i < 16; ++i) draw_fill(4 + (int)i * 14, 188, 10, 6, replay_on && !replay_which && replay_pos > i ? C_GREEN : C_DIM);
        for (unsigned i = 0; i < drill.mine_count && i < 16; ++i) {
            unsigned ok = i < drill.ref_count && drill.mine[i].note % 12 == drill.ref[i].note % 12;
            draw_fill(4 + (int)i * 14, 198, 10, 6, ok ? C_GREEN : C_RED);
        }
    }
    /* Hint line. */
    draw_if_changed(shown_hint, hint_line, 4, 212, 1, C_YELLOW, 232, force);
    /* Notice bar. */
    const char *n = learn_notice_text();
    draw_if_changed(shown_notice, n ? n : "", 4, 230, 1, C_TEXT, 232, force);
}
