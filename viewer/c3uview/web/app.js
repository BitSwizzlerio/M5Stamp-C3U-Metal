"use strict";
// C3U Viewer: the page. The Python program sends events (/events) and takes requests (/api/...).

const $ = (id) => document.getElementById(id);
const SVG = "http://www.w3.org/2000/svg";
const api = (path, body) =>
  fetch(path, { method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN },
                body: JSON.stringify(body || {}) }).then((r) => r.json()).catch(() => ({ result: "no answer" }));

// ---------------------------------------------------------------------------------------
// The board: pads where they are on the M5Stamp C3U (see images/C122-B_02.jpg).
// ---------------------------------------------------------------------------------------
const LEFT = ["3", "4", "5", "6", "7", "8", "10", "5V", "GND", "5V", "1", "0"];
const RIGHT = ["3V3", "21", "20", "EN", "9", "GND", "18", "19", "5V", "GND"];
const ROW0 = 70, ROW = 37, XL = 150, XR = 370;   // the board spans x 150-370 of a 520-wide drawing
const EXTRA = { 21: "TX", 20: "RX", 18: "D−", 19: "D+", 9: "BTN" };
const pads = {};               // gpio number -> { group, ring, fill, state }
let pins = [];                 // the latest decoded pins, indexed by gpio
let chipInfo = {};
let selected = null;
let button = null;             // the drawn button, shown pressed while GPIO9 reads 0

function el(name, attrs, parent, text) {
  const e = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (text !== undefined) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}

function drawBoard() {
  const svg = $("board");
  el("rect", { x: XL, y: 30, width: XR - XL, height: 500, rx: 22, fill: "#fafbfc", stroke: "#c9ced6", "stroke-width": 2 }, svg);
  el("text", { x: 260, y: 84, "text-anchor": "middle", "font-size": 34, "font-weight": 800, fill: "#202a35" }, svg, "C3U");
  el("text", { x: 260, y: 104, "text-anchor": "middle", "font-size": 13, fill: "#c0392b" }, svg, "ESP32-C3");
  // the RGB LED and the button, in the middle of the board
  el("text", { x: 260, y: 300, "text-anchor": "middle", "font-size": 11, fill: "#66727f" }, svg, "STATUS LED: G2");
  pad(svg, 2, 260, 330, "", "middle");
  el("text", { x: 260, y: 380, "text-anchor": "middle", "font-size": 11, fill: "#66727f" }, svg, "BUTTON: G9");
  button = el("rect", { x: 236, y: 390, width: 48, height: 22, rx: 11, fill: "#8bc34a", stroke: "#5f8f2c", "stroke-width": 2 }, svg);
  el("text", { x: 260, y: 405, "text-anchor": "middle", "font-size": 11, "font-weight": 700, fill: "#fff", "pointer-events": "none" }, svg, "BTN");
  // the USB connector
  el("rect", { x: 215, y: 500, width: 90, height: 40, rx: 8, fill: "#3d8ee0" }, svg);
  el("text", { x: 260, y: 526, "text-anchor": "middle", "font-size": 15, "font-weight": 700, fill: "#fff" }, svg, "USB");
  el("text", { x: 260, y: 470, "text-anchor": "middle", "font-size": 11, fill: "#66727f" }, svg, "USB data: G18, G19");

  LEFT.forEach((name, i) => header(svg, name, XL, ROW0 + i * ROW, "end"));
  RIGHT.forEach((name, i) => header(svg, name, XR, ROW0 + (i + 2) * ROW, "start"));
}

function header(svg, name, x, y, anchor) {
  if (/^\d+$/.test(name)) return pad(svg, Number(name), x, y, anchor);
  const g = el("g", {}, svg);
  el("rect", { x: x - 16, y: y - 12, width: 32, height: 24, rx: 12, fill: name === "GND" ? "#2b2f36" : name === "EN" ? "#e3b51b" : "#d23b3b" }, g);
  el("text", { x, y: y + 4, "text-anchor": "middle", "font-size": 10.5, "font-weight": 700, fill: "#fff" }, g, name);
}

