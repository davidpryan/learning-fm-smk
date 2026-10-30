/* SPDX-License-Identifier: GPL-3.0-only
 * Stage 1: Lantern (SMK-37 Elite) / Ember (FM-1). Hear what you play.
 *
 * Both boards run the same page. The SMK-37 has the pads, so it carries the chord palette; the FM-1 lights the
 * key to press under your fingers. Every drill also runs on one box alone.
 *
 * Controls (SLOOP's panel labels; the SMK's buttons as HARDWARE CALIBRATION maps them):
 *   keys        play; a key outside the scale is nudged to the nearest scale note (hints on) or muted (off)
 *   SCL         next scale type           GLO + key    set the root
 *   OCT -/+     octave                    ENV          why-hints and nudge on / off
 *   FX          staff (FM-1) on / off     EDIT / SAVE  next sound bank / next patch
 *   PLAY        start or stop a drill     REC          replay: the reference, then your attempt
 *   ARP         start the picked lesson / stop it      HOME   stop everything
 *   GLO + SCL   FM-1: CHORD KEYS (a key plays its degree's triad)
 *   knobs       1 bank  2 patch  3 reverb  4 delay  5 slow %  6 lesson  7 drill kind
 *   pads (SMK)  1-7 the triads of degrees 1-7, 8 sus4; 9-15 their sevenths, 16 sus2 */

enum { HELD_MAX = 10 };
static uint8_t s1_held[HELD_MAX]; static unsigned s1_held_n;
static uint8_t s1_chord_mode;                     /* FM-1: keys play degree chords */
static uint8_t s1_hints = 1, s1_staff = 1;
static uint8_t s1_bank, s1_patch, s1_reverb = 40, s1_delay = 20;
static uint8_t s1_roman[4]; static uint8_t s1_roman_n;   /* degree << 1 | seventh, newest first */
static uint8_t s1_minor;
static uint8_t s1_pad_down[16];
static uint32_t s1_hint_until; static char s1_hint[40];
static unsigned s1_lesson_pick;
static unsigned s1_follow_key;                    /* 1 + key index (from F3) to light, 0 none */
static unsigned s1_follow_pad;                    /* 1 + pad */
static uint8_t s1_drill_kind = DR_ECHO;

/* ---- drill ---- */
enum { DS_IDLE, DS_PLAYING_REF, DS_WAIT_ANSWER, DS_RESULT };
struct s1_event { uint8_t note, vel; uint16_t at; };   /* at: ms / 4 from the start */
enum { EV_MAX = 24 };
static struct {
    unsigned state, kind;
    unsigned index, total, hits, misses;
    uint8_t target[4], target_count;
    unsigned target_degree, target_seventh;
    uint32_t started, due;
    struct s1_event ref[EV_MAX]; unsigned ref_count;
    struct s1_event mine[EV_MAX]; unsigned mine_count;
    uint8_t answer[8]; unsigned answer_count; uint32_t answer_first;
    unsigned prog[8], prog_count, prog_pos;
    unsigned tempo;
} s1d;
static unsigned s1_replay_which, s1_replay_on, s1_replay_pos; static uint32_t s1_replay_started;

static unsigned slow_ms(unsigned ms, const struct lctx *x) { return ms * 100u / (x->slow_percent ? x->slow_percent : 100u); }
static unsigned s1_rand(void) { static uint32_t s = 12345; s = s * 1664525u + 1013904223u; return s >> 8; }

static void held_add(unsigned note)
{
    unsigned i;
    for (i = 0; i < s1_held_n; i++) if (s1_held[i] == note) return;
    if (s1_held_n < HELD_MAX) s1_held[s1_held_n++] = (uint8_t)note;
}
static void held_remove(unsigned note)
{
    unsigned i;
    for (i = 0; i < s1_held_n; i++) if (s1_held[i] == note) { s1_held[i] = s1_held[s1_held_n - 1u]; s1_held_n--; return; }
}
static void roman_push(unsigned degree, unsigned seventh)
{
    unsigned i;
    for (i = 3; i > 0; i--) s1_roman[i] = s1_roman[i - 1];
    s1_roman[0] = (uint8_t)((degree << 1) | (seventh & 1u));
    if (s1_roman_n < 4) s1_roman_n++;
}
static unsigned chord_octave(const struct lctx *x) { return (unsigned)(48 + 12 * x->octave); }
static int key_index(const struct lctx *x, unsigned note)   /* note -> key 0..26 from F3 at this octave, or -1 */
{
    int k = (int)note - (53 + 12 * x->octave);
    return k >= 0 && k < 27 ? k : -1;
}

