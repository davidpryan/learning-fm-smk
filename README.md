# Learning firmware for the M-VAVE SMK-37 Elite and FM-1

Four teaching firmwares, one per stage, that take a complete beginner from
"what note is this?" to a finished song on two inexpensive boxes. Built on
**SLOOP** (the open 4-track groovebox firmware for the FM-1) with
genkigenki's SMK-37 board layer, the way every existing open SMK-37 firmware
is built. GPL-3.0-only.

| Stage | SMK-37 Elite | FM-1 | New idea |
| --- | --- | --- | --- |
| 1 Hear it | Lantern | Ember | notes and chords named as you play, drills, 20 lessons |
| 2 Loop it | Pulse | Root | chord loop, generated drums, a bass that follows |
| 3 Grow it | Shadow | Tape | tracks defined relative to other tracks, takes |
| 4 Finish it | Stage | Scribe | sections, transitions, a whole song performed by hand |

**Status:** the base (plain SLOOP for the SMK-37 Elite and the FM-1) builds in
CI; the learn layer is being re-targeted from the earlier M-VAVE Open SDK
version (kept in `attic/`, see `attic/WHY.md`). Read `docs/SAFETY.md` before
installing anything.

## Layout

* `sloop/` SLOOP with the SMK-37 board layer, vendored verbatim (`sloop/UPSTREAM.txt`)
* `learn/` the learn layer (compiled into SLOOP behind `FELUCCA_LEARN=1`)
* `lessons/` the 20 lesson packs and their generator
* `docs/` design, safety, recovery, per-stage control references
* `tools/` CI dependency fetcher (toolchain + SDK files, hash-verified)
* `attic/` the earlier Open-SDK implementation, for reference

## Build

```
tools/ci_deps.sh                       # JieLi Linux toolchain + 3 AC79 SDK files (~/.jieli, ~/fw-AC79_AIoT_SDK)
cd sloop && BOARD=smk37 MODEL=elite ./build.sh      # -> build/sloop-smk37-elite.fwsc
cd sloop && ./build.sh                               # FM-1
```
Install with `sloop/tools/fm1_install.py` or the web installer; back to stock
with M-VAVE's updater. See `sloop/SMK37.md`.
