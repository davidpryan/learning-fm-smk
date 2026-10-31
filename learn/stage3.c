/* SPDX-License-Identifier: GPL-3.0-only
 * Stage 3: Shadow (SMK-37 Elite) / Tape (FM-1). Grow it.
 *
 * A shadow is a part described by its relationship to a part you already have: pick a source, then a rule, and
 * the shadow is rendered from the source; change the source and it follows. SLOOP has three synth tracks and the
 * drum track, so the three shadow slots have a home each:
 *   A  CHORDS  rules HOLD, ARP, TOP NOTE    rendered onto the CHORDS track (in place of the plain held chords)
 *   B  MELODY  rules 3RD ABOVE, 6TH BELOW, ECHO, ANSWER   rendered onto the MELODY track as notes added to the
 *              take (a POLY step holds four notes; the take itself is kept and re-rendered from)
 *   C  DRUMS   rule GHOST                   quiet hat hits on the drum track where the HAT lane is silent
 * Obedience: strict plays every rendered note as written; loose thins the notes and wobbles their velocity.
 *
 * Tape: takes of the melody in RAM (four, for undo / redo / A-B), punch-in by bar, a whole pass with REC+PLAY,
 * momentary solo of any track, an export of the four tracks over SysEx. Nothing here writes Flash.
 *
 * Controls (both boards):
 *   keys        play the selected tape track live (recorded while taping)
 *   REC         hold while playing: punch the next bar; tap: A/B the previous / current take
 *   REC+PLAY    tape a whole pass now        PLAY   play / stop (ends a tape pass)
 *   HOME        stop everything              SAVE   undo take   GLO+SAVE redo take   HOME+REC export
 *   SCL         next tape track              ENV    hints on / off     LFO   hear the source, then the shadow
 *   FX          remove the selected shadow   EDIT   next shadow slot   ARP   lesson
 *   K1 octave  K2 density (FM-1: obedience; GLO+K2 density)  K3 velocity  K4 patch  K5 send pick  K6 send  K7 slow
 *   SMK pads    1-3 source (Chords, Melody, Drums)  4-6 shadow slots (hold 2 s: remove)  7 next tape track
 *               8 export  9-16 rules (hold 2 s: the why-hint demo); GLO + pad 1-6: solo while held
 *   SMK faders  1-3 obedience of A, B, C; 4 the selected send of the selected track
 *   FM-1 keys   GLO + keys 1-3 source, 4-6 slot, 9-16 rule, 17-19 tape track */

enum { SHADOWS = 3, TAPE_TRACKS = 3, TAKES = 4 };
enum { SRC_CHORDS, SRC_MELODY, SRC_DRUMS, SRC_COUNT };
enum { RULE_HOLD, RULE_ARP, RULE_THIRD, RULE_SIXTH, RULE_ECHO, RULE_ANSWER, RULE_TOP, RULE_GHOST, RULE_COUNT };
static const uint8_t RULE_SRC[RULE_COUNT] = {SRC_CHORDS, SRC_CHORDS, SRC_MELODY, SRC_MELODY, SRC_MELODY, SRC_MELODY, SRC_CHORDS, SRC_DRUMS};
static const char *const RULE_NAME[RULE_COUNT] = {"HOLD", "ARP", "3RD ABOVE", "6TH BELOW", "ECHO", "ANSWER", "TOP NOTE", "GHOST"};
static const char *const RULE_HINT[RULE_COUNT] = {   /* (each fits the line: 29 characters) */
    "HOLD: CHORD TONES, 1 PER BEAT", "ARP: CHORD TONES, YOUR ORDER",
    "3RD ABOVE: MELODY +2 STEPS", "6TH BELOW: MELODY -5 STEPS",
    "ECHO: 1 BEAT LATE, 8VA, SOFT", "ANSWER: LAST 4 NOTES REVERSED",
    "TOP NOTE: HIGHEST CHORD TONE", "GHOST: SOFT HITS OFF THE HAT"};
static const char *const SRC_NAME[SRC_COUNT] = {"CHORDS", "MELODY", "DRUMS"};
static const uint8_t SRC_TRACK[SRC_COUNT] = {LT_CHORDS, LT_KEYS, LT_DRUMS};   /* the track a source plays on */
static const uint8_t TAPE_TRK[TAPE_TRACKS] = {LT_KEYS, LT_CHORDS, LT_BASS};
static const char *const TAPE_NM[TAPE_TRACKS] = {"MEL", "CHD", "BASS"};

struct shadow { uint8_t used, rule, density, velocity, obedience; int8_t octave; };
static struct shadow s3_sh[SHADOWS];              /* slot = source */
static uint8_t s3_sel;                            /* the selected slot */
static uint8_t s3_arp[8] = {0, 1, 2, 1}, s3_arp_len = 4, s3_arp_entry; static uint32_t s3_arp_entry_at;
static uint8_t s3_src_armed; static uint32_t s3_src_armed_at;
static uint8_t s3_rule_hinted, s3_hints = 1;
static uint8_t s3_pad_down[16]; static uint32_t s3_pad_since[16]; static uint8_t s3_pad_hint_done[16];
static uint8_t s3_solo;                           /* bit per track: momentary solo held by the layer */
static uint32_t s3_seen_rev; static uint32_t s3_rev_at; static uint8_t s3_render_pending;
static step_t s3_takes[TAKES][SONG_STEPS];        /* the melody's takes (2.5 KB) */
static step_t s3_mel_src[SONG_STEPS];             /* the melody as taped: the melody shadow renders from this */
static step_t s3_scratch[SONG_STEPS];             /* the pre-punch copy */
static uint8_t s3_take_count, s3_take_cur = 255;
static uint8_t s3_tape;                           /* 0 melody, 1 chords, 2 bass */
static uint8_t s3_punch_armed, s3_punch_on, s3_punch_bars, s3_rec_full, s3_rec_consumed, s3_rec_slow;
static uint8_t s3_ab_pending, s3_ab_side;
static unsigned s3_last_step; static uint8_t s3_bar_boundary;
static uint8_t s3_send_pick, s3_export_track, s3_export_frame, s3_export_on, s3_from_partner;
static uint8_t s3_demo_phase; static uint32_t s3_demo_until; static uint8_t s3_demo_src, s3_demo_shadow;
static uint32_t s3_sounding[4];
static uint32_t s3_hint_until; static char s3_hint[40];
static uint8_t s3_follow_pad;
static unsigned s3_root, s3_scale;

/* ---- theory ---- */
static unsigned diatonic_shift(unsigned note, int degrees)
{
    int n;
    if (!theory_in_scale(s3_root, s3_scale, note)) note = theory_nudge(s3_root, s3_scale, note);
    n = (int)note;
    while (degrees > 0 && n < 127) { do { n++; } while (n < 127 && !theory_in_scale(s3_root, s3_scale, (unsigned)n)); degrees--; }
    while (degrees < 0 && n > 0) { do { n--; } while (n > 0 && !theory_in_scale(s3_root, s3_scale, (unsigned)n)); degrees++; }
    return (unsigned)(n < 0 ? 0 : n > 127 ? 127 : n);
}

