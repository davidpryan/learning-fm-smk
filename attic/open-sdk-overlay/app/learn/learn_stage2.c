/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 2: Pulse (SMK-37) / Root (FM-1). Loop it.
 *
 * One page on both boards: a four-lane drum grid with Euclidean generators,
 * a chord loop (one chord per beat), a generated bass line and the engine's
 * own melody recorder, all through the shared song model (learn_song.h).
 *
 * Controls (logical, both boards):
 *   PLAY        start / stop the loop          HOME   stop everything
 *   REC tap     arm chord recording (click on) / after a take: replay raw vs snapped
 *   REC+PLAY    arm / finish the melody recorder (one pass of the loop)
 *   REC+SAVE    undo the last engine edit       SAVE   re-roll the lane (clears manual)
 *   SCL         next drum lane                 ENV    rhythm trainer start / stop
 *   FX          SMK: pads GRID <-> CHORDS      FM-1: keys CHORDS <-> MELODY
 *   ARP         lesson start / stop            EDIT   hold for the second knob row
 *   K1 hits  K2 shift  K3 swing*  K4 fill*  K5 bass rule  K6 tempo  K7 quantise
 *   EDIT+K1 patch  +K2 track  +K3 slow %  +K4 lesson  +K5 decay  +K6 bars  +K7 bass octave
 *   (* locked until the rhythm trainer is passed)
 *   SMK pads    GRID: 16 steps of the lane (SHIFT+pad 1-4 picks the lane)
 *               CHORDS: 1-7 triads, 8 rest, 9-15 sevenths, 16 rest; trainer: pad 1 taps
 *   SMK faders  Drums, Bass, Chords, Melody levels
 *   FM-1 keys   GLO + keys 1-16 = grid steps, GLO + keys 17-19 = bass rule;
 *               otherwise chord keys (white key = its degree) or melody keys */
#include "learn_song.h"
#include <string.h>

enum { PM_GRID, PM_CHORDS };            /* SMK pad mode / FM-1 key mode (PM_CHORDS = chord keys, PM_GRID = melody keys) */
enum { TS_IDLE, TS_COUNT_IN, TS_SCORING, TS_RESULT };
enum { TRK_DRUMS, TRK_BASS, TRK_CHORDS, TRK_MELODY };
enum { TR_MAX_STEPS = 64 };

static uint8_t lane_sel, pad_mode, track_sel;
static uint8_t chord_arm, rec_cursor, melody_rec, melody_wrapped, melody_start, melody_seen;
static uint8_t take_valid, replay_which, quant_strength = 80;
static uint8_t pad_down[16], rec_combo, needs_write;
static uint8_t patch_sel[4], lesson_pick, graduated;
static uint32_t hold_since, session_start;
static uint8_t hold_degree, hold_hinted, head_step, head_shown;
static char big[16], hint_line[40];
static uint32_t hint_until;
static struct groove_step raw_cells[SONG_STEPS], quant_cells[SONG_STEPS];

/* ---- rhythm trainer ---- */
static struct {
    uint8_t state, round, bars, lesson, offbeat, hot;
    uint16_t pattern;
    unsigned tempo, step_ms, window, count;    /* count = scored steps */
    uint32_t t0, due;
    int next_click;                            /* step index, count-in runs from -16 */
    unsigned hits, misses, taps;
    int last_err;
    uint8_t hit_flag[TR_MAX_STEPS];
} tr;

/* ---- drawn-state cache ---- */
static char shown_head[32], shown_big[16], shown_status[32], shown_prompt[32], shown_stats[32], shown_lesson[32], shown_hint[40], shown_notice[32];
static uint16_t shown_lane[LANE_COUNT], shown_bass, shown_mel;
static uint8_t shown_chords[4], shown_beat, shown_lane_sel;

static const uint8_t lane_part[LANE_COUNT] = {0, 1, 10, 6};     /* 808 parts: bd, sd, ch, cp */
static const uint8_t lane_decay[LANE_COUNT] = {2, 4, 2, 2};     /* "Decay" index in each part */
static const uint16_t lane_col[LANE_COUNT] = {C_RED, C_YELLOW, C_TEXT, C_PURPLE};
static const uint32_t lane_pad[LANE_COUNT] = {P_RED, P_YELLOW, P_WHITE, P_PURPLE};
static const char *const lane_name[LANE_COUNT] = {"KICK", "SNARE", "HAT", "PERC"};
static const char *const rule_name[BASS_RULES] = {"KICK-LOCK", "OFFBEAT", "WALK"};
static const uint8_t track_of[4] = {LT_DRUMS, LT_BASS, LT_CHORDS, LT_MELODY};
static const uint8_t bank_of[4] = {BANK_PIANO, BANK_BASS, BANK_EP, BANK_PLUCK};
static const char *const track_name[4] = {"DRUMS", "BASS", "CHORDS", "MELODY"};

/* ---- small helpers ---- */
static unsigned slow_ms(unsigned ms, const struct lctx *x) { return ms * 100U / (x->slow_percent ? x->slow_percent : 100); }
static unsigned sput(char *d, unsigned n, const char *s, unsigned cap) { while (*s && n + 1 < cap) d[n++] = *s++; d[n] = 0; return n; }
static unsigned nput(char *d, unsigned n, unsigned v, unsigned cap)
{
    char tmp[6]; unsigned t = 0;
    do { tmp[t++] = (char)('0' + v % 10); v /= 10; } while (v && t < 5);
    while (t && n + 1 < cap) d[n++] = tmp[--t];
    d[n] = 0; return n;
}
static void set_big(const char *label, unsigned value, unsigned show_value)
{
    unsigned n = sput(big, 0, label, sizeof big);
    if (show_value) { n = sput(big, n, " ", sizeof big); nput(big, n, value, sizeof big); }
}
static unsigned has_offbeat(uint16_t mask) { return (mask & 0xEEEE) != 0; }
static unsigned loop_beats(void) { return song.bars * 4; }

static void send_grid(unsigned lane)
{
    uint16_t m = song.lane[lane].mask;
    uint8_t p[4] = {(uint8_t)lane, (uint8_t)(m & 127), (uint8_t)((m >> 7) & 127), (uint8_t)((m >> 14) & 3)};
    lsyx_send(LCMD_GRID, p, 4);
}
static void send_log(void)
{
    uint8_t lg[3] = {(uint8_t)log_streak(), (uint8_t)(log_unlocked(UNLOCK_SWING) ? (log_unlocked(UNLOCK_FILL) ? 3 : 2) : 1), (uint8_t)(log_attempts() & 127)};
    lsyx_send(LCMD_LOG, lg, 3);
}
static void send_transport(unsigned what) { uint8_t p = (uint8_t)what; lsyx_send(LCMD_TRANSPORT, &p, 1); }

