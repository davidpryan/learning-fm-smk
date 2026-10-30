# Recovery

Work down this list; stop at the first step that works.

## 1. It boots but the learn screen is stuck

Audio runs on SLOOP's own audio ISR, not on the learn screen loop. Hold
GLO + HOME for two seconds to switch to plain SLOOP, which is compiled into
the firmware. If that does not respond either, power-cycle: only RAM state
(practice log, takes, sections) is lost. Report the page, the device and what
you pressed as an issue.

## 2. It boots but you want stock back

SMK-37 Elite: M-VAVE's updater (M-UPGRADE) with the official package, or
SLOOP's installer with `--force` and that package. FM-1: the installer page's
*Return to the official firmware (V15)*, or M-UPGRADE. See SAFETY.md.

## 3. It restarts into update mode, or the install was cut off

The device waits in SLOOP's update loader (the same one every SLOOP install
uses). Connect USB, open the installer page for your device, press INSTALL
again. The page checks the model before it writes.

## 4. It does not start the firmware

After two failed starts SLOOP opens its USB recovery screen (identity
`SMK-37 Elite_000` / `FM-1_000`), where the installer can write a package
again. FM-1: hold OCT− alone while switching on (*SLOOP USB RESCUE*), then
install again. After a third failed start the chip drops into its mask-ROM
update mode (UBOOT), which cannot be erased by software:

* SMK-37: the port kit documents UBOOT and a full-flash backup/restore with
  jl-uboot-tool: https://github.com/genkigenki/fm1-to-smk37
  (docs/02-update-and-recovery.md). Check that the calibration record
  (`trimdat` at 0xC1224) is present in any dump you restore.
* FM-1: https://github.com/kurogedelic/FM-1-transporter.

Read a low-level tool's README fully before using it.

## 5. Ask

Open an issue with: device and model, which package (name + SHA-256), what
the screen showed, and whether the installer still sees the device. Include
`provenance.json` from the release.
