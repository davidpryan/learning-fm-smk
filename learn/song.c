/* SPDX-License-Identifier: GPL-3.0-only
 * The shared song model of Stages 2-4: the loop as a beginner sees it (a chord per beat, a bass rule, four drum
 * lanes, tempo, swing, fills), rendered into SLOOP's own tracks so SLOOP does the playing:
 *   LT_KEYS   (track 1) the player's melody: SLOOP's live recorder writes it      16 steps a bar, 1/16
 *   LT_CHORDS (track 2) a chord on the first 16th of each beat, held with TIE steps
 *   LT_BASS   (track 3) the bass notes the rule derives
 *   LT_DRUMS  (the drum track) the lanes KICK, SNARE, HAT and PERC
 * The steps are written from the main loop as SLOOP's own editor does it. Nothing here is saved to Flash by the
 * layer (SLOOP's autosave of the project is SLOOP's own business, as ever). */

enum { SONG_MAX_BARS = 4, SONG_BEATS = SONG_MAX_BARS * 4, SONG_STEPS = SONG_MAX_BARS * 16 };
enum { LANE_KICK, LANE_SNARE, LANE_HAT, LANE_PERC, LANE_COUNT };
enum { BASS_KICK_LOCK, BASS_OFFBEAT, BASS_WALK, BASS_RULES };
enum { DL_KICK = 0, DL_SNARE = 2, DL_CLAP = 3, DL_HAT = 4, DL_OPEN = 5, DL_RIM = 7, DL_RIDE = 12, DL_SHAKER = 13, DL_COWBELL = 15 };

struct song_chord { uint8_t degree, seventh; };   /* degree 0 = rest */
struct song_lane { uint16_t mask; uint8_t hits, shift, manual; };
static struct {
    struct song_chord chord[SONG_BEATS];
    unsigned bars;                 /* 1, 2 or 4 */
    unsigned root, minor;
    unsigned tempo;                /* 60..180 */
    unsigned bass_rule, bass_octave;   /* octave 0..2: 36 + 12 * octave */
    struct song_lane lane[LANE_COUNT];
    unsigned swing;                /* 50..75 % */
    unsigned fill_every;           /* 0, 2, 4, 8 bars */
    unsigned chord_octave;         /* 60 */
    uint8_t perc_lane;             /* the PERC lane's sound: a drum lane (DL_CLAP, DL_RIM, ...) */
} lsong;
static const uint8_t SONG_LANE_DRUM[LANE_COUNT] = {DL_KICK, DL_SNARE, DL_HAT, DL_CLAP};
static uint32_t lsong_rev;                     /* bumped by every render: Stage 3's shadows follow it */

static uint16_t song_euclid(unsigned hits, unsigned steps, unsigned shift)
{
    uint16_t mask = 0;
    unsigned bucket = 0, i;
    if (!steps || steps > 16) return 0;
    if (hits > steps) hits = steps;
    for (i = 0; i < steps; i++) {                 /* Bresenham-style spread */
        bucket += hits;
        if (bucket >= steps) { bucket -= steps; mask |= (uint16_t)(1u << i); }
    }
    if (mask) while (!(mask & 1u)) mask = (uint16_t)((mask >> 1) | ((mask & 1u) << (steps - 1)));   /* first hit on step 0 */
    for (i = 0; i < shift % steps; i++) mask = (uint16_t)(((mask << 1) | (mask >> (steps - 1))) & ((1u << steps) - 1u));
    return mask;
}

static unsigned song_steps(void) { return lsong.bars * 16u; }
static unsigned song_beats(void) { return lsong.bars * 4u; }
static void song_lane_generate(unsigned lane)
{
    if (lane >= LANE_COUNT || lsong.lane[lane].manual) return;
    lsong.lane[lane].mask = song_euclid(lsong.lane[lane].hits, 16, lsong.lane[lane].shift);
}
static void song_lane_toggle(unsigned lane, unsigned step)
{
    if (lane >= LANE_COUNT || step >= 16) return;
    lsong.lane[lane].mask ^= (uint16_t)(1u << step);
    lsong.lane[lane].manual = 1;
}
static void song_apply_key(unsigned root, unsigned minor) { lsong.root = root % 12u; lsong.minor = minor; }
static void song_set_bars(unsigned bars) { lsong.bars = bars == 2u ? 2u : bars == 4u ? 4u : 1u; }
static void song_set_tempo(unsigned bpm)
{
    if (bpm < 60u) bpm = 60u;
    if (bpm > 180u) bpm = 180u;
    lsong.tempo = bpm;
    learn_set_bpm(bpm);
}
static void song_set_swing(unsigned percent)
{
    if (percent < 50u) percent = 50u;
    if (percent > 75u) percent = 75u;
    lsong.swing = percent;
    song.g[G_SWING] = (int16_t)((percent - 50u) * 4u);   /* SLOOP: 100 = odd steps half a step late */
}
static void song_set_click(unsigned mode) { song.g[G_CLOCK] = (int16_t)(mode > 2u ? 2u : mode); }   /* 0 off, 1 while recording, 2 on */

