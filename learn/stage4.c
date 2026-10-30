/* SPDX-License-Identifier: GPL-3.0-only
 * Stage 4: Stage (SMK-37 Elite) / Scribe (FM-1). Finish it: a whole song performed with the hands and captured in
 * one pass. The loop that pages 2-3 built (the song model plus the melody track) is stored into one of eight RAM
 * section slots, each with a type (Intro .. Free) and an energy (1-8: how much of the band plays). Slots are
 * queued from the pads (SMK) or GLO + keys (FM-1) and recalled at a loop boundary, with a transition edited into
 * the last bar before the change. REC keeps a record of the section order; a `song` lesson step supplies a ghost
 * order for follow-me and mistake replay. Nothing here touches Flash.
 *
 * Controls (both boards):
 *   SMK pads 1-8   queue a slot (tap: next loop, double-tap: next beat, hold: play while held, then back);
 *                  held 2 s: the why-hint
 *   SMK pads 9-16  GLO held: store the loop into slot 1-8; plain: cycle a stored slot's type
 *   FM-1 keys      GLO held: keys 1-8 queue slot 1-8, keys 9-16 store into 1-8
 *   SCL            the type the next store uses     SAVE   store into the cursor slot    PLAY  transport
 *   REC            the song record / replay ghost vs performed      HOME  stop (again within 2 s: discard)
 *   ARP            the picked lesson                ENV    next transition
 *   K1 energy  K2 store type  K3 lesson  K4 slow %  K5 transition  K6 tempo nudge +-10 %  K7 swing
 *   SMK faders     1 energy, 2 drums, 3 music (bass, chords), 4 melody */

enum { SEC_MAX = 8, REC_MAX = 32, GHOST_MAX = 8 };
enum { STY_INTRO, STY_VERSE, STY_PRE, STY_CHORUS, STY_BRIDGE, STY_BREAK, STY_OUTRO, STY_FREE, STY_COUNT };
enum { TRN_NONE, TRN_DROP, TRN_RISER, TRN_FILL, TRN_HANDOFF, TRN_COUNT };
enum { QM_BAR, QM_BEAT, QM_HOLD };
enum { S4_NONE = 0xFF, LESSON_BARS = 4 };

struct section {
    struct song_chord chord[SONG_BEATS];
    uint8_t bars, root, minor, tempo, bass_rule, bass_octave, swing, fill_every, perc_lane;
    struct song_lane lane[LANE_COUNT];
    step_t melody[SONG_STEPS];
    uint8_t used, type, energy;
};
struct rec_entry { uint8_t slot, type, bars, energy; };

static struct section s4_sec[SEC_MAX];
static uint8_t s4_active = S4_NONE, s4_queued = S4_NONE, s4_qmode, s4_hold_return = S4_NONE, s4_cursor;
static uint8_t s4_store_type = STY_VERSE, s4_trans = TRN_NONE, s4_energy = 7;
static uint8_t s4_trans_applied; static int16_t s4_rev_keep[2];
static unsigned s4_last_step, s4_bars_played, s4_last_bar, s4_last_beat;
static uint32_t s4_play_started, s4_elapsed;
static uint32_t s4_pad_at[16], s4_tap_at[8]; static uint8_t s4_pad_down[16], s4_pad_hinted[16];
static uint8_t s4_key_slot[16];
static struct rec_entry s4_rec[REC_MAX]; static unsigned s4_rec_n; static uint8_t s4_recording;
static uint32_t s4_stop_at;
static uint8_t s4_ghost[GHOST_MAX], s4_ghost_n, s4_ghost_pos, s4_perf[GHOST_MAX], s4_perf_n, s4_perf_late[GHOST_MAX];
static uint8_t s4_ghost_energy_hit[GHOST_MAX], s4_follow_pad, s4_follow_blink, s4_press_timing;
static uint8_t s4_replay_which, s4_replay_shown, s4_lesson_pick, s4_passed_once;
static uint8_t s4_mirror_slot = S4_NONE, s4_mirror_type;
static char s4_hint[40]; static uint32_t s4_hint_until;

static const char *const TYPE_NM[STY_COUNT] = {"INTRO", "VERSE", "PRE", "CHORUS", "BRIDGE", "BREAK", "OUTRO", "FREE"};
static const char *const TRANS_NM[TRN_COUNT] = {"NONE", "DROP", "RISER", "FILL", "HAND-OFF"};
static const uint32_t TYPE_PAD[STY_COUNT] = {0x404040, P_BLUE, 0x108050, P_ORANGE, P_PURPLE, P_WHITE, 0x404040, P_GREEN};
#define LC_TEAL 0x0410
static const uint16_t TYPE_RGB[STY_COUNT] = {LC_DIM, LC_BLUE, LC_TEAL, LC_ORANGE, LC_PURPLE, LC_TEXT, LC_DIM, LC_GREEN};
static const uint8_t TYPE_ENERGY[STY_COUNT] = {2, 4, 5, 8, 3, 1, 2, 4};
static const uint8_t ENERGY_ORDER[4] = {LT_DRUMS, LT_BASS, LT_CHORDS, LT_KEYS};   /* what plays at energy 1, 2, 3, 4+ */

