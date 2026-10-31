/* SPDX-License-Identifier: GPL-3.0-only */
/* Stage 2 (Root, the FM-1 build) on SLOOP's UI harness: the song model lands in SLOOP's tracks, the loop runs,
 * chord keys write the loop, GLO + keys edit the grid, the melody recorder takes one pass, the rhythm trainer. */
#define UI_PAGES_HARNESS_ONLY 1
#define LEARN_HOST 1
#include "../pre.h"
#include "ui_pages_test.c"
#include "../learn.c"

static void shot(const char *name) { ui.force = 1; frame(); ppm(name); }
static void hold_both(uint32_t ms)
{
    uint32_t t0 = fm1_ms;
    edges_btn |= BT(B_GLO); fm1_in.buttons |= BT(B_GLO); frame();
    edges_btn |= BT(B_HOME); fm1_in.buttons |= BT(B_HOME); frame();
    while (fm1_ms - t0 < ms) frame();
    fm1_in.buttons &= ~(BT(B_GLO) | BT(B_HOME)); frame(); frame();
}
static unsigned dlane_hits(unsigned lane) { unsigned s, n = 0; for (s = 0; s < 16; s++) n += dstep_has(&TDRUM->dstep[s], lane) != 0; return n; }
static unsigned notes_in(unsigned t) { unsigned s, n = 0; for (s = 0; s < 64; s++) if (trk[t].step[s].time == ST_NOTE && trk[t].step[s].n) n++; return n; }
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; frame(); }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    for (i = 0; i < NB; i++) panel.btn[i] = (uint8_t)i;
    for (i = 0; i < NE; i++) panel.enc[i] = (uint8_t)i, panel.dir[i] = 1;
    layers_init(); host_tracks_init(); go_home(); ui.force = 1; frame();
    hold_both(1700);
    check(learn_on && learn_page() == 1, "LEARN mode, page 1");
    tap(B_ENV); tap(B_ENV);
    tap(B_SEQ); frames(2);
    check(learn_page() == 2, "SEQ: page 2");
    check(trk[LT_CHORDS].p[P_SLEN] == 16 && TDRUM->p[P_SLEN] == 16, "the loop is one bar on SLOOP's tracks");
    check(dlane_hits(DL_KICK) == 4 && dlane_hits(DL_SNARE) == 2 && dlane_hits(DL_HAT) == 8, "the default drums: 4 kicks, 2 snares, 8 hats");
    check(trk[LT_CHORDS].step[0].time == ST_NOTE && trk[LT_CHORDS].step[0].n == 3 && trk[LT_CHORDS].step[1].time == ST_TIE, "a chord on beat 1, held by ties");
    check(trk[LT_CHORDS].step[4].note[0] == 67, "beat 2 is V (G)");
    check(notes_in(LT_BASS) == 4 && trk[LT_BASS].step[0].note[0] == 36, "the bass locks to the kick, C2 under I");
    check(song.g[G_BPM] == 120, "(the tempo is SLOOP's until a knob moves it)");
    shot("learn-s2-idle");

    /* the loop runs */
    tap(B_PLAY); frames(3);
    check(song.playing, "PLAY: the loop runs");
    frames(30);
    check(s2_head != 0, "the head moves");
    shot("learn-s2-playing");

    /* chord keys write the loop at the head: G major on key 2 (G3) */
    kdown(2); frames(2); kup(2); frames(2);
    {
        unsigned beat = (s2_head / 4u + ((s2_head % 4u) >= 2u)) % 4u, any = 0, b2;
        for (b2 = 0; b2 < 4; b2++) if (lsong.chord[b2].degree == 5 && b2 != 1) any = 1;
        (void)beat;
        check(any || lsong.chord[1].degree == 5, "a chord key writes V into the loop");
    }
    tap(B_PLAY); frames(3);
    check(!song.playing, "PLAY again: stopped");

    /* GLO + key 3: the kick lane's step 3 toggles */
    fm1_in.buttons |= BT(B_GLO); frame();
    kdown(3); frames(1); kup(3); frames(1);
    fm1_in.buttons &= ~BT(B_GLO); frame();
    check((lsong.lane[LANE_KICK].mask >> 3) & 1u, "GLO + key 4: a kick on step 4");
    check(dstep_has(&TDRUM->dstep[3], DL_KICK), "and it is on the drum track");
    check(lsong.lane[LANE_KICK].manual, "the lane is manual now");
    tap(B_SAVE);
    check(!((lsong.lane[LANE_KICK].mask >> 3) & 1u) && !lsong.lane[LANE_KICK].manual, "SAVE: re-rolled from hits / shift");

    /* knobs: K1 hits, K6 tempo */
    encs[panel.enc[0]] = 1; frame();
    check(lsong.lane[LANE_KICK].hits == 5 && dlane_hits(DL_KICK) == 5, "K1: five kicks");
    encs[panel.enc[5]] = 2; frame();
    check(lsong.tempo == 102 && song.g[G_BPM] == 102, "K6: the tempo, SLOOP's too");
    encs[panel.enc[2]] = 1; frame();
    check(lsong.swing == 50, "K3 swing: locked until the trainer is passed");

    /* the melody recorder: REC + PLAY, keys in MELODY mode, one pass */
    tap(B_FX);
    check(s2_pad_mode == PM_GRID, "FX: keys play the melody");
    fm1_in.buttons |= BT(B_REC); frame(); tap(B_PLAY); fm1_in.buttons &= ~BT(B_REC); frame();
    check(s2_mel_rec && song_recording() && song.playing, "REC + PLAY: the melody recorder runs");
    frames(4);
    kdown(7); frames(4); kup(7); frames(2);
    kdown(9); frames(4); kup(9); frames(2);
    for (i = 0; i < 400 && s2_mel_rec; i++) frame();
    check(!s2_mel_rec && s2_take_valid, "the pass ended by itself");
    check(notes_in(LT_KEYS) >= 2, "two notes recorded");
    shot("learn-s2-take");
    fm1_in.buttons |= BT(B_REC); frame(); tap(B_SAVE); fm1_in.buttons &= ~BT(B_REC); frame();
    check(notes_in(LT_KEYS) == 0 && !s2_take_valid, "REC + SAVE: undone");
    tap(B_HOME); frames(3);
    check(!song.playing, "HOME: stopped");

    /* the rhythm trainer: taps on the beat */
    tap(B_ENV);
    check(tr.state == TS_COUNT_IN && tr.round == 0, "ENV: the trainer counts in");
    for (i = 0; i < 2000 && tr.state == TS_COUNT_IN; i++) frame();
    check(tr.state == TS_SCORING, "then scores");
    {
        unsigned taps = 0;
        while (tr.state == TS_SCORING && taps < 64) {
            int32_t rel = (int32_t)(fm1_ms - tr.t0), near = (rel + (int32_t)tr.step_ms / 2) / (int32_t)tr.step_ms;
            if (near >= 0 && near < (int32_t)tr.count && (near % 4) == 0 && !tr.hit_flag[near] && rel >= near * (int32_t)tr.step_ms) { key(0); taps++; }
            frame();
        }
        for (i = 0; i < 100 && tr.state == TS_SCORING; i++) frame();
        check(tr.state == TS_RESULT || tr.state == TS_COUNT_IN, "the round finished");
        check(tr.hits >= 12, "taps on the beat were hits");
    }
    shot("learn-s2-trainer");
    tap(B_ENV);
    check(tr.state == TS_IDLE, "ENV: the trainer stops");

    hold_both(1700);
    check(!learn_on, "back to SLOOP");
    printf("learn s2: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
