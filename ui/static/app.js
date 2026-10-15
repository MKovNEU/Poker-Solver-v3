"use strict";

// ================================================================ cards and ranges (mirrors src/core/cards.cpp)

const RANKS = "23456789TJQKA"; // rank 0..12
const SUITS = "cdhs";          // suit 0..3
const GRID = "AKQJT98765432";  // grid row / column 0 is the ace
const SUIT_SYMBOL = { c: "♣", d: "♦", h: "♥", s: "♠" };
const NUM_COMBOS = 1326;
const PLAYERS = ["OOP", "IP"];
const STREETS = ["Flop", "Turn", "River"];
const STREET_KEYS = ["flop", "turn", "river"];

const cardRank = c => c >> 2;
const cardSuit = c => c & 3;
const cardStr = c => RANKS[cardRank(c)] + SUITS[cardSuit(c)];

function parseCard(s) {
  if (!s || s.length !== 2) return -1;
  const r = RANKS.indexOf(s[0].toUpperCase());
  const su = SUITS.indexOf(s[1].toLowerCase());
  return r < 0 || su < 0 ? -1 : r * 4 + su;
}

function comboIndex(a, b) {
  const hi = Math.max(a, b), lo = Math.min(a, b);
  return hi * (hi - 1) / 2 + lo;
}

const COMBO_CARDS = new Array(NUM_COMBOS);
for (let hi = 1; hi < 52; hi++) for (let lo = 0; lo < hi; lo++) COMBO_CARDS[comboIndex(hi, lo)] = [hi, lo];

const comboOf = str => comboIndex(parseCard(str.slice(0, 2)), parseCard(str.slice(2, 4)));

// grid cell (row * 13 + col): pairs on the diagonal, suited above it, offsuit below
function comboCell(combo) {
  const [hi, lo] = COMBO_CARDS[combo];
  const r1 = cardRank(hi), r2 = cardRank(lo);
  if (r1 === r2) return (12 - r1) * 14;
  if (cardSuit(hi) === cardSuit(lo)) return (12 - r1) * 13 + (12 - r2);
  return (12 - r2) * 13 + (12 - r1);
}

function cellLabel(cell) {
  const row = Math.floor(cell / 13), col = cell % 13;
  if (row === col) return GRID[row] + GRID[row];
  if (row < col) return GRID[row] + GRID[col] + "s";
  return GRID[col] + GRID[row] + "o";
}

const CELL_COMBOS = Array.from({ length: 169 }, () => []);
for (let c = 0; c < NUM_COMBOS; c++) CELL_COMBOS[comboCell(c)].push(c);

// "AA", "AK", "AKs", "AKo"
function parseClass(s) {
  if (s.length < 2 || s.length > 3) return null;
  let a = RANKS.indexOf(s[0].toUpperCase()), b = RANKS.indexOf(s[1].toUpperCase());
  if (a < 0 || b < 0) return null;
  if (a === b) return s.length === 3 ? null : { hi: a, lo: a, type: "pair" };
  if (a < b) [a, b] = [b, a];
  let type = "any";
  if (s.length === 3) {
    const k = s[2].toLowerCase();
    if (k === "s") type = "suited";
    else if (k === "o") type = "offsuit";
    else return null;
  }
  return { hi: a, lo: b, type };
}

function setClass(w, hc, value) {
  for (let s1 = 0; s1 < 4; s1++) {
    for (let s2 = 0; s2 < 4; s2++) {
      if (hc.type === "pair" && s1 >= s2) continue;
      if (hc.type === "suited" && s1 !== s2) continue;
      if (hc.type === "offsuit" && s1 === s2) continue;
      w[comboIndex(hc.hi * 4 + s1, hc.lo * 4 + s2)] = value;
    }
  }
}

function applySpec(w, spec, value) {
  const fail = () => { throw new Error(`bad range entry '${spec}'`); };
  if (spec.length === 4) { // exact combo, AhKh
    const c1 = parseCard(spec.slice(0, 2)), c2 = parseCard(spec.slice(2));
    if (c1 >= 0 && c2 >= 0) {
      if (c1 === c2) fail();
      w[comboIndex(c1, c2)] = value;
      return;
    }
  }
  const dash = spec.indexOf("-");
  if (dash >= 0) { // TT-77, A9s-A5s
    const a = parseClass(spec.slice(0, dash)), b = parseClass(spec.slice(dash + 1));
    if (!a || !b || a.type !== b.type) fail();
    if (a.type === "pair") {
      for (let r = Math.min(a.hi, b.hi); r <= Math.max(a.hi, b.hi); r++) setClass(w, { hi: r, lo: r, type: "pair" }, value);
    } else {
      if (a.hi !== b.hi) fail();
      for (let r = Math.min(a.lo, b.lo); r <= Math.max(a.lo, b.lo); r++) setClass(w, { hi: a.hi, lo: r, type: a.type }, value);
    }
    return;
  }
  if (spec.endsWith("+")) { // 77+, A9s+
    const a = parseClass(spec.slice(0, -1));
    if (!a) fail();
    if (a.type === "pair") for (let r = a.hi; r < 13; r++) setClass(w, { hi: r, lo: r, type: "pair" }, value);
    else for (let r = a.lo; r < a.hi; r++) setClass(w, { hi: a.hi, lo: r, type: a.type }, value);
    return;
  }
  const a = parseClass(spec);
  if (!a) fail();
  setClass(w, a, value);
}

function parseRange(text) {
  const w = new Float32Array(NUM_COMBOS);
  for (const token of text.split(/[\s,]+/)) {
    if (!token) continue;
    let spec = token, value = 1;
    const colon = token.indexOf(":");
    if (colon >= 0) {
      spec = token.slice(0, colon);
      value = Number(token.slice(colon + 1));
      if (!isFinite(value) || value < 0 || value > 1) throw new Error(`weight must be in [0, 1]: '${token}'`);
    }
    applySpec(w, spec, value);
  }
  return w;
}

function cellInfo(w, cell) {
  const combos = CELL_COMBOS[cell];
  let sum = 0, uniform = true;
  for (const c of combos) {
    sum += w[c];
    if (Math.abs(w[c] - w[combos[0]]) > 1e-6) uniform = false;
  }
  return { avg: sum / combos.length, uniform, value: w[combos[0]] };
}

const weightSuffix = v => (v >= 0.9995 ? "" : ":" + String(+v.toFixed(3)));

// compact PioSolver text: runs of equal weight become 22+, A2s+, KTo-K8o, mixed classes list their combos
function rangeToText(w) {
  const info = Array.from({ length: 169 }, (_, cell) => cellInfo(w, cell));
  const parts = [], exact = [];
  const runs = (cells, name) => {
    for (let i = 0; i < cells.length;) {
      const v = info[cells[i]];
      if (!v.uniform || v.value <= 0) { i++; continue; }
      let j = i;
      while (j + 1 < cells.length && info[cells[j + 1]].uniform && Math.abs(info[cells[j + 1]].value - v.value) < 1e-6) j++;
      parts.push(name(i, j) + weightSuffix(v.value));
      i = j + 1;
    }
  };
  const pair = k => GRID[k] + GRID[k];
  runs(Array.from({ length: 13 }, (_, k) => k * 14),
    (i, j) => (i === 0 && j > 0 ? pair(j) + "+" : i === j ? pair(i) : `${pair(i)}-${pair(j)}`));
  for (const type of ["s", "o"]) {
    for (let r = 0; r < 12; r++) {
      const cells = [];
      for (let k = r + 1; k < 13; k++) cells.push(type === "s" ? r * 13 + k : k * 13 + r);
      const hand = i => GRID[r] + GRID[r + 1 + i] + type;
      runs(cells, (i, j) => (i === 0 && j > 0 ? hand(j) + "+" : i === j ? hand(i) : `${hand(i)}-${hand(j)}`));
    }
  }
  for (let cell = 0; cell < 169; cell++) {
    if (info[cell].uniform) continue;
    for (const c of CELL_COMBOS[cell]) {
      if (w[c] > 0) exact.push(cardStr(COMBO_CARDS[c][0]) + cardStr(COMBO_CARDS[c][1]) + weightSuffix(w[c]));
    }
  }
  return parts.concat(exact).join(", ");
}

// ================================================================ helpers

const $ = sel => document.querySelector(sel);
const $$ = sel => Array.from(document.querySelectorAll(sel));

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, ch => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[ch]);
}

async function api(path, body) {
  const options = body === undefined ? {} : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) };
  const res = await fetch("/api/" + path, options);
  let data = null;
  try { data = await res.json(); } catch (e) { data = null; }
  if (!res.ok) throw new Error((data && data.error) || `request failed (HTTP ${res.status})`);
  return data;
}

const fmtChips = x => (x == null || !isFinite(x) ? "–" : String(Math.round(x * 100) / 100));
const fmtPct = (x, digits = 1) => (100 * x).toFixed(digits) + "%";
const fmtExpl = p => (p >= 10 ? p.toFixed(1) : p >= 1 ? p.toFixed(2) : p.toFixed(3)) + "%";
const fmtCount = n => (n >= 1e6 ? (n / 1e6).toFixed(2) + "M" : n >= 1e4 ? (n / 1e3).toFixed(1) + "K" : n.toLocaleString());
const fmtMB = mb => (mb >= 1024 ? (mb / 1024).toFixed(1) + " GB" : mb >= 10 ? Math.round(mb) + " MB" : mb.toFixed(1) + " MB");

function fmtTime(s) {
  if (s < 10) return s.toFixed(1) + "s";
  if (s < 60) return Math.round(s) + "s";
  const m = Math.floor(s / 60), r = Math.floor(s - 60 * m);
  return `${m}m ${String(r).padStart(2, "0")}s`;
}

