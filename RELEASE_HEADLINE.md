## Rest-mode supersampling

- Standing still (~350 ms, no buttons held) renders the scene twice — once normally, once with sampling shifted half a column — and merges the pair on a 1-pixel checkerboard into a single parked frame. On a CRT the dither fuses into sub-pixel detail on wall edges, textures, and floor/ceiling patterns.
- The parked frame is static: no page flipping while parked. Fluorescent palette flicker continues; ambience playback is unaffected.
- Any input exits immediately and the next frame renders normally. Adaptive resolution while moving is unchanged.
- Rooms with a powered monitor keep their live static instead of parking.
- Toggle: TESTING > ULTRA. Default on.

## Desk console

- The console now appears in every procedurally generated level. Placement required both an open 3x3 around the cell and a wall to back onto, which cannot both be true, so no generated level had ever received one.
- Activating the console requires facing it, within about fifteen degrees. Walking past and pressing A no longer starts it.
- Procgen PVMs are the desk-mounted composite; placement requires walkable neighbor cells. The canonical map's console sits at its authored nook, facing east, and boots the Master System.
- Console geometry is imported from the GLB as an authored 3-step ziggurat. The bake format gains a wedge primitive (a box with an inset top) for the sloped face, which points toward the player.
- New hand-edited bezel and rear-panel art. A single-file web editor round-trips the front and rear textures through editable PNGs, matched to the engine's mirrored sampling.
- Per-box shade ramps: desk, PVM, and Master System each carry their own gradient; the Master System is on a charcoal ramp.
- Monitor power-on settle flicker fixed.

## Asset viewer

- Composites (desk set, console) are first-class viewer entries; every asset opens the same way.
- Size control moved to the bare d-pad.

## Movement and audio

- Footsteps require actual displacement: walking into a wall no longer steps in place. Head-bob cadence follows the footstep rate, including sprint.
- Exit passage: dedicated corridor slide loop at native rate; landing scuff unchanged; both slide levels doubled.
- Crouched movement replaces footsteps with a drag sound.
- Walking into a bulkhead ducks automatically and passes through at walking speed. Crawl spaces still require the crawl.

## Master System

- Boot menu screen: TEST PATTERN banner wipe, startup chime reversed.
- TEST PATTERN: the minigame presents as a diagnostic screen; the level name appears in the title. Procedurally generated levels supply their own name, so each reads as its own specimen.
- Generative PSG score: a sparse G-minor melody with an echo channel and a walking bass, seeded from the level so no two play the same line.
- The title card no longer shows leftovers from a previous boot.

## Master System on the monitor

- Activating the desk console boots the Master System on the PVM itself. The title card and its chime play in the room, on the monitor, and after a short beat the screen takes over full-screen.
- The handoff keeps the same program running, so the chime and music do not restart across the cut.
- The 32X mix fades out while the Master System has the stage and returns on exit.

## Zoom into the glass

- Pressing A at the desk console now zooms into the monitor instead of cutting to full screen. The room magnifies around the glass and dissolves away; the picture grows out of the tube, re-rendered sharp at every size, and lands exactly on the full-screen frame. The player never moves, and leaving the session returns to the exact standing view.
- The full-screen picture is drawn by the 32X from the live Z80 tile stream, using the same font the Genesis path uses, so the two renderers agree pixel for pixel.
- The session plays inside a wallpaper-yellow frame: full brightness through the zoom, dimmed once the screen takes over.
- Input latency during the mini-game roughly halved.
- Fixed: a stale title screen could freeze over the mini-game picture; the exit hole's deepest shade and the SMS banner's % glyph were reading past their arrays; the PSG is silenced at power-on instead of singing from boot.

## Master System quality of life

- The console boots to a specs terminal: Master System operating specs, the level's specimen id, and two entries — DIAGNOSTICS (live frame counter, heartbeat, and a controller test that lights each button as you hold it) and FIELD MAP (the exercise). Every screen's START returns to the terminal; the terminal's START exits to the room. One ROM, layered exits.
- The picture channel moved to a delta protocol: only changed cells cross to the 32X, applied atomically per frame, with a background repair sweep that heals any dropped cell in under a frame. Screen transitions converge instantly; the mini-game's input-to-screen delay is at its floor.
- The Z80 is now properly parked when idle — frozen with its bus held rather than free-running or held in reset — so the sound chips keep their state across sessions. The fluorescent hum survives entering and leaving the console.
- AUDIO menu gains a HUM row (the synthesized bed's on/off); the hum's amplitude wobble is off pending a proper retune.
- The zoom-into-the-glass transition no longer smears at its edges mid-flight.

Under the hood for what's next: a host-verified true mode-4 Master System video pipeline (patterns, palettes, sprites, scroll) — the foundation for real SMS graphics on the console's screen.
