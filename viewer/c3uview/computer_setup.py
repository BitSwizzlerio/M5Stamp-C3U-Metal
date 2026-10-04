"""
computer_setup.py - the one-time computer setup that reading pins over JTAG needs.

Windows: the board's serial side works with Windows' own driver. Windows also
binds its generic WinUSB driver to the JTAG side by itself, but without the
DeviceInterfaceGUIDs registry value that OpenOCD's USB library (libusb) uses to
find the device, so OpenOCD can't open it. Espressif's signed driver package adds
that value. The viewer forces the package onto the device, as Espressif's ESP-IDF
installer does, so that it replaces the generic one whichever Windows would rank
higher; it runs itself with administrator rights to do that (one permission
prompt). It looks for the zip next to the viewer first, so a teacher can provide
it without internet.

Linux: a udev rule lets ordinary users open the board. The viewer prints it;
installing it needs sudo, so the user runs it.
"""
import hashlib
import os
import sys
import tempfile
import urllib.request
import zipfile

from .paths import base

DRIVER_ZIP = "idf-driver-esp32-usb-jtag-2021-07-15.zip"
DRIVER_URL = "https://dl.espressif.com/dl/idf-driver/" + DRIVER_ZIP
DRIVER_SHA256 = "84e741dbec5526e3152bded421b4f06f990cd2d1d7e83b907c40e81f9db0f30e"
JTAG_INTERFACE = r"SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_1001&MI_02"

# Let ordinary users open the board, and tell ModemManager (which probes new USB serial
# devices for modems by opening them and typing AT commands) to leave it alone.
UDEV_RULE_PATH = "/etc/udev/rules.d/60-c3u-viewer.rules"
UDEV_RULES = ('SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="1001", MODE="0666", '
              'ENV{ID_MM_DEVICE_IGNORE}="1"\n'
              'SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666", '
              'ENV{ID_MM_DEVICE_IGNORE}="1"\n')
UDEV_COMMAND = ("printf '" + UDEV_RULES.replace("\n", "\\n") + f"' | sudo tee {UDEV_RULE_PATH}"
                " && sudo udevadm control --reload-rules && sudo udevadm trigger")


def _linux_processes():
    """{pid: command name} for every process we can see."""
    found = {}
    for pid in os.listdir("/proc"):
        if pid.isdigit():
            try:
                with open(f"/proc/{pid}/comm") as f:
                    found[int(pid)] = f.read().strip()
            except OSError:
                pass
    return found


def _linux_users_of(path):
    """The processes that have 'path' open, as ["name (pid)"], where we're allowed to look."""
    users = []
    real = os.path.realpath(path)
    for pid, name in _linux_processes().items():
        try:
            for fd in os.listdir(f"/proc/{pid}/fd"):
                if os.path.realpath(f"/proc/{pid}/fd/{fd}") == real:
                    users.append(f"{name} ({pid})")
                    break
        except OSError:
            pass
    return users


def _modem_manager_running():
    return any(name == "ModemManager" for name in _linux_processes().values())


def _udev_rule_installed():
    try:
        with open(UDEV_RULE_PATH) as f:
            return "ID_MM_DEVICE_IGNORE" in f.read()
    except OSError:
        return False


def windows_driver_state():
    """'ok' if the JTAG interface has WinUSB and the DeviceInterfaceGUIDs that libusb needs;
    'generic' if it has WinUSB without them (Windows' own automatic install); 'missing' if it
    has another driver or none; 'unknown' if Windows has never seen the board, or this isn't Windows."""
    if os.name != "nt":
        return "unknown"
    if os.environ.get("C3U_TEST_DRIVER_STATE"):     # for testing the banners on a computer that's set up
        return os.environ["C3U_TEST_DRIVER_STATE"]
    try:
        return _driver_state_from_registry()
    except Exception:                       # whatever goes wrong, don't stop the viewer
        return "unknown"


