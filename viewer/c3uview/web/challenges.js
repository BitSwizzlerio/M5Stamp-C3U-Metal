"use strict";
// challenges.js: the Challenges card. Each challenge is a task with a hint, and a check that
// watches the pins the viewer already reads (about 20 times a second) and ticks the task off
// once it has seen it done. Progress is remembered in this browser.
//
// The ladder challenges only appear while the Ladder Logic card is open, and they insist that
// the ladder, not Lua, drives the pins. To add a challenge, add an entry to CHALLENGES with one
// of the check types below; docs/challenges.md explains each.

window.Challenges = (() => {
  const CHALLENGES = [
    { id: "light", group: "Lua", title: "Light G4",
      task: "Make G4 an output and set it to 1, so an LED on G4 lights. Keep it on for 2 seconds.",
      hint: "<code>gpio.output(4)</code>, then <code>gpio.write(4, 1)</code>.",
      check: { type: "on", pin: 4, seconds: 2 } },
    { id: "follow", group: "Lua", title: "G4 follows the button",
      task: "Make G4 light while the board's button is held, and go off when it's let go. Press it 3 times.",
      hint: "The button is G9, and it reads 0 while it's held. In a loop: <code>gpio.write(4, button() and 1 or 0)</code>, with a short <code>delay(10)</code>.",
      check: { type: "follow", pin: 4, input: 9, presses: 3 } },
    { id: "blink", group: "Lua", title: "Blink G4 at 2 Hz",
      task: "Make G4 turn on and off twice a second, and keep it going for 5 seconds.",
      hint: "A loop: write 1, <code>delay(250)</code>, write 0, <code>delay(250)</code>.",
      check: { type: "blink", pin: 4, hz: 2, seconds: 5 } },
    { id: "timer", group: "Lua", title: "A 3-second timer",
      task: "When the button is pressed, light G4 for 3 seconds, then turn it off.",
      hint: "Wait in a loop until <code>button()</code> is true, then write 1, <code>delay(3000)</code>, write 0.",
      check: { type: "timer", pin: 4, input: 9, seconds: 3 } },
    { id: "sos", group: "Lua", title: "SOS in Morse on G4",
      task: "Flash S-O-S on G4: three short flashes, three long ones, three short ones. Long flashes last at least twice as long as short ones.",
      hint: "Write a function <code>flash(ms)</code> that turns G4 on for ms milliseconds, then off for 200. Then call it nine times.",
      check: { type: "sos", pin: 4 } },
    { id: "seal", group: "Ladder", title: "A seal-in",
      task: "In ladder logic: a press of the button turns G4 on, and G4 stays on after the button is let go. Keep it on for 2 seconds.",
      hint: "<code>LDN G9</code>, then <code>OR G4</code> in parallel with it, then <code>OUT G4</code>. The motor example does this.",
      check: { type: "seal", pin: 4, input: 9, seconds: 2, ladder: true } },
    { id: "count5", group: "Ladder", title: "Count to five",
      task: "In ladder logic: G4 stays off for the first four presses of the button, and comes on at the fifth.",
      hint: "<code>LDF G9</code> counts presses into <code>CTU C0 5</code>; then <code>LD C0</code> and <code>OUT G4</code>. Or count into D0 with <code>ADD</code>, and compare.",
      check: { type: "count", pin: 4, input: 9, presses: 5, ladder: true } },
    { id: "flasher", group: "Ladder", title: "A level-crossing flasher",
      task: "In ladder logic: G4 and G5 take turns, each on for about half a second, never both at once. Keep it going for 5 seconds.",
      hint: "Two timers that restart each other, as in <code>examples/ladder/flasher.il</code>.",
      check: { type: "alternate", a: 4, b: 5, hz: 1, seconds: 5, ladder: true } },
    { id: "compare", group: "Ladder", title: "Light G4 from 10",
      task: "In ladder logic: G4 is on while D0 is 10 or more, and off while it's less. Show it both ways, by changing D0.",
      hint: "<code>LD&gt;= D0 10</code>, then <code>OUT G4</code>. Change D0 with <code>plc.set(\"D0\", 12)</code>, the Data tab, or an <code>@set D0</code> line for the operator panel.",
      check: { type: "compare", pin: 4, reg: 0, at: 10, ladder: true } },
    { id: "fan", group: "Ladder", title: "A fan that runs on",
      task: "In ladder logic: G5 comes on with G4 and stays on for 2 seconds after G4 goes off, like a cooling fan. G4 follows the button.",
      hint: "<code>LDN G9</code> and <code>OUT G4</code>; then <code>LD G4</code>, <code>TOF T0 2000</code> and <code>OUT G5</code>.",
      check: { type: "rundown", pin: 4, follower: 5, input: 9, seconds: 2, ladder: true } },
  ];
  const STORE_DONE = "c3u-viewer.challenges-done";
  const STORE_OPEN = "c3u-viewer.challenges-open";
  const KEEP_S = 40;                                          // seconds of pin history to keep

  let done = {};
  try { done = JSON.parse(localStorage.getItem(STORE_DONE) || "{}") || {}; } catch (e) { done = {}; }
  let current = null;                                         // the challenge open in the card
  let since = 0;                                              // when it was opened: older history doesn't count
  let ladderOpen = false;
  let samples = [];                                           // [{t, lv: {pin: level}, out: {pin: bool}}]
  let status = { text: "", progress: 0 };

  const L = () => window.Ladder || {};
  // The simulator runs ladder logic but not Lua, so it only offers the ladder challenges.
  const visibleList = () => CHALLENGES.filter((c) => window.Backend ? c.group === "Ladder" : ladderOpen || c.group !== "Ladder");

  // ---- pin history -------------------------------------------------------------------------
  function record(ev) {
    const lv = {}, out = {};
    for (const p of ev.pins) {
      const v = p.level ?? p.out_level;
      if (v == null || p.reserved) continue;
      lv[p.pin] = v ? 1 : 0;
      out[p.pin] = p.mode === "output";
    }
    samples.push({ t: ev.t, lv, out });
    while (samples.length && samples[0].t < ev.t - KEEP_S) samples.shift();
  }

  const recent = () => samples.filter((s) => s.t >= since);
  // The times a pin changed, in the history since the challenge opened: [{t, v}], v the new level.
  function edges(pin) {
    const list = [];
    let last = null;
    for (const s of recent()) {
      const v = s.lv[pin];
      if (v === undefined) continue;
      if (last !== null && v !== last) list.push({ t: s.t, v });
      last = v;
    }
    return list;
  }
  const now = () => (samples.length ? samples[samples.length - 1] : null);
  const levelAt = (pin, t) => {                                // the level at time t (the last sample at or before it)
    let v = null;
    for (const s of samples) { if (s.t > t) break; if (s.lv[pin] !== undefined) v = s.lv[pin]; }
    return v;
  };
  // Presses of the button: G9 falls to 0, then rises again. [{down, up}] with up null while held.
  function presses(input) {
    const list = [];
    for (const e of edges(input)) {
      if (e.v === 0) list.push({ down: e.t, up: null });
      else if (list.length && list[list.length - 1].up === null) list[list.length - 1].up = e.t;
    }
    return list;
  }
  const ladderDrives = (pin) => { const l = L().live; return !!(l && l.present && l.claimed && ((l.out_mask >>> pin) & 1)); };

  // ---- the checks: each returns {done, progress (0 to 1), text} ------------------------------
  const CHECKS = {
    on(c) {
      const s = now();
      if (!s || !s.out[c.pin]) return { progress: 0, text: `G${c.pin} isn't an output yet.` };
      if (!s.lv[c.pin]) return { progress: 0, text: `G${c.pin} is an output, but it's 0.` };
      const e = edges(c.pin), from = e.length ? e[e.length - 1].t : (recent()[0] || s).t;
      const held = s.t - from;
      return { done: held >= c.seconds, progress: held / c.seconds, text: `G${c.pin} is 1: ${held.toFixed(1)} of ${c.seconds} seconds.` };
    },

    follow(c) {
      const s = now();
      if (!s || !s.out[c.pin]) return { progress: 0, text: `G${c.pin} isn't an output yet.` };
      let good = 0, bad = "";
      for (const p of presses(c.input)) {
        if (p.up === null || s.t < p.up + 0.5) continue;      // wait until it has settled after the release
        const onDuring = samples.some((x) => x.t >= p.down && x.t <= p.up + 0.3 && x.lv[c.pin] === 1);
        const offAfter = levelAt(c.pin, p.up + 0.45) === 0;
        if (onDuring && offAfter) good++;
        else bad = onDuring ? `G${c.pin} stayed on after the button was let go.` : `G${c.pin} didn't light while the button was held.`;
      }
      return { done: good >= c.presses, progress: good / c.presses, text: bad || `${good} of ${c.presses} presses followed. Press the button.` };
    },

    blink(c) {
      const e = edges(c.pin).filter((x) => x.v === 1);
      if (e.length < 2) return { progress: 0, text: `Waiting for G${c.pin} to blink.` };
      const want = 1 / c.hz;
      let run = 0, last = 0;
      for (let i = 1; i < e.length; i++) {
        const period = e[i].t - e[i - 1].t;
        last = period;
        run = Math.abs(period - want) <= want * 0.15 ? run + period : 0;
      }
      const s = now(), stale = s && s.t - e[e.length - 1].t > want * 1.5;
      if (stale) run = 0;
      const hz = last ? (1 / last).toFixed(1) : "?";
      return { done: run >= c.seconds, progress: run / c.seconds,
               text: stale ? `G${c.pin} has stopped blinking.` : `G${c.pin} is blinking at ${hz} Hz: ${run.toFixed(1)} of ${c.seconds} seconds at ${c.hz} Hz.` };
    },

    timer(c) {
      const e = edges(c.pin);
      for (const p of presses(c.input)) {
        const on = e.find((x) => x.v === 1 && x.t >= p.down - 0.1 && x.t <= p.down + 0.5);
        if (!on) continue;
        const off = e.find((x) => x.v === 0 && x.t > on.t);
        if (!off) {
          const s = now();
          return { progress: Math.min(0.99, (s.t - on.t) / c.seconds), text: `G${c.pin} came on: ${(s.t - on.t).toFixed(1)} seconds so far.` };
        }
        const lasted = off.t - on.t;
        if (Math.abs(lasted - c.seconds) <= 0.35) return { done: true, progress: 1, text: `G${c.pin} stayed on for ${lasted.toFixed(1)} seconds.` };
        return { progress: 0, text: `G${c.pin} stayed on for ${lasted.toFixed(1)} seconds, not ${c.seconds}. Try again.` };
      }
      return { progress: 0, text: "Press the button." };
    },

    sos(c) {
      const e = edges(c.pin), on = [];
      for (let i = 1; i < e.length; i++) if (e[i - 1].v === 1 && e[i].v === 0) on.push(e[i].t - e[i - 1].t);
      if (on.length < 9) return { progress: on.length / 9, text: `${on.length} of 9 flashes seen.` };
      const d = on.slice(-9), mean = (a) => a.reduce((x, y) => x + y, 0) / a.length;
      const short = mean([...d.slice(0, 3), ...d.slice(6)]), long = mean(d.slice(3, 6));
      const fits = (x, m) => Math.abs(x - m) <= m * 0.5;
      const ok = long >= 2 * short && short >= 0.08 && d.every((x, i) => fits(x, i >= 3 && i < 6 ? long : short));
      return { done: ok, progress: ok ? 1 : 0.9,
               text: ok ? "S-O-S." : `The last 9 flashes don't read S-O-S yet (short ${short.toFixed(2)} s, long ${long.toFixed(2)} s). Send it again.` };
    },

    seal(c) {
      const s = now();
      for (const p of presses(c.input)) {
        if (p.up === null) return { progress: 0, text: "Now let go of the button." };
        if (levelAt(c.pin, p.up + 0.3) !== 1) continue;
        const e = edges(c.pin).find((x) => x.v === 0 && x.t > p.up);
        const held = (e ? e.t : s.t) - p.up;
        if (e && held < c.seconds) return { progress: 0, text: `G${c.pin} went off ${held.toFixed(1)} seconds after the button was let go.` };
        return { done: held >= c.seconds, progress: held / c.seconds, text: `G${c.pin} is still on: ${held.toFixed(1)} of ${c.seconds} seconds after the release.` };
      }
      return { progress: 0, text: "Press the button." };
    },

    count(c) {
      let n = 0;
      for (const p of presses(c.input)) {
        n++;
        const after = levelAt(c.pin, (p.up ?? p.down) + 0.4);
        if (after === 1 && n < c.presses) return { progress: 0, text: `G${c.pin} came on at press ${n}, not ${c.presses}. Reset the count (plc.load it again) and try again.` };
        if (n === c.presses) {
          if (after === 1) return { done: true, progress: 1, text: `G${c.pin} came on at press ${c.presses}.` };
          if (now().t > (p.up ?? p.down) + 0.6) return { progress: 0, text: `G${c.pin} didn't come on at press ${c.presses}. Reset the count and try again.` };
        }
      }
      return { progress: n / c.presses, text: `${n} of ${c.presses} presses. G${c.pin} should be off until the last.` };
    },

    alternate(c) {
      const ea = edges(c.a).filter((x) => x.v === 1);
      if (ea.length < 2) return { progress: 0, text: `Waiting for G${c.a} and G${c.b} to flash.` };
      const both = recent().filter((s) => s.lv[c.a] === 1 && s.lv[c.b] === 1).length;
      if (both > 2) { since = now().t; return { progress: 0, text: `G${c.a} and G${c.b} were on together. Starting again.` }; }
      const want = 1 / c.hz;
      let run = 0;
      for (let i = 1; i < ea.length; i++) {
        const period = ea[i].t - ea[i - 1].t;
        run = Math.abs(period - want) <= want * 0.2 ? run + period : 0;
      }
      const bToggles = edges(c.b).length;
      return { done: run >= c.seconds && bToggles >= 4, progress: Math.min(run / c.seconds, bToggles / 4),
               text: `Taking turns: ${run.toFixed(1)} of ${c.seconds} seconds at about ${c.hz} Hz.` };
    },

    compare(c) {
      const live = L().live;
      const d = live && live.d ? live.d[c.reg] : undefined;
      if (d === undefined) return { progress: 0, text: `Waiting for a program that uses D${c.reg}.` };
      const s = now(), g = s && s.lv[c.pin];
      state.compare = state.compare || { above: false, below: false };
      if (d >= c.at && g === 1) state.compare.above = true;
      if (d < c.at && g === 0) state.compare.below = true;
      if ((d >= c.at) !== (g === 1)) {
        state.compareWrongSince ??= s.t;
        if (s.t - state.compareWrongSince > 0.6) return { progress: 0, text: `D${c.reg} is ${d}, and G${c.pin} is ${g}.` };
      } else state.compareWrongSince = undefined;
      const k = (state.compare.above ? 1 : 0) + (state.compare.below ? 1 : 0);
      return { done: k === 2, progress: k / 2,
               text: `D${c.reg} is ${d}, G${c.pin} is ${g}. Seen ${state.compare.above ? "✓" : "–"} 10 or more, ${state.compare.below ? "✓" : "–"} less.` };
    },

    rundown(c) {
      const e = edges(c.pin).filter((x) => x.v === 0);
      for (const off of e) {
        if (levelAt(c.follower, off.t + 0.3) !== 1) continue;
        const f = edges(c.follower).find((x) => x.v === 0 && x.t > off.t);
        if (!f) return { progress: Math.min(0.99, (now().t - off.t) / c.seconds), text: `G${c.follower} is running on: ${(now().t - off.t).toFixed(1)} seconds.` };
        const lasted = f.t - off.t;
        if (Math.abs(lasted - c.seconds) <= 0.35) return { done: true, progress: 1, text: `G${c.follower} ran on for ${lasted.toFixed(1)} seconds.` };
        return { progress: 0, text: `G${c.follower} ran on for ${lasted.toFixed(1)} seconds, not ${c.seconds}. Try again.` };
      }
      return { progress: 0, text: `Press the button, so G${c.pin} comes on, then let go.` };
    },
  };
  let state = {};

  function evaluate() {
    const c = CHALLENGES.find((x) => x.id === current);
    if (!c || done[c.id] || !samples.length) return;
    if (c.check.ladder) {
      const pins = [c.check.pin, c.check.a, c.check.b, c.check.follower].filter((p) => p !== undefined);
      if (!pins.every(ladderDrives)) {
        status = { progress: 0, text: `This one is for ladder logic: load a ladder program that drives ${pins.map((p) => "G" + p).join(" and ")}, and run it.` };
        return paintStatus();
      }
    }
    const r = CHECKS[c.check.type](c.check);
    status = { text: r.text, progress: Math.max(0, Math.min(1, r.progress || 0)) };
    if (r.done) {
      done[c.id] = true;
      try { localStorage.setItem(STORE_DONE, JSON.stringify(done)); } catch (e) { /* no storage: keep it for now */ }
      log(`Challenge done: ${c.title}.`);
      render();
      return;
    }
    paintStatus();
  }

  // ---- the card ----------------------------------------------------------------------------
  const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;");

  function render() {
    const body = $("challenge-body"), list = visibleList();
    const total = list.length, finished = list.filter((c) => done[c.id]).length;
    let html = `<div class="chprog"><span class="bar"><span style="width:${(100 * finished) / total}%"></span></span>` +
               `<span class="hint">${finished} of ${total} done</span></div>`;
    for (const group of ["Lua", "Ladder"]) {
      const items = list.filter((c) => c.group === group);
      if (!items.length) continue;
      html += `<h4 class="chgroup">${group}</h4><ol class="chlist">`;
      for (const c of items) {
        const isDone = !!done[c.id], open = c.id === current;
        html += `<li class="${isDone ? "done" : ""}${open ? " open" : ""}"><button class="chhead" data-id="${c.id}" aria-expanded="${open}">` +
                `<span class="st" aria-label="${isDone ? "done" : open ? "in progress" : "not started"}">${isDone ? "✓" : open ? "●" : "○"}</span>${esc(c.title)}</button>`;
        if (open) {
          html += `<div class="chbody"><p>${c.task}</p><details><summary>Hint</summary><p>${c.hint}</p></details>` +
                  (isDone ? `<div class="live ok"><span>✓ Done. </span><button class="chagain" data-id="${c.id}">Do it again</button></div>`
                          : `<div class="live"><span class="ring" id="ch-ring"></span><span id="ch-status">Watching…</span></div>`) +
                  "</div>";
        }
        html += "</li>";
      }
      html += "</ol>";
    }
    if (!ladderOpen && !window.Backend) html += `<p class="foot">${CHALLENGES.filter((c) => c.group === "Ladder").length} ladder challenges appear when Ladder Logic is open.</p>`;
    html += '<p class="foot"><button class="linkish" id="ch-reset">Start all over</button></p>';
    body.innerHTML = html;
    body.querySelectorAll(".chhead").forEach((b) => b.onclick = () => openChallenge(b.dataset.id === current ? null : b.dataset.id));
    body.querySelectorAll(".chagain").forEach((b) => b.onclick = () => { delete done[b.dataset.id]; save(); openChallenge(b.dataset.id); });
    $("ch-reset").onclick = () => { done = {}; save(); openChallenge(null); };
    paintStatus();
  }

  function paintStatus() {
    const ring = $("ch-ring"), text = $("ch-status");
    if (ring) ring.style.setProperty("--p", `${Math.round(status.progress * 100)}%`);
    if (text) text.textContent = status.text || "Watching…";
  }

  function save() { try { localStorage.setItem(STORE_DONE, JSON.stringify(done)); } catch (e) { /* no storage */ } }

  function openChallenge(id) {
    current = id;
    since = now() ? now().t : 0;
    state = {};
    status = { text: "Watching…", progress: 0 };
    render();
  }

  function setOpen(open) {
    $("challenge-card").hidden = !open;
    $("btn-challenges").classList.toggle("on", open);
    $("btn-challenges").setAttribute("aria-pressed", String(open));
    try { localStorage.setItem(STORE_OPEN, open ? "1" : "0"); } catch (e) { /* just don't remember */ }
    if (open) render();
  }

  $("btn-challenges").onclick = () => setOpen($("challenge-card").hidden);
  ladderOpen = !$("ladder-card").hidden;                      // ladder.js may have opened it already
  let remembered = location.hash === "#challenges";
  try { remembered = remembered || localStorage.getItem(STORE_OPEN) === "1"; } catch (e) { /* no storage */ }
  if (remembered) setOpen(true);

  return {
    pins(ev) {
      record(ev);
      if (!$("challenge-card").hidden && current) evaluate();
    },
    ladderOpen(open) {
      ladderOpen = open;
      if (!open && CHALLENGES.find((c) => c.id === current)?.group === "Ladder") current = null;
      if (!$("challenge-card").hidden) render();
    },
    list: CHALLENGES,                                         // for tests and docs
    open: openChallenge,
  };
})();
