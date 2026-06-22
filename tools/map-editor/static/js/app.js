'use strict';
/* Backrooms 32X map editor. Canvas grid + vector overlays, palette driven by
 * the same registry.json the ROM build uses, load/save .map via the backend
 * (which round-trips through the shared tools/mapfmt.py). */

const ME = {
  reg: null, model: null, name: null,
  layer: 'grid',
  brush: 1,                  // grid cell value to paint
  decalKind: 'outlet',
  partStyle: 'chevron', partHeight: 'full', partCrawl: 'no',
  partPending: null, crawlPending: null,
  cell: 22,
  glyphForVal: {}, colorForVal: {},
};

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
function fitCell() {
  const n = Math.max(ME.model.w, ME.model.h);
  ME.cell = Math.max(10, Math.floor(640 / n));
}

/* ---------- rendering ---------- */
function draw() {
  if (!ME.model) return;
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
    ctx.setLineDash(p.height === 'low' ? [6, 4] : []);
    ctx.beginPath(); ctx.moveTo(p.x1 * cs, p.y1 * cs); ctx.lineTo(p.x2 * cs, p.y2 * cs); ctx.stroke();
    ctx.setLineDash([]);
    dot(p.x1, p.y1, '#000'); dot(p.x2, p.y2, '#000');
  }

  for (const d of ME.model.decals) drawDecal(d);
  drawSpawn(ME.model.spawn);

  if (ME.partPending) dot(ME.partPending.x, ME.partPending.y, '#fff');
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
  ctx.fillText(d.kind === 'door' ? 'D' : '⊙', d.x * cs, d.y * cs);
}
function drawSpawn(s) {
  const cs = ME.cell, x = s.x * cs, y = s.y * cs;
  ctx.fillStyle = '#39d353'; ctx.beginPath(); ctx.arc(x, y, cs * 0.3, 0, 7); ctx.fill();
  const dir = { N: [0, -1], S: [0, 1], E: [1, 0], W: [-1, 0] }[s.facing] || [0, -1];
  ctx.strokeStyle = '#000'; ctx.lineWidth = 2;
  ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + dir[0] * cs * 0.5, y + dir[1] * cs * 0.5); ctx.stroke();
}

/* ---------- input ---------- */
let painting = false, paintVal = 0;
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
        right ? deleteNearest(ME.model.decals, d => Math.hypot(d.x - wx, d.y - wy)) : placeDecal(wx, wy); break;
      case 'partition':
        right ? deleteNearest(ME.model.partitions, p => distSeg(wx, wy, p)) : clickPartition(wx, wy); break;
      case 'crawl':
        right ? deleteCrawlAt(cx, cy) : clickCrawl(cx, cy); break;
    }
    draw();
  };
  canvas.onmousemove = e => {
    const { cx, cy } = evCell(e);
    $('#coords').textContent = cx + ',' + cy;
    if (painting && ME.layer === 'grid') { setGridVal(cx, cy, paintVal); draw(); }
  };
  window.addEventListener('mouseup', () => { painting = false; });
}
function placeDecal(wx, wy) {
  const cx = Math.floor(wx), cy = Math.floor(wy), fx = wx - cx, fy = wy - cy;
  const d = { N: fy, S: 1 - fy, W: fx, E: 1 - fx };
  let face = 'N', best = 9;
  for (const k in d) if (d[k] < best) { best = d[k]; face = k; }
  const pos = {
    N: [cx + 0.5, cy], S: [cx + 0.5, cy + 1], W: [cx, cy + 0.5], E: [cx + 1, cy + 0.5]
  }[face];
  ME.model.decals.push({ kind: ME.decalKind, x: pos[0], y: pos[1], face });
}
function clickPartition(wx, wy) {
  const px = Math.round(wx), py = Math.round(wy);
  if (!ME.partPending) { ME.partPending = { x: px, y: py }; return; }
  const a = ME.partPending;
  if (a.x !== px || a.y !== py)
    ME.model.partitions.push({
      x1: a.x, y1: a.y, x2: px, y2: py,
      style: ME.partStyle, height: ME.partHeight, crawl: ME.partCrawl
    });
  ME.partPending = null;
}
function clickCrawl(cx, cy) {
  if (!ME.crawlPending) { ME.crawlPending = { cx, cy }; return; }
  const s = ME.crawlPending;
  if (s.cy === cy) {
    const a = Math.min(s.cx, cx), b = Math.max(s.cx, cx);
    ME.model.crawls.push({ cx: a, cy, dir: 'E', len: b - a + 1 });
  } else if (s.cx === cx) {
    const a = Math.min(s.cy, cy), b = Math.max(s.cy, cy);
    ME.model.crawls.push({ cx, cy: a, dir: 'S', len: b - a + 1 });
  }
  ME.crawlPending = null;
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
function distSeg(px, py, p) {                 /* point-to-segment distance */
  const vx = p.x2 - p.x1, vy = p.y2 - p.y1, wx = px - p.x1, wy = py - p.y1;
  const L = vx * vx + vy * vy || 1e-6;
  let t = (wx * vx + wy * vy) / L; t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - (p.x1 + t * vx), py - (p.y1 + t * vy));
}