function cardHtml(c, small) {
  const s = SUITS[cardSuit(c)];
  return `<span class="card${small ? " sm" : ""} s-${s}">${RANKS[cardRank(c)]}<span class="s">${SUIT_SYMBOL[s]}</span></span>`;
}

function comboHtml(str) {
  const half = i => `${str[i]}<span class="su-${str[i + 1]}">${SUIT_SYMBOL[str[i + 1]]}</span>`;
  return `<span class="combo">${half(0)}${half(2)}</span>`;
}

function setStatus(text) { $("#status-pill").textContent = text; }

function showView(name) {
  for (const v of ["preflop", "setup", "solve", "strategy"]) $(`#view-${v}`).hidden = v !== name;
  for (const t of $$(".tab")) t.classList.toggle("active", t.dataset.view === name);
  if (name === "solve") renderChart();
}

function enableTab(name, on) { $(`.tab[data-view="${name}"]`).disabled = !on; }

// ================================================================ setup

const DEFAULTS = {
  board: "Qs Jh 2c",
  pot: 5.5,
  stack: 97.5,
  oop_range: "22-QQ,A2s-AQs,K2s+,Q4s+,J6s+,T6s+,96s+,85s+,74s+,63s+,53s+,43s,A8o-AJo,KTo+,QTo+,JTo,T9o,98o",
  ip_range: "22+,A2s+,K2s+,Q5s+,J7s+,T7s+,96s+,85s+,75s+,64s+,54s,A5o+,K9o+,Q9o+,J9o+,T9o",
  // per street: OOP bet, OOP raise, IP bet, IP raise
  sizes: { flop: ["33%", "3x", "33%", "3x"], turn: ["66%", "3x", "66%", "3x"], river: ["66%", "3x", "66%", "3x"] },
  max_raises: 2,
  add_allin_threshold: 1.5,
  force_allin_threshold: 0.15,
  target_exploitability: 0.5,
  iterations: 1000,
  check_every: 10,
  threads: 0,
  isomorphism: true,
  compress: true,
};

const state = {
  ranges: [new Float32Array(NUM_COMBOS), new Float32Array(NUM_COMBOS)],
  rangePlayer: 0,
  board: [],
  examples: {},
  builtConfig: null, // config of the tree the server holds
  builtFresh: false, // that tree hasn't been solved yet
  tree: null,
  freeMemory: null,
};

// ---------------------------------------------------------------- range editor

const editorCells = [];
let painting = null; // weight being painted while the pointer is down

function buildRangeEditor() {
  const grid = $("#range-grid");
  for (let cell = 0; cell < 169; cell++) {
    const c = document.createElement("div");
    c.className = "cell";
    c.dataset.cell = cell;
    c.innerHTML = `<div class="fill"></div><span class="lbl">${cellLabel(cell)}</span><span class="val"></span>`;
    grid.appendChild(c);
    editorCells.push(c);
  }
  const cellAt = e => {
    const t = document.elementFromPoint(e.clientX, e.clientY);
    const c = t && t.closest(".cell");
    return c && grid.contains(c) ? +c.dataset.cell : null;
  };
  grid.addEventListener("pointerdown", e => {
    const cell = cellAt(e);
    if (cell === null) return;
    e.preventDefault();
    grid.setPointerCapture(e.pointerId);
    const w = paintWeight();
    painting = Math.abs(cellInfo(state.ranges[state.rangePlayer], cell).avg - w) < 1e-6 ? 0 : w;
    paint(cell);
  });
  grid.addEventListener("pointermove", e => {
    if (painting === null) return;
    const cell = cellAt(e);
    if (cell !== null) paint(cell);
  });
  const end = () => {
    if (painting === null) return;
    painting = null;
    syncRangeText();
  };
  grid.addEventListener("pointerup", end);
  grid.addEventListener("pointercancel", end);
  grid.style.touchAction = "none";
}

const paintWeight = () => Number($("#paint-weight").value) / 100;

function paint(cell) {
  for (const c of CELL_COMBOS[cell]) state.ranges[state.rangePlayer][c] = painting;
  renderEditorCell(cell);
  renderRangeCount();
}

function renderEditorCell(cell) {
  const { avg } = cellInfo(state.ranges[state.rangePlayer], cell);
  const c = editorCells[cell];
  c.querySelector(".fill").style.height = (avg * 100).toFixed(1) + "%";
  c.querySelector(".val").textContent = avg > 0 && avg < 0.995 ? Math.round(avg * 100) : "";
  c.classList.toggle("out", avg <= 0);
}

function renderRangeCount() {
  const total = state.ranges[state.rangePlayer].reduce((a, b) => a + b, 0);
  $("#range-combos").textContent = `${PLAYERS[state.rangePlayer]}: ${total.toFixed(1)} combos · ${(total / NUM_COMBOS * 100).toFixed(1)}% of hands`;
}

function renderRangeEditor() {
  for (let cell = 0; cell < 169; cell++) renderEditorCell(cell);
  for (const b of $$("#range-player button")) b.classList.toggle("on", +b.dataset.p === state.rangePlayer);
  renderRangeCount();
}

function syncRangeText() {
  $("#range-text").value = rangeToText(state.ranges[state.rangePlayer]);
  $("#range-error").textContent = "";
  refreshTreeSummary();
}

// ---------------------------------------------------------------- board, pot, sizes

function buildDeck(container, onPick) {
  for (const s of [3, 2, 1, 0]) {
    for (let r = 12; r >= 0; r--) {
      const c = r * 4 + s;
      const b = document.createElement("div");
      b.className = `card s-${SUITS[s]}`;
      b.dataset.card = c;
      b.innerHTML = `${RANKS[r]}<span class="s">${SUIT_SYMBOL[SUITS[s]]}</span>`;
      container.appendChild(b);
    }
  }
  container.addEventListener("click", e => {
    const t = e.target.closest(".card");
    if (t && !t.classList.contains("dead")) onPick(+t.dataset.card);
  });
}

function renderBoard() {
  $("#board-cards").innerHTML = state.board.length
    ? state.board.map(c => cardHtml(c)).join("")
    : `<span class="placeholder">Pick a flop, turn or river board (3 to 5 cards)</span>`;
  for (const b of $("#board-deck").children) b.classList.toggle("picked", state.board.includes(+b.dataset.card));
  const first = Math.max(0, state.board.length - 3);
  $$("#sizes-body tr").forEach((tr, s) => tr.classList.toggle("off", s < first));
  refreshTreeSummary();
}

function renderSpr() {
  const pot = Number($("#pot").value), stack = Number($("#stack").value);
  $("#spr").textContent = pot > 0 && stack >= 0 ? (stack / pot).toFixed(1) : "–";
}

