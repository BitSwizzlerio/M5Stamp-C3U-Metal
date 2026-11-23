"use strict";
// sim.js: the C3U Simulator's stand-in for the viewer program. It loads the board's ladder engine,
// compiled to WebAssembly (plc.wasm, built by sim/build.py from app/plc.c and app/plc_asm.c), and
// answers the viewer's page (app.js and the rest, unchanged) as the viewer program would: it sends
// the same 'pins', 'ladder' and 'ladder-program' events, and takes the same requests.
//
// plc_image is read straight out of the WebAssembly memory with the offsets the viewer uses to read
// it from a real board over JTAG (viewer/c3uview/ladder.py), and the viewer's commands (Run, Stop,
// switching an M bit, setting a D register) are written into its command word, as on the board.

window.Backend = (() => {
  const OFF = { magic: 0, layout: 4, loads: 8, running: 12, claimed: 16, period: 20, scans: 24, scanUs: 28, scanMax: 32,
                late: 36, inMask: 40, outMask: 44, inputs: 48, outputs: 52, m: 56, timers: 88, counters: 600, trace: 1112,
                words: 1240, program: 1244, sourceLen: 3292, source: 3296, command: 7392, d: 7404, commandValue: 7660 };
  const RESERVED = new Set([11, 12, 13, 14, 15, 16, 17, 18, 19]);   // flash and USB, as on the board
  const NOTES = { 2: "RGB LED (SK6812)", 9: "button (low while pressed)", 11: "flash power (VDD_SPI)", 12: "flash",
                  13: "flash", 14: "flash", 15: "flash", 16: "flash", 17: "flash", 18: "USB D-", 19: "USB D+",
                  20: "RX on the ISP header (UART0)", 21: "TX on the ISP header (UART0)" };
  const B = { simulated: true };
  let w = null, handler = null, lastLoads = -1, frame = 0;
  let simMs = 0, lastReal = 0, owed = 0;
  const wired = {};                                         // pin -> 0 or 1, for "switches" the user has clicked
  let line = "";                                            // what's typed at the console
  // plc.wasm sits next to this script, and the example programs in examples/ beside its folder:
  // that holds both in the C3U Viewer (/static/sim.js, /examples/) and on the web (static/, examples/).
  const HERE = document.currentScript.src;
  const near = (path) => new URL(path, HERE).href;

  B.greeting = "\x1b[2mThe C3U Simulator: the board's own ladder engine, running in your browser.\r\n" +
               "Lua needs a real board and the C3U Viewer; here, open an .il file or an example,\r\n" +
               "click BTN to press the button, and click an input pin to switch it.\r\n" +
               "Type plc.run(), plc.stop(), plc.step() or plc.status().\x1b[0m\r\n> ";

  // ---- the engine ---------------------------------------------------------------------------
  const view = () => new DataView(w.memory.buffer, w.sim_image(), w.sim_image_size());
  const u32 = (off) => view().getUint32(off, true);
  const i32 = (off) => view().getInt32(off, true);
  const words = (off, n) => Array.from({ length: n }, (_, i) => u32(off + 4 * i));

  async function boot() {
    const stub = new Proxy({}, { get: () => () => 0 });       // the few WASI calls the C library makes: nothing to do
    const imports = { wasi_snapshot_preview1: stub };
    const response = await fetch(near("plc.wasm"));
    if (!response.ok) throw new Error(`plc.wasm: ${response.status} ${response.statusText}`);
    // instantiateStreaming needs the server to call it application/wasm; not every one does.
    const { instance } = await WebAssembly.instantiate(await response.arrayBuffer(), imports);
    w = instance.exports;
    w._initialize();
    w.sim_reset_board();
  }

  function load(text) {
    const bytes = new TextEncoder().encode(text.replace(/\r\n?/g, "\n"));
    if (bytes.length >= w.sim_text_size()) return say("plc.load: the program is too long: the board keeps up to 4095 characters.\r\n");
    new Uint8Array(w.memory.buffer, w.sim_text(), bytes.length).set(bytes);
    if (!w.sim_load(bytes.length)) {
      const p = w.sim_error(), mem = new Uint8Array(w.memory.buffer);
      let end = p;
      while (mem[end]) end++;
      const message = new TextDecoder().decode(mem.subarray(p, end));
      say(`plc.load: ${message}\r\n`);
      log(`plc.load: ${message}`, true);
      return false;
    }
    say(`${w.sim_instructions()} instructions in ${w.sim_rungs()} rungs\r\n`);
    return true;
  }

  function command(word, value = 0) {                         // as the viewer writes it over JTAG
    view().setUint32(OFF.commandValue, value >>> 0, true);
    view().setUint32(OFF.command, word >>> 0, true);
    w.sim_advance_ms(1);                                      // plc_poll() carries it out
    simMs += 1;
  }

  // ---- events, as the viewer program sends them ------------------------------------------------
  function send(ev) { if (handler) handler(ev); }
  function say(text) {
    const bytes = new TextEncoder().encode(text);
    let bin = "";
    bytes.forEach((b) => (bin += String.fromCharCode(b)));
    send({ type: "term", data: btoa(bin) });
    return true;
  }

  function pinsEvent() {
    const levels = w.sim_pins(), drives = w.sim_outputs(), inMask = u32(OFF.inMask);
    const pins = [];
    for (let pin = 0; pin < 22; pin++) {
      const level = (levels >>> pin) & 1;
      const p = { pin, note: NOTES[pin] || "", reserved: RESERVED.has(pin), function: "GPIO", drive: 2, feeds: [],
                  pull: "none", input_enabled: false, level: null };
      if (p.reserved) { p.mode = "reserved"; p.summary = `${NOTES[pin]}: in use by the board, leave it alone`; }
      else if ((drives >>> pin) & 1) {
        Object.assign(p, { mode: "output", driven_by: "GPIO_OUT", out_level: level, level, input_enabled: true,
                           summary: `output, set to ${level}` });
      } else if (((inMask >>> pin) & 1) || pin === 9 || wired[pin] !== undefined) {
        const pull = pin === 9 ? "up" : "down";
        Object.assign(p, { mode: "input", pull, input_enabled: true, level,
                           summary: wired[pin] !== undefined ? "input, switched to 1 (click to switch it off)"
                                  : pin === 9 ? "input, pull-up: hold BTN to press" : `input, pull-${pull}: click to switch it on` });
      } else { p.mode = "off"; p.summary = "not used by the program"; }
      pins.push(p);
    }
    return { type: "pins", t: Date.now() / 1000, frame: frame++, pins, chip: { cpu_hz: 40e6, apb_hz: 40e6, ledc_on: false, adc_on: false } };
  }

  function ladderEvent() {
    const timers = {}, counters = {}, d = {};
    for (let n = 0; n < 32; n++) {
      timers[n] = [Math.floor(u32(OFF.timers + 16 * n) / 1000), u32(OFF.timers + 16 * n + 4), u32(OFF.timers + 16 * n + 8)];
      counters[n] = [u32(OFF.counters + 16 * n), u32(OFF.counters + 16 * n + 4), u32(OFF.counters + 16 * n + 8)];
    }
    for (let n = 0; n < 64; n++) d[n] = i32(OFF.d + 4 * n);
    const n = u32(OFF.words);
    return { type: "ladder", simulated: true, present: true, layout: u32(OFF.layout), loads: u32(OFF.loads), running: !!u32(OFF.running),
             claimed: !!u32(OFF.claimed), detail: true, period_ms: Math.floor(u32(OFF.period) / 1000), scans: u32(OFF.scans),
             scan_us: u32(OFF.scanUs), scan_us_max: u32(OFF.scanMax), late: u32(OFF.late), in_mask: u32(OFF.inMask),
             out_mask: u32(OFF.outMask), inputs: u32(OFF.inputs), outputs: u32(OFF.outputs), m: words(OFF.m, 8), current: true,
             trace: words(OFF.trace, Math.max(1, Math.ceil(n / 32))), timers, counters, d };
  }

  function programEvent() {
    const n = u32(OFF.words), len = Math.min(u32(OFF.sourceLen), 4095);
    const source = new TextDecoder().decode(new Uint8Array(w.memory.buffer, w.sim_image() + OFF.source, len));
    return { type: "ladder-program", loads: u32(OFF.loads), program: words(OFF.program, n), source };
  }

  function tick() {
    const now = performance.now();
    owed += Math.min(250, now - lastReal);                    // a background tab catches up a little, not for minutes
    lastReal = now;
    const ms = Math.floor(owed);
    if (ms > 0) { w.sim_advance_ms(ms); simMs += ms; owed -= ms; }
    const ladder = ladderEvent();
    if (ladder.loads !== lastLoads) { lastLoads = ladder.loads; send(programEvent()); }
    send(pinsEvent());
    send(ladder);
  }

  // ---- requests, as the viewer program takes them -----------------------------------------------
  B.api = async (path, body) => {
    if (!w) return { result: "the simulator is still starting" };
    if (path === "/api/run") return { result: runText(body.text || "") };
    if (path === "/api/keys") { typed(new TextDecoder().decode(Uint8Array.from(atob(body.data || ""), (c) => c.charCodeAt(0)))); return { result: "ok" }; }
    if (path === "/api/ladder-command") {
      const name = body.command;
      if (name === "run") { command(1 | (Math.max(1, Math.min(1000, +body.period_ms || 10)) << 8)); return { result: "ok" }; }
      if (name === "stop") { command(2); return { result: "ok" }; }
      if (name === "set") { command(3 | (((+body.m & 0xFF) | (body.value ? 0x100 : 0)) << 8)); return { result: "ok" }; }
      if (name === "setd") { command(4 | ((+body.d & 0x3F) << 8), +body.value | 0); return { result: "ok" }; }
      return { result: "unknown ladder command" };
    }
    if (path === "/api/ladder" || path === "/api/stop") return { result: "ok" };
    return { result: "that needs a real board and the C3U Viewer" };
  };

  // "plc.load[[ ... ]] plc.run()", as the Ladder Logic card and the operator panel type it.
  function runText(text) {
    const m = text.match(/plc\.load\[\[\n?([\s\S]*?)\n?\]\]([\s\S]*)$/);
    if (!m) { say("\r\nLua needs a real board and the C3U Viewer. The simulator runs ladder logic.\r\n> "); return "ok"; }
    say("\r\n");
    if (load(m[1])) {
      const run = m[2].match(/plc\.run\((\d*)\)/);
      if (run) { w.sim_run(+run[1] || 10); say(`ladder running, a scan every ${+run[1] || 10} ms\r\n`); }
    }
    say("> ");
    return "ok";
  }

  // A few plc functions at the console; everything else needs Lua.
  function typed(text) {
    for (const ch of text) {
      if (ch === "\r") {
        say("\r\n");
        const cmd = line.trim();
        line = "";
        let m;
        if (!cmd) {}
        else if ((m = cmd.match(/^plc\.run\((\d*)\)$/))) {
          if (!u32(OFF.words)) say("plc.run: there is no program yet: open an .il file or an example\r\n");
          else { w.sim_run(+m[1] || 10); say(`ladder running, a scan every ${+m[1] || 10} ms\r\n`); }
        } else if (cmd === "plc.stop()") { w.sim_stop(); say("ladder stopped\r\n"); }
        else if (cmd === "plc.step()") { w.sim_step(); }
        else if (cmd === "plc.status()") {
          const l = ladderEvent();
          say(`${l.running ? `running, a scan every ${l.period_ms} ms` : "stopped"}; ${l.scans} scans\r\n`);
        } else say("Lua needs a real board and the C3U Viewer. Here: plc.run(), plc.stop(), plc.step(), plc.status().\r\n");
        say("> ");
      } else if (ch === "\x7f" || ch === "\b") {
        if (line) { line = line.slice(0, -1); say("\b \b"); }
      } else if (ch >= " ") { line += ch; say(ch); }
    }
  }

  // ---- the board picture: BTN is a push button, input pins are switches -------------------------
  B.attachBoard = (pads, button) => {
    const press = (down) => { if (w) w.sim_set_input(9, down ? 0 : -1); };
    for (const target of [button, pads[9] && pads[9].group].filter(Boolean)) {
      target.style.cursor = "pointer";
      target.style.touchAction = "none";                     // on a touch screen: a hold presses, not scrolls
      target.addEventListener("contextmenu", (e) => e.preventDefault());   // nor opens the long-press menu
      target.addEventListener("pointerdown", (e) => { press(true); target.setPointerCapture?.(e.pointerId); });
      for (const up of ["pointerup", "pointercancel", "lostpointercapture"]) target.addEventListener(up, () => press(false));
    }
    for (const [pin, pad] of Object.entries(pads)) {
      const n = +pin;
      if (n === 9 || n === 2 || RESERVED.has(n)) continue;
      pad.group.addEventListener("click", () => {
        if (!w || ((w.sim_outputs() >>> n) & 1)) return;    // outputs drive themselves
        if (wired[n] === 1) { delete wired[n]; w.sim_set_input(n, -1); }
        else { wired[n] = 1; w.sim_set_input(n, 1); }
      });
      pad.group.setAttribute("title", "Click to switch this input between 1 (wired to 3V3) and 0");
    }
    if (button) button.setAttribute("title", "Hold to press the button: G9 reads 0 while it's held");
  };

  // ---- starting -------------------------------------------------------------------------------
  B.start = async (h) => {
    handler = h;
    document.body.classList.add("sim");
    try { await boot(); }
    catch (e) { say(`\r\nThe simulator couldn't start: ${e.message}\r\n`); return; }
    send({ type: "status", simulated: true, rate: 20 });
    lastReal = performance.now();
    setInterval(tick, 50);
    setInterval(() => send({ type: "status", simulated: true, rate: 20 }), 5000);
    // Start with the motor example on the board, running, with its operator panel.
    try {
      const text = await (await fetch(near("../examples/motor.il"))).text();
      runText(`plc.load[[\n${text}\n]] plc.run()`);
    } catch (e) { /* no example: an empty board */ }
  };

  // The Examples menu in the top bar (the page from sim/build.py site adds it).
  document.addEventListener("DOMContentLoaded", () => {
    const button = document.getElementById("btn-examples"), menu = document.getElementById("sim-examples");
    if (!button || !menu) return;
    button.onclick = () => { menu.hidden = !menu.hidden; };
    menu.querySelectorAll("[data-example]").forEach((b) => b.onclick = async () => {
      menu.hidden = true;
      const name = b.dataset.example;
      const text = await (await fetch(near(`../examples/${name}.il`))).text();
      window.Ladder?.load(`${name}.il`, text);
    });
  });

  return B;
})();
