#!/usr/bin/env python3
"""Sync the PVM editors' embedded startup art from the edited PNGs.

The single-file editors (tools/pvm_bezel_editor.html, tools/pvm_rear_editor.html)
carry their starting image as a baked `let data=[...]` array — which goes stale
the moment Mike saves a new pvm_bezel_edit.png / pvm_rear_edit.png. This
rewrites each editor's array from the current PNG, quantizing through the
editor's OWN legend (parsed out of the HTML, so the two can never drift).

Run after any art save; the bake into the engine textures stays
tools/pvm_bezel_edit.py's job. No arguments:

    python3 tools/sync_pvm_editors.py
"""
import re
import pathlib
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAIRS = [
    (ROOT / "tools/pvm_bezel_editor.html", ROOT / "pvm_bezel_edit.png"),
    (ROOT / "tools/pvm_rear_editor.html",  ROOT / "pvm_rear_edit.png"),
]


def sync(html_path: pathlib.Path, png_path: pathlib.Path) -> None:
    html = html_path.read_text()
    dims = re.search(r"const W=(\d+),\s*H=(\d+)", html)
    w, h = int(dims.group(1)), int(dims.group(2))
    leg_src = re.search(r"const LEG=\{(.*?)\};", html, re.S).group(1)
    leg = {int(m.group(1)): tuple(int(x) for x in m.group(2).split(","))
           for m in re.finditer(r"(\d+)\s*:\s*\[([\d\s,]+)\]", leg_src)}

    im = Image.open(png_path).convert("RGB")
    sc = im.width / w
    if im.height / h != sc:
        raise SystemExit(f"{png_path.name}: {im.size} is not a {w}x{h} multiple")
    data = []
    for y in range(h):
        for x in range(w):
            px = im.getpixel((int(x * sc + sc / 2), int(y * sc + sc / 2)))
            best = min(leg, key=lambda v: sum((a - b) ** 2
                                              for a, b in zip(leg[v], px)))
            data.append(best)

    new = re.sub(r"let data=\[[^\]]*\];",
                 "let data=[" + ", ".join(map(str, data)) + "];",
                 html, count=1)
    if new == html:
        print(f"{html_path.name}: already in sync")
    else:
        html_path.write_text(new)
        print(f"{html_path.name}: startup art <- {png_path.name}")


for html_path, png_path in PAIRS:
    sync(html_path, png_path)
