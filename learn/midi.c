/* SPDX-License-Identifier: GPL-3.0-only
 * The learn layer's sound and MIDI plumbing: two rings between the audio ISR and the main loop, and the
 * ISR-side routines SLOOP's seq.c calls (learn_follow, learn_keys_block, learn_ext_event, learn_midi_in).
 *
 *   ISR -> main  lev_q   key / pad / MIDI-in events (what the player did)
 *   main -> ISR  lsq     sound requests: track, note, velocity (what the layer wants to hear)
 *
 * Single producer, single consumer each; the indices are bytes, so a torn read cannot happen on this core.
 * A full ring drops the newest event rather than blocking: nothing here ever waits. */

enum { LEV_KEY = 1, LEV_PAD = 2, LEV_MIDI = 3 };
#define LEV_N 64u
static volatile uint8_t lev_q[LEV_N][3];
static volatile uint8_t lev_w, lev_r;
#define LSQ_N 64u
static volatile uint8_t lsq[LSQ_N][3];
static volatile uint8_t lsq_w, lsq_r;

/* (learn_on and learn_isr: pre.h) */
static uint32_t lk_prev;                      /* the 27 note keys as last seen by the ISR */
static uint32_t lsn_on[NTRK][4];              /* ISR: the notes the layer has on, per track (bit per note) */

static void lev_push(uint32_t kind, uint32_t a, uint32_t b)   /* ISR side */
{
    uint8_t w = lev_w, n = (uint8_t)((w + 1u) % LEV_N);
    if (n == lev_r)
        return;                                   /* full: dropped */
    lev_q[w][0] = (uint8_t)kind;
    lev_q[w][1] = (uint8_t)a;
    lev_q[w][2] = (uint8_t)b;
    lev_w = n;
}
static int lev_pull(uint8_t *kind, uint8_t *a, uint8_t *b)   /* main side */
{
    uint8_t r = lev_r;
    if (r == lev_w)
        return 0;
    *kind = lev_q[r][0];
    *a = lev_q[r][1];
    *b = lev_q[r][2];
    lev_r = (uint8_t)((r + 1u) % LEV_N);
    return 1;
}
static void lsq_push(uint32_t track, uint32_t note, uint32_t vel)   /* main side */
{
    uint8_t w = lsq_w, n = (uint8_t)((w + 1u) % LSQ_N);
    if (n == lsq_r)
        return;
    lsq[w][0] = (uint8_t)track;
    lsq[w][1] = (uint8_t)(note & 127u);
    lsq[w][2] = (uint8_t)(vel & 127u);
    lsq_w = n;
}

/* ---- ISR side (seq.c) ---- */
static void lsn_off_all(void)                     /* every note the layer has on ends (ISR) */
{
    uint32_t t, w, b;
    for (t = 0; t < NPART; t++)
        for (w = 0; w < 4u; w++) {
            uint32_t m = lsn_on[t][w];
            for (b = 0; m && b < 32u; b++)
                if ((m >> b) & 1u) {
                    input_off(&trk[t], w * 32u + b);
                    midi_out_note(trk_midi_ch(t), w * 32u + b, 0);
                }
            lsn_on[t][w] = 0;
        }
}

static void learn_follow(void)
{
    uint32_t k;
    if (learn_isr == learn_on)
        return;
    if (learn_on) {                               /* into LEARN: the synth's keys let go, held keys stay silent */
        for (k = 0; k < 27u; k++)
            if ((kb_prev >> k) & 1u)
                key_up(k);
        kb_prev = 0;
#if BOARD_SMK37
        lk_prev = fm1_in.knotes;
#else
        lk_prev = fm1_in.notes;
#endif
        lev_w = lev_r = 0;
        lsq_r = lsq_w;
    } else {                                      /* back to SLOOP: the layer's notes end, held keys stay silent */
        lsn_off_all();
        kb_prev = fm1_in.notes;
    }
    learn_isr = learn_on;
}

static void lsq_drain(void)                       /* the sound requests due (ISR) */
{
    while (lsq_r != lsq_w) {
        uint8_t r = lsq_r, t = lsq[r][0], n = lsq[r][1], v = lsq[r][2];
        lsq_r = (uint8_t)((r + 1u) % LSQ_N);
        if (t == LT_DRUMS) {                      /* a drum lane */
            uint32_t lane = n % DRUM_LANES;
            if (v) {
                drum_input(lane, vel_lvl(v), 0, 0);
                midi_out_note(trk_midi_ch(TRK_DRUM), LANE_NOTE[lane], v);
            } else {
                midi_out_note(trk_midi_ch(TRK_DRUM), LANE_NOTE[lane], 0);
            }
            continue;
        }
        t %= NPART;
        {
            uint32_t *w = &lsn_on[t][(n >> 5) & 3u], bit = 1u << (n & 31u);
            if (v) {
                if (*w & bit)
                    input_off(&trk[t], n);        /* (retriggered: the old one ends first) */
                *w |= bit;
                input_on(&trk[t], n, v);
                midi_out_note(trk_midi_ch(t), n, v);
            } else if (*w & bit) {
                *w &= ~bit;
                input_off(&trk[t], n);
                midi_out_note(trk_midi_ch(t), n, 0);
            }
        }
    }
}

