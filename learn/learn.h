/* SPDX-License-Identifier: GPL-3.0-only
 * Learn layer for SLOOP (FM-1 / SMK-37 Elite): a beginner's teaching mode.
 *
 * learn.c is #included into SLOOP's single translation unit (firmware/src/felucca.c) after ui_daw.c when the
 * firmware is built with FELUCCA_LEARN=1. LEARN mode works like DAW mode: while it is on, the keys, pads, buttons,
 * knobs, faders, screen and lights belong to the learn layer; the stock SLOOP UI is still in the firmware and comes
 * back with GLO + HOME held for two seconds (or HOME menu > LEARN OFF is not needed: the same hold toggles).
 *
 * What keeps the device safe (see docs/SAFETY.md):
 *  - the learn layer never writes Flash: the practice log, takes and sections are RAM and go out over SysEx;
 *  - nothing here runs in the audio ISR except two small routines that copy events into rings
 *    (learn_keys_block / learn_ext_event / learn_midi_in) and one that drains the sound ring (learn_keys_block);
 *  - no malloc, no blocking, no recursion; every buffer is fixed and bounded.
 *
 * Context rules inside this layer:
 *  ISR side   (seq.c, audio ISR): learn_follow, learn_keys_block, learn_ext_event, learn_midi_in.
 *  main side  (ui_input.c / ui_draw.c): everything else. Stage code runs on the main side only.
 */
#ifndef LEARN_H
#define LEARN_H

#ifndef LEARN_STAGE
#define LEARN_STAGE 4            /* the pages built in: all four (a lower number: a cut-down development build) */
#endif
#if LEARN_STAGE < 1 || LEARN_STAGE > 4
#error "LEARN_STAGE must be 1..4"
#endif
#define LEARN_VERSION "2.0"
#include "pre.h"

/* ---- board ---- */
#if BOARD_SMK37
#define LEARN_BOARD_SMK37 1
#define LEARN_KEYS 37u
#define LEARN_PADS 16u
#define LEARN_FIRST_NOTE 48u   /* keybed key 0 = C3 at octave 0 */
#define LEARN_DEVICE LDEV_SMK
#else
#define LEARN_BOARD_FM1 1
#define LEARN_KEYS 27u
#define LEARN_PADS 0u
#define LEARN_FIRST_NOTE 53u   /* key 0 = F3 */
#define LEARN_DEVICE LDEV_FM1
#endif

/* What the buttons and knobs are called on the panel in the player's hands (the screen's words):
 * the SMK-37 prints other names than SLOOP's labels, and its K1 is the volume, so SLOOP's seven function knobs
 * are its K2..K8. */
#if BOARD_SMK37
#define LBL_PLAY "SEQ PLAY"
#define LBL_REC "SEQ REC"
#define LBL_HOME "PATCH"
#define LBL_GLO "GLOBE"
#define LBL_SCL "SCALE"
#define LBL_ENV "CHORD"
#define LBL_LFO "NOTE RPT"
#define LBL_EDIT "PARA"
#define LBL_SAVE "BT"
#define LBL_K(n) ("K" #n)        /* (n: the printed knob; see LKN for the layer's knob numbers) */
static const char *const LKN[7] = {"K2", "K3", "K4", "K5", "K6", "K7", "K8"};   /* the layer's knob 1..7 as printed */
#else
#define LBL_PLAY "PLAY"
#define LBL_REC "REC"
#define LBL_HOME "HOME"
#define LBL_GLO "GLO"
#define LBL_SCL "SCL"
#define LBL_ENV "ENV"
#define LBL_LFO "LFO"
#define LBL_EDIT "EDIT"
#define LBL_SAVE "SAVE"
static const char *const LKN[7] = {"K1", "K2", "K3", "K4", "K5", "K6", "K7"};
#endif

/* Logical buttons: SLOOP's panel labels (panel.c B_*), both boards. */
enum { LB_FX = B_FX, LB_SCL = B_SCL, LB_ENV = B_ENV, LB_LFO = B_LFO, LB_EDIT = B_EDIT, LB_GLO = B_GLO,
       LB_HOME = B_HOME, LB_SAVE = B_SAVE, LB_ARP = B_ARP, LB_SEQ = B_SEQ, LB_PLAY = B_PLAY, LB_REC = B_REC,
       LB_OCT_DOWN = B_OCTDN, LB_OCT_UP = B_OCTUP, LB_COUNT = NB };