function buildSizes() {
  const body = $("#sizes-body");
  STREET_KEYS.forEach((st, s) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${STREETS[s]}</td>` +
      [0, 1, 2, 3].map(k => `<td><input type="text" data-street="${st}" data-k="${k}" placeholder="none" spellcheck="false"></td>`).join("");
    body.appendChild(tr);
  });
}

const sizeInput = (st, k) => $(`#sizes-body input[data-street="${st}"][data-k="${k}"]`);

// ---------------------------------------------------------------- config text <-> form

const asBool = v => ["true", "1", "yes", "on"].includes(String(v).toLowerCase());

function parseConfigText(text) {
  const cfg = JSON.parse(JSON.stringify(DEFAULTS));
  cfg.sizes = { flop: ["", "", "", ""], turn: ["", "", "", ""], river: ["", "", "", ""] };
  const shared = {}, own = {};
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/#.*/, "").trim();
    const eq = line.indexOf("=");
    if (eq < 0) continue;
    const key = line.slice(0, eq).trim().toLowerCase(), value = line.slice(eq + 1).trim();
    const m = key.match(/^(oop_|ip_)?(flop|turn|river)_(bet|raise)$/);
    if (m) {
      const k = m[3] === "bet" ? 0 : 1;
      if (m[1]) own[`${m[1]}${m[2]}${k}`] = value;
      else shared[`${m[2]}${k}`] = value;
      continue;
    }
    if (key === "board") cfg.board = value;
    else if (key === "pot") cfg.pot = Number(value);
    else if (key === "stack" || key === "effective_stack") cfg.stack = Number(value);
    else if (key === "oop_range") cfg.oop_range = value;
    else if (key === "ip_range") cfg.ip_range = value;
    else if (key in cfg && typeof cfg[key] === "number") cfg[key] = Number(value);
    else if (key in cfg && typeof cfg[key] === "boolean") cfg[key] = asBool(value);
  }
  for (const st of STREET_KEYS) {
    for (const k of [0, 1]) {
      const base = shared[`${st}${k}`] ?? "";
      cfg.sizes[st][k] = own[`oop_${st}${k}`] ?? base;
      cfg.sizes[st][2 + k] = own[`ip_${st}${k}`] ?? base;
    }
  }
  return cfg;
}

function setForm(cfg) {
  const clean = cfg.board.replace(/[\s,]/g, "");
  state.board = [];
  for (let i = 0; i + 1 < clean.length; i += 2) {
    const c = parseCard(clean.slice(i, i + 2));
    if (c >= 0 && !state.board.includes(c)) state.board.push(c);
  }
  try {
    state.ranges = [parseRange(cfg.oop_range), parseRange(cfg.ip_range)];
  } catch (e) {
    $("#range-error").textContent = e.message;
  }
  $("#pot").value = cfg.pot;
  $("#stack").value = cfg.stack;
  for (const st of STREET_KEYS) for (let k = 0; k < 4; k++) sizeInput(st, k).value = cfg.sizes[st][k];
  $("#max-raises").value = cfg.max_raises;
  $("#add-allin").value = cfg.add_allin_threshold;
  $("#force-allin").value = cfg.force_allin_threshold;
  $("#target").value = cfg.target_exploitability;
  $("#iterations").value = cfg.iterations;
  $("#check-every").value = cfg.check_every;
  $("#threads").value = cfg.threads;
  $("#isomorphism").checked = cfg.isomorphism;
  $("#compress").checked = cfg.compress;
  renderBoard();
  renderSpr();
  renderRangeEditor();
  syncRangeText();
}

function numberField(id, label, min) {
  const v = Number($(id).value);
  if ($(id).value === "" || !isFinite(v) || v < min) throw new Error(`${label} must be a number ≥ ${min}`);
  return v;
}

function configText() {
  if (state.board.length < 3) throw new Error("Pick a board of 3 to 5 cards");
  for (const p of [0, 1]) {
    if (!state.ranges[p].some(w => w > 0)) throw new Error(`The ${PLAYERS[p]} range is empty`);
  }
  const pot = numberField("#pot", "Pot", 0);
  if (pot <= 0) throw new Error("Pot must be positive");
  const lines = [
    `board = ${state.board.map(cardStr).join(" ")}`,
    `pot = ${pot}`,
    `stack = ${numberField("#stack", "Stack", 0)}`,
    `oop_range = ${rangeToText(state.ranges[0])}`,
    `ip_range = ${rangeToText(state.ranges[1])}`,
  ];
  const first = state.board.length - 3;
  STREET_KEYS.forEach((st, s) => {
    if (s < first) return;
    ["oop", "ip"].forEach((p, pi) => ["bet", "raise"].forEach((kind, k) => {
      const v = sizeInput(st, pi * 2 + k).value.trim();
      if (v) lines.push(`${p}_${st}_${kind} = ${v}`);
    }));
  });
  lines.push(
    `max_raises = ${numberField("#max-raises", "Raises per street", 0)}`,
    `add_allin_threshold = ${numberField("#add-allin", "Add all-in", 0)}`,
    `force_allin_threshold = ${numberField("#force-allin", "Force all-in", 0)}`,
    `target_exploitability = ${numberField("#target", "Target", 0)}`,
    `iterations = ${numberField("#iterations", "Max iterations", 1)}`,
    `check_every = ${numberField("#check-every", "Check every", 1)}`,
    `threads = ${numberField("#threads", "Threads", 0)}`,
    `isomorphism = ${$("#isomorphism").checked}`,
    `compress = ${$("#compress").checked}`,
  );
  return lines.join("\n") + "\n";
}

// ---------------------------------------------------------------- build and run

function showSetupError(message) { $("#setup-error").textContent = message; }

let summaryTimer = null;
const refreshTreeSummary = () => { clearTimeout(summaryTimer); summaryTimer = setTimeout(renderTreeSummary, 150); };

function renderTreeSummary() {
  const t = state.tree;
  if (!t) { $("#tree-summary").innerHTML = ""; return; }
  let changed = true;
  try { changed = configText() !== state.builtConfig; } catch (e) { /* incomplete form */ }
  let html = (changed ? `<span class="muted">Last built tree:</span> ` : "") + `<b>${fmtCount(t.nodes)}</b> nodes (${fmtCount(t.action_nodes)} decisions, ${fmtCount(t.terminal_nodes)} terminal)` +
    ` · <b>${fmtMB(t.memory_mb)}</b> for the solve · ${t.hands[0]} OOP / ${t.hands[1]} IP combos on this board` +
    (t.iso_cards ? ` · ${t.iso_cards} isomorphic cards merged` : "");
  if (changed) {
    html += `<br><span class="muted">The settings changed since, Run builds the new tree. Check tree size shows its size first.</span>`;
  } else if (state.freeMemory && t.memory_mb > 0.85 * state.freeMemory) {
    html += `<br><span class="warn">That is more than this machine has free (${fmtMB(state.freeMemory)}), it will swap. Use fewer sizes or raises.</span>`;
  }
  $("#tree-summary").innerHTML = html;
}

async function buildTree(text) {
  setStatus("Building tree…");
  showSetupError("");
  const res = await api("build", { config: text });
  state.builtConfig = text;
  state.builtFresh = true;
  state.tree = res.tree;
  state.freeMemory = res.free_memory_mb;
  resetSolve();
  renderTreeSummary();
  setStatus("Tree built");
  return res.tree;
}

async function runSolver() {
  let text;
  try {
    text = configText();
  } catch (e) {
    return showSetupError(e.message);
  }
  if (solve.state === "solving" && !confirm("A solve is running. Stop it and start this one?")) return;
  for (const b of [$("#run"), $("#check-tree")]) b.disabled = true;
  try {
    if (text !== state.builtConfig || !state.builtFresh) await buildTree(text);
    const t = state.tree;
    if (state.freeMemory && t.memory_mb > 0.85 * state.freeMemory &&
        !confirm(`This tree needs ${fmtMB(t.memory_mb)} but only ${fmtMB(state.freeMemory)} is free. Solve anyway?`)) {
      setStatus("Tree built");
      return;
    }
    await startSolve(t.iterations, t.target);
  } catch (e) {
    showSetupError(e.message);
    setStatus("Idle");
  } finally {
    for (const b of [$("#run"), $("#check-tree")]) b.disabled = false;
  }
}

async function checkTree() {
  let text;
  try {
    text = configText();
  } catch (e) {
    return showSetupError(e.message);
  }
  if (solve.state === "solving" && !confirm("A solve is running. Stop it?")) return;
  $("#check-tree").disabled = true;
  try {
    await buildTree(text);
  } catch (e) {
    showSetupError(e.message);
    setStatus("Idle");
  } finally {
    $("#check-tree").disabled = false;
  }
}

// ================================================================ solve

const solve = {
  state: "idle", progress: [], iteration: 0, seconds: 0, result: null, error: null,
  target: 0.5, maxIterations: 1000, opened: false, timer: null,
};

function resetSolve() {
  clearTimeout(solve.timer);
  Object.assign(solve, { state: "idle", progress: [], iteration: 0, seconds: 0, result: null, error: null, opened: false });
  $("#continue-target").value = "";
  enableTab("solve", false);
  enableTab("strategy", false);
  nav.nodes.clear();
  nav.evs.clear();
}

async function startSolve(iterations, target) {
  await api("solve", { iterations, target });
  state.builtFresh = false;
  Object.assign(solve, { state: "solving", target, maxIterations: iterations, result: null, error: null });
  nav.nodes.clear();
  nav.evs.clear();
  enableTab("solve", true);
  enableTab("strategy", false);
  showView("solve");
  renderSolve();
  poll();
}

async function poll() {
  clearTimeout(solve.timer);
  try {
    const st = await api(`status?since=${solve.progress.length}`);
    solve.progress.push(...st.progress);
    Object.assign(solve, { iteration: st.iteration, seconds: st.seconds, state: st.state, result: st.result, error: st.error });
  } catch (e) {
    solve.error = e.message;
  }
  renderSolve();
  if (solve.state === "solving") {
    solve.timer = setTimeout(poll, 400);
  } else if (solve.state === "solved") {
    enableTab("strategy", true);
    if (!solve.opened && !(solve.result && solve.result.stopped)) { // after a manual stop, stay for "Keep solving"
      solve.opened = true;
      setTimeout(openStrategy, 600);
    }
  }
}

function renderSolve() {
  const last = solve.progress[solve.progress.length - 1];
  const t = state.tree;
  $("#t-expl").textContent = last ? fmtExpl(last.exploitability) : "–";
  $("#t-expl-sub").textContent = last
    ? `${fmtChips(last.exploitability * t.pot / 100)} chips · target ${solve.target}%`
    : `target ${solve.target}% · first check after ${t ? "iteration 10" : "–"}`;
  $("#t-iter").textContent = solve.iteration.toLocaleString();
  $("#t-iter-sub").textContent = `limit ${solve.maxIterations.toLocaleString()}`;
  $("#t-time").textContent = fmtTime(solve.seconds);
  $("#t-speed").textContent = solve.iteration ? `${(solve.seconds / solve.iteration).toFixed(2)} s per iteration` : "";
  if (t) {
    $("#t-tree").textContent = fmtCount(t.nodes);
    $("#t-tree-sub").textContent = `nodes · ${fmtMB(t.memory_mb)} · ${t.threads} threads`;
  }

  const solving = solve.state === "solving";
  $("#stop").hidden = !solving;
  $("#continue-box").hidden = solve.state !== "solved";
  $("#open-strategy").hidden = solve.state !== "solved";
  if (solve.state === "solved" && !$("#continue-target").value) $("#continue-target").value = +(solve.target / 2).toPrecision(2);

  let result = "";
  if (solve.state === "solved" && solve.result) {
    const r = solve.result;
    result = `${r.stopped ? "Stopped" : "Finished"} after ${r.iterations.toLocaleString()} iterations in ${fmtTime(r.seconds)} at ` +
      `${fmtExpl(r.exploitability)} of the pot. EV: OOP ${fmtChips(r.ev[0])}, IP ${fmtChips(r.ev[1])} of ${fmtChips(t.pot)}.`;
  }
  if (solve.error) result += (result ? " " : "") + `Error: ${solve.error}`;
  $("#solve-result").textContent = result;

  if (solving) setStatus(last ? `Solving · ${fmtExpl(last.exploitability)}` : `Solving · iteration ${solve.iteration}`);
  else if (solve.state === "solved" && solve.result) setStatus(`Solved · ${fmtExpl(solve.result.exploitability)}`);
  else if (solve.state === "failed") setStatus("Solver failed");

  $("#progress-table").innerHTML = solve.progress.map(p =>
    `<tr><td>${p.iteration}</td><td>${fmtTime(p.seconds)}</td><td>${fmtExpl(p.exploitability)}</td><td>${fmtChips(p.exploitability * t.pot / 100)}</td></tr>`).join("");
  if (!$("#view-solve").hidden) renderChart();
}

// ---------------------------------------------------------------- exploitability chart

let chartGeom = null;

function niceStep(max, count) {
  const steps = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200];
  return steps.find(s => max / s <= count) || 7200;
}