static void apply_energy(unsigned e)
{
    unsigned i;
    if (e < 1) e = 1;
    if (e > 8) e = 8;
    s4_energy = (uint8_t)e;
    for (i = 0; i < 4u; i++) trk[ENERGY_ORDER[i]].p[P_MUTE] = (int16_t)(i >= e);
    for (i = 0; i < NPART; i++) trk[i].p[P_LEVEL] = (int16_t)(e >= 4u ? 96 + (e - 4u) * 8u : 96);   /* 5-8: louder */
}
static void send_section(unsigned state)
{
    uint8_t p[4];
    unsigned left = s4_active < SEC_MAX && s4_sec[s4_active].bars > s4_bars_played ? s4_sec[s4_active].bars - s4_bars_played : 0u;
    p[0] = (uint8_t)(s4_active < SEC_MAX ? s4_active : 127u); p[1] = (uint8_t)(s4_active < SEC_MAX ? s4_sec[s4_active].type : 0u);
    p[2] = (uint8_t)state; p[3] = (uint8_t)left;
    lsyx_send(LCMD_SECTION, p, 4);
}
static void send_songpos(const struct lctx *x)
{
    unsigned secs = (s4_recording || song_playing() ? x->now - s4_play_started : s4_elapsed) / 1000u;
    uint8_t p[4];
    p[0] = (uint8_t)(s4_bars_played & 127u); p[1] = (uint8_t)((song_step() / 4u) % 4u); p[2] = (uint8_t)(secs & 127u); p[3] = (uint8_t)((secs >> 7) & 127u);
    lsyx_send(LCMD_SONGPOS, p, 4);
}
static void send_path(void)
{
    uint8_t p[2]; p[0] = (uint8_t)(s4_follow_pad ? 0 : 2); p[1] = (uint8_t)(s4_follow_pad ? s4_follow_pad - 1u : 0u);
    lsyx_send(LCMD_PATH, p, 2);
}

