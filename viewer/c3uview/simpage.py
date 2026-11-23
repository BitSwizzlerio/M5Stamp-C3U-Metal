"""
simpage.py - the C3U Simulator's page: the viewer's own index.html, run by web/sim.js
instead of this program.

sim.js loads the board's ladder engine, compiled to WebAssembly (web/plc.wasm, built by
sim/build.py wasm), and answers the page as server.py would. The page itself changes
only a little: a new title, the buttons that need a real board hidden (Run a .lua
file, Stop, Flash, Diagnostics), an Examples menu, and sim.js loaded before app.js.

The same page is served two ways:
  - by the viewer, at /sim (the Simulate button), with a link back to the board;
  - on the web, from GitHub Pages (sim/build.py site), with a link to get the viewer.
"""

REPO = "https://github.com/BitSwizzlerio/M5Stamp-C3U-Metal"

EXAMPLES = [("motor", "motor: start, stop and a fan"),
            ("flasher", "flasher: two timers"),
            ("toggle", "toggle: one button, on and off"),
            ("numbers", "numbers: counting and sums"),
            ("tank", "tank: filling to a setpoint")]

STYLE = """<style>
  .actions { position: relative; }
  .sim-menu { position: absolute; top: 100%; z-index: 20; display: flex; flex-direction: column; gap: 2px; padding: 6px;
              margin-top: 4px; background: #fff; border: 1px solid #c3cad3; border-radius: 6px; box-shadow: 0 4px 14px #0002; }
  .sim-menu[hidden] { display: none; }
  .sim-menu button { text-align: left; border: 0; background: none; }
  .sim-link { align-self: center; font-size: 13px; }
</style>
"""


def page(html, version, web):
    """The simulator's page, from the viewer's index.html. web: True for GitHub Pages, False for the viewer's /sim."""
    html = html.replace("\r\n", "\n")

    def swap(old, new):
        nonlocal html
        if html.count(old) != 1:
            raise ValueError(f"index.html has changed, and simpage.py needs to follow: {old!r}")
        html = html.replace(old, new)

    menu = "".join(f'\n      <button data-example="{name}">{label}</button>' for name, label in EXAMPLES)
    link = (f'<a class="sim-link" href="{REPO}/releases/latest" target="_blank" rel="noopener" '
            f'title="The C3U Viewer, for a real M5Stamp C3U: its pins over JTAG, and Lua">Get the viewer for a real board</a>'
            if web else
            '<a class="sim-link" href="/" title="The viewer, with the board on USB">Back to the board</a>')
    extras = (f'    <button id="btn-examples" title="Open one of the example ladder programs">Examples ▾</button>\n'
              f'    <div id="sim-examples" class="sim-menu" hidden>{menu}\n    </div>\n    {link}\n')

    swap("<title>C3U Viewer</title>", "<title>C3U Simulator</title>")
    swap('<h1>C3U Viewer <span class="version">{{VERSION}}</span></h1>',
         f'<h1>C3U Simulator <span class="version">{version}</span></h1>')
    for button in ("btn-run", "btn-stop", "btn-flash", "btn-diag", "btn-simulate"):   # these need a real board
        swap(f'<button id="{button}"', f'<button id="{button}" hidden')
    swap('    <input id="file-run"', extras + '    <input id="file-run"')
    swap("</head>", STYLE + "</head>")
    swap('<div class="card-title">Lua console <span class="hint">Type here. Enter runs a line; Ctrl-C stops a program; '
         'paste several lines to type them in one by one.</span></div>',
         '<div class="card-title">Console <span class="hint">Lua needs a real board; here, plc.run(), plc.stop(), '
         'plc.step() and plc.status() work.</span></div>')
    swap('const TOKEN = "{{TOKEN}}";', 'const TOKEN = "";')
    swap('<script src="/static/app.js"></script>', '<script src="/static/sim.js"></script>\n<script src="/static/app.js"></script>')
    # Relative, so it works under any folder: /sim in the viewer, /M5Stamp-C3U-Metal/ on GitHub Pages.
    return html.replace('"/static/', '"static/')