function tickTime(s) {
  if (s < 1) return +s.toFixed(2) + "s";
  if (s < 60) return s + "s";
  const m = Math.floor(s / 60), r = s - 60 * m;
  return r ? `${m}m${r}s` : `${m}m`;
}

const tickPct = v => (v >= 1 ? String(v) : String(+v.toPrecision(1))) + "%";

function renderChart() {
  const svg = $("#chart");
  const W = svg.clientWidth, H = svg.clientHeight || 340;
  if (!W) return;
  svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  const pts = solve.progress.filter(p => p.exploitability > 0);
  if (!pts.length) {
    svg.innerHTML = `<text x="${W / 2}" y="${H / 2}" text-anchor="middle">Waiting for the first exploitability check…</text>`;
    chartGeom = null;
    return;
  }
  const m = { l: 52, r: 92, t: 16, b: 30 };
  const iw = W - m.l - m.r, ih = H - m.t - m.b;
  const target = solve.target > 0 ? solve.target : null;
  const last = pts[pts.length - 1];
  const step = niceStep(Math.max(solve.seconds, last.seconds, 0.01), 6);
  const xMax = Math.ceil(Math.max(solve.seconds, last.seconds, 0.01) / step) * step;
  const values = pts.map(p => p.exploitability).concat(target ? [target] : []);
  const lo = Math.log10(Math.min(...values) / 1.5), hi = Math.log10(Math.max(...values) * 1.5);
  const X = s => m.l + (s / xMax) * iw;
  const Y = v => m.t + ((hi - Math.log10(v)) / (hi - lo)) * ih;

  let ticks = [];
  for (let e = Math.floor(lo); e <= Math.ceil(hi); e++) {
    for (const k of [1, 2, 5]) {
      const v = k * 10 ** e, lv = Math.log10(v);
      if (lv >= lo && lv <= hi) ticks.push(+v.toPrecision(1));
    }
  }
  if (ticks.length > 7) ticks = ticks.filter(v => Number.isInteger(Math.log10(v)));

  let html = "";
  for (const v of ticks) {
    html += `<line x1="${m.l}" x2="${m.l + iw}" y1="${Y(v)}" y2="${Y(v)}" stroke="var(--grid)"/>` +
      `<text x="${m.l - 8}" y="${Y(v) + 4}" text-anchor="end">${tickPct(v)}</text>`;
  }
  for (let s = 0; s <= xMax + 1e-9; s += step) {
    const sv = +s.toFixed(6);
    html += `<line x1="${X(sv)}" x2="${X(sv)}" y1="${m.t + ih}" y2="${m.t + ih + 4}" stroke="var(--axis)"/>` +
      `<text x="${X(sv)}" y="${m.t + ih + 18}" text-anchor="middle">${tickTime(sv)}</text>`;
  }
  html += `<line x1="${m.l}" x2="${m.l + iw}" y1="${m.t + ih}" y2="${m.t + ih}" stroke="var(--axis)"/>`;
  if (target) {
    html += `<line x1="${m.l}" x2="${m.l + iw}" y1="${Y(target)}" y2="${Y(target)}" stroke="var(--muted)"/>` +
      `<text x="${m.l + iw + 8}" y="${Y(target) + 4}">target ${target}%</text>`;
  }
  const d = pts.map((p, i) => `${i ? "L" : "M"}${X(p.seconds).toFixed(1)},${Y(p.exploitability).toFixed(1)}`).join("");
  html += `<path d="${d}" fill="none" stroke="var(--accent)" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>`;
  const lx = X(last.seconds), ly = Y(last.exploitability);
  const endY = target && Math.abs(ly - Y(target)) < 14 ? ly - 12 : ly + 4;
  html += `<circle cx="${lx}" cy="${ly}" r="4" fill="var(--accent)" stroke="var(--surface)" stroke-width="2"/>` +
    `<text class="end-label" x="${lx + 9}" y="${endY}">${fmtExpl(last.exploitability)}</text>`;
  html += `<line id="cross" x1="0" x2="0" y1="${m.t}" y2="${m.t + ih}" stroke="var(--text-2)" stroke-width="1" visibility="hidden"/>` +
    `<circle id="hover-dot" r="5" fill="var(--accent)" stroke="var(--surface)" stroke-width="2" visibility="hidden"/>` +
    `<rect x="${m.l}" y="${m.t}" width="${iw}" height="${ih}" fill="transparent" id="hit"/>`;
  svg.innerHTML = html;
  chartGeom = { X, Y, pts };
}

function chartHover(e) {
  const tip = $("#chart-tip");
  if (!chartGeom) return;
  const svg = $("#chart");
  const rect = svg.getBoundingClientRect();
  const x = e.clientX - rect.left;
  let best = chartGeom.pts[0];
  for (const p of chartGeom.pts) if (Math.abs(chartGeom.X(p.seconds) - x) < Math.abs(chartGeom.X(best.seconds) - x)) best = p;
  const px = chartGeom.X(best.seconds), py = chartGeom.Y(best.exploitability);
  const cross = $("#cross"), dot = $("#hover-dot");
  cross.setAttribute("x1", px);
  cross.setAttribute("x2", px);
  cross.setAttribute("visibility", "visible");
  dot.setAttribute("cx", px);
  dot.setAttribute("cy", py);
  dot.setAttribute("visibility", "visible");
  tip.innerHTML = `Iteration ${best.iteration} · ${fmtTime(best.seconds)}<br><b>${fmtExpl(best.exploitability)}</b> of pot · ${fmtChips(best.exploitability * state.tree.pot / 100)} chips`;
  tip.hidden = false;
  const left = px + 14 + tip.offsetWidth > rect.width ? px - 14 - tip.offsetWidth : px + 14;
  tip.style.left = left + "px";
  tip.style.top = Math.max(0, py - tip.offsetHeight - 8) + "px";
}

function chartLeave() {
  $("#chart-tip").hidden = true;
  const cross = $("#cross"), dot = $("#hover-dot");
  if (cross) cross.setAttribute("visibility", "hidden");
  if (dot) dot.setAttribute("visibility", "hidden");
}

// ================================================================ strategy navigator

const nav = {
  path: [],          // action indices and dealt cards, as strings
  nodes: new Map(),  // path key -> node JSON
  evs: new Map(),    // path key -> EV JSON
  mode: "strategy",  // strategy | range | ev
  player: 0,         // player shown in range / EV mode
  hover: null,
  pinned: null,
  seq: 0,
  busy: 0,
};

const pathKey = path => path.join(",");
const currentNode = () => nav.nodes.get(pathKey(nav.path));

function setBusy(delta, label) {
  nav.busy += delta;
  const el = $("#nav-loading");
  el.hidden = nav.busy <= 0;
  if (label) el.textContent = label;
}

async function fetchNode(path) {
  const k = pathKey(path);
  if (!nav.nodes.has(k)) nav.nodes.set(k, await api(`node?path=${encodeURIComponent(k)}`));
  return nav.nodes.get(k);
}

async function go(path) {
  const seq = ++nav.seq;
  setBusy(1, "Loading…");
  try {
    for (let i = 0; i <= path.length; i++) await fetchNode(path.slice(0, i)); // the line bar shows every step
    if (seq !== nav.seq) return;
    nav.path = path;
    nav.pinned = null;
    const node = currentNode();
    if (node.kind === "action") nav.player = node.player;
    $("#nav-error").textContent = "";
    renderStrategy();
    if (nav.mode === "ev") loadEv();
  } catch (e) {
    $("#nav-error").textContent = e.message;
  } finally {
    setBusy(-1);
  }
}

async function loadEv() {
  const k = pathKey(nav.path);
  if (nav.evs.has(k)) return;
  setBusy(1, "Computing EV…");
  try {
    nav.evs.set(k, await api(`ev?path=${encodeURIComponent(k)}`));
    if (pathKey(nav.path) === k) renderStrategy();
  } catch (e) {
    $("#nav-error").textContent = e.message;
  } finally {
    setBusy(-1);
  }
}

function openStrategy() {
  enableTab("strategy", true);
  showView("strategy");
  go([]);
}

// colours by role: fold, check / call / limp, then bets and raises in size order
function actionColors(node) {
  if (!node._colors) {
    let k = 0;
    node._colors = node.actions.map(a => (a.kind === "fold" ? "var(--fold)"
      : a.kind === "check" || a.kind === "call" || a.kind === "limp" ? "var(--passive)" : `var(--agg${Math.min(4, ++k)})`));
  }
  return node._colors;
}

function actionLabel(a) {
  switch (a.kind) {
    case "fold": return "Fold";
    case "check": return "Check";
    case "limp": return "Limp";
    case "call": return `Call ${fmtChips(a.amount)}`;
    case "bet": return `Bet ${fmtChips(a.amount)}`;
    case "raise": return `Raise ${fmtChips(a.amount)}`;
    case "allin": return `All-in ${fmtChips(a.amount)}`;
    default: return a.label;
  }
}

function actionNote(node, a) {
  const facing = node.actions[0].kind === "fold";
  if (a.kind === "bet" || (a.kind === "allin" && !facing)) return `${Math.round((a.amount / node.pot) * 100)}% pot`;
  return "";
}

// opponent reach each of p's hands can face (no shared card), like Solver::compatible_mass: averaging EVs needs
// these weights or the two players' averages don't add up to the pot
function compatibleMass(node, p) {
  if (!node._mass) node._mass = [];
  if (!node._mass[p]) {
    const cardSum = new Float64Array(52), byCombo = new Map();
    let total = 0;
    for (const [str, reach] of node.hands[p ^ 1]) {
      const [a, b] = COMBO_CARDS[comboOf(str)];
      cardSum[a] += reach;
      cardSum[b] += reach;
      total += reach;
      byCombo.set(str, reach);
    }
    node._mass[p] = node.hands[p].map(([str]) => {
      const [a, b] = COMBO_CARDS[comboOf(str)];
      return Math.max(0, total - cardSum[a] - cardSum[b] + (byCombo.get(str) || 0));
    });
  }
  return node._mass[p];
}

