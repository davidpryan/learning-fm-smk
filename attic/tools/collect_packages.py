#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Collect a built package into dist/ with a descriptive name, a SHA-256
sidecar and a provenance JSON (board, stage, pair names, vendor commit,
repository commit, build time, size). Also archives the exact assembled
source tree, because GPLv3 asks for the corresponding source of every
binary we hand out."""
import argparse, hashlib, json, os, shutil, subprocess, sys, tarfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = {1: ("Lantern", "Ember"), 2: ("Pulse", "Root"), 3: ("Shadow", "Tape"), 4: ("Stage", "Scribe")}

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def git(*args, cwd=ROOT):
    try:
        return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return "unknown"

def vendor_commit(vendor):
    """Submodule commit if vendor/ is a submodule, else the pin recorded in VENDOR-PINS.txt."""
    sub = git("rev-parse", "HEAD:vendor/" + vendor)
    if len(sub) == 40:
        return sub
    try:
        for line in open(os.path.join(ROOT, "VENDOR-PINS.txt"), encoding="utf-8"):
            parts = line.split()
            if parts and parts[0] == vendor:
                return parts[1]
    except OSError:
        pass
    return "unknown"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True, choices=["fm1", "smk37"])
    ap.add_argument("--stage", required=True, type=int, choices=[1, 2, 3, 4])
    ap.add_argument("--out", default="dist")
    ap.add_argument("--model", choices=["elite", "pro"], default="elite")
    a = ap.parse_args()
    tree = os.path.join(ROOT, "build", "trees", f"{a.board}-s{a.stage}" + (f"-{a.model}" if a.board == "smk37" else ""))
    if not os.path.isdir(tree):
        tree = os.path.join(ROOT, "build", "trees", f"{a.board}-s{a.stage}")
    if a.board == "fm1":
        src = os.path.join(tree, "build", "dx7", "generated", "FM1_OFFICIAL_OPEN.fwsc")
        vendor = "fm1_open"
        device = "FM-1"
        name = NAMES[a.stage][1]
    else:
        src = os.path.join(tree, "build", a.model, "generated", f"SMK37_{a.model.upper()}_OPEN.fwsc")
        vendor = "smk37_open"
        device = "SMK-37 " + a.model.capitalize()
        name = NAMES[a.stage][0]
    if not os.path.isfile(src):
        sys.exit(f"collect: package not found at {src}")
    version = f"V5{a.stage}"
    out = os.path.join(ROOT, a.out)
    os.makedirs(out, exist_ok=True)
    board_tag = a.board if a.board == "fm1" else ("smk37e" if a.model == "elite" else "smk37p")
    base = f"{name.lower()}-stage{a.stage}-{board_tag}-{version}"
    dst = os.path.join(out, base + ".fwsc")
    shutil.copyfile(src, dst)
    digest = sha256(dst)
    with open(dst + ".sha256", "w", encoding="utf-8") as fh:
        fh.write(f"{digest}  {os.path.basename(dst)}\n")
    # Corresponding source: the assembled tree minus build output.
    tar_path = os.path.join(out, base + "-source.tar.gz")
    with tarfile.open(tar_path, "w:gz") as tar:
        for dirpath, dirnames, filenames in os.walk(tree):
            dirnames[:] = [d for d in dirnames if d not in ("build", "host", ".git")]
            for f in filenames:
                full = os.path.join(dirpath, f)
                tar.add(full, arcname=os.path.join(base + "-source", os.path.relpath(full, tree)))
    prov = {
        "package": os.path.basename(dst), "sha256": digest, "bytes": os.path.getsize(dst),
        "device": device, "firmware_name": name, "stage": a.stage, "pair": f"{NAMES[a.stage][0]} (SMK-37) + {NAMES[a.stage][1]} (FM-1)",
        "release_version": version, "update_identity": ("FM-1_" if a.board == "fm1" else f"SMK-37 {a.model.capitalize()}_") + str(8000000 + 50 + a.stage), "model": a.model if a.board == "smk37" else None,
        "vendor": vendor, "vendor_commit": vendor_commit(vendor),
        "repository_commit": git("rev-parse", "HEAD"), "built_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source_archive": os.path.basename(tar_path), "source_sha256": sha256(tar_path),
        "restore_stock": "Use the matching workbench's Restore factory button or M-VAVE's updater with the official package.",
    }
    with open(os.path.join(out, base + ".provenance.json"), "w", encoding="utf-8") as fh:
        json.dump(prov, fh, indent=2)
    print(f"collect: {os.path.relpath(dst, ROOT)} sha256 {digest} ({prov['bytes']} bytes)")

if __name__ == "__main__":
    main()
