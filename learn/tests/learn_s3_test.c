/* SPDX-License-Identifier: GPL-3.0-only */
/* Stage 3 (Tape, the FM-1 build) on SLOOP's UI harness: shadows render onto SLOOP's tracks and follow their
 * sources, the tape takes a pass, undo / A-B, solo, export. */
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
static unsigned notes_in(unsigned t) { unsigned s, n = 0; for (s = 0; s < 64; s++) if (trk[t].step[s].time == ST_NOTE && trk[t].step[s].n) n++; return n; }
static unsigned poly_in(unsigned t) { unsigned s, n = 0; for (s = 0; s < 64; s++) if (trk[t].step[s].time == ST_NOTE && trk[t].step[s].n >= 2) n++; return n; }
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; frame(); }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }
static void glo_key(uint32_t k) { fm1_in.buttons |= BT(B_GLO); frame(); kdown(k); frames(1); kup(k); frames(1); fm1_in.buttons &= ~BT(B_GLO); frame(); }
static void take_pass(uint32_t a, uint32_t b)
{
    uint32_t i;
    fm1_in.buttons |= BT(B_REC); frame(); tap(B_PLAY); fm1_in.buttons &= ~BT(B_REC); frame();
    frames(4); kdown(a); frames(4); kup(a); frames(2); kdown(b); frames(4); kup(b); frames(2);
    for (i = 0; i < 2000 && s3_recording(); i++) { frame(); if (i == 300) tap(B_PLAY); }
}

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    for (i = 0; i < NB; i++) panel.btn[i] = (uint8_t)i;
    for (i = 0; i < NE; i++) panel.enc[i] = (uint8_t)i, panel.dir[i] = 1;
    layers_init(); host_tracks_init(); go_home(); ui.force = 1; frame();
    hold_both(1700);
    tap(B_ENV); tap(B_ENV);                       /* (the welcome screen: the first press dismisses it) */
    tap(B_SEQ); frames(2); tap(B_SEQ); frames(2);
    check(learn_page() == 3, "page 3");
    check(trk[LT_CHORDS].step[0].n == 3, "the loop from page 2 is there: chords held");
    shot("learn-s3-idle");

    /* a chords shadow: ARP */
    glo_key(0);
    check(s3_src_armed == 1, "GLO + key 1: CHORDS armed");
    glo_key(8 + RULE_ARP);
    check(s3_sh[SRC_CHORDS].used && s3_sh[SRC_CHORDS].rule == RULE_ARP, "GLO + key 10: shadow A is ARP");
    check(notes_in(LT_CHORDS) >= 8 && poly_in(LT_CHORDS) == 0, "the CHORDS track is arpeggiated: single notes");
    shot("learn-s3-arp");
    encs[panel.enc[0]] = 1; frame();
    check(s3_sh[SRC_CHORDS].octave == 1 && trk[LT_CHORDS].step[0].note[0] >= 72, "K1: the shadow an octave up, re-rendered");
    /* the shadow follows its source: a chord change on the model */
    lsong.chord[0].degree = 4; song_write_chords();
    frames(30);
    check(s3_sh[SRC_CHORDS].used && trk[LT_CHORDS].step[0].note[0] % 12 == 5, "a chord change: the arp follows (IV = F)");
    tap(B_FX);
    check(!s3_sh[SRC_CHORDS].used && trk[LT_CHORDS].step[0].n == 3, "FX: the shadow removed, the plain chords back");

    /* the tape: a pass, then a melody shadow a third above */
    take_pass(7, 9);
    check(s3_take_count == 1 && s3_take_cur == 0, "REC + PLAY: one take");
    check(notes_in(LT_KEYS) >= 2, "the take has the notes");
    glo_key(1); glo_key(8 + RULE_THIRD);
    check(s3_sh[SRC_MELODY].used, "shadow B: 3RD ABOVE");
    check(poly_in(LT_KEYS) >= 2, "every melody note got its third");
    {
        unsigned s, ok = 1;
        for (s = 0; s < 64; s++) if (trk[LT_KEYS].step[s].n >= 2) { unsigned a = trk[LT_KEYS].step[s].note[0], b = trk[LT_KEYS].step[s].note[1]; if (b < a + 3 || b > a + 4) ok = 0; }
        check(ok, "a third: 3 or 4 semitones up");
    }
    shot("learn-s3-third");
    take_pass(11, 14);
    check(s3_take_count == 2 && s3_take_cur == 1, "a second take");
    tap(B_SAVE); frames(2);
    check(s3_take_cur == 0, "SAVE: the earlier take");
    check(poly_in(LT_KEYS) >= 2, "with its shadow");
    fm1_in.buttons |= BT(B_GLO); frame(); tap(B_SAVE); fm1_in.buttons &= ~BT(B_GLO); frame();
    check(s3_take_cur == 1, "GLO + SAVE: redo");

    /* a ghost: drums */
    glo_key(2); glo_key(8 + RULE_GHOST);
    {
        unsigned s, hats = 0; for (s = 0; s < 16; s++) hats += dstep_has(&TDRUM->dstep[s], DL_HAT) != 0;
        check(s3_sh[SRC_DRUMS].used && hats == 16, "shadow C GHOST: a hat on every 16th now");
    }

    /* solo over SysEx, the export */
    {
        uint8_t f[] = {0x7D, 'L', 'N', 1, LDEV_ANY, 3, LCMD_SOLO, LT_CHORDS, 1};
        learn_sysex(f, sizeof f); frame();
        check(song.solo == (1u << LT_CHORDS), "SOLO over SysEx: SLOOP solos the chords");
        f[8] = 0; learn_sysex(f, sizeof f); frame();
        check(song.solo == 0, "and back");
    }
    fm1_in.buttons |= BT(B_HOME); frame(); tap(B_REC); fm1_in.buttons &= ~BT(B_HOME); frame();
    check(s3_export_on, "HOME + REC: the export runs");
    for (i = 0; i < 200 && s3_export_on; i++) frame();
    check(!s3_export_on, "and ends");
    tap(B_HOME); frames(2);
    check(!song.playing, "HOME: stopped");
    hold_both(1700);
    check(!learn_on, "back to SLOOP");
    printf("learn s3: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
