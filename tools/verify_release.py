#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Verify a dist/ directory (or a downloaded release): every .fwsc has a
matching .sha256 sidecar and provenance JSON, the digest matches, the file
is a plausible size for its board, and the name encodes board and stage.
Run before publishing and, as a user, after downloading."""
import glob, hashlib, json, os, re, sys

LIMITS = {"fm1": (400_000, 1_100_000), "smk37e": (400_000, 1_100_000), "smk37p": (400_000, 1_100_000)}

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "dist"
    pkgs = sorted(glob.glob(os.path.join(d, "*.fwsc")))
    if not pkgs:
        sys.exit(f"verify: no .fwsc in {d}")
    bad = 0
    for p in pkgs:
        name = os.path.basename(p)
        m = re.fullmatch(r"(lantern|ember|pulse|root|shadow|tape|stage|scribe)-stage([1-4])-(fm1|smk37e|smk37p)-V5([1-4])\.fwsc", name)
        if not m or m.group(2) != m.group(4):
            print(f"BAD NAME {name}"); bad += 1; continue
        board = m.group(3)
        side = p + ".sha256"
        prov = p[:-5] + ".provenance.json"
        if not os.path.isfile(side) or not os.path.isfile(prov):
            print(f"MISSING SIDECAR {name}"); bad += 1; continue
        expected = open(side, encoding="utf-8").read().split()[0]
        actual = sha256(p)
        if expected != actual:
            print(f"SHA MISMATCH {name}: sidecar {expected} file {actual}"); bad += 1
        pj = json.load(open(prov, encoding="utf-8"))
        if pj.get("sha256") != actual:
            print(f"PROVENANCE MISMATCH {name}"); bad += 1
        size = os.path.getsize(p)
        lo, hi = LIMITS[board]
        if not lo <= size <= hi:
            print(f"SIZE OUT OF RANGE {name}: {size}"); bad += 1
        with open(p, "rb") as fh:
            head = fh.read(16)
        if len(head) < 16:
            print(f"TRUNCATED {name}"); bad += 1
        print(f"ok {name} {size} bytes sha256 {actual[:16]}...")
    print("VERIFY " + ("FAILED" if bad else "PASSED"))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
