"use strict";
// panels.js: the card under the pin details, with a tab for each of five views.
//   Data          everything the ladder program uses, live; switch M bits, set D registers
//   Timing        how the ladder is keeping up: scans a second, scan times, late scans
//   Events        every change on the pins, newest first
//   Instructions  the IL instruction set, with the ones the loaded program uses picked out
//   Board         the board's own numbers, from board_status (app/status.c, c3uview/board.py)
// The three ladder tabs only appear while the Ladder Logic card is open, so someone who hasn't
// got to ladder logic yet sees just Events and Board. Opening Ladder Logic selects Data.

window.Panels = (() => {
  const LADDER_TABS = new Set(["data", "timing", "instructions"]);
  const STORE = "c3u-viewer.panel";
  const tabs = [...document.querySelectorAll("#more-card .vt")];
  let ladderOpen = false;
  let current = "events";
  try { current = localStorage.getItem(STORE) || "events"; } catch (e) { /* no storage: start on Events */ }

  const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const kb = (bytes) => `${(bytes / 1024).toFixed(1)} KB`;
  const ladder = () => window.Ladder || {};

  // ---- tabs ------------------------------------------------------------------------------------
  function visible(k) { return ladderOpen || !LADDER_TABS.has(k); }

  function select(k, remember) {
    if (!visible(k)) k = "events";
    current = k;
    for (const t of tabs) {
      const on = t.dataset.k === k;
      t.hidden = !visible(t.dataset.k);
      t.setAttribute("aria-selected", String(on));
      t.tabIndex = on ? 0 : -1;
    }
    for (const p of document.querySelectorAll("#more-card .panel")) p.hidden = p.dataset.k !== k;
    if (remember) try { localStorage.setItem(STORE, k); } catch (e) { /* just don't remember */ }
    render();
  }

  tabs.forEach((t) => {
    t.addEventListener("click", () => select(t.dataset.k, true));
    t.addEventListener("keydown", (e) => {                    // arrow keys move between the tabs shown
      const step = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
      if (!step) return;
      const shown = tabs.filter((x) => !x.hidden);
      const next = shown[(shown.indexOf(t) + step + shown.length) % shown.length];
      next.focus();
      select(next.dataset.k, true);
    });
  });

  function render() {
    if (current === "data") renderData(true);
    else if (current === "timing") renderTiming();
    else if (current === "events") renderEvents();
    else if (current === "instructions") renderInstructions();
    else if (current === "board") renderBoard();
  }

  // ---- what the program calls each pin, relay and register ---------------------------------
  // The comment on the line that drives it (its coil), or else the first line that uses it.
  function names(prog) {
    const found = new Map();
    for (const item of prog.ins) {
      const uses = item.values ? item.values.filter((v) => v.kind)
                 : item.op === "ANB" || item.op === "ORB" || !item.kind ? [] : [item];   // ANB, ORB: no operand
      for (const u of uses) {
        const key = u.kind + u.index;
        const comment = prog.comment(item.line);
        const entry = found.get(key) || { kind: u.kind, index: u.index, comment: "", driven: null };
        if (item.op === "OUT" && !entry.driven) entry.driven = item;
        if (comment && (!entry.comment || item.op === "OUT")) entry.comment = comment;
        found.set(key, entry);
      }
    }
    return found;
  }

  function pinComment(pin) {
    const prog = ladder().prog;
    if (!prog || !ladder().live || !ladder().live.present) return "";
    const entry = names(prog).get("G" + pin);
    return entry ? entry.comment : "";
  }

  // ---- Data ------------------------------------------------------------------------------------
  let dataFor = null;            // the program the table was built for

  function rowsFor(prog, live) {
    const all = [...names(prog).values()];
    const order = { G: 0, T: 2, C: 3, M: 4, D: 5 };
    const rank = (r) => (r.kind === "G" ? (((live.out_mask >>> r.index) & 1) ? 1 : 0) : order[r.kind]);
    return all.sort((a, b) => rank(a) - rank(b) || a.index - b.index);
  }

  function renderData(rebuild) {
    const box = $("panel-data");
    const prog = ladder().prog, live = ladder().live;
    if (!prog || !live || !live.present || prog.loads !== live.loads) {
      box.innerHTML = '<p class="hint">Waiting for the board…</p>';
      dataFor = null;
      return;
    }
    if (!prog.ins.length) { box.innerHTML = '<p class="hint">There\'s no ladder program on the board yet.</p>'; return; }
    if (!live.detail) return;                                  // the values come with the next read
    if (rebuild || dataFor !== prog.loads) {
      dataFor = prog.loads;
      const rows = rowsFor(prog, live).map((r) => {
        const key = r.kind + r.index;
        const dot = r.kind === "G" ? (((live.out_mask >>> r.index) & 1) ? "out" : "in")
                  : { T: "tc", C: "tc", M: "rel", D: "num" }[r.kind];
        let cell = `<span class="v" data-v="${key}"></span>`;
        if (r.kind === "M" && !r.driven)
          cell = `<button class="v mforce" data-v="${key}" data-m="${r.index}" title="Click to switch M${r.index}"></button>`;
        if (r.kind === "M" && r.driven)
          cell += ` <span class="hint" title="The OUT coil in rung ${r.driven.rung} drives it">rung ${r.driven.rung}</span>`;
        if (r.kind === "D")
          cell += ` <input class="dset" id="set-d${r.index}" inputmode="numeric" placeholder="new" aria-label="New value for D${r.index}">` +
                  `<button class="set" data-d="${r.index}">Set</button>`;
        return `<tr><td><i class="dot ${dot}"></i>${key}</td><td class="val">${cell}</td><td>${esc(r.comment)}</td></tr>`;
      }).join("");
      box.innerHTML = '<p class="hint ptitle">everything the program uses, live; the names come from its comments</p>' +
        `<table class="dt"><thead><tr><th>Name</th><th>Value</th><th>Comment</th></tr></thead><tbody>${rows}</tbody></table>`;
      box.querySelectorAll("button[data-m]").forEach((b) => b.addEventListener("click", async () => {
        const m = +b.dataset.m;
        const result = await api("/api/ladder-command", { command: "set", m, value: !mBit(m) });
        if (result.result !== "ok") log(`Couldn't switch M${m}: ${result.result}`, true);
      }));
      box.querySelectorAll("button[data-d]").forEach((b) => b.addEventListener("click", () => setD(+b.dataset.d)));
      box.querySelectorAll("input.dset").forEach((i) => i.addEventListener("keydown", (e) => {
        if (e.key === "Enter") setD(+i.id.slice(5));
      }));
    }
    for (const span of box.querySelectorAll("[data-v]")) span.innerHTML = valueHtml(span.dataset.v, live);
  }

  function mBit(n) {
    const live = ladder().live;
    return !!(live && live.m && ((live.m[n >> 5] >>> (n & 31)) & 1));
  }

  function valueHtml(key, live) {
    const kind = key[0], n = +key.slice(1);
    if (kind === "G") {
      const bit = (((live.out_mask >>> n) & 1) ? live.outputs : live.inputs) >>> n & 1;
      return `<b class="pill1 ${bit ? "on" : ""}">${bit}</b>`;
    }
    if (kind === "M") return `<b class="pill1 ${mBit(n) ? "on" : ""}">${mBit(n) ? 1 : 0}</b>`;
    if (kind === "D") return `<b class="num">${live.d && live.d[n] !== undefined ? live.d[n] : "?"}</b>`;
    const t = (kind === "T" ? live.timers : live.counters) || {};
    const [now, preset, done] = t[n] || [0, 0, 0];
    const unit = kind === "T" ? " ms" : "";
    const width = preset ? Math.min(100, (100 * now) / preset) : 0;
    return `<span class="num">${now} / ${preset}${unit}${done ? " ✓" : ""}</span><span class="bar"><span style="width:${width}%"></span></span>`;
  }

  async function setD(n) {
    const input = $(`set-d${n}`);
    const text = input.value.trim();
    if (!/^-?\d+$/.test(text)) { log(`D${n}: type a whole number, such as 250 or -5.`, true); return; }
    const result = await api("/api/ladder-command", { command: "setd", d: n, value: Number(text) });
    if (result.result !== "ok") log(`Couldn't set D${n}: ${result.result}`, true);
    else input.value = "";
  }

  // ---- Timing ----------------------------------------------------------------------------------
  // Scans a second, worked out from the scan counter, a second at a time, for the last minute.
  const seconds = [];            // [{scans, late}] for each whole second, oldest first
  let bucket = null;             // {sec, ms, scans, late} at the first reading in the second being counted
  let lastSeen = null;           // the last ladder event with the scan counters

  function countScans(live) {
    if (!live.present || !live.detail) return;
    const ms = performance.now(), sec = Math.floor(ms / 1000);
    if (lastSeen && (live.scans < lastSeen.scans || live.loads !== lastSeen.loads)) bucket = null;  // a new program
    if (!bucket) bucket = { sec, ms, scans: live.scans, late: live.late };
    if (sec !== bucket.sec) {
      // Readings come about every 100 ms, so divide by the time between the two readings,
      // not by a whole second, or the rate would jump around by a tenth.
      seconds.push({ scans: (1000 * (live.scans - bucket.scans)) / (ms - bucket.ms), late: live.late - bucket.late });
      while (seconds.length > 60) seconds.shift();
      bucket = { sec, ms, scans: live.scans, late: live.late };
      if (current === "timing") renderTiming();
    }
    lastSeen = live;
  }

  function renderTiming() {
    const box = $("panel-timing");
    const live = ladder().live;
    if (!live || !live.present || !live.detail) { box.innerHTML = '<p class="hint">Waiting for the board…</p>'; return; }
    const expected = live.running && live.period_ms ? 1000 / live.period_ms : 0;
    const rate = seconds.length ? Math.round(seconds[seconds.length - 1].scans) : 0;
    const lateMinute = seconds.reduce((sum, s) => sum + s.late, 0);
    const share = live.period_ms ? (100 * live.scan_us) / (live.period_ms * 1000) : 0;
    box.innerHTML = '<p class="hint ptitle">how the ladder is keeping up, over the last minute</p>' +
      '<div class="tiles">' +
      `<div class="tile"><span class="k">scans a second</span><span class="n">${live.running ? rate : "–"}</span><span class="s">${expected ? `of ${Math.round(expected)}` : "stopped"}</span></div>` +
      `<div class="tile"><span class="k">last scan</span><span class="n">${live.scan_us} µs</span><span class="s">${share.toFixed(1)}% of ${live.period_ms} ms</span></div>` +
      `<div class="tile"><span class="k">longest</span><span class="n">${live.scan_us_max} µs</span><span class="s">since plc.load()</span></div>` +
      `<div class="tile${lateMinute ? " warn" : ""}"><span class="k">late</span><span class="n">${lateMinute}</span><span class="s">in the last minute</span></div>` +
      "</div>" + timingChart(expected) +
      '<p class="foot">Red: seconds with late scans. A busy Lua program or the garbage collector can make scans late; exercise 12 explains why.</p>';
  }

  function timingChart(expected) {
    const W = 360, H = 120, L = 30, B = 18, T = 8;
    const ref = Math.round(expected || Math.max(10, ...seconds.map((s) => s.scans)));   // the line to measure against
    const top = Math.max(ref, ...seconds.map((s) => s.scans)) * 1.1;
    const x = (i) => L + (i + 60 - seconds.length) * (W - L - 6) / 59;
    const y = (v) => T + (top - v) * (H - B - T) / top;
    const ticks = [0, Math.round(ref / 2), ref];
    let svg = `<svg class="spark" viewBox="0 0 ${W} ${H}" role="img" aria-label="Scans a second over the last minute">`;
    for (const v of ticks)
      svg += `<line x1="${L}" x2="${W - 6}" y1="${y(v).toFixed(1)}" y2="${y(v).toFixed(1)}" class="g"/>` +
             `<text x="${L - 5}" y="${(y(v) + 3.5).toFixed(1)}" class="ax" text-anchor="end">${v}</text>`;
    seconds.forEach((s, i) => {
      if (s.late) svg += `<rect x="${(x(i) - 2.5).toFixed(1)}" y="${T}" width="5" height="${(y(0) - T).toFixed(1)}" class="late"/>`;
    });
    if (seconds.length > 1) {
      const pts = seconds.map((s, i) => `${x(i).toFixed(1)},${y(s.scans).toFixed(1)}`);
      svg += `<path d="M${x(0).toFixed(1)},${y(0).toFixed(1)} L${pts.join(" L")} L${x(seconds.length - 1).toFixed(1)},${y(0).toFixed(1)} Z" class="fill"/>` +
             `<polyline points="${pts.join(" ")}" class="line"/>` +
             `<circle cx="${x(seconds.length - 1).toFixed(1)}" cy="${y(seconds[seconds.length - 1].scans).toFixed(1)}" r="3" class="end"/>`;
    }
    svg += `<text x="${L}" y="${H - 4}" class="ax">-60 s</text><text x="${W - 6}" y="${H - 4}" class="ax" text-anchor="end">now</text></svg>`;
    return svg;
  }

  // ---- Events ----------------------------------------------------------------------------------
  const events = [];             // newest first: {t, pin, level}
  const lastLevel = {};          // pin -> {level, mode}, to see changes

  function watchPins(ev) {
    let added = false;
    for (const p of ev.pins) {
      const level = p.level ?? p.out_level;
      if (p.reserved || p.pwm || (p.mode !== "input" && p.mode !== "output") || level == null) {
        delete lastLevel[p.pin];
        continue;
      }
      const before = lastLevel[p.pin];
      if (before && before.mode === p.mode && before.level !== level) {
        events.unshift({ t: ev.t, pin: p.pin, level });
        added = true;
      }
      lastLevel[p.pin] = { level, mode: p.mode };
    }
    if (events.length > 300) events.length = 300;
    if (added && current === "events") renderEvents();
  }

  function clock(t) {
    const d = new Date(t * 1000);
    return d.toLocaleTimeString([], { hour12: false }) + "." + Math.floor(d.getMilliseconds() / 100);
  }

  function eventLine(e) {
    const what = pinComment(e.pin);
    return { time: clock(e.t), pin: `G${e.pin}`, change: `${e.level ? "↑" : "↓"} ${e.level}`, what };
  }

  function renderEvents() {
    const box = $("panel-events");
    const items = events.map((e) => {
      const l = eventLine(e);
      return `<li><time>${l.time}</time><b>${l.pin}</b><span class="arrow ${e.level ? "up" : "down"}">${l.change}</span>` +
             `<span class="what">${esc(l.what)}</span></li>`;
    }).join("");
    box.innerHTML = '<p class="hint ptitle">every change on the pins, newest first</p>' +
      `<div class="evbar"><span class="hint">${events.length} change${events.length === 1 ? "" : "s"}</span>` +
      '<span class="grow"></span><button id="events-copy">Copy</button><button id="events-clear">Clear</button></div>' +
      (items ? `<ol class="ev">${items}</ol>` : '<p class="hint">Nothing yet. Press the board\'s button: G9 goes to 0 while it\'s held.</p>') +
      '<p class="foot">Times are when the viewer saw the change, to about 0.1 s. A change and back between two reads is missed.</p>';
    $("events-clear").onclick = () => { events.length = 0; renderEvents(); };
    $("events-copy").onclick = async () => {
      const text = events.map((e) => { const l = eventLine(e); return `${l.time}  ${l.pin}  ${l.change}  ${l.what}`.trimEnd(); }).join("\n");
      try { await navigator.clipboard.writeText(text); log(`Copied ${events.length} pin changes.`); }
      catch (err) { log("Couldn't copy: the browser didn't allow it.", true); }
    };
  }

  // ---- Instructions ----------------------------------------------------------------------------
  const SHEET = [
    ["Contacts", [["LD  AND  OR", "while the operand is 1", ["LD", "AND", "OR"]],
                  ["LDN ANDN ORN", "while the operand is 0", ["LDN", "ANDN", "ORN"]],
                  ["LDP ANDP ORP", "one scan, as it turns on", ["LDP", "ANDP", "ORP"]],
                  ["LDF ANDF ORF", "one scan, as it turns off", ["LDF", "ANDF", "ORF"]]]],
    ["Blocks", [["ANB  ORB", "join the last two blocks, in series or in parallel", ["ANB", "ORB"]]]],
    ["Coils", [["OUT G4", "follows the rung", ["OUT"]], ["SET  RST", "latch on, off; RST also resets T and C", ["SET", "RST"]]]],
    ["Timers", [["TON T0 ms", "on-delay", ["TON"]], ["TOF T0 ms", "off-delay", ["TOF"]],
                ["TP T0 ms", "a pulse of the preset", ["TP"]], ["RTO T0 ms", "keeps its time until RST", ["RTO"]]]],
    ["Counters", [["CTU C0 n", "count up", ["CTU"]], ["CTD C0 n", "count down; with a CTU: up/down", ["CTD"]]]],
    ["Numbers", [["LD>= D0 5", "compare: = <> > >= < <=, after LD, AND or OR", ["LDC", "ANDC", "ORC"]],
                 ["MOV 5 D0", "D0 = 5, every scan the rung is on", ["MOV"]],
                 ["ADD D0 1 D0", "and SUB, MUL, DIV", ["ADD", "SUB", "MUL", "DIV"]]]],
  ];

  function renderInstructions() {
    const prog = ladder().prog;
    const used = new Set(prog ? prog.ins.map((i) => i.op) : []);
    const groups = SHEET.map(([title, items]) => `<div class="cg"><h4>${title}</h4>` + items.map(([il, what, ops]) =>
      `<div class="ci${ops.some((o) => used.has(o)) ? " used" : ""}"><code>${esc(il)}</code><span>${esc(what)}</span></div>`).join("") + "</div>").join("");
    $("panel-instructions").innerHTML = '<p class="hint ptitle">IL at a glance; green: used in the program on the board</p>' +
      `<div class="cheat">${groups}</div>` +
      '<p class="foot">Operands: G0–G21 the pins, M0–M255 relays, T0–T31 timers, C0–C31 counters, D0–D63 numbers. ' +
      '<code>plc.help()</code> at the console has the same list.</p>';
  }

  // ---- Board -----------------------------------------------------------------------------------
  let boardNow = null;
  let lastUpdates = null, sameSince = 0;

  function watchBoard(ev) {
    if (ev.present) {
      if (ev.updates !== lastUpdates) { lastUpdates = ev.updates; sameSince = performance.now(); }
    }
    boardNow = ev;
    if (current === "board") renderBoard();
  }

  function duration(ms) {
    const s = Math.floor(ms / 1000), m = Math.floor(s / 60), h = Math.floor(m / 60), d = Math.floor(h / 24);
    if (d) return `${d} d ${h % 24} h`;
    if (h) return `${h} h ${m % 60} min`;
    if (m) return `${m} min ${s % 60} s`;
    return `${s} s`;
  }

  function renderBoard() {
    const box = $("panel-board"), b = boardNow;
    if (!b) { box.innerHTML = '<p class="hint">Waiting for the board…</p>'; return; }
    if (!b.present) {
      box.innerHTML = '<p class="hint">This board\'s firmware doesn\'t publish these numbers. <b>Flash…</b> writes the C3U-Metal that comes with this viewer, which does.</p>';
      return;
    }
    const stuck = performance.now() - sameSince > 3000;
    const meter = (part, whole) => `<div class="meter"><span style="width:${Math.min(100, (100 * part) / whole).toFixed(1)}%"></span></div>`;
    box.innerHTML = '<p class="hint ptitle">the firmware\'s own numbers, about once a second</p>' +
      (stuck ? '<p class="warnline">These numbers have stopped changing: the program may be stuck in a long C function, or have crashed.</p>' : "") +
      '<div class="hl">' +
      `<div class="hrow"><span class="k">running for</span><span class="n">${duration(b.uptime_ms)}</span></div>` +
      `<div class="hrow"><span class="k">CPU</span><span class="n">${b.cpu_mhz} MHz</span></div>` +
      `<div class="hrow"><span class="k">heap in use</span><span class="n">${kb(b.heap_in_use)} of ${kb(b.heap_size)}</span></div>` +
      meter(b.heap_in_use, b.heap_size) +
      `<div class="hrow"><span class="k">claimed by malloc so far</span><span class="n">${kb(b.heap_claimed)}</span></div>` +
      `<div class="hrow"><span class="k">stack, deepest so far</span><span class="n">${kb(b.stack_max_used)} of ${kb(b.stack_size)}</span></div>` +
      meter(b.stack_max_used, b.stack_size) +
      (b.stack_overflowed ? '<p class="warnline">The stack has grown into the heap. Restart the board.</p>' : "") +
      `<div class="hrow"><span class="k">last start</span><span class="n">${esc(b.reset_text)}</span></div>` +
      `<div class="hrow"><span class="k">firmware built</span><span class="n">${esc(b.built)}</span></div>` +
      "</div>" +
      '<p class="foot"><code>mem()</code> at the console prints the heap and the stack too.</p>';
  }

  ladderOpen = !$("ladder-card").hidden;                    // ladder.js may have opened it already
  select(current, false);

  return {
    // The Ladder Logic card opened or closed: show or hide the ladder tabs; opening shows Data.
    ladderOpen(open) {
      const was = ladderOpen;
      ladderOpen = open;
      select(open && !was ? "data" : current, false);
    },
    pins(ev) { watchPins(ev); },
    ladder(ev) {
      countScans(ev);
      if (current === "data") renderData(false);
      else if (current === "timing" && !seconds.length) renderTiming();
    },
    program() { if (current === "data") renderData(true); else if (current === "instructions") renderInstructions(); },
    board(ev) { watchBoard(ev); },
  };
})();