function pad(svg, gpio, x, y, anchor) {
  const g = el("g", { class: "pad", "data-pin": gpio }, svg);
  const ring = el("circle", { class: "ring", cx: x, cy: y, r: 14, fill: "#fff", stroke: "#b7bec8", "stroke-width": 3 }, g);
  const fill = el("circle", { cx: x, cy: y, r: 9, fill: "#fff" }, g);
  el("text", { x, y: y + 4, "text-anchor": "middle", "font-size": 11, "font-weight": 700, fill: "#202a35", "pointer-events": "none" }, g, gpio);
  let state = null;
  if (anchor !== "middle") {
    const dx = anchor === "end" ? -24 : 24;
    el("text", { x: x + dx, y: y - 2, "text-anchor": anchor, "font-size": 12.5, "font-weight": 700, fill: "#202a35" }, g,
       "G" + gpio + (EXTRA[gpio] ? " " + EXTRA[gpio] : ""));
    state = el("text", { x: x + dx, y: y + 12, "text-anchor": anchor, "font-size": 10.5, fill: "#66727f" }, g, "");
  } else {
    state = el("text", { x, y: y + 30, "text-anchor": "middle", "font-size": 10.5, fill: "#66727f" }, g, "");
  }
  g.addEventListener("click", () => select(gpio));
  pads[gpio] = { group: g, ring, fill, state };
}

const COLOURS = { out: "#f06a2b", in: "#2e9e5b", pwm: "#8a5cd1", analog: "#1a9c94", periph: "#3d7fd1", off: "#b7bec8", reserved: "#d5d9df" };

function shortState(p) {
  if (p.pwm && p.pwm.running) return `PWM ${fmtHz(p.pwm.hz)} ${p.pwm.duty}%`;
  if (p.pwm) return "PWM stopped";
  switch (p.mode) {
    case "output": return p.driven_by && p.driven_by !== "GPIO_OUT" ? `out: ${p.driven_by}` : `out ${p.out_level}`;
    case "input": return `in ${p.level}` + (p.pull === "up" ? " ↑" : p.pull === "down" ? " ↓" : "");
    case "analog": return "analog";
    case "reserved": return p.note.startsWith("USB") ? "USB" : "flash";
    case "peripheral": return p.function;
    default: return "off";
  }
}

function fmtHz(hz) { return hz >= 1000 ? `${+(hz / 1000).toFixed(2)} kHz` : `${+hz.toFixed(1)} Hz`; }

function paint(p) {
  const pd = pads[p.pin];
  if (!pd) return;
  let ring = COLOURS.off, fill = "#fff";
  if (p.pwm) { ring = COLOURS.pwm; fill = p.pwm.running ? COLOURS.pwm : "#fff"; }
  else if (p.mode === "output") { ring = COLOURS.out; fill = (p.level ?? p.out_level) ? COLOURS.out : "#fff"; }
  else if (p.mode === "input") { ring = COLOURS.in; fill = p.level ? COLOURS.in : "#fff"; }
  else if (p.mode === "analog") { ring = COLOURS.analog; fill = COLOURS.analog; }
  else if (p.mode === "peripheral") { ring = COLOURS.periph; fill = p.level ? "#cfe0f6" : "#fff"; }
  else if (p.mode === "reserved") { ring = COLOURS.reserved; fill = COLOURS.reserved; }
  pd.ring.setAttribute("stroke", ring);
  pd.fill.setAttribute("fill", fill);
  pd.fill.setAttribute("opacity", p.pwm && p.pwm.running ? 0.35 + 0.65 * p.pwm.duty / 100 : 1);
  if (pd.state) pd.state.textContent = shortState(p);
  if (p.pin === 9 && button) {
    const pressed = p.level === 0;
    button.setAttribute("fill", pressed ? "#4e7d1c" : "#8bc34a");
    button.setAttribute("transform", pressed ? "translate(0 2)" : "");
  }
}

// ---------------------------------------------------------------------------------------
// The detail panel for the selected pin.
// ---------------------------------------------------------------------------------------
function select(gpio) {
  if (selected !== null && pads[selected]) pads[selected].group.classList.remove("selected");
  selected = gpio;
  pads[gpio].group.classList.add("selected");
  showDetails();
}

