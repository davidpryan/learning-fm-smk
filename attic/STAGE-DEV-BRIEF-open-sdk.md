# Stage developer brief

Read this fully before writing a stage module. It is the contract between
the learn layer (`overlay/app/learn/`) and the stage files.

## What this repo is

Four teaching firmwares for the M-VAVE FM-1 and SMK-37 Elite / Pro, built as a
small "learn layer" on top of M-VAVE's official open firmware
(`vendor/fm1_open`, `vendor/smk37_open`). The stock engine (sequencer,
DX7/808/303/PSG/sample engines, MIDI, storage, Bluetooth) is a binary SDK on
the device and C source in the browser preview (`web/engine`). We never
touch the engine; we drive it through `app/instrument.h` (`groove_*`).

The learn layer replaces the stock screen loop while LEARN mode is on
(`learn_main.c`), owns the pads on the SMK-37, and routes key / pad / knob /
fader / SysEx events to the active stage page. Pages: 1 Lantern/Ember,
2 Pulse/Root, 3 Shadow/Tape, 4 Stage/Scribe. SEQ cycles pages. A Stage N
firmware compiles pages 1..N.

## Hard rules (safety)

1. **No Flash writes.** Do not call `fm1_flash_*`, `groove_project_save_slot`,
   `groove_samples_*` write paths, or anything that persists. State is RAM.
   Takes and sections in Stages 3-4 live in RAM arrays, not project slots.
2. **Control task only.** Stage functions run from `learn_control()`.
   Never block, spin, or allocate. No `malloc`.
3. **Bounded work per poll.** The LCD is slow: redraw only what changed
   (see the `shown_*` cache pattern in `learn_stage1.c`). A full-screen
   clear costs ~57 KB of SPI traffic; do it only on `x->redraw`.
4. **Strict warnings.** Learn sources build with `-Wall -Wextra -Werror`
   (host) and must also be plain C11 for the JieLi pi32 clang 4.0.1: no VLAs,
   no `__int128`, no designated-initialiser tricks beyond C99, no `%` printf
   use (there is no printf on the device; format numbers by hand as the
   stage 1 code does).
5. **RAM budget.** Keep each stage's static data under ~6 KB. The device
   is tight. Prefer `uint8_t` arrays.
6. **No new hooks into the vendor app.** Everything a stage needs is
   available through `learn.h`, `learn_song.h` and `instrument.h`.

## Files you own

* `overlay/app/learn/learn_stageN.c` — the page (N = your stage).
* `tools/host/test_stageN.inc` — host tests, `static void test_stageN(void)`,
  using the `CHECK` macro and the `sim_*` helpers declared in `harness.c`.
* `docs/stageN.md` — the player-facing control reference for the pair.

Do not edit other files. If you need a shared helper that does not exist,
add a `static` one in your stage file.

## API you call

`learn.h` (read it): `struct lctx`, theory helpers, drawing, MIDI helpers,
SysEx (`lsyx_send`, command enum), practice log, lesson runner, notices,
`learn_bank_select`, track enum `LT_*`.

`learn_song.h` (read it): the song model and `song_write_*` renderers.
Stage 2 is the owner of the model's values; Stages 3-4 read them and add
their own tracks.

`instrument.h` (vendor, read the parts you use): `groove_get_view`,
`groove_sequence_edit/replace/read/chord`, `groove_step_setting(_edit)`
(settings: 0 velocity, 1 gate 0..15 (0 = 50 %, 15 = 100 %), 2 probability
0 always / 1..7 = n/8, 3 chord id, 4 tie, 5 lock param, 6 lock value, 7 note),
`groove_sequence_setting(_edit)` (0 swing %, 1 metronome, 2 count-in),
`groove_mix_toggle(track, solo)`, `groove_control_edit(track, program, volume, cutoff)`,
`groove_sends_edit`, `groove_edit_checkpoint/undo`, `groove_edit_record`,
`groove_edit_copy/paste/clear(track, step, scope 0 step / 1 page / 2 track)`,
`groove_sequence_length(_set)` (16/32/48/64), `groove_tempo_*`, `groove_arp_*`.
Transport: `song_play(1)` sends MIDI Start (0xFA) locally, `song_play(0)` Stop.

