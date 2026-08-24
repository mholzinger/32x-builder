'use strict';
/* Backrooms 32X map editor. Canvas grid + vector overlays, palette driven by
 * the same registry.json the ROM build uses, load/save .map via the backend
 * (which round-trips through the shared tools/mapfmt.py). */

const ME = {
  reg: null, model: null, name: null,
  layer: 'grid',
  brush: 1,                  // grid cell value to paint
  decalKind: 'outlet',
  partStyle: 'chevron', partHeight: 'full', partCrawl: 'no', partPreset: 'freehand', partHover: null,
  partPending: null, crawlPending: null, darkPending: null,
  cell: 22,
  glyphForVal: {}, colorForVal: {},
};
window.ME = ME;   // shared with the raycaster preview (raycast.js)

const $ = s => document.querySelector(s);
const canvas = $('#grid'), ctx = canvas.getContext('2d');

const jget  = u => fetch(u).then(r => r.json());
const jpost = (u, b) => fetch(u, {
  method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(b)
}).then(r => r.json());

const status = s => { $('#status').textContent = s; };

/* ---------- model helpers (grid stored as glyph-strings) ---------- */
function gridVal(cx, cy) {
  const g = ME.model.grid;
  if (cy < 0 || cy >= g.length || cx < 0 || cx >= g[cy].length) return 1;
  return ME.reg.cells.glyphs[g[cy][cx]];
}
function setGridVal(cx, cy, val) {
  if (cx < 0 || cx >= ME.model.w || cy < 0 || cy >= ME.model.h) return;
  const g = ME.model.grid, ch = ME.glyphForVal[val];
  g[cy] = g[cy].substring(0, cx) + ch + g[cy].substring(cx + 1);
}
function runCells(c) {
  const [dx, dy] = ME.reg.crawl.dir[c.dir];
  const out = [];
  for (let i = 0; i < c.len; i++) out.push([c.cx + dx * i, c.cy + dy * i]);
  return out;
}
function setLight(cx, cy, add) {
  if (!ME.model.lights) ME.model.lights = [];
  if (cx < 0 || cy < 0 || cx >= ME.model.w || cy >= ME.model.h) return;
  const i = ME.model.lights.findIndex(l => l.cx === cx && l.cy === cy);
  if (add && i < 0) { if (!budgetRoom('lights', 1)) return; ME.model.lights.push({ cx, cy }); }
  else if (!add && i >= 0) ME.model.lights.splice(i, 1);
}
function seedLights() {                // the engine's auto-grid as a starting point
  const m = ME.model; m.lights = [];
  for (let my = 1; my < m.h - 1; my += 2)
    for (let mx = 1; mx < m.w - 1; mx += 2)
      if (gridVal(mx, my) === 0) {
        if (!budgetRoom('lights', 1)) return;      // stop at the engine cap
        m.lights.push({ cx: mx, cy: my });
      }
}
function bakeOutlets() {               // place_outlets:N -> explicit decals (the engine rule)
  const m = ME.model;
  const target = (m.options && m.options.place_outlets) | 0;
  let num = m.decals.length;
  if (target <= num) return 0;
  let count = 0;
  for (let y = 1; y < m.h - 1; y++) for (let x = 1; x < m.w - 1; x++) {
    if (gridVal(x, y) === 0) continue;
    if (gridVal(x-1,y)===0 || gridVal(x+1,y)===0 || gridVal(x,y-1)===0 || gridVal(x,y+1)===0) count++;
  }
  if (count === 0) return 0;
  const stride = Math.max(1, Math.floor(count / (target - num)));
  let seen = 0, added = 0;
  for (let y = 1; y < m.h - 1 && num < target; y++)
    for (let x = 1; x < m.w - 1 && num < target; x++) {
      if (gridVal(x, y) === 0) continue;
      let face, ox, oy;
      if (gridVal(x-1,y)===0)      { face='W'; ox=x;     oy=y+0.5; }
      else if (gridVal(x+1,y)===0) { face='E'; ox=x+1;   oy=y+0.5; }
      else if (gridVal(x,y-1)===0) { face='N'; ox=x+0.5; oy=y; }
      else if (gridVal(x,y+1)===0) { face='S'; ox=x+0.5; oy=y+1; }
      else continue;
      const place = (seen % stride === 0); seen++;
      if (!place) continue;
      if (!budgetRoom('decals', 1)) { m.options.place_outlets = 0; return added; }
      m.decals.push({ kind: 'outlet', x: ox, y: oy, z: 0.20, face });
      num++; added++;
    }
  m.options.place_outlets = 0;
  return added;
}
function fitCell() {
  const n = Math.max(ME.model.w, ME.model.h);
  ME.cell = Math.max(10, Math.floor(640 / n));
}

/* ---------- resource budget ----------
 * Live counts vs the registry's engine caps, so authors see limits WHILE
 * building instead of at submit time ("8 hours invested in broken maps").
 * Counts use the same arrays the serializer writes, so the numbers match
 * lint exactly. Placement is hard-stopped at the cap with a status hint. */
/* How many cell-edges a partition run rasterizes to — a 10-cell wall is ONE
 * segment but TEN edges. This is the cap that actually bites (n_pedges is a
 * uint8_t in the ROM), and it's invisible in the segment count: you can sit at
 * 47/64 segments and still be six from the edge ceiling. Same maths as
 * lint_maps' edge_total, so the panel and the build agree. */
const partEdges = p => Math.abs(p.x2 - p.x1) + Math.abs(p.y2 - p.y1);
/* Standalone decals (neanderthal, chair) live in the engine's standups[], NOT
 * the wall-decal array, so they don't spend the wall-decal budget. */
const isStandaloneKind = id => {
  const k = ((ME.reg && ME.reg.decals && ME.reg.decals.kinds) || []).find(x => x.id === id);
  return !!(k && k.standalone);
};
const BUDGET_ROWS = [
  ['partitions', 'Partitions', 'max_partitions', m => m.partitions.length],
  ['edges',      'Partition edges', 'max_partition_edges',
                 m => m.partitions.reduce((n, p) => n + partEdges(p), 0)],
  ['decals',     'Decals',     'max_decals',
                 m => m.decals.filter(d => !isStandaloneKind(d.kind)).length],
  ['chairs',     'Chairs','max_chairs',     m => m.decals.filter(d => d.kind === 'chair').length],
  ['desks',      'Desks', 'max_desks',      m => m.decals.filter(d => d.kind === 'desk').length],
  ['pvms',       'PVMs',  'max_pvms',       m => m.decals.filter(d => d.kind === 'pvm').length],
  ['crawls',     'Crawl runs', 'max_crawl_runs', m => m.crawls.length],
  ['lights',     'Lights',     'max_lights',     m => (m.lights || []).length],
  ['dark',       'Dark rooms', 'max_dark_rooms', m => (m.dark || []).length],
];
function budgetCap(capKey) {
  const lim = (ME.reg && ME.reg.limits) || {};
  return (capKey in lim) ? lim[capKey] : Infinity;
}
/* true if adding n more of `kind` stays within the cap; else flash + hint */
function budgetRoom(kind, n) {
  const row = BUDGET_ROWS.find(r => r[0] === kind);
  if (!row || !ME.model) return true;
  const cap = budgetCap(row[2]);
  if (row[3](ME.model) + n <= cap) return true;
  status(row[1] + ' limit ' + cap + ' reached \u2014 delete some (\u2715 tool) first');
  const el = $('#budget-' + kind);
  if (el) { el.classList.remove('bflash'); void el.offsetWidth; el.classList.add('bflash'); }
  return false;
}
function updateBudget() {
  const box = $('#budget');
  if (!box || !ME.model || !ME.reg) return;
  box.innerHTML = '';
  /* The ~15fps floor in partition edges: the Smooth band's ceiling. The edge
   * CAP (max_partition_edges, 255) is a hardware uint8 limit, ~3.6x looser than
   * this \u2014 so an edge count can be "green" against the cap while already under
   * 15fps. Flag that gap amber on the edges row so the framerate floor is
   * visible before the hardware cap, not only after. */
  const smoothEdges = (ME.reg.playability && ME.reg.playability.bands &&
                       ME.reg.playability.bands[0] && ME.reg.playability.bands[0].max) || Infinity;
  for (const [kind, label, capKey, count] of BUDGET_ROWS) {
    const n = count(ME.model), cap = budgetCap(capKey);
    const overFloor = kind === 'edges' && n > smoothEdges && n < cap;
    const row = document.createElement('div');
    row.id = 'budget-' + kind;
    row.className = 'brow' + (n > cap ? ' bover' : n >= cap ? ' bfull' : overFloor ? ' bwarn' : '');
    if (overFloor)
      row.title = n + ' edges is past the ~15fps floor (' + smoothEdges + '). Still under the '
                + cap + '-edge hardware cap, but expect ~12fps or below \u2014 walk-test it.';
    const name = document.createElement('span'); name.textContent = label;
    const val  = document.createElement('span');
    val.textContent = n + ' / ' + (cap === Infinity ? '\u2014' : cap);
    row.appendChild(name); row.appendChild(val);
    box.appendChild(row);
  }
  /* Playability gradient (measured, see registry.playability): partition edges
   * -> an fps band. It's a PROXY — dense sightlines run slower than the total
   * says — so it's advisory, and the real test is the Walk preview. */
  const play = ME.reg.playability;
  if (play && play.bands) {
    const edges = ME.model.partitions.reduce((n, p) => n + partEdges(p), 0);
    const band = play.bands.find(b => edges <= b.max) || play.bands[play.bands.length - 1];
    const el = document.createElement('div');
    el.className = 'brow bplay';
    el.style.borderLeft = '4px solid ' + band.color;
    el.style.marginTop = '6px';
    const name = document.createElement('span');
    name.textContent = 'Playability';
    const val = document.createElement('span');
    val.textContent = band.label + ' · ' + band.fps;
    val.style.color = band.color;
    el.appendChild(name); el.appendChild(val);
    el.title = 'Estimated from partition edges (' + edges + '). Layout matters — '
             + 'dense sightlines run slower. Walk-test to be sure.';
    box.appendChild(el);
  }
}

