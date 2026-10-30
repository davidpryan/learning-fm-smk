/* SPDX-License-Identifier: GPL-3.0-only
 * Stage 2: Pulse (SMK-37 Elite) / Root (FM-1). Loop it.
 *
 * One page on both boards: a four-lane drum grid with Euclidean generators, a chord loop (a chord per beat),
 * a generated bass line and SLOOP's own melody recorder, all through the shared song model (song.c).
 *
 * Controls (both boards):
 *   PLAY        start / stop the loop          HOME   stop everything
 *   REC tap     arm chord recording (click on) / after a take: replay raw vs snapped
 *   REC+PLAY    arm / finish the melody recorder (one pass of the loop)
 *   REC+SAVE    undo the last take             SAVE   re-roll the lane (clears manual)
 *   SCL         next drum lane                 ENV    rhythm trainer start / stop
 *   FX          SMK: pads GRID <-> CHORDS      FM-1: keys CHORDS <-> MELODY
 *   ARP         lesson start / stop            EDIT   hold for the second knob row
 *   K1 hits  K2 shift  K3 swing*  K4 fill*  K5 bass rule  K6 tempo  K7 snap
 *   EDIT+K1 patch  +K2 track  +K3 slow %  +K4 lesson  +K5 perc sound  +K6 bars  +K7 bass octave
 *   (* locked until the rhythm trainer is passed)
 *   SMK pads    GRID: the 16 steps of the lane (GLO + pad 1-4 picks the lane)
 *               CHORDS: 1-7 triads, 8 rest, 9-15 sevenths, 16 rest; the trainer: pad 1 taps
 *   SMK faders  Melody, Chords, Bass, Drums levels
 *   FM-1 keys   GLO + keys 1-16 = the lane's steps, GLO + keys 17-19 = the bass rule;
 *               otherwise chord keys (a white key = its degree) or melody keys */

enum { PM_GRID, PM_CHORDS };                      /* SMK pad mode / FM-1 key mode (PM_GRID = melody keys there) */
enum { TS_IDLE, TS_COUNT_IN, TS_SCORING, TS_RESULT };
enum { S2T_MELODY, S2T_CHORDS, S2T_BASS, S2T_DRUMS };
enum { TR_MAX_STEPS = 64 };

static uint8_t s2_lane, s2_pad_mode, s2_track;
static uint8_t s2_chord_arm, s2_rec_cursor, s2_mel_rec, s2_mel_wrapped, s2_mel_start, s2_mel_seen;
static uint8_t s2_take_valid, s2_replay_which, s2_quant = 80;
static uint8_t s2_pad_down[16], s2_rec_combo, s2_needs_write;
static uint8_t s2_patch[4], s2_lesson_pick, s2_graduated;
static uint32_t s2_hold_since, s2_session_start;
static uint8_t s2_hold_degree, s2_hold_hinted, s2_head;
static char s2_big[16], s2_hint[40];
static uint32_t s2_hint_until;
static step_t s2_raw[SONG_STEPS], s2_quant_cells[SONG_STEPS], s2_undo[SONG_STEPS];
static uint8_t s2_undo_valid;

static struct {                                   /* the rhythm trainer */
    uint8_t state, round, bars, lesson, offbeat, hot;
    uint16_t pattern;
    unsigned tempo, step_ms, window, count;
    uint32_t t0, due;
    int next_click;
    unsigned hits, misses, taps;
    int last_err;
    uint8_t hit_flag[TR_MAX_STEPS];
} tr;

static const uint16_t LANE_COL[LANE_COUNT] = {LC_RED, LC_YELLOW, LC_TEXT, LC_PURPLE};
static const uint32_t LANE_PAD[LANE_COUNT] = {P_RED, P_YELLOW, P_WHITE, P_PURPLE};
static const char *const LANE_NM[LANE_COUNT] = {"KICK", "SNARE", "HAT", "PERC"};
static const char *const RULE_NM[BASS_RULES] = {"KICK-LOCK", "OFFBEAT", "WALK"};
static const uint8_t S2_TRACK[4] = {LT_KEYS, LT_CHORDS, LT_BASS, LT_DRUMS};
static const char *const S2_TRACK_NM[4] = {"MELODY", "CHORDS", "BASS", "DRUMS"};
static const uint8_t PERC_LANES[5] = {DL_CLAP, DL_RIM, DL_SHAKER, DL_COWBELL, DL_OPEN};

static void s2_set_big(const char *label, unsigned value, unsigned show_value)
{
    unsigned n = ls_cat(s2_big, 0, sizeof s2_big, label);
    if (show_value) { n = ls_cat(s2_big, n, sizeof s2_big, " "); ls_num(s2_big, n, sizeof s2_big, value); }
}
static unsigned has_offbeat(uint16_t mask) { return (mask & 0xEEEEu) != 0u; }

static void send_grid(unsigned lane)
{
    uint16_t m = lsong.lane[lane].mask;
    uint8_t p[4];
    p[0] = (uint8_t)lane; p[1] = (uint8_t)(m & 127u); p[2] = (uint8_t)((m >> 7) & 127u); p[3] = (uint8_t)((m >> 14) & 3u);
    lsyx_send(LCMD_GRID, p, 4);
}
static void send_log(void)
{
    uint8_t lg[3];
    lg[0] = (uint8_t)log_streak();
    lg[1] = (uint8_t)(log_unlocked(UNLOCK_SWING) ? (log_unlocked(UNLOCK_FILL) ? 3 : 2) : 1);
    lg[2] = (uint8_t)(log_attempts() & 127u);
    lsyx_send(LCMD_LOG, lg, 3);
}
static void send_transport(unsigned what) { uint8_t p = (uint8_t)what; lsyx_send(LCMD_TRANSPORT, &p, 1); }
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
    unsigned any = 0, l;
    if (!ls || ls->type != DR_GROOVE) return;
    for (l = 0; l < LANE_COUNT; l++) {
        unsigned hits = ls->data[2 * l], shift = ls->data[2 * l + 1];
        if (!hits) continue;
        any = 1;
        if (lsong.lane[l].mask != song_euclid(hits, 16, shift)) return;
    }
    if (!any) return;
    learn_notice("GROOVE MATCHED!", 1200);
    lesson_step_done(1, 100);
}
static void rhythm_lesson_done(unsigned passed, unsigned pct)
{
    const struct lesson_step *ls = lesson_step();
    unsigned reps, i;
    if (!ls || ls->type != DR_RHYTHM) return;
    reps = ls->count ? ls->count : 1u;            /* (the step's count is bars: one round settles it) */
    for (i = 0; i < reps && lesson_step() == ls; i++) lesson_step_done(passed, pct);
}

