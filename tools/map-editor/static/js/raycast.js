'use strict';
/* First-person raycaster preview for the map editor. Reads the SAME model the
 * editor holds (window.ME.model) AND the real ROM palette (window.ME.assets,
 * exported from raycast.c's build_palette), so the preview renders in the
 * game's actual colors. Floor/ceiling are cast per-pixel which unlocks the
 * ceiling tile grid, carpet stain hash, the cell_light "pools of light" and
 * crawlspace cues — all ported from the engine. 320x200, scaled 2x. */
window.RC = (function () {
  const W = 320, H = 200, MID = H / 2;
  const FOG_DIST = 10;          // cells to full fog (engine MAX_VIEW_DIST ~10)
  const STEPCAP = 96;
  let canvas, ctx, img, raf = null, keys = {}, last = 0;
  let px, py, pa;               // player pos (cells) + angle (radians)
  let eyeH = 0.5;               // eye height 0..1; crouches in crawlspaces
  let lightGrid = null, lowGrid = null, fixtureGrid = null, hasLow = false;
  let darkGrid = null;
  let procOut = [];             // outlets the engine would auto-place (place_outlets)

  function A() { return window.ME.assets; }
  function facingRad(facing) { return (window.ME.reg.facing[facing] ?? 192) / 256 * 2 * Math.PI; }
  function cellVal(cx, cy) {
    const m = window.ME.model, g = m.grid;
    if (cx < 0 || cy < 0 || cx >= m.w || cy >= m.h) return 1;
    if (cy >= g.length || cx >= g[cy].length) return 1;
    return window.ME.reg.cells.glyphs[g[cy][cx]] ?? 1;
  }
  function runCells(c) {
    const [dx, dy] = window.ME.reg.crawl.dir[c.dir]; const out = [];
    for (let i = 0; i < c.len; i++) out.push([c.cx + dx * i, c.cy + dy * i]);
    return out;
  }
  function lit(cx, cy) { return (lightGrid && cy >= 0 && cx >= 0 && cy < lightGrid.length && cx < lightGrid[0].length) ? lightGrid[cy][cx] : 0; }
  function low(cx, cy) { return (lowGrid && cy >= 0 && cx >= 0 && cy < lowGrid.length && cx < lowGrid[0].length) ? lowGrid[cy][cx] : 0; }
  /* DARK ROOM (engine: cell_light bit 7). No fixtures, everything toward fog. */
  function dark(cx, cy) { return (darkGrid && cy >= 0 && cx >= 0 && cy < darkGrid.length && cx < darkGrid[0].length) ? darkGrid[cy][cx] : 0; }
  const DARK_ROOM_SHADE = 6;   /* == DARK_ROOM_SHADE in raycast.c */

  /* port of init_lights: auto-grid fixtures every 2 cells on open non-crawl
   * cells, then the 3x3 cell_light boost (centre +2, neighbours +1, cap 3). */
  function computeLighting() {
    const m = window.ME.model;
    lightGrid = Array.from({ length: m.h }, () => new Int8Array(m.w));
    lowGrid = Array.from({ length: m.h }, () => new Uint8Array(m.w));
    darkGrid = Array.from({ length: m.h }, () => new Uint8Array(m.w));
    for (const d of (m.dark || []))
      for (let y = Math.max(0, d.y0); y <= Math.min(m.h - 1, d.y1); y++)
        for (let x = Math.max(0, d.x0); x <= Math.min(m.w - 1, d.x1); x++) darkGrid[y][x] = 1;
    fixtureGrid = Array.from({ length: m.h }, () => new Uint8Array(m.w));
    hasLow = false;
    for (const c of m.crawls) for (const [x, y] of runCells(c))
      if (y >= 0 && y < m.h && x >= 0 && x < m.w) { lowGrid[y][x] = 1; hasLow = true; }
    // fixtures: the authored Lights layer if any, else the engine's auto-grid.
    const pts = [];
    if (m.lights && m.lights.length) {
      for (const l of m.lights)
        if (l.cx >= 0 && l.cy >= 0 && l.cx < m.w && l.cy < m.h && !darkGrid[l.cy][l.cx])
          pts.push([l.cx, l.cy]);
    } else {
      for (let my = 1; my < m.h - 1; my += 2)
        for (let mx = 1; mx < m.w - 1; mx += 2)
          if (cellVal(mx, my) === 0 && !lowGrid[my][mx] && !darkGrid[my][mx]) pts.push([mx, my]);
    }
    const cap = A().bases.LIGHT_BOOST_MAX ?? 3;
    for (const [lx, ly] of pts) {
      fixtureGrid[ly][lx] = 1;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        const x = lx + dx, y = ly + dy;
        if (x < 0 || y < 0 || x >= m.w || y >= m.h) continue;
        let v = lightGrid[y][x] + ((dx === 0 && dy === 0) ? 2 : 1);
        lightGrid[y][x] = v > cap ? cap : v;
      }
    }
  }
  function fixture(cx, cy) {
    return fixtureGrid && cy >= 0 && cx >= 0 && cy < fixtureGrid.length &&
           cx < fixtureGrid[0].length && fixtureGrid[cy][cx];
  }

  /* Port of raycast_place_outlets: explicit decals count toward the target;
   * place one outlet every (faces/remaining)-th visible wall face. Deterministic
   * -> matches the ROM. */
  function computeOutlets(m) {
    const target = (m.options && m.options.place_outlets) | 0;
    let num = m.decals.length;
    if (target <= num) return [];
    const w = m.w, h = m.h;
    let count = 0;
    for (let y = 1; y < h - 1; y++) for (let x = 1; x < w - 1; x++) {
      if (cellVal(x, y) === 0) continue;
      if (cellVal(x - 1, y) === 0 || cellVal(x + 1, y) === 0 ||
          cellVal(x, y - 1) === 0 || cellVal(x, y + 1) === 0) count++;
    }
    if (count === 0) return [];
    let stride = Math.floor(count / (target - num)); if (stride < 1) stride = 1;
    const out = []; let seen = 0;
    for (let y = 1; y < h - 1 && num < target; y++) for (let x = 1; x < w - 1 && num < target; x++) {
      if (cellVal(x, y) === 0) continue;
      let face, ox, oy; const cx = x + 0.5, cy = y + 0.5;
      if (cellVal(x - 1, y) === 0) { face = 'W'; ox = x;     oy = cy; }
      else if (cellVal(x + 1, y) === 0) { face = 'W'; ox = x + 1; oy = cy; }
      else if (cellVal(x, y - 1) === 0) { face = 'N'; ox = cx;    oy = y; }
      else if (cellVal(x, y + 1) === 0) { face = 'N'; ox = cx;    oy = y + 1; }
      else continue;
      if ((seen++ % stride) !== 0) continue;
      out.push({ kind: 'outlet', x: ox, y: oy, z: 0.20, face });
      num++;
    }
    return out;
  }

  function start() {
    if (!window.ME || !window.ME.model || !window.ME.assets) return;
    const m = window.ME.model;
    px = m.spawn.x; py = m.spawn.y; pa = facingRad(m.spawn.facing);
    computeLighting();
    procOut = computeOutlets(m);
    document.getElementById('preview').style.display = 'flex';
    canvas = document.getElementById('pv-canvas');
    canvas.width = W; canvas.height = H;
    ctx = canvas.getContext('2d');
    img = ctx.createImageData(W, H);
    window.addEventListener('keydown', onKey);
    window.addEventListener('keyup', onKeyUp);
    last = performance.now();
    raf = requestAnimationFrame(loop);
  }
  function stop() {
    if (raf) cancelAnimationFrame(raf);
    raf = null; keys = {};
    window.removeEventListener('keydown', onKey);
    window.removeEventListener('keyup', onKeyUp);
    const pv = document.getElementById('preview');
    if (pv) pv.style.display = 'none';
  }
  function onKey(e) {
    const k = e.key.toLowerCase(); keys[k] = true;
    if (k === 'escape') { stop(); return; }
    if (k.startsWith('arrow') || k === ' ') e.preventDefault();
  }
  function onKeyUp(e) { keys[e.key.toLowerCase()] = false; }

  function distToSeg(x, y, x1, y1, x2, y2) {
    const vx = x2 - x1, vy = y2 - y1, wx = x - x1, wy = y - y1;
    const L = vx * vx + vy * vy || 1e-6;
    let t = (wx * vx + wy * vy) / L; t = t < 0 ? 0 : t > 1 ? 1 : t;
    return Math.hypot(x - (x1 + t * vx), y - (y1 + t * vy));
  }
  function partBlocked(x, y) {           // collide against the partition slabs
    const r = PT_HALF + 0.25;            // slab half-thickness + player radius (ROM feel)
    for (const p of window.ME.model.partitions)
      if (distToSeg(x, y, p.x1, p.y1, p.x2, p.y2) < r) return true;
    return false;
  }
  function move(dt) {
    const rot = 2.6 * dt, spd = 3.2 * dt;
    if (keys['arrowleft'] || keys['q']) pa -= rot;
    if (keys['arrowright'] || keys['e']) pa += rot;
    const dx = Math.cos(pa), dy = Math.sin(pa);
    let nx = px, ny = py;
    if (keys['w'] || keys['arrowup']) { nx += dx * spd; ny += dy * spd; }
    if (keys['s'] || keys['arrowdown']) { nx -= dx * spd; ny -= dy * spd; }
    if (keys['a']) { nx += dy * spd; ny -= dx * spd; }
    if (keys['d']) { nx -= dy * spd; ny += dx * spd; }
    const pad = 0.12;
    if (cellVal(Math.floor(nx + Math.sign(nx - px) * pad), Math.floor(py)) === 0 && !partBlocked(nx, py)) px = nx;
    if (cellVal(Math.floor(px), Math.floor(ny + Math.sign(ny - py) * pad)) === 0 && !partBlocked(px, ny)) py = ny;
  }

  function raySeg(ox, oy, rx, ry, ax, ay, bx, by) {
    const ex = bx - ax, ey = by - ay, wx = ax - ox, wy = ay - oy;
    const det = ex * ry - ey * rx;
    if (Math.abs(det) < 1e-9) return null;
    const t = (ex * wy - ey * wx) / det, u = (rx * wy - ry * wx) / det;
    if (t <= 0 || u < 0 || u > 1) return null;
    return { dist: t, side: Math.abs(ex) > Math.abs(ey) ? 1 : 0, u: u };
  }
  const PT_HALF = 0.05;   // slab half-thickness (cells) -> 0.1 total, matches the ROM (PART_HALF_THICK)
  /* Flush shift (matches the engine): a run collinear with a wall face shifts
   * so its face lands ON the line instead of centered — no seam jog at the
   * joint. Returns the signed centerline offset along the perpendicular:
   * -PT_HALF (slab sits on the + side, face on line), +PT_HALF (- side), or
   * 0 (centered). Scans the run's cells plus one continuation cell past each
   * end; a wall on exactly one side votes flush, both sides = tee = centered. */
  function slabShift(p) {
    const vertical = Math.round(p.x1) === Math.round(p.x2);
    const line = vertical ? Math.round(p.x1) : Math.round(p.y1);
    const a = Math.min(vertical ? p.y1 : p.x1, vertical ? p.y2 : p.x2);
    const b = Math.max(vertical ? p.y1 : p.x1, vertical ? p.y2 : p.x2);
    let neg = false, pos = false;   // wall on the - / + side of the line
    for (let t = Math.floor(a) - 1; t <= Math.ceil(b); t++) {
      const cLo = vertical ? cellVal(line - 1, t) : cellVal(t, line - 1);
      const cHi = vertical ? cellVal(line, t)     : cellVal(t, line);
      if (cLo && cHi) continue;     // tee / wall both sides
      if (cLo) neg = true;
      if (cHi) pos = true;
    }
    if (neg && !pos) return  PT_HALF;   // wall on - side -> slab on + side, face on line
    if (pos && !neg) return -PT_HALF;
    return 0;
  }
  /* ALL partitions the ray crosses within maxd (nearest face per partition),
   * sorted FAR -> NEAR for painter's-order overlay. Returning only the nearest
   * (as before) meant a half-height counter erased every partition behind it;
   * the engine keeps and layers the crossings, so we do too. */
  function partitionHits(rx, ry, maxd) {
    const parts = window.ME.model.partitions; const out = [];
    for (const p of parts) {
      const dx = p.x2 - p.x1, dy = p.y2 - p.y1, len = Math.hypot(dx, dy) || 1;
      const ux = dx / len, uy = dy / len;              // along the run
      const px0 = -uy, py0 = ux;                       // unit perpendicular
      const sh = (p._shift !== undefined ? p._shift : (p._shift = slabShift(p)));
      const cxL = p.x1 + px0 * sh, cyL = p.y1 + py0 * sh;   // shifted centerline
      const cx1 = p.x2 + px0 * sh, cy1 = p.y2 + py0 * sh;
      const nx = px0 * PT_HALF, ny = py0 * PT_HALF;    // slab half-thickness offset
      const ax = cxL + nx, ay = cyL + ny, bx = cx1 + nx, by = cy1 + ny;   // thin box:
      const cx2 = cx1 - nx, cy2 = cy1 - ny, ex = cxL - nx, ey = cyL - ny; // 2 faces + 2 caps
      const edges = [[ax, ay, bx, by], [bx, by, cx2, cy2], [cx2, cy2, ex, ey], [ex, ey, ax, ay]];
      let best = null;                                 // nearest face of THIS partition
      for (const e of edges) {
        const h = raySeg(px, py, rx, ry, e[0], e[1], e[2], e[3]);
        if (h && h.dist < maxd && (!best || h.dist < best.dist)) {
          const hx = px + rx * h.dist, hy = py + ry * h.dist;
          const u = ((hx - p.x1) * dx + (hy - p.y1) * dy) / (len * len);   // along the divider
          const hv = (window.ME.reg.partition.height[p.height] | 0);
          best = { dist: h.dist, side: h.side, style: p.style, height: p.height,
                   hfrac: hv > 0 ? hv / 256 : 1,
                   u: u, seg: p, len: len, shift: sh, ux: ux, uy: uy };
        }
      }
      if (best) out.push(best);
    }
    out.sort((a, b) => b.dist - a.dist);               // far -> near (painter's order)
    return out;
  }
  /* Countertop top: the flat horizontal wood plane capping a half/low
   * partition, drawn the crawl-ceiling way — walk screen rows up from the
   * band's top edge, map each to a distance on the height plane, and lay
   * wood while the sampled point is inside the slab's footprint (thickness
   * band x run extent). Mirrors the ROM's sampled-plane countertop. */
  function drawCountertop(data, x, ph, hfrac, bandTopY, dirRx, dirRy) {
    const B = A().bases, P = A().palette;
    const woodBase = B.WOODTOP_BASE; if (woodBase === undefined) return;
    const eh = eyeH; if (hfrac >= eh) return;   // top only visible when it sits BELOW the eye
    const seg = ph.seg, sh = ph.shift, ux = ph.ux, uy = ph.uy;
    const px0 = -uy, py0 = ux;
    const lineX = (seg.x1 + px0 * sh), lineY = (seg.y1 + py0 * sh);   // a point on the centerline
    const ua = 0, ub = ph.len;                                        // run extent (param along u)
    const sl1 = SL() - 1;
    for (let y = bandTopY - 1; y > MID; y--) {
      const d = (eh - hfrac) * H / (y - MID);          // row (below horizon) -> distance on the hfrac plane
      if (d <= 0) break;
      const wx = px + dirRx * d, wy = py + dirRy * d;
      const un = (wx - lineX) * px0 + (wy - lineY) * py0;              // perpendicular offset
      const uu = (wx - seg.x1) * ux + (wy - seg.y1) * uy;             // along the run
      if (un < -PT_HALF || un > PT_HALF || uu < ua || uu > ub) break; // left the footprint
      let s = shadeIdx(d, 0, lit(Math.floor(wx), Math.floor(wy))) >> 1;   // 16 levels -> 8-entry wood ramp
      if (s > 4) s = 4; if (s < 0) s = 0;   // stay in the BROWN range (6-7 are grey), matching the ROM
      const c = P[woodBase + s] || [0, 0, 0];
      const o = (y * W + x) * 4;
      data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
    }
  }
  /* wallpaper texel (0..4 shade offset). Textures are x-major: data[x*h+y]. */
  const TILE = 2;   // texture repeats per cell (tunable feel)
  function texVal(t, tu, tv) {
    let x = tu % t.w; if (x < 0) x += t.w;
    let y = tv % t.h; if (y < 0) y += t.h;
    return t.data[(x | 0) * t.h + (y | 0)];
  }

  const SL = () => A().bases.SHADE_LEVELS ?? 16;
  function shadeIdx(dist, extra, boost) {
    const sl = SL();
    let s = Math.floor(dist / FOG_DIST * (sl - 1)) + (extra | 0) - (boost | 0) * 2;
    return s < 0 ? 0 : (s > sl - 1 ? sl - 1 : s);
  }
  function put(data, o, idx) {
    const c = A().palette[idx] || [0, 0, 0];
    data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
  }

  function render() {
    const data = img.data, B = A().bases;
    /* Recompute flush shifts each frame so wall edits between preview opens
     * are reflected (cheap — a handful of partitions). */
    for (const p of window.ME.model.partitions) p._shift = slabShift(p);
    const dirX = Math.cos(pa), dirY = Math.sin(pa);
    const planeX = -dirY * 0.66, planeY = dirX * 0.66;
    const rdxL = dirX - planeX, rdyL = dirY - planeY;
    const rdxR = dirX + planeX, rdyR = dirY + planeY;

    /* Crouch when standing in a crawlspace cell (forced-crouch, like the ROM):
     * lowers the eye so the slab reads as a low ceiling pressing down. */
    const pcx = Math.floor(px), pcy = Math.floor(py);
    const nearLow = low(pcx, pcy) || low(pcx + 1, pcy) || low(pcx - 1, pcy) ||
                    low(pcx, pcy + 1) || low(pcx, pcy - 1);
    eyeH += ((nearLow ? 0.28 : 0.5) - eyeH) * 0.25;
    const eh = eyeH, sl1 = SL() - 1;
    const CRAWL = (B.CRAWL_CEIL_H || 135) / 256;     // slab height (fraction of wall)
    const slabOn = hasLow && eh < CRAWL;

    /* FLOOR + CEILING — per-pixel cast at eye height eh. Ceiling is a DUAL
     * plane: the low slab (nearer) wins over the full ceiling (farther) in
     * crawlspace cells, so the ceiling actually drops in a crawl tube. */
    for (let y = 0; y < H; y++) {
      if (y === MID) continue;                       // horizon — walls cover it
      const floor = y > MID, p = floor ? (y - MID) : (MID - y);
      let o = y * W * 4;
      if (floor) {
        const rowDist = eh * H / p;
        let wx = px + rowDist * rdxL, wy = py + rowDist * rdyL;
        const sx = rowDist * (rdxR - rdxL) / W, sy = rowDist * (rdyR - rdyL) / W;
        const base = shadeIdx(rowDist, 0, 0);
        for (let x = 0; x < W; x++, wx += sx, wy += sy, o += 4) {
          const cx = Math.floor(wx), cy = Math.floor(wy);
          let sh = base - lit(cx, cy) * 2; if (sh < 0) sh = 0;
          let idx = B.FLOOR_BASE + sh;
          const hx = (Math.floor(wx * 8)) & 0xFF, hy = (Math.floor(wy * 8)) & 0xFF;
          if (((hx * 73 + hy * 31) & 0xF) < 6) idx = B.FLOOR_BASE + Math.min(sl1, sh + 2);
          if (low(cx, cy)) idx = B.FLOOR_BASE + Math.min(sl1, sh + 3);   // dark crawl floor
          if (dark(cx, cy)) idx = B.FLOOR_BASE + Math.min(sl1, sh + DARK_ROOM_SHADE);
          put(data, o, idx);
        }
      } else {
        const fDist = (1 - eh) * H / p;                // full ceiling plane (far)
        let fwx = px + fDist * rdxL, fwy = py + fDist * rdyL;
        const fsx = fDist * (rdxR - rdxL) / W, fsy = fDist * (rdyR - rdyL) / W;
        const fBase = shadeIdx(fDist, 0, 0);
        const sDist = slabOn ? (CRAWL - eh) * H / p : 0;   // slab plane (near)
        let swx = px + sDist * rdxL, swy = py + sDist * rdyL;
        const ssx = sDist * (rdxR - rdxL) / W, ssy = sDist * (rdyR - rdyL) / W;
        const sBase = slabOn ? shadeIdx(sDist, 0, 0) : 0;
        for (let x = 0; x < W; x++, fwx += fsx, fwy += fsy, swx += ssx, swy += ssy, o += 4) {
          if (slabOn && low(Math.floor(swx), Math.floor(swy))) {
            const gx = (swx * 4) % 1, gy = (swy * 4) % 1;
            const grid = (gx >= 0 ? gx : gx + 1) < 0.06 || (gy >= 0 ? gy : gy + 1) < 0.06;
            put(data, o, grid ? B.LOWCEIL_SEAM : B.LOWCEIL_COLOR);   // low slab, gridded
            void sBase;
            continue;
          }
          const cx = Math.floor(fwx), cy = Math.floor(fwy);
          if (fixture(cx, cy)) {                         // bright fluorescent panel
            const fxf = fwx - cx, fyf = fwy - cy;
            if (fxf > 0.2 && fxf < 0.8 && fyf > 0.2 && fyf < 0.8) { put(data, o, B.LIGHT_BASE); continue; }
          }
          let sh = fBase - lit(cx, cy) * 2; if (sh < 0) sh = 0;
          /* DARK ROOM ceiling: the ROM draws its darkness OVER the tile grid
           * (you can't read tile grid in an unlit room), so skip the grid here
           * rather than shading it — same resulting look, and it matches. */
          if (dark(cx, cy)) {
            put(data, o, B.CEIL_BASE + Math.min(sl1, sh + DARK_ROOM_SHADE));
            continue;
          }
          const gx = (fwx * 4) % 1, gy = (fwy * 4) % 1;
          const grid = (gx >= 0 ? gx : gx + 1) < 0.06 || (gy >= 0 ? gy : gy + 1) < 0.06;
          put(data, o, B.CEIL_BASE + (grid ? Math.min(sl1, sh + 3) : sh));
        }
      }
    }

    /* WALLS + PARTITIONS (DDA, overwrites the middle band) */
    const zbuf = new Float32Array(W);
    for (let x = 0; x < W; x++) {
      const cam = 2 * x / W - 1;
      const rx = dirX + planeX * cam, ry = dirY + planeY * cam;
      let mapX = Math.floor(px), mapY = Math.floor(py);
      const ddx = Math.abs(1 / rx), ddy = Math.abs(1 / ry);
      let stepX, stepY, sdX, sdY;
      if (rx < 0) { stepX = -1; sdX = (px - mapX) * ddx; } else { stepX = 1; sdX = (mapX + 1 - px) * ddx; }
      if (ry < 0) { stepY = -1; sdY = (py - mapY) * ddy; } else { stepY = 1; sdY = (mapY + 1 - py) * ddy; }
      let hit = 0, side = 0, val = 1, steps = 0;
      let prevLow = low(mapX, mapY), capDist = -1;   // first non-low -> low boundary
      while (!hit && steps++ < STEPCAP) {
        if (sdX < sdY) { sdX += ddx; mapX += stepX; side = 0; } else { sdY += ddy; mapY += stepY; side = 1; }
        const curLow = low(mapX, mapY);
        if (capDist < 0 && curLow && !prevLow) capDist = (side === 0 ? sdX - ddx : sdY - ddy);
        prevLow = curLow;
        val = cellVal(mapX, mapY); if (val !== 0) hit = 1;
      }
      let dist = side === 0 ? sdX - ddx : sdY - ddy; if (dist < 0.02) dist = 0.02;

      const eh = eyeH, sl1 = SL() - 1, P = A().palette;
      zbuf[x] = dist;                              // depth for sprite z-test

      /* ── BACKGROUND: the DDA wall (or black void), full height. A partial
       * partition is drawn as an OVERLAY on top of this (see-over), matching
       * the engine — so the wall behind a half-height counter still shows
       * above the band instead of blanking to floor/ceiling. */
      {
        const wallBot = MID + eh * H / dist, wallTop = MID - (1 - eh) * H / dist;
        const lineH = wallBot - wallTop;
        const y0 = Math.max(0, Math.ceil(wallTop)), y1 = Math.min(H, Math.ceil(wallBot));
        if (val === 2) {
          /* VOID EXIT: a missing wall. Draw NO wall, so the floor+ceiling cast
           * above show through, running out into the expanse — matches the
           * engine's see-through opening. */
        } else {
          const tex = A().textures.wall, baseIdx = B.WALL_BASE;
          let bgShade = shadeIdx(dist, side === 1 ? 2 : 0, lit(mapX, mapY));
          /* A face seen FROM a dark room: the viewer-side cell is the open one
           * (back-step off the solid wall), same as the engine's model. */
          if (darkGrid) {
            const vx = side === 0 ? mapX - stepX : mapX;
            const vy = side === 1 ? mapY - stepY : mapY;
            if (dark(vx, vy)) bgShade = Math.min(SL() - 1, bgShade + DARK_ROOM_SHADE);
          }
          const wallX = side === 0 ? (py + dist * ry) : (px + dist * rx);
          const tu = Math.floor((((wallX - Math.floor(wallX)) * TILE) % 1) * tex.w);
          for (let y = y0; y < y1; y++) {
            const vf = (y - wallTop) / lineH;
            const tv = Math.floor((((vf * TILE) % 1 + 1) % 1) * tex.h);
            let s = bgShade + texVal(tex, tu, tv); if (s > sl1) s = sl1;
            const c = P[baseIdx + s] || [0, 0, 0];
            const o = (y * W + x) * 4; data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
          }
        }
      }

      /* ── FOREGROUND: every partition slab the ray crosses, drawn FAR->NEAR
       * over the background so a near half-height counter doesn't erase the
       * partitions behind it (their tops still show above its band). */
      const phs = partitionHits(rx, ry, dist);
      for (const ph of phs) {
        const pdist = ph.dist < 0.02 ? 0.02 : ph.dist;
        const tex = ph.style === 'spotted' ? A().textures.partition : A().textures.wall;
        const baseIdx = ph.style === 'spotted' ? B.PARTITION_BASE : B.WALL_BASE;
        const pShade = shadeIdx(pdist, ph.side === 1 ? 2 : 0, 0);
        const segLen = Math.hypot(ph.seg.x2 - ph.seg.x1, ph.seg.y2 - ph.seg.y1) || 1;
        const tu = Math.floor(((ph.u * segLen * TILE) % 1) * tex.w);
        const hfrac = ph.hfrac;                    // low=0.75, half=0.375, full=1
        const pBot = MID + eh * H / pdist, pTop = MID - (1 - eh) * H / pdist;
        const pLineH = pBot - pTop;
        const py0 = Math.max(0, Math.ceil(pBot - pLineH * hfrac)), py1 = Math.min(H, Math.ceil(pBot));
        for (let y = py0; y < py1; y++) {
          const vf = (y - pTop) / pLineH;
          const tv = Math.floor((((vf * TILE) % 1 + 1) % 1) * tex.h);
          let s = pShade + texVal(tex, tu, tv); if (s > sl1) s = sl1;
          const c = P[baseIdx + s] || [0, 0, 0];
          const o = (y * W + x) * 4; data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
        }
        if (hfrac < 1) drawCountertop(data, x, ph, hfrac, py0, rx, ry);
        /* Nearest partition (partial OR full) is the column's closest solid, so
         * it sets the sprite depth — matching the engine (WALL_DIST = fg_t[0]),
         * so the neanderthal behind a counter is occluded, not printed on top. */
        if (pdist < zbuf[x]) zbuf[x] = pdist;
      }

      /* Crawlspace mouth header (lintel): the solid band from the lowered slab
       * up to the full ceiling at the crawl boundary — makes the dropped ceiling
       * visible standing OUTSIDE looking in, not only once you're inside it. */
      if (capDist > 0.02) {
        const CR = (B.CRAWL_CEIL_H || 135) / 256;
        if (eh < CR) {
          const yCe = MID - (1 - eh) * H / capDist, ySl = MID - (CR - eh) * H / capDist;
          const cy0 = Math.max(0, Math.floor(yCe)), cy1 = Math.min(H, Math.ceil(ySl));
          const cc = P[B.WALL_BASE + shadeIdx(capDist, 0, 0)] || [0, 0, 0];
          for (let y = cy0; y < cy1; y++) {
            const o = (y * W + x) * 4;
            data[o] = cc[0]; data[o + 1] = cc[1]; data[o + 2] = cc[2]; data[o + 3] = 255;
          }
        }
      }
    }

    /* WALL DECALS — outlet + exit door, locked FLAT on their wall plane (a
     * wall-aligned textured slice, not a billboard) so they foreshorten with
     * the wall. [sprite, half-width, centre-z, height]. */
    const spr = A().sprites || {};
    for (const dec of window.ME.model.decals) {
      if (dec.kind === 'outlet' && spr.outlet) drawWallDecal(data, zbuf, dec, spr.outlet, 0.031, dec.z != null ? dec.z : 0.20, 0.098);
      else if (dec.kind === 'door' && spr.door) drawWallDecal(data, zbuf, dec, spr.door, 0.24, 0.49, 0.98);
      else if (dec.kind === 'exit_hole') drawExitHole(data, zbuf, dec);
      else if (spr[dec.kind] && spr[dec.kind].wall) {
        /* Community WALL decal (bake_sprite --mount wall): same flat-on-the-
         * wall draw as the outlet, at its registry size + placed height. */
        const cs = spr[dec.kind];
        const kz = ((window.ME.reg.decals.kinds.find(k => k.id === dec.kind) || {}).z);
        drawWallDecal(data, zbuf, dec, cs, cs.world_hw,
                      dec.z != null ? dec.z : (kz != null ? kz : 0.5), cs.world_h);
      }
    }
    if (spr.outlet) for (const o of procOut)   // engine's auto-placed outlets
      drawWallDecal(data, zbuf, o, spr.outlet, 0.031, 0.20, 0.098);
    /* FREE-STANDING billboards, far -> near: the neanderthal standup plus
     * every registry-standalone community standee (bake_sprite.py) — the
     * export carries each one's world dims, so new sprites render at true
     * scale with no edits here. */
    const bills = [];
    for (const dec of window.ME.model.decals) {
      if (dec.kind === 'chair') continue;                 /* live-3D below */
      let sp = null, hw = 0, h = 0;
      if (dec.kind === 'neanderthal' && spr.neander) {
        sp = spr.neander; hw = 0.45, h = 0.90;
      } else if (spr[dec.kind] && spr[dec.kind].world_h && !spr[dec.kind].wall) {
        sp = spr[dec.kind];
        /* The 0.9 is a STANDEE fudge borrowed from the neanderthal: a printed
         * cutout reads slightly smaller than its nominal size. A box model is
         * real geometry with real dimensions, so shrinking it just makes the
         * preview lie about scale -- the desk showed up looking like a side
         * table. registry decals.kinds[].box_model says which is which, so the
         * import tool can set it and this stays data-driven. */
        const kd = (window.ME.reg.decals.kinds || []).find(k => k.id === dec.kind);
        const scale = (kd && kd.box_model) ? 1.0 : 0.9;
        h = sp.world_h * scale;
        /* drawSprite's width argument is the FULL width, not a half-extent --
         * the neanderthal branch above passes 0.45/0.90, which are already full
         * dimensions. Passing world_hw straight through drew every generic
         * sprite at exactly HALF its proper width; the desk showed up looking
         * like a side table. */
        hw = sp.world_hw * 2 * scale;
      }
      if (!sp) continue;
      const ddx = dec.x - px, ddy = dec.y - py;
      bills.push({ d2: ddx * ddx + ddy * ddy, dec, sp, hw, h });
    }
    bills.sort((a, b) => b.d2 - a.d2);
    for (const it of bills)
      drawSprite(data, zbuf, it.dec.x, it.dec.y, it.sp, it.hw, it.h, it.h / 2);

    /* Live-3D chairs — projected geometry, far -> near. */
    const chairs = [];
    for (const dec of window.ME.model.decals)
      if (dec.kind === 'chair') {
        const ddx = dec.x - px, ddy = dec.y - py; chairs.push({ d2: ddx * ddx + ddy * ddy, dec });
      }
    chairs.sort((a, b) => b.d2 - a.d2);
    for (const it of chairs) drawChair3D(data, zbuf, it.dec);

    ctx.putImageData(img, 0, 0);
  }

  /* Wall-aligned decal: a textured slice in the wall plane. The decal is a
   * short segment (along the wall) at the decal point; per column we ray-test
   * it and draw its vertical band [zc-h/2 .. zc+h/2] foreshortened by depth. */
  function drawWallDecal(data, zbuf, dec, sprite, hw, zc, h) {
    let ax, ay, bx, by;
    if (dec.face === 'N' || dec.face === 'S') { ax = dec.x - hw; ay = dec.y; bx = dec.x + hw; by = dec.y; }
    else { ax = dec.x; ay = dec.y - hw; bx = dec.x; by = dec.y + hw; }
    const dirX = Math.cos(pa), dirY = Math.sin(pa);
    const planeX = -dirY * 0.66, planeY = dirX * 0.66;
    const eh = eyeH, P = A().palette;
    for (let col = 0; col < W; col++) {
      const cam = 2 * col / W - 1;
      const hit = raySeg(px, py, dirX + planeX * cam, dirY + planeY * cam, ax, ay, bx, by);
      if (!hit) continue;
      const dist = hit.dist;
      if (dist <= 0.05 || dist > zbuf[col] + 0.2) continue;     // off-wall / behind wall
      const colH = H / dist;
      const top = MID - (zc + h / 2 - eh) * colH, bot = MID - (zc - h / 2 - eh) * colH;
      const lineH = bot - top;
      const tx = (hit.u * sprite.w) | 0;
      if (tx < 0 || tx >= sprite.w) continue;
      for (let y = Math.max(0, Math.floor(top)); y < Math.min(H, Math.ceil(bot)); y++) {
        const ty = ((y - top) / lineH * sprite.h) | 0;
        if (ty < 0 || ty >= sprite.h) continue;
        const idx = sprite.px[ty * sprite.w + tx];
        if (idx < 0) continue;
        const c = P[idx]; if (!c) continue;
        const o = (y * W + col) * 4;
        data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
      }
    }
  }

  /* EXIT HOLE — ports the engine's draw_exit_hole: a carved box in the wall
   * face (sill 100/256, head 212/256, half-width 0.30), interior shaded by
   * the ray's traversal depth with the FILM-REFERENCE asymmetry: the lit
   * side wall holds readable for a cell, the shadow side and the head fall
   * to the murk register fast, the sill stays lit at the lip. Procedural
   * (no sprite) — same bands, same palette ramp as the ROM. */
  function drawExitHole(data, zbuf, dec) {
    const HW = 0.30, Z0 = 100 / 256, Z1 = 212 / 256;
    const ns = dec.face === 'N' || dec.face === 'S';
    let ax, ay, bx, by;
    if (ns) { ax = dec.x - HW; ay = dec.y; bx = dec.x + HW; by = dec.y; }
    else { ax = dec.x; ay = dec.y - HW; bx = dec.x; by = dec.y + HW; }
    /* Interior direction: the solid side of the face plane. face N = seen
     * from the north (smaller y) => interior +y; mirrored for the rest. */
    const dirIn = dec.face === 'N' ? 1 : dec.face === 'S' ? -1
                : dec.face === 'W' ? 1 : -1;
    const dirX = Math.cos(pa), dirY = Math.sin(pa);
    const planeX = -dirY * 0.66, planeY = dirX * 0.66;
    const eh = eyeH, B2 = B, P = A().palette, sl = SL();
    const wallIdx = s => P[B2.WALL_BASE + Math.max(0, Math.min(sl - 1, s))] || [0, 0, 0];
    for (let col = 0; col < W; col++) {
      const cam = 2 * col / W - 1;
      const rdx = dirX + planeX * cam, rdy = dirY + planeY * cam;
      const hit = raySeg(px, py, rdx, rdy, ax, ay, bx, by);
      if (!hit) continue;
      const t = hit.dist;
      if (t <= 0.05 || t > zbuf[col] + 0.2) continue;
      const rdp = ns ? rdy : rdx, rda = ns ? rdx : rdy;
      if (Math.abs(rdp) < 1e-4) continue;
      const off = (hit.u - 0.5) * 2 * HW;
      /* Far plane one cell deeper, clamped to the cavity side. */
      let t2 = t + dirIn / rdp, sideHit = false;
      if (Math.abs(rda) > 1e-3) {
        const edge = rda > 0 ? HW - off : off + HW;
        const ts = t + Math.max(0, edge) / Math.abs(rda);
        if (ts < t2) { t2 = ts; sideHit = true; }
      }
      if (t2 < t) t2 = t;
      const bsh = shadeIdx(t, 0, 0);
      const murk = Math.min(sl - 2, bsh + 8);
      let s0, reach;                          /* asymmetric interior (film ref) */
      if (sideHit && rda < 0) { s0 = bsh + 1; reach = 1.0; }
      else if (sideHit)       { s0 = bsh + 5; reach = 0.33; }
      else                    { s0 = bsh + 2; reach = 0.5; }
      const dIn = Math.max(0, t2 - t);
      const sv = Math.min(murk, s0 + (murk - s0) * Math.min(1, dIn / reach));
      const yAt = (z, d) => MID - (z - eh) * H / d;
      const hn = yAt(Z1, t), hf = yAt(Z1, t2), sn = yAt(Z0, t), sf = yAt(Z0, t2);
      const headLo = Math.min(hn, hf), headHi = Math.max(hn, hf);
      const sillLo = Math.min(sn, sf), sillHi = Math.max(sn, sf);
      const fill = (y0, y1, sAt) => {
        for (let y = Math.max(0, Math.floor(y0)); y < Math.min(H, Math.ceil(y1)); y++) {
          const c = wallIdx(Math.round(sAt((y - y0) / Math.max(1, y1 - y0))));
          const o = (y * W + col) * 4;
          data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
        }
      };
      fill(headLo, headHi, f => bsh + 4 + (sv - bsh - 4) * f);   /* head: shadowed */
      fill(headHi, sillLo, () => sv);                            /* core / side wall */
      fill(sillLo, sillHi, f => sv + (bsh + 1 - sv) * f);        /* sill: lit lip */
    }
  }

  /* sw/sh are FULL world width and height (not half-extents) -- spw/sph below are
 * used as the total on-screen span. */
function drawSprite(data, zbuf, wx, wy, sprite, sw, sh, zc) {
    const dirX = Math.cos(pa), dirY = Math.sin(pa);
    const planeX = -dirY * 0.66, planeY = dirX * 0.66;
    const dx = wx - px, dy = wy - py;
    const invDet = 1 / (planeX * dirY - dirX * planeY);
    const tX = invDet * (dirY * dx - dirX * dy);
    const tY = invDet * (-planeY * dx + planeX * dy);    // forward depth
    if (tY <= 0.1) return;
    const colH = H / tY, sX = (W / 2) * (1 + tX / tY);
    const spw = sw * colH, sph = sh * colH;
    const cYc = MID - (zc - eyeH) * colH;                // billboard vertical centre
    const x0 = Math.floor(sX - spw / 2), x1 = Math.ceil(sX + spw / 2);
    const y0 = Math.floor(cYc - sph / 2), y1 = Math.ceil(cYc + sph / 2);
    const P = A().palette;
    for (let x = x0; x < x1; x++) {
      if (x < 0 || x >= W || tY > zbuf[x] + 0.15) continue;   // off-screen or behind a wall
      const tx = (x - x0) / spw * sprite.w | 0;
      if (tx < 0 || tx >= sprite.w) continue;
      for (let y = Math.max(0, y0); y < Math.min(H, y1); y++) {
        const ty = (y - y0) / sph * sprite.h | 0;
        const idx = sprite.px[ty * sprite.w + tx];
        if (idx < 0) continue;                                 // transparent
        const c = P[idx]; if (!c) continue;
        const o = (y * W + x) * 4;
        data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
      }
    }
  }

  /* Live-3D chair — ports draw_chair_3d: the 7-box ladder-back projected
   * through the camera (not a billboard), backface-culled, shaded from a fixed
   * light, painter-sorted, distance-fogged, wall z-tested. Rides the DOOR_BASE
   * brown ramp (palette index 96 + shade). */
  const CHAIR_H = 0.375, DOOR_BASE = 96;
  const CHAIR_BOXES = [
    [-0.26, 0.42, -0.26,  0.26, 0.48,  0.20],   // seat
    [-0.26, 0.00, -0.26, -0.20, 0.42, -0.20],   // front-L
    [ 0.20, 0.00, -0.26,  0.26, 0.42, -0.20],   // front-R
    [-0.26, 0.00,  0.20, -0.20, 1.00,  0.26],   // post BL
    [ 0.20, 0.00,  0.20,  0.26, 1.00,  0.26],   // post BR
    [-0.26, 0.90,  0.21,  0.26, 1.00,  0.25],   // top rail
    [-0.26, 0.68,  0.215, 0.26, 0.76,  0.245],  // mid slat
  ];
  const CHAIR_FV = [[1,3,7,5],[0,4,6,2],[2,6,7,3],[0,1,5,4],[4,5,7,6],[0,2,3,1]];
  const FACE_RAD = { E: 0, S: Math.PI / 2, W: Math.PI, N: 3 * Math.PI / 2 };

  function drawChair3D(data, zbuf, chair) {
    const fr = FACE_RAD[chair.face] != null ? FACE_RAD[chair.face] : 0;
    const fc = Math.cos(fr), fs = Math.sin(fr);
    const dirX = Math.cos(pa), dirY = Math.sin(pa);
    const planeX = -dirY * 0.66, planeY = dirX * 0.66;
    const det = planeX * dirY - dirX * planeY;
    if (Math.abs(det) < 1e-6) return;
    const invDet = 1 / det, P = A().palette, faces = [];
    for (const b of CHAIR_BOXES) {
      const SX = [], SY = [], DE = [], CL = [];
      for (let v = 0; v < 8; v++) {
        const mx = ((v & 1) ? b[3] : b[0]) * CHAIR_H;
        const my = ((v & 2) ? b[4] : b[1]) * CHAIR_H;
        const mz = ((v & 4) ? b[5] : b[2]) * CHAIR_H;
        const rx = mx * fc + mz * fs, rz = -mx * fs + mz * fc;
        const ddx = chair.x + rx - px, ddy = chair.y + rz - py;
        const d = invDet * (-planeY * ddx + planeX * ddy);
        DE[v] = d;
        if (d < 0.06) { CL[v] = 1; SX[v] = SY[v] = 0; continue; }
        CL[v] = 0;
        const lat = invDet * (dirY * ddx - dirX * ddy);
        SX[v] = (W / 2) * (1 + lat / d);
        SY[v] = MID - (my - eyeH) * H / d;
      }
      for (let f = 0; f < 6; f++) {
        const vi = CHAIR_FV[f];
        if (CL[vi[0]] || CL[vi[1]] || CL[vi[2]] || CL[vi[3]]) continue;
        const ax = SX[vi[1]] - SX[vi[0]], ay = SY[vi[1]] - SY[vi[0]];
        const bx = SX[vi[2]] - SX[vi[0]], by = SY[vi[2]] - SY[vi[0]];
        if (ax * by - ay * bx <= 0) continue;                 // backface
        const d = (DE[vi[0]] + DE[vi[1]] + DE[vi[2]] + DE[vi[3]]) / 4;
        let shade = (f === 2) ? 4 : (f === 3) ? 1 : (f === 1 || f === 5) ? 3 : 2;
        if (d > 2) { shade -= Math.floor((d - 2) * 5 / 4); }   // distance fog
        if (shade < 0) shade = 0; if (shade > 4) shade = 4;
        faces.push({ x: vi.map(i => SX[i]), y: vi.map(i => SY[i]), d, shade });
      }
    }
    faces.sort((a, b) => b.d - a.d);                          // far -> near
    for (const fa of faces) {
      const c = P[DOOR_BASE + fa.shade]; if (!c) continue;
      fillTriZ(data, zbuf, fa.x[0], fa.y[0], fa.x[1], fa.y[1], fa.x[2], fa.y[2], fa.d, c);
      fillTriZ(data, zbuf, fa.x[0], fa.y[0], fa.x[2], fa.y[2], fa.x[3], fa.y[3], fa.d, c);
    }
  }

  function fillTriZ(data, zbuf, x0, y0, x1, y1, x2, y2, depth, c) {
    let t;
    if (y0 > y1) { t=y0;y0=y1;y1=t; t=x0;x0=x1;x1=t; }
    if (y0 > y2) { t=y0;y0=y2;y2=t; t=x0;x0=x2;x2=t; }
    if (y1 > y2) { t=y1;y1=y2;y2=t; t=x1;x1=x2;x2=t; }
    if (y2 === y0) return;
    let xl = x0, xls = (x2 - x0) / (y2 - y0);
    for (let seg = 0; seg < 2; seg++) {
      const ya = seg ? y1 : y0, yb = seg ? y2 : y1;
      if (ya === yb) { if (seg === 0) xl += xls * (y1 - y0); continue; }
      let xs = seg ? x1 : x0;
      const xss = (seg ? (x2 - x1) : (x1 - x0)) / (yb - ya);
      for (let y = Math.floor(ya); y < yb; y++) {
        if (y >= 0 && y < H) {
          let a = Math.round(xl), b = Math.round(xs);
          if (a > b) { t=a;a=b;b=t; }
          if (a < 0) a = 0; if (b >= W) b = W - 1;
          for (let x = a; x <= b; x++) {
            if (depth > zbuf[x] + 0.1) continue;              // behind a wall
            const o = (y * W + x) * 4;
            data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
          }
        }
        xl += xls; xs += xss;
      }
    }
  }

  function loop(now) {
    const dt = Math.min(0.05, (now - last) / 1000); last = now;
    move(dt); render();
    raf = requestAnimationFrame(loop);
  }
  return { start, stop };
})();