/* ---- sections ---- */
static void store_slot(struct lctx *x, unsigned slot, unsigned type)
{
    struct section *s;
    char line[32]; unsigned n;
    if (slot >= SEC_MAX) return;
    s = &s4_sec[slot];
    memcpy(s->chord, lsong.chord, sizeof s->chord);
    s->bars = (uint8_t)lsong.bars; s->root = (uint8_t)lsong.root; s->minor = (uint8_t)lsong.minor; s->tempo = (uint8_t)lsong.tempo;
    s->bass_rule = (uint8_t)lsong.bass_rule; s->bass_octave = (uint8_t)lsong.bass_octave; s->swing = (uint8_t)lsong.swing;
    s->fill_every = (uint8_t)lsong.fill_every; s->perc_lane = lsong.perc_lane;
    memcpy(s->lane, lsong.lane, sizeof s->lane);
    memcpy(s->melody, trk[LT_KEYS].step, sizeof s->melody);
    s->used = 1; s->type = (uint8_t)(type % STY_COUNT); s->energy = s4_energy;
    s4_cursor = (uint8_t)slot;
    if (s4_active == S4_NONE) { s4_active = (uint8_t)slot; s4_bars_played = 0; }
    n = ls_cat(line, 0, sizeof line, "STORED "); n = ls_num(line, n, sizeof line, slot + 1u); n = ls_cat(line, n, sizeof line, " "); ls_cat(line, n, sizeof line, TYPE_NM[s->type]);
    learn_notice(line, 900);
    send_section(0);
    x->redraw = 1;
}
static void revert_transition(void)
{
    if (!s4_trans_applied) return;
    s4_trans_applied = 0;
    trk[LT_DRUMS].p[P_REV] = s4_rev_keep[0]; trk[LT_CHORDS].p[P_REV] = s4_rev_keep[1];
    apply_energy(s4_energy);                      /* (undoes a drop) */
}
/* the incoming section becomes the loop: the model, every renderer, the melody, tempo, energy, the key */
static void recall_slot(struct lctx *x, unsigned slot)
{
    const struct section *s;
    if (slot >= SEC_MAX || !s4_sec[slot].used) return;
    revert_transition();
    s = &s4_sec[slot];
    if (s4_recording && s4_active < SEC_MAX && s4_rec_n < REC_MAX) {
        s4_rec[s4_rec_n].slot = s4_active; s4_rec[s4_rec_n].type = s4_sec[s4_active].type;
        s4_rec[s4_rec_n].bars = (uint8_t)(s4_bars_played > 255u ? 255u : s4_bars_played); s4_rec[s4_rec_n].energy = s4_energy; s4_rec_n++;
    }
    memcpy(lsong.chord, s->chord, sizeof lsong.chord);
    lsong.bars = s->bars; lsong.root = s->root; lsong.minor = s->minor; lsong.tempo = s->tempo;
    lsong.bass_rule = s->bass_rule; lsong.bass_octave = s->bass_octave; lsong.swing = s->swing; lsong.fill_every = s->fill_every; lsong.perc_lane = s->perc_lane;
    memcpy(lsong.lane, s->lane, sizeof lsong.lane);
    song_write_all();
    song_track_setup(LT_KEYS);
    memcpy(trk[LT_KEYS].step, s->melody, sizeof s->melody);
    song_set_tempo(lsong.tempo);
    song_set_swing(lsong.swing);
    apply_energy(s->energy);
    s4_active = (uint8_t)slot; s4_bars_played = 0; s4_last_bar = 0; s4_cursor = (uint8_t)slot;
    x->root = lsong.root; x->minor = lsong.minor; x->scale = lsong.minor ? SCALE_MINOR : SCALE_MAJOR;
    if (s4_ghost_n && s4_perf_n < GHOST_MAX) {   /* a lesson: the performed order grows, the press is scored */
        s4_perf[s4_perf_n] = s->type; s4_perf_late[s4_perf_n] = s4_press_timing == 2u;
        s4_ghost_energy_hit[s4_perf_n] = s4_ghost_pos < s4_ghost_n && TYPE_ENERGY[s4_ghost[s4_ghost_pos]] == s->energy;
        s4_perf_n++; if (s4_ghost_pos < s4_ghost_n) s4_ghost_pos++;
        s4_press_timing = 0; s4_follow_pad = 0; s4_follow_blink = 0; send_path();
    }
    send_section(1);
    x->redraw = 1;
}
/* transitions: one-bar edits when the last bar before the switch begins; the recall that follows undoes them */
static void apply_transition(struct lctx *x)
{
    const struct section *in;
    unsigned steps = song_steps(), last = steps - 16u, s;
    uint8_t p;
    if (s4_trans_applied || s4_trans == TRN_NONE || s4_queued >= SEC_MAX) return;
    s4_trans_applied = 1;
    in = &s4_sec[s4_queued];
    s4_rev_keep[0] = trk[LT_DRUMS].p[P_REV]; s4_rev_keep[1] = trk[LT_CHORDS].p[P_REV];
    switch (s4_trans) {
    case TRN_DROP:
        trk[LT_DRUMS].p[P_MUTE] = 1; trk[LT_BASS].p[P_MUTE] = 1;
        break;
    case TRN_RISER:                               /* hats double, then every 16th; the reverb climbs */
        lsong.lane[LANE_HAT].mask |= 0x5555u; lsong.lane[LANE_HAT].mask |= 0xFF00u; lsong.lane[LANE_HAT].manual = 1;
        song_write_drums();
        trk[LT_DRUMS].p[P_REV] = (int16_t)(s4_rev_keep[0] + 40 > 127 ? 127 : s4_rev_keep[0] + 40);
        trk[LT_CHORDS].p[P_REV] = (int16_t)(s4_rev_keep[1] + 30 > 127 ? 127 : s4_rev_keep[1] + 30);
        break;
    case TRN_FILL:
        lsong.fill_every = 1; song_write_drums();
        break;
    default:                                      /* HAND-OFF: the melody fades through the last bar; the next chord comes a beat early */
        for (s = last; s < steps; s++) trk[LT_KEYS].step[s].vel = (uint8_t)(trk[LT_KEYS].step[s].vel * (16u - (s - last)) / 16u);
        if (in->chord[0].degree) { lsong.chord[song_beats() - 1u] = in->chord[0]; song_write_chords(); }
        break;
    }
    p = s4_trans; lsyx_send(LCMD_TRANSITION, &p, 1);
    (void)x;
}
static void queue_slot(struct lctx *x, unsigned slot, unsigned mode)
{
    uint8_t p[2];
    if (slot >= SEC_MAX || !s4_sec[slot].used) { learn_notice(LEARN_PADS ? "EMPTY: " LBL_GLO "+PAD 9-16 STORES" : "EMPTY: " LBL_GLO "+KEY 9-16 STORES", 900); return; }
    s4_queued = (uint8_t)slot; s4_qmode = (uint8_t)mode;
    p[0] = (uint8_t)slot; p[1] = (uint8_t)mode; lsyx_send(LCMD_QUEUE, p, 2);
    if (s4_ghost_n && s4_ghost_pos < s4_ghost_n) s4_press_timing = (uint8_t)(s4_follow_pad ? 1u : (s4_bars_played >= LESSON_BARS ? 2u : 1u));
    if (!song_playing()) { recall_slot(x, slot); s4_queued = S4_NONE; return; }
    if (lsong.bars == 1u || song_bar() == lsong.bars - 1u) apply_transition(x);
    x->redraw = 1;
}