/* Which budget rows are over cap right now, with counts — for the load-time
 * notice and the submit preflight. */
function overBudget() {
  if (!ME.model || !ME.reg) return [];
  return BUDGET_ROWS
    .map(([kind, label, capKey, count]) => ({ kind, label, n: count(ME.model), cap: budgetCap(capKey) }))
    .filter(r => r.n > r.cap);
}
/* Announce budget status after any load/import so the author learns limits
 * immediately, not at submit time. Over-budget maps still load and edit
 * freely — the author decides how to trim (or runs Optimize). */
function announceBudget(prefix) {
  const over = overBudget();
  if (!over.length) { status(prefix); return; }
  status(prefix + ' \u2014 OVER BUDGET: ' +
         over.map(r => r.label + ' ' + r.n + '/' + r.cap).join(', ') +
         ' \u2014 trim with the \u2715 tool or try \u2699 Optimize');
}

/* ---------- lossless optimizer (\u2699 Optimize) ----------
 * Reduces the ENCODING without changing what the map looks like: merges
 * collinear touching/overlapping partition segments, re-covers crawl cells
 * with the fewest maximal runs, and drops exact-duplicate decals. Coverage
 * (which edges/cells/sprites exist) is preserved exactly, so it can never
 * alter gameplay — it only recovers budget wasted on redundant encoding. */
function mergePartitions(parts) {
  const groups = new Map();          // line+attrs -> intervals
  const passthrough = [];
  for (const p of parts) {
    const vert = p.x1 === p.x2, horiz = p.y1 === p.y2;
    if (vert === horiz) { passthrough.push(p); continue; }   // point or diagonal: leave alone
    const key = (vert ? 'v' + p.x1 : 'h' + p.y1) +
                '|' + p.style + '|' + p.height + '|' + p.crawl;
    const lo = vert ? Math.min(p.y1, p.y2) : Math.min(p.x1, p.x2);
    const hi = vert ? Math.max(p.y1, p.y2) : Math.max(p.x1, p.x2);
    if (!groups.has(key)) groups.set(key, { vert, line: vert ? p.x1 : p.y1,
      style: p.style, height: p.height, crawl: p.crawl, ivs: [] });
    groups.get(key).ivs.push([lo, hi]);
  }
  const out = [...passthrough];
  for (const g of groups.values()) {
    g.ivs.sort((a, b) => a[0] - b[0]);
    let cur = null;
    for (const [lo, hi] of g.ivs) {
      if (cur && lo <= cur[1]) cur[1] = Math.max(cur[1], hi);   // touch/overlap
      else { if (cur) out.push(segOf(g, cur)); cur = [lo, hi]; }
    }
    if (cur) out.push(segOf(g, cur));
  }
  return out;
  function segOf(g, iv) {
    return g.vert
      ? { x1: g.line, y1: iv[0], x2: g.line, y2: iv[1], style: g.style, height: g.height, crawl: g.crawl }
      : { x1: iv[0], y1: g.line, x2: iv[1], y2: g.line, style: g.style, height: g.height, crawl: g.crawl };
  }
}
function mergeCrawls(crawls) {
  const cells = new Set();
  for (const c of crawls) {
    const dx = c.dir === 'E' ? 1 : 0, dy = c.dir === 'S' ? 1 : 0;
    for (let k = 0; k < c.len; k++) cells.add((c.cx + dx * k) + ',' + (c.cy + dy * k));
  }
  const has = (x, y) => cells.has(x + ',' + y);
  const covered = new Set(), out = [];
  // greedy: repeatedly emit the maximal H/V run covering the most new cells
  let guard = cells.size + 4;
  while (covered.size < cells.size && guard-- > 0) {
    let best = null;   // {new, cx, cy, dir, len}
    for (const key of cells) {
      if (covered.has(key)) continue;
      const [x, y] = key.split(',').map(Number);
      // maximal East run through (x,y)
      let sx = x; while (has(sx - 1, y)) sx--;
      let ex = x; while (has(ex + 1, y)) ex++;
      let hn = 0; for (let i = sx; i <= ex; i++) if (!covered.has(i + ',' + y)) hn++;
      if (!best || hn > best.new) best = { new: hn, cx: sx, cy: y, dir: 'E', len: ex - sx + 1 };
      let sy = y; while (has(x, sy - 1)) sy--;
      let ey = y; while (has(x, ey + 1)) ey++;
      let vn = 0; for (let j = sy; j <= ey; j++) if (!covered.has(x + ',' + j)) vn++;
      if (vn > best.new) best = { new: vn, cx: x, cy: sy, dir: 'S', len: ey - sy + 1 };
    }
    if (!best) break;
    out.push({ cx: best.cx, cy: best.cy, dir: best.dir, len: best.len });
    const dx = best.dir === 'E' ? 1 : 0, dy = best.dir === 'S' ? 1 : 0;
    for (let k = 0; k < best.len; k++) covered.add((best.cx + dx * k) + ',' + (best.cy + dy * k));
  }
  return out;
}
function dedupDecals(decals) {
  const seen = new Set(), out = [];
  for (const d of decals) {
    const k = d.kind + '|' + Math.round(d.x * 1000) + '|' + Math.round(d.y * 1000) + '|' + (d.face || '');
    if (seen.has(k)) continue;
    seen.add(k); out.push(d);
  }
  return out;
}
function doOptimize() {
  if (!ME.model) return;
  const m = ME.model;
  const b = { p: m.partitions.length, c: m.crawls.length, d: m.decals.length };
  m.partitions = mergePartitions(m.partitions);
  m.crawls     = mergeCrawls(m.crawls);
  m.decals     = dedupDecals(m.decals);
  const parts = [];
  if (m.partitions.length < b.p) parts.push('partitions ' + b.p + '\u2192' + m.partitions.length);
  if (m.crawls.length     < b.c) parts.push('crawls ' + b.c + '\u2192' + m.crawls.length);
  if (m.decals.length     < b.d) parts.push('decals ' + b.d + '\u2192' + m.decals.length);
  ME.partPending = ME.crawlPending = ME.darkPending = null;
  draw(); saveWip();
  if (!parts.length) { announceBudget('optimize: already minimal'); return; }
  const over = overBudget();
  const tail = over.length
    ? ' \u2014 still over: ' + over.map(r => r.label + ' ' + r.n + '/' + r.cap).join(', ') +
      ' (needs manual trimming)'
    : ' \u2014 now within budget \u2713';
  status('optimized: ' + parts.join(', ') + tail);
}

