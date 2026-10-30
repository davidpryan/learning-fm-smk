/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Immediate-mode drawing for the learn pages. Text is composed in a 16-row
 * band buffer and transferred with fm1_screen_blit; rectangles use
 * fm1_screen_fill. Stages redraw only what changed (they call draw_* from
 * their *_draw function when a value differs from the last drawn one).
 * Glyph tables are the stock FM1 Open 5x7 font (same project, same licence). */
#include "learn.h"
#include <string.h>

enum { BAND_ROWS = 4 };
static uint16_t band[FM1_SCREEN_WIDTH * BAND_ROWS];

static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punctuation[32][5] = {
    {0,0,0x5f,0,0}, {0,7,0,7,0}, {0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12}, {0x63,0x13,8,0x64,0x63},
    {0x36,0x49,0x55,0x22,0x50}, {0,5,3,0,0}, {0,0x1c,0x22,0x41,0},
    {0,0x41,0x22,0x1c,0}, {0x14,8,0x3e,8,0x14}, {8,8,0x3e,8,8},
    {0,0x50,0x30,0,0}, {8,8,8,8,8}, {0,0x60,0x60,0,0}, {0x60,0x18,6,1,0},
    {0,0x36,0x36,0,0}, {0,0x56,0x36,0,0}, {8,0x14,0x22,0x41,0},
    {0x14,0x14,0x14,0x14,0x14}, {0x41,0x22,0x14,8,0}, {2,1,0x51,9,6},
    {0x32,0x49,0x79,0x41,0x3e}, {0,0x7f,0x41,0x41,0},
    {1,6,0x18,0x60,0}, {0,0x41,0x41,0x7f,0}, {4,2,1,2,4},
    {0x40,0x40,0x40,0x40,0x40}, {0,1,2,4,0}, {0,8,0x36,0x41,0},
    {0,0,0x7f,0,0}, {0,0x41,0x36,8,0}, {8,4,8,0x10,8}
};
/* Lowercase letters render as small capitals; '°' and 'ø' fall back. */
static const uint8_t *glyph(unsigned char c)
{
    static const uint8_t blank[5] = {0};
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return font[c - '0'];
    if (c >= 'A' && c <= 'Z') return font[10 + c - 'A'];
    if (c >= '!' && c <= '~') return punctuation[c - (c < ':' ? 33U : c < '[' ? 43U : c < '{' ? 69U : 95U)];
    if (c == 0xB0) return punctuation['*' - 33]; /* degree sign -> * */
    return blank;
}

void draw_begin(void) {}
void draw_end(void) {}
void draw_clear(void) { fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, C_BG); }

void draw_fill(int x, int y, int w, int h, uint16_t rgb565)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)FM1_SCREEN_WIDTH || y >= (int)FM1_SCREEN_HEIGHT || w <= 0 || h <= 0) return;
    if (x + w > (int)FM1_SCREEN_WIDTH) w = (int)FM1_SCREEN_WIDTH - x;
    if (y + h > (int)FM1_SCREEN_HEIGHT) h = (int)FM1_SCREEN_HEIGHT - y;
    fm1_screen_fill((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, rgb565);
}
void draw_hline(int x, int y, int w, uint16_t rgb565) { draw_fill(x, y, w, 1, rgb565); }
void draw_frame(int x, int y, int w, int h, uint16_t rgb565)
{
    draw_fill(x, y, w, 1, rgb565); draw_fill(x, y + h - 1, w, 1, rgb565);
    draw_fill(x, y, 1, h, rgb565); draw_fill(x + w - 1, y, 1, h, rgb565);
}
void draw_dot(int x, int y, int r, uint16_t rgb565)
{
    for (int dy = -r; dy <= r; ++dy) {
        int span = 0;
        while ((span + 1) * (span + 1) + dy * dy <= r * r) ++span;
        draw_fill(x - span, y + dy, 2 * span + 1, 1, rgb565);
    }
}

int draw_text_width(const char *s, unsigned scale)
{
    unsigned n = 0;
    while (s && s[n]) ++n;
    return (int)(n * 6U * scale);
}

