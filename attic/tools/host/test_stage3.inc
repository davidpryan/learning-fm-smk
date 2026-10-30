/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Stage 3 host tests: Shadow (SMK-37) / Tape (FM-1). Included by harness.c. */
#include "learn_song.h"

static void s3_tap(unsigned button) { sim_control(0, button, 1); sim_control(0, button, 0); sim_advance(20); }

/* Source pad / rule pad on the SMK; the same choices as GLO + key on the FM-1. */
static void s3_choose(unsigned index)
{
#ifdef LEARN_BOARD_SMK37
    sim_smk37(3 /*SMK37_PAD*/, index, 100); sim_advance(10); sim_smk37(3, index, 0); sim_advance(20);
#else
    sim_control(0, LB_GLO, 1); sim_control(1, index, 1); sim_advance(10); sim_control(1, index, 0); sim_control(0, LB_GLO, 0); sim_advance(20);
#endif
}

/* Count the diatonic scale steps from a to b in the key the page is using (C major). */
static int s3_scale_steps(unsigned a, unsigned b)
{
    int steps = 0;
    for (unsigned n = a < b ? a : b; n < (a < b ? b : a); ++n) if (theory_in_scale(0, SCALE_MAJOR, n + 1)) ++steps;
    return a < b ? steps : -steps;
}

