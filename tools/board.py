"""
board.py - find the M5Stamp C3U's USB serial port and open it. Used by the other tools.

The ESP32-C3 has USB built in (its USB Serial/JTAG peripheral), so the PC sees
the same serial port whatever program the board is running. It is recognised
by Espressif's USB vendor ID, 0x303A, and the product ID 0x1001.
"""
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing. Run this from an ESP-IDF terminal, where it is installed.")

ESPRESSIF_VID = 0x303A
USB_SERIAL_JTAG_PID = 0x1001

NOT_FOUND = ("M5Stamp C3U not found. Plug it in. If it still isn't found, unplug it, "
             "hold its button while plugging it back in, and try again.")


def find_port():
    """The board's serial port name (such as COM11 or /dev/ttyACM0), or None."""
    for port in sorted(list_ports.comports(), key=lambda p: p.device):
        if port.vid == ESPRESSIF_VID and port.pid == USB_SERIAL_JTAG_PID:
            return port.device
    return None


def wait_for_port(timeout=10.0):
    """Wait until the board is plugged in, or back after a reset, and return its port name."""
    deadline = time.monotonic() + timeout
    while True:
        port = find_port()
        if port:
            return port
        if time.monotonic() > deadline:
            sys.exit(NOT_FOUND)
        time.sleep(0.25)


def open_port(port, timeout=5.0):
    """Open the board's serial port without resetting it, retrying for a while if that fails."""
    deadline = time.monotonic() + timeout
    while True:
        link = serial.Serial()
        link.port = port
        link.baudrate = 115200      # USB ignores the baud rate, but it has to be set to something
        link.timeout = 0.05         # read() waits at most this long
        # The chip's USB peripheral treats DTR and RTS like the reset and boot-mode
        # wires of a USB-serial adapter. Keeping both off leaves the program running.
        link.dtr = False
        link.rts = False
        try:
            link.open()
            return link
        except serial.SerialException as error:
            if time.monotonic() > deadline:
                sys.exit(f"Could not open {port}: {error}\n"
                         "Is a serial monitor or terminal already using it? Close it and try again.")
            time.sleep(0.25)