function showDetails() {
  const p = pins[selected];
  if (!p) return;
  const rows = [
    ["Mode", p.mode],
    ["Level", p.level === null ? "not readable (input switched off)" : p.level],
    ["Pull", p.pull === "none" ? "none" : `pull-${p.pull} resistor on`],
    ["Pad function", p.function === "GPIO" ? "GPIO (through the GPIO matrix)" : `${p.function} (straight from IO_MUX)`],
  ];
  if (p.driven_by) rows.push(["Driven by", p.driven_by === "GPIO_OUT" ? `the GPIO_OUT register, set to ${p.out_level}` : p.driven_by]);
  if (p.pwm && p.pwm.running) rows.push(["PWM", `LEDC channel ${p.pwm.channel} on timer ${p.pwm.timer}: ${fmtHz(p.pwm.hz)}, ` +
                                                  `high ${p.pwm.duty}% of the time, ${p.pwm.bits}-bit duty`]);
  if (p.mode === "analog" && chipInfo.adc_last !== undefined) rows.push(["ADC", `last reading ${chipInfo.adc_last} of 4095 (attenuation ${chipInfo.adc_atten})`]);
  if (p.feeds.length) rows.push(["Feeds", p.feeds.join(", ")]);
  rows.push(["Drive strength", `${p.drive} of 3`]);
  if (p.note) rows.push(["On this board", p.note]);
  const watching = watched.has(selected) ? "checked" : "";
  const html =
    `<h2>GPIO${p.pin}</h2><p class="summary">${escapeHtml(p.summary)}</p>` +
    `<table>${rows.map(([k, v]) => `<tr><td>${k}</td><td>${escapeHtml(String(v))}</td></tr>`).join("")}</table>` +
    `<label class="hint"><input type="checkbox" id="watch" ${watching}> show in History</label>`;
  if (html === lastDetails) return;       // redraw only on change, so clicks aren't lost
  lastDetails = html;
  $("details").innerHTML = html;
  $("watch").addEventListener("change", (e) => {
    e.target.checked ? watched.add(selected) : watched.delete(selected);
    lastDetails = null;
  });
}
let lastDetails = null;

function escapeHtml(s) { return s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])); }

// ---------------------------------------------------------------------------------------
// History: the last 10 seconds of the watched pins.
// ---------------------------------------------------------------------------------------
const HISTORY_S = 10;
const watched = new Set([9]);
const trace = {};               // gpio -> [[time, value 0..1 or null], ...]
const lastMode = {};

function record(t, list) {
  for (const p of list) {
    const value = p.pwm ? (p.pwm.running ? p.pwm.duty / 100 : 0) : (p.level ?? p.out_level ?? null);
    (trace[p.pin] ||= []).push([t, value, !!p.pwm]);
    while (trace[p.pin].length && trace[p.pin][0][0] < t - HISTORY_S - 1) trace[p.pin].shift();
    // Start watching a pin as soon as a program makes it an output or gives it PWM.
    const mode = p.pwm ? "pwm" : p.mode;
    if (lastMode[p.pin] !== undefined && mode !== lastMode[p.pin] && (mode === "output" || mode === "pwm") && !p.reserved)
      watched.add(p.pin);
    lastMode[p.pin] = mode;
  }
}

