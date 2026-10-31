/* SPDX-License-Identifier: GPL-3.0-only
 * The learn layer's SysEx vocabulary, both directions:
 *   F0 7D 'L' 'N' 01 <device: 1 SMK, 2 FM-1, 3 any> <stage> <cmd> <payload, 7-bit> F7
 * Out: ota_wire_send (usb.c, USB-MIDI; returns at once when no host is connected).
 * In: editor.c ed_service() hands us the frame body (without F0 / F7) on the main loop: learn_sysex().
 * 0x7D is the MIDI manufacturer id reserved for non-commercial use. */

static uint32_t lsyx_partner_ms;                  /* the partner box last spoke (ms | 1), 0 never */
static uint32_t lsyx_ping_ms;
#if FELUCCA_OTA
static uint32_t lsyx_fail_until;                  /* a send failed (host not draining): no sends until */
#endif
static uint8_t lsyx_in[LSYX_MAX + 4];             /* one incoming command, parsed, for the stages */
static uint8_t lsyx_in_cmd, lsyx_in_n, lsyx_in_ready;

static int lsyx_send(unsigned cmd, const uint8_t *payload, unsigned bytes)
{
#if FELUCCA_OTA
    uint8_t f[LSYX_MAX + 10];
    unsigned i, n = 0;
    if (bytes > LSYX_MAX)
        return -1;
    if (!*(volatile uint8_t *)&usb.config)
        return -1;                                /* no USB host: nothing to send to */
    if ((int32_t)(fm1_ms - lsyx_fail_until) < 0)
        return -1;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 'L'; f[n++] = 'N'; f[n++] = 0x01;
    f[n++] = LEARN_DEVICE; f[n++] = LEARN_STAGE; f[n++] = (uint8_t)(cmd & 0x7Fu);
    for (i = 0; i < bytes; i++)
        f[n++] = (uint8_t)(payload[i] & 0x7Fu);
    f[n++] = 0xF7;
    if (ota_wire_send(f, n) < 0) {
        lsyx_fail_until = fm1_ms + 1000u;         /* (the host is not reading: try again in a second) */
        return -1;
    }
    return 0;
#else
    (void)cmd; (void)payload; (void)bytes;
    return -1;
#endif
}

static unsigned lsyx_partner_present(void) { return lsyx_partner_ms && fm1_ms - (lsyx_partner_ms & ~1u) < 4000u; }

static void lsyx_poll(uint32_t now)
{
    if (now - lsyx_ping_ms >= 1500u) {
        lsyx_ping_ms = now;
        lsyx_send(LCMD_PING, 0, 0);
    }
}

/* a frame from the USB host (main loop; the stages see it on the next learn_input) */
static void learn_sysex(const uint8_t *p, uint32_t n)
{
    unsigned dev, cmd, len;
    if (n < 7u || p[0] != 0x7Du || p[1] != 'L' || p[2] != 'N' || p[3] != 0x01u)
        return;
    dev = p[4];
    cmd = p[6];
    if (dev != LDEV_ANY && dev != LEARN_DEVICE)
        return;                                   /* for the other box */
    lsyx_partner_ms = fm1_ms | 1u;
    if (cmd == LCMD_PING) {
        lsyx_send(LCMD_PONG, 0, 0);
        return;
    }
    if (cmd == LCMD_PONG)
        return;
    len = n - 7u;
    if (len > LSYX_MAX)
        len = LSYX_MAX;
    if (lsyx_in_ready)
        return;                                   /* one at a time; a second before the first is read is dropped */
    memcpy(lsyx_in, p + 7, len);
    lsyx_in_cmd = (uint8_t)cmd;
    lsyx_in_n = (uint8_t)len;
    lsyx_in_ready = 1;
}

static int lsyx_pull(uint8_t *cmd, const uint8_t **payload, unsigned *bytes)
{
    if (!lsyx_in_ready)
        return 0;
    *cmd = lsyx_in_cmd;
    *payload = lsyx_in;
    *bytes = lsyx_in_n;
    lsyx_in_ready = 0;
    return 1;
}