/* ---- rendering ---- */
static unsigned jitter_for(const struct shadow *sh, unsigned s, unsigned vel)
{
    unsigned loose = 127u - sh->obedience, j = loose * 30u / 127u;
    uint32_t r = (uint32_t)(s * 2654435761u) ^ (uint32_t)(sh->rule * 97u);
    int v = (int)vel * (int)sh->velocity / 100;
    r = (r >> 13) ^ r;
    if (j) v += (int)(r % (2u * j + 1u)) - (int)j;
    return (unsigned)(v < 1 ? 1 : v > 127 ? 127 : v);
}
static unsigned obey_keep(const struct shadow *sh, unsigned s)   /* loose: some notes are left out (4/8 .. every one) */
{
    unsigned p = sh->obedience >= 120u ? 8u : 4u + (unsigned)sh->obedience * 3u / 120u;
    uint32_t r = (uint32_t)(s * 40503u + sh->rule * 7919u);
    return ((r >> 7) & 7u) < p;
}
static unsigned density_keep(const struct shadow *sh, unsigned s)
{
    if (sh->density >= 7) return 1;
    return ((s * 5u + 3u) % 8u) <= sh->density;
}
static void step_put(track_t *t, unsigned s, int note, unsigned vel)   /* a note added to step s (a POLY chord of up to 4) */
{
    step_t *st = &t->step[s % NSTEP];
    unsigned k;
    if (note < 0 || note > 127 || !vel) return;
    if (st->time != ST_NOTE || !st->n) { memset(st, 0, sizeof *st); st->time = ST_NOTE; }
    for (k = 0; k < st->n; k++) if (st->note[k] == (uint8_t)note) return;
    if (st->n >= 4u) return;
    st->note[st->n++] = (uint8_t)note;
    if (vel > st->vel) st->vel = (uint8_t)vel;
}
static void render_chords_shadow(void)            /* slot A onto the CHORDS track */
{
    const struct shadow *sh = &s3_sh[SRC_CHORDS];
    track_t *t = &trk[LT_CHORDS];
    unsigned steps = song_steps(), s, beat, k;
    if (!sh->used) { song_write_chords(); return; }
    song_track_setup(LT_CHORDS);
    song_steps_rest(t);
    switch (sh->rule) {
    case RULE_HOLD:
        for (beat = 0; beat < steps / 4u; beat++) {
            uint8_t notes[4]; unsigned n;
            song_chord_notes(lsong.chord[beat].degree, lsong.chord[beat].seventh, notes, &n);
            if (!n || !density_keep(sh, beat) || !obey_keep(sh, beat)) continue;
            for (k = 0; k < n; k++) step_put(t, beat * 4u, (int)notes[k] + 12 * sh->octave, jitter_for(sh, beat, 80));
            for (k = 1; k < 4u; k++) t->step[beat * 4u + k].time = ST_TIE;
        }
        t->p[P_SGATE] = 100;
        break;
    case RULE_ARP: {
        unsigned rate = sh->density >= 6u ? 1u : sh->density >= 3u ? 2u : 4u;
        for (s = 0, k = 0; s < steps; s += rate, k++) {
            unsigned seventh, n, idx; int d = song_degree_at_step(s, &seventh);
            uint8_t notes[4];
            song_chord_notes(d > 0 ? (unsigned)d : 0u, seventh, notes, &n);
            if (!n || !obey_keep(sh, s)) continue;
            idx = s3_arp[k % (s3_arp_len ? s3_arp_len : 1u)];
            step_put(t, s, (int)notes[idx % n] + 12 * (int)(idx / n) + 12 * sh->octave, jitter_for(sh, s, 90));
        }
        t->p[P_SGATE] = (int16_t)(rate == 1u ? 50 : 70);
        break; }
    default: {                                    /* TOP NOTE: the highest chord tone, as a slow line */
        int prev = -1;
        for (s = 0; s < steps; s++) {
            unsigned seventh, n; int d = song_degree_at_step(s, &seventh);
            uint8_t notes[4];
            song_chord_notes(d > 0 ? (unsigned)d : 0u, seventh, notes, &n);
            if (!n) { prev = -1; continue; }
            if ((int)notes[n - 1] == prev) { t->step[s].time = ST_TIE; continue; }
            step_put(t, s, (int)notes[n - 1] + 12 * sh->octave, jitter_for(sh, s, 70));
            prev = notes[n - 1];
        }
        t->p[P_SGATE] = 100;
        break; }
    }
}
static void render_melody_shadow(void)            /* slot B: the take plus its shadow, onto the MELODY track */
{
    const struct shadow *sh = &s3_sh[SRC_MELODY];
    track_t *t = &trk[LT_KEYS];
    unsigned steps = song_steps(), s;
    memcpy(t->step, s3_mel_src, sizeof s3_mel_src);
    if (!sh->used) return;
    switch (sh->rule) {
    case RULE_THIRD: case RULE_SIXTH:
        for (s = 0; s < steps; s++) {
            const step_t *src = &s3_mel_src[s];
            if (src->time != ST_NOTE || !src->n || !density_keep(sh, s) || !obey_keep(sh, s)) continue;
            step_put(t, s, (int)diatonic_shift(src->note[0], sh->rule == RULE_THIRD ? 2 : -5) + 12 * sh->octave, jitter_for(sh, s, src->vel ? src->vel : 100u));
        }
        break;
    case RULE_ECHO:
        for (s = 0; s < steps; s++) {
            const step_t *src = &s3_mel_src[s];
            if (src->time != ST_NOTE || !src->n || !density_keep(sh, s) || !obey_keep(sh, s)) continue;
            step_put(t, (s + 4u) % steps, (int)src->note[0] + 12 + 12 * sh->octave, jitter_for(sh, s, (src->vel ? src->vel : 100u) * 60u / 100u));
        }
        break;
    default: {                                    /* ANSWER: in a silent bar, the last four notes backwards */
        uint8_t notes[SONG_STEPS], at[SONG_STEPS]; unsigned count = 0, bar, k, i;
        for (s = 0; s < steps; s++) if (s3_mel_src[s].time == ST_NOTE && s3_mel_src[s].n) { notes[count] = s3_mel_src[s].note[0]; at[count] = (uint8_t)s; count++; }
        if (!count) break;
        for (bar = 0; bar < steps / 16u; bar++) {
            unsigned silent = 1, last = count - 1u;
            for (s = bar * 16u; s < bar * 16u + 16u; s++) if (s3_mel_src[s].time == ST_NOTE && s3_mel_src[s].n) silent = 0;
            if (!silent) continue;
            for (i = 0; i < count; i++) if (at[i] < bar * 16u) last = i;
            for (k = 0; k < 4u && k < count; k++) {
                i = (last + count - k) % count;
                if (density_keep(sh, bar * 16u + k * 4u) && obey_keep(sh, bar * 16u + k * 4u))
                    step_put(t, bar * 16u + k * 4u, (int)notes[i] + 12 * sh->octave, jitter_for(sh, bar * 16u + k, 85));
            }
        }
        break; }
    }
}
static void render_drums_shadow(void)             /* slot C: GHOST hits onto the drum track */
{
    const struct shadow *sh = &s3_sh[SRC_DRUMS];
    unsigned steps = song_steps(), s;
    song_write_drums();                           /* (the lanes as the model says, then the ghosts) */
    if (!sh->used) return;
    for (s = 0; s < steps; s++) {
        if (dstep_has(&TDRUM->dstep[s], DL_HAT) || !density_keep(sh, s) || !obey_keep(sh, s)) continue;
        dstep_set(&TDRUM->dstep[s], DL_HAT, jitter_for(sh, s, 35) < 48u ? LV_GHOST : LV_SOFT, 0);
    }
}
static void render_all(void)
{
    render_chords_shadow(); render_melody_shadow(); render_drums_shadow();
    s3_seen_rev = lsong_rev; s3_render_pending = 0;
}
static void render_slot(unsigned id)
{
    if (id == SRC_CHORDS) render_chords_shadow(); else if (id == SRC_MELODY) render_melody_shadow(); else render_drums_shadow();
    s3_seen_rev = lsong_rev;
}