def _driver_state_from_registry():
    import winreg
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, JTAG_INTERFACE)
    except OSError:
        return "unknown"
    found = "missing"
    for i in range(winreg.QueryInfoKey(key)[0]):
        instance = winreg.EnumKey(key, i)
        try:
            service = winreg.QueryValueEx(winreg.OpenKey(key, instance), "Service")[0]
        except OSError:
            continue
        if service.lower() != "winusb":
            continue
        try:
            params = winreg.OpenKey(key, instance + r"\Device Parameters")
            if winreg.QueryValueEx(params, "DeviceInterfaceGUIDs")[0]:
                return "ok"
        except OSError:
            pass
        found = "generic"
    return found


def _driver_zip(report):
    """The driver package: next to the viewer if a teacher put it there, otherwise from Espressif."""
    places = [os.path.join(os.path.dirname(sys.executable), DRIVER_ZIP), os.path.join(base(), DRIVER_ZIP),
              os.path.join(os.getcwd(), DRIVER_ZIP)]
    for path in places:
        if os.path.isfile(path):
            report(f"Using {path}")
            with open(path, "rb") as f:
                return f.read()
    report(f"Downloading Espressif's driver package from {DRIVER_URL}")
    with urllib.request.urlopen(DRIVER_URL, timeout=60) as response:
        return response.read()


def install_windows_driver(report):
    """Install Espressif's signed WinUSB driver for the board's JTAG interface. Returns True if it worked."""
    try:
        data = _driver_zip(report)
    except OSError as error:
        report(f"Couldn't get the driver package: {error}. Put {DRIVER_ZIP} next to the viewer and try again.")
        return False
    if hashlib.sha256(data).hexdigest() != DRIVER_SHA256:
        report("The driver package isn't the one this viewer expects, so it wasn't installed.")
        return False
    folder = tempfile.mkdtemp(prefix="c3u-driver-")
    path = os.path.join(folder, DRIVER_ZIP)
    with open(path, "wb") as f:
        f.write(data)
    with zipfile.ZipFile(path) as z:
        z.extractall(folder)
    inf = next((os.path.join(folder, n) for n in os.listdir(folder) if n.lower().endswith(".inf")), None)
    if not inf:
        report("The driver package has no .inf file.")
        return False
    report("Installing the driver. Windows will ask for permission: choose Yes.")
    # Run this same program again, with administrator rights, to do force_install().
    if getattr(sys, "frozen", False):
        program, arguments = sys.executable, f'--install-driver "{inf}"'
    else:
        script = os.path.join(base(), "c3u_viewer.py")
        program, arguments = sys.executable, f'"{script}" --install-driver "{inf}"'
    code = _run_as_admin(program, arguments)
    if code is None:
        report("Permission wasn't given, so the driver wasn't installed.")
        return False
    if code == 0:
        report("The driver is installed. The pins should appear within a few seconds; "
               "if they don't, unplug the board and plug it back in.")
        return True
    if code == NOT_PLUGGED_IN:
        report("The driver is ready, and Windows will use it as soon as the board is plugged in. "
               "Plug it in, or unplug it and plug it back in.")
        return True
    report(f"Installing the driver failed (Windows error 0x{code & 0xFFFFFFFF:08X}). "
           "Is the board plugged in? Copy the diagnostics if it keeps failing.")
    return False


def force_install(inf):
    """Put the driver in 'inf' onto the board's JTAG interface even though Windows ranks its own
    generic driver higher. Runs with administrator rights. Returns 0, or a Windows error code."""
    import ctypes
    from ctypes import wintypes
    newdev = ctypes.WinDLL("newdev", use_last_error=True)
    update = newdev.UpdateDriverForPlugAndPlayDevicesW
    update.argtypes = [wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
                       ctypes.POINTER(wintypes.BOOL)]
    update.restype = wintypes.BOOL
    reboot = wintypes.BOOL(False)
    INSTALLFLAG_FORCE = 0x1                 # install even if the current driver ranks better
    if update(None, "USB\\VID_303A&PID_1001&MI_02", inf, INSTALLFLAG_FORCE, ctypes.byref(reboot)):
        return 0
    error = ctypes.get_last_error()
    # With the board unplugged, Windows still imports the driver and marks the board for it at
    # the next plug-in, but reports that nothing was updated (sometimes with no error code).
    return NOT_PLUGGED_IN if error in (0, NOT_PLUGGED_IN) else error


