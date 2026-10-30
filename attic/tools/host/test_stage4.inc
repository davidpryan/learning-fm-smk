/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 4 host tests: sections, queueing at a loop boundary, energy. */
#include "learn_song.h"
static void s4_tap(unsigned button) { sim_control(0, button, 1); sim_control(0, button, 0); sim_advance(20); }
#ifdef LEARN_BOARD_SMK37
static void s4_k1(int delta) { sim_smk37(4 /*SMK37_ENCODER*/, 4 /*physical K5 = logical K1*/, delta); sim_advance(20); }
static void s4_store(unsigned slot) { sim_smk37(0, 0, 1); sim_smk37(3, 8 + slot, 100); sim_advance(20); sim_smk37(3, 8 + slot, 0); sim_smk37(0, 0, 0); sim_advance(20); }
static void s4_queue(unsigned slot) { sim_smk37(3, slot, 100); sim_advance(20); sim_smk37(3, slot, 0); sim_advance(20); }
#else
static void s4_k1(int delta) { sim_control(2, 0, delta); sim_advance(20); }
/* FM-1: GLO + key 8..15 stores slot 0..7, GLO + key 0..7 queues it. */
static void s4_store(unsigned slot) { sim_control(0, LB_GLO, 1); sim_control(1, 8 + slot, 1); sim_advance(20); sim_control(1, 8 + slot, 0); sim_control(0, LB_GLO, 0); sim_advance(20); }
static void s4_queue(unsigned slot) { sim_control(0, LB_GLO, 1); sim_control(1, slot, 1); sim_advance(20); sim_control(1, slot, 0); sim_control(0, LB_GLO, 0); sim_advance(20); }
#endif

static void test_stage4(void)
{
    unsigned on;
    /* Page 1 -> 2 -> 3 -> 4. */
    while (learn_page() != 1) s4_tap(LB_SEQ);
    s4_tap(LB_SEQ); s4_tap(LB_SEQ); s4_tap(LB_SEQ);
    CHECK(learn_page() == 4, "page 4 is showing (got %u)", learn_page());
    sim_control(0, LB_HOME, 1); sim_control(0, LB_HOME, 0); sim_advance(50);
    drain_midi(&on);
    CHECK(lit_pixels() > 100, "stage 4 drew something (%u lit samples)", lit_pixels());

    /* Store the current loop into slot 1 (VERSE, the default type) and slot 2 (SCL once = PRE). */
    s4_store(0);
    s4_tap(LB_SCL);
    s4_store(1);
#ifdef LEARN_BOARD_SMK37
    unsigned c1 = sim_pad_rgb(0), c2 = sim_pad_rgb(1), c3 = sim_pad_rgb(2);
    CHECK(c1 != 0, "slot 1 pad is lit after a store (got %06x)", c1);
    CHECK(c2 != 0, "slot 2 pad is lit after a store (got %06x)", c2);
    CHECK(c1 != c2, "slots with different types have different colours (%06x vs %06x)", c1, c2);
    CHECK(c3 == 0, "slot 3 is still empty (got %06x)", c3);
#else
    /* No pads on the FM-1: the slot strip on screen is the only colour feedback; skip the colour check. */
    CHECK(lit_pixels() > 100, "FM-1 slot strip drew");
#endif

    /* Queue slot 2 while playing; after the loop wraps it is the active section. */
    s4_tap(LB_PLAY);
    sim_advance(100);
    CHECK(song_playing(), "PLAY starts the loop");
    s4_queue(1);
    /* One bar at 100 BPM is 2.4 s; the engine track may still be 64 steps long (4 bars = 9.6 s)
     * when no earlier stage has rendered the model, so allow a full 64-step wrap. The page
     * reports its active slot in the SECTION frame it sends on every bar; the capture ring
     * holds 64 messages, so drain it as time advances. */
    {
        uint8_t m[128]; unsigned port, n, active = 255;
        for (unsigned t = 0; t < 110; ++t) {
            sim_advance(100);
            while ((n = sim_midi_pop(m, &port)) != 0)
                if (n >= 12 && m[0] == 0xF0 && m[2] == 'L' && m[7] == LCMD_SECTION) active = m[8];
        }
        CHECK(active == 1, "after a loop boundary the active slot is 2 (SECTION frame slot %u)", active);
    }

    /* Energy 1 leaves only the drums unmuted. K1 is encoder 0; turn it down from 7. */
    for (unsigned i = 0; i < 8; ++i) s4_k1(-1);
    {
        struct groove_view v;
        groove_get_view(LT_DRUMS, 0, &v);  CHECK(!v.muted, "energy 1: drums unmuted");
        groove_get_view(LT_BASS, 0, &v);   CHECK(v.muted, "energy 1: bass muted");
        groove_get_view(LT_CHORDS, 0, &v); CHECK(v.muted, "energy 1: chords muted");
        groove_get_view(LT_MELODY, 0, &v); CHECK(v.muted, "energy 1: melody muted");
    }
    /* Energy 8 unmutes everything again. */
    for (unsigned i = 0; i < 8; ++i) s4_k1(1);
    {
        struct groove_view v;
        groove_get_view(LT_MELODY, 0, &v); CHECK(!v.muted, "energy 8: melody unmuted");
    }
    sim_control(0, LB_HOME, 1); sim_control(0, LB_HOME, 0); sim_advance(50);
    CHECK(!song_playing(), "HOME stops the loop");
    drain_midi(&on);
}