function drawHistory(now) {
  const canvas = $("history");
  const width = canvas.clientWidth;
  if (canvas.width !== width * devicePixelRatio) canvas.width = width * devicePixelRatio;
  const rows = [...watched].sort((a, b) => a - b);
  const rowH = 30, height = Math.max(60, rows.length * rowH + 22);
  if (canvas.height !== height * devicePixelRatio) { canvas.height = height * devicePixelRatio; canvas.style.height = height + "px"; }
  const g = canvas.getContext("2d");
  g.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
  g.clearRect(0, 0, width, height);
  g.font = "12px system-ui, sans-serif";
  const x0 = 64, x1 = width - 8, xs = (t) => x0 + (x1 - x0) * (1 - (now - t) / HISTORY_S);
  g.fillStyle = "#66727f";
  for (let s = 0; s <= HISTORY_S; s += 2) g.fillText(s === 0 ? "now" : `-${s}s`, xs(now - s) - 10, height - 4);
  if (!rows.length) { g.fillText("Click a pin and tick 'show in History'.", x0, 30); return; }
  rows.forEach((pin, i) => {
    const top = 6 + i * rowH, bottom = top + rowH - 10;
    g.fillStyle = "#202a35";
    g.fillText(`G${pin}`, 8, bottom - 4);
    g.strokeStyle = "#e3e7ec"; g.beginPath(); g.moveTo(x0, bottom + 0.5); g.lineTo(x1, bottom + 0.5); g.stroke();
    const points = (trace[pin] || []).filter(([t]) => t >= now - HISTORY_S - 0.2);
    const pwm = points.some((p) => p[2]);
    const mode = pins[pin] ? pins[pin].mode : "off";
    g.strokeStyle = pwm ? COLOURS.pwm : mode === "input" ? COLOURS.in : mode === "analog" ? COLOURS.analog : COLOURS.out;
    g.lineWidth = 2;
    g.beginPath();
    let started = false, prevY = null;
    for (const [t, v] of points) {
      if (v === null) { started = false; continue; }
      const x = Math.max(x0, xs(t)), y = bottom - v * (rowH - 14);
      if (!started) { g.moveTo(x, y); started = true; }
      else if (pwm) g.lineTo(x, y);
      else { g.lineTo(x, prevY); g.lineTo(x, y); }
      prevY = y;
    }
    if (started) g.lineTo(x1, prevY);
    g.stroke();
    g.lineWidth = 1;
  });
}

// ---------------------------------------------------------------------------------------
// The terminal.
// ---------------------------------------------------------------------------------------
const term = new Terminal({ cursorBlink: true, fontSize: 14, scrollback: 5000,
  fontFamily: "Consolas, 'Cascadia Mono', ui-monospace, monospace", theme: { background: "#111820" } });
const fit = new FitAddon.FitAddon();
term.loadAddon(fit);
term.open($("terminal"));
fit.fit();
term.write("\x1b[2mConnected to the viewer. Press Enter for a Lua prompt.\x1b[0m\r\n");
new ResizeObserver(() => fit.fit()).observe($("terminal"));

let sending = Promise.resolve();
function sendKeys(text) {
  const bytes = new TextEncoder().encode(text);
  let bin = "";
  bytes.forEach((b) => (bin += String.fromCharCode(b)));
  const data = btoa(bin);
  sending = sending.then(() => api("/api/keys", { data }));
}

term.onData((d) => {
  // Several lines at once is a paste: type them in one by one, waiting for each prompt.
  if (d.length > 1 && d.split("\r").length > 2) {
    api("/api/run", { name: "the pasted lines", text: d.replace(/\r\n?/g, "\n") });
    return;
  }
  sendKeys(d);
});