/* ---------- rendering ---------- */
function draw() {
  if (!ME.model) return;
  updateBudget();
  const cs = ME.cell, w = ME.model.w, h = ME.model.h;
  canvas.width = w * cs; canvas.height = h * cs;

  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      ctx.fillStyle = ME.colorForVal[gridVal(x, y)] || '#000';
      ctx.fillRect(x * cs, y * cs, cs, cs);
    }

  ctx.fillStyle = 'rgba(80,160,220,0.30)';                 /* crawlspace tint */
  for (const c of ME.model.crawls)
    for (const [cx, cy] of runCells(c)) ctx.fillRect(cx * cs, cy * cs, cs, cs);

  ctx.strokeStyle = '#3a3626'; ctx.lineWidth = 1;          /* grid lines */
  ctx.beginPath();
  for (let x = 0; x <= w; x++) { ctx.moveTo(x * cs, 0); ctx.lineTo(x * cs, h * cs); }
  for (let y = 0; y <= h; y++) { ctx.moveTo(0, y * cs); ctx.lineTo(w * cs, y * cs); }
  ctx.stroke();

  for (const p of ME.model.partitions) {                   /* partitions */
    ctx.strokeStyle = p.style === 'spotted' ? '#9aa84a' : '#caa84a';
    ctx.lineWidth = 4;
    ctx.setLineDash(p.height !== 'full' ? [6, 4] : []);
    ctx.beginPath(); ctx.moveTo(p.x1 * cs, p.y1 * cs); ctx.lineTo(p.x2 * cs, p.y2 * cs); ctx.stroke();
    ctx.setLineDash([]);
    dot(p.x1, p.y1, '#000'); dot(p.x2, p.y2, '#000');
  }

  for (const d of ME.model.decals) drawDecal(d);
  if (ME.model.lights) {                          // ceiling light fixtures
    ctx.fillStyle = '#fff7d0';
    for (const l of ME.model.lights) ctx.fillRect(l.cx * cs + cs * 0.28, l.cy * cs + cs * 0.28, cs * 0.44, cs * 0.44);
  }
  drawSpawn(ME.model.spawn);

  /* Partition authoring aids: flush-run hint, rubber-band + length, preset ghost. */
  if (ME.layer === 'partition') {
    ctx.save();
    /* cyan tint any placed run the engine will flush-align to a wall */
    for (const p of ME.model.partitions) {
      if (p.x1 === p.x2 || p.y1 === p.y2) {
        if (runIsFlush(p.x1, p.y1, p.x2, p.y2)) {
          ctx.strokeStyle = '#33d6ff'; ctx.lineWidth = 2; ctx.setLineDash([3, 3]);
          ctx.beginPath(); ctx.moveTo(p.x1 * cs, p.y1 * cs); ctx.lineTo(p.x2 * cs, p.y2 * cs); ctx.stroke();
          ctx.setLineDash([]);
        }
      }
    }
    const hv = ME.partHover;
    if (hv && (!ME.partPreset || ME.partPreset === 'freehand')) {
      if (ME.partPending) {                       // rubber-band from pending point to cursor
        const a = ME.partPending; let ex = hv.x, ey = hv.y;
        if (a.x !== ex && a.y !== ey) { if (Math.abs(ex - a.x) >= Math.abs(ey - a.y)) ey = a.y; else ex = a.x; }
        ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; ctx.setLineDash([5, 4]);
        ctx.beginPath(); ctx.moveTo(a.x * cs, a.y * cs); ctx.lineTo(ex * cs, ey * cs); ctx.stroke();
        ctx.setLineDash([]);
        const len = Math.abs(ex - a.x) + Math.abs(ey - a.y);
        if (len > 0) {
          ctx.fillStyle = '#fff'; ctx.font = '12px monospace';
          ctx.fillText(len + ' cell' + (len > 1 ? 's' : ''), (ex * cs) + 6, (ey * cs) - 6);
        }
      }
    } else if (hv && ME.partPreset && ME.partPreset !== 'freehand') {
      const segs = PART_PRESETS[ME.partPreset] || [];   // ghost the preset at cursor
      ctx.strokeStyle = 'rgba(255,255,255,0.55)'; ctx.lineWidth = 3; ctx.setLineDash([4, 3]);
      ctx.beginPath();
      for (const s of segs) { ctx.moveTo((hv.x + s[0]) * cs, (hv.y + s[1]) * cs); ctx.lineTo((hv.x + s[2]) * cs, (hv.y + s[3]) * cs); }
      ctx.stroke(); ctx.setLineDash([]);
    }
    ctx.restore();
  }
  if (ME.partPending) dot(ME.partPending.x, ME.partPending.y, '#fff');
  if (ME.model.dark) {
    for (const d of ME.model.dark) {
      ctx.fillStyle = 'rgba(10,10,30,0.55)';                  /* unlit = ink */
      ctx.fillRect(d.x0 * cs, d.y0 * cs, (d.x1 - d.x0 + 1) * cs, (d.y1 - d.y0 + 1) * cs);
      ctx.strokeStyle = '#6a6ad0';
      ctx.strokeRect(d.x0 * cs + 0.5, d.y0 * cs + 0.5,
                     (d.x1 - d.x0 + 1) * cs - 1, (d.y1 - d.y0 + 1) * cs - 1);
    }
  }
  if (ME.darkPending) {
    ctx.strokeStyle = '#6a6ad0';
    ctx.strokeRect(ME.darkPending.cx * cs + 1, ME.darkPending.cy * cs + 1, cs - 2, cs - 2);
  }
  if (ME.crawlPending) {
    ctx.strokeStyle = '#fff'; ctx.lineWidth = 2;
    ctx.strokeRect(ME.crawlPending.cx * cs + 1, ME.crawlPending.cy * cs + 1, cs - 2, cs - 2);
  }
}
function dot(wx, wy, color) {
  const cs = ME.cell; ctx.fillStyle = color;
  ctx.beginPath(); ctx.arc(wx * cs, wy * cs, 3, 0, 7); ctx.fill();
}
function drawDecal(d) {
  const cs = ME.cell, k = ME.reg.decals.kinds.find(x => x.id === d.kind);
  ctx.fillStyle = k ? k.color : '#f0f';
  ctx.beginPath(); ctx.arc(d.x * cs, d.y * cs, cs * 0.22, 0, 7); ctx.fill();
  ctx.fillStyle = '#000'; ctx.font = Math.round(cs * 0.42) + 'px monospace';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  const g = d.kind === 'door' ? 'D' : d.kind === 'neanderthal' ? 'N'
          : d.kind === 'exit_hole' ? '▩' : '⊙';
  ctx.fillText(g, d.x * cs, d.y * cs);
}
function drawSpawn(s) {
  const cs = ME.cell, x = s.x * cs, y = s.y * cs;
  ctx.fillStyle = '#39d353'; ctx.beginPath(); ctx.arc(x, y, cs * 0.3, 0, 7); ctx.fill();
  const dir = { N: [0, -1], S: [0, 1], E: [1, 0], W: [-1, 0] }[s.facing] || [0, -1];
  ctx.strokeStyle = '#000'; ctx.lineWidth = 2;
  ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + dir[0] * cs * 0.5, y + dir[1] * cs * 0.5); ctx.stroke();
}

/* ---------- input ---------- */
let painting = false, paintVal = 0, lightAdd = true;
function evCell(e) {
  const r = canvas.getBoundingClientRect();
  const wx = (e.clientX - r.left) / ME.cell, wy = (e.clientY - r.top) / ME.cell;
  return { cx: Math.floor(wx), cy: Math.floor(wy), wx, wy };
}
function wireCanvas() {
  canvas.oncontextmenu = e => e.preventDefault();
  canvas.onmousedown = e => {
    e.preventDefault();
    const right = e.button === 2, { cx, cy, wx, wy } = evCell(e);
    switch (ME.layer) {
      case 'grid':
        painting = true; paintVal = right ? 0 : ME.brush;
        setGridVal(cx, cy, paintVal); break;
      case 'spawn':
        if (!right) { ME.model.spawn.x = cx + 0.5; ME.model.spawn.y = cy + 0.5; } break;
      case 'decal':
        right ? deleteNearest(ME.model.decals, d => Math.hypot(d.x - wx, d.y - wy), 1.0) : placeDecal(wx, wy); break;
      case 'partition':
        if (right) {                                   // right-click ends the chain, else deletes
          if (ME.partPending) ME.partPending = null;
          else deleteNearest(ME.model.partitions, p => distSeg(wx, wy, p));
        } else clickPartition(wx, wy);
        break;
      case 'dark':
        if (right) { if (ME.darkPending) ME.darkPending = null; else deleteDarkAt(cx, cy); }
        else clickDark(cx, cy);
        break;
      case 'crawl':
        right ? deleteCrawlAt(cx, cy) : clickCrawl(cx, cy); break;
      case 'lights':
        painting = true; lightAdd = !right; setLight(cx, cy, lightAdd); break;
      case 'erase':
        painting = true; if (eraseAt(cx, cy, wx, wy)) status('deleted'); break;
    }
    draw();
  };
  canvas.onmousemove = e => {
    const { cx, cy, wx, wy } = evCell(e);
    $('#coords').textContent = cx + ',' + cy;
    if (ME.layer === 'partition') {                 // live rubber-band / preset ghost
      ME.partHover = { x: Math.round(wx), y: Math.round(wy) };
      if (!painting) { draw(); return; }
    }
    if (!painting) return;
    if (ME.layer === 'grid') { setGridVal(cx, cy, paintVal); draw(); }
    else if (ME.layer === 'lights') { setLight(cx, cy, lightAdd); draw(); }
    else if (ME.layer === 'erase') { if (eraseAt(cx, cy, wx, wy)) draw(); }   // drag to erase
  };
  canvas.onmouseleave = () => { if (ME.partHover) { ME.partHover = null; draw(); } };
  window.addEventListener('mouseup', () => { painting = false; });
}
function placeDecal(wx, wy) {
  const kind = ME.reg.decals.kinds.find(k => k.id === ME.decalKind);
  const budgetKind = (ME.decalKind === 'chair') ? 'chairs' : 'decals';
  if (!budgetRoom(budgetKind, 1)) return;
  if (kind && kind.standalone) {            // free-standing (neanderthal, chair): no wall snap
    ME.model.decals.push({ kind: ME.decalKind, x: Math.floor(wx) + 0.5, y: Math.floor(wy) + 0.5, face: 'N' });
    return;
  }
  const cx = Math.floor(wx), cy = Math.floor(wy), fx = wx - cx, fy = wy - cy;
  const d = { N: fy, S: 1 - fy, W: fx, E: 1 - fx };
  let face = 'N', best = 9;
  for (const k in d) if (d[k] < best) { best = d[k]; face = k; }
  const pos = {
    N: [cx + 0.5, cy], S: [cx + 0.5, cy + 1], W: [cx, cy + 0.5], E: [cx + 1, cy + 0.5]
  }[face];
  ME.model.decals.push({ kind: ME.decalKind, x: pos[0], y: pos[1], face });
}
/* Premade partition objects: relative axis-aligned segment lists, anchored
 * at the clicked cell corner and extending +x/+y. Dropped as real segments so
 * they lint/render exactly like hand-drawn runs. */