// per grid cell: reach, reach-weighted action frequencies and EV, and the hands in it
function aggregate(node, p) {
  const A = node.kind === "action" && p === node.player ? node.actions.length : 0;
  const ev = nav.evs.get(pathKey(nav.path));
  const mass = ev ? compatibleMass(node, p) : null;
  const cells = Array.from({ length: 169 }, () => ({ reach: 0, freq: new Array(A).fill(0), ev: 0, evWeight: 0, hands: [] }));
  node.hands[p].forEach((h, i) => {
    const c = cells[comboCell(comboOf(h[0]))];
    c.reach += h[1];
    for (let a = 0; a < A; a++) c.freq[a] += h[1] * h[2][a];
    const v = ev && ev.ev[p][i];
    if (v != null) {
      c.ev += h[1] * mass[i] * v;
      c.evWeight += h[1] * mass[i];
    }
    c.hands.push(i);
  });
  return cells;
}

function possibleCombos(node) {
  const dead = new Set(node.board.map(parseCard));
  return CELL_COMBOS.map(list => list.filter(c => !dead.has(COMBO_CARDS[c][0]) && !dead.has(COMBO_CARDS[c][1])).length);
}

function evColor(v, pot) {
  // diverging: grey at 0, blue toward winning the whole pot, red toward losing a pot's worth
  const t = Math.max(-1, Math.min(1, v / pot));
  const mid = [0x38, 0x38, 0x35], pos = [0x39, 0x87, 0xe5], neg = [0xe6, 0x67, 0x67];
  const end = t >= 0 ? pos : neg, a = Math.abs(t);
  return `rgb(${mid.map((m, i) => Math.round(m + (end[i] - m) * a)).join(",")})`;
}

const fmtEv = v => (Math.abs(v) >= 10 ? v.toFixed(0) : Math.abs(v) < 0.05 ? "0.0" : v.toFixed(1));

function effectiveMode(node) {
  if (node.kind !== "action" && nav.mode === "strategy") return "range";
  return nav.mode;
}

function shownPlayer(node, mode) { return mode === "strategy" ? node.player : nav.player; }

function renderStrategy() {
  const node = currentNode();
  if (!node) return;
  const mode = effectiveMode(node);
  const p = shownPlayer(node, mode);
  renderLineBar();
  renderSpotHead(node);
  renderDeal(node);
  for (const b of $$("#mode-seg button")) {
    b.classList.toggle("on", b.dataset.mode === mode);
    b.disabled = b.dataset.mode === "strategy" && node.kind !== "action";
  }
  $("#player-seg").hidden = mode === "strategy";
  for (const b of $$("#player-seg button")) b.classList.toggle("on", +b.dataset.p === p);
  const cells = aggregate(node, p);
  renderGrid(node, mode, p, cells);
  renderSummary(node);
  renderDetail(node, mode, p, cells);
}

function renderLineBar() {
  const root = nav.nodes.get("");
  const parts = [];
  const startStreet = root.board.length - 3;
  parts.push(`<div class="step" data-path=""><div class="step-head"><b>${STREETS[startStreet]}</b><span>pot ${fmtChips(root.pot)}</span></div>` +
    `<div class="cards">${root.board.map(s => cardHtml(parseCard(s), true)).join("")}</div></div>`);
  for (let i = 0; i <= nav.path.length; i++) {
    const prefix = nav.path.slice(0, i);
    const node = nav.nodes.get(pathKey(prefix));
    const chosen = i < nav.path.length ? nav.path[i] : null;
    const cls = `step${chosen === null ? " current" : ""}`;
    if (node.kind === "action") {
      const colors = actionColors(node);
      parts.push(`<div class="${cls}" data-path="${pathKey(prefix)}"><div class="step-head"><b>${PLAYERS[node.player]}</b><span>${STREETS[node.street]}</span></div>` +
        node.actions.map((a, j) => `<div class="act${String(j) === chosen ? " chosen" : ""}" data-path="${pathKey(prefix.concat(String(j)))}">` +
          `<span class="dot" style="background:${colors[j]}"></span>${escapeHtml(actionLabel(a))}<span class="pct">${fmtPct(a.freq, 0)}</span></div>`).join("") +
        `</div>`);
    } else if (node.kind === "chance") {
      parts.push(`<div class="${cls}" data-path="${pathKey(prefix)}"><div class="step-head"><b>${STREETS[node.street + 1]}</b><span>pot ${fmtChips(node.pot)}</span></div>` +
        (chosen ? `<div class="cards">${cardHtml(parseCard(chosen), true)}</div>` : `<div class="muted">pick a card</div>`) + `</div>`);
    } else {
      parts.push(`<div class="${cls}"><div class="step-head"><b>${node.kind === "fold" ? `${PLAYERS[node.player]} folds` : "Showdown"}</b></div>` +
        `<div class="muted">pot ${fmtChips(node.pot)}</div></div>`);
    }
  }
  const bar = $("#line-bar");
  bar.innerHTML = parts.join("");
  bar.scrollLeft = bar.scrollWidth;
}

function renderSpotHead(node) {
  const combos = [0, 1].map(p => node.hands[p].reduce((a, h) => a + h[1], 0));
  let who;
  if (node.kind === "action") who = `${PLAYERS[node.player]} to act`;
  else if (node.kind === "chance") who = `Deal the ${STREETS[node.street + 1].toLowerCase()}`;
  else if (node.kind === "fold") who = `${PLAYERS[node.player]} folds, ${PLAYERS[node.player ^ 1]} wins ${fmtChips(node.pot)}`;
  else who = "Showdown";
  $("#spot-head").innerHTML =
    `<div class="cards">${node.board.map(s => cardHtml(parseCard(s))).join("")}</div>` +
    `<div><div class="who">${who}</div><div class="facts"><span>Pot <b>${fmtChips(node.pot)}</b></span>` +
    `<span>Stacks <b>${fmtChips(node.stacks[0])}</b> / <b>${fmtChips(node.stacks[1])}</b></span>` +
    `<span>Combos OOP <b>${combos[0].toFixed(1)}</b> · IP <b>${combos[1].toFixed(1)}</b></span></div></div>`;
}

function renderDeal(node) {
  const deal = $("#deal");
  deal.hidden = node.kind !== "chance";
  if (deal.hidden) return;
  const dead = new Set(node.board.map(parseCard));
  for (const b of deal.querySelectorAll(".deck .card")) b.classList.toggle("dead", dead.has(+b.dataset.card));
  deal.querySelector(".deal-title").textContent = `Pick the ${STREETS[node.street + 1].toLowerCase()} card`;
}

function renderGrid(node, mode, p, cells) {
  const possible = possibleCombos(node);
  const colors = node.kind === "action" ? actionColors(node) : [];
  const html = [];
  for (let cell = 0; cell < 169; cell++) {
    const c = cells[cell];
    const frac = possible[cell] ? Math.min(1, c.reach / possible[cell]) : 0;
    const inRange = c.reach > 1e-6;
    let fill = "", val = "";
    if (inRange && mode === "strategy") {
      fill = c.freq.map((f, a) => (f > 1e-3 * c.reach ? `<i style="flex:${f} 1 0;background:${colors[a]}"></i>` : "")).join("");
    } else if (inRange && mode === "range") {
      fill = `<i style="flex:1 1 0;background:var(--accent)"></i>`;
    } else if (inRange && mode === "ev" && c.evWeight > 0) {
      const v = c.ev / c.evWeight;
      fill = `<i style="flex:1 1 0;background:${evColor(v, node.pot)}"></i>`;
      val = fmtEv(v);
    }
    const cls = `cell${inRange ? "" : " out"}${nav.pinned === cell ? " pinned" : ""}${nav.hover === cell ? " hover" : ""}`;
    html.push(`<div class="${cls}" data-cell="${cell}"><div class="fill" style="height:${(frac * 100).toFixed(1)}%">${fill}</div>` +
      `<span class="lbl">${cellLabel(cell)}</span>${val ? `<span class="val">${val}</span>` : ""}</div>`);
  }
  $("#strat-grid").innerHTML = html.join("");
}

function renderSummary(node) {
  const el = $("#summary");
  const ev = nav.evs.get(pathKey(nav.path));
  let evLine = "";
  if (ev) {
    const avg = [0, 1].map(p => {
      const mass = compatibleMass(node, p);
      let num = 0, den = 0;
      node.hands[p].forEach((h, i) => {
        if (ev.ev[p][i] != null) { num += h[1] * mass[i] * ev.ev[p][i]; den += h[1] * mass[i]; }
      });
      return den > 0 ? fmtChips(num / den) : "–";
    });
    evLine = `<div class="summary-foot">Average EV: OOP ${avg[0]} · IP ${avg[1]} (of the ${fmtChips(node.pot)} pot)</div>`;
  }
  if (node.kind !== "action") {
    const text = node.kind === "chance" ? "Pick a card on the left to continue down the tree."
      : node.kind === "fold" ? `${PLAYERS[node.player]} folds. The hand is over.` : "The hand goes to showdown.";
    el.innerHTML = `<div class="panel-head"><h2>${node.kind === "chance" ? "Chance" : "Hand over"}</h2></div><div class="empty-note">${text}</div>${evLine}`;
    return;
  }
  const colors = actionColors(node);
  const total = node.hands[node.player].reduce((a, h) => a + h[1], 0);
  el.innerHTML =
    `<div class="panel-head"><h2>${PLAYERS[node.player]} strategy</h2><span class="muted">${total.toFixed(1)} combos</span></div>` +
    `<div class="mix-bar">${node.actions.map((a, i) => (a.freq > 0.0005 ? `<i style="flex:${a.freq} 1 0;background:${colors[i]}"></i>` : "")).join("")}</div>` +
    `<div class="actions-list">${node.actions.map((a, i) => {
      const note = actionNote(node, a);
      return `<div class="action-row" data-action="${i}" title="Take this action"><span class="dot" style="background:${colors[i]}"></span>` +
        `<span>${escapeHtml(actionLabel(a))}${note ? ` <span class="muted">${note}</span>` : ""}</span>` +
        `<span class="pct">${fmtPct(a.freq)}</span><span class="combos">${(a.freq * total).toFixed(1)} combos</span></div>`;
    }).join("")}</div>` + evLine;
}