/* ---------- sidebar ---------- */
function buildLayers() {
  const layers = [['grid', 'Grid'], ['crawl', 'Crawlspace'],
    ['partition', 'Partitions'], ['decal', 'Decals'], ['spawn', 'Spawn']];
  const c = $('#layers'); c.innerHTML = '';
  for (const [id, label] of layers) {
    const b = document.createElement('button');
    b.textContent = label; b.dataset.layer = id;
    b.onclick = () => {
      ME.layer = id; ME.partPending = ME.crawlPending = null;
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
  } else if (ME.layer === 'partition') {
    t.textContent = 'Partition';
    p.appendChild(choiceRow('Style', Object.keys(ME.reg.partition.style), () => ME.partStyle, v => ME.partStyle = v));
    p.appendChild(choiceRow('Height', Object.keys(ME.reg.partition.height), () => ME.partHeight, v => ME.partHeight = v));
    p.appendChild(choiceRow('Crawl-under', Object.keys(ME.reg.partition.crawl), () => ME.partCrawl, v => ME.partCrawl = v));
  } else if (ME.layer === 'spawn') {
    t.textContent = 'Spawn facing';
    for (const f of ['N', 'E', 'S', 'W'])
      p.appendChild(paletteBtn(f, null, ME.model && ME.model.spawn.facing === f, () => ME.model.spawn.facing = f));
  } else {
    t.textContent = 'Crawlspace';
    const n = document.createElement('p');
    n.style.color = 'var(--ink)';
    n.textContent = 'Click a start cell, then an aligned end cell (same row or column) to mark a low-ceiling run.';
    p.appendChild(n);
  }
}

/* ---------- file bar ---------- */
async function refreshList() {
  const r = await jget('/maps'); const sel = $('#map-list');
  sel.innerHTML = '<option value="">—</option>';
  for (const n of r.maps) {
    const o = document.createElement('option'); o.value = n; o.textContent = n; sel.appendChild(o);
  }
}
async function doNew(size) {
  const r = await jget('/new?w=' + size + '&h=' + size);
  ME.model = r.model; ME.name = null; $('#map-name').value = ME.model.name;
  fitCell(); status('new ' + size + '×' + size); buildPalette(); draw();
}
async function doLoad(name) {
  const r = await jget('/maps/' + name);
  if (r.error) { status('load error: ' + r.error); return; }
  ME.model = r.model; ME.name = name; $('#map-name').value = ME.model.name;
  $('#map-list').value = name; fitCell(); status('loaded ' + name); buildPalette(); draw();
}
async function doSave() {
  let name = ($('#map-name').value.trim() || ME.model.name || 'untitled');
  ME.model.name = name.toUpperCase().slice(0, 16);
  const r = await jpost('/maps/' + name.toLowerCase().replace(/[^a-z0-9_-]/g, ''), ME.model);
  if (r.error) { status('save error: ' + r.error); return; }
  ME.name = r.name; status('saved ' + r.name + '.map  (rebuild the ROM to compile it in)');
  await refreshList(); $('#map-list').value = r.name;
}
function wireFileBar() {
  $('#btn-new').onclick = () => doNew(parseInt($('#new-size').value, 10));
  $('#btn-save').onclick = doSave;
  $('#btn-reload').onclick = () => { if (ME.name) doLoad(ME.name); };
  $('#map-list').onchange = e => { if (e.target.value) doLoad(e.target.value); };
}

/* ---------- init ---------- */
async function init() {
  ME.reg = await jget('/registry');
  for (const c of ME.reg.cells.palette) {
    ME.glyphForVal[c.value] = c.glyph; ME.colorForVal[c.value] = c.color;
  }
  buildLayers(); buildPalette(); wireFileBar(); wireCanvas();
  await refreshList();
  await doNew(16);
}
init();