/* ---- the record and the lesson ---- */
static void lesson_load_ghost(void)
{
    const struct lesson_step *ls = lesson_step();
    unsigned i;
    s4_ghost_n = 0; s4_ghost_pos = 0; s4_perf_n = 0; s4_passed_once = 0; s4_replay_shown = 0;
    if (!ls || ls->type != DR_SONG) return;
    for (i = 0; i < GHOST_MAX && ls->data[i]; i++) s4_ghost[s4_ghost_n++] = (uint8_t)((ls->data[i] - 1u) % STY_COUNT);
    if (ls->tempo) song_set_tempo(ls->tempo);
}
static unsigned lesson_score(void)
{
    unsigned order = 0, en = 0, i;
    if (!s4_ghost_n) return 0;
    for (i = 0; i < s4_ghost_n; i++) { if (i < s4_perf_n && s4_perf[i] == s4_ghost[i]) order++; if (i < s4_perf_n && s4_ghost_energy_hit[i]) en++; }
    return (order * 100u / s4_ghost_n + en * 100u / s4_ghost_n) / 2u;
}
static void lesson_finish(struct lctx *x)
{
    unsigned score, late = 0, i, passed;
    const struct lesson_step *ls = lesson_step();
    uint8_t lg[3];
    if (!s4_ghost_n) return;
    score = lesson_score();
    for (i = 0; i < s4_perf_n; i++) late += s4_perf_late[i];
    passed = score >= (ls && ls->target ? ls->target : 80u) && !late;
    log_add(LOGK_SONG, score, late, lsong.tempo, x->slow_percent < 100u, lesson_running(), score);
    lesson_step_done(passed, score);
    s4_passed_once = 1; s4_replay_which = 0; s4_replay_shown = 1;
    learn_notice(passed ? "SONG PASSED!" : "REC: COMPARE ORDERS", 1500);
    lg[0] = (uint8_t)log_streak(); lg[1] = 4; lg[2] = (uint8_t)(log_attempts() & 127u); lsyx_send(LCMD_LOG, lg, 3);
    s4_ghost_pos = s4_ghost_n; s4_follow_pad = 0; send_path();
    x->redraw = 1;
}
static void record_start(struct lctx *x)
{
    uint8_t p = 1;
    s4_rec_n = 0; s4_recording = 1; s4_play_started = x->now; s4_bars_played = 0;
    s4_perf_n = 0; s4_ghost_pos = 0; s4_replay_shown = 0;
    if (!song_playing()) song_play(1);
    learn_notice("SONG RECORD ON", 900);
    lsyx_send(LCMD_REC, &p, 1);
    x->redraw = 1;
}
static void record_stop(struct lctx *x)
{
    unsigned secs;
    uint8_t p = 0;
    if (!s4_recording) return;
    if (s4_active < SEC_MAX && s4_rec_n < REC_MAX) { s4_rec[s4_rec_n].slot = s4_active; s4_rec[s4_rec_n].type = s4_sec[s4_active].type; s4_rec[s4_rec_n].bars = (uint8_t)s4_bars_played; s4_rec[s4_rec_n].energy = s4_energy; s4_rec_n++; }
    s4_recording = 0; s4_elapsed = x->now - s4_play_started;
    secs = s4_elapsed / 1000u;
    log_add(LOGK_SONG, s4_rec_n, 0, lsong.tempo, x->slow_percent < 100u, lesson_running(), secs > 255u ? 255u : secs);
    lsyx_send(LCMD_REC, &p, 1);
    learn_notice("KEPT. " LBL_HOME " AGAIN: DISCARD", 2000);
    if (s4_ghost_n) lesson_finish(x);
    x->redraw = 1;
}

/* ---- why-hints ---- */
static void slot_hint(struct lctx *x, unsigned slot)
{
    unsigned n, b;
    if (slot >= SEC_MAX || !s4_sec[slot].used) ls_cat(s4_hint, 0, sizeof s4_hint, "EMPTY: STORE THE LOOP HERE");
    else {
        const struct section *s = &s4_sec[slot];
        n = ls_cat(s4_hint, 0, sizeof s4_hint, TYPE_NM[s->type]); n = ls_cat(s4_hint, n, sizeof s4_hint, " E"); n = ls_num(s4_hint, n, sizeof s4_hint, s->energy);
        n = ls_cat(s4_hint, n, sizeof s4_hint, " "); n = ls_num(s4_hint, n, sizeof s4_hint, s->bars); n = ls_cat(s4_hint, n, sizeof s4_hint, "B ");
        for (b = 0; b < 4u && n < 30u; b++) { if (b) n = ls_cat(s4_hint, n, sizeof s4_hint, "-"); n = ls_cat(s4_hint, n, sizeof s4_hint, theory_roman(s->minor, s->chord[b].degree ? s->chord[b].degree : 1u, s->chord[b].seventh)); }
    }
    s4_hint_until = x->now + 3000u; x->redraw = 1;
}
static const char *trans_hint(void)
{
    static const char *const h[TRN_COUNT] = {"NO EDIT BETWEEN SECTIONS", "DROP: DRUMS+BASS OUT, LAST BAR", "RISER: HATS DOUBLE, REVERB UP", "FILL: SNARE+HAT ROLL, LAST BAR", "HAND-OFF: MELODY FADES, CHORD EARLY"};
    return h[s4_trans % TRN_COUNT];
}

/* ---- slot presses (pads, GLO + keys) ---- */
static void slot_press(struct lctx *x, unsigned slot)
{
    unsigned dbl = x->now - s4_tap_at[slot] < 350u;
    s4_tap_at[slot] = x->now; s4_pad_at[slot] = x->now;
    s4_hold_return = s4_active;
    queue_slot(x, slot, dbl ? QM_BEAT : QM_BAR);
}
static void slot_release(struct lctx *x, unsigned slot)
{
    unsigned back = s4_hold_return; s4_hold_return = S4_NONE;
    if (x->now - s4_pad_at[slot] < 600u || !s4_sec[slot].used || back >= SEC_MAX || back == slot || !s4_sec[back].used) return;
    if (s4_queued == slot) { s4_queued = S4_NONE; revert_transition(); x->redraw = 1; }
    else if (s4_active == slot) { uint8_t p[2]; p[0] = (uint8_t)slot; p[1] = QM_HOLD; lsyx_send(LCMD_QUEUE, p, 2); queue_slot(x, back, QM_BAR); }
}