static void play_degree(struct lctx *x, unsigned degree, unsigned seventh, unsigned slot, unsigned velocity)
{
    uint8_t notes[4], p[3];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, seventh, chord_octave(x), notes);
    if (!n) return;
    lmidi_chord_on(LT_CHORDS, notes, n, velocity, slot);
    roman_push(degree, seventh); s1_minor = (uint8_t)x->minor;
    if (s1_hints) { str_cpy(s1_hint, theory_why_hint(x->minor, degree), sizeof s1_hint); s1_hint_until = x->now + 2500u; }
    p[0] = (uint8_t)degree; p[1] = (uint8_t)seventh; p[2] = (uint8_t)x->minor;
    lsyx_send(LCMD_HINT, p, 3);
    x->redraw = 1;
}
static void sus_chord(struct lctx *x, unsigned sus4, unsigned slot, unsigned velocity)
{
    unsigned degree = s1_roman_n ? s1_roman[0] >> 1 : 1u;
    uint8_t notes[4];
    unsigned n = theory_degree_chord(x->root, x->minor, degree, 0, chord_octave(x), notes);
    if (n < 3) return;
    notes[1] = (uint8_t)(notes[0] + (sus4 ? 5u : 2u));
    lmidi_chord_on(LT_CHORDS, notes, 3, velocity, slot);
    x->redraw = 1;
}

static void drill_send_state(void)
{
    unsigned tries = s1d.hits + s1d.misses;
    uint8_t p[4];
    p[0] = (uint8_t)s1d.state; p[1] = (uint8_t)s1d.kind; p[2] = (uint8_t)s1d.index;
    p[3] = (uint8_t)(tries ? s1d.hits * 100u / tries : 0u);
    lsyx_send(LCMD_DRILL, p, 4);
}

static void drill_pick_target(struct lctx *x)
{
    uint8_t notes[4];
    const struct lesson_step *ls = lesson_step();
    if (s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) {
        unsigned base = (unsigned)(53 + 12 * x->octave), note;   /* the 27 shared keys, from F3 */
        if (s1d.kind == DR_SCALE) {               /* up the scale from the root, one note a turn */
            unsigned start = s1d.target_count ? s1d.target[0] + 1u : base + ((x->root + 12u - base % 12u) % 12u);
            note = start;
            while (!theory_in_scale(x->root, x->scale, note)) note++;
            if (note >= base + 27u) note = base + ((x->root + 12u - base % 12u) % 12u);
        } else {
            unsigned pool[32], pool_n = 0, k;
            for (k = 0; k < 24u; k++) if (theory_in_scale(x->root, x->scale, base + k)) pool[pool_n++] = base + k;
            if (ls && ls->data[0]) {              /* the lesson restricts the degrees */
                unsigned filtered[32], fn = 0, i, j;
                for (i = 0; i < pool_n; i++) {
                    int d = theory_degree(x->root, x->scale, pool[i]);
                    for (j = 0; j < 8u && ls->data[j]; j++) if ((int)ls->data[j] == d) { filtered[fn++] = pool[i]; break; }
                }
                if (fn) { memcpy(pool, filtered, fn * sizeof pool[0]); pool_n = fn; }
            }
            note = pool_n ? pool[s1_rand() % pool_n] : base;
        }
        s1d.target[0] = (uint8_t)note; s1d.target_count = 1;
        s1d.ref_count = 1; s1d.ref[0].note = (uint8_t)note; s1d.ref[0].vel = 100; s1d.ref[0].at = 0;
        s1d.target_degree = 0;
    } else {
        unsigned degree, seventh = ls ? ls->seventh : 0u, i;
        if (s1d.kind == DR_PROGRESSION) degree = s1d.prog[s1d.prog_pos % (s1d.prog_count ? s1d.prog_count : 1u)];
        else if (ls && ls->data[0]) { unsigned n = 0; while (n < 8u && ls->data[n]) n++; degree = ls->data[s1_rand() % n]; }
        else degree = 1u + s1_rand() % 7u;
        s1d.target_count = (uint8_t)theory_degree_chord(x->root, x->minor, degree, seventh, chord_octave(x), notes);
        memcpy(s1d.target, notes, 4);
        s1d.target_degree = degree; s1d.target_seventh = seventh;
        s1d.ref_count = s1d.target_count;
        for (i = 0; i < s1d.target_count; i++) { s1d.ref[i].note = notes[i]; s1d.ref[i].vel = 100; s1d.ref[i].at = 0; }
    }
    s1d.answer_count = 0; s1d.mine_count = 0;
}

static void drill_play_reference(struct lctx *x)
{
    uint8_t p[2];
    s1d.state = DS_PLAYING_REF; s1d.started = x->now;
    if (s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) lmidi_note(LT_KEYS, s1d.target[0], 100);
    else lmidi_chord_on(LT_CHORDS, s1d.target, s1d.target_count, 100, 18);
    s1d.due = x->now + slow_ms(600, x);
    if (s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) {   /* follow-me: light what to press */
        int k = key_index(x, s1d.target[0]);
        s1_follow_key = k >= 0 ? (unsigned)k + 1u : 0u; s1_follow_pad = 0;
        p[0] = 1; p[1] = s1d.target[0]; lsyx_send(LCMD_PATH, p, 2);
    } else {
        s1_follow_pad = s1d.target_degree + (s1d.target_seventh ? 8u : 0u); s1_follow_key = 0;
        p[0] = 0; p[1] = (uint8_t)(s1_follow_pad - 1u); lsyx_send(LCMD_PATH, p, 2);
    }
    drill_send_state(); x->redraw = 1;
}