/* ---- the chord loop ---- */
static void chord_sound(struct lctx *x, unsigned degree, unsigned seventh, unsigned slot, unsigned velocity)
{
    uint8_t notes[4], p[3];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, seventh, lsong.chord_octave, notes);
    if (n) lmidi_chord_on(LT_CHORDS, notes, n, velocity, slot);
    p[0] = (uint8_t)degree; p[1] = (uint8_t)seventh; p[2] = (uint8_t)x->minor;
    lsyx_send(LCMD_HINT, p, 3);
}
static void chord_store(unsigned beat, unsigned degree, unsigned seventh)
{
    uint8_t p[5];
    if (beat >= SONG_BEATS) return;
    lsong.chord[beat].degree = (uint8_t)degree; lsong.chord[beat].seventh = (uint8_t)(seventh & 1u);
    song_write_chords(); song_write_bass();
    p[0] = (uint8_t)(beat / 4u); p[1] = (uint8_t)(beat % 4u); p[2] = (uint8_t)degree; p[3] = (uint8_t)(seventh & 1u); p[4] = (uint8_t)lsong.minor;
    lsyx_send(LCMD_CHORD, p, 5);
}
/* a chord tap lands on the beat under the play head (late in the beat: the next), or at the write cursor when
 * the loop is stopped and recording is armed */
static void chord_tap(struct lctx *x, unsigned degree, unsigned seventh)
{
    unsigned beats = song_beats();
    if (song_playing()) {
        unsigned s = song_step();
        chord_store((s / 4u + ((s % 4u) >= 2u)) % beats, degree, seventh);
    } else if (s2_chord_arm) {
        chord_store(s2_rec_cursor, degree, seventh);
        s2_rec_cursor = (uint8_t)((s2_rec_cursor + 1u) % beats);
    } else return;
    x->redraw = 1;
}
static void why_hint(struct lctx *x, unsigned degree)
{
    uint8_t notes[4]; char nn[4]; unsigned k;
    unsigned n = theory_degree_chord(x->root, x->minor, degree, 0, 36u + 12u * lsong.bass_octave, notes);
    if (!n) return;
    k = ls_cat(s2_hint, 0, sizeof s2_hint, theory_roman(x->minor, degree, 0));
    k = ls_cat(s2_hint, k, sizeof s2_hint, " -> BASS ");
    k = ls_cat(s2_hint, k, sizeof s2_hint, theory_note_name(notes[0], nn));
    k = ls_cat(s2_hint, k, sizeof s2_hint, ", ");
    ls_cat(s2_hint, k, sizeof s2_hint, RULE_NM[lsong.bass_rule % BASS_RULES]);
    s2_hint_until = x->now + 3000u; x->redraw = 1;
}
static void chord_press(struct lctx *x, unsigned degree, unsigned seventh, unsigned slot, unsigned velocity)
{
    if (degree) chord_sound(x, degree, seventh, slot, velocity);
    chord_tap(x, degree, seventh);
    s2_hold_degree = (uint8_t)degree; s2_hold_since = x->now; s2_hold_hinted = 0;
}
static void chord_arm_set(struct lctx *x, unsigned on)
{
    uint8_t p;
    s2_chord_arm = (uint8_t)(on != 0u);
    if (on) {
        s2_rec_cursor = 0;
        song_set_click(2);
        if (!song_playing()) { song_play(1); send_transport(1); }
        learn_notice("TAP CHORDS ON THE CLICK", 1200);
    } else { song_set_click(1); learn_notice("CHORDS KEPT", 700); }
    p = s2_chord_arm; lsyx_send(LCMD_REC, &p, 1);
    x->redraw = 1;
}

/* ---- the melody recorder ---- */
static void melody_arm(struct lctx *x, unsigned on)
{
    if (on) {
        memcpy(s2_undo, trk[LT_KEYS].step, sizeof s2_undo); s2_undo_valid = 1;
        if (!song_playing()) { song_play(1); send_transport(1); }
        song_record(1);
        s2_mel_rec = 1; s2_mel_wrapped = 0; s2_mel_seen = 0; s2_mel_start = (uint8_t)song_step();
        learn_notice("MELODY: PLAY ONE PASS", 1200);
        send_transport(2);
    } else {
        song_record(0); s2_mel_rec = 0;
    }
    x->redraw = 1;
}
static void melody_finish(struct lctx *x)
{
    unsigned notes;
    song_record(0); s2_mel_rec = 0;
    memcpy(s2_raw, trk[LT_KEYS].step, sizeof s2_raw);
    song_quantise_track(LT_KEYS, s2_quant);
    memcpy(s2_quant_cells, trk[LT_KEYS].step, sizeof s2_quant_cells);
    notes = song_track_notes(LT_KEYS, 0, 0);
    s2_take_valid = 1; s2_replay_which = 0;
    log_add(LOGK_TAKE, notes, 0, lsong.tempo, x->slow_percent < 100u, lesson_running(), s2_quant);
    learn_notice(notes ? "TAKE DONE. REC: RAW / SNAP" : "NO NOTES IN THAT PASS", 1500);
    send_log(); x->redraw = 1;
}
static void melody_requantise(void)
{
    if (!s2_take_valid) return;
    memcpy(trk[LT_KEYS].step, s2_raw, sizeof s2_raw);
    song_quantise_track(LT_KEYS, s2_quant);
    memcpy(s2_quant_cells, trk[LT_KEYS].step, sizeof s2_quant_cells);
    if (s2_replay_which) memcpy(trk[LT_KEYS].step, s2_raw, sizeof s2_raw);
}
static void replay_toggle(struct lctx *x)
{
    uint8_t p;
    if (!s2_take_valid) { learn_notice("NOTHING TO REPLAY YET", 800); return; }
    s2_replay_which = !s2_replay_which;
    memcpy(trk[LT_KEYS].step, s2_replay_which ? s2_raw : s2_quant_cells, sizeof s2_raw);
    p = s2_replay_which; lsyx_send(LCMD_REPLAY, &p, 1);
    learn_notice(s2_replay_which ? "REPLAY: RAW TAKE" : "REPLAY: SNAPPED", 800);
    x->redraw = 1;
}
static int take_undo(void)
{
    if (!s2_undo_valid) return -1;
    memcpy(trk[LT_KEYS].step, s2_undo, sizeof s2_undo);
    s2_undo_valid = 0;
    return 0;
}

