# SMS test games

Maintainer doc. Design and constraints for Z80 mini-games that boot inside
the running 32X game. The first one (MAZE, build-242) proved the harness;
this doc defines the harness contract so more games can be added without
re-deriving it, and sketches the next candidates.

## 1. Harness (what shipped in build-242)

The 68000 loads a Z80 program into sound RAM through the $A00000 window
and executes it on the Z80. All game logic runs on the Z80. The 68000 is
a bridge: once per frame, under a brief bus request, it writes the pad
byte, bumps a frame counter, and copies out a tile buffer the game
maintains, which it then writes to the Genesis VDP name table (mode 5,
boot font). The Z80 never touches the VDP. Ares' MD core has no mode 4
and duplicates Z80 control-port writes; both problems are bypassed
entirely by this split.

Level injection: the blob reserves fixed regions for the level pack.
The SH-2 packs the live 32x32 `world_map` to 1 bit per cell plus spawn,
exit, the 16-tile level name, and the two edge-partition bitmaps
(pedge_n/pedge_w — thin slabs read as thin slabs on the map, and block
edge crossings, instead of vanishing), and streams the 440 bytes as 220
indexed words (command 0x0B); the 68000 splits the three ranges back to
their addresses after the code copy and before the Z80 leaves reset
(command 0x0C — the game's vars live in the gaps, so no straight copy).
Teardown parks the Z80 and sweeps the play rows (command 0x0D). The SH-2 owns the exit path (START in
`sms_game_screen`), so no Z80 state can trap the player.

Z80 start order (command 0x0C): release reset, request bus, copy code +
map, zero mailboxes, assert reset, release bus, release reset. The diag
spike (command 9) parks the Z80 instead; the game must run it.

## 2. Memory contract

Fixed addresses in Z80 RAM, single source of truth = the game's `.DEFINE`
lines, parsed by `tools/gen_z80_game.py` into `md_src/z80_sms_game.h`.

All RAM addresses in game.asm are written RB+offset: RB = 0 in the
harness build (Genesis Z80 RAM), RB = $C000 in the STANDALONE build
(section 2a) — same layout either way.

| Region      | Address       | Size | Owner                                  |
|-------------|---------------|------|----------------------------------------|
| code + data | $0000-$16FF   | 5888 | game (uploaded blob)                   |
| METATILES   | $1700-$182F   | 304  | game data (tiles.inc: 19 rows x 16 ids)|
| TILEBUF     | $1900-$1BFF   | 768  | game writes, 68K reads (24 rows x 32)  |
| MAP_BITS    | $1C00-$1C7F   | 128  | 68K patches (1bpp map, $80>>(x&7))     |
| MAP_META    | $1C80-$1C83   | 4    | 68K patches (spawn_x/y, exit_x/y)      |
| MAP_NAME    | $1C84-$1C93   | 16   | 68K patches (level name, tile ids)     |
| game vars   | $1CA0-...     | free | game                                   |
| PEDGE_N     | $1D00-$1D83   | 132  | 68K patches (33 edge rows x 4B: the N  |
|             |               |      | edge of cell (x,y), bit $80>>(x&7))    |
| PEDGE_W     | $1D90-$1E2F   | 160  | 68K patches (32 rows x 5B, x 0..32:    |
|             |               |      | the W edge of cell (x,y))              |
| HEART       | $1F00         | 1    | game increments (liveness forensics)   |
| stack       | grows < $1F80 | ~128 | game                                   |
| PAD_MBX     | $1FF4         | 1    | 68K writes pad byte each frame         |
| DIRTY_MBX   | $1FF5         | 1    | game sets 1 = new frame; 68K clears    |
| STATE_MBX   | $1FF6         | 1    | game: 0 playing, nonzero = end state   |
| FRAME_MBX   | $1FF7         | 1    | 68K increments at 60 Hz; game paces on |

Pad byte: U=$01 D=$02 L=$04 R=$08 B=$10 C=$20 A=$40 START=$80.

SMOOTH MAZE on the 32X arm: while the maze is being PLAYED, the SH-2
does not draw from TILEBUF at all — the 68K publishes the player's
cell + maze-active on COMM14 each frame (bit15 valid, bit10 active,
bits 9-5 x, 4-0 y), and the SH-2 renders the scrolled view itself from
world_map/pedge/sms_tiles.h with the same pixel camera and 4px/frame
glide as the standalone's scroll engine (m_main.c sms_maze_*). The
game's RPT_DELAY is 9 so cell repeats never outrun the 8-frame glide.
TILEBUF remains authoritative for every text screen and the glass;
the epoch reader sits maze frames out and its repair rotation heals
the picture when a text screen returns.

Display: TILEBUF holds tile ids. 0-44 are the boot font (mars.c
`NextChr`): space=0, dot=1, digits 2-11, A-Z 12-37, `:.->|+%` 38-44 —
every TEXT screen uses these. Ids 128+ are ART TILES: 4bpp 8x8 art from
sms/tileset.json (authored in tools/tile-editor.html, compiled by
tools/gen_sms_tiles.py). The maze's map view is built from METATILE
CELLS — each map cell a 4x4 block of art tiles (32x32 px), an 8x6-cell
viewport scrolled on both axes — stamped from the METATILES table. Art
renders on the SMS32X arm only (the SH-2 draws it through a dedicated
15-color CRAM run, SMS_PIC_BASE in m_main.c); the MD plane-B arm has
just the boot font in VRAM and clamps art ids to '%', so it shows a
legible-but-monochrome maze. The glass painter's lit/unlit phosphor
approximation treats any nonzero id as lit, art included.

Rules the harness enforces or assumes:
- One logic step per FRAME_MBX change. The Z80 free-runs between ticks.
- Set DIRTY only when TILEBUF changed; the 68K blits only on DIRTY.
- Mailboxes live outside the uploaded blob; uninitialized Z80 RAM boots
  $FF, so the game zeroes its own state first (the phantom-command lesson).
- Win check before wall check where the goal is a wall cell (the procgen
  exit door is one).
- TILEBUF and the var block sit ABOVE the uploaded image, so a re-upload
  does NOT clear them: a game inherits the previous session's screen (and
  a loaded savestate inherits that slot's). Clear TILEBUF at startup, or
  any screen that does not paint all 768 cells shows last run's leftovers
  — they compound boot after boot. The simulator boots from a $55 fill to
  keep this honest.

### 2a. Standalone .sms build (the fast Ares loop)

`make maze-ares` (sms/Makefile) wraps the SAME game.asm in a mode-4
shim (games/maze/standalone.asm) and emits games/maze/maze.sms — a
real 32KB cartridge image for Ares' MASTER SYSTEM core (which has real
mode 4; it is the MD core's mode-4 stub that is broken). No 32X boot,
no menu walk: open the .sms, A (button 1) starts, button 2 is START.
The shim plays the missing hardware's roles: VDP init + planar tiles
from tiles_sms.inc (same codegen run as everything else), a demo level
pack copied where the 68K would patch, sa_frame as the per-frame
bridge (vblank poll -> pad byte, FRAME_MBX, DIRTY blit), and psg_write
as OUT ($7F). game.asm's RB define lifts the whole RAM contract to
$C000.

The maze runs in SCROLL MODE (the Snail Maze pattern from
srcref/sms/bios13.asm, modernized per the devkitSMS survey): a pixel
camera over the 1024x1024-px map, VDP regs 8/9 pan it (reg 8 negated;
reg 9 wrapped at 224), ONE seam column/row streamed per 8px tile
crossing into the 32x28 name table (invariant: name row nr holds map
row base+k where nr=(base%28+k)%28; symmetric for columns), column 0
masked to hide the seam pop, and the player as 16 hardware sprites at
world - camera (sprite tiles = the player metatile with the floor
diffed to transparent, VRAM tile 224, sprite palette mirrors BG).
Moves glide at 4px/frame; the shim gates PAD_MBX to zero during a
glide so a held direction chains cell-by-cell. Font screens (terminal,
diag, card, debrief) stay on the plain DIRTY-blit path; transitions
reset scroll regs and empty the sprite list. Entry does a display-off
full paint (~2 dark frames, classifier-cached).

Boot-verified headlessly by tools/sim_sms_standalone.py (the game
sim's core + port instructions + a minimal VDP/pad model): boot,
terminal blit, cursor input, A into the maze, slides, BOTH seam axes
checked against an exact 896-entry name-table recomputation, escape
back to blit mode — run it after touching standalone.asm, before any
emulator session. FIRST-BOOT GOTCHA: Ares
starts the Master System with Controller Port 1 EMPTY — "input isn't
wired" means ares menu -> Master System -> Controller Port 1 ->
Gamepad, not a ROM bug (confirmed in ares source: the $DC read isn't
gated by $3F/$3E state; the port just has no device until assigned).
Remaining caveat: the DIRTY blit overruns vblank — fine in an
emulator, needs budgeting before real hardware.

## 3. Directory layout

    sms/
      DESIGN.md            this file
      Makefile             make GAME=<name> builds games/<name>
      hello.asm, gen_font.py, verify.py    the diag cart (standalone .sms)
      games/
        maze/game.asm      build-242 escape game (asm)
        hello-c/           the C toolchain spike: main.c + harness.h + crt0.s
        snail/             planned, section 5
        berzerk/           planned, section 6

Asm games assemble with wla-z80 + wlalink; C games compile with SDCC
(-mz80). Both produce a game.bin that `tools/gen_z80_game.py` trims and
packages. One game (maze) is compiled into the 68K side today;
multi-game selection is a milestone.

### 3a. C games (the compile-an-SMS-game-from-source path)

`games/hello-c/` proves it: 713 bytes of SDCC-compiled C that draws a
border and walks a P tile with the D-pad. `harness.h` is the platform —
the contract addresses as volatile pointers plus a frame_wait() helper;
it plays the role SMSlib plays in devkitSMS, because SMSlib itself
cannot work here (OUT-based VDP, mode 4, mapper — none exist for a
Genesis Z80 running from RAM). Porting an open-source SMS game (the
Ms. Pac-Man source, say) means keeping its C game logic and swapping
SMSlib rendering/input calls for harness.h ones.

Build mechanics that took debugging to land, recorded so they stay
landed: crt0 zeroes _DATA and _BSS before gsinit (Z80 RAM boots $FF and
makebin fills image gaps with $FF — nothing may rely on implicit
zeros); `--data-loc 0` chains the data areas right after code (SDCC's
default 0x8000 threw gsinit outside the image); SDCC output is named
out.ihx because `-o game.ihx` names every intermediate game.* and the
generated game.asm shadows the hand-written-asm convention; makebin -p
truncates at the last occupied byte.

Two known gaps: hello-c cannot BOOT until the multi-game milestone
wires a blob table into the 68K, and the subset simulator cannot
execute SDCC output (IX/IY frames etc.) — C games currently build
unverified. Options when it matters: grow the interpreter, or swap the
sim's core for a full Z80 emulator and keep the harness/assert layer.

## 4. MAZE v2: the escape simulator (current focus)

Direction (2026-08-09): v1 has NO enemies. The target is a passable
map-to-exit simulator using the best the Master System can do —
presentation and atmosphere, not mechanics.

Shipped rules: overhead view, D-pad moves one cell — HELD state behind
an 8-frame move cooldown (MOVE_COOL, = the display glide length on both
arms, so the game can never outrun the picture; edges + auto-repeat
caused run-ahead "input stickiness" on the 32X arm, which cannot gate
the pad the way the standalone shim does) — walls block (blocked
attempts set no cooldown: holding into a wall walks the moment a
corridor opens), stepping into the exit cell wins, escape screen,
START exits. v2 additions, each independent:

1. Fog of war: 128-byte seen-bitmap, cells revealed in a radius-3 square
   around the player, unseen cells render blank. ~150 bytes. Turns the
   overhead view from a spoiler into exploration.
2. Generative liminal audio (section 4a). The headline atmosphere item.
3. Step counter on the HUD row (row 0, digits). ~60 bytes. A score
   without new mechanics; optional.

Deferred until after v1: the wanderer entity (any hostile), BERZERK.

## 4a. Generative liminal audio (PSG)

The SN76489 PSG is memory-mapped at $7F11 in Z80 space — writable from
the mini-game with no OUT instructions. Three square channels + one noise
channel, 4-bit attenuation each, 10-bit tone dividers.

CHOSEN (Mike, from WAV audition): SPACE-A, the phrase+echo engine —
short G-minor motifs from the At the Inn transcription with music-box
decay, a 21-frame pseudo-delay echo channel, and a slow D/C/Bb bass walk.
Shipped in the maze blob; the sim asserts byte parity of the PSG stream.
B2-lydian (drone pair + sparse melody, below) is BANKED as the
kicking-off point — the engine sketch it came from:
- Channels 0+1: drone pair. Same scale note with dividers offset by 1-2,
  producing a slow beat frequency of a few Hz. Note changes are rare
  (every 10-30 s, LFSR-scheduled) and step through a small modal table
  (whole-tone or lydian fragments read as "empty building").
- Channel 2: sparse melody. LFSR picks a note and an inter-onset gap of
  3-15 s; volume steps 4 -> 15 (off) over 1-2 s for a long fade.
- Noise channel: near-silent periodic-noise floor, or off; occasional
  brief swell as an "air handler" event.
- 16-bit LFSR seeded from FRAME_MBX at boot so runs differ.
- Teardown rule: commands 0x0C and 0x0D write attenuation 15 to all four
  channels — the game must never leave the PSG sounding.

Preview workflow (no hardware in the loop): the simulator captures $7F11
writes with frame timestamps; a small SN76489 model in Python renders
them to WAV. Candidate engines get auditioned as WAV files on the Mac
before a single ROM build. The 32X PWM mix (buzz, neon, hello, footstep
one-shots) ducks to silence over ~0.5 s on SMS entry and ramps back on
exit — amb_volume and step_volume cover the whole mixer (Mike's call,
2026-08-09: the PSG owns the stage during the SMS window).

## 4b. Menu screen + boot chime (derived from BIOS 1.3)

Findings from `srcref/sms/bios13.asm`, the mechanisms we derived from
(no Sega tile or tone data is copied — it could not run here anyway;
the BIOS targets mode-4 hardware the harness does not have):
- Logo reveal: the SEGA tiles sit in the tilemap behind two masking
  sprites that shrink one pixel column per update (MaskingSpriteTiles
  $00,$01,$03..$7F) while HScroll slides the logo in.
- Chime: a 4-voice song engine (ancestor of the Alex Kidd driver);
  three voices in tone-ramp mode sweep up and land on a chord, with
  envelope and vibrato tables and a noise accent. The Snail Maze songs
  live in the same format directly below it.

Ours, in the maze blob (branding: TEST PATTERN, Mike 2026-08-11 — the
clinical Async-style register, not "Escape the Backrooms"): a TEST /
PATTERN banner in 3x5 block glyphs of boot-font '%' tiles, revealed by
a tile-column wipe (one column per frame — the harness has no sprites
or scroll, so the wipe is tile-granular); beneath it the LEVEL'S NAME,
patched in with the map as 16 tile-id bytes at MAP_NAME $1C84 (procgen
levels use cur_map_name's syllable hash, so each generated level is its
own specimen id); A TO BEGIN EXERCISE. The debrief screen is EXERCISE
COMPLETE + the level name + PRESS START TO EXIT. A starts the maze. The chime is the SE-GA gesture REVERSED (Mike's sms_putrats.wav
concept — the startup sound played backwards): the G-minor chord
(G3/D4/G4) swells in from silence over ~0.6 s, holds, sweeps DOWN a
24-step divider table to the low cluster, and cuts — reversed tapes do
not fade out. Then the SPACE-A engine takes the channels. Preview:
`sound/sms_liminal/CHIME-putrats.wav`; the sim asserts the whole
stream (chime + engine) byte-for-byte.

## 4c. Game-on-glass: the SMS picture on the in-world PVM (banked)

The PVM defaults to static when an SMS game powers up. The old idea was
snooping VRAM for the picture; unnecessary in this architecture — the
SMS display never touches VRAM. TILEBUF in Z80 RAM is the entire
display state and the 68K already copies it out every dirty frame
(sms_game_tiles in md_main.c). Remaining work:
- 68K -> SH-2 channel: COMM4 (index) + COMM6 (tile word), 68K free-runs
  the rotation from its idle loop, SH-2 samples ~dozens of pairs per
  frame into a local copy. No handshake — the joypad-bridge starvation
  history says never add request/response COMM traffic under render
  load. Convergence tearing reads as a CRT locking onto a signal, which
  is the aesthetic, not a bug.
- Glass render v1: 32x24 tile grid -> lit/unlit phosphor cells on the
  PVM texture (nonzero tile = lit). v2: real glyphs via the SH-2 font.
- Dead cells MUST use a real dark palette index, never 0: the 32X
  framebuffer drops byte writes of zero, so a 0 cell shows the WALL
  behind the monitor. Bit us once here; see the FB-zero-write note.
- The SH-2 GATHERS the broadcast (spin until all 48 words seen, bounded)
  rather than sampling blindly — blind sampling drew the picture in over
  seconds because each read had to land on a slot still needed.
- Console flow: A boots the glass session, a short beat plays on the
  monitor, then command 18 hands off to fullscreen with the SAME Z80
  running (no reboot = no second chime). The handoff must paint the
  cached frame once: the Z80 sets DIRTY only when its picture changes,
  so an idle attract card would leave the fullscreen black.
- The eventual continuous zoom into the glass is a roadmap item
  ("Zoom-into-the-glass transition"), gated on glyph fidelity.
Pairs with the diegetic PVM+console trigger that eventually replaces
the TESTING menu rows.

## 5. SNAIL (Snail Maze homage)

Reference: the built-in game in the SMS v1.3 BIOS (boot with no cartridge,
hold Up+1+2); `Snail2 NTSC v1.02.sms` homebrew also in the library. Core
rule: reach the goal before the timer runs out.

Ours: the shipped maze game plus a countdown. Timer in seconds (frames/60)
drawn on the HUD row; reach the exit = GOAL screen, timer zero = TIME OVER
screen, both wait for START. Par time derived from spawn-to-exit Manhattan
distance times a tuning constant, so procgen levels self-calibrate.
Estimated delta from maze: ~200 bytes. This is the cheapest second game
and exercises STATE_MBX with a lose state, which the harness has not seen.

## 6. BERZERK (Berzerk-like) — deferred (v1 is enemy-free)

Reference: Berzerk (arcade, 1980). No SMS port exists; this is a
from-scratch tile game, not a port. One room per run, built from the live
map viewport. Robots (4-6) step toward the player on a slow clock and fire
4-directional shots; the player moves and fires (A/B/C + facing from last
move); walls kill robots that step into them (the arcade's core joke);
contact with robot, shot, or nothing else kills the player. Clear the
room = score screen.

Cost estimate: entity array (6 x 4 bytes), shot array (8 x 3 bytes),
per-frame entity clock, tile-grid collision — roughly 600-800 bytes over
the maze base. Fits the 6400-byte budget with wide margin. The open
question is feel at cell granularity; if it plays too coarse, sub-cell
positions (half-cell steps rendered by alternating tiles) are the fallback
before any custom-tile work.

## 7. Out of scope for this tier: real SMS ROMs

This harness runs RAM-resident, OUT-free, mode-5-duet payloads. Real SMS
software (the BIOS Snail Maze included) needs VDP mode 4, port-mapped I/O
(OUT), and 32KB+ with a mapper. Known paths, both preserved for a later
arc: the real-hardware mode-4 duet (v4, commit ~64a0a21) on MiSTer, or an
SH-2-side SMS emulator. Reference library for that arc:
`root@mister.tv.local:"/media/usb0/games/SMS/Master System"` (full set,
BIOS carts under `5 Tools & Service Test Carts/Bios`).

Toolchain for the standalone-.sms milestone (Mike, 2026-08-09):
xfixium's open-source SMS Ms. Pac-Man (github.com/xfixium/SMS-Ms-Pac-Man)
is a complete worked example on the modern stack — devkitSMS (SMSlib +
PSGlib) compiled with SDCC, C not asm, banked PNG-derived tile resources,
poll/vblank/audio/state main loop. Adjacent tools: Emulicious (SMS
debugger-emulator, the right test bench for mode-4 builds since Ares' MD
core has no mode 4), SMS Tile Studio, smspower.org dev forums + Maxim's
tutorial. Also a direct gameplay reference: its ghost.h chase AI is the
template for whenever enemies land post-v1 — a ghost-chase through the
backrooms maze is a closer fit to our maze game than BERZERK's robots.
PSGlib's compiled-VGM music format is the authored-track alternative to
our generative engine if the standalone tier ever wants a real score.

## 8. Testing

`tools/sim_z80_game.py`: subset Z80 interpreter that runs the real
assembled bytes against a scripted 68K (pad sequences in, TILEBUF asserts
out; unknown opcode = failure). Each game gets a scripted boot-to-win and
boot-to-lose run. The interpreter grows opcodes only as games need them.

## 9. Milestones

- M1 DONE: `games/` layout, parameterized build
- M2 partial: SPACE-A music shipped (Mike's pick); fog of war still open
- M2.5 DONE: C toolchain spike (games/hello-c, SDCC, harness.h)
- M3: multi-game plumbing — 68K blob table, SMSGAME menu row becomes
  LEFT/RIGHT picker; first bootable C game rides this
- M4: SNAIL (countdown timer, first lose state)
- M5: BERZERK or a ghost-chase (first enemies, explicitly post-v1)