static void drill_start(struct lctx *x, unsigned kind, unsigned total)
{
    memset(&s1d, 0, sizeof s1d);
    s1d.kind = kind; s1d.total = total ? total : 10u; s1d.tempo = 90;
    if (kind == DR_PROGRESSION) {
        const struct lesson_step *ls = lesson_step();
        unsigned i;
        s1d.prog_count = 0;
        if (ls) for (i = 0; i < 8u && ls->data[i]; i++) s1d.prog[s1d.prog_count++] = ls->data[i];
        if (!s1d.prog_count) { s1d.prog[0] = 1; s1d.prog[1] = 5; s1d.prog[2] = 6; s1d.prog[3] = 4; s1d.prog_count = 4; }
        s1d.total = s1d.prog_count * (total ? total : 2u);
    }
    drill_pick_target(x);
    drill_play_reference(x);
    learn_notice(kind == DR_ECHO ? "ECHO: COPY THE NOTE" : kind == DR_SCALE ? "SCALE: PLAY IT UP" :
                 kind == DR_CHORD ? "FIND THE CHORD" : "PLAY THE PROGRESSION", 1200);
}

static void drill_stop(struct lctx *x)
{
    uint8_t p[2];
    if (s1d.state == DS_PLAYING_REF) {
        lmidi_chord_off(18);
        if (s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) lmidi_note(LT_KEYS, s1d.target[0], 0);
    }
    s1d.state = DS_IDLE; s1_follow_key = s1_follow_pad = 0;
    p[0] = 2; p[1] = 0; lsyx_send(LCMD_PATH, p, 2);
    drill_send_state(); x->redraw = 1;
}

static void drill_score_answer(struct lctx *x)
{
    unsigned hit = 0;
    uint8_t p[2];
    if (s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) {
        hit = s1d.answer_count && s1d.answer[0] % 12u == s1d.target[0] % 12u;
        if (hit && s1d.answer[0] != s1d.target[0]) learn_notice("RIGHT NOTE, OTHER OCTAVE", 900);
    } else {
        unsigned r1, r2;
        unsigned k1 = theory_identify(s1d.answer, s1d.answer_count, &r1);
        unsigned k2 = theory_identify(s1d.target, s1d.target_count, &r2);
        hit = k1 != CHORD_NONE && k1 == k2 && r1 == r2;
        if (!hit && s1d.answer_count && s1d.answer[0] % 12u == s1d.target[0] % 12u) learn_notice("RIGHT ROOT, WRONG CHORD", 900);
    }
    if (hit) s1d.hits++; else s1d.misses++;
    s1d.index++;
    if (s1d.kind == DR_PROGRESSION) s1d.prog_pos++;
    p[0] = (uint8_t)hit; p[1] = (uint8_t)(s1d.hits * 100u / (s1d.hits + s1d.misses)); lsyx_send(LCMD_SCORE, p, 2);
    learn_notice(hit ? "HIT" : "MISS", 500);
    s1d.state = DS_RESULT; s1d.due = x->now + slow_ms(500, x);
    x->redraw = 1;
}

