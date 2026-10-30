#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Build the static install site.

    tools/build_site.py --release <dir with release assets or empty> --out _site

Pages: index (the four pairs), one page per pair with both installers,
safety, recovery, lessons. Installers are the complete workbench HTML files
built by the vendor Build.ps1 (-Package); the SMK-37 one is unpacked with
the vendor's Build-Hosted.cjs into a static directory, the FM-1 one is
served as a single page. Without release assets the site still builds with
the documentation and "not released yet" placeholders."""
import argparse, glob, html, json, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAIRS = [
    (1, "Lantern", "Ember", "Hear it", "Notes and chords named and shown as you play."),
    (2, "Pulse", "Root", "Loop it", "A chord loop, generated drums and a bass that follows."),
    (3, "Shadow", "Tape", "Grow it", "Tracks defined relative to other tracks, and a four-track recorder."),
    (4, "Stage", "Scribe", "Finish it", "Sections, transitions and a whole song performed with your hands."),
]
CSS = """
:root{--bg:#0f1115;--fg:#e8e8ec;--dim:#9aa0a8;--card:#171a21;--line:#2a2f3a;--ok:#39d353;--warn:#f0b429;--bad:#ff5c5c;--link:#7cc4ff}
@media (prefers-color-scheme: light){:root{--bg:#fafafa;--fg:#16181d;--dim:#5a606a;--card:#fff;--line:#dcdfe6;--link:#0b63c4}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:860px;margin:0 auto;padding:24px 16px 64px}a{color:var(--link)}h1{font-size:28px;margin:8px 0 4px}h2{margin-top:32px;border-bottom:1px solid var(--line);padding-bottom:4px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin:12px 0}.grid{display:grid;grid-template-columns:1fr;gap:12px}@media(min-width:640px){.grid{grid-template-columns:1fr 1fr}}
.tag{display:inline-block;padding:2px 8px;border-radius:999px;font-size:12px;border:1px solid var(--line);color:var(--dim);margin-right:6px}
.warn{border-left:4px solid var(--warn);padding-left:12px}.danger{border-left:4px solid var(--bad);padding-left:12px}.ok{border-left:4px solid var(--ok);padding-left:12px}
code,pre{font:13px/1.4 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;background:rgba(127,127,127,.12);border-radius:4px}code{padding:1px 4px}pre{padding:10px;overflow:auto}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{border:1px solid var(--line);padding:6px 8px;text-align:left;vertical-align:top}th{background:rgba(127,127,127,.1)}
nav{font-size:14px;color:var(--dim)}nav a{margin-right:12px}.btn{display:inline-block;background:var(--link);color:#fff;padding:10px 16px;border-radius:8px;text-decoration:none;font-weight:600}
.btn.off{background:var(--line);color:var(--dim);pointer-events:none}.small{font-size:13px;color:var(--dim)}ol li,ul li{margin:4px 0}
"""

def md_to_html(text):
    """Small markdown subset: headings, paragraphs, lists, tables, code fences, inline code/bold/links."""
    out, lines, i = [], text.splitlines(), 0
    def inline(s):
        s = html.escape(s)
        s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
        s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
        s = re.sub(r"\*([^*]+)\*", r"<em>\1</em>", s)
        s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', s)
        return s
    while i < len(lines):
        l = lines[i]
        if l.startswith("```"):
            j = i + 1; buf = []
            while j < len(lines) and not lines[j].startswith("```"): buf.append(lines[j]); j += 1
            out.append("<pre>" + html.escape("\n".join(buf)) + "</pre>"); i = j + 1; continue
        m = re.match(r"^(#{1,4})\s+(.*)", l)
        if m: out.append(f"<h{len(m.group(1))}>{inline(m.group(2))}</h{len(m.group(1))}>"); i += 1; continue
        if l.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"): rows.append(lines[i]); i += 1
            cells = [[c.strip() for c in r.strip().strip("|").split("|")] for r in rows if not re.match(r"^\|[\s\-:|]+\|$", r)]
            if cells:
                out.append("<table><tr>" + "".join(f"<th>{inline(c)}</th>" for c in cells[0]) + "</tr>" +
                           "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in cells[1:]) + "</table>")
            continue
        if re.match(r"^\s*([-*]|\d+\.)\s+", l):
            ordered = bool(re.match(r"^\s*\d+\.", l)); items = []
            while i < len(lines) and re.match(r"^\s*([-*]|\d+\.)\s+", lines[i]):
                items.append(re.sub(r"^\s*([-*]|\d+\.)\s+", "", lines[i])); i += 1
            tag = "ol" if ordered else "ul"
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>"); continue
        if not l.strip(): i += 1; continue
        para = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "|", "```")) and not re.match(r"^\s*([-*]|\d+\.)\s+", lines[i]):
            para.append(lines[i]); i += 1
        out.append("<p>" + inline(" ".join(para)) + "</p>")
    return "\n".join(out)

def page(title, body, depth=0, nav_extra=""):
    up = "../" * depth
    nav = (f'<nav><a href="{up}index.html">Pairs</a><a href="{up}safety.html">Safety</a><a href="{up}recovery.html">Recovery</a>'
           f'<a href="{up}lessons.html">Lessons</a><a href="{up}architecture.html">How it works</a>{nav_extra}</nav>')
    return (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{html.escape(title)}</title><style>{CSS}</style></head><body><main>{nav}{body}'
            f'<p class="small">Unofficial community firmware built on M-VAVE\'s open firmware (GPLv3 with the SDK linking exception). Not affiliated with M-VAVE. Install at your own risk; read Safety first.</p></main></body></html>')

def read_doc(name):
    p = os.path.join(ROOT, "docs", name)
    return open(p, encoding="utf-8").read() if os.path.isfile(p) else f"# {name}\n\n(not written yet)"

def find_asset(release, base, suffix):
    p = os.path.join(release, base + suffix)
    return p if os.path.isfile(p) else None

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--release", default="release"); ap.add_argument("--out", default="_site")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    if os.path.isdir(out): shutil.rmtree(out)
    os.makedirs(os.path.join(out, "pairs")); os.makedirs(os.path.join(out, "install")); os.makedirs(os.path.join(out, "lessons"))
    release = os.path.abspath(a.release) if os.path.isdir(a.release) else None
    version = None
    cards = []
    for stage, smk, fm1, tagline, blurb in PAIRS:
        entries = []
        for board, name, device in (("smk37e", smk, "SMK-37 Elite"), ("smk37p", smk, "SMK-37 Pro"), ("fm1", fm1, "FM-1")):
            base = f"{name.lower()}-stage{stage}-{board}-V5{stage}"
            pkg = release and find_asset(release, base, ".fwsc")
            installer = release and find_asset(release, base, "-installer.html")
            sha = open(pkg + ".sha256", encoding="utf-8").read().split()[0] if pkg and os.path.isfile(pkg + ".sha256") else None
            prov = json.load(open(pkg[:-5] + ".provenance.json", encoding="utf-8")) if pkg and os.path.isfile(pkg[:-5] + ".provenance.json") else {}
            install_dir = os.path.join(out, "install", base)
            install_url = None
            if installer:
                os.makedirs(install_dir, exist_ok=True)
                if board.startswith("smk37"):
                    hosted = os.path.join(ROOT, "vendor", "smk37_open", "tools", "Build-Hosted.cjs")
                    if os.path.isfile(hosted) and shutil.which("node"):
                        subprocess.run(["node", hosted, installer, os.path.join(install_dir, "hosted")], check=True)
                        install_url = f"../install/{base}/hosted/index.html"
                if not install_url:
                    shutil.copyfile(installer, os.path.join(install_dir, "index.html")); install_url = f"../install/{base}/index.html"
                shutil.copyfile(installer, os.path.join(install_dir, base + "-installer.html"))
            if pkg:
                for suffix in (".fwsc", ".fwsc.sha256", "-source.tar.gz"):
                    src = os.path.join(release, base + suffix)
                    if os.path.isfile(src):
                        os.makedirs(install_dir, exist_ok=True); shutil.copyfile(src, os.path.join(install_dir, base + suffix))
                pj = os.path.join(release, base + ".provenance.json")
                if os.path.isfile(pj): shutil.copyfile(pj, os.path.join(install_dir, base + ".provenance.json"))
            entries.append(dict(board=board, name=name, device=device, base=base, pkg=bool(pkg), sha=sha, install=install_url, prov=prov, installer=bool(installer)))
            if prov.get("repository_commit"): version = prov.get("release_version", version)
        # pair page
        body = [f'<h1>Stage {stage}: {tagline}</h1><p><span class="tag">{smk} on the SMK-37 Elite (or Pro)</span><span class="tag">{fm1} on the FM-1</span></p><p>{html.escape(blurb)}</p>']
        body.append('<div class="card danger"><strong>Before you install anything:</strong> read <a href="../safety.html">Safety</a>. Back up your projects, voices and samples in the stock workbench, keep the device on USB power, and never unplug during a write. The way back to stock is on every installer page (Restore factory) and in <a href="../recovery.html">Recovery</a>.</div>')
        body.append('<div class="grid">')
        for e in entries:
            body.append(f'<div class="card"><h3>{e["name"]} <span class="small">for the {e["device"]}</span></h3>' + ('<p class="small">Pro and Elite packages carry different update identities; the loader only installs the one for the model you confirm.</p>' if e["board"].startswith("smk37") else ""))
            wb = "https://open.m-vave.com/fm1-workbench" if e["board"] == "fm1" else "https://open.m-vave.com/smk37-workbench"
            if e["install"]:
                body.append(f'<p><a class="btn" href="{e["install"]}">Open the {e["device"]} installer</a></p>')
                body.append('<p class="small">Desktop Chrome or Edge, USB cable, allow MIDI/SysEx. The page is M-VAVE\'s own workbench, built from this release\'s source, with this package embedded.</p>')
            elif e["pkg"]:
                body.append(f'<p><a class="btn" href="../install/{e["base"]}/{e["base"]}.fwsc" download>Download the {e["device"]} package</a> <a class="btn" href="{wb}" target="_blank" rel="noopener">Open M-VAVE\'s workbench</a></p>')
                body.append('<p class="small">In the workbench (desktop Chrome or Edge, USB cable): back up, then Device update and choose the downloaded .fwsc file. Restore factory is on the same page.</p>')
            else:
                body.append('<p><a class="btn off" href="#">Package: not released yet</a></p><p class="small">Builds appear here after the first tagged release.</p>')
            if e["pkg"]:
                body.append(f'<p class="small">Package: <a href="../install/{e["base"]}/{e["base"]}.fwsc">{e["base"]}.fwsc</a> · <a href="../install/{e["base"]}/{e["base"]}.fwsc.sha256">sha256</a> · <a href="../install/{e["base"]}/{e["base"]}-source.tar.gz">source</a> · <a href="../install/{e["base"]}/{e["base"]}.provenance.json">provenance</a></p>')
                body.append(f'<pre>sha256: {e["sha"]}</pre>')
                body.append(f'<p class="small">Update identity {html.escape(e["prov"].get("update_identity",""))}: the loader only accepts it on a physical {e["device"]}.</p>')
            body.append('</div>')
        body.append('</div>')
        body.append('<h2>Install, step by step</h2><ol>'
                    '<li>Download the package for the device you hold, check its SHA-256 against the sidecar, and open M-VAVE\'s workbench for that device in desktop Chrome or Edge. Plug the device in over USB and switch it on.</li>'
                    '<li>In the workbench, <strong>back up</strong> projects, voice groups A-D and samples (Projects &amp; sounds). Keep the files.</li>'
                    '<li>Choose <strong>Device update</strong>. Coming from stock firmware the workbench uses force mode and asks you to confirm the physical model. Confirm only the model you are holding.</li>'
                    '<li>Keep USB connected until the firmware and sample checks finish and the device restarts. The screen shows the stage page.</li>'
                    '<li>Pair the two boxes with a USB-C cable through a phone, tablet or host box (see How it works), or Bluetooth MIDI for Stages 1 and 4.</li>'
                    '<li>To go back: open the same installer and use <strong>Restore factory</strong>, or any other pair\'s installer to move stage.</li></ol>')
        body.append('<h2>Controls</h2>' + md_to_html(read_doc(f"stage{stage}.md")))
        with open(os.path.join(out, "pairs", f"stage{stage}.html"), "w", encoding="utf-8") as fh:
            fh.write(page(f"Stage {stage}: {smk} + {fm1}", "\n".join(body), depth=1))
        ready = all(e["pkg"] for e in entries if e["board"] != "smk37p")
        cards.append(f'<div class="card"><h3><a href="pairs/stage{stage}.html">Stage {stage}: {tagline}</a></h3><p><span class="tag">{smk} · SMK-37 Elite / Pro</span><span class="tag">{fm1} · FM-1</span></p><p>{html.escape(blurb)}</p><p class="small">{"Installers ready" if ready else "Not released yet"}</p></div>')
    # lessons
    lessons_dir = os.path.join(ROOT, "build", "lessons")
    lesson_rows = []
    if os.path.isfile(os.path.join(lessons_dir, "manifest.json")):
        for l in json.load(open(os.path.join(lessons_dir, "manifest.json"), encoding="utf-8")):
            shutil.copyfile(os.path.join(lessons_dir, l["file"]), os.path.join(out, "lessons", l["file"]))
            lesson_rows.append(f'<tr><td>{l["index"]}</td><td>{html.escape(l["title"])}</td><td>{l["stage"]}</td><td>{l["steps"]}</td><td><a href="lessons/{l["file"]}">{l["file"]}</a></td></tr>')
    lessons_body = ('<h1>Lesson packs</h1><p>Twenty lessons are compiled into every firmware and run from the device (ARP button, K6 to pick). '
                    'The .syx files below are selectors: send one from a DAW or a SysEx tool over USB and both boxes jump to that lesson. They are 10 bytes each and change nothing in Flash.</p>'
                    '<table><tr><th>#</th><th>Lesson</th><th>From stage</th><th>Steps</th><th>Selector</th></tr>' + "".join(lesson_rows) + '</table>')
    with open(os.path.join(out, "lessons.html"), "w", encoding="utf-8") as fh: fh.write(page("Lesson packs", lessons_body))
    for name, title in (("safety", "Safety"), ("recovery", "Recovery"), ("architecture", "How it works")):
        with open(os.path.join(out, f"{name}.html"), "w", encoding="utf-8") as fh:
            fh.write(page(title, md_to_html(read_doc(name.upper() + ".md"))))
    index = ('<h1>Learning firmware for the FM-1 and SMK-37 Elite</h1>'
             '<p>Four firmware pairs that teach a complete beginner to play and record, built on M-VAVE\'s official open firmware. Each stage keeps the previous stage\'s controls and adds one idea.</p>'
             + (f'<p class="small">Latest release: {html.escape(version)}</p>' if version else '<p class="small">No release yet. Pages show documentation only.</p>')
             + '<div class="grid">' + "".join(cards) + '</div>'
             '<div class="card warn"><strong>Three rules.</strong> Back up first. Install only the package for the model you hold. Keep USB power connected until the device restarts. Everything else is in <a href="safety.html">Safety</a>.</div>')
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as fh: fh.write(page("Learning firmware pairs", index))
    open(os.path.join(out, ".nojekyll"), "w").close()
    print(f"site: {out}")

if __name__ == "__main__":
    main()
