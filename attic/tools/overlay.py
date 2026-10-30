#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Assemble a buildable source tree for one (board, stage) pair.

    tools/overlay.py --board fm1   --stage 1 [--out build/trees/fm1-s1]
    tools/overlay.py --board smk37 --stage 3 [--out build/trees/smk37-s3]
    tools/overlay.py --all

It copies the pinned vendor tree (vendor/fm1_open or vendor/smk37_open),
adds overlay/app/learn/, writes app/learn/learn_config.h and inserts five
small hooks into the stock app with anchored text edits. Every anchor must
be found exactly once, otherwise the tool stops: a silent mis-patch is how
a firmware ends up doing something nobody reviewed.

The stock update identity is kept (FM-1_8000NNN / SMK-37 Pro_8000NNN):
the loader only installs a package built for the physical model, and the
version number encodes the learn stage (V51..V54) so the workbench can
tell the builds apart.
"""
import argparse, os, shutil, sys, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARDS = {
    "fm1": dict(vendor="fm1_open", define="LEARN_BOARD_FM1", product="FM-1_"),
    "smk37": dict(vendor="smk37_open", define="LEARN_BOARD_SMK37", product=None),
}
STAGE_NAMES = {1: "Lantern/Ember", 2: "Pulse/Root", 3: "Shadow/Tape", 4: "Stage/Scribe"}
MODELS = ("elite", "pro")  # SMK-37 model; the app source is shared, the package identity differs

def _read(path):
    """Read as text with the file's own line endings preserved (newline="")."""
    with open(path, encoding="utf-8", newline="") as fh:
        return fh.read()

def _write(path, src):
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(src)

def insert_after(path, anchor, text, count=1):
    src = _read(path)
    if "\r\n" in src:
        anchor = anchor.replace("\n", "\r\n"); text = text.replace("\n", "\r\n")
    n = src.count(anchor)
    if n != count:
        sys.exit(f"overlay: anchor found {n} times (expected {count}) in {path}:\n  {anchor!r}")
    if text in src:
        sys.exit(f"overlay: hook already present in {path}")
    src = src.replace(anchor, anchor + text)
    _write(path, src)

def replace_once(path, pattern, repl):
    src = _read(path)
    cr = "\r" if "\r\n" in src else ""
    new, n = re.subn(pattern, repl + cr, src, flags=re.M)
    if n != 1:
        sys.exit(f"overlay: pattern matched {n} times (expected 1) in {path}: {pattern}")
    _write(path, new)

def patch_build_size(out):
    """The learn layer is control-task UI code; build it at -Oz like the
    vendor's own UI and storage units so the image fits the fixed Flash
    layout (453,921 bytes for the application on the SMK-37; the first
    Stage 1 build at -O2 was 21 KB over). One edit: app/learn/* joins the
    vendor's existing -Oz rule."""
    bp = os.path.join(out, "sdk", "Build.ps1")
    src = _read(bp)
    anchor = ")) { $options += '-Oz' }"
    n = src.count(anchor)
    if n != 1:
        sys.exit(f"overlay: -Oz anchor found {n}x in Build.ps1")
    src = src.replace(anchor, ") -or $source.StartsWith('app/learn/')) { $options += '-Oz' }")
    _write(bp, src)