static void set_metronome(unsigned value)
{
    int delta = (int)value - (int)groove_sequence_setting(1);
    if (delta) groove_sequence_setting_edit(1, delta);
}
static void drums_changed(unsigned lane)
{
    song_write_drums();
    if (lane == LANE_KICK) song_write_bass();
    send_grid(lane);
}

/* ---- lesson helpers ---- */
static void groove_check(void)
{
    const struct lesson_step *ls = lesson_step();
    if (!ls || ls->type != DR_GROOVE) return;
    unsigned any = 0;
    for (unsigned l = 0; l < LANE_COUNT; ++l) {
        unsigned hits = ls->data[2 * l], shift = ls->data[2 * l + 1];
        if (!hits) continue;
        any = 1;
        if (song.lane[l].mask != song_euclid(hits, 16, shift)) return;
    }
    if (!any) return;
    learn_notice("GROOVE MATCHED!", 1200);
    lesson_step_done(1, 100);
}
static void rhythm_lesson_done(unsigned passed, unsigned pct)
{
    const struct lesson_step *ls = lesson_step();
    if (!ls || ls->type != DR_RHYTHM) return;
    /* The step's count is bars, not repetitions: one round settles the step. */
    unsigned reps = ls->count ? ls->count : 1;
    for (unsigned i = 0; i < reps && lesson_step() == ls; ++i) lesson_step_done(passed, pct);
}

/* ---- chord loop ---- */
static void chord_sound(struct lctx *x, unsigned degree, unsigned seventh, unsigned slot, unsigned velocity)
{
    uint8_t notes[4];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, seventh, song.chord_octave, notes);
    if (n) lmidi_chord_on(learn_track_channel(LT_CHORDS), notes, n, velocity, slot);
    uint8_t p[3] = {(uint8_t)degree, (uint8_t)seventh, (uint8_t)x->minor};
    lsyx_send(LCMD_HINT, p, 3);
}
static void chord_store(unsigned beat, unsigned degree, unsigned seventh)
{
    if (beat >= SONG_BEATS) return;
    song.chord[beat].degree = (uint8_t)degree; song.chord[beat].seventh = (uint8_t)(seventh & 1);
    song_write_chords(); song_write_bass();
    uint8_t p[5] = {(uint8_t)(beat / 4), (uint8_t)(beat % 4), (uint8_t)degree, (uint8_t)(seventh & 1), (uint8_t)song.minor};
    lsyx_send(LCMD_CHORD, p, 5);
}
/* A chord tap lands on the beat under the play head (late in the beat: the next one),
 * or at the write cursor when the loop is stopped and recording is armed. */
static void chord_tap(struct lctx *x, unsigned degree, unsigned seventh)
{
    unsigned beats = loop_beats();
    if (song_playing()) {
        unsigned s = song_step();
        chord_store((s / 4 + ((s % 4) >= 2)) % beats, degree, seventh);
    } else if (chord_arm) {
        chord_store(rec_cursor, degree, seventh);
        rec_cursor = (uint8_t)((rec_cursor + 1) % beats);
    } else return;
    x->redraw = 1;
}
static void why_hint(struct lctx *x, unsigned degree)
{
    uint8_t notes[4]; char nn[4];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, 0, 36 + 12 * song.bass_octave, notes);
    if (!n) return;
    unsigned k = sput(hint_line, 0, theory_roman(x->minor, degree, 0), sizeof hint_line);
    k = sput(hint_line, k, " -> BASS ", sizeof hint_line);
    k = sput(hint_line, k, theory_note_name(notes[0], nn), sizeof hint_line);
    k = sput(hint_line, k, ", ", sizeof hint_line);
    sput(hint_line, k, rule_name[song.bass_rule % BASS_RULES], sizeof hint_line);
    hint_until = x->now + 3000U; x->redraw = 1;
}
static void chord_press(struct lctx *x, unsigned degree, unsigned seventh, unsigned slot, unsigned velocity)
{
    if (degree) chord_sound(x, degree, seventh, slot, velocity);
    chord_tap(x, degree, seventh);
    hold_degree = (uint8_t)degree; hold_since = x->now; hold_hinted = 0;
}
static void chord_arm_set(struct lctx *x, unsigned on)
{
    chord_arm = (uint8_t)(on != 0);
    if (on) {
        rec_cursor = 0;
        set_metronome(2);
        if (!song_playing()) { song_play(1); send_transport(1); }
        learn_notice("TAP CHORDS ON THE CLICK", 1200);
    } else { set_metronome(1); learn_notice("CHORDS KEPT", 700); }
    uint8_t p = chord_arm; lsyx_send(LCMD_REC, &p, 1);
    x->redraw = 1;
}

/* ---- melody recorder ---- */
static void melody_arm(struct lctx *x, unsigned on)
{
    if (on) {
        if (!song_playing()) { song_play(1); send_transport(1); }
        song_record(1);
        melody_rec = 1; melody_wrapped = 0; melody_seen = 0; melody_start = (uint8_t)song_step();
        learn_notice("MELODY: PLAY ONE PASS", 1200);
        send_transport(2);
    } else {
        song_record(0); melody_rec = 0;
    }
    x->redraw = 1;
}
static void melody_finish(struct lctx *x)
{
    unsigned steps = song.bars * 16;
    song_record(0); melody_rec = 0;
    groove_sequence_read(LT_MELODY, 0, raw_cells, steps);
    song_quantise_track(LT_MELODY, quant_strength);
    groove_sequence_read(LT_MELODY, 0, quant_cells, steps);
    unsigned notes = 0; for (unsigned s = 0; s < steps; ++s) if (raw_cells[s].velocity) ++notes;
    take_valid = 1; replay_which = 0;
    log_add(LOGK_TAKE, notes, 0, song.tempo, x->slow_percent < 100, lesson_running(), quant_strength);
    learn_notice(notes ? "TAKE DONE. REC: RAW / SNAP" : "NO NOTES IN THAT PASS", 1500);
    send_log(); x->redraw = 1;
}
static void melody_requantise(void)
{
    unsigned steps = song.bars * 16;
    if (!take_valid) return;
    groove_sequence_replace(LT_MELODY, 0, raw_cells, steps);
    song_quantise_track(LT_MELODY, quant_strength);
    groove_sequence_read(LT_MELODY, 0, quant_cells, steps);
    if (replay_which) groove_sequence_replace(LT_MELODY, 0, raw_cells, steps);
}
static void replay_toggle(struct lctx *x)
{
    if (!take_valid) { learn_notice("NOTHING TO REPLAY YET", 800); return; }
    replay_which = !replay_which;
    groove_sequence_replace(LT_MELODY, 0, replay_which ? raw_cells : quant_cells, song.bars * 16);
    uint8_t p = replay_which; lsyx_send(LCMD_REPLAY, &p, 1);
    learn_notice(replay_which ? "REPLAY: RAW TAKE" : "REPLAY: SNAPPED", 800);
    x->redraw = 1;
}