static void shadow_send(unsigned id)
{
    uint8_t p[4]; p[0] = (uint8_t)id; p[1] = (uint8_t)id; p[2] = s3_sh[id].rule; p[3] = s3_sh[id].obedience;
    lsyx_send(LCMD_SHADOW, p, 4);
}
static void shadow_create(struct lctx *x, unsigned rule, unsigned obedience)
{
    unsigned id = RULE_SRC[rule % RULE_COUNT];
    struct shadow *sh = &s3_sh[id];
    if (rule >= RULE_COUNT) return;
    memset(sh, 0, sizeof *sh);
    sh->used = 1; sh->rule = (uint8_t)rule; sh->density = 7; sh->velocity = 100;
    sh->obedience = (uint8_t)(obedience > 127u ? 127u : obedience); sh->octave = 0;
    render_slot(id);
    s3_sel = (uint8_t)id;
    if (rule == RULE_ARP) { s3_arp_entry = 1; s3_arp_entry_at = x->now; learn_notice(LEARN_PADS ? "TAP A PATTERN ON PADS 9-16" : "GLO+KEYS 9-16: THE PATTERN", 1500); }
    if (s3_hints && !(s3_rule_hinted & (1u << rule))) { s3_rule_hinted |= (uint8_t)(1u << rule); str_cpy(s3_hint, RULE_HINT[rule], sizeof s3_hint); s3_hint_until = x->now + 3000u; }
    x->redraw = 1;
}
static void shadow_remove(struct lctx *x, unsigned id)
{
    if (id >= SHADOWS || !s3_sh[id].used) return;
    s3_sh[id].used = 0;
    render_slot(id);
    learn_notice("SHADOW REMOVED", 800); x->redraw = 1;
}

/* ---- momentary solo ---- */
static void solo_set(unsigned track, unsigned on)
{
    unsigned bit = 1u << (track % NTRK);
    uint8_t p[2];
    if (!!(s3_solo & bit) == !!on) return;
    if (on) s3_solo |= (uint8_t)bit; else s3_solo &= (uint8_t)~bit;
    song.solo = s3_solo;
    p[0] = (uint8_t)track; p[1] = (uint8_t)on; lsyx_send(LCMD_SOLO, p, 2);
}
static void solo_clear(void) { unsigned t; for (t = 0; t < NTRK; t++) if (s3_solo & (1u << t)) solo_set(t, 0); }

/* ---- takes (of the melody) ---- */
static unsigned count_notes(const step_t *c, unsigned steps)
{
    unsigned s, n = 0; for (s = 0; s < steps; s++) if (c[s].time == ST_NOTE && c[s].n) n++; return n;
}
static void take_capture(struct lctx *x)
{
    unsigned notes, in_key = 0, s, score;
    const struct lesson_step *ls;
    uint8_t p[3];
    if (s3_take_count == TAKES) { unsigned i; for (i = 0; i + 1u < TAKES; i++) memcpy(s3_takes[i], s3_takes[i + 1], sizeof s3_takes[0]); s3_take_count = TAKES - 1; }   /* (no memmove on the device) */
    memcpy(s3_mel_src, trk[LT_KEYS].step, sizeof s3_mel_src);   /* (what was taped: the shadow renders from it) */
    memcpy(s3_takes[s3_take_count], s3_mel_src, sizeof s3_mel_src);
    s3_take_cur = s3_take_count; s3_take_count++;
    notes = count_notes(s3_mel_src, song_steps());
    for (s = 0; s < song_steps(); s++) if (s3_mel_src[s].time == ST_NOTE && s3_mel_src[s].n && theory_in_scale(s3_root, s3_scale, s3_mel_src[s].note[0])) in_key++;
    score = notes ? in_key * 100u / notes : 0u;
    log_add(LOGK_TAKE, in_key, notes - in_key, lsong.tempo, s3_rec_slow, lesson_running(), score);
    ls = lesson_step();
    if (ls && ls->type == DR_REFTAKE) {
        unsigned need = ls->data[0] ? ls->data[0] : 4u;
        lesson_step_done(notes >= need && score >= (ls->target ? ls->target : 80u) && !s3_rec_slow, score);
    }
    p[0] = LT_KEYS; p[1] = 1; p[2] = s3_take_count; lsyx_send(LCMD_TRACK, p, 3);
    if (score >= 80u && notes >= 4u && !s3_rec_slow && !log_unlocked(UNLOCK_STAGE4)) { log_unlock(UNLOCK_STAGE4); learn_notice(LEARN_STAGE > 3 ? "A TAKE IN KEY! SEQ: PAGE 4" : "A TAKE IN KEY!", 2500); }
    else learn_notice(s3_rec_slow ? "TAKE SAVED (SLOW)" : "TAKE SAVED", 900);
    render_melody_shadow();
    x->redraw = 1;
}
static void take_load(unsigned index)
{
    if (index >= s3_take_count) return;
    memcpy(s3_mel_src, s3_takes[index], sizeof s3_mel_src);
    s3_take_cur = (uint8_t)index;
    render_melody_shadow();
}
static void take_undo_s3(struct lctx *x)
{
    if (s3_take_cur == 255u || s3_take_cur == 0u || s3_take_count < 2u) { learn_notice("NO EARLIER TAKE", 800); return; }
    take_load(s3_take_cur - 1u); learn_notice("UNDO: EARLIER TAKE", 800); x->redraw = 1;
}
static void take_redo_s3(struct lctx *x)
{
    if (s3_take_cur == 255u || s3_take_cur + 1u >= s3_take_count) { learn_notice("NO LATER TAKE", 800); return; }
    take_load(s3_take_cur + 1u); learn_notice("REDO: LATER TAKE", 800); x->redraw = 1;
}