/* ---- public ---- */
static void stage4_init(void)
{
    if (!lsong.bars) song_init();
    memset(s4_sec, 0, sizeof s4_sec);
    s4_active = s4_queued = s4_hold_return = S4_NONE; s4_cursor = 0; s4_store_type = STY_VERSE; s4_trans = TRN_NONE; s4_energy = 7;
    s4_trans_applied = 0; s4_last_step = s4_bars_played = s4_last_bar = s4_last_beat = 0;
    s4_rec_n = 0; s4_recording = 0; s4_ghost_n = s4_perf_n = 0; s4_follow_pad = 0; s4_replay_shown = 0; s4_mirror_slot = S4_NONE;
    memset(s4_pad_down, 0, sizeof s4_pad_down); memset(s4_key_slot, 0, sizeof s4_key_slot); s4_hint[0] = 0;
    apply_energy(s4_energy);
}

static void stage4_key(struct lctx *x, unsigned note, unsigned velocity)
{
    int k = (int)note - (int)(53u + 12u * (unsigned)(x->octave + 2)) + 24;
    if (!velocity) {
        if (k >= 0 && k < 16 && s4_key_slot[k]) { unsigned slot = s4_key_slot[k] - 1u; s4_key_slot[k] = 0; s4_pad_down[slot] = 0; slot_release(x, slot); x->redraw = 1; return; }
        lmidi_note(LT_KEYS, note, 0); x->redraw = 1; return;
    }
    if (!LEARN_PADS && (x->buttons & LB(LB_GLO)) && k >= 0 && k < 16) {
        unsigned slot = (unsigned)k & 7u;
        if (k < 8) { s4_key_slot[k] = (uint8_t)(slot + 1u); s4_pad_down[slot] = 1; s4_pad_hinted[slot] = 0; slot_press(x, slot); }
        else store_slot(x, slot, s4_store_type);
        return;
    }
    lmidi_note(LT_KEYS, note, velocity);
    x->redraw = 1;
}

static void stage4_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    (void)pressure;
    if (pad >= 16) return;
    if (velocity) {
        s4_pad_down[pad] = 1; s4_pad_at[pad] = x->now; s4_pad_hinted[pad] = 0;
        if (pad < 8) slot_press(x, pad);
        else if (x->buttons & LB(LB_GLO)) store_slot(x, pad - 8u, s4_store_type);
        else if (s4_sec[pad - 8u].used) { s4_sec[pad - 8u].type = (uint8_t)((s4_sec[pad - 8u].type + 1u) % STY_COUNT); learn_notice(TYPE_NM[s4_sec[pad - 8u].type], 700); s4_cursor = (uint8_t)(pad - 8u); }
        else learn_notice(LBL_GLO "+PAD STORES HERE", 700);
    } else {
        s4_pad_down[pad] = 0;
        if (pad < 8) slot_release(x, pad);
    }
    x->redraw = 1;
}

static void stage4_encoder(struct lctx *x, unsigned id, int delta)
{
    uint8_t p;
    switch (id) {
    case 0: apply_energy((unsigned)((int)s4_energy + delta < 1 ? 1 : (int)s4_energy + delta > 8 ? 8 : (int)s4_energy + delta)); break;
    case 1: s4_store_type = (uint8_t)((s4_store_type + STY_COUNT + (delta > 0 ? 1u : STY_COUNT - 1u)) % STY_COUNT); learn_notice(TYPE_NM[s4_store_type], 700); break;
    case 2: { int v = (int)s4_lesson_pick + delta; s4_lesson_pick = (uint8_t)(v < 0 ? 0 : v >= (int)lesson_count ? (int)lesson_count - 1 : v); learn_notice(lessons[s4_lesson_pick].title, 1200); p = s4_lesson_pick; lsyx_send(LCMD_LESSON, &p, 1); } break;
    case 3: { int v = (int)x->slow_percent + delta * 5; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); } break;
    case 4: s4_trans = (uint8_t)((s4_trans + TRN_COUNT + (delta > 0 ? 1u : TRN_COUNT - 1u)) % TRN_COUNT); learn_notice(trans_hint(), 1200); p = s4_trans; lsyx_send(LCMD_TRANSITION, &p, 1); break;
    case 5: {                                     /* the tempo, +-10 % around the section's own */
        unsigned base = s4_active < SEC_MAX ? s4_sec[s4_active].tempo : lsong.tempo, lo = base * 9u / 10u, hi = base * 11u / 10u;
        int v = (int)lsong.tempo + delta; song_set_tempo((unsigned)(v < (int)lo ? (int)lo : v > (int)hi ? (int)hi : v)); } break;
    case 6: song_set_swing((unsigned)((int)lsong.swing + delta)); break;
    default: break;
    }
    x->redraw = 1;
}

static void stage4_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (value > 127u) value = 127u;
    if (id == 0) { apply_energy(1u + value * 7u / 127u); x->redraw = 1; return; }
    if (id == 1) trk[LT_DRUMS].p[P_LEVEL] = (int16_t)value;
    else if (id == 2) { trk[LT_BASS].p[P_LEVEL] = (int16_t)value; trk[LT_CHORDS].p[P_LEVEL] = (int16_t)value; }
    else if (id == 3) trk[LT_KEYS].p[P_LEVEL] = (int16_t)value;
    x->redraw = 1;
}