static void drill_poll(struct lctx *x)
{
    if (s1d.state == DS_PLAYING_REF && (int32_t)(x->now - s1d.due) >= 0) {
        lmidi_chord_off(18);
        if (s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) lmidi_note(LT_KEYS, s1d.target[0], 0);
        s1d.state = DS_WAIT_ANSWER; s1d.due = x->now + slow_ms(6000, x);
        drill_send_state(); x->redraw = 1;
    } else if (s1d.state == DS_WAIT_ANSWER) {
        /* a chord answer is complete 350 ms after its first note; a single note at once */
        if (s1d.answer_count && ((s1d.kind == DR_ECHO || s1d.kind == DR_SCALE) || x->now - s1d.answer_first >= slow_ms(350, x)))
            drill_score_answer(x);
        else if ((int32_t)(x->now - s1d.due) >= 0) {
            s1d.answer_count = 0; s1d.misses++; s1d.index++;
            learn_notice("TOO SLOW - LISTEN AGAIN", 900); s1d.state = DS_RESULT; s1d.due = x->now + 400u;
        }
    } else if (s1d.state == DS_RESULT && (int32_t)(x->now - s1d.due) >= 0) {
        if (s1d.index >= s1d.total) {
            unsigned pct = s1d.hits * 100u / (s1d.hits + s1d.misses);
            unsigned logk = s1d.kind == DR_ECHO || s1d.kind == DR_SCALE ? LOGK_ECHO : s1d.kind == DR_CHORD ? LOGK_CHORD : LOGK_PROGRESSION;
            const struct lesson_step *ls;
            uint8_t p[3];
            log_add(logk, s1d.hits, s1d.misses, s1d.tempo, x->slow_percent < 100u, lesson_running(), pct);
            if (lesson_running()) lesson_step_done(pct >= 80u, pct);
            s1d.state = DS_IDLE; s1_follow_key = s1_follow_pad = 0;
            p[0] = 2; p[1] = 0; lsyx_send(LCMD_PATH, p, 2);
            learn_notice(pct >= 80u ? "PASSED!" : "KEEP GOING", 1500);
            p[0] = (uint8_t)log_streak(); p[1] = (uint8_t)(log_unlocked(UNLOCK_STAGE2) ? 2 : 1); p[2] = (uint8_t)(log_attempts() & 127u);
            lsyx_send(LCMD_LOG, p, 3);
            drill_send_state(); x->redraw = 1;
            ls = lesson_step();                   /* a lesson: its next drill starts by itself */
            if (ls && ls->type != DR_TEXT) { x->root = ls->root; x->scale = ls->scale; x->minor = ls->minor; drill_start(x, ls->type, ls->count); }
        } else { drill_pick_target(x); drill_play_reference(x); }
    }
    if (s1_replay_on) {                           /* the reference or the attempt, replayed */
        const struct s1_event *ev = s1_replay_which ? s1d.mine : s1d.ref;
        unsigned n = s1_replay_which ? s1d.mine_count : s1d.ref_count, i;
        while (s1_replay_pos < n && x->now - s1_replay_started >= slow_ms(ev[s1_replay_pos].at * 4u, x)) {
            lmidi_note(s1_replay_which ? LT_KEYS : LT_CHORDS, ev[s1_replay_pos].note, ev[s1_replay_pos].vel);
            s1_replay_pos++;
        }
        if (s1_replay_pos >= n && x->now - s1_replay_started >= slow_ms(700, x) + (n ? slow_ms(ev[n - 1].at * 4u, x) : 0u)) {
            for (i = 0; i < n; i++) lmidi_note(s1_replay_which ? LT_KEYS : LT_CHORDS, ev[i].note, 0);
            s1_replay_on = 0; x->redraw = 1;
        }
    }
}

static void drill_answer_note(struct lctx *x, unsigned note, unsigned velocity)
{
    if (s1d.state != DS_WAIT_ANSWER || !velocity) return;
    if (!s1d.answer_count) s1d.answer_first = x->now;
    if (s1d.answer_count < 8) s1d.answer[s1d.answer_count++] = (uint8_t)note;
    if (s1d.mine_count < EV_MAX) {
        s1d.mine[s1d.mine_count].note = (uint8_t)note; s1d.mine[s1d.mine_count].vel = (uint8_t)velocity;
        s1d.mine[s1d.mine_count].at = (uint16_t)((x->now - s1d.answer_first) / 4u); s1d.mine_count++;
    }
}

static void s1_sends(void)
{
    trk[LT_KEYS].p[P_REV] = s1_reverb; trk[LT_KEYS].p[P_DLY] = s1_delay;
    trk[LT_CHORDS].p[P_REV] = s1_reverb; trk[LT_CHORDS].p[P_DLY] = s1_delay;
}

/* ---- public ---- */
static void stage1_init(void)
{
    s1_held_n = 0; s1_chord_mode = 0; memset(&s1d, 0, sizeof s1d); s1_roman_n = 0;
    memset(s1_pad_down, 0, sizeof s1_pad_down); s1_follow_key = s1_follow_pad = 0; s1_replay_on = 0; s1_hint[0] = 0;
    learn_bank_select(LT_KEYS, s1_bank, s1_patch);
    learn_bank_select(LT_CHORDS, s1_bank, s1_patch);
    s1_sends();
}

static void stage1_key(struct lctx *x, unsigned note, unsigned velocity)
{
    unsigned play;
    if (velocity && (x->buttons & LB(LB_GLO))) {  /* GLO + key: the root */
        uint8_t p[2];
        x->root = note % 12u; x->redraw = 1;
        p[0] = (uint8_t)x->root; p[1] = (uint8_t)x->scale; lsyx_send(LCMD_KEY, p, 2);
        return;
    }
    if (velocity && s1_chord_mode) {
        int d = theory_degree(x->root, x->scale, note);
        if (d) {
            uint8_t n[4]; unsigned i, c;
            play_degree(x, (unsigned)d, 0, 17, velocity);
            c = theory_degree_chord(x->root, x->minor, (unsigned)d, 0, chord_octave(x), n);
            for (i = 0; i < c; i++) drill_answer_note(x, n[i], velocity);
            return;
        }
    }
    if (!velocity) {
        unsigned nudged = theory_nudge(x->root, x->scale, note);
        if (s1_chord_mode) lmidi_chord_off(17);
        held_remove(note); held_remove(nudged);
        lmidi_note(LT_KEYS, note, 0);
        if (nudged != note) lmidi_note(LT_KEYS, nudged, 0);   /* (the nudged note may be the one sounding) */
        x->redraw = 1; return;
    }
    play = note;
    if (!theory_in_scale(x->root, x->scale, note)) {
        if (s1_hints) play = theory_nudge(x->root, x->scale, note);
        else { learn_notice("NOT IN THE KEY", 400); x->redraw = 1; return; }
    }
    held_add(play);
    lmidi_note(LT_KEYS, play, velocity);
    drill_answer_note(x, play, velocity);
    x->redraw = 1;
}

