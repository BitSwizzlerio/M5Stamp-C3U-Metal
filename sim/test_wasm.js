// test_wasm.js - check the WebAssembly engine (viewer/c3uview/web/plc.wasm) in Node, as the
// simulator's page (sim.js) drives it: load motor.il, press the button, watch G4 and G5, then
// press Stop on the operator panel by writing the command word, as the viewer does over JTAG.
//
//     node sim/test_wasm.js [path/to/plc.wasm]
"use strict";
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const file = process.argv[2] || path.join(ROOT, "viewer", "c3uview", "web", "plc.wasm");
const COMMAND = 7392, SET_M = 3;
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? "PASS" : "FAIL"}  ${what}`); if (!ok) failed++; };

const stub = new Proxy({}, { get: () => () => 0 });       // the WASI calls the C library makes: nothing to do
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(file)), { wasi_snapshot_preview1: stub }).exports;
w._initialize();
w.sim_reset_board();
check(w.sim_api_version() === 1, "API version 1");

function load(text) {
  const bytes = new TextEncoder().encode(text.replace(/\r/g, ""));
  new Uint8Array(w.memory.buffer, w.sim_text(), bytes.length).set(bytes);
  return w.sim_load(bytes.length);
}
const pin = (n) => (w.sim_pins() >>> n) & 1;
const image = () => new DataView(w.memory.buffer, w.sim_image(), w.sim_image_size());

check(load(fs.readFileSync(path.join(ROOT, "examples", "ladder", "motor.il"), "utf8")) === 1, "motor.il loads");
check(w.sim_instructions() === 9 && w.sim_rungs() === 2, `9 instructions in 2 rungs (${w.sim_instructions()} in ${w.sim_rungs()})`);
check(image().getUint32(0, true) === 0x5244414c, "plc_image starts with LADR");
w.sim_run(10);
w.sim_advance_ms(50);
check(pin(4) === 0, "G4 off at the start");
w.sim_set_input(9, 0); w.sim_advance_ms(100); w.sim_set_input(9, -1); w.sim_advance_ms(50);
check(pin(4) === 1, "a press of the button starts the motor (G4), and it stays on");
check(pin(5) === 0, "the fan (G5) waits");
w.sim_advance_ms(2100);
check(pin(5) === 1, "the fan comes on after 2 s");
image().setUint32(COMMAND, SET_M | ((1 | 0x100) << 8), true);   // M1 (Stop on the panel) to 1 ...
w.sim_advance_ms(30);
image().setUint32(COMMAND, SET_M | (1 << 8), true);             // ... and back to 0
w.sim_advance_ms(30);
check(pin(4) === 0 && pin(5) === 0, "Stop on the panel stops both");
check(image().getUint32(24, true) > 200, "it scanned every 10 ms");

check(load("LD G4\nFOO G5\n") === 0, "a bad program is refused");
const p = w.sim_error(), mem = new Uint8Array(w.memory.buffer);
const message = new TextDecoder().decode(mem.subarray(p, mem.indexOf(0, p)));
check(message.startsWith("line 2:"), `with the line in the message ("${message}")`);

console.log(failed ? `${failed} failed` : "all passed");
process.exit(failed ? 1 : 0);
