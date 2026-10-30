/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 4: Stage (SMK-37) / Scribe (FM-1). Finish it: a whole song
 * performed with the hands and captured in one pass.
 *
 * The loop that Stages 2-3 built (the shared song model plus the melody
 * track's cells) can be stored into one of eight RAM section slots, each
 * with a type (Intro .. Free) and an energy (1-8 = how many tracks are
 * unmuted). Slots are queued from the pads (SMK) or GLO+keys (FM-1) and
 * recalled at a loop boundary, with an autofill transition edited into the
 * last bar before the change. REC keeps a performance log of the section
 * order; a `song` lesson step supplies a ghost order for follow-me and
 * mistake replay. Nothing here touches Flash.
 *
 * Controls (logical, both boards):
 *   SMK pads 1-8   queue slot (tap next loop, double-tap next beat, hold =
 *                  play while held then return); hold 2 s = why-hint
 *   SMK pads 9-16  SHIFT/GLO held: store the loop into slot 1-8;
 *                  unshifted: cycle a stored slot's type
 *   FM-1 keys      GLO held: keys 1-8 queue slot 1-8, keys 9-16 store 1-8
 *   SCL            cycle the type used by the next store
 *   SAVE           store into the cursor slot    PLAY  transport
 *   REC            song record / replay ghost vs performed
 *   HOME           stop (second press within 2 s discards the record)
 *   ARP            start / stop the picked lesson   ENV  cycle transition
 *   K1 energy  K2 store type  K3 lesson  K4 slow %  K5 transition
 *   K6 tempo nudge +-10 %  K7 swing
 *   faders (SMK)   1 energy, 2 drums, 3 music, 4 melody */
#include "learn_song.h"
#include <string.h>

enum { SEC_MAX = 8, SEC_CELLS = 64, REC_MAX = 32, GHOST_MAX = 8 };
enum { ST_INTRO, ST_VERSE, ST_PRE, ST_CHORUS, ST_BRIDGE, ST_BREAK, ST_OUTRO, ST_FREE, ST_COUNT };
enum { TR_NONE, TR_DROP, TR_RISER, TR_FILL, TR_HANDOFF, TR_COUNT };
enum { QM_BAR, QM_BEAT, QM_HOLD };
enum { NONE = 0xFF, LESSON_BARS = 4 };

struct section {
    struct song_model model;
    struct groove_step melody[SEC_CELLS];
    uint8_t used, type, energy, pad;
};
struct rec_entry { uint8_t slot, type, bars, energy; };

static struct section sections[SEC_MAX];
static struct groove_step scratch[SEC_CELLS];      /* read/edit buffer for the melody track */
static uint8_t active_slot = NONE, queued_slot = NONE, queue_mode, hold_return = NONE, cursor_slot;
static uint8_t store_type = ST_VERSE, transition = TR_NONE, energy = 7;
static uint8_t trans_applied; static int8_t send_delta[2];   /* reverb added to drums, chords by a riser */
static unsigned last_step, bars_played, last_bar, last_beat;
static uint32_t play_started, elapsed_frozen;
static uint32_t pad_press_at[16], last_tap_at[8]; static uint8_t pad_down[16], pad_hinted[16];
static uint8_t key_slot[16];                       /* FM-1: 1 + slot a held GLO key maps to */
/* Song record. */
static struct rec_entry record[REC_MAX]; static unsigned rec_count; static uint8_t recording;
static uint32_t stop_at;
/* Lesson ghost (section types 0..7) and the performed order, plus follow-me. */
static uint8_t ghost[GHOST_MAX], ghost_count, ghost_pos, perf[GHOST_MAX], perf_count, perf_late[GHOST_MAX];
static uint8_t ghost_energy_hit[GHOST_MAX], follow_pad, follow_blink, press_timing; /* timing 0 none 1 green 2 yellow */
static uint8_t replay_which, replay_shown, lesson_pick, passed_once, lesson_tempo_set;
/* Mirror of the partner's state (SECTION frames) for a box without sections. */
static uint8_t mirror_slot = NONE, mirror_type, mirror_bars;
/* Drawn-state cache. */
static char shown_head[32], shown_big[16], shown_row[32], shown_time[16], shown_trans[32], shown_prompt[32], shown_hint[40], shown_notice[32];
static unsigned shown_slots = ~0U, shown_bar = ~0U, shown_strip = ~0U;
static char hint_line[40]; static uint32_t hint_until;

static const char *const type_names[ST_COUNT] = {"INTRO", "VERSE", "PRE", "CHORUS", "BRIDGE", "BREAK", "OUTRO", "FREE"};
static const char *const trans_names[TR_COUNT] = {"NONE", "DROP", "RISER", "FILL", "HAND-OFF"};
static const uint32_t type_pad[ST_COUNT] = {0x101010, P_BLUE, 0x002818, P_ORANGE, P_PURPLE, P_WHITE, 0x101010, P_GREEN};
#define C_TEAL 0x0410
static const uint16_t type_screen[ST_COUNT] = {C_DIM, C_BLUE, C_TEAL, C_ORANGE, C_PURPLE, C_TEXT, C_DIM, C_GREEN};
static const uint8_t type_energy[ST_COUNT] = {2, 4, 5, 8, 3, 1, 2, 4};  /* ghost energy for a lesson */
/* Energy unmute order: drums, bass, chords, melody, shadows. Energy 8 = all seven (keys stay live). */
static const uint8_t energy_order[7] = {LT_DRUMS, LT_BASS, LT_CHORDS, LT_MELODY, LT_SHADOW_A, LT_SHADOW_B, LT_SHADOW_C};

/* ---- small helpers ---- */
static char *put_str(char *d, const char *s, unsigned max) { while (*s && max--) *d++ = *s++; *d = 0; return d; }
static char *put_num(char *d, unsigned v)
{
    char tmp[10]; unsigned n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < sizeof tmp);
    while (n) *d++ = tmp[--n];
    *d = 0; return d;
}
static unsigned modifier(const struct lctx *x)
{
    unsigned m = (x->c.buttons & LB(LB_GLO)) != 0;
#ifdef LEARN_BOARD_SMK37
    extern unsigned smk37_shift(void);
    m |= smk37_shift();
#endif
    return m;
}
static unsigned track_muted(unsigned track) { struct groove_view v; groove_get_view(track, 0, &v); return v.muted; }
static unsigned track_volume(unsigned track) { struct groove_view v; groove_get_view(track, 0, &v); return v.volume; }
static void set_mute(unsigned track, unsigned muted) { if (track_muted(track) != muted) groove_mix_toggle(track, 0); }