def patch_build_for_linux(out):
    """Let the vendor Build.ps1 run under pwsh on Linux with JieLi's Linux
    toolchain (jieli-linux-toolchains-*, the same clang 4.0.1 for pi32v2).
    Following the Felucca-family firmwares, which build this chip on Ubuntu:
      * tools are reached through a shim bin/ (tools/ci/get_linux_toolchain.sh
        makes it: clang, pi32v2-ld -> pi32v2/bin/ld, llvm-objcopy -> common/bin/objcopy ...);
      * the gold LTO plugin is common/bin/LLVMgold.so in the Linux tarball, so
        the vendor's LTO compile and link recipe is kept verbatim; only the
        plugin file name changes. All vendor post-link checks
        (Test-Firmware: SRAM placement, heap, callbacks, sections) stay on;
      * the Windows-binary hash loop becomes a presence check: the tarball is
        pinned by SHA-256 in CI and the compiler version is verified there;
      * three '\\' path assumptions use the platform separator.
    Nothing else in Build.ps1 changes; the C# packager runs under pwsh."""
    import re as _re
    bp = os.path.join(out, "sdk", "Build.ps1")
    src = _read(bp)
    nl = "\r\n" if "\r\n" in src else "\n"
    def must(pattern, repl, count=None, regex=False):
        nonlocal src
        if regex:
            new, n = _re.subn(pattern, repl, src)
        else:
            n = src.count(pattern); new = src.replace(pattern, repl)
        if n == 0 or (count is not None and n != count):
            sys.exit(f"overlay: linux patch anchor found {n}x in Build.ps1: {pattern!r}")
        src = new
    # 1. tool names
    for a, b in (("'clang.exe'", "'clang'"), ("'llvm-nm.exe'", "'llvm-nm'"), ("'llvm-objcopy.exe'", "'llvm-objcopy'"),
                 ("'pi32v2-ld.exe'", "'pi32v2-ld'")):
        must(a, b)
    must("containing bin/clang.exe", "containing bin/clang")
    # 2. LTO plugin: the Linux tarball ships common/bin/LLVMgold.so, so the vendor's
    #    LTO link recipe is kept verbatim; only the plugin's file name changes.
    must("'LLVMgold.dll'", "'LLVMgold.so'", 1)
    # 3. toolchain check
    start = src.find("if ($manifest.external_toolchain) {")
    end = src.find("$synth = Get-Content", start)
    if start < 0 or end < 0:
        sys.exit("overlay: linux patch: external_toolchain block not found")
    block = ("if ($manifest.external_toolchain) {" + nl +
             "    foreach ($required in @('bin/clang','bin/pi32v2-ld','bin/LLVMgold.so','bin/llvm-nm','bin/llvm-objcopy')) {" + nl +
             "        if (!(Test-Path -LiteralPath (Join-Path $Toolchain $required))) { throw \"Linux toolchain is missing $required\" }" + nl +
             "    }" + nl + "}" + nl)
    src = src[:start] + block + src[end:]
    src = _re.sub(r"[ \t]*\$synth\.pi32_compiler_sha256 -cne [^\r\n]*\r?\n", "", src)  # SMK script only
    must("throw 'Install JieLi Windows toolchain 2.5.3", "throw 'Install the JieLi Linux toolchain")
    # 4. path separators
    must("StartsWith($root + '\\build\\', [StringComparison]::OrdinalIgnoreCase)",
         "StartsWith((Join-Path $root 'build') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)", 1)
    must("$full.StartsWith($sdk + '\\', [StringComparison]::OrdinalIgnoreCase)",
         "$full.StartsWith($sdk + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)", 1)
    # 5. The manifest hashes Build.ps1 itself; this patched copy cannot match it.
    #    Every other SDK file (headers, libraries, packer tools, stock packages) is still verified.
    must("(Hash $full) -ne $file.Value) { throw \"SDK payload changed: $($file.Name)\" }",
         "($file.Name -ne 'sdk/Build.ps1' -and (Hash $full) -ne $file.Value)) { throw \"SDK payload changed: $($file.Name)\" }", 1)
    must("(Join-Path $root 'app/synth'))+'\\')", "(Join-Path $root 'app/synth'))+[IO.Path]::DirectorySeparatorChar)", 1)
    must(".Replace('\\','/')", ".Replace([string][IO.Path]::DirectorySeparatorChar,'/')")
    _write(bp, src)
    with open(os.path.join(out, "LINUX-BUILD-PATCH.txt"), "w", encoding="utf-8") as fh:
        fh.write("sdk/Build.ps1 patched for Linux: tool names via shim bin/, LLVMgold.so for the LTO plugin, toolchain presence check instead of Windows binary hashes, platform path separators, the manifest self-hash of Build.ps1 skipped (all other SDK files still verified).\n")

