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
  if (add && i < 0) ME.model.lights.push({ cx, cy });
  else if (!add && i >= 0) ME.model.lights.splice(i, 1);
}
function seedLights() {                // the engine's auto-grid as a starting point
  const m = ME.model; m.lights = [];
  for (let my = 1; my < m.h - 1; my += 2)
    for (let mx = 1; mx < m.w - 1; mx += 2)
      if (gridVal(mx, my) === 0) m.lights.push({ cx: mx, cy: my });
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
  if (ME.model.lights) {                          // ceiling light fixtures
    ctx.fillStyle = '#fff7d0';
    for (const l of ME.model.lights) ctx.fillRect(l.cx * cs + cs * 0.28, l.cy * cs + cs * 0.28, cs * 0.44, cs * 0.44);
  }
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
  const g = d.kind === 'door' ? 'D' : d.kind === 'neanderthal' ? 'N' : '⊙';
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
      case 'crawl':
        right ? deleteCrawlAt(cx, cy) : clickCrawl(cx, cy); break;
      case 'lights':
        painting = true; lightAdd = !right; setLight(cx, cy, lightAdd); break;
    }
    draw();
  };
  canvas.onmousemove = e => {
    const { cx, cy } = evCell(e);
    $('#coords').textContent = cx + ',' + cy;
    if (!painting) return;
    if (ME.layer === 'grid') { setGridVal(cx, cy, paintVal); draw(); }
    else if (ME.layer === 'lights') { setLight(cx, cy, lightAdd); draw(); }
  };
  window.addEventListener('mouseup', () => { painting = false; });
}
function placeDecal(wx, wy) {
  const kind = ME.reg.decals.kinds.find(k => k.id === ME.decalKind);
  if (kind && kind.standalone) {            // free-standing billboard (neanderthal): no wall snap
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
function clickPartition(wx, wy) {
  const px = Math.round(wx), py = Math.round(wy);
  if (!ME.partPending) { ME.partPending = { x: px, y: py }; return; }
  const a = ME.partPending;
  if (a.x !== px || a.y !== py) {
    ME.model.partitions.push({
      x1: a.x, y1: a.y, x2: px, y2: py,
      style: ME.partStyle, height: ME.partHeight, crawl: ME.partCrawl
    });
    ME.partPending = { x: px, y: py };   // CHAIN: keep going for connected walls
  }
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
  if (run) {
    ME.model.crawls.push(run);
    for (const [x, y] of runCells(run)) setGridVal(x, y, 0);   // a crawl tunnel is open floor
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
  const layers = [['grid', 'Grid'], ['crawl', 'Crawlspace'], ['lights', 'Lights'],
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
    p.appendChild(choiceRow('Style', Object.keys(ME.reg.partition.style), () => ME.partStyle, v => ME.partStyle = v));
    p.appendChild(choiceRow('Height', Object.keys(ME.reg.partition.height), () => ME.partHeight, v => ME.partHeight = v));
    p.appendChild(choiceRow('Crawl-under', Object.keys(ME.reg.partition.crawl), () => ME.partCrawl, v => ME.partCrawl = v));
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
function syncName() {
  const name = ($('#map-name').value.trim() || ME.model.name || 'untitled');
  ME.model.name = name.toUpperCase().slice(0, 16);
  return name.toLowerCase().replace(/[^a-z0-9_-]/g, '') || 'untitled';
}
async function refreshList() {
  const r = await jget('/maps'); const sel = $('#map-list');
  sel.innerHTML = '<option value="">—</option>';
  for (const m of r.maps) {                       // {name, role, folder, protected}
    const o = document.createElement('option');
    o.value = m.name;
    o.textContent = (m.protected ? '🔒 ' : '') + m.name + '  (' + m.role + ')';
    sel.appendChild(o);
  }
}
async function doNew(size) {
  const r = await jget('/new?w=' + size + '&h=' + size);
  ME.model = r.model; ME.name = null; $('#map-name').value = ME.model.name;
  fitCell(); status('new ' + size + '×' + size); buildPalette(); draw(); saveWip();
}
async function doLoad(name) {
  const r = await jget('/maps/' + name);
  if (r.error) { status('load error: ' + r.error); return; }
  ME.model = r.model; ME.name = name; $('#map-name').value = ME.model.name;
  $('#map-list').value = name;
  const tag = ME.model.protected || (ME.model.role && ME.model.role !== 'community')
    ? '  — protected: edits Export as your own community copy' : '';
  fitCell(); status('loaded ' + name + tag); buildPalette(); draw(); saveWip();
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

/* Import = open a .map a user picked off their disk (parsed by the shared
   Python module so the editor never disagrees with the build). */
function doImport(file) {
  const reader = new FileReader();
  reader.onload = async () => {
    const r = await fetch('/parse', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: reader.result });
    const j = await r.json();
    if (j.error) { status('import error: ' + j.error); return; }
    ME.model = j.model; ME.name = null; $('#map-name').value = ME.model.name;
    fitCell(); status('imported ' + (file.name || 'map')); buildPalette(); draw(); saveWip();
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
function loadWip() {
  try { const s = localStorage.getItem(WIP_KEY); return s ? JSON.parse(s) : null; } catch (e) { return null; }
}

/* ---------- init ---------- */
async function init() {
  ME.reg = await jget('/registry');
  ME.assets = await jget('/assets');   // real ROM palette + base indices (for the Walk preview)
  for (const c of ME.reg.cells.palette) {
    ME.glyphForVal[c.value] = c.glyph; ME.colorForVal[c.value] = c.color;
  }
  buildLayers(); buildPalette(); wireFileBar(); wireCanvas();
  try {                                            // hosted editor is read-only: no Save-to-repo
    const cfg = await jget('/config');
    if (cfg.readonly) $('#btn-save').style.display = 'none';
  } catch (e) {}
  await refreshList();
  const wip = loadWip();
  if (wip && wip.model) {
    ME.model = wip.model; ME.name = wip.name; $('#map-name').value = ME.model.name;
    fitCell(); status('restored your in-progress map'); buildPalette(); draw();
  } else {
    await doNew(16);
  }
  setInterval(saveWip, 4000);                      // periodic safety net while editing
}
init();