/* Stage 2 owns the model and initialises it; if page 4 is the first to look, give it defaults. */
static unsigned loop_bars(void) { if (!song.bars) song_init(); return song.bars; }

static void apply_energy(unsigned e)
{
    if (e < 1) e = 1; if (e > 8) e = 8;
    energy = (uint8_t)e;
    for (unsigned i = 0; i < 7; ++i) set_mute(energy_order[i], i >= e);
}

static void send_section(unsigned state)
{
    unsigned left = active_slot < SEC_MAX && sections[active_slot].model.bars > bars_played ? sections[active_slot].model.bars - bars_played : 0;
    uint8_t p[4] = {(uint8_t)(active_slot < SEC_MAX ? active_slot : 127), (uint8_t)(active_slot < SEC_MAX ? sections[active_slot].type : 0), (uint8_t)state, (uint8_t)left};
    lsyx_send(LCMD_SECTION, p, 4);
}
static void send_songpos(const struct lctx *x)
{
    unsigned secs = (recording || song_playing() ? x->now - play_started : elapsed_frozen) / 1000U;
    uint8_t p[4] = {(uint8_t)(bars_played & 127), (uint8_t)song_beat(), (uint8_t)(secs & 127), (uint8_t)((secs >> 7) & 127)};
    lsyx_send(LCMD_SONGPOS, p, 4);
}
static void send_path(void)
{
    uint8_t p[2] = {(uint8_t)(follow_pad ? 0 : 2), (uint8_t)(follow_pad ? follow_pad - 1 : 0)};
    lsyx_send(LCMD_PATH, p, 2);
}

/* ---- sections ---- */
static void store_slot(struct lctx *x, unsigned slot, unsigned type)
{
    if (slot >= SEC_MAX) return;
    struct section *s = &sections[slot];
    memcpy(&s->model, &song, sizeof song);
    memset(s->melody, 0, sizeof s->melody);
    groove_sequence_read(LT_MELODY, 0, s->melody, SEC_CELLS);
    s->used = 1; s->type = (uint8_t)(type % ST_COUNT); s->energy = energy;
    cursor_slot = (uint8_t)slot;
    if (active_slot == NONE) { active_slot = (uint8_t)slot; bars_played = 0; }
    char line[32]; char *d = put_str(line, "STORED ", 8); d = put_num(d, slot + 1); d = put_str(d, " ", 1); put_str(d, type_names[s->type], 8);
    learn_notice(line, 900);
    send_section(0);
    x->redraw = 1;
}

static void revert_transition(void)
{
    if (!trans_applied) return;
    trans_applied = 0;
    if (send_delta[0]) { groove_sends_edit(LT_DRUMS, 0, -send_delta[0]); send_delta[0] = 0; }
    if (send_delta[1]) { groove_sends_edit(LT_CHORDS, 0, -send_delta[1]); send_delta[1] = 0; }
    apply_energy(energy);                 /* undoes a Drop */
}

/* The incoming section becomes the loop: model copy, every renderer, the
 * melody cells, length, energy and tempo. Called at a loop boundary. */