/* ---- recording (the melody track; SLOOP's live recorder, as in Stage 2) ---- */
static unsigned s3_recording(void) { return song_recording() || s3_punch_on || s3_rec_full; }
static void s3_rec_begin(struct lctx *x, unsigned full)
{
    uint8_t p = 2;
    if (s3_tape != 0) { s3_tape = 0; learn_notice("TAPE: MELODY", 600); }
    memcpy(trk[LT_KEYS].step, s3_mel_src, sizeof s3_mel_src);   /* (the take alone: the shadow comes back after) */
    song_record(1);
    s3_rec_slow = x->slow_percent < 100u; s3_rec_full = (uint8_t)full;
    if (!song_playing()) song_play(1);
    if (!s3_from_partner) lsyx_send(LCMD_TRANSPORT, &p, 1);
    x->redraw = 1;
}
static void s3_rec_end(struct lctx *x)
{
    unsigned s;
    if (!s3_recording()) return;
    song_record(0);
    if (s3_punch_on)                              /* only the punched bars keep the new notes */
        for (s = 0; s < SONG_STEPS; s++) if (!((s3_punch_bars >> (s / 16u)) & 1u)) trk[LT_KEYS].step[s] = s3_scratch[s];
    s3_punch_on = s3_punch_armed = s3_rec_full = 0; s3_punch_bars = 0;
    take_capture(x);
}
static void stop_all(struct lctx *x)
{
    uint8_t p = 0;
    if (s3_recording()) s3_rec_end(x);
    s3_punch_armed = 0; s3_ab_pending = 0; s3_demo_phase = 0;
    if (song_playing()) song_play(0);
    solo_clear(); lmidi_all_off(); memset(s3_sounding, 0, sizeof s3_sounding);
    if (!s3_from_partner) lsyx_send(LCMD_TRANSPORT, &p, 1);
    x->redraw = 1;
}

/* ---- export: every track, eight steps a frame (LCMD_TRACK: track, frame, take, then per step
 * note1 vel n|time, or for the drum track the lane mask in 3 bytes) ---- */
static void export_poll(void)
{
    unsigned i, first, t;
    uint8_t p[3 + 8 * 3];
    if (!s3_export_on) return;
    t = s3_export_track < 3u ? TAPE_TRK[s3_export_track] : LT_DRUMS;
    first = s3_export_frame * 8u;
    p[0] = (uint8_t)t; p[1] = s3_export_frame; p[2] = (uint8_t)(t == LT_KEYS && s3_take_cur != 255u ? s3_take_cur + 1u : 0u);
    for (i = 0; i < 8u; i++) {
        unsigned s = first + i;
        if (t == LT_DRUMS) {
            uint32_t m = dstep_mask(&TDRUM->dstep[s % NSTEP]);
            p[3 + i * 3] = (uint8_t)(m & 127u); p[4 + i * 3] = (uint8_t)((m >> 7) & 127u); p[5 + i * 3] = (uint8_t)((m >> 14) & 3u);
        } else {
            const step_t *st = &trk[t].step[s % NSTEP];
            p[3 + i * 3] = (uint8_t)(st->n ? st->note[0] & 127u : 0u); p[4 + i * 3] = (uint8_t)(st->vel & 127u);
            p[5 + i * 3] = (uint8_t)((st->n & 7u) | (st->time & 3u) << 3);
        }
    }
    lsyx_send(LCMD_TRACK, p, sizeof p);
    if (++s3_export_frame >= song_steps() / 8u) { s3_export_frame = 0; if (++s3_export_track >= 4u) { s3_export_on = 0; learn_notice("EXPORT DONE", 1000); } }
}

/* ---- the why-hint demo: the source alone, then the shadow alone ---- */
static void demo_start(struct lctx *x, unsigned rule)
{
    unsigned id = RULE_SRC[rule % RULE_COUNT];
    str_cpy(s3_hint, RULE_HINT[rule % RULE_COUNT], sizeof s3_hint); s3_hint_until = x->now + 3000u;
    if (!s3_sh[id].used || s3_sh[id].rule != rule || !song_playing()) { x->redraw = 1; return; }
    s3_demo_src = SRC_TRACK[id]; s3_demo_shadow = (uint8_t)(id == SRC_MELODY ? LT_KEYS : id == SRC_CHORDS ? LT_CHORDS : LT_DRUMS);
    solo_clear(); solo_set(s3_demo_src, 1); s3_demo_phase = 1; s3_demo_until = x->now + slow_ms(2400, x);
    learn_notice(id == SRC_MELODY ? "LISTEN: TAKE, THEN SHADOW" : "SOURCE ALONE...", 1200); x->redraw = 1;
}
static void demo_poll(struct lctx *x)
{
    if (!s3_demo_phase || (int32_t)(x->now - s3_demo_until) < 0) return;
    if (s3_demo_phase == 1) { solo_set(s3_demo_src, 0); solo_set(s3_demo_shadow, 1); s3_demo_phase = 2; s3_demo_until = x->now + slow_ms(2400, x); learn_notice("...NOW THE SHADOW", 1200); }
    else { solo_set(s3_demo_shadow, 0); s3_demo_phase = 0; }
    x->redraw = 1;
}

/* ---- selection, shared by the pads (SMK) and GLO + keys (FM-1) ---- */
static void select_source(struct lctx *x, unsigned source)
{
    unsigned r;
    s3_src_armed = (uint8_t)(source + 1u); s3_src_armed_at = x->now; s3_arp_entry = 0;
    learn_notice(source == SRC_CHORDS ? "CHORDS: NOW PICK A RULE" : source == SRC_MELODY ? "MELODY: NOW PICK A RULE" : "DRUMS: NOW PICK A RULE", 2000);
    for (r = 0; r < RULE_COUNT; r++) if (RULE_SRC[r] == source) { uint8_t p[2]; s3_follow_pad = (uint8_t)(9 + r); p[0] = 0; p[1] = (uint8_t)(8 + r); lsyx_send(LCMD_PATH, p, 2); break; }
    x->redraw = 1;
}
static void pick_rule(struct lctx *x, unsigned rule)
{
    char line[32];
    unsigned n, source;
    uint8_t p[2];
    if (!s3_src_armed) { learn_notice(LEARN_PADS ? "PICK A SOURCE: PADS 1-3" : "PICK A SOURCE: GLO+KEY 1-3", 1200); return; }
    source = s3_src_armed - 1u;
    if (RULE_SRC[rule] != source) {
        n = ls_cat(line, 0, sizeof line, RULE_NAME[rule]); n = ls_cat(line, n, sizeof line, " FOLLOWS "); ls_cat(line, n, sizeof line, SRC_NAME[RULE_SRC[rule]]);
        learn_notice(line, 1200); return;
    }
    if (s3_sh[source].used) learn_notice("REPLACING THE SLOT'S SHADOW", 1000);
    shadow_create(x, rule, s3_sh[source].obedience ? s3_sh[source].obedience : 127u);
    shadow_send(source);
    s3_src_armed = 0; s3_follow_pad = 0; p[0] = 2; p[1] = 0; lsyx_send(LCMD_PATH, p, 2);
    n = ls_cat(line, 0, sizeof line, "SHADOW "); line[n++] = (char)('A' + source); line[n] = 0; n = ls_cat(line, n, sizeof line, ": "); ls_cat(line, n, sizeof line, RULE_NAME[rule]);
    learn_notice(line, 1200);
}
static void select_tape(struct lctx *x, unsigned tt)
{
    if (tt >= TAPE_TRACKS) return;
    if (s3_recording()) s3_rec_end(x);
    lmidi_all_off(); memset(s3_sounding, 0, sizeof s3_sounding);
    s3_tape = (uint8_t)tt;
    learn_notice(tt == 0 ? "TAPE: MELODY (KEYS PLAY IT)" : tt == 1 ? "KEYS PLAY THE CHORDS TRACK" : "KEYS PLAY THE BASS TRACK", 900);
    x->redraw = 1;
}