const PART_PRESETS = {
  freehand: null,
  counter:  [[0, 0, 4, 0]],
  L:        [[0, 0, 3, 0], [0, 0, 0, 3]],
  U:        [[0, 0, 0, 3], [0, 3, 4, 3], [4, 3, 4, 0]],
  booth:    [[0, 0, 3, 0], [3, 0, 3, 3], [3, 3, 0, 3], [0, 3, 0, 0]],
};
function dropPreset(ax, ay) {
  const segs = PART_PRESETS[ME.partPreset]; if (!segs) return;
  if (!budgetRoom('partitions', segs.length)) return;
  const presetEdges = segs.reduce((n, s) =>
    n + Math.abs(s[2] - s[0]) + Math.abs(s[3] - s[1]), 0);
  if (!budgetRoom('edges', presetEdges)) return;
  for (const s of segs)
    ME.model.partitions.push({
      x1: ax + s[0], y1: ay + s[1], x2: ax + s[2], y2: ay + s[3],
      style: ME.partStyle, height: ME.partHeight, crawl: ME.partCrawl
    });
}
/* True if a run on this integer line has a wall on exactly one side (so the
 * engine will flush-shift it — used for the editor's alignment hint). */
function runIsFlush(x1, y1, x2, y2) {
  const vertical = x1 === x2;
  const line = vertical ? x1 : y1;
  const a = Math.min(vertical ? y1 : x1, vertical ? y2 : x2);
  const b = Math.max(vertical ? y1 : x1, vertical ? y2 : x2);
  let neg = false, pos = false;
  for (let t = a - 1; t <= b; t++) {
    const cLo = vertical ? gridVal(line - 1, t) : gridVal(t, line - 1);
    const cHi = vertical ? gridVal(line, t)     : gridVal(t, line);
    if (cLo && cHi) continue;
    if (cLo) neg = true;
    if (cHi) pos = true;
  }
  return (neg && !pos) || (pos && !neg);
}
function clickPartition(wx, wy) {
  const px = Math.round(wx), py = Math.round(wy);
  if (ME.partPreset && ME.partPreset !== 'freehand') { dropPreset(px, py); return; }
  if (!ME.partPending) { ME.partPending = { x: px, y: py }; return; }
  const a = ME.partPending;
  if (a.x !== px || a.y !== py) {
    if (a.x !== px && a.y !== py) {          // enforce axis-aligned: snap to the longer axis
      if (Math.abs(px - a.x) >= Math.abs(py - a.y)) py = a.y; else px = a.x;
    }
    if (!budgetRoom('partitions', 1)) { ME.partPending = null; return; }
    if (!budgetRoom('edges', Math.abs(px - a.x) + Math.abs(py - a.y))) {
      ME.partPending = null; return;
    }
    ME.model.partitions.push({
      x1: a.x, y1: a.y, x2: px, y2: py,
      style: ME.partStyle, height: ME.partHeight, crawl: ME.partCrawl
    });
    ME.partPending = { x: px, y: py };   // CHAIN: keep going for connected walls
  }
}
/* Dark room: two clicks = opposite corners of an unlit rect. The engine gives
 * it no ceiling fixtures and renders its walls/floor/ceiling toward fog. */
function clickDark(cx, cy) {
  if (!ME.model.dark) ME.model.dark = [];
  if (!ME.darkPending) { ME.darkPending = { cx, cy }; return; }
  const a = ME.darkPending;
  ME.darkPending = null;
  if (!budgetRoom('dark', 1)) return;
  ME.model.dark.push({
    x0: Math.min(a.cx, cx), y0: Math.min(a.cy, cy),
    x1: Math.max(a.cx, cx), y1: Math.max(a.cy, cy),
  });
}
function clickCrawl(cx, cy) {
  if (!ME.crawlPending) { ME.crawlPending = { cx, cy }; return; }
  const s = ME.crawlPending;
  let run = null;
  if (s.cy === cy) {
    const a = Math.min(s.cx, cx), b = Math.max(s.cx, cx);
    run = { cx: a, cy, dir: 'E', len: b - a + 1 };
  } else if (s.cx === cx) {
    const a = Math.min(s.cy, cy), b = Math.max(s.cy, cy);
    run = { cx, cy: a, dir: 'S', len: b - a + 1 };
  }
  if (run && !budgetRoom('crawls', 1)) { ME.crawlPending = null; return; }
  if (run) {
    ME.model.crawls.push(run);
    for (const [x, y] of runCells(run)) setGridVal(x, y, 0);   // a crawl tunnel is open floor
  }
  ME.crawlPending = null;
}
function arrDel(a, o) { const i = a.indexOf(o); if (i >= 0) a.splice(i, 1); }
/* Universal eraser (the ✕ Delete tool): remove whatever is under the cursor,
 * regardless of layer. Overlays (partition / decal / light / crawl) win by
 * proximity; with none close, clear a wall/void grid cell to floor. Returns
 * true if it deleted something (so drag-erase can keep going). */
function eraseAt(cx, cy, wx, wy) {
  let best = null;
  const consider = (d, del) => { if (d != null && (best === null || d < best.d)) best = { d, del }; };
  for (const p of ME.model.partitions) consider(distSeg(wx, wy, p), () => arrDel(ME.model.partitions, p));
  for (const dc of ME.model.decals) consider(Math.hypot(dc.x - wx, dc.y - wy), () => arrDel(ME.model.decals, dc));
  if (ME.model.lights) for (const l of ME.model.lights)
    consider(Math.hypot(l.cx + 0.5 - wx, l.cy + 0.5 - wy), () => arrDel(ME.model.lights, l));
  if (ME.model.dark) for (const d of ME.model.dark)
    if (cx >= d.x0 && cx <= d.x1 && cy >= d.y0 && cy <= d.y1)
      consider(0.3, () => arrDel(ME.model.dark, d));
  if (ME.model.crawls) for (const cr of ME.model.crawls)
    if (runCells(cr).some(([x, y]) => x === cx && y === cy)) consider(0.2, () => arrDel(ME.model.crawls, cr));
  if (best && best.d < 0.85) { best.del(); return true; }
  if (gridVal(cx, cy)) { setGridVal(cx, cy, 0); return true; }   // fall back: clear a wall/void cell
  return false;
}
function deleteNearest(arr, distFn, thresh = 0.6) {
  let bi = -1, bd = thresh;
  arr.forEach((it, i) => { const d = distFn(it); if (d < bd) { bd = d; bi = i; } });
  if (bi >= 0) arr.splice(bi, 1);
}
function deleteCrawlAt(cx, cy) {
  ME.model.crawls = ME.model.crawls.filter(c =>
    !runCells(c).some(([x, y]) => x === cx && y === cy));
}
function deleteDarkAt(cx, cy) {
  if (!ME.model.dark) return;
  ME.model.dark = ME.model.dark.filter(d =>
    !(cx >= d.x0 && cx <= d.x1 && cy >= d.y0 && cy <= d.y1));
}
function distSeg(px, py, p) {                 /* point-to-segment distance */
  const vx = p.x2 - p.x1, vy = p.y2 - p.y1, wx = px - p.x1, wy = py - p.y1;
  const L = vx * vx + vy * vy || 1e-6;
  let t = (wx * vx + wy * vy) / L; t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - (p.x1 + t * vx), py - (p.y1 + t * vy));
}