static void stage1_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure)
{
    (void)pressure;
    if (pad >= 16) return;
    if (velocity) {
        unsigned slot = pad + 1u;
        s1_pad_down[pad] = 1;
        if (pad == 7) sus_chord(x, 1, slot, velocity);
        else if (pad == 15) sus_chord(x, 0, slot, velocity);
        else {
            unsigned degree = (pad & 7u) + 1u, seventh = pad >= 8u, i, c;
            uint8_t n[4];
            play_degree(x, degree, seventh, slot, velocity);
            c = theory_degree_chord(x->root, x->minor, degree, seventh, chord_octave(x), n);
            for (i = 0; i < c; i++) drill_answer_note(x, n[i], velocity);
        }
    } else {
        s1_pad_down[pad] = 0; lmidi_chord_off(pad + 1u);
    }
    x->redraw = 1;
}

static void stage1_encoder(struct lctx *x, unsigned id, int delta)
{
    switch (id) {
    case 0: {
        unsigned n = learn_bank_count();
        s1_bank = (uint8_t)((s1_bank + n + (delta > 0 ? 1u : n - 1u)) % n); s1_patch = 0;
        learn_bank_select(LT_KEYS, s1_bank, s1_patch); learn_bank_select(LT_CHORDS, s1_bank, s1_patch); s1_sends();
        learn_notice(learn_bank_name(s1_bank), 900);
    } break;
    case 1: {
        unsigned n = learn_patch_count(s1_bank); if (!n) n = 1;
        s1_patch = (uint8_t)((s1_patch + n + (delta > 0 ? 1u : n - 1u)) % n);
        learn_bank_select(LT_KEYS, s1_bank, s1_patch); learn_bank_select(LT_CHORDS, s1_bank, s1_patch); s1_sends();
        { char b[24]; unsigned k = ls_cat(b, 0, sizeof b, "PATCH "); ls_num(b, k, sizeof b, s1_patch + 1u); learn_notice(b, 900); }
    } break;
    case 2: { int v = (int)s1_reverb + delta * 8; s1_reverb = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); s1_sends(); } break;
    case 3: { int v = (int)s1_delay + delta * 8; s1_delay = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); s1_sends(); } break;
    case 4: {
        int v = (int)x->slow_percent + delta * 5; uint8_t p;
        x->slow_percent = (unsigned)(v < 50 ? 50 : v > 100 ? 100 : v); p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1);
    } break;
    case 5: {
        int v = (int)s1_lesson_pick + delta;
        s1_lesson_pick = (unsigned)(v < 0 ? 0 : v >= (int)lesson_count ? (int)lesson_count - 1 : v);
        learn_notice(lessons[s1_lesson_pick].title, 1200);
    } break;
    case 6: {
        static const uint8_t kinds[4] = {DR_ECHO, DR_CHORD, DR_PROGRESSION, DR_SCALE};
        unsigned i = 0; while (i < 4u && kinds[i] != s1_drill_kind) i++;
        i = (i + 4u + (delta > 0 ? 1u : 3u)) % 4u; s1_drill_kind = kinds[i];
        learn_notice(s1_drill_kind == DR_ECHO ? "DRILL: ECHO" : s1_drill_kind == DR_CHORD ? "DRILL: CHORD MATCH" :
                     s1_drill_kind == DR_PROGRESSION ? "DRILL: PROGRESSION" : "DRILL: SCALE", 1000);
    } break;
    default: break;
    }
    x->redraw = 1;
}

static void stage1_fader(struct lctx *x, unsigned id, unsigned value)
{
    if (id == 0) { s1_reverb = (uint8_t)value; s1_sends(); }
    else if (id == 1) { s1_delay = (uint8_t)value; s1_sends(); }
    else if (id == 3) { uint8_t p; x->slow_percent = 50u + value * 50u / 127u; p = (uint8_t)x->slow_percent; lsyx_send(LCMD_SLOW, &p, 1); x->redraw = 1; }
}

