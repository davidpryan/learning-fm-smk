/* SPDX-License-Identifier: GPL-3.0-only */
/* The learn layer on SLOOP's UI harness (sloop/tests/ui_pages_test.c, an FM-1 build): GLO + HOME held switches
 * LEARN mode on and off, the keys reach the synth through the ISR rings, the drill runs, lessons run, the screen
 * renders (DIR/learn-*.ppm), and SLOOP's own UI is untouched when the mode is off.
 * Build (from the repo root; the firmware's stage is the harness's too):
 *   cc -O1 -w -DFELUCCA_LEARN=1 -DLEARN_STAGE=1 -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
 *      -Isloop/tests -o build/host/learn_ui_test learn/tests/learn_ui_test.c -lm */
#define UI_PAGES_HARNESS_ONLY 1
#define LEARN_HOST 1
#include "../pre.h"
#include "ui_pages_test.c"
#include "../learn.c"

static void shot(const char *name) { ui.force = 1; frame(); ppm(name); }
static void hold_both(uint32_t ms)               /* GLO + HOME held for ms, then let go */
{
    uint32_t t0 = fm1_ms;
    edges_btn |= BT(B_GLO); fm1_in.buttons |= BT(B_GLO); frame();
    edges_btn |= BT(B_HOME); fm1_in.buttons |= BT(B_HOME); frame();
    while (fm1_ms - t0 < ms) frame();
    fm1_in.buttons &= ~(BT(B_GLO) | BT(B_HOME)); frame(); frame();
}
static unsigned notes_on(unsigned t)             /* the layer's notes sounding on track t (ISR view) */
{
    unsigned w, n = 0; uint32_t m;
    for (w = 0; w < 4u; w++) for (m = lsn_on[t][w]; m; m &= m - 1u) n++;
    return n;
}
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; frame(); }
static void key_up_(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    for (i = 0; i < NB; i++) panel.btn[i] = (uint8_t)i;
    for (i = 0; i < NE; i++) panel.enc[i] = (uint8_t)i, panel.dir[i] = 1;
    layers_init();
    host_tracks_init();
    go_home(); ui.force = 1; frame();

    /* ---- the switch ---- */
    check(!learn_on, "boots in SLOOP, LEARN off");
    hold_both(600);
    check(!learn_on, "GLO + HOME 0.6 s: nothing yet");
    song.playing = 1;
    hold_both(1700);
    check(learn_on, "GLO + HOME 1.7 s: LEARN mode");
    check(transport_req == 2 || !song.playing, "the sequencer was told to stop");
    check(!ui.menu, "no menu left open");
    song.playing = 0; transport_req = 0;
    frames(4);
    check(learn_isr == 1, "the ISR followed into LEARN");
    check(lov_welcome, "the welcome screen");
    shot("learn-welcome");
    tap(B_ENV);
    check(!lov_welcome && s1_hints, "a button dismisses it and does nothing else");
    shot("learn-s1-idle");

    /* ---- a key: C4 (key 7 = C4 in C major: in the scale) ---- */
    kdown(7);
    frames(2);
    check(notes_on(LT_KEYS) == 1, "a key down: one note on the KEYS track");
    check(s1_held_n == 1 && s1_held[0] == 60, "the layer sees C4");
    shot("learn-s1-c4");
    key_up_(7);
    frames(2);
    check(notes_on(LT_KEYS) == 0, "the key up: the note ends");

    /* ---- a key outside the scale is nudged (hints on): C#4 (key 8) -> D4 ---- */
    kdown(8); frames(2);
    check(s1_held_n == 1 && s1_held[0] != 61 && theory_in_scale(0, SCALE_MAJOR, s1_held[0]), "C# in C major: nudged into the scale");
    key_up_(8); frames(2);
    check(notes_on(LT_KEYS) == 0 && s1_held_n == 0, "its release ends the nudged note");

    /* ---- hints off: the key is muted ---- */
    tap(B_ENV);
    kdown(8); frames(2);
    check(notes_on(LT_KEYS) == 0 && s1_held_n == 0, "hints off: C# is muted");
    key_up_(8); frames(2);
    tap(B_ENV);

    /* ---- a chord: C E G ---- */
    kdown(7); kdown(11); kdown(14); frames(2);
    check(notes_on(LT_KEYS) == 3 && s1_held_n == 3, "three keys: three notes");
    shot("learn-s1-chord");
    key_up_(7); key_up_(11); key_up_(14); frames(2);
    check(notes_on(LT_KEYS) == 0, "all released");

    /* ---- the root: GLO + key ---- */
    fm1_in.buttons |= BT(B_GLO); frame();
    key(2);                                        /* G3 */
    fm1_in.buttons &= ~BT(B_GLO); frame();
    check(lx.root == 7, "GLO + G: the root is G");
    check(learn_on, "(GLO + a key did not leave the mode)");
    check(notes_on(LT_KEYS) == 0, "setting the root makes no sound");
    tap(B_SCL);
    check(lx.scale == SCALE_MINOR && lx.minor, "SCL: the next scale, minor");
    shot("learn-s1-gminor");

    /* ---- a drill: PLAY, then answer ---- */
    tap(B_PLAY);
    check(s1d.state == DS_PLAYING_REF && s1d.total == 10, "PLAY: an echo drill, the reference plays");
    frames(2);
    check(notes_on(LT_KEYS) == 1, "the reference note sounds");
    for (i = 0; i < 60 && s1d.state != DS_WAIT_ANSWER; i++) frame();
    check(s1d.state == DS_WAIT_ANSWER, "then your turn");
    frames(2);
    check(notes_on(LT_KEYS) == 0, "the reference stopped");
    shot("learn-s1-drill");
    {   /* answer with the right note: a hit */
        int k = (int)s1d.target[0] - 53;
        check(k >= 0 && k < 27, "the target is on the keys");
        key(k < 0 ? 0 : (uint32_t)k);
        for (i = 0; i < 10 && s1d.state == DS_WAIT_ANSWER; i++) frame();
        check(s1d.hits == 1 && s1d.misses == 0, "the right key: a hit");
    }
    for (i = 0; i < 80 && s1d.state != DS_WAIT_ANSWER; i++) frame();
    check(s1d.state == DS_WAIT_ANSWER && s1d.index == 1, "the second round");
    {   /* a wrong note: a miss */
        uint32_t wrong = (s1d.target[0] - 53u + 1u) % 27u;
        if (!theory_in_scale(lx.root, lx.scale, 53u + wrong)) wrong = (wrong + 1u) % 27u;
        key(wrong);
        for (i = 0; i < 10 && s1d.state == DS_WAIT_ANSWER; i++) frame();
        check(s1d.misses == 1, "a wrong key: a miss");
    }
    tap(B_HOME);
    check(s1d.state == DS_IDLE, "HOME: the drill stops");
    frames(2);
    check(notes_on(LT_KEYS) == 0 && notes_on(LT_CHORDS) == 0, "HOME: silence");

    /* ---- a lesson: ARP starts lesson 1 (two TEXT steps, then an echo drill) ---- */
    tap(B_ARP);
    check(lesson_running() == 1 && lesson_progress() == 0, "ARP: lesson 1 runs");
    check(lx.root == 0 && lx.scale == SCALE_MAJOR, "the lesson sets its key");
    shot("learn-s1-lesson");
    key(7); frames(2);
    check(lesson_progress() == 1, "a key: the TEXT step is read");
    key(7); frames(2);
    check(lesson_progress() == 2 && s1d.state != DS_IDLE && s1d.kind == DR_ECHO, "the second: the echo drill starts");
    tap(B_ARP);
    check(!lesson_running() && s1d.state == DS_IDLE, "ARP again: the lesson stops");

    /* ---- knobs ---- */
    encs[panel.enc[0]] = 1; frame();
    check(trk[LT_KEYS].eng_req == 1 && trk[LT_CHORDS].eng_req == 1, "knob 1: the next bank is the next engine, both tracks");
    encs[panel.enc[4]] = -1; frame();
    check(lx.slow_percent == 95, "knob 5: slow 95 %");
    encs[panel.enc[5]] = 1; frame();
    check(lesson_offset(lesson_pick(1)) == 1, "knob 6: the next lesson");

    /* ---- octave ---- */
    tap(B_OCTUP);
    check(lx.octave == 1, "OCT+: up an octave");
    kdown(7); frames(2);
    check(s1_held[0] == 72, "the key is C5 now");
    key_up_(7); frames(2);
    tap(B_OCTDN);

    /* ---- FM-1 chord keys ---- */
    fm1_in.buttons |= BT(B_GLO); frame(); tap(B_SCL); fm1_in.buttons &= ~BT(B_GLO); frame();
    check(s1_chord_mode, "GLO + SCL: CHORD KEYS");
    kdown(7); frames(2);
    check(notes_on(LT_CHORDS) == 3, "a key plays its triad on the CHORDS track");
    shot("learn-s1-chordkeys");
    key_up_(7); frames(2);
    check(notes_on(LT_CHORDS) == 0, "released");
    fm1_in.buttons |= BT(B_GLO); frame(); tap(B_SCL); fm1_in.buttons &= ~BT(B_GLO); frame();

    /* ---- MIDI in from the other box shows and counts ---- */
    midi_in_q[mi_w++ % MQ] = 0x09u | 0x90u << 8 | 64u << 16 | 100u << 24; frames(2);
    check((lmidi_held_external() >> 4) & 1u, "a note from USB MIDI: shown as held (E)");
    midi_in_q[mi_w++ % MQ] = 0x08u | 0x80u << 8 | 64u << 16; frames(2);
    check(lmidi_held_external() == 0, "its release");

    /* ---- the SysEx frame parser ---- */
    {
        uint8_t f[] = {0x7D, 'L', 'N', 1, LDEV_ANY, 1, LCMD_SETLESSON, 3};
        learn_sysex(f, sizeof f); frame();
        check(lesson_running() == 3, "SysEx SETLESSON 3: lesson 3 runs");
        f[7] = 0; learn_sysex(f, sizeof f); frame();
        check(!lesson_running(), "SETLESSON 0: stopped");
        f[4] = LDEV_SMK; f[7] = 2; learn_sysex(f, sizeof f); frame();
        check(!lesson_running(), "a frame for the SMK: ignored on the FM-1");
    }

    /* ---- a whole drill, answered right: the result card stays; the log and help overlays ---- */
    tap(B_HOME); frames(2);
    lx.root = 0; lx.scale = SCALE_MAJOR; lx.minor = 0;
    tap(B_PLAY);
    for (i = 0; i < 4000 && s1d.state != DS_IDLE; i++) {
        if (s1d.state == DS_WAIT_ANSWER && !s1d.answer_count) { int k = (int)s1d.target[0] - 53; if (k >= 0 && k < 27) key((uint32_t)k); }
        frame();
    }
    check(s1d.state == DS_IDLE && lresult.valid && lresult.pct == 100 && lresult.passed, "ten right answers: the result card, 100 %, passed");
    shot("learn-s1-result");
    check(log_count() >= 1 && log_get(0)->score == 100, "the log has it");
    fm1_in.buttons |= BT(B_GLO); frame(); tap(B_ARP); fm1_in.buttons &= ~BT(B_GLO); frame();
    check(lov_log && !lesson_running(), "GLO + ARP: the results log, not a lesson");
    shot("learn-log");
    fm1_in.buttons |= BT(B_GLO); frame(); tap(B_SEQ); fm1_in.buttons &= ~BT(B_GLO); frame();
    check(lov_help == 1 && !lov_log && learn_page() == 1, "GLO + SEQ: the help, the page unchanged");
    shot("learn-help");
    tap(B_SEQ);
    check(lov_help == 2 && learn_page() == 1, "SEQ pages the help, not the pages");
    shot("learn-help2");
    tap(B_SEQ);
    check(lov_help == 3, "screen 3");
    shot("learn-help3");
    tap(B_SEQ);
    check(lov_help == 4, "screen 4: colours and signs");
    shot("learn-help4");
    tap(B_SEQ);
    check(lov_help == 0 && learn_page() == 1, "and off again");
    fm1_in.buttons |= BT(B_GLO); frame(); tap(B_SEQ); fm1_in.buttons &= ~BT(B_GLO); frame();
    tap(B_ENV);
    check(!lov_help, "any button closes it");
    tap(B_ENV);
    kdown(7); frames(2);
    check(!lresult.valid, "a key clears the result card");
    key_up_(7); frames(2);
#if LEARN_STAGE >= 2
    /* ---- pages keep their work across a visit elsewhere ---- */
    tap(B_SEQ); frames(2);
    check(learn_page() == 2, "SEQ: page 2");
    encs[panel.enc[5]] = 1; frame();
    { unsigned t = lsong.tempo; tap(B_SEQ); frames(2); for (i = 2; i < LEARN_STAGE; i++) { tap(B_SEQ); frames(2); } tap(B_SEQ); frames(2);
      check(learn_page() == 2 && lsong.tempo == t, "round the pages and back: page 2 kept its tempo"); }
    while (learn_page() != 1) { tap(B_SEQ); frames(2); }
#endif

    /* ---- back to SLOOP ---- */
    kdown(7); frames(2);
    hold_both(1700);
    check(!learn_on, "GLO + HOME again: SLOOP");
    frames(4);
    check(learn_isr == 0 && notes_on(LT_KEYS) == 0, "the ISR followed out and ended the layer's note");
    key_up_(7); frames(2);
    shot("learn-off");
    lev_r = lev_w; key(7); frames(2);
    check(lev_r == lev_w, "a key in SLOOP: nothing reaches the layer");

    printf("learn ui: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
