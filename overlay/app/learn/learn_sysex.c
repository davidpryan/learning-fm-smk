/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Learn SysEx: F0 7D 'L' 'N' 01 <device> <stage> <cmd> <payload...> F7
 *
 * 7D is the MIDI "educational / non-commercial" manufacturer ID, used here
 * exactly as the MIDI specification intends. The stock firmware's own SysEx
 * starts F0 7D 'F' 'M' 02, so the two never collide: learn_sysex() only
 * claims frames whose third and fourth bytes are 'L' 'N'.
 *
 * learn_sysex() runs on the routing task: it copies the frame into a ring
 * and requests control work. Everything else runs on the control task. */
#include "learn.h"
#include <string.h>

enum { RING = 8 };
static volatile uint8_t frames[RING][LSYX_MAX];
static volatile uint8_t lengths[RING];
static volatile unsigned ring_w, ring_r;
static uint32_t partner_at = 0, last_ping = 0;
static unsigned partner_ever;

static unsigned my_device(void)
{
#ifdef LEARN_BOARD_SMK37
    return LDEV_SMK;
#else
    return LDEV_FM1;
#endif
}

int learn_sysex(const uint8_t *m, size_t n)
{
    if (!m || n < 9 || n > LSYX_MAX || m[0] != 0xF0 || m[n - 1] != 0xF7) return 0;
    if (m[1] != 0x7D || m[2] != 'L' || m[3] != 'N' || m[4] != 1) return 0;
    /* Address check: a frame for the other device is ignored but still ours. */
    if (!(m[5] & my_device())) return 1;
    unsigned next = (ring_w + 1) % RING;
    if (next == ring_r) return 1;
    memcpy((void *)frames[ring_w], m, n);
    lengths[ring_w] = (uint8_t)n;
    ring_w = next;
    fm1_control_request();
    return 1;
}

int lsyx_send(unsigned cmd, const uint8_t *payload, unsigned bytes)
{
    uint8_t m[LSYX_MAX];
    if (bytes + 9 > LSYX_MAX) return -1;
    m[0] = 0xF0; m[1] = 0x7D; m[2] = 'L'; m[3] = 'N'; m[4] = 1;
    m[5] = (uint8_t)(LDEV_ANY & ~my_device()); /* to the other box */
    m[6] = LEARN_STAGE; m[7] = (uint8_t)cmd;
    for (unsigned i = 0; i < bytes; ++i) m[8 + i] = payload[i] & 0x7F;
    m[8 + bytes] = 0xF7;
    /* USB and BLE both; a port that is offline returns -1 and is skipped. */
    int r = -1;
    if (fm1_midi_ready_ports() & (1U << FM1_MIDI_USB)) r = fm1_midi_send_port(FM1_MIDI_USB, m, 9 + bytes);
    if (fm1_midi_ready_ports() & (1U << FM1_MIDI_BLE)) { int b = fm1_midi_send_port(FM1_MIDI_BLE, m, 9 + bytes); if (r) r = b; }
    return r;
}

int lsyx_pull(uint8_t *cmd, uint8_t *payload, unsigned *bytes)
{
    if (ring_r == ring_w) return 0;
    unsigned n = lengths[ring_r];
    const volatile uint8_t *f = frames[ring_r];
    *cmd = f[7];
    *bytes = n - 9;
    for (unsigned i = 0; i < *bytes; ++i) payload[i] = f[8 + i];
    ring_r = (ring_r + 1) % RING;
    partner_at = fm1_millis(); partner_ever = 1;
    if (*cmd == LCMD_PING) { uint8_t p = 0; lsyx_send(LCMD_PONG, &p, 1); return lsyx_pull(cmd, payload, bytes); }
    if (*cmd == LCMD_PONG) return lsyx_pull(cmd, payload, bytes);
    return 1;
}

unsigned lsyx_partner_seen(void) { return partner_ever ? fm1_millis() - partner_at : 0xFFFFFFFFu; }
unsigned lsyx_partner_present(void) { return partner_ever && fm1_millis() - partner_at < 6000U; }
void lsyx_poll(uint32_t now)
{
    if (now - last_ping >= 2500U) { uint8_t p = 0; lsyx_send(LCMD_PING, &p, 1); last_ping = now; }
}