/* ---- the rhythm trainer ---- */
static void trainer_click(unsigned step)
{
    if ((step % 16u) == 0u) lmidi_drum(DL_RIM, 120);   /* the bar's accent */
    else if ((step % 4u) == 0u) lmidi_drum(DL_RIM, 90);
    if (tr.round == 1u && step < tr.count && ((tr.pattern >> (step % 16u)) & 1u)) lmidi_drum(DL_HAT, 80);   /* the pattern */
}
static unsigned trainer_target(unsigned step) { return tr.round == 0u ? (step % 4u) == 0u : ((tr.pattern >> (step % 16u)) & 1u); }
static void trainer_round(struct lctx *x, unsigned round)
{
    uint8_t p[2];
    tr.state = TS_COUNT_IN; tr.round = (uint8_t)round;
    tr.count = tr.bars * 16u; if (tr.count > TR_MAX_STEPS) tr.count = TR_MAX_STEPS;
    tr.step_ms = slow_ms(15000u / (tr.tempo ? tr.tempo : 100u), x);
    tr.window = slow_ms(120, x);
    tr.t0 = x->now + 16u * tr.step_ms; tr.next_click = -16;
    tr.hits = tr.misses = tr.taps = 0; tr.last_err = 0; tr.hot = 0;
    memset(tr.hit_flag, 0, sizeof tr.hit_flag);
    tr.offbeat = (uint8_t)(round == 1u && has_offbeat(tr.pattern));
    s2_set_big(round ? "PATTERN" : "ON BEAT", 0, 0);
    learn_notice(round ? "TAP THE PATTERN" : "TAP ON THE BEAT", 1200);
    p[0] = 0; p[1] = 0; lsyx_send(LCMD_PATH, p, 2);
    x->redraw = 1;
}
static void trainer_start(struct lctx *x, const struct lesson_step *ls)
{
    if (song_playing()) { song_play(0); send_transport(0); }
    if (s2_mel_rec) melody_arm(x, 0);
    tr.lesson = ls != 0;
    if (ls) {
        tr.pattern = (uint16_t)(ls->data[0] | (ls->data[1] << 7) | ((ls->data[2] & 3u) << 14));
        if (!tr.pattern) tr.pattern = 0x1111;
        tr.tempo = ls->tempo ? ls->tempo : lsong.tempo;
        tr.bars = (uint8_t)(ls->count && ls->count <= 4u ? ls->count : 4u);
        trainer_round(x, 1);
    } else {
        tr.pattern = lsong.lane[LANE_KICK].mask ? lsong.lane[LANE_KICK].mask : 0x1111u;
        tr.tempo = lsong.tempo; tr.bars = 4;
        trainer_round(x, 0);
    }
}
static void trainer_stop(struct lctx *x)
{
    uint8_t p[2];
    if (tr.state == TS_IDLE) return;
    tr.state = TS_IDLE; tr.hot = 0;
    p[0] = 2; p[1] = 0; lsyx_send(LCMD_PATH, p, 2);
    s2_set_big(LEARN_PADS ? "PULSE" : "ROOT", 0, 0);
    x->redraw = 1;
}
static void trainer_finish(struct lctx *x)
{
    unsigned s, total, pct, before, after;
    uint8_t k;
    for (s = 0; s < tr.count; s++) if (trainer_target(s) && !tr.hit_flag[s]) tr.misses++;
    total = tr.hits + tr.misses; pct = total ? tr.hits * 100u / total : 0u;
    before = (log_unlocked(UNLOCK_SWING) ? 1u : 0u) | (log_unlocked(UNLOCK_FILL) ? 2u : 0u);
    log_add(LOGK_RHYTHM, tr.hits, tr.misses, tr.tempo, x->slow_percent < 100u, lesson_running(), (pct & ~1u) | tr.offbeat);
    after = (log_unlocked(UNLOCK_SWING) ? 1u : 0u) | (log_unlocked(UNLOCK_FILL) ? 2u : 0u);
    if ((after & ~before) & 1u) { learn_notice("SWING UNLOCKED! TRY K3", 2000); k = 2; lsyx_send(LCMD_UNLOCK, &k, 1); }
    else if ((after & ~before) & 2u) { learn_notice("FILL UNLOCKED! TRY K4", 2000); k = 3; lsyx_send(LCMD_UNLOCK, &k, 1); }
    else learn_notice(pct >= 80u ? (x->slow_percent < 100u ? "PASSED (SLOW: NO UNLOCK)" : "PASSED!") : "KEEP GOING", 1500);
    send_log();
    s2_set_big(pct >= 80u ? "PASS" : "MISS", pct, 1);
    tr.state = TS_RESULT; tr.due = x->now + 1500u; tr.hot = 0;
    if (tr.lesson) { rhythm_lesson_done(pct >= 80u, pct); tr.round = 1; }
    x->redraw = 1;
}
static void trainer_tap(struct lctx *x)
{
    int rel, near, best = -1, best_err = 0, s, e;
    unsigned hit, total, pct;
    uint8_t sc[2], ts[3];
    if (tr.state != TS_SCORING) return;
    rel = (int)(x->now - tr.t0);
    near = (rel + (int)tr.step_ms / 2) / (int)tr.step_ms;
    for (s = near - 3; s <= near + 3; s++) {
        int err;
        if (s < 0 || s >= (int)tr.count || !trainer_target((unsigned)s) || tr.hit_flag[s]) continue;
        err = rel - s * (int)tr.step_ms;
        if (best < 0 || (err < 0 ? -err : err) < (best_err < 0 ? -best_err : best_err)) { best = s; best_err = err; }
    }
    tr.taps++;
    hit = best >= 0 && (unsigned)(best_err < 0 ? -best_err : best_err) <= tr.window;
    if (hit) { tr.hit_flag[best] = 1; tr.hits++; } else tr.misses++;
    tr.last_err = hit ? best_err : (best >= 0 ? best_err : 0);
    total = tr.hits + tr.misses; pct = total ? tr.hits * 100u / total : 0u;
    e = tr.last_err < -63 ? -63 : tr.last_err > 63 ? 63 : tr.last_err;
    sc[0] = (uint8_t)(e + 64); sc[1] = (uint8_t)pct; lsyx_send(LCMD_SCORE, sc, 2);
    ts[0] = (uint8_t)(x->now & 127u); ts[1] = (uint8_t)((x->now >> 7) & 127u); ts[2] = (uint8_t)((x->now >> 14) & 127u); lsyx_send(LCMD_TAP, ts, 3);
    if (!hit) s2_set_big("MISS", 0, 0);
    else if (best_err > 15) s2_set_big("LATE", (unsigned)best_err, 1);
    else if (best_err < -15) s2_set_big("EARLY", (unsigned)-best_err, 1);
    else s2_set_big("ON TIME", 0, 0);
    x->redraw = 1;
}
static void trainer_poll(struct lctx *x)
{
    int32_t rel;
    unsigned hot = 0;
    if (tr.state == TS_IDLE) return;
    if (tr.state == TS_RESULT) {
        if ((int32_t)(x->now - tr.due) >= 0) { if (tr.round == 0u) trainer_round(x, 1); else trainer_stop(x); }
        return;
    }
    rel = (int32_t)(x->now - tr.t0);
    if (rel >= tr.next_click * (int32_t)tr.step_ms && tr.next_click < (int)tr.count) {   /* one click a poll at most */
        trainer_click(tr.next_click < 0 ? (unsigned)(tr.next_click + 16) : (unsigned)tr.next_click);
        tr.next_click++;
        if (tr.next_click == 0 && tr.state == TS_COUNT_IN) { tr.state = TS_SCORING; x->redraw = 1; }
    }
    if (tr.state == TS_SCORING) {                 /* follow-me: the tap pad is bright while a target is due */
        int near = (rel + (int)tr.step_ms / 2) / (int)tr.step_ms;
        if (near >= 0 && near < (int)tr.count && trainer_target((unsigned)near)) {
            int err = rel - near * (int)tr.step_ms;
            hot = (unsigned)(err < 0 ? -err : err) <= tr.window / 2u;
        }
    }
    if (hot != tr.hot) { uint8_t p[2]; tr.hot = (uint8_t)hot; p[0] = (uint8_t)(hot ? 0 : 2); p[1] = 0; lsyx_send(LCMD_PATH, p, 2); }
    if (tr.state == TS_SCORING && rel >= (int32_t)(tr.count * tr.step_ms + tr.window)) trainer_finish(x);
}