/* ---- public ---- */
static void stage3_init(void)
{
    memset(s3_sh, 0, sizeof s3_sh); s3_take_count = 0; s3_take_cur = 255;
    s3_sel = s3_tape = 0; s3_src_armed = 0; s3_arp_entry = 0; s3_rule_hinted = 0; s3_solo = 0;
    s3_punch_armed = s3_punch_on = s3_punch_bars = s3_rec_full = s3_rec_consumed = 0; s3_ab_pending = 0; s3_export_on = 0; s3_demo_phase = 0;
    memset(s3_sounding, 0, sizeof s3_sounding); s3_hint[0] = 0; s3_follow_pad = 0; s3_send_pick = 0;
    s3_last_step = 0; s3_bar_boundary = 0;
    if (!lsong.bars) song_init();                 /* (page 3 before page 2 ever ran) */
    memcpy(s3_mel_src, trk[LT_KEYS].step, sizeof s3_mel_src);
    s3_seen_rev = lsong_rev; s3_render_pending = 0;
}

static void stage3_key(struct lctx *x, unsigned note, unsigned velocity)
{
    int k = (int)note - (int)(53u + 12u * (unsigned)(x->octave + 2)) + 24;
    unsigned t = TAPE_TRK[s3_tape];
    if (note > 127) return;
    if (velocity && (x->buttons & LB(LB_GLO)) && !LEARN_PADS && k >= 0 && k < 27) {
        if (k < 3) select_source(x, (unsigned)k);
        else if (k < 6) { s3_sel = (uint8_t)(k - 3); learn_notice(s3_sh[s3_sel].used ? "SHADOW SELECTED" : "EMPTY SLOT: SOURCE THEN RULE", 900); x->redraw = 1; }
        else if (k >= 8 && k < 16) {
            if (s3_arp_entry && s3_arp_len < 8u) { s3_arp[s3_arp_len++] = (uint8_t)(k - 8); s3_arp_entry_at = x->now; render_slot(SRC_CHORDS); x->redraw = 1; }
            else pick_rule(x, (unsigned)(k - 8));
        }
        else if (k >= 16 && k < 19) select_tape(x, (unsigned)(k - 16));
        return;
    }
    if (velocity) {
        s3_sounding[note >> 5] |= 1u << (note & 31u);
        lmidi_note(t, note, velocity);
    } else if (s3_sounding[note >> 5] & (1u << (note & 31u))) {
        s3_sounding[note >> 5] &= ~(1u << (note & 31u));
        lmidi_note(t, note, 0);
    }
    x->redraw = 1;
}

static void stage3_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    (void)pressure;
    if (pad >= 16) return;
    if (velocity) {
        s3_pad_down[pad] = 1; s3_pad_since[pad] = x->now; s3_pad_hint_done[pad] = 0;
        if (pad < 6 && (x->buttons & LB(LB_GLO))) { solo_set(pad < 3 ? SRC_TRACK[pad] : (pad == 3 ? LT_CHORDS : pad == 4 ? LT_KEYS : LT_DRUMS), 1); x->redraw = 1; return; }
        if (pad < 3) select_source(x, pad);
        else if (pad < 6) { s3_sel = (uint8_t)(pad - 3); learn_notice(s3_sh[s3_sel].used ? (LEARN_PADS ? "SHADOW SELECTED: K2-K5 EDIT" : "SHADOW SELECTED: K1-K4 EDIT") : "EMPTY SLOT: SOURCE THEN RULE", 900); }
        else if (pad == 6) select_tape(x, (s3_tape + 1u) % TAPE_TRACKS);
        else if (pad == 7) { s3_export_on = 1; s3_export_track = s3_export_frame = 0; learn_notice("EXPORTING TRACKS...", 1000); }
        else {
            unsigned rule = pad - 8u;
            if (s3_arp_entry && s3_arp_len < 8u) { s3_arp[s3_arp_len++] = (uint8_t)rule; s3_arp_entry_at = x->now; render_slot(SRC_CHORDS); }
            else pick_rule(x, rule);
        }
    } else {
        s3_pad_down[pad] = 0;
        if (pad < 6) solo_set(pad < 3 ? SRC_TRACK[pad] : (pad == 3 ? LT_CHORDS : pad == 4 ? LT_KEYS : LT_DRUMS), 0);
    }
    x->redraw = 1;
}

static void stage3_encoder(struct lctx *x, unsigned id, int delta)
{
    struct shadow *sh = &s3_sh[s3_sel];
    unsigned st = TAPE_TRK[s3_tape];
    int v;
    if (id == 6 && (x->buttons & LB(LB_GLO))) { lesson_pick_turn(3, delta); return; }   /* GLO + the slow knob: the lesson */
    if (!LEARN_PADS && id == 1 && !(x->buttons & LB(LB_GLO))) {   /* the FM-1 has no obedience fader: K2 */
        v = (int)sh->obedience + delta * 8; sh->obedience = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        if (sh->used) { render_slot(s3_sel); shadow_send(s3_sel); }
        learn_notice(sh->obedience >= 120u ? "OBEDIENCE: STRICT" : sh->obedience >= 60u ? "OBEDIENCE: HUMAN" : "OBEDIENCE: LOOSE", 800);
        x->redraw = 1; return;
    }
    switch (id) {
    case 0: v = sh->octave + (delta > 0 ? 1 : -1); sh->octave = (int8_t)(v < -2 ? -2 : v > 2 ? 2 : v); if (sh->used) render_slot(s3_sel); break;
    case 1: v = (int)sh->density + (delta > 0 ? 1 : -1); sh->density = (uint8_t)(v < 1 ? 1 : v > 7 ? 7 : v); if (sh->used) render_slot(s3_sel); break;
    case 2: v = (int)sh->velocity + delta * 5; sh->velocity = (uint8_t)(v < 20 ? 20 : v > 150 ? 150 : v); if (sh->used) render_slot(s3_sel); break;
    case 3: if (s3_sel != SRC_DRUMS) {
        unsigned t = s3_sel == SRC_CHORDS ? LT_CHORDS : LT_KEYS, n = learn_patch_count(trk[t].eng_req);
        if (!n) n = 1;
        learn_bank_select(t, trk[t].eng_req, (trk[t].preset + n + (delta > 0 ? 1u : n - 1u)) % n);
        learn_notice("PATCH CHANGED", 600);
    } break;
    case 4: s3_send_pick = (uint8_t)(delta > 0 ? 1 : 0); learn_notice(s3_send_pick ? "SEND: DELAY" : "SEND: REVERB", 700); break;
    case 5: {
        int16_t *sp = &trk[st].p[s3_send_pick ? P_DLY : P_REV]; char b[24]; unsigned k;
        v = *sp + delta * 8; *sp = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        k = ls_cat(b, 0, sizeof b, TAPE_NM[s3_tape]); k = ls_cat(b, k, sizeof b, s3_send_pick ? " DELAY " : " REVERB ");
        k = ls_num(b, k, sizeof b, (unsigned)*sp * 100u / 127u); ls_cat(b, k, sizeof b, "%"); learn_notice(b, 900);
    } break;
    case 6: { uint8_t p; v = (int)x->slow_percent + delta * 5; x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); } break;
    default: break;
    }
    x->redraw = 1;
}

