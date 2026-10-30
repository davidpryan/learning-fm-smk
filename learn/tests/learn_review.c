/* SPDX-License-Identifier: GPL-3.0-only */
/* The UI review tour: drives every page through its main states and writes each screen with the device's lights
 * under it (SMK-37: the 16 pad colours; FM-1: the 27 key LEDs) to DIR/<board>-<name>.ppm. Not a test: no checks.
 * From the repo root: tools/ui_review.sh (builds it for both boards, writes docs/ui-review/*.png, FM-1 left,
 * SMK-37 right). LEARN_UI_SMK=1 renders the SMK-37's screens, names and pads on the FM-1 harness. */
#define UI_PAGES_HARNESS_ONLY 1
#include "../pre.h"
#include "ui_pages_test.c"
#include "../learn.c"

#if LEARN_UI_SMK
#define BOARD_TAG "smk"
#else
#define BOARD_TAG "fm1"
#endif

/* the screen and, under it, the device's lights as the player sees them */
static void shot_dev(const char *name)
{
    char path[512];
    FILE *f;
    unsigned i, x, y, H = 240 + 44;
    static uint8_t img[240 * 284 * 3];
    ui.force = 1; frame();
    memset(img, 24, sizeof img);
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t p = swap16(screen[i]);
        img[i * 3] = (uint8_t)((p >> 11) * 255 / 31); img[i * 3 + 1] = (uint8_t)(((p >> 5) & 63) * 255 / 63); img[i * 3 + 2] = (uint8_t)((p & 31) * 255 / 31);
    }
#if LEARN_UI_SMK
    for (i = 0; i < 16u; i++) {                   /* the pads: top row 1-8, bottom row 9-16 */
        uint32_t c = page_pad_colour(i);
        unsigned px = 4 + (i & 7u) * 29u, py = 244 + (i >= 8 ? 20u : 0u);
        for (y = 0; y < 18u; y++) for (x = 0; x < 27u; x++) {
            uint8_t *o = &img[((py + y) * 240u + px + x) * 3u];
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
        }
    }
#else
    for (i = 0; i < 27u; i++) {                   /* the key LEDs: dim = lit, full = follow-me */
        unsigned px = 4 + i * 8u, on = (lbright_keys >> i) & 1u, dim = (llit_keys >> i) & 1u;
        for (y = 0; y < 10u; y++) for (x = 0; x < 6u; x++) {
            uint8_t *o = &img[((250 + y) * 240u + px + x) * 3u];
            o[0] = o[1] = (uint8_t)(on ? 255 : dim ? 110 : 40); o[2] = (uint8_t)(on ? 200 : dim ? 60 : 40);
        }
    }