/* ---- knobs ---- */
static void lane_regen(struct lctx *x, unsigned lane)
{
    lsong.lane[lane].manual = 0; song_lane_generate(lane); drums_changed(lane); groove_check(); x->redraw = 1;
}
static void s2_encoder_plain(struct lctx *x, unsigned id, int delta)
{
    struct song_lane *ln = &lsong.lane[s2_lane];
    switch (id) {
    case 0: { int v = (int)ln->hits + delta; ln->hits = (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v); s2_set_big("HITS", ln->hits, 1); lane_regen(x, s2_lane); } break;
    case 1: { int v = (int)ln->shift + delta; ln->shift = (uint8_t)((v % 16 + 16) % 16); s2_set_big("SHIFT", ln->shift, 1); lane_regen(x, s2_lane); } break;
    case 2:
        if (!log_unlocked(UNLOCK_SWING)) { learn_notice("LOCKED - PASS THE RHYTHM TRAINER", 1500); break; }
        song_set_swing((unsigned)((int)lsong.swing + delta < 50 ? 50 : (int)lsong.swing + delta)); s2_set_big("SWING", lsong.swing, 1); break;
    case 3:
        if (!log_unlocked(UNLOCK_FILL)) { learn_notice("LOCKED - PASS THE RHYTHM TRAINER", 1500); break; }
        { static const uint8_t opts[4] = {0, 2, 4, 8}; unsigned i = 0; while (i < 3u && opts[i] != lsong.fill_every) i++; i = (i + 4u + (delta > 0 ? 1u : 3u)) % 4u; lsong.fill_every = opts[i]; }
        s2_set_big("FILL", lsong.fill_every, lsong.fill_every != 0u); song_write_drums(); break;
    case 4: lsong.bass_rule = (lsong.bass_rule + BASS_RULES + (delta > 0 ? 1u : BASS_RULES - 1u)) % BASS_RULES; song_write_bass(); s2_set_big(RULE_NM[lsong.bass_rule], 0, 0); break;
    case 5: song_set_tempo((unsigned)((int)lsong.tempo + delta * 2 < 60 ? 60 : (int)lsong.tempo + delta * 2)); s2_set_big("TEMPO", lsong.tempo, 1); break;
    case 6: { int v = (int)s2_quant + delta * 10; s2_quant = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); s2_set_big("SNAP", s2_quant, 1); melody_requantise(); } break;
    default: break;
    }
    x->redraw = 1;
}
static void s2_encoder_edit(struct lctx *x, unsigned id, int delta)
{
    switch (id) {
    case 0: {
        unsigned t = S2_TRACK[s2_track], n;
        if (s2_track == S2T_DRUMS) { learn_notice("DRUMS: EDIT+K5 PICKS THE PERC", 900); break; }
        n = learn_patch_count(trk[t].eng_req); if (!n) n = 1;
        s2_patch[s2_track] = (uint8_t)((s2_patch[s2_track] + n + (delta > 0 ? 1u : n - 1u)) % n);
        learn_bank_select(t, trk[t].eng_req, s2_patch[s2_track]);
        s2_set_big("PATCH", s2_patch[s2_track] + 1u, 1);
    } break;
    case 1: s2_track = (uint8_t)((s2_track + 4u + (delta > 0 ? 1u : 3u)) % 4u); s2_set_big(S2_TRACK_NM[s2_track], 0, 0); break;
    case 2: { int v = (int)x->slow_percent + delta * 5; uint8_t p; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); s2_set_big("SLOW", x->slow_percent, 1); } break;
    case 3: { int v = (int)s2_lesson_pick + delta; s2_lesson_pick = (uint8_t)(v < 0 ? 0 : v >= (int)lesson_count ? (int)lesson_count - 1 : v); learn_notice(lessons[s2_lesson_pick].title, 1200); } break;
    case 4: { unsigned i = 0; while (i < 4u && PERC_LANES[i] != lsong.perc_lane) i++; i = (i + 5u + (delta > 0 ? 1u : 4u)) % 5u; lsong.perc_lane = PERC_LANES[i]; song_write_drums(); s2_set_big(LANE_NAME[lsong.perc_lane], 0, 0); } break;
    case 5: song_set_bars(delta > 0 ? (lsong.bars == 1u ? 2u : 4u) : (lsong.bars == 4u ? 2u : 1u)); song_write_all(); song_track_setup(LT_KEYS); s2_take_valid = 0; s2_set_big("BARS", lsong.bars, 1); break;
    case 6: { int v = (int)lsong.bass_octave + delta; lsong.bass_octave = (unsigned)(v < 0 ? 0 : v > 2 ? 2 : v); song_write_bass(); s2_set_big("BASS OCT", lsong.bass_octave + 1u, 1); } break;
    default: break;
    }
    x->redraw = 1;
}