static void stage4_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_QUEUE: if (n >= 2 && learn_page() == 4) queue_slot(x, p[0], p[1] % 3u); break;
    case LCMD_TRANSITION: if (n >= 1) { s4_trans = (uint8_t)(p[0] % TRN_COUNT); x->redraw = 1; } break;
    case LCMD_SECTION: if (n >= 4) { s4_mirror_slot = p[0]; s4_mirror_type = (uint8_t)(p[1] % STY_COUNT); x->redraw = 1; } break;
    case LCMD_PATH: if (n >= 2 && learn_page() == 4) { s4_follow_pad = p[0] == 0 ? (uint8_t)(p[1] + 1u) : 0u; x->redraw = 1; } break;
    case LCMD_LESSON: if (n >= 1 && p[0] < lesson_count) s4_lesson_pick = p[0]; break;
    case LCMD_LOG: if (n >= 3 && p[1] >= 4) log_unlock(UNLOCK_STAGE4); break;
    default: break;
    }
}

static void stage4_control(struct lctx *x)
{
    unsigned b = x->btn_down, i;
    const struct lesson_step *ls;
    if (b & LB(LB_SCL)) { s4_store_type = (uint8_t)((s4_store_type + 1u) % STY_COUNT); learn_notice(TYPE_NM[s4_store_type], 700); x->redraw = 1; }
    if (b & LB(LB_SAVE)) store_slot(x, s4_cursor, s4_store_type);
    if (b & LB(LB_ENV)) stage4_encoder(x, 4, 1);
    if (b & LB(LB_PLAY)) {
        if (song_playing()) { song_play(0); s4_elapsed = x->now - s4_play_started; }
        else { song_play(1); s4_play_started = x->now; s4_last_step = 0; s4_bars_played = 0; }
        x->redraw = 1;
    }
    if (b & LB(LB_REC)) {
        if (s4_recording) learn_notice(LBL_HOME " STOPS THE RECORD", 800);
        else if (s4_passed_once || (s4_ghost_n && s4_perf_n)) { uint8_t p; s4_replay_which = !s4_replay_which; s4_replay_shown = 1; learn_notice(s4_replay_which ? "REPLAY: YOUR ORDER" : "REPLAY: GHOST ORDER", 800); p = s4_replay_which; lsyx_send(LCMD_REPLAY, &p, 1); }
        else record_start(x);
        x->redraw = 1;
    }
    if (b & LB(LB_HOME)) {
        if (s4_recording) record_stop(x);
        else if (x->now - s4_stop_at < 2000u && s4_rec_n) { s4_rec_n = 0; s4_replay_shown = 0; learn_notice("RECORD DISCARDED", 900); }
        s4_stop_at = x->now;
        if (song_playing()) { song_play(0); s4_elapsed = x->now - s4_play_started; }
        s4_queued = S4_NONE; revert_transition(); lmidi_all_off(); x->redraw = 1;
    }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); s4_ghost_n = 0; s4_follow_pad = 0; send_path(); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(s4_lesson_pick); lesson_load_ghost(); if (!s4_ghost_n) learn_notice("NOT A SONG LESSON", 1000); }
        x->redraw = 1;
    }
    ls = lesson_step();
    if (ls && ls->type == DR_TEXT && x->keys_down) { lesson_step_done(1, 100); lesson_load_ghost(); }
    else if (ls && ls->type == DR_SONG && !s4_ghost_n) lesson_load_ghost();
    else if (!ls && s4_ghost_n) { s4_ghost_n = 0; s4_follow_pad = 0; }

    if (song_playing()) {                         /* the position: bars, beats, the loop boundary */
        unsigned step = song_step(), bar = step / 16u, beat = (step / 4u) % 4u, wrapped = step < s4_last_step;
        if (wrapped || bar != s4_last_bar) {
            if (wrapped || bar > s4_last_bar) s4_bars_played++;
            s4_last_bar = bar;
            if (s4_queued < SEC_MAX && s4_qmode != QM_BEAT && wrapped) { recall_slot(x, s4_queued); s4_queued = S4_NONE; }
            else if (s4_queued < SEC_MAX && bar == lsong.bars - 1u) apply_transition(x);
            else send_section(1);
            x->redraw = 1;
        }
        if (beat != s4_last_beat) {
            s4_last_beat = beat;
            if (s4_queued < SEC_MAX && s4_qmode == QM_BEAT) { recall_slot(x, s4_queued); s4_queued = S4_NONE; }
            send_songpos(x);
            x->redraw = 1;
        }
        s4_last_step = step;
    }
    if (s4_ghost_n && s4_ghost_pos < s4_ghost_n && s4_queued == S4_NONE) {   /* follow-me: a slot of the next ghost type */
        unsigned want = s4_ghost[s4_ghost_pos], pad = 0, lit, blink;
        for (i = 0; i < SEC_MAX && !pad; i++) if (s4_sec[i].used && s4_sec[i].type == want) pad = i + 1u;
        lit = pad && (s4_active == S4_NONE || s4_bars_played + 2u >= LESSON_BARS || !song_playing()) ? pad : 0u;
        blink = lit && s4_bars_played + 1u >= LESSON_BARS;
        if (lit != s4_follow_pad || blink != s4_follow_blink) { s4_follow_pad = (uint8_t)lit; s4_follow_blink = (uint8_t)blink; send_path(); x->redraw = 1; }
        if (s4_ghost_pos == 0 && s4_perf_n == 0 && pad == 0) { str_cpy(s4_hint, "STORE A SLOT OF EACH TYPE", sizeof s4_hint); s4_hint_until = x->now + 1500u; }
    }
    if (s4_ghost_n && s4_perf_n >= s4_ghost_n && !s4_passed_once && !s4_recording) lesson_finish(x);
    for (i = 0; i < 8u; i++) if (s4_pad_down[i] && !s4_pad_hinted[i] && x->now - s4_pad_at[i] >= 2000u) { s4_pad_hinted[i] = 1; slot_hint(x, i); }
    if (s4_hint[0] && (int32_t)(x->now - s4_hint_until) >= 0) { s4_hint[0] = 0; x->redraw = 1; }
    {
        uint8_t st, d1, d2;
        while (lmidi_pull_external(&st, &d1, &d2)) x->redraw = 1;
    }
    {
        static unsigned shown_sec;
        unsigned secs = (song_playing() || s4_recording ? x->now - s4_play_started : s4_elapsed) / 1000u;
        if (secs != shown_sec) { shown_sec = secs; x->redraw = 1; }
    }
    llit_keys = lbright_keys = 0;
}