function renderDetail(node, mode, p, cells) {
  const el = $("#detail");
  const cell = nav.hover ?? nav.pinned;
  if (cell === null || cell === undefined) {
    el.innerHTML = `<div class="panel-head"><h2>Hands</h2></div><div class="empty-note">Hover a hand class in the grid to see each combo. Click to keep it here.` +
      (mode !== "ev" ? " Switch to EV to see what each hand is worth." : "") + `</div>`;
    return;
  }
  const c = cells[cell];
  const ev = nav.evs.get(pathKey(nav.path));
  const strategy = mode === "strategy";
  const colors = strategy ? actionColors(node) : [];
  let html = `<div class="panel-head"><h2>${cellLabel(cell)} <span class="muted">${PLAYERS[p]} · ${c.reach.toFixed(2)} combos</span></h2></div>`;
  if (!c.hands.length) {
    el.innerHTML = html + `<div class="empty-note">Not in ${PLAYERS[p]}'s range here.</div>`;
    return;
  }
  if (strategy && c.reach > 0) {
    html += `<div class="mix-bar">${c.freq.map((f, a) => (f > 1e-3 * c.reach ? `<i style="flex:${f} 1 0;background:${colors[a]}"></i>` : "")).join("")}</div>`;
  }
  html += `<table class="detail-table"><thead><tr><th>Combo</th><th>Weight</th>` +
    (strategy ? node.actions.map((a, i) => `<th><span class="dot" style="background:${colors[i]}"></span> ${escapeHtml(actionLabel(a).split(" ")[0])}</th>`).join("") : "") +
    (ev ? "<th>EV</th>" : "") + `</tr></thead><tbody>`;
  for (const i of c.hands) {
    const h = node.hands[p][i];
    html += `<tr><td>${comboHtml(h[0])}</td><td>${h[1].toFixed(2)}</td>` +
      (strategy ? h[2].map(f => `<td>${fmtPct(f, 0)}</td>`).join("") : "") +
      (ev ? `<td>${ev.ev[p][i] == null ? "–" : fmtChips(ev.ev[p][i])}</td>` : "") + `</tr>`;
  }
  el.innerHTML = html + `</tbody></table>`;
}

function renderDetailOnly() {
  const node = currentNode();
  if (!node) return;
  const mode = effectiveMode(node);
  const p = shownPlayer(node, mode);
  renderDetail(node, mode, p, aggregate(node, p));
  for (const c of $("#strat-grid").children) c.classList.toggle("hover", +c.dataset.cell === nav.hover);
}

// ================================================================ preflop

const PF_DEFAULTS = {
  players: 6, stack: 100, ante: 0, open: 2.5, sb_open: 3, limp: "sb", iso: 3.5, threebet_ip: 3, threebet_oop: 4,
  squeeze: 1, fourbet_ip: 2.25, fourbet_oop: 2.5, allin_raise: 4, allin_threshold: 40, realization: 0.7,
  realization_spr: 8, playability: 1, target: 1, iterations: 1000,
};
const PF_NUMBERS = ["stack", "ante", "open", "sb_open", "iso", "threebet_ip", "threebet_oop", "squeeze", "fourbet_ip",
  "fourbet_oop", "allin_threshold", "realization", "realization_spr", "playability", "target", "iterations"];

const pf = {
  state: "none", message: null, progress: [], iteration: 0, seconds: 0, result: null, tree: null, timer: null,
  solveSent: false, saved: false,
  path: [], nodes: new Map(), mode: "strategy", seat: 0, hover: null, pinned: null, seq: 0,
};

const pfKey = path => path.join(",");
const pfNode = () => pf.nodes.get(pfKey(pf.path));
const classCombos = cls => CELL_COMBOS[cls].length; // preflop classes use the grid layout
const rangeCombos = reach => reach.reduce((sum, r, cls) => sum + r * classCombos(cls), 0);

function pfSetForm(cfg) {
  $("#pf-players").value = cfg.players;
  $("#pf-limp").value = cfg.limp;
  $("#pf-allin_raise").value = cfg.allin_raise;
  for (const key of PF_NUMBERS) $(`#pf-${key}`).value = cfg[key];
  pfRenderStatus();
}

function pfParseConfig(text) {
  const cfg = { ...PF_DEFAULTS };
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/#.*/, "").trim();
    const eq = line.indexOf("=");
    if (eq < 0) continue;
    let key = line.slice(0, eq).trim().toLowerCase();
    const value = line.slice(eq + 1).trim().toLowerCase();
    if (key === "target_exploitability") key = "target";
    if (key === "limp") cfg.limp = value;
    else if (key === "players" || key === "allin_raise") cfg[key] = parseInt(value, 10);
    else if (PF_NUMBERS.includes(key)) {
      let v = parseFloat(value); // "3x", "2.5bb" and "40%" read as their number
      if (key === "allin_threshold" && !value.endsWith("%")) v *= 100;
      cfg[key] = v;
    }
  }
  return cfg;
}

function pfConfigText() {
  const lines = [`players = ${$("#pf-players").value}`, `limp = ${$("#pf-limp").value}`, `allin_raise = ${$("#pf-allin_raise").value}`];
  for (const key of PF_NUMBERS) {
    const input = $(`#pf-${key}`), v = Number(input.value);
    if (input.value === "" || !isFinite(v)) throw new Error(`${key.replace(/_/g, " ")} must be a number`);
    if (key === "allin_threshold") lines.push(`allin_threshold = ${v}%`);
    else if (key === "target") lines.push(`target_exploitability = ${v}`);
    else lines.push(`${key} = ${v}`);
  }
  lines.push("check_every = 25");
  return lines.join("\n") + "\n";
}

function pfRenderStatus() {
  const last = pf.progress[pf.progress.length - 1];
  let text = "";
  if (pf.state === "building") text = pf.message || "Building the tree…";
  else if (pf.state === "ready" || pf.state === "solving") {
    text = `Solving · iteration ${pf.iteration}` + (last ? ` · ${last.exploitability.toFixed(2)} mbb/hand` : "") + ` · ${fmtTime(pf.seconds)}`;
  } else if (pf.state === "solved" && pf.result) {
    text = pf.result.cached ? `Loaded the saved solution (${pf.result.iterations} iterations)`
      : `Solved: ${pf.result.iterations} iterations in ${fmtTime(pf.result.seconds)}, ${pf.result.exploitability.toFixed(2)} mbb/hand`;
  }
  $("#pf-status").textContent = text;
  const bar = $("#pf-progress");
  bar.hidden = !(pf.state === "ready" || pf.state === "solving");
  if (pf.tree) bar.firstElementChild.style.width = Math.min(100, (100 * pf.iteration) / pf.tree.iterations) + "%";
  const n = $("#pf-players").value;
  $("#pf-brief").textContent = `${n === "2" ? "heads-up" : n + "-max"} · ${$("#pf-stack").value}bb · open ${$("#pf-open").value} · ` +
    `3-bet ${$("#pf-threebet_ip").value}× / ${$("#pf-threebet_oop").value}×` + (pf.state === "solved" ? " · solved" : "");
}

function pfFail(message) {
  $("#pf-error").textContent = message;
  $("#pf-solve").disabled = false;
  pfRenderStatus();
}

async function pfSolve() {
  let text;
  try {
    text = pfConfigText();
  } catch (e) {
    return pfFail(e.message);
  }
  $("#pf-error").textContent = "";
  clearTimeout(pf.timer);
  Object.assign(pf, { state: "building", message: null, progress: [], result: null, tree: null, solveSent: false, saved: false,
    path: [], pinned: null, hover: null });
  pf.nodes.clear();
  $("#pf-nav").hidden = true;
  $("#pf-solve").disabled = true;
  pfRenderStatus();
  try {
    await api("build?kind=preflop", { config: text });
  } catch (e) {
    return pfFail(e.message);
  }
  pfPoll();
}

async function pfPoll() {
  clearTimeout(pf.timer);
  let st;
  try {
    st = await api(`status?kind=preflop&since=${pf.progress.length}`);
  } catch (e) {
    return pfFail(e.message);
  }
  if (st.state === "none") return;
  pf.progress.push(...st.progress);
  Object.assign(pf, { state: st.state, message: st.message, iteration: st.iteration, seconds: st.seconds, result: st.result, tree: st.tree });
  if (st.state === "ready" && !pf.solveSent) {
    pf.solveSent = true;
    try {
      await api("solve?kind=preflop", { iterations: st.tree.iterations, target: st.tree.target });
    } catch (e) {
      return pfFail(e.message);
    }
    pf.state = "solving";
  }
  pfRenderStatus();
  if (st.state === "failed") return pfFail(st.error || "the preflop solver stopped");
  if (st.state === "solved") {
    $("#pf-solve").disabled = false;
    if (!st.result.cached && !pf.saved) { // next time this game loads instantly
      pf.saved = true;
      api("save?kind=preflop", {}).catch(() => {});
    }
    $("#pf-settings").open = false;
    $("#pf-nav").hidden = false;
    if (!pf.nodes.size) pfGo([]);
    return;
  }
  pf.timer = setTimeout(pfPoll, 350);
}

