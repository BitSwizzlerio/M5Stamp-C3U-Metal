"use strict";
// ladder.js: the Ladder Logic card. Hidden until the Ladder Logic button opens it, so the viewer
// looks the same as ever to someone who hasn't got to ladder logic yet.
//
// The server reads the board's ladder engine over JTAG (c3uview/ladder.py) and sends two events:
//   'ladder-program'  the program's instruction words and its IL text, after each plc.load()
//   'ladder'          what the last scan did: which instructions passed power, timers, counters
// The drawing is worked out here from the instruction words, by the same rules as the board's
// own plc.show() (app/plc_show.c): AND and ANB put things in series, OR and ORB in parallel.
//
// Clicking an M contact or coil switches that M bit on the board, as plc.set() would. The click
// goes to the ladder over JTAG (ladder.py's commands), so it works while a Lua program runs.

window.Ladder = (() => {
  // The instructions, in the order of enum plc_op in app/plc.h.
  const OPS = ["LD", "LDN", "AND", "ANDN", "OR", "ORN", "ANB", "ORB", "OUT",
               "LDP", "LDF", "ANDP", "ANDF", "ORP", "ORF", "SET", "RST", "TON", "TOF", "CTU",
               "TP", "RTO", "CTD", "LDC", "ANDC", "ORC", "MOV", "ADD", "SUB", "MUL", "DIV"];
  const LOADS = new Set(["LD", "LDN", "LDP", "LDF", "LDC"]);
  const SERIES = new Set(["AND", "ANDN", "ANDP", "ANDF", "ANDC"]);
  const PARALLEL = new Set(["OR", "ORN", "ORP", "ORF", "ORC"]);
  const COMPARES = new Set(["LDC", "ANDC", "ORC"]);
  const MATHS = new Set(["MOV", "ADD", "SUB", "MUL", "DIV"]);
  const VALUES = { LDC: 2, ANDC: 2, ORC: 2, MOV: 2, ADD: 3, SUB: 3, MUL: 3, DIV: 3 };   // value words that follow
  const CMP = ["=", "<>", ">", ">=", "<", "<="];
  const CMP_SHOWN = ["=", "≠", ">", "≥", "<", "≤"];
  const MATH_SIGN = { ADD: "+", SUB: "−", MUL: "×", DIV: "÷" };
  const BOXES = new Set(["TON", "TOF", "CTU", "TP", "RTO", "CTD"]);
  const BOX_WHAT = { TON: "on-delay", TOF: "off-delay", TP: "pulse", RTO: "retentive", CTU: "count up", CTD: "count down" };
  const COILS = new Set(["OUT", "SET", "RST", "MOV", "ADD", "SUB", "MUL", "DIV"]);  // what ends a rung
  const KINDS = ["G", "M", "T", "C"];
  const VALUE_KINDS = ["G", "M", "T", "C", "D", ""];      // a value word's kind; "" is a plain number
  // examples/ladder/motor.il, with its operator panel (hmi.js reads the @ lines).
  const EXAMPLE = "; @button M0 Start\n; @button M1 Stop\n; @lamp   G4 Motor\n; @lamp   G5 Fan green\n; @show   T0 Fan delay\n" +
                  "LDN  G9      ; start: the button\nOR   M0      ; or Start on the panel\nOR   G4      ; seal-in\n" +
                  "ANDN G10     ; stop\nANDN M1      ; or Stop on the panel\nOUT  G4      ; motor\n" +
                  "LD   G4\nTON  T0 2000 ; wait 2 s\nOUT  G5      ; fan\n";
  const ON = "#2e9e5b", OFF = "#b7bec8", INK = "#202a35", MUTED = "#66727f";
  const STORE = "c3u-viewer.ladder-open";

  let prog = null;        // { loads, ins: [{i, op, kind, index, name, preset, values, cmp, line}], rungs, comment }
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
      const nv = VALUES[op] || 0;
      if (nv) {
        item.kind = item.name = "";
        item.values = ev.program.slice(i + 1, i + 1 + nv).map((v) => {
          const k = VALUE_KINDS[v >>> 28] ?? "", n = v & 0x0FFFFFFF;
          return k ? { kind: k, index: n, text: k + n } : { kind: "", number: (n << 4) >> 4, text: String((n << 4) >> 4) };
        });
        if (COMPARES.has(op)) item.cmp = index % 6;
      }
      ins.push(item);
      i += BOXES.has(op) ? 2 : 1 + nv;
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
    return { loads: ev.loads, ins, rungs, comment, source: ev.source };
  }

  // A value's number now: a D register, a timer's ms, a count, or the number itself. null if not read yet.
  function valueNow(v) {
    if (v.kind === "") return v.number;
    if (!live || !live.current) return null;
    const table = v.kind === "D" ? live.d : v.kind === "T" ? live.timers : v.kind === "C" ? live.counters : null;
    const got = table ? table[v.index] : undefined;
    return got === undefined ? null : Array.isArray(got) ? got[0] : got;
  }

  // "LD>= D0 100", "ADD D0 1 D0": an instruction as it would be typed.
  function ilText(item) {
    const values = item.values ? " " + item.values.map((v) => v.text).join(" ") : "";
    if (COMPARES.has(item.op))
      return ((item.op === "LDC" ? "LD" : item.op === "ANDC" ? "AND" : "OR") + CMP[item.cmp]).padEnd(4) + values;
    if (item.values) return item.op.padEnd(4) + values;
    if (item.op === "ANB" || item.op === "ORB") return item.op;
    return `${item.op.padEnd(4)} ${item.name}` + (BOXES.has(item.op) ? ` ${item.preset}` : "");
  }

  function mBit(n) {
    return !!(live && live.m && ((live.m[n >> 5] >>> (n & 31)) & 1));
  }

  // Make the area of an M contact or coil clickable, to switch the M bit. An M bit that an OUT
  // coil drives would only go back at the next scan, so that one just says why it can't be switched.
  function forceTarget(svg, item, x, y, w, h) {
    if (item.kind !== "M" || !live || !live.detail) return;
    const n = item.index, now = mBit(n);
    const out = prog.ins.find((i) => i.op === "OUT" && i.kind === "M" && i.index === n);
    const r = el("rect", { x, y, width: w, height: h, rx: 6, class: out ? "force-no" : "force" }, svg);
    if (out) {
      el("title", {}, r, `M${n} is ${now ? 1 : 0}. The OUT coil in rung ${out.rung} drives it, so it can't be switched by hand.`);
      return;
    }
    el("title", {}, r, `M${n} is ${now ? 1 : 0}: click to set it to ${now ? 0 : 1}, like plc.set("M${n}", ${now ? 0 : 1})`);
    r.addEventListener("click", async () => {
      const result = await api("/api/ladder-command", { command: "set", m: n, value: !mBit(n) });
      if (result.result !== "ok") log(`Couldn't switch M${n}: ${result.result}`, true);
    });
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
  const CELL = 64, ROW = 74, JOIN = 14, BOX_W = 150, COIL_W = 64, MATH_W = 150, CMP_W = 128, RAIL = 28;

  function measure(n, atRail) {
    n.atRail = atRail;
    if (n.type === "leaf") { n.w = COMPARES.has(n.c.op) ? CMP_W : CELL; n.rows = 1; return; }
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
    // A compare contact: a box holding the comparison, with the values it compared under it.
    const compareLeaf = (n, x, y, p) => {
      const c = n.c, closed = traced(c.i), out = p && closed;
      const l = x + 10, r = x + CMP_W - 10;
      wire(x, y, l, y, p);
      el("rect", { x: l, y: y - 15, width: r - l, height: 30, rx: 5, fill: closed ? (p ? "#dff3e6" : "#eef1f5") : "#fafbfc",
                   stroke: out ? ON : p ? ON : OFF, "stroke-width": 2 }, svg);
      const [a, b] = c.values;
      el("text", { x: (l + r) / 2, y: y + 4.5, "text-anchor": "middle", "font-size": 12.5, "font-weight": 700, fill: INK }, svg,
         `${a.text} ${CMP_SHOWN[c.cmp]} ${b.text}`);
      const now = c.values.filter((v) => v.kind !== "").map((v) => `${v.text} = ${valueNow(v) ?? "?"}`).join(", ");
      if (now) el("text", { x: (l + r) / 2, y: y - 22, "text-anchor": "middle", "font-size": 10.5, fill: MUTED }, svg, now);
      let text = prog.comment(c.line);
      if (text.length > 22) text = text.slice(0, 21) + "…";
      if (text) el("text", { x: (l + r) / 2, y: y + 32, "text-anchor": "middle", "font-size": 10.5, fill: MUTED }, svg, text);
      wire(r, y, x + CMP_W, y, out);
      return out;
    };
    const leaf = (n, x, y, p) => {
      if (COMPARES.has(n.c.op)) return compareLeaf(n, x, y, p);
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
      forceTarget(svg, c, x + 4, y - 38, CELL - 8, 60);
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
    const outW = prog.ins.some((i) => MATHS.has(i.op)) ? MATH_W : COIL_W;   // the column the coils go in
    const width = Math.max(560, RAIL + Math.max(0, ...laid.map((l) => l.w)) + 40 + outW + 30);
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
        const done = traced(b.i), t = live && live.current ? (b.kind === "C" ? live.counters : live.timers) : null;
        const now = t && t[b.index] ? t[b.index][0] : 0;
        el("rect", { x, y: y - 30, width: BOX_W, height: 60, rx: 6, fill: "#fafbfc", stroke: p ? ON : OFF, "stroke-width": 2 }, svg);
        el("text", { x: x + 10, y: y - 12, "font-size": 12.5, "font-weight": 700, fill: INK }, svg, `${b.op}  ${b.name}`);
        el("text", { x: x + BOX_W - 10, y: y - 12, "text-anchor": "end", "font-size": 10.5, fill: MUTED }, svg, BOX_WHAT[b.op] || "");
        el("text", { x: x + 10, y: y + 6, "font-size": 11.5, fill: INK }, svg, `${now} / ${b.preset}${b.kind === "C" ? "" : " ms"}`);
        el("rect", { x: x + 10, y: y + 14, width: BOX_W - 20, height: 6, rx: 3, fill: "#eceff3" }, svg);
        el("rect", { x: x + 10, y: y + 14, width: (BOX_W - 20) * Math.min(1, now / b.preset), height: 6, rx: 3, fill: ON }, svg);
        x += BOX_W;
        p = done;
      }
      const cx = width - 30 - outW;                             // coils, lined up on the right
      if (r.coils.length === 0) wire(x, y, width - 30, y, p);
      r.coils.forEach((c, k) => {
        const cy = y + k * ROW, on = traced(c.i);
        if (k === 0) wire(x, y, cx, y, p);
        else wire(cx - JOIN, cy, cx, cy, p);
        if (MATHS.has(c.op)) {                                  // MOV, ADD...: a box that works out a number
          const result = c.values[c.values.length - 1];
          const sum = c.op === "MOV" ? c.values[0].text : `${c.values[0].text} ${MATH_SIGN[c.op]} ${c.values[1].text}`;
          wire(cx, cy, cx + 6, cy, p);
          el("rect", { x: cx + 6, y: cy - 22, width: outW - 12, height: 44, rx: 6, fill: on ? "#dff3e6" : "#fafbfc",
                       stroke: p ? ON : OFF, "stroke-width": 2 }, svg);
          el("text", { x: cx + 14, y: cy - 6, "font-size": 12.5, "font-weight": 700, fill: INK }, svg, c.op);
          el("text", { x: cx + outW - 14, y: cy - 6, "text-anchor": "end", "font-size": 11.5, fill: INK }, svg,
             `${result.text} = ${sum}`);
          el("text", { x: cx + 14, y: cy + 13, "font-size": 11, fill: MUTED }, svg, `${result.text} is ${valueNow(result) ?? "?"}`);
          let caption = prog.comment(c.line);
          if (caption.length > 24) caption = caption.slice(0, 23) + "…";
          if (caption) el("text", { x: cx + outW / 2, y: cy + 34, "text-anchor": "middle", "font-size": 10.5, fill: MUTED }, svg, caption);
          wire(cx + outW - 6, cy, width - 30, cy, on);
          return;
        }
        const mid = cx + outW / 2;
        if (on) el("circle", { cx: mid, cy, r: 11, fill: c.kind === "G" ? "#fde3d6" : "#dff3e6" }, svg);
        el("path", { d: `M ${mid - 6} ${cy - 13} A 16 16 0 0 0 ${mid - 6} ${cy + 13}`, fill: "none", stroke: p ? ON : OFF, "stroke-width": 3 }, svg);
        el("path", { d: `M ${mid + 6} ${cy - 13} A 16 16 0 0 1 ${mid + 6} ${cy + 13}`, fill: "none", stroke: p ? ON : OFF, "stroke-width": 3 }, svg);
        if (c.op !== "OUT") el("text", { x: mid, y: cy + 4, "text-anchor": "middle", "font-size": 11, "font-weight": 700, fill: INK }, svg, c.op[0]);
        wire(mid + 12, cy, width - 30, cy, on);
        wire(cx, cy, mid - 12, cy, p);
        labels(mid, cy, c);
        forceTarget(svg, c, mid - 30, cy - 38, 60, 60);
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
    const out = ['<span class="c">  #  instruction         on</span>'];
    let rung = -1;
    for (const item of prog.ins) {
      if (item.rung !== rung) { rung = item.rung; out.push(`<span class="c">     ; rung ${rung}</span>`); }
      const text = ilText(item);
      const scanned = live && live.current && live.scans > 0;
      const on = !scanned ? '<span class="p0">-</span>' : traced(item.i) ? '<span class="p1">1</span>' : '<span class="p0">0</span>';
      let extra = "";
      if (BOXES.has(item.op) && scanned) {
        const t = item.kind === "C" ? live.counters : live.timers, now = t && t[item.index] ? t[item.index][0] : 0;
        extra = `${now} / ${item.preset}${item.kind === "C" ? "" : " ms"}`;
      } else if (MATHS.has(item.op) && scanned) {
        const result = item.values[item.values.length - 1];
        extra = `${result.text} = ${valueNow(result) ?? "?"}`;
      }
      const comment = prog.comment(item.line);
      out.push(`${String(item.i).padStart(3)}  ${esc(text.padEnd(20))}${on}   ${esc(extra.padEnd(16))}` +
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
      ? `running, a scan every ${live.period_ms} ms; ${live.scans} scans` +
        (live.simulated ? "" : `, the last took ${live.scan_us} µs`) +
        (live.late ? `; ${live.late} started late` : "")
      : "stopped: Run ladder starts it";
    if (!has) return;
    if (watchedFor !== prog.loads && live.detail) {          // show the ladder's pins in History
      watchedFor = prog.loads;
      for (let n = 0; n < 22; n++) if (((live.in_mask | live.out_mask) >>> n) & 1) watched.add(n);
    }
    $("ladder-io").innerHTML = `<span><i class="sw in-hi"></i><b>inputs</b> ${pins(live.in_mask)}</span>` +
                   `<span><i class="sw out-hi"></i><b>outputs</b> ${pins(live.out_mask)}</span>` +
                   `<span>green wires have power</span>` +
                   (prog.ins.some((i) => i.kind === "M") ? `<span>click an M contact or coil to switch it</span>` : "");
    if (view === "drawing") drawRungs($("ladder-svg"));
    else drawIL($("ladder-il"));
  }

  // ---- typing into the console ----------------------------------------------------------------
  async function type(name, text) {
    const result = await api("/api/run", { name, text });
    if (result.result !== "ok") log(`Couldn't type ${name}: ${result.result}`, true);
  }

  // Type an IL program into the board, then 'after' (plc.run() unless told otherwise; "" for nothing).
  function load(name, il, after = "plc.run()") {
    il = il.replace(/\r\n?/g, "\n").replace(/\n+$/, "");
    if (il.includes("]]")) return log(`${name} contains "]]", which would end the program early.`, true);
    if (il.split("\n").some((l) => l.length > 480)) return log(`${name} has a line longer than 480 characters.`, true);
    if (il.length > 4000) return log(`${name} is too long: the board keeps up to 4000 characters, comments included.`, true);
    // Blank lines become ";" so the board's line numbers in errors match the file's.
    const lines = il.split("\n").map((l) => (l.trim() ? l : ";"));
    type(name, ["plc.load[[", ...lines, "]]" + (after ? " " + after : "")].join("\n"));
    return true;
  }

  // ---- opening and closing --------------------------------------------------------------------
  function setOpen(open) {
    $("ladder-card").hidden = !open;
    $("btn-ladder").classList.toggle("on", open);
    $("btn-ladder").setAttribute("aria-pressed", String(open));
    try { localStorage.setItem(STORE, open ? "1" : "0"); } catch (e) { /* private window: just don't remember */ }
    clearInterval(heartbeat);
    window.Panels?.ladderOpen(open);                          // the ladder tabs under the pin details
    window.Hmi?.ladderOpen(open);                             // and the operator panel
    window.Challenges?.ladderOpen(open);                      // and the ladder challenges
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
  // Run and Stop go to the ladder over JTAG, not through the console, so they work
  // even while a Lua program keeps the console busy (see ladder.py).
  $("btn-plc-run").onclick = async () => {
    const running = live && live.running;
    const result = await api("/api/ladder-command",
                             { command: running ? "stop" : "run", period_ms: (live && live.period_ms) || 10 });
    if (result.result !== "ok") log(`Couldn't ${running ? "stop" : "run"} the ladder: ${result.result}`, true);
  };
  // Open it again if it was open last time, or if the address ends in #ladder (handy for a bookmark).
  let remembered = location.hash === "#ladder";
  try { remembered = remembered || localStorage.getItem(STORE) === "1"; } catch (e) { /* no storage */ }
  if (remembered) setOpen(true);

  return {
    get prog() { return prog; },          // for panels.js and hmi.js
    load,                                 // for hmi.js: save a changed program back to the board
    get live() { return live; },
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
