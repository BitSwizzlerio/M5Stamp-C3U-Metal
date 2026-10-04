#!/usr/bin/env python3
"""
build.py - make the single-file C3U Viewer program for this computer's system.

    python build.py

Run it on Windows to make dist/c3u-viewer.exe, and on Linux to make
dist/c3u-viewer. PyInstaller can't build for another system, so each one is
built on its own kind of computer (Linux works under WSL).

It needs:
- the Python packages in requirements.txt, and PyInstaller;
- build/c3u-metal.bin in the project, from "cmake --build build", which the
  viewer carries so that it can put C3U-Metal back on a board.

It downloads Espressif's OpenOCD for this system, checks it against the release's
published checksums, and puts it inside the program with its licence.
"""
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
CACHE = os.path.join(HERE, ".cache")
STAGE = os.path.join(HERE, ".stage")

OPENOCD_VERSION = "v0.12.0-esp32-20260304"
OPENOCD_RELEASE = f"https://github.com/espressif/openocd-esp32/releases/download/{OPENOCD_VERSION}/"
OPENOCD_SOURCE = f"https://github.com/espressif/openocd-esp32/tree/{OPENOCD_VERSION}"
OPENOCD_COPYING = f"https://raw.githubusercontent.com/espressif/openocd-esp32/{OPENOCD_VERSION}/COPYING"


def system():
    machine = platform.machine().lower()
    if sys.platform == "win32":
        return "win64"
    if sys.platform.startswith("linux"):
        return {"x86_64": "linux-amd64", "amd64": "linux-amd64", "aarch64": "linux-arm64"}.get(machine)
    return None


def fetch(url, name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name)
    if not os.path.isfile(path):
        print(f"downloading {url}")
        with urllib.request.urlopen(url, timeout=120) as r, open(path, "wb") as f:
            shutil.copyfileobj(r, f)
    return path


def openocd(target):
    """Download, check and unpack OpenOCD into .stage/openocd: bin/ and share/openocd/."""
    suffix = "zip" if target == "win64" else "tar.gz"
    name = f"openocd-esp32-{target}-{OPENOCD_VERSION[1:]}.{suffix}"
    archive = fetch(OPENOCD_RELEASE + name, name)
    sums = open(fetch(OPENOCD_RELEASE + f"openocd-esp32-{OPENOCD_VERSION}-checksum.sha256",
                      f"openocd-{OPENOCD_VERSION}.sha256")).read()
    expected = next((line.split()[0] for line in sums.splitlines()
                     if not line.startswith("#") and line.strip().endswith(name)), None)
    actual = hashlib.sha256(open(archive, "rb").read()).hexdigest()
    if expected != actual:
        sys.exit(f"{name}: checksum {actual} doesn't match the release's {expected}")
    print(f"{name}: checksum matches the release")

    out = os.path.join(STAGE, "openocd")
    unpack = os.path.join(STAGE, "unpack")
    shutil.rmtree(unpack, ignore_errors=True)
    if suffix == "zip":
        zipfile.ZipFile(archive).extractall(unpack)
    else:
        with tarfile.open(archive) as t:
            t.extractall(unpack, filter="tar") if hasattr(tarfile, "data_filter") else t.extractall(unpack)
    root = os.path.join(unpack, "openocd-esp32")
    shutil.copytree(os.path.join(root, "bin"), os.path.join(out, "bin"))
    shutil.copytree(os.path.join(root, "share", "openocd", "scripts"), os.path.join(out, "share", "openocd", "scripts"))
    shutil.copytree(os.path.join(root, "share", "openocd", "espressif"), os.path.join(out, "share", "openocd", "espressif"))
    shutil.rmtree(unpack)


