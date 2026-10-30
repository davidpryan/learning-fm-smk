/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Host-only additions to the vendor browser simulator. Textually includes
 * web/engine/Simulator.c so the helpers below can reach its static state. */
#include "Simulator.c"

/* Advance simulated time by rendering audio blocks (the control task polls every 10 ms). */
void sim_advance(unsigned ms)
{
    uint32_t until = now_ms + ms;
    while ((int32_t)(now_ms - until) < 0) (void)sim_render();
}
uint32_t sim_pixel(unsigned x, unsigned y) { return x < 240 && y < 240 ? pixels[y * 240 + x] : 0; }
/* Pop one captured outgoing MIDI message: returns byte count, fills out (<=128). */
unsigned sim_midi_pop(uint8_t *out, unsigned *port)
{
    if (capture_read == capture_write) return 0;
    unsigned at = capture_read++ & 63U, n = midi_capture[at].bytes;
    memcpy(out, midi_capture[at].data, n);
    *port = midi_capture[at].port;
    return n;
}
#ifdef LEARN_BOARD_SMK37
uint32_t smk37_pad_color(unsigned pad, unsigned lit);
unsigned sim_pad_rgb(unsigned pad) { return smk37_pad_color(pad, 0); }
#else
unsigned sim_pad_rgb(unsigned pad) { (void)pad; return 0; }
#endif