static void recall_slot(struct lctx *x, unsigned slot)
{
    if (slot >= SEC_MAX || !sections[slot].used) return;
    revert_transition();
    const struct section *s = &sections[slot];
    if (recording && active_slot < SEC_MAX && rec_count < REC_MAX) {
        record[rec_count].slot = active_slot; record[rec_count].type = sections[active_slot].type;
        record[rec_count].bars = (uint8_t)(bars_played > 255 ? 255 : bars_played); record[rec_count].energy = energy; ++rec_count;
    }
    memcpy(&song, &s->model, sizeof song);
    song_write_all();
    groove_sequence_length_set(LT_MELODY, loop_bars() * 16);
    groove_sequence_replace(LT_MELODY, 0, s->melody, SEC_CELLS);
    song_set_tempo(song.tempo);
    song_set_swing(song.swing);
    apply_energy(s->energy);
    active_slot = (uint8_t)slot; bars_played = 0; last_bar = 0; cursor_slot = (uint8_t)slot;
    x->root = song.root; x->minor = song.minor; x->scale = song.minor ? SCALE_MINOR : SCALE_MAJOR;
    /* Lesson: the performed order grows by one; score the timing of the press. */
    if (ghost_count && perf_count < GHOST_MAX) {
        perf[perf_count] = s->type; perf_late[perf_count] = press_timing == 2; ghost_energy_hit[perf_count] = (ghost_pos < ghost_count && type_energy[ghost[ghost_pos]] == s->energy);
        ++perf_count; if (ghost_pos < ghost_count) ++ghost_pos;
        press_timing = 0; follow_pad = 0; follow_blink = 0; send_path();
    }
    send_section(1);
    x->redraw = 1;
}

/* Autofill transitions: one-bar edits applied when the last bar before the
 * switch begins. Everything is undone by the recall that follows. */
static void apply_transition(struct lctx *x)
{
    if (trans_applied || transition == TR_NONE || queued_slot >= SEC_MAX) return;
    trans_applied = 1;
    const struct section *in = &sections[queued_slot];
    unsigned steps = loop_bars() * 16, last = steps - 16;
    switch (transition) {
    case TR_DROP:
        set_mute(LT_DRUMS, 1); set_mute(LT_BASS, 1);
        break;
    case TR_RISER:
        /* Hat on every 8th in the first half of the bar, every 16th in the second; reverb climbs. */
        song.lane[LANE_HAT].mask |= 0x5555U; song.lane[LANE_HAT].mask |= 0xFF00U; song.lane[LANE_HAT].manual = 1;
        song_write_drums();
        send_delta[0] = 6; send_delta[1] = 5;
        groove_sends_edit(LT_DRUMS, 0, send_delta[0]); groove_sends_edit(LT_CHORDS, 0, send_delta[1]);
        break;
    case TR_FILL:
        song.fill_every = 1; song_write_drums();
        break;
    case TR_HANDOFF:
        /* Melody fades through the last bar; the incoming section's first chord enters a beat early. */
        groove_sequence_read(LT_MELODY, 0, scratch, SEC_CELLS);
        for (unsigned s = last; s < steps && s < SEC_CELLS; ++s) scratch[s].velocity = (uint8_t)(scratch[s].velocity * (16 - (s - last)) / 16);
        groove_sequence_replace(LT_MELODY, 0, scratch, SEC_CELLS);
        if (in->model.chord[0].degree) { song.chord[loop_bars() * 4 - 1] = in->model.chord[0]; song_write_chords(); }
        break;
    default: break;
    }
    uint8_t p = transition; lsyx_send(LCMD_TRANSITION, &p, 1);
    (void)x;
}

static void queue_slot(struct lctx *x, unsigned slot, unsigned mode)
{
    if (slot >= SEC_MAX || !sections[slot].used) { learn_notice("EMPTY SLOT: SHIFT+PAD TO STORE", 900); return; }
    queued_slot = (uint8_t)slot; queue_mode = (uint8_t)mode;
    uint8_t p[2] = {(uint8_t)slot, (uint8_t)mode}; lsyx_send(LCMD_QUEUE, p, 2);
    /* Lesson follow-me timing: in the lit window = in time, after it = late. */
    if (ghost_count && ghost_pos < ghost_count) press_timing = (uint8_t)(follow_pad ? 1 : (bars_played >= LESSON_BARS ? 2 : 1));
    if (!song_playing()) { recall_slot(x, slot); queued_slot = NONE; return; }
    if (loop_bars() == 1 || song_bar() == loop_bars() - 1) apply_transition(x);
    x->redraw = 1;
}

