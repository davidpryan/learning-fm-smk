/* SPDX-License-Identifier: GPL-3.0-only */
/* Stage 4 (Scribe, the FM-1 build) on SLOOP's UI harness: sections stored and recalled at the loop boundary,
 * energy mutes, transitions, the song record. */
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
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; frame(); }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }
static void glo_key(uint32_t k) { fm1_in.buttons |= BT(B_GLO); frame(); kdown(k); frames(1); kup(k); frames(1); fm1_in.buttons &= ~BT(B_GLO); frame(); }

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    for (i = 0; i < NB; i++) panel.btn[i] = (uint8_t)i;
    for (i = 0; i < NE; i++) panel.enc[i] = (uint8_t)i, panel.dir[i] = 1;
    layers_init(); host_tracks_init(); go_home(); ui.force = 1; frame();
    hold_both(1700);
    tap(B_ENV); tap(B_ENV);                       /* (the welcome screen: the first press dismisses it) */
    tap(B_SEQ); frames(2); tap(B_SEQ); frames(2); tap(B_SEQ); frames(2);
    check(learn_page() == 4, "page 4");
    check(lsong.bars == 1 && trk[LT_CHORDS].step[0].n == 3, "the loop is there");
    shot("learn-s4-idle");

    /* store slot 1 as VERSE (GLO + key 9), then change the chords and store slot 2 as CHORUS */
    glo_key(8);
    check(s4_sec[0].used && s4_sec[0].type == STY_VERSE && s4_active == 0, "GLO + key 9: slot 1 stored as VERSE, active");
    tap(B_SCL); tap(B_SCL);
    check(s4_store_type == STY_CHORUS, "SCL twice: STORE AS CHORUS");
    lsong.chord[0].degree = 4; lsong.chord[1].degree = 5; song_write_chords(); song_write_bass();
    encs[panel.enc[0]] = -1; frame(); encs[panel.enc[0]] = -1; frame(); encs[panel.enc[0]] = -1; frame();
    check(s4_energy == 4 && trk[LT_KEYS].p[P_MUTE] == 0, "K1 down: energy 4, the melody still in");
    encs[panel.enc[0]] = -1; frame();
    check(s4_energy == 3 && trk[LT_KEYS].p[P_MUTE] == 1 && trk[LT_DRUMS].p[P_MUTE] == 0, "energy 3: the melody out, the drums in");
    glo_key(9);
    check(s4_sec[1].used && s4_sec[1].type == STY_CHORUS && s4_sec[1].energy == 3 && s4_sec[1].chord[0].degree == 4, "GLO + key 10: slot 2 CHORUS with its energy and chords");
    shot("learn-s4-stored");

    /* play, queue slot 1: it is recalled at the loop boundary, with slot 1's chords and energy */
    tap(B_PLAY); frames(3);
    check(song.playing, "PLAY");
    glo_key(0);
    check(s4_queued == 0 && s4_qmode == QM_BAR, "GLO + key 1: slot 1 queued for the next loop");
    for (i = 0; i < 600 && s4_queued != S4_NONE; i++) frame();
    check(s4_queued == S4_NONE && s4_active == 0, "recalled at the loop boundary");
    check(lsong.chord[0].degree == 1 && trk[LT_CHORDS].step[0].note[0] == 60 && s4_energy == 7 && trk[LT_KEYS].p[P_MUTE] == 0, "slot 1's chords, energy and mutes are back");
    shot("learn-s4-playing");

    /* the record: REC, a change to slot 2, HOME */
    tap(B_REC);
    check(s4_recording, "REC: the song record runs");
    encs[panel.enc[4]] = 1; frame();
    check(s4_trans == TRN_DROP, "K5: the DROP transition");
    glo_key(1);
    for (i = 0; i < 600 && s4_queued != S4_NONE; i++) frame();
    check(s4_active == 1 && s4_rec_n == 1 && s4_rec[0].slot == 0, "slot 2 recalled; the record has the verse");
    check(trk[LT_DRUMS].p[P_MUTE] == 0 || s4_energy < 1, "the drop was undone by the recall");
    tap(B_HOME); frames(2);
    check(!s4_recording && s4_rec_n == 2 && !song.playing, "HOME: the record ends with two sections, stopped");
    shot("learn-s4-record");
    tap(B_HOME); frames(2);
    check(s4_rec_n == 0, "HOME again: discarded");
    hold_both(1700);
    check(!learn_on, "back to SLOOP");
    printf("learn s4: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