/* ---- rhythm trainer ---- */
static void trainer_click(unsigned step)
{
    unsigned ch = learn_track_channel(LT_DRUMS);
    if ((step % 16) == 0) lmidi_note(ch, groove_drum_note(5), 120);        /* rim, bar accent */
    else if ((step % 4) == 0) lmidi_note(ch, groove_drum_note(5), 90);
    if (tr.round == 1 && step < tr.count && ((tr.pattern >> (step % 16)) & 1U)) lmidi_note(ch, groove_drum_note(10), 80); /* closed hat on the pattern */
}
static unsigned trainer_target(unsigned step)
{
    return tr.round == 0 ? (step % 4) == 0 : ((tr.pattern >> (step % 16)) & 1U);
}
static void trainer_round(struct lctx *x, unsigned round)
{
    tr.state = TS_COUNT_IN; tr.round = (uint8_t)round;
    tr.count = tr.bars * 16U; if (tr.count > TR_MAX_STEPS) tr.count = TR_MAX_STEPS;
    tr.step_ms = slow_ms(15000U / (tr.tempo ? tr.tempo : 100), x);
    tr.window = slow_ms(120, x);
    tr.t0 = x->now + 16U * tr.step_ms; tr.next_click = -16;
    tr.hits = tr.misses = tr.taps = 0; tr.last_err = 0; tr.hot = 0;
    memset(tr.hit_flag, 0, sizeof tr.hit_flag);
    tr.offbeat = (uint8_t)(round == 1 && has_offbeat(tr.pattern));
    set_big(round ? "PATTERN" : "ON BEAT", 0, 0);
    learn_notice(round ? "TAP THE PATTERN" : "TAP ON THE BEAT", 1200);
    uint8_t p[2] = {0, 0}; lsyx_send(LCMD_PATH, p, 2);
    x->redraw = 1;
}
static void trainer_start(struct lctx *x, const struct lesson_step *ls)
{
    if (song_playing()) { song_play(0); send_transport(0); }
    if (melody_rec) melody_arm(x, 0);
    tr.lesson = ls != NULL;
    if (ls) {
        tr.pattern = (uint16_t)(ls->data[0] | (ls->data[1] << 7) | ((ls->data[2] & 3) << 14));
        if (!tr.pattern) tr.pattern = 0x1111;
        tr.tempo = ls->tempo ? ls->tempo : song.tempo;
        tr.bars = (uint8_t)(ls->count && ls->count <= 4 ? ls->count : 4);
        trainer_round(x, 1);
    } else {
        tr.pattern = song.lane[LANE_KICK].mask ? song.lane[LANE_KICK].mask : 0x1111;
        tr.tempo = song.tempo; tr.bars = 4;
        trainer_round(x, 0);
    }
}
static void trainer_stop(struct lctx *x)
{
    if (tr.state == TS_IDLE) return;
    tr.state = TS_IDLE; tr.hot = 0;
    uint8_t p[2] = {2, 0}; lsyx_send(LCMD_PATH, p, 2);
    set_big(learn_is_fm1() ? "ROOT" : "PULSE", 0, 0);
    x->redraw = 1;
}
static void trainer_finish(struct lctx *x)
{
    for (unsigned s = 0; s < tr.count; ++s) if (trainer_target(s) && !tr.hit_flag[s]) ++tr.misses;
    unsigned total = tr.hits + tr.misses, pct = total ? tr.hits * 100U / total : 0;
    unsigned before = (log_unlocked(UNLOCK_SWING) ? 1U : 0U) | (log_unlocked(UNLOCK_FILL) ? 2U : 0U);
    log_add(LOGK_RHYTHM, tr.hits, tr.misses, tr.tempo, x->slow_percent < 100, lesson_running(), (pct & ~1U) | tr.offbeat);
    unsigned after = (log_unlocked(UNLOCK_SWING) ? 1U : 0U) | (log_unlocked(UNLOCK_FILL) ? 2U : 0U);
    if ((after & ~before) & 1U) { learn_notice("SWING UNLOCKED! TRY K3", 2000); uint8_t k = 2; lsyx_send(LCMD_UNLOCK, &k, 1); }
    else if ((after & ~before) & 2U) { learn_notice("FILL UNLOCKED! TRY K4", 2000); uint8_t k = 3; lsyx_send(LCMD_UNLOCK, &k, 1); }
    else learn_notice(pct >= 80 ? (x->slow_percent < 100 ? "PASSED (SLOW: NO UNLOCK)" : "PASSED!") : "KEEP GOING", 1500);
    send_log();
    set_big(pct >= 80 ? "PASS" : "MISS", pct, 1);
    tr.state = TS_RESULT; tr.due = x->now + 1500U; tr.hot = 0;
    if (tr.lesson) { rhythm_lesson_done(pct >= 80, pct); tr.round = 1; } /* lesson: one round */
    x->redraw = 1;
}
static void trainer_tap(struct lctx *x)
{
    if (tr.state != TS_SCORING) return;
    int rel = (int)(x->now - tr.t0);
    int near = (rel + (int)tr.step_ms / 2) / (int)tr.step_ms;
    int best = -1, best_err = 0;
    for (int s = near - 3; s <= near + 3; ++s) {
        if (s < 0 || s >= (int)tr.count || !trainer_target((unsigned)s) || tr.hit_flag[s]) continue;
        int err = rel - s * (int)tr.step_ms;
        if (best < 0 || (err < 0 ? -err : err) < (best_err < 0 ? -best_err : best_err)) { best = s; best_err = err; }
    }
    ++tr.taps;
    unsigned hit = best >= 0 && (unsigned)(best_err < 0 ? -best_err : best_err) <= tr.window;
    if (hit) { tr.hit_flag[best] = 1; ++tr.hits; } else ++tr.misses;
    tr.last_err = hit ? best_err : (best >= 0 ? best_err : 0);
    unsigned total = tr.hits + tr.misses, pct = total ? tr.hits * 100U / total : 0;
    int e = tr.last_err < -63 ? -63 : tr.last_err > 63 ? 63 : tr.last_err;
    uint8_t sc[2] = {(uint8_t)(e + 64), (uint8_t)pct}; lsyx_send(LCMD_SCORE, sc, 2);
    uint8_t ts[3] = {(uint8_t)(x->now & 127), (uint8_t)((x->now >> 7) & 127), (uint8_t)((x->now >> 14) & 127)}; lsyx_send(LCMD_TAP, ts, 3);
    if (!hit) set_big("MISS", 0, 0);
    else if (best_err > 15) set_big("LATE", (unsigned)best_err, 1);
    else if (best_err < -15) set_big("EARLY", (unsigned)-best_err, 1);
    else set_big("ON TIME", 0, 0);
    x->redraw = 1;
}
static void trainer_poll(struct lctx *x)
{
    if (tr.state == TS_IDLE) return;
    if (tr.state == TS_RESULT) {
        if ((int32_t)(x->now - tr.due) >= 0) {
            if (tr.round == 0) trainer_round(x, 1);
            else trainer_stop(x);
        }
        return;
    }
    /* Clicks on time; one per poll at most keeps the control task light. */
    int32_t rel = (int32_t)(x->now - tr.t0);
    if (rel >= tr.next_click * (int32_t)tr.step_ms && tr.next_click < (int)tr.count) {
        trainer_click(tr.next_click < 0 ? (unsigned)(tr.next_click + 16) : (unsigned)tr.next_click);
        ++tr.next_click;
        if (tr.next_click == 0 && tr.state == TS_COUNT_IN) { tr.state = TS_SCORING; x->redraw = 1; }
    }
    /* Follow-me: the tap pad is bright while a target is due. */
    unsigned hot = 0;
    if (tr.state == TS_SCORING) {
        int near = (rel + (int)tr.step_ms / 2) / (int)tr.step_ms;
        if (near >= 0 && near < (int)tr.count && trainer_target((unsigned)near)) {
            int err = rel - near * (int)tr.step_ms;
            hot = (unsigned)(err < 0 ? -err : err) <= tr.window / 2;
        }
    }
    if (hot != tr.hot) { tr.hot = (uint8_t)hot; uint8_t p[2] = {(uint8_t)(hot ? 0 : 2), 0}; lsyx_send(LCMD_PATH, p, 2); }
    if (tr.state == TS_SCORING && rel >= (int32_t)(tr.count * tr.step_ms + tr.window)) trainer_finish(x);
}

