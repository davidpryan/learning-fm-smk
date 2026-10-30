# Safety: installing and going back without bricking anything

These firmwares are built the same way M-VAVE builds its own open firmware
and are installed with M-VAVE's own browser workbench. That is the single
most important safety property: nothing here invents a flashing path.

## What can and cannot go wrong

**The bootloader, Bluetooth pairing data, factory calibration and chip key
are not written by anything in this project.** On the SMK-37 Elite / Pro those live
in Flash ranges the stock second-stage bootloader and the SDK keep outside
the application's reach. The learn layer only ever calls the SDK; it makes
**no Flash writes at all** (practice log, takes and sections are RAM, and the
CI audit fails the build if a Flash-writing call appears in the learn code).

What *can* go wrong, and what to do:

| Risk | Likelihood | Protection |
| --- | --- | --- |
| Interrupted write (cable pulled, battery dies) | the main real risk | Keep USB connected and the device on until it restarts. The loader stays installed; re-run the installer. |
| Wrong model package (Elite vs Pro, FM-1 vs SMK) | low | The update identity in each package names the model; the loader asks you to confirm the physical model and refuses a mismatch. Only install the package named for the device in your hands. |
| Lost projects, voices or samples | medium if you skip the backup | Back up in the workbench first (Projects & sounds). Learn firmware uses the same project format but do not rely on it. |
| A bug in the learn layer freezes the screen | possible, it is new code | Audio and MIDI keep running on the engine's own tasks. Power-cycle. Hold SHIFT+HOME (SMK) or GLO+HOME (FM-1) two seconds to return to the stock UI, which is still inside the firmware. |
| Tampered download | low | Every package ships with a SHA-256 sidecar and a provenance file naming the exact source commit. `sha256sum -c name.fwsc.sha256`. The installer page you open is itself built from that release's source. |

## Before every install

1. Charge the device, then run it from USB during the install.
2. Open the stock workbench and back up projects, voice groups A to D and samples. Keep the files; the learn firmware does not protect them.
3. Verify the checksum of what you downloaded against the sidecar, and that the sidecar matches the release page.
4. Install from desktop Chrome or Edge over a USB cable, not Bluetooth.
5. Confirm the physical model only when it matches the device in your hands.
6. Do not touch the device until it restarts and shows the stage page.

## Going back to stock

Every installer page has M-VAVE's **Restore factory** function, which uses
the unmodified official package (FM-1 V15 / SMK-37 V16) from the SDK. The
stock M-VAVE updater also works. Your Flash calibration and Bluetooth keys
are untouched by this project, so the stock firmware comes back complete.

## Moving between stages

Install the next pair's packages over the current one with the same
workbench flow; no return to stock is needed in between. Stage N contains
pages 1..N, so nothing is lost.

## If the device will not boot

See RECOVERY.md: the loader (reached by the update flow) and, beneath it,
the chip's mask-ROM USB bootloader, which cannot be erased from software.

## What this project does not do

* It does not write the SMK-37's calibration record, Bluetooth keys, the
  bootloader, the chip key or the stock resource area, ever.
* It does not use a custom update loader or packaging; packages are made by
  the vendor `Build.ps1` and `sdk/Package.cs`, with the vendor's SHA-256
  checks of the toolchain and SDK in place.
* It does not ship a package built on a machine nobody can audit: CI builds
  from a tagged commit, and the provenance file records the commit and the
  vendor submodule commit.