#define LB(b) (1u << (b))

/* ---- colours: RGB565 for the screen, 0xRRGGBB for the pads ---- */
#define LC_BG     0x0000
#define LC_TEXT   0xFFFF
#define LC_DIM    0x9CF3          /* secondary text (readable on the small LCD; never darker for text) */
#define LC_GREY   0xC618          /* the header and labels */
#define LC_GREEN  0x07E0
#define LC_ORANGE 0xFD20
#define LC_CYAN   0x07FF
#define LC_BLUE   0x3D9F
#define LC_RED    0xF800
#define LC_YELLOW 0xFFE0
#define LC_PURPLE 0xA81F
#define LC_PANEL  0x1082
#define LC_AMBER  0xFEA0
/* Pad colours (0xRRGGBB) at the brightness SLOOP's own pad palette uses; P_DIM a quarter, P_FULL saturated. */
#define P_OFF    0x000000
#define P_GREEN  0x1e9a50
#define P_ORANGE 0xa04010
#define P_CYAN   0x1a8c90
#define P_BLUE   0x2060c0
#define P_RED    0xb01818
#define P_YELLOW 0xa08010
#define P_WHITE  0x606060
#define P_PURPLE 0x6a20a0
#define P_DIM(c) ((((c) >> 2) & 0x3f3f3f))
static uint32_t P_FULL(uint32_t c)
{
    uint32_t r = ((c >> 16) & 255u) * 2u, g = ((c >> 8) & 255u) * 2u, b = (c & 255u) * 2u;
    return (r > 255u ? 255u : r) << 16 | (g > 255u ? 255u : g) << 8 | (b > 255u ? 255u : b);
}

/* ---- music theory (theory.c) ---- */
enum { SCALE_MAJOR, SCALE_MINOR, SCALE_PENT_MAJ, SCALE_PENT_MIN, SCALE_BLUES, SCALE_COUNT };
enum { CHORD_MAJ, CHORD_MIN, CHORD_DIM, CHORD_SUS2, CHORD_SUS4, CHORD_MAJ7, CHORD_MIN7, CHORD_DOM7, CHORD_NONE };
static const char *theory_note_name(unsigned note, char out[4]);
static const char *theory_scale_name(unsigned scale);
static unsigned theory_scale_mask(unsigned scale);
static int theory_in_scale(unsigned root, unsigned scale, unsigned note);
static unsigned theory_nudge(unsigned root, unsigned scale, unsigned note);
static int theory_degree(unsigned root, unsigned scale, unsigned note);
static unsigned theory_degree_chord(unsigned root, unsigned minor, unsigned degree, unsigned seventh,
                                    unsigned octave_note, uint8_t notes[4]);
static const char *theory_roman(unsigned minor, unsigned degree, unsigned seventh);
static unsigned theory_degree_colour(unsigned minor, unsigned degree);
static unsigned theory_identify(const uint8_t *notes, unsigned count, unsigned *root_pc);
static const char *theory_chord_suffix(unsigned kind);
static const char *theory_interval_name(int semitones);
static const char *theory_why_hint(unsigned minor, unsigned degree);

/* ---- drawing (draw.c): a 240x240 screen drawn as two 240x120 canvases (gfx.c cv_*) ---- */
#define LD_W 240
#define LD_H 240
static void ld_fill(int x, int y, int w, int h, uint16_t c);
static void ld_frame(int x, int y, int w, int h, uint16_t c);
static void ld_text(int x, int y, const char *s, unsigned big, uint16_t c);   /* big: FONT_L (32 px) else FONT_S (16 px) */
static int ld_text_w(const char *s, unsigned big);
static void ld_text_c(int y, const char *s, unsigned big, uint16_t c);
static void ld_keymap(int y, int h, unsigned keys, unsigned first_note, const uint8_t *state);   /* state[k]: 0 off, 1 lit, 2 bright */
static void ld_bar_text(int y, const char *s, uint16_t bar, uint16_t fg);   /* a line of text on a filled bar */
static void ld_staff(int x, int y, unsigned note);
static void ld_dot(int x, int y, int r, uint16_t c);
/* small string helpers (no printf on the device) */
static unsigned ls_cat(char *d, unsigned n, unsigned cap, const char *s);
static unsigned ls_num(char *d, unsigned n, unsigned cap, unsigned v);
static int ls_eq(const char *a, const char *b);