static void stage3_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (id >= 4) return;
    if (id < 3) {
        s3_sh[id].obedience = (uint8_t)(value & 127u);
        if (s3_sh[id].used) { render_slot(id); shadow_send(id); }
    } else {
        trk[TAPE_TRK[s3_tape]].p[s3_send_pick ? P_DLY : P_REV] = (int16_t)(value & 127u);
    }
    x->redraw = 1;
}

static void stage3_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_SHADOW:
        if (n >= 4 && p[2] < RULE_COUNT) shadow_create(x, p[2], p[3]);
        break;
    case LCMD_SOLO:
        if (n >= 2 && p[0] < NTRK) { solo_set(p[0], p[1]); x->redraw = 1; }
        break;
    case LCMD_TRANSPORT:
        if (n >= 1 && learn_page() == 3) {
            s3_from_partner = 1;
            if (p[0] == 0) stop_all(x);
            else if (p[0] == 1) { if (!song_playing()) song_play(1); }
            else if (p[0] == 2) { if (!s3_recording()) s3_rec_begin(x, 1); }
            else if (p[0] == 3) take_undo_s3(x);
            s3_from_partner = 0; x->redraw = 1;
        }
        break;
    case LCMD_PATH:
        if (n >= 2 && learn_page() == 3) { s3_follow_pad = p[0] == 0 ? (uint8_t)(p[1] + 1u) : 0u; x->redraw = 1; }
        break;
    default: break;
    }
}

static void stage3_control(struct lctx *x)
{
    unsigned b = x->btn_down, glo = x->buttons & LB(LB_GLO), home = x->buttons & LB(LB_HOME), step, p;
    s3_root = x->root; s3_scale = x->scale;
    if (lsong.root != x->root || lsong.minor != x->minor) { song_apply_key(x->root, x->minor); song_write_chords(); song_write_bass(); }
    step = song_step(); s3_bar_boundary = 0;
    if (song_playing() && (step < s3_last_step || step / 16u != s3_last_step / 16u)) s3_bar_boundary = 1;
    s3_last_step = step;

    if (b & LB(LB_REC)) {
        if (home) { s3_export_on = 1; s3_export_track = s3_export_frame = 0; learn_notice("EXPORTING TRACKS...", 1000); s3_rec_consumed = 1; }
        else if (s3_recording()) { s3_rec_end(x); s3_rec_consumed = 1; }
        else { s3_rec_consumed = 0; if (song_playing()) { s3_punch_armed = 1; learn_notice("PUNCH AT THE NEXT BAR", 1200); } }
    }
    if (b & LB(LB_PLAY)) {
        uint8_t tp;
        if ((x->buttons & LB(LB_REC)) && !s3_recording()) { s3_punch_armed = 0; s3_rec_consumed = 1; s3_rec_begin(x, 1); learn_notice("TAPING...", 1000); }
        else if (s3_recording()) s3_rec_end(x);
        else if (song_playing()) { song_play(0); tp = 0; lsyx_send(LCMD_TRANSPORT, &tp, 1); }
        else { song_play(1); tp = 1; lsyx_send(LCMD_TRANSPORT, &tp, 1); }
        x->redraw = 1;
    }
    if (x->btn_up & LB(LB_REC)) {
        if (s3_rec_consumed) s3_rec_consumed = 0;
        else if (s3_punch_on) { s3_punch_armed = 2; learn_notice("PUNCH ENDS AT THE BAR", 1000); }
        else {
            if (s3_punch_armed) s3_punch_armed = 0;
            if (s3_take_count >= 2u) { uint8_t r; s3_ab_pending = 1; s3_ab_side ^= 1u; learn_notice(s3_ab_side ? "A/B: PREVIOUS TAKE" : "A/B: CURRENT TAKE", 900); r = s3_ab_side; lsyx_send(LCMD_REPLAY, &r, 1); }
            else learn_notice("TAPE TWO TAKES TO COMPARE", 900);
        }
        x->redraw = 1;
    }
    {   /* a lesson's TEXT step: any key or pad continues (the press also does what it does) */
        const struct lesson_step *ls = lesson_step();
        if (ls && ls->type == DR_TEXT && x->keys_down) lesson_step_done(1, 100);
    }
    if (b & LB(LB_HOME)) stop_all(x);
    if (b & LB(LB_SAVE)) { if (glo) take_redo_s3(x); else { uint8_t tp = 3; take_undo_s3(x); lsyx_send(LCMD_TRANSPORT, &tp, 1); } }
    if (b & LB(LB_SCL)) select_tape(x, (s3_tape + 1u) % TAPE_TRACKS);
    if (b & LB(LB_ENV)) { s3_hints = !s3_hints; learn_notice(s3_hints ? "HINTS ON" : "HINTS OFF", 700); x->redraw = 1; }
    if (b & LB(LB_FX)) shadow_remove(x, s3_sel);
    if (b & LB(LB_EDIT)) { s3_sel = (uint8_t)((s3_sel + 1u) % SHADOWS); x->redraw = 1; }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(lesson_pick(3)); if (!lesson_running()) learn_notice("NO LESSON ON THIS PAGE", 900); }
        x->redraw = 1;
    }
    if (b & LB(LB_LFO)) { s3_arp_entry = 0; if (s3_sh[s3_sel].used) demo_start(x, s3_sh[s3_sel].rule); }

    if (s3_bar_boundary) {                        /* the punch state machine, on bar boundaries */
        if (s3_punch_armed == 1u && !s3_punch_on) {
            if (s3_tape != 0) s3_tape = 0;
            memcpy(trk[LT_KEYS].step, s3_mel_src, sizeof s3_mel_src);
            memcpy(s3_scratch, trk[LT_KEYS].step, sizeof s3_scratch);
            s3_punch_on = 1; s3_punch_armed = 0; s3_punch_bars = (uint8_t)(1u << (step / 16u));
            song_record(1); s3_rec_slow = x->slow_percent < 100u; learn_notice("PUNCH IN", 600); x->redraw = 1;
        } else if (s3_punch_on && s3_punch_armed == 2u) { s3_rec_end(x); learn_notice("PUNCH OUT", 600); }
        else if (s3_punch_on) s3_punch_bars |= (uint8_t)(1u << (step / 16u));
        if (s3_ab_pending && s3_take_count >= 2u) {
            unsigned cur = s3_take_cur == 255u ? s3_take_count - 1u : s3_take_cur;
            unsigned base = s3_ab_side ? (cur ? cur - 1u : cur + 1u) : cur;
            if (base < s3_take_count) { memcpy(s3_mel_src, s3_takes[base], sizeof s3_mel_src); render_melody_shadow(); }
            s3_ab_pending = 0; x->redraw = 1;
        }
        if (s3_punch_on) x->redraw = 1;
    }
    if (s3_ab_pending && !song_playing()) {
        unsigned cur = s3_take_cur == 255u ? s3_take_count - 1u : s3_take_cur;
        unsigned base = s3_ab_side ? (cur ? cur - 1u : cur + 1u) : cur;
        if (base < s3_take_count) { memcpy(s3_mel_src, s3_takes[base], sizeof s3_mel_src); render_melody_shadow(); }
        s3_ab_pending = 0; x->redraw = 1;
    }
    if (s3_punch_armed && !song_playing()) s3_punch_armed = 0;

    for (p = 8; p < 16u; p++)                     /* a rule pad held two seconds: the why-hint demo */
        if (s3_pad_down[p] && !s3_pad_hint_done[p] && x->now - s3_pad_since[p] >= 2000u) { s3_pad_hint_done[p] = 1; demo_start(x, p - 8u); }
    for (p = 3; p < 6u; p++)                      /* a slot pad held two seconds: the shadow goes */
        if (s3_pad_down[p] && !s3_pad_hint_done[p] && !glo && x->now - s3_pad_since[p] >= 2000u) { s3_pad_hint_done[p] = 1; shadow_remove(x, p - 3u); }
    demo_poll(x);
    if (s3_src_armed && x->now - s3_src_armed_at >= 5000u) { s3_src_armed = 0; s3_follow_pad = 0; x->redraw = 1; }
    if (s3_arp_entry && x->now - s3_arp_entry_at >= 4000u) { s3_arp_entry = 0; x->redraw = 1; }

    if (lsong_rev != s3_seen_rev) { if (!s3_render_pending) s3_rev_at = x->now; s3_render_pending = 1; s3_seen_rev = lsong_rev; }
    if (s3_render_pending && !s3_recording() && x->now - s3_rev_at >= 300u) {
        unsigned any = 0, i;
        for (i = 0; i < SHADOWS; i++) any |= s3_sh[i].used;
        if (any) render_all(); else s3_render_pending = 0;
        x->redraw = 1;
    }
    export_poll();
    if (s3_hint[0] && (int32_t)(x->now - s3_hint_until) >= 0) { s3_hint[0] = 0; x->redraw = 1; }
    {
        uint8_t st, d1, d2; unsigned changed = 0;
        while (lmidi_pull_external(&st, &d1, &d2)) changed = 1;
        if (changed) x->redraw = 1;
    }
    {
        unsigned h = song_playing() ? step : 0u;
        static unsigned shown; if (h / 2u != shown / 2u) { shown = h; x->redraw = 1; }
    }
    llit_keys = lbright_keys = 0;
}

