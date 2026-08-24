# Community sprites: put your own standee in the Backrooms

Any transparent PNG can become a free-standing cardboard-style standee that
ships in the ROM and is placeable in community maps — same pipeline as
community maps: bake → PR → CI builds → next release has it.

**Where it ships.** A baked sprite is tiered `community`, which means it
compiles into `backrooms-community.32x` and into your own
`backrooms-<yourname>.32x`, and community maps can place it freely. The
flagship `backrooms.32x` carries first-party assets only; the maintainer
promotes a sprite into it by changing `"tier": "community"` to `"core"` in
`registry.json`. See the tier table in the [README](README.md).

## The easy way: the hosted editor

1. Open the [map editor](https://backrooms-32x-project.fly.dev/) and find
   **Add a sprite** in the sidebar.
2. Pick an image (PNG with transparency works best), name it
   (`a-z 0-9 _`, 2–16 chars), and choose the **mount** — this is the big
   decision:
   - **Free-standing (standee)** — stands on the floor facing the camera,
     players collide with it. The neanderthal is one. For props, figures,
     furniture.
   - **Wall decal** — painted flat onto a wall face, foreshortens with the
     wall, mounts at any height. The electrical outlet is one. For posters,
     signs, stains, fixtures.

   Set its world size (1.0 = floor to ceiling), and for wall decals the
   height on the wall. Hit **Bake sprite** — the preview is pixel-exact
   through the real 32X palette.
3. **Place in this map** arms it as a decal: standees drop on an open
   cell, wall decals mount on a wall *edge*. Then **▶ Walk** the map and
   judge your import in first person — before it even ships.
4. **🚀 Submit sprite PR** (signed in with GitHub) opens a two-file pull
   request under your name: the baked texture and its registry entries.
   Or **⬇ Bundle** downloads both files for a manual PR.

Two places it can go. **🏠 Save to my copy** commits it to your own fork,
where the community palette arena is yours alone — spend all ~14 sprite slots
on your own art, split a drawing into as many assets as you want, and your
fork's CI builds you a ROM with them in it. **🚀 Submit sprite PR** offers it
to the main game instead; once that merges, the next `build-N` release renders
your standee and every mapper can place it.

## The CLI way

```sh
# a free-standing standee (like the neanderthal):
python3 tools/bake_sprite.py --id traffic_cone --src cone.png \
        --height 0.55 --author yourname
# a wall decal (like the outlet), centred 60% up the wall:
python3 tools/bake_sprite.py --id warning_sign --src sign.png \
        --mount wall --height 0.3 --z 0.6 --author yourname
make        # regenerates sprite_defs.h; the sprite is now placeable
```

One command writes `sh_src/spr_<id>_tex.h` and registers the sprite in
`registry.json` (both `assets.sprites` and `decals.kinds`). Commit those two
files in your PR.

## What the bake does (and its limits)

- Crops to your image's transparent bounding box, then scales DOWN to the
  texel budget for its mount: a standee gets ≤64×96 (about 4 KB), a wall decal
  ≤224×384 (about 26 KB). Wall decals get the bigger share because fine grain
  is what they're usually made of — a stippled tear baked at 89 texels wide
  averages its speckle into a smooth smudge, which is mangling your art, not
  shrinking it.
- Reduces your image to up to **7 of its own colors** (median-cut, like a
  small-palette GIF), so your art keeps its hues — greens stay green. Each
  sprite gets its own slot block in the console's palette; about 14 color
  sprites fit, which is comfortably above the engine's other limits.
  Strong distinct colors survive best; subtle gradients merge.
- **Hi-res close-up (optional, standees only)**: bakes a second texture at
  double resolution that the engine swaps in within 3 cells — the same LOD
  trick the neanderthal uses. About 5× the ROM cost, so save it for hero
  pieces (`--hi` on the CLI, the checkbox in the editor).
- The standee renders as a camera-facing billboard with distance fog, is
  solid to walk into, and fully participates in the engine's occlusion.

## Rules of thumb

- Strong silhouette + clear midtones survive 16 shades; subtle color
  gradients won't.
- Keep it in the spirit of the place. Maintainers review PRs before merge.
- Kind numbers are assigned automatically; if two sprite PRs race, the
  second just rebases (re-run the bake or bump the `kind` by one).