static int song_degree_at_step(unsigned step, unsigned *seventh)
{
    unsigned beat = (step / 4u) % song_beats();
    if (seventh) *seventh = lsong.chord[beat].seventh;
    return lsong.chord[beat].degree;
}
static void song_chord_notes(unsigned degree, unsigned seventh, uint8_t notes[4], unsigned *count)
{
    *count = degree ? theory_degree_chord(lsong.root, lsong.minor, degree, seventh, lsong.chord_octave, notes) : 0u;
}

/* ---- rendering into SLOOP's tracks ---- */
static void song_track_setup(unsigned t)
{
    trk[t].p[P_SLEN] = (int16_t)song_steps();
    trk[t].p[P_SDIV] = 2;                         /* 1/16 */
}
static void song_steps_rest(track_t *t)           /* every step a REST (the synth tracks) */
{
    unsigned k;
    for (k = 0; k < NSTEP; k++) {
        memset(&t->step[k], 0, sizeof t->step[k]);
        t->step[k].time = ST_REST;
    }
}
static void song_write_chords(void)
{
    lsong_rev++;
    track_t *t = &trk[LT_CHORDS];
    unsigned beat, k;
    song_track_setup(LT_CHORDS);
    song_steps_rest(t);
    for (beat = 0; beat < song_beats(); beat++) {
        uint8_t notes[4]; unsigned n, s = beat * 4u;
        song_chord_notes(lsong.chord[beat].degree, lsong.chord[beat].seventh, notes, &n);
        if (!n) continue;
        memcpy(t->step[s].note, notes, 4);
        t->step[s].n = (uint8_t)n;
        t->step[s].time = ST_NOTE;
        t->step[s].vel = 96;
        for (k = 1; k < 4u; k++) t->step[s + k].time = ST_TIE;   /* held through the beat */
    }
    t->p[P_SGATE] = 100;
}
static void song_write_bass(void)
{
    lsong_rev++;
    track_t *t = &trk[LT_BASS];
    unsigned s, steps = song_steps();
    uint16_t kick = lsong.lane[LANE_KICK].mask;
    song_track_setup(LT_BASS);
    song_steps_rest(t);
    for (s = 0; s < steps; s++) {
        unsigned seventh, in_bar = s % 16u, play = 0, note;
        int degree = song_degree_at_step(s, &seventh);
        uint8_t notes[4]; unsigned n;
        if (!degree) continue;
        n = theory_degree_chord(lsong.root, lsong.minor, (unsigned)degree, 0, 36u + 12u * lsong.bass_octave, notes);
        if (!n) continue;
        note = notes[0];
        switch (lsong.bass_rule) {
        case BASS_KICK_LOCK: play = (kick >> in_bar) & 1u; break;
        case BASS_OFFBEAT: play = (kick >> ((in_bar + 14u) % 16u)) & 1u; break;   /* two 16ths after each kick */
        default:
            play = (in_bar % 4u) == 0u || (in_bar % 4u) == 2u;
            if ((in_bar % 4u) == 2u) note = notes[2];                           /* the fifth */
            if ((in_bar / 4u) % 2u == 1u && (in_bar % 4u) == 0u) note = notes[0] + 12u;
            break;
        }
        if (play) {
            t->step[s].note[0] = (uint8_t)note; t->step[s].n = 1; t->step[s].time = ST_NOTE; t->step[s].vel = 100;
        }
    }
    t->p[P_SGATE] = (int16_t)(lsong.bass_rule == BASS_WALK ? 96 : 64);
}
static void song_write_drums(void)
{
    lsong_rev++;
    track_t *t = TDRUM;
    unsigned l, s, steps = song_steps();
    song_track_setup(LT_DRUMS);
    memset(t->dstep, 0, sizeof t->dstep);
    for (l = 0; l < LANE_COUNT; l++) {
        unsigned lane = l == LANE_PERC ? lsong.perc_lane : SONG_LANE_DRUM[l];
        for (s = 0; s < steps; s++) {
            unsigned bar = s / 16u, in_bar = s % 16u, on = (lsong.lane[l].mask >> in_bar) & 1u;
            /* a fill: the last beat of the last bar of each fill period, the hat and snare double up */
            if (lsong.fill_every && ((bar + 1u) % lsong.fill_every) == 0u && in_bar >= 12u && (l == LANE_HAT || l == LANE_SNARE)) on = 1;
            if (on) dstep_set(&t->dstep[s], lane, l == LANE_HAT ? LV_SOFT : LV_NORM, 0);
        }
    }
}
static void song_write_all(void) { song_write_drums(); song_write_chords(); song_write_bass(); }

