#!/usr/bin/env python3
"""
build.py - build the ladder engine for a PC (the tests) and for WebAssembly (the simulator).

    python sim/build.py test             build and run the tests; compare their output with sim/expected.txt
    python sim/build.py test --update    ... and accept the new output as expected
    python sim/build.py test --sanitize  build with AddressSanitizer and UBSan (gcc or clang on Linux)
    python sim/build.py fuzz [seconds]   fuzz the assembler with libFuzzer (clang on Linux), 60 s unless told
    python sim/build.py wasm             build viewer/c3uview/web/plc.wasm with the WASI SDK
    python sim/build.py site             build the web simulator into sim/out/site, for GitHub Pages

The engine's sources (app/plc.c, plc_asm.c, plc_show.c, drivers/gpio.c) compile
unchanged: sim/host/esp32c3-regs.h points REG() at the simulated chip in sim/hw.c.

Compilers: "test" uses $CC, or gcc; "fuzz" uses $CC, or clang. "wasm" uses $WASI_SDK_PATH/bin/clang, or the
WASI SDK in C:/wasi-sdk (Windows) or /opt/wasi-sdk.
"""
import difflib
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ENGINE = ["app/plc.c", "app/plc_asm.c", "app/plc_show.c", "drivers/gpio.c", "sim/hw.c"]
INCLUDES = ["sim/host", "sim", "app", "board", "drivers", "chip"]
FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-missing-field-initializers"]
OUT = os.path.join(HERE, "out")
WEB = os.path.join(ROOT, "viewer", "c3uview", "web")
WASM = os.path.join(WEB, "plc.wasm")                # the viewer serves it too: its Simulate button


def run(cmd, **kw):
    """Run a compiler, with its own folder first on PATH: on Windows, another mingw64 on PATH
    (Git for Windows has one) can otherwise lend it the wrong DLLs, and it stops without a word."""
    print(" ".join(cmd) if len(" ".join(cmd)) < 200 else cmd[0] + " ...")
    exe = shutil.which(cmd[0]) or cmd[0]
    env = dict(os.environ, PATH=os.path.dirname(os.path.abspath(exe)) + os.pathsep + os.environ.get("PATH", ""))
    subprocess.run(cmd, check=True, cwd=ROOT, env=env, **kw)


def includes():
    return [f"-I{d}" for d in INCLUDES]


def test(update, sanitize):
    os.makedirs(OUT, exist_ok=True)
    cc = os.environ.get("CC", "gcc")
    exe = os.path.join(OUT, "test_ladder" + (".exe" if os.name == "nt" else ""))
    extra = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else []
    run([cc, *FLAGS, *extra, *includes(), *ENGINE, "sim/test_ladder.c", "-o", exe])
    compiler_dir = os.path.dirname(os.path.abspath(shutil.which(cc) or cc))
    env = dict(os.environ, PATH=compiler_dir + os.pathsep + os.environ.get("PATH", ""))
    result = subprocess.run([exe, "examples/ladder"], cwd=ROOT, capture_output=True, text=True, env=env)
    output = result.stdout.replace("\r\n", "\n")
    print(output.split("\n--- ")[0] if "--- " in output else output, end="")
    print("\n".join(l for l in output.splitlines() if l.startswith(("PASS", "FAIL")) or "passed," in l)[-2000:])
    if result.returncode != 0 or result.stderr.strip():
        print(result.stderr)
        sys.exit("the tests failed")
    expected_file = os.path.join(HERE, "expected.txt")
    if update or not os.path.exists(expected_file):
        open(expected_file, "w", newline="\n").write(output)
        print(f"wrote {os.path.relpath(expected_file, ROOT)}")
        return
    expected = open(expected_file).read().replace("\r\n", "\n")
    if output != expected:
        sys.stdout.writelines(difflib.unified_diff(expected.splitlines(True), output.splitlines(True), "expected.txt", "now"))
        sys.exit("the output differs from sim/expected.txt (if the change is right: build.py test --update)")
    print("the output matches sim/expected.txt")