function b64bytes(s) {
  const bin = atob(s), out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

// ---------------------------------------------------------------------------------------
// Status, messages and buttons.
// ---------------------------------------------------------------------------------------
function pill(id, kind, text) { const p = $(id); p.className = "pill " + kind; p.textContent = text; }

function showStatus(s) {
  if (s.serial.connected) pill("pill-serial", "good", `Console: ${s.serial.port}`);
  else if (s.serial.problem === "busy") pill("pill-serial", "bad", "Console: port in use by another program");
  else if (s.serial.problem === "permission") pill("pill-serial", "bad", "Console: no permission (see above)");
  else pill("pill-serial", "bad", "Console: board not found");
  const j = s.jtag;
  if (j.state === "ready") pill("pill-jtag", "good", `Pins: live, ${j.rate || "…"} reads/s`);
  else if (j.state === "error") pill("pill-jtag", "bad", `Pins: ${j.problem}`);
  else if (s.busy === "flashing") pill("pill-jtag", "warn", "Pins: paused while flashing");
  else pill("pill-jtag", "warn", "Pins: connecting…");
  $("btn-run").disabled = !!s.busy || !s.serial.connected;
  $("btn-flash").disabled = !!s.busy;
  $("bundled-name").textContent = s.firmware || "C3U-Metal (not bundled)";
  showSetup(s.setup, s.busy);
}

let lastSetup = "";
function showSetup(setup, busy) {
  const box = $("setup");
  const key = JSON.stringify([setup, busy === "installing"]);
  if (key === lastSetup) return;
  lastSetup = key;
  box.hidden = !setup;
  if (!setup) { box.innerHTML = ""; return; }
  box.innerHTML = `<p>${escapeHtml(setup.message)}</p>`;
  const diag = document.createElement("button");
  diag.textContent = "Copy diagnostics";
  diag.onclick = showDiagnostics;
  box.appendChild(diag);
  if (setup.kind === "windows-driver") {
    const b = document.createElement("button");
    b.className = "primary";
    b.textContent = busy === "installing" ? "Installing…" : "Install the driver";
    b.disabled = busy === "installing";
    b.onclick = () => api("/api/install-driver").then((r) => r.result !== "ok" && log(`Couldn't install: ${r.result}`, true));
    box.appendChild(b);
  } else if (setup.command) {
    const copy = document.createElement("button");
    copy.textContent = "Copy";
    copy.onclick = () => navigator.clipboard.writeText(setup.command).then(() => (copy.textContent = "Copied"));
    box.appendChild(copy);
    const code = document.createElement("code");
    code.textContent = setup.command;
    box.appendChild(code);
  }
}

function log(text, error) {
  const line = document.createElement("div");
  line.textContent = text;
  if (error || /fail|error/i.test(text)) line.className = "err";
  const box = $("log");
  box.appendChild(line);
  while (box.childNodes.length > 300) box.removeChild(box.firstChild);
  box.scrollTop = box.scrollHeight;
}

async function showDiagnostics() {
  $("diag-text").value = "Collecting…";
  $("diag-dialog").showModal();
  $("diag-text").value = (await api("/api/diagnostics")).result;
}
$("btn-diag").onclick = showDiagnostics;
$("diag-copy").onclick = () => {
  const text = $("diag-text");
  navigator.clipboard.writeText(text.value).then(() => ($("diag-copy").textContent = "Copied"),
    () => { text.select(); document.execCommand("copy"); });
};

$("btn-stop").onclick = () => api("/api/stop");
$("btn-run").onclick = () => $("file-run").click();
$("file-run").onchange = async (e) => {
  const file = e.target.files[0];
  e.target.value = "";
  if (!file) return;
  const result = await api("/api/run", { name: file.name, text: await file.text() });
  if (result.result !== "ok") log(`Couldn't run ${file.name}: ${result.result}`, true);
  term.focus();
};

$("btn-flash").onclick = () => $("flash-dialog").showModal();
$("flash-dialog").addEventListener("close", async () => {
  if ($("flash-dialog").returnValue !== "go") return;
  const useFile = document.querySelector('input[name="image"]:checked').value === "file";
  const body = { erase: $("flash-erase").checked };
  if (useFile) {
    const file = $("flash-file").files[0];
    if (!file) { log("Choose a .bin file to flash.", true); return; }
    const bytes = new Uint8Array(await file.arrayBuffer());
    let bin = "";
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
    body.image = btoa(bin);
    body.name = file.name;
  }
  const result = await api("/api/flash", body);
  if (result.result !== "ok") log(`Couldn't flash: ${result.result}`, true);
});

// ---------------------------------------------------------------------------------------
// Events from the viewer program.
// ---------------------------------------------------------------------------------------
let failures = 0;
function connect() {
  const events = new EventSource(`/events?token=${encodeURIComponent(TOKEN)}`);
  events.onopen = () => (failures = 0);
  events.onmessage = (m) => {
    const ev = JSON.parse(m.data);
    if (ev.type === "term") term.write(b64bytes(ev.data));
    else if (ev.type === "pins") {
      pins = ev.pins;
      chipInfo = ev.chip;
      ev.pins.forEach(paint);
      record(ev.t, ev.pins);
      if (selected !== null) showDetails();
      $("pill-cpu").textContent = `CPU ${Math.round(ev.chip.cpu_hz / 1e6)} MHz`;
    } else if (ev.type === "status") showStatus(ev);
    else if (ev.type === "log") log(ev.text);
  };
  events.onerror = () => {
    if (++failures > 3) {
      pill("pill-serial", "bad", "The viewer program has stopped: start it again, then reload this page");
      pill("pill-jtag", "bad", "Pins: no viewer");
      events.close();
    }
  };
}

function animate() {
  drawHistory(Date.now() / 1000);
  requestAnimationFrame(animate);
}

drawBoard();
connect();
requestAnimationFrame(animate);
term.focus();
