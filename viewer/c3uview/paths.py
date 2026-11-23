"""
paths.py - where the viewer's own files are.

Run from source, they sit next to this package. Packaged by PyInstaller, they are
unpacked into a folder that sys._MEIPASS names.
"""
import os
import sys


def base():
    if getattr(sys, "frozen", False):
        return sys._MEIPASS
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # viewer/


def resource(*parts):
    return os.path.join(base(), *parts)


def examples_dir():
    """The example ladder programs, for the simulator: bundled with the viewer, or the project's own."""
    for path in (resource("examples"), os.path.join(base(), os.pardir, "examples", "ladder")):
        if os.path.isdir(path):
            return os.path.normpath(path)
    return None


def firmware_image():
    """The C3U-Metal image to flash: bundled with the viewer, or the project's own build when run from source."""
    for path in (resource("firmware", "c3u-metal.bin"),
                 os.path.join(base(), os.pardir, "build", "c3u-metal.bin")):
        if os.path.isfile(path):
            return os.path.normpath(path)
    return None