/* ---- knobs ---- */
static void lane_regen(struct lctx *x, unsigned lane)
{
    song.lane[lane].manual = 0; song_lane_generate(lane); drums_changed(lane); groove_check(); x->redraw = 1;
}
static void encoder_plain(struct lctx *x, unsigned id, int delta)
{
    struct song_lane *ln = &song.lane[lane_sel];
    switch (id) {
    case 0: { int v = (int)ln->hits + delta; ln->hits = (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v); set_big("HITS", ln->hits, 1); lane_regen(x, lane_sel); } break;
    case 1: { int v = (int)ln->shift + delta; ln->shift = (uint8_t)((v % 16 + 16) % 16); set_big("SHIFT", ln->shift, 1); lane_regen(x, lane_sel); } break;
    case 2:
        if (!log_unlocked(UNLOCK_SWING)) { learn_notice("LOCKED - PASS THE RHYTHM TRAINER", 1500); break; }
        song_set_swing((unsigned)((int)song.swing + delta < 50 ? 50 : (int)song.swing + delta)); set_big("SWING", song.swing, 1); break;
    case 3:
        if (!log_unlocked(UNLOCK_FILL)) { learn_notice("LOCKED - PASS THE RHYTHM TRAINER", 1500); break; }
        { static const uint8_t opts[4] = {0, 2, 4, 8}; unsigned i = 0; while (i < 3 && opts[i] != song.fill_every) ++i; i = (i + 4 + (delta > 0 ? 1 : 3)) % 4; song.fill_every = opts[i]; }
        set_big("FILL", song.fill_every, song.fill_every != 0); song_write_drums(); break;
    case 4: song.bass_rule = (song.bass_rule + BASS_RULES + (delta > 0 ? 1 : BASS_RULES - 1)) % BASS_RULES; song_write_bass(); set_big(rule_name[song.bass_rule], 0, 0); break;
    case 5: song_set_tempo((unsigned)((int)song.tempo + delta * 2 < 60 ? 60 : (int)song.tempo + delta * 2)); set_big("TEMPO", song.tempo, 1); break;
    case 6: { int v = (int)quant_strength + delta * 10; quant_strength = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); set_big("SNAP", quant_strength, 1); melody_requantise(); } break;
    default: break;
    }
    x->redraw = 1;
}
static void encoder_edit(struct lctx *x, unsigned id, int delta)
{
    switch (id) {
    case 0:
        if (track_sel == TRK_DRUMS) { learn_notice("DRUMS: EDIT+K5 SETS DECAY", 900); break; }
        patch_sel[track_sel] = (uint8_t)((patch_sel[track_sel] + 8 + (delta > 0 ? 1 : 7)) % 8);
        learn_bank_select(track_of[track_sel], bank_of[track_sel], patch_sel[track_sel]);
        set_big("PATCH", patch_sel[track_sel] + 1, 1); break;
    case 1: track_sel = (uint8_t)((track_sel + 4 + (delta > 0 ? 1 : 3)) % 4); set_big(track_name[track_sel], 0, 0); break;
    case 2: { int v = (int)x->slow_percent + delta * 5; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); uint8_t p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); set_big("SLOW", x->slow_percent, 1); } break;
    case 3: { int v = (int)lesson_pick + delta; lesson_pick = (uint8_t)(v < 0 ? 0 : v >= (int)lesson_count ? (int)lesson_count - 1 : v); learn_notice(lessons[lesson_pick].title, 1200); } break;
    case 4: groove_tone_edit(LT_DRUMS, lane_part[lane_sel], lane_decay[lane_sel], delta * 4); set_big("DECAY", 0, 0); break;
    case 5: song_set_bars(delta > 0 ? (song.bars == 1 ? 2 : 4) : (song.bars == 4 ? 2 : 1)); song_write_all(); take_valid = 0; set_big("BARS", song.bars, 1); break;
    case 6: { int v = (int)song.bass_octave + delta; song.bass_octave = (unsigned)(v < 0 ? 0 : v > 2 ? 2 : v); song_write_bass(); set_big("BASS OCT", song.bass_octave + 1, 1); } break;
    default: break;
    }
    x->redraw = 1;
}

