"use strict";
// ladder.js: the Ladder Logic card. Hidden until the Ladder Logic button opens it, so the viewer
// looks the same as ever to someone who hasn't got to ladder logic yet.
//
// The server reads the board's ladder engine over JTAG (c3uview/ladder.py) and sends two events:
//   'ladder-program'  the program's instruction words and its IL text, after each plc.load()
//   'ladder'          what the last scan did: which instructions passed power, timers, counters
// The drawing is worked out here from the instruction words, by the same rules as the board's
// own plc.show() (app/plc_show.c): AND and ANB put things in series, OR and ORB in parallel.

window.Ladder = (() => {
  // The instructions, in the order of enum plc_op in app/plc.h.
  const OPS = ["LD", "LDN", "AND", "ANDN", "OR", "ORN", "ANB", "ORB", "OUT",
               "LDP", "LDF", "ANDP", "ANDF", "ORP", "ORF", "SET", "RST", "TON", "TOF", "CTU"];
  const LOADS = new Set(["LD", "LDN", "LDP", "LDF"]);
  const SERIES = new Set(["AND", "ANDN", "ANDP", "ANDF"]);
  const PARALLEL = new Set(["OR", "ORN", "ORP", "ORF"]);
  const BOXES = new Set(["TON", "TOF", "CTU"]);
  const COILS = new Set(["OUT", "SET", "RST"]);
  const KINDS = ["G", "M", "T", "C"];
  const EXAMPLE = "LDN  G9      ; start: the button\nOR   G4      ; seal-in\nANDN G10     ; stop\nOUT  G4      ; motor\n" +
                  "LD   G4\nTON  T0 2000 ; wait 2 s\nOUT  G5      ; fan\n";
  const ON = "#2e9e5b", OFF = "#b7bec8", INK = "#202a35", MUTED = "#66727f";
  const STORE = "c3u-viewer.ladder-open";

  let prog = null;        // { loads, ins: [{i, op, kind, index, name, preset, line}], rungs, comments }
  let live = null;        // the last 'ladder' event
  let view = "drawing";
  let heartbeat = null;
  let watchedFor = null;  // the program whose pins were added to History

  // ---- the program --------------------------------------------------------------------------
  function decode(ev) {
    const ins = [], lines = ev.source.split("\n");
    for (let i = 0; i < ev.program.length; ) {
      const w = ev.program[i], op = OPS[w & 0x3F] || "?", kind = KINDS[(w >> 6) & 3], index = (w >> 8) & 0xFF;
      const item = { i, op, kind, index, name: kind + index, line: w >>> 16 };
      if (BOXES.has(op)) item.preset = ev.program[i + 1];
      ins.push(item);
      i += BOXES.has(op) ? 2 : 1;
    }
    // Split into rungs: contacts, then boxes, then coils; the next LD starts a new rung.
    const rungs = [];
    let rung = null;
    for (const item of ins) {
      if (LOADS.has(item.op) && (!rung || rung.boxes.length || rung.coils.length)) {
        rung = { contacts: [], boxes: [], coils: [] };
        rungs.push(rung);
      }
      if (!rung) continue;
      if (BOXES.has(item.op)) rung.boxes.push(item);
      else if (COILS.has(item.op)) rung.coils.push(item);
      else rung.contacts.push(item);
      item.rung = rungs.length - 1;
    }
    const comment = (line) => {
      const text = lines[line - 1] || "";
      const m = text.match(/(;|--|\/\/)\s*(.*)$/);
      return m ? m[2].trim() : "";
    };
    return { loads: ev.loads, ins, rungs, comment };
  }

  function traced(i) {
    return !!(live && live.current && live.scans > 0 && live.trace && ((live.trace[i >> 5] >>> (i & 31)) & 1));
  }

  // Rebuild a rung's contacts as a tree of series and parallel groups, as plc_show.c does.
  function tree(contacts) {
    const join = (type, a, b) => {
      const parts = [];
      for (const n of [a, b]) {
        if (n.type === type) parts.push(...n.parts);
        else parts.push(n);
      }
      return { type, parts };
    };
    const stack = [];
    for (const c of contacts) {
      if (LOADS.has(c.op)) stack.push({ type: "leaf", c });
      else if (SERIES.has(c.op)) stack.push(join("series", stack.pop(), { type: "leaf", c }));
      else if (PARALLEL.has(c.op)) stack.push(join("parallel", stack.pop(), { type: "leaf", c }));
      else { const b = stack.pop(); stack.push(join(c.op === "ANB" ? "series" : "parallel", stack.pop(), b)); }
    }
    return stack[0];
  }

  // ---- drawing --------------------------------------------------------------------------------
  const CELL = 64, ROW = 74, JOIN = 14, BOX_W = 150, COIL_W = 64, RAIL = 28;

  function measure(n, atRail) {
    n.atRail = atRail;
    if (n.type === "leaf") { n.w = CELL; n.rows = 1; return; }
    let w = 0, rows = 0;
    n.parts.forEach((p, k) => {
      measure(p, n.type === "series" ? atRail && k === 0 : atRail);
      if (n.type === "series") { w += p.w; rows = Math.max(rows, p.rows); }
      else { w = Math.max(w, p.w); rows += p.rows; }
    });
    if (n.type === "parallel") w += (atRail ? 0 : JOIN) + JOIN;
    n.w = w;
    n.rows = rows;
  }

  function pinColour(item) {
    if (item.kind !== "G" || !live || !live.detail) return null;
    if ((live.out_mask >>> item.index) & 1) return COLOURS.out;
    if ((live.in_mask >>> item.index) & 1) return COLOURS.in;
    return null;
  }

  function drawRungs(svg) {
    svg.textContent = "";
    const wire = (x1, y1, x2, y2, p) => {
      if (x2 !== x1 || y2 !== y1)
        el("line", { x1, y1, x2, y2, stroke: p ? ON : OFF, "stroke-width": p ? 3 : 2, "stroke-linecap": "round" }, svg);
    };
    const labels = (cx, y, item) => {
      const dot = pinColour(item);
      el("text", { x: cx + (dot ? 4 : 0), y: y - 22, "text-anchor": "middle", "font-size": 12.5, "font-weight": 700, fill: INK }, svg, item.name);
      if (dot) el("circle", { cx: cx - item.name.length * 4 - 5, cy: y - 26, r: 4, fill: dot }, svg);
      let text = prog.comment(item.line);
      if (text.length > 12) text = text.slice(0, 11) + "…";
      if (text) el("text", { x: cx, y: y + 32, "text-anchor": "middle", "font-size": 10.5, fill: MUTED }, svg, text);
    };
    const leaf = (n, x, y, p) => {
      const c = n.c, closed = traced(c.i), out = p && closed;
      const mid = x + CELL / 2, l = mid - 9, r = mid + 9;
      wire(x, y, l, y, p);
      if (closed) el("rect", { x: l, y: y - 12, width: r - l, height: 24, fill: p ? "#dff3e6" : "#eef1f5" }, svg);
      el("line", { x1: l, y1: y - 13, x2: l, y2: y + 13, stroke: p ? ON : OFF, "stroke-width": 3 }, svg);
      el("line", { x1: r, y1: y - 13, x2: r, y2: y + 13, stroke: out ? ON : OFF, "stroke-width": 3 }, svg);
      if (/N$/.test(c.op)) el("line", { x1: l - 3, y1: y + 12, x2: r + 3, y2: y - 12, stroke: out ? ON : INK, "stroke-width": 2 }, svg);
      if (/[PF]$/.test(c.op)) el("text", { x: mid, y: y + 4, "text-anchor": "middle", "font-size": 11, "font-weight": 700, fill: INK }, svg, c.op.slice(-1));
      wire(r, y, x + CELL, y, out);
      labels(mid, y, c);
      return out;
    };
    const node = (n, x, y, p) => {
      if (n.type === "leaf") return leaf(n, x, y, p);
      if (n.type === "series") {
        for (const part of n.parts) { p = node(part, x, y, p); x += part.w; }
        return p;
      }
      const left = n.atRail ? 0 : JOIN, right = x + n.w - JOIN / 2;
      let out = false, row = y, last = y;
      if (left) wire(x, y, x + JOIN / 2, y, p);
      for (const part of n.parts) {
        const q = node(part, x + left, row, p);
        wire(x + left + part.w, row, right, row, q);
        out = out || q;
        last = row;
        row += part.rows * ROW;
      }
      if (left) wire(x + JOIN / 2, y, x + JOIN / 2, last, p);
      if (n.atRail) wire(RAIL, y, RAIL, last, p);
      wire(right, y, right, last, out);
      wire(right, y, x + n.w, y, out);
      if (n.parts.length > 1) el("circle", { cx: right, cy: y, r: 4, fill: out ? ON : OFF }, svg);
      return out;
    };

    // Lay out every rung, so all the coils line up on the right.
    const laid = prog.rungs.map((r) => {
      const root = tree(r.contacts);
      measure(root, true);
      return { r, root, w: root.w + r.boxes.length * (BOX_W + 20) };
    });
    const width = Math.max(560, RAIL + Math.max(0, ...laid.map((l) => l.w)) + 40 + COIL_W + 30);
    const railP = !!(live && live.claimed);
    let y = 44;
    const tops = [];
    for (const { r, root } of laid) {
      tops.push(y);
      const rows = Math.max(root.rows, r.coils.length);
      el("text", { x: 8, y: y + 4, "font-size": 11, fill: MUTED }, svg, String(prog.rungs.indexOf(r)));
      let p = node(root, RAIL, y, railP);
      let x = RAIL + root.w;
      for (const b of r.boxes) {                                  // timers and counters, in series
        wire(x, y, x + 20, y, p);
        x += 20;
        const done = traced(b.i), t = live && live.current ? (b.op === "CTU" ? live.counters : live.timers) : null;
        const now = t && t[b.index] ? t[b.index][0] : 0;
        el("rect", { x, y: y - 30, width: BOX_W, height: 60, rx: 6, fill: "#fafbfc", stroke: p ? ON : OFF, "stroke-width": 2 }, svg);
        el("text", { x: x + 10, y: y - 12, "font-size": 12.5, "font-weight": 700, fill: INK }, svg, `${b.op}  ${b.name}`);
        el("text", { x: x + BOX_W - 10, y: y - 12, "text-anchor": "end", "font-size": 10.5, fill: MUTED }, svg,
           b.op === "TON" ? "on-delay" : b.op === "TOF" ? "off-delay" : "counter");
        el("text", { x: x + 10, y: y + 6, "font-size": 11.5, fill: INK }, svg, `${now} / ${b.preset}${b.op === "CTU" ? "" : " ms"}`);
        el("rect", { x: x + 10, y: y + 14, width: BOX_W - 20, height: 6, rx: 3, fill: "#eceff3" }, svg);
        el("rect", { x: x + 10, y: y + 14, width: (BOX_W - 20) * Math.min(1, now / b.preset), height: 6, rx: 3, fill: ON }, svg);
        x += BOX_W;
        p = done;
      }
      const cx = width - 30 - COIL_W;                           // coils, lined up on the right
      if (r.coils.length === 0) wire(x, y, width - 30, y, p);
      r.coils.forEach((c, k) => {
        const cy = y + k * ROW, on = traced(c.i);
        if (k === 0) wire(x, y, cx, y, p);
        else wire(cx - JOIN, cy, cx, cy, p);
        const mid = cx + COIL_W / 2;
        if (on) el("circle", { cx: mid, cy, r: 11, fill: c.kind === "G" ? "#fde3d6" : "#dff3e6" }, svg);
        el("path", { d: `M ${mid - 6} ${cy - 13} A 16 16 0 0 0 ${mid - 6} ${cy + 13}`, fill: "none", stroke: p ? ON : OFF, "stroke-width": 3 }, svg);
        el("path", { d: `M ${mid + 6} ${cy - 13} A 16 16 0 0 1 ${mid + 6} ${cy + 13}`, fill: "none", stroke: p ? ON : OFF, "stroke-width": 3 }, svg);
        if (c.op !== "OUT") el("text", { x: mid, y: cy + 4, "text-anchor": "middle", "font-size": 11, "font-weight": 700, fill: INK }, svg, c.op[0]);
        wire(mid + 12, cy, width - 30, cy, on);
        wire(cx, cy, mid - 12, cy, p);
        labels(mid, cy, c);
      });
      if (r.coils.length > 1) wire(cx - JOIN, y, cx - JOIN, y + (r.coils.length - 1) * ROW, p);
      y += rows * ROW + 6;
    }
    const height = Math.max(120, y - 20);
    el("line", { x1: RAIL, y1: 14, x2: RAIL, y2: height - 10, stroke: railP ? ON : OFF, "stroke-width": 4 }, svg);
    el("line", { x1: width - 30, y1: 14, x2: width - 30, y2: height - 10, stroke: OFF, "stroke-width": 4 }, svg);
    svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    svg.style.width = `${width}px`;         /* its natural size; CSS shrinks it to fit a narrow card */
  }

  // ---- the IL view ----------------------------------------------------------------------------
  function drawIL(pre) {
    const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;");
    const out = ['<span class="c">  #  instruction       on</span>'];
    let rung = -1;
    for (const item of prog.ins) {
      if (item.rung !== rung) { rung = item.rung; out.push(`<span class="c">     ; rung ${rung}</span>`); }
      let text = item.op;
      if (item.op !== "ANB" && item.op !== "ORB") text = `${item.op.padEnd(4)} ${item.name}`;
      if (BOXES.has(item.op)) text += ` ${item.preset}`;
      const scanned = live && live.current && live.scans > 0;
      const on = !scanned ? '<span class="p0">-</span>' : traced(item.i) ? '<span class="p1">1</span>' : '<span class="p0">0</span>';
      let extra = "";
      if (BOXES.has(item.op) && scanned) {
        const t = item.op === "CTU" ? live.counters : live.timers, now = t && t[item.index] ? t[item.index][0] : 0;
        extra = `${now} / ${item.preset}${item.op === "CTU" ? "" : " ms"}`;
      }
      const comment = prog.comment(item.line);
      out.push(`${String(item.i).padStart(3)}  ${esc(text.padEnd(18))}${on}   ${esc(extra.padEnd(16))}` +
               (comment ? `<span class="c">; ${esc(comment)}</span>` : ""));
    }
    pre.innerHTML = out.join("\n");
  }

  // ---- the card -------------------------------------------------------------------------------
  function pins(mask) {
    const list = [];
    for (let n = 0; n < 22; n++) if ((mask >>> n) & 1) list.push("G" + n);
    return list.length ? list.join(" ") : "none";
  }

  function render() {
    const card = $("ladder-card");
    const pill = $("pill-plc");
    pill.hidden = !(live && live.present && (live.running || !card.hidden && prog && prog.ins.length));
    if (live && live.present) {
      pill.textContent = live.running ? `Ladder: running${live.detail ? `, ${live.period_ms} ms scan` : ""}` : "Ladder: stopped";
      pill.className = "pill " + (live.running ? "good" : "quiet");
    }
    if (card.hidden) return;

    let state = "body";
    if (!live) state = "waiting";
    else if (!live.present) state = "old";
    else if (!prog || prog.loads !== live.loads) state = "waiting";
    else if (!prog.ins.length) state = "empty";
    for (const s of ["waiting", "old", "empty", "body"]) $(`ladder-${s}`).hidden = s !== state;

    const has = state === "body";
    $("btn-plc-run").disabled = !has;
    $("btn-plc-run").textContent = live && live.running ? "Stop ladder" : "Run ladder";
    $("btn-il-open").disabled = !live || !live.present;
    $("ladder-hint").textContent = !has ? "" : live.running
      ? `running, a scan every ${live.period_ms} ms; ${live.scans} scans, the last took ${live.scan_us} µs`
      : "stopped: Run ladder starts it";
    if (!has) return;
    if (watchedFor !== prog.loads && live.detail) {          // show the ladder's pins in History
      watchedFor = prog.loads;
      for (let n = 0; n < 22; n++) if (((live.in_mask | live.out_mask) >>> n) & 1) watched.add(n);
    }
    $("ladder-io").innerHTML = `<span><i class="sw in-hi"></i><b>inputs</b> ${pins(live.in_mask)}</span>` +
                   `<span><i class="sw out-hi"></i><b>outputs</b> ${pins(live.out_mask)}</span>` +
                   `<span>green wires have power</span>`;
    if (view === "drawing") drawRungs($("ladder-svg"));
    else drawIL($("ladder-il"));
  }

  // ---- typing into the console ----------------------------------------------------------------
  async function type(name, text) {
    const result = await api("/api/run", { name, text });
    if (result.result !== "ok") log(`Couldn't type ${name}: ${result.result}`, true);
  }

  function load(name, il) {
    il = il.replace(/\r\n?/g, "\n").replace(/\n+$/, "");
    if (il.includes("]]")) return log(`${name} contains "]]", which would end the program early.`, true);
    if (il.split("\n").some((l) => l.length > 480)) return log(`${name} has a line longer than 480 characters.`, true);
    if (il.length > 4000) return log(`${name} is too long: the board keeps up to 4000 characters, comments included.`, true);
    // Blank lines become ";" so the board's line numbers in errors match the file's.
    const lines = il.split("\n").map((l) => (l.trim() ? l : ";"));
    type(name, ["plc.load[[", ...lines, "]] plc.run()"].join("\n"));
  }

  // ---- opening and closing --------------------------------------------------------------------
  function setOpen(open) {
    $("ladder-card").hidden = !open;
    $("btn-ladder").classList.toggle("on", open);
    $("btn-ladder").setAttribute("aria-pressed", String(open));
    try { localStorage.setItem(STORE, open ? "1" : "0"); } catch (e) { /* private window: just don't remember */ }
    clearInterval(heartbeat);
    api("/api/ladder", { open });
    if (open) heartbeat = setInterval(() => api("/api/ladder", { open: true }), 3000);
    render();
  }

  function setView(v) {
    view = v;
    $("view-drawing").setAttribute("aria-pressed", String(v === "drawing"));
    $("view-il").setAttribute("aria-pressed", String(v === "il"));
    $("ladder-drawing").hidden = v !== "drawing";
    $("ladder-il").hidden = v !== "il";
    render();
  }

  $("btn-ladder").onclick = () => setOpen($("ladder-card").hidden);
  $("view-drawing").onclick = () => setView("drawing");
  $("view-il").onclick = () => setView("il");
  $("btn-example").onclick = () => load("the example (motor and fan)", EXAMPLE);
  $("btn-il-open").onclick = () => $("file-il").click();
  $("file-il").onchange = async (e) => {
    const file = e.target.files[0];
    e.target.value = "";
    if (file) load(file.name, await file.text());
  };
  $("btn-plc-run").onclick = () => type(live && live.running ? "plc.stop()" : "plc.run()",
                                        live && live.running ? "plc.stop()" : "plc.run()");
  // Open it again if it was open last time, or if the address ends in #ladder (handy for a bookmark).
  let remembered = location.hash === "#ladder";
  try { remembered = remembered || localStorage.getItem(STORE) === "1"; } catch (e) { /* no storage */ }
  if (remembered) setOpen(true);

  return {
    program(ev) { prog = decode(ev); render(); },
    state(ev) { live = ev; render(); },
    // A line for the pin details panel, or null.
    pinNote(pin) {
      if (!live || !live.present || !live.claimed || !live.detail || !prog) return null;
      const uses = prog.ins.filter((i) => i.kind === "G" && i.index === pin);
      if ((live.out_mask >>> pin) & 1) {
        const coil = uses.find((i) => COILS.has(i.op));
        return `the coil ( G${pin} ) in rung ${coil ? coil.rung : "?"} drives this pin. ` +
               `gpio.write(${pin}, …) is refused while the ladder runs.`;
      }
      if ((live.in_mask >>> pin) & 1) {
        const rungs = [...new Set(uses.map((i) => i.rung))].join(", ");
        return `an input to the ladder, read in rung ${rungs}. gpio.read(${pin}) still works.`;
      }
      return null;
    },
  };
})();
