/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * MIDI plumbing for the learn layer.
 *
 * Local notes go through groove_midi_local(): the engine plays them and,
 * with the stock MIDI preferences, they also leave on USB/BLE so the
 * partner box (or a DAW) hears them. External notes arrive on the routing
 * task through learn_midi_observe() and are drained here on the control
 * task, so a tutor screen can show what the other keyboard is playing. */
#include "learn.h"
#include <string.h>

/* Chord slots: notes the learn layer is holding, so releases always match. */
struct held { uint8_t count, channel; uint8_t notes[4]; };
static struct held chords[20];

void lmidi_note(unsigned channel, unsigned note, unsigned velocity)
{
    if (note > 127 || channel > 15) return;
    uint8_t m[3] = {(uint8_t)((velocity ? 0x90 : 0x80) | channel), (uint8_t)note, (uint8_t)(velocity & 127)};
    (void)groove_midi_local(m, 3);
}
void lmidi_cc(unsigned channel, unsigned cc, unsigned value)
{
    uint8_t m[3] = {(uint8_t)(0xB0 | (channel & 15)), (uint8_t)(cc & 127), (uint8_t)(value & 127)};
    (void)groove_midi_local(m, 3);
}
void lmidi_program(unsigned channel, unsigned program)
{
    uint8_t m[2] = {(uint8_t)(0xC0 | (channel & 15)), (uint8_t)(program & 127)};
    (void)groove_midi_local(m, 2);
}
void lmidi_chord_on(unsigned channel, const uint8_t *notes, unsigned count, unsigned velocity, unsigned slot)
{
    if (slot >= sizeof chords / sizeof chords[0]) return;
    lmidi_chord_off(slot);
    if (count > 4) count = 4;
    chords[slot].count = (uint8_t)count; chords[slot].channel = (uint8_t)channel;
    for (unsigned i = 0; i < count; ++i) { chords[slot].notes[i] = notes[i]; lmidi_note(channel, notes[i], velocity); }
}
void lmidi_chord_off(unsigned slot)
{
    if (slot >= sizeof chords / sizeof chords[0]) return;
    for (unsigned i = 0; i < chords[slot].count; ++i) lmidi_note(chords[slot].channel, chords[slot].notes[i], 0);
    chords[slot].count = 0;
}
void lmidi_all_off(void)
{
    for (unsigned s = 0; s < sizeof chords / sizeof chords[0]; ++s) lmidi_chord_off(s);
    for (unsigned ch = 0; ch < 16; ++ch) lmidi_cc(ch, 123, 0);
}

/* ---- external note ring (routing task -> control task) ---- */
enum { RING = 64 };
static volatile uint8_t ring[RING][3];
static volatile unsigned ring_w, ring_r;
static uint8_t ext_notes[16];
static unsigned ext_count;

void learn_midi_observe(unsigned source, const uint8_t *m, size_t n)
{
    (void)source;
    if (!m || n < 2 || n > 3) return;
    unsigned s = m[0] & 0xF0;
    if (s != 0x90 && s != 0x80 && s != 0xB0) return;
    unsigned next = (ring_w + 1) % RING;
    if (next == ring_r) return; /* full: drop, never block */
    ring[ring_w][0] = m[0]; ring[ring_w][1] = m[1]; ring[ring_w][2] = n > 2 ? m[2] : 0;
    ring_w = next;
    fm1_control_request();
}

int lmidi_pull_external(uint8_t *status, uint8_t *data1, uint8_t *data2)
{
    if (ring_r == ring_w) return 0;
    *status = ring[ring_r][0]; *data1 = ring[ring_r][1]; *data2 = ring[ring_r][2];
    ring_r = (ring_r + 1) % RING;
    unsigned s = *status & 0xF0;
    if (s == 0x90 && *data2) {
        unsigned found = 0;
        for (unsigned i = 0; i < ext_count; ++i) if (ext_notes[i] == *data1) found = 1;
        if (!found && ext_count < sizeof ext_notes) ext_notes[ext_count++] = *data1;
    } else if (s == 0x80 || (s == 0x90 && !*data2)) {
        for (unsigned i = 0; i < ext_count; ++i) if (ext_notes[i] == *data1) {
            ext_notes[i] = ext_notes[ext_count - 1]; --ext_count; break;
        }
    } else if (s == 0xB0 && (*data1 == 123 || *data1 == 120)) ext_count = 0;
    return 1;
}
uint32_t lmidi_held_external(void)
{
    uint32_t mask = 0;
    for (unsigned i = 0; i < ext_count; ++i) mask |= 1U << (ext_notes[i] % 12);
    return mask | (ext_count << 12);
}
void lmidi_external_note_list(uint8_t *notes, unsigned *count, unsigned max)
{
    unsigned n = ext_count < max ? ext_count : max;
    memcpy(notes, ext_notes, n);
    *count = n;
}