async function pfFetch(path) {
  const k = pfKey(path);
  if (!pf.nodes.has(k)) pf.nodes.set(k, await api(`node?kind=preflop&path=${encodeURIComponent(k)}`));
  return pf.nodes.get(k);
}

async function pfGo(path) {
  const seq = ++pf.seq;
  $("#pf-loading").hidden = false;
  try {
    for (let i = 0; i <= path.length; i++) await pfFetch(path.slice(0, i));
    if (seq !== pf.seq) return;
    pf.path = path;
    pf.pinned = null;
    const node = pfNode();
    if (node.kind === "action") pf.seat = node.player;
    else if (node.kind === "showdown") pf.seat = node.oop;
    $("#pf-nav-error").textContent = "";
    pfRender();
  } catch (e) {
    $("#pf-nav-error").textContent = e.message;
  } finally {
    if (seq === pf.seq) $("#pf-loading").hidden = true;
  }
}

const pfMode = node => (node.kind === "action" ? pf.mode : "range");
const pfShownSeat = (node, mode) => (mode === "strategy" ? node.player : pf.seat);

function pfRender() {
  const node = pfNode();
  if (!node) return;
  const mode = pfMode(node);
  pfRenderLine();
  pfRenderHead(node);
  for (const b of $$("#pf-mode button")) {
    b.classList.toggle("on", b.dataset.mode === mode);
    b.disabled = b.dataset.mode === "strategy" && node.kind !== "action";
  }
  const seats = $("#pf-seat");
  seats.hidden = mode !== "range";
  seats.innerHTML = node.positions.map((name, s) => `<button data-seat="${s}" class="${s === pf.seat ? "on" : ""}">${name}</button>`).join("");
  pfRenderGrid(node, mode);
  pfRenderSummary(node);
  pfRenderDetail(node, mode);
}

function pfRenderLine() {
  const parts = [];
  for (let i = 0; i <= pf.path.length; i++) {
    const prefix = pf.path.slice(0, i);
    const node = pf.nodes.get(pfKey(prefix));
    const chosen = i < pf.path.length ? pf.path[i] : null;
    let cls = `step${chosen === null ? " current" : ""}`;
    if (node.kind === "action") {
      if (chosen !== null && node.actions[+chosen].kind === "fold") cls += " out";
      const colors = actionColors(node);
      parts.push(`<div class="${cls}" data-path="${pfKey(prefix)}"><div class="step-head"><b>${node.positions[node.player]}</b><span>pot ${fmtChips(node.pot)}</span></div>` +
        node.actions.map((a, j) => `<div class="act${String(j) === chosen ? " chosen" : ""}" data-path="${pfKey(prefix.concat(String(j)))}">` +
          `<span class="dot" style="background:${colors[j]}"></span>${escapeHtml(actionLabel(a))}<span class="pct">${fmtPct(a.freq, 0)}</span></div>`).join("") +
        `</div>`);
    } else {
      const title = node.kind === "fold" ? `${node.positions[node.winner]} wins` : node.allin ? "All-in" : "Flop";
      parts.push(`<div class="${cls}"><div class="step-head"><b>${title}</b></div><div class="muted">pot ${fmtChips(node.pot)}</div></div>`);
    }
  }
  const bar = $("#pf-line");
  bar.innerHTML = parts.join("");
  bar.scrollLeft = bar.scrollWidth;
}

function pfRenderHead(node) {
  const pos = node.positions;
  let who;
  if (node.kind === "action") who = `${pos[node.player]} to act`;
  else if (node.kind === "fold") who = `${pos[node.winner]} wins the pot`;
  else who = `${node.allin ? "All-in" : "Flop"}: ${pos[node.oop]} (OOP) vs ${pos[node.ip]} (IP)`;
  const invested = pos.map((p, s) => (node.folded[s] ? "" : `${p} <b>${fmtChips(node.invested[s])}</b>`)).filter(Boolean).join(" · ");
  $("#pf-head").innerHTML = `<div><div class="who">${who}</div><div class="facts"><span>Pot <b>${fmtChips(node.pot)}</b></span>` +
    (node.kind === "showdown" && !node.allin ? `<span>Behind <b>${fmtChips(node.behind)}</b></span><span>SPR <b>${(node.behind / node.pot).toFixed(1)}</b></span>` : "") +
    `<span>In: ${invested}</span></div></div>`;
}

function pfRenderGrid(node, mode) {
  const reach = node.reach[pfShownSeat(node, mode)];
  const colors = node.kind === "action" ? actionColors(node) : [];
  const html = [];
  for (let cls = 0; cls < 169; cls++) {
    const r = reach[cls], inRange = r > 1e-4;
    let fill = "";
    if (inRange && mode === "strategy") {
      fill = node.strategy[cls].map((f, a) => (f > 1e-3 ? `<i style="flex:${f} 1 0;background:${colors[a]}"></i>` : "")).join("");
    } else if (inRange) {
      fill = `<i style="flex:1 1 0;background:var(--accent)"></i>`;
    }
    const val = mode === "range" && inRange && r < 0.995 ? Math.round(r * 100) : "";
    html.push(`<div class="cell${inRange ? "" : " out"}${pf.pinned === cls ? " pinned" : ""}${pf.hover === cls ? " hover" : ""}" data-cell="${cls}">` +
      `<div class="fill" style="height:${(Math.min(1, r) * 100).toFixed(1)}%">${fill}</div><span class="lbl">${cellLabel(cls)}</span>` +
      (val !== "" ? `<span class="val">${val}</span>` : "") + `</div>`);
  }
  $("#pf-grid").innerHTML = html.join("");
}

function pfRenderSummary(node) {
  const el = $("#pf-summary");
  const pos = node.positions;
  if (node.kind === "action") {
    const colors = actionColors(node);
    const total = rangeCombos(node.reach[node.player]);
    el.innerHTML = `<div class="panel-head"><h2>${pos[node.player]} strategy</h2><span class="muted">${total.toFixed(1)} combos · ${fmtPct(total / NUM_COMBOS)} of hands</span></div>` +
      `<div class="mix-bar">${node.actions.map((a, i) => (a.freq > 0.0005 ? `<i style="flex:${a.freq} 1 0;background:${colors[i]}"></i>` : "")).join("")}</div>` +
      `<div class="actions-list">${node.actions.map((a, i) => `<div class="action-row" data-action="${i}" title="Take this action">` +
        `<span class="dot" style="background:${colors[i]}"></span><span>${escapeHtml(actionLabel(a))}</span>` +
        `<span class="pct">${fmtPct(a.freq)}</span><span class="combos">${(a.freq * total).toFixed(1)} combos</span></div>`).join("")}</div>`;
  } else if (node.kind === "fold") {
    el.innerHTML = `<div class="panel-head"><h2>Hand over</h2></div><div class="empty-note">${pos[node.winner]} wins the ${fmtChips(node.pot)} pot.</div>`;
  } else if (node.allin) {
    el.innerHTML = `<div class="panel-head"><h2>All-in preflop</h2></div><div class="empty-note">No chips behind, the hand is settled by equity.</div>`;
  } else {
    el.innerHTML = `<div class="panel-head"><h2>Heads-up flop</h2></div><div class="flop-box"><div class="facts">` +
      `<span>OOP</span><b>${pos[node.oop]} · ${rangeCombos(node.reach[node.oop]).toFixed(1)} combos</b>` +
      `<span>IP</span><b>${pos[node.ip]} · ${rangeCombos(node.reach[node.ip]).toFixed(1)} combos</b>` +
      `<span>Pot</span><b>${fmtChips(node.pot)}</b><span>Behind</span><b>${fmtChips(node.behind)}</b></div>` +
      `<button class="primary" id="pf-use">Use these ranges in Setup</button>` +
      `<span class="muted">Fills both ranges, the pot and the stack. Pick the board and bet sizes there.</span></div>`;
  }
}

function pfRenderDetail(node, mode) {
  const el = $("#pf-detail");
  const cls = pf.hover ?? pf.pinned;
  if (cls === null || cls === undefined) {
    el.innerHTML = `<div class="panel-head"><h2>Hands</h2></div><div class="empty-note">Hover a hand class to see how it's played. Click to keep it here.</div>`;
    return;
  }
  const seat = pfShownSeat(node, mode);
  const r = node.reach[seat][cls], n = classCombos(cls);
  let html = `<div class="panel-head"><h2>${cellLabel(cls)} <span class="muted">${node.positions[seat]} · ${(r * n).toFixed(2)} of ${n} combos still in</span></h2></div>`;
  if (r <= 1e-4) {
    html += `<div class="empty-note">Not in ${node.positions[seat]}'s range here.</div>`;
  } else if (mode === "strategy") {
    const colors = actionColors(node), s = node.strategy[cls];
    html += `<div class="mix-bar">${s.map((f, a) => (f > 1e-3 ? `<i style="flex:${f} 1 0;background:${colors[a]}"></i>` : "")).join("")}</div>` +
      `<div class="actions-list">${node.actions.map((a, i) => `<div class="action-row"><span class="dot" style="background:${colors[i]}"></span>` +
        `<span>${escapeHtml(actionLabel(a))}</span><span class="pct">${fmtPct(s[i])}</span><span></span></div>`).join("")}</div>`;
  } else {
    html += `<div class="empty-note">${node.positions[seat]} plays ${fmtPct(r)} of these combos to here.</div>`;
  }
  el.innerHTML = html;
}