/* ---- public ---- */
static void stage2_init(void)
{
    song_init();
    s2_lane = LANE_KICK; s2_track = S2T_DRUMS; s2_chord_arm = 0; s2_mel_rec = 0; s2_take_valid = 0; s2_replay_which = 0; s2_undo_valid = 0;
    memset(s2_pad_down, 0, sizeof s2_pad_down); memset(&tr, 0, sizeof tr); memset(s2_patch, 0, sizeof s2_patch);
    s2_hint[0] = 0; s2_graduated = 0; s2_session_start = 0; s2_needs_write = 1; s2_hold_degree = 0; s2_head = 0;
    s2_pad_mode = LEARN_PADS ? PM_GRID : PM_CHORDS;
    s2_set_big(LEARN_PADS ? "PULSE" : "ROOT", 0, 0);
}

static void key_step_toggle(struct lctx *x, unsigned step)
{
    song_lane_toggle(s2_lane, step); drums_changed(s2_lane); groove_check(); x->redraw = 1;
}

static void stage2_key(struct lctx *x, unsigned note, unsigned velocity)
{
    int k = (int)note - (int)(53u + 12u * (unsigned)(x->octave + 2)) + 24;   /* key 0..26 from F3 (the octave off) */
    if (tr.state == TS_COUNT_IN || tr.state == TS_SCORING) {
        if (!LEARN_PADS) { if (velocity) trainer_tap(x); return; }
    }
    if (!velocity) {
        lmidi_chord_off(17);
        lmidi_note(LT_KEYS, note, 0);
        s2_hold_degree = 0; x->redraw = 1; return;
    }
    if (!LEARN_PADS && (x->buttons & LB(LB_GLO))) {
        if (k >= 0 && k < 16) key_step_toggle(x, (unsigned)k);
        else if (k >= 16 && k < 19) { lsong.bass_rule = (unsigned)(k - 16); song_write_bass(); s2_set_big(RULE_NM[lsong.bass_rule], 0, 0); x->redraw = 1; }
        return;
    }
    if (s2_pad_mode == PM_CHORDS) {
        int d = theory_degree(x->root, x->scale, note);
        if (d) chord_press(x, (unsigned)d, 0, 17, velocity);
        x->redraw = 1; return;
    }
    lmidi_note(LT_KEYS, note, velocity);
    x->redraw = 1;
}

static void stage2_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    (void)pressure;
    if (pad >= 16) return;
    if (!velocity) { s2_pad_down[pad] = 0; lmidi_chord_off(pad + 1u); if (s2_hold_degree && s2_pad_mode == PM_CHORDS) s2_hold_degree = 0; x->redraw = 1; return; }
    s2_pad_down[pad] = 1;
    if (tr.state == TS_COUNT_IN || tr.state == TS_SCORING) { if (pad == 0) trainer_tap(x); return; }
    if ((x->buttons & LB(LB_GLO)) && s2_pad_mode == PM_GRID) { s2_lane = (uint8_t)(pad % LANE_COUNT); s2_set_big(LANE_NM[s2_lane], 0, 0); x->redraw = 1; return; }
    if (s2_pad_mode == PM_GRID) { key_step_toggle(x, pad); return; }
    if (pad == 7 || pad == 15) { chord_tap(x, 0, 0); learn_notice("REST", 400); return; }
    chord_press(x, (pad & 7u) + 1u, pad >= 8u, pad + 1u, velocity);
    x->redraw = 1;
}

static void stage2_encoder(struct lctx *x, unsigned id, int delta)
{
    if (id > 6 || !delta) return;
    if (x->buttons & LB(LB_EDIT)) s2_encoder_edit(x, id, delta); else s2_encoder_plain(x, id, delta);
}

static void stage2_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (id >= 4) return;
    trk[S2_TRACK[id]].p[P_LEVEL] = (int16_t)(value & 127u);
    s2_set_big(S2_TRACK_NM[id], value & 127u, 1);
    x->redraw = 1;
}