/* ---- public ---- */
void stage2_init(void)
{
    song_init();
    lane_sel = LANE_KICK; track_sel = TRK_DRUMS; chord_arm = 0; melody_rec = 0; take_valid = 0; replay_which = 0;
    memset(pad_down, 0, sizeof pad_down); memset(&tr, 0, sizeof tr); memset(patch_sel, 0, sizeof patch_sel);
    hint_line[0] = 0; graduated = 0; session_start = 0; needs_write = 1; head_shown = 255; shown_beat = 255; hold_degree = 0;
#ifdef LEARN_BOARD_SMK37
    pad_mode = PM_GRID;
#else
    pad_mode = PM_CHORDS;
#endif
    set_big(learn_is_fm1() ? "ROOT" : "PULSE", 0, 0);
    shown_head[0] = shown_big[0] = shown_status[0] = shown_prompt[0] = shown_stats[0] = shown_lesson[0] = shown_hint[0] = shown_notice[0] = 0;
}

static void key_step_toggle(struct lctx *x, unsigned step)
{
    song_lane_toggle(lane_sel, step); drums_changed(lane_sel); groove_check(); x->redraw = 1;
}

void stage2_key(struct lctx *x, unsigned note, unsigned velocity)
{
    int k = (int)note - (LEARN_FIRST_NOTE + 12 * x->octave);
    unsigned ch = learn_track_channel(LT_MELODY);
    if (tr.state == TS_COUNT_IN || tr.state == TS_SCORING) {
#ifdef LEARN_BOARD_FM1
        if (velocity) trainer_tap(x);
        return;
#endif
    }
    if (!velocity) {
        lmidi_chord_off(17);
        lmidi_note(ch, note, 0);
        hold_degree = 0; x->redraw = 1; return;
    }
#ifdef LEARN_BOARD_FM1
    if (x->c.buttons & LB(LB_GLO)) {
        if (k >= 0 && k < 16) key_step_toggle(x, (unsigned)k);
        else if (k >= 16 && k < 19) { song.bass_rule = (unsigned)(k - 16); song_write_bass(); set_big(rule_name[song.bass_rule], 0, 0); x->redraw = 1; }
        return;
    }
#else
    (void)k;
#endif
    if (pad_mode == PM_CHORDS) {
        int d = theory_degree(x->root, x->scale, note);
        if (d) chord_press(x, (unsigned)d, 0, 17, velocity);
        x->redraw = 1; return;
    }
    lmidi_note(ch, note, velocity);
    x->redraw = 1;
}

void stage2_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    (void)pressure;
    if (pad >= 16 || velocity == 0xFFFF) return;
    if (!velocity) { pad_down[pad] = 0; lmidi_chord_off(pad + 1); if (hold_degree && pad_mode == PM_CHORDS) hold_degree = 0; x->redraw = 1; return; }
    pad_down[pad] = 1;
    if (tr.state == TS_COUNT_IN || tr.state == TS_SCORING) { if (pad == 0) trainer_tap(x); return; }
#ifdef LEARN_BOARD_SMK37
    extern unsigned smk37_shift(void);
    if (smk37_shift() && pad_mode == PM_GRID) { lane_sel = (uint8_t)(pad % LANE_COUNT); set_big(lane_name[lane_sel], 0, 0); x->redraw = 1; return; }
#endif
    if (pad_mode == PM_GRID) { key_step_toggle(x, pad); return; }
    if (pad == 7 || pad == 15) { chord_tap(x, 0, 0); learn_notice("REST", 400); return; }
    chord_press(x, (pad & 7) + 1, pad >= 8, pad + 1, velocity);
    x->redraw = 1;
}

void stage2_encoder(struct lctx *x, unsigned id, int delta)
{
    if (id > 6 || !delta) return;
    if (x->c.buttons & LB(LB_EDIT)) encoder_edit(x, id, delta); else encoder_plain(x, id, delta);
}

void stage2_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (id >= 4) return;
    unsigned track = track_of[id];
    struct groove_view v; groove_get_view(track, 0, &v);
    int delta = (int)(value & 127) - (int)v.volume;
    if (delta) groove_control_edit(track, 0, delta, 0);
    set_big(track_name[id], value & 127, 1);
    x->redraw = 1;
}

void stage2_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_GRID:
        if (n >= 4 && p[0] < LANE_COUNT) {
            song.lane[p[0]].mask = (uint16_t)(p[1] | (p[2] << 7) | ((p[3] & 3) << 14)); song.lane[p[0]].manual = 1;
            song_write_drums(); if (p[0] == LANE_KICK) song_write_bass(); x->redraw = 1;
        }
        break;
    case LCMD_CHORD:
        if (n >= 5) {
            unsigned beat = p[0] * 4U + p[1];
            if (beat < SONG_BEATS) { song.chord[beat].degree = p[2]; song.chord[beat].seventh = p[3] & 1; song_write_chords(); song_write_bass(); x->redraw = 1; }
        }
        break;
    case LCMD_REC:
        if (n >= 1 && (p[0] != 0) != chord_arm) { chord_arm = (uint8_t)(p[0] != 0); rec_cursor = 0; set_metronome(chord_arm ? 2 : 1); x->redraw = 1; }
        break;
    case LCMD_TRANSPORT:
        if (n >= 1) {
            if (p[0] == 0 && song_playing()) song_play(0);
            else if (p[0] == 1 && !song_playing()) song_play(1);
            else if (p[0] == 3) groove_edit_undo();
            x->redraw = 1;
        }
        break;
    case LCMD_UNLOCK:
        if (n >= 1) { if (p[0] == 2) log_unlock(UNLOCK_SWING); else if (p[0] == 3) log_unlock(UNLOCK_FILL); x->redraw = 1; }
        break;
    case LCMD_SCORE:
        if (n >= 2 && tr.state == TS_IDLE && learn_page() == 2) { int e = (int)p[0] - 64; set_big(e > 15 ? "LATE" : e < -15 ? "EARLY" : "ON TIME", (unsigned)(e < 0 ? -e : e), e > 15 || e < -15); x->redraw = 1; }
        break;
    case LCMD_LOG:
        if (n >= 3) { if (p[1] >= 2) log_unlock(UNLOCK_SWING); if (p[1] >= 3) log_unlock(UNLOCK_FILL); }
        break;
    default: break;
    }
}

