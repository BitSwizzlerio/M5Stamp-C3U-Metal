#!/usr/bin/env python3
"""
c3u_viewer.py - watch an M5Stamp C3U's pins and talk to its Lua, in a browser.

    python c3u_viewer.py                 start, and open the browser
    python c3u_viewer.py --no-browser    start, and print the address to open
    python c3u_viewer.py --port COM5     if the board isn't found by itself

Plug the board in by USB. The viewer finds it, reads its pins over the USB
cable's JTAG side without stopping the program, and relays the Lua console
over the serial side. Close this window, or press Ctrl-C, to stop.
"""
import argparse
import os
import sys
import tempfile
import threading
import urllib.request
import webbrowser

from c3uview import __version__
from c3uview.server import Hub, serve


INSTANCE_FILE = os.path.join(tempfile.gettempdir(), "c3u-viewer.port")


def running_viewer():
    """The port of a viewer that's already running, or None. Two viewers would fight over
    the board: each would take some of the console's text, and their OpenOCDs would fight
    over JTAG. A running viewer writes its port to INSTANCE_FILE; check that it still answers."""
    try:
        with open(INSTANCE_FILE) as f:
            port = int(f.read().strip())
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1) as page:
            if b"<title>C3U Viewer</title>" in page.read(4096):
                return port
    except (OSError, ValueError):
        pass
    return None


def main():
    parser = argparse.ArgumentParser(description="Watch an M5Stamp C3U's pins and talk to its Lua, in a browser.")
    parser.add_argument("--port", help="the board's serial port, if it isn't found by itself")
    parser.add_argument("--http-port", type=int, default=8723, help="the local web port (default 8723)")
    parser.add_argument("--no-browser", action="store_true", help="don't open the browser")
    parser.add_argument("--install-driver", metavar="INF", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.install_driver:                 # the viewer runs itself like this, as administrator
        from c3uview.computer_setup import force_install
        sys.exit(force_install(args.install_driver))

    already = running_viewer()
    if already:
        url = f"http://127.0.0.1:{already}/"
        print(f"C3U Viewer is already running: {url}", flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        return

    hub = Hub(args.port)
    try:
        server = serve(hub, args.http_port)
    except OSError as error:
        sys.exit(f"Couldn't start the web server: {error}")
    port = server.server_address[1]
    url = f"http://127.0.0.1:{port}/"
    try:
        with open(INSTANCE_FILE, "w") as f:
            f.write(str(port))
    except OSError:
        pass

    print(f"C3U Viewer {__version__}", flush=True)
    print(f"Open {url} in a browser. Keep this window open; close it or press Ctrl-C to stop.", flush=True)
    if not args.no_browser:
        threading.Timer(0.5, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        print("Stopping...")
        hub.shutdown()
        server.server_close()
        try:
            with open(INSTANCE_FILE) as f:
                ours = f.read().strip() == str(port)
            if ours:
                os.remove(INSTANCE_FILE)
        except OSError:
            pass


if __name__ == "__main__":
    main()
