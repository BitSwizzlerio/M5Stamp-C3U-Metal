"""
server.py - the hub between the board and the browser.

One thread reads the pins over JTAG about 20 times a second; another relays the
Lua console. The browser gets everything as a stream of server-sent events and
sends keys and requests back with small POSTs.

The server only listens on 127.0.0.1, and every request must carry a token that
is generated at start-up and put into the page. That stops other web pages open
in the same browser from typing into the board or flashing it.
"""
import base64
import json
import os
import queue
import secrets
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from . import __version__, chip, computer_setup, flasher
from .console import Console, find_port
from .jtag import Jtag
from .paths import firmware_image, resource

POLL_HZ = 20
MAX_BODY = 8 * 1024 * 1024
WEB = resource("c3uview", "web")
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png"}


class Hub:
    def __init__(self, serial_port=None):
        self.token = secrets.token_urlsafe(24)
        self.clients = set()
        self.clients_lock = threading.Lock()
        self.busy = None                    # None, "flashing" or "running"
        self.stop_run = threading.Event()
        self.last_pins = None
        self.poll_paused = threading.Event()
        self.rate = 0.0
        self.running = True
        self.backlog = bytearray()          # recent console output, for a page opened later
        self.messages = []                  # recent messages, likewise
        self.warned_at = 0.0
        self.jtag = Jtag()
        self.console = Console(self._console_data, self.publish_status, serial_port)
        threading.Thread(target=self._poll, daemon=True).start()

    # ---- events to the browser --------------------------------------------------------
    def subscribe(self):
        q = queue.Queue(maxsize=500)
        with self.clients_lock:
            self.clients.add(q)
        q.put(self.status())
        for text in self.messages:
            q.put({"type": "log", "text": text})
        if self.backlog:
            q.put({"type": "term", "data": base64.b64encode(bytes(self.backlog)).decode()})
        if self.last_pins:
            q.put(self.last_pins)
        return q

    def unsubscribe(self, q):
        with self.clients_lock:
            self.clients.discard(q)

    def broadcast(self, event):
        with self.clients_lock:
            clients = list(self.clients)
        for q in clients:
            try:
                q.put_nowait(event)
            except queue.Full:              # a stalled browser tab: drop its oldest news
                try:
                    q.get_nowait()
                    q.put_nowait(event)
                except (queue.Empty, queue.Full):
                    pass

    def status(self):
        image = firmware_image()
        setup = None
        if self.jtag.state != "ready" or self.console.problem:
            setup = computer_setup.advice("\n".join(self.jtag.log), self.console.connected, self.console.problem,
                                          self.jtag.state == "error")
        return {"type": "status", "version": __version__, "busy": self.busy,
                "serial": {"connected": self.console.connected, "port": self.console.port,
                           "problem": self.console.problem},
                "jtag": {"state": self.jtag.state, "problem": self.jtag.problem, "rate": round(self.rate, 1)},
                "setup": setup,
                "firmware": os.path.basename(image) if image else None}

    def publish_status(self):
        self.broadcast(self.status())

    def log(self, text):
        self.messages = (self.messages + [text])[-100:]
        self.broadcast({"type": "log", "text": text})

    def _console_data(self, data):
        self.backlog += data
        del self.backlog[:-32768]
        self.broadcast({"type": "term", "data": base64.b64encode(data).decode()})

    # ---- reading the pins ---------------------------------------------------------------
    def _poll(self):
        frame, system, slow, failures = 0, None, {}, 0
        window_start, window_frames = time.monotonic(), 0
        while self.running:
            if self.poll_paused.is_set():
                time.sleep(0.1)
                continue
            if self.jtag.state != "ready":
                self.publish_status()
                if not self.jtag.start():
                    self.publish_status()
                    time.sleep(3)
                    continue
                frame, system, slow, failures = 0, None, {}, 0
                self.publish_status()
            started = time.monotonic()
            try:
                regs = self.jtag.read(chip.plan(frame, system))
            except OSError:
                regs, failures = None, 99
            if regs is None:
                failures += 1
                if failures > 20:           # two seconds of nothing: start OpenOCD again
                    self.jtag.state, self.jtag.problem = "error", "Lost the chip over JTAG; reconnecting."
                    self.jtag.stop()
                    self.jtag.state = "error"
                    self.publish_status()
                time.sleep(0.1)
                continue
            failures = 0
            system = regs["system"]
            if "in_sel" in regs:
                slow["in_sel"] = regs["in_sel"]
            regs.update({k: v for k, v in slow.items() if k not in regs})
            event = chip.decode(regs)
            event.update(type="pins", t=time.time(), frame=frame)
            self.last_pins = event
            self.broadcast(event)
            frame += 1
            window_frames += 1
            if started - window_start >= 2:
                self.rate = window_frames / (started - window_start)
                window_start, window_frames = started, 0
                self.publish_status()
            time.sleep(max(0.0, 1 / POLL_HZ - (time.monotonic() - started)))

    # ---- things the browser asks for --------------------------------------------------
    def type_keys(self, data):
        if self.busy == "flashing":
            delivered = False
        else:
            delivered = self.console.write(data)
        if not delivered and time.monotonic() - self.warned_at > 2:
            self.warned_at = time.monotonic()
            self.log("The board isn't connected, so that wasn't typed. "
                     + ("Wait for flashing to finish." if self.busy == "flashing" else "Is it plugged in?"))

    def run_text(self, name, text):
        if self.busy:
            return f"busy {self.busy}"
        self.busy = "running"
        self.stop_run.clear()
        self.publish_status()

        def job():
            try:
                self.log(f"Typing {name} into the board...")
                self.console.type_lines(text, self.stop_run, self.log)
            finally:
                self.busy = None
                self.publish_status()

        threading.Thread(target=job, daemon=True).start()
        return "ok"

    def stop(self):
        self.stop_run.set()
        self.console.write(b"\x03")          # Ctrl-C: stops a running Lua program

    def flash(self, image_b64, name, erase):
        if self.busy:
            return f"busy {self.busy}"
        if image_b64:
            handle, path = tempfile.mkstemp(suffix=".bin")
            with os.fdopen(handle, "wb") as f:
                f.write(base64.b64decode(image_b64))
            temporary = True
        else:
            path, temporary = firmware_image(), False
            if not path:
                return "no C3U-Metal image is bundled with this viewer"
        self.busy = "flashing"
        self.publish_status()

        def job():
            ok = False
            try:
                self.poll_paused.set()
                self.jtag.stop()
                port = self.console.port or find_port()
                self.console.pause()
                if not port:
                    self.log("No board found. Plug it in, or hold its button while plugging it in, and try again.")
                    return
                self.log(f"Flashing {name} to the board on {port}" + (", erasing first" if erase else ""))
                ok = flasher.flash(port, path, erase, self.log)
            finally:
                if temporary:
                    try:
                        os.remove(path)
                    except OSError:
                        pass
                self.console.resume()
                self.jtag.state = "stopped"
                self.poll_paused.clear()
                self.busy = None
                self.log("Done: the board is starting the new program." if ok else
                         "Flashing failed. If the board doesn't answer, unplug it, hold its button "
                         "while plugging it back in, and flash again.")
                self.publish_status()

        threading.Thread(target=job, daemon=True).start()
        return "ok"

    def diagnostics(self):
        s = self.status()
        lines = [f"viewer: {__version__}, busy: {s['busy']}",
                 f"console: {s['serial']}",
                 f"pins (JTAG): {s['jtag']}",
                 f"setup advice: {s['setup']}",
                 f"bundled firmware: {s['firmware']}"]
        return computer_setup.diagnostics(lines, self.jtag.openocd, self.jtag.log)

    def install_driver(self):
        if self.busy:
            return f"busy {self.busy}"
        if os.name != "nt":
            return "only needed on Windows"
        self.busy = "installing"
        self.publish_status()

        def job():
            try:
                computer_setup.install_windows_driver(self.log)
            finally:
                self.busy = None
                self.jtag.state = "stopped"     # try JTAG again straight away
                self.publish_status()

        threading.Thread(target=job, daemon=True).start()
        return "ok"

    def shutdown(self):
        self.running = False
        self.poll_paused.set()
        self.jtag.stop()
        self.console.close()