/* ---- record and lesson ---- */
static void lesson_load_ghost(void)
{
    const struct lesson_step *ls = lesson_step();
    ghost_count = 0; ghost_pos = 0; perf_count = 0; passed_once = 0; replay_shown = 0; lesson_tempo_set = 0;
    if (!ls || ls->type != DR_SONG) return;
    for (unsigned i = 0; i < GHOST_MAX && ls->data[i]; ++i) ghost[ghost_count++] = (uint8_t)((ls->data[i] - 1) % ST_COUNT);
    if (ls->tempo) { song_set_tempo(ls->tempo); lesson_tempo_set = 1; }
}
static unsigned lesson_score(void)
{
    if (!ghost_count) return 0;
    unsigned order = 0, en = 0;
    for (unsigned i = 0; i < ghost_count; ++i) { if (i < perf_count && perf[i] == ghost[i]) ++order; if (i < perf_count && ghost_energy_hit[i]) ++en; }
    return (order * 100U / ghost_count + en * 100U / ghost_count) / 2;
}
static void lesson_finish(struct lctx *x)
{
    if (!ghost_count) return;
    unsigned score = lesson_score(), late = 0;
    for (unsigned i = 0; i < perf_count; ++i) late += perf_late[i];
    const struct lesson_step *ls = lesson_step();
    unsigned passed = score >= (ls && ls->target ? ls->target : 80) && !late;
    log_add(LOGK_SONG, score, late, song.tempo, x->slow_percent < 100, lesson_running(), score);
    lesson_step_done(passed, score);
    passed_once = 1; replay_which = 0; replay_shown = 1;
    learn_notice(passed ? "SONG PASSED!" : "REC: COMPARE ORDERS", 1500);
    uint8_t lg[3] = {(uint8_t)log_streak(), 4, (uint8_t)(log_attempts() & 127)}; lsyx_send(LCMD_LOG, lg, 3);
    ghost_pos = ghost_count; follow_pad = 0; send_path();
    x->redraw = 1;
}
static void record_start(struct lctx *x)
{
    rec_count = 0; recording = 1; play_started = x->now; bars_played = 0;
    perf_count = 0; ghost_pos = 0; replay_shown = 0;
    if (!song_playing()) song_play(1);
    learn_notice("SONG RECORD ON", 900);
    uint8_t p = 1; lsyx_send(LCMD_REC, &p, 1);
    x->redraw = 1;
}
static void record_stop(struct lctx *x)
{
    if (!recording) return;
    if (active_slot < SEC_MAX && rec_count < REC_MAX) { record[rec_count].slot = active_slot; record[rec_count].type = sections[active_slot].type; record[rec_count].bars = (uint8_t)bars_played; record[rec_count].energy = energy; ++rec_count; }
    recording = 0; elapsed_frozen = x->now - play_started;
    unsigned secs = elapsed_frozen / 1000U;
    log_add(LOGK_SONG, rec_count, 0, song.tempo, x->slow_percent < 100, lesson_running(), (unsigned)(secs > 255 ? 255 : secs));
    uint8_t p = 0; lsyx_send(LCMD_REC, &p, 1);
    learn_notice("SONG SAVED IN RAM. HOME AGAIN: DISCARD", 2000);
    if (ghost_count) lesson_finish(x);
    x->redraw = 1;
}

/* ---- why-hints ---- */
static void slot_hint(struct lctx *x, unsigned slot)
{
    char *d = hint_line;
    if (slot >= SEC_MAX || !sections[slot].used) { put_str(hint_line, "EMPTY: STORE THE LOOP HERE", 39); }
    else {
        const struct section *s = &sections[slot];
        d = put_str(d, type_names[s->type], 8); d = put_str(d, " E", 2); d = put_num(d, s->energy); d = put_str(d, " ", 1); d = put_num(d, s->model.bars); d = put_str(d, "B ", 2);
        for (unsigned b = 0; b < 4 && d < hint_line + 30; ++b) { if (b) d = put_str(d, "-", 1); d = put_str(d, theory_roman(s->model.minor, s->model.chord[b].degree ? s->model.chord[b].degree : 1, s->model.chord[b].seventh), 5); }
    }
    hint_until = x->now + 3000U; x->redraw = 1;
}
static const char *trans_hint(void)
{
    static const char *const h[TR_COUNT] = {"NO EDIT BETWEEN SECTIONS", "DROP: DRUMS+BASS OUT, LAST BAR", "RISER: HATS DOUBLE, REVERB UP", "FILL: SNARE+HAT ROLL, LAST BAR", "HAND-OFF: MELODY FADES, CHORD EARLY"};
    return h[transition % TR_COUNT];
}

/* ---- slot presses, shared by pads and GLO+keys ---- */
static void slot_press(struct lctx *x, unsigned slot)
{
    unsigned dbl = x->now - last_tap_at[slot] < 350U;
    last_tap_at[slot] = x->now; pad_press_at[slot] = x->now;
    hold_return = active_slot;                 /* where a hold comes back to */
    queue_slot(x, slot, dbl ? QM_BEAT : QM_BAR);
}
static void slot_release(struct lctx *x, unsigned slot)
{
    /* A long press was a momentary play: come back to where we were at the next loop. */
    unsigned back = hold_return; hold_return = NONE;
    if (x->now - pad_press_at[slot] < 600U || !sections[slot].used || back >= SEC_MAX || back == slot || !sections[back].used) return;
    if (queued_slot == slot) { queued_slot = NONE; revert_transition(); x->redraw = 1; }   /* never got there */
    else if (active_slot == slot) { uint8_t p[2] = {(uint8_t)slot, QM_HOLD}; lsyx_send(LCMD_QUEUE, p, 2); queue_slot(x, back, QM_BAR); }
}

/* ---- public ---- */
void stage4_init(void)
{
    (void)loop_bars();
    memset(sections, 0, sizeof sections);
    active_slot = queued_slot = hold_return = NONE; cursor_slot = 0; store_type = ST_VERSE; transition = TR_NONE; energy = 7;
    trans_applied = 0; send_delta[0] = send_delta[1] = 0; last_step = bars_played = last_bar = last_beat = 0;
    rec_count = 0; recording = 0; ghost_count = perf_count = 0; follow_pad = 0; replay_shown = 0; mirror_slot = NONE;
    memset(pad_down, 0, sizeof pad_down); memset(key_slot, 0, sizeof key_slot);
    shown_head[0] = shown_big[0] = shown_row[0] = shown_time[0] = shown_trans[0] = shown_prompt[0] = shown_hint[0] = shown_notice[0] = hint_line[0] = 0;
    shown_slots = shown_bar = shown_strip = ~0U;
}