static uint32_t src_colour(unsigned source) { return source == SRC_CHORDS ? P_CYAN : source == SRC_MELODY ? P_GREEN : P_RED; }
static uint32_t stage3_pad_colour(unsigned pad)
{
    unsigned blink = (fm1_ms / 125u) & 1u, p, rule;
    uint32_t c;
    if (pad >= 16) return P_OFF;
    if (pad < 3) {
        c = src_colour(pad);
        for (p = 8; p < 16u; p++) if (s3_pad_down[p] && RULE_SRC[p - 8u] == pad) return blink ? P_FULL(c) : P_DIM(c);
        if (s3_src_armed == pad + 1u) return blink ? P_FULL(c) : c;
        return (s3_solo >> SRC_TRACK[pad]) & 1u ? P_FULL(c) : c;
    }
    if (pad < 6) {
        unsigned id = pad - 3u;
        if (!s3_sh[id].used) return id == s3_sel ? P_DIM(P_WHITE) : P_OFF;
        c = src_colour(id);
        return id == s3_sel ? c : P_DIM(c);
    }
    if (pad == 6) { c = s3_tape == 0 ? P_GREEN : s3_tape == 1 ? P_CYAN : P_BLUE; return s3_recording() ? (blink ? P_RED : P_DIM(P_RED)) : P_DIM(c); }
    if (pad == 7) return s3_export_on ? P_WHITE : P_DIM(P_WHITE);
    rule = pad - 8u; c = src_colour(RULE_SRC[rule]);
    if (s3_pad_down[pad]) return P_FULL(c);
    if (s3_follow_pad == pad + 1u) return blink ? P_FULL(c) : c;
    if (s3_arp_entry) return rule < s3_arp_len ? P_ORANGE : P_DIM(P_ORANGE);
    if (s3_src_armed && s3_src_armed - 1u == RULE_SRC[rule]) return c;
    return P_DIM(c);
}

