/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 2 host tests: Pulse (SMK-37) / Root (FM-1). Included by harness.c. */
#include "learn_song.h"

static unsigned engine_drum_steps(unsigned drum, unsigned steps)
{
    /* Count the steps where the drum's bit is set in the 808 track. */
    unsigned n = 0; struct groove_view v;
    for (unsigned s = 0; s < steps; ++s) { groove_get_view(LT_DRUMS, s, &v); if (v.step.drums & (1U << drum)) ++n; }
    return n;
}

static void test_stage2(void)
{
    unsigned on, msgs;
    /* Page 2 is one SEQ press away from page 1. */
    sim_control(0, LB_SEQ, 1); sim_control(0, LB_SEQ, 0);
    sim_advance(60);
    CHECK(learn_page() == 2, "SEQ switched to page 2 (got %u)", learn_page());
    CHECK(lit_pixels() > 100, "page 2 drew the grid (%u lit samples)", lit_pixels());
    drain_midi(&on);

    /* The default kick lane is four on the floor and already in the engine. */
    struct groove_view v;
    uint16_t kick_before = song.lane[LANE_KICK].mask;
    CHECK(kick_before == 0x1111, "default kick lane is four on the floor (mask %04x)", kick_before);
    groove_get_view(LT_DRUMS, 0, &v);
    CHECK(v.step.drums & 1U, "kick note is in the engine at step 0 (drums %04x)", v.step.drums);
    CHECK(engine_drum_steps(0, 16) == 4, "engine has 4 kick steps (got %u)", engine_drum_steps(0, 16));
    CHECK(v.active_steps & 1ULL, "drum track step 0 is active");

    /* K1 (encoder 0) changes the kick lane: hits 4 -> 5 regenerates the mask. */
#ifdef LEARN_BOARD_SMK37
    sim_smk37(4 /*SMK37_ENCODER*/, 4 /*physical K1 -> logical 0*/, 1);
#else
    sim_control(2, 0, 1);
#endif
    sim_advance(30);
    CHECK(song.lane[LANE_KICK].hits == 5, "K1 raised kick hits to 5 (got %u)", song.lane[LANE_KICK].hits);
    CHECK(song.lane[LANE_KICK].mask != kick_before, "K1 changed the kick lane mask (%04x -> %04x)", kick_before, song.lane[LANE_KICK].mask);
    CHECK(song.lane[LANE_KICK].mask == song_euclid(5, 16, 0), "kick mask is the Euclidean E(5,16)");
    CHECK(!song.lane[LANE_KICK].manual, "turning Hits clears manual");
    CHECK(engine_drum_steps(0, 16) == 5, "song_write_drums put 5 kick notes into the engine (got %u)", engine_drum_steps(0, 16));
    /* Bass follows the kick lane under KICK-LOCK. */
    groove_get_view(LT_BASS, 0, &v);
    CHECK(v.step.velocity != 0, "bass note under the first kick");

    /* PLAY starts the transport; PLAY again stops it. */
    drain_midi(&on);
    sim_control(0, LB_PLAY, 1); sim_control(0, LB_PLAY, 0);
    sim_advance(30);
    CHECK(song_playing(), "PLAY started the transport");
    sim_advance(700);
    CHECK(song_step() > 0, "play head advanced (step %u)", song_step());
    sim_control(0, LB_PLAY, 1); sim_control(0, LB_PLAY, 0);
    sim_advance(30);
    CHECK(!song_playing(), "PLAY again stopped the transport");

#ifdef LEARN_BOARD_SMK37
    /* A pad tap toggles a step of the selected lane and marks it manual. */
    {
        uint16_t before = song.lane[LANE_KICK].mask;
        sim_smk37(3 /*SMK37_PAD*/, 1, 100); sim_advance(10); sim_smk37(3, 1, 0); sim_advance(20);
        CHECK(song.lane[LANE_KICK].mask == (before ^ 2U), "pad 2 toggled kick step 1 (%04x -> %04x)", before, song.lane[LANE_KICK].mask);
        CHECK(song.lane[LANE_KICK].manual == 1, "pad tap set manual");
        groove_get_view(LT_DRUMS, 1, &v);
        CHECK((v.step.drums & 1U) == ((song.lane[LANE_KICK].mask >> 1) & 1U), "engine step 1 follows the toggled mask");
        CHECK(sim_pad_rgb(1) != 0 || sim_pad_rgb(0) != 0, "grid pads are lit");
        /* SCL picks the next lane; the pad colour follows the lane. */
        sim_control(0, LB_SCL, 1); sim_control(0, LB_SCL, 0); sim_advance(20);
        CHECK(sim_pad_rgb(4) == P_YELLOW, "snare lane: a lit step is yellow (got %06x)", sim_pad_rgb(4));
        sim_control(0, LB_SCL, 1); sim_control(0, LB_SCL, 0); sim_advance(20);
        sim_control(0, LB_SCL, 1); sim_control(0, LB_SCL, 0); sim_advance(20);
        sim_control(0, LB_SCL, 1); sim_control(0, LB_SCL, 0); sim_advance(20); /* back to kick */
    }
    /* Faders set track levels. */
    sim_smk37(7 /*SMK37_FADER*/, 1, 40); sim_advance(20);
    groove_get_view(LT_BASS, 0, &v);
    CHECK(v.volume == 40, "fader 2 set the bass level to 40 (got %u)", v.volume);
    sim_smk37(7, 1, 100); sim_advance(20);
#else
    /* FM-1: GLO + key 1 toggles step 1 of the lane. */
    {
        uint16_t before = song.lane[LANE_KICK].mask;
        sim_control(0, LB_GLO, 1); sim_control(1, 1, 1); sim_advance(10); sim_control(1, 1, 0); sim_control(0, LB_GLO, 0); sim_advance(20);
        CHECK(song.lane[LANE_KICK].mask == (before ^ 2U), "GLO+key 2 toggled kick step 1 (%04x -> %04x)", before, song.lane[LANE_KICK].mask);
        CHECK(song.lane[LANE_KICK].manual == 1, "key toggle set manual");
    }
#endif

    /* Swing (K3) is locked until a rhythm round is logged. */
    {
        unsigned swing_before = song.swing;
#ifdef LEARN_BOARD_SMK37
        sim_smk37(4, 0 /*physical K3 -> logical 2*/, 2);
#else
        sim_control(2, 2, 2);
#endif
        sim_advance(30);
        CHECK(song.swing == swing_before, "swing is locked before the trainer (still %u)", song.swing);
        CHECK(!log_unlocked(UNLOCK_SWING), "UNLOCK_SWING not yet earned");
        /* A logged, passed on-beat rhythm round unlocks it. */
        log_add(LOGK_RHYTHM, 16, 0, 100, 0, 0, 100);
        CHECK(log_unlocked(UNLOCK_SWING), "a passed rhythm round unlocks swing");
#ifdef LEARN_BOARD_SMK37
        sim_smk37(4, 0, 2);
#else
        sim_control(2, 2, 2);
#endif
        sim_advance(30);
        CHECK(song.swing == swing_before + 2, "K3 now moves swing (%u -> %u)", swing_before, song.swing);
        CHECK(groove_sequence_setting(0) == song.swing - 50, "engine swing follows the model (%u)", groove_sequence_setting(0));
    }

    /* The rhythm trainer: ENV starts it, taps are scored, the round logs an entry. */
    {
        unsigned logs_before = log_count();
        sim_control(0, LB_ENV, 1); sim_control(0, LB_ENV, 0);
        sim_advance(50);
        drain_midi(&on);
        /* Count-in is 16 steps at 150 ms (100 BPM); then 64 scored steps. Tap every beat on time. */
        sim_advance(16 * 150 - 50 + 5);
        for (unsigned beat = 0; beat < 16; ++beat) {
#ifdef LEARN_BOARD_SMK37
            sim_smk37(3, 0, 100); sim_advance(10); sim_smk37(3, 0, 0);
#else
            sim_control(1, 0, 1); sim_advance(10); sim_control(1, 0, 0);
#endif
            sim_advance(600 - 10);
        }
        sim_advance(400);
        CHECK(log_count() == logs_before + 1, "the on-beat round was logged (%u -> %u)", logs_before, log_count());
        const struct log_entry *e = log_get(0);
        CHECK(e && e->kind == LOGK_RHYTHM, "log entry is a rhythm round");
        CHECK(e && e->hits >= 13, "most taps were on time (hits %u, misses %u)", e ? e->hits : 0, e ? e->misses : 0);
        sim_control(0, LB_HOME, 1); sim_control(0, LB_HOME, 0); sim_advance(30);
    }

    /* Chord recording: REC arms, a chord pad / key writes the beat under the cursor. */
    {
        sim_control(0, LB_REC, 1); sim_control(0, LB_REC, 0); sim_advance(30);
        CHECK(song_playing(), "REC started the loop with the click");
#ifdef LEARN_BOARD_SMK37
        sim_control(0, LB_FX, 1); sim_control(0, LB_FX, 0); sim_advance(20); /* pads -> chords */
        sim_smk37(3, 3 /*IV*/, 100); sim_advance(10); sim_smk37(3, 3, 0); sim_advance(20);
#else
        sim_control(1, 0, 1); sim_advance(10); sim_control(1, 0, 0); sim_advance(20); /* F3 = IV in C */
#endif
        unsigned found = 0; for (unsigned b = 0; b < 4; ++b) if (song.chord[b].degree == 4) found = 1;
        CHECK(found, "a chord tap stored IV in the loop");
        sim_control(0, LB_HOME, 1); sim_control(0, LB_HOME, 0); sim_advance(30);
        CHECK(!song_playing(), "HOME stopped the loop");
    }

    /* Melody recorder: REC+PLAY arms the engine recorder on LT_MELODY. */
    {
        sim_control(0, LB_REC, 1); sim_control(0, LB_PLAY, 1); sim_control(0, LB_PLAY, 0); sim_control(0, LB_REC, 0);
        sim_advance(30);
        CHECK(song_recording(), "REC+PLAY armed the melody recorder");
        CHECK(song_playing(), "and the loop runs");
        sim_control(0, LB_HOME, 1); sim_control(0, LB_HOME, 0); sim_advance(30);
        CHECK(!song_recording(), "HOME disarmed the recorder");
    }
    /* Page 2 SysEx: a GRID message from the partner replaces a lane. */
    {
        extern int learn_sysex(const uint8_t *, size_t);
        const uint8_t grid_msg[] = {0xF0, 0x7D, 'L', 'N', 1, 3, 2, LCMD_GRID, LANE_SNARE, 0x10, 0x01, 0x00, 0xF7}; /* 0x0090 */
        learn_sysex(grid_msg, sizeof grid_msg);
        sim_advance(30);
        CHECK(song.lane[LANE_SNARE].mask == 0x0090, "GRID SysEx replaced the snare lane (got %04x)", song.lane[LANE_SNARE].mask);
    }
    msgs = drain_midi(&on); (void)msgs;
    /* Back to page 1 so later tests see the stage-1 page again. */
    for (unsigned i = 0; i < LEARN_STAGE - 1; ++i) { sim_control(0, LB_SEQ, 1); sim_control(0, LB_SEQ, 0); sim_advance(20); }
    CHECK(learn_page() == 1, "returned to page 1");
}