/* ---------- sidebar ---------- */
function buildLayers() {
  const layers = [['grid', 'Grid'], ['crawl', 'Crawlspace'], ['lights', 'Lights'],
    ['dark', 'Dark rooms'],
    ['partition', 'Partitions'], ['decal', 'Decals'], ['spawn', 'Spawn'], ['erase', '✕ Delete']];
  const c = $('#layers'); c.innerHTML = '';
  for (const [id, label] of layers) {
    const b = document.createElement('button');
    b.textContent = label; b.dataset.layer = id;
    if (id === 'erase') { b.style.color = '#ff6b6b'; b.style.borderColor = '#7a2b2b'; }  // the red X tool
    b.onclick = () => {
      ME.layer = id; ME.partPending = ME.crawlPending = ME.darkPending = null;
      buildLayers(); buildPalette(); draw();
    };
    if (id === ME.layer) b.classList.add('active');
    c.appendChild(b);
  }
}
function paletteBtn(label, color, active, onclick) {
  const b = document.createElement('button');
  if (color) { const s = document.createElement('span'); s.className = 'swatch'; s.style.background = color; b.appendChild(s); }
  b.appendChild(document.createTextNode(label));
  if (active) b.classList.add('active');
  b.onclick = () => { onclick(); buildPalette(); draw(); };
  return b;
}
function choiceRow(label, opts, getter, setter) {
  const wrap = document.createElement('div');
  const h = document.createElement('div');
  h.textContent = label; h.style.cssText = 'margin:6px 0 2px;color:var(--accent)';
  wrap.appendChild(h);
  for (const o of opts) wrap.appendChild(paletteBtn(o, null, getter() === o, () => setter(o)));
  return wrap;
}
function buildPalette() {
  const p = $('#palette'); p.innerHTML = ''; const t = $('#palette-title');
  if (ME.layer === 'grid') {
    t.textContent = 'Cell brush';
    for (const c of ME.reg.cells.palette)
      p.appendChild(paletteBtn(c.label, c.color, ME.brush === c.value, () => ME.brush = c.value));
  } else if (ME.layer === 'decal') {
    t.textContent = 'Decal kind';
    for (const k of ME.reg.decals.kinds)
      p.appendChild(paletteBtn(k.label, k.color, ME.decalKind === k.id, () => ME.decalKind = k.id));
    const po = ME.model.options && ME.model.options.place_outlets;
    if (po > 0) {                       // make the engine's procedural outlets real + editable
      const bake = document.createElement('button');
      bake.textContent = 'Bake procedural outlets';
      bake.style.marginTop = '8px';
      bake.onclick = () => { const n = bakeOutlets(); status('baked ' + n + ' outlets'); draw(); buildPalette(); };
      p.appendChild(bake);
    }
  } else if (ME.layer === 'partition') {
    t.textContent = 'Partition';
    p.appendChild(choiceRow('Shape', Object.keys(PART_PRESETS),
      () => ME.partPreset || 'freehand', v => { ME.partPreset = v; ME.partPending = null; draw(); }));
    p.appendChild(choiceRow('Style', Object.keys(ME.reg.partition.style), () => ME.partStyle, v => ME.partStyle = v));
    p.appendChild(choiceRow('Height', Object.keys(ME.reg.partition.height), () => ME.partHeight, v => ME.partHeight = v));
    p.appendChild(choiceRow('Crawl-under', Object.keys(ME.reg.partition.crawl), () => ME.partCrawl, v => ME.partCrawl = v));
    const hint = document.createElement('p');
    hint.style.color = 'var(--ink)'; hint.style.fontSize = '12px';
    hint.textContent = 'Freehand: click endpoints (chains; right-click ends). Shapes: one click drops the piece. Endpoints snap to the grid; runs along a wall face auto-align flush (cyan hint).';
    p.appendChild(hint);
  } else if (ME.layer === 'erase') {
    t.textContent = '✕ Delete';
    const n = document.createElement('p');
    n.style.color = 'var(--ink)'; n.style.fontSize = '12px';
    n.textContent = 'Click (or drag) any object to delete it — partitions, decals, lights, crawl runs, or wall/void cells. Whatever is under the cursor goes. Works across all layers, no right-click needed.';
    p.appendChild(n);
  } else if (ME.layer === 'spawn') {
    t.textContent = 'Spawn facing';
    for (const f of ['N', 'E', 'S', 'W'])
      p.appendChild(paletteBtn(f, null, ME.model && ME.model.spawn.facing === f, () => ME.model.spawn.facing = f));
  } else if (ME.layer === 'lights') {
    t.textContent = 'Ceiling lights';
    const n = document.createElement('p');
    n.style.color = 'var(--ink)';
    n.textContent = 'Left-drag to place ceiling-light fixtures, right-drag to remove. The preview renders the panels + their light pools. (Empty = the engine auto-grid default.)';
    p.appendChild(n);
    const seed = document.createElement('button');
    seed.textContent = 'Seed from auto-grid';
    seed.onclick = () => { seedLights(); draw(); buildPalette(); };
    p.appendChild(seed);
    const clr = document.createElement('button');
    clr.textContent = 'Clear all';
    clr.onclick = () => { ME.model.lights = []; draw(); };
    p.appendChild(clr);
  } else {
    t.textContent = 'Crawlspace';
    const n = document.createElement('p');
    n.style.color = 'var(--ink)';
    n.textContent = 'Click a start cell, then an aligned end cell (same row or column) to mark a low-ceiling run.';
    p.appendChild(n);
  }
}

/* ---------- file bar ---------- */
/* ---------- story chain (next:) ----------
 * `next:` names the map the EXIT DOOR leads to; empty = procedural. gen_maps
 * resolves it by NAME (uppercased) and HARD-FAILS the build if the target
 * doesn't exist or the map points at itself — so this picker only ever offers
 * titles that exist, and never the current map. */
function buildNextSel() {
  const sel = $('#map-next');
  if (!sel || !ME.model) return;
  const cur  = (ME.model.next || '').trim();
  const self = (ME.model.name || '').trim().toUpperCase();
  const titles = (ME.mapTitles || []).filter(t => t.toUpperCase() !== self);
  sel.innerHTML = '';
  const none = document.createElement('option');
  none.value = ''; none.textContent = '\u2014 procedural \u2014';
  sel.appendChild(none);
  const opts = titles.slice();
  /* Keep a target we don't have locally (imported chain, or the other half of
   * the pair still on disk): rebuilding the list would otherwise drop the
   * value silently on the next save. */
  if (cur && !titles.some(t => t.toUpperCase() === cur.toUpperCase())) opts.push(cur + ' (not in this checkout)');
  for (const t of opts) {
    const bare = t.replace(' (not in this checkout)', '');
    const o = document.createElement('option');
    o.value = bare; o.textContent = t;
    sel.appendChild(o);
  }
  sel.value = cur;
  /* Renaming the map to match its own target would fail the build; surface it
   * here rather than at submit. */
  if (cur && cur.toUpperCase() === self) {
    status('next: cannot point at this map itself \u2014 pick another or set procedural');
  }
}

function syncName() {
  const name = ($('#map-name').value.trim() || ME.model.name || 'untitled');
  ME.model.name = name.toUpperCase().slice(0, 16);
  return name.toLowerCase().replace(/[^a-z0-9_-]/g, '') || 'untitled';
}
async function refreshList() {
  const r = await jget('/maps'); const sel = $('#map-list');
  ME.mapTitles = r.maps.map(m => m.title).filter(Boolean).sort();
  sel.innerHTML = '<option value="">—</option>';
  for (const m of r.maps) {                       // {name, role, folder, protected}
    const o = document.createElement('option');
    o.value = m.name;
    o.textContent = (m.protected ? '🔒 ' : '') + m.name + '  (' + m.role + ')';
    sel.appendChild(o);
  }
  buildNextSel();
}
async function doNew(size) {
  const r = await jget('/new?w=' + size + '&h=' + size);
  ME.model = r.model; ME.name = null; $('#map-name').value = ME.model.name;
  fitCell(); status('new ' + size + '×' + size); buildPalette(); buildNextSel(); draw(); saveWip();
}
async function doLoad(name) {
  const r = await jget('/maps/' + name);
  if (r.error) { status('load error: ' + r.error); return; }
  ME.model = r.model; ME.name = name; $('#map-name').value = ME.model.name;
  $('#map-list').value = name;
  const tag = ME.model.protected || (ME.model.role && ME.model.role !== 'community')
    ? '  — protected: edits Export as your own community copy' : '';
  fitCell(); buildPalette(); buildNextSel(); draw(); saveWip(); announceBudget('loaded ' + name + tag);
}

/* Trigger a browser download of `text` as `fname`. Unlike a clipboard write,
   this can't silently fail on a stale user-gesture, so it's the reliable way to
   get a large map's real content over to GitHub. */
function downloadText(text, fname) {
  const url = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
  const a = document.createElement('a'); a.href = url; a.download = fname;
  document.body.appendChild(a); a.click(); a.remove();
  URL.revokeObjectURL(url);
}

/* Export = download the .map to the user's disk (the save path on the hosted,
   read-only editor). The session never leaves the browser; the file does. */
async function doExport() {
  syncName();
  const r = await fetch('/export', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(ME.model),
  });
  if (!r.ok) { const e = await r.json().catch(() => ({})); status('export error: ' + (e.error || r.status)); return; }
  const blob = await r.blob();
  const fname = (ME.model.name || 'untitled').toLowerCase().replace(/[^a-z0-9_-]/g, '') + '.map';
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a'); a.href = url; a.download = fname;
  document.body.appendChild(a); a.click(); a.remove();
  URL.revokeObjectURL(url);
  status('exported ' + fname + ' → add it under maps/community/ and open a PR');
}

/* ---------- GitHub sign-in (the one-click submit path) ----------
 * The backend has done the whole fork -> branch -> commit -> PR dance as the
 * signed-in user since Phase 3, but the editor never rendered a way to sign
 * in — so every contributor fell through to the pre-filled-URL flow and hit
 * github.com's "you must fork this repository" wall, which is exactly where
 * non-git authors stop. This is that missing control. */
function goSignIn() {
  saveWipNow();                     // the round trip leaves the page; keep the map
  window.location.href = '/auth/login';
}

