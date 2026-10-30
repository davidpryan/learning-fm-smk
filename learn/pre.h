/* SPDX-License-Identifier: GPL-3.0-only
 * The learn layer's entry points, declared before SLOOP's seq.c / ui_*.c / editor.c reference them
 * (felucca.c includes this right after it defaults FELUCCA_LEARN; learn.c, included after ui_daw.c, defines them). */
#ifndef LEARN_PRE_H
#define LEARN_PRE_H
#include <stdint.h>
static volatile uint8_t learn_on;                 /* LEARN mode is on (main side) */
static uint8_t learn_isr;                         /* the mode the keys last ran in (audio ISR side) */
/* audio ISR (seq.c) */
static void learn_follow(void);
static void learn_keys_block(void);
#if BOARD_SMK37
static void learn_ext_event(uint32_t k, uint32_t v);
#endif
static void learn_midi_in(uint32_t status, uint32_t d1, uint32_t d2);
/* main loop (ui_input.c, ui_draw.c, editor.c) */
static void learn_switch(uint32_t *pressed);
static void learn_input(uint32_t pressed, uint32_t now_ticks);
static void learn_leds(void);
static int learn_screen(void);
static void learn_sysex(const uint8_t *p, uint32_t n);
#endif