static void stage2_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_GRID:
        if (n >= 4 && p[0] < LANE_COUNT) {
            lsong.lane[p[0]].mask = (uint16_t)(p[1] | (p[2] << 7) | ((p[3] & 3u) << 14)); lsong.lane[p[0]].manual = 1;
            song_write_drums(); if (p[0] == LANE_KICK) song_write_bass(); x->redraw = 1;
        }
        break;
    case LCMD_CHORD:
        if (n >= 5) {
            unsigned beat = p[0] * 4u + p[1];
            if (beat < SONG_BEATS) { lsong.chord[beat].degree = p[2]; lsong.chord[beat].seventh = p[3] & 1u; song_write_chords(); song_write_bass(); x->redraw = 1; }
        }
        break;
    case LCMD_REC:
        if (n >= 1 && (p[0] != 0u) != s2_chord_arm) { s2_chord_arm = (uint8_t)(p[0] != 0u); s2_rec_cursor = 0; song_set_click(s2_chord_arm ? 2u : 1u); x->redraw = 1; }
        break;
    case LCMD_TRANSPORT:
        if (n >= 1) {
            if (p[0] == 0 && song_playing()) song_play(0);
            else if (p[0] == 1 && !song_playing()) song_play(1);
            else if (p[0] == 3) take_undo();
            x->redraw = 1;
        }
        break;
    case LCMD_UNLOCK:
        if (n >= 1) { if (p[0] == 2) log_unlock(UNLOCK_SWING); else if (p[0] == 3) log_unlock(UNLOCK_FILL); x->redraw = 1; }
        break;
    case LCMD_SCORE:
        if (n >= 2 && tr.state == TS_IDLE && learn_page() == 2) { int e = (int)p[0] - 64; s2_set_big(e > 15 ? "LATE" : e < -15 ? "EARLY" : "ON TIME", (unsigned)(e < 0 ? -e : e), e > 15 || e < -15); x->redraw = 1; }
        break;
    case LCMD_LOG:
        if (n >= 3) { if (p[1] >= 2) log_unlock(UNLOCK_SWING); if (p[1] >= 3) log_unlock(UNLOCK_FILL); }
        break;
    default: break;
    }
}

static void stage2_control(struct lctx *x)
{
    unsigned b = x->btn_down, held = x->buttons;
    const struct lesson_step *ls = lesson_step();
    if (s2_needs_write) { s2_needs_write = 0; song_apply_key(x->root, x->minor); song_write_all(); song_set_click(1); s2_session_start = x->now; }
    if (lsong.root != x->root || lsong.minor != x->minor) { song_apply_key(x->root, x->minor); song_write_chords(); song_write_bass(); x->redraw = 1; }
    if (b & LB(LB_REC)) s2_rec_combo = 0;
    if (b & LB(LB_PLAY)) {
        if (ls && ls->type == DR_RHYTHM && tr.state == TS_IDLE && !(held & LB(LB_REC))) trainer_start(x, ls);
        else if (held & LB(LB_REC)) { s2_rec_combo = 1; if (s2_mel_rec) melody_finish(x); else melody_arm(x, 1); }
        else if (tr.state != TS_IDLE) trainer_stop(x);
        else if (song_playing()) { song_play(0); send_transport(0); if (s2_mel_rec) melody_arm(x, 0); }
        else { song_play(1); send_transport(1); }
        x->redraw = 1;
    }
    if (b & LB(LB_SAVE)) {
        if (held & LB(LB_REC)) { s2_rec_combo = 1; if (take_undo() == 0) { learn_notice("UNDONE", 700); s2_take_valid = 0; send_transport(3); } else learn_notice("NOTHING TO UNDO", 700); }
        else { s2_set_big(LANE_NM[s2_lane], 0, 0); lane_regen(x, s2_lane); learn_notice("LANE RE-ROLLED", 600); }
        x->redraw = 1;
    }
    if ((x->btn_up & LB(LB_REC)) && !s2_rec_combo) {
        if (s2_take_valid && s2_pad_mode == PM_GRID && !s2_chord_arm) replay_toggle(x);
        else chord_arm_set(x, !s2_chord_arm);
    }
    if (b & LB(LB_SCL)) { s2_lane = (uint8_t)((s2_lane + 1u) % LANE_COUNT); s2_set_big(LANE_NM[s2_lane], 0, 0); x->redraw = 1; }
    if (b & LB(LB_ENV)) { if (tr.state != TS_IDLE) trainer_stop(x); else trainer_start(x, 0); }
    if (b & LB(LB_FX)) {
        s2_pad_mode = (uint8_t)(s2_pad_mode == PM_GRID ? PM_CHORDS : PM_GRID);
        learn_notice(!LEARN_PADS ? (s2_pad_mode == PM_CHORDS ? "KEYS: CHORDS" : "KEYS: MELODY") : (s2_pad_mode == PM_CHORDS ? "PADS: CHORDS" : "PADS: GRID"), 900);
        x->redraw = 1;
    }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); trainer_stop(x); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(s2_lesson_pick); ls = lesson_step(); if (ls && ls->type == DR_RHYTHM) trainer_start(x, ls); else if (ls && ls->type == DR_GROOVE) groove_check(); }
    }
    if (b & LB(LB_HOME)) {
        trainer_stop(x); if (s2_mel_rec) melody_arm(x, 0); if (s2_chord_arm) chord_arm_set(x, 0);
        if (song_playing()) { song_play(0); send_transport(0); }
        lmidi_all_off(); memset(s2_pad_down, 0, sizeof s2_pad_down); s2_hold_degree = 0; x->redraw = 1;
    }
    ls = lesson_step();                           /* TEXT steps continue on any press; RHYTHM steps run the trainer */
    if (ls && ls->type == DR_TEXT && (x->keys_down || (b & ~(LB(LB_ARP) | LB(LB_SEQ))))) {
        lesson_step_done(1, 100); ls = lesson_step();
        if (ls && ls->type == DR_RHYTHM) trainer_start(x, ls);
        else if (ls && ls->type == DR_GROOVE) groove_check();
        else if (ls && ls->type != DR_TEXT) learn_notice("THIS STEP IS ON PAGE 1", 1200);
    }
    if (s2_mel_rec) {                             /* the pass ends when the head comes round to where it started */
        unsigned s = song_step();
        if (s < s2_head) s2_mel_wrapped = 1;
        if (s2_mel_wrapped && s >= s2_mel_start) melody_finish(x);
        else if (song_playing()) s2_mel_seen = 1;
        else if (s2_mel_seen) melody_arm(x, 0);
    }
    {
        unsigned h = song_playing() ? song_step() : 0u;
        if (h != s2_head) { s2_head = (uint8_t)h; x->redraw = 1; }
    }
    if (s2_hold_degree && !s2_hold_hinted && song_playing() && x->now - s2_hold_since >= slow_ms(2000, x)) { s2_hold_hinted = 1; why_hint(x, s2_hold_degree); }
    if (s2_hint[0] && (int32_t)(x->now - s2_hint_until) >= 0) { s2_hint[0] = 0; x->redraw = 1; }
    {   /* the partner's notes count as chord taps while armed */
        uint8_t st, d1, d2;
        while (lmidi_pull_external(&st, &d1, &d2))
            if (st == 0x90u && d2 && s2_chord_arm) { int d = theory_degree(x->root, x->scale, d1); if (d) chord_tap(x, (unsigned)d, 0); }
    }
    trainer_poll(x);
    /* graduation: every part of the loop in place at full tempo with a rhythm pass, inside five minutes */
    if (!s2_graduated && s2_take_valid && x->slow_percent >= 100u && log_unlocked(UNLOCK_SWING) && x->now - s2_session_start < 300000u) {
        unsigned chords = 0, i;
        for (i = 0; i < song_beats(); i++) if (lsong.chord[i].degree) chords++;
        if (chords && lsong.lane[LANE_KICK].mask) { s2_graduated = 1; log_add(LOGK_SONG, 1, 0, lsong.tempo, 0, lesson_running(), 100); log_unlock(UNLOCK_STAGE3); learn_notice(LEARN_STAGE > 2 ? "A WHOLE LOOP! SEQ: PAGE 3 IS READY" : "GRADUATED: A WHOLE LOOP!", 3000); send_log(); }
    }
    llit_keys = lbright_keys = 0;
}