static void test_stage3(void)
{
    unsigned on;
    struct groove_view v;
    /* Page 3 is two SEQ presses from page 1. */
    while (learn_page() != 1) s3_tap(LB_SEQ);
    s3_tap(LB_SEQ); s3_tap(LB_SEQ);
    sim_advance(60);
    CHECK(learn_page() == 3, "SEQ twice switched to page 3 (got %u)", learn_page());
    /* The stage-1 tests left the key in G: put it back in C major so the thirds below are checkable. */
    {
        extern int learn_sysex(const uint8_t *, size_t);
        const uint8_t key_msg[] = {0xF0, 0x7D, 'L', 'N', 1, 3, 1, LCMD_KEY, 0, SCALE_MAJOR, 0xF7};
        learn_sysex(key_msg, sizeof key_msg);
        sim_advance(60);
    }
    CHECK(lit_pixels() > 100, "page 3 drew the lanes (%u lit samples)", lit_pixels());
    drain_midi(&on);

    /* The song model is rendered (stage 2 did it) and the shadow tracks start empty. */
    groove_get_view(LT_CHORDS, 0, &v);
    CHECK(v.active_steps != 0, "chord track has notes to shadow");
    groove_get_view(LT_SHADOW_A, 0, &v);
    CHECK(v.active_steps == 0, "shadow A starts empty");

    /* Hold shadow: source Chords (pad 1), rule Hold (pad 9). */
    s3_choose(0);
    s3_choose(8);
    sim_advance(50);
    groove_get_view(LT_SHADOW_A, 0, &v);
    CHECK(v.active_steps != 0, "Hold shadow rendered active steps into LT_SHADOW_A");
    CHECK(v.step.velocity != 0, "Hold shadow has a chord at step 0");
    CHECK(groove_step_setting(LT_SHADOW_A, 1, 4) == 1, "Hold ties the chord through the beat");
    {
        /* Step 0 carries the song's first chord: the shadow's root is that chord's root. */
        uint8_t notes[4]; unsigned n;
        song_chord_notes(song.chord[0].degree, song.chord[0].seventh, notes, &n);
        groove_get_view(LT_SHADOW_A, 0, &v);
        CHECK(n && v.step.note % 12 == notes[0] % 12, "Hold shadow root at step 0 matches the song chord (note %u, chord root %u)", v.step.note, n ? notes[0] : 0);
    }
#ifdef LEARN_BOARD_SMK37
    CHECK(sim_pad_rgb(3) != 0, "shadow A's pad is lit");
    CHECK(sim_pad_rgb(0) == P_CYAN || sim_pad_rgb(0) == P_FULL(P_CYAN) || sim_pad_rgb(0) == P_DIM(P_CYAN), "Chords pad is cyan (got %06x)", sim_pad_rgb(0));
#endif

    /* Third-above shadow of a melody written straight into the engine. */
    {
        static const uint8_t mel[4] = {60, 62, 64, 67};  /* C D E G */
        groove_edit_clear(LT_MELODY, 0, 2);
        for (unsigned i = 0; i < 4; ++i) groove_sequence_edit(LT_MELODY, i * 4, mel[i], 100);
        s3_choose(1);      /* Melody */
        s3_choose(10);     /* Third above */
        sim_advance(50);
        groove_get_view(LT_SHADOW_B, 0, &v);
        CHECK(v.active_steps != 0, "Third-above shadow rendered into LT_SHADOW_B");
        CHECK(v.step.velocity != 0, "shadow B has a note at step 0");
        CHECK(v.step.note == 64, "shadow B step 0 is E, a diatonic third above C (got %u)", v.step.note);
        CHECK(s3_scale_steps(60, v.step.note) == 2, "that is two scale steps up");
        groove_get_view(LT_SHADOW_B, 12, &v);
        CHECK(v.step.note == 71, "shadow B step 12 is B, a third above G (got %u)", v.step.note);
        /* The shadow follows the melody: editing the source re-renders after the revision settles. */
        groove_sequence_edit(LT_MELODY, 0, 65, 100); /* F */
        sim_advance(400);
        groove_get_view(LT_SHADOW_B, 0, &v);
        CHECK(v.step.note == 69, "editing the melody re-rendered the shadow (F -> A, got %u)", v.step.note);
    }

    /* Tape: a full pass is captured as a take and can be undone. */
    {
        sim_control(0, LB_REC, 1); sim_control(0, LB_PLAY, 1); sim_control(0, LB_PLAY, 0); sim_control(0, LB_REC, 0);
        sim_advance(30);
        CHECK(groove_edit_recording(), "REC+PLAY started taping the melody track");
        CHECK(song_playing(), "and the loop runs");
        /* A key while taping is recorded on LT_MELODY by the engine's own recorder. */
#ifdef LEARN_BOARD_SMK37
        sim_smk37(1 /*SMK37_KEY*/, 9, 100); sim_advance(120); sim_smk37(1, 9, 0);
#else
        sim_control(1, 4, 1); sim_advance(120); sim_control(1, 4, 0);
#endif
        sim_advance(200);
        s3_tap(LB_PLAY);                       /* ends the pass: take 1 */
        CHECK(!groove_edit_recording(), "PLAY ended the tape pass");
        groove_get_view(LT_MELODY, 0, &v);
        uint64_t take1 = v.active_steps;
        CHECK(take1 != 0, "take 1 has notes");
        /* Second take: a different melody, then undo returns the first. */
        groove_edit_clear(LT_MELODY, 0, 2);
        groove_sequence_edit(LT_MELODY, 8, 67, 100);
        sim_control(0, LB_REC, 1); sim_control(0, LB_PLAY, 1); sim_control(0, LB_PLAY, 0); sim_control(0, LB_REC, 0);
        sim_advance(60);
        s3_tap(LB_PLAY);
        groove_get_view(LT_MELODY, 0, &v);
        uint64_t take2 = v.active_steps;
        CHECK(take2 != take1, "take 2 differs from take 1");
        s3_tap(LB_SAVE);                       /* undo: back to take 1 */
        groove_get_view(LT_MELODY, 0, &v);
        CHECK(v.active_steps == take1, "SAVE undid to take 1 (%llx vs %llx)", (unsigned long long)v.active_steps, (unsigned long long)take1);
        sim_control(0, LB_GLO, 1); s3_tap(LB_SAVE); sim_control(0, LB_GLO, 0); sim_advance(20);
        groove_get_view(LT_MELODY, 0, &v);
        CHECK(v.active_steps == take2, "GLO+SAVE redid to take 2");
        CHECK(log_count() >= 2, "takes were logged (%u entries)", log_count());
        s3_tap(LB_HOME);
        CHECK(!song_playing(), "HOME stopped the loop");
    }
    /* Export: HOME+REC emits LCMD_TRACK frames, one per poll. */
    {
        unsigned n, port, frames = 0; uint8_t m[128];
        /* HOME's all-notes-off burst is flushed by the router over many polls and the
         * capture ring holds 64 messages: keep draining until it is quiet. */
        sim_control(0, LB_HOME, 1);
        for (unsigned i = 0; i < 40; ++i) { sim_advance(10); drain_midi(&on); }
        sim_control(0, LB_REC, 1); sim_control(0, LB_REC, 0); sim_control(0, LB_HOME, 0);
        for (unsigned i = 0; i < 60; ++i) {
            sim_advance(10);
            while ((n = sim_midi_pop(m, &port)) != 0) if (port == 1 && n > 9 && m[0] == 0xF0 && m[2] == 'L' && m[7] == LCMD_TRACK) ++frames;
        }
        CHECK(frames == 4 * (song.bars * 16 / 8), "export produced one LCMD_TRACK frame per 8 cells of 4 tracks (got %u)", frames);
    }
    /* Back to page 1 so later tests see the stage-1 page again. */
    while (learn_page() != 1) s3_tap(LB_SEQ);
    CHECK(learn_page() == 1, "returned to page 1");
}