void stage4_key(struct lctx *x, unsigned note, unsigned velocity)
{
    int k = (int)note - (LEARN_FIRST_NOTE + 12 * x->octave);
    if (!velocity) {
        if (k >= 0 && k < 16 && key_slot[k]) { unsigned slot = key_slot[k] - 1; key_slot[k] = 0; pad_down[slot] = 0; slot_release(x, slot); x->redraw = 1; return; }
        lmidi_note(learn_track_channel(LT_MELODY), note, 0); x->redraw = 1; return;
    }
#ifdef LEARN_BOARD_FM1
    if (modifier(x) && k >= 0 && k < 16) {
        unsigned slot = (unsigned)k & 7;
        if (k < 8) { key_slot[k] = (uint8_t)(slot + 1); pad_down[slot] = 1; pad_hinted[slot] = 0; slot_press(x, slot); }
        else store_slot(x, slot, store_type);
        return;
    }
#endif
    lmidi_note(learn_track_channel(LT_MELODY), note, velocity);
    x->redraw = 1;
}

void stage4_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    if (pad >= 16 || velocity == 0xFFFF) { (void)pressure; return; }
    if (velocity) {
        pad_down[pad] = 1; pad_press_at[pad] = x->now; pad_hinted[pad] = 0;
        if (pad < 8) slot_press(x, pad);
        else if (modifier(x)) store_slot(x, pad - 8, store_type);
        else if (sections[pad - 8].used) { sections[pad - 8].type = (uint8_t)((sections[pad - 8].type + 1) % ST_COUNT); learn_notice(type_names[sections[pad - 8].type], 700); cursor_slot = (uint8_t)(pad - 8); }
        else learn_notice("SHIFT+PAD STORES HERE", 700);
    } else {
        pad_down[pad] = 0;
        if (pad < 8) slot_release(x, pad);
    }
    x->redraw = 1;
}

void stage4_encoder(struct lctx *x, unsigned id, int delta)
{
    switch (id) {
    case 0: apply_energy((unsigned)((int)energy + delta < 1 ? 1 : (int)energy + delta > 8 ? 8 : (int)energy + delta)); break;
    case 1: store_type = (uint8_t)((store_type + ST_COUNT + (delta > 0 ? 1 : ST_COUNT - 1)) % ST_COUNT); learn_notice(type_names[store_type], 700); break;
    case 2: { int v = (int)lesson_pick + delta; lesson_pick = (uint8_t)(v < 0 ? 0 : v >= (int)lesson_count ? (int)lesson_count - 1 : v); learn_notice(lessons[lesson_pick].title, 1200); uint8_t p = lesson_pick; lsyx_send(LCMD_LESSON, &p, 1); } break;
    case 3: { int v = (int)x->slow_percent + delta * 5; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); uint8_t p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); } break;
    case 4: transition = (uint8_t)((transition + TR_COUNT + (delta > 0 ? 1 : TR_COUNT - 1)) % TR_COUNT); learn_notice(trans_hint(), 1200); { uint8_t p = transition; lsyx_send(LCMD_TRANSITION, &p, 1); } break;
    case 5: {   /* Tempo nudge: +-10 % around the section's own tempo. */
        unsigned base = active_slot < SEC_MAX ? sections[active_slot].model.tempo : song.tempo, lo = base * 9 / 10, hi = base * 11 / 10;
        int v = (int)song.tempo + delta; song_set_tempo((unsigned)(v < (int)lo ? (int)lo : v > (int)hi ? (int)hi : v)); } break;
    case 6: song_set_swing((unsigned)((int)song.swing + delta)); break;
    default: break;
    }
    x->redraw = 1;
}

void stage4_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (value > 127) value = 127;
    if (id == 0) { apply_energy(1 + value * 7 / 127); x->redraw = 1; return; }
    static const uint8_t groups[3][5] = {{LT_DRUMS, 255, 255, 255, 255}, {LT_BASS, LT_CHORDS, LT_SHADOW_A, LT_SHADOW_B, LT_SHADOW_C}, {LT_MELODY, LT_KEYS, 255, 255, 255}};
    if (id > 3) return;
    for (unsigned i = 0; i < 5 && groups[id - 1][i] != 255; ++i) {
        unsigned t = groups[id - 1][i]; int d = (int)value - (int)track_volume(t);
        if (d) groove_control_edit(t, 0, d, 0);
    }
    x->redraw = 1;
}

