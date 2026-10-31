#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Build learn_lessons.c and lessons/*.syx from lessons/*.json.

Lesson JSON schema (one file per lesson, sorted by file name):
{
  "title": "First Sounds",           # <= 20 characters, shown on the device
  "stage": 1,                        # the stage firmware this lesson first appears in
  "unlocks": ["stage2"],             # optional: stage2 | stage3 | stage4 | swing | fill
  "steps": [
    {"type": "text", "prompt": "PRESS ANY KEY TO START"},
    {"type": "echo", "count": 5, "target": 60, "key": "C major", "degrees": [1,2,3],
     "prompt": "COPY THE NOTE YOU HEAR"}
  ]
}
Step types: text, echo, scale, chord, progression (page 1), rhythm, groove (page 2),
reftake (pages 2 and 3: a melody take), song (page 4). A lesson's stage is its page.
"key" is "<root> <scale>", scale in major | minor | penta_maj | penta_min | blues.
"seventh": true asks for seventh chords. "degrees" lists allowed degrees (echo, chord)
or the progression order (progression). "pattern" is a 16-char drum pattern (rhythm).
"lanes": {"kick": [hits, shift], ...} the groove to match (kick, snare, hat, perc).
"notes": the notes a take needs (reftake). "order": the sections to play (song):
intro verse pre chorus bridge break outro free.
"prompt_fm1": the prompt on the FM-1 when it differs (no pads, other button names);
the SMK-37's is "prompt". Titles are shown in capitals (roman numerals keep their case).
"""
import json, os, re, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LESSONS = os.path.join(ROOT, "lessons")
OUT_C = os.path.join(ROOT, "learn", "lessons.c")
OUT_SYX = os.path.join(ROOT, "build", "lessons")

TYPES = {"echo": 1, "chord": 2, "progression": 3, "scale": 4, "rhythm": 5, "groove": 6, "reftake": 7, "song": 8, "text": 9}
SCALES = {"major": 0, "minor": 1, "penta_maj": 2, "penta_min": 3, "blues": 4}
NOTES = {"C": 0, "C#": 1, "DB": 1, "D": 2, "D#": 3, "EB": 3, "E": 4, "F": 5, "F#": 6, "GB": 6, "G": 7, "G#": 8, "AB": 8, "A": 9, "A#": 10, "BB": 10, "B": 11}
UNLOCKS = {"swing": 1, "fill": 2, "stage2": 4, "stage3": 8, "stage4": 16}
LANES = {"kick": 0, "snare": 1, "hat": 2, "perc": 3}
SECTIONS = ["intro", "verse", "pre", "chorus", "bridge", "break", "outro", "free"]
PAGE_MAX = {1: 31, 2: 7, 3: 7, 4: 7}   # the progress record keeps 5 bits for page 1, 3 for the others
ROMAN = re.compile(r"^(i|ii|iii|iv|v|vi|vii)[o7]*$", re.I)

def title_caps(t):
    return " ".join(w if ROMAN.match(w) else w.upper() for w in t.split(" "))

def check_prompt(f, i, prompt):
    if len(prompt) > 29:
        sys.exit(f"{f} step {i}: prompt longer than 29 characters (one screen line): {prompt!r}")
    if not re.fullmatch(r"[ -~]*", prompt):
        sys.exit(f"{f} step {i}: prompt must be printable ASCII")

def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

def parse_key(key):
    root, scale = key.split()
    r = NOTES[root.upper()]
    sc = SCALES[scale.lower()]
    minor = 1 if sc in (1, 3, 4) else 0
    return r, sc, minor

def main():
    files = sorted(glob.glob(os.path.join(LESSONS, "*.json")))
    if not files:
        sys.exit("no lessons found")
    lessons = []
    for f in files:
        with open(f, encoding="utf-8") as fh:
            d = json.load(fh)
        if len(d["title"]) > 20:
            sys.exit(f"{f}: title longer than 20 characters")
        d["title"] = title_caps(d["title"])
        steps = []
        for i, s in enumerate(d["steps"]):
            t = TYPES[s["type"]]
            prompt = s.get("prompt", "")
            prompt_fm1 = s.get("prompt_fm1", prompt)
            check_prompt(f, i, prompt); check_prompt(f, i, prompt_fm1)
            r, sc, minor = parse_key(s.get("key", "C major"))
            data = [0] * 8
            if "degrees" in s:
                degs = s["degrees"][:8]
                if any(not 1 <= x <= 7 for x in degs):
                    sys.exit(f"{f} step {i}: degrees must be 1..7")
                data[:len(degs)] = degs
            if "pattern" in s:
                pat = s["pattern"].ljust(16, ".")[:16]
                mask = sum(1 << k for k, ch in enumerate(pat) if ch != ".")
                data[0] = mask & 127; data[1] = (mask >> 7) & 127; data[2] = (mask >> 14) & 3
            if "lanes" in s:
                for name, (hits, shift) in s["lanes"].items():
                    if not 1 <= hits <= 16 or not 0 <= shift < 16:
                        sys.exit(f"{f} step {i}: lane {name}: hits 1..16, shift 0..15")
                    data[2 * LANES[name]] = hits; data[2 * LANES[name] + 1] = shift
            if "order" in s:
                if not 1 <= len(s["order"]) <= 8:
                    sys.exit(f"{f} step {i}: order: 1..8 sections")
                for k, name in enumerate(s["order"]):
                    data[k] = SECTIONS.index(name) + 1
            if "notes" in s:
                data[0] = int(s["notes"])
            steps.append(dict(type=t, count=int(s.get("count", 1)), tempo=int(s.get("tempo", 90)),
                              target=int(s.get("target", 80)), root=r, scale=sc, minor=minor,
                              seventh=1 if s.get("seventh") else 0, data=data, prompt=prompt, prompt_fm1=prompt_fm1))
        unl = 0
        for u in d.get("unlocks", []):
            unl |= UNLOCKS[u]
        stage = int(d.get("stage", 1))
        if stage not in PAGE_MAX:
            sys.exit(f"{f}: stage (page) 1..4")
        if sum(1 for l in lessons if l["stage"] == stage) >= PAGE_MAX[stage]:
            sys.exit(f"{f}: page {stage} has room for {PAGE_MAX[stage]} lessons (the progress record)")
        lessons.append(dict(title=d["title"], stage=stage, number=len(lessons) + 1,
                            unlocks=unl, steps=steps, file=os.path.basename(f)))

    lines = ["/* SPDX-License-Identifier: GPL-3.0-only",
             " * GENERATED by lessons/make_lessons.py from the lessons/ JSON files. Do not edit; edit the JSON and re-run. */",
             "/* (included by learn/learn.c; everything is static: SLOOP is one translation unit) */",
             "#if LEARN_PADS                     /* LP(smk, fm1): the prompt for this board */",
             "#define LP(smk, fm1) smk", "#else", "#define LP(smk, fm1) fm1", "#endif", ""]
    for n, l in enumerate(lessons):
        lines.append(f"static const struct lesson_step lsteps_{n}[] = {{")
        for s in l["steps"]:
            d = ",".join(str(x) for x in s["data"])
            pr = c_str(s['prompt']) if s['prompt'] == s['prompt_fm1'] else f"LP({c_str(s['prompt'])}, {c_str(s['prompt_fm1'])})"
            lines.append(f"    {{{s['type']},{s['count']},{s['tempo']},{s['target']},{s['root']},{s['scale']},{s['minor']},{s['seventh']},{{{d}}},{pr}}},")
        lines.append("};")
    lines.append("static const struct lesson lessons[] = {")
    for n, l in enumerate(lessons):
        lines.append(f"    {{{c_str(l['title'])},{l['stage']},{l['number']},{len(l['steps'])},{l['unlocks']},lsteps_{n}}},")
    lines.append("};")
    lines.append(f"#define lesson_count {len(lessons)}u")
    os.makedirs(os.path.dirname(OUT_C), exist_ok=True)
    with open(OUT_C, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    # SysEx selector files: F0 7D 'L' 'N' 01 03 <stage> 1D <index+1> F7 starts the lesson on either box.
    os.makedirs(OUT_SYX, exist_ok=True)
    manifest = []
    for n, l in enumerate(lessons):
        frame = bytes([0xF0, 0x7D, ord('L'), ord('N'), 1, 3, l["stage"], 29, n + 1, 0xF7])
        name = f"{n + 1:02d}-{re.sub(r'[^a-z0-9]+', '-', l['title'].lower()).strip('-')}.syx"
        with open(os.path.join(OUT_SYX, name), "wb") as fh:
            fh.write(frame)
        manifest.append(dict(index=n + 1, title=l["title"], stage=l["stage"], steps=len(l["steps"]), file=name, source=l["file"]))
    with open(os.path.join(OUT_SYX, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
    print(f"{len(lessons)} lessons -> {os.path.relpath(OUT_C, ROOT)} and {os.path.relpath(OUT_SYX, ROOT)}/")

if __name__ == "__main__":
    main()