NOT_PLUGGED_IN = 0xE000020B                 # ERROR_NO_SUCH_DEVINST


def _run_as_admin(program, arguments):
    """Run a program with administrator rights (Windows shows its permission prompt) and wait.
    Returns its exit code, or None if the user said no."""
    import ctypes
    from ctypes import wintypes

    class ShellExecuteInfo(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("fMask", wintypes.ULONG), ("hwnd", wintypes.HWND),
                    ("lpVerb", wintypes.LPCWSTR), ("lpFile", wintypes.LPCWSTR), ("lpParameters", wintypes.LPCWSTR),
                    ("lpDirectory", wintypes.LPCWSTR), ("nShow", ctypes.c_int), ("hInstApp", wintypes.HINSTANCE),
                    ("lpIDList", ctypes.c_void_p), ("lpClass", wintypes.LPCWSTR), ("hkeyClass", wintypes.HKEY),
                    ("dwHotKey", wintypes.DWORD), ("hIconOrMonitor", wintypes.HANDLE), ("hProcess", wintypes.HANDLE)]

    info = ShellExecuteInfo()
    info.cbSize = ctypes.sizeof(info)
    info.fMask = 0x40                       # SEE_MASK_NOCLOSEPROCESS: give us the process to wait for
    info.lpVerb = "runas"
    info.lpFile = program
    info.lpParameters = arguments
    info.nShow = 0                          # SW_HIDE
    if not ctypes.windll.shell32.ShellExecuteExW(ctypes.byref(info)):
        return None
    ctypes.windll.kernel32.WaitForSingleObject(info.hProcess, 120_000)
    code = wintypes.DWORD()
    ctypes.windll.kernel32.GetExitCodeProcess(info.hProcess, ctypes.byref(code))
    ctypes.windll.kernel32.CloseHandle(info.hProcess)
    return code.value


def advice(log_text, board_seen, console_problem, jtag_failed):
    """What the computer still needs, as {"kind": ..., "message": ...}, or None."""
    text = log_text.lower()
    if console_problem == "busy":
        return {"kind": "busy",
                "message": "Another program has the board's serial port open, so the console can't use it. "
                           "Close any other serial monitor, terminal or the Arduino IDE."}
    if os.name == "nt":
        state = windows_driver_state()
        # Offer the driver whenever it might be the answer: installing it again does no harm.
        if state in ("missing", "generic") or (jtag_failed and board_seen and state != "ok"):
            return {"kind": "windows-driver",
                    "message": "Windows needs a driver for the board's JTAG side before the pins can be read. "
                               "The console works without it."}
        if jtag_failed and board_seen:
            return {"kind": "jtag-failed",
                    "message": "The console works, but the pins can't be read over JTAG, although the driver "
                               "looks installed. Unplug the board and plug it back in. If that doesn't help, "
                               "copy the diagnostics and send them to whoever looks after the viewer."}
    elif "libusb_error_access" in text or "permission" in text or console_problem == "permission":
        return {"kind": "linux-udev",
                "message": "Linux needs a udev rule before you can use the board without root. "
                           "Run this once in a terminal, then unplug the board and plug it back in:",
                "command": UDEV_COMMAND}
    elif sys.platform.startswith("linux") and _modem_manager_running() and not _udev_rule_installed():
        return {"kind": "linux-udev",
                "message": "ModemManager is running. It opens new USB serial devices to look for modems, "
                           "which can reset the board and scramble the console. Run this once in a terminal "
                           "to make it leave the board alone, then unplug the board and plug it back in:",
                "command": UDEV_COMMAND}
    elif jtag_failed and board_seen:
        return {"kind": "jtag-failed",
                "message": "The console works, but the pins can't be read over JTAG. Unplug the board and plug "
                           "it back in. If that doesn't help, copy the diagnostics and send them on."}
    return None


