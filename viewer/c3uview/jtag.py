"""
jtag.py - read the chip's memory over JTAG, through OpenOCD, without stopping it.

The ESP32-C3's USB port is a serial port and a JTAG debugger at the same time.
OpenOCD drives the JTAG side. Its RISC-V debug support can read memory three
ways; this uses only "sysbus", in which the debug module reads the system bus
itself and the CPU never notices. (Measured: a running program does exactly the
same amount of work with or without 1200 reads a second.) The other two ways
need the CPU stopped, so they are switched off.
"""
import collections
import glob
import os
import shutil
import socket
import subprocess
import threading
import time

from .paths import resource

TCL_END = b"\x1a"


def find_openocd():
    """OpenOCD for Espressif chips: bundled with the viewer, named by C3U_OPENOCD, or installed."""
    exe = "openocd.exe" if os.name == "nt" else "openocd"
    candidates = [resource("openocd", "bin", exe), os.environ.get("C3U_OPENOCD", "")]
    if os.name == "nt":
        candidates += sorted(glob.glob(r"C:\Espressif\tools\openocd-esp32\*\openocd-esp32\bin\openocd.exe"),
                             reverse=True)
    candidates += sorted(glob.glob(os.path.expanduser("~/.espressif/tools/openocd-esp32/*/openocd-esp32/bin/openocd")),
                         reverse=True)
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    return shutil.which("openocd")


_job = None


def _tie_to_us(proc):
    """Make sure OpenOCD dies with the viewer, even if the viewer's window is just closed.
    Otherwise it would keep the JTAG interface, and the next start would find it busy."""
    global _job
    if os.name != "nt":
        return                              # on Linux, _linux_child() below does it
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    if _job is None:
        class Limits(ctypes.Structure):
            _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                        ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                        ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                        ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                        ("SchedulingClass", wintypes.DWORD)]

        class Extended(ctypes.Structure):
            _fields_ = [("Basic", Limits), ("Io", ctypes.c_uint64 * 6),
                        ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                        ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

        kernel32.CreateJobObjectW.restype = wintypes.HANDLE
        job = kernel32.CreateJobObjectW(None, None)
        info = Extended()
        info.Basic.LimitFlags = 0x2000      # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        kernel32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
        _job = job
    kernel32.AssignProcessToJobObject(wintypes.HANDLE(_job), wintypes.HANDLE(int(proc._handle)))


def _linux_child():
    """Run in the child before OpenOCD starts: ask Linux to stop it when the viewer goes."""
    import ctypes
    import signal
    ctypes.CDLL("libc.so.6", use_errno=True).prctl(1, signal.SIGTERM)    # PR_SET_PDEATHSIG


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# What OpenOCD's error messages usually mean, for someone who has never heard of JTAG.
HINTS = [
    ("LIBUSB_ERROR_NOT_SUPPORTED", "The USB driver for the board's JTAG interface isn't installed."),
    ("LIBUSB_ERROR_ACCESS", "No permission to use the board's JTAG interface (on Linux: install the udev rule)."),
    ("could not find or open device", "Board not found on JTAG. Is it plugged in? Is another debugger using it?"),
    ("LIBUSB_ERROR_BUSY", "Another program, probably a debugger, is using the board's JTAG interface."),
    ("unable to open", "Board not found on JTAG. Is it plugged in?"),
    ("scan chain", "JTAG found the USB interface but not the chip. Unplug the board and plug it back in."),
]


class Jtag:
    """Keeps one OpenOCD running and reads memory through its TCL port. Not thread-safe: one reader."""

    def __init__(self):
        self.openocd = find_openocd()
        self.proc = None
        self.sock = None
        self.log = collections.deque(maxlen=200)
        self.state = "stopped"          # stopped, starting, ready, error, paused
        self.problem = ""

    # ---- starting and stopping OpenOCD ------------------------------------------
    def start(self):
        if not self.openocd:
            self.state, self.problem = "error", "OpenOCD wasn't found, so the pin view can't work. The terminal still can."
            return False
        self.stop()
        self.state, self.problem = "starting", ""
        port = free_port()
        args = [self.openocd]
        scripts = os.path.join(os.path.dirname(os.path.dirname(self.openocd)), "share", "openocd", "scripts")
        if os.path.isdir(scripts):
            args += ["-s", scripts]
        args += ["-c", "gdb_port disabled", "-c", "telnet_port disabled", "-c", f"tcl_port {port}",
                 "-f", "board/esp32c3-builtin.cfg", "-c", "init", "-c", "riscv set_mem_access sysbus"]
        if os.name == "nt":
            self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        else:
            self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         stdin=subprocess.DEVNULL, preexec_fn=_linux_child)
        _tie_to_us(self.proc)
        threading.Thread(target=self._collect_log, args=(self.proc,), daemon=True).start()

        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                return self._failed("OpenOCD stopped while starting.")
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError:
                time.sleep(0.2)
        else:
            return self._failed("OpenOCD didn't open its control port.")

        # Has init found the chip? Read something that always exists: GPIO_IN.
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                if self.read([("probe", 0x6000403C, 1)]):
                    self.state = "ready"
                    return True
            except OSError:
                return self._failed("Lost the connection to OpenOCD.")
            if self.proc.poll() is not None:
                return self._failed("OpenOCD stopped while starting.")
            time.sleep(0.3)
        return self._failed("The chip didn't answer over JTAG.")

    def stop(self):
        if self.sock:
            try:
                self.sock.sendall(b"shutdown" + TCL_END)
            except OSError:
                pass
            self.sock.close()
            self.sock = None
        if self.proc:
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)
            self.proc = None
        if self.state != "paused":
            self.state = "stopped"

    def _failed(self, message):
        text = "\n".join(self.log)
        hint = next((h for key, h in HINTS if key.lower() in text.lower()), "")
        self.state, self.problem = "error", (hint or message)
        self.stop()
        self.state = "error"
        return False

    def _collect_log(self, proc):
        for raw in proc.stdout:
            line = raw.decode(errors="replace").rstrip()
            if line:
                self.log.append(line)

    # ---- reading ---------------------------------------------------------------------
    def _command(self, text):
        self.sock.sendall(text.encode() + TCL_END)
        data = b""
        while not data.endswith(TCL_END):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise OSError("OpenOCD closed the connection")
            data += chunk
        return data[:-1].decode(errors="replace")

    def read(self, blocks):
        """Read blocks [(name, address, words)] in one round trip. {name: [words]} or None on failure.

        A block that can't be read (for example while the chip resets) makes the whole
        read fail, rather than return half a picture."""
        script = ("set r {} ; foreach {a n} {" +
                  " ".join(f"0x{addr:08X} {count}" for _, addr, count in blocks) +
                  "} { if {[catch {read_memory $a 32 $n} v]} { lappend r ERR } else { lappend r $v } } ;"
                  " join $r |")
        reply = self._command(script).strip()
        parts = reply.split("|")
        if len(parts) != len(blocks) or any(p.strip() == "ERR" or not p.strip() for p in parts):
            return None
        result = {}
        for (name, _, count), part in zip(blocks, parts):
            words = [int(w, 16) for w in part.split()]
            if len(words) != count:
                return None
            result[name] = words
        return result