/* ---- the screen ---- */
static uint16_t src_rgb(unsigned source) { return source == SRC_CHORDS ? LC_CYAN : source == SRC_MELODY ? LC_GREEN : LC_RED; }
static void stage3_draw(struct lctx *x)
{
    unsigned steps = song_steps(), head = song_playing() ? song_step() % steps : 0xffu, i, n;
    char line[40], big[16], sub[32];
    const struct shadow *sh = &s3_sh[s3_sel];
    const struct lesson_step *ls = lesson_step();
    n = ls_num(line, 0, sizeof line, lsong.tempo); n = ls_cat(line, n, sizeof line, "BPM ");
    n = ls_num(line, n, sizeof line, lsong.bars); n = ls_cat(line, n, sizeof line, lsong.bars == 1u ? "BAR" : "BARS");
    if (x->slow_percent < 100u) { n = ls_cat(line, n, sizeof line, " SLOW "); n = ls_num(line, n, sizeof line, x->slow_percent); ls_cat(line, n, sizeof line, "%"); }
    ld_text(4, 2, line, 0, LC_GREY);
    ld_fill(228, 6, 8, 8, lsyx_partner_present() ? LC_GREEN : LC_PANEL);
    big[0] = 0; sub[0] = 0;
    if (s3_src_armed) { ls_cat(big, 0, sizeof big, SRC_NAME[s3_src_armed - 1u]); ls_cat(sub, 0, sizeof sub, LEARN_PADS ? "NOW A RULE: PAD 9-16" : "NOW A RULE: " LBL_GLO "+KEY 9-16"); }
    else if (sh->used) {
        big[0] = (char)('A' + s3_sel); big[1] = ' '; big[2] = 0; ls_cat(big, 2, sizeof big, RULE_NAME[sh->rule]);
        n = ls_cat(sub, 0, sizeof sub, SRC_NAME[s3_sel]); n = ls_cat(sub, n, sizeof sub, " OCT");
        n = ls_cat(sub, n, sizeof sub, sh->octave < 0 ? "-" : "+"); n = ls_num(sub, n, sizeof sub, (unsigned)(sh->octave < 0 ? -sh->octave : sh->octave));
        n = ls_cat(sub, n, sizeof sub, " DENS "); n = ls_num(sub, n, sizeof sub, sh->density);
        ls_cat(sub, n, sizeof sub, sh->obedience >= 120u ? " STRICT" : sh->obedience >= 60u ? " HUMAN" : " LOOSE");
    } else { ls_cat(big, 0, sizeof big, "PICK A SOURCE"); ls_cat(sub, 0, sizeof sub, "THEN A RULE: A NEW PART"); }
    ld_text(4, 20, big, 1, LC_TEXT);
    ld_text(4, 54, sub, 0, sh->used && !s3_src_armed ? src_rgb(s3_sel) : LC_CYAN);
    /* a source armed: the rules, by pad / key number, the ones that fit it bright */
    if (s3_src_armed) {
        for (i = 0; i < RULE_COUNT; i++) {
            unsigned ok = RULE_SRC[i] == s3_src_armed - 1u;
            int cx = i < 4u ? 4 : 124, cy = 74 + (int)(i % 4u) * 20;
            n = ls_num(line, 0, sizeof line, 9u + i); n = ls_cat(line, n, sizeof line, i < 1u ? "  " : " "); ls_cat(line, n, sizeof line, RULE_NAME[i]);
            ld_text(cx, cy, line, 0, ok ? src_rgb(RULE_SRC[i]) : LC_DIM);
        }
    } else
    /* the tape lanes: MEL, CHD, BASS, DRUMS: a note-density bar, the head, the punch, the take, the level */
    for (i = 0; i < 4u; i++) {
        unsigned t = i < 3u ? TAPE_TRK[i] : LT_DRUMS, active = 0, s, punch = (s3_punch_on && i == 0u) ? s3_punch_bars : 0u;
        unsigned rec = s3_recording() && i == 0u, lvl, shadowed = i == 0u ? s3_sh[SRC_MELODY].used : i == 1u ? s3_sh[SRC_CHORDS].used : i == 3u ? s3_sh[SRC_DRUMS].used : 0u;
        int y = 74 + (int)i * 22;
        uint16_t c = i == 0u ? LC_GREEN : i == 1u ? LC_CYAN : i == 2u ? LC_BLUE : LC_RED;
        if (i == 3u) { for (s = 0; s < steps; s++) if (dstep_mask(&TDRUM->dstep[s])) active++; }
        else for (s = 0; s < steps; s++) if (trk[t].step[s].time == ST_NOTE && trk[t].step[s].n) active++;
        ld_text(4, y - 2, i < 3u ? TAPE_NM[i] : "DRM", 0, (i < 3u && s3_tape == i) ? LC_TEXT : c);
        if (i < 3u && s3_tape == i && rec) ld_fill(0, y, 2, 12, LC_RED);
        ld_frame(46, y, 120, 12, LC_PANEL);
        if (active) ld_fill(47, y + 1, (int)(active * 118u / steps) + 1, 10, shadowed ? c : LC_DIM);
        for (s = 0; s < steps / 16u; s++) if ((punch >> s) & 1u) ld_fill(47 + (int)s * 118 / (int)(steps / 16u), y + 13, 118 / (int)(steps / 16u), 2, LC_RED);
        if (head != 0xffu) ld_fill(47 + (int)(head * 118u / steps), y + 1, 2, 10, rec ? LC_RED : LC_ORANGE);
        lvl = (unsigned)(trk[t].peak < 0 ? 0 : trk[t].peak) >> 10; if (lvl > 36u) lvl = 36u;
        ld_frame(196, y, 40, 12, LC_PANEL);
        if (lvl) ld_fill(198, y + 2, (int)lvl, 8, (s3_solo >> t) & 1u ? LC_YELLOW : LC_GREEN);
    }
    line[0] = 0;
    if (s3_recording()) ls_cat(line, 0, sizeof line, s3_punch_on ? "PUNCH: RELEASE " LBL_REC " TO END" : "TAPING: " LBL_PLAY " TO FINISH");
    else if (s3_punch_armed) ls_cat(line, 0, sizeof line, "PUNCH WAITS FOR THE BAR");
    else if (s3_arp_entry) ls_cat(line, 0, sizeof line, LEARN_PADS ? "TAP PATTERN ON PADS 9-16" : "GLO+KEYS 9-16: THE PATTERN");
    else if (s3_demo_phase) ls_cat(line, 0, sizeof line, s3_demo_phase == 1u ? "LISTEN: SOURCE ALONE" : "LISTEN: SHADOW ALONE");
    else if (ls) ls_cat(line, 0, sizeof line, ls->prompt ? ls->prompt : "");
    else ls_cat(line, 0, sizeof line, LEARN_PADS ? "PADS 1-3 SOURCE, 9-16 RULE" : LBL_GLO "+KEY 1-3 SRC, 9-16 RULE");
    ld_text(4, 166, line, 0, s3_recording() ? LC_RED : LC_AMBER);
    if (lesson_running()) {
        const struct lesson *l = &lessons[lesson_running() - 1u];
        n = ls_cat(line, 0, sizeof line, "L"); n = ls_num(line, n, sizeof line, lesson_running()); n = ls_cat(line, n, sizeof line, " ");
        n = ls_cat(line, n, sizeof line, l->title); n = ls_cat(line, n, sizeof line, " ");
        n = ls_num(line, n, sizeof line, lesson_progress() + 1u); n = ls_cat(line, n, sizeof line, "/"); ls_num(line, n, sizeof line, l->step_count);
        line[29] = 0;
        ld_text(4, 184, line, 0, LC_PURPLE);
    } else {
        n = ls_cat(line, 0, sizeof line, "ARP: "); lesson_name(line + n, sizeof line - n, lesson_pick(3)); line[29] = 0;
        ld_text(4, 184, line, 0, LC_DIM);
    }
    /* the takes (or how to make one) */
    if (s3_take_count) {
        n = ls_cat(line, 0, sizeof line, "TAKE "); n = ls_num(line, n, sizeof line, s3_take_cur != 255u ? s3_take_cur + 1u : 0u);
        n = ls_cat(line, n, sizeof line, " OF "); n = ls_num(line, n, sizeof line, s3_take_count);
        ls_cat(line, n, sizeof line, "  " LBL_REC ": A/B");
        ld_text(4, 202, line, 0, LC_AMBER);
    } else ld_text(4, 202, LBL_REC "+" LBL_PLAY ": TAPE A PASS", 0, LC_DIM);
    if (s3_hint[0]) ld_text(4, 222, s3_hint, 0, LC_YELLOW);   /* (notices: the header bar, main.c) */
}