async function refreshAuth() {
  const el = $('#gh-auth');
  if (!el) return;
  const j = await jget('/auth/user').catch(() => ({}));
  ME.ghOAuth = !!j.oauth;
  ME.ghUser = j.login || null;
  el.textContent = '';
  if (!ME.ghOAuth) return;          // instance without OAuth: manual flow only
  if (ME.ghUser) {
    const who = document.createElement('span');
    who.className = 'gh-who';
    who.textContent = '@' + ME.ghUser;
    who.title = 'Signed in with GitHub — Submit opens your pull request in one click';
    const out = document.createElement('button');
    out.className = 'gh-out'; out.textContent = 'sign out';
    out.onclick = async () => {
      await fetch('/auth/logout', { method: 'POST' });
      await refreshAuth(); status('signed out of GitHub');
    };
    el.append(who, out);
    refreshFork();
  } else {
    const inb = document.createElement('button');
    inb.className = 'gh-signin'; inb.textContent = 'Sign in with GitHub';
    inb.title = 'One click: the editor makes your own copy (fork) of the game repo, ' +
                'commits your map to it and opens the pull request for you.';
    inb.onclick = goSignIn;
    el.append(inb);
    const fw = $('#fork-panel-wrap'); if (fw) fw.style.display = 'none';
  }
}

/* ---------- your own fork ----------
 * A fork isn't plumbing on the way to a PR, it's the contributor's own copy of
 * the game. In it they own the whole budget — the community palette arena, the
 * sprite numbering, the ROM — so they can build out as many maps and assets as
 * they like without competing with anyone else's uploads, and their fork's CI
 * cuts them a ROM on every push. Submitting upstream stays available for work
 * they want in the main game. */
async function refreshFork() {
  const p = $('#fork-panel');
  if (!p || !ME.ghUser) return;
  const j = await jget('/fork/status').catch(() => ({}));
  ME.fork = (j && j.exists) ? j.fork : null;
  ME.forkIsUpstream = !!(j && j.is_upstream);
  const wrap = $('#fork-panel-wrap');
  if (ME.forkIsUpstream) { if (wrap) wrap.style.display = 'none'; return; }  // maintainer
  if (wrap) wrap.style.display = '';
  p.style.display = '';
  p.textContent = '';
  const line = document.createElement('div');
  if (ME.fork) {
    line.innerHTML = 'Your copy: <a href="https://github.com/' + ME.fork +
      '" target="_blank" rel="noopener">' + ME.fork + ' ↗</a>';
    p.appendChild(line);
    const save = document.createElement('button');
    save.className = 'primary'; save.textContent = '⬇ Save to my copy';
    save.title = 'Commit this map to YOUR repo. Your copy builds its own ROM ' +
                 'with your maps and sprites in it — no review, no waiting.';
    save.onclick = doForkSaveMap;
    const ld = document.createElement('button');
    ld.textContent = '↻ my sprites';
    ld.title = 'Re-read the sprites in your copy and put them in the Decals palette.';
    ld.onclick = () => loadForkAssets(false);
    p.appendChild(ld);
    loadForkAssets(true);            // silent on load: your assets are just there
    const rom = document.createElement('button');
    rom.textContent = 'my builds ↗';
    rom.title = 'Your fork’s Actions tab: each push builds a ROM you can download.';
    rom.onclick = () => window.open('https://github.com/' + ME.fork + '/actions', '_blank');
    p.append(save, rom);
  } else {
    line.textContent = 'Make your own copy of the game: your maps, your sprites, ' +
                       'your whole asset budget, your own ROM builds.';
    p.appendChild(line);
    const mk = document.createElement('button');
    mk.className = 'primary'; mk.textContent = 'Create my copy';
    mk.title = 'Forks the game on GitHub under your account. Takes a few seconds.';
    mk.onclick = async () => {
      mk.disabled = true; status('creating your copy of the game…');
      const r = await jpost('/fork/create', {});
      mk.disabled = false;
      if (r.error) { status('fork failed: ' + r.error); return; }
      await refreshFork();
      status('your copy is ready: ' + r.fork + ' — enable Actions on it once and ' +
             'every save builds you a ROM.');
    };
    p.appendChild(mk);
  }
}

/* Pull the signed-in contributor's OWN sprites out of their fork and register
   them in this session — palette button, grid glyph, preview texels — using the
   same injection the bake flow uses. Without it a fork is write-only: assets
   you saved yesterday simply aren't there today. */
async function loadForkAssets(quiet) {
  if (!ME.ghUser || !ME.fork) return 0;
  const j = await jget('/fork/assets').catch(() => ({}));
  const list = (j && j.sprites) || [];
  let added = 0;
  for (const s of list) {
    if (!ME.reg.decals.kinds.find(k => k.id === s.id)) {
      const ent = { id: s.id, kind: s.kind, z: s.z, glyph: '⧉', color: '#b8b0a4',
                    label: s.id + ' (your copy)' };
      if (s.mount !== 'wall') ent.standalone = true;
      ME.reg.decals.kinds.push(ent);
    }
    const A = window.ME.assets;
    if (A && !A.sprites[s.id]) {
      for (let i = 0; i < s.pal8.length; i++) A.palette[s.base + 1 + i] = s.pal8[i];
      const px = new Array(s.w * s.h);
      for (let i = 0; i < s.texels.length; i++)
        px[i] = s.texels[i] === 0 ? -1 : s.base + s.texels[i];
      A.sprites[s.id] = { w: s.w, h: s.h, px, world_h: s.world_h,
                          world_hw: s.world_hw, wall: s.mount === 'wall' };
    }
    added++;
  }
  if (added) buildPalette();
  if (!quiet) {
    status(added ? 'loaded ' + added + ' sprite(s) from ' + ME.fork +
                   ' — they are in the Decals palette'
                 : 'no extra sprites in ' + ME.fork + ' yet');
  }
  return added;
}

async function doForkSaveMap() {
  syncName();
  status('saving to your copy…');
  const r = await fetch('/fork/save_map', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(ME.model),
  });
  const j = await r.json().catch(() => ({}));
  if (!r.ok || !j.ok) {
    const errs = (j.errors || [j.error || ('HTTP ' + r.status)]);
    status('save failed: ' + errs[0]); return;
  }
  statusActions('saved to ' + j.fork + ' as ' + j.path + '.', [
    ['see the build', () => window.open(j.actions_url, '_blank'),
     'Your fork builds a ROM on each push (enable Actions on the fork once).'],
  ]);
}

/* The status bar can carry ACTIONS, not just text — a dead-end message ("sign
   in first") is what stranded the first outside tester. */
function statusActions(text, actions) {
  const el = $('#status');
  el.textContent = text + ' ';
  for (const [label, fn, title] of actions) {
    const b = document.createElement('button');
    b.className = 'status-act'; b.textContent = label;
    if (title) b.title = title;
    b.onclick = fn;
    el.append(b);
  }
}

function promptSignIn(prefix) {
  statusActions((prefix || '') + 'submitting opens a pull request under your own ' +
                'GitHub account:', [
    ['Sign in with GitHub', goSignIn,
     'One click — the editor forks the repo, commits your map and opens the PR for you.'],
    ['do it by hand instead', () => submitManual(),
     'Opens github.com with your map pre-filled. GitHub will ask you to fork first.'],
  ]);
}

/* Submit = the community-PR pipeline. Server runs the SAME lint gate CI runs;
   signed in, it forks/commits/opens the PR for you. Signed out, it hands back a
   pre-filled github.com new-file URL and GitHub walks you through the same steps
   manually. Either way the PR is authored by THEIR GitHub identity — no tokens
   ever touch this server. CI lints + builds the ROM on the PR; merge = next ROM. */
async function doSubmit() {
  syncName();
  /* Preflight the budget CLIENT-SIDE: refuse before posting anything, with
   * the exact overage list. The server/CI lint still guards (source of
   * truth), but authors should never discover limits at submit time. */
  const over = BUDGET_ROWS
    .map(([kind, label, capKey, count]) => ({ label, n: count(ME.model), cap: budgetCap(capKey) }))
    .filter(r => r.n > r.cap);
  if (over.length) {
    status('cannot submit \u2014 over budget: ' +
           over.map(r => r.label + ' ' + r.n + '/' + r.cap).join(', ') +
           ' \u2014 delete with the \u2715 tool until the Budget panel is green');
    updateBudget();
    return;
  }
  if (ME.ghUser) {                     // one-click signed-in path
    status('opening your pull request…');
    const r = await fetch('/submit_pr', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(ME.model),
    });
    const j = await r.json().catch(() => ({}));
    if (r.ok && j.ok) {
      window.open(j.pr_url, '_blank');
      status((j.existing ? 'updated your open PR #' : 'pull request #') + j.pr_number +
             ' — CI is checking your map; once merged it ships in the next ROM release.');
      return;
    }
    const errs = (j.errors || [j.error || ('HTTP ' + r.status)]);
    if (r.status !== 401) { status('submit failed: ' + errs.join(' | ')); return; }
    /* session expired -> sign in again rather than silently demoting them to
     * the manual flow they signed in to avoid. */
    await refreshAuth();
    promptSignIn('your GitHub session expired — ');
    return;
  }
  /* Not signed in, but this instance CAN do it for them: offer that first. */
  if (ME.ghOAuth) { promptSignIn(''); return; }
  await submitManual();
}

