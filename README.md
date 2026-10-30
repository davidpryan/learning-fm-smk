# Learning firmware pairs for the M-VAVE FM-1 and SMK-37 Elite (and Pro)

Four teaching firmwares, one per stage, that take a complete beginner from
"what note is this?" to a finished song on two inexpensive boxes:

| Stage | SMK-37 Elite / Pro | FM-1 | New idea |
| --- | --- | --- | --- |
| 1 Hear it | Lantern | Ember | notes and chords named as you play, drills, 20 lessons |
| 2 Loop it | Pulse | Root | chord loop, generated drums, a bass that follows |
| 3 Grow it | Shadow | Tape | tracks defined relative to other tracks, a 4-track recorder |
| 4 Finish it | Stage | Scribe | sections, transitions, a whole song performed by hand |

The SMK-37 Elite is the primary target; the Pro shares the same source and gets its own package with its own update identity (the loader refuses a mismatch).

Built on M-VAVE's official open firmware ([fm1_open](https://github.com/m-vave/fm1_open),
[smk37_open](https://github.com/m-vave/smk37_open)) and installed with
M-VAVE's own browser workbench. **Read [docs/SAFETY.md](docs/SAFETY.md)
before installing anything.** Unofficial; not affiliated with M-VAVE.

## Get the firmware

Each tagged release publishes, for every pair and board (SMK-37 Elite, SMK-37 Pro, FM-1): the `.fwsc`
package, its SHA-256, a provenance file (source commits, build time), the
installer page (the vendor workbench with the package embedded, with
Restore factory built in) and the corresponding source archive. The install
site (`pages` workflow, GitHub Pages) has one page per pair.

## Build and test on any machine

```
git clone --recurse-submodules <this repo>
python3 tools/make_lessons.py              # lessons/*.json -> C table + .syx selectors
python3 tools/overlay.py --all             # assembles build/trees/<board>-s<stage> (SMK-37: --model elite|pro, default elite)
for t in build/trees/*; do tools/host_build.sh "$t"; done   # native build + simulator tests
python3 tools/audit_safety.py              # no Flash writes, only the reviewed hooks
tools/device_syntax_check.sh build/trees/smk37-s4           # device-header syntax + sizes
```
Device packages need the JieLi 2.5.3 Windows toolchain and the vendor
`sdk/Build.ps1`; the `firmware` workflow does this on `windows-latest`
(see `tools/ci/Install-Toolchain.ps1` for the toolchain options). See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Layout

* `overlay/app/learn/` the learn layer: shared code plus one file per stage
* `lessons/` the 20 lesson packs (JSON); `tools/make_lessons.py` compiles them
* `tools/overlay.py` assembles a buildable tree; `tools/audit_safety.py` checks it
* `tools/host/` native test harness on the vendor browser engine
* `vendor/` the two official open firmwares, pinned (`VENDOR-PINS.txt`)
* `docs/` safety, recovery, architecture, spec, per-stage control references
* `.github/workflows/` ci (host tests), firmware (device builds), release, pages

## Licence

GPL-3.0-only WITH the FM1/SMK37 SDK linking exception, the same terms as
the vendor APP sources (`LICENSE`). Built packages contain the vendor binary
SDK under its own terms (`vendor/*/THIRD_PARTY.md`); the release workflow
ships those notices and the corresponding source with every package.
