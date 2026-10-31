# How it works

## One learn layer, two boards, four pages

SLOOP is a single translation unit (`sloop/firmware/src/felucca.c` includes
every source). The learn layer (`learn/learn.c`, which includes the rest of
`learn/`) is included after SLOOP's DAW mode and works exactly like it: a
*mode* that, while on, owns the keys, pads, buttons, knobs, faders, screen
and lights, with plain SLOOP still inside (GLO + HOME held two seconds
toggles). Everything is behind `FELUCCA_LEARN`; with it off the SLOOP build
is byte-identical to upstream. The hooks, nine lines in six files, are listed
in `sloop/firmware/src/LEARN-PATCH.md`:

| Hook | Where | What it does |
| --- | --- | --- |
| `learn_switch`, `learn_input` | ui_input.c `ui_input()` | the GLO+HOME switch; in LEARN mode the buttons, knobs, faders and queued events go to the layer |
| `learn_leds` | ui_input.c `ui_leds()` | button LEDs, key LEDs (FM-1), pad colours (SMK) |
| `learn_screen` | ui_draw.c `ui_draw()` | the page is drawn as two 240x120 canvases on SLOOP's `cv_*` |
| `learn_follow`, `learn_keys_block` | seq.c `keyboard_block()` (audio ISR) | the 27 keys' edges into a ring; the layer's sound requests into `input_on/off` |
| `learn_ext_event` | seq.c `ext_keys_block()` (SMK) | the 16 pads and the ten outer keys into the same ring |
| `learn_midi_in` | seq.c MIDI-in drain | notes from the other box, for the tutor |
| `learn_sysex` | editor.c `ed_service()` | `F0 7D 'L' 'N'` frames, before the editor's own protocol |

## Contexts and safety

Two single-producer rings cross the audio ISR boundary (`learn/midi.c`):
ISR → main carries key / pad / MIDI events; main → ISR carries sound
requests (track, note, velocity) that the ISR turns into SLOOP's
`input_on` / `input_off` and MIDI out. Nothing of the layer's logic runs in
the ISR; nothing blocks; no allocation. The layer has no Flash code: the
practice log, takes, sections and lessons are RAM / compiled in. SLOOP's own
autosave of the project is unchanged. On the SMK-37 the lesson progress (16
bits: `lprog_word`, learn/main.c) is carried in the two spare bytes of SLOOP's
settings object (`smkset_t rsv[2]`) by SLOOP's own quiet-time settings save;
the layer only sets `settings_later` when the value changes (see SAFETY.md).

Sequencer data (steps, drum lanes, lengths, tempo, swing, mutes, levels) is
written from the main loop into SLOOP's track structures, the way SLOOP's
own editor does it.

## The pages

Each page is one file with the same entry points (init, control, draw, key,
pad, encoder, fader, sysex, pad colour), dispatched by `learn/main.c`.
Pages 2–4 share the loop model in `learn/song.c`, rendered into SLOOP's
tracks: track 1 the melody (SLOOP's own live recorder), track 2 the chords
(a chord per beat held by TIE steps), track 3 the bass the rule derives, the
drum track the four lanes. Page 3's shadows render onto those same tracks
(chords rules onto track 2, melody rules as notes added to the take's POLY
steps, ghost hats onto the drum track). Page 4's sections snapshot the model
and the melody steps.

## Pairing the two boxes

Both boxes send their keys over USB MIDI (the SMK-37 also over its TRS MIDI
OUT) and the layer adds a SysEx vocabulary (`learn/sysex.c`) for everything
that is not a note: key change, follow-me, hints, drill state, scores, the
practice log, the loop's grid and chords, section queueing. Frames are
`F0 7D 'L' 'N' 01 <device> <page> <cmd> ... F7`. Neither box can host the
other over USB (both are USB devices): use a phone or tablet with a hub and
a MIDI router app, a small USB MIDI host box, or the SMK-37's TRS out into
the FM-1's TRS in for the one-way case.

## Lessons and the practice log

Lessons are JSON in `lessons/`, compiled in by `lessons/make_lessons.py`
(`learn/lessons.c`, plus `.syx` selector files). The practice log and the
unlock bits live in RAM; `LCMD_LOGDUMP` exports them. Page graduations are
guidance (a notice names the next page); nothing is locked.

## Building and testing

* Host: `tools/host_tests.sh` compiles the layer into SLOOP's own UI harness
  (`sloop/tests/ui_pages_test.c`, gcc) for every page and drives it: the
  switch, keys through both rings into the synth, drills, lessons, the loop
  on the tracks, shadows, takes, sections; the screens come out as PPM.
* Device: SLOOP's `tools/build.py` with JieLi's Linux toolchain
  (`tools/ci_deps.sh`, pinned by SHA-256). `FELUCCA_LEARN=1` adds the layer
  and selects the smaller sample set (`FELUCCA_SAMPLE_SETS`), ~100 KB of
  flash the pages need. CI builds plain SLOOP and the learn firmware for
  both boards, reports the image size, the biggest symbols and any build
  error as annotations, and on a `vX.Y` tag publishes the install site and
  a release (`tools/make_site.py`, SLOOP's web installer per device).