/* The zero-auth path: lint server-side, then hand the author a pre-filled
   github.com page. GitHub itself asks them to fork — say so up front, because
   an unexplained "you must fork this repository" reads as a permission error. */
async function submitManual() {
  const r = await fetch('/submit_url', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(ME.model),
  });
  const j = await r.json().catch(() => ({}));
  if (!r.ok || !j.ok) {
    const errs = (j.errors || [j.error || ('HTTP ' + r.status)]);
    status('submit blocked by lint: ' + errs[0] + (errs.length > 1 ? '  (+' + (errs.length - 1) + ' more)' : ''));
    return;
  }
  if (j.url_len < 7500) {                 // fits comfortably in a URL
    window.open(j.url, '_blank');
    status('opening GitHub. It will ask you to FORK the repository — that just makes ' +
           'your own copy to commit into, so click it. Then "Propose new file" → ' +
           '"Create pull request". Your map lands in ' + j.filename + ' under your own name.');
  } else {                                // too big for a URL: hand over a REAL file
    /* The old path opened GitHub's empty new-file editor and copied the map to
     * the clipboard for a manual paste — a paste that silently failed (stale
     * user-gesture) or got skipped, committing an empty file. Instead download
     * the actual .map and open GitHub's drag-drop UPLOAD page: real content, no
     * clipboard, no empty editor. */
    const fname = (j.filename.split('/').pop()) || 'map.map';
    downloadText(j.text, fname);
    window.open(j.upload_url, '_blank');
    status('your map is too big for GitHub’s URL editor, so it downloaded as ' +
           fname + ' — drag that file into the GitHub upload page that just opened ' +
           '(it will ask you to fork the repo first: that is your own copy, click it), ' +
           'then "Propose changes" to open your PR.');
  }
}

/* Import = open a .map a user picked off their disk (parsed by the shared
   Python module so the editor never disagrees with the build). */
function doImport(file) {
  const reader = new FileReader();
  reader.onload = async () => {
    const r = await fetch('/parse', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: reader.result });
    const j = await r.json();
    if (j.error) { status('import error: ' + j.error); return; }
    ME.model = j.model; ME.name = null; $('#map-name').value = ME.model.name;
    fitCell(); buildPalette(); buildNextSel(); draw(); saveWip(); announceBudget('imported ' + (file.name || 'map'));
  };
  reader.readAsText(file);
}

async function doSave() {                          // local-dev only (hidden when readonly)
  syncName();
  const r = await jpost('/maps/' + syncName(), ME.model);
  if (r.error) { status('save error: ' + r.error); return; }
  const how = r.cloned ? 'cloned to maps/community/' : 'saved to maps/community/';
  ME.name = r.name; status(how + ' as ' + r.name + '.map  (rebuild the ROM to compile it in)');
  await refreshList(); $('#map-list').value = r.name;
}

function wireFileBar() {
  $('#btn-new').onclick = () => doNew(parseInt($('#new-size').value, 10));
  $('#btn-export').onclick = doExport;
  $('#btn-optimize').onclick = doOptimize;
  $('#map-next').onchange = e => {
    ME.model.next = e.target.value;
    saveWip();
    status(e.target.value ? 'exit door \u2192 ' + e.target.value
                          : 'exit door \u2192 procedural level');
  };
  /* A rename changes what "itself" means, so re-filter the picker. */
  $('#map-name').oninput = () => { if (ME.model) { syncName(); buildNextSel(); } };
  $('#btn-submit').onclick = doSubmit;
  $('#btn-import').onclick = () => $('#file-import').click();
  $('#file-import').onchange = e => { if (e.target.files[0]) doImport(e.target.files[0]); e.target.value = ''; };
  $('#btn-save').onclick = doSave;
  $('#btn-reload').onclick = () => { if (ME.name) doLoad(ME.name); };
  $('#map-list').onchange = e => { if (e.target.value) doLoad(e.target.value); };
  $('#btn-walk').onclick = () => window.RC.start();
  $('#btn-exit-preview').onclick = () => window.RC.stop();
}

/* ---------- in-browser session persistence (per the overlay editor's pattern) ---------- */
const WIP_KEY = 'backrooms-map-editor-wip';
let _wipTimer = null;
function saveWip() {                                // debounced; keeps the session local to this browser
  if (_wipTimer) return;
  _wipTimer = setTimeout(() => {
    _wipTimer = null;
    try { localStorage.setItem(WIP_KEY, JSON.stringify({ model: ME.model, name: ME.name })); } catch (e) {}
  }, 800);
}
function saveWipNow() {                             // before we leave the page (sign-in)
  if (_wipTimer) { clearTimeout(_wipTimer); _wipTimer = null; }
  try { localStorage.setItem(WIP_KEY, JSON.stringify({ model: ME.model, name: ME.name })); }
  catch (e) {}
}
function loadWip() {
  try { const s = localStorage.getItem(WIP_KEY); return s ? JSON.parse(s) : null; } catch (e) { return null; }
}

/* ---------- init ---------- */
/* Each init phase is isolated: one throwing phase must NOT abort the rest —
 * a single failure used to leave the whole editor (incl. the map dropdown)
 * dead with no on-screen clue. Errors are surfaced in the status bar +
 * console so a browser-only failure is visible, not silent. */
async function step(label, fn) {
  try { return await fn(); }
  catch (e) { console.error('[init:' + label + ']', e); status('init ' + label + ' failed: ' + (e && e.message || e)); return null; }
}
async function init() {
  await step('assets', async () => {
    ME.reg = await jget('/registry');
    ME.assets = await jget('/assets');   // real ROM palette + base indices (Walk preview)
    for (const c of ME.reg.cells.palette) {
      ME.glyphForVal[c.value] = c.glyph; ME.colorForVal[c.value] = c.color;
    }
  });
  /* Map list FIRST + independently: it only needs /maps + #map-list (both
   * simple), so a later UI-wiring failure can never leave it empty. */
  await step('maplist', refreshList);
  await step('layers',   () => buildLayers());
  await step('palette',  () => buildPalette());
  await step('filebar',  () => wireFileBar());
  await step('canvas',   () => wireCanvas());
  await step('config',   async () => { const cfg = await jget('/config'); if (cfg.readonly) $('#btn-save').style.display = 'none'; });
  await step('auth',     () => refreshAuth());
  await step('firstmap', async () => {
    const wip = loadWip();
    if (wip && wip.model) {
      ME.model = wip.model; ME.name = wip.name; $('#map-name').value = ME.model.name;
      fitCell(); status('restored your in-progress map'); buildPalette(); draw();
    } else {
      await doNew(16);
    }
  });
  setInterval(saveWip, 4000);                      // periodic safety net while editing
}
init();


/* ---------- community sprite upload (Add a sprite panel) ----------
 * POSTs the image to /bake_sprite; the server bakes it against the shared
 * COMM ramp and returns the full submission bundle. The baked standee is
 * immediately PLACEABLE in this session (registry + assets patched
 * client-side) so authors can walk their map with it before the PR merges. */