static void song_init(void)
{
    static const uint8_t prog[4] = {1, 5, 6, 4};  /* I - V - vi - IV: something to lock to */
    unsigned l, b;
    memset(&lsong, 0, sizeof lsong);
    lsong.bars = 1; lsong.tempo = 100; lsong.chord_octave = 60; lsong.swing = 50; lsong.perc_lane = DL_CLAP;
    lsong.lane[LANE_KICK].hits = 4; lsong.lane[LANE_SNARE].hits = 2; lsong.lane[LANE_SNARE].shift = 4; lsong.lane[LANE_HAT].hits = 8;
    for (l = 0; l < LANE_COUNT; l++) song_lane_generate(l);
    for (b = 0; b < SONG_BEATS; b++) { lsong.chord[b].degree = prog[b % 4u]; lsong.chord[b].seventh = 0; }
    song_track_setup(LT_KEYS);
}

/* ---- transport and position ---- */
static void song_play(unsigned on)
{
    if (on && !song.playing) transport_req = 1;
    else if (!on && song.playing) transport_req = 2;
}
static unsigned song_playing(void) { return song.playing; }
static unsigned song_step(void)                   /* the 16th playing now, 0 .. bars * 16 - 1 */
{
    uint32_t b = clk_beat, p = clk_pos;
    return (unsigned)((b * 4u + p * 4u / BEAT_U) % song_steps());
}
static unsigned song_bar(void) { return song_step() / 16u; }

/* ---- the melody recorder: SLOOP's own live recorder on LT_KEYS ---- */
static void song_record(unsigned on)
{
    if (on) { song.rec = (uint8_t)(1u << LT_KEYS); rec_wait = 0; }
    else song.rec = 0;
}
static unsigned song_recording(void) { return (song.rec >> LT_KEYS) & 1u; }

/* snap-to-feel: a note on an odd 16th moves to the nearest 8th when the strength says so (>= 50 %: late notes
 * only; >= 75 %: every odd 16th); the recorder already puts notes on 16ths */
static void song_quantise_track(unsigned track, unsigned strength_percent)
{
    track_t *t = &trk[track % NTRK];
    unsigned steps = song_steps(), s;
    if (strength_percent < 50u || track >= NPART) return;
    for (s = 0; s < steps; s++) {
        unsigned target;
        if (t->step[s].time != ST_NOTE || !t->step[s].n || (s % 2u) == 0u) continue;
        target = (s % 4u == 1u) ? s - 1u : (s + 1u < steps ? s + 1u : s - 1u);
        if (strength_percent < 75u && (s % 4u) == 3u) continue;
        if (t->step[target].time != ST_NOTE || !t->step[target].n) {
            t->step[target] = t->step[s];
            memset(&t->step[s], 0, sizeof t->step[s]);
            t->step[s].time = ST_REST;
        }
    }
}
static unsigned song_track_notes(unsigned track, uint32_t *mask16_of_bar, unsigned bar)   /* notes in the track, bit per 16th of a bar */
{
    const track_t *t = &trk[track % NTRK];
    unsigned s, n = 0, steps = song_steps();
    uint32_t m = 0;
    for (s = 0; s < steps; s++)
        if (t->step[s].time == ST_NOTE && t->step[s].n) {
            n++;
            if (s / 16u == bar) m |= 1u << (s % 16u);
        }
    if (mask16_of_bar) *mask16_of_bar = m;
    return n;
}