def assemble(board, stage, out, model="elite", host_os="windows"):
    b = BOARDS[board]
    vendor = os.path.join(ROOT, "vendor", b["vendor"])
    if not os.path.isfile(os.path.join(vendor, "app", "screen.c")):
        sys.exit(f"overlay: vendor tree missing at {vendor}. Run: git submodule update --init")
    if os.path.isdir(out):
        shutil.rmtree(out)
    shutil.copytree(vendor, out, ignore=shutil.ignore_patterns(".git", "build"))
    learn_dst = os.path.join(out, "app", "learn")
    shutil.copytree(os.path.join(ROOT, "overlay", "app", "learn"), learn_dst)
    if not os.path.isfile(os.path.join(learn_dst, "learn_lessons.c")):
        sys.exit("overlay: run tools/make_lessons.py first (learn_lessons.c is generated)")
    # Stage modules above LEARN_STAGE are not compiled in: delete them from the tree.
    for s in range(stage + 1, 5):
        f = os.path.join(learn_dst, f"learn_stage{s}.c")
        if os.path.exists(f):
            os.remove(f)
    with open(os.path.join(learn_dst, "learn_config.h"), "w", encoding="utf-8") as fh:
        fh.write("/* GENERATED by tools/overlay.py */\n#ifndef LEARN_CONFIG_H\n#define LEARN_CONFIG_H\n"
                 f"#define LEARN_STAGE {stage}\n#define {b['define']} 1\n"
                 f"#define LEARN_STAGE_NAME \"{STAGE_NAMES[stage]}\"\n#endif\n")
    app = os.path.join(out, "app")
    # 1. UI loop and init.
    insert_after(os.path.join(app, "screen.c"), "void groove_screen_control(void)\n{\n",
                 "    { extern int learn_control(void); if (learn_control()) return; }\n")
    insert_after(os.path.join(app, "screen.c"), "void groove_screen_init(void)\n{\n",
                 "    { extern void learn_init(void); learn_init(); }\n")
    # 2. SysEx observer (routing task).
    insert_after(os.path.join(app, "transfer.c"), "void fm1_app_sysex(const uint8_t *m, size_t n)\n{\n",
                 "    { extern int learn_sysex(const uint8_t *, size_t); if (learn_sysex(m, n)) return; }\n")
    # 3. MIDI-in observer (routing task).
    insert_after(os.path.join(app, "midi_router.c"),
                 "void fm1_app_midi_received(unsigned source, const uint8_t *m, size_t n)\n{\n    if (source >= 3 || !m || !n || n > 3) return;\n",
                 "    { extern void learn_midi_observe(unsigned, const uint8_t *, size_t); learn_midi_observe(source, m, n); }\n")
    if board == "smk37":
        # 4. Panel events and pad colours.
        insert_after(os.path.join(app, "smk37_controls.c"), "void smk37_control_event(unsigned kind,unsigned id,int value)\n{\n",
                     "    { extern int learn_smk37_event(unsigned, unsigned, int); if (learn_smk37_event(kind, id, value)) return; }\n")
        insert_after(os.path.join(app, "smk37_controls.c"), "uint32_t smk37_pad_color(unsigned pad,unsigned lit)\n{\n",
                     "    { extern int learn_pad_color(unsigned, uint32_t *); uint32_t c; if (learn_pad_color(pad, &c)) return c; }\n")
    # 5. Version identity: V5<stage>, product number 80000 5<stage>.
    version = f"V5{stage}"
    number = 8000000 + 50 + stage
    vh = os.path.join(app, "version.h")
    replace_once(vh, r'^#define FM1_RELEASE_VERSION "V[0-9]+"[ \t]*\r?$', f'#define FM1_RELEASE_VERSION "{version}"')
    if board == "fm1":
        replace_once(vh, r'^#define FM1_UPDATE_PRODUCT "FM-1_8[0-9]{6}"[ \t]*\r?$', f'#define FM1_UPDATE_PRODUCT "FM-1_{number}"')
    else:
        replace_once(vh, r'^#define FM1_UPDATE_PRODUCT_PRO "SMK-37 Pro_8[0-9]{6}"[ \t]*\r?$', f'#define FM1_UPDATE_PRODUCT_PRO "SMK-37 Pro_{number}"')
        replace_once(vh, r'^#define FM1_UPDATE_PRODUCT_ELITE "SMK-37 Elite_8[0-9]{6}"[ \t]*\r?$', f'#define FM1_UPDATE_PRODUCT_ELITE "SMK-37 Elite_{number}"')
    patch_build_size(out)
    if host_os == "linux":
        patch_build_for_linux(out)
    with open(os.path.join(out, "LEARN-BUILD.txt"), "w", encoding="utf-8") as fh:
        fh.write(f"board={board}\nstage={stage}\nname={STAGE_NAMES[stage]}\nversion={version}\nmodel={model if board == 'smk37' else '-'}\nhost_os={host_os}\n")
    print(f"overlay: {board} stage {stage} -> {os.path.relpath(out, ROOT)}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", choices=BOARDS.keys())
    ap.add_argument("--stage", type=int, choices=[1, 2, 3, 4])
    ap.add_argument("--out")
    ap.add_argument("--model", choices=MODELS, default="elite", help="SMK-37 model (default elite)")
    ap.add_argument("--host-os", choices=("windows", "linux"), default="windows", help="where the vendor Build.ps1 will run")
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args()
    if a.all:
        for board in BOARDS:
            for stage in (1, 2, 3, 4):
                assemble(board, stage, os.path.join(ROOT, "build", "trees", f"{board}-s{stage}"), a.model, a.host_os)
        return
    if not a.board or not a.stage:
        ap.error("--board and --stage, or --all")
    assemble(a.board, a.stage, a.out or os.path.join(ROOT, "build", "trees", f"{a.board}-s{a.stage}"), a.model, a.host_os)

if __name__ == "__main__":
    main()
