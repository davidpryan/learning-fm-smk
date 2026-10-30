/* SPDX-License-Identifier: GPL-3.0-only
 * Drawing for the learn pages on SLOOP's canvas (gfx.c). The 240x240 screen is drawn as two 240x120 canvases,
 * exactly as DAW mode does; a stage's draw function is called once per half with ld_y0 = 0 then 120 and draws in
 * screen coordinates. Everything clips to the current half, so text and boxes may straddle the middle. */

static int ld_y0;                                 /* the screen row of the current canvas' top */

static void ld_fill(int x, int y, int w, int h, uint16_t c) { cv_rect(x, y - ld_y0, w, h, c); }
static void ld_frame(int x, int y, int w, int h, uint16_t c)
{
    ld_fill(x, y, w, 1, c);
    ld_fill(x, y + h - 1, w, 1, c);
    ld_fill(x, y, 1, h, c);
    ld_fill(x + w - 1, y, 1, h, c);
}
static void ld_text(int x, int y, const char *s, unsigned big, uint16_t c)
{
    int yy = y - ld_y0, h = big ? 32 : 16;
    if (yy >= 120 || yy + h <= 0)
        return;                                   /* wholly in the other half: nothing to do */
    cv_text(x, yy, big ? &FONT_L : &FONT_S, s, c);
}
static int ld_text_w(const char *s, unsigned big) { return (int)text_w(big ? &FONT_L : &FONT_S, s); }
static void ld_text_c(int y, const char *s, unsigned big, uint16_t c)
{
    int w = ld_text_w(s, big);
    ld_text((LD_W - w) / 2, y, s, big, c);
}
static void ld_dot(int x, int y, int r, uint16_t c)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = r - (dy < 0 ? -dy : dy);
        ld_fill(x - dx, y + dy, 2 * dx + 1, 1, c);
    }
}

/* a strip showing `keys` keys from `first_note`: white keys as boxes, black keys as short dark boxes above them;
 * state[k] 1 = lit (cyan), 2 = bright (white: follow-me) */
static void ld_keymap(int y, int h, unsigned keys, unsigned first_note, const uint8_t *state)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    unsigned k, whites = 0, wi = 0;
    int ww, x0;
    for (k = 0; k < keys; k++)
        if (!BLACK[(first_note + k) % 12u])
            whites++;
    if (!whites)
        return;
    ww = LD_W / (int)whites;
    x0 = (LD_W - ww * (int)whites) / 2;
    ld_fill(0, y, LD_W, h, LC_BG);
    for (k = 0; k < keys; k++) {                  /* white keys first */
        unsigned n = first_note + k;
        if (BLACK[n % 12u])
            continue;
        {
            int x = x0 + (int)wi * ww;
            uint16_t c = state[k] == 2u ? LC_TEXT : state[k] ? LC_CYAN : LC_PANEL;
            ld_fill(x + 1, y, ww - 2, h, c);
            if (n % 12u == 0u)                    /* a C: a dot at the bottom, the player's landmark */
                ld_fill(x + ww / 2 - 1, y + h - 4, 3, 2, c == LC_PANEL ? LC_DIM : LC_BG);
            wi++;
        }
    }
    wi = 0;
    for (k = 0; k < keys; k++) {                  /* then the black keys, over the gap to the right of each white */
        unsigned n = first_note + k;
        if (!BLACK[n % 12u]) {
            wi++;
            continue;
        }
        {
            int x = x0 + (int)wi * ww - ww / 3, bw = (2 * ww) / 3;
            uint16_t c = state[k] == 2u ? LC_TEXT : state[k] ? LC_CYAN : LC_BG;
            if (bw < 3) bw = 3;
            ld_fill(x, y, bw, (h * 3) / 5, c);
            if (c == LC_BG)
                ld_frame(x, y, bw, (h * 3) / 5, LC_DIM);
        }
    }
}

/* a line of text on a filled bar across the screen (a prompt that cannot be missed) */
static void ld_bar_text(int y, const char *s, uint16_t bar, uint16_t fg)
{
    ld_fill(0, y, LD_W, 18, bar);
    ld_text(4, y + 1, s, 0, fg);
}

/* a small treble staff with one note (middle C = 60 on a ledger line below) */
static void ld_staff(int x, int y, unsigned note)
{
    static const int8_t STEP[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};   /* diatonic step of a pitch class */
    static const uint8_t SHARP[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    int i, oct = (int)note / 12 - 1, step = (oct - 4) * 7 + STEP[note % 12u];   /* 0 = C4 */
    int ny = y + 24 - (step - 2) * 3;             /* E4 (step 2) sits on the bottom line, 3 px a step */
    ld_fill(x, y - 10, 66, 48, LC_BG);
    for (i = 0; i < 5; i++)
        ld_fill(x, y + 24 - i * 6, 66, 1, LC_DIM);
    if (step <= 0)                                /* ledger lines below (C4 and lower) */
        for (i = 0; i >= step; i -= 2)
            ld_fill(x + 24, y + 24 - (i - 2) * 3, 20, 1, LC_DIM);
    if (step >= 12)                               /* ledger lines above (A5 and higher) */
        for (i = 12; i <= step; i += 2)
            ld_fill(x + 24, y + 24 - (i - 2) * 3, 20, 1, LC_DIM);
    ld_dot(x + 34, ny, 3, LC_TEXT);
    if (SHARP[note % 12u])
        ld_text(x + 44, ny - 8, "#", 0, LC_TEXT);
}

/* string helpers: append into d (cap bytes including the terminator), return the new length */
static unsigned ls_cat(char *d, unsigned n, unsigned cap, const char *s)
{
    while (*s && n + 1u < cap)
        d[n++] = *s++;
    d[n] = 0;
    return n;
}
static unsigned ls_num(char *d, unsigned n, unsigned cap, unsigned v)
{
    char b[11];
    unsigned i = 0, j;
    do {
        b[i++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && i < 10u);
    for (j = i; j > 0; j--)
        if (n + 1u < cap)
            d[n++] = b[j - 1];
    d[n] = 0;
    return n;
}
static int ls_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