def _windows_devices():
    """What Windows knows about the board's three USB functions, for the diagnostics."""
    import winreg
    lines = []
    for function, name in (("", "the composite device"), ("&MI_00", "the serial port (MI_00)"),
                           ("&MI_02", "the JTAG interface (MI_02)")):
        path = r"SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_1001" + function
        try:
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path)
        except OSError as error:
            lines.append(f"  {name}: no registry entry ({error.__class__.__name__})")
            continue
        for i in range(winreg.QueryInfoKey(key)[0]):
            instance = winreg.EnumKey(key, i)
            values = {}
            try:
                sub = winreg.OpenKey(key, instance)
                for value in ("Service", "Driver", "ConfigFlags", "DeviceDesc", "Mfg"):
                    try:
                        values[value] = winreg.QueryValueEx(sub, value)[0]
                    except OSError:
                        pass
            except OSError as error:
                values["error"] = str(error)
            lines.append(f"  {name} {instance}: {values}")
    return lines


def diagnostics(hub_lines, openocd_path, openocd_log):
    """Everything needed to work out why something doesn't work, as text to copy."""
    import platform
    import subprocess
    out = ["C3U Viewer diagnostics", ""] + hub_lines
    out += [f"system: {platform.platform()}, Python {platform.python_version()}, "
            f"{'packaged' if getattr(sys, 'frozen', False) else 'from source'}",
            f"OpenOCD: {openocd_path}"]
    if os.name == "nt":
        out += ["", f"JTAG driver check: {windows_driver_state()}", "registry:"]
        try:
            out += _windows_devices()
        except Exception as error:          # the diagnostics must never fail
            out.append(f"  couldn't read the registry: {error!r}")
        try:
            result = subprocess.run(["pnputil", "/enum-devices", "/connected", "/drivers"], capture_output=True,
                                    text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
            blocks = [b for b in result.stdout.split("\n\n") if "303A" in b.upper()]
            out += ["", "pnputil, the board's devices:"] + (blocks or ["  (none listed)"])
        except Exception as error:
            out.append(f"pnputil failed: {error!r}")
    elif sys.platform.startswith("linux"):
        try:
            import glob
            import grp
            try:
                with open("/etc/os-release") as f:
                    release = next((l.split("=", 1)[1].strip().strip('"') for l in f if l.startswith("PRETTY_NAME=")), "?")
            except OSError:
                release = "?"
            out += ["", f"distribution: {release}",
                    f"groups: {', '.join(sorted(grp.getgrgid(g).gr_name for g in os.getgroups()))}",
                    f"udev rule {UDEV_RULE_PATH}: {'installed' if _udev_rule_installed() else 'not installed'}",
                    f"ModemManager running: {_modem_manager_running()}",
                    "OpenOCD processes: " + (", ".join(f"{pid}" for pid, name in _linux_processes().items()
                                                       if name == "openocd") or "none")]
            for tty in sorted(glob.glob("/dev/ttyACM*")):
                st = os.stat(tty)
                out.append(f"{tty}: mode {oct(st.st_mode & 0o777)}, group {grp.getgrgid(st.st_gid).gr_name}, "
                           f"open in: {', '.join(_linux_users_of(tty)) or 'nothing else we can see'}")
        except Exception as error:           # the diagnostics must never fail
            out.append(f"couldn't look at the Linux setup: {error!r}")
    out += ["", "OpenOCD's last messages:"] + (list(openocd_log) or ["  (none)"])
    return "\n".join(out)