void stage4_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_QUEUE: if (n >= 2 && learn_page() == 4) queue_slot(x, p[0], p[1] % 3); break;
    case LCMD_TRANSITION: if (n >= 1) { transition = (uint8_t)(p[0] % TR_COUNT); x->redraw = 1; } break;
    case LCMD_SECTION: if (n >= 4) { mirror_slot = p[0]; mirror_type = (uint8_t)(p[1] % ST_COUNT); mirror_bars = p[3]; x->redraw = 1; } break;
    case LCMD_SONGPOS: break;                         /* position is shown from our own transport */
    case LCMD_PATH: if (n >= 2) { follow_pad = p[0] == 0 ? (uint8_t)(p[1] + 1) : 0; x->redraw = 1; } break;
    case LCMD_LESSON: if (n >= 1 && p[0] < lesson_count) lesson_pick = p[0]; break;
    case LCMD_LOG: if (n >= 3 && p[1] >= 4) log_unlock(UNLOCK_STAGE4); break;
    default: break;
    }
}

void stage4_control(struct lctx *x)
{
    unsigned b = x->btn_down;
    if (b & LB(LB_SCL)) { store_type = (uint8_t)((store_type + 1) % ST_COUNT); learn_notice(type_names[store_type], 700); x->redraw = 1; }
    if (b & LB(LB_SAVE)) store_slot(x, cursor_slot, store_type);
    if (b & LB(LB_ENV)) stage4_encoder(x, 4, 1);
    if (b & LB(LB_PLAY)) {
        if (song_playing()) { song_play(0); elapsed_frozen = x->now - play_started; }
        else { song_play(1); play_started = x->now; last_step = 0; bars_played = 0; }
        x->redraw = 1;
    }
    if (b & LB(LB_REC)) {
        if (recording) learn_notice("HOME STOPS THE RECORD", 800);
        else if (passed_once || (ghost_count && perf_count)) { replay_which = !replay_which; replay_shown = 1; learn_notice(replay_which ? "REPLAY: YOUR ORDER" : "REPLAY: GHOST ORDER", 800); uint8_t p = replay_which; lsyx_send(LCMD_REPLAY, &p, 1); }
        else record_start(x);
        x->redraw = 1;
    }
    if (b & LB(LB_HOME)) {
        if (recording) record_stop(x);
        else if (x->now - stop_at < 2000U && rec_count) { rec_count = 0; replay_shown = 0; learn_notice("RECORD DISCARDED", 900); }
        stop_at = x->now;
        if (song_playing()) { song_play(0); elapsed_frozen = x->now - play_started; }
        queued_slot = NONE; revert_transition(); lmidi_all_off(); x->redraw = 1;
    }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); ghost_count = 0; follow_pad = 0; send_path(); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(lesson_pick); lesson_load_ghost(); if (!ghost_count) learn_notice("NOT A SONG LESSON", 1000); }
        x->redraw = 1;
    }
    /* A TEXT step continues on any key; a new song step loads its ghost. */
    const struct lesson_step *ls = lesson_step();
    if (ls && ls->type == DR_TEXT && x->keys_down) { lesson_step_done(1, 100); lesson_load_ghost(); }
    else if (ls && ls->type == DR_SONG && !ghost_count) lesson_load_ghost();
    else if (!ls && ghost_count) { ghost_count = 0; follow_pad = 0; }

    /* Transport position: bars, beats and the loop boundary. */
    if (song_playing()) {
        unsigned step = song_step(), bar = step / 16, beat = (step / 4) % 4, wrapped = step < last_step;
        if (wrapped || bar != last_bar) {
            if (wrapped || bar > last_bar) ++bars_played;
            last_bar = bar;
            if (queued_slot < SEC_MAX && queue_mode != QM_BEAT && wrapped) { recall_slot(x, queued_slot); queued_slot = NONE; }
            else if (queued_slot < SEC_MAX && bar == loop_bars() - 1) apply_transition(x);
            else send_section(1);
            x->redraw = 1;
        }
        if (beat != last_beat) {
            last_beat = beat;
            if (queued_slot < SEC_MAX && queue_mode == QM_BEAT) { recall_slot(x, queued_slot); queued_slot = NONE; }
            send_songpos(x);
            x->redraw = 1;
        }
        last_step = step;
    }
    /* Follow-me for a song lesson: find a stored slot of the next ghost type. */
    if (ghost_count && ghost_pos < ghost_count && queued_slot == NONE) {
        unsigned want = ghost[ghost_pos], pad = 0;
        for (unsigned i = 0; i < SEC_MAX && !pad; ++i) if (sections[i].used && sections[i].type == want) pad = i + 1;
        unsigned lit = pad && (active_slot == NONE || bars_played + 2 >= LESSON_BARS || !song_playing()) ? pad : 0;
        unsigned blink = lit && bars_played + 1 >= LESSON_BARS;
        if (lit != follow_pad || blink != follow_blink) { follow_pad = (uint8_t)lit; follow_blink = (uint8_t)blink; send_path(); x->redraw = 1; }
        if (ghost_pos == 0 && perf_count == 0 && pad == 0) { strncpy(hint_line, "STORE A SLOT OF EACH TYPE", sizeof hint_line - 1); hint_until = x->now + 1500U; }
    }
    if (ghost_count && perf_count >= ghost_count && !passed_once && !recording) lesson_finish(x);
    /* Why-hints: a slot pad held two seconds. */
    for (unsigned i = 0; i < 8; ++i) if (pad_down[i] && !pad_hinted[i] && x->now - pad_press_at[i] >= 2000U) { pad_hinted[i] = 1; slot_hint(x, i); }
    if (hint_line[0] && (int32_t)(x->now - hint_until) >= 0) { hint_line[0] = 0; x->redraw = 1; }
    /* External notes play the melody track like the keys do. */
    uint8_t st, d1, d2;
    while (lmidi_pull_external(&st, &d1, &d2)) if ((st & 0xF0) == 0x90 || (st & 0xF0) == 0x80) x->redraw = 1;
}