static void learn_keys_block(void)                /* the 27 note keys shared by both boards, and the sound ring */
{
#if BOARD_SMK37
    uint32_t cur = fm1_in.knotes;
#else
    uint32_t cur = fm1_in.notes;
#endif
    uint32_t ch = cur ^ lk_prev, k;
    for (k = 0; ch && k < 27u; k++)
        if ((ch >> k) & 1u)
            lev_push(LEV_KEY, 53u + k, (cur >> k) & 1u ? key_vel(k) : 0u);   /* key 0 = F3 = 53 on both boards */
    lk_prev = cur;
    lsq_drain();
}

#if BOARD_SMK37
static void learn_ext_event(uint32_t k, uint32_t v)   /* the pads (SMK_EXT_DAW + pad) and the ten outer keys */
{
    if (k >= SMK_EXT_DAW) {
        if (k - SMK_EXT_DAW < SMK_NPAD)
            lev_push(LEV_PAD, k - SMK_EXT_DAW, v);
    } else if (k < FM1_NKEY) {
        lev_push(LEV_KEY, LEARN_FIRST_NOTE + k, v);   /* keybed key k: C3 = 48 + k */
    }
}
#endif

static void learn_midi_in(uint32_t status, uint32_t d1, uint32_t d2)
{
    lev_push(LEV_MIDI, d1, (status & 0xF0u) == 0x90u ? d2 : 0u);
}

/* ---- main side ---- */
static void lmidi_note(unsigned track, unsigned note, unsigned velocity) { lsq_push(track, note, velocity); }
static void lmidi_drum(unsigned lane, unsigned velocity) { lsq_push(LT_DRUMS, lane, velocity); }

/* chords the layer holds, by slot, so a release always matches what was started */
#define LCH_SLOTS 20u
static uint8_t lch_notes[LCH_SLOTS][4], lch_count[LCH_SLOTS], lch_track[LCH_SLOTS];
static void lmidi_chord_off(unsigned slot)
{
    unsigned i;
    if (slot >= LCH_SLOTS)
        return;
    for (i = 0; i < lch_count[slot]; i++)
        lmidi_note(lch_track[slot], lch_notes[slot][i], 0);
    lch_count[slot] = 0;
}
static void lmidi_chord_on(unsigned track, const uint8_t *notes, unsigned count, unsigned velocity, unsigned slot)
{
    unsigned i;
    if (slot >= LCH_SLOTS)
        return;
    lmidi_chord_off(slot);
    if (count > 4u)
        count = 4u;
    lch_track[slot] = (uint8_t)track;
    lch_count[slot] = (uint8_t)count;
    for (i = 0; i < count; i++) {
        lch_notes[slot][i] = notes[i];
        lmidi_note(track, notes[i], velocity);
    }
}
static void lmidi_all_off(void)
{
    unsigned s, n;
    for (s = 0; s < LCH_SLOTS; s++)
        lmidi_chord_off(s);
    for (n = 0; n < 128u; n++)                    /* (what single notes may be on: the ring only ends those that are) */
        lmidi_note(LT_KEYS, n, 0);
}

/* notes that arrived from outside (the other box over USB / TRS) */
#define LEXT_N 32u
static uint8_t lext_q[LEXT_N][2], lext_w, lext_r;
static uint32_t lext_held;                        /* bits 0..11 pitch classes held, bits 12.. count */
static uint8_t lext_cnt[12];
static void lext_push(unsigned note, unsigned vel)
{
    uint8_t n = (uint8_t)((lext_w + 1u) % LEXT_N);
    unsigned pc = note % 12u, i, total = 0;
    if (vel) {
        if (lext_cnt[pc] < 255u) lext_cnt[pc]++;
    } else if (lext_cnt[pc]) {
        lext_cnt[pc]--;
    }
    lext_held = 0;
    for (i = 0; i < 12u; i++)
        if (lext_cnt[i]) { lext_held |= 1u << i; total++; }
    lext_held |= total << 12;
    if (n == lext_r)
        return;
    lext_q[lext_w][0] = (uint8_t)note;
    lext_q[lext_w][1] = (uint8_t)vel;
    lext_w = n;
}
static int lmidi_pull_external(uint8_t *status, uint8_t *data1, uint8_t *data2)
{
    if (lext_r == lext_w)
        return 0;
    *status = lext_q[lext_r][1] ? 0x90u : 0x80u;
    *data1 = lext_q[lext_r][0];
    *data2 = lext_q[lext_r][1];
    lext_r = (uint8_t)((lext_r + 1u) % LEXT_N);
    return 1;
}
static uint32_t lmidi_held_external(void) { return lext_held; }

/* Beginner sound banks: SLOOP's engines, their presets. A bank is an engine; a patch its preset. */
static unsigned learn_bank_count(void) { return NENGINES; }
static const char *learn_bank_name(unsigned bank) { return ENGINES[bank % NENGINES]->name; }
static unsigned learn_patch_count(unsigned bank) { return ENGINES[bank % NENGINES]->npresets; }
static void learn_bank_select(unsigned track, unsigned bank, unsigned patch)
{
    track_t *t;
    if (track >= NPART)
        return;
    t = &trk[track];
    if (t->eng_req != bank % NENGINES)
        set_engine_of(t, bank % NENGINES);
    apply_preset_to(t, patch);
    t->p[P_AMODE] = 0;                            /* no arpeggiator, no keyboard hold: the layer plays plain notes */
    t->p[P_AHOLD] = 0;
}
