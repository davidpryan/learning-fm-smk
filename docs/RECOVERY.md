# Recovery

Work down this list; stop at the first step that works.

## 1. It boots but the screen is stuck

Audio and MIDI run on the engine, not on the learn screen loop. Hold
SHIFT+HOME (SMK-37 Elite / Pro) or GLO+HOME (FM-1) for two seconds to switch to the
stock UI, which is compiled into every learn firmware. Report the stuck
page as an issue with the stage, board and what you pressed.

## 2. It boots into the learn page but you want stock back

Open the installer page for the pair you have and use **Restore factory**,
or M-VAVE's own updater with the official package. Both use the unmodified
factory file from the vendor SDK.

## 3. It restarts into the update loader

The loader is the stock M-VAVE update loader; it is what every Open
firmware and every community firmware uses. Connect USB, open any of this
project's installer pages (or the official workbench) in desktop Chrome or
Edge, and run **Device update** again, or **Restore factory**. The workbench
asks you to confirm the physical model before writing.

## 4. Nothing responds over USB after a failed write

Power-cycle with USB connected. The FM-1 and SMK-37 share the JieLi AC791N
(WL82) chip, whose mask-ROM bootloader cannot be erased by software:

* SMK-37: the community port kit documents the loader soft key and the
  mask-ROM USB bootloader (it appears as a mass-storage device,
  `WL82 UBOOT1.00`) and a full-flash backup/restore procedure with
  jl-uboot-tool: https://github.com/genkigenki/fm1-to-smk37
  (docs/02-update-and-recovery.md).
* FM-1: the same chip; community rescue tools exist for the case where the
  bootloader is not reachable by software (FM-1 Transporter,
  MvaveFM1Unbricker), linked from the port kit's credits.

Before using a low-level tool, read its README fully. A full 1 MiB backup
taken *before* your first install is the best insurance; the port kit
explains how to take one and to check that the calibration record
(`trimdat` at 0xC1224 on the SMK-37) is present in the dump.

## 5. Ask

Open an issue with: device and model, which package (name + SHA-256), what
the screen showed, and whether the loader is reachable. Include the
provenance JSON from the release.