#endif
    snprintf(path, sizeof path, "%s/" BOARD_TAG "-%s.ppm", outdir, name);
    f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n240 %u\n255\n", H);
    fwrite(img, 1, 240u * H * 3u, f);
    fclose(f);
}
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
static void glo(uint32_t b) { fm1_in.buttons |= BT(B_GLO); frame(); tap(b); fm1_in.buttons &= ~BT(B_GLO); frame(); }
static void glo_key(uint32_t k) { fm1_in.buttons |= BT(B_GLO); frame(); kdown(k); kup(k); fm1_in.buttons &= ~BT(B_GLO); frame(); }
static void pad(uint32_t p, uint32_t v) { lev_push(LEV_PAD, p, v); frame(); }
static void glo_pad(uint32_t p) { fm1_in.buttons |= BT(B_GLO); frame(); pad(p, 100); pad(p, 0); fm1_in.buttons &= ~BT(B_GLO); frame(); }
static void knob(unsigned role, int d) { encs[panel.enc[role]] = d; frame(); }

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    for (i = 0; i < NB; i++) panel.btn[i] = (uint8_t)i;
    for (i = 0; i < NE; i++) panel.enc[i] = (uint8_t)i, panel.dir[i] = 1;
    layers_init(); host_tracks_init(); go_home(); ui.force = 1; frame();
    hold_both(1700);
    shot_dev("p1-01-welcome");
    tap(B_ENV); tap(B_ENV);
    frames(200);
    shot_dev("p1-02-idle");
    kdown(7); frames(2); shot_dev("p1-03-note-c4"); kup(7); frames(2);
    kdown(7); kdown(11); kdown(14); frames(2); shot_dev("p1-04-chord-ceg"); kup(7); kup(11); kup(14); frames(2);
    kdown(8); frames(2); shot_dev("p1-05-nudged-csharp"); kup(8); frames(2);
    if (LEARN_PADS) { pad(4, 100); frames(2); shot_dev("p1-06-pad-V"); pad(4, 0); frames(2); }
    else { glo(B_SCL); kdown(9); frames(2); shot_dev("p1-06-chordkey-G"); kup(9); glo(B_SCL); frames(2); }
    knob(2, 1); shot_dev("p1-07-knob-reverb");
    tap(B_ARP); frames(2); shot_dev("p1-08-lesson-text");
    key(7); key(7); frames(2);
    shot_dev("p1-09-drill-listen");
    for (i = 0; i < 200 && s1d.state != DS_WAIT_ANSWER; i++) frame();
    shot_dev("p1-10-drill-yourturn");
    for (i = 0; i < 6000 && lesson_running(); i++) {
        if (s1d.state == DS_WAIT_ANSWER && !s1d.answer_count) { int k = (int)s1d.target[0] - 53; key(k >= 0 && k < 27 ? (uint32_t)k : 0u); }
        else frame();
    }
    shot_dev("p1-11-lesson-done");
    frames(150); shot_dev("p1-12-result-card");
    glo(B_ARP); shot_dev("p1-13-log"); tap(B_ENV);
    glo(B_SEQ); shot_dev("p1-14-help1"); tap(B_SEQ); shot_dev("p1-15-help2"); tap(B_SEQ); shot_dev("p1-16-help3"); tap(B_SEQ);
    glo_key(2); tap(B_SCL); frames(60); shot_dev("p1-17-g-minor");
    tap(B_PLAY); for (i = 0; i < 200 && s1d.state != DS_WAIT_ANSWER; i++) frame();
    key(0); frames(60); shot_dev("p1-18-drill-miss");
    tap(B_HOME); frames(2);
    knob(6, 1); tap(B_PLAY); for (i = 0; i < 200 && s1d.state != DS_WAIT_ANSWER; i++) frame();
    shot_dev("p1-19-chord-drill");
    tap(B_HOME); frames(150);

    /* ---- page 2 ---- */
    tap(B_SEQ); frames(100); shot_dev("p2-01-idle");
    tap(B_PLAY); frames(40); shot_dev("p2-02-playing");
    if (LEARN_PADS) { pad(3, 100); pad(3, 0); frames(2); shot_dev("p2-03-pad-step"); }
    else { glo_key(3); shot_dev("p2-03-glokey-step"); }
    tap(B_FX); frames(2);
    if (LEARN_PADS) { pad(4, 100); frames(2); shot_dev("p2-04-chords-pad-V"); pad(4, 0); }
    else { kdown(2); frames(2); shot_dev("p2-04-chordkey-G"); kup(2); }
    tap(B_FX);
    knob(2, 1); shot_dev("p2-05-swing-locked");
    tap(B_HOME); frames(2);
    tap(B_ENV); frames(10); shot_dev("p2-06-trainer-countin");
    for (i = 0; i < 2000 && tr.state == TS_COUNT_IN; i++) frame();
    {
        unsigned taps = 0;
        while (tr.state == TS_SCORING && taps < 16) {
            int32_t rel = (int32_t)(fm1_ms - tr.t0), nr = (rel + (int32_t)tr.step_ms / 2) / (int32_t)tr.step_ms;
            if (nr >= 0 && nr < (int32_t)tr.count && (nr % 4) == 0 && !tr.hit_flag[nr] && rel >= nr * (int32_t)tr.step_ms) {
                if (LEARN_PADS) { pad(0, 100); pad(0, 0); } else key(0);
                taps++;
                if (taps == 4) shot_dev("p2-07-trainer-scoring");
            }
            frame();
        }
    }
    tap(B_ENV); frames(2);
    knob(5, 1); frames(2);
    tap(B_FX); if (LEARN_PADS) tap(B_FX);
    fm1_in.buttons |= BT(B_REC); frame(); tap(B_PLAY); fm1_in.buttons &= ~BT(B_REC); frame();
    frames(4); shot_dev("p2-08-melody-rec");
    kdown(7); frames(4); kup(7); kdown(9); frames(4); kup(9);
    for (i = 0; i < 600 && s2_mel_rec; i++) frame();
    frames(10); shot_dev("p2-09-take-done");
    tap(B_HOME); frames(150);
    glo(B_SEQ); shot_dev("p2-10-help1"); tap(B_SEQ); shot_dev("p2-11-help2"); tap(B_SEQ); shot_dev("p2-12-help3"); tap(B_SEQ);

    /* ---- page 3 ---- */
    tap(B_SEQ); frames(100); shot_dev("p3-01-idle");
    if (LEARN_PADS) pad(0, 100), pad(0, 0); else glo_key(0);
    frames(2); shot_dev("p3-02-source-armed");
    if (LEARN_PADS) pad(8 + RULE_ARP, 100), pad(8 + RULE_ARP, 0); else glo_key(8 + RULE_ARP);
    frames(2); shot_dev("p3-03-arp-shadow");
    frames(300);
    fm1_in.buttons |= BT(B_REC); frame(); tap(B_PLAY); fm1_in.buttons &= ~BT(B_REC); frame();
    frames(4); kdown(7); frames(4); kup(7); kdown(11); frames(4); kup(11);
    shot_dev("p3-04-taping");
    for (i = 0; i < 2000 && s3_recording(); i++) { frame(); if (i == 300) tap(B_PLAY); }
    if (LEARN_PADS) pad(1, 100), pad(1, 0), pad(8 + RULE_THIRD, 100), pad(8 + RULE_THIRD, 0); else glo_key(1), glo_key(8 + RULE_THIRD);
    frames(2); shot_dev("p3-05-third-above");
    tap(B_HOME); frames(150); shot_dev("p3-06-stopped");
    glo(B_SEQ); shot_dev("p3-07-help1"); tap(B_SEQ); shot_dev("p3-08-help2"); tap(B_SEQ); tap(B_SEQ);

    /* ---- page 4 ---- */
    tap(B_SEQ); frames(100); shot_dev("p4-01-idle");
    if (LEARN_PADS) glo_pad(8); else glo_key(8);
    frames(2); shot_dev("p4-02-stored-verse");
    tap(B_SCL); tap(B_SCL);
    lsong.chord[0].degree = 4; song_write_chords();
    if (LEARN_PADS) glo_pad(9); else glo_key(9);
    frames(2);
    tap(B_PLAY); frames(10);
    if (LEARN_PADS) pad(0, 100), pad(0, 0); else glo_key(0);
    frames(4); shot_dev("p4-03-queued");
    for (i = 0; i < 600 && s4_queued != S4_NONE; i++) frame();
    shot_dev("p4-04-recalled");
    tap(B_REC); frames(4);
    if (LEARN_PADS) pad(1, 100), pad(1, 0); else glo_key(1);
    for (i = 0; i < 600 && s4_queued != S4_NONE; i++) frame();
    frames(60); shot_dev("p4-05-recording");
    tap(B_HOME); frames(10); shot_dev("p4-06-record-kept");
    frames(150);
    glo(B_SEQ); shot_dev("p4-07-help1"); tap(B_SEQ); shot_dev("p4-08-help2"); tap(B_SEQ); tap(B_SEQ);
    printf("review " BOARD_TAG ": done\n");
    return 0;
}
