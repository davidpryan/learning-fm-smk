# How it works

## One learn layer, two boards, four stages

`overlay/app/learn/` is a small C module compiled into M-VAVE's official
open firmware for the FM-1 (`vendor/fm1_open`) and the SMK-37 Elite / Pro
(`vendor/smk37_open`). `tools/overlay.py` copies a vendor tree, adds the
learn sources, generates `learn_config.h` (board + stage) and inserts five
one-line hooks into the stock app:

| Hook | File | What it does |
| --- | --- | --- |
| `learn_init()` | app/screen.c, `groove_screen_init` | sets up the beginner instrument (tracks, banks, live MIDI out) |
| `learn_control()` | app/screen.c, `groove_screen_control` | runs the learn page instead of the stock UI while LEARN mode is on |
| `learn_sysex()` | app/transfer.c, `fm1_app_sysex` | claims `F0 7D 'L' 'N'` frames, leaves the stock `F0 7D 'F' 'M'` protocol alone |
| `learn_midi_observe()` | app/midi_router.c, `fm1_app_midi_received` | lets the tutor see notes arriving from the other box |
| `learn_smk37_event()` / `learn_pad_color()` | app/smk37_controls.c (SMK only) | keys, pads, knobs, faders and pad colours |

The CI safety audit (`tools/audit_safety.py`) diffs every assembled tree
against the pinned vendor tree and fails if anything other than those
lines, the version identity and the `app/learn` directory differ.

The stock engine (sequencer, DX7/808/303/PSG/sampler, MIDI, Bluetooth,
storage, the update loader) is untouched. On the device it is the vendor's
binary SDK; in the browser workbench and in our host tests it is the
vendor's C engine (`web/engine`). The learn layer drives it only through
`app/instrument.h`.

## Pairing the two boxes

Both boxes send live key output over USB and Bluetooth MIDI and listen on
the same channels (track 1 = channel 1, ... drums = channel 10). The learn
layer adds a SysEx vocabulary (`learn_sysex.c`) for everything that is not
a note: key change, follow-me, hints, drill state, scores, the practice
log, section queueing. Frames are `F0 7D 'L' 'N' 01 <device> <stage> <cmd>
... F7`; the stage byte lets a Stage 2 Pulse talk to a Stage 3 Tape.

Neither box can host the other over USB (both are USB devices). Use a
phone or tablet with a USB-C hub and a MIDI router app (AUM or MIDI Flow on
iOS, MIDI Hub on Android), a small USB MIDI host box (CME H2MIDI Pro or
similar), or Bluetooth MIDI for the stages where timing is not critical.

## Stages

Stage N firmware compiles pages 1..N; SEQ cycles pages. Each page is one
file (`learn_stageN.c`) with the same eight entry points (init, control,
draw, key, pad, encoder, fader, sysex, pad colour). Stages 2-4 share the
song model in `learn_song.c`, which renders chords, a derived bass and drum
lanes into engine tracks so the engine does the playing.

## Lessons and the practice log

Lessons are JSON in `lessons/`, compiled into the firmware by
`tools/make_lessons.py` (also emitting `.syx` selector files). The practice
log and unlocks live in RAM; `LCMD_LOGDUMP` exports them. No Flash is
written by the learn layer in this version.

## Building

* Host (any OS with clang/gcc): `tools/overlay.py --board smk37 --stage 1`
  then `tools/host_build.sh build/trees/smk37-s1`. This links the learn
  layer against the vendor browser engine and runs the simulator tests.
* Device: the vendor's `sdk/Build.ps1`, run under `pwsh` on Ubuntu with
  JieLi's Linux toolchain, the way the Felucca-family FM-1 firmwares build
  this chip in their CI (`tools/get_toolchain.sh` there; `jieli-linux-
  toolchains-*`, clang 4.0.1 for pi32v2, a plain tarball). Differences from
  the Windows kit the vendor script expects, all handled by
  `tools/overlay.py --host-os linux` on the assembled copy and allowed by
  the safety audit: tool names (`pi32v2/bin/ld`, `common/bin/objcopy`)
  reached through a shim `bin/`; no gold LTO plugin on Linux, so `-flto`
  and the `--plugin-opt` link options are dropped (the vendor recipe already
  builds its UI and board units without LTO; the rest loses cross-unit
  inlining only); the Windows-binary hash loop becomes a presence check,
  with the tarball pinned by SHA-256 in `tools/ci/jieli-linux.sha256` and
  the compiler version verified; Build.ps1's hash of itself is skipped
  (every other SDK file is still verified); three `\` path assumptions use
  the platform separator. All vendor post-link checks (SRAM placement,
  heap, callbacks, sections) and the C# packager run unchanged.
  See `.github/workflows/firmware.yml`.
  The vendor's embedded-installer HTML build is Windows-only, so releases
  ship `.fwsc` packages for M-VAVE's hosted workbench instead. On a Windows
  PC with the JieLi 2.5.3 kit, the unpatched vendor build (`build.bat`)
  works on any assembled tree too.