uint32_t stage4_pad_colour(unsigned pad)
{
    if (pad >= 16) return P_OFF;
    uint32_t t = fm1_millis();
    if (pad < 8) {
        if (!sections[pad].used) return follow_pad == pad + 1 ? P_FULL(P_WHITE) : P_OFF;
        uint32_t c = type_pad[sections[pad].type];
        if (follow_pad == pad + 1) return (follow_blink && (t / 200U) & 1U) ? P_OFF : P_FULL(c);
        if (queued_slot == pad) return ((t / 250U) & 1U) ? P_FULL(c) : P_DIM(c);
        if (active_slot == pad) return (song_playing() && (song_beat() & 1U)) ? P_FULL(c) : c;
        return P_DIM(c);
    }
    unsigned slot = pad - 8;
    if (pad_down[pad]) return P_FULL(type_pad[store_type]);
    return sections[slot].used ? P_DIM(type_pad[sections[slot].type]) : 0x040404;
}

/* ---- drawing ---- */
static void draw_if_changed(char *shown, const char *text, int x, int y, unsigned scale, uint16_t fg, int clear_w, unsigned force)
{
    if (!force && !strcmp(shown, text)) return;
    draw_fill(x, y, clear_w, (int)(7 * scale), C_BG);
    draw_text(x, y, text, scale, fg, C_BG);
    strncpy(shown, text, 31); shown[31] = 0;
}

