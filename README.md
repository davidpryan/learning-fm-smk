# Learning firmware for the M-VAVE SMK-37 Elite and FM-1

One firmware per device that takes a complete beginner from "what note is
this?" to a finished song, in four pages. Built on **SLOOP** (the open 4-track
groovebox firmware for the FM-1) with genkigenki's SMK-37 board layer, the way
every existing open SMK-37 firmware is built. GPL-3.0-only.

| Page (SEQ cycles them) | SMK-37 Elite | FM-1 | What it teaches |
| --- | --- | --- | --- |
| 1 Hear it | Lantern | Ember | notes and chords named as you play, nudge-or-mute, drills, 20 lessons, follow-me lights |
| 2 Loop it | Pulse | Root | a chord per beat, Euclidean drums, a bass that follows the kick, the melody recorder, the rhythm trainer |
| 3 Grow it | Shadow | Tape | parts described by a rule on another part (hold, arp, 3rd above, echo, ghost ...), takes, punch, A/B |
| 4 Finish it | Stage | Scribe | eight sections queued by hand, transitions, energy, the song record |

Plain SLOOP is still inside: **hold GLO + HOME for two seconds** to switch
between LEARN and SLOOP, both ways. Each page's graduation (the last lesson,
a whole loop, a take in key) says when the next page is ready; nothing is
locked, so you can look ahead.

**Status:** both firmwares build in CI with the real JieLi toolchain and pass
the host test suite (every page, on SLOOP's own UI harness). They have not
yet been run on hardware. Read `docs/SAFETY.md` before installing anything.

## Install

Releases (`vX.Y` tags) publish an install site with **one installer page per
device** and the packages with SHA-256 sidecars. Each installer checks the
model and refuses the other device's package. Back to stock: M-VAVE's own
updater with the official package (the FM-1 page has a button for it).
Details and the order of operations: `docs/SAFETY.md`; the first run on a
device: `docs/FIRST-HARDWARE-TEST.md`; if something goes wrong: `docs/RECOVERY.md`.

Packages: `sloop-smk37-elite-learn.fwsc` (identity `SMK-37 Elite_9xy`) and
`felucca-learn.fwsc` (identity `FM-1_9xy`).

## Layout

* `sloop/` SLOOP with the SMK-37 board layer, vendored verbatim (`sloop/UPSTREAM.txt`); the learn hooks are listed in `sloop/firmware/src/LEARN-PATCH.md` and compile to nothing without `FELUCCA_LEARN=1`
* `learn/` the learn layer: `main.c` (the mode), `midi.c` (the ISR rings), `theory.c`, `song.c` (the loop model on SLOOP's tracks), `stage1..4.c` (the pages), `tests/` (host tests)
* `lessons/` the 20 lesson packs (JSON) and their generator (`learn/lessons.c`, `build/lessons/*.syx`)
* `docs/` design, safety, recovery, architecture, per-page control references
* `tools/` CI dependency fetcher (toolchain + SDK files, hash-verified), host test runner, size report, site builder
* `attic/` the earlier M-VAVE Open-SDK implementation, for reference (`attic/WHY.md`)

## Build and test

```
tools/host_tests.sh                    # every page on SLOOP's UI harness (gcc), screens in build/host/*.ppm
tools/ci_deps.sh                       # JieLi Linux toolchain + 3 AC79 SDK files (~/.jieli, ~/fw-AC79_AIoT_SDK)
cd sloop && FELUCCA_LEARN=1 BOARD=smk37 MODEL=elite ./build.sh   # -> build/sloop-smk37-elite-learn.fwsc
cd sloop && FELUCCA_LEARN=1 ./build.sh                           # -> build/felucca-learn.fwsc
```

A learn build carries a smaller sample set than plain SLOOP (piano, upright
bass, vibes and the GM kit; `FELUCCA_SAMPLE_SETS`) to make room for the
pages. Projects made in full SLOOP that use horns, strings, flute or scratch
samples play another sample there.