void stage2_control(struct lctx *x)
{
    unsigned b = x->btn_down, held = x->c.buttons;
    if (needs_write) { needs_write = 0; song_apply_key(x->root, x->minor); song_write_all(); set_metronome(1); session_start = x->now; }
    if (song.root != x->root || song.minor != x->minor) { song_apply_key(x->root, x->minor); song_write_chords(); song_write_bass(); x->redraw = 1; }
    /* Buttons. REC is a modifier: a combo cancels the tap action on release. */
    const struct lesson_step *ls = lesson_step();
    if (b & LB(LB_REC)) rec_combo = 0;
    if (b & LB(LB_PLAY)) {
        if (ls && ls->type == DR_RHYTHM && tr.state == TS_IDLE && !(held & LB(LB_REC))) trainer_start(x, ls);
        else if (held & LB(LB_REC)) { rec_combo = 1; if (melody_rec) melody_finish(x); else melody_arm(x, 1); }
        else if (tr.state != TS_IDLE) trainer_stop(x);
        else if (song_playing()) { song_play(0); send_transport(0); if (melody_rec) melody_arm(x, 0); }
        else { song_play(1); send_transport(1); }
        x->redraw = 1;
    }
    if (b & LB(LB_SAVE)) {
        if (held & LB(LB_REC)) { rec_combo = 1; if (groove_edit_undo() == 0) { learn_notice("UNDONE", 700); take_valid = 0; send_transport(3); } else learn_notice("NOTHING TO UNDO", 700); }
        else { set_big(lane_name[lane_sel], 0, 0); lane_regen(x, lane_sel); learn_notice("LANE RE-ROLLED", 600); }
        x->redraw = 1;
    }
    if ((x->btn_up & LB(LB_REC)) && !rec_combo) {
        if (take_valid && pad_mode == PM_GRID && !chord_arm) replay_toggle(x);
        else chord_arm_set(x, !chord_arm);
    }
    if (b & LB(LB_SCL)) { lane_sel = (uint8_t)((lane_sel + 1) % LANE_COUNT); set_big(lane_name[lane_sel], 0, 0); x->redraw = 1; }
    if (b & LB(LB_ENV)) { if (tr.state != TS_IDLE) trainer_stop(x); else trainer_start(x, NULL); }
    if (b & LB(LB_FX)) {
        pad_mode = (uint8_t)(pad_mode == PM_GRID ? PM_CHORDS : PM_GRID);
        learn_notice(learn_is_fm1() ? (pad_mode == PM_CHORDS ? "KEYS: CHORDS" : "KEYS: MELODY") : (pad_mode == PM_CHORDS ? "PADS: CHORDS" : "PADS: GRID"), 900);
        x->redraw = 1;
    }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); trainer_stop(x); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(lesson_pick); const struct lesson_step *ls = lesson_step(); if (ls && ls->type == DR_RHYTHM) trainer_start(x, ls); else if (ls && ls->type == DR_GROOVE) groove_check(); }
    }
    if (b & LB(LB_HOME)) {
        trainer_stop(x); if (melody_rec) melody_arm(x, 0); if (chord_arm) chord_arm_set(x, 0);
        if (song_playing()) { song_play(0); send_transport(0); }
        lmidi_all_off(); memset(pad_down, 0, sizeof pad_down); hold_degree = 0; x->redraw = 1;
    }
    /* Lesson flow: TEXT steps continue on any press; RHYTHM steps run the trainer (PLAY). */
    ls = lesson_step();
    if (ls && ls->type == DR_TEXT && (x->keys_down || (b & ~(LB(LB_ARP) | LB(LB_SEQ))))) {
        lesson_step_done(1, 100); ls = lesson_step();
        if (ls && ls->type == DR_RHYTHM) trainer_start(x, ls);
        else if (ls && ls->type == DR_GROOVE) groove_check();
        else if (ls && ls->type != DR_TEXT) learn_notice("THIS STEP IS ON PAGE 1", 1200);
    }
    /* Melody pass: finish once the play head has come round to where it started. */
    if (melody_rec) {
        unsigned s = song_step();
        if (s < head_step) melody_wrapped = 1;
        if (melody_wrapped && s >= melody_start) melody_finish(x);
        else if (song_playing()) melody_seen = 1;       /* the transport word is published by the audio task a little later */
        else if (melody_seen) melody_arm(x, 0);
    }
    head_step = (uint8_t)song_step();
    /* Why-hint: a chord held two seconds while the loop runs names its bass note. */
    if (hold_degree && !hold_hinted && song_playing() && x->now - hold_since >= slow_ms(2000, x)) { hold_hinted = 1; why_hint(x, hold_degree); }
    if (hint_line[0] && (int32_t)(x->now - hint_until) >= 0) { hint_line[0] = 0; x->redraw = 1; }
    /* The partner's notes on the chord channel count as chord taps while armed. */
    uint8_t st, d1, d2;
    while (lmidi_pull_external(&st, &d1, &d2)) {
        if ((st & 0xF0) == 0x90 && d2 && (st & 15) == learn_track_channel(LT_CHORDS) && chord_arm) {
            int d = theory_degree(x->root, x->scale, d1);
            if (d) chord_tap(x, (unsigned)d, 0);
        }
    }
    trainer_poll(x);
    /* Graduation: every part of the loop in place at full tempo with a rhythm pass, inside five minutes. */
    if (!graduated && take_valid && x->slow_percent >= 100 && log_unlocked(UNLOCK_SWING) && x->now - session_start < 300000U) {
        unsigned chords = 0; for (unsigned i = 0; i < loop_beats(); ++i) if (song.chord[i].degree) ++chords;
        if (chords && song.lane[LANE_KICK].mask) { graduated = 1; log_add(LOGK_SONG, 1, 0, song.tempo, 0, lesson_running(), 100); learn_notice("GRADUATED: A WHOLE LOOP!", 3000); send_log(); }
    }
}

