"use strict";
// hmi.js: the Operator panel card. A ladder program describes its own panel in comment lines,
// which the board's assembler skips and this file reads:
//
//   ; @group   Pumps                  a heading over the widgets after it
//   ; @button  M0 Start               a push button: M0 is 1 while it's held
//   ; @switch  M1 Auto                a switch: click to turn M1 on or off
//   ; @lamp    G4 Motor green         a lamp for any bit, G M T or C; colour green, red, amber or blue
//   ; @show    D0 Parts               a number: D, a timer's ms (T) or a count (C)
//   ; @bar     D0 Level 0 100         a bar gauge for a D register, from its lowest to its highest
//   ; @set     D1 Speed 0 100         a box to type a D register's value, kept between the two numbers
//
// So the panel travels with the program: open another .il file and the panel changes with it.
// Buttons and switches go to the board through the ladder's command word over JTAG (ladder.py),
// so they work while a Lua program runs. Edit panel changes the panel and writes the lines back
// into the program; a program with no @ lines is offered a panel made from what it uses.

window.Hmi = (() => {
  const SPEC = {        // what each widget may point at, and how its line is written
    group:  { refs: "",     needs: "a heading, such as @group Pumps" },
    button: { refs: "M",    needs: "an M bit and a label, such as @button M0 Start" },
    switch: { refs: "M",    needs: "an M bit and a label, such as @switch M1 Auto" },
    lamp:   { refs: "GMTC", needs: "a bit (G, M, T or C) and a label, such as @lamp G4 Motor green" },
    show:   { refs: "DTC",  needs: "D, T or C and a label, such as @show D0 Parts" },
    bar:    { refs: "D",    needs: "a D register, a label, and the lowest and highest values, such as @bar D0 Level 0 100" },
    set:    { refs: "D",    needs: "a D register, a label, and the lowest and highest values, such as @set D1 Speed 0 100" },
  };
  const COUNT = { G: 22, M: 256, T: 32, C: 32, D: 64 };
  const COLOURS = ["green", "red", "amber", "blue"];
  const KIND_NAMES = { button: "Button (while held)", switch: "Switch", lamp: "Lamp", show: "Display", bar: "Bar", set: "Number box", group: "Heading" };
  const LINE = /^\s*(?:;|--|\/\/)\s*@([A-Za-z]+)\s*(.*?)\s*$/;

  let ladderOpen = false;
  let panelFor = null;          // the program (its 'loads' number) the panel was built for
  let widgets = [];             // what the program's @ lines describe
  let problems = [];            // their mistakes, as "line 3: ..."
  let editing = null;           // { widgets, sel } while Edit panel is on
  let starter = false;          // showing a panel made from the program, for one without @ lines

  const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  const L = () => window.Ladder || {};

  // ---- reading the @ lines -----------------------------------------------------------------
  function parse(source) {
    const found = [], wrong = [];
    (source || "").split("\n").forEach((raw, i) => {
      const m = raw.match(LINE);
      if (!m) return;
      const kind = m[1].toLowerCase(), words = m[2].split(/\s+/).filter(Boolean), at = `line ${i + 1}`;
      const spec = SPEC[kind];
      if (!spec) return wrong.push(`${at}: @${m[1]} isn't a panel widget. They are @${Object.keys(SPEC).join(", @")}.`);
      if (kind === "group") return found.push({ kind, label: words.join(" ") || "Group" });
      const ref = (words.shift() || "").toUpperCase(), r = ref.match(/^([GMTCD])(\d+)$/);
      if (!r || !spec.refs.includes(r[1]) || +r[2] >= COUNT[r[1]]) return wrong.push(`${at}: @${kind} needs ${spec.needs}.`);
      const w = { kind, ref, k: r[1], n: +r[2] };
      if (kind === "lamp" && COLOURS.includes((words[words.length - 1] || "").toLowerCase())) w.colour = words.pop().toLowerCase();
      if (kind === "bar" || kind === "set") {
        const hi = words[words.length - 1], lo = words[words.length - 2];
        if (/^-?\d+$/.test(lo || "") && /^-?\d+$/.test(hi || "") && +lo < +hi) { words.length -= 2; w.min = +lo; w.max = +hi; }
        else if (kind === "bar") return wrong.push(`${at}: @bar needs ${spec.needs}.`);
      }
      w.label = words.join(" ") || ref;
      found.push(w);
    });
    return { found, wrong };
  }

  function lineFor(w) {
    const pad = (s) => s.padEnd(6);                             // "@button M0", "@lamp   G4": as the examples line up
    if (w.kind === "group") return `; @${pad("group")} ${w.label}`;
    let text = `; @${pad(w.kind)} ${w.ref} ${w.label}`;
    if (w.kind === "lamp" && w.colour) text += ` ${w.colour}`;
    if ((w.kind === "bar" || w.kind === "set") && w.min !== undefined) text += ` ${w.min} ${w.max}`;
    return text;
  }

  // The program's text with its @ lines replaced: the new ones go after the comments at its top.
  function withPanel(source, list) {
    const lines = (source || "").replace(/\r\n?/g, "\n").split("\n").filter((l) => !LINE.test(l));
    let top = 0;
    while (top < lines.length && /^\s*(;|--|\/\/|$)/.test(lines[top])) top++;
    const block = list.map(lineFor);
    if (block.length) block.push(";");
    lines.splice(top, 0, ...block);
    return lines.join("\n");
  }

  // ---- what the program uses: names, and a panel to start from -----------------------------
  function namesInProgram(prog) {
    const found = new Map();
    for (const item of prog.ins) {
      const uses = item.values ? item.values.filter((v) => v.kind) : item.op === "ANB" || item.op === "ORB" || !item.kind ? [] : [item];
      for (const u of uses) {
        const key = u.kind + u.index;
        const e = found.get(key) || { k: u.kind, n: u.index, comment: "", driven: false };
        const c = prog.comment(item.line);
        if (item.op === "OUT" || item.op === "SET" || item.op === "RST") e.driven = e.driven || item.op === "OUT";
        if (c && (!e.comment || item.op === "OUT")) e.comment = c;
        found.set(key, e);
      }
    }
    return found;
  }

  function suggest(prog, live) {
    const out = [], names = [...namesInProgram(prog).values()];
    const label = (e) => (e.comment || e.k + e.n).replace(/\s*\.\.\.\s*$/, "").slice(0, 24);
    for (const e of names) if (e.k === "M" && !e.driven) out.push({ kind: "switch", ref: "M" + e.n, k: "M", n: e.n, label: label(e) });
    for (const e of names) if (e.k === "G" && ((live.out_mask >>> e.n) & 1)) out.push({ kind: "lamp", ref: "G" + e.n, k: "G", n: e.n, label: label(e) });
    for (const e of names) if (e.k === "T" || e.k === "C" || e.k === "D") out.push({ kind: "show", ref: e.k + e.n, k: e.k, n: e.n, label: label(e) });
    return out;
  }

  // ---- live values -------------------------------------------------------------------------
  function bit(k, n) {
    const live = L().live;
    if (!live || !live.detail) return false;
    if (k === "G") return !!((((live.out_mask >>> n) & 1) ? live.outputs : live.inputs) >>> n & 1);
    if (k === "M") return !!(live.m && ((live.m[n >> 5] >>> (n & 31)) & 1));
    const t = (k === "T" ? live.timers : live.counters) || {};
    return !!(t[n] && t[n][2]);
  }

  function number(k, n) {
    const live = L().live;
    if (!live || !live.detail) return null;
    if (k === "D") return live.d && live.d[n] !== undefined ? live.d[n] : null;
    const t = (k === "T" ? live.timers : live.counters) || {};
    return t[n] ? t[n][0] : null;
  }

  // ---- drawing the panel -------------------------------------------------------------------
  function widgetHtml(w, i, edit) {
    const cap = (extra) => `<span class="cap">${esc(w.ref)}${extra || ""}</span>`;
    const lbl = `<span class="lbl">${esc(w.label)}</span>`;
    const attrs = `class="w${w.kind === "bar" ? " wide" : ""}${edit && editing.sel === i ? " sel" : ""}" data-i="${i}"` +
                  (edit ? ` draggable="true" tabindex="0" title="Click to change it; drag to move it"` : "");
    const handle = edit ? '<span class="handle" aria-hidden="true">⠿</span>' : "";
    switch (w.kind) {
      case "button": return `<div ${attrs}>${handle}<button class="push ${/stop|off|reset|halt/i.test(w.label) ? "stop" : "go"}" data-press="${w.n}" ${edit ? "tabindex=\"-1\"" : ""}>${esc(w.label)}</button>${cap(" · while held")}</div>`;
      case "switch": return `<div ${attrs}>${handle}<button class="toggle" role="switch" aria-checked="false" data-switch="${w.n}" aria-label="${esc(w.label)}" ${edit ? "tabindex=\"-1\"" : ""}><i></i></button>${lbl}${cap()}</div>`;
      case "lamp":   return `<div ${attrs}>${handle}<span class="bulb ${w.colour || ""}" data-lamp="${i}"></span>${lbl}${cap()}</div>`;
      case "show":   return `<div ${attrs}>${handle}<span class="lcd" data-num="${i}">–</span>${lbl}${cap()}</div>`;
      case "bar":    return `<div ${attrs}>${handle}<span class="gauge"><span data-bar="${i}"></span><b data-barv="${i}">–</b></span>${lbl}${cap(` · ${w.min} to ${w.max}`)}</div>`;
      case "set":    return `<div ${attrs}>${handle}<span class="setter"><span class="now" data-num="${i}">–</span>` +
                            `<input id="hmi-set-${i}" inputmode="numeric" placeholder="new" aria-label="New value for ${esc(w.label)}" ${edit ? "tabindex=\"-1\"" : ""}>` +
                            `<button data-set="${i}" ${edit ? "tabindex=\"-1\"" : ""}>Set</button></span>${lbl}` +
                            `${cap(w.min !== undefined ? ` · ${w.min} to ${w.max}` : "")}</div>`;
    }
    return "";
  }

  function headingHtml(w, i, edit) {
    return edit ? `<h5 class="pgroup${editing.sel === i ? " sel" : ""}" data-i="${i}" draggable="true" tabindex="0">⠿ ${esc(w.label)}</h5>`
                : `<h5 class="pgroup">${esc(w.label)}</h5>`;
  }

  // Headings break the widgets into groups; each group is a grid of tiles.
  function gridHtml(list, edit) {
    let html = "", open = false;
    list.forEach((w, i) => {
      if (w.kind === "group") {
        if (open) html += "</div>";
        html += headingHtml(w, i, edit);
        html += '<div class="hmi">';
        open = true;
        return;
      }
      if (!open) { html += '<div class="hmi">'; open = true; }
      html += widgetHtml(w, i, edit);
    });
    if (edit) {
      if (!open) html += '<div class="hmi">';
      html += '<button class="w add" id="hmi-add" type="button">+<span class="lbl">Add</span><span class="cap">a button, switch, lamp, display, bar, number box or heading</span></button>';
      open = true;
    }
    if (open) html += "</div>";
    return html;
  }

  function show() {
    const card = $("panel-card");
    const prog = L().prog, live = L().live;
    const ready = ladderOpen && live && live.present && prog && prog.loads === live.loads && prog.ins.length;
    card.hidden = !ready;
    if (!ready) return;
    if (panelFor !== prog.loads) {                            // a new program: read its panel again
      panelFor = prog.loads;
      ({ found: widgets, wrong: problems } = parse(prog.source));
      editing = null;
      starter = !widgets.length;
    }
    const body = $("panel-body"), tools = $("panel-tools");
    if (editing) {
      $("panel-hint").textContent = "editing: click a widget to change it, drag to move it";
      tools.innerHTML = "";
      body.innerHTML = `<div class="edwrap"><div class="edgrid">${gridHtml(editing.widgets, true)}</div>${formHtml()}</div>` +
        '<div class="edbar"><span class="hint">Saving writes the @ lines into the program and loads it again, which starts the ladder from the beginning.</span>' +
        '<button id="hmi-copy">Copy @ lines</button><button id="hmi-download">Download .il</button><button id="hmi-cancel">Cancel</button>' +
        '<button class="primary" id="hmi-save">Save to the program</button></div>';
      wireEditor();
    } else if (starter) {
      const s = suggest(prog, live);
      $("panel-hint").textContent = "this program has no @ lines yet";
      tools.innerHTML = "";
      body.innerHTML = '<p class="starter">Here\'s a panel made from the program: a switch for each M bit it doesn\'t drive, a lamp for each output, ' +
        "and a display for each timer, counter and D register. Keep it as it is, or change it first.</p>" +
        (s.length ? `<div class="ghost">${gridHtml(s, false)}</div>` : '<p class="hint">The program uses nothing a panel could show yet.</p>') +
        '<div class="edbar"><span class="hint">Either way it\'s written into the program as @ lines.</span>' +
        `<button id="hmi-edit-first">Edit it first</button><button class="primary" id="hmi-use" ${s.length ? "" : "disabled"}>Use this panel</button></div>`;
      $("hmi-edit-first").onclick = () => startEditing(s);
      $("hmi-use").onclick = () => save(s);
    } else {
      $("panel-hint").textContent = "made by the @ lines in the program";
      tools.innerHTML = '<button id="btn-panel-edit" title="Add, change and arrange the panel\'s widgets">Edit panel</button>';
      $("btn-panel-edit").onclick = () => startEditing(widgets);
      body.innerHTML = gridHtml(widgets, false) +
        (problems.length ? `<ul class="hmi-problems">${problems.map((p) => `<li>${esc(p)}</li>`).join("")}</ul>` : "");
      wirePanel();
    }
    values();
  }

  // Refresh the values in place, so a click or a half-typed number isn't lost.
  function values() {
    const list = editing ? editing.widgets : starter ? null : widgets;
    const body = $("panel-body");
    if (!list) return;
    list.forEach((w, i) => {
      if (w.kind === "lamp") body.querySelector(`[data-lamp="${i}"]`)?.classList.toggle("lit", bit(w.k, w.n));
      if (w.kind === "switch") {
        const t = body.querySelector(`[data-i="${i}"] [data-switch]`);
        if (t) { const on = bit("M", w.n); t.classList.toggle("on", on); t.setAttribute("aria-checked", String(on)); }
      }
      if (w.kind === "show" || w.kind === "set") {
        const el = body.querySelector(`[data-num="${i}"]`), v = number(w.k, w.n);
        if (el) el.innerHTML = v === null ? "–" : esc(v) + (w.k === "T" ? "<small>ms</small>" : "");
      }
      if (w.kind === "bar") {
        const v = number("D", w.n), span = Math.max(1, w.max - w.min);
        const pct = v === null ? 0 : Math.max(0, Math.min(100, (100 * (v - w.min)) / span));
        const fill = body.querySelector(`[data-bar="${i}"]`), label = body.querySelector(`[data-barv="${i}"]`);
        if (fill) fill.style.width = `${pct}%`;
        if (label) label.textContent = v === null ? "–" : String(v);
      }
    });
  }

  // ---- operating the panel -----------------------------------------------------------------
  async function command(body, what) {
    const result = await api("/api/ladder-command", body);
    if (result.result !== "ok") log(`Couldn't ${what}: ${result.result}`, true);
  }

  function wirePanel() {
    const body = $("panel-body");
    body.querySelectorAll("[data-press]").forEach((b) => {
      const m = +b.dataset.press;
      let down = false;
      const press = (e) => { if (down) return; down = true; b.classList.add("down"); if (e.pointerId !== undefined) b.setPointerCapture(e.pointerId); command({ command: "set", m, value: true }, `press M${m}`); };
      const release = () => { if (!down) return; down = false; b.classList.remove("down"); command({ command: "set", m, value: false }, `release M${m}`); };
      b.addEventListener("pointerdown", press);
      b.addEventListener("pointerup", release);
      b.addEventListener("pointercancel", release);
      b.addEventListener("lostpointercapture", release);
      b.addEventListener("keydown", (e) => { if ((e.key === " " || e.key === "Enter") && !e.repeat) { e.preventDefault(); press(e); } });
      b.addEventListener("keyup", (e) => { if (e.key === " " || e.key === "Enter") release(); });
    });
    body.querySelectorAll("[data-switch]").forEach((t) => t.addEventListener("click", () => {
      const m = +t.dataset.switch;
      command({ command: "set", m, value: !bit("M", m) }, `switch M${m}`);
    }));
    body.querySelectorAll("[data-set]").forEach((b) => {
      const i = +b.dataset.set, input = $(`hmi-set-${i}`);
      const go = () => {
        const w = widgets[i], text = input.value.trim();
        if (!/^-?\d+$/.test(text)) return log(`${w.label}: type a whole number.`, true);
        if (w.min !== undefined && (+text < w.min || +text > w.max)) return log(`${w.label}: keep it from ${w.min} to ${w.max}.`, true);
        command({ command: "setd", d: w.n, value: +text }, `set ${w.ref}`);
        input.value = "";
      };
      b.addEventListener("click", go);
      input.addEventListener("keydown", (e) => { if (e.key === "Enter") go(); });
    });
  }

  // ---- Edit panel --------------------------------------------------------------------------
  function startEditing(list) {
    editing = { widgets: list.map((w) => ({ ...w })), sel: list.length ? 0 : null };
    show();
  }

  function formHtml() {
    const w = editing.widgets[editing.sel];
    if (!w) return '<div class="edform"><p class="hint">Click a widget to change it, or + Add for a new one.</p></div>';
    const kinds = Object.keys(KIND_NAMES).map((k) => `<option value="${k}" ${k === w.kind ? "selected" : ""}>${KIND_NAMES[k]}</option>`).join("");
    const used = [...namesInProgram(L().prog).keys()].filter((key) => SPEC[w.kind].refs.includes(key[0]));
    const refField = w.kind === "group" ? "" :
      `<label>Points at<input id="ed-ref" value="${esc(w.ref || "")}" list="ed-refs" spellcheck="false" placeholder="${SPEC[w.kind].refs.split("").map((k) => k + "0").join(", ")}"></label>` +
      `<datalist id="ed-refs">${used.map((u) => `<option value="${u}">`).join("")}</datalist>`;
    const colour = w.kind !== "lamp" ? "" :
      `<label>Colour<span class="swatches">${["", ...COLOURS].map((c) =>
        `<button type="button" class="c ${c || "none"}${(w.colour || "") === c ? " on" : ""}" data-colour="${c}" aria-label="${c || "the pin's own colour"}"></button>`).join("")}</span></label>`;
    const range = w.kind !== "bar" && w.kind !== "set" ? "" :
      `<div class="pair"><label>From<input id="ed-min" inputmode="numeric" value="${w.min ?? (w.kind === "bar" ? 0 : "")}"></label>` +
      `<label>To<input id="ed-max" inputmode="numeric" value="${w.max ?? (w.kind === "bar" ? 100 : "")}"></label></div>`;
    return `<form class="edform" id="ed-form" onsubmit="return false">` +
      `<label>Widget<select id="ed-kind">${kinds}</select></label>${refField}` +
      `<label>${w.kind === "group" ? "Heading" : "Label"}<input id="ed-label" value="${esc(w.label || "")}"></label>${colour}${range}` +
      `<p class="ed-error" id="ed-error"></p>` +
      `<div class="pair"><button type="button" id="ed-left" title="Move it earlier">◀ Earlier</button><button type="button" id="ed-right" title="Move it later">Later ▶</button></div>` +
      `<button type="button" id="ed-remove">Remove</button></form>`;
  }

  // Check one widget's settings: returns the problem, or "".
  function check(w) {
    if (w.kind === "group") return w.label ? "" : "Give the heading some words.";
    const r = (w.ref || "").match(/^([GMTCD])(\d+)$/);
    if (!r || !SPEC[w.kind].refs.includes(r[1]) || +r[2] >= COUNT[r[1]]) return `A ${KIND_NAMES[w.kind].toLowerCase()} needs ${SPEC[w.kind].needs.replace(/, such as.*$/, "")}.`;
    if (!w.label) return "Give it a label.";
    if ((w.kind === "bar" || w.kind === "set") && w.min !== undefined && !(w.min < w.max)) return "The first number must be smaller than the second.";
    if (w.kind === "bar" && w.min === undefined) return "A bar needs its lowest and highest values.";
    return "";
  }

  function select(i) { editing.sel = i; show(); }

  function wireTile(el) {
      el.addEventListener("click", (e) => { e.preventDefault(); select(+el.dataset.i); });
      el.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); select(+el.dataset.i); } });
      el.addEventListener("dragstart", (e) => { e.dataTransfer.setData("text/plain", el.dataset.i); e.dataTransfer.effectAllowed = "move"; });
      el.addEventListener("dragover", (e) => { e.preventDefault(); el.classList.add("drop"); });
      el.addEventListener("dragleave", () => el.classList.remove("drop"));
      el.addEventListener("drop", (e) => {
        e.preventDefault();
        const from = +e.dataTransfer.getData("text/plain"), to = +el.dataset.i;
        if (Number.isNaN(from) || from === to) return show();
        const [w] = editing.widgets.splice(from, 1);
        editing.widgets.splice(to, 0, w);
        select(to);
      });
  }

  function wireEditor() {
    const body = $("panel-body");
    body.querySelectorAll(".edgrid [data-i]").forEach(wireTile);
    $("hmi-add").onclick = () => {
      editing.widgets.push({ kind: "lamp", ref: "", label: "", k: "", n: 0 });
      select(editing.widgets.length - 1);
      $("ed-ref")?.focus();
    };
    const w = editing.widgets[editing.sel];
    if (w) {
      const set = (field, value, redraw) => {
        w[field] = value;
        const r = (w.ref || "").match(/^([GMTCD])(\d+)$/);
        if (r) { w.k = r[1]; w.n = +r[2]; }
        $("ed-error").textContent = check(w);
        if (redraw) return show();
        // Redraw only this tile, so the box being typed in keeps its cursor.
        const tile = body.querySelector(`.edgrid [data-i="${editing.sel}"]`);
        if (tile) {
          tile.outerHTML = w.kind === "group" ? headingHtml(w, editing.sel, true) : widgetHtml(w, editing.sel, true);
          wireTile(body.querySelector(`.edgrid [data-i="${editing.sel}"]`));
          values();
        }
      };
      $("ed-kind").onchange = (e) => {
        set("kind", e.target.value, false);
        if ((w.kind === "bar" || w.kind === "set") && w.min === undefined && w.kind === "bar") { w.min = 0; w.max = 100; }
        show();
      };
      const ref = $("ed-ref");
      if (ref) ref.oninput = () => set("ref", ref.value.trim().toUpperCase(), false);
      $("ed-label").oninput = (e) => set("label", e.target.value.trim(), false);
      body.querySelectorAll("[data-colour]").forEach((c) => c.onclick = () => { w.colour = c.dataset.colour || undefined; show(); });
      const lo = $("ed-min"), hi = $("ed-max");
      const range = () => {
        const a = lo.value.trim(), b = hi.value.trim();
        if (/^-?\d+$/.test(a) && /^-?\d+$/.test(b)) { w.min = +a; w.max = +b; } else { delete w.min; delete w.max; }
        $("ed-error").textContent = check(w);
      };
      if (lo) { lo.oninput = range; hi.oninput = range; }
      $("ed-left").onclick = () => move(-1);
      $("ed-right").onclick = () => move(1);
      $("ed-remove").onclick = () => { editing.widgets.splice(editing.sel, 1); editing.sel = editing.widgets.length ? Math.min(editing.sel, editing.widgets.length - 1) : null; show(); };
      $("ed-error").textContent = check(w);
    }
    $("hmi-cancel").onclick = () => { editing = null; show(); };
    $("hmi-copy").onclick = async () => {
      const text = editing.widgets.map(lineFor).join("\n");
      try { await navigator.clipboard.writeText(text); log(`Copied ${editing.widgets.length} @ lines.`); }
      catch (e) { log("Couldn't copy: the browser didn't allow it.", true); }
    };
    $("hmi-download").onclick = () => {
      const blob = new Blob([withPanel(L().prog.source, editing.widgets) + "\n"], { type: "text/plain" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "ladder.il";
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    };
    $("hmi-save").onclick = () => save(editing.widgets);
  }

  function move(step) {
    const i = editing.sel, j = i + step;
    if (j < 0 || j >= editing.widgets.length) return;
    [editing.widgets[i], editing.widgets[j]] = [editing.widgets[j], editing.widgets[i]];
    editing.sel = j;
    show();
  }

  // Write the panel into the program and load it again, keeping it running if it was.
  function save(list) {
    const bad = list.map(check).findIndex((p) => p);
    if (bad >= 0) {
      if (!editing) startEditing(list);
      editing.sel = bad;
      show();
      return log(`The panel isn't saved yet: ${check(list[bad])}`, true);
    }
    const live = L().live, source = withPanel(L().prog.source, list);
    const after = live && live.running ? `plc.run(${live.period_ms || 10})` : "";
    if (L().load("the program with its panel", source, after)) { editing = null; starter = false; panelFor = null; }
  }

  ladderOpen = !$("ladder-card").hidden;                      // ladder.js may have opened it before this file loaded

  return {
    ladderOpen(open) { ladderOpen = open; show(); },
    update() {                                              // a new reading from the board
      if (!ladderOpen) return;
      const prog = L().prog;
      if ($("panel-card").hidden || !prog || prog.loads !== panelFor) show();
      else values();
    },
    program() { panelFor = null; show(); },
    parse, withPanel,                                         // for tests
  };
})();