/* ---- sound and MIDI (midi.c): requests to the audio ISR, MIDI out, notes from outside ---- */
enum { LT_KEYS = 0, LT_CHORDS = 1, LT_BASS = 2, LT_DRUMS = 3 };   /* SLOOP tracks 1..3 and the drum track */
static void lmidi_note(unsigned track, unsigned note, unsigned velocity);
static void lmidi_drum(unsigned lane, unsigned velocity);
static void lmidi_all_off(void);
static void lmidi_chord_on(unsigned track, const uint8_t *notes, unsigned count, unsigned velocity, unsigned slot);
static void lmidi_chord_off(unsigned slot);
static int lmidi_pull_external(uint8_t *status, uint8_t *data1, uint8_t *data2);
static uint32_t lmidi_held_external(void);
static void learn_bank_select(unsigned track, unsigned bank, unsigned patch);
static const char *learn_bank_name(unsigned bank);
static unsigned learn_bank_count(void);
static unsigned learn_patch_count(unsigned bank);

/* ---- SysEx vocabulary (sysex.c): F0 7D 'L' 'N' 01 <device> <stage> <cmd> payload F7 ---- */
enum { LDEV_SMK = 1, LDEV_FM1 = 2, LDEV_ANY = 3 };
enum {
    LCMD_KEY = 1, LCMD_PAD = 2, LCMD_TEXT = 3, LCMD_PATH = 4, LCMD_HINT = 5, LCMD_REPLAY = 6, LCMD_SLOW = 7,
    LCMD_LOG = 8, LCMD_DRILL = 9, LCMD_CHORD = 10, LCMD_GRID = 11, LCMD_REC = 12, LCMD_TAP = 13, LCMD_SCORE = 14,
    LCMD_UNLOCK = 15, LCMD_SHADOW = 16, LCMD_SOLO = 17, LCMD_TRANSPORT = 18, LCMD_TRACK = 19, LCMD_METER = 20,
    LCMD_QUEUE = 21, LCMD_TRANSITION = 22, LCMD_SECTION = 23, LCMD_SONGPOS = 24, LCMD_LESSON = 25, LCMD_PING = 26,
    LCMD_PONG = 27, LCMD_LOGDUMP = 28, LCMD_SETLESSON = 29
};
#define LSYX_MAX 64
static int lsyx_send(unsigned cmd, const uint8_t *payload, unsigned bytes);
static void lsyx_poll(uint32_t now);
static unsigned lsyx_partner_present(void);

/* ---- practice log (log.c), RAM only ---- */
enum { LOGK_ECHO, LOGK_CHORD, LOGK_PROGRESSION, LOGK_RHYTHM, LOGK_TAKE, LOGK_SONG, LOGK_LESSON };
struct log_entry { uint8_t kind, hits, misses, slow; uint16_t tempo; uint32_t at_ms; uint8_t lesson, score; };
static void log_init(void);
static void log_add(unsigned kind, unsigned hits, unsigned misses, unsigned tempo, unsigned slow, unsigned lesson, unsigned score);
static unsigned log_count(void);
static const struct log_entry *log_get(unsigned index_from_newest);
static unsigned log_streak(void);
static unsigned log_attempts(void);
static unsigned log_unlocked(unsigned feature);
static void log_unlock(unsigned feature);
enum { UNLOCK_SWING = 1, UNLOCK_FILL = 2, UNLOCK_STAGE2 = 4, UNLOCK_STAGE3 = 8, UNLOCK_STAGE4 = 16 };
static int log_dump(uint8_t *out, unsigned capacity);

/* ---- lesson packs (lessons.c, generated from lessons/*.json) ---- */
enum { DR_ECHO = 1, DR_CHORD = 2, DR_PROGRESSION = 3, DR_SCALE = 4, DR_RHYTHM = 5,
       DR_GROOVE = 6, DR_REFTAKE = 7, DR_SONG = 8, DR_TEXT = 9 };