uint32_t stage2_pad_colour(unsigned pad)
{
    static const uint32_t colours[7] = {P_GREEN, P_ORANGE, P_CYAN, P_BLUE, P_RED, P_PURPLE, P_WHITE};
    if (pad >= 16) return P_OFF;
    if (tr.state == TS_COUNT_IN || tr.state == TS_SCORING) return pad == 0 ? (tr.hot ? P_FULL(P_RED) : P_RED) : P_OFF;
    if (pad_mode == PM_CHORDS) {
        if (pad == 7 || pad == 15) return pad_down[pad] ? P_FULL(P_WHITE) : P_DIM(P_WHITE);
        unsigned degree = (pad & 7) + 1, seventh = pad >= 8;
        uint32_t c = colours[theory_degree_colour(song.minor, degree)];
        if (seventh) c = P_DIM(c);
        if (pad_down[pad]) return P_FULL(c);
        if (song_playing()) { unsigned sv; int d = song_degree_at_step(head_step, &sv); if (d == (int)degree && sv == seventh) return P_FULL(c); }
        return c;
    }
    unsigned on = (song.lane[lane_sel].mask >> pad) & 1U, at_head = song_playing() && (head_step % 16) == pad;
    uint32_t c = lane_pad[lane_sel];
    if (on) return at_head ? P_FULL(c) : c;
    if (at_head) return P_DIM(c);
    return (pad % 4) == 0 ? P_DIM(P_WHITE) : P_OFF;
}

/* ---- drawing ---- */
enum { GX = 44, GW = 12, GH = 9, ROW_Y = 50, ROW_H = 12, ROW_BASS = 4, ROW_CHORD = 5, ROW_MEL = 6 };
static int row_y(unsigned row) { return ROW_Y + (int)row * ROW_H; }

static void draw_if_changed(char *shown, const char *text, int x, int y, unsigned scale, uint16_t fg, int clear_w, unsigned force)
{
    if (!force && !strcmp(shown, text)) return;
    draw_fill(x, y, clear_w, (int)(7 * scale), C_BG);
    draw_text(x, y, text, scale, fg, C_BG);
    strncpy(shown, text, 31); shown[31] = 0;
}
static void draw_cell(unsigned row, unsigned col, unsigned on, unsigned hot, uint16_t colour)
{
    int cx = GX + (int)col * GW, cy = row_y(row);
    uint16_t c = on ? colour : (col % 4 == 0 ? C_PANEL : C_BG);
    if (hot) { draw_fill(cx, cy, GW - 1, GH, on ? C_TEXT : C_DIM); if (on) draw_fill(cx + 2, cy + 2, GW - 5, GH - 4, colour); }
    else draw_fill(cx, cy, GW - 1, GH, c);
}
static void draw_chord_cell(unsigned beat, unsigned hot)
{
    int cx = GX + (int)beat * GW * 4, cy = row_y(ROW_CHORD);
    unsigned bar = head_step / 16, idx = (bar * 4 + beat) % loop_beats();
    unsigned d = song.chord[idx].degree;
    draw_fill(cx, cy, GW * 4 - 2, GH + 2, hot ? C_ORANGE : C_PANEL);
    draw_text(cx + 2, cy + 2, d ? theory_roman(song.minor, d, song.chord[idx].seventh) : "-", 1, hot ? C_BG : C_CYAN, hot ? C_ORANGE : C_PANEL);
}

