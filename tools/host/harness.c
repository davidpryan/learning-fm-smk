/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Host test harness: links the vendor app, the learn layer and the vendor
 * browser engine (web/engine, the same C the official workbench preview
 * runs) into a native executable. It drives the firmware through the
 * simulator's own entry points (sim_boot, sim_control, sim_smk37) and
 * checks observable behaviour: screen pixels, pad colours, MIDI output.
 *
 * No hardware is involved; this proves the learn logic and the hook
 * points compile and behave, not that the JieLi toolchain build is sound. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "learn.h"

/* From web/engine/Simulator.c */
void sim_boot(unsigned clear);
void sim_control(unsigned kind, unsigned index, int value);
void sim_release(void);
void sim_midi_capture(unsigned enable);
unsigned sim_midi_pop(uint8_t *out, unsigned *port);
int sim_smk37(unsigned kind, unsigned id, int value);
uint32_t sim_pixel(unsigned x, unsigned y);
void sim_advance(unsigned ms);
unsigned sim_pad_rgb(unsigned pad);

static unsigned failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Count screen pixels that are not black: a crude "something was drawn" probe. */
static unsigned lit_pixels(void)
{
    unsigned n = 0;
    for (unsigned y = 0; y < 240; y += 2) for (unsigned x = 0; x < 240; x += 2) if (sim_pixel(x, y)) ++n;
    return n;
}

static unsigned drain_midi(unsigned *notes_on)
{
    unsigned count = 0, n, port; uint8_t m[128];
    *notes_on = 0;
    while ((n = sim_midi_pop(m, &port)) != 0) {
        ++count;
        if (port == 1 /*USB*/ && n == 3 && (m[0] & 0xF0) == 0x90 && m[2]) ++*notes_on;
    }
    return count;
}

/* Per-stage tests live beside this file; each defines static void test_stageN(void). */
#if LEARN_STAGE >= 2
#include "test_stage2.inc"
#endif
#if LEARN_STAGE >= 3
#include "test_stage3.inc"
#endif
#if LEARN_STAGE >= 4
#include "test_stage4.inc"
#endif

int main(void)
{
    sim_boot(1);
    sim_midi_capture(1);
    unsigned on, msgs;
    drain_midi(&on);
    CHECK(learn_active(), "learn mode is on after boot");
    sim_advance(50);
    CHECK(lit_pixels() > 100, "stage page drew something (%u lit samples)", lit_pixels());

#ifdef LEARN_BOARD_SMK37
    /* Key 0 at octave 0 is C3 (48). It must sound and leave on MIDI. */
    sim_smk37(1 /*SMK37_KEY*/, 0, 100);
    sim_advance(10);
    msgs = drain_midi(&on);
    CHECK(on == 1, "SMK key press produced one note-on (got %u in %u messages)", on, msgs);
    sim_smk37(1, 0, 0);
    sim_advance(10); drain_midi(&on);
    /* Pad 0 = degree I triad: three note-ons. */
    sim_smk37(3 /*SMK37_PAD*/, 0, 100);
    sim_advance(10);
    msgs = drain_midi(&on);
    CHECK(on == 3, "pad I produced a triad (got %u note-ons)", on);
    CHECK(sim_pad_rgb(0) != 0, "pad I is lit");
    sim_smk37(3, 0, 0);
    sim_advance(10); drain_midi(&on);
    /* An out-of-key black key is nudged when hints are on: C#3 -> C3 or D3 (one note-on). */
    sim_smk37(1, 1, 100); sim_advance(10);
    msgs = drain_midi(&on);
    CHECK(on == 1, "off-key note was nudged, not dropped (got %u)", on);
    sim_smk37(1, 1, 0); sim_advance(10); drain_midi(&on);
#else
    /* FM-1 key 0 = F3 (53), in C major. */
    sim_control(1, 0, 1); sim_advance(10);
    msgs = drain_midi(&on);
    CHECK(on == 1, "FM-1 key press produced one note-on (got %u in %u messages)", on, msgs);
    sim_control(1, 0, 0); sim_advance(10); drain_midi(&on);
#endif
    /* PLAY starts an echo drill: the reference note sounds, then waits for an answer. */
    sim_control(0, LB_PLAY, 1); sim_control(0, LB_PLAY, 0);
    sim_advance(20);
    msgs = drain_midi(&on);
    CHECK(on >= 1, "drill reference note played (got %u)", on);
    sim_advance(700);                 /* reference ends, YOUR TURN */
    drain_midi(&on);
    /* Answer with every key in turn: exactly one of them is the target pitch class. */
    sim_advance(10);
    sim_control(0, LB_HOME, 1); sim_control(0, LB_HOME, 0); /* stop the drill cleanly */
    sim_advance(10);
    /* Mode switch combo: GLO+HOME (FM-1) / SHIFT+HOME (SMK) held for two seconds hands back to stock. */
#ifdef LEARN_BOARD_SMK37
    sim_smk37(0 /*SMK37_BUTTON*/, 0, 1);  /* SHIFT physical */
    sim_smk37(0, 2, 1);                   /* HOME physical  */
    sim_advance(2200);
    CHECK(!learn_active(), "SHIFT+HOME for 2 s switches to the stock UI");
    sim_smk37(0, 0, 0); sim_smk37(0, 2, 0);
    sim_advance(100);
    sim_smk37(0, 0, 1); sim_smk37(0, 2, 1); sim_advance(2200);
    CHECK(learn_active(), "and back to LEARN");
    sim_smk37(0, 0, 0); sim_smk37(0, 2, 0);
#else
    sim_control(0, LB_GLO, 1); sim_control(0, LB_HOME, 1);
    sim_advance(2200);
    CHECK(!learn_active(), "GLO+HOME for 2 s switches to the stock UI");
    sim_control(0, LB_GLO, 0); sim_control(0, LB_HOME, 0);
    sim_advance(100);
    sim_control(0, LB_GLO, 1); sim_control(0, LB_HOME, 1); sim_advance(2200);
    CHECK(learn_active(), "and back to LEARN");
    sim_control(0, LB_GLO, 0); sim_control(0, LB_HOME, 0);
#endif
    /* Learn SysEx: a KEY message changes the root; the stock FM SysEx is untouched. */
    {
        extern int learn_sysex(const uint8_t *, size_t);
        const uint8_t key_msg[] = {0xF0, 0x7D, 'L', 'N', 1, 3, 1, LCMD_KEY, 7, 0, 0xF7};
        CHECK(learn_sysex(key_msg, sizeof key_msg) == 1, "learn claims its own SysEx");
        const uint8_t stock[] = {0xF0, 0x7D, 'F', 'M', 2, 0, 0, 0, 0xF7};
        CHECK(learn_sysex(stock, sizeof stock) == 0, "learn ignores the stock FM1 SysEx");
    }
    sim_advance(50);
#if LEARN_STAGE >= 2
    test_stage2();
#endif
#if LEARN_STAGE >= 3
    test_stage3();
#endif
#if LEARN_STAGE >= 4
    test_stage4();
#endif
    printf("%s\n", failures ? "HOST TESTS FAILED" : "HOST TESTS PASSED");
    return failures ? 1 : 0;
}