static void stage1_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    switch (cmd) {
    case LCMD_PATH:
        if (n >= 2) {
            if (p[0] == 1) { int k = key_index(x, p[1]); s1_follow_key = k >= 0 ? (unsigned)k + 1u : 0u; s1_follow_pad = 0; }
            else if (p[0] == 0) { s1_follow_pad = p[1] + 1u; s1_follow_key = 0; }
            else s1_follow_key = s1_follow_pad = 0;
            x->redraw = 1;
        }
        break;
    case LCMD_HINT:
        if (n >= 3 && s1_hints) { roman_push(p[0], p[1]); s1_minor = p[2]; str_cpy(s1_hint, theory_why_hint(p[2], p[0]), sizeof s1_hint); s1_hint_until = x->now + 2500u; x->redraw = 1; }
        break;
    case LCMD_DRILL:                              /* the partner's drill: this screen shows its prompt */
        if (n >= 4 && (s1d.state == DS_IDLE || !s1d.total)) {
            s1d.kind = p[1];
            s1d.state = p[0] == DS_IDLE ? DS_IDLE : p[0] == DS_WAIT_ANSWER ? DS_WAIT_ANSWER : DS_PLAYING_REF; s1d.index = p[2];
            if (p[0] == DS_IDLE) s1_follow_key = s1_follow_pad = 0;
            if (p[0] != DS_IDLE) s1d.due = x->now + 8000u;   /* (it shows for a while even if the partner falls silent) */
            s1d.total = 0;                        /* (a mirrored drill scores nothing here) */
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

static void stage1_control(struct lctx *x)
{
    unsigned b = x->btn_down;
    const struct lesson_step *ls;
    if (b & LB(LB_SCL)) {
        if (!LEARN_PADS && (x->buttons & LB(LB_GLO))) { s1_chord_mode = !s1_chord_mode; learn_notice(s1_chord_mode ? "CHORD KEYS ON" : "CHORD KEYS OFF", 900); }
        else {
            uint8_t p[2];
            x->scale = (x->scale + 1u) % SCALE_COUNT;
            x->minor = x->scale == SCALE_MINOR || x->scale == SCALE_PENT_MIN || x->scale == SCALE_BLUES;
            p[0] = (uint8_t)x->root; p[1] = (uint8_t)x->scale; lsyx_send(LCMD_KEY, p, 2);
        }
        x->redraw = 1;
    }
    if (b & LB(LB_ENV)) { s1_hints = !s1_hints; learn_notice(s1_hints ? "HINTS + NUDGE ON" : "HINTS OFF, MUTE OFF-KEY", 900); x->redraw = 1; }
    if (b & LB(LB_FX)) { s1_staff = !s1_staff; x->redraw = 1; }
    if (b & LB(LB_EDIT)) stage1_encoder(x, 0, 1);
    if (b & LB(LB_SAVE)) stage1_encoder(x, 1, 1);
    if (b & LB(LB_PLAY)) { if (s1d.state != DS_IDLE && s1d.total) drill_stop(x); else drill_start(x, s1_drill_kind, 10); }
    if (b & LB(LB_REC)) {
        if (s1d.ref_count || s1d.mine_count) { s1_replay_which = !s1_replay_which; s1_replay_on = 1; s1_replay_pos = 0; s1_replay_started = x->now; learn_notice(s1_replay_which ? "REPLAY: YOU" : "REPLAY: REFERENCE", 800); }
        else learn_notice("NOTHING TO REPLAY YET", 800);
    }
    if (b & LB(LB_ARP)) {
        if (lesson_running()) { lesson_stop(); drill_stop(x); learn_notice("LESSON STOPPED", 800); }
        else { lesson_start(s1_lesson_pick); ls = lesson_step(); if (ls && ls->type != DR_TEXT) drill_start(x, ls->type, ls->count); }
    }
    if (b & LB(LB_HOME)) { drill_stop(x); s1_replay_on = 0; lmidi_all_off(); s1_held_n = 0; x->redraw = 1; }
    /* a TEXT lesson step: any key or pad continues */
    ls = lesson_step();
    if (ls && ls->type == DR_TEXT && s1d.state == DS_IDLE && x->keys_down) {
        lesson_step_done(1, 100); ls = lesson_step();
        if (ls && ls->type != DR_TEXT) { x->root = ls->root; x->scale = ls->scale; x->minor = ls->minor; drill_start(x, ls->type, ls->count); }
    }
    /* a mirrored drill (the partner's) times out on its own */
    if (s1d.state != DS_IDLE && !s1d.total && (int32_t)(x->now - s1d.due) >= 0) { s1d.state = DS_IDLE; s1_follow_key = s1_follow_pad = 0; x->redraw = 1; }
    {   /* notes from the other box count as answers */
        uint8_t st, d1, d2; unsigned changed = 0;
        while (lmidi_pull_external(&st, &d1, &d2)) { changed = 1; if (st == 0x90u && d2) drill_answer_note(x, d1, d2); }
        if (changed) x->redraw = 1;
    }
    if (s1d.total) drill_poll(x);
    else if (s1_replay_on) drill_poll(x);
    if (s1_hint[0] && (int32_t)(x->now - s1_hint_until) >= 0) { s1_hint[0] = 0; x->redraw = 1; }
    {   /* the key lights */
        uint32_t lit = 0, bright = 0, ext = lmidi_held_external();
        unsigned i, k;
        for (i = 0; i < s1_held_n; i++) { int ki = key_index(x, s1_held[i]); if (ki >= 0) lit |= 1u << ki; }
        if (ext >> 12) for (k = 0; k < 27u; k++) if ((ext >> ((53u + 12u * (unsigned)(x->octave + 2) + k) % 12u)) & 1u) lit |= 1u << k;
        if (s1_follow_key) bright |= 1u << (s1_follow_key - 1u);
        llit_keys = lit; lbright_keys = bright;
    }
}

static uint32_t stage1_pad_colour(unsigned pad)
{
    static const uint32_t colours[7] = {P_GREEN, P_ORANGE, P_CYAN, P_BLUE, P_RED, P_PURPLE, P_WHITE};
    unsigned degree; uint32_t c;
    if (pad >= 16) return P_OFF;
    if (pad == 7 || pad == 15) return s1_pad_down[pad] ? P_FULL(P_WHITE) : P_DIM(P_WHITE);
    degree = (pad & 7u) + 1u;
    c = colours[theory_degree_colour(s1_minor, degree)];
    if (pad >= 8) c = P_DIM(c);
    if (s1_follow_pad == pad + 1u) return P_FULL(c);
    if (s1_pad_down[pad]) return P_FULL(c);
    return c;
}

/* ---- the screen (drawn twice a frame, one half each; everything clips) ---- */
static void stage1_draw(struct lctx *x)
{
    char nn[4], line[40], big[16], sub[32];
    unsigned n;
    const struct lesson_step *ls = lesson_step();
    /* header: key, scale, octave, slow, partner dot */
    theory_note_name(x->root, nn);
    n = ls_cat(line, 0, sizeof line, nn);
    n = ls_cat(line, n, sizeof line, " ");
    n = ls_cat(line, n, sizeof line, theory_scale_name(x->scale));
    n = ls_cat(line, n, sizeof line, "  OCT ");
    if (x->octave < 0) n = ls_cat(line, n, sizeof line, "-");
    n = ls_num(line, n, sizeof line, (unsigned)(x->octave < 0 ? -x->octave : x->octave));
    if (x->slow_percent < 100u) { n = ls_cat(line, n, sizeof line, "  SLOW "); n = ls_num(line, n, sizeof line, x->slow_percent); }
    ld_text(4, 2, line, 0, LC_DIM);
    ld_fill(228, 6, 8, 8, lsyx_partner_present() ? LC_GREEN : LC_PANEL);
    /* the big line: the chord if 3+ held, else the last note, else the last chord played from the pads */
    big[0] = 0; sub[0] = 0;
    if (s1_held_n >= 3) {
        unsigned r, k = theory_identify(s1_held, s1_held_n, &r);
        int d;
        theory_note_name(r, nn);
        n = ls_cat(big, 0, sizeof big, nn); ls_cat(big, n, sizeof big, theory_chord_suffix(k));
        d = theory_degree(x->root, x->scale, r);
        if (d && k != CHORD_NONE) ls_cat(sub, 0, sizeof sub, theory_roman(x->minor, (unsigned)d, k >= CHORD_MAJ7));
    } else if (s1_held_n) {
        static const char *const ord[8] = {"", "ROOT", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH"};
        unsigned note = s1_held[s1_held_n - 1u];
        int d = theory_degree(x->root, x->scale, note);
        theory_note_name(note, nn);
        n = ls_cat(big, 0, sizeof big, nn); ls_num(big, n, sizeof big, note / 12u > 0u ? note / 12u - 1u : 0u);
        n = ls_cat(sub, 0, sizeof sub, d ? ord[d] : "OUTSIDE");
        if (s1_held_n >= 2) { n = ls_cat(sub, n, sizeof sub, "  "); ls_cat(sub, n, sizeof sub, theory_interval_name((int)note - (int)s1_held[s1_held_n - 2u])); }
    } else if (s1_roman_n) {
        ls_cat(big, 0, sizeof big, theory_roman(s1_minor, s1_roman[0] >> 1, s1_roman[0] & 1u));
        ls_cat(sub, 0, sizeof sub, "LAST CHORD");
    } else { ls_cat(big, 0, sizeof big, "PLAY"); ls_cat(sub, 0, sizeof sub, LEARN_PADS ? "KEYS OR PADS" : "ANY KEY"); }
    {   /* FONT_L has capitals only: lower-case roman numerals are drawn small, in a box */
        unsigned i, lower = 0;
        for (i = 0; big[i]; i++) if (big[i] >= 'a' && big[i] <= 'z') lower = 1;
        if (lower) { ld_text(4, 28, big, 0, LC_TEXT); }
        else ld_text(4, 20, big, 1, LC_TEXT);
    }
    ld_text(4, 54, sub, 0, LC_CYAN);
    /* the keymap strip */
    ld_keymap(74, 24, 27, (unsigned)(53 + 12 * x->octave), llit_keys, lbright_keys);
    /* the progression line */
    {
        unsigned i;
        n = 0; line[0] = 0;
        for (i = s1_roman_n; i > 0; i--) {
            n = ls_cat(line, n, sizeof line, theory_roman(s1_minor, s1_roman[i - 1] >> 1, s1_roman[i - 1] & 1u));
            if (i > 1) n = ls_cat(line, n, sizeof line, " - ");
        }
        ld_text(4, 102, line, 0, LC_ORANGE);
    }
#if LEARN_PADS
    {   /* the pad palette: two rows of eight, coloured by function */
        static const uint16_t cols[7] = {LC_GREEN, LC_ORANGE, LC_CYAN, LC_BLUE, LC_RED, LC_PURPLE, LC_TEXT};
        unsigned p;
        for (p = 0; p < 16; p++) {
            int cx = 4 + (int)(p & 7u) * 29, cy = 122 + (p >= 8 ? 18 : 0);
            uint16_t c = (p == 7 || p == 15) ? LC_DIM : cols[theory_degree_colour(s1_minor, (p & 7u) + 1u)];
            unsigned hot = s1_pad_down[p] || s1_follow_pad == p + 1u;
            ld_fill(cx, cy, 27, 16, hot ? c : LC_PANEL);
            ld_text(cx + 2, cy, p == 7 ? "s4" : p == 15 ? "s2" : theory_roman(s1_minor, (p & 7u) + 1u, 0), 0, hot ? LC_BG : c);
        }
    }
#else
    if (s1_staff) {
        unsigned note = s1_held_n ? s1_held[s1_held_n - 1u] : (s1d.state == DS_WAIT_ANSWER && s1d.target_count ? s1d.target[0] : 999u);
        if (note < 128u) ld_staff(4, 116, note);
    }
    ld_text(84, 122, s1_chord_mode ? "CHORD KEYS" : "SINGLE KEYS", 0, LC_DIM);
    ld_text(84, 140, "GLO+SCL TOGGLES", 0, LC_PANEL);
#endif
    /* the drill / lesson prompt */
    line[0] = 0;
    if (s1d.state != DS_IDLE) {
        n = ls_cat(line, 0, sizeof line, s1d.kind == DR_ECHO ? "ECHO " : s1d.kind == DR_SCALE ? "SCALE " : s1d.kind == DR_CHORD ? "CHORD " : "PROG ");
        n = ls_num(line, n, sizeof line, s1d.index + 1u);
        if (s1d.total) { n = ls_cat(line, n, sizeof line, "/"); n = ls_num(line, n, sizeof line, s1d.total); }
        n = ls_cat(line, n, sizeof line, s1d.state == DS_PLAYING_REF ? "  LISTEN" : s1d.state == DS_WAIT_ANSWER ? "  YOUR TURN" : "");
    } else if (ls) {
        ls_cat(line, 0, sizeof line, ls->prompt ? ls->prompt : "");
    } else {
        n = ls_cat(line, 0, sizeof line, "PLAY: DRILL  ARP: LESSON ");
        ls_num(line, n, sizeof line, s1_lesson_pick + 1u);
    }
    ld_text(4, 160, line, 0, s1d.state == DS_WAIT_ANSWER ? LC_GREEN : LC_AMBER);
    /* the lesson title and progress */
    if (lesson_running()) {
        const struct lesson *l = &lessons[lesson_running() - 1u];
        n = ls_cat(line, 0, sizeof line, l->title);
        n = ls_cat(line, n, sizeof line, "  ");
        n = ls_num(line, n, sizeof line, lesson_progress() + 1u);
        n = ls_cat(line, n, sizeof line, "/");
        ls_num(line, n, sizeof line, l->step_count);
        ld_text(4, 178, line, 0, LC_PURPLE);
    }
    /* replay dots: the reference above, the attempt below, misses red */
    if (s1d.ref_count || s1d.mine_count) {
        unsigned i;
        for (i = 0; i < s1d.ref_count && i < 16u; i++)
            ld_fill(4 + (int)i * 14, 198, 10, 6, s1_replay_on && !s1_replay_which && s1_replay_pos > i ? LC_GREEN : LC_DIM);
        for (i = 0; i < s1d.mine_count && i < 16u; i++) {
            unsigned ok = i < s1d.ref_count && s1d.mine[i].note % 12u == s1d.ref[i].note % 12u;
            ld_fill(4 + (int)i * 14, 207, 10, 6, ok ? LC_GREEN : LC_RED);
        }
    }
    /* the bottom line: a notice, else the why-hint */
    {
        const char *nt = learn_notice_text();
        if (nt) ld_text(4, 222, nt, 0, LC_TEXT);
        else if (s1_hint[0]) ld_text(4, 222, s1_hint, 0, LC_YELLOW);
    }
}