(function () {
  const $s = id => document.getElementById(id);
  if (!$s('spr-bake')) return;
  let bundle = null;
  const mountVal = () =>
    (document.querySelector('input[name="spr-mount"]:checked') || {}).value || 'billboard';
  const RULES = {
    billboard: 'Free-standing: it will stand on the floor and block ' +
               'movement, like the neanderthal.',
    wall: 'Wall decal: painted flat on a wall face at the height you ' +
          'set, like the outlet.'
  };
  /* Steps 1+2 gate the bake; the button stays CLICKABLE either way and the
     click says what is missing. It used to be disabled until ready, and a
     disabled button swallows clicks silently \u2014 reported from the field as
     "clicking on step five doesn't do anything" (the only feedback was a
     hover tooltip). Dim it when not ready, never deafen it. */
  const NAME_RE = /^[a-z][a-z0-9_]{1,15}$/;
  const nameFix = n => n.toLowerCase().replace(/[^a-z0-9_]+/g, '_')
                        .replace(/^[^a-z]+/, '').slice(0, 16);
  const readiness = () => {
    const f = $s('spr-file').files[0];
    const name = $s('spr-name').value.trim();
    const nameOk = NAME_RE.test(name);
    $s('spr-name-hint').textContent =
      name === '' ? '' :
      nameOk ? '\u2713 ' + name : 'lowercase a-z 0-9 _ only, 2-16 chars, letter first';
    const btn = $s('spr-bake');
    btn.style.opacity = (f && nameOk) ? '' : '.55';
    btn.title = !f ? 'Step 1: pick an image first'
              : !nameOk ? 'Step 2: give it a valid name'
              : 'Bake it \u2014 the preview shows exactly what ships';
  };
  let sprRot = 0, sprMir = false;
  let sprMark = 0;          /* 0 = whole image; N = only that mark of a sheet */
  const thumbCss = () => {
    $s('spr-thumb').style.transform =
      'rotate(' + sprRot + 'deg) scaleX(' + (sprMir ? -1 : 1) + ')';
  };
  $s('spr-file').addEventListener('change', () => {
    const f = $s('spr-file').files[0];
    sprRot = 0; sprMir = false; sprMark = 0; thumbCss();
    $s('spr-sheet').style.display = 'none';
    if (f) {
      $s('spr-thumb').src = URL.createObjectURL(f);
      $s('spr-orient').style.display = '';
    } else $s('spr-orient').style.display = 'none';
    readiness();
  });
  $s('spr-rot').addEventListener('click', () => { sprRot = (sprRot + 90) % 360; thumbCss(); });
  $s('spr-mir').addEventListener('click', () => { sprMir = !sprMir; thumbCss(); });
  $s('spr-name').addEventListener('input', readiness);
  for (const r of document.querySelectorAll('input[name="spr-mount"]'))
    r.addEventListener('change', () => {
      const m = mountVal();
      $s('spr-rules').innerHTML = RULES[m];
      $s('spr-z-label').style.display = (m === 'wall') ? '' : 'none';
      $s('spr-h-label').firstChild.textContent = (m === 'wall') ? 'size ' : 'size ';
    });
  $s('spr-h').addEventListener('input', () =>
    $s('spr-h-val').textContent = ($s('spr-h').value / 100).toFixed(2));
  $s('spr-z').addEventListener('input', () =>
    $s('spr-z-val').textContent = ($s('spr-z').value / 100).toFixed(2));
  $s('spr-bake').addEventListener('click', async () => {
    const f = $s('spr-file').files[0];
    if (!f) { $s('spr-msg').textContent = 'step 1: pick an image first'; return; }
    const name = $s('spr-name').value.trim();
    if (!NAME_RE.test(name)) {
      const fix = nameFix(name);
      $s('spr-msg').textContent = 'step 2: name it first — lowercase ' +
        'a-z 0-9 _ only, 2-16 chars, letter first' +
        (fix && fix.length >= 2 ? ' (try "' + fix + '")' : '');
      return;
    }
    const fd = new FormData();
    fd.append('image', f);
    fd.append('id', $s('spr-name').value.trim());
    fd.append('height', ($s('spr-h').value / 100).toFixed(2));
    fd.append('mount', mountVal());
    fd.append('z', ($s('spr-z').value / 100).toFixed(2));
    fd.append('rotate', String(sprRot));
    fd.append('mirror', sprMir ? '1' : '0');
    fd.append('hi', $s('spr-hi').checked ? '1' : '0');
    fd.append('mark', String(sprMark));
    $s('spr-msg').textContent = 'baking…';
    const r = await fetch('/bake_sprite', { method: 'POST', body: fd });
    const j = await r.json();
    if (!j.ok) { $s('spr-msg').textContent = j.error || 'bake failed'; return; }
    bundle = j;
    $s('spr-preview').innerHTML =
      '<img alt="preview" style="image-rendering:pixelated;background:#333" ' +
      'src="data:image/png;base64,' + j.preview_png + '">' +
      '<div style="font-size:11px">' + j.id + ' — ' + j.w + 'x' + j.h +
      ' texels, kind ' + j.kind + ' (' +
      (j.mount === 'wall' ? 'wall decal' : 'standee') +
      ', ' + j.pal8.length + ' colors' + (j.hi ? ', +hi-res' : '') +
      (j.mark ? ', mark ' + j.mark + ' of ' + j.marks : '') + ')</div>';
    $s('spr-actions').style.display = '';
    /* A SHEET (several separate marks on one canvas) is the one case where the
       preview looks plausible and the in-game decal is a smudge: the marks are
       all there, each shrunk into a corner of one decal. Say so, and offer the
       per-mark bake right here. */
    showSheetChoice(j);
    /* Palette headroom: the shared game holds 14 sprite palettes TOTAL. Say so
       at bake time, because it decides where this asset should live. */
    if (j.arena) {
      const a = j.arena, m = $s('spr-arena');
      m.style.display = '';
      m.textContent = a.left > 3
        ? 'Shared game: ' + a.left + ' of ' + a.cap + ' sprite palettes left. ' +
          'In your own copy you have all ' + a.cap + '.'
        : 'Shared game: only ' + a.left + ' of ' + a.cap + ' sprite palettes left — ' +
          'the console holds no more. Save this to your own copy instead, where ' +
          'the whole budget is yours.';
    }
    $s('spr-msg').textContent = 'baked \u2014 now try it in the map (step 6)';
  });

  function showSheetChoice(j) {
    const box = $s('spr-sheet');
    box.textContent = '';
    if (!j.sheet_warning) { box.style.display = 'none'; return; }
    box.style.display = '';
    const p = document.createElement('div');
    p.textContent = j.sheet_warning;
    box.appendChild(p);
    for (let i = 1; i <= j.marks; i++) {
      const b = document.createElement('button');
      b.textContent = 'bake mark ' + i;
      b.title = 'Re-bake using ONLY mark ' + i + ' (1 is the biggest). Upload the '
              + 'same image again under another name for the others.';
      b.onclick = () => { sprMark = i; $s('spr-bake').click(); };
      box.appendChild(b);
    }
  }
  $s('spr-try').addEventListener('click', () => {
    if (!bundle) return;
    /* Session-local registration: palette button, grid glyph, walkthrough
     * billboard. The .map will reference the kind by ID — it builds only
     * after the sprite PR merges, and lint says so. */
    if (!ME.reg.decals.kinds.find(k => k.id === bundle.id)) {
      const ent = { id: bundle.id, kind: bundle.kind, z: bundle.z,
        glyph: '⧉', color: '#b8b0a4',
        label: bundle.id + ' (community ' +
               (bundle.mount === 'wall' ? 'wall decal' : 'standee') + ')' };
      if (bundle.mount !== 'wall') ent.standalone = true;
      ME.reg.decals.kinds.push(ent);
    }
    const A = window.ME.assets;
    if (A && !A.sprites[bundle.id]) {
      /* The sprite's own palette isn't in the exported CRAM yet (it ships
       * with the PR), so patch its 7 entries into the session palette at
       * the allocated arena base, then index texels against them. */
      for (let i = 0; i < bundle.pal8.length; i++)
        A.palette[bundle.base + 1 + i] = bundle.pal8[i];
      const px = new Array(bundle.w * bundle.h);
      for (let i = 0; i < bundle.texels.length; i++)
        px[i] = bundle.texels[i] === 0 ? -1 : bundle.base + bundle.texels[i];
      A.sprites[bundle.id] = { w: bundle.w, h: bundle.h, px,
        world_h: bundle.world_h, world_hw: bundle.world_hw,
        wall: bundle.mount === 'wall' };
    }
    ME.layer = 'decals'; ME.decalKind = bundle.id;
    buildPalette();
    status(bundle.mount === 'wall'
      ? 'wall decal armed — click a wall EDGE to mount it (ships after the PR merges)'
      : 'standee armed — click a cell to place it (ships after the PR merges)');
  });
  $s('spr-download').addEventListener('click', () => {
    if (!bundle) return;
    const dl = (name, text) => {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
      a.download = name; a.click(); URL.revokeObjectURL(a.href);
    };
    dl('spr_' + bundle.id + '_tex.h', bundle.tex_h);
    if (bundle.tex_h_hi) dl('spr_' + bundle.id + '_tex_hi.h', bundle.tex_h_hi);
    dl('registry.json', bundle.registry);
    status('bundle downloaded — see SPRITES.md for the PR steps');
  });
  $s('spr-fork').addEventListener('click', async () => {
    if (!bundle) return;
    $s('spr-msg').textContent = 'saving to your copy…';
    const r = await fetch('/fork/save_sprite', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: bundle.id, tex_h: bundle.tex_h,
                             tex_h_hi: bundle.tex_h_hi || '',
                             registry: bundle.registry }) });
    const j = await r.json().catch(() => ({}));
    if (j.ok) {
      $s('spr-msg').innerHTML = 'saved to <a href="' + j.repo_url +
        '" target="_blank">' + j.fork + ' ↗</a> — your next build has it';
    } else if (r.status === 401) {
      $s('spr-msg').textContent = 'sign in with GitHub (top right) first';
    } else {
      $s('spr-msg').textContent = (j.errors || [j.error || 'failed']).join('; ');
    }
  });
  $s('spr-submit').addEventListener('click', async () => {
    if (!bundle) return;
    $s('spr-msg').textContent = 'opening PR…';
    const r = await fetch('/submit_sprite_pr', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: bundle.id, tex_h: bundle.tex_h,
                             tex_h_hi: bundle.tex_h_hi || '',
                             registry: bundle.registry }) });
    const j = await r.json();
    if (j.ok) {
      $s('spr-msg').innerHTML = '<a href="' + j.pr_url + '" target="_blank">PR #'
        + j.pr_number + ' opened ↗</a>';
    } else if (r.status === 401) {
      /* Actionable, not a dead end: the sign-in is one click from here. */
      $s('spr-msg').textContent = 'this opens a PR under your GitHub account — ';
      const b = document.createElement('button');
      b.className = 'status-act'; b.textContent = 'Sign in with GitHub';
      b.title = 'Or use ⬇ Bundle to download the files and PR them yourself.';
      b.onclick = goSignIn;
      $s('spr-msg').append(b);
    } else {
      $s('spr-msg').textContent = (j.errors || [j.error || 'failed']).join('; ');
    }
  });
})();
