#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Safety audit, run in CI and before every release.

1. The learn layer must not call anything that writes Flash or persists
   state, allocates, or prints. The device's calibration, Bluetooth keys and
   bootloader live in Flash ranges the SDK keeps out of reach, but the user
   partition (projects, samples, voice library) is reachable through the
   fm1_flash_* API, and a bug there costs the player their data. So: none.
2. The assembled trees must differ from the pinned vendor trees only by the
   files tools/overlay.py is allowed to touch, with exactly the hook lines
   it is meant to add. Anything else means a reviewer has not seen it.
"""
import os, re, sys, glob, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FORBIDDEN = [
    r"\bfm1_flash_write\b", r"\bfm1_flash_erase\b", r"\bgroove_project_save_slot\b",
    r"\bgroove_samples_begin\b", r"\bgroove_samples_write\b", r"\bgroove_samples_finish\b",
    r"\bsmk37_syscfg_write\b", r"\bgroove_storage_prepare\b", r"\bgroove_fm_import\b",
    r"\bmalloc\b", r"\bcalloc\b", r"\brealloc\b", r"\bfree\s*\(", r"\bprintf\b", r"\bsprintf\b",
    r"\bfm1_device_recovery\b", r"\bfm1_flash_invalidate\b",
]
ALLOWED_DIFF = {"app/screen.c", "app/transfer.c", "app/midi_router.c", "app/smk37_controls.c", "app/version.h", "sdk/Build.ps1"}
# The Linux build patch may only touch these things in sdk/Build.ps1.
BUILD_PS1_ALLOWED = re.compile(r"clang|llvm-nm|llvm-objcopy|pi32v2-ld|LLVMgold|external_toolchain|Linux toolchain|Test-Path|required|\}|\{|^\s*$|pi32_compiler_sha256|JieLi"
                               r"|plugin-opt|'-flto'|sdk\.elf|DirectorySeparatorChar|StartsWith|Replace\(|SDK payload changed")

def scan_learn():
    bad = 0
    for f in sorted(glob.glob(os.path.join(ROOT, "overlay", "app", "learn", "*.[ch]"))):
        src = open(f, encoding="utf-8").read()
        # Strip comments so documentation can mention the names.
        code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        code = re.sub(r"//.*", "", code)
        for pat in FORBIDDEN:
            for m in re.finditer(pat, code):
                line = code[:m.start()].count("\n") + 1
                print(f"FORBIDDEN {os.path.relpath(f, ROOT)}:{line}: {m.group(0)}")
                bad += 1
    return bad

def _files(root, skip):
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in skip]
        for f in filenames:
            full = os.path.join(dirpath, f)
            out[os.path.relpath(full, root).replace(os.sep, "/")] = full
    return out

def _lines(path):
    with open(path, "rb") as fh:
        return fh.read().replace(b"\r\n", b"\n").decode("utf-8", "replace").splitlines()

def diff_tree(board, stage, tree=None):
    """Pure-Python comparison (no external diff): portable across Linux, macOS and Windows runners."""
    import difflib
    vendor = os.path.join(ROOT, "vendor", "fm1_open" if board == "fm1" else "smk37_open")
    tree = tree or os.path.join(ROOT, "build", "trees", f"{board}-s{stage}")
    if not os.path.isdir(tree):
        print(f"SKIP {board} stage {stage}: not assembled")
        return 0
    bad = 0
    skip = {".git", "build", "host", "learn"}
    v = _files(vendor, skip)
    t = _files(tree, skip)
    for rel in sorted(set(t) - set(v)):
        if rel not in ("LEARN-BUILD.txt", "LINUX-BUILD-PATCH.txt"):
            print(f"UNEXPECTED FILE {board}-s{stage}: {rel}")
            bad += 1
    for rel in sorted(set(v) - set(t)):
        print(f"MISSING VENDOR FILE {board}-s{stage}: {rel}")
        bad += 1
    for rel in sorted(set(v) & set(t)):
        a, b = _lines(v[rel]), _lines(t[rel])
        if a == b:
            continue
        if rel not in ALLOWED_DIFF:
            print(f"UNEXPECTED DIFF {board}-s{stage}: {rel}")
            bad += 1
            continue
        added, removed = [], []
        for line in difflib.unified_diff(a, b, lineterm="", n=0):
            if line.startswith("+++") or line.startswith("---") or line.startswith("@@"):
                continue
            if line.startswith("+"):
                added.append(line[1:])
            elif line.startswith("-"):
                removed.append(line[1:])
        if rel == "app/version.h":
            ok = all(("FM1_RELEASE_VERSION" in l or "FM1_UPDATE_PRODUCT" in l or not l.strip()) for l in added + removed)
        elif rel == "sdk/Build.ps1":
            # The -Oz rule gains app/learn/ on every host; the Linux patch is allowed only with its marker file.
            oz = [l for l in added + removed if "$options += '-Oz'" in l]
            added = [l for l in added if l not in oz]; removed = [l for l in removed if l not in oz]
            oz_ok = len(oz) == 2 and any("StartsWith('app/learn/')" in l for l in oz)
            if not added and not removed:
                ok = oz_ok
                if not ok:
                    print(f"UNEXPECTED EDIT {board}-s{stage} {rel}: {oz}")
                    bad += 1
                continue
            # Removed lines may also be the two bookkeeping lines of the replaced Windows hash loop.
            extra_removed = {"        $name = $entry.Name", "        $file = Join-Path $Toolchain $name"}
            extra_added = set()
            ok = (oz_ok and os.path.isfile(os.path.join(tree, "LINUX-BUILD-PATCH.txt")) and all(BUILD_PS1_ALLOWED.search(l) or l in extra_added for l in added)
                  and all(BUILD_PS1_ALLOWED.search(l) or l in extra_removed for l in removed))
        else:
            ok = not removed and all(("learn_" in l and "extern" in l) for l in added)
        if not ok:
            print(f"UNEXPECTED EDIT {board}-s{stage} {rel}: +{added} -{removed}")
            bad += 1
    return bad

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", help="audit one assembled tree (reads its LEARN-BUILD.txt) instead of build/trees/*")
    a = ap.parse_args()
    bad = scan_learn()
    if a.tree:
        info = dict(l.strip().split("=", 1) for l in open(os.path.join(a.tree, "LEARN-BUILD.txt"), encoding="utf-8") if "=" in l)
        bad += diff_tree(info["board"], int(info["stage"]), os.path.abspath(a.tree))
    else:
        for board in ("fm1", "smk37"):
            for stage in (1, 2, 3, 4):
                bad += diff_tree(board, stage)
    print("SAFETY AUDIT " + ("FAILED" if bad else "PASSED"))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