def fuzz(seconds):
    """Run the libFuzzer target (sim/fuzz_asm.c) for a while, starting from the example programs."""
    cc = os.environ.get("CC", "clang")
    exe = os.path.join(OUT, "fuzz_asm")
    corpus = os.path.join(OUT, "corpus")
    os.makedirs(corpus, exist_ok=True)
    for name in glob.glob(os.path.join(ROOT, "examples", "ladder", "*.il")):
        shutil.copy(name, corpus)
    run([cc, *FLAGS, "-g", "-fsanitize=fuzzer,address,undefined", "-fno-sanitize-recover=all", *includes(),
         *ENGINE, "sim/fuzz_asm.c", "-o", exe])
    run([exe, corpus, f"-dict={os.path.join(HERE, 'fuzz.dict')}", f"-max_total_time={seconds}", "-max_len=4095",
         f"-artifact_prefix={OUT}{os.sep}"])


def wasi_clang():
    candidates = [os.environ.get("WASI_SDK_PATH", "")] + glob.glob("C:/wasi-sdk/wasi-sdk-*") + glob.glob("/opt/wasi-sdk*")
    for base in candidates:
        for name in ("clang.exe", "clang"):
            path = os.path.join(base, "bin", name)
            if base and os.path.isfile(path):
                return path
    sys.exit("The WASI SDK wasn't found: set WASI_SDK_PATH to where it is unpacked")


def wasm():
    target = WASM
    exports = ["sim_api_version", "sim_image", "sim_image_size", "sim_text", "sim_text_size", "sim_load", "sim_error",
               "sim_instructions", "sim_rungs", "sim_run", "sim_stop", "sim_step", "sim_advance_ms", "sim_set_input",
               "sim_pins", "sim_outputs", "sim_reset_board"]
    engine = [f for f in ENGINE if not f.endswith("plc_show.c")]     # the browser draws its own rungs
    run([wasi_clang(), "--target=wasm32-wasip1", "-mexec-model=reactor", *FLAGS, *includes(), *engine, "sim/wasm_api.c",
         "-Wl,--strip-all", *[f"-Wl,--export={e}" for e in exports], "-o", target])
    os.chmod(target, 0o644)         # the linker marks its output executable on Linux; git would see a change
    print(f"built {os.path.relpath(target, ROOT)}: {os.path.getsize(target) // 1024} KB")


def site():
    """Build the web simulator into sim/out/site, for GitHub Pages (.github/workflows/pages.yml):
    the viewer's own page and scripts, run by viewer/c3uview/web/sim.js instead of the viewer program."""
    if not os.path.exists(WASM):
        sys.exit("build the engine first: python sim/build.py wasm")
    sys.path.insert(0, os.path.join(ROOT, "viewer"))
    from c3uview import __version__, simpage

    out = os.path.join(OUT, "site")
    shutil.rmtree(out, ignore_errors=True)
    shutil.copytree(WEB, os.path.join(out, "static"), ignore=shutil.ignore_patterns("index.html"))
    shutil.copytree(os.path.join(ROOT, "examples", "ladder"), os.path.join(out, "examples"))
    html = open(os.path.join(WEB, "index.html"), encoding="utf-8").read()
    page = simpage.page(html, __version__, web=True)
    open(os.path.join(out, "index.html"), "w", encoding="utf-8", newline="\n").write(page)
    open(os.path.join(out, ".nojekyll"), "w").close()      # serve the files as they are
    print(f"built {os.path.relpath(out, ROOT)}; to try it:  python -m http.server -d {os.path.relpath(out, ROOT)}")


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args or args[0] not in ("test", "fuzz", "wasm", "site"):
        sys.exit(__doc__)
    if args[0] == "test":
        test("--update" in args, "--sanitize" in args)
    elif args[0] == "fuzz":
        fuzz(int(args[1]) if len(args) > 1 else 60)
    elif args[0] == "wasm":
        wasm()
    else:
        site()