void stage4_draw(struct lctx *x)
{
    unsigned force = x->redraw;
    if (force) draw_clear();
    char line[40]; char *d;
    /* Header: page name, energy, tempo, record state, slow. */
    d = put_str(line, learn_is_fm1() ? "SCRIBE E" : "STAGE E", 9); d = put_num(d, energy); d = put_str(d, " ", 1); d = put_num(d, song.tempo); d = put_str(d, "BPM", 3);
    if (recording) d = put_str(d, " REC", 4);
    if (x->slow_percent < 100) { d = put_str(d, " S", 2); d = put_num(d, x->slow_percent); }
    draw_if_changed(shown_head, line, 4, 2, 1, recording ? C_RED : C_DIM, 200, force);
    draw_fill(228, 2, 8, 8, lsyx_partner_present() ? C_GREEN : C_PANEL);
    /* Big line: the playing section. */
    unsigned atype = active_slot < SEC_MAX ? sections[active_slot].type : mirror_slot < SEC_MAX ? mirror_type : ST_COUNT;
    draw_if_changed(shown_big, atype < ST_COUNT ? type_names[atype] : "LOOP", 4, 18, 4, atype < ST_COUNT ? type_screen[atype] : C_TEXT, 232, force);
    /* Row: bars played / length, next section and beats until it starts. */
    d = put_str(line, "BAR ", 4); d = put_num(d, bars_played + 1); d = put_str(d, "/", 1); d = put_num(d, loop_bars());
    if (queued_slot < SEC_MAX) {
        unsigned beats_left = queue_mode == QM_BEAT ? 1 : (loop_bars() * 4 - ((song_step() / 4) % (loop_bars() * 4)));
        d = put_str(d, " > ", 3); d = put_str(d, type_names[sections[queued_slot].type], 6); d = put_str(d, " IN ", 4); d = put_num(d, beats_left);
    }
    draw_if_changed(shown_row, line, 4, 50, 2, queued_slot < SEC_MAX ? C_ORANGE : C_CYAN, 232, force);
    /* Elapsed time. */
    unsigned ms = song_playing() || recording ? x->now - play_started : elapsed_frozen, secs = ms / 1000U;
    d = put_num(line, secs / 60); *d++ = ':'; *d++ = (char)('0' + (secs % 60) / 10); *d++ = (char)('0' + secs % 10); *d = 0;
    draw_if_changed(shown_time, line, 4, 72, 2, C_TEXT, 60, force);
    /* Thin song bar: the record so far as coloured blocks, the current section growing, play head. */
    {
        unsigned total = bars_played + 1, state;
        for (unsigned i = 0; i < rec_count; ++i) total += record[i].bars;
        state = total ^ (rec_count << 12) ^ (song_beat() << 20) ^ (atype << 24);
        if (force || state != shown_bar) {
            shown_bar = state;
            draw_fill(70, 74, 166, 10, C_PANEL);
            unsigned px = 0, scale = total ? 166U / total : 166U; if (scale < 2) scale = 2;
            for (unsigned i = 0; i < rec_count && px < 166; ++i) { unsigned w = record[i].bars * scale; if (px + w > 166) w = 166 - px; draw_fill(70 + (int)px, 75, (int)w, 8, type_screen[record[i].type]); px += w + 1; }
            unsigned w = (bars_played + 1) * scale; if (px + w > 166) w = 166 - px;
            if (atype < ST_COUNT) draw_fill(70 + (int)px, 75, (int)w, 8, type_screen[atype]);
            int head = 70 + (int)px + (int)(bars_played * scale + (song_beat() * scale) / 4); if (head > 234) head = 234;
            draw_fill(head, 74, 2, 10, C_TEXT);
        }
    }
    /* Slot strip: eight cells coloured by type, active framed, queued in orange text. */
    {
        unsigned state = 0;
        for (unsigned i = 0; i < 8; ++i) state |= (sections[i].used ? (sections[i].type + 1U) : 0U) << (i * 4);
        unsigned extra = (active_slot & 15U) | ((queued_slot & 15U) << 4) | ((follow_pad & 15U) << 8);
        uint32_t key = state ^ (extra << 20) ^ ((unsigned)store_type << 28);
        if (force || key != shown_slots) {
            shown_slots = key;
            for (unsigned p = 0; p < 8; ++p) {
                int cx = 4 + (int)p * 29, cy = 92;
                unsigned used = sections[p].used; uint16_t c = used ? type_screen[sections[p].type] : C_PANEL;
                unsigned hot = active_slot == p || follow_pad == p + 1;
                draw_fill(cx, cy, 27, 22, hot ? c : C_BG);
                draw_frame(cx, cy, 27, 22, queued_slot == p ? C_ORANGE : used ? c : C_PANEL);
                char lbl[4]; lbl[0] = (char)('1' + p); lbl[1] = used ? type_names[sections[p].type][0] : '-'; lbl[2] = 0;
                draw_text(cx + 7, cy + 7, lbl, 1, hot ? C_BG : used ? c : C_DIM, hot ? c : C_BG);
            }
            d = put_str(line, "STORE AS ", 9); put_str(d, type_names[store_type], 8);
            draw_fill(4, 118, 232, 8, C_BG); draw_text(4, 118, line, 1, type_screen[store_type], C_BG);
        }
    }
    /* Transition and energy meter. */
    d = put_str(line, "K5 ", 3); d = put_str(d, trans_names[transition], 9); d = put_str(d, "  ENERGY ", 9);
    for (unsigned i = 0; i < 8; ++i) *d++ = i < energy ? '#' : '.'; *d = 0;
    draw_if_changed(shown_trans, line, 4, 132, 1, C_AMBER, 232, force);
    d = put_str(line, learn_is_fm1() ? "GLO+KEY: SLOT  SAVE: STORE  SCL: TYPE" : "PAD: SLOT  SHIFT+PAD 9-16: STORE", 39); line[38] = 0;
    draw_text(4, 144, line, 1, C_PANEL, C_BG);
    /* Prompt: lesson prompt or the record hint. */
    {
        const struct lesson_step *ls = lesson_step();
        if (ls) { strncpy(line, ls->prompt ? ls->prompt : "", 31); line[31] = 0; }
        else if (recording) { d = put_str(line, "RECORDING ", 10); d = put_num(d, rec_count + 1); put_str(d, " SECTIONS  HOME: STOP", 21); }
        else { d = put_str(line, "REC: RECORD   ARP: LESSON ", 26); d = put_num(d, lesson_pick + 1); *d = 0; }
        draw_if_changed(shown_prompt, line, 4, 164, 1, recording ? C_RED : C_AMBER, 232, force);
    }
    if (lesson_running()) {
        const struct lesson *l = &lessons[lesson_running() - 1];
        d = put_str(line, l->title, 20); *d++ = ' '; d = put_num(d, lesson_progress() + 1); *d++ = '/'; put_num(d, l->step_count);
        draw_text(4, 176, line, 1, C_PURPLE, C_BG);
    } else if (force) draw_fill(4, 176, 232, 8, C_BG);
    /* Replay strips: ghost order above, performed below, mismatches red. */
    {
        unsigned state = ghost_count | (perf_count << 4) | (replay_shown << 8) | (replay_which << 9) | ((unsigned)ghost_pos << 10);
        if (force || state != shown_strip) {
            shown_strip = state;
            draw_fill(4, 188, 232, 18, C_BG);
            if (ghost_count) {
                for (unsigned i = 0; i < ghost_count; ++i) draw_fill(4 + (int)i * 28, 188, 24, 6, replay_shown && replay_which ? C_PANEL : type_screen[ghost[i]]);
                for (unsigned i = 0; i < perf_count; ++i) { unsigned ok = i < ghost_count && perf[i] == ghost[i]; draw_fill(4 + (int)i * 28, 198, 24, 6, replay_shown && !replay_which ? C_PANEL : ok ? (perf_late[i] ? C_YELLOW : C_GREEN) : C_RED); }
            }
        }
    }
    /* Hint and notice. */
    draw_if_changed(shown_hint, hint_line, 4, 212, 1, C_YELLOW, 232, force);
    const char *nt = learn_notice_text();
    draw_if_changed(shown_notice, nt ? nt : "", 4, 230, 1, C_TEXT, 232, force);
}