static uint32_t stage4_pad_colour(unsigned pad)
{
    uint32_t t = fm1_ms, c;
    unsigned slot;
    if (pad >= 16) return P_OFF;
    if (pad < 8) {
        if (!s4_sec[pad].used) return s4_follow_pad == pad + 1u ? P_FULL(P_WHITE) : P_OFF;
        c = TYPE_PAD[s4_sec[pad].type];
        if (s4_follow_pad == pad + 1u) return (s4_follow_blink && (t / 200u) & 1u) ? P_OFF : P_FULL(c);
        if (s4_queued == pad) return ((t / 250u) & 1u) ? P_FULL(c) : P_DIM(c);
        if (s4_active == pad) return (song_playing() && ((song_step() / 4u) & 1u)) ? P_FULL(c) : c;
        return P_DIM(c);
    }
    slot = pad - 8u;
    if (s4_pad_down[pad]) return P_FULL(TYPE_PAD[s4_store_type]);
    return s4_sec[slot].used ? P_DIM(TYPE_PAD[s4_sec[slot].type]) : 0x101010u;
}

/* ---- the screen ---- */
static void stage4_draw(struct lctx *x)
{
    char line[40];
    unsigned n, i, atype = s4_active < SEC_MAX ? s4_sec[s4_active].type : s4_mirror_slot < SEC_MAX ? s4_mirror_type : STY_COUNT;
    unsigned ms = song_playing() || s4_recording ? x->now - s4_play_started : s4_elapsed, secs = ms / 1000u;
    const struct lesson_step *ls = lesson_step();
    n = ls_cat(line, 0, sizeof line, LEARN_PADS ? "STAGE E" : "SCRIBE E"); n = ls_num(line, n, sizeof line, s4_energy); n = ls_cat(line, n, sizeof line, " ");
    n = ls_num(line, n, sizeof line, lsong.tempo); n = ls_cat(line, n, sizeof line, "BPM");
    if (s4_recording) n = ls_cat(line, n, sizeof line, " REC");
    if (x->slow_percent < 100u) { n = ls_cat(line, n, sizeof line, " S"); ls_num(line, n, sizeof line, x->slow_percent); }
    ld_text(4, 2, line, 0, s4_recording ? LC_RED : LC_DIM);
    ld_fill(228, 6, 8, 8, lsyx_partner_present() ? LC_GREEN : LC_PANEL);
    ld_text(4, 20, atype < STY_COUNT ? TYPE_NM[atype] : "LOOP", 1, atype < STY_COUNT ? TYPE_RGB[atype] : LC_TEXT);
    /* the row: the bar played of the loop's bars, the queued section and the beats until it */
    n = ls_cat(line, 0, sizeof line, "BAR "); n = ls_num(line, n, sizeof line, s4_bars_played + 1u); n = ls_cat(line, n, sizeof line, "/"); n = ls_num(line, n, sizeof line, lsong.bars);
    if (s4_queued < SEC_MAX) {
        unsigned beats_left = s4_qmode == QM_BEAT ? 1u : (song_beats() - ((song_step() / 4u) % song_beats()));
        n = ls_cat(line, n, sizeof line, " > "); n = ls_cat(line, n, sizeof line, TYPE_NM[s4_sec[s4_queued].type]); n = ls_cat(line, n, sizeof line, " IN "); ls_num(line, n, sizeof line, beats_left);
    }
    ld_text(4, 54, line, 0, s4_queued < SEC_MAX ? LC_ORANGE : LC_CYAN);
    n = ls_num(line, 0, sizeof line, secs / 60u); line[n++] = ':'; line[n++] = (char)('0' + (secs % 60u) / 10u); line[n++] = (char)('0' + secs % 10u); line[n] = 0;
    ld_text(4, 72, line, 0, LC_TEXT);
    {   /* the song bar: the record so far as blocks, the current section growing, the head */
        unsigned total = s4_bars_played + 1u, px = 0, scale, w;
        int head;
        for (i = 0; i < s4_rec_n; i++) total += s4_rec[i].bars;
        scale = total ? 166u / total : 166u; if (scale < 2u) scale = 2u;
        ld_fill(70, 74, 166, 12, LC_PANEL);
        for (i = 0; i < s4_rec_n && px < 166u; i++) { w = s4_rec[i].bars * scale; if (px + w > 166u) w = 166u - px; ld_fill(70 + (int)px, 75, (int)w, 10, TYPE_RGB[s4_rec[i].type]); px += w + 1u; }
        w = (s4_bars_played + 1u) * scale; if (px + w > 166u) w = 166u - px;
        if (atype < STY_COUNT) ld_fill(70 + (int)px, 75, (int)w, 10, TYPE_RGB[atype]);
        head = 70 + (int)px + (int)(s4_bars_played * scale + (((song_step() / 4u) % 4u) * scale) / 4u); if (head > 234) head = 234;
        ld_fill(head, 74, 2, 12, LC_TEXT);
    }
    for (i = 0; i < 8u; i++) {                    /* the slot strip */
        int cx = 4 + (int)i * 29, cy = 92;
        unsigned used = s4_sec[i].used, hot = s4_active == i || s4_follow_pad == i + 1u;
        uint16_t c = used ? TYPE_RGB[s4_sec[i].type] : LC_PANEL;
        char lbl[3];
        ld_fill(cx, cy, 27, 24, hot ? c : LC_BG);
        ld_frame(cx, cy, 27, 24, s4_queued == i ? LC_ORANGE : used ? c : LC_PANEL);
        lbl[0] = (char)('1' + i); lbl[1] = used ? TYPE_NM[s4_sec[i].type][0] : '-'; lbl[2] = 0;
        ld_text(cx + 5, cy + 4, lbl, 0, hot ? LC_BG : used ? c : LC_DIM);
    }
    n = ls_cat(line, 0, sizeof line, "STORE AS "); ls_cat(line, n, sizeof line, TYPE_NM[s4_store_type]);
    ld_text(4, 120, line, 0, TYPE_RGB[s4_store_type]);
    n = ls_cat(line, 0, sizeof line, LKN[4]); n = ls_cat(line, n, sizeof line, " "); n = ls_cat(line, n, sizeof line, TRANS_NM[s4_trans]); n = ls_cat(line, n, sizeof line, "  ENERGY ");
    for (i = 0; i < 8u && n + 1u < sizeof line; i++) line[n++] = i < s4_energy ? '#' : '.'; line[n] = 0;
    ld_text(4, 138, line, 0, LC_AMBER);
    ld_text(4, 156, LEARN_PADS ? "PAD: SLOT " LBL_GLO "+PAD 9-16 STORE" : LBL_GLO "+KEY: SLOT  " LBL_SAVE ": STORE", 0, LC_DIM);
    if (ls) ls_cat(line, 0, sizeof line, ls->prompt ? ls->prompt : "");
    else if (s4_recording) { n = ls_cat(line, 0, sizeof line, "RECORDING "); n = ls_num(line, n, sizeof line, s4_rec_n + 1u); ls_cat(line, n, sizeof line, " SECTIONS  " LBL_HOME ": STOP"); }
    else ls_cat(line, 0, sizeof line, s4_active == S4_NONE ? (LEARN_PADS ? LBL_GLO "+PAD 9-16: STORE LOOP" : LBL_GLO "+KEY 9-16: STORE LOOP")
                                      : (LEARN_PADS ? "PAD 1-8: NEXT SECTION" : LBL_GLO "+KEY 1-8: NEXT SECTION"));
    ld_text(4, 174, line, 0, s4_recording ? LC_RED : LC_AMBER);
    if (!ls && !s4_recording && !lesson_running()) ld_text(4, 192, LBL_REC ": RECORD THE SONG", 0, LC_DIM);
    if (lesson_running()) {
        const struct lesson *l = &lessons[lesson_running() - 1u];
        n = ls_cat(line, 0, sizeof line, l->title); n = ls_cat(line, n, sizeof line, " "); n = ls_num(line, n, sizeof line, lesson_progress() + 1u); n = ls_cat(line, n, sizeof line, "/"); ls_num(line, n, sizeof line, l->step_count);
        ld_text(4, 192, line, 0, LC_PURPLE);
    }
    if (s4_ghost_n) {                             /* the replay strips: the ghost order, the performed order */
        for (i = 0; i < s4_ghost_n; i++) ld_fill(4 + (int)i * 28, 208, 24, 5, s4_replay_shown && s4_replay_which ? LC_PANEL : TYPE_RGB[s4_ghost[i]]);
        for (i = 0; i < s4_perf_n; i++) { unsigned ok = i < s4_ghost_n && s4_perf[i] == s4_ghost[i]; ld_fill(4 + (int)i * 28, 215, 24, 5, s4_replay_shown && !s4_replay_which ? LC_PANEL : ok ? (s4_perf_late[i] ? LC_YELLOW : LC_GREEN) : LC_RED); }
    }
    {
        const char *nt = learn_notice_text();
        if (nt) ld_text(4, 222, nt, 0, LC_TEXT);
        else if (s4_hint[0]) ld_text(4, 222, s4_hint, 0, LC_YELLOW);
    }
}