function pfUseRanges() {
  const node = pfNode();
  if (!node || node.kind !== "showdown") return;
  try {
    state.ranges = [parseRange(node.oop_range), parseRange(node.ip_range)];
  } catch (e) {
    $("#pf-nav-error").textContent = e.message;
    return;
  }
  $("#pot").value = node.pot;
  $("#stack").value = node.behind;
  state.rangePlayer = 0;
  renderRangeEditor();
  syncRangeText();
  renderSpr();
  const pos = node.positions;
  $("#setup-note").innerHTML = `Ranges from the preflop solve: <b>${escapeHtml(node.line.join(", "))}</b>. ` +
    `${pos[node.oop]} is OOP, ${pos[node.ip]} is IP. Pick a board and bet sizes, then run.`;
  $("#setup-note").hidden = false;
  showView("setup");
}

function pfWire() {
  $("#pf-solve").addEventListener("click", pfSolve);
  $("#pf-example").addEventListener("change", e => {
    const text = state.examples[e.target.value];
    if (text) pfSetForm(pfParseConfig(text));
    e.target.value = "";
  });
  $("#pf-settings").addEventListener("input", pfRenderStatus);
  $("#pf-line").addEventListener("click", e => {
    const t = e.target.closest("[data-path]");
    if (t) pfGo(t.dataset.path ? t.dataset.path.split(",") : []);
  });
  $("#pf-summary").addEventListener("click", e => {
    if (e.target.closest("#pf-use")) return pfUseRanges();
    const row = e.target.closest("[data-action]");
    if (row) pfGo(pf.path.concat(row.dataset.action));
  });
  for (const b of $$("#pf-mode button")) {
    b.addEventListener("click", () => {
      if (b.disabled) return;
      pf.mode = b.dataset.mode;
      pfRender();
    });
  }
  $("#pf-seat").addEventListener("click", e => {
    const b = e.target.closest("[data-seat]");
    if (!b) return;
    pf.seat = +b.dataset.seat;
    pfRender();
  });
  const grid = $("#pf-grid");
  grid.addEventListener("mouseover", e => {
    const c = e.target.closest(".cell");
    if (!c || +c.dataset.cell === pf.hover) return;
    pf.hover = +c.dataset.cell;
    const node = pfNode();
    pfRenderDetail(node, pfMode(node));
    for (const cell of grid.children) cell.classList.toggle("hover", +cell.dataset.cell === pf.hover);
  });
  grid.addEventListener("mouseleave", () => {
    pf.hover = null;
    const node = pfNode();
    if (node) pfRenderDetail(node, pfMode(node));
    for (const cell of grid.children) cell.classList.remove("hover");
  });
  grid.addEventListener("click", e => {
    const c = e.target.closest(".cell");
    if (!c) return;
    pf.pinned = pf.pinned === +c.dataset.cell ? null : +c.dataset.cell;
    pfRender();
  });
  window.addEventListener("keydown", e => {
    if ($("#view-preflop").hidden || /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) return;
    if ((e.key === "Backspace" || e.key === "ArrowLeft") && pf.path.length) {
      e.preventDefault();
      pfGo(pf.path.slice(0, -1));
    }
  });
}

// ================================================================ wiring

function wire() {
  for (const t of $$(".tab")) t.addEventListener("click", () => !t.disabled && showView(t.dataset.view));

  // setup
  for (const b of $$("#range-player button")) {
    b.addEventListener("click", () => {
      state.rangePlayer = +b.dataset.p;
      renderRangeEditor();
      syncRangeText();
    });
  }
  $("#paint-weight").addEventListener("input", () => { $("#paint-weight-val").textContent = $("#paint-weight").value + "%"; });
  $("#range-all").addEventListener("click", () => { state.ranges[state.rangePlayer].fill(1); renderRangeEditor(); syncRangeText(); });
  $("#range-clear").addEventListener("click", () => { state.ranges[state.rangePlayer].fill(0); renderRangeEditor(); syncRangeText(); });
  let rangeTimer = null;
  $("#range-text").addEventListener("input", () => {
    clearTimeout(rangeTimer);
    rangeTimer = setTimeout(() => {
      try {
        state.ranges[state.rangePlayer] = parseRange($("#range-text").value);
        $("#range-error").textContent = "";
        renderRangeEditor();
      } catch (e) {
        $("#range-error").textContent = e.message;
      }
    }, 300);
  });
  $("#range-text").addEventListener("change", () => { if (!$("#range-error").textContent) syncRangeText(); });

  buildDeck($("#board-deck"), c => {
    const i = state.board.indexOf(c);
    if (i >= 0) state.board.splice(i, 1);
    else if (state.board.length < 5) state.board.push(c);
    renderBoard();
  });
  $("#board-clear").addEventListener("click", () => { state.board = []; renderBoard(); });
  $("#board-random").addEventListener("click", () => {
    const cards = new Set();
    while (cards.size < 3) cards.add(Math.floor(Math.random() * 52));
    state.board = [...cards];
    renderBoard();
  });
  for (const id of ["#pot", "#stack"]) $(id).addEventListener("input", renderSpr);
  for (const type of ["input", "change"]) $("#view-setup").addEventListener(type, refreshTreeSummary);
  $("#example-select").addEventListener("change", e => {
    const text = state.examples[e.target.value];
    if (text) setForm(parseConfigText(text));
    e.target.value = "";
  });
  $("#run").addEventListener("click", runSolver);
  $("#check-tree").addEventListener("click", checkTree);

  // solve
  $("#stop").addEventListener("click", async () => {
    try { await api("stop", {}); setStatus("Stopping…"); } catch (e) { solve.error = e.message; renderSolve(); }
  });
  $("#continue").addEventListener("click", async () => {
    const target = Number($("#continue-target").value);
    if (!(target >= 0)) return;
    try {
      await startSolve(Math.max(solve.maxIterations, solve.iteration + 1000), target);
      solve.opened = false;
    } catch (e) {
      solve.error = e.message;
      renderSolve();
    }
  });
  $("#open-strategy").addEventListener("click", openStrategy);
  const svg = $("#chart");
  svg.addEventListener("mousemove", chartHover);
  svg.addEventListener("mouseleave", chartLeave);
  window.addEventListener("resize", () => { if (!$("#view-solve").hidden) renderChart(); });

  // strategy
  $("#line-bar").addEventListener("click", e => {
    const t = e.target.closest("[data-path]");
    if (t) go(t.dataset.path ? t.dataset.path.split(",") : []);
  });
  $("#summary").addEventListener("click", e => {
    const row = e.target.closest("[data-action]");
    if (row) go(nav.path.concat(row.dataset.action));
  });
  for (const b of $$("#mode-seg button")) {
    b.addEventListener("click", () => {
      if (b.disabled) return;
      nav.mode = b.dataset.mode;
      renderStrategy();
      if (nav.mode === "ev") loadEv();
    });
  }
  for (const b of $$("#player-seg button")) {
    b.addEventListener("click", () => { nav.player = +b.dataset.p; renderStrategy(); });
  }
  const grid = $("#strat-grid");
  grid.addEventListener("mouseover", e => {
    const c = e.target.closest(".cell");
    if (c && +c.dataset.cell !== nav.hover) {
      nav.hover = +c.dataset.cell;
      renderDetailOnly();
    }
  });
  grid.addEventListener("mouseleave", () => { nav.hover = null; renderDetailOnly(); });
  grid.addEventListener("click", e => {
    const c = e.target.closest(".cell");
    if (!c) return;
    nav.pinned = nav.pinned === +c.dataset.cell ? null : +c.dataset.cell;
    renderStrategy();
  });
  const deal = $("#deal");
  deal.innerHTML = `<div class="deal-title"></div><div class="deck"></div>`;
  buildDeck(deal.querySelector(".deck"), c => go(nav.path.concat(cardStr(c))));
  window.addEventListener("keydown", e => {
    if ($("#view-strategy").hidden || /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) return;
    if ((e.key === "Backspace" || e.key === "ArrowLeft") && nav.path.length) {
      e.preventDefault();
      go(nav.path.slice(0, -1));
    }
  });
}

async function init() {
  buildRangeEditor();
  buildSizes();
  wire();
  pfWire();
  setForm(DEFAULTS);
  pfSetForm(PF_DEFAULTS);
  try {
    const info = await api("info");
    state.freeMemory = info.free_memory_mb;
    if (!info.solver) showSetupError("solver.exe was not found next to the Makefile. Build it first (mingw32-make).");
  } catch (e) {
    showSetupError("Can't reach the UI server: " + e.message);
  }
  try {
    state.examples = await api("examples");
    for (const name of Object.keys(state.examples)) {
      $(name.startsWith("preflop") ? "#pf-example" : "#example-select").add(new Option(name, name));
    }
  } catch (e) { /* examples are optional */ }

  // a preflop game may already be on the server (page reloaded)
  try {
    const st = await api("status?kind=preflop&since=0");
    if (st.state !== "none") {
      if (st.config) pfSetForm(pfParseConfig(st.config));
      pf.solveSent = st.state !== "ready";
      pfPoll();
    }
  } catch (e) { /* no preflop session */ }

  // a solve may still be running on the server (page reloaded), pick it back up
  try {
    const st = await api("status?since=0");
    if (!st.tree) return;
    state.tree = st.tree;
    if (st.config) {
      setForm(parseConfigText(st.config));
      state.builtConfig = configText();
    }
    renderTreeSummary();
    Object.assign(solve, { progress: st.progress, iteration: st.iteration, seconds: st.seconds, state: st.state,
      result: st.result, target: st.tree.target, maxIterations: st.tree.iterations, opened: true });
    if (st.state === "solving" || st.state === "solved") {
      state.builtFresh = false;
      enableTab("solve", true);
      showView("solve");
      renderSolve();
      if (st.state === "solving") poll();
      else enableTab("strategy", true);
    } else {
      state.builtFresh = st.state === "ready";
    }
  } catch (e) { /* no session yet */ }
}

init();
