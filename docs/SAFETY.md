# Safety: installing and going back without bricking anything

These firmwares are SLOOP builds. They are packaged by SLOOP's own
`tools/build.py` (the same update loader, the same `.fwsc` format, the same
stock update protocol) and installed with SLOOP's own web installer. That is
the single most important safety property: **nothing in this project invents
a flashing path**, and the stock tools still see the device as usual.

## What is and is not written

**The learn layer writes no Flash at all.** The practice log, takes, sections
and lessons live in RAM; the lesson packs are compiled in. What SLOOP itself
stores (its project autosave, settings, user presets, samples) is SLOOP's
normal, well-tested behaviour and is unchanged by the learn layer.

Never written by SLOOP or by this project: the bootloader (0x00000–0x03FFF),
the Bluetooth pairing area (BTIF), the factory calibration record
(USRTRIM, `trimdat`), and the chip key. The SMK-37 board layer documents
these ranges and asserts them.

**LEARN mode works in SLOOP's live project.** Pages 2–4 write their loop
into the four tracks and page 1 turns the arpeggiators off, and SLOOP's
autosave then keeps that as the music in progress. This cannot harm the
device, but it replaces whatever loop you had in SLOOP's working memory:
**save your SLOOP project to a slot before entering LEARN mode** if you care
about it.

## Risks and what protects you

| Risk | Likelihood | Protection |
| --- | --- | --- |
| Interrupted write (cable pulled, battery dies) | the main real risk | Keep USB connected and the device on until it restarts. The device stays in update mode; press INSTALL again and it finishes. |
| Wrong device package | low | Each package carries its model identity (`SMK-37 Elite_9xy`, `FM-1_9xy`); the installer checks the connected model and refuses a mismatch. The install site keeps the two devices on separate pages. |
| Lost projects, presets or samples | low (SLOOP keeps them across installs) | Save a backup from SLOOP's web editor first anyway; it costs nothing. |
| A bug in the learn layer freezes its screen | possible, it is new code | Hold GLO + HOME two seconds: plain SLOOP, still inside the firmware. Audio runs on SLOOP's own ISR; the layer only queues events. Power-cycling loses nothing but RAM state. |
| A learn build will not start | low | After two failed starts SLOOP opens its USB recovery screen where the installer can write a package again (see RECOVERY.md). |
| Tampered download | low | Every package ships with a SHA-256 sidecar and `provenance.json` naming the exact commit and the SLOOP upstream commit. `sha256sum -c name.fwsc.sha256`. |

## Before every install

1. Charge the device, then run it from USB during the install (a data cable, no hub).
2. If it runs SLOOP already, save a backup from the SLOOP web editor.
3. Verify the checksum of what you downloaded against the sidecar.
4. Open the installer page **for the device in your hands** in desktop Chrome or Edge.
5. Do not touch the device until it restarts.

## Going back to stock

- **SMK-37 Elite:** M-VAVE's own updater (M-UPGRADE) with the official SMK-37 Elite package, or SLOOP's installer with `--force` and that package. The stock firmware keeps its key/knob/fader mapping in the user sample area; SLOOP's web editor has *Prepare the return to M-Vave* for the case described in `sloop/SMK37.md`.
- **FM-1:** the installer page's *Return to the official firmware (V15)* with M-VAVE's `FM-1.fwsc`, or M-UPGRADE.

Your calibration and Bluetooth keys are untouched by this project, so the
stock firmware comes back complete.

## Moving between versions

Install the newer package over the current one with the same installer; no
return to stock is needed in between. SLOOP keeps projects, presets, samples
and settings across installs.

## If the device will not boot

See RECOVERY.md.