def make_handler(hub):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def _send(self, code, body, content_type="text/plain; charset=utf-8"):
            data = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urlparse(self.path)
            if url.path == "/":
                with open(os.path.join(WEB, "index.html"), encoding="utf-8") as f:
                    page = f.read().replace("{{TOKEN}}", hub.token).replace("{{VERSION}}", __version__)
                return self._send(200, page, TYPES[".html"])
            if url.path == "/events":
                if parse_qs(url.query).get("token", [""])[0] != hub.token:
                    return self._send(403, "bad token")
                return self._events()
            if url.path.startswith("/static/"):
                rel = os.path.normpath(url.path[len("/static/"):])
                full = os.path.join(WEB, rel)
                if rel.startswith("..") or os.path.isabs(rel) or not os.path.isfile(full):
                    return self._send(404, "not found")
                with open(full, "rb") as f:
                    return self._send(200, f.read(), TYPES.get(os.path.splitext(full)[1], "application/octet-stream"))
            self._send(404, "not found")

        def _events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            q = hub.subscribe()
            try:
                while hub.running:
                    try:
                        event = q.get(timeout=15)
                        self.wfile.write(b"data: " + json.dumps(event).encode() + b"\n\n")
                    except queue.Empty:
                        self.wfile.write(b": keep-alive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
                pass
            finally:
                hub.unsubscribe(q)
                self.close_connection = True

        def do_POST(self):
            if self.headers.get("X-Token") != hub.token:
                return self._send(403, "bad token")
            length = int(self.headers.get("Content-Length", 0))
            if length > MAX_BODY:
                return self._send(413, "too big")
            try:
                body = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                return self._send(400, "bad request")
            action = urlparse(self.path).path
            if action == "/api/keys":
                hub.type_keys(base64.b64decode(body.get("data", "")))
                result = "ok"
            elif action == "/api/run":
                result = hub.run_text(body.get("name", "a program"), body.get("text", ""))
            elif action == "/api/stop":
                hub.stop()
                result = "ok"
            elif action == "/api/install-driver":
                result = hub.install_driver()
            elif action == "/api/diagnostics":
                result = hub.diagnostics()
            elif action == "/api/flash":
                result = hub.flash(body.get("image"), body.get("name", "C3U-Metal"), bool(body.get("erase")))
            else:
                return self._send(404, "not found")
            self._send(200, json.dumps({"result": result}), "application/json")

    return Handler


class ExclusiveServer(ThreadingHTTPServer):
    """A web server that won't share its port. Python's HTTP server normally sets SO_REUSEADDR,
    which on Windows lets a second program bind a port already in use, so two viewers started
    one after the other would both answer on it."""
    allow_reuse_address = False
    daemon_threads = True

    def server_bind(self):
        import socket
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):              # Windows
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


def serve(hub, port):
    """Start the web server on the first free port from 'port' up. Returns the server."""
    for candidate in range(port, port + 20):
        try:
            return ExclusiveServer(("127.0.0.1", candidate), make_handler(hub))
        except OSError:
            continue
    raise OSError(f"no free port between {port} and {port + 19}")
