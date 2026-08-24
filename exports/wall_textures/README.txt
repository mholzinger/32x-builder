BACKROOMS 32X — art pipeline reference (wall textures & sprites)
================================================================

This is the technical half: what your image becomes inside the game.
You can ignore all of it and just send us good art — the build handles
the conversion — but if you want to preview the trade-offs in Photoshop
or GIMP, here is exactly what happens.

THE GOLDEN RULE
  Work big. Everything is DOWNSAMPLED at build time — the neanderthal
  standee starts as a high-res photo and ships at 128x256. Nothing is
  ever upscaled, so resolution you give us is never wasted; it just has
  to survive shrinking (see per-type targets below).

WALL TEXTURES (this folder)
  What you edit:   SOURCE_walltile.jpg / SOURCE_square_composite.jpg,
                   at any size — 1024x1024 or larger, roughly square.
  What it becomes: TWO versions, scaled with Lanczos resampling:
                     64 x 64   shown on nearby walls
                     16 x 16   shown on distant walls
  Color:           discarded. Only LUMINANCE survives, posterized to
                   5 levels; the game re-tints it backrooms-yellow.
                   Photoshop/GIMP preview: Desaturate, then
                   Posterize -> 5 levels, then scale to 64x64.
  Tiling:          the 64x64 tile repeats 4x4 on every wall segment,
                   edge-to-edge forever. To check your seams: Filter >
                   Other > Offset (PS) / Layer > Transform > Offset
                   (GIMP) by 50%,50% with wrap — anything that shows a
                   line at the cross needs fixing.
                   Compare: wall_hi_64_tiled2x2_big.png in this folder.
  Rule of thumb:   detail thinner than 1/64th of your canvas width is
                   gone after the shrink. Bold beats fine.

SPRITES (standees, posters, signs, props — via the web editor)
  Submit here:     https://backrooms-32x-project.fly.dev  ("Add a sprite")
  What you upload: a PNG with a real alpha channel (transparent
                   background). Any resolution.
  What it becomes: the image is cropped to its opaque pixels, then
                   scaled to fit inside 64 x 96 (default: 48 wide,
                   height follows your aspect ratio). Total budget
                   4096 pixels — a tall 40x96 or a wide 64x64 both fit.
                   Optional "hi-res close-up": a second copy at 2x
                   (up to 128x192) the engine swaps in when the player
                   is within 3 tiles — the neanderthal's own trick.
  Alpha:           pixels under 50% opacity become fully transparent;
                   everything else is fully opaque. No soft edges —
                   feathered/anti-aliased fringes will halo. Keep your
                   cutout hard-edged (GIMP: Layer > Transparency >
                   Threshold Alpha; PS: harden the mask).
  Color:           your image is reduced to up to 7 of its own colors
                   (like a GIF with a tiny palette), picked automatically
                   to match your art. Greens stay green, reds stay red.
                   Strong distinct colors survive best; subtle gradients
                   between similar shades will merge.
                   Preview in PS/GIMP: Image > Mode > Indexed, 7 colors.
  What survives:   strong silhouettes, high contrast, chunky midtones.
                   What doesn't: hue detail, subtle gradients, thin
                   linework, soft glows.

SIZE CHEAT SHEET (what ships in the ROM)
  wall texture     64x64 near + 16x16 far, 5 luminance levels, tiles 4x
  sprite (standee) up to 64x96, 7 colors of its own, optional 128x192 hi-res
  wall decal       same as standee, painted flat on a wall at set height
  neanderthal      32x64 + 128x256 hi-res (the reference standee)

SENDING WALL ART BACK
  Full-res original, PNG preferred. However you got this folder works;
  a GitHub pull request adding it to images/ also works.
