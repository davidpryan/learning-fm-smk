#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The install site: one installer page per device, built with SLOOP's own web installer (sloop/web/make_site.py),
and a front page that keeps the two devices apart.

  tools/make_site.py VERSION OUT_DIR sloop-smk37-elite-learn.fwsc felucca-learn.fwsc

  OUT_DIR/index.html                 the front page: which device, the checksums, the safety steps
  OUT_DIR/smk37-elite/webapp/installer/   SLOOP's installer page carrying the SMK-37 Elite package
  OUT_DIR/fm1/webapp/installer/           the same for the FM-1
  OUT_DIR/*.fwsc, *.sha256, provenance.json   the packages, their checksums, what built them
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICES = [  # (slug, name, identity prefix the package must carry, the stock package the user goes back with)
    ("smk37-elite", "M-VAVE SMK-37 Elite", "SMK-37 Elite_", "the official SMK-37 Elite package from m-vave.com, with M-UPGRADE"),
    ("fm1", "M-VAVE FM-1", "FM-1_", "the official FM-1 V15 package from m-vave.com (the installer page has a Return to the official firmware button)"),
]


def product_of(raw):                           # the identity: one marker byte after each of the first 20 blocks (fm1pkg.js)
    return "".join(chr((m - i - 1) & 0xFF) for i in range(20) if (m := raw[i * 0x30 + 0x2F]) != 0x7D)


def main(version, out, pkgs):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    prov = {"version": version, "commit": os.environ.get("GITHUB_SHA", subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()),
            "sloop_upstream": (ROOT / "sloop" / "UPSTREAM.txt").read_text().strip() if (ROOT / "sloop" / "UPSTREAM.txt").exists() else "?",
            "packages": []}
    for (slug, name, ident, back), pkg in zip(DEVICES, pkgs):
        pkg = Path(pkg)
        raw = pkg.read_bytes()
        product = product_of(raw)
        if not product.startswith(ident):
            raise SystemExit(f"{pkg}: identity {product!r} is not a {name} package")
        sha = hashlib.sha256(raw).hexdigest()
        subprocess.run([sys.executable, str(ROOT / "sloop" / "web" / "make_site.py"), str(pkg), version, str(out / slug)], check=True, cwd=ROOT / "sloop")
        shutil.copy(pkg, out / pkg.name)
        (out / (pkg.name + ".sha256")).write_text(f"{sha}  {pkg.name}\n")
        prov["packages"].append({"device": name, "file": pkg.name, "identity": product, "sha256": sha, "bytes": len(raw)})
        rows.append((slug, name, product, pkg.name, sha, back))
    (out / "provenance.json").write_text(json.dumps(prov, indent=2) + "\n")
    cards = "\n".join(f"""
<section class="card" id="{slug}">
  <h2>{name}</h2>
  <p class="id">Package identity <code>{product}</code> &middot; <code>{file}</code></p>
  <p class="sha">SHA-256 <code>{sha}</code></p>
  <p><a class="go" href="{slug}/webapp/installer/">Open the {name} installer</a></p>
  <p class="back">Back to stock: {back}.</p>
</section>""" for slug, name, product, file, sha, back in rows)
    html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Learn firmware {version}</title>
<style>
:root{{--bg:#111;--fg:#eee;--dim:#9a9a9a;--panel:#1c1c1c;--line:#333;--go:#2bd47a;--warn:#f2b134}}
body{{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif}}
main{{max-width:760px;margin:0 auto;padding:24px 16px}}
h1{{font-size:1.6rem;margin:0 0 4px}} h2{{margin:0 0 8px;font-size:1.25rem}}
.card{{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:16px;margin:16px 0}}
code{{font-family:ui-monospace,monospace;font-size:.9em;word-break:break-all}}
.go{{display:inline-block;background:var(--go);color:#000;padding:10px 16px;border-radius:8px;text-decoration:none;font-weight:600}}
.dim,.id,.sha,.back{{color:var(--dim);font-size:.92rem}}
.warn{{border-color:var(--warn)}} ol li{{margin:4px 0}}
</style></head><body><main>
<h1>Learn firmware {version}</h1>
<p class="dim">One firmware per device, four pages inside (Hear it, Loop it, Grow it, Finish it). Built on SLOOP.
GLO + HOME held two seconds switches between LEARN and plain SLOOP. GPL-3.0-only, no warranty.</p>
<section class="card warn">
  <h2>Before you install</h2>
  <ol>
    <li><b>Pick the page for the device in your hands.</b> Each installer checks the model and refuses the other device's package.</li>
    <li>Charge the device, then keep it on USB (a data cable, no hub) for the whole install. Desktop Chrome or Edge.</li>
    <li>If it runs SLOOP already, save a backup from the SLOOP web editor first (projects, presets, samples, settings are kept across installs, but a backup costs nothing).</li>
    <li>Check the SHA-256 on this page against the sidecar in the release.</li>
    <li>Do not touch the device until it restarts. If the install is cut off, the device stays in update mode: press INSTALL again.</li>
  </ol>
  <p class="dim">The learn layer never writes Flash itself (practice log, takes and sections live in RAM). Recovery: <a href="https://github.com/davidpryan/learning-fm-smk/blob/main/docs/RECOVERY.md">docs/RECOVERY.md</a>.</p>
</section>
{cards}
<p class="dim">Provenance: <a href="provenance.json">provenance.json</a> (commit, SLOOP upstream commit, checksums).
Source: <a href="https://github.com/davidpryan/learning-fm-smk">github.com/davidpryan/learning-fm-smk</a>.</p>
</main></body></html>
"""
    (out / "index.html").write_text(html, encoding="utf-8")
    print(f"site: {out}: " + ", ".join(f"{slug} ({product})" for slug, _, product, *_ in rows))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3:5])