static uint32_t stage2_pad_colour(unsigned pad)
{
    static const uint32_t colours[7] = {P_GREEN, P_ORANGE, P_CYAN, P_BLUE, P_RED, P_PURPLE, P_WHITE};
    unsigned on, at_head;
    uint32_t c;
    if (pad >= 16) return P_OFF;
    if (tr.state == TS_COUNT_IN || tr.state == TS_SCORING) return pad == 0 ? (tr.hot ? P_FULL(P_RED) : P_RED) : P_OFF;
    if (s2_pad_mode == PM_CHORDS) {
        unsigned degree = (pad & 7u) + 1u, seventh = pad >= 8u;
        if (pad == 7 || pad == 15) return s2_pad_down[pad] ? P_FULL(P_WHITE) : P_DIM(P_WHITE);
        c = colours[theory_degree_colour(lsong.minor, degree)];
        if (seventh) c = P_DIM(c);
        if (s2_pad_down[pad]) return P_FULL(c);
        if (song_playing()) { unsigned sv; int d = song_degree_at_step(s2_head, &sv); if (d == (int)degree && sv == seventh) return P_FULL(c); }
        return c;
    }
    on = (lsong.lane[s2_lane].mask >> pad) & 1u; at_head = song_playing() && (s2_head % 16u) == pad;
    c = LANE_PAD[s2_lane];
    if (on) return at_head ? P_FULL(c) : c;
    if (at_head) return P_DIM(c);
    return (pad % 4u) == 0u ? P_DIM(P_WHITE) : P_OFF;
}

/* ---- the screen ---- */
enum { GX = 50, GW = 11, GH = 10, ROW_Y = 56, ROW_H = 14, ROW_BASS = 4, ROW_CHORD = 5, ROW_MEL = 6 };
static int row_y(unsigned row) { return ROW_Y + (int)row * ROW_H; }
static void draw_cell(unsigned row, unsigned col, unsigned on, unsigned hot, uint16_t colour)
{
    int cx = GX + (int)col * GW, cy = row_y(row);
    uint16_t c = on ? colour : (col % 4u == 0u ? LC_PANEL : LC_BG);
    if (hot) { ld_fill(cx, cy, GW - 1, GH, on ? LC_TEXT : LC_DIM); if (on) ld_fill(cx + 2, cy + 2, GW - 5, GH - 4, colour); }
    else ld_fill(cx, cy, GW - 1, GH, c);
}
static void draw_chord_cell(unsigned beat, unsigned hot)
{
    int cx = GX + (int)beat * GW * 4, cy = row_y(ROW_CHORD) - 2;
    unsigned bar = s2_head / 16u, idx = (bar * 4u + beat) % song_beats();
    unsigned d = lsong.chord[idx].degree;
    ld_fill(cx, cy, GW * 4 - 2, 14, hot ? LC_ORANGE : LC_PANEL);
    ld_text(cx + 2, cy - 1, d ? theory_roman(lsong.minor, d, lsong.chord[idx].seventh) : "-", 0, hot ? LC_BG : LC_CYAN);
}