struct lesson_step {
    uint8_t type, count, tempo, target;
    uint8_t root, scale, minor, seventh;
    uint8_t data[8];
    const char *prompt;                       /* <= 30 chars */
};
struct lesson {
    const char *title;                        /* <= 20 chars */
    uint8_t stage, number, step_count, unlocks;
    const struct lesson_step *steps;
};

/* ---- the stage context (main side) ---- */
struct lctx {
    uint32_t buttons;             /* fm1_in.buttons as logical LB bits this poll */
    uint32_t btn_down, btn_up;    /* logical LB press / release edges this poll */
    uint32_t keys_down;           /* 1 = a key or pad went down this poll (TEXT steps continue on it) */
    uint32_t now;                 /* fm1_ms */
    int octave;                   /* -2..+2 */
    unsigned root, scale, minor;  /* the key */
    unsigned slow_percent;        /* 50..100 */
    unsigned redraw;              /* the screen must be drawn again */
};
static void stage1_init(void);
static void stage1_control(struct lctx *x);
static void stage1_draw(struct lctx *x);
static void stage1_key(struct lctx *x, unsigned note, unsigned velocity);
static void stage1_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure);
static void stage1_encoder(struct lctx *x, unsigned id, int delta);
static void stage1_fader(struct lctx *x, unsigned id, unsigned value);
static void stage1_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n);
static uint32_t stage1_pad_colour(unsigned pad);
#if LEARN_STAGE >= 2
static void stage2_init(void); static void stage2_control(struct lctx *x); static void stage2_draw(struct lctx *x);
static void stage2_key(struct lctx *x, unsigned note, unsigned velocity);
static void stage2_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure);
static void stage2_encoder(struct lctx *x, unsigned id, int delta);
static void stage2_fader(struct lctx *x, unsigned id, unsigned value);
static void stage2_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n);
static uint32_t stage2_pad_colour(unsigned pad);
#endif
#if LEARN_STAGE >= 3
static void stage3_init(void); static void stage3_control(struct lctx *x); static void stage3_draw(struct lctx *x);
static void stage3_key(struct lctx *x, unsigned note, unsigned velocity);
static void stage3_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure);
static void stage3_encoder(struct lctx *x, unsigned id, int delta);
static void stage3_fader(struct lctx *x, unsigned id, unsigned value);
static void stage3_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n);
static uint32_t stage3_pad_colour(unsigned pad);
#endif
#if LEARN_STAGE >= 4
static void stage4_init(void); static void stage4_control(struct lctx *x); static void stage4_draw(struct lctx *x);
static void stage4_key(struct lctx *x, unsigned note, unsigned velocity);
static void stage4_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure);
static void stage4_encoder(struct lctx *x, unsigned id, int delta);
static void stage4_fader(struct lctx *x, unsigned id, unsigned value);
static void stage4_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n);
static uint32_t stage4_pad_colour(unsigned pad);
#endif

/* ---- shared services (main.c of the layer) ---- */
static unsigned learn_page(void);
static void learn_set_page(unsigned page);
static void learn_notice(const char *text, unsigned ms);
static const char *learn_notice_text(void);
static void lesson_start(unsigned index);
static void lesson_stop(void);
static unsigned lesson_running(void);
static const struct lesson_step *lesson_step(void);
static void lesson_step_done(unsigned passed, unsigned score);
static unsigned lesson_progress(void);
/* the key lights (FM-1: the LEDs under the 27 keys; both: the keymap strip): bit k = key k from F3 */
static uint32_t llit_keys, lbright_keys;
/* overlays shared by every page (main.c): the results log (GLO + ARP) and the page's help (GLO + SEQ) */
static uint8_t lov_log, lov_help, lov_welcome;   /* lov_help: 0 off, 1.. the help screen shown */
#define HELP_SCREENS 3u
/* the last drill's result, shown until the player plays again (page 1 fills it; main.c draws nothing of it) */
static struct { uint8_t valid, kind, passed; unsigned hits, misses, pct; } lresult;
/* the metronome / clock the stages share: SLOOP's tempo */
static unsigned learn_bpm(void);
static void learn_set_bpm(unsigned bpm);
#endif