Drum notes: `groove_drum_note(d)` with d = 0 kick, 1 snare, 10 closed hat,
6 clap, 9 open hat, 5 rim, 8 cymbal. The drum track is `LT_DRUMS` (808).

## Stage functions (all required, see `learn.h`)

```
void stageN_init(void);
void stageN_control(struct lctx *x);  // every poll: buttons via x->btn_down, timers via x->now
void stageN_draw(struct lctx *x);     // x->redraw = 1 means full redraw
void stageN_key(struct lctx *x, unsigned note, unsigned velocity);   // velocity 0 = release
void stageN_pad(struct lctx *x, unsigned pad, unsigned velocity, unsigned pressure); // velocity 0xFFFF = pressure update
void stageN_encoder(struct lctx *x, unsigned id, int delta);          // id 0..6 = K1..K7
void stageN_fader(struct lctx *x, unsigned id, unsigned value);       // SMK only, id 0..3, value 0..127
void stageN_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n);
uint32_t stageN_pad_colour(unsigned pad);                             // 0xRRGGBB, keep <= 0x30 per channel unless P_FULL
```
`stageN_sysex` is called for every learn SysEx regardless of page, so ignore
commands that are not yours. Board differences: `#ifdef LEARN_BOARD_SMK37`
(pads, faders, 37 keys from C3) else FM-1 (27 keys from F3, no pads, no
faders; map pad functions onto the ten "outer" buttons or keys with GLO held).

Logical buttons (`LB_*`, both boards): OCT_DOWN OCT_UP FX SCL ENV LFO EDIT GLO
HOME SAVE ARP SEQ PLAY REC. OCT and SEQ are consumed by `learn_main.c` (octave,
page switch); everything else is yours on your page. HOME should always stop
what the page is doing.

## Screen

240x240, RGB565, `draw_text(x, y, s, scale 1..4, fg, bg)`; glyphs are
6*scale wide, 7*scale tall. Keep the layout family of Stage 1: header at
y=2 (scale 1), big value at y=18 (scale 4), rows below, prompt at y=164,
hint y=212, notice bar y=230 (`learn_notice_text()`).

## Colour language (shared, do not change)

Green = I / playing / pass. Orange = V / Chorus / queued. Cyan = IV / Chords
track. Blue = minor / Verse. Red = kick / armed / recording. Yellow = snare /
muted. White = hat / sus / empty. Purple = perc / bVII / Bridge. A dim version
means "follows the full-colour one".

## Teaching layer (every stage)

Practice log (`log_add` with the right `LOGK_*`), slow mode (`x->slow_percent`:
scale every tutor/playback interval with `ms * 100 / slow_percent`; a
recording made under 100 % is logged `slow`), mistake replay (two
alternatives on REC), why-hints on a 2 s hold (`learn_notice` or a hint
line), follow-me (light the next pad/key; send `LCMD_PATH`), and lessons:
`lesson_step()` returns the current step; when its `type` is your drill
kind run it and call `lesson_step_done(passed, score)`.

## Build and test loop

```
python3 tools/make_lessons.py                       # once
python3 tools/overlay.py --board smk37 --stage N && tools/host_build.sh build/trees/smk37-sN
python3 tools/overlay.py --board fm1   --stage N && tools/host_build.sh build/trees/fm1-sN
```
Both must print `HOST TESTS PASSED`. The harness boots the firmware in
M-VAVE's browser engine; `sim_advance(ms)` runs audio blocks and polls the
control loop every 10 ms. Switch to your page in a test with
`sim_control(0, LB_SEQ, 1); sim_control(0, LB_SEQ, 0);` repeated (page
order 1 → 2 → 3 → 4 → 1). Check MIDI output with `sim_midi_pop`, pads with
`sim_pad_rgb`, the engine with `groove_get_view`.

## Specification

The player-facing spec for every stage is in `docs/SPEC.md` (exported from
the design document). Implement your stage's section, including its
teaching-layer paragraphs and the MIDI table. Where the spec names 18 pads,
the hardware has 16: use pads 1-16 and move the two overflow functions to
buttons (say which in `docs/stageN.md`).