static void stage2_draw(struct lctx *x)
{
    unsigned playing = song_playing(), head = s2_head, bar = head / 16u, col = head % 16u, hot_col = playing ? col : 255u, l, c;
    char line[40], nn[4];
    unsigned n;
    uint32_t bass, mel;
    const struct lesson_step *ls = lesson_step();
    /* the header: page, tempo, bars, key, swing / fill, slow */
    n = ls_cat(line, 0, sizeof line, LEARN_PADS ? "PULSE " : "ROOT ");
    n = ls_num(line, n, sizeof line, lsong.tempo); n = ls_cat(line, n, sizeof line, "BPM ");
    n = ls_num(line, n, sizeof line, lsong.bars); n = ls_cat(line, n, sizeof line, "BAR ");
    n = ls_cat(line, n, sizeof line, theory_note_name(x->root, nn)); n = ls_cat(line, n, sizeof line, x->minor ? "m" : "");
    if (lsong.swing > 50u) { n = ls_cat(line, n, sizeof line, " SW"); n = ls_num(line, n, sizeof line, lsong.swing); }
    if (lsong.fill_every) { n = ls_cat(line, n, sizeof line, " F"); n = ls_num(line, n, sizeof line, lsong.fill_every); }
    if (x->slow_percent < 100u) { n = ls_cat(line, n, sizeof line, " S"); ls_num(line, n, sizeof line, x->slow_percent); }
    ld_text(4, 2, line, 0, LC_DIM);
    ld_fill(228, 6, 8, 8, lsyx_partner_present() ? LC_GREEN : LC_PANEL);
    ld_text(4, 20, s2_big, 1, tr.state == TS_SCORING ? LC_GREEN : LC_TEXT);
    /* the lanes */
    for (l = 0; l < LANE_COUNT; l++) {
        uint16_t m = lsong.lane[l].mask;
        ld_text(4, row_y(l) - 3, LANE_NM[l], 0, l == s2_lane ? LANE_COL[l] : LC_DIM);
        for (c = 0; c < 16u; c++) draw_cell(l, c, (m >> c) & 1u, c == hot_col, LANE_COL[l]);
    }
    song_track_notes(LT_BASS, &bass, bar);
    song_track_notes(LT_KEYS, &mel, bar);
    ld_text(4, row_y(ROW_BASS) - 3, "BASS", 0, LC_BLUE);
    for (c = 0; c < 16u; c++) draw_cell(ROW_BASS, c, (bass >> c) & 1u, c == hot_col, LC_BLUE);
    ld_text(4, row_y(ROW_CHORD) - 3, "CHD", 0, LC_CYAN);
    for (c = 0; c < 4u; c++) draw_chord_cell(c, playing && col / 4u == c);
    ld_text(4, row_y(ROW_MEL) - 3, "MEL", 0, LC_GREEN);
    for (c = 0; c < 16u; c++) draw_cell(ROW_MEL, c, (mel >> c) & 1u, c == hot_col, LC_GREEN);
    /* the status line: mode, bass rule, recorders */
    n = ls_cat(line, 0, sizeof line, !LEARN_PADS ? (s2_pad_mode == PM_CHORDS ? "KEYS:CHD " : "KEYS:MEL ") : (s2_pad_mode == PM_CHORDS ? "PADS:CHD " : "PADS:GRID "));
    n = ls_cat(line, n, sizeof line, RULE_NM[lsong.bass_rule % BASS_RULES]);
    if (s2_chord_arm) n = ls_cat(line, n, sizeof line, " CHD-REC");
    if (s2_mel_rec) n = ls_cat(line, n, sizeof line, " MEL-REC");
    else if (s2_take_valid) n = ls_cat(line, n, sizeof line, s2_replay_which ? " RAW" : " SNAP");
    ld_text(4, 156, line, 0, s2_chord_arm || s2_mel_rec ? LC_RED : LC_DIM);
    ld_fill(4, 173, 232, 1, playing ? LC_GREEN : LC_PANEL);
    /* the prompt: the trainer, the lesson step or the idle help */
    line[0] = 0;
    if (tr.state == TS_COUNT_IN) ls_cat(line, 0, sizeof line, "COUNT-IN... GET READY");
    else if (tr.state == TS_SCORING) ls_cat(line, 0, sizeof line, tr.round ? "TAP THE PATTERN" : "TAP ON THE BEAT");
    else if (tr.state == TS_RESULT) ls_cat(line, 0, sizeof line, "ROUND DONE");
    else if (ls) ls_cat(line, 0, sizeof line, ls->prompt ? ls->prompt : "");
    else { n = ls_cat(line, 0, sizeof line, "ENV: TRAINER  ARP: LESSON "); ls_num(line, n, sizeof line, s2_lesson_pick + 1u); }
    ld_text(4, 176, line, 0, tr.state == TS_SCORING ? LC_GREEN : LC_AMBER);
    /* the trainer's stats, or what is locked */
    line[0] = 0;
    if (tr.state != TS_IDLE) {
        unsigned total = tr.hits + tr.misses, pct = total ? tr.hits * 100u / total : 0u;
        n = ls_cat(line, 0, sizeof line, "ACC "); n = ls_num(line, n, sizeof line, pct); n = ls_cat(line, n, sizeof line, "%  HIT ");
        n = ls_num(line, n, sizeof line, tr.hits); n = ls_cat(line, n, sizeof line, " MISS "); n = ls_num(line, n, sizeof line, tr.misses);
        n = ls_cat(line, n, sizeof line, "  "); ls_num(line, n, sizeof line, tr.tempo);
    } else if (lesson_running()) {
        const struct lesson *ln = &lessons[lesson_running() - 1u];
        n = ls_cat(line, 0, sizeof line, ln->title); n = ls_cat(line, n, sizeof line, "  ");
        n = ls_num(line, n, sizeof line, lesson_progress() + 1u); n = ls_cat(line, n, sizeof line, "/"); ls_num(line, n, sizeof line, ln->step_count);
    } else if (!log_unlocked(UNLOCK_SWING)) ls_cat(line, 0, sizeof line, "SWING+FILL LOCKED: PASS TRAINER");
    else if (!log_unlocked(UNLOCK_FILL)) ls_cat(line, 0, sizeof line, "FILL LOCKED: TAP THE PATTERN");
    ld_text(4, 194, line, 0, tr.state != TS_IDLE ? LC_TEXT : lesson_running() ? LC_PURPLE : LC_PANEL);
    {
        const char *nt = learn_notice_text();
        if (nt) ld_text(4, 222, nt, 0, LC_TEXT);
        else if (s2_hint[0]) ld_text(4, 222, s2_hint, 0, LC_YELLOW);
    }
}
