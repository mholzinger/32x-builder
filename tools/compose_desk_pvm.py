#!/usr/bin/env python3
"""Compose the desk-with-PVM box model from the two shipped bakes.

Monitor ONLY (pvm3d.h box 0 — the stand stays on the floor-model), seated
on the desk's desktop. Rendered under the PVM's world_h (0.4), so desk
coordinates scale by world_h_desk/world_h_pvm = 0.31/0.4 = 0.775 to keep
the desk at its true size; the monitor's own scale factors cancel exactly
(pvm->desk * desk->final = 0.4/0.31 * 0.31/0.4 = 1), so its coords carry
over unchanged, just translated onto the desktop.

Monitor sits OFF-CENTER (x -93 final units): reads as placed by a person,
not by a compiler. Random-position variants arrive with the second desk
item (one variant mechanism, designed once).

The Master System console beside it is a single WEDGE (cbox_t.taper), not
the 3-step ziggurat it shipped as — see the comment at console_boxes.
"""
import os
import pathlib
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
from fit_wedges import fit_wedges

T = 0.775                      # desk-unit -> final-unit (0.31 / 0.4)
DESK = [(-252, 0, -112, -98, 243, 119),
        (120, 0, -124, 260, 243, 119),
        (-277, 243, -124, 273, 256, 119)]
MON = (-53, 160, -55, 53, 256, 46)     # pvm3d.h box 0
MON_CX, MON_CZ = -93, -6               # placement on the desktop (final units)

desk_f = [tuple(round(v * T) for v in b) for b in DESK]
desk_top = max(b[4] for b in desk_f)                     # y of the desktop
mx0, my0, mz0, mx1, my1, mz1 = MON
mw2 = (mx1 - mx0) // 2
mzc = (mz0 + mz1) // 2
mon_f = (MON_CX - mw2, desk_top, mz0 - mzc + MON_CZ,
         MON_CX + mw2, desk_top + (my1 - my0), mz1 - mzc + MON_CZ)

out = pathlib.Path(__file__).resolve().parent.parent / "sh_src" / "desk_pvm3d.h"
# SPLIT THE DESKTOP SLAB at the monitor's x-extent: a box that both spans a
# stacked box (monitor, Y-verdict) and extends past the pedestals (X-verdict
# vs the monitor) makes the pairwise separating-axis comparator CYCLIC, and
# the insertion sort then picks an angle-dependent arbitrary order — Mike's
# desk-through-monitor screenshot. Three segments = every pair single-axis
# consistent, cycle impossible.
slab = desk_f[2]
sx0, sy0, sz0, sx1, sy1, sz1 = slab
m_x0, m_x1 = mon_f[0], mon_f[3]
slab_parts = [(sx0, sy0, sz0, m_x0, sy1, sz1),
              (m_x0, sy0, sz0, m_x1, sy1, sz1),
              (m_x1, sy0, sz0, sx1, sy1, sz1)]
desk_f = desk_f[:2] + slab_parts
# THE CONSOLE: the Master System is a flat-bottomed TRAPEZOID — a sloped
# shell that greedy AABB baking turns into a lumpy staircase. It shipped as
# a hand-typed 3-step ziggurat, then as a hand-typed single wedge, and both
# were guesses: 120 x 38 x 33 is 3.6 : 1 : 1.2, where the real console is
# 5.2 : 1 : 2.5. Now it comes from the MODEL, via tools/fit_wedges.py.
#
# Two bands is the fit. Band 0 returns near-vertical (the console body),
# band 1 carries the taper up to the flat top — which is asymmetric in z,
# so the top rectangle is NOT centred over the base. That asymmetry is the
# whole reason the wedge carries four independent top coordinates.
#
# SCALE is true-size off the desk, not eyeballed. The desktop is 198 final
# units above the floor and an office desk is 730 mm, so one final unit is
# 730/198 = 3.69 mm. The Master System mk1 is 365 mm wide -> 99 units.
FIT = fit_wedges(os.path.join(REPO, "models", "sega_master_system.glb"),
                 bands=2, material="SEGA_MS_Black")
fit_boxes, fit_w, _fit_d = FIT
MM_PER_UNIT = 730.0 / 198.0
CON_W = 365.0 / MM_PER_UNIT            # console width in final units (~99)
CON_S = CON_W / fit_w                  # normalized-model -> final units
CON_CX = 90                            # placement on the desktop, off-centre

# The console's long SLOPE faces the player and its flat top sits toward the
# BACK. In the GLB the flat top is at -z, and -z is the engine's model FRONT
# (the face the facing rotation points at the camera), so importing the model's
# z verbatim stood the console backwards — long slope at the rear. Negating z
# turns it around. Negation also REVERSES the interval, so z0/z1 and tz0/tz1
# have to swap or the box comes out with z0 > z1 and every face inside out.
CON_FLIP_Z = True

def _place(b):
    """Normalized wedge (height 1.0, centred on its own footprint) -> final
    units on the desktop. y0 = 0 lands on desk_top, so the console sits ON
    the desk rather than intersecting it."""
    def px(v): return round(CON_CX + v * CON_S)
    def py(v): return round(desk_top + v * CON_S)
    def pz(v): return round(-v * CON_S if CON_FLIP_Z else v * CON_S)
    z0, z1 = (pz(b[5]), pz(b[2])) if CON_FLIP_Z else (pz(b[2]), pz(b[5]))
    tz0, tz1 = (pz(b[9]), pz(b[7])) if CON_FLIP_Z else (pz(b[7]), pz(b[9]))
    return (px(b[0]), py(b[1]), z0, px(b[3]), py(b[4]), z1,
            px(b[6]), tz0, px(b[8]), tz1, b[10])

console_boxes = [_place(b) for b in fit_boxes]
boxes = [mon_f] + desk_f + console_boxes   # MONITOR FIRST: ftex binds to box 0
def _row(b):
    """BOX6/WEDGE rather than a brace-elided literal: same bytes, but the
    model tables stop tripping -Wmissing-field-initializers on every row."""
    b = tuple(b) + (0,) * (11 - len(b))
    if b[10]:
        return "    WEDGE(" + ",".join(f"{v:6d}" for v in b[:10]) + "),"
    return "    BOX6(" + ",".join(f"{v:6d}" for v in b[:6]) + "),"

lines = "\n".join(_row(b) for b in boxes)
out.write_text(f"""/* Auto-generated by tools/compose_desk_pvm.py — do not edit.
 * The desk bake with the PVM MONITOR (pvm3d.h box 0, no stand) seated
 * off-center on the desktop. Drawn under the PVM's world_h (0.4): desk
 * coords are scaled 0.775 to stay true-size, the monitor's scale factors
 * cancel to exactly 1. Box 0 = the monitor (carries pvm_front_tex).
 * Total model height {mon_f[4]} units = {mon_f[4]/256*0.4:.3f} cells. */
#ifndef DESK_PVM3D_H
#define DESK_PVM3D_H
#include <stdint.h>
#include "chair3d.h"

#define DESK_PVM_NBOXES {len(boxes)}

static const cbox_t desk_pvm_boxes[DESK_PVM_NBOXES] = {{
{lines}
}};

#endif /* DESK_PVM3D_H */
""")
print(f"wrote {out}: monitor {mon_f}, model top y={mon_f[4]}")