def licences():
    """The licences of everything the program carries, and where to get OpenOCD's source."""
    out = os.path.join(STAGE, "licenses")
    os.makedirs(out, exist_ok=True)
    shutil.copy(os.path.join(PROJECT, "LICENSE"), os.path.join(out, "C3U-Metal-LICENSE.txt"))
    shutil.copy(fetch(OPENOCD_COPYING, f"openocd-{OPENOCD_VERSION}-COPYING"), os.path.join(out, "OpenOCD-COPYING.txt"))
    # COPYING points at these two for the licence itself.
    raw = OPENOCD_COPYING.rsplit("/", 1)[0]
    shutil.copy(fetch(raw + "/LICENSES/preferred/GPL-2.0", f"openocd-{OPENOCD_VERSION}-GPL-2.0"),
                os.path.join(out, "OpenOCD-GPL-2.0.txt"))
    shutil.copy(fetch(raw + "/LICENSES/license-rules.txt", f"openocd-{OPENOCD_VERSION}-license-rules"),
                os.path.join(out, "OpenOCD-license-rules.txt"))
    shutil.copy(os.path.join(HERE, "c3uview", "web", "vendor", "XTERM-LICENSE.txt"), os.path.join(out, "xterm.js-LICENSE.txt"))
    from importlib import metadata
    for package in ("esptool", "pyserial"):
        dist = metadata.distribution(package)
        files = [f for f in (dist.files or []) if "licen" in f.name.lower() or "copying" in f.name.lower()]
        if files:
            source = dist.locate_file(files[0])
        elif package == "pyserial":          # its wheel doesn't include the licence text
            source = fetch(f"https://raw.githubusercontent.com/pyserial/pyserial/v{dist.version}/LICENSE.txt",
                           f"pyserial-{dist.version}-LICENSE.txt")
        else:
            sys.exit(f"no licence file found for {package}")
        shutil.copy(source, os.path.join(out, f"{package}-LICENSE.txt"))
    with open(os.path.join(out, "THIRD-PARTY.txt"), "w", encoding="utf-8") as f:
        f.write("C3U Viewer carries these programs and libraries, unchanged:\n\n"
                f"- OpenOCD for Espressif chips, {OPENOCD_VERSION}: GPL-2.0. It runs as a separate\n"
                f"  program. Source: {OPENOCD_SOURCE}\n"
                "- esptool: GPL-2.0-or-later. Source: https://github.com/espressif/esptool\n"
                "- pyserial: BSD-3-Clause. Source: https://github.com/pyserial/pyserial\n"
                "- xterm.js and its fit addon: MIT. Source: https://github.com/xtermjs/xterm.js\n"
                "- Python and its standard library: PSF licence. Source: https://www.python.org\n\n"
                "C3U Viewer itself is part of C3U-Metal, MIT licensed:\n"
                "https://github.com/BitSwizzlerio/M5Stamp-C3U-Metal\n")


def firmware():
    image = os.path.join(PROJECT, "build", "c3u-metal.bin")
    if not os.path.isfile(image):
        sys.exit("build/c3u-metal.bin is missing: build C3U-Metal first (cmake --build build)")
    os.makedirs(os.path.join(STAGE, "firmware"), exist_ok=True)
    shutil.copy(image, os.path.join(STAGE, "firmware", "c3u-metal.bin"))


def main():
    target = system()
    if not target:
        sys.exit(f"don't know which OpenOCD to use on {sys.platform} {platform.machine()}")
    shutil.rmtree(STAGE, ignore_errors=True)
    os.makedirs(STAGE)
    openocd(target)
    licences()
    firmware()

    sep = ";" if os.name == "nt" else ":"
    data = [(os.path.join(HERE, "c3uview", "web"), "c3uview/web"),
            (os.path.join(STAGE, "openocd"), "openocd"),
            (os.path.join(STAGE, "firmware"), "firmware"),
            (os.path.join(STAGE, "licenses"), "licenses")]
    args = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile",
            "--name", "c3u-viewer", "--distpath", os.path.join(HERE, "dist"),
            "--workpath", os.path.join(HERE, ".stage", "work"), "--specpath", os.path.join(HERE, ".stage"),
            "--collect-data", "esptool"]
    for src, dest in data:
        args += ["--add-data", f"{src}{sep}{dest}"]
    args.append(os.path.join(HERE, "c3u_viewer.py"))
    subprocess.run(args, check=True, cwd=HERE)

    # The licences also go next to the program, where people can read them.
    shutil.copytree(os.path.join(STAGE, "licenses"), os.path.join(HERE, "dist", "licenses"), dirs_exist_ok=True)
    exe = os.path.join(HERE, "dist", "c3u-viewer" + (".exe" if os.name == "nt" else ""))
    print(f"\nbuilt {exe} ({os.path.getsize(exe) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