void stage2_draw(struct lctx *x)
{
    unsigned force = x->redraw;
    if (force) draw_clear();
    unsigned playing = song_playing(), head = head_step, bar = head / 16, col = head % 16;
    /* Header: page, tempo, bars, key, swing/fill state. */
    {
        char line[32]; char nn[4]; unsigned n = sput(line, 0, learn_is_fm1() ? "ROOT " : "PULSE ", sizeof line);
        n = nput(line, n, song.tempo, sizeof line); n = sput(line, n, "BPM ", sizeof line);
        n = nput(line, n, song.bars, sizeof line); n = sput(line, n, "BAR ", sizeof line);
        n = sput(line, n, theory_note_name(x->root, nn), sizeof line); n = sput(line, n, x->minor ? "m" : "", sizeof line);
        if (song.swing > 50) { n = sput(line, n, " SW", sizeof line); n = nput(line, n, song.swing, sizeof line); }
        if (song.fill_every) { n = sput(line, n, " F", sizeof line); n = nput(line, n, song.fill_every, sizeof line); }
        if (x->slow_percent < 100) { n = sput(line, n, " S", sizeof line); nput(line, n, x->slow_percent, sizeof line); }
        draw_if_changed(shown_head, line, 4, 2, 1, C_DIM, 200, force);
        draw_fill(228, 2, 8, 8, lsyx_partner_present() ? C_GREEN : C_PANEL);
    }
    draw_if_changed(shown_big, big, 4, 18, 4, tr.state == TS_SCORING ? C_GREEN : C_TEXT, 232, force);
    /* Lane labels (selected lane bright). */
    if (force || shown_lane_sel != lane_sel) {
        shown_lane_sel = lane_sel;
        for (unsigned l = 0; l < LANE_COUNT; ++l) { draw_fill(4, row_y(l), 38, GH, C_BG); draw_text(4, row_y(l) + 1, lane_name[l], 1, l == lane_sel ? lane_col[l] : C_DIM, C_BG); }
        draw_text(4, row_y(ROW_BASS) + 1, "BASS", 1, C_BLUE, C_BG);
        draw_text(4, row_y(ROW_CHORD) + 2, "CHD", 1, C_CYAN, C_BG);
        draw_text(4, row_y(ROW_MEL) + 1, "MEL", 1, C_GREEN, C_BG);
    }
    /* Drum lanes from the model; bass and melody from the engine's step masks for the bar in view. */
    struct groove_view vb, vm; groove_get_view(LT_BASS, 0, &vb); groove_get_view(LT_MELODY, 0, &vm);
    uint16_t bass = (uint16_t)(vb.active_steps >> (bar * 16)), mel = (uint16_t)(vm.active_steps >> (bar * 16));
    unsigned head_moved = playing ? head != head_shown : head_shown != 255;
    unsigned hot_col = playing ? col : 255, old_col = head_shown == 255 ? 255 : head_shown % 16;
    for (unsigned l = 0; l < LANE_COUNT; ++l) {
        uint16_t m = song.lane[l].mask;
        if (force || m != shown_lane[l]) { shown_lane[l] = m; for (unsigned c = 0; c < 16; ++c) draw_cell(l, c, (m >> c) & 1U, c == hot_col, lane_col[l]); }
        else if (head_moved) { if (old_col < 16) draw_cell(l, old_col, (m >> old_col) & 1U, 0, lane_col[l]); if (hot_col < 16) draw_cell(l, hot_col, (m >> hot_col) & 1U, 1, lane_col[l]); }
    }
    if (force || bass != shown_bass) { shown_bass = bass; for (unsigned c = 0; c < 16; ++c) draw_cell(ROW_BASS, c, (bass >> c) & 1U, c == hot_col, C_BLUE); }
    else if (head_moved) { if (old_col < 16) draw_cell(ROW_BASS, old_col, (bass >> old_col) & 1U, 0, C_BLUE); if (hot_col < 16) draw_cell(ROW_BASS, hot_col, (bass >> hot_col) & 1U, 1, C_BLUE); }
    if (force || mel != shown_mel) { shown_mel = mel; for (unsigned c = 0; c < 16; ++c) draw_cell(ROW_MEL, c, (mel >> c) & 1U, c == hot_col, C_GREEN); }
    else if (head_moved) { if (old_col < 16) draw_cell(ROW_MEL, old_col, (mel >> old_col) & 1U, 0, C_GREEN); if (hot_col < 16) draw_cell(ROW_MEL, hot_col, (mel >> hot_col) & 1U, 1, C_GREEN); }
    {
        unsigned beat = playing ? col / 4 : 255, changed = force;
        for (unsigned bt = 0; bt < 4; ++bt) { unsigned idx = (bar * 4 + bt) % loop_beats(); uint8_t v = (uint8_t)((song.chord[idx].degree << 1) | song.chord[idx].seventh); if (v != shown_chords[bt]) { shown_chords[bt] = v; changed = 1; } }
        if (changed) for (unsigned bt = 0; bt < 4; ++bt) draw_chord_cell(bt, bt == beat);
        else if (beat != shown_beat) { if (shown_beat < 4) draw_chord_cell(shown_beat, 0); if (beat < 4) draw_chord_cell(beat, 1); }
        shown_beat = (uint8_t)beat;
    }
    head_shown = playing ? (uint8_t)head : 255;
    /* Status line: mode, transport, bass rule, recorder. */
    {
        char line[32]; unsigned n = 0;
        n = sput(line, n, learn_is_fm1() ? (pad_mode == PM_CHORDS ? "KEYS:CHD " : "KEYS:MEL ") : (pad_mode == PM_CHORDS ? "PADS:CHD " : "PADS:GRID "), sizeof line);
        n = sput(line, n, rule_name[song.bass_rule % BASS_RULES], sizeof line);
        if (chord_arm) n = sput(line, n, " CHD-REC", sizeof line);
        if (melody_rec) n = sput(line, n, " MEL-REC", sizeof line);
        else if (take_valid) n = sput(line, n, replay_which ? " RAW" : " SNAP", sizeof line);
        draw_if_changed(shown_status, line, 4, 140, 1, chord_arm || melody_rec ? C_RED : C_DIM, 232, force);
        draw_fill(4, 150, 232, 1, playing ? C_GREEN : C_PANEL);
    }
    /* Prompt: trainer, lesson step or idle help. */
    {
        char line[32] = ""; const struct lesson_step *ls = lesson_step();
        if (tr.state == TS_COUNT_IN) sput(line, 0, "COUNT-IN... GET READY", sizeof line);
        else if (tr.state == TS_SCORING) sput(line, 0, tr.round ? "TAP THE PATTERN" : "TAP ON THE BEAT", sizeof line);
        else if (tr.state == TS_RESULT) sput(line, 0, "ROUND DONE", sizeof line);
        else if (ls) { strncpy(line, ls->prompt ? ls->prompt : "", 31); line[31] = 0; }
        else { unsigned n = sput(line, 0, "ENV: TRAINER  ARP: LESSON ", sizeof line); nput(line, n, lesson_pick + 1, sizeof line); }
        draw_if_changed(shown_prompt, line, 4, 164, 1, tr.state == TS_SCORING ? C_GREEN : C_AMBER, 232, force);
    }
    /* Trainer stats: accuracy, hits, misses. */
    {
        char line[32] = "";
        if (tr.state != TS_IDLE) {
            unsigned total = tr.hits + tr.misses, pct = total ? tr.hits * 100U / total : 0;
            unsigned n = sput(line, 0, "ACC ", sizeof line); n = nput(line, n, pct, sizeof line); n = sput(line, n, "%  HIT ", sizeof line);
            n = nput(line, n, tr.hits, sizeof line); n = sput(line, n, " MISS ", sizeof line); n = nput(line, n, tr.misses, sizeof line);
            n = sput(line, n, "  ", sizeof line); nput(line, n, tr.tempo, sizeof line);
        } else if (!log_unlocked(UNLOCK_SWING)) sput(line, 0, "SWING+FILL LOCKED: PASS TRAINER", sizeof line);
        else if (!log_unlocked(UNLOCK_FILL)) sput(line, 0, "FILL LOCKED: TAP THE PATTERN", sizeof line);
        draw_if_changed(shown_stats, line, 4, 176, 1, tr.state != TS_IDLE ? C_TEXT : C_PANEL, 232, force);
    }
    /* Lesson title and progress. */
    {
        char line[32] = "";
        if (lesson_running()) {
            const struct lesson *l = &lessons[lesson_running() - 1];
            unsigned n = 0; for (const char *s = l->title; *s && n < 20; ++s) line[n++] = *s;
            line[n++] = ' '; line[n++] = (char)('0' + lesson_progress() + 1); line[n++] = '/'; line[n++] = (char)('0' + l->step_count); line[n] = 0;
        }
        draw_if_changed(shown_lesson, line, 4, 188, 1, C_PURPLE, 232, force);
    }
    draw_if_changed(shown_hint, hint_line, 4, 212, 1, C_YELLOW, 232, force);
    const char *nt = learn_notice_text();
    draw_if_changed(shown_notice, nt ? nt : "", 4, 230, 1, C_TEXT, 232, force);
}
