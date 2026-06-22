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

  /* port of init_lights: auto-grid fixtures every 2 cells on open non-crawl
   * cells, then the 3x3 cell_light boost (centre +2, neighbours +1, cap 3). */
  function computeLighting() {
    const m = window.ME.model;
    lightGrid = Array.from({ length: m.h }, () => new Int8Array(m.w));
    lowGrid = Array.from({ length: m.h }, () => new Uint8Array(m.w));
    fixtureGrid = Array.from({ length: m.h }, () => new Uint8Array(m.w));
    hasLow = false;
    for (const c of m.crawls) for (const [x, y] of runCells(c))
      if (y >= 0 && y < m.h && x >= 0 && x < m.w) { lowGrid[y][x] = 1; hasLow = true; }
    // fixtures: the authored Lights layer if any, else the engine's auto-grid.
    const pts = [];
    if (m.lights && m.lights.length) {
      for (const l of m.lights)
        if (l.cx >= 0 && l.cy >= 0 && l.cx < m.w && l.cy < m.h) pts.push([l.cx, l.cy]);
    } else {
      for (let my = 1; my < m.h - 1; my += 2)
        for (let mx = 1; mx < m.w - 1; mx += 2)
          if (cellVal(mx, my) === 0 && !lowGrid[my][mx]) pts.push([mx, my]);
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
  function partBlocked(x, y) {           // collide against the partition boxes
    const r = PT_HALF + 0.14;
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
  const PT_HALF = 0.08;   // partition half-thickness (cells) -> visible depth, not a 1px line
  function partitionHit(rx, ry, maxd) {
    const parts = window.ME.model.partitions; let best = null;
    for (const p of parts) {
      const dx = p.x2 - p.x1, dy = p.y2 - p.y1, len = Math.hypot(dx, dy) || 1;
      const nx = -dy / len * PT_HALF, ny = dx / len * PT_HALF;     // perpendicular offset
      const ax = p.x1 + nx, ay = p.y1 + ny, bx = p.x2 + nx, by = p.y2 + ny;   // a thin box:
      const cx2 = p.x2 - nx, cy2 = p.y2 - ny, ex = p.x1 - nx, ey = p.y1 - ny; // 2 faces + 2 caps
      const edges = [[ax, ay, bx, by], [bx, by, cx2, cy2], [cx2, cy2, ex, ey], [ex, ey, ax, ay]];
      for (const e of edges) {
        const h = raySeg(px, py, rx, ry, e[0], e[1], e[2], e[3]);
        if (h && h.dist < maxd && (!best || h.dist < best.dist)) {
          const hx = px + rx * h.dist, hy = py + ry * h.dist;
          const u = ((hx - p.x1) * dx + (hy - p.y1) * dy) / (len * len);   // along the divider
          best = { dist: h.dist, side: h.side, style: p.style, height: p.height, u: u, seg: p, len: len };
        }
      }
    }
    return best;
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

      let hfrac = 1, voidCol = false, tex = null, baseIdx = 0, distShade = 0, tu = 0;
      const ph = partitionHit(rx, ry, dist);
      if (ph) {
        dist = ph.dist < 0.02 ? 0.02 : ph.dist;
        tex = ph.style === 'spotted' ? A().textures.partition : A().textures.wall;
        baseIdx = ph.style === 'spotted' ? B.PARTITION_BASE : B.WALL_BASE;
        distShade = shadeIdx(dist, ph.side === 1 ? 2 : 0, 0);
        const segLen = Math.hypot(ph.seg.x2 - ph.seg.x1, ph.seg.y2 - ph.seg.y1) || 1;
        tu = Math.floor(((ph.u * segLen * TILE) % 1) * tex.w);
        if (ph.height === 'low') hfrac = 0.75;
      } else if (val === 2) {
        voidCol = true;                            // black-exit void
      } else {
        tex = A().textures.wall;
        baseIdx = B.WALL_BASE;
        distShade = shadeIdx(dist, side === 1 ? 2 : 0, lit(mapX, mapY));
        const wallX = side === 0 ? (py + dist * ry) : (px + dist * rx);
        tu = Math.floor((((wallX - Math.floor(wallX)) * TILE) % 1) * tex.w);
      }

      const eh = eyeH, sl1 = SL() - 1;
      zbuf[x] = dist;                              // depth for sprite z-test
      const wallBot = MID + eh * H / dist, wallTop = MID - (1 - eh) * H / dist;
      const lineH = wallBot - wallTop;
      const y0 = Math.max(0, Math.ceil(wallBot - lineH * hfrac));
      const y1 = Math.min(H, Math.ceil(wallBot));
      const P = A().palette;
      for (let y = y0; y < y1; y++) {
        const o = (y * W + x) * 4;
        let c;
        if (voidCol || !tex) { c = [0, 0, 0]; }
        else {
          const vf = (y - wallTop) / lineH;
          const tv = Math.floor((((vf * TILE) % 1 + 1) % 1) * tex.h);
          let s = distShade + texVal(tex, tu, tv); if (s > sl1) s = sl1;
          c = P[baseIdx + s] || [0, 0, 0];
        }
        data[o] = c[0]; data[o + 1] = c[1]; data[o + 2] = c[2]; data[o + 3] = 255;
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
    }
    if (spr.outlet) for (const o of procOut)   // engine's auto-placed outlets
      drawWallDecal(data, zbuf, o, spr.outlet, 0.031, 0.20, 0.098);
    /* FREE-STANDING billboards — the neanderthal standup, far -> near. */
    const bills = [];
    for (const dec of window.ME.model.decals)
      if (dec.kind === 'neanderthal' && spr.neander) {
        const ddx = dec.x - px, ddy = dec.y - py; bills.push({ d2: ddx * ddx + ddy * ddy, dec });
      }
    bills.sort((a, b) => b.d2 - a.d2);
    for (const it of bills) drawSprite(data, zbuf, it.dec.x, it.dec.y, spr.neander, 0.45, 0.90, 0.45);

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

  function loop(now) {
    const dt = Math.min(0.05, (now - last) / 1000); last = now;
    move(dt); render();
    raf = requestAnimationFrame(loop);
  }
  return { start, stop };
})();