void draw_text(int x, int y, const char *s, unsigned scale, uint16_t fg, uint16_t bg)
{
    if (!s || !scale || scale > 4 || y >= (int)FM1_SCREEN_HEIGHT || x >= (int)FM1_SCREEN_WIDTH) return;
    unsigned cw = 6U * scale, ch = 7U * scale;
    unsigned n = (unsigned)strlen(s);
    if (x < 0) { unsigned skip = ((unsigned)(-x) + cw - 1) / cw; if (skip >= n) return; s += skip; n -= skip; x += (int)(skip * cw); }
    unsigned avail = ((unsigned)FM1_SCREEN_WIDTH - (unsigned)x) / cw;
    if (n > avail) n = avail;
    if (!n) return;
    unsigned w = n * cw;
    /* Compose and transfer in slabs of up to BAND_ROWS rows. */
    for (unsigned row0 = 0; row0 < ch; row0 += BAND_ROWS) {
        unsigned rows = ch - row0 < BAND_ROWS ? ch - row0 : BAND_ROWS;
        int sy = y + (int)row0;
        if (sy >= (int)FM1_SCREEN_HEIGHT) break;
        if (sy + (int)rows > (int)FM1_SCREEN_HEIGHT) rows = (unsigned)((int)FM1_SCREEN_HEIGHT - sy);
        for (unsigned r = 0; r < rows; ++r) {
            unsigned grow = (row0 + r) / scale, mask = 1U << grow;
            uint16_t *p = band + r * w;
            for (unsigned i = 0; i < n; ++i) {
                const uint8_t *g = glyph((unsigned char)s[i]);
                for (unsigned col = 0; col < cw; ++col) {
                    unsigned gc = col / scale;
                    *p++ = gc < 5 && (g[gc] & mask) ? fg : bg;
                }
            }
        }
        fm1_screen_blit((unsigned)x, (unsigned)sy, w, rows, band, (size_t)w * rows);
    }
}

void draw_text_centered(int y, const char *s, unsigned scale, uint16_t fg, uint16_t bg)
{
    int w = draw_text_width(s, scale);
    int x = ((int)FM1_SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;
    draw_text(x, y, s, scale, fg, bg);
}

/* Keyboard strip: white keys as light bars, black keys as short dark bars on top. */
void draw_keymap(int y, int h, unsigned keys, unsigned first_note, uint64_t lit, uint64_t bright)
{
    static const uint8_t black[12] = {0,1,0,1,0,0,1,0,1,0,1,0};
    unsigned whites = 0;
    for (unsigned k = 0; k < keys; ++k) if (!black[(first_note + k) % 12]) ++whites;
    if (!whites) return;
    unsigned ww = (unsigned)FM1_SCREEN_WIDTH / whites, x = (FM1_SCREEN_WIDTH - ww * whites) / 2, wx = x;
    draw_fill(0, y, FM1_SCREEN_WIDTH, h, C_BG);
    /* White keys */
    for (unsigned k = 0; k < keys; ++k) {
        if (black[(first_note + k) % 12]) continue;
        uint16_t c = (bright >> k) & 1 ? C_GREEN : (lit >> k) & 1 ? C_CYAN : 0xC618;
        draw_fill((int)wx + 1, y, (int)ww - 2, h, c);
        wx += ww;
    }
    /* Black keys, centred on the boundary between white keys. */
    wx = x;
    for (unsigned k = 0; k < keys; ++k) {
        unsigned pc = (first_note + k) % 12;
        if (!black[pc]) { wx += ww; continue; }
        uint16_t c = (bright >> k) & 1 ? C_GREEN : (lit >> k) & 1 ? C_CYAN : 0x2104;
        int bw = (int)ww * 2 / 3; if (bw < 3) bw = 3;
        draw_fill((int)wx - bw / 2, y, bw, h * 3 / 5, c);
    }
}

/* Treble staff: lines E4 G4 B4 D5 F5. Notes below E4 use the bass staff (G2 B2 D3 F3 A3). */
void draw_staff(int x, int y, unsigned note)
{
    static const int8_t major_pos[12] = {0,0,1,1,2,3,3,4,4,5,5,6}; /* pitch class -> letter index C=0 */
    int treble = note >= 64;
    unsigned ref = treble ? 64 : 43;            /* E4 or G2 sits on the bottom line */
    int letter = (int)(note / 12) * 7 + major_pos[note % 12];
    int ref_letter = (int)(ref / 12) * 7 + major_pos[ref % 12];
    int steps = letter - ref_letter;            /* 0 = bottom line, +1 = space above */
    draw_fill(x, y - 10, 60, 40, C_BG);
    for (int l = 0; l < 5; ++l) draw_hline(x, y + 16 - l * 4, 60, C_DIM);
    draw_text(x + 2, y - 8, treble ? "TREBLE" : "BASS", 1, C_DIM, C_BG);
    int ny = y + 16 - steps * 2;
    if (ny < y - 4 || ny > y + 24) return;
    /* Ledger lines for notes outside the staff. */
    if (steps < 0) for (int s = -2; s >= steps; s -= 2) draw_hline(x + 30, y + 16 - s * 2, 14, C_DIM);
    if (steps > 8) for (int s = 10; s <= steps; s += 2) draw_hline(x + 30, y + 16 - s * 2, 14, C_DIM);
    draw_dot(x + 37, ny, 3, C_TEXT);
    static const uint8_t sharp[12] = {0,1,0,1,0,0,1,0,1,0,1,0};
    if (sharp[note % 12]) draw_text(x + 44, ny - 3, "#", 1, C_TEXT, C_BG);
}
