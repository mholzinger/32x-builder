#include "mars.h"
#include "raycast.h"
#include "shared.h"
#include "menu.h"
#include "sh2_asm.h"
#include "sin_table.h"
#include "wall_tex.h"
#include "wall_tex_hi.h"
#include "neander_tex.h"
#include "neander_tex_hi.h"
#include "outlet_tex.h"
#include "door_tex.h"
#include "partition_tex.h"
#include "custom_maps.h"

/* d32xr Lever 1: place a per-frame-hot renderer function in cacheable SDRAM
 * (.ramtext, copied from ROM at boot) instead of executing it from slow,
 * uncacheable ROM. The 32X MARS header's ROM->SDRAM copy carries it over; see
 * mars.ld. No behavior change — pure instruction-fetch speedup. */
#define RAMTEXT __attribute__((section(".ramtext")))

/* Player spawn — south end of the col-16 spine corridor in the
 * hand-tuned 32x32 Backrooms map. Walls flank tightly at cols 15/17
 * for the iconic "infinite hallway" first frame. The corridor opens
 * north into a central band, then four distinct zones branch off:
 * NW = office cubicles, NE = nested rooms, SW = twisty maze,
 * SE = lounge with pillars. */
player_t player = {
    .x = FX(16.5),
    .y = FX(28.5),
    .angle = 192,
};

/* Hand-tuned 32x32 Backrooms map (tools/gen_backrooms_map.py).
 * Pivoted away from the movie.blend extraction because that geometry
 * was either too cramped (32x32 of an 82-wide model swallowed all the
 * doorways) or too sprawling (64x64 had doorways but felt like one
 * long corridor in any direction). This hand-tuned map captures the
 * "AI-generated procedural rooms" Backrooms feel by giving each
 * cardinal direction a distinct character:
 *
 *   NW (rows 1-8, cols 1-13):  office cubicles — 4 small rooms with
 *                              irregular doorways, Level-0 office floor.
 *   NE (rows 1-8, cols 17-30): nested rooms — three concentric boxes
 *                              ("room within a room within a room",
 *                              the iconic Backrooms doorway shot).
 *   CENTER (rows 10-15):       open band with pillar islands.
 *   SW (rows 17-30, cols 1-14): twisty maze with partial walls.
 *   SE (rows 17-30, cols 17-30): open lounge with scattered pillars
 *                                and a couple of "stub walls" that
 *                                make no logical sense (the uncanny
 *                                "why is this here" Backrooms vibe).
 *   SPAWN CORRIDOR: tight col-16 N-S corridor from row 17 to 28,
 *                   side-doors at (15,20), (17,23), (15,26). */
static const uint8_t fixed_map[AUTH_H][AUTH_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,1,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0,0,1,0,0,1,1,1,1,1,1,0,0,1,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,1,1,1,0,1,1,0,0,1,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,1,1,1,1,1,1,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,0,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,1,0,0,1,1,1,1,1,0,1,0,1,0,1,0,0,0,0,1,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1},
    {1,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,0,0,1,1,1,1,1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1},
    {1,0,0,0,1,1,1,1,1,1,0,0,0,1,0,1,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,1,0,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,1,0,1,1,1,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,1,1,1,1,0,1,0,1,1,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* The HobbyTown lobby — a tiny 5x5 room (open cols 2-6, rows 2-6; the rest
 * of the 32x32 grid is solid, unused). The box is grid walls; the dividers
 * are free-standing wallpaper PARTITIONS set in raycast_load_lobby. Spawn
 * (S) is bottom-centre facing north; walk up through the entrance gap,
 * around the T-divider, and out the east exit to load the chosen level.
 *
 *        col: 2 3 4 5 6 7
 *        r2   . | . . . E    | = T-stem (partition, x=3)
 *        r3   . |== . . #    == = T-arm (partition, x3->5)
 *        r4   . . . . . #
 *        r5   == . == . #    entrance wall (partition), gap cols 3-4
 *        r6   . . S . . #    S = spawn (faces north); E = east exit */
static const uint8_t lobby_map[AUTH_H][AUTH_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* The live world grid the raycaster reads. Filled at runtime by
 * raycast_load_fixed() / raycast_load_lobby() / procgen_run() — the
 * hand-tuned layout now lives in fixed_map above and is copied in on
 * demand, so the lobby and procedural maps can replace it in place. */
uint8_t world_map[MAP_H][MAP_W];

/* Palette layout (8bpp, 256 entries):
 *   0          : black (sky / unrendered)
 *   1..16      : yellow wallpaper, brightest..darkest (distance shading)
 *   17..32     : brown carpet floor, brightest..darkest
 *   33..48     : off-white ceiling, brightest..darkest
 */
#define WALL_BASE    1
#define FLOOR_BASE   17
#define CEIL_BASE    33
#define LIGHT_BASE   49     /* 4 entries: full / 75% / 50% / 25% for flicker */
#define CHAIR_BASE   53     /* 4 entries: chair wood, its own ramp — DOOR_DARK
                             * (shared with the door recess) bottoms at near-black,
                             * which fog turned every in-game chair into. This ramp
                             * is a touch lighter at both ends so a fogged chair
                             * still reads as dark wood, not silhouette. */
#define NEANDER_BASE 64     /* 8 entries: 0=cardboard back, 1-7=figure shades */
#define SHADE_LEVELS 16
/* Projected-silhouette shadow: the figure's own texture laid flat on the floor,
 * its opaque texels dithered in this dark floor shade. DIR = world direction the
 * shadow falls (engine angle, light-fixed, not camera-relative); LEN = how far
 * the silhouette stretches. Tuning knobs. */
#define STANDUP_SHADOW_COLOR  (uint8_t)(FLOOR_BASE + SHADE_LEVELS - 3)
#define STANDUP_SHADOW_LEN    FX(0.6)    /* how far the silhouette stretches on the floor */
#define STANDUP_SHADOW_TUCK   FX(0.09)   /* sink the near edge this far UNDER him (a real
                                          * contact shadow does) so both feet tuck in, closing
                                          * the spread-stance foot gap */
#define OUTLET_BASE   72    /* 5 entries: 0=slot-dark .. 4=plate-white */
#define OUTLET_LEVELS 5
#define PARTITION_BASE 77   /* 16 entries: olive spotted-wallpaper, bright..fog */
#define LOWCEIL_COLOR  93   /* (legacy) dark-beige panel fill — kept for A/B fallback */
#define LOWCEIL_SEAM   94   /* (legacy) darker-beige panel seam */
#define LOWCEIL_SEAM_W 0x1000   /* seam half-width, 16.16 (lattice every 0.5 cell) */
/* Crawlspace ceiling = the room's ceiling TILE, but LIGHTLESS: the CEIL_BASE
 * ramp at a dim/unlit shade, with the grid at the same 0.25-cell tile period as
 * the open ceiling (CEIL_GRID_DENSITY = 4) so the tunnel tiles line up with the
 * room's. No fluorescent panels are drawn on it — that's the "lightless". */
#define LOWCEIL_TILE_SHADE 6              /* near (unlit) shade into the CEIL_BASE ramp */
#define CEIL_TILE_MASK 0x3FFF             /* 0.25-cell period (FX_ONE / CEIL_GRID_DENSITY - 1) */
#define CEIL_TILE_LINE 0x520              /* grid-line half-width, 16.16 */
#define DOOR_BASE     96    /* 8 entries: 0-4 grey metal, 5-6 green EXIT sign, 7 white */
#define DOOR_DARK_BASE 104  /* 4 entries: extra-dark greys BELOW the door ramp, so the
                             * leaf can darken as it swings away from us into shadow */
#define STIPPLE_BASE  108   /* 5 entries: the door ramp shades nudged ~1-2 darker — the
                             * baked stipple dots, a soft almost-imperceptible dapple */
#define HANDLE_BASE   113   /* 4 entries: warm gold/brass for the door handle hardware
                             * (dark, mid, light, highlight) so it reads as metal not tan */
#define HOLE_DEEP_BASE 140  /* 4 entries, light->dark: glow-bright, glow-dim,
                             * deep-mid, deepest — the EXIT HOLE's cool run.
                             * 140/141 are the back panel's faint interior
                             * light; 142/143 are the deep tail. hole_ramp
                             * splices the wall ramp onto DOOR_DARK, and while
                             * the luma runs monotonic the HUE does not --
                             * wall floor #4A4A41 is olive, DOOR_DARK+2 #4A3929
                             * is brown. The back panel's gradient spans exactly
                             * that join, so it rendered as a warm top half and
                             * a cool bottom half meeting at a hard line: a
                             * shading edge with no geometry under it, which is
                             * precisely what breaks the read of a square
                             * tunnel.
                             *
                             * They now also carry the DARK-ROOM aesthetic. The
                             * whole ramp above them is WALL_BASE -- the
                             * WALLPAPER's own ramp -- so a back panel painted
                             * from it is a dark piece of wallpaper, tan cast
                             * and all, rather than a room with no light in it.
                             * Merely de-warming them did nothing visible -- one
                             * CRAM step of red out of 31. The cue that actually
                             * reads is COOL: every lit surface in this game is
                             * tungsten-warm (R>B), so a back panel with B>R is
                             * the only thing in frame with no light on it, and
                             * that contrast is what says "another space" rather
                             * than "shadowed wall". Free
                             * CRAM: 140..143 sit between the EXIT sign's plate
                             * and COMM_BASE, so the community arena is
                             * untouched. */
/* MASTER SYSTEM console ramp, four near-black steps at 190..193.
 *
 * A CORE ramp parked in the community arena's free tail next to the PVM's,
 * not a community upload. It is not in registry.json on purpose: a sprite
 * palette is capped at 7 colours there and pvm already spends 5, and an entry
 * of its own would mint a phantom sprite kind with no texture that the asset
 * viewer would then cycle through.
 *
 * It exists because the console needs to stay BLACK and still show a light
 * gradient, and the PVM's ramp cannot do both — its top two steps are case
 * gray, so holding the console dark there meant biasing it down onto two
 * indices and losing the gradient entirely. Its own ramp buys all four steps
 * inside charcoal. */
#define SMS_RAMP_BASE 190
/* VIEWER_INK (asset-viewer label navy) lives in raycast.h — m_main.c draws
 * with it. A dedicated entry rather than a borrowed ramp step: the viewer
 * labels sit on a flat wallpaper-yellow backdrop, and the jamb brown they
 * used to use was close enough in tone to read as part of the wall. */
#define COMM_BASE     144   /* start of the COMMUNITY CRAM ARENA (144..255):
                             * each contributor sprite owns an 8-slot block
                             * holding ITS OWN median-cut palette (base+1..
                             * base+7, darkest->brightest; base+0 spare), the
                             * same model as the door/outlet/neanderthal.
                             * Allocated by tools/bake_sprite.py, painted from
                             * the generated comm_pal.h below — 14 sprites fit. */
#define SIGN_GREEN_BASE 132 /* 4 entries: EXIT-sign green letters, near->fog */
#define SIGN_WHITE_BASE 136  /* 4 entries: EXIT-sign white plate, near->fog */
#define WOODTOP_BASE  124   /* 8 entries: countertop wood, jamb-brown fading into
                             * the FOG color — the half-height top plane's own
                             * distance ramp (the 4-step jamb ramp never reached
                             * fog, so far countertops read as floating planks) */
#define FRAME_BASE    117   /* 5 entries: the door jamb/casing — a muted-brown ramp
                             * (dark..light, +4 lit) so the frame darkens with distance
                             * like the walls while staying less tan than the door */

/* The data-driven sprite table (codegen'd from registry.json "assets"). Indexed
 * by decal/standup kind; carries each asset's texture(s), dims, palette base,
 * world size and flags so renderers read DATA instead of per-kind #defines.
 * Included here — after the *_BASE macros it references. */
#include "sprite_defs.h"
#include "comm_pal.h"     /* community CRAM arena (generated) */
#include "chair3d.h"
/* chair_model.h (the baked 1,692-tri hero GLB mesh) is no longer compiled
 * in: it shipped only as the viewer's MESH variant, which nothing in the
 * game used, and it crawled even wireframed. The file stays on disk as the
 * bake reference; every model view now renders its box list. */
#include "desk3d.h"         /* tools/bake_boxes.py output — imported GLB as boxes */
#include "pvm3d.h"          /* same, for the PVM monitor + stand */
#include "desk_pvm3d.h"     /* tools/compose_desk_pvm.py — monitor on desk */
#include "pvm_rear_tex.h"   /* Mike's rear panel (tools/pvm_bezel_edit.py) */
#include "pvm_front_tex.h"  /* screen + control panel, mapped onto box 0's front face */
#include "chair_dir_tex.h"     /* directional billboard views baked from the box model */
#include "desk_dir_tex.h"      /* same, for the imported desk (tools/bake_dir_sprites.py) */
#include "pvm_dir_tex.h"       /* same, for the PVM monitor + stand */
#include "deskset_dir_tex.h"   /* same, for the desk-with-PVM composite — MULTI-RAMP */
#include "chair_shadow_tex.h"  /* plan-silhouette floor-shadow stencils, feet-anchored */


/* Wall texture comes from wall_tex.h (generated from images/walltile.jpg). */
#define TEX_W WALL_TEX_WIDTH
#define TEX_H WALL_TEX_HEIGHT

/* Liminal fog. DDA walks rays out to MAX_VIEW_DIST then bails — walls
 * beyond that simply aren't hit. The shade ramp uses the shorter
 * FOG_RAMP_DIST so walls reach full-fog shade by ~6 cells; anything
 * the DDA finds between FOG_RAMP_DIST and MAX_VIEW_DIST renders at
 * shade 15 (indistinguishable from the floor/ceiling fog), giving the
 * Backrooms "emerges from the greenish darkness" effect rather than a
 * hard pop-in at the cutoff.
 *
 * PERF: MAX_VIEW_DIST pulled 10 -> 7. Walls are already full-fog shade by
 * FOG_RAMP_DIST (6), so the 7..10 band was rendering wall pixels that are
 * indistinguishable from the fog fill — pure waste on open sightlines. The
 * DDA now bails a few cells sooner (fewer steps) and those far fog-walls
 * become fog fill instead. The 1-cell buffer (6 full-fog -> 7 cut) keeps the
 * cut inside the fog so there's no pop. row_color (floor/ceiling fog) is a
 * screen-row ramp, independent of this, so those passes are unchanged. */
#define FOG_RAMP_DIST     FX(6)
#define MAX_VIEW_DIST     FX(7)
#define MAX_VIEW_DIST_INT 7
/* WALLS=LOD depth bands (perpDist). Below NEAR = full res, NEAR..FAR = half,
 * beyond FAR = quarter + dither. Tunable — where the three zones fall. */
#define LOD_T_NEAR        FX(2)
#define LOD_T_FAR         FX(4)
/* B-fix: a see-over partition THIS close overrides the LOD near-band drop and
 * forces the quad back to full horizontal res. The near-band drop (lod_near_cs)
 * coarsens the whole near band for FPS in slab-heavy views, but a partition an
 * arm's length away is exactly where the chunkiness screams — so we spend the
 * res back, but ONLY nose-to-slab, so a room full of distant dividers keeps the
 * speedup. Lagged one quad (mirrors lod_prev_depth), so at most a 4px near edge
 * stays coarse before it snaps to full. */
#define PART_SHARP_D      FX(2.5)
/* Partition-dense feedback (SHARED_UC->wall_dense): the primary latches ON when
 * last frame's wall pass exceeds WALL_DENSE_ON and OFF below WALL_DENSE_OFF, in
 * raw FRT ticks. The band is wide because dropping the near band to half roughly
 * halves the slab cost — a sparse room sits ~2-5k, the SM Furniture pit ~14k, and
 * the pit at half-near-res lands ~8k, still well above OFF so it can't strobe. */
#define WALL_DENSE_ON     9000
#define WALL_DENSE_OFF    4500

/* Drop-ceiling grid density — number of panel boundaries per 1-unit map
 * cell. Higher = denser grid. The cost is identical at any density; we
 * just scale world coordinates by this factor before integer-crossing
 * detection so a boundary at every (1/CEIL_GRID_DENSITY) units triggers. */
#define CEIL_GRID_DENSITY 4

/* Wallpaper chevron pattern strength at close range, 0-16. 8 caps
 * the chevron-vs-base shade gap at 2 levels (out of 16) — subtle
 * enough that the wall reads as yellow first, motif second, matching
 * how an actual Backrooms wallpaper sits against the yellow base. */
#define WALL_PATTERN_MAX 8

/* Per-column z-buffer captured during wall draw so the light billboards
 * can z-test against walls. 0x7FFFFFFF = no wall hit (light wins).
 *
 * Both CPUs write to this array (primary cols 0-159, secondary 160-319) and
 * the primary reads it back for the sprite z-test. ALL accesses must
 * go through the WALL_DIST() macro below, which routes them via the
 * | 0x20000000 cache-through alias so neither CPU sees stale cached
 * values written by the other. */
static fx_t wall_dist[SCREEN_W];
#define WALL_DIST(i) (((volatile fx_t *)((uintptr_t)wall_dist | 0x20000000))[i])
/* Top screen row of the column's occluder when it is a PARTIAL (see-over)
 * partition, 0 = full-height/empty. Lets the sprite pass draw a standup's
 * rows ABOVE a half-height divider instead of vanishing column-wide (the
 * neanderthal-behind-a-partition bug). Same cache-through discipline. */
static int16_t part_topv[SCREEN_W];
#define PART_TOP(i) (((volatile int16_t *)((uintptr_t)part_topv | 0x20000000))[i])
/* BACKGROUND depth (>>4, like the slab pass's wd[] cache) saved when a see-over
 * partial overwrites WALL_DIST with its own near depth. The ceiling-tail passes
 * (crawl slab / bulkhead / caps) draw rows ABOVE the partial's PART_TOP, so they
 * must z-test those rows against what's BEHIND the counter, not the counter --
 * without this a bulkhead vanished column-wide behind any half-height divider
 * (the same disease PART_TOP cured for sprites). 32767 = no background saved.
 * Same cache-through discipline. */
static uint8_t bg_distv[SCREEN_W];   /* depth >> 12 (1/16 cell, range 16 cells):
                                      * coarse is fine -- it answers "is the
                                      * ceiling slab in front of the backdrop",
                                      * surfaces cells apart. 255 = none. */
#define BG_DIST(i) (((volatile uint8_t *)((uintptr_t)bg_distv | 0x20000000))[i])

/* Recorded main-wall silhouette (clamped drawStart/drawEnd) at each raycast
 * anchor column, for the SEAMS=SMOOTH post-pass. Each CPU only reads back its
 * OWN column half here (disjoint from the other's), so plain statics — no
 * cache-through needed, unlike wall_dist/part_topv which the sprite pass reads
 * across the seam. seam_valid gates: set only where a solid wall actually filled. */
/* seam_top doubles as the validity flag: -1 = no wall recorded, 0..SCREEN_H-1 =
 * a solid wall's top (its bottom in seam_bot, depth in WALL_DIST). Saves a
 * separate seam_valid[] array. */
static int16_t seam_top[SCREEN_W];
static int16_t seam_bot[SCREEN_W];
#define SEAM_VALID(c) (seam_top[c] >= 0)
/* Set by the wall pass to its own seam_rec each frame. The seam arrays are ONLY
 * written (and only reset to -1) when seam_rec is on, so with SEAMS=HARD and no
 * dither/LOD they hold whatever the last recording frame left behind, forever.
 * The covered-row skip below must not trust stale seams, hence this flag. */
static int g_seam_rec_on = 0;

/* COVERED-ROW SKIP. Ceiling paints [0,horizon) and carpet paints [horizon,
 * SCREEN_H), both for every column and both BEFORE the wall pass, so any row the
 * walls will cover in EVERY column is drawn and immediately buried. Measured OD
 * (wall coverage) runs 8% in open rooms, 32% in a corridor, and 92% in partition
 * views -- so this pays exactly where the partition scenes hurt.
 *
 * Row-level, not per-column, on purpose: carpet's inner loop is one hash and a
 * conditional byte store, so a per-sample bounds test would cost more than the
 * store it skips. Collapsing to a scalar pair makes the per-row test free.
 *
 * The band is LAST frame's -- the wall pass resets the seams after we run. That
 * is fine: raycast_clear_half already laid the base gradient and these passes
 * only ADD detail, so being wrong on a fast turn costs one frame of missing
 * stains/grid over correct base colour, never garbage. COVER_MARGIN shrinks the
 * band at both edges to absorb a frame of turning. Per-CPU over its own column
 * range, so no cross-SH2 coherency question arises. */
#define COVER_MARGIN 4
static inline void covered_rows(int col_start, int col_end, int *lo, int *hi) {
    int b_lo = 0, b_hi = SCREEN_H - 1;
    if (!g_seam_rec_on) { *lo = 1; *hi = 0; return; }   /* stale seams: skip nothing */
    for (int c = col_start; c < col_end; c++) {
        if (!SEAM_VALID(c)) { b_lo = 1; b_hi = 0; break; }  /* a bare column covers no row */
        int t = seam_top[c], b = seam_bot[c];
        if (t > b_lo) b_lo = t;
        if (b < b_hi) b_hi = b;
        if (b_lo > b_hi) break;
    }
    *lo = b_lo + COVER_MARGIN;
    *hi = b_hi - COVER_MARGIN;
}

/* NUM_PARTITIONS_MAX declared in raycast.h so procgen sees the same cap. */

/* Purge a byte range from the SH-2 cache via the 0x40000000 alias (one store
 * invalidates a 16-byte line). */
static inline void purge_cache_range(const void *p, unsigned bytes) {
    uintptr_t a   = (uintptr_t)p & ~(uintptr_t)15;
    uintptr_t end = (uintptr_t)p + bytes;
    for (; a < end; a += 16) *(volatile uint32_t *)(a | 0x40000000) = 0;
}


/* Hardware (a<<16)/b in 16.16 — signed 64÷32 on the SH-2 divide unit at
 * 0xFFFFFF00, ~39 cycles vs ~250 for a libgcc software int64 divide. The
 * 48-bit dividend (a<<16) is fed as DVDNTH:DVDNTL with the high word
 * sign-extended; the unit divides signed natively and the read of DVDNTL
 * stalls until the divide completes.
 *
 * No saturation: every caller gates the divide behind |denom| >= 64, which
 * bounds the quotient inside 32 bits — overflow is impossible here. */
static inline fx_t fx_div_hw(fx_t a, fx_t b) {
    int32_t  hi = a >> 16;               /* sign-extended high word of (a<<16) */
    uint32_t lo = (uint32_t)a << 16;
    int32_t  q;
    __asm__ __volatile__ (
        "mov #-128, r1\n\t"
        "add r1, r1\n\t"                 /* r1 = 0xFFFFFF00 */
        "mov.l %1, @(0, r1)\n\t"         /* DVSR  = divisor          */
        "mov.l %2, @(16, r1)\n\t"        /* DVDNTH = dividend high    */
        "mov.l %3, @(20, r1)\n\t"        /* DVDNTL = dividend low → start */
        "mov.l @(20, r1), %0\n\t"        /* quotient (stalls if !done) */
        : "=r"(q) : "r"(b), "r"(hi), "r"(lo) : "r1"
    );
    return q;
}

/* Floor-standing cardboard cutouts. Each standup has a world position
 * and a facing direction. silhouette=1 renders as flat dark outline only
 * (the iconic "something is watching" Backrooms vibe) and disappears when
 * the player gets too close. */
typedef struct {
    fx_t x, y;
    uint8_t facing_angle;
    uint8_t silhouette;
    uint8_t kind;             /* sprite_defs[] index — the billboard's art/dims/base */
} standup_t;

/* Per-map cutouts. Was a const array of ONE — which meant the fixed map's
 * neanderthal stood in EVERY level (procgen, lobby, every community map) while
 * authored kind-2 decals were never wired in at all: they landed in decals[],
 * which only draws things pinned to a wall plane and never collides. So maps
 * showed a cutout nobody placed and ignored the ones they did. Now populated
 * per-map by the loaders, like lights. */
/* Free-standing objects a map may spawn. This is a HARD cap the loader
 * silently clips against (first-come by decal order), so a map whose standalone
 * decals exceed it loses the tail — the testbed's 7 desks vanished behind 3
 * neanderthals + 21 chairs at the old 24. tools/lint_maps.py enforces the same
 * number via registry limits.max_standups so that failure is caught at build
 * time now, not by squinting at the map. Costs ~10 B of .bss and ~14 B of
 * render stack per slot (order[]/d2[] are stack arrays in the hot path). */
#define MAX_STANDUPS 36
static standup_t standups[MAX_STANDUPS];
static int       num_standups = 0;
/* The fixed map's original cutout, installed by raycast_load_fixed only. */
static const standup_t fixed_standups[] = {
    /* Neanderthal ~5 cells north of spawn, pulled west to hug the col-15
     * flat wall (x=16 face) so it stands against the wall and leaves the east
     * side of the col-16 corridor walkable. Solid (collides), the "iconic
     * Backrooms cardboard cutout" moment. Audio via the Voyager hello loop. */
    { FX(16.3), FX(23.5), 64,  0, 2 },   /* kind 2 = neanderthal */
};
/* Shove state, parallel to standups[]. A cutout you've walked into is ARMED;
 * a second, deliberate push in that direction tips it over (see player_update).
 * uint8 each — the secondary reads standups[] for drawing, so both ride the
 * sprite-cache purge. */
static uint8_t standup_armed[MAX_STANDUPS];
static uint8_t standup_down[MAX_STANDUPS];
/* Direction a toppled cutout lies, base->tip (engine angle). Set at the shove
 * to the player's facing, so it falls away from the camera. Read by the
 * secondary for the fallen-flat draw, so it rides the sprite-cache purge. */
static uint8_t standup_fall_dir[MAX_STANDUPS];
/* Tip-over animation progress, 0..STANDUP_FALL_MAX. 0 while standing; set to 1
 * at the shove and advanced one step per frame. Below MAX it renders as a rigid
 * panel hinged at the feet, rotating down; at MAX it hands off to the flat
 * floor bitmap. Read by the secondary for its draw half, so it rides the purge. */
#define STANDUP_FALL_MAX 5
/* hero_dying ramp per frame (0..255). 6 => ~42 frames ~= 2.5s at ~15fps for the
 * full speed-up / reverse / fade tape-death of the Voyager hello. */
#define HERO_DYING_RATE  6
static uint8_t standup_fall_prog[MAX_STANDUPS];
/* Power state of a screen-bearing box model (the PVM): 1 = on, screen shows
 * live static; 0 = off, dark glass. Toggled by raycast_pvm_use (the A
 * interaction). Read by the secondary for its draw half, so it rides the
 * sprite-cache purge. Default ON at map load — a wall of dead TVs is a
 * choice someone has to make, not a boot state. */
static uint8_t standup_power[MAX_STANDUPS];
/* Screen CONTENT of a powered PVM: 0 = live static, 1 = the EXIT TELEGRAPH
 * (the tiny POV frame captured from the level's exit at map load). Flips on
 * every power-ON, so cycling the set with A alternates the two — the A
 * button is the whole interface. Read by the secondary for its draw half,
 * so it rides the sprite-cache purge. */
static uint8_t standup_scr_mode[MAX_STANDUPS];
/* CRT BLOOM: frame_count stamp of the last power-ON. For BLOOM_FRAMES
 * rendered frames the glass plays the tube strike: a white line flares
 * at center (STRIKE_FRAMES), then the picture/static unfolds vertically
 * from it — the image sampled through an animated reciprocal so it
 * stretches out of the line like a warming CRT. Progress derives from
 * SHARED_UC->frame_count (both CPUs read the same clock: no per-CPU
 * counters to race, no decrement to double-run). */
static uint16_t standup_bloom_start[MAX_STANDUPS];
/* 1 = this PVM sits on a desk (the composite desk_pvm box model draws in
 * place of the floor stand; same kind, so interact/power/bloom/telegraph
 * all apply unchanged). Set by procgen right after add_standup. */
static uint8_t standup_on_desk[MAX_STANDUPS];
#define BLOOM_FRAMES  3   /* near the floor: 1 strike + 2 unfold; below this it's just a pop */
#define STRIKE_FRAMES 1
#define OFF_FRAMES    2   /* power-off: collapse frame + falling-line frame */
#define ON_SETTLE     2   /* after the unfold: 1 stable frame, 1 dark drop,
                           * then steady — the degauss catching the picture,
                           * losing it, catching it for good (Mike's call) */
/* Which side hit the floor: 1 = pushed from the FRONT, figure lands face up
 * (the printed side shows); 0 = pushed from BEHIND, lands face down — the
 * blank cardboard back shows, mirrored. Set at the shove from the same
 * front/back dot test the standing billboard uses. */
static uint8_t standup_fall_face[MAX_STANDUPS];
/* Fallen body length along the floor, in 1/64 world units (so 0.9 -> 57). Set at
 * the shove to min(reach, 0.9): a body falling toward a partition stops AT it
 * instead of clipping through (partitions aren't in the WALL_DIST z-buffer, so
 * the flat renderer can't z-cull them). 0 = "not set" -> full 0.9. Read by the
 * secondary for its draw half, so it rides the sprite-cache purge. */
static uint8_t standup_fall_len_q[MAX_STANDUPS];
static inline fx_t standup_body_len(int i) {
    uint8_t q = standup_fall_len_q[i];
    return q ? ((fx_t)q << (FX_SHIFT - 6)) : FX(0.9);
}
void standups_clear(void) {
    num_standups = 0;
    for (int i = 0; i < MAX_STANDUPS; i++) {
        standup_armed[i] = 0; standup_down[i] = 0;
        standup_fall_dir[i] = 0; standup_fall_prog[i] = 0;
        standup_fall_face[i] = 1;
        standup_fall_len_q[i] = 0;
        /* Found DEAD (Mike's call, reversing the old boot-state note): a
         * room of running TVs nobody started was the wrong kind of wrong.
         * The A press is what wakes one — and the first wake shows STATIC
         * (mode 0); the telegraph is the second cycle's reveal. */
        standup_power[i] = 0;
        standup_scr_mode[i] = 0;
        standup_bloom_start[i] = (uint16_t)0x8000;   /* far in the past */
        standup_on_desk[i] = 0;
    }
}

/* Body length of a toppled cutout, along the floor. Standing height is shorter
 * (STANDUP quad, 2/3 unit) but a fallen body reads better at ~a person's length;
 * the old billboard used the same 0.9 for its fall, so the brief length "pop" at
 * the start of the fall is unchanged. */
#define STANDUP_FALL_LEN FX(0.9)

/* Standing-billboard word-pair LOD: a figure taller than this (px) is close
 * enough that it dominates the sprite pass, so its fill drops to 2px blocks via
 * word stores (halves the FB writes). ~100px = within ~1.5 cells, where the
 * figure fills most of the screen height and the blocking is invisible. */
#define STANDUP_LOD_H  100

/* Distance from (bx,by) along engine-angle `dir` to the nearest solid cell,
 * capped at `maxd`. Marched in 1/8-cell steps (<=8 for a 0.9 body). Lets a
 * toppling cutout lean against a wall instead of half-sinking into it. */
extern uint8_t pedge_cell[MAP_H][MAP_W];   /* defined below: cell touches a partition */
static fx_t standup_wall_reach(fx_t bx, fx_t by, uint8_t dir, fx_t maxd) {
    fx_t dx = COS_FX(dir), dy = SIN_FX(dir);
    int bmx = (int)(bx >> FX_SHIFT), bmy = (int)(by >> FX_SHIFT);   /* his own cell */
    for (fx_t t = FX(0.125); t <= maxd; t += FX(0.125)) {
        int mx = (bx + FX_MUL(dx, t)) >> FX_SHIFT;
        int my = (by + FX_MUL(dy, t)) >> FX_SHIFT;
        if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) return t;
        if (world_map[my][mx] != 0) return t;
        /* Partitions are free-standing slabs, not world_map cells — lean against
         * one instead of clipping under it. But ONLY once the march leaves his own
         * cell: pedge_cell is true if the cell merely TOUCHES a partition on any
         * edge, so a slab beside his feet (not in his fall path) would otherwise
         * pin him upright and he'd never topple. */
        if (g_pedge_any && (mx != bmx || my != bmy) && pedge_cell[my][mx]) return t;
    }
    return maxd;
}

/* Largest tip angle (0..64) whose head reach L*sin(a) still clears `reach`.
 * SIN_FX is monotonic over the quarter turn, so break on the first miss. */
static uint8_t standup_lean_angle(fx_t reach, fx_t L) {
    if (reach >= L) return 64;
    uint8_t a = 0;
    for (uint8_t k = 1; k <= 64; k++) {
        if (FX_MUL(L, SIN_FX(k)) <= reach) a = k; else break;
    }
    return a;
}

/* Append a free-standing object (neanderthal kind 2 / chair kind 3) at a world
 * position, UPRIGHT (down/fall state already cleared by standups_clear). Used
 * by procgen, which authors its own instead of inheriting the previous map's. */
/* Does any standup already occupy grid cell (cx,cy)? Procgen's spawn guard:
 * two assets in one cell interpenetrate. Deliberately NOT enforced inside
 * raycast_add_standup — hand-authored maps keep the right to overlap (the
 * banked merged-furniture set-dressing feature). */
int raycast_standup_in_cell(int cx, int cy) {
    for (int i = 0; i < num_standups; i++)
        if ((int)(standups[i].x >> FX_SHIFT) == cx
         && (int)(standups[i].y >> FX_SHIFT) == cy)
            return 1;
    return 0;
}

void raycast_standup_make_desk(void) {
    /* Procgen calls this right after add_standup: the newest standup (a
     * PVM) becomes the desk-mounted composite. */
    if (num_standups > 0) standup_on_desk[num_standups - 1] = 1;
}

void raycast_add_standup(fx_t x, fx_t y, uint8_t facing, uint8_t kind) {
    if (num_standups >= MAX_STANDUPS) return;
    standups[num_standups].x            = x;
    standups[num_standups].y            = y;
    standups[num_standups].facing_angle = facing;
    standups[num_standups].silhouette   = 0;
    standups[num_standups].kind         = kind;
    num_standups++;
}

/* Procgen dark rooms: it can't see the full cm_dark_t (custom_maps.h isn't in
 * its include set), so it appends through here. The array is engine-owned and
 * g_map_dark is pointed at it. */
#define PROCGEN_DARK_MAX 6   /* enclosed + crawl + hallway guaranteed, plus headroom */
static cm_dark_t procgen_dark[PROCGEN_DARK_MAX];
void raycast_add_dark_room(int x0, int y0, int x1, int y1) {
    if (g_map_dark != procgen_dark) { g_map_dark = procgen_dark; g_map_n_dark = 0; }
    if (g_map_n_dark >= PROCGEN_DARK_MAX) return;
    procgen_dark[g_map_n_dark] = (cm_dark_t){ (uint8_t)x0, (uint8_t)y0,
                                              (uint8_t)x1, (uint8_t)y1 };
    g_map_n_dark++;
}

/* Wall-mounted decals (currently just the lobby outlet): small billboards
 * anchored at a height fraction z (0=floor, 1=ceiling) instead of the floor.
 * Populated per-map — raycast_load_lobby sets one; load_fixed/procgen clear
 * num_decals so the outlet only shows in the lobby. z is the plate CENTRE. */
/* axis: which wall the plate lies flat on. 1 = wall runs along X (plate spans
 * X, normal +/-Y, e.g. the lobby entrance wall); 0 = wall runs along Y (plate
 * spans Y, normal +/-X, e.g. a side-corridor wall). Drives the foreshortened,
 * wall-flat projection in draw_decals so the plate doesn't pivot to face the
 * camera like a billboard. */
/* kind: 0 = outlet plate, 1 = the metal fire DOOR (full-height, floor-anchored,
 * transparent surround so the EXIT sign sits on the wall). Same wall-flat,
 * foreshortened render — it stays put on the wall instead of swivelling. */
typedef struct { fx_t x, y, z; uint8_t axis; uint8_t kind; } decal_t;
#define DECAL_OUTLET_H  FX(0.098)  /* outlet plate height as a fraction of wall (30% smaller) */
#define DECAL_OUTLET_HW FX(0.031)  /* half the plate's world width; it lies flat on its wall */
/* Door: H 0.98 of wall centred at z 0.49 -> foot on the floor, head near the
 * ceiling (the EXIT sign pokes up). HW 0.24 -> ~0.48 cell wide, so the 128x256
 * texture renders at its true ~1:2 door proportions instead of a square. */
#define DECAL_DOOR_H   FX(0.98)
#define DECAL_DOOR_HW  FX(0.24)
/* Depth of the door recess's wood reveal (the visible jamb): the first slice
 * of cavity thickness apertures at DOOR width before flaring to the cell's
 * full interior. ~a real jamb's wall-thickness; 0.25 read as a tunnel. */
#define DOOR_REVEAL_D  FX(0.10)
#define DECAL_DOOR_Z   FX(0.49)
/* Door swing: 0 = closed, DOOR_OPEN_MAX = fully open (edge-on, all void). The
 * leaf's visible width is driven LINEARLY off this counter (not cos of an angle)
 * so each frame moves the same amount — even motion instead of a cos end-snap.
 * 64 steps eased at DOOR_OPEN_STEP/frame ≈ 1.5 s open at ~14 fps. */
#define DOOR_OPEN_MAX   64
#define DOOR_OPEN_STEP  12    /* ~0.4s open/close at ~14fps (2x faster again) */
#define RECESS_SHADE_BASE 4   /* base dimness inside the open door's recess;
                               * door_shade (distance) adds on top */
static int g_door_open = 0, g_door_target = 0;
/* Leaf rectangle inside the door texture (column-major door_tex[x][y]). Only
 * this slab swings; the EXIT sign and the wall surround outside it stay static.
 * The exact bounds are computed by bake_door.py from the door pixels and emitted
 * into door_tex.h as DOOR_LEAF_*, so the animated region always matches the art. */
#define LEAF_X0  DOOR_LEAF_X0
#define LEAF_X1  DOOR_LEAF_X1
#define LEAF_Y0  DOOR_LEAF_Y0
#define LEAF_Y1  DOOR_LEAF_Y1
decal_t decals[16];
int     num_decals = 0;

/* Free-standing wallpaper partitions ("fake walls") — AUTHORING buffers
 * only. The lobby/fixed/procgen loaders describe partitions as world-space
 * segments here, then raycast_stamp_partition_edges() rasterizes them into
 * the pedge_w/pedge_n cell-edge flags that actually render and collide
 * (custom maps skip this entirely — the codegen pre-rasterizes).
 * partition_t / NUM_PARTITIONS_MAX are declared in raycast.h. */
/* ── First-class SLAB partitions ─────────────────────────────────────────
 * A partition is a THIN SLAB (2*PART_HALF_THICK cells thick) centered on a
 * CELL-EDGE line, built from one panel per cell — a row of individual
 * square panels, closed at the run's ends with real cap faces, interior
 * never rendered. Flags live on the edges: pedge_w[y][x] = a slab on the
 * vertical line x (the WEST edge of cell x), pedge_n[y][x] = one on the
 * horizontal line y. The DDA resolves them with no per-column math:
 *   side faces — crossing a flagged line, the near face sits HALF_THICK
 *     before it along the crossing axis: t_face = t_line - HALF*deltaDist
 *     (deltaDist is the walk's own per-axis step — no divides);
 *   end caps — entering a cell whose perpendicular edge is flagged within
 *     HALF_THICK of the crossing point, the cap face IS that crossing.
 * Flags use the cm_pedge_t encoding (custom_maps.h). Populated per map
 * load; the secondary purges with the same gen-gated path as world_map. */
#define PART_HALF_THICK FX(0.05)   /* slab = 0.1 cells thick — real office-partition proportions */
/* Per-flag slab geometry. fo = offset of the slab's LOW face behind the
 * line: centered = HALF, FLUSH_LO = 0 (band [line, line+2H]), FLUSH_HI =
 * 2*HALF (band [line-2H, line]). Pullback to the near face for a crossing
 * is fo (traveling +) or 2H-fo (traveling -); both are 0/1x/2x the
 * per-column pb constant, so selection needs no multiplies. */
#define SLAB_FO(f) (((f) & CM_PEDGE_FLUSH_LO) ? 0 : \
                    ((f) & CM_PEDGE_FLUSH_HI) ? (PART_HALF_THICK * 2) : PART_HALF_THICK)
#define SLAB_PULL(f, pb, step_pos) \
    (((f) & CM_PEDGE_FLUSH_LO) ? ((step_pos) ? 0 : ((pb) << 1)) : \
     ((f) & CM_PEDGE_FLUSH_HI) ? ((step_pos) ? ((pb) << 1) : 0) : (pb))
uint8_t pedge_w[MAP_H][MAP_W + 1];
uint8_t pedge_n[MAP_H + 1][MAP_W];
int g_pedge_any = 0;
/* Proximity gate: 1 where a DDA step must run the (expensive) slab side/cap/
 * recovery tests — i.e. cells within reach of a partition edge. Every map has
 * SOME partition, so g_pedge_any is ~always set; without this gate the DDA
 * ran the full partition math on every step of every column even in empty
 * views, taxing the whole game. The tests read edges up to ±1 cell away, so
 * marking a radius-2 halo around each edge can never skip a real hit. */
uint8_t pedge_cell[MAP_H][MAP_W];
/* Run-extent LUT: for each flagged edge cell, the [lo,hi] cell span of its
 * maximal same-flag run. The wall pass's fg gather needs the run ends for the
 * countertop end-clip; walking the run per contact per column measured
 * Q:2898 cells/frame at the partition-alley pose. Two byte reads replace the
 * walk when part_diag==1. Rebuilt by pedge_build_cells on every map load;
 * the secondary purges these with the same gen-gated path as the pedges. */
uint8_t prun_lo_w[MAP_H][MAP_W + 1], prun_hi_w[MAP_H][MAP_W + 1];
uint8_t prun_lo_n[MAP_H + 1][MAP_W], prun_hi_n[MAP_H + 1][MAP_W];
void pedge_build_cells(void);
volatile uint16_t prof_dda_steps = 0;   /* primary half: DDA steps walked/frame */
volatile uint16_t prof_dda_fat   = 0;   /* primary half: steps in the slab-gate path */
/* Partition-campaign counters (primary half, per frame) — where do the wall
 * pass's ticks actually go? Kept vs promoted vs overlay columns + the
 * run-extent walk, which re-scans the whole run per contact per column. */
volatile uint16_t prof_efg_kept  = 0;   /* partial slab contacts kept (fg slots) */
volatile uint16_t prof_runwalk   = 0;   /* cells walked in run-extent scans */
volatile uint16_t prof_ovl_cols  = 0;   /* columns that took the overlay path */
volatile uint16_t prof_promote_cols = 0; /* columns promoted to the fast main path */
volatile uint16_t prof_pass_ovl  = 0;   /* primary half: see-over overlay ticks/frame */
volatile uint16_t prof_ovl_px    = 0;   /* of prof_pass_ovl: just the pixel loops (HUD U);
                                         * O minus U = per-column setup (shade/divides) */
volatile uint16_t prof_split_col = 0;   /* adaptive split column (HUD K) */
volatile uint16_t prof_pass_chair = 0;  /* primary half: chair fill ticks/frame (HUD H) */
static inline uint16_t prof_frt_read(void);

/* Convert the authored partitions[] (integer, axis-aligned segments — the
 * working representation every loader already produces) into first-class
 * edges, then EMPTY the legacy arrays: after this call the map's dividers
 * render and collide purely through the DDA edge model. */
void raycast_stamp_partition_edges(void) {
    for (int i = 0; i < num_partitions; i++) {
        int x1 = FX_INT(partitions[i].x1), y1 = FX_INT(partitions[i].y1);
        int x2 = FX_INT(partitions[i].x2), y2 = FX_INT(partitions[i].y2);
        uint8_t hc = (partition_height[i] == 192) ? 1
                   : (partition_height[i] == 96)  ? 2 : 0;
        uint8_t ef = (uint8_t)(CM_PEDGE_PRESENT
                   | (partition_style[i] ? CM_PEDGE_SPOTTED : 0)
                   | (hc << 1));
        if (x1 == x2) {                       /* vertical: WEST edges on line x */
            int ya = y1 < y2 ? y1 : y2, yb = y1 > y2 ? y1 : y2;
            /* Collinear-wall flush: if the run shares its line with a wall
             * face, shift the slab so the faces align (no seam jog). */
            int ww = 0, we = 0;
            for (int y = ya - 1; y <= yb; y++) {   /* incl. one past each end */
                int cw, ce;
                if ((unsigned)y >= (unsigned)MAP_H) continue;
                cw = (x1 > 0     && world_map[y][x1 - 1]);
                ce = (x1 < MAP_W && world_map[y][x1]);
                if (cw && ce) continue;            /* perpendicular tee */
                if (cw) ww = 1;
                if (ce) we = 1;
            }
            /* Slab on the side OPPOSITE the wall, face on the line. */
            if (ww && !we)      ef |= CM_PEDGE_FLUSH_LO;   /* wall west -> slab east */
            else if (we && !ww) ef |= CM_PEDGE_FLUSH_HI;   /* wall east -> slab west */
            if (x1 >= 0 && x1 <= MAP_W)
                for (int y = ya; y < yb; y++)
                    if ((unsigned)y < (unsigned)MAP_H) pedge_w[y][x1] = ef;
        } else if (y1 == y2) {                /* horizontal: NORTH edges on line y */
            int xa = x1 < x2 ? x1 : x2, xb = x1 > x2 ? x1 : x2;
            int wn = 0, ws = 0;
            for (int x = xa - 1; x <= xb; x++) {   /* incl. one past each end */
                int cn, cs;
                if ((unsigned)x >= (unsigned)MAP_W) continue;
                cn = (y1 > 0     && world_map[y1 - 1][x]);
                cs = (y1 < MAP_H && world_map[y1][x]);
                if (cn && cs) continue;
                if (cn) wn = 1;
                if (cs) ws = 1;
            }
            if (wn && !ws)      ef |= CM_PEDGE_FLUSH_LO;   /* wall north -> slab south */
            else if (ws && !wn) ef |= CM_PEDGE_FLUSH_HI;   /* wall south -> slab north */
            if (y1 >= 0 && y1 <= MAP_H)
                for (int x = xa; x < xb; x++)
                    if ((unsigned)x < (unsigned)MAP_W) pedge_n[y1][x] = ef | CM_PEDGE_AXIS_N;
        }
        g_pedge_any = 1;
    }
    /* Transitive flush: a centered run collinear with a flushed run takes the
     * same shift, so faces along one line never jog at the gaps between runs
     * (and both meet the wall corner the flushed run aligned to). */
    for (int x = 0; x <= MAP_W; x++) {          /* vertical lines */
        uint8_t fl = 0;
        for (int y = 0; y < MAP_H; y++)
            if ((pedge_w[y][x] & CM_PEDGE_PRESENT) &&
                (pedge_w[y][x] & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI)))
                fl = pedge_w[y][x] & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI);
        if (fl)
            for (int y = 0; y < MAP_H; y++)
                if ((pedge_w[y][x] & CM_PEDGE_PRESENT) &&
                    !(pedge_w[y][x] & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI)))
                    pedge_w[y][x] |= fl;
    }
    for (int y = 0; y <= MAP_H; y++) {          /* horizontal lines */
        uint8_t fl = 0;
        for (int x = 0; x < MAP_W; x++)
            if ((pedge_n[y][x] & CM_PEDGE_PRESENT) &&
                (pedge_n[y][x] & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI)))
                fl = pedge_n[y][x] & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI);
        if (fl)
            for (int x = 0; x < MAP_W; x++)
                if ((pedge_n[y][x] & CM_PEDGE_PRESENT) &&
                    !(pedge_n[y][x] & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI)))
                    pedge_n[y][x] |= fl;
    }
    num_partitions = 0;
    pedge_build_cells();
}

void pedge_clear(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x <= MAP_W; x++) pedge_w[y][x] = 0;
    for (int y = 0; y <= MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) pedge_n[y][x] = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) pedge_cell[y][x] = 0;
    g_pedge_any = 0;
}

/* Directional slab gate. Per cell, 8 bits keyed by the crossing that ENTERS
 * it: gb = (side<<1)|positive_step. Low nibble (1<<gb): the crossed line is
 * flagged — run the 1-byte SIDE test. High nibble (16<<gb): an end/junction
 * event is POSSIBLE for exactly that crossing (a flagged neighbor along the
 * crossed line, or a one-sided ENTERED-only cap candidate on a perpendicular
 * line) — only then run the fat recovery + cap tests. The old radius halos
 * put ~88%% of DDA steps near counters on the fat path (short runs are all
 * ends; the end halos blanketed them). Direction-exact bits cut fat entries
 * to ~3%% with bit-identical output: 221,184-ray sim sweep, 0 mismatches. */
static uint8_t pw_at(int x, int y) {
    return ((unsigned)y < (unsigned)MAP_H && (unsigned)x <= (unsigned)MAP_W)
         ? pedge_w[y][x] : 0;
}
static uint8_t pn_at(int x, int y) {
    return ((unsigned)y <= (unsigned)MAP_H && (unsigned)x < (unsigned)MAP_W)
         ? pedge_n[y][x] : 0;
}
void pedge_build_cells(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) pedge_cell[y][x] = 0;
    if (!g_pedge_any) return;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            uint8_t m = 0;
            #define GP(e) ((e) & CM_PEDGE_PRESENT)
            /* gb=1: X crossing heading +X — crossed line x (west edge) */
            if (GP(pw_at(x, y))) m |= 1 << 1;
            if (GP(pw_at(x, y - 1) | pw_at(x, y + 1)) ||
                (GP(pn_at(x, y))     && !GP(pn_at(x - 1, y))) ||
                (GP(pn_at(x, y + 1)) && !GP(pn_at(x - 1, y + 1))))
                m |= 16 << 1;
            /* gb=0: heading -X — crossed line x+1 */
            if (GP(pw_at(x + 1, y))) m |= 1 << 0;
            if (GP(pw_at(x + 1, y - 1) | pw_at(x + 1, y + 1)) ||
                (GP(pn_at(x, y))     && !GP(pn_at(x + 1, y))) ||
                (GP(pn_at(x, y + 1)) && !GP(pn_at(x + 1, y + 1))))
                m |= 16 << 0;
            /* gb=3: Y crossing heading +Y — crossed line y (north edge) */
            if (GP(pn_at(x, y))) m |= 1 << 3;
            if (GP(pn_at(x - 1, y) | pn_at(x + 1, y)) ||
                (GP(pw_at(x, y))     && !GP(pw_at(x, y - 1))) ||
                (GP(pw_at(x + 1, y)) && !GP(pw_at(x + 1, y - 1))))
                m |= 16 << 3;
            /* gb=2: heading -Y — crossed line y+1 */
            if (GP(pn_at(x, y + 1))) m |= 1 << 2;
            if (GP(pn_at(x - 1, y + 1) | pn_at(x + 1, y + 1)) ||
                (GP(pw_at(x, y))     && !GP(pw_at(x, y + 1))) ||
                (GP(pw_at(x + 1, y)) && !GP(pw_at(x + 1, y + 1))))
                m |= 16 << 2;
            #undef GP
            pedge_cell[y][x] = m;
        }
    }

    /* Run-extent LUT (see prun_* decls): group maximal same-flag runs once,
     * stamping every member cell with the run's [lo,hi]. Exact-equality
     * grouping matches the wall pass's old per-column walk (== ef). */
    for (int x = 0; x <= MAP_W; x++)
        for (int y = 0; y < MAP_H; ) {
            uint8_t f = pedge_w[y][x];
            if (!(f & CM_PEDGE_PRESENT)) { y++; continue; }
            int y2 = y;
            while (y2 + 1 < MAP_H && pedge_w[y2 + 1][x] == f) y2++;
            for (int k = y; k <= y2; k++) {
                prun_lo_w[k][x] = (uint8_t)y;
                prun_hi_w[k][x] = (uint8_t)y2;
            }
            /* GLANCING GATE: a shallow ray pierces the face up to
             * HALF_THICK/tan(theta) cells past the run end before crossing
             * the centerline, but the crossing gate only reached +/-1 cell —
             * the missed-column artifact the span experiment exposed. Arm the
             * fat bit K=4 cells beyond each end so the recovery (which now
             * indexes the exact pierce cell) can reach it: covers angles
             * down to ~0.7 degrees. */
            for (int e = 1; e <= 4; e++) {
                int ya = y - e, yb = y2 + e;
                if (ya >= 0) {
                    if (x < MAP_W) pedge_cell[ya][x]     |= 16 << 1;
                    if (x > 0)     pedge_cell[ya][x - 1] |= 16 << 0;
                }
                if (yb < MAP_H) {
                    if (x < MAP_W) pedge_cell[yb][x]     |= 16 << 1;
                    if (x > 0)     pedge_cell[yb][x - 1] |= 16 << 0;
                }
            }
            y = y2 + 1;
        }
    for (int y = 0; y <= MAP_H; y++)
        for (int x = 0; x < MAP_W; ) {
            uint8_t f = pedge_n[y][x];
            if (!(f & CM_PEDGE_PRESENT)) { x++; continue; }
            int x2 = x;
            while (x2 + 1 < MAP_W && pedge_n[y][x2 + 1] == f) x2++;
            for (int k = x; k <= x2; k++) {
                prun_lo_n[y][k] = (uint8_t)x;
                prun_hi_n[y][k] = (uint8_t)x2;
            }
            for (int e = 1; e <= 4; e++) {   /* glancing gate, horizontal twin */
                int xa = x - e, xb = x2 + e;
                if (xa >= 0) {
                    if (y < MAP_H) pedge_cell[y][xa]     |= 16 << 3;
                    if (y > 0)     pedge_cell[y - 1][xa] |= 16 << 2;
                }
                if (xb < MAP_W) {
                    if (y < MAP_H) pedge_cell[y][xb]     |= 16 << 3;
                    if (y > 0)     pedge_cell[y - 1][xb] |= 16 << 2;
                }
            }
            x = x2 + 1;
        }
}

partition_t partitions[NUM_PARTITIONS_MAX] = {
    /* SE lounge — 4-cell partition along Y=22. */
    { FX(22), FX(22), FX(26), FX(22) },
    /* Central band — 3-cell partition along X=20. */
    { FX(20), FX(11), FX(20), FX(14) },
};
int num_partitions = 2;
/* Per-partition wallpaper: 0 = chevron (like the main walls), 1 = spotted
 * olive divider. Indexed alongside partitions[]; set per-map. */
uint8_t partition_style[NUM_PARTITIONS_MAX] = {0};
/* Per-partition render height: 0 = full ceiling-to-floor (default), else a
 * fraction*256 (e.g. 192 = 3/4) for a low cubicle-style divider anchored at
 * the floor — the ceiling shows above it. Matches the HobbyTown reference's
 * low office partitions. */
uint8_t partition_height[NUM_PARTITIONS_MAX] = {0};
/* Eye-height below which the body fits through low openings (crawlspaces).
 * The eye eases to CROUCH_EYE(=40) crouched, STAND_EYE(=128) standing. */
#define CRAWL_PASS_EYE 100
/* Bulkheads (doorway headers/soffits) sit far higher than a duct slab, so
 * they only ever asked for a dipped head — but one global pass height made
 * them demand the full A+B crawl, which reads as the architecture fighting
 * you. Headers now clear on a duck (see DUCK_EYE); duct slabs keep the
 * deliberate crawl. Must stay ABOVE DUCK_EYE so a completed duck fits. */
#define BULKHEAD_PASS_EYE 96
#define NUM_PARTITIONS num_partitions
#define NUM_STANDUPS num_standups

/* Illuminated drop-ceiling panels (the Backrooms iconic recessed
 * fluorescent panels). Positions are scattered across the map rather
 * than placed at every cell — that scattered "some tiles are lit,
 * most aren't" pattern is what makes the lobby reference photo read
 * as actual drop-ceiling lighting rather than "lights everywhere". */
typedef struct { fx_t x, y; } light_t;

/* Grid of recessed fluorescent fixtures populated by init_lights() at
 * boot — one panel every 2 cells in both axes, skipping cells that
 * are inside walls. Matches the regular cadence of the Sketchfab
 * Backrooms reference where the drop ceiling holds a fixture roughly
 * every other panel run. 200 slots is enough for the densest possible
 * 32×32 walkable map (~250 cells / 4 = ~62 fixtures in practice). */
/* 200 was quietly binding on EVERYTHING: the procedural every-other-cell grid
 * alone wants 225 on a 32x32, so procgen maps were silently losing their last
 * 25 fixtures, and a community map came in at 468. 512 covers both. The cost
 * is per-frame iteration in draw_lights, so the purge below now touches only
 * the live entries instead of the whole array — see raycast_purge_sprite_cache. */
#define MAX_LIGHTS 512
static light_t lights[MAX_LIGHTS];
static int num_lights = 0;
#define NUM_LIGHTS num_lights
/* When set, init_lights uses the lobby's hand-authored fluorescent runs
 * instead of the default every-other-cell auto-grid. Set per-map. */
int g_lobby_ceiling = 0;

/* Per-cell ceiling height (fraction*256 of the wall): 256 = full open ceiling,
 * CRAWL_CEIL_H = a low crawlspace slab. This is the first-class data model —
 * ceiling height is a property of every cell, like walkable/wall is. Any run of
 * low cells is a crawlspace; collision, forced-crouch, light culling and the
 * slab render all read this array, so crawlspaces can be placed anywhere (and
 * by procgen) without special-casing a single zone. */
/* ONE primitive: a per-cell lowered ceiling. ANY lowered cell forces a crouch
 * (uniform rule -- the walk-under "bulkhead" category was collapsed back into
 * crawlspace: two names for one thing, and the gameplay split wasn't worth a
 * second rule). Heights differ only VISUALLY; the named values live in
 * raycast.h (CRAWL_CEIL_H / BULKHEAD_CEIL_H) beside ceil_h_add_run_h. */
uint8_t ceil_h[MAP_H][MAP_W];      /* extern: procgen/loaders author it */
#define CEIL_H(y,x) (((volatile uint8_t *)((uintptr_t)ceil_h | 0x20000000))[(y)*MAP_W + (x)])

/* Render-reject bbox over all low cells + an "any low ceiling present" flag.
 * The slab per-row reject uses the bbox; the bulkhead pass needs PER-CRAWLSPACE
 * rects (a single union bbox caps the wrong faces when crawlspaces are
 * disjoint), so each low-ceiling run is recorded as its own rect. */
int   g_lowceil_active = 0;
/* Distinct slab heights present in this map, one render pass-pair each. Small
 * on purpose (heights are a named set in registry.json); overflow beyond the
 * cap falls back to CRAWL height at authoring time via lint, never silently. */
#define MAX_CEIL_HS 3
static uint8_t ceil_hs[MAX_CEIL_HS];
static int     n_ceil_hs = 0;
/* Replicate a palette byte into both lanes of a 16-bit word for the half-res
 * wall word-stores. The toggle itself lives in SHARED_UC->wall_halfres (both
 * CPUs run draw_walls, so it must be cache-through coherent). */
#define WDUP(c) ((uint16_t)(((uint16_t)(uint8_t)(c) << 8) | (uint8_t)(c)))
/* Replicate a palette byte across all 4 lanes of a 32-bit word for the QUARTER-res
 * wall path (4px per computed column). Only the main wall fill uses it; the store
 * is always 4-aligned there (col is a multiple of 4 in quarter mode and SCREEN_W
 * is a multiple of 4). Kept lean — NOT applied at the overlay/embed sites, which
 * stay half-res in quarter mode (a motion-only mode, so the mismatch is masked). */
#define LDUP(c) ((uint32_t)((uint8_t)(c)) * 0x01010101u)
static fx_t g_lowceil_x0, g_lowceil_y0, g_lowceil_x1, g_lowceil_y1;
#define MAX_LOWCEIL_RECTS 8
static fx_t lowceil_rect[MAX_LOWCEIL_RECTS][4];   /* {x0,y0,x1,y1} world coords */
/* Each rect's slab height (CRAWL_CEIL_H / BULKHEAD_CEIL_H). The slab + cap
 * passes run once PER HEIGHT and must touch only their own rects -- an untagged
 * list capped crawl rects at bulkhead height and vice versa (phantom bright
 * bands), and a merged union bbox made every pass scan every zone (the F:05
 * two-pass slideshow). */
static uint8_t lowceil_rect_h[MAX_LOWCEIL_RECTS];
static int  n_lowceil_rect = 0;

/* Reset every cell to full-height open ceiling (called by each map loader).
 * 255 is "full" (u8 cap; the 1/256 difference is invisible). */

/* EXIT HOLE: the alternate way out — a dark opening carved into the CENTER of
 * a wall face (like a crawl mouth, but elevated: you pull up into it). Placed
 * by exit_place_common(want_hole=1) on the farthest reachable wall face, same
 * as the door. Drawn by draw_exit_hole in the tail pass (both CPUs — purged
 * with the lowceil set): dark void interior framed by a lit sill ledge and a
 * shadowed top reveal, both from TRUE near/far plane projections, so the
 * cavity has crawlspace-style depth. */
static int g_exit_hole_cx = -1, g_exit_hole_cy = -1;   /* the WALL cell */
static int g_exit_hole_ax, g_exit_hole_ay;             /* approach (open) cell */
static uint8_t g_exit_hole_axis;      /* 1: hole face is a y-plane (like decals) */
static fx_t g_exit_hole_plane;        /* the face plane's world coordinate */
static fx_t g_exit_hole_c0;           /* face center along the other axis */
static int  g_exit_hole_dir;          /* +1/-1: approach -> wall along the axis */
/* Opening geometry: half-width across the face, sill + head heights /256. */
#define HOLE_HW  FX(0.30)
#define HOLE_Z0  100                  /* sill: ~0.39 up the wall — must climb */
#define HOLE_Z1  212                  /* head: ~0.83 up the wall */
/* The wall's own cut THICKNESS at the left/right aperture edges — the vertical
 * jamb, exactly what DOOR_REVEAL_D is for the door. Head and sill always had
 * their reveals (the underside and the ledge); without this one the wallpaper
 * met the cavity on a hard line and the opening read as painted on. */
#define HOLE_REVEAL_D  FX(0.12)
/* The hole's own shade ramp: the wall ramp (luma 223..73) CONTINUED past its
 * floor, because a wall-ramp interior read mid-brown however hard the shade
 * was pushed — the hole looked lit. But a tail run down to near-black read
 * UNCANNY: a soft black blob behind a glowing frame, not a room. So the tail
 * is exactly ONE warm step past the wall floor — DOOR_DARK+2, luma 60, a dark
 * yellow-brown rather than a neutral dark. Depth here is a deep colour, not an
 * absence of one. Monotonic across the join (DOOR_DARK+3 at luma 84 is
 * skipped, being LIGHTER than the wall floor), so one fade runs from lit
 * wallpaper to the far dark with no palette seam. */
/* 18, not 17. There are 18 initializers below and HOLE_DARKEST is 17, so the
 * clamp in hole_shade() indexed one past the end while the compiler silently
 * discarded the very entry it wanted ("excess elements in array initializer").
 * The hole's deepest band was taking its colour from whatever .rodata followed.
 * Found by the same warning that caught the font's '%' glyph. */
static const uint8_t hole_ramp[18] = {
    WALL_BASE + 0,  WALL_BASE + 1,  WALL_BASE + 2,  WALL_BASE + 3,
    WALL_BASE + 4,  WALL_BASE + 5,  WALL_BASE + 6,  WALL_BASE + 7,
    WALL_BASE + 8,  WALL_BASE + 9,  WALL_BASE + 10, WALL_BASE + 11,
    WALL_BASE + 12, WALL_BASE + 13, WALL_BASE + 14, WALL_BASE + 15,
    HOLE_DEEP_BASE + 2, HOLE_DEEP_BASE + 3,
};
#define HOLE_DARKEST 17
/* Dither policy, third iteration and the keeper: the interior shades by the
 * world's fog law at each surface's ray distance (crawlspace endpoints),
 * carried in 8.8 so the value BETWEEN two fog bands survives -- and the 2x2
 * bayer resolves that in-between. Mixing therefore starts at the OUTER EDGE
 * where the hole begins and spreads evenly with depth: no floating front
 * (the eased-ramp-to-murk's failure), no hard contour bands (the pure
 * fog-step version's). FLATS still round -- the back panel in a sustained
 * mix is a print pattern. Blue noise's table is gone from the binary;
 * tools/gen_bluenoise.py regenerates it. */
static const uint8_t hole_bayer[4] = { 0, 128, 192, 64 };
static inline uint8_t hole_shade(int acc8, int bay) {
    int v = (acc8 + bay) >> 8;
    if (v < 0) v = 0; else if (v > HOLE_DARKEST) v = HOLE_DARKEST;
    return hole_ramp[v];
}
/* The wall pass's fog curve in 8.8 -- same knee, same span, fractional so
 * the dither has an in-between to resolve. Matches bsh's integer version
 * in draw_exit_hole; keep the two in step. */
static inline int hole_fog8(fx_t d) {
    if (d < FX(2.5)) return (int)((d * 512) / FX(2.5));
    if (d > FOG_RAMP_DIST) d = FOG_RAMP_DIST;
    return 512 + (int)(((d - FX(2.5)) * (13 * 256)) / (FOG_RAMP_DIST - FX(2.5)));
}
/* Side-panel vertical AO: the head above shadows the top of a side wall, the
 * sill below bounces into its foot. Depth is constant down a side-wall column,
 * so without this the panel is one flat value — which is the other half of why
 * it banded. Applied as ramp steps, top to bottom. */
#define AO_TOP   1     /* was 2: with the head band falling dark onto the same
                        * rows, 2 stacked into a drawn black line at the seam */
#define AO_BOT  (-1)
/* (HOLE_SIDE_SPAN is gone: the crawlspace law bounds a side panel's travel
 * naturally -- fog over one cell of depth is a couple of steps -- where the
 * old eased-ramp-to-murk needed an artificial cap to stay lit.) */

/* ── DESTINATION PEEK ───────────────────────────────────────────────────
 * The climb is a transition point: by the time the player commits, the next
 * map's seed is decided. climb_commit (m_main) generates that map, calls
 * raycast_peek_render for ONE low-res frame from its spawn POV, then rebuilds
 * the current map (deterministic loaders make that a byte-identical restore).
 * During the climb the back panel shows this bitmap scaled into the cavity --
 * the faint interior light resolving into an actual place -- so the level
 * change reads as continuous space instead of a loading fade.
 *
 * Walls-only, primary-CPU, 96x64: through a dark aperture the destination is
 * small, fogged and glimpsed, so the full pipeline (two CPUs, textures,
 * sprites, vblank flip discipline) would buy nothing and risk plenty.
 * Both CPUs BLIT the buffer in the tail pass; the secondary purges its lines
 * in raycast_purge_lowceil_cache, and the flag is purged there every frame. */
#define PEEK_W  96
#define PEEK_H  64
_Static_assert(PEEK_W * PEEK_H <= RAYCAST_PEEK_CLAIM,
               "peek grew past its exported hero_scratch claim — bump "
               "RAYCAST_PEEK_CLAIM (and mind the Voyager ring above it)");
#define PEEK_DIM 1                    /* extra fog step: glimpsed, not displayed */
/* SDRAM is packed to the byte (the stack overlay saw to that), so the bitmap
 * ALIASES the low end of the title's 71 KB hero_scratch (.hero_overlay) --
 * live only during the climb, while hero_scratch is live only at the title,
 * and both fill before every use. Low end = farthest from the stack's spill
 * zone at the overlay's top. */
extern uint8_t hero_scratch[];
#define peek_buf hero_scratch
#define PEEK_BYTES (PEEK_W * PEEK_H)
static int g_hole_peek_on = 0;
/* Which duct wall is LIT (captured at the threshold; see
 * raycast_corridor_orient). Read by the corridor, the preview, AND the
 * panel blit's mirror on both CPUs -- declared up here with the peek so
 * the per-tail purge below can reach it. */
static int g_corr_lit_left = 1;
void raycast_peek_clear(void) { g_hole_peek_on = 0; }

/* Generic tiny walls-only POV render — the peek's engine, parameterized so
 * the PVM exit-telegraph can capture at screen-glass resolution. W/H up to
 * 96x64 (the row-shade scratch). dim = extra fog steps. */
static void pov_render(uint8_t *buf, int W, int H, fx_t px, fx_t py,
                       uint8_t angle, int dim) {
    fx_t dirX = COS_FX(angle), dirY = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66)), planeY = FX_MUL(dirX, FX(0.66));
    /* Floor/ceiling per-row shade: perspective fog toward the horizon, so
     * the ground plane recedes instead of blurring into one brown slab.
     * Row distance ~ (eye height * H/2) / rows-from-horizon. */
    uint8_t rowc[64], rowf[64];
    for (int y = 0; y < H; y++) {
        int dy = y - (H >> 1); if (dy < 0) dy = -dy;
        if (dy < 1) dy = 1;
        fx_t d = ((fx_t)(H >> 1) << FX_SHIFT) / (dy * 2);   /* ~cells */
        int sh = (hole_fog8(d) >> 8) + dim;
        if (sh > 15) sh = 15;
        rowc[y] = (uint8_t)(CEIL_BASE + sh);
        rowf[y] = (uint8_t)(FLOOR_BASE + sh);
    }
    for (int col = 0; col < W; col++) {
        fx_t camX = ((fx_t)(2 * col - W) << FX_SHIFT) / W;
        fx_t rdx = dirX + FX_MUL(planeX, camX);
        fx_t rdy = dirY + FX_MUL(planeY, camX);
        fx_t perp = FX(8);            /* fallback: degenerate ray = far wall */
        int side = 0, hit = 1;
        fx_t ax = rdx < 0 ? -rdx : rdx, ay = rdy < 0 ? -rdy : rdy;
        if (ax > FX(0.02) || ay > FX(0.02)) {
            int mapX = FX_INT(px), mapY = FX_INT(py);
            fx_t dX = ax > FX(0.02) ? fx_div_hw(FX_ONE, ax) : FX(64);
            fx_t dY = ay > FX(0.02) ? fx_div_hw(FX_ONE, ay) : FX(64);
            int stepX = rdx < 0 ? -1 : 1, stepY = rdy < 0 ? -1 : 1;
            fx_t sdX = rdx < 0 ? FX_MUL(px - ((fx_t)mapX << FX_SHIFT), dX)
                               : FX_MUL(((fx_t)(mapX + 1) << FX_SHIFT) - px, dX);
            fx_t sdY = rdy < 0 ? FX_MUL(py - ((fx_t)mapY << FX_SHIFT), dY)
                               : FX_MUL(((fx_t)(mapY + 1) << FX_SHIFT) - py, dY);
            for (int n = 0; n < 48; n++) {
                if (sdX < sdY) { sdX += dX; mapX += stepX; side = 0; }
                else           { sdY += dY; mapY += stepY; side = 1; }
                if ((unsigned)mapX >= MAP_W || (unsigned)mapY >= MAP_H) break;
                hit = world_map[mapY][mapX];
                if (hit) {
                    perp = side ? sdY - dY : sdX - dX;
                    break;
                }
            }
            if (perp < FX(0.05)) perp = FX(0.05);
            if (perp > FX(12))   perp = FX(12);
        }
        /* Same fog curve as the hole's face shade, plus the peek dim. */
        int sh;
        if (perp < FX(2.5)) sh = (int)((perp * 2) / FX(2.5));
        else { fx_t past = perp - FX(2.5);
               sh = 2 + (int)((past * 13) / (FOG_RAMP_DIST - FX(2.5))); }
        sh += dim + side;
        if (hit == 2) sh = 15;                   /* void doorway: pure fog-dark */
        if (sh > 15) sh = 15;
        int lh = (int)divu_u32((uint32_t)(H << FX_SHIFT), (uint32_t)perp);
        int top = (H - lh) >> 1, bot = (H + lh) >> 1;
        if (top < 0) top = 0;
        if (bot > H - 1) bot = H - 1;
        uint8_t cw = (uint8_t)(WALL_BASE + sh);
        uint8_t *d = buf + col;
        int y = 0;
        for (; y < top; y++, d += W) *d = rowc[y];
        for (; y <= bot; y++, d += W) *d = cw;
        for (; y < H; y++, d += W) *d = rowf[y];
    }
    purge_cache_range(buf, (unsigned)(W * H));   /* self-read is fresh; belt */
}

void raycast_peek_render(fx_t px, fx_t py, uint8_t angle) {
    pov_render(peek_buf, PEEK_W, PEEK_H, px, py, angle, PEEK_DIM);
    /* Nonzero = on, and it carries its birth frame so the blit can DISSOLVE
     * the peek in over the first few frames instead of popping. */
    g_hole_peek_on = (int)SHARED_UC->frame_count + 1;
}

void ceil_h_clear(void) {       /* CEIL_H_FULL lives in raycast.h (procgen reads it) */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) ceil_h[y][x] = CEIL_H_FULL;
    g_lowceil_active = 0;
    n_ceil_hs = 0;
    n_lowceil_rect = 0;
}

/* Mark a straight run of `len` cells from (cx,cy) along (dx,dy) as a low-ceiling
 * crawlspace, recording it as one rect (so its mouth gets capped correctly) and
 * growing the overall render bbox. dx,dy in {0,1}; len>=1. */
void ceil_h_add_run_h(int cx, int cy, int dx, int dy, int len, int h) {
    if (len < 1) return;
    {   /* record the height in the distinct-heights list (one pass-pair each) */
        int known = 0;
        for (int i = 0; i < n_ceil_hs; i++) if (ceil_hs[i] == (uint8_t)h) known = 1;
        if (!known) {
            if (n_ceil_hs < MAX_CEIL_HS) ceil_hs[n_ceil_hs++] = (uint8_t)h;
            else h = CRAWL_CEIL_H;   /* over the cap: render as classic crawl */
        }
    }
    for (int k = 0; k < len; k++) {
        int x = cx + dx * k, y = cy + dy * k;
        if ((unsigned)x < MAP_W && (unsigned)y < MAP_H) ceil_h[y][x] = (uint8_t)h;
    }
    fx_t x0 = FX(cx), y0 = FX(cy);
    fx_t x1 = FX(cx + dx * (len - 1) + 1), y1 = FX(cy + dy * (len - 1) + 1);
    if (n_lowceil_rect < MAX_LOWCEIL_RECTS) {
        lowceil_rect[n_lowceil_rect][0] = x0; lowceil_rect[n_lowceil_rect][1] = y0;
        lowceil_rect[n_lowceil_rect][2] = x1; lowceil_rect[n_lowceil_rect][3] = y1;
        lowceil_rect_h[n_lowceil_rect] = (uint8_t)h;
        n_lowceil_rect++;
    }
    if (!g_lowceil_active) {
        g_lowceil_x0 = x0; g_lowceil_x1 = x1; g_lowceil_y0 = y0; g_lowceil_y1 = y1;
        g_lowceil_active = 1;
    } else {
        if (x0 < g_lowceil_x0) g_lowceil_x0 = x0;
        if (x1 > g_lowceil_x1) g_lowceil_x1 = x1;
        if (y0 < g_lowceil_y0) g_lowceil_y0 = y0;
        if (y1 > g_lowceil_y1) g_lowceil_y1 = y1;
    }
}
/* Drop the secondary's stale cache of the crawlspace render geometry before it
 * runs the tail pass (CMD_TAIL). These are written ONCE by the primary at map
 * load; the secondary purges the lines so it re-reads the fresh values from
 * SDRAM (write-through means the primary's writes are already there). ceil_h is
 * accessed uncached (CEIL_H) so it needs no purge. Primary never calls this. */
void raycast_purge_lowceil_cache(void) {
    purge_cache_range(&g_lowceil_active, sizeof g_lowceil_active);
    purge_cache_range(&g_lowceil_x0,     4 * sizeof(fx_t));   /* x0,y0,x1,y1 */
    purge_cache_range(&n_lowceil_rect,   sizeof n_lowceil_rect);
    purge_cache_range(lowceil_rect,      sizeof lowceil_rect);
    purge_cache_range(lowceil_rect_h,    sizeof lowceil_rect_h);
    purge_cache_range(ceil_hs,           sizeof ceil_hs);
    purge_cache_range(&n_ceil_hs,        sizeof n_ceil_hs);
    purge_cache_range(ceil_h,            sizeof ceil_h);  /* slab reads it cached */
    /* Exit-hole face (drawn in the tail pass on both CPUs). cx..dir are
     * contiguous statics; purge the whole block by spanning them. */
    purge_cache_range(&g_exit_hole_cx,   sizeof g_exit_hole_cx);
    purge_cache_range(&g_exit_hole_cy,   sizeof g_exit_hole_cy);
    purge_cache_range(&g_exit_hole_axis, sizeof g_exit_hole_axis);
    purge_cache_range(&g_exit_hole_plane, sizeof g_exit_hole_plane);
    purge_cache_range(&g_exit_hole_c0,   sizeof g_exit_hole_c0);
    purge_cache_range(&g_exit_hole_dir,  sizeof g_exit_hole_dir);
    /* Destination peek: the flag flips mid-map (climb commit), so it is
     * purged every tail pass, and the bitmap only while it is live. The
     * lit-side flag rides along -- the blit MIRRORS by it, and the
     * secondary draws half the panel. */
    purge_cache_range(&g_hole_peek_on,   sizeof g_hole_peek_on);
    purge_cache_range(&g_corr_lit_left,  sizeof g_corr_lit_left);
    if (g_hole_peek_on) purge_cache_range(peek_buf, PEEK_BYTES);
}

/* True if the cell holding world point (wx,wy) is a low crawlspace ceiling.
 * Reads ceil_h CACHED (not the uncached CEIL_H alias) — this runs per-pixel in
 * the slab's hot loop, so the ~12-cyc uncached tax would dominate. Coherency:
 * the secondary purges ceil_h in raycast_purge_lowceil_cache before the tail
 * pass; the primary wrote it write-through, so its own cache is already fresh. */
static inline int ceil_is_low(fx_t wx, fx_t wy) {
    int cx = FX_INT(wx), cy = FX_INT(wy);
    if ((unsigned)cx >= MAP_W || (unsigned)cy >= MAP_H) return 0;
    return ceil_h[cy][cx] != CEIL_H_FULL;
}
/* This cell's ceiling height, or CEIL_H_FULL out of bounds. Lets a ceiling pass
 * fill/cap only the cells at ITS height, so crawlspaces (135) and bulkheads (200)
 * cast as separate single-height planes. */
static inline int ceil_h_at(fx_t wx, fx_t wy) {
    int cx = FX_INT(wx), cy = FX_INT(wy);
    if ((unsigned)cx >= MAP_W || (unsigned)cy >= MAP_H) return CEIL_H_FULL;
    return ceil_h[cy][cx];
}

/* Per-cell light boost (0..LIGHT_BOOST_MAX): each fixture brightens its own
 * cell and its neighbours, so a wall or partition adjacent to a light reads
 * as lit. Built in init_lights; read by both CPUs in the wall loop via the
 * cache-through alias. */
static uint8_t cell_light[MAP_H][MAP_W];
/* Read CACHED (no 0x20000000 alias). cell_light is rebuilt only at map load by
 * init_lights on the primary; the per-column wall light-boost then reads it both
 * CPUs. Uncached (~12 cyc) was waste. Coherency: the primary bumps
 * SHARED_UC->cell_light_gen after each rebuild; the secondary purges these lines
 * once when the gen changes (raycast_purge_partition_cache) so it re-reads fresh
 * but the lines stay warm across frames. The primary wrote them, so it's current. */
#define CELL_LIGHT(y,x) (((volatile uint8_t *)cell_light)[(y)*MAP_W + (x)])
#define LIGHT_BOOST_MAX 3
/* DARK ROOM flag, stolen from cell_light's top bit. cell_light only ever holds
 * a 0..3 boost, so bit 7 is free — and riding this array means dark rooms
 * inherit its cache purge and gen counter for nothing. A separate array would
 * have needed both. Read the boost as (cl & 3), the darkness as (cl & CELL_DARK). */
#define CELL_DARK       0x80
/* How far a dark room pushes a surface down the fog ramp. 6 lands it near the
 * dark end without going pure black — "lit by what leaks in", not "off". */
#define DARK_ROOM_SHADE 6
static uint8_t cell_dark_seed[MAP_H][MAP_W];   /* build-time scratch (primary only) */
/* No dark_rect[] here on purpose. The crawlspace keeps its rects because the
 * bulkhead pass caps each tunnel MOUTH individually; dark rooms have no
 * geometry, so once the cells are stamped and the union bbox is grown, the
 * rects have nothing left to say. The renderer only ever needs the per-cell
 * bit + the union. Per-frame cost therefore scales with the union's SCREEN
 * COVERAGE, not the room count — two rooms at opposite corners cost more than
 * eight clustered ones, because the row-reject stops rejecting. */
int  g_dark_active = 0;                     /* 0 => every dark-room test compiles out */
fx_t g_dark_x0, g_dark_y0, g_dark_x1, g_dark_y1;   /* union bbox, like g_lowceil_* */
/* True if the world point is inside a dark room. Mirrors ceil_is_low(). */
static int cell_is_dark(fx_t wx, fx_t wy) {
    int cx = FX_INT(wx), cy = FX_INT(wy);
    if ((unsigned)cx >= (unsigned)MAP_W || (unsigned)cy >= (unsigned)MAP_H) return 0;
    return (CELL_LIGHT(cy, cx) & CELL_DARK) != 0;
}
#define LIT_FOG_CAP     9   /* a lit surface never fogs darker than this (-2 per light level) */
#define SIDE_SHADE      1   /* N/S-facing faces are this many shades darker (form cue) */

/* Canonical shade for a textured world surface: distance fog ramp -> uniform
 * darken -> N/S side cue -> capped by local fixture light -> pushed darker under
 * a low ceiling and in a dark room. EVERY such surface (grid wall, partition,
 * and anything future) MUST route through this so none can forget the crawlspace
 * / dark-room push -- that omission is the whole "system X glows where the walls
 * fog" bug class (partition #12, etc.). Callers derive lit / lowceil_dark /
 * room_dark from their OWN hit cell (the derivation differs: DDA cell + back-step
 * for a grid wall, hit point for a free-standing slab) and pass the resolved
 * inputs in. Far-field strobe is a separate wall effect applied after. Returns a
 * shade level 0..SHADE_LEVELS-1; add it to the surface's palette base. */
static inline int surface_shade(fx_t dist, int side, int lit,
                                int lowceil_dark, int room_dark) {
    int s;
    if (dist < FX(2.5)) {
        s = (int)((dist * 2) / FX(2.5));
    } else {
        fx_t past = dist - FX(2.5);
        fx_t span = FOG_RAMP_DIST - FX(2.5);
        s = 2 + (int)((past * 13) / span);
    }
    if (s > SHADE_LEVELS - 1) s = SHADE_LEVELS - 1;
    s += 1;                                  /* muted-hallway darken */
    if (side) s += SIDE_SHADE;               /* N/S form cue */
    if (s < 0) s = 0;
    if (s > SHADE_LEVELS - 1) s = SHADE_LEVELS - 1;
    if (lit) {                               /* lit surface resists fog */
        int cap = LIT_FOG_CAP - (lit - 1) * 2;
        if (cap < 0) cap = 0;                /* never invert the ramp */
        if (s > cap) s = cap;
    }
    if (lowceil_dark) {                      /* under a low ceiling: no fixture light */
        s += 5;
        if (s > SHADE_LEVELS - 1) s = SHADE_LEVELS - 1;
    }
    if (room_dark) {                         /* dark room: only what leaks in */
        s += DARK_ROOM_SHADE;
        if (s > SHADE_LEVELS - 1) s = SHADE_LEVELS - 1;
    }
    return s;
}

/* Authored ceiling fixtures for the map being loaded, or NULL for "derive the
 * procedural grid". Set by raycast_load_custom, CLEARED by every other loader
 * (procgen/fixed/lobby) — a stale pointer here would light the next map with
 * the previous one's fixtures, the same class of bug as the world_map cache
 * ghosting. Read by init_lights, which runs from raycast_init AFTER the loader.
 * Not const-folded: the loaders run before raycast_init on every map change. */
const struct cm_light_s *g_map_lights   = 0;
uint16_t                 g_map_n_lights = 0;
const struct cm_dark_s  *g_map_dark     = 0;
uint8_t                  g_map_n_dark   = 0;

static void init_lights(void) {
    /* Dark rooms first: the fixture placement below consults them, so a dark
     * room gets no ceiling lights the same way a crawlspace doesn't. */
    g_dark_active = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) cell_dark_seed[y][x] = 0;
    for (int i = 0; i < g_map_n_dark; i++) {
        int x0 = g_map_dark[i].x0, y0 = g_map_dark[i].y0;
        int x1 = g_map_dark[i].x1, y1 = g_map_dark[i].y1;
        for (int y = y0; y <= y1 && y < MAP_H; y++)
            for (int x = x0; x <= x1 && x < MAP_W; x++) cell_dark_seed[y][x] = 1;
        fx_t fx0 = FX(x0), fy0 = FX(y0), fx1 = FX(x1 + 1), fy1 = FX(y1 + 1);
        if (!g_dark_active) {
            g_dark_x0 = fx0; g_dark_x1 = fx1; g_dark_y0 = fy0; g_dark_y1 = fy1;
            g_dark_active = 1;
        } else {
            if (fx0 < g_dark_x0) g_dark_x0 = fx0;
            if (fx1 > g_dark_x1) g_dark_x1 = fx1;
            if (fy0 < g_dark_y0) g_dark_y0 = fy0;
            if (fy1 > g_dark_y1) g_dark_y1 = fy1;
        }
    }
    num_lights = 0;
    if (g_map_lights && g_map_n_lights) {
        /* Hand-placed fixtures from the .map — the author's lighting wins and
         * we do NOT second-guess it: no wall/crawlspace filtering like the
         * procedural branch does, because "a light there" was a decision. */
        for (int i = 0; i < g_map_n_lights && num_lights < MAX_LIGHTS; i++) {
            /* A fixture inside a dark room is a contradiction — drop it. */
            if (cell_dark_seed[g_map_lights[i].cy][g_map_lights[i].cx]) continue;
            lights[num_lights].x = FX(g_map_lights[i].cx) + FX(0.5);
            lights[num_lights].y = FX(g_map_lights[i].cy) + FX(0.5);
            num_lights++;
        }
    } else if (g_lobby_ceiling) {
        /* Hand-authored lobby ceiling: a fixture in every even column
         * (x=2,4,6,8) down rows 2-5 and 7 (skip the entrance row 6) — gives
         * continuous fluorescent runs like the reference photo. */
        for (int my = 2; my <= 7 && num_lights < MAX_LIGHTS; my++) {
            if (my == 6) continue;
            for (int mx = 2; mx <= 8 && num_lights < MAX_LIGHTS; mx += 2) {
                if (world_map[my][mx] != 0) continue;
                lights[num_lights].x = FX(mx) + FX(0.5);
                lights[num_lights].y = FX(my) + FX(0.5);
                num_lights++;
            }
        }
    } else {
        for (int my = 1; my < MAP_H - 1 && num_lights < MAX_LIGHTS; my += 2) {
            for (int mx = 1; mx < MAP_W - 1 && num_lights < MAX_LIGHTS; mx += 2) {
                if (world_map[my][mx] != 0) continue;
                /* No fixtures in a low-ceiling crawlspace cell — the slab is the
                 * ceiling there, so an overhead light would float on top of it. */
                /* Skip a fixture on/around a crawlspace: its own cell AND the
                 * 4-neighbours, so none floats over the low ceiling or hangs at
                 * the tunnel mouth where it'd be seen straight through. */
                if (cell_dark_seed[my][mx]) continue;   /* dark room: no fixtures */
                if (g_lowceil_active) {
                    int near_low = CEIL_H(my, mx) != CEIL_H_FULL
                        || (mx > 0          && CEIL_H(my, mx - 1) != CEIL_H_FULL)
                        || (mx < MAP_W - 1  && CEIL_H(my, mx + 1) != CEIL_H_FULL)
                        || (my > 0          && CEIL_H(my - 1, mx) != CEIL_H_FULL)
                        || (my < MAP_H - 1  && CEIL_H(my + 1, mx) != CEIL_H_FULL);
                    if (near_low) continue;
                }
                lights[num_lights].x = FX(mx) + FX(0.5);
                lights[num_lights].y = FX(my) + FX(0.5);
                num_lights++;
            }
        }
    }
    /* Build the per-cell light boost from the placed fixtures. */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) cell_light[y][x] = 0;
    for (int i = 0; i < num_lights; i++) {
        int lcx = FX_INT(lights[i].x), lcy = FX_INT(lights[i].y);
        for (int dy = -1; dy <= 1; dy++) {
            int cy = lcy + dy; if (cy < 0 || cy >= MAP_H) continue;
            for (int dx = -1; dx <= 1; dx++) {
                int cx = lcx + dx; if (cx < 0 || cx >= MAP_W) continue;
                int v = cell_light[cy][cx] + ((dx == 0 && dy == 0) ? 2 : 1);
                if (v > LIGHT_BOOST_MAX) v = LIGHT_BOOST_MAX;
                cell_light[cy][cx] = (uint8_t)v;
            }
        }
    }
    /* Fold the dark rooms into cell_light's top bit, AFTER the boost loop
     * (which only ever writes 0..3). One array, one purge, one gen. */
    if (g_dark_active)
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (cell_dark_seed[y][x]) cell_light[y][x] |= CELL_DARK;
    /* Signal the secondary that cell_light changed so it purges its now-stale
     * cached lines once (see CELL_LIGHT). Write-through already pushed our
     * writes to SDRAM. */
    SHARED_UC->cell_light_gen++;
}

/* Secondary: purge cell_light AND world_map once per map-load (gen change)
 * before the wall pass, so reads stay cache-warm across frames yet coherent
 * across loads. The primary writes both (procgen_run / the loaders), so its
 * cache is current and it never calls this. Without the world_map purge the
 * secondary renders the PREVIOUS map's grid on its screen half until the lines
 * happen to evict — most visibly the lobby's black-void (==2) cells bleeding
 * into the next procgen level. */
/* Exit-telegraph storage (capture logic lives with the exit code below):
 * the tiny POV frame each powered PVM can show, captured once per map load
 * at the resolution of the PVM's glass. Defined here so the gen purge can
 * size it. */
static uint8_t tg_buf[PVM_FRONT_TEX_W * PVM_FRONT_TEX_H];
static int tg_valid = 0;
static int tg_bx0, tg_by0, tg_bw, tg_bh;   /* glass bbox in pvm_front_tex */

/* ── GAME-ON-GLASS (DESIGN.md 4c) ───────────────────────────────────────
 * The live SMS mini-game picture on every powered PVM. The 68K runs the
 * Z80 headless and free-runs a broadcast of its TILEBUF as (index, word)
 * pairs on COMM10/COMM6; the primary samples a few dozen pairs per frame
 * (double-reading the index for coherence — a torn pair is one wrong
 * cell for one pass, i.e. noise), then bakes the 32x24 tile grid into a
 * tg_buf-format phosphor-cell frame. While a session is live it
 * overrides static AND telegraph on every powered screen: a wall of
 * monitors all showing the same exercise. The picture assembles cell by
 * cell out of the dark as pairs arrive — the CRT locking onto a signal,
 * deliberately left unsmoothed. */
#define GLASS_WORDS 48            /* 24x32 cells, one bit each */
/* DEAD glass must be a REAL palette index, never 0: the 32X framebuffer
 * DROPS byte writes of zero (the sprite-transparency quirk), so a 0 cell
 * never lands and whatever the wall pass left there shows through — the
 * wallpaper-inside-the-monitor bug, exactly as documented. base+0 is the
 * PVM ramp's own dark-glass core, the same value the powered-off screen
 * uses. LIT is the bright text white. */
#define GLASS_DEAD  185           /* == PVM_RAMP_BASE, asserted at its define */
#define GLASS_LIT   49
static uint16_t glass_bits[GLASS_WORDS];
static uint8_t glass_buf[PVM_FRONT_TEX_W * PVM_FRONT_TEX_H];
static uint8_t glass_active = 0;
static void tg_scan_glass(void);

void raycast_glass_set_active(int on) {
    if (on) {
        for (unsigned i = 0; i < GLASS_WORDS; i++) glass_bits[i] = 0;
        for (unsigned i = 0; i < sizeof glass_buf; i++) glass_buf[i] = GLASS_DEAD;
    }
    glass_active = (uint8_t)on;
}

int raycast_glass_active(void) {
    return glass_active;
}

/* Primary, once per frame, before the render kicks off. */
void raycast_glass_sample(void) {
    if (!glass_active) return;
    tg_scan_glass();                       /* idempotent bbox */
    /* GATHER, don't gamble. Blind sampling drew the picture in cell
     * by cell over seconds: each read had to LAND on a slot we still
     * needed, and the 68K's rotation outruns the reader. Instead spin
     * until every slot has been seen once — the rotation passes all 48
     * in well under a millisecond of 68K time, so the whole frame
     * arrives inside ONE render frame. BOUNDED (the SMS-exit-hang law):
     * if the broadcast ever stops, we leave with a partial frame rather
     * than wedging the render loop. */
    {
        uint8_t seen[GLASS_WORDS];
        for (int i = 0; i < GLASS_WORDS; i++) seen[i] = 0;
        int need = GLASS_WORDS;
        uint32_t guard = 6000;
        while (need && --guard) {
            uint16_t i1 = MARS_SYS_COMM10;
            uint16_t w  = MARS_SYS_COMM6;
            if (MARS_SYS_COMM10 != i1) continue;   /* torn pair: skip */
            if (i1 >= GLASS_WORDS) continue;       /* stale pad word: skip */
            if (seen[i1]) continue;
            seen[i1] = 1;
            glass_bits[i1] = w;
            need--;
        }
    }
    /* Bake phosphor cells at glass resolution. scr bytes are FINAL palette
     * indices (emissive, no lut fold). Glyph fidelity is a later pass;
     * occupancy already reads as the banner wipe, the maze, the moving P. */
    for (int y = 0; y < tg_bh; y++) {
        int ty = (y * 24) / tg_bh;
        uint8_t *dst = &glass_buf[(unsigned)y * (unsigned)tg_bw];
        const uint16_t *wrow = &glass_bits[(unsigned)ty * 2u];   /* 32 cells = 2 words */
        for (int x = 0; x < tg_bw; x++) {
            int tx = (x * 32) / tg_bw;
            dst[x] = (wrow[tx >> 4] & (uint16_t)(1u << (tx & 15)))
                   ? GLASS_LIT : GLASS_DEAD;
        }
    }
}
void raycast_purge_cell_light(void) {
    static uint8_t seen_gen = 0xFF;
    uint8_t g = SHARED_UC->cell_light_gen;
    if (g != seen_gen) {
        purge_cache_range(cell_light, sizeof cell_light);
        purge_cache_range(world_map,  sizeof world_map);
        purge_cache_range(pedge_w,    sizeof pedge_w);   /* edge partitions   */
        purge_cache_range(pedge_n,    sizeof pedge_n);   /* ride the map gen  */
        purge_cache_range(pedge_cell, sizeof pedge_cell);/* + proximity gate  */
        purge_cache_range(&g_pedge_any, sizeof g_pedge_any);
        purge_cache_range(prun_lo_w, sizeof prun_lo_w);  /* run-extent LUT    */
        purge_cache_range(prun_hi_w, sizeof prun_hi_w);
        purge_cache_range(prun_lo_n, sizeof prun_lo_n);
        purge_cache_range(prun_hi_n, sizeof prun_hi_n);
        purge_cache_range(tg_buf,   sizeof tg_buf);      /* exit telegraph:   */
        purge_cache_range(&tg_valid, sizeof tg_valid);   /* captured once per */
        purge_cache_range(&tg_bx0,  sizeof tg_bx0);      /* map load by the   */
        purge_cache_range(&tg_by0,  sizeof tg_by0);      /* primary           */
        purge_cache_range(&tg_bw,   sizeof tg_bw);
        purge_cache_range(&tg_bh,   sizeof tg_bh);
        seen_gen = g;
    }
}

/* How many times the wallpaper tile repeats per 1-unit map cell.
 * TEX_W/H must be powers of 2 so the wrap can be a cheap bitmask.
 * The new source (square_walltile_composite.jpg) is a designed
 * seamless tile with 8 chevron ribbons baked into the frame — one
 * tile per cell renders 8 ribbons per wall cell with clean tile
 * seams (left edge matches right edge). Lo and hi share the same
 * tile rate; the LOD swap is purely a resolution upgrade per
 * source repeat. */
#define WALL_TILE_X        4
#define WALL_TILE_Y        4
#define WALL_TILE_HI_X     4
#define WALL_TILE_HI_Y     4
/* Spotted partition wallpaper. PARTITION_TILE = how many times the 64x64 dot
 * tile repeats per cell (overall dot scale). PARTITION_DETAIL = peak dot
 * darkness vs the yellow — kept low so the dots read as a faint tint up
 * close, and it's distance-faded in the draw loop so far partitions fade to
 * plain yellow (you only notice the dots on the near wall). */
#define PARTITION_TILE_X   8
#define PARTITION_TILE_Y   8
#define PARTITION_DETAIL   8
#define WALL_LOD_THRESHOLD FX(2)
#define TEX_W_MASK  (TEX_W - 1)
#define TEX_H_MASK  (TEX_H - 1)


/* Precomputed pixel color per screen row for the base floor/ceiling layer.
 * Indexes [0..SCREEN_H/2-1] = ceiling, bright at top dim toward horizon;
 * [SCREEN_H/2..SCREEN_H-1] = floor, dim near horizon bright at bottom. */
static uint8_t row_color[SCREEN_H];

/* Precomputed cameraX value per screen column. Replaces a per-column divide
 * (one of the few remaining ones in the wall loop) with a single table load. */
static fx_t cameraX_table[SCREEN_W];
/* ULTRA rest-pair camera jitter: HALF of cameraX's per-column step (which is
 * 2*FX_ONE/SCREEN_W). The twin frame shifts every wall ray — and the floor/
 * ceiling row spans, via +plane*this — by half a pixel, so the parked 60Hz
 * flip pair blends into an effective double-width image on a CRT. */
#define ULTRA_JIT_FX ((fx_t)(FX_ONE / SCREEN_W))
/* The twin is MERGED, never alternated (the B00288-290 arc: 30Hz/phase
 * temporal flip never fused on the PVM — texel fields, v-phase and shade
 * bands each read as shimmer, and pinning one leak surfaced the next).
 * m_main renders pass A (ultra_twin=1: frozen, no jitter) and pass B
 * (ultra_twin=2: frozen + this half-column shift), then checkerboard-merges
 * them into ONE static frame — differing pixels alternate A/B spatially,
 * which the CRT fuses into the supersample. So the jitter is applied to
 * EVERYTHING again (walls, floor, ceiling, slab): every difference it
 * creates is detail for the merge, and nothing on screen ever changes. */

static void build_shading_tables(void) {
    int mid = SCREEN_H / 2;
    for (int y = 0; y < SCREEN_H; y++) {
        int yy;
        if (y < mid)      yy = mid - y;
        else if (y > mid) yy = y - mid;
        else              yy = 1;          /* avoid div-by-zero at horizon */
        /* Compressed perspective shade ramp. True mid/yy keeps the
         * close ceiling/floor at shade 0 across most of the screen
         * and only ramps to fog in the last ~10 rows at the horizon.
         * Multiplying by 3/2 makes the fade visible across the full
         * vertical extent — matches the cadence of the wall_shade
         * ramp which hits full fog at perpDist 6. */
        int shade = ((mid * 3) / (yy * 2)) - 1;
        if (shade < 0) shade = 0;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
        row_color[y] = (y <= mid) ? (CEIL_BASE + shade)
                                  : (FLOOR_BASE + shade);
    }

    /* The pre-rendered "distance ring" ceiling shading used to live
     * here — darken every CEIL_GRID_DENSITY-th screen-distance row,
     * computed once into row_color. It produced static horizontal
     * bands tied to screen row (not world Y) that READ as a grid but
     * didn't animate. Now that raycast_draw_ceiling_grid does per-row
     * band fallback for world-Y crossings when facing cardinal, those
     * dynamic bands collide with the pre-rendered ones and show as
     * doubled horizontal lines. Pre-rendered version removed. */
}

/* Fade target — what every surface fades toward at maximum distance.
 * Pure black: misty grey was reading as "too bright" at depth, removing
 * the contrast between near and far. Black gives the classic raycaster
 * "darkness eats the corridor" look.
 *
 * Note: this isn't a perf knob — it's just palette base values at init.
 * Changing these has zero runtime cost. */
/* Mid-grey fog. Equal RGB so the far distance reads as neutral rather
 * than tinted any direction. RGB(8,8,8) ≈ 26% brightness — distinctly
 * "in the haze" but still a clearly visible surface rather than a hole
 * punched in the world. Distance shade 15 lands at ≈ RGB(9, 9, 8). */
#define FOG_R 8
#define FOG_G 8
#define FOG_B 8

/* Linear blend of bright base (weight: SHADE_LEVELS - i) toward fog (weight: i). */
#define MIX(bright, fog, i) (((bright) * (SHADE_LEVELS - (i)) + (fog) * (i)) / SHADE_LEVELS)

/* ══ Live-tunable palette (COLOR menu tab) ══════════════════════════════════
 * The four "mood" surfaces — wall, floor, ceiling, light panels — are each
 * generated from a bright ANCHOR color, transformed by global WARMTH (+R/-B) and
 * SATURATION (chroma scaled around luma), then faded toward FOG across the 16
 * shade levels. The COLOR tab edits the anchors + masters live and raycast_pal_
 * flush() repaints their CRAM in the vblank window. With WARMTH 0 / SAT 100 the
 * anchors below reproduce the current look exactly; bake settled values here. */
enum { PSURF_WALL, PSURF_FLOOR, PSURF_CEIL, PSURF_LIGHT, PSURF_N };
static int8_t g_anchor[PSURF_N][3] = {
    { 31, 27, 18 },   /* WALL  — milky cream-yellow (Mike-tuned on the PVM) */
    { 26, 22, 16 },   /* FLOOR — warm tan           */
    { 25, 22, 16 },   /* CEIL  — warm cream         */
    { 29, 26, 22 },   /* LIGHT — soft warm ivory    */
};
static int g_pal_warmth = 0;     /* -12..+12 : +R -B  */
static int g_pal_sat    = 100;   /* 0..200 % : chroma */
static volatile int g_pal_dirty = 0;

static inline int pal_clamp(int v) { return v < 0 ? 0 : (v > 31 ? 31 : v); }
static void pal_effective(int s, int *er, int *eg, int *eb) {
    int r = g_anchor[s][0], g = g_anchor[s][1], b = g_anchor[s][2];
    int luma = (r + g + b) / 3;
    r = luma + (r - luma) * g_pal_sat / 100 + g_pal_warmth;
    g = luma + (g - luma) * g_pal_sat / 100;
    b = luma + (b - luma) * g_pal_sat / 100 - g_pal_warmth;
    *er = pal_clamp(r); *eg = pal_clamp(g); *eb = pal_clamp(b);
}

/* Repaint the four mood ramps at brightness lvl/FADE_STEPS. CRAM — call in vblank. */
void raycast_pal_apply(int lvl) {
    int wr,wg,wb, fr,fg,fb, cr,cg,cb, lr,lg,lb;
    pal_effective(PSURF_WALL,  &wr,&wg,&wb);
    pal_effective(PSURF_FLOOR, &fr,&fg,&fb);
    pal_effective(PSURF_CEIL,  &cr,&cg,&cb);
    pal_effective(PSURF_LIGHT, &lr,&lg,&lb);
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(WALL_BASE + i,  MIX(wr,FOG_R,i)*lvl/FADE_STEPS, MIX(wg,FOG_G,i)*lvl/FADE_STEPS, MIX(wb,FOG_B,i)*lvl/FADE_STEPS);
        Hw32xSetBGColor(FLOOR_BASE + i, MIX(fr,FOG_R,i)*lvl/FADE_STEPS, MIX(fg,FOG_G,i)*lvl/FADE_STEPS, MIX(fb,FOG_B,i)*lvl/FADE_STEPS);
        Hw32xSetBGColor(CEIL_BASE + i,  MIX(cr,FOG_R,i)*lvl/FADE_STEPS, MIX(cg,FOG_G,i)*lvl/FADE_STEPS, MIX(cb,FOG_B,i)*lvl/FADE_STEPS);
    }
    /* Light: 4 flicker states as brightness ratios of the tuned panel. */
    static const int lrat[4] = { 100, 93, 79, 62 };
    for (int i = 0; i < 4; i++)
        Hw32xSetBGColor(LIGHT_BASE + i, lr*lrat[i]/100*lvl/FADE_STEPS,
                        lg*lrat[i]/100*lvl/FADE_STEPS, lb*lrat[i]/100*lvl/FADE_STEPS);
}

/* Called each frame in m_main's vblank window — repaints only when tuning. */
void raycast_pal_flush(void) {
    if (!g_pal_dirty) return;
    g_pal_dirty = 0;
    raycast_pal_apply(FADE_STEPS);
}

/* COLOR-tab controls. ch 0/1/2 = R/G/B of surface s; masters are warmth/sat. */
void raycast_pal_ch(int s, int ch, int dir) {
    int v = g_anchor[s][ch] + dir; g_anchor[s][ch] = (int8_t)pal_clamp(v); g_pal_dirty = 1;
}
int  raycast_pal_ch_get(int s, int ch) { return g_anchor[s][ch]; }
void raycast_pal_warmth(int dir) {
    g_pal_warmth += dir; if (g_pal_warmth < -12) g_pal_warmth = -12; if (g_pal_warmth > 12) g_pal_warmth = 12; g_pal_dirty = 1;
}
int  raycast_pal_warmth_get(void) { return g_pal_warmth; }
void raycast_pal_sat(int dir) {
    g_pal_sat += dir * 5; if (g_pal_sat < 0) g_pal_sat = 0; if (g_pal_sat > 200) g_pal_sat = 200; g_pal_dirty = 1;
}
int  raycast_pal_sat_get(void) { return g_pal_sat; }
/* Reset every mood anchor + masters to the shipped defaults. Keep this literal
 * in sync with the g_anchor[] initializer above (both are the baked palette). */
void raycast_pal_reset(void) {
    static const int8_t def[PSURF_N][3] = {
        { 31, 27, 18 }, { 26, 22, 16 }, { 25, 22, 16 }, { 29, 26, 22 } };
    for (int s = 0; s < PSURF_N; s++)
        for (int c = 0; c < 3; c++) g_anchor[s][c] = def[s][c];
    g_pal_warmth = 0; g_pal_sat = 100; g_pal_dirty = 1;
}

/* Set the gameplay palette scaled to brightness lvl/FADE_STEPS (FADE_STEPS
 * = full bright, 0 = black) — drives the lobby->map fade-through-black.
 * Must be called inside vblank (CRAM write). FADE_STEPS is in raycast.h. */
/* The chair's 4 CRAM entries at full brightness — single source of truth.
 * Also called by the asset viewer, whose host screen (the start-menu hero
 * image) paints ALL 256 CRAM entries with its own palette: without this
 * repaint the viewer chair renders in whatever the hero image left at
 * 53-56 (it read TAN long after the game itself was fixed). */
void raycast_paint_chair_ramp(void) {
    Hw32xSetBGColor(CHAIR_BASE + 0, 12, 10,  7);
    Hw32xSetBGColor(CHAIR_BASE + 1, 15, 12,  9);
    Hw32xSetBGColor(CHAIR_BASE + 2, 17, 14, 11);
    Hw32xSetBGColor(CHAIR_BASE + 3, 18, 15, 12);
}

void raycast_set_brightness(int lvl) {
    if (lvl < 0) lvl = 0; else if (lvl > FADE_STEPS) lvl = FADE_STEPS;
    Hw32xSetBGColor(0, 0, 0, 0);
    /* Wall/floor/ceil/light ramps come from the tunable palette engine (COLOR
     * tab), so a fade re-applies whatever the anchors/masters currently are. */
    raycast_pal_apply(lvl);
    {
        static const uint8_t ch[4][3] = { {12,10,7}, {15,12,9}, {17,14,11}, {18,15,12} };
        for (int i = 0; i < 4; i++)
            Hw32xSetBGColor(CHAIR_BASE + i, ch[i][0]*lvl/FADE_STEPS, ch[i][1]*lvl/FADE_STEPS, ch[i][2]*lvl/FADE_STEPS);
    }
    {
        static const uint8_t nb[8][3] = {{16,11,5},{2,2,1},{7,5,3},{11,8,6},{16,12,9},{19,16,13},{23,20,17},{26,22,19}};
        for (int i = 0; i < 8; i++)
            Hw32xSetBGColor(NEANDER_BASE + i, nb[i][0]*lvl/FADE_STEPS, nb[i][1]*lvl/FADE_STEPS, nb[i][2]*lvl/FADE_STEPS);
    }
    {
        static const uint8_t ob[OUTLET_LEVELS][3] = {{2,2,2},{9,9,8},{16,15,13},{22,21,18},{28,27,23}};
        for (int i = 0; i < OUTLET_LEVELS; i++)
            Hw32xSetBGColor(OUTLET_BASE + i, ob[i][0]*lvl/FADE_STEPS, ob[i][1]*lvl/FADE_STEPS, ob[i][2]*lvl/FADE_STEPS);
    }
    {
        /* Door: grey metal (0-4) + green EXIT sign (5-6) + white (7). The grey
         * ramp is lifted + range-compressed vs the bake preview — a lighter,
         * softer (muted) metal. Fades with the level. */
        static const uint8_t db[8][3] = {
            {15,12,9},{18,15,11},{22,18,14},{25,21,16},{29,25,20},{3,12,6},{8,19,10},{29,30,28}};
        for (int i = 0; i < 8; i++)
            Hw32xSetBGColor(DOOR_BASE + i, db[i][0]*lvl/FADE_STEPS, db[i][1]*lvl/FADE_STEPS, db[i][2]*lvl/FADE_STEPS);
        /* Extra-dark warm browns below the door ramp (leaf fades into shadow when open). */
        static const uint8_t dk[4][3] = { {4,3,2}, {7,5,3}, {9,7,5}, {12,10,7} };
        for (int i = 0; i < 4; i++)
            Hw32xSetBGColor(DOOR_DARK_BASE + i, dk[i][0]*lvl/FADE_STEPS, dk[i][1]*lvl/FADE_STEPS, dk[i][2]*lvl/FADE_STEPS);
        /* Exit-hole deep tail: hue bridge from the wall ramp's floor (9,9,8)
         * down to DOOR_DARK+2 (9,7,5). Same luma trajectory, no hue step. */
        static const uint8_t hd[4][3] = { {14,13,9}, {13,12,8}, {12,11,8}, {10,9,7} };
        for (int i = 0; i < 4; i++)
            Hw32xSetBGColor(HOLE_DEEP_BASE + i, hd[i][0]*lvl/FADE_STEPS, hd[i][1]*lvl/FADE_STEPS, hd[i][2]*lvl/FADE_STEPS);
        /* Soft stipple dapple shades (door ramp ~1-2 darker). */
        static const uint8_t sp[5][3] = { {13,11,8},{16,14,10},{20,17,13},{24,20,15},{28,24,18} };
        for (int i = 0; i < 5; i++)
            Hw32xSetBGColor(STIPPLE_BASE + i, sp[i][0]*lvl/FADE_STEPS, sp[i][1]*lvl/FADE_STEPS, sp[i][2]*lvl/FADE_STEPS);
        /* Muted aged-bronze handle hardware. */
        static const uint8_t gh[4][3] = { {10,8,5},{15,12,8},{21,17,12},{26,22,16} };
        for (int i = 0; i < 4; i++)
            Hw32xSetBGColor(HANDLE_BASE + i, gh[i][0]*lvl/FADE_STEPS, gh[i][1]*lvl/FADE_STEPS, gh[i][2]*lvl/FADE_STEPS);
        static const uint8_t fr[5][3] = { {9,8,7},{12,11,9},{15,13,11},{18,16,13},{21,18,15} };
        for (int i = 0; i < 5; i++)
            Hw32xSetBGColor(FRAME_BASE + i, fr[i][0]*lvl/FADE_STEPS, fr[i][1]*lvl/FADE_STEPS, fr[i][2]*lvl/FADE_STEPS);
        /* EXIT-sign fog ramps: the sign USED to stay lit at any distance (it
         * glowed unnaturally on a fogged door). Now the green letters and white
         * plate darken toward fog with door_shade, indexed in the dlut. */
        static const uint8_t sg[4][3] = { {8,19,10},{7,15,9},{7,11,8},{8,9,8} };
        static const uint8_t sw[4][3] = { {28,26,20},{21,20,15},{15,14,11},{9,9,8} };  /* ivory/tooth, not stark white */
        for (int i = 0; i < 4; i++) {
            Hw32xSetBGColor(SIGN_GREEN_BASE + i, sg[i][0]*lvl/FADE_STEPS, sg[i][1]*lvl/FADE_STEPS, sg[i][2]*lvl/FADE_STEPS);
            Hw32xSetBGColor(SIGN_WHITE_BASE + i, sw[i][0]*lvl/FADE_STEPS, sw[i][1]*lvl/FADE_STEPS, sw[i][2]*lvl/FADE_STEPS);
        }
        /* Countertop wood -> fog, fade-scaled like everything else. */
        for (int i = 0; i < 8; i++)
            Hw32xSetBGColor(WOODTOP_BASE + i,
                ((18 * (7 - i) + FOG_R * i) / 7) * lvl / FADE_STEPS,
                ((16 * (7 - i) + FOG_G * i) / 7) * lvl / FADE_STEPS,
                ((13 * (7 - i) + FOG_B * i) / 7) * lvl / FADE_STEPS);
    }
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(PARTITION_BASE + i,
            MIX(24,FOG_R,i)*lvl/FADE_STEPS, MIX(25,FOG_G,i)*lvl/FADE_STEPS, MIX(15,FOG_B,i)*lvl/FADE_STEPS);
    }
    /* COMMUNITY ARENA (144..255) fades too. build_palette stamps comm_pal
     * once at full brightness and nothing rescaled it, so every community
     * sprite — and the PVM, whose ramp lives in the arena — sat lit at
     * full white through the blackout while the world went dark around it
     * (Mike's fade-out screenshot: glowing monitor in a black room). */
    for (int i = 0; i < COMM_PAL_COUNT; i++)
        Hw32xSetBGColor(comm_pal[i].idx,
                        comm_pal[i].r * lvl / FADE_STEPS,
                        comm_pal[i].g * lvl / FADE_STEPS,
                        comm_pal[i].b * lvl / FADE_STEPS);
    /* Master System console (SMS_RAMP_BASE, see the define). Four near-black
     * steps, darkest first, so chair_face_shade's 0..3 lands on all of them.
     * Stamped HERE inside pal_apply rather than once in build_palette, so it
     * fades with the room — the ramp right above it is the cautionary tale. */
    {
        static const uint8_t sms_l[4] = { 2, 4, 7, 11 };
        for (int i = 0; i < 4; i++)
            Hw32xSetBGColor(SMS_RAMP_BASE + i,
                            sms_l[i] * lvl / FADE_STEPS,
                            sms_l[i] * lvl / FADE_STEPS,
                            (sms_l[i] + 1) * lvl / FADE_STEPS);
    }
    Hw32xSetBGColor(VIEWER_INK, 2 * lvl / FADE_STEPS,
                                3 * lvl / FADE_STEPS,
                               14 * lvl / FADE_STEPS);   /* navy label ink */
}

static void build_palette(void) {
    Hw32xSetBGColor(0, 0, 0, 0);
    /* Automap overlay reds (see raycast.h) — deliberately outside every ramp
     * so no fog/shade math ever lands on them. */
    Hw32xSetBGColor(AMAP_RED,        22, 3, 3);
    Hw32xSetBGColor(AMAP_RED_BRIGHT, 31, 6, 6);
    /* Countertop wood -> fog (see WOODTOP_BASE). */
    for (int i = 0; i < 8; i++)
        Hw32xSetBGColor(WOODTOP_BASE + i,
                        (18 * (7 - i) + FOG_R * i) / 7,
                        (16 * (7 - i) + FOG_G * i) / 7,
                        (13 * (7 - i) + FOG_B * i) / 7);
    /* Community sprite palettes: every contributor sprite's own quantized
     * colors, generated into comm_pal.h by gen_assets from the registry. */
    for (int i = 0; i < COMM_PAL_COUNT; i++)
        Hw32xSetBGColor(comm_pal[i].idx,
                        comm_pal[i].r, comm_pal[i].g, comm_pal[i].b);
    /* Walls / carpet / ceiling / fluorescent panels all come from the tunable
     * palette engine (COLOR tab) — anchors default to the shipped look, warmth 0,
     * sat 100. Edit anchors there live; bake settled values into g_anchor[]. */
    raycast_pal_apply(FADE_STEPS);
    /* Crawlspace ceiling: dark-beige drop-panel. The fill is a warm muted tan;
     * the seam is a darker tan for the panel grid, which gives the surface the
     * parallax/structure cue that a flat fill lacked (flat read as a void). */
    Hw32xSetBGColor(LOWCEIL_COLOR, 16, 14, 10);
    Hw32xSetBGColor(LOWCEIL_SEAM,  10,  8,  5);
    /* Neanderthal cardboard standup. Index 0 = cardboard back (warm tan
     * brown). 1-7 = quantized figure shades pulled from the 32x64 PNG
     * texture by 7-bucket brightness quantization. */
    Hw32xSetBGColor(NEANDER_BASE + 0, 16, 11,  5);
    Hw32xSetBGColor(NEANDER_BASE + 1,  2,  2,  1);
    Hw32xSetBGColor(NEANDER_BASE + 2,  7,  5,  3);
    Hw32xSetBGColor(NEANDER_BASE + 3, 11,  8,  6);
    Hw32xSetBGColor(NEANDER_BASE + 4, 16, 12,  9);
    Hw32xSetBGColor(NEANDER_BASE + 5, 19, 16, 13);
    Hw32xSetBGColor(NEANDER_BASE + 6, 23, 20, 17);
    Hw32xSetBGColor(NEANDER_BASE + 7, 26, 22, 19);
    /* Electrical outlet decal: slot-dark -> warm plate-white. */
    Hw32xSetBGColor(OUTLET_BASE + 0,  2,  2,  2);
    Hw32xSetBGColor(OUTLET_BASE + 1,  9,  9,  8);
    Hw32xSetBGColor(OUTLET_BASE + 2, 16, 15, 13);
    Hw32xSetBGColor(OUTLET_BASE + 3, 22, 21, 18);
    Hw32xSetBGColor(OUTLET_BASE + 4, 28, 27, 23);
    /* The metal fire door: warm taupe/brown slab (0-4) + green EXIT sign (5-6)
     * + white (7). Tinged brown (R>G>B) so it sits in the warm backrooms palette
     * instead of reading as cold grey; lifted + compressed = lighter & muted. */
    Hw32xSetBGColor(DOOR_BASE + 0, 15, 12,  9);
    Hw32xSetBGColor(DOOR_BASE + 1, 18, 15, 11);
    Hw32xSetBGColor(DOOR_BASE + 2, 22, 18, 14);
    Hw32xSetBGColor(DOOR_BASE + 3, 25, 21, 16);
    Hw32xSetBGColor(DOOR_BASE + 4, 29, 25, 20);
    Hw32xSetBGColor(DOOR_BASE + 5,  3, 12,  6);
    Hw32xSetBGColor(DOOR_BASE + 6,  8, 19, 10);
    Hw32xSetBGColor(DOOR_BASE + 7, 29, 30, 28);
    /* Extra-dark warm browns below the door ramp — the leaf fades into these as
     * it swings away into the recess shadow (kept warm to match the door). */
    Hw32xSetBGColor(DOOR_DARK_BASE + 0,  4,  3,  2);
    Hw32xSetBGColor(DOOR_DARK_BASE + 1,  7,  5,  3);
    Hw32xSetBGColor(DOOR_DARK_BASE + 2,  9,  7,  5);
    Hw32xSetBGColor(DOOR_DARK_BASE + 3, 12, 10,  7);
    /* Exit-hole cool run (see HOLE_DEEP_BASE): glow-bright, glow-dim,
     * deep-mid, deepest. Light-to-dark so the back panel indexes it flat.
     * LIFTED twice by hardware review: consumer CRTs crush the low end, so
     * at play distance anything under ~luma 70 read as flat black. Deep-mid
     * (most of the panel) now sits ~83 -- a grey shadow, plainly not black
     * -- and the hue stays cool (B-R +16..24) so it still reads as another
     * space, never as shadowed wallpaper. */
    /* Hue: an UNLIT YELLOW WALL, darker side, flat -- the wallpaper's own
     * hue family (B ~= 0.65 R) without its chevron or its light. The panel
     * reads as the same paper in a room with no bulbs, not another
     * material. (Was warm-grey; before that blue; Mike's call each step.) */
    Hw32xSetBGColor(HOLE_DEEP_BASE + 0, 14, 13,  9);
    Hw32xSetBGColor(HOLE_DEEP_BASE + 1, 13, 12,  8);
    Hw32xSetBGColor(HOLE_DEEP_BASE + 2, 12, 11,  8);
    Hw32xSetBGColor(HOLE_DEEP_BASE + 3, 10,  9,  7);
    /* EXIT-sign fog ramps (green letters + white plate, near->fog). */
    Hw32xSetBGColor(SIGN_GREEN_BASE + 0,  8, 19, 10);
    Hw32xSetBGColor(SIGN_GREEN_BASE + 1,  7, 15,  9);
    Hw32xSetBGColor(SIGN_GREEN_BASE + 2,  7, 11,  8);
    Hw32xSetBGColor(SIGN_GREEN_BASE + 3,  8,  9,  8);
    Hw32xSetBGColor(SIGN_WHITE_BASE + 0, 28, 26, 20);   /* ivory/tooth plate, not stark white */
    Hw32xSetBGColor(SIGN_WHITE_BASE + 1, 21, 20, 15);
    Hw32xSetBGColor(SIGN_WHITE_BASE + 2, 15, 14, 11);
    Hw32xSetBGColor(SIGN_WHITE_BASE + 3,  9,  9,  8);
    /* Chair wood — its own ramp, a touch lighter than the door recess. */
    raycast_paint_chair_ramp();
    /* Soft stipple dapple — the door ramp nudged ~1-2 darker (barely there). */
    Hw32xSetBGColor(STIPPLE_BASE + 0, 13, 11,  8);
    Hw32xSetBGColor(STIPPLE_BASE + 1, 16, 14, 10);
    Hw32xSetBGColor(STIPPLE_BASE + 2, 20, 17, 13);
    Hw32xSetBGColor(STIPPLE_BASE + 3, 24, 20, 15);
    Hw32xSetBGColor(STIPPLE_BASE + 4, 28, 24, 18);
    /* Handle hardware: muted aged bronze (desaturated — reads as metal without
     * popping). dark -> highlight. */
    Hw32xSetBGColor(HANDLE_BASE + 0, 10,  8,  5);
    Hw32xSetBGColor(HANDLE_BASE + 1, 15, 12,  8);
    Hw32xSetBGColor(HANDLE_BASE + 2, 21, 17, 12);
    Hw32xSetBGColor(HANDLE_BASE + 3, 26, 22, 16);
    /* Door jamb/casing: a muted-brown ramp (dark..light), +4 = lit. */
    Hw32xSetBGColor(FRAME_BASE + 0,  9,  8,  7);
    Hw32xSetBGColor(FRAME_BASE + 1, 12, 11,  9);
    Hw32xSetBGColor(FRAME_BASE + 2, 15, 13, 11);
    Hw32xSetBGColor(FRAME_BASE + 3, 18, 16, 13);
    Hw32xSetBGColor(FRAME_BASE + 4, 21, 18, 15);
    /* Partition wallpaper: muted olive-green eggshell (the spotted divider
     * in the reference) — greener / less saturated than the yellow walls,
     * bright..fog like the wall ramp so distance shading + dot motif work. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(PARTITION_BASE + i,
                        MIX(24, FOG_R, i),
                        MIX(25, FOG_G, i),
                        MIX(15, FOG_B, i));
    }
}

/* Byte pointer to the start of pixel data in the current back framebuffer.
 * (32X 8bpp layout: 0x100 words of line table, then pixels at byte offset 0x200.)
 * Non-volatile: the 32X framebuffer at 0x24000000 isn't SH-2 cached, so
 * writes go through directly. A single `asm("" ::: "memory")` barrier at
 * end-of-render commits any reordered stores before the VDP sees them. */
static inline uint8_t *fb_pixels(void) {
    return (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
}

/* Eye height (8.8 fraction of room height): 128 = standing (mid-wall, the
 * original symmetric wall projection); 64 = crawling (eye a quarter up
 * from the floor). Held in SHARED_UC->eye_h so both CPUs' wall draw see it. */
#define STAND_EYE  128
#define CROUCH_EYE 40          /* ~1/6 up the wall — down near the carpet */
/* Automatic duck under a bulkhead: a dipped head, not a crouch. Deliberately
 * ABOVE sound.c's crawl threshold (eye_h <= 80) so ducking through a header
 * keeps the walking footsteps instead of switching to the crawl drag — you
 * are walking, just stooped. */
#define DUCK_EYE   88
/* Crouch tone-gradient shift: how many row_color steps to slide the
 * floor/ceiling fade as the eye drops (more bright carpet before the fade,
 * ceiling fogs sooner). Applied identically in clear_half, the carpet, and
 * the ceiling-grid so the whole gradient travels together. Reuses the same
 * row_color shift the look-pitch uses, just keyed to eye height instead of
 * pitch. 0 when standing. NOTE: this shifts COLOR only — the carpet's stain
 * skip/LOD must stay on the *geometric* (unshifted) distance or far rows
 * un-skip into aliased noise. */
#define CROUCH_GRAD_SHIFT(eh)  (((STAND_EYE - (int)(eh)) * 1) >> 2)

/* SDRAM staging for the wall textures. The .rodata arrays live in cart ROM
 * (0x02000000); these copies live in SDRAM .bss (0x06000000). The SH-2's 4KB
 * write-through cache thrashes on the ~8KB of texture data, and a cache-miss
 * refill from cart ROM is far slower than from SDRAM — so the per-column
 * shade_lut build (which reads a tex_h-byte column slice) gets cheaper refills.
 * Copied once; write-through means the secondary sees it with no flush. */
static uint8_t wall_tex_hi_ram[WALL_TEX_HI_WIDTH][WALL_TEX_HI_HEIGHT];
static uint8_t partition_tex_ram[PARTITION_TEX_WIDTH][PARTITION_TEX_HEIGHT];
static uint8_t wall_tex_ram[WALL_TEX_WIDTH][WALL_TEX_HEIGHT];
/* Door too (8KB): a nearby open/closed door covers 100+ columns and its
 * per-pixel dlut[col_base[oty]] reads touch the whole texture every frame —
 * cache-miss refills from cart ROM were the door-open frame drop. */
static uint8_t door_tex_ram[DOOR_TEX_WIDTH][DOOR_TEX_HEIGHT];

static void stage_textures_to_sdram(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    const uint8_t *s; uint8_t *d; int i, n;
    s = (const uint8_t *)wall_tex_hi;   d = (uint8_t *)wall_tex_hi_ram;
    n = (int)sizeof(wall_tex_hi);   for (i = 0; i < n; i++) d[i] = s[i];
    s = (const uint8_t *)partition_tex; d = (uint8_t *)partition_tex_ram;
    n = (int)sizeof(partition_tex); for (i = 0; i < n; i++) d[i] = s[i];
    s = (const uint8_t *)wall_tex;      d = (uint8_t *)wall_tex_ram;
    n = (int)sizeof(wall_tex);      for (i = 0; i < n; i++) d[i] = s[i];
    s = (const uint8_t *)door_tex;      d = (uint8_t *)door_tex_ram;
    n = (int)sizeof(door_tex);      for (i = 0; i < n; i++) d[i] = s[i];
}

static void raycast_capture_exit_view(void);   /* exit telegraph (below) */
void raycast_init(void) {
    stage_textures_to_sdram();
    build_palette();
    build_shading_tables();
    init_lights();
    raycast_peek_clear();     /* every portal lands here; the peek is spent */
    SHARED_UC->eye_h = STAND_EYE;        /* standing until the player crouches */
    /* Precompute cameraX[col] = 2*col/SCREEN_W - 1 in FX. */
    for (int col = 0; col < SCREEN_W; col++) {
        cameraX_table[col] = ((fx_t)col << (FX_SHIFT + 1)) / SCREEN_W - FX_ONE;
    }
    /* EXIT TELEGRAPH: capture the tiny POV frame from this map's exit —
     * AFTER init_lights bumped cell_light_gen, so the secondary's gen purge
     * (which also covers tg_*) fires on its next frame and both halves show
     * the same picture. */
    raycast_capture_exit_view();
}

/* --- Map loaders --------------------------------------------------- *
 * Each fills world_map[]/partitions[] and parks the player. Call BEFORE
 * raycast_init() (or re-call init_lights via raycast_init) so the
 * ceiling-fixture grid is laid over the new map. */

/* Protected spawn->exit corridor. Bit x of row y set = cell (x,y) must stay
 * traversable: the BFS parent-chain path from the exit door's approach cell
 * back to spawn, plus the door's own wall cell. Everything procgen places
 * AFTER the door (chairs are immovable, crawl tubes carve wall cells, outlets
 * squat on faces) consults this so the exit is solvable BY CONSTRUCTION —
 * no verify-and-evict pass. Furniture can still block ALTERNATE routes;
 * that's a detour, not a softlock. */
uint32_t exit_path_bits[MAP_H];
static int g_exit_wall_cx = -1, g_exit_wall_cy = -1;   /* the door's wall cell */

int raycast_exit_path_cell(int x, int y) {
    return (int)((exit_path_bits[y] >> x) & 1u);
}

/* The cell the SMS mini-game's player must step into to escape, covering all
 * three exit flavors: the procgen door's wall cell (walking INTO the door is
 * the win — it stays a wall bit in the packed map, so the game checks exit
 * before wall), the exit hole's open approach cell, or an authored map's door
 * decal wall cell. 0 = this level has no exit (the lobby). */
int raycast_exit_cell(int *cx, int *cy) {
    if (g_exit_wall_cx >= 0) { *cx = g_exit_wall_cx; *cy = g_exit_wall_cy; return 1; }
    if (g_exit_hole_cx >= 0) { *cx = g_exit_hole_ax; *cy = g_exit_hole_ay; return 1; }
    for (int d = 0; d < num_decals; d++) {
        if (decals[d].kind != 1) continue;
        int px_ = FX_INT(decals[d].x), py_ = FX_INT(decals[d].y);
        int qx = px_, qy = py_;
        if (decals[d].axis) { py_ = FX_INT(decals[d].y - (FX_ONE >> 1));
                              qy  = FX_INT(decals[d].y + (FX_ONE >> 1)); }
        else                { px_ = FX_INT(decals[d].x - (FX_ONE >> 1));
                              qx  = FX_INT(decals[d].x + (FX_ONE >> 1)); }
        /* the WALL side of the decal plane is the target cell */
        if ((unsigned)px_ < MAP_W && (unsigned)py_ < MAP_H &&
            world_map[py_][px_] == 0) { *cx = qx; *cy = qy; }
        else                          { *cx = px_; *cy = py_; }
        if ((unsigned)*cx < MAP_W && (unsigned)*cy < MAP_H) return 1;
        break;
    }
    return 0;
}

/* ── EXIT TELEGRAPH ─────────────────────────────────────────────────────
 * Every powered PVM can show a tiny POV frame captured FROM the level's
 * exit, looking back down the way a player would approach — the monitors
 * whisper the way out. Captured ONCE at map load (pov_render, walls-only,
 * real palette indices) at the resolution of the PVM's glass, so display
 * is a straight texel-for-texel read in tex_tri_lut's screen branch.
 * tg_* geometry is load-constant; the secondary purges it with the other
 * map-gen state so its screen half reads the same picture. */


/* Glass bbox scan, lazy, per-CPU (ROM texture -> both CPUs converge on the
 * same answer without sharing). Texel >= 5 is the screen region — the same
 * test tex_tri_lut's decode uses. */
static void tg_scan_glass(void) {
    if (tg_bw > 0) return;
    int x0 = PVM_FRONT_TEX_W, y0 = PVM_FRONT_TEX_H, x1 = -1, y1 = -1;
    for (int y = 0; y < PVM_FRONT_TEX_H; y++)
        for (int x = 0; x < PVM_FRONT_TEX_W; x++)
            if (pvm_front_tex[y][x] >= 5) {
                if (x < x0) x0 = x; if (x > x1) x1 = x;
                if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
    if (x1 < 0) { x0 = y0 = 0; x1 = PVM_FRONT_TEX_W - 1; y1 = PVM_FRONT_TEX_H - 1; }
    tg_bx0 = x0; tg_by0 = y0; tg_bw = x1 - x0 + 1; tg_bh = y1 - y0 + 1;
}

/* Nearest of 8 compass angles to the vector (dx, dy) — argmax of dots, the
 * same no-atan2 trick the directional sprites use. */
static uint8_t tg_angle8(fx_t dx, fx_t dy) {
    uint8_t best = 0; int64_t bestd = (int64_t)1 << 62; bestd = -bestd;
    for (int k = 0; k < 8; k++) {
        uint8_t a = (uint8_t)(k * 32);
        int64_t d = (int64_t)COS_FX(a) * dx + (int64_t)SIN_FX(a) * dy;
        if (d > bestd) { bestd = d; best = a; }
    }
    return best;
}

/* Capture the telegraph at map load. Camera: the exit's APPROACH cell, aimed
 * back along the protected spawn->exit corridor (walk the path bits a few
 * cells for a bearing); authored maps without path bits fall back to the
 * exit door's inward normal. No exit at all -> tg_valid stays 0 and the
 * telegraph mode shows plain static. */
static void raycast_capture_exit_view(void) {
    tg_valid = 0;
    tg_scan_glass();
    int wx = -1, wy = -1, ax = -1, ay = -1;
    if (g_exit_wall_cx >= 0) {
        wx = g_exit_wall_cx; wy = g_exit_wall_cy;
    } else if (g_exit_hole_cx >= 0) {
        wx = g_exit_hole_cx; wy = g_exit_hole_cy;
        ax = g_exit_hole_ax; ay = g_exit_hole_ay;
    } else {
        /* Authored map: the exit is a door decal (kind 1). Its wall cell is
         * the cell the decal plane borders on the SOLID side. */
        for (int d = 0; d < num_decals; d++) {
            if (decals[d].kind != 1) continue;
            int cx = FX_INT(decals[d].x), cy = FX_INT(decals[d].y);
            /* Probe both sides of the plane for the open approach cell. */
            int px_ = cx, py_ = cy, qx = cx, qy = cy;
            if (decals[d].axis) { py_ = FX_INT(decals[d].y - (FX_ONE >> 1));
                                  qy  = FX_INT(decals[d].y + (FX_ONE >> 1)); }
            else                { px_ = FX_INT(decals[d].x - (FX_ONE >> 1));
                                  qx  = FX_INT(decals[d].x + (FX_ONE >> 1)); }
            if ((unsigned)px_ < MAP_W && (unsigned)py_ < MAP_H &&
                world_map[py_][px_] == 0) { ax = px_; ay = py_; wx = qx; wy = qy; }
            else if ((unsigned)qx < MAP_W && (unsigned)qy < MAP_H &&
                     world_map[qy][qx] == 0) { ax = qx; ay = qy; wx = px_; wy = py_; }
            break;
        }
        if (ax < 0) return;
    }
    if (ax < 0) {
        /* Door exits: approach = the open orthogonal neighbour of the wall
         * cell, exit-path cell preferred (that's the protected corridor). */
        static const int nb[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (int pass = 0; pass < 2 && ax < 0; pass++)
            for (int k = 0; k < 4; k++) {
                int nx = wx + nb[k][0], ny = wy + nb[k][1];
                if ((unsigned)nx >= MAP_W || (unsigned)ny >= MAP_H) continue;
                if (world_map[ny][nx] != 0) continue;
                if (pass == 0 && !raycast_exit_path_cell(nx, ny)) continue;
                ax = nx; ay = ny; break;
            }
        if (ax < 0) return;
    }
    /* Bearing: walk the exit path AWAY from the exit for a few cells so the
     * camera looks down the corridor, not at the first wall of a turn. */
    int bx = ax, by = ay, px2 = wx, py2 = wy;
    for (int step = 0; step < 5; step++) {
        static const int nb[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        int nx = -1, ny = -1;
        for (int k = 0; k < 4; k++) {
            int tx = bx + nb[k][0], ty = by + nb[k][1];
            if (tx == px2 && ty == py2) continue;
            if ((unsigned)tx >= MAP_W || (unsigned)ty >= MAP_H) continue;
            if (!raycast_exit_path_cell(tx, ty)) continue;
            nx = tx; ny = ty; break;
        }
        if (nx < 0) break;
        px2 = bx; py2 = by; bx = nx; by = ny;
    }
    fx_t ex = ((fx_t)ax << FX_SHIFT) + (FX_ONE >> 1);
    fx_t ey = ((fx_t)ay << FX_SHIFT) + (FX_ONE >> 1);
    uint8_t ang;
    if (bx != ax || by != ay)
        ang = tg_angle8(((fx_t)bx << FX_SHIFT) + (FX_ONE >> 1) - ex,
                        ((fx_t)by << FX_SHIFT) + (FX_ONE >> 1) - ey);
    else
        ang = tg_angle8(ex - (((fx_t)wx << FX_SHIFT) + (FX_ONE >> 1)),
                        ey - (((fx_t)wy << FX_SHIFT) + (FX_ONE >> 1)));
    pov_render(tg_buf, tg_bw, tg_bh, ex, ey, ang, 0);
    tg_valid = 1;
    purge_cache_range(&tg_valid, sizeof tg_valid);
    purge_cache_range(&tg_bx0, sizeof tg_bx0);
    purge_cache_range(&tg_by0, sizeof tg_by0);
    purge_cache_range(&tg_bw, sizeof tg_bw);
    purge_cache_range(&tg_bh, sizeof tg_bh);
}

/* Pepper outlets across the live world_map's visible wall faces (a wall cell
 * with an open orthogonal neighbour), appending to decals[] until it holds
 * `target` total. Two passes: count candidates, then place every stride-th so
 * they spread across the whole map instead of clustering in the first rows.
 * Each plate sits at the face's grid plane, centred on the cell, receptacle
 * height. Map-agnostic (scans the live world_map), so procgen reuses it too. */
void raycast_place_outlets(int target) {
    if (num_decals >= target) return;
    int count = 0;
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (world_map[y][x] == 0) continue;
            if (world_map[y][x-1] == 0 || world_map[y][x+1] == 0 ||
                world_map[y-1][x] == 0 || world_map[y+1][x] == 0) count++;
        }
    if (count == 0) return;
    int stride = count / (target - num_decals);
    if (stride < 1) stride = 1;

    int seen = 0;
    for (int y = 1; y < MAP_H - 1 && num_decals < target; y++)
        for (int x = 1; x < MAP_W - 1 && num_decals < target; x++) {
            if (world_map[y][x] == 0) continue;
            uint8_t axis; fx_t px, py;
            fx_t cx = ((fx_t)x << FX_SHIFT) + (FX_ONE >> 1);
            fx_t cy = ((fx_t)y << FX_SHIFT) + (FX_ONE >> 1);
            if      (world_map[y][x-1] == 0) { axis = 0; px = (fx_t)x     << FX_SHIFT; py = cy; }
            else if (world_map[y][x+1] == 0) { axis = 0; px = (fx_t)(x+1) << FX_SHIFT; py = cy; }
            else if (world_map[y-1][x] == 0) { axis = 1; px = cx; py = (fx_t)y     << FX_SHIFT; }
            else if (world_map[y+1][x] == 0) { axis = 1; px = cx; py = (fx_t)(y+1) << FX_SHIFT; }
            else continue;
            /* Never on the exit's cell: the fixed neighbour order above can
             * pick the door's face (plate on the jamb) or the exit hole's
             * (plate floating over the opening). */
            if (x == g_exit_wall_cx && y == g_exit_wall_cy) continue;
            if (x == g_exit_hole_cx && y == g_exit_hole_cy) continue;
            if ((seen++ % stride) != 0) continue;
            decals[num_decals++] = (decal_t){ px, py, FX(0.20), axis };
        }
}

/* True if (mx,my) is >= 5 cells from every existing partition centre, so newly
 * placed dividers spread out instead of piling up. */
static int partition_clear_of_others(fx_t mx, fx_t my) {
    for (int i = 0; i < num_partitions; i++) {
        fx_t ox = (partitions[i].x1 + partitions[i].x2) >> 1;
        fx_t oy = (partitions[i].y1 + partitions[i].y2) >> 1;
        if (FX_ABS(mx - ox) < FX(5) && FX_ABS(my - oy) < FX(5)) return 0;
    }
    return 1;
}

/* Pepper a few free-standing partition dividers into the live world_map's open
 * areas, appending to partitions[] until it holds `target` total. A candidate
 * needs a fully-open 2-row x (L+2)-col block (both sides of the divider plus a
 * cell past each end) so the divider floats in the room and you can always walk
 * around either end — it never seals a path. Alternating wallpaper style. The
 * hand-map analogue of the procgen partition placement to come. */
static void place_partitions_fixed(int target) {
    const int L = 3;
    uint8_t style = 0;
    /* Horizontal dividers: line y=gy, span cols cx..cx+L. */
    for (int gy = 3; gy < MAP_H - 3 && num_partitions < target; gy++)
        for (int cx = 2; cx < MAP_W - 2 - L && num_partitions < target; cx++) {
            int ok = 1;
            for (int x = cx - 1; x <= cx + L && ok; x++)
                if (world_map[gy-1][x] || world_map[gy][x]) ok = 0;
            if (!ok) continue;
            fx_t mx = ((fx_t)cx << FX_SHIFT) + ((fx_t)L << (FX_SHIFT - 1));
            fx_t my = (fx_t)gy << FX_SHIFT;
            if (!partition_clear_of_others(mx, my)) continue;
            partitions[num_partitions] = (partition_t){
                (fx_t)cx << FX_SHIFT, my, (fx_t)(cx + L) << FX_SHIFT, my };
            partition_style[num_partitions] = style & 1; style++;
            num_partitions++;
        }
    /* Vertical dividers: line x=gx, span rows cy..cy+L. */
    for (int gx = 3; gx < MAP_W - 3 && num_partitions < target; gx++)
        for (int cy = 2; cy < MAP_H - 2 - L && num_partitions < target; cy++) {
            int ok = 1;
            for (int y = cy - 1; y <= cy + L && ok; y++)
                if (world_map[y][gx-1] || world_map[y][gx]) ok = 0;
            if (!ok) continue;
            fx_t mx = (fx_t)gx << FX_SHIFT;
            fx_t my = ((fx_t)cy << FX_SHIFT) + ((fx_t)L << (FX_SHIFT - 1));
            if (!partition_clear_of_others(mx, my)) continue;
            partitions[num_partitions] = (partition_t){
                mx, (fx_t)cy << FX_SHIFT, mx, (fx_t)(cy + L) << FX_SHIFT };
            partition_style[num_partitions] = style & 1; style++;
            num_partitions++;
        }
}

/* The hand-tuned 32x32 Backrooms map + its two dividers. */
void raycast_load_fixed(void) {
    pedge_clear();                     /* fixed map: legacy partitions only */
    g_exit_wall_cx = g_exit_wall_cy = -1;   /* no stale exits (see load_lobby) */
    g_exit_hole_cx = g_exit_hole_cy = -1;
    /* Authored 32x32 map at the top-left of the live grid; rest is solid wall
     * (its own boundary seals the playable area, so the fill is never seen). */
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            world_map[r][c] = (r < AUTH_H && c < AUTH_W) ? fixed_map[r][c] : 1;
    partitions[0] = (partition_t){ FX(22), FX(22), FX(26), FX(22) };
    partitions[1] = (partition_t){ FX(20), FX(11), FX(20), FX(14) };
    num_partitions = 2;
    partition_style[0] = 1; partition_style[1] = 1;   /* both spotted polka-dot */
    /* Pepper several more free-standing dividers through the map's open rooms
     * (non-blocking — each floats with walkable ends). ~10% more wall variety. */
    place_partitions_fixed(8);
    /* All fixed-map dividers are low 3/4-height cubicle partitions you see over
     * (HobbyTown look). They sit in open bands with walls far behind, so the
     * ceiling correctly shows above them — and partial columns draw fewer
     * pixels, lightening the partition-heavy fixed map. */
    for (int i = 0; i < NUM_PARTITIONS_MAX; i++) {
        partition_height[i] = (i < num_partitions) ? 192 : 0;
    }
    raycast_stamp_partition_edges();   /* fixed-map dividers go first-class */
    g_lobby_ceiling = 0;
    g_map_lights = 0; g_map_n_lights = 0;   /* procedural grid; drop any custom map's fixtures */
    g_map_dark = 0; g_map_n_dark = 0;
    /* The original cutout lives here, on the map it was placed for. */
    standups_clear();
    for (unsigned i = 0; i < sizeof fixed_standups / sizeof fixed_standups[0]; i++)
        standups[num_standups++] = fixed_standups[i];
    /* Low-ceiling crawlspace tunnel: mark the long west-edge corridor (column 1,
     * rows 22-26) as low-ceiling cells. Collision, forced-crouch, light culling
     * and the slab render all derive from ceil_h[] now, so this is just "these
     * cells have a low ceiling" — place more anywhere by marking more cells. */
    ceil_h_clear();
    ceil_h_add_run_h(1, 22, 0, 1, 5, CRAWL_CEIL_H);   /* col 1, rows 22-26 — one capped run */
    /* Wall outlet on the east wall of the spawn corridor (col 17, west face,
     * rows 24-28), ~2 cells ahead and just right of spawn so it reads on the
     * way out. X 0.16 west of the X=17 face so it sits just in front; z=0.20
     * receptacle height (same as the lobby outlet). */
    /* One curated outlet on the spawn-corridor wall (col-17 west face, x=17.0
     * plane, y=26.5 — ~2 cells ahead-right of spawn), then ~11 more peppered
     * across the map's visible wall faces. */
    num_decals = 0;
    decals[num_decals++] = (decal_t){ FX(17.0), FX(26.5), FX(0.20), 0, 0 };
    raycast_place_outlets(12);
    /* The fire door + green EXIT sign, embedded full-height in the south wall
     * (y=31) directly behind spawn (player faces north). Turn around and the
     * "way out" is right there — flat on the wall, foreshortening as you move,
     * doing nothing. axis 1 = wall runs along X; kind 1 = door. */
    decals[num_decals++] = (decal_t){ FX(16.5), FX(31.0), DECAL_DOOR_Z, 1, 1 };
    g_door_open = g_door_target = 0;          /* start shut on every (re)load */
    SHARED_UC->door_open = 0;
    player.x = FX(16.5); player.y = FX(28.5); player.angle = 192;
}

/* Load a hand-authored map from the generated custom_maps[] table. Replays the
 * POD descriptor (tools/gen_maps.py emits it from a .map file) into the live
 * world exactly like raycast_load_fixed does — this is the ONE place that knows
 * the engine-private decal_t / partition decor encodings, so the generated
 * custom_maps.c stays pure data. */
void raycast_load_custom(int idx) {
    if (idx < 0 || idx >= custom_map_count) idx = 0;
    const custom_map_t *m = &custom_maps[idx];

    /* Grid: authored w*h into the top-left of the live MAP_W*MAP_H, rest wall
     * (size-aware — no AUTH_W/H assumption). */
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            world_map[r][c] = (r < m->h && c < m->w) ? m->grid[r * m->w + c] : 1;

    /* Partitions are FIRST-CLASS: cell-edge flags the DDA hits natively
     * (full-height edges terminate the ray like walls; partials fill the
     * overlay band slots). The codegen pre-rasterized the authored segments
     * into these edge lists. */
    pedge_clear();
    for (int i = 0; i < m->n_pedges; i++) {
        uint8_t ef = m->pedges[i].flags;
        int ex = m->pedges[i].x, ey = m->pedges[i].y;
        if (ef & CM_PEDGE_AXIS_N) {
            if (ex < MAP_W && ey <= MAP_H) { pedge_n[ey][ex] = ef; g_pedge_any = 1; }
        } else {
            if (ex <= MAP_W && ey < MAP_H) { pedge_w[ey][ex] = ef; g_pedge_any = 1; }
        }
    }
    /* The authoring buffers stay empty here — custom maps arrive
     * pre-rasterized; only the lobby/fixed/procgen loaders author
     * partitions[] segments (converted by raycast_stamp_partition_edges). */
    num_partitions = 0;
    pedge_build_cells();

    /* Authored fixtures (init_lights reads these when raycast_init runs next).
     * n_lights == 0 leaves the procedural grid in charge. */
    g_map_lights   = m->lights;
    g_map_n_lights = m->n_lights;
    g_map_dark     = m->dark;
    g_map_n_dark   = m->n_dark;

    /* Ceiling: full everywhere, then the low-ceiling crawl runs. */
    g_lobby_ceiling = m->lobby_ceiling;
    ceil_h_clear();
    /* Slab height comes from the map now (registry crawl.h names -> ceil_h
     * values); 0 from older generated tables means classic crawl. */
    for (int i = 0; i < m->n_crawls; i++)
        ceil_h_add_run_h(m->crawls[i].cx, m->crawls[i].cy,
                         m->crawls[i].dx, m->crawls[i].dy, m->crawls[i].len,
                         m->crawls[i].h ? m->crawls[i].h : CRAWL_CEIL_H);

    /* Exit-path state is per-map: cleared here, and the BFS DOOR placement
     * runs BEFORE the decal loop — exit_place_common clears both exit states
     * on entry, so running it after would wipe an authored exit_hole decal
     * (the testbed's hole vanished under its place_exit_door option). */
    for (int i = 0; i < MAP_H; i++) exit_path_bits[i] = 0;
    g_exit_wall_cx = g_exit_wall_cy = -1;
    g_exit_hole_cx = g_exit_hole_cy = -1;
    num_decals = 0;
    standups_clear();
    if (m->place_exit_door) raycast_place_exit_door();

    /* Decals: explicit placements after the door (both append to decals[]).
     * Kind 2 is a FREE-STANDING cutout, not a wall decal — it belongs in
     * standups[] (drawn as a billboard, collides). Routing it into decals[]
     * was why authored neanderthals neither showed nor blocked. */
    for (int i = 0; i < m->n_decals; i++) {
        /* Free-standing objects live in standups[]: they billboard/render in
         * world and collide. Everything else is a wall-anchored decal. The
         * test is the registry's own STANDALONE flag (via sprite_defs), so a
         * community-submitted standee spawns with zero loader edits — the
         * old hardcoded kind==2||3 silently dropped any new kind. */
        int knd = m->decals[i].kind;
        int standalone = knd >= 0 && knd < SPRITE_DEF_COUNT &&
                         (sprite_defs[knd].flags & SPRITE_F_STANDALONE);
        if (standalone) {
            if (num_standups < MAX_STANDUPS) {
                standups[num_standups].x            = m->decals[i].x;
                standups[num_standups].y            = m->decals[i].y;
                standups[num_standups].facing_angle = m->decals[i].facing;
                standups[num_standups].silhouette   = 0;
                standups[num_standups].kind         = m->decals[i].kind;
                /* Authored desk-mounted PVM: the pvm token with z=1 (the z
                 * field is unused by free-standing kinds — it is the flag). */
                if (m->decals[i].kind == PVM_ASSET_KIND &&
                    m->decals[i].z >= FX(1))
                    standup_on_desk[num_standups] = 1;
                num_standups++;
            }
        } else if (m->decals[i].kind == 4) {
            /* AUTHORED EXIT HOLE (registry kind 4): not a rendered decal —
             * it programs the hole state directly. The wall side is derived
             * from the live grid (the solid cell adjacent to the face plane),
             * so authors just place it on a wall face like an outlet. */
            int axis = m->decals[i].axis;
            fx_t plane = axis ? m->decals[i].y : m->decals[i].x;
            fx_t c0    = axis ? m->decals[i].x : m->decals[i].y;
            int pi = FX_INT(plane), ci = FX_INT(c0);
            int ax0 = axis ? ci : pi,     ay0 = axis ? pi : ci;      /* cell past the plane */
            int ax1 = axis ? ci : pi - 1, ay1 = axis ? pi - 1 : ci;  /* cell before it */
            if (ax0 >= 0 && ax0 < MAP_W && ay0 >= 0 && ay0 < MAP_H &&
                ax1 >= 0 && ax1 < MAP_W && ay1 >= 0 && ay1 < MAP_H &&
                (world_map[ay0][ax0] != 0) != (world_map[ay1][ax1] != 0)) {
                int w_after = world_map[ay0][ax0] != 0;
                g_exit_hole_cx = w_after ? ax0 : ax1;
                g_exit_hole_cy = w_after ? ay0 : ay1;
                g_exit_hole_ax = w_after ? ax1 : ax0;
                g_exit_hole_ay = w_after ? ay1 : ay0;
                g_exit_hole_axis  = (uint8_t)axis;
                g_exit_hole_plane = plane;
                g_exit_hole_c0    = c0;
                g_exit_hole_dir   = w_after ? 1 : -1;
            }
        } else if (num_decals < 16) {
            decals[num_decals++] = (decal_t){ m->decals[i].x, m->decals[i].y,
                m->decals[i].z, m->decals[i].axis, m->decals[i].kind };
        }
    }
    if (m->place_outlets)   raycast_place_outlets(m->place_outlets);
    g_door_open = g_door_target = 0;
    SHARED_UC->door_open = 0;

    player.x = m->spawn_x; player.y = m->spawn_y; player.angle = m->spawn_angle;
}

/* Stamp the recurring EXIT (door or ceiling hole) into the live map as a
 * SURPRISE you have to find: flood-fill the cells actually reachable from
 * spawn, then drop it on the FARTHEST reachable spot — a wall face for the
 * door, any open cell for the hole. Deep in the level, but always walkable-to
 * (never sealed off), and the spawn->exit corridor is recorded in
 * exit_path_bits either way. Only ever opens into the next level. */
static void exit_place_common(int want_hole) {
    const int SPAWN_CX = 16, SPAWN_CY = 28;   /* must match procgen.c */
    static uint8_t reach[MAP_H][MAP_W];
    static uint16_t queue[MAP_H * MAP_W];
    /* BFS tree as a DIRECTION byte (index into dxs/dys of the step that
     * reached the cell) — half the RAM of a cell-index array; the link
     * budget is tight enough that the uint16 version overflowed `ram'. */
    static uint8_t parent_dir[MAP_H * MAP_W];
    for (int y = 0; y < MAP_H; y++) {
        exit_path_bits[y] = 0;
        for (int x = 0; x < MAP_W; x++) reach[y][x] = 0;
    }
    g_exit_wall_cx = g_exit_wall_cy = -1;
    g_exit_hole_cx = g_exit_hole_cy = -1;

    /* BFS over open cells from the spawn (16,28 — cleared by the vestibule). */
    int head = 0, tail = 0;
    reach[SPAWN_CY][SPAWN_CX] = 1;
    queue[tail++] = (uint16_t)(SPAWN_CY * MAP_W + SPAWN_CX);
    static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int c = queue[head++], cx = c % MAP_W, cy = c / MAP_W;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (reach[ny][nx] || world_map[ny][nx] != 0) continue;
            reach[ny][nx] = 1;
            parent_dir[ny * MAP_W + nx] = (uint8_t)k;
            queue[tail++] = (uint16_t)(ny * MAP_W + nx);
        }
    }

    /* Farthest reachable open cell with a wall face to mount the exit on —
     * the hinged door and the climb-in hole share the same real estate. */
    int best_d2 = -1, best_axis = 0;
    int best_ox = -1, best_oy = -1, best_wx = -1, best_wy = -1;
    fx_t best_px = 0, best_py = 0;
    for (int oy = 1; oy < MAP_H - 1; oy++) {
        for (int ox = 1; ox < MAP_W - 1; ox++) {
            if (!reach[oy][ox]) continue;
            int d2 = (ox - SPAWN_CX) * (ox - SPAWN_CX)
                   + (oy - SPAWN_CY) * (oy - SPAWN_CY);
            if (d2 <= best_d2) continue;
            fx_t cx = ((fx_t)ox << FX_SHIFT) + FX(0.5);
            fx_t cy = ((fx_t)oy << FX_SHIFT) + FX(0.5);
            int axis, found = 1, wx = ox, wy = oy; fx_t px, py;
            if      (world_map[oy][ox + 1]) { axis = 0; px = (fx_t)(ox + 1) << FX_SHIFT; py = cy; wx = ox + 1; }
            else if (world_map[oy][ox - 1]) { axis = 0; px = (fx_t)ox       << FX_SHIFT; py = cy; wx = ox - 1; }
            else if (world_map[oy + 1][ox]) { axis = 1; px = cx; py = (fx_t)(oy + 1) << FX_SHIFT; wy = oy + 1; }
            else if (world_map[oy - 1][ox]) { axis = 1; px = cx; py = (fx_t)oy       << FX_SHIFT; wy = oy - 1; }
            else found = 0;
            if (found) {
                best_d2 = d2; best_axis = axis; best_px = px; best_py = py;
                best_ox = ox; best_oy = oy; best_wx = wx; best_wy = wy;
            }
        }
    }

    int committed = 0;
    if (best_d2 >= 0 && want_hole) {
        /* Hole: same wall face the door would take, but instead of a decal
         * it's a carved-void render + climb trigger. Record the face. */
        g_exit_hole_cx = best_wx;  g_exit_hole_cy = best_wy;
        g_exit_hole_ax = best_ox;  g_exit_hole_ay = best_oy;
        g_exit_hole_axis  = (uint8_t)best_axis;
        g_exit_hole_plane = best_axis ? best_py : best_px;
        g_exit_hole_c0    = best_axis ? best_px : best_py;
        g_exit_hole_dir   = best_axis ? (best_wy - best_oy) : (best_wx - best_ox);
        /* Wall cell joins the protected set: the crawl carver must not
         * tunnel through the hole's cavity. */
        exit_path_bits[best_wy] |= 1u << best_wx;
        committed = 1;
    } else if (best_d2 >= 0 && num_decals < (int)(sizeof decals / sizeof decals[0])) {
        decals[num_decals++] =
            (decal_t){ best_px, best_py, DECAL_DOOR_Z, (uint8_t)best_axis, 1 };
        /* The door's wall cell joins the protected set so the crawl carver
         * can't tunnel through the cavity itself. */
        g_exit_wall_cx = best_wx; g_exit_wall_cy = best_wy;
        exit_path_bits[best_wy] |= 1u << best_wx;
        committed = 1;
    }
    if (committed) {
        /* Mark the protected corridor: approach -> spawn via the BFS tree
         * (each hop is the cell it was first reached from, so this is a
         * shortest walkable path). */
        int c = best_oy * MAP_W + best_ox;
        int spawn_c = SPAWN_CY * MAP_W + SPAWN_CX;
        for (int guard = 0; guard < MAP_W * MAP_H; guard++) {
            exit_path_bits[c / MAP_W] |= 1u << (c % MAP_W);
            if (c == spawn_c) break;
            int k = parent_dir[c];            /* step that reached c: walk it back */
            c -= dys[k] * MAP_W + dxs[k];
        }
    }
    g_door_open = g_door_target = 0;
    SHARED_UC->door_open = 0;
}

void raycast_place_exit_door(void) { exit_place_common(0); }
void raycast_place_exit_hole(void) { exit_place_common(1); }

/* Pull-up trigger: standing near the middle of the hole's APPROACH cell,
 * facing the wall it's carved into ("walk up to it and climb"). The game
 * loop turns this into the climb-out (freeze input, ramp eye+pitch, portal). */
int raycast_exit_hole_check(void) {
    if (g_exit_hole_cx < 0) return 0;
    fx_t hx = ((fx_t)g_exit_hole_ax << FX_SHIFT) + FX(0.5);
    fx_t hy = ((fx_t)g_exit_hole_ay << FX_SHIFT) + FX(0.5);
    if (FX_ABS(player.x - hx) >= FX(0.25) || FX_ABS(player.y - hy) >= FX(0.25))
        return 0;
    /* Facing the face: approach->wall is cardinal; angle 0 = +x, 64 = +y. */
    int dx = g_exit_hole_cx - g_exit_hole_ax;
    int dy = g_exit_hole_cy - g_exit_hole_ay;
    uint8_t want = (dx > 0) ? 0 : (dx < 0) ? 128 : (dy > 0) ? 64 : 192;
    uint8_t diff = (uint8_t)(player.angle - want);
    return diff < 40 || diff > 216;
}


/* The A interaction on a PVM: toggle the nearest powered screen within reach.
 * Reach is a 1.2-cell box plus a front-hemisphere dot test — same spirit as
 * the exit-hole check (committed, never stumbled into: the caller only calls
 * on a FRESH A press). Returns 1 if a set was toggled, so the caller can gate
 * any competing A action behind it. */
/* "I am looking AT the console": half-angle of the aim cone as a tangent
 * (15 deg; 20 deg = 0.364, 30 deg = 0.577), plus the set's own half-width so
 * the cone is measured to its body rather than to a point. */
#define PVM_USE_CONE_TAN FX(0.268)
#define PVM_USE_HALF_W   FX(0.35)

int raycast_pvm_use(void) {
    int best = -1;
    fx_t bestm = 0;
    fx_t pdx = COS_FX(player.angle), pdy = SIN_FX(player.angle);
    for (int i = 0; i < num_standups; i++) {
        if (standups[i].kind != PVM_ASSET_KIND || standup_down[i]) continue;
        fx_t dx = standups[i].x - player.x, dy = standups[i].y - player.y;
        if (FX_ABS(dx) >= FX(1.2) || FX_ABS(dy) >= FX(1.2)) continue;
        fx_t dot = FX_MUL(pdx, dx) + FX_MUL(pdy, dy);
        if (dot <= 0) continue;                                 /* behind us */
        /* The DESK unit boots the Master System — a whole-screen commitment,
         * so it asks for a deliberate look rather than a shoulder brush.
         * Measured to the SET, not to a mathematical point: the view axis
         * may miss the origin by tan(15 deg) of the distance PLUS the set's
         * own half-width. A pure point-cone was far too strict up close —
         * standing right in front of a monitor, its body subtends much more
         * than 15 degrees, so looking straight at it still missed the
         * origin and the press died. cross is the perpendicular distance
         * from the view ray (pd is unit), so no sqrt anywhere. Ordinary
         * sets keep the loose reach — slapping one on as you pass is the
         * point of them. */
        if (standup_on_desk[i]) {
            fx_t cross = FX_MUL(pdx, dy) - FX_MUL(pdy, dx);
            if (FX_ABS(cross) > FX_MUL(dot, PVM_USE_CONE_TAN) + PVM_USE_HALF_W)
                continue;
        }
        fx_t m = FX_ABS(dx) + FX_ABS(dy);
        if (best < 0 || m < bestm) { best = i; bestm = m; }
    }
    if (best < 0) return 0;
    if (standup_on_desk[best]) {
        /* The desk unit is the CONSOLE: A boots the Master System game
         * (caller sees 2 and runs the transition), it never toggles off
         * by hand. First press wakes it with the full strike. */
        if (!standup_power[best]) {
            standup_power[best] = 1;
            standup_scr_mode[best] = 0;                  /* static while booting */
            standup_bloom_start[best] = (uint16_t)SHARED_UC->frame_count;
            SHARED_UC->crt_sfx = 1;                      /* degauss */
        }
        return 2;
    }
    standup_power[best] ^= 1;
    /* Stamp BOTH edges: power-on plays the strike+unfold, power-off the
     * phosphor collapse (same window, mode picked by power state). */
    standup_bloom_start[best] = (uint16_t)SHARED_UC->frame_count;
    /* Every power CYCLE alternates the screen: static, telegraph, static...
     * Flipped on the OFF edge, so the FIRST wake of a found-dead set shows
     * plain static and the telegraph is the next cycle's reveal. */
    if (!standup_power[best]) standup_scr_mode[best] ^= 1;
    /* The switch is audible: degauss thunk on power-on, dying whine on off
     * (one-shot request to the mixer, same channel style as the climb). */
    SHARED_UC->crt_sfx = standup_power[best] ? 1 : 2;
    return 1;
}

/* Power down the desk console on return from the full-screen game: the
 * collapse animation plays as the world comes back — you watched it die. */
void raycast_pvm_desk_off(void) {
    for (int i = 0; i < num_standups; i++)
        if (standup_on_desk[i] && standup_power[i]) {
            standup_power[i] = 0;
            standup_bloom_start[i] = (uint16_t)SHARED_UC->frame_count;
            SHARED_UC->crt_sfx = 2;                      /* the off click */
        }
}

/* Portal check: returns 1 when the EXIT door is open far enough AND the player
 * has stepped into its doorway — the cue for the game loop to fade through into
 * a fresh procedurally generated map. The "exit" only loops you deeper in. */
int raycast_door_portal_check(void) {
    /* Black-void exit (world_map cell == 2 — e.g. the lobby's dark doorway):
     * no hinged door to open, so stepping against the void cell portals out
     * immediately. This is what makes the lobby exit work when it's re-entered
     * as a live map mid-game (MAPS-tab warp), not just in the intro Phase B. */
    int pcx = FX_INT(player.x), pcy = FX_INT(player.y);
    if ((pcx + 1 < MAP_W && world_map[pcy][pcx + 1] == 2) ||
        (pcx - 1 >= 0    && world_map[pcy][pcx - 1] == 2) ||
        (pcy + 1 < MAP_H && world_map[pcy + 1][pcx] == 2) ||
        (pcy - 1 >= 0    && world_map[pcy - 1][pcx] == 2)) return 1;
    /* Hinged exit door (procgen / fixed maps): must be opened, then stepped into. */
    if (g_door_open < DOOR_OPEN_MAX * 3 / 4) return 0;
    for (int d = 0; d < num_decals; d++) {
        if (decals[d].kind != 1) continue;
        /* Stepped into the doorway (any orientation). */
        if (FX_ABS(player.x - decals[d].x) < FX(0.7) &&
            FX_ABS(player.y - decals[d].y) < FX(0.7)) return 1;
    }
    return 0;
}

/* Load the tiny 8x8 lobby: the grid box (lobby_map) plus the free-standing
 * wallpaper PARTITION that IS the photo's divider. Spawn (X) sits bottom-
 * west facing north so the divider stands on your right; walk up the west
 * side, across the top, and out the east exit doorway (col 10, rows 2-4) to
 * enter the chosen level. */
void raycast_load_lobby(void) {
    pedge_clear();                     /* lobby: legacy partitions only (inc 3) */
    /* The lobby has no exits of its own; a STALE hole/door from the previous
     * map would keep draw_exit_hole live every frame AND arm the climb gate
     * at a phantom cell. procgen/custom clear these; the lobby never did. */
    g_exit_wall_cx = g_exit_wall_cy = -1;
    g_exit_hole_cx = g_exit_hole_cy = -1;
    /* Authored 32x32 lobby at the top-left of the live grid; rest solid wall. */
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            world_map[r][c] = (r < AUTH_H && c < AUTH_W) ? lobby_map[r][c] : 1;
    /* Free-standing wallpaper PARTITION dividers, per the sketch (5x5):
     *  - T-divider top-left: vertical stem (x=3, rows 2-3) + arm (row 3, x3->5)
     *  - entrance wall (row 5) split by a centre gap (cols 3-4) you walk up. */
    partitions[0] = (partition_t){ FX(3), FX(2), FX(3), FX(4) };  /* T stem     */
    partitions[1] = (partition_t){ FX(3), FX(3), FX(4), FX(3) };  /* T arm (1 cell) */
    partitions[2] = (partition_t){ FX(2), FX(6), FX(4), FX(6) };  /* entrance L (depth +1, wall @ y=6) */
    partitions[3] = (partition_t){ FX(5), FX(6), FX(7), FX(6) };  /* entrance R */
    num_partitions = 4;
    /* T-stem, T-arm, entrance-L = spotted olive wallpaper; entrance-R (the
     * outlet wall) = chevron, same as the main walls (per the reference). */
    partition_style[0] = 1; partition_style[1] = 1;
    partition_style[2] = 1; partition_style[3] = 0;
    /* T-divider per the HobbyTown reference: the N/S stem (partitions[0],
     * vertical) runs full height to the ceiling; the E/W arm (partitions[1],
     * horizontal) is a low 3/4-height cubicle divider you see over. Entrance
     * walls stay full (room boundary / outlet wall). */
    partition_height[0] = 0;   partition_height[1] = 192;   /* stem full, arm 3/4 */
    partition_height[2] = 0;   partition_height[3] = 0;
    /* Crawl-under beam shelved — no crawl elements placed for now. */
    g_lobby_ceiling = 1;                  /* hand-authored fluorescent runs */
    g_map_lights = 0; g_map_n_lights = 0;   /* lobby uses its own built-in runs */
    g_map_dark = 0; g_map_n_dark = 0;
    standups_clear();                       /* no cutout in the lobby */
    ceil_h_clear();                       /* no crawlspaces in the lobby */
    /* Outlet on entrance-R's south face (the photo's right-hand partition),
     * low and right-of-center in the spawn/menu view. Placed FX(0.16) south
     * of the y=5 wall line so it sits just in front of the face, not inside
     * it; z=0.15 = standard receptacle height up the 1.0-tall wall. */
    /* Outlet embedded in entrance-R's south face (the partition line y=6),
     * centred at x=5.33. axis 1 = the wall runs along X, so the plate spans X
     * on the y=6 plane. z=0.20 receptacle height. */
    decals[0] = (decal_t){ FX(5.33), FX(6.0), FX(0.20), 1 };
    num_decals = 1;
    raycast_stamp_partition_edges();    /* lobby dividers go first-class */
    player.x = FX(5.0); player.y = FX(7.6); player.angle = 184;
}

/* Per-frame palette nudge on the brightest wall and ceiling entries —
 * mimics a dying fluorescent's flicker. Must be called from inside
 * vblank (after the COMM12 tick wait) to avoid mid-frame CRAM tearing. */
void raycast_shimmer(void) {
    /* Flicker AROUND the current tuned palette — never hardcode. This runs every
     * frame, so pulling the bright wall/ceil from the palette engine also makes
     * it the per-frame re-asserter of live COLOR-tab edits (fixing the old bug
     * where stale constants here stamped grey over the brightest ceiling and
     * clobbered every palette change). When shimmer is off the flicker is 0, so
     * it simply holds the tuned bright values. */
    int wr, wg, wb, cr, cg, cb;
    pal_effective(PSURF_WALL, &wr, &wg, &wb);
    pal_effective(PSURF_CEIL, &cr, &cg, &cb);
    int wall_f = 0, ceil_f = 0;
    if (SHARED_UC->lighting_flags & LIGHTING_SHIMMER) {
        static uint32_t frame_count = 0;
        frame_count++;
        uint32_t r = frame_count * 1103515245u + 12345u;   /* LCG, top bits cleanest */
        wall_f = (r >> 28) & 3;     /* 0..3, barely visible per pixel, reads as flicker */
        ceil_f = (r >> 26) & 3;
    }
    #define SUB(v, f) ((v) - (f) < 0 ? 0 : (v) - (f))
    Hw32xSetBGColor(WALL_BASE, SUB(wr, wall_f), SUB(wg, wall_f), wb);  /* wall B steady */
    Hw32xSetBGColor(CEIL_BASE, SUB(cr, ceil_f), SUB(cg, ceil_f), SUB(cb, ceil_f));
    #undef SUB
}

/* Asset-viewer backdrop: park the TUNED bright wallpaper yellow in CRAM 0
 * for the viewer's lifetime, restore black on exit. Index 0 is the one slot
 * nothing else will fight over — texel 0 is transparency system-wide (the
 * FB drops zero byte-writes), so no asset ever draws it, and the shimmer
 * only ever rewrites WALL_BASE/CEIL_BASE. Filling the screen with WALL_BASE
 * itself made the whole backdrop strobe with the fluorescent flicker. */
void raycast_backdrop_wall(int on) {
    if (on) {
        int wr, wg, wb;
        pal_effective(PSURF_WALL, &wr, &wg, &wb);
        Hw32xSetBGColor(0, wr, wg, wb);
    } else {
        Hw32xSetBGColor(0, 0, 0, 0);
    }
}

/* Paint the tuned wallpaper yellow into an arbitrary CRAM entry — the SMS
 * session frames its picture with the room's colour (m_main SMS_FS_FRAME).
 * Same source as the viewer backdrop, so it follows the COLOR lab live.
 * scale256: 256 = full wallpaper brightness (the zoom), less = dimmed (the
 * session's settled frame) — one palette write recolours every frame pixel
 * already on screen, no redraw. */
void raycast_paint_wallpaper_index(int idx, int scale256) {
    int wr, wg, wb;
    pal_effective(PSURF_WALL, &wr, &wg, &wb);
    Hw32xSetBGColor(idx, (wr * scale256) >> 8, (wg * scale256) >> 8,
                         (wb * scale256) >> 8);
}


/* Returns 1 if cell (x, y) is walkable, 0 if blocked or out of bounds. A void
 * EXIT cell (==2) is an opening you walk out through, so it's walkable — the
 * portal check fires as you reach it. */
static int cell_passable(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 0;
    return world_map[y][x] == 0 || world_map[y][x] == 2;
}

/* Player's body radius in world cells — used by both wall and partition
 * collision so the camera maintains a small visible gap from any
 * surface. 0.25 = 25 cm ≈ 10". Larger feels sluggish, smaller lets
 * the camera press up against a wall and break the immersion. */
#define PLAYER_RADIUS FX(0.25)

/* Returns 1 if (px, py) would intersect a SOLID standup (the cardboard
 * cutout), treated as a small axis-aligned box + player radius. Silhouette
 * "watcher" standups stay intangible — they're meant to be a glimpse, not a
 * wall. Makes the cutout a real free-standing obstacle you bump into. */
/* It's a CARDBOARD CUTOUT, so the box is a panel, not a pillar: barely there
 * along its facing normal, shoulder-wide across. The old box was square
 * (0.12 both axes -> a 0.74 square with the player radius), which read as an
 * invisible column and made a 1-cell corridor impassable. Thin-face is both
 * truer and more uncanny: it stops you dead face-on, but you can slip past
 * its edge. */
#define STANDUP_HALF_THICK FX(0.03)   /* cardboard, face-on */
#define STANDUP_HALF_WIDTH FX(0.08)   /* slim: was 0.20 (+0.25 player radius = 0.9-wide
                                       * blocker, filled a corridor). A person's core,
                                       * not their shoulders — bump face-on, slip the edge. */
/* Box models are declared further down (they need cbox_t + the model headers);
 * collision needs them here. */
/* base: the 4-deep CRAM shade ramp the model's faces paint (base + shade 0..3,
 * fog walks it down). The chair and desk both shipped on the chair's dark wood
 * ramp — the flat fill and the far-billboard vmap hardcoded CHAIR_BASE — so
 * their rows pin that look on purpose; a new import brings its OWN ramp (the
 * gray PVM was the tell: it rendered wood-brown). ftex: optional face texture
 * painted on box 0's -z (front) face instead of the flat fill, same 1..4 ramp
 * values, 0 nowhere (a box face is opaque). */
/* stand_bias: shade steps subtracted from every box EXCEPT box 0 (the carve's
 * primary mass) — a two-tone model on one ramp: the PVM's monitor stays case
 * gray while its cart drops to the ramp's near-black end. Costs nothing per
 * pixel (same fill, darker index); the dir bake applies the same bias
 * (bake_dir_sprites.py --stand-bias), so near/far/viewer agree. */
typedef struct boxmodel_s { const cbox_t *boxes; uint8_t nboxes; uint8_t kind; uint8_t base;
                 const uint8_t *ftex; uint8_t ftw, fth;
                 uint8_t stand_bias;
                 /* optional per-box ramp override (composite models mix
                  * materials: the desk_pvm's monitor is case-gray, its desk
                  * wood-brown). NULL = every box uses .base. */
                 const uint8_t *box_base;
                 /* optional per-box stand_bias override, same shape as
                  * box_base and for the same reason. The scalar assumes ONE
                  * secondary mass ("everything that is not box 0 is the
                  * stand"), which a composite breaks: the desk set inherited
                  * the PVM's bias of 2 and applied it to the DESK, subtracting
                  * two steps from a 0..3 ramp and collapsing the pedestals'
                  * front and side faces onto the same index — a desk with no
                  * light gradient, lit top and flat everything else. NULL =
                  * the scalar rule. */
                 const uint8_t *box_bias;
                 /* optional REAR-face texture (box 0, +z): same dims as
                  * ftex by the editor-kit contract, case values only —
                  * never both visible with the front, so it costs no
                  * frame time. */
                 const uint8_t *rtex;
                 /* optional COMPOSITE variant of this kind's model (the
                  * PVM's desk-mounted set). The asset viewer walks this —
                  * a new composite is one pointer, never a new enum. */
                 const struct boxmodel_s *alt;
                 const char *alt_name; } boxmodel_t;
/* Shade steps to subtract from box b: the per-box table when a model carries
 * one, else the scalar rule (box 0 is the primary mass and keeps full range). */
static inline int boxmodel_bias(const boxmodel_t *bm, int b) {
    if (bm->box_bias) return bm->box_bias[b];
    return (b > 0) ? bm->stand_bias : 0;
}
static const boxmodel_t *boxmodel_for_kind(int kind);
static void boxmodel_footprint(int kind, fx_t *hx, fx_t *hz);
static void boxmodel_footprint_bm(const boxmodel_t *bm, fx_t wh,
                                  fx_t *hx, fx_t *hz);
static const boxmodel_t *boxmodel_for_standup(int i);
static fx_t world_h_for_standup(int i);

/* Index of the solid cutout containing (px,py), or -1. */
static int standup_blocker(fx_t px, fx_t py) {
    for (int i = 0; i < NUM_STANDUPS; i++) {
        if (standups[i].silhouette || standup_down[i]) continue;  /* toppled = walk over it */
        if (standups[i].kind == CHAIR_SPRITE_KIND) {
            /* Furniture, not a wall: a slim square with a pad SMALLER than
             * PLAYER_RADIUS, so a chair centered in a 1-cell hallway leaves
             * a passable edge — you squeeze past a chair closer than you'd
             * hug a wall. (footprint 0.10 + 0.10 pad = 0.20 half-extent vs
             * the hall's 0.25 of centre clearance.) */
            const fx_t m = FX(0.20);
            fx_t cdx = px - standups[i].x, cdy = py - standups[i].y;
            if (cdx > -m && cdx < m && cdy > -m && cdy < m) return i;
            continue;
        }
        if (boxmodel_for_standup(i)) {
            /* Any OTHER box model blocks over its real footprint. Without this
             * an imported model inherited the flat-cutout slab below, so you
             * could walk into a desk that is nearly a cell wide — and standing
             * inside it put vertices behind the near clip, which drops whole
             * faces and makes the model look hollow from the inside out.
             * Footprint comes from the box list itself, so it can never drift
             * from the geometry being drawn. */
            fx_t hx, hz;
            boxmodel_footprint_bm(boxmodel_for_standup(i),
                                  world_h_for_standup(i), &hx, &hz);
            /* facing: E0 S64 W128 N192 — N/S facers present width along X.
             * Desk composites get a wider bubble: 0.10 lets you press your
             * face in until the near clip eats the desktop (invisible
             * tabletop, Mike's screenshot); 0.30 keeps every face whole. */
            fx_t pad = standup_on_desk[i] ? FX(0.30) : FX(0.10);
            int ns = (standups[i].facing_angle == 64 || standups[i].facing_angle == 192);
            fx_t mx = (ns ? hx : hz) + pad;
            fx_t my = (ns ? hz : hx) + pad;
            fx_t cdx = px - standups[i].x, cdy = py - standups[i].y;
            if (cdx > -mx && cdx < mx && cdy > -my && cdy < my) return i;
            continue;
        }
        /* facing: E0 S64 W128 N192 — N/S facers present their face along Y. */
        int ns = (standups[i].facing_angle == 64 || standups[i].facing_angle == 192);
        fx_t mx = (ns ? STANDUP_HALF_WIDTH : STANDUP_HALF_THICK) + PLAYER_RADIUS;
        fx_t my = (ns ? STANDUP_HALF_THICK : STANDUP_HALF_WIDTH) + PLAYER_RADIUS;
        fx_t dx = px - standups[i].x;
        fx_t dy = py - standups[i].y;
        if (dx > -mx && dx < mx && dy > -my && dy < my) return i;
    }
    return -1;
}
static int standup_collides(fx_t px, fx_t py) { return standup_blocker(px, py) >= 0; }

/* Raised when a move was refused ONLY because a bulkhead header wanted the
 * head lower. player_update consumes it as an automatic duck, so the player
 * walks through doorway soffits without ever pressing crouch. Duct slabs
 * never raise it — those keep the deliberate crawl. Primary CPU only. */
static uint8_t duck_bump = 0;

static int position_clear(fx_t px, fx_t py) {
    /* Check all 4 corners of the player's bounding box against wall
     * cells. The 4-CARDINAL check (N/S/E/W edges) missed diagonal
     * corner clips — when player approaches an isolated 1-cell pillar
     * from a diagonal, all 4 cardinal points land in walkable cells
     * while the box's corner clips the pillar. Symptom was thin
     * "phantom walls" that the player could walk through in hallways
     * with isolated pillar columns. The 4-CORNER check catches this:
     * each diagonal corner of the box is tested against the cell it
     * lands in, so any pillar touching any corner blocks the move. */
    int xL = FX_INT(px - PLAYER_RADIUS);
    int xR = FX_INT(px + PLAYER_RADIUS);
    int yT = FX_INT(py - PLAYER_RADIUS);
    int yB = FX_INT(py + PLAYER_RADIUS);
    if (!cell_passable(xL, yT)) return 0;
    if (!cell_passable(xR, yT)) return 0;
    if (!cell_passable(xL, yB)) return 0;
    if (!cell_passable(xR, yB)) return 0;
    /* Slab partitions: block CROSSING a flagged line, and keep the body a
     * margin off it (slab half-thickness + player radius — the same 0.4
     * feel as the original box collision, so the camera never clips into
     * the slab's own thickness). */
    if (g_pedge_any) {
        int fxc = FX_INT(player.x), fyc = FX_INT(player.y);
        int txc = FX_INT(px), tyc = FX_INT(py);
        if (txc != fxc) {
            int line = txc > fxc ? txc : fxc;
            if ((unsigned)fyc < (unsigned)MAP_H && line >= 0 && line <= MAP_W &&
                (pedge_w[fyc][line] & CM_PEDGE_PRESENT)) return 0;
        }
        if (tyc != fyc) {
            int line = tyc > fyc ? tyc : fyc;
            if ((unsigned)fxc < (unsigned)MAP_W && line >= 0 && line <= MAP_H &&
                (pedge_n[line][fxc] & CM_PEDGE_PRESENT)) return 0;
        }
        {
            const fx_t EPAD = PART_HALF_THICK + PLAYER_RADIUS;
            int cx = FX_INT(px), cy = FX_INT(py);
            fx_t frx = px & 0xFFFF, fry = py & 0xFFFF;
            if ((unsigned)cx < (unsigned)MAP_W && (unsigned)cy < (unsigned)MAP_H) {
                if (frx < EPAD && (pedge_w[cy][cx] & CM_PEDGE_PRESENT)) return 0;
                if (frx > FX_ONE - EPAD &&
                    (pedge_w[cy][cx + 1] & CM_PEDGE_PRESENT)) return 0;
                if (fry < EPAD && (pedge_n[cy][cx] & CM_PEDGE_PRESENT)) return 0;
                if (fry > FX_ONE - EPAD &&
                    (pedge_n[cy + 1][cx] & CM_PEDGE_PRESENT)) return 0;
            }
        }
    }
    if (standup_collides(px, py))   return 0;
    /* Low-ceiling cells hang below standing head height, so entering one costs
     * head room — but HOW MUCH depends on the cell. A duct slab still demands
     * the deliberate A+B crawl; a bulkhead header only wants a dipped head, so
     * a refusal there raises duck_bump and player_update ducks for you on the
     * next frame — walk into a doorway and you stoop through it instead of
     * bouncing off. Per-cell via ceil_h[], so this covers every map. */
    if (g_lowceil_active) {
        int cx = FX_INT(px), cy = FX_INT(py);
        if ((unsigned)cx < MAP_W && (unsigned)cy < MAP_H) {
            uint8_t ch = CEIL_H(cy, cx);
            if (ch != CEIL_H_FULL) {
                int header = (ch >= BULKHEAD_CEIL_H);
                int need = header ? BULKHEAD_PASS_EYE : CRAWL_PASS_EYE;
                if ((int)SHARED_UC->eye_h >= need) {
                    if (header) duck_bump = 1;
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Head-bob state. bob_phase advances when the player is moving, and
 * raycast_render applies a small perpendicular position sway derived
 * from sin(bob_phase) before computing the camera basis. Cheap (just
 * two extra muls per frame) and reads as "walking through a hallway"
 * — the single biggest immersion bump per line of code. */
static uint8_t bob_phase   = 0;
static uint8_t is_walking  = 0;
static uint8_t is_running  = 0;   /* moving AND holding A (sprint) — speeds footstep cadence */
/* Eased manual pitch (signed pixels). C button drives it toward +40
 * (look down) when held, eases back to 0 on release. Walking pitch bob
 * (±1 from SIN_FX(bob_phase)) is added on top each frame. */
static int     pitch_smooth_y = 0;

/* CLIMB-INTO-THE-HOLE camera: called by the game loop each pull-up frame with
 * progress t/total. Four POV beats, all through channels the renderer already
 * follows (pitch_smooth_y, eye_h, player position — player_update is frozen
 * during the climb so the writes stick):
 *   A  PLANT   (3/16)   hands to the sill, gaze DOWN to them, eye DIPS
 *   B  HAUL    (to 10)  the pull: eye clears the sill, gaze further down onto
 *                       the hole's floor, body draws halfway to the aperture
 *   C  HANG    (to 12)  balanced on the arms, eye settles back, gaze held
 *   D  SHIMMY  (rest)   torso through: gaze comes back up and forward, body
 *                       sways across the aperture, then the caller fades out
 * Tuning knobs (eye_h is 8.8 of room height, 128 = standing; pitch is +DOWN): */
#define PU_DIP_EYE      120   /* A: the load before the lift */
#define PU_TOP_EYE      176   /* B: head clears the sill */
#define PU_SET_EYE      164   /* C: weight settles onto the arms */
#define PU_END_EYE      160   /* D: crawl height */
#define PU_HANDS_PITCH   14   /* A: down at the sill you just grabbed */
#define PU_FLOOR_PITCH   22   /* B/C: down into the hole you are climbing onto */
#define PU_SHIMMY   FX(0.035) /* D: lateral wriggle across the face */
void raycast_exit_pullup(int t, int total) {
    static fx_t sx, sy;
    if (g_exit_hole_cx < 0 || total <= 0) { pitch_smooth_y = 0; return; }
    if (t <= 1) {
        sx = player.x; sy = player.y;
        SHARED_UC->slide_sfx = 2;      /* corridor shuffle, 30% drag (amb_shuffle) */
    }
    /* No footsteps during the climb: player_update is frozen, so is_walking
     * would hold whatever it was at the trigger — force it quiet. */
    is_walking = 0; is_running = 0;
    /* Aperture hover point: centered on the face, just outside the plane. */
    fx_t tx = g_exit_hole_axis ? g_exit_hole_c0
                               : (g_exit_hole_plane - g_exit_hole_dir * FX(0.12));
    fx_t ty = g_exit_hole_axis ? (g_exit_hole_plane - g_exit_hole_dir * FX(0.12))
                               : g_exit_hole_c0;
    /* FOUR beats. The old three glanced UP and then rode one linear ramp
     * forward, which reads as levitating: nothing about it costs the body
     * anything. A person hauling themselves into a hole gives it away with
     * where they LOOK — down at the hands they just planted, then down at the
     * floor of the hole for the whole lift (you watch what you are climbing
     * onto), and only at the very end back up and forward. Pitch is positive
     * DOWN here, which is why the old -22 was exactly backwards. */
    int a_end = total * 3 / 16;  if (a_end < 1) a_end = 1;
    int b_end = total * 10 / 16; if (b_end <= a_end) b_end = a_end + 1;
    int c_end = total * 12 / 16; if (c_end <= b_end) c_end = b_end + 1;
    if (c_end >= total) c_end = total - 1;
    fx_t mx = sx + ((tx - sx) >> 1), my = sy + ((ty - sy) >> 1);

    if (t <= a_end) {                                   /* A: PLANT */
        /* Hands onto the sill and the knees load — the eye DIPS before it
         * rises. That dip is the whole difference between a lift and a float. */
        int f = (t << 8) / a_end;
        pitch_smooth_y = (PU_HANDS_PITCH * f) >> 8;
        SHARED_UC->eye_h = (uint8_t)(128 - (((128 - PU_DIP_EYE) * f) >> 8));
    } else if (t <= b_end) {                            /* B: HAUL */
        /* The pull. Eye clears the sill while the gaze goes further DOWN, onto
         * the floor of the hole. Body travels half the distance to the mouth —
         * arms, not legs, so it is slower than the approach was. */
        int f = ((t - a_end) << 8) / (b_end - a_end);   /* 0..256 */
        pitch_smooth_y = PU_HANDS_PITCH
                       + (((PU_FLOOR_PITCH - PU_HANDS_PITCH) * f) >> 8);
        SHARED_UC->eye_h = (uint8_t)(PU_DIP_EYE
                       + (((PU_TOP_EYE - PU_DIP_EYE) * f) >> 8));
        player.x = sx + (fx_t)(((int64_t)(mx - sx) * f) >> 8);
        player.y = sy + (fx_t)(((int64_t)(my - sy) * f) >> 8);
    } else if (t <= c_end) {                            /* C: HANG */
        /* Weight transfers onto the arms at the top and the eye sinks back a
         * little. Two frames of held gaze, still looking down into the hole:
         * the beat where a person is balanced and not yet committed. */
        int f = ((t - b_end) << 8) / (c_end - b_end);
        pitch_smooth_y = PU_FLOOR_PITCH;
        SHARED_UC->eye_h = (uint8_t)(PU_TOP_EYE
                       - (((PU_TOP_EYE - PU_SET_EYE) * f) >> 8));
        player.x = mx; player.y = my;
    } else {                                            /* D: SHIMMY */
        /* Torso in. Only now does the gaze come back up and forward, and the
         * body sways across the aperture as it wriggles through — two sine
         * beats decaying to nothing, on the along-face axis. */
        int f = ((t - c_end) << 8) / (total - c_end);
        pitch_smooth_y = PU_FLOOR_PITCH - ((PU_FLOOR_PITCH * f) >> 8);
        SHARED_UC->eye_h = (uint8_t)(PU_SET_EYE
                       - (((PU_SET_EYE - PU_END_EYE) * f) >> 8));
        player.x = mx + (fx_t)(((int64_t)(tx - mx) * f) >> 8);
        player.y = my + (fx_t)(((int64_t)(ty - my) * f) >> 8);
        fx_t sway = FX_MUL(PU_SHIMMY, SIN_FX((uint8_t)(f << 1)));
        sway = (fx_t)(((int64_t)sway * (256 - f)) >> 8);   /* decay to still */
        if (g_exit_hole_axis) player.x += sway; else player.y += sway;
        if (t == c_end + 1) SHARED_UC->slide_sfx = 2;      /* torso scrape, 30% drag */
    }
    if (t >= total) pitch_smooth_y = 0;
}

/* TRAVEL CAMERA: while the corridor overlay draws the duct, the
 * raycaster underneath renders the DESTINATION from a camera moving
 * toward its spawn -- starting up to three open cells behind it (probed;
 * fewer if the map is tight, zero collapses to a fixed viewpoint whose
 * window-crop growth still reads as approach), at standing-plus eye, the
 * height we crawled in at. The end window reveals this live render:
 * travelling TO the spawn point, arriving ON it. */
static fx_t g_trav_sx, g_trav_sy, g_trav_ex, g_trav_ey;
static uint8_t g_trav_ang;
#define TRAV_EYE 160              /* standing, raised a touch: duct height */
#define CORR_VEIL_T 2             /* mirrors CORR_VEIL: held frames, no motion */
void raycast_corridor_travel_init(fx_t sx, fx_t sy, uint8_t ang) {
    fx_t dirx = COS_FX(ang), diry = SIN_FX(ang);
    int n = 0;
    for (int i = 1; i <= 3; i++) {
        int cx = FX_INT(sx - dirx * i);
        int cy = FX_INT(sy - diry * i);
        if ((unsigned)cx >= MAP_W || (unsigned)cy >= MAP_H) break;
        if (world_map[cy][cx] != 0) break;
        n = i;
    }
    g_trav_ex = sx;               g_trav_ey = sy;
    g_trav_sx = sx - dirx * n;    g_trav_sy = sy - diry * n;
    g_trav_ang = ang;
    player.x = g_trav_sx; player.y = g_trav_sy; player.angle = ang;
    SHARED_UC->eye_h = TRAV_EYE;
    SHARED_UC->pitch_y = 0;
}
void raycast_corridor_travel(int t, int total) {
    if (total < 1) total = 1;
    int p = 0;
    if (t > CORR_VEIL_T) p = ((t - CORR_VEIL_T) << 8) / (total - CORR_VEIL_T);
    if (p > 256) p = 256;
    int p2 = (p * p) >> 9;
    p2 += p >> 1;
    if (p2 > 256) p2 = 256;
    player.x = g_trav_sx
             + (fx_t)(((int64_t)(g_trav_ex - g_trav_sx) * p2) >> 8);
    player.y = g_trav_sy
             + (fx_t)(((int64_t)(g_trav_ey - g_trav_sy) * p2) >> 8);
    player.angle = g_trav_ang;
    SHARED_UC->eye_h = TRAV_EYE;
    is_walking = 0; is_running = 0;
}

/* CORRIDOR CRAWL: the interior of the wall, as a set piece. Replaces the
 * old outside-the-plane camera slide: after the pullup, the view cuts to a
 * scripted one-point-perspective duct THREE CELLS deep, crawled end to end.
 * Drawn OVER the normal frame (raycast_render still runs underneath, which
 * keeps both CPUs' pipeline and the audio pump alive) on the primary only.
 *
 * Everything is axis-aligned by construction -- a straight crawl down a
 * square duct projects every surface to bands between concentric
 * rectangles -- so the whole thing is rectangle fills: cheap enough that
 * this is the smoothest-moving camera in the game. Depth reads from the
 * ring shading (LIGHT LIVES AT THE EXIT: rings brighten toward the far
 * end, per the enclosure rule) and from darker RIB rings at each cell
 * seam, which is what makes it three cells of something rather than a
 * tube. The destination peek fills the end plane, growing until it IS the
 * frame -- the camera-push into the spawn point, made literal. */
/* The duct's axial spans and per-segment base shades, shared by the
 * polygon corridor, the through-panel preview, and anything else that
 * must agree with them: cell, rib collar, cell, rib collar, cell. */
static const fx_t corr_seg_z[6] = { 0, FX(0.94), FX(1.06),
                                    FX(1.94), FX(2.06), FX(3.0) };
/* Overhead-light story (Mike): the room's ceiling light falls PAST the
 * hole, not into it — the whole duct sits in shade, entrance included,
 * and the old mid-tunnel plunge (9 against entrance 2) read as a black
 * BAND rather than depth. Darker ends, gentler middle: one continuous
 * shaded material, exit segment left a shade lighter where the
 * destination's light reaches in. */
static const int8_t corr_seg_base[5] = { 4, 6, 7, 6, 4 };
/* Facet shade in 8.8, resolved through the hole's own 2x2 -- every duct
 * surface is a live 50/50 weave of adjacent ramp entries (the half-step),
 * the same dithered material the raycaster's interior and the panel
 * preview wear. Flat single-index fills read as a different substance at
 * the swap (Mike: keep the panel dithering through the tunnel). */
static inline int corr_facet8(int sh) {
    if (sh < 0) sh = 0; else if (sh > 14) sh = 14;
    return (sh << 8) + 128;
}
/* Capture which duct wall is lit from the hole's orientation at the
 * threshold (before the flush wipes it): the raycaster lights the side
 * whose rays drift with rda < 0, so the corridor keeps the same wall lit
 * across the swap and the asymmetry carries through. */
void raycast_corridor_orient(void) {
    if (g_exit_hole_cx < 0) { g_corr_lit_left = 1; return; }
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX = COS_FX(angle), dirY = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66)), planeY = FX_MUL(dirX, FX(0.66));
    fx_t rdx = dirX - planeX, rdy = dirY - planeY;   /* screen-left ray */
    fx_t rda = g_exit_hole_axis ? rdx : rdy;
    g_corr_lit_left = rda < 0;
}
/* Flat-fill a convex quad (screen coords, any winding) with scanline
 * edge-walking. Coordinates may lie far outside the frame (near-clipped
 * geometry projects huge); spans clamp per row. The box-renderer fill
 * idea at its smallest. */
static void corr_fill_quad(uint8_t *fb, const int *qx, const int *qy,
                           int s8) {
    int ymin = qy[0], ymax = qy[0];
    for (int i = 1; i < 4; i++) {
        if (qy[i] < ymin) ymin = qy[i];
        if (qy[i] > ymax) ymax = qy[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > SCREEN_H - 1) ymax = SCREEN_H - 1;
    if (ymin > ymax) return;
    int16_t xl[SCREEN_H], xr[SCREEN_H];
    for (int y = ymin; y <= ymax; y++) { xl[y] = SCREEN_W; xr[y] = -1; }
    for (int i = 0; i < 4; i++) {
        int x0 = qx[i], y0 = qy[i], x1 = qx[(i + 1) & 3], y1 = qy[(i + 1) & 3];
        if (y0 == y1) {
            if (y0 < ymin || y0 > ymax) continue;
            int a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0;
            if (a < xl[y0]) xl[y0] = (int16_t)(a < 0 ? 0 : a);
            if (b > xr[y0]) xr[y0] = (int16_t)(b > SCREEN_W ? SCREEN_W : b);
            continue;
        }
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        int dxdy = ((x1 - x0) << 12) / (y1 - y0);
        int ys = y0 < ymin ? ymin : y0;
        int ye = y1 > ymax ? ymax : y1;
        int acc = (x0 << 12) + dxdy * (ys - y0);
        for (int y = ys; y <= ye; y++, acc += dxdy) {
            int x = acc >> 12;
            int xc = x < 0 ? 0 : x > SCREEN_W ? SCREEN_W : x;
            if (xc < xl[y]) xl[y] = (int16_t)xc;
            if (xc > xr[y]) xr[y] = (int16_t)xc;
        }
    }
    for (int y = ymin; y <= ymax; y++) {
        uint8_t *row = fb + y * SCREEN_W;
        uint8_t c0 = hole_shade(s8, hole_bayer[((y & 1) << 1) | 0]);
        uint8_t c1 = hole_shade(s8, hole_bayer[((y & 1) << 1) | 1]);
        for (int x = xl[y]; x < xr[y]; x++) row[x] = (x & 1) ? c1 : c0;
    }
}

#define CORR_LEN   FX(3.0)     /* three cells of duct */
#define CORR_STOP  FX(0.10)    /* camera all but touches the end plane: the
                                * destination fills the frame before the fall */
#define CORR_START (-FX(0.12)) /* camera BEGINS where the raycaster left it:
                                * 0.12 outside the mouth, so the swap frame
                                * is a geometric match (the box-intro rule) */
#define CORR_VEIL  2           /* held frames at the swap (the panel already
                                * opened during the pullup's dissolve) */
#define CORR_HW    FX(0.30)    /* duct half-width  (the hole's aperture)   */
#define CORR_HH    FX(0.22)    /* duct half-height (sill..head, halved)    */
void raycast_crawl_corridor(int t, int total) {
    if (total < 1) total = 1;
    uint8_t *fb = fb_pixels();
    /* THE BOX-INTRO GRAMMAR: swap on a matching still, THEN move. Frames
     * 0..CORR_VEIL hold the camera at the raycaster's exact position while
     * the back panel -- drawn intact over the duct -- dissolves open.
     * Motion begins only once the panel has given way. */
    int p = 0;
    if (t > CORR_VEIL)
        p = ((t - CORR_VEIL) << 8) / (total - CORR_VEIL);
    if (p > 256) p = 256;
    /* Ease-in: the first push off the sill is slow, then the crawl finds
     * its rhythm. p2 = p eased toward constant speed. */
    int p2 = (p * p) >> 9;              /* 0..128: slow half */
    p2 += p >> 1;                       /* + linear: net accelerating 0..256 */
    if (p2 > 256) p2 = 256;
    fx_t cz = CORR_START
            + (fx_t)(((int64_t)(CORR_LEN - CORR_STOP - CORR_START) * p2) >> 8);
    /* Elbows-and-knees: two bob cycles over the length. The bob is a TRUE
     * camera translation (applied to the quads below), so near geometry
     * sways more than far -- real parallax, not a frame shift. */
    uint8_t ph = (uint8_t)(p2 << 1);
    int cxs = SCREEN_W >> 1;
    int cys = SCREEN_H >> 1;
    if (t == 1 || t == total / 2) SHARED_UC->slide_sfx = 2;   /* the scrapes, 30% drag */

    /* POLYGONS, the box's own language: the duct is camera-space QUADS --
     * four walls per axial segment, thin darker rib collars at the cell
     * seams, the destination on the end quad -- near-clipped, projected,
     * painter-ordered far-to-near, and FLAT-FILLED per facet. No
     * gradients, no per-pixel tricks: each panel is one clean polygon of
     * one shade, and the facet edges between segments are what read as
     * built geometry, exactly like the title box's panels. The bob is a
     * true camera translation, so every quad parallaxes properly. */
    fx_t camx = (fx_t)(((int64_t)SIN_FX((uint8_t)(ph + 64)) * 4) >> 8);
    fx_t camy = (fx_t)(((int64_t)SIN_FX(ph) * 6) >> 8);
    const fx_t NEARZ = FX(0.05);
    /* THE WINDOW IS LIVE. Nothing is painted inside the end rect: the
     * raycaster underneath is rendering the REAL destination from the
     * travel camera (see raycast_corridor_travel), so the opening shows
     * the actual place, full resolution, growing as both cameras close.
     * The duct's own quads occlude everything to the sides and behind. */
    /* Wall quads, far segment to near. Each side of each segment is one
     * flat facet; its shade is the bathtub at the segment's centre (the
     * room lights the entrance, the destination the exit, dark between)
     * with the hole's trim: head +1, sill -1, ribs +2. */
    /* Per-segment BASE shades, colour-matched to what the raycaster's
     * interior actually paints at the swap frame (its near shades are
     * almost pure surface offsets -- fog is ~0 that close): entrance base
     * 2, dark middle 11, exit lit by the destination, ribs proud of their
     * neighbours. Surface trims are the raycaster's own: head +3 (the
     * reveal), sill +0 (the lit ledge), lit wall +0, shadow wall +4. */
    for (int s = 4; s >= 0; s--) {
        fx_t z0 = corr_seg_z[s] - cz, z1 = corr_seg_z[s + 1] - cz;
        if (z1 <= NEARZ) continue;                 /* fully behind us */
        if (z0 < NEARZ) z0 = NEARZ;
        int sh = corr_seg_base[s];
        /* Projected corner columns/rows at both depths, camera-translated. */
        int lx0 = cxs + (int)(((int64_t)(-CORR_HW - camx) * 242) / z0);
        int rx0 = cxs + (int)(((int64_t)( CORR_HW - camx) * 242) / z0);
        int ty0 = cys - (int)(((int64_t)( CORR_HH - camy) * 224) / z0);
        int by0 = cys - (int)(((int64_t)(-CORR_HH - camy) * 224) / z0);
        int lx1 = cxs + (int)(((int64_t)(-CORR_HW - camx) * 242) / z1);
        int rx1 = cxs + (int)(((int64_t)( CORR_HW - camx) * 242) / z1);
        int ty1 = cys - (int)(((int64_t)( CORR_HH - camy) * 224) / z1);
        int by1 = cys - (int)(((int64_t)(-CORR_HH - camy) * 224) / z1);
        int cx4[4], cy4[4];
        int c;
        c = corr_facet8(sh + 1);                   /* head: one step of reveal */
        cx4[0] = lx0; cy4[0] = ty0; cx4[1] = rx0; cy4[1] = ty0;
        cx4[2] = rx1; cy4[2] = ty1; cx4[3] = lx1; cy4[3] = ty1;
        corr_fill_quad(fb, cx4, cy4, c);
        c = corr_facet8(sh);                       /* sill: the lit ledge */
        cx4[0] = lx1; cy4[0] = by1; cx4[1] = rx1; cy4[1] = by1;
        cx4[2] = rx0; cy4[2] = by0; cx4[3] = lx0; cy4[3] = by0;
        corr_fill_quad(fb, cx4, cy4, c);
        c = corr_facet8(sh + (g_corr_lit_left ? 0 : 4));   /* left wall */
        cx4[0] = lx0; cy4[0] = ty0; cx4[1] = lx1; cy4[1] = ty1;
        cx4[2] = lx1; cy4[2] = by1; cx4[3] = lx0; cy4[3] = by0;
        corr_fill_quad(fb, cx4, cy4, c);
        c = corr_facet8(sh + (g_corr_lit_left ? 4 : 0));   /* right wall */
        cx4[0] = rx1; cy4[0] = ty1; cx4[1] = rx0; cy4[1] = ty0;
        cx4[2] = rx0; cy4[2] = by0; cx4[3] = rx1; cy4[3] = by1;
        corr_fill_quad(fb, cx4, cy4, c);
    }
}

/* THROUGH-PANEL DUCT PREVIEW -- the "dithered extension", visible from the
 * MOMENT the climb is triggered. Renders the corridor's own facets (same
 * segments, same bases, same lit side) as seen through the panel window
 * into the peek bitmap; the back panel's existing 4-frame dissolve then
 * opens the panel onto it during the pullup, while the raycaster still
 * owns the frame. By the swap, the panel is already open onto the duct
 * the polygon corridor will show -- no veil needed, no pop anywhere. */
void raycast_duct_preview(void) {
    /* A MINIATURE OF THE CORRIDOR'S OWN FRAME 0 -- not the literal
     * through-window optics. The literal projection compresses the duct
     * into flat horizontal bands at one cell of distance (correct, and
     * wrong: it matches nothing the swap will show). What the panel should
     * hold is the composition the corridor opens on -- converging
     * trapezoids, visible side walls, rib collars -- so the dissolve, the
     * pullup, and the swap are all one continuous framing. Same
     * projection, same tables, scaled into peek space. */
    fx_t dzx[PEEK_W]; fx_t dzy[PEEK_H];
    for (int u = 0; u < PEEK_W; u++) {
        int a = u - PEEK_W / 2; if (a < 0) a = -a;
        dzx[u] = a < 1 ? FX(64)
               : (fx_t)(((int64_t)CORR_HW * 73) / a);   /* 242 * 96/320 */
    }
    for (int v = 0; v < PEEK_H; v++) {
        int a = v - PEEK_H / 2; if (a < 0) a = -a;
        dzy[v] = a < 1 ? FX(64)
               : (fx_t)(((int64_t)CORR_HH * 64) / a);   /* 224 * 64/224 */
    }
    /* Camera at the PANEL PLANE, not at the player: through a hole in a
     * wall you see the duct from cell 1 onward -- dark rings, rib collars,
     * the deep stretch (Mike's 45.png framing) -- never the near cell's
     * lit walls, which made the preview read as a lit ROOM (and so as a
     * spawn-point image) instead of a tunnel. */
    const fx_t cz = FX(1.0), dend = CORR_LEN - FX(1.0);
    for (int v = 0; v < PEEK_H; v++) {
        uint8_t *dst = peek_buf + v * PEEK_W;
        for (int u = 0; u < PEEK_W; u++) {
            fx_t dx = dzx[u], dy = dzy[v];
            if (dx > dend && dy > dend) { dst[u] = HOLE_DEEP_BASE + 2; continue; }
            fx_t z = cz + (dx < dy ? dx : dy);
            int s = 4;
            while (s > 0 && z < corr_seg_z[s]) s--;
            int sh = corr_seg_base[s];
            if (dx < dy) sh += ((u < PEEK_W / 2) == (g_corr_lit_left != 0)) ? 0 : 4;
            else         sh += (v < PEEK_H / 2) ? 1 : 0;
            dst[u] = hole_shade(corr_facet8(sh),
                                hole_bayer[((v & 1) << 1) | (u & 1)]);
        }
    }
    purge_cache_range(peek_buf, PEEK_BYTES);
    g_hole_peek_on = (int)SHARED_UC->frame_count + 1;   /* dissolve from NOW */
}

/* Eye height (8.8 fraction of room height), eased toward STAND/CROUCH as
 * the player holds X. STAND_EYE/CROUCH_EYE are #defined up by raycast_init. */
static int     eye_smooth = STAND_EYE;
/* Transient look-down "head dip" while standing up out of a crouch — glances
 * at the floor as the eye climbs through the lower half of the rise, then
 * eases back to level by mid-stand. Added as extra pitch in raycast_render. */
static int     standup_dip = 0;
#define STANDUP_DIP 40         /* peak look-down pixels while rising; 0 disables */

/* ARRIVAL DROP: the far side of the climb. Cutting to a lit room said "level
 * loaded"; falling out of an opening that ISN'T THERE when you turn around
 * says something much worse, and costs one animation. Called per fade-in frame
 * by the portal so the picture comes up DURING the fall.
 *
 * The landing hands off rather than scripting the stand: it leaves eye_smooth
 * compressed and lets player_update's existing crouch-release ease the player
 * upright, which already carries the standup_dip floor-glance. So the last
 * beat of the arrival is the same motion as getting up anywhere else. */
#define AD_START_EYE   232     /* out of an opening high in the wall */
#define AD_IMPACT_EYE   56     /* the compression you land in */
#define AD_FALL_PITCH   26     /* the floor coming up to meet you */
void raycast_arrival_drop(int t, int total) {
    int fall = total * 2 / 3; if (fall < 1) fall = 1;
    pitch_smooth_y = AD_FALL_PITCH;
    if (t <= fall) {
        int f = (t << 8) / fall;
        int f2 = (f * f) >> 8;              /* gravity: the fall accelerates */
        eye_smooth = AD_START_EYE
                   - (((AD_START_EYE - AD_IMPACT_EYE) * f2) >> 8);
    } else {
        eye_smooth = AD_IMPACT_EYE;
        if (t == fall + 1) SHARED_UC->slide_sfx = 1;   /* the landing scuff, native rate */
    }
    SHARED_UC->eye_h = (uint8_t)eye_smooth;
    is_walking = 0; is_running = 0;
}

/* Settle the eye to standing NOW. The arrival drop deliberately leaves
 * eye_smooth compressed so gameplay's crouch-release plays the stand -- but
 * the INTRO scripts its own stand-up, and without this the leftover
 * compression replayed the bounce on the first frame of whatever level the
 * start list launched. */
void raycast_eye_settle(void) {
    eye_smooth = STAND_EYE;
    SHARED_UC->eye_h = STAND_EYE;
}

/* Read controller, advance player by one frame. Axis-separated collision
 * gives natural sliding along walls. */
/* pad is read ONCE per frame by the caller (the loop already reads it for
 * menu/metrics) and passed in — the old self-read here was a second 68K
 * HwMdReadPad round-trip per frame, pure overhead. */
void player_update(uint16_t pad) {
    /* Hold C → look mode. D-pad UP/DOWN drive pitch in two phases:
     *   Phase 1 — ease toward the comfortable angle ±40 at 25%/frame
     *             (~11 frames / 183 ms to settle). Same exponential
     *             ramp shape as the LEFT/RIGHT pivot at walk speed
     *             (4 angle units/frame) so the gaze and pivot feel
     *             paced the same.
     *   Phase 2 — once you've reached ±40 and KEEP holding, linear
     *             ramp at 1 px/frame from ±40 out to ±80. ~40 more
     *             frames / 670 ms to fully extend. This is the
     *             "extra" the player discovers if they really lean.
     * Release direction (or C) → spring back to 0 at 25%/frame
     * (symmetric to phase 1). Forward/back walking is suspended during
     * the C hold since UP/DOWN are repurposed for pitch. Y-shear caps
     * the convincing tilt at about ±80; past that walls visibly slide
     * rather than tilt. */
    int look_mode = (pad & SEGA_CTRL_C) != 0;
    int up_held   = look_mode && (pad & SEGA_CTRL_UP)   != 0;
    int down_held = look_mode && (pad & SEGA_CTRL_DOWN) != 0;
    if (up_held && !down_held) {
        if (pitch_smooth_y > -40) {
            /* Phase 1: ease toward -40 at 25%/frame. */
            pitch_smooth_y += (-40 - pitch_smooth_y) >> 2;
        } else {
            /* Phase 2: slow linear extension past -40. */
            pitch_smooth_y -= 1;
            if (pitch_smooth_y < -80) pitch_smooth_y = -80;
        }
    } else if (down_held && !up_held) {
        if (pitch_smooth_y < 40) {
            pitch_smooth_y += (40 - pitch_smooth_y) >> 2;
        } else {
            pitch_smooth_y += 1;
            if (pitch_smooth_y > 80) pitch_smooth_y = 80;
        }
    } else {
        /* No direction (or C released): ease toward 0 at 25%/frame. */
        pitch_smooth_y += (0 - pitch_smooth_y) >> 2;
    }

    /* Hold X to crawl — ease the eye down toward the floor (the wall draw
     * reads SHARED_UC->eye_h). Variable eye height now; partial-height
     * crawl-under walls come later. */
    /* Crouch = A+B held together. We deliberately do NOT use the 6-button X
     * button: emulators routinely bind X to the same key as D-pad Left (so
     * X-crouch fires on Left), and they report a 6-button type, so we can't
     * distinguish them from a real MiSTer 6-button pad to gate it. A+B is
     * collision-free on every pad. Trade-off: holding sprint+strafe now
     * crouch-strafes instead of sprint-strafing. */
    int crouching = (pad & SEGA_CTRL_A) && (pad & SEGA_CTRL_B);
    /* Forced crouch inside a low-ceiling crawlspace: while the player's body is
     * within the zone the slab is overhead, so they stay stuck crouching until
     * they crawl all the way out — releasing A+B mid-tunnel does NOT stand them
     * up (they'd clip through the ceiling). Standing only returns once the body
     * clears the zone. */
    /* ...but a BULKHEAD is a doorway header, not a duct: it only asks you to
     * stoop. Inside one, or refused entry to one (duck_bump), the eye dips to
     * DUCK_EYE automatically and you keep walking at normal speed — no crouch
     * button, no crawl pace. Ducts still latch the full crouch above. */
    int auto_duck = 0;
    if (g_lowceil_active) {
        int cx = FX_INT(player.x), cy = FX_INT(player.y);
        if ((unsigned)cx < MAP_W && (unsigned)cy < MAP_H) {
            uint8_t ch = CEIL_H(cy, cx);
            if (ch != CEIL_H_FULL) {
                /* Only a full-height header ducks. Anything lower — ducts and
                 * any authored height between — keeps the crawl it has always
                 * had, so community maps can't be surprised by this. */
                if (ch >= BULKHEAD_CEIL_H) auto_duck = 1;   /* header: dip */
                else                       crouching = 1;   /* duct: stay down */
            }
        }
    }
    if (duck_bump) { auto_duck = 1; duck_bump = 0; }
    {
        int target = crouching ? CROUCH_EYE
                   : (auto_duck ? DUCK_EYE : STAND_EYE);
        int d = target - eye_smooth;
        /* Drop into crouch 2× faster than we rise: d<0 (eye falling toward the
         * floor) eases at 50%/frame, d>0 (standing back up) stays at 25%. */
        if (d > -4 && d < 4) eye_smooth = target;
        else eye_smooth += (d < 0) ? (d >> 1) : (d >> 2);
        SHARED_UC->eye_h = (uint8_t)eye_smooth;

        /* Stand-up floor-glance: while rising (not crouching) through the
         * lower 3/4 of the climb, aim the dip toward the floor; once we pass
         * 75% of the rise the target drops to 0 so the view eases back to
         * level — mimicking how you glance at the ground as you get up. */
        int dip_target = (!crouching && eye_smooth > CROUCH_EYE + 8
                          && eye_smooth < (STAND_EYE * 3 + CROUCH_EYE) / 4)
                         ? STANDUP_DIP : 0;
        /* Dip toward the floor fast (>>1), but ease the gaze back to level at
         * HALF that speed (>>2) so it reads as the player taking a moment to
         * collect before settling into the standing perspective. */
        int dd = dip_target - standup_dip;
        standup_dip += (dd > 0) ? (dd >> 1) : (dd >> 2);
    }

    /* Hold A to run — bumps walk speed and turn rate together so you can
     * quickly reorient while sprinting. No running while crouched; crawling
     * is slow. */
    int sprinting = !crouching && !auto_duck && (pad & SEGA_CTRL_A) != 0;
    fx_t walk = crouching ? FX(0.064) : (sprinting ? FX(0.15) : FX(0.08));
    /* crawl was FX(0.04); +60% -- ducts and headers are set dressing to move
     * through, not a molasses toll. Still slower than the 0.08 walk. */
    uint8_t turn = sprinting ? 8 : 4;

    /* B toggles left/right between turning and strafing. */
    int strafing = (pad & SEGA_CTRL_B) != 0;

    if (!strafing) {
        if (pad & SEGA_CTRL_LEFT)  player.angle -= turn;
        if (pad & SEGA_CTRL_RIGHT) player.angle += turn;
    }

    fx_t dirX = COS_FX(player.angle);
    fx_t dirY = SIN_FX(player.angle);
    /* Player's RIGHT is dir rotated +90° (with +y down). */
    fx_t rightX = -dirY;
    fx_t rightY =  dirX;

    fx_t dx = 0, dy = 0;
    /* Suspend walking forward/back while C is held — UP/DOWN are
     * borrowed for look-up/look-down in look_mode (above). */
    if (!look_mode) {
    if (pad & SEGA_CTRL_UP)   { dx += FX_MUL(dirX, walk); dy += FX_MUL(dirY, walk); }
    if (pad & SEGA_CTRL_DOWN) { dx -= FX_MUL(dirX, walk); dy -= FX_MUL(dirY, walk); }
    if (strafing) {
        if (pad & SEGA_CTRL_RIGHT) { dx += FX_MUL(rightX, walk); dy += FX_MUL(rightY, walk); }
        if (pad & SEGA_CTRL_LEFT)  { dx -= FX_MUL(rightX, walk); dy -= FX_MUL(rightY, walk); }
    }
    }   /* end if (!look_mode) — UP/DOWN handled as look pitch above. */

    /* Axis-separated collision: try X first, then Y. Remember which cutout (if
     * any) refused the move — that's what a second push will tip over. */
    int shove = -1;
    fx_t newX = player.x + dx;
    if (position_clear(newX, player.y)) player.x = newX;
    else { int b = standup_blocker(newX, player.y); if (b >= 0) shove = b; }

    fx_t newY = player.y + dy;
    if (position_clear(player.x, newY)) player.y = newY;
    else { int b = standup_blocker(player.x, newY); if (b >= 0) shove = b; }

    /* CARDBOARD, NOT CONCRETE. Walk into a cutout and it stops you dead — the
     * uncanny beat. Push again, deliberately, in the same direction and it
     * tips over and you squeeze past. Gated on a fresh press of the movement
     * input rather than a hold timer, so it's an act of intent and not a
     * function of how long you leaned (and it can't depend on frame rate). */
    {
        static uint16_t prev_move = 0, prev_a = 0;
        static int a_latch = 0;
        const uint16_t MOVE_MASK = SEGA_CTRL_UP | SEGA_CTRL_DOWN
                                 | SEGA_CTRL_LEFT | SEGA_CTRL_RIGHT;
        uint16_t move = pad & MOVE_MASK;
        int fresh_push = (move & ~prev_move) != 0;   /* a direction newly pressed */
        int a_fresh = (pad & SEGA_CTRL_A) && !(prev_a & SEGA_CTRL_A);
        /* Press-latch: the pad is sampled once per rendered frame, so at low fps
         * a quick A tap can land a hair before you're in range and be gone by the
         * next sample. Hold the press actionable for ~3 frames; consumed the
         * instant it topples something, so one tap still fells exactly one. */
        if (a_fresh) a_latch = 3;
        int a_trigger = a_latch > 0;
        /* A also pushes a cutout over: a deliberate press, so it works by
         * proximity (face him and tap A) as well as while shoving into him —
         * no "arm on first contact" step the accidental-lean guard needs. If
         * A is latched with nothing being shoved, grab the nearest topple-able
         * cutout within reach and treat it as the shove target below. */
        if (a_trigger && shove < 0) {
            int64_t bestd2 = (int64_t)FX(1.3) * FX(1.3);
            int pfxc = FX_INT(player.x), pfyc = FX_INT(player.y);
            for (int si = 0; si < NUM_STANDUPS; si++) {
                /* BOX MODELS are solid furniture: they block and never topple.
                 * Gating on the chair alone let the desk be shoved over, and a
                 * toppled object is deliberately walk-through -- so the desk lost
                 * its collision the moment you pushed into it. The topple pipeline
                 * is billboard/flat-bitmap only anyway, so a fallen desk could not
                 * render either. */
                if (standups[si].kind != NEANDER_ASSET_KIND || standup_down[si]) continue;
                /* Don't topple THROUGH a partition: if a flagged slab edge sits
                 * between the player's cell and the cutout's, you can't actually
                 * reach it (the same pedge_w/pedge_n test the movement collision
                 * uses at ~1974). Without this, a cutout parked behind a counter
                 * tips over when you press A on the far side. */
                if (g_pedge_any) {
                    int sxc = FX_INT(standups[si].x), syc = FX_INT(standups[si].y);
                    if (sxc != pfxc) { int line = sxc > pfxc ? sxc : pfxc;
                        if ((unsigned)pfyc < (unsigned)MAP_H && line >= 0 && line <= MAP_W &&
                            (pedge_w[pfyc][line] & CM_PEDGE_PRESENT)) continue; }
                    if (syc != pfyc) { int line = syc > pfyc ? syc : pfyc;
                        if ((unsigned)pfxc < (unsigned)MAP_W && line >= 0 && line <= MAP_H &&
                            (pedge_n[line][pfxc] & CM_PEDGE_PRESENT)) continue; }
                }
                fx_t ddx = standups[si].x - player.x, ddy = standups[si].y - player.y;
                int64_t d2 = (int64_t)ddx * ddx + (int64_t)ddy * ddy;
                if (d2 < bestd2) { bestd2 = d2; shove = si; }
            }
        }
        if (shove >= 0 && standups[shove].kind != NEANDER_ASSET_KIND) {
            /* Only the neanderthal topples. Everything else blocks and stays
             * put. This gate was chair-only, so the desk could be shoved over —
             * and a toppled object is walk-through, which is exactly how the
             * desk lost its collision in procgen. */
        } else if (shove >= 0) {
            if (a_trigger || (standup_armed[shove] && fresh_push)) {
                a_latch = 0;                                   /* consume: one tap, one topple */
                standup_down[shove] = 1;                       /* timber */
                standup_fall_prog[shove] = 1;                  /* start the tip-over */
                uint8_t facing = standups[shove].facing_angle;
                {   /* pushed from the front -> lands face up; from behind ->
                     * face down (cardboard back). Same dot as the billboard's
                     * front/back test: (standup-player). forward < 0 = front. */
                    fx_t fwx = COS_FX(facing), fwy = SIN_FX(facing);
                    fx_t dxs = standups[shove].x - player.x;
                    fx_t dys = standups[shove].y - player.y;
                    standup_fall_face[shove] =
                        (FX_MUL(dxs, fwx) + FX_MUL(dys, fwy)) < 0;
                }
                /* Fall along the facing axis so the tip is rigid about the width
                 * axis (no texture twist): backward (facing+128) when pushed from
                 * the front, forward otherwise — both "away from the push". */
                uint8_t fdir = standup_fall_face[shove]
                             ? (uint8_t)(facing + 128) : facing;
                standup_fall_dir[shove] = fdir;
                /* Always land FLAT on the floor (fall_angle 64) — a permanent
                 * mid-air lean reads as "frozen." But the flat body ignores
                 * partitions in the z-buffer (they're a free-standing overlay,
                 * not in WALL_DIST), so cap its floor length to whatever's ahead
                 * so he stops AT the slab instead of clipping through it. */
                fx_t reach = standup_wall_reach(standups[shove].x, standups[shove].y,
                                                fdir, STANDUP_FALL_LEN);
                if (reach > STANDUP_FALL_LEN) reach = STANDUP_FALL_LEN;
                if (reach < FX(0.4))          reach = FX(0.4);  /* keep a visible body */
                standup_fall_len_q[shove] = (uint8_t)(reach >> (FX_SHIFT - 6));
            } else standup_armed[shove] = 1;                   /* first contact */
        }
        /* Advance every toppling cutout's fall one step per frame. */
        for (int si = 0; si < NUM_STANDUPS; si++)
            if (standup_down[si] && standup_fall_prog[si] < STANDUP_FALL_MAX)
                standup_fall_prog[si]++;
        /* Broken-tape death of the Voyager hello: ramp hero_dying while a felled
         * neanderthal (any non-chair standup that's down) exists, reset to alive
         * otherwise. ~2.5s to fully dead; the mixer reads it to warp the hello. */
        int neander_down = 0;
        for (int si = 0; si < NUM_STANDUPS; si++)
            if (standups[si].kind != CHAIR_SPRITE_KIND && standup_down[si]) { neander_down = 1; break; }
        if (neander_down) {
            int hd = (int)SHARED_UC->hero_dying + HERO_DYING_RATE;
            SHARED_UC->hero_dying = (uint8_t)(hd > 255 ? 255 : hd);
        } else {
            SHARED_UC->hero_dying = 0;
        }
        if (a_latch > 0) a_latch--;
        prev_move = move;
        prev_a = (uint16_t)(pad & SEGA_CTRL_A);
    }

    /* Track walking state and advance bob phase. WALKING = the position
     * actually changed after collision, not "movement input held":
     * pressed face-first into a wall you go nowhere, so the carpet
     * footsteps / crawl slide stay silent instead of marching in place.
     * Wall SLIDING still counts — the clipped axis zeroes but the other
     * carries, the position changes, the steps play. (Also stills the
     * head-bob against walls, which matches what your eyes see.) */
    {
        static fx_t prev_px_w, prev_py_w;
        is_walking = (player.x != prev_px_w || player.y != prev_py_w);
        prev_px_w = player.x;
        prev_py_w = player.y;
    }
    /* Turning-in-place counts as motion for the res/LOD gates (a spin redraws
     * every column); detected by angle delta so it covers every turn path. */
    {
        static uint8_t prev_ang_t;
        SHARED_UC->is_turning = (player.angle != prev_ang_t);
        prev_ang_t = (uint8_t)player.angle;
    }
    /* Running = actually moving while sprinting (A held). The pump reads this
     * to play the carpet footsteps at a faster cadence to match the stride. */
    is_running = (is_walking && sprinting) ? 1 : 0;
    /* Bob cadence matches the footstep audio: 1.5x while sprinting, so
     * eyes and ears agree on stride rate. Amplitude deliberately
     * unchanged (deeper bob at these framerates reads as judder). */
    if (is_walking) bob_phase += is_running ? 30 : 20;   /* ~4.7 / ~7 Hz */

    /* INTERACT: the run button (A) doubles as "use" when you're within reach of
     * the door — a rising-edge press toggles it open/closed. Holding A still
     * just runs (the edge fires once). */
    static uint16_t prev_pad_pu = 0xFFFF;
    if ((pad & SEGA_CTRL_A) && !(prev_pad_pu & SEGA_CTRL_A)) {
        for (int d = 0; d < num_decals; d++) {
            if (decals[d].kind != 1) continue;
            /* Arm's reach, orientation-agnostic: you must be right at the door. */
            if (FX_ABS(decals[d].x - player.x) < FX(1.0) &&
                FX_ABS(decals[d].y - player.y) < FX(1.0)) {
                g_door_target = g_door_target ? 0 : DOOR_OPEN_MAX;
                break;
            }
        }
    }
    prev_pad_pu = pad;
    /* Ease the swing toward the target at a fixed velocity (uniform motion),
     * clamping so we land exactly on the target. Publish for both CPUs. */
    if (g_door_open < g_door_target) {
        g_door_open += DOOR_OPEN_STEP;
        if (g_door_open > g_door_target) g_door_open = g_door_target;
    } else if (g_door_open > g_door_target) {
        g_door_open -= DOOR_OPEN_STEP;
        if (g_door_open < g_door_target) g_door_open = g_door_target;
    }
    SHARED_UC->door_open = (uint8_t)g_door_open;
}

/* Render each cardboard standup as a textured Wolf3D-style billboard.
 * Same camera-space transform as draw_lights. Vertical centering on the
 * horizon places the figure's feet on the floor and head 1 world unit up.
 * Front/back is dot(player - standup, standup_forward) — positive = front
 * (sample texture), negative = back (cardboard fill). */
/* The outlet is no longer a separate object — it is painted INTO the wall
 * column during the wall raster (see the wall-embedded outlet pass at the end
 * of raycast_draw_walls). decals[] just carries where/how big it is; there is
 * no billboard pass any more. */

/* A toppled cutout, lying flat on the floor as a perspective-scaled bitmap.
 * The cutout is a world-space rectangle on the floor plane: base at its old
 * feet, extending `L` along its fall direction, `Wd` wide. We reuse the
 * carpet's inverse floor-projection — for each screen floor pixel we already
 * know its world (X,Y), so we just test rectangle membership and sample the
 * neanderthal texture. Wall occlusion is the same per-column z-test the
 * billboard uses (rowDist vs WALL_DIST). Bounded to the rect's screen bbox so
 * it costs a few thousand pixels, not a full floor pass. */
/* Intersect [*xa,*xb] with the screen-x range where v0 + x*dv stays in [lo,hi].
 * Returns 0 (skip the row) if the intersection is empty. */
static inline int fallen_clip_x(int *xa, int *xb, fx_t v0, fx_t dv, fx_t lo, fx_t hi) {
    if (dv > -64 && dv < 64)                       /* ~parallel: constant across row */
        return (v0 >= lo && v0 <= hi);
    int64_t a = (((int64_t)(lo - v0)) << FX_SHIFT) / dv;   /* x (16.16) where v==lo */
    int64_t b = (((int64_t)(hi - v0)) << FX_SHIFT) / dv;   /* x (16.16) where v==hi */
    if (a > b) { int64_t t = a; a = b; b = t; }
    int lo_x = (int)((a + 0xFFFF) >> FX_SHIFT);    /* ceil  */
    int hi_x = (int) (b            >> FX_SHIFT);    /* floor */
    if (lo_x > *xa) *xa = lo_x;
    if (hi_x < *xb) *xb = hi_x;
    return (*xa <= *xb);
}

static void draw_fallen_standup(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb) {
    const sprite_def_t *sd = &sprite_defs[standups[i].kind];
    /* Prefer the hi-res texture (4x detail): laid flat across the floor the
     * lo-res 32x64 reads blocky. hi-res is column-major (column texX starts at
     * texX*tex_h); lo-res is row-major. */
    const uint8_t *tex; int tex_w, tex_h, col_major;
    if (sd->tex_hi) { tex = sd->tex_hi; tex_w = sd->w_hi; tex_h = sd->h_hi; col_major = 1; }
    else            { tex = sd->tex;    tex_w = sd->w;    tex_h = sd->h;    col_major = 0; }
    uint8_t front_base = sd->base;
    int face_up = standup_fall_face[i];
    uint8_t back_c = (uint8_t)(sd->base + 0);   /* cardboard back fill */
    int focal_const = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    if (focal_const < 1) focal_const = 1;

    uint8_t fa = standup_fall_dir[i];
    fx_t fdx = COS_FX(fa), fdy = SIN_FX(fa);
    fx_t ppx = -fdy, ppy = fdx;             /* unit perpendicular (width axis) */
    fx_t Bx = standups[i].x, By = standups[i].y;
    const fx_t L    = standup_body_len(i); /* length along the floor (capped at slabs) */
    const fx_t HALFW = FX(0.225);           /* half width */
    fx_t k_t = FX_DIV(FX(tex_h), L);        /* texY units per world-unit along */
    fx_t k_s = FX_DIV(FX(tex_w), HALFW * 2);/* texX units per world-unit across */

    /* Vertical screen extent from the 4 floor corners. */
    int miny = SCREEN_H, maxy = -1;
    for (int c = 0; c < 4; c++) {
        fx_t t = (c & 2) ? L : 0;
        fx_t s = (c & 1) ? HALFW : -HALFW;
        fx_t wx = Bx + FX_MUL(fdx, t) + FX_MUL(ppx, s);
        fx_t wy = By + FX_MUL(fdy, t) + FX_MUL(ppy, s);
        fx_t rx = wx - px, ry = wy - py;
        fx_t tY = FX_MUL(inv_det, FX_MUL(-planeY, rx) + FX_MUL(planeX, ry));
        if (tY < FX(0.06)) tY = FX(0.06);
        int scrY = horizon_y + (int)(((int32_t)focal_const << FX_SHIFT) / tY);
        if (scrY < miny) miny = scrY;
        if (scrY > maxy) maxy = scrY;
    }
    if (miny < horizon_y + 1) miny = horizon_y + 1;
    if (maxy > SCREEN_H - 1)  maxy = SCREEN_H - 1;
    if (miny > maxy) return;

    fx_t leftDirX = dirX - planeX, leftDirY = dirY - planeY;
    fx_t dPlaneX = planeX * 2, dPlaneY = planeY * 2;   /* rightDir - leftDir */
    for (int y = miny; y <= maxy; y++) {
        int p = y - horizon_y;
        if (p <= 0) continue;
        fx_t rowDist = (fx_t)(((int64_t)focal_const << FX_SHIFT) / p);
        fx_t stepX = FX_MUL(rowDist, dPlaneX) / SCREEN_W;
        fx_t stepY = FX_MUL(rowDist, dPlaneY) / SCREEN_W;
        fx_t rx0 = px + FX_MUL(rowDist, leftDirX) - Bx;   /* rel at screen x=0 */
        fx_t ry0 = py + FX_MUL(rowDist, leftDirY) - By;
        fx_t t0 = FX_MUL(rx0, fdx) + FX_MUL(ry0, fdy);    /* along-axis at x=0 */
        fx_t dt = FX_MUL(stepX, fdx) + FX_MUL(stepY, fdy);
        fx_t s0 = FX_MUL(rx0, ppx) + FX_MUL(ry0, ppy);    /* across-axis at x=0 */
        fx_t ds = FX_MUL(stepX, ppx) + FX_MUL(stepY, ppy);

        int xa = col_start, xb = col_end - 1;
        if (!fallen_clip_x(&xa, &xb, t0, dt, 0, L)) continue;
        if (!fallen_clip_x(&xa, &xb, s0, ds, -HALFW, HALFW)) continue;

        fx_t tnum = t0 + (fx_t)xa * dt;
        fx_t snum = s0 + (fx_t)xa * ds;
        fx_t texY_fx = FX_MUL(tnum, k_t);
        fx_t texX_fx = FX_MUL(snum + HALFW, k_s);
        fx_t dtexY = FX_MUL(dt, k_t);
        fx_t dtexX = FX_MUL(ds, k_s);
        uint8_t *prow = fb + (uintptr_t)y * SCREEN_W;
        for (int x = xa; x <= xb; x++) {
            if (rowDist < WALL_DIST(x)) {               /* not behind a wall */
                int texY = tex_h - 1 - (int)(texY_fx >> FX_SHIFT);
                /* Face-down shows the card's BACK: the silhouette seen
                 * THROUGH the panel, i.e. the MIRROR of the front mask.
                 * One fixed mapping had the cardboard-up outline reversed. */
                int texX = face_up ? tex_w - 1 - (int)(texX_fx >> FX_SHIFT)
                                   : (int)(texX_fx >> FX_SHIFT);
                if (texX < 0) texX = 0; else if (texX >= tex_w) texX = tex_w - 1;
                if (texY < 0) texY = 0; else if (texY >= tex_h) texY = tex_h - 1;
                /* texX flipped to un-mirror vs the upright quad (staff on the
                 * right). The FALLING renderer flips the same way, so the two
                 * stay consistent and there's no L/R jump at settle. */
                uint8_t v = col_major ? tex[texX * tex_h + texY]
                                      : tex[texY * tex_w + texX];
                if (v) prow[x] = face_up ? (uint8_t)(front_base + v) : back_c;
            }
            texY_fx += dtexY;
            texX_fx += dtexX;
        }
    }
}

/* Mid-topple: the cutout as a rigid panel hinged at its feet, rotating from
 * vertical (theta 0) toward flat (theta 90). Rendered as horizontal slices from
 * feet to head: each slice sits at world floor offset d = v*H*sin(theta) along
 * the fall direction, raised h = v*H*cos(theta) above the floor, projected with
 * the same focal length as the billboard. At MAX the caller hands off to the
 * flat floor bitmap, so this only runs for the few in-flight frames. */
static void draw_falling_standup(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb, uint8_t theta) {
    const sprite_def_t *sd = &sprite_defs[standups[i].kind];
    const uint8_t *tex; int tex_w, tex_h, col_major;
    if (sd->tex_hi) { tex = sd->tex_hi; tex_w = sd->w_hi; tex_h = sd->h_hi; col_major = 1; }
    else            { tex = sd->tex;    tex_w = sd->w;    tex_h = sd->h;    col_major = 0; }
    uint8_t front_base = sd->base;
    int face_up = standup_fall_face[i];
    uint8_t back_c = (uint8_t)(sd->base + 0);   /* cardboard back fill */
    int focal_const = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    if (focal_const < 1) focal_const = 1;

    uint8_t fa = standup_fall_dir[i];
    fx_t fdx = COS_FX(fa), fdy = SIN_FX(fa);
    fx_t Bx = standups[i].x, By = standups[i].y;
    /* Polarity must MATCH the standing billboard at the handoff frame. The
     * old fixed 'un-mirror' constant agreed with the upright view from the
     * FRONT side only — pushed from behind, the figure X-flipped mid-shove.
     * Same width-axis projection test as the standing path: reversed when
     * the axis points screen-right, forward when screen-left. */
    fx_t wpx = COS_FX((uint8_t)(standups[i].facing_angle + 64));
    fx_t wpy = SIN_FX((uint8_t)(standups[i].facing_angle + 64));
    int fall_rev = (FX_MUL(inv_det,
                       FX_MUL(dirY, wpx) - FX_MUL(dirX, wpy)) < 0);
    const fx_t H = standup_body_len(i);     /* capped at slabs so he stops, not clips */
    const fx_t HALFW = FX(0.225);
    fx_t cs = COS_FX(theta), sn = SIN_FX(theta);

    /* Project N+1 sample points feet->head, then rasterize every screen row of
     * each span BETWEEN adjacent samples, lerping position/width/texture row.
     * The first cut painted one screen row per slice — up close the panel is
     * taller (in rows) than there are slices, so unpainted rows striped through
     * as see-through "tearing". Span-filling covers every row exactly once. */
    const int N = 48;                       /* sample points feet->head */
    int  s_sy[N + 1], s_cx[N + 1], s_hw[N + 1], s_ty[N + 1];
    fx_t s_tY[N + 1];
    uint8_t s_ok[N + 1];
    for (int stp = 0; stp <= N; stp++) {
        s_ok[stp] = 0;
        fx_t vf = (fx_t)stp * (FX_ONE / N);     /* 0..1 along the panel */
        fx_t hlen = FX_MUL(vf, H);
        fx_t h = FX_MUL(hlen, cs);              /* height above floor */
        fx_t d = FX_MUL(hlen, sn);              /* along-floor offset */
        fx_t rx = Bx + FX_MUL(fdx, d) - px;
        fx_t ry = By + FX_MUL(fdy, d) - py;
        fx_t tY = FX_MUL(inv_det, FX_MUL(-planeY, rx) + FX_MUL(planeX, ry));
        if (tY < FX(0.15)) continue;
        fx_t tX = FX_MUL(inv_det, FX_MUL(dirY, rx) - FX_MUL(dirX, ry));
        int cx  = (SCREEN_W >> 1)
                + (int)(((int32_t)(SCREEN_W >> 1) * FX_DIV(tX, tY)) >> FX_SHIFT);
        int fyF = horizon_y + (int)(((int32_t)focal_const << FX_SHIFT) / tY);
        int sy  = fyF - (int)(((int64_t)h * SCREEN_H) / tY);
        int half_w = (int)(((int64_t)HALFW * SCREEN_H) / tY);
        if (half_w < 1) half_w = 1;
        int texY = tex_h - 1 - (int)(((int64_t)vf * tex_h) >> FX_SHIFT);
        if (texY < 0) texY = 0; else if (texY >= tex_h) texY = tex_h - 1;
        s_sy[stp] = sy;  s_cx[stp] = cx;  s_hw[stp] = half_w;
        s_ty[stp] = texY;  s_tY[stp] = tY;  s_ok[stp] = 1;
    }
    for (int k = 0; k < N; k++) {
        if (!s_ok[k] || !s_ok[k + 1]) continue;
        int span  = s_sy[k + 1] - s_sy[k];
        int steps = span < 0 ? -span : span;
        int sdir  = span < 0 ? -1 : 1;
        for (int t = 0; t <= steps; t++) {
            if (t == 0 && k > 0) continue;      /* shared boundary row: drawn by prev span */
            int y = s_sy[k] + sdir * t;
            if (y < 0 || y >= SCREEN_H) continue;
            int div = steps ? steps : 1;
            int cx     = s_cx[k] + ((s_cx[k + 1] - s_cx[k]) * t) / div;
            int half_w = s_hw[k] + ((s_hw[k + 1] - s_hw[k]) * t) / div;
            int texY   = s_ty[k] + ((s_ty[k + 1] - s_ty[k]) * t) / div;
            fx_t tY    = s_tY[k] + (fx_t)(((int64_t)(s_tY[k + 1] - s_tY[k]) * t) / div);
            if (half_w < 1) half_w = 1;
            int x0 = cx - half_w, x1 = cx + half_w;
            int left = x0;                      /* texture origin (pre-clip) */
            if (x0 < col_start) x0 = col_start;
            if (x1 > col_end - 1) x1 = col_end - 1;
            if (x0 > x1) continue;
            fx_t dtexX = (fx_t)((((int64_t)tex_w) << FX_SHIFT) / (2 * half_w));
            fx_t texX_fx = (fx_t)(x0 - left) * dtexX;
            uint8_t *prow = fb + (uintptr_t)y * SCREEN_W;
            for (int x = x0; x <= x1; x++) {
                if (tY < WALL_DIST(x)) {
                    int texX = fall_rev
                             ? tex_w - 1 - (int)(texX_fx >> FX_SHIFT)
                             : (int)(texX_fx >> FX_SHIFT);   /* viewer-side polarity */
                    if (texX < 0) texX = 0; else if (texX >= tex_w) texX = tex_w - 1;
                    uint8_t v = col_major ? tex[texX * tex_h + texY]
                                          : tex[texY * tex_w + texX];
                    if (v) prow[x] = face_up ? (uint8_t)(front_base + v) : back_c;
                }
                texX_fx += dtexX;
            }
        }
    }
}

/* ---- TRUE-3D chair -------------------------------------------------------
 * Real geometry projected through the raycaster's own camera, not a billboard:
 * each model vertex -> world (facing rotation + placement) -> camera space
 * (the same inv_det transform sprites use) -> perspective screen point, with
 * height mapped exactly like a standup's feet/top. Faces backface-cull by
 * screen winding, shade from a fixed light (per-facing constant, so it does
 * NOT swim as the player moves), painter-sort far->near, and fill with a
 * per-column z-test against WALL_DIST so walls occlude it. Rides the
 * neanderthal brown ramp (NEANDER_BASE + 1..7). */
static uint8_t chair_face_shade(int axis, fx_t fc, fx_t fs) {
    /* FIXED per-model-axis cel shade — must match the billboard bake
     * (tools/bake_dir_sprites.py FACES axsh {top6, +x/-x 4, +z/-z 3, bottom2}
     * through (s-1)*5/7): top 3, +x/-x 2, +z/-z 1, bottom 0. The bake is a
     * still per facing and CAN'T relight, so its face shades are constant per
     * model axis; the 3D chair used a world-fixed light that rotated with the
     * facing, so the SIDES drifted vs the sprite and popped colour at the LOD
     * swap. Matching removes the drift (fc/fs no longer needed). */
    (void)fc; (void)fs;
    switch (axis) {
    case 2:  return 3;   /* top    */
    case 3:  return 0;   /* bottom */
    case 0:  case 1: return 2;   /* +x / -x sides */
    default: return 1;   /* +z / -z front/back */
    }
}

/* Flicker offset (0 steady / 1 dim / 2 off) of the dominant (nearest, within 4
 * cells) light over (bx,by) — the SAME light + seed + thresholds draw_lights and
 * the chair shadow use, so a fixture, its shadow, and the chair under it all
 * pulse in one frame. Returns 0 when flicker is disabled or no light is near. */
static int light_flicker_at(fx_t bx, fx_t by) {
    if (!(SHARED_UC->lighting_flags & LIGHTING_FLICKER)) return 0;
    int best = -1; int32_t best_d2 = 0x7FFFFFFF;
    for (int L = 0; L < NUM_LIGHTS; L++) {
        int32_t dxc = (int32_t)((bx - lights[L].x) >> 8);
        int32_t dyc = (int32_t)((by - lights[L].y) >> 8);
        int32_t d2 = dxc * dxc + dyc * dyc;
        if (d2 >= 1048576) continue;                 /* >4 cells: no cast */
        if (d2 < best_d2) { best_d2 = d2; best = L; }
    }
    if (best < 0) return 0;
    uint32_t r = SHARED_UC->frame_count * 1103515245u + (uint32_t)best * 12347u;
    int roll = (int)((r >> 24) & 0x1F);
    if (roll < 2) return 2;
    if (roll < 5) return 1;
    return 0;
}

/* Scanline triangle fill, constant depth (the chair spans ~one cell; WALL_DIST
 * varies slowly across it), per-pixel wall z-test, clipped to this CPU's
 * column half. */
static void chair_tri_fill(int x0, int y0, int x1, int y1, int x2, int y2,
        uint8_t c, fx_t depth, int zt, int dither, int col_start, int col_end, uint8_t *fb) {
    int t;
    if (y0 > y1) { t=y0;y0=y1;y1=t; t=x0;x0=x1;x1=t; }
    if (y0 > y2) { t=y0;y0=y2;y2=t; t=x0;x0=x2;x2=t; }
    if (y1 > y2) { t=y1;y1=y2;y2=t; t=x1;x1=x2;x2=t; }
    if (y2 == y0) return;
    int32_t xl = x0 << 16, xls = ((x2 - x0) << 16) / (y2 - y0);
    for (int seg = 0; seg < 2; seg++) {
        int ya = seg ? y1 : y0, yb = seg ? y2 : y1;
        if (ya != yb) {
            int32_t xs = (seg ? x1 : x0) << 16;
            int32_t xss = ((seg ? (x2 - x1) : (x1 - x0)) << 16) / (yb - ya);
            for (int y = ya; y < yb; y++) {
                if (y >= 0 && y < SCREEN_H) {
                    int a = xl >> 16, b = xs >> 16;
                    if (a > b) { t=a;a=b;b=t; }
                    if (a < col_start) a = col_start;
                    if (b > col_end - 1) b = col_end - 1;
                    uint8_t *row = fb + (uintptr_t)y * SCREEN_W;
                    int par = dither ? (y & 1) : 2;  /* checkerboard: write x of this parity */
                    if (zt) {                        /* occlusion possible */
                        for (int x = a; x <= b; x++)
                            if ((x & 1) != par && depth < WALL_DIST(x)) row[x] = c;
                    } else {                         /* chair fully in the open */
                        for (int x = a; x <= b; x++)
                            if ((x & 1) != par) row[x] = c;
                    }
                }
                xl += xls; xs += xss;
            }
        } else if (seg == 0) {
            /* flat-top: advance the long edge across the (empty) top segment */
            xl += xls * (y1 - y0);
        }
    }
}

/* ---- Directional billboard sets -------------------------------------
 * A model baked by tools/bake_dir_sprites.py ships N views around the half
 * circle plus sector tables that map a bearing to (view, mirror). The chair
 * and the imported desk emit structurally identical tables under different
 * prefixes, so wrap them in one descriptor and the picker stops caring which
 * asset it is drawing. Adding a set is one row here plus the include. */
typedef struct { const uint8_t *tex; uint8_t w; } dirview_t;
typedef struct {
    const dirview_t *views;
    const uint8_t   *sect_v, *sect_view, *sect_mirror;
    uint16_t         nsect, h, wmax;
    uint16_t         vspan;   /* 8.8: image band height / model front height */
    /* Optional EXTRA ramp bases for a COMPOSITE bake, NULL-terminated. A set
     * baked from one material puts every texel on its own base (values 1..4)
     * and needs none of this. A composite cannot: the desk set is a gray
     * monitor on a brown desk carrying a charcoal console, and one ramp has
     * to paint two of those three wrong. tools/bake_dir_sprites.py --box-ramp
     * assigns each BOX a slot; slot 0 stays values 1..4 on .views' own base,
     * and slot n>0 takes four values at 10+(n-1)*4, appended past the screen
     * range so nothing already baked has to move. vbase[n-1] is that slot's
     * CRAM base — the same numbers boxmodels[].box_base uses, kept here
     * rather than in the bake because CRAM layout is the engine's business. */
    const uint8_t   *vbase;
} dirset_t;

/* Sets baked before the shared-band bake crop each view to its own content,
 * which already includes the pitched camera's top-face rows — 1.0 keeps them
 * drawing exactly as shipped. A re-bake emits the real constant. */
#ifndef CHAIR_DIR_VSPAN
#define CHAIR_DIR_VSPAN 256
#endif

/* Extra ramp bases for the desk set's slots 1 and 2, NULL-terminated. */
static const uint8_t deskset_vbase[] = { CHAIR_BASE, SMS_RAMP_BASE, 0 };
/* The free-standing PVM's one extra slot: its stand, on the same charcoal
 * ramp the near geometry now uses (pvm_box_base). */
static const uint8_t pvm_vbase[] = { SMS_RAMP_BASE, 0 };
static const dirset_t dirsets[] = {
    { (const dirview_t *)chair_dir_views, chair_dir_sect_v, chair_dir_sect_view,
      chair_dir_sect_mirror, CHAIR_DIR_SECTORS, CHAIR_DIR_H, CHAIR_DIR_WMAX,
      CHAIR_DIR_VSPAN },
    { (const dirview_t *)desk_dir_views,  desk_dir_sect_v,  desk_dir_sect_view,
      desk_dir_sect_mirror,  DESK_DIR_SECTORS,  DESK_DIR_H,  DESK_DIR_WMAX,
      DESK_DIR_VSPAN },
    { (const dirview_t *)pvm_dir_views,   pvm_dir_sect_v,   pvm_dir_sect_view,
      pvm_dir_sect_mirror,   PVM_DIR_SECTORS,   PVM_DIR_H,   PVM_DIR_WMAX,
      PVM_DIR_VSPAN, pvm_vbase },
    /* The desk set. Slot 0 (values 1..4) is the monitor on PVM_RAMP_BASE, the
     * set's own .base; slot 1 is the desk and slot 2 the console, matching
     * desk_pvm_box_base exactly — the far LOD wears the same three materials
     * the near geometry does, which is the whole point of baking it. */
    { (const dirview_t *)deskset_dir_views, deskset_dir_sect_v,
      deskset_dir_sect_view, deskset_dir_sect_mirror, DESKSET_DIR_SECTORS,
      DESKSET_DIR_H, DESKSET_DIR_WMAX, DESKSET_DIR_VSPAN, deskset_vbase },
};
/* The composite is NOT reachable by kind — it shares PVM_ASSET_KIND with the
 * floor-standing monitor, which is exactly why the desk set was drawing the
 * stand's billboard at range. It is selected per standup instead, the same
 * way boxmodel_for_standup picks its geometry. */
#define DIRSET_DESKSET (DIRSET_COUNT - 1)
static const uint8_t dirset_kind[] = { CHAIR_ASSET_KIND, DESK_ASSET_KIND,
                                       PVM_ASSET_KIND, 0xFF };
#define DIRSET_COUNT (int)(sizeof dirsets / sizeof dirsets[0])

/* Decode scratch is shared, so it must fit the WIDEST view of ANY set — the
 * desk's 114 dwarfs the chair's 40, and sizing off the chair would have run
 * the desk's decode off the end of the buffer. */
#define DIRSET_PV_A (CHAIR_DIR_WMAX * CHAIR_DIR_H)
#define DIRSET_PV_B (DESK_DIR_WMAX  * DESK_DIR_H)
#define DIRSET_PV_C (PVM_DIR_WMAX   * PVM_DIR_H)
#define DIRSET_PV_D (DESKSET_DIR_WMAX * DESKSET_DIR_H)
#define DIRSET_PV_AB (DIRSET_PV_A > DIRSET_PV_B ? DIRSET_PV_A : DIRSET_PV_B)
#define DIRSET_PV_CD (DIRSET_PV_C > DIRSET_PV_D ? DIRSET_PV_C : DIRSET_PV_D)
#define DIRSET_PV_MAX (DIRSET_PV_AB > DIRSET_PV_CD ? DIRSET_PV_AB : DIRSET_PV_CD)
/* This scratch is .bss and RAM here is nearly full — the desk's first bake at
 * --height 56 made it 6,384 B and overflowed the ram region by 2,336. Re-baking
 * the set shorter is the fix (a wide, short object needs rows, not height), so
 * fail HERE with a number rather than in the linker with a region name. */
_Static_assert(DIRSET_PV_MAX <= 4096,
               "directional decode scratch too big — re-bake the set at a "
               "smaller --height (see tools/bake_dir_sprites.py)");

static const dirset_t *dirset_for_kind(int kind) {
    for (int i = 0; i < DIRSET_COUNT; i++)
        if (dirset_kind[i] == kind) return &dirsets[i];
    return 0;
}
/* Billboard set for a placed standup. Kind alone cannot answer this: the desk
 * composite shares PVM_ASSET_KIND with the floor-standing monitor, so a
 * kind lookup handed the desk set the STAND's billboard and it swapped to a
 * monitor on legs at four cells. Mirrors boxmodel_for_standup. */
static const dirset_t *dirset_for_standup(int i) {
    if (standup_on_desk[i]) return &dirsets[DIRSET_DESKSET];
    return dirset_for_kind(standups[i].kind);
}

/* ---- Box models -----------------------------------------------------
 * A kind that owns real 3D geometry: the hand-authored chair and anything
 * imported through tools/bake_boxes.py. The render path keys off THIS rather
 * than a hardcoded CHAIR_SPRITE_KIND test, so a new import draws in true 3D
 * with no renderer edits. World height comes from sprite_defs[kind].world_h,
 * so the box model and its billboard can never disagree about scale. */
/* Largest box count of any model in ROM. Sizes both the viewer's bx_* mesh
 * arrays and draw_chair_3d's per-call face buffer, so EVERY model that can
 * reach either path must be counted here — otherwise an oversized import runs
 * off the end of a stack array in the hot render loop. */
#define BX_MAXBOXES 10   /* was CHAIR_NBOXES (9); the desk-PVM-console
                          * composite carries 10. Costs ~170B more of the
                          * deep render stack (faces[]) — inside budget. */
_Static_assert(CHAIR_NBOXES <= BX_MAXBOXES && DESK_NBOXES <= BX_MAXBOXES &&
               PVM_NBOXES <= BX_MAXBOXES && DESK_PVM_NBOXES <= BX_MAXBOXES,
               "imported box model exceeds the box-render arrays — raise BX_MAXBOXES");

/* PVM ramp: the comm_pal arena rows for registry pal[0..3] (base 184 -> CRAM
 * 185..188, dark charcoal to light case gray). Kept in lockstep with
 * registry.json assets.sprites[pvm] by the comm_pal.h codegen. */
#define PVM_RAMP_BASE 185
_Static_assert(GLASS_DEAD == PVM_RAMP_BASE,
               "game-on-glass dead-cell index must be the PVM ramp's dark glass");
static const boxmodel_t desk_pvm_model;
/* The free-standing PVM's STAND (boxes 1..5: base plate and four legs) had
 * the same shade collapse the desk and the console did — stand_bias 2 held it
 * near-black by spending two of its four levels, so its top, sides and front
 * all landed on the same two indices. It wants the same answer: darkness from
 * the PALETTE, not from the bias. SMS_RAMP_BASE is already four charcoal
 * steps and a black metal stand is exactly what it suits, so this costs no
 * new CRAM. The monitor (box 0) keeps the case-gray PVM ramp. */
static const uint8_t pvm_box_base[PVM_NBOXES] = {
    PVM_RAMP_BASE,                                          /* monitor */
    SMS_RAMP_BASE, SMS_RAMP_BASE, SMS_RAMP_BASE,
    SMS_RAMP_BASE, SMS_RAMP_BASE };                         /* stand    */
static const uint8_t pvm_box_bias[PVM_NBOXES] = { 0, 0, 0, 0, 0, 0 };

static const boxmodel_t boxmodels[] = {
    { chair_boxes, CHAIR_NBOXES, CHAIR_ASSET_KIND, CHAIR_BASE, 0, 0, 0, 0 },
    { desk_boxes,  DESK_NBOXES,  DESK_ASSET_KIND,  CHAIR_BASE, 0, 0, 0, 0 },
    { pvm_boxes,   PVM_NBOXES,   PVM_ASSET_KIND,   PVM_RAMP_BASE,
      (const uint8_t *)pvm_front_tex, PVM_FRONT_TEX_W, PVM_FRONT_TEX_H, 2,
      pvm_box_base, pvm_box_bias, (const uint8_t *)pvm_rear_tex,
      (const struct boxmodel_s *)&desk_pvm_model, "DESK SET" },
};
#define BOXMODEL_COUNT (int)(sizeof boxmodels / sizeof boxmodels[0])

static const boxmodel_t *boxmodel_for_kind(int kind) {
    for (int i = 0; i < BOXMODEL_COUNT; i++)
        if (boxmodels[i].kind == kind) return &boxmodels[i];
    return 0;
}

/* The desk-with-PVM composite: PVM kind (so every monitor behavior applies
 * verbatim) but its own geometry and mixed ramps. Selected per STANDUP via
 * the on_desk flag, never by kind lookup. Drawn under world_h 0.46 (the
 * model tops out at y=294 of the 0.4-cell scale = 0.46 cells). */
static const uint8_t desk_pvm_box_base[DESK_PVM_NBOXES] = {
    PVM_RAMP_BASE, CHAIR_BASE, CHAIR_BASE,
    CHAIR_BASE, CHAIR_BASE, CHAIR_BASE,
    SMS_RAMP_BASE, SMS_RAMP_BASE };    /* the Master System: body + sloped */
                                       /* upper, on its OWN charcoal ramp  */
/* Per-box shade bias — every box 0 now, which is the point. The scalar 2 this
 * composite inherited from the PVM (where it drops the CART to near-black) was
 * being applied to the DESK, taking two steps off a four-step ramp and landing
 * the pedestals' front (-z, shade 1) and side (+-x, shade 2) faces both on 0:
 * flat brown, no gradient.
 *
 * The CONSOLE had the identical collapse for the identical reason, and biasing
 * is the wrong tool for it either way — bias buys darkness by SPENDING shade
 * levels, so a model that must be both dark and lit cannot have both. Its own
 * ramp (SMS_RAMP_BASE) buys the darkness in the palette instead and leaves all
 * four levels to do their job. */
static const uint8_t desk_pvm_box_bias[DESK_PVM_NBOXES] = {
    0,                                 /* monitor — primary mass, full range */
    0, 0, 0, 0, 0,                     /* desk: pedestals + the split slab    */
    0, 0 };                            /* Master System, dark via its ramp    */
static const boxmodel_t desk_pvm_model = {
    desk_pvm_boxes, DESK_PVM_NBOXES, PVM_ASSET_KIND, PVM_RAMP_BASE,
    (const uint8_t *)pvm_front_tex, PVM_FRONT_TEX_W, PVM_FRONT_TEX_H, 2,
    desk_pvm_box_base, desk_pvm_box_bias, (const uint8_t *)pvm_rear_tex };

static const boxmodel_t *boxmodel_for_standup(int i) {
    if (standup_on_desk[i]) return &desk_pvm_model;
    return boxmodel_for_kind(standups[i].kind);
}
static fx_t world_h_for_standup(int i) {
    /* composite tops out at y=294 of the 0.4 scale = 0.46 cells */
    if (standup_on_desk[i]) return (fx_t)(FX(0.4) * 294 / 256);
    return sprite_defs[standups[i].kind].world_h;
}

/* World-space half-extents of a box model's footprint, straight from its box
 * list (model units are 8.8 with height 1.0, scaled by the sprite's world_h).
 * Collision reads this so the blocker always matches the geometry drawn. */
static void boxmodel_footprint(int kind, fx_t *hx, fx_t *hz) {
    boxmodel_footprint_bm(boxmodel_for_kind(kind),
                          sprite_defs[kind].world_h, hx, hz);
}
static void boxmodel_footprint_bm(const boxmodel_t *bm, fx_t wh,
                                  fx_t *hx, fx_t *hz) {
    *hx = *hz = 0;
    if (!bm) return;
    int x0 = 32767, x1 = -32768, z0 = 32767, z1 = -32768;
    for (int b = 0; b < bm->nboxes; b++) {
        int16_t qlo[3], qhi[3];
        cbox_bounds(&bm->boxes[b], qlo, qhi);   /* wedge top can overhang */
        if (qlo[0] < x0) x0 = qlo[0];
        if (qhi[0] > x1) x1 = qhi[0];
        if (qlo[2] < z0) z0 = qlo[2];
        if (qhi[2] > z1) z1 = qhi[2];
    }
    *hx = (fx_t)((((int32_t)(x1 - x0) / 2) * wh) >> 8);
    *hz = (fx_t)((((int32_t)(z1 - z0) / 2) * wh) >> 8);
}

/* BOX PAINT ORDER, exact.
 *
 * Two DISJOINT axis-aligned boxes always have an axis on which their intervals
 * do not overlap. That gap is a separating plane, and the box on the same side
 * of it as the eye is unambiguously the nearer one — paint it last. No
 * centroid, no tie, and it works in HEIGHT, which is the whole point:
 * faces[].depth is horizontal distance and never sees height at all.
 *
 * That blind spot is the desk. Its tabletop spans the entire footprint, so its
 * centroid depth reads "middle of the desk" — the same as the pedestals'. From
 * a crouch the tabletop is ABOVE the eye and the pedestals are BESIDE it, but a
 * horizontal-only key cannot tell those apart, so the tabletop's underside won
 * the sort and painted over the pedestal faces you should see through the knee
 * hole. Pixel diff against a ray-cast of the same camera: 944 wrong pixels on
 * the centroid order, 282 on this one, and the entire tabletop-over-pedestal
 * class is gone (the rest is single-pixel silhouette edging in the test
 * rasteriser). The chair never showed it because none of its boxes spans
 * another the way the tabletop spans both pedestals.
 *
 * Faces WITHIN a box need no sort at all: a convex box's visible faces meet
 * only at edges and can never overlap. Verified pixel-identical with and
 * without, which is what pays for this function's space in .ramtext.
 *
 * e[] is the eye in model units (x, y=height above floor, z). */
static int box_nearer(const cbox_t *mb, int ia, int ib, const int32_t *e) {
    int16_t alo[3], ahi[3], blo[3], bhi[3];
    cbox_bounds(&mb[ia], alo, ahi);      /* union of base + wedge top rect */
    cbox_bounds(&mb[ib], blo, bhi);
    for (int k = 0; k < 3; k++) {
        if (ahi[k] <= blo[k]) return (e[k] < (int32_t)ahi[k]) ?  1 : -1;
        if (bhi[k] <= alo[k]) return (e[k] < (int32_t)bhi[k]) ? -1 :  1;
    }
    return 0;                    /* touching on every axis: order is a no-op */
}

__attribute__((noinline))
static void boxmodel_order(const cbox_t *mb, int n, fx_t eox, fx_t eoy,
                           fx_t fc, fx_t fs, fx_t world_h, uint8_t eye_h,
                           uint8_t *ord) {
    /* Eye into model space: un-rotate about the object centre by the facing
     * (inverse of the per-vertex rx/rz rotation), then scale to model units.
     * Height comes from eye_h, the same value that drives `focal`. */
    fx_t inv_wh = fx_div_hw(FX_ONE, world_h);
    int32_t e[3];
    e[0] = FX_MUL(FX_MUL(eox, fc) - FX_MUL(eoy, fs), inv_wh) >> 8;
    e[1] = FX_MUL((fx_t)eye_h << 8, inv_wh) >> 8;
    e[2] = FX_MUL(FX_MUL(eox, fs) + FX_MUL(eoy, fc), inv_wh) >> 8;
    for (int i = 0; i < n; i++) ord[i] = (uint8_t)i;
    for (int i = 1; i < n; i++) {              /* insertion sort, far -> near */
        uint8_t v = ord[i];
        int j = i;
        while (j > 0 && box_nearer(mb, ord[j - 1], v, e) > 0) {
            ord[j] = ord[j - 1]; j--;
        }
        ord[j] = v;
    }
}

/* Forward decl: tex_tri is defined just below draw_chair_3d, but the
 * MODE+A textured-chair A/B path calls it from inside the chair face loop. */
static void tex_tri(uint8_t *fb, int col_start, int col_end, fx_t depth,
        const uint8_t *tex, int tw, int th, int col_major, int skip_z, int shadow,
        uint8_t front_base, uint8_t back_c, int is_front,
        int ax, int ay, fx_t au, fx_t av, int bx, int by, fx_t bu, fx_t bv,
        int cx, int cy, fx_t cu, fx_t cv);
/* Opaque box-face texture fill (the PVM's screen/control panel). Own routine
 * rather than a tex_tri mode: tex_tri's inner loop is the screen-filling
 * neanderthal quad's, and a per-pixel LUT branch there taxes the 7fps-floor
 * scene for a face it never draws. Texels are 1..4 ramp values decoded through
 * lut[] (fog/flicker/dark folded in by the caller), never 0 — no transparency
 * test, every covered pixel paints.
 * Texels 5..9 are the SCREEN (bake_face_tex): 5 = the elliptical glass core,
 * 6..9 = the bezel-opening rim carrying its albedo (v-5). Powered off
 * (noise_seed 0) the core paints lut[5] dark and the rim paints its albedo —
 * the corner shading that reads as CRT glass curvature. Powered on, the WHOLE
 * opening floods with per-pixel static from lut[10..13] — the RAW ramp, no
 * fog/dark walk: a CRT emits light, so the static glows full-contrast in the
 * gloom instead of collapsing like a lit surface. */
static void tex_tri_lut(uint8_t *fb, int col_start, int col_end, fx_t depth,
        const uint8_t *tex, int tw, int th, int do_z, const uint8_t *lut,
        unsigned noise_seed, const uint8_t *scr, const int *bloom,
        int ax, int ay, fx_t au, fx_t av, int bx, int by, fx_t bu, fx_t bv,
        int cx, int cy, fx_t cu, fx_t cv);

/* Decorrelated 2-bit noise for the static. The first cut hashed x*13 + y*29,
 * and the linear form showed exactly as you'd fear: diagonal banding that
 * read as a waveform crawling the glass. Table-scramble instead: the row
 * lookup perturbs the column index, so no linear structure survives.
 * xorshift32(0x32583258), generated once — content is arbitrary, stability
 * is not (both CPUs and the bake must agree it's ROM const). */
static const uint8_t static_tbl[256] = {
    113,205,254,235,  3,131,130,121,128, 88,140,203,  9, 72,105, 70,
    235,158,152, 70, 50, 79,229,161,103,151,254,169,234,172, 19,227,
    185,211,191,226,226, 31,182, 61,181, 66,206,  1,191,141,104,  4,
     26,141,168,148, 94, 71, 20,154,147,214, 13, 80,243, 21, 81, 89,
    137, 15,159, 62, 17,122,150, 18,179, 13,187,110,150,191, 52,162,
     21,150, 60,125,167,232,172, 23,184,121,213, 82,105,108, 27,126,
     28,179, 72, 67,198, 85,188,211, 81,165, 32,143, 99,215, 91,231,
    142, 18,198, 59,219,159,160,182,193,141,249, 36, 44,153, 60, 76,
     18, 48, 99,194,  9,194, 91,  8,235,167, 56,229, 44,117, 76,150,
    137, 50, 91,186,199,107,146, 25,189,137,  3,136, 68,243,168,193,
     75,192, 75,186,113,170,235,246,171, 90,105, 15,134, 56,158, 16,
     17, 99,153,145,128,  5,173,241, 74, 24, 60, 43,  7, 78,251,156,
    154,126,213, 15, 35, 24, 76, 53, 76,205,190,124,192,126,  4,247,
     92,  5, 81, 43,200,191,123,242,  7, 80, 25, 20,220, 74,135, 89,
    224, 40,105,175,248, 68,164, 85, 15,130,194, 80,127,153,255,116,
    251,137,104,164,101,197,  8,216,175,177,230, 18,  3,255,  6,175,
};
#define STATIC_NOISE(x, y, seed) \
    (static_tbl[((x) + static_tbl[((y) + (seed)) & 255] + ((seed) >> 8)) & 255] & 3)

/* One projected box face, shared by draw_chair_3d and draw_panel_face. */
typedef struct { int16_t sx[4], sy[4]; fx_t depth; uint8_t shade;
                 uint8_t ftex; uint8_t bxi;
                 int16_t ccx, ccy; } cface_t;   /* face-center projection;
                                  * ccx == -32768 -> no fan (clipped) */

/* The textured front face, LUT build included. Split out of draw_chair_3d and
 * noinline for the same RAM reason as draw_boxmodel_shadow: inlined, this
 * (two 15-arg call setups plus the fog math) grew the RAMTEXT draw_standups
 * blob past the ram region — as a ROM call the hot path keeps only the
 * per-face flag test. */
__attribute__((noinline))
static void draw_panel_face(uint8_t *fb, int col_start, int col_end,
        const cface_t *fc, const boxmodel_t *bm,
        fx_t center_depth, int chair_dark, int dark, int zt, int power,
        const uint8_t *scr, int bloom_e) {
    /* Same fog + flicker + dark-room walk the flat faces get, applied to ramp
     * values 1..4 so the textured face dims in lockstep and the LOD swap
     * stays seamless. 5 = dark glass (screen, power off). 6..9 = the RAW ramp
     * for the static hash — emissive, so it cuts through fog and dark rooms. */
    uint8_t flut[14];
    int fog_c = 0;
    fx_t fdc = center_depth - FX(2);
    if (fdc > 0) fog_c = (int)(((int64_t)fdc * 5) / (FOG_RAMP_DIST - FX(2)));
    for (int v = 1; v <= 4; v++) {
        int sh = (v - 1) - fog_c - chair_dark;
        if (sh < 0 || dark) sh = 0;
        flut[v] = (uint8_t)(bm->base + sh);          /* panel + off-rim (v-5) */
        flut[9 + v] = (uint8_t)(bm->base + (v - 1)); /* raw ramp: static */
    }
    flut[5] = (uint8_t)(bm->base + 0);               /* dark glass core */
    flut[6] = flut[1]; flut[7] = flut[2];            /* unused by the decode,  */
    flut[8] = flut[3]; flut[9] = flut[4];            /* set so nothing floats  */
    unsigned seed = (power && !scr)
        ? (((unsigned)SHARED_UC->frame_count * 2654435761u) | 1) : 0;
    /* This face will paint live noise — tell the ULTRA park to stay away this
     * frame (a parked frame freezes the static into a photograph). Once per
     * face, not per pixel: the write is uncached. */
    if (seed) SHARED_UC->pvm_static_live = 1;
    if (!power) scr = 0;                 /* dark glass wins over telegraph */
    /* Rear face: Mike's back panel, case values only — no glass texels,
     * so the screen branches (static/telegraph/bloom) never trigger. */
    const uint8_t *ptex = (fc->ftex == 2) ? bm->rtex : bm->ftex;
    /* UVs by corner identity (face-5 vi order is bl,tl,tr,br; texture row 0 =
     * top), in texel units for the shift-sampling. Same two-triangle split as
     * the flat fill. */
    /* Bloom shaping, precomputed per face (stack = per-CPU, no races):
     * {elapsed, band half-height, unfold step 8.8, glass center ty}. */
    int bloomv[5]; const int *bp = 0;
    if (bloom_e >= 0 && !(power && bloom_e == BLOOM_FRAMES)) {
        /* (power-on frame BLOOM_FRAMES draws PLAIN: the one stable catch
         * frame between the unfold and the flicker drop.) */
        bloomv[0] = bloom_e;
        int half = tg_bh / 2; if (half < 1) half = 1;
        bloomv[3] = tg_by0 + half;             /* default: glass center */
        bloomv[4] = !power;                    /* 1 = collapse (power-off) */
        if (power && bloom_e > BLOOM_FRAMES) {
            bloomv[4] = 2;                     /* the drop: one dark frame */
        } else if (!power) {
            /* Frame 0: thin band at center. Frame 1: single line fallen
             * to the lower glass, on its way off the bottom. */
            if (bloom_e == 0) {
                int hv = half >> 2; if (hv < 1) hv = 1;
                bloomv[1] = hv;
            } else {
                bloomv[1] = 0;                 /* one texel */
                bloomv[3] = tg_by0 + tg_bh - (tg_bh >> 3) - 1;
            }
            bloomv[2] = 256;
        } else if (bloom_e >= STRIKE_FRAMES) {
            int hv = (half * (bloom_e - STRIKE_FRAMES + 1))
                   / (BLOOM_FRAMES - STRIKE_FRAMES);
            if (hv < 1) hv = 1;
            bloomv[1] = hv;
            bloomv[2] = (half << 8) / hv;
        } else { bloomv[1] = 0; bloomv[2] = 256; }
        bp = bloomv;
    }
    fx_t TW = FX(bm->ftw), TH = FX(bm->fth);
    if (fc->ccx != -32768) {
        /* 4-triangle fan around the true-perspective center: each patch's
         * affine error is a quarter of the 2-tri split's. Corner UV order
         * differs per face (front vs rear have different vi windings). */
        fx_t U[4], V[4];
        /* FRONT U runs TW->0, not 0->TW. The in-game face reads mirrored
         * against the asset viewer and the dir bake, which both show the
         * panel as authored — so the flip belongs HERE, on the one path that
         * is wrong, and not in the texture. Mirroring the texture instead was
         * tried and it just moved the problem: pvm_front_tex feeds the near
         * face, the viewer, and both baked billboards, so flipping the data
         * turned the other three around with it.
         *
         * Comparing UV corner order between the paths is NOT enough to tell
         * which is mirrored — the corners are identical here and in
         * raycast_model_view. What differs is upstream, in the vertex
         * transform each path runs before these UVs are ever applied. */
        if (fc->ftex == 2) { U[0]=TW; V[0]=TH; U[1]=0; V[1]=TH; U[2]=0; V[2]=0; U[3]=TW; V[3]=0; }
        else               { U[0]=TW; V[0]=TH; U[1]=TW; V[1]=0; U[2]=0;  V[2]=0; U[3]=0;  V[3]=TH; }
        for (int k = 0; k < 4; k++) {
            int k2 = (k + 1) & 3;
            tex_tri_lut(fb, col_start, col_end, fc->depth,
                    ptex, bm->ftw, bm->fth, zt, flut, seed, scr, bp,
                    fc->sx[k], fc->sy[k],  U[k],  V[k],
                    fc->sx[k2],fc->sy[k2], U[k2], V[k2],
                    fc->ccx,   fc->ccy,    TW/2,  TH/2);
        }
        return;
    }
    if (fc->ftex == 2) {
        /* REAR face (+z, chair_face_v order bl,br,tr,tl viewed from
         * behind, model +x on the viewer's LEFT): its own UV corner
         * assignment — reusing the front's rotated the panel 90°. */
        tex_tri_lut(fb, col_start, col_end, fc->depth,
                ptex, bm->ftw, bm->fth, zt, flut, seed, scr, bp,
                fc->sx[0],fc->sy[0], TW, TH,
                fc->sx[1],fc->sy[1], 0,  TH,
                fc->sx[2],fc->sy[2], 0,  0);
        tex_tri_lut(fb, col_start, col_end, fc->depth,
                ptex, bm->ftw, bm->fth, zt, flut, seed, scr, bp,
                fc->sx[0],fc->sy[0], TW, TH,
                fc->sx[2],fc->sy[2], 0,  0,
                fc->sx[3],fc->sy[3], TW, 0);
        return;
    }
    tex_tri_lut(fb, col_start, col_end, fc->depth,
            ptex, bm->ftw, bm->fth, zt, flut, seed, scr, bp,
            fc->sx[0],fc->sy[0], 0,  TH,
            fc->sx[1],fc->sy[1], 0,  0,
            fc->sx[2],fc->sy[2], TW, 0);
    tex_tri_lut(fb, col_start, col_end, fc->depth,
            ptex, bm->ftw, bm->fth, zt, flut, seed, scr, bp,
            fc->sx[0],fc->sy[0], 0,  TH,
            fc->sx[2],fc->sy[2], TW, 0,
            fc->sx[3],fc->sy[3], TW, TH);
}

static void draw_chair_3d(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb,
        const boxmodel_t *bm, fx_t world_h) {
    const cbox_t *mboxes = bm->boxes;
    int mnboxes = bm->nboxes;
    fx_t cx = standups[i].x, cy = standups[i].y;
    uint8_t facing = standups[i].facing_angle;
    /* Rotate so the model's FRONT (-z, the open seat side) points along the
     * decal facing dir (cos fa, sin fa) — the registry/billboard convention.
     * The raw rotation mapped face=E to a north-facing chair (front came out
     * at 192-fa), so the 3D pop-in disagreed with the directional billboard
     * by a quarter turn. Sim-verified for all four cardinals. (The rotation
     * is mirrored vs the raw one; the box chair is x-symmetric so this is
     * invisible.) */
    fx_t fc = COS_FX((uint8_t)(facing + 64)), fs = -SIN_FX((uint8_t)(facing + 64));
    int focal = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    /* Near clip tied to the chair's collision box: the 0.20 half-extent
     * square stops the camera >=~0.10 from any vertex, so 0.08 never
     * fires in legal positions — faces render all the way to contact.
     * The old 0.25 dropped the FRONT faces while the player was still
     * legally outside the hitbox: 'visually clips through the chair'. */
    const fx_t NEARC = FX(0.08);

    /* Screen coords are 16-bit: the near-clip (depth >= FX(0.2)) bounds every
     * projected corner to well under +/-1000, so int16 is lossless and takes
     * ~864 B off the deep render stack (the high-water probe found the Master
     * OVER budget in crawlspace scenes). */
    cface_t faces[BX_MAXBOXES * 6];
    int nf = 0;

    /* Exact far->near box order (separating axis, height included). Faces are
     * then appended box by box and NOT re-sorted: a convex box's visible faces
     * cannot overlap each other. See boxmodel_order. */
    uint8_t border[BX_MAXBOXES];
    boxmodel_order(mboxes, mnboxes, px - cx, py - cy, fc, fs, world_h,
                   SHARED_UC->eye_h, border);

    for (int bo = 0; bo < mnboxes; bo++) {
        int b = border[bo];
        const cbox_t *bx = &mboxes[b];
        int csx[8], csy[8], cclip[8];
        fx_t cdep[8];
        for (int v = 0; v < 8; v++) {
            int16_t cvx, cvy, cvz;                     /* 8.8 model */
            cbox_corner(bx, v, &cvx, &cvy, &cvz);      /* wedge-aware */
            int32_t mx = cvx, my = cvy, mz = cvz;
            fx_t wx = (fx_t)((mx * world_h) >> 8);            /* model -> world fx */
            fx_t wy = (fx_t)((my * world_h) >> 8);            /* height above floor */
            fx_t wz = (fx_t)((mz * world_h) >> 8);
            fx_t rx = FX_MUL(wx, fc) + FX_MUL(wz, fs);        /* rotate by facing */
            fx_t rz = -FX_MUL(wx, fs) + FX_MUL(wz, fc);
            fx_t ddx = (cx + rx) - px, ddy = (cy + rz) - py;
            fx_t depth = FX_MUL(inv_det, -FX_MUL(planeY, ddx) + FX_MUL(planeX, ddy));
            cdep[v] = depth;
            cclip[v] = depth < NEARC;
            if (cclip[v]) { csx[v] = csy[v] = 0; continue; }
            /* One reciprocal, then multiplies: screenX, floor row, and the
             * height offset all divide by depth — three HW divides per vertex
             * (56 verts x 2 CPUs) was the chair's dominant cost. */
            fx_t inv_d = fx_div_hw(FX_ONE, depth);            /* 1/depth, 16.16 */
            fx_t lat = FX_MUL(inv_det, FX_MUL(dirY, ddx) - FX_MUL(dirX, ddy));
            csx[v] = (SCREEN_W >> 1)
                   + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(lat, inv_d)) >> FX_SHIFT);
            int floor_y = horizon_y + (int)(((int64_t)focal * inv_d) >> FX_SHIFT);
            csy[v] = floor_y - (int)((FX_MUL(wy, inv_d) * SCREEN_H) >> FX_SHIFT);
        }
        for (int f = 0; f < 6; f++) {
            const uint8_t *vi = chair_face_v[f];
            if (cclip[vi[0]] || cclip[vi[1]] || cclip[vi[2]] || cclip[vi[3]]) continue;
            int32_t ax = csx[vi[1]]-csx[vi[0]], ay = csy[vi[1]]-csy[vi[0]];
            int32_t bx2 = csx[vi[2]]-csx[vi[0]], by = csy[vi[2]]-csy[vi[0]];
            /* Backface cull. Sign proven against the engine's own projection
             * (plane = (-dirY,dirX)*0.66, det = -0.66, screen y down): visible
             * front faces project NEGATIVE cross. The old <= kept the far
             * faces — every box rendered inside-out. The silhouette was
             * identical (closed box), but each shade came from the OPPOSITE
             * face: seat top wore the bottom's darkest shade in-game while
             * the (cull-free) viewer showed it lit.
             *
             * Re-verified against a model-space plane-side test (is the eye
             * outside this face's plane) over 9216 poses of the real desk:
             * agreement on every single face, zero disagreements. This cull is
             * NOT the desk bug — that is the seam test on the next line. */
            if (ax * by - ay * bx2 >= 0) continue;
            faces[nf].depth = (cdep[vi[0]]+cdep[vi[1]]+cdep[vi[2]]+cdep[vi[3]]) >> 2;
            {
                int fsh = chair_face_shade(f, fc, fs);
                int bias = boxmodel_bias(bm, b);
                fsh = (fsh > bias) ? fsh - bias : 0;
                faces[nf].shade = (uint8_t)fsh;
            }
            /* Face 5 is -z, the model front (the facing rotation points it
             * along the decal dir) — the face a ftex model paints its panel
             * texture on. Only box 0 (the carve's primary mass) carries it. */
            faces[nf].ftex = (bm->ftex != 0 && b == 0 && f == 5) ? 1
                           : (bm->rtex != 0 && b == 0 && f == 4) ? 2 : 0;
            faces[nf].bxi  = (uint8_t)b;
            faces[nf].ccx = -32768;
            if (faces[nf].ftex) {
                /* TRUE-perspective center for the 4-triangle fan: affine UV
                 * across two big triangles shears the bezel up close (the
                 * PS1 wobble, Mike's warp screenshot). One more projection
                 * halves the error. Same transform as the corner verts. */
                int32_t mcx = ((int32_t)bx->x0 + bx->x1) >> 1;
                int32_t mcy = ((int32_t)bx->y0 + bx->y1) >> 1;
                int32_t mcz = (f == 4) ? bx->z1 : bx->z0;
                fx_t wxc = (fx_t)((mcx * world_h) >> 8);
                fx_t wyc = (fx_t)((mcy * world_h) >> 8);
                fx_t wzc = (fx_t)((mcz * world_h) >> 8);
                fx_t rxc = FX_MUL(wxc, fc) + FX_MUL(wzc, fs);
                fx_t rzc = -FX_MUL(wxc, fs) + FX_MUL(wzc, fc);
                fx_t dxc = (cx + rxc) - px, dyc = (cy + rzc) - py;
                fx_t dc = FX_MUL(inv_det, -FX_MUL(planeY, dxc) + FX_MUL(planeX, dyc));
                if (dc >= NEARC) {
                    fx_t idc = fx_div_hw(FX_ONE, dc);
                    fx_t latc = FX_MUL(inv_det, FX_MUL(dirY, dxc) - FX_MUL(dirX, dyc));
                    faces[nf].ccx = (int16_t)((SCREEN_W >> 1)
                        + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(latc, idc)) >> FX_SHIFT));
                    int fyc = horizon_y + (int)(((int64_t)focal * idc) >> FX_SHIFT);
                    faces[nf].ccy = (int16_t)(fyc
                        - (int)((FX_MUL(wyc, idc) * SCREEN_H) >> FX_SHIFT));
                }
            }
            for (int k = 0; k < 4; k++) { faces[nf].sx[k]=csx[vi[k]]; faces[nf].sy[k]=csy[vi[k]]; }
            nf++;
        }
    }
    /* No face sort here on purpose. faces[] is already in exact paint order:
     * boxes far->near from boxmodel_order, and faces within a box in any order
     * because a convex box's visible faces never overlap. The centroid-depth
     * sort this replaces was the desk bug — it ranked a full-footprint tabletop
     * against the pedestals it spans using horizontal distance alone. */
    /* Occlusion pre-check: if the chair's FARTHEST face is nearer than every
     * wall across its column span, no pixel can be occluded — the fills skip
     * the per-pixel WALL_DIST read entirely (the common open-room case). */
    int minx = SCREEN_W, maxx = -1; fx_t maxdep = 0;
    for (int q = 0; q < nf; q++) {
        if (faces[q].depth > maxdep) maxdep = faces[q].depth;
        for (int k = 0; k < 4; k++) {
            if (faces[q].sx[k] < minx) minx = faces[q].sx[k];
            if (faces[q].sx[k] > maxx) maxx = faces[q].sx[k];
        }
    }
    if (minx < col_start) minx = col_start;
    if (maxx > col_end - 1) maxx = col_end - 1;
    int zt = 0;
    for (int x = minx; x <= maxx; x++)
        if (maxdep >= WALL_DIST(x)) { zt = 1; break; }
    /* Fog off the chair CENTRE depth, uniform across all faces — the billboard
     * fogs uniformly off transformY, so per-face fog (a gradient across the
     * chair) left a faint tone difference at the LOD swap. One depth, matched. */
    fx_t center_depth = FX_MUL(inv_det, -FX_MUL(planeY, cx - px) + FX_MUL(planeX, cy - py));
    /* Flicker response: close up (inside the 2-cell fog cutoff) the chair is at
     * full shade, so when its light strobes fully OFF, drop the faces one subtle
     * step — the seat catches it most. Barely there, but it pulses in sync with
     * the shadow fade and the panel above. */
    int chair_dark = (light_flicker_at(cx, cy) >= 2) ? 1 : 0;
    for (int q = 0; q < nf; q++) {
        /* Distance fog: chairs used to stay full-bright to the horizon while
         * everything around them faded. Fade the door shade toward the darkest
         * brown (index 0) past ~2 cells, reaching full fog by FOG_RAMP_DIST,
         * same ramp the walls use. Cheap: one calc per face. */
        int shade = faces[q].shade;
        fx_t fd = center_depth - FX(2);
        if (fd > 0) {
            int fog = (int)(((int64_t)fd * 5) / (FOG_RAMP_DIST - FX(2)));
            shade -= fog;
        }
        shade -= chair_dark;                    /* subtle light-flicker dim */
        if (shade < 0) shade = 0;
        /* A chair standing in a dark room honors the dark: collapse to the
         * deepest brown so it reads as a shape in the gloom, like the walls. */
        if (cell_is_dark(cx, cy)) shade = 0;
        uint8_t fbase = bm->box_base ? bm->box_base[faces[q].bxi]
                                     : bm->base;
        uint8_t c = (uint8_t)(fbase + shade);
        if (faces[q].ftex) {
            {
                int be = -1;
                {
                    uint16_t el = (uint16_t)((uint16_t)SHARED_UC->frame_count
                                             - standup_bloom_start[i]);
                    uint16_t win = standup_power[i]
                                 ? (BLOOM_FRAMES + ON_SETTLE) : OFF_FRAMES;
                    if (el < win) be = (int)el;
                }
                /* Publish the GLASS's projected rect for the zoom: quad bbox
                 * insetted by the glass's texel fractions (front U samples
                 * MIRRORED — see the UV comment in draw_panel_face — so the
                 * x fractions swap sides). Face-on this is exact; during the
                 * approach it is within a pixel or two, which is all the
                 * hand-over needs. Uncached: either CPU may own this face. */
                if (glass_active && standup_on_desk[i] && faces[q].ftex == 1) {
                    int qx0 = faces[q].sx[0], qx1 = qx0;
                    int qy0 = faces[q].sy[0], qy1 = qy0;
                    for (int v2 = 1; v2 < 4; v2++) {
                        int sx = faces[q].sx[v2], sy = faces[q].sy[v2];
                        if (sx < qx0) qx0 = sx; if (sx > qx1) qx1 = sx;
                        if (sy < qy0) qy0 = sy; if (sy > qy1) qy1 = sy;
                    }
                    int qw = qx1 - qx0, qh = qy1 - qy0;
                    int fw = bm->ftw, fh = bm->fth;
                    SHARED_UC->glass_sr_x0 =
                        (int16_t)(qx0 + qw * (fw - tg_bx0 - tg_bw) / fw);
                    SHARED_UC->glass_sr_x1 =
                        (int16_t)(qx0 + qw * (fw - tg_bx0) / fw);
                    SHARED_UC->glass_sr_y0 =
                        (int16_t)(qy0 + qh * tg_by0 / fh);
                    SHARED_UC->glass_sr_y1 =
                        (int16_t)(qy0 + qh * (tg_by0 + tg_bh) / fh);
                }
                draw_panel_face(fb, col_start, col_end, &faces[q], bm,
                            center_depth, chair_dark, cell_is_dark(cx, cy), zt,
                            standup_power[i],
                            glass_active ? glass_buf :
                            (standup_scr_mode[i] && tg_valid) ? tg_buf : 0, be);
            }
            continue;
        }
        if (SHARED_UC->chair_tex) {
            /* MODE+A A/B: route the two face triangles through the shipping
             * textured rasterizer (tex_tri), sampling wall_tex (16x16,
             * column-major) as a throwaway. front_base=DOOR_BASE keeps the
             * sampled indices (0..~4) inside the wood/door palette. UVs map
             * the face corners 0,1,2,3 -> (0,0)(1,0)(1,1)(0,1). Purpose is the
             * per-pixel cost (64-bit UV muls + clamps + uncached z-test), not
             * a faithful chair texture. */
            tex_tri(fb, col_start, col_end, faces[q].depth,
                    (const uint8_t *)wall_tex, WALL_TEX_WIDTH, WALL_TEX_HEIGHT, 1, 0, 0,
                    (uint8_t)DOOR_BASE, (uint8_t)DOOR_BASE, 1,
                    faces[q].sx[0],faces[q].sy[0], 0,       0,
                    faces[q].sx[1],faces[q].sy[1], FX_ONE,  0,
                    faces[q].sx[2],faces[q].sy[2], FX_ONE,  FX_ONE);
            tex_tri(fb, col_start, col_end, faces[q].depth,
                    (const uint8_t *)wall_tex, WALL_TEX_WIDTH, WALL_TEX_HEIGHT, 1, 0, 0,
                    (uint8_t)DOOR_BASE, (uint8_t)DOOR_BASE, 1,
                    faces[q].sx[0],faces[q].sy[0], 0,       0,
                    faces[q].sx[2],faces[q].sy[2], FX_ONE,  FX_ONE,
                    faces[q].sx[3],faces[q].sy[3], 0,       FX_ONE);
            continue;
        }
        chair_tri_fill(faces[q].sx[0],faces[q].sy[0], faces[q].sx[1],faces[q].sy[1],
                       faces[q].sx[2],faces[q].sy[2], c, faces[q].depth, zt, 0, col_start, col_end, fb);
        chair_tri_fill(faces[q].sx[0],faces[q].sy[0], faces[q].sx[2],faces[q].sy[2],
                       faces[q].sx[3],faces[q].sy[3], c, faces[q].depth, zt, 0, col_start, col_end, fb);
    }
}

/* Textured triangle, affine UV in [0,FX_ONE], per-pixel wall z-test and
 * transparency (texel 0 = see-through). Non-transparent texels paint front_base
 * + texel (the figure) when is_front, else the flat cardboard back colour. Used
 * by the world-quad standup below. */
static void tex_tri(uint8_t *fb, int col_start, int col_end, fx_t depth,
        const uint8_t *tex, int tw, int th, int col_major, int skip_z, int shadow,
        uint8_t front_base, uint8_t back_c, int is_front,
        int ax, int ay, fx_t au, fx_t av, int bx, int by, fx_t bu, fx_t bv,
        int cx, int cy, fx_t cu, fx_t cv) {
    int X[3] = {ax,bx,cx}, Y[3] = {ay,by,cy};
    fx_t U[3] = {au,bu,cu}, V[3] = {av,bv,cv};
    for (int i = 0; i < 2; i++) for (int j = i+1; j < 3; j++) if (Y[j] < Y[i]) {
        int t; t=X[i];X[i]=X[j];X[j]=t; t=Y[i];Y[i]=Y[j];Y[j]=t;
        fx_t f; f=U[i];U[i]=U[j];U[j]=f; f=V[i];V[i]=V[j];V[j]=f;
    }
    if (Y[2] == Y[0]) return;
    /* Incremental edges (no per-scanline divide). Long edge spans 0->2; the short
     * edge runs 0->1 then switches to 1->2 at the middle vertex. U,V are texel
     * units so per-pixel sampling is a shift, not a 64-bit mul. */
    int dyL = Y[2] - Y[0];
    fx_t xL = (fx_t)X[0] << FX_SHIFT, dxL = ((fx_t)(X[2]-X[0]) << FX_SHIFT) / dyL;
    fx_t uL = U[0], duL = (U[2]-U[0]) / dyL;
    fx_t vL = V[0], dvL = (V[2]-V[0]) / dyL;
    int dyS0 = Y[1] - Y[0];
    fx_t xS, dxS, uS, duS, vS, dvS;
    if (dyS0 > 0) {
        xS = (fx_t)X[0] << FX_SHIFT; dxS = ((fx_t)(X[1]-X[0]) << FX_SHIFT) / dyS0;
        uS = U[0]; duS = (U[1]-U[0]) / dyS0; vS = V[0]; dvS = (V[1]-V[0]) / dyS0;
    } else {
        int dyS1 = Y[2] - Y[1];
        xS = (fx_t)X[1] << FX_SHIFT; dxS = dyS1>0 ? ((fx_t)(X[2]-X[1]) << FX_SHIFT)/dyS1 : 0;
        uS = U[1]; duS = dyS1>0 ? (U[2]-U[1])/dyS1 : 0; vS = V[1]; dvS = dyS1>0 ? (V[2]-V[1])/dyS1 : 0;
    }
    for (int y = Y[0]; y < Y[2]; y++) {
        if (y == Y[1] && dyS0 > 0) {                       /* switch short edge 1->2 */
            int dyS1 = Y[2] - Y[1];
            xS = (fx_t)X[1] << FX_SHIFT; dxS = dyS1>0 ? ((fx_t)(X[2]-X[1]) << FX_SHIFT)/dyS1 : 0;
            uS = U[1]; duS = dyS1>0 ? (U[2]-U[1])/dyS1 : 0; vS = V[1]; dvS = dyS1>0 ? (V[2]-V[1])/dyS1 : 0;
        }
        if (y >= 0 && y < SCREEN_H) {
            int xa = xL >> FX_SHIFT, xb = xS >> FX_SHIFT;
            fx_t ua = uL, ub = uS, va = vL, vb = vS;
            if (xa > xb) { int t=xa;xa=xb;xb=t; fx_t f=ua;ua=ub;ub=f; f=va;va=vb;vb=f; }
            int span = xb - xa; if (span < 1) span = 1;
            fx_t du = (ub - ua) / span, dv = (vb - va) / span;
            fx_t u = ua, v = va;
            if (xa < col_start) { u += du*(col_start-xa); v += dv*(col_start-xa); xa = col_start; }
            if (xb > col_end - 1) xb = col_end - 1;
            uint8_t *row = fb + (uintptr_t)y * SCREEN_W;
            for (int x = xa; x <= xb; x++, u += du, v += dv) {
                int tx = u >> FX_SHIFT, ty = v >> FX_SHIFT;
                if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
                if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
                uint8_t s = col_major ? tex[tx * th + ty] : tex[ty * tw + tx];
                if (!s) continue;                          /* transparent: no z-read, no write */
                if (!skip_z && depth >= WALL_DIST(x)) continue;
                if (shadow) {                              /* silhouette -> dithered shadow */
                    /* front_base carries the shadow COLOR in shadow mode (it's
                     * unused for texel decode here) — lets the caller flicker it
                     * toward the floor in sync with the casting light. */
                    if ((x ^ y) & 1) row[x] = front_base;
                } else {
                    row[x] = is_front ? (uint8_t)(front_base + s) : back_c;
                }
            }
        }
        xL += dxL; uL += duL; vL += dvL;
        xS += dxS; uS += duS; vS += dvS;
    }
}

/* See the forward decl above draw_chair_3d for why this is not a tex_tri
 * mode. Same edge-walk; the inner loop is smaller: row-major only, opaque
 * (texels 1..4, never 0), paint = lut[texel].
 * noinline and not RAMTEXT on purpose (same call as draw_boxmodel_shadow):
 * LTO folding it into the RAMTEXT draw_standups blob overflowed the ram
 * region, and one panel face a frame does not earn RAM-resident code. */
__attribute__((noinline))
static void tex_tri_lut(uint8_t *fb, int col_start, int col_end, fx_t depth,
        const uint8_t *tex, int tw, int th, int do_z, const uint8_t *lut,
        unsigned noise_seed, const uint8_t *scr, const int *bloom,
        int ax, int ay, fx_t au, fx_t av, int bx, int by, fx_t bu, fx_t bv,
        int cx, int cy, fx_t cu, fx_t cv) {
    int X[3] = {ax,bx,cx}, Y[3] = {ay,by,cy};
    fx_t U[3] = {au,bu,cu}, V[3] = {av,bv,cv};
    for (int i = 0; i < 2; i++) for (int j = i+1; j < 3; j++) if (Y[j] < Y[i]) {
        int t; t=X[i];X[i]=X[j];X[j]=t; t=Y[i];Y[i]=Y[j];Y[j]=t;
        fx_t f; f=U[i];U[i]=U[j];U[j]=f; f=V[i];V[i]=V[j];V[j]=f;
    }
    if (Y[2] == Y[0]) return;
    int dyL = Y[2] - Y[0];
    fx_t xL = (fx_t)X[0] << FX_SHIFT, dxL = ((fx_t)(X[2]-X[0]) << FX_SHIFT) / dyL;
    fx_t uL = U[0], duL = (U[2]-U[0]) / dyL;
    fx_t vL = V[0], dvL = (V[2]-V[0]) / dyL;
    int dyS0 = Y[1] - Y[0];
    fx_t xS, dxS, uS, duS, vS, dvS;
    if (dyS0 > 0) {
        xS = (fx_t)X[0] << FX_SHIFT; dxS = ((fx_t)(X[1]-X[0]) << FX_SHIFT) / dyS0;
        uS = U[0]; duS = (U[1]-U[0]) / dyS0; vS = V[0]; dvS = (V[1]-V[0]) / dyS0;
    } else {
        int dyS1 = Y[2] - Y[1];
        xS = (fx_t)X[1] << FX_SHIFT; dxS = dyS1>0 ? ((fx_t)(X[2]-X[1]) << FX_SHIFT)/dyS1 : 0;
        uS = U[1]; duS = dyS1>0 ? (U[2]-U[1])/dyS1 : 0; vS = V[1]; dvS = dyS1>0 ? (V[2]-V[1])/dyS1 : 0;
    }
    for (int y = Y[0]; y < Y[2]; y++) {
        if (y == Y[1] && dyS0 > 0) {
            int dyS1 = Y[2] - Y[1];
            xS = (fx_t)X[1] << FX_SHIFT; dxS = dyS1>0 ? ((fx_t)(X[2]-X[1]) << FX_SHIFT)/dyS1 : 0;
            uS = U[1]; duS = dyS1>0 ? (U[2]-U[1])/dyS1 : 0; vS = V[1]; dvS = dyS1>0 ? (V[2]-V[1])/dyS1 : 0;
        }
        if (y >= 0 && y < SCREEN_H) {
            int xa = xL >> FX_SHIFT, xb = xS >> FX_SHIFT;
            fx_t ua = uL, ub = uS, va = vL, vb = vS;
            if (xa > xb) { int t=xa;xa=xb;xb=t; fx_t f=ua;ua=ub;ub=f; f=va;va=vb;vb=f; }
            int span = xb - xa; if (span < 1) span = 1;
            fx_t du = (ub - ua) / span, dv = (vb - va) / span;
            fx_t u = ua, v = va;
            if (xa < col_start) { u += du*(col_start-xa); v += dv*(col_start-xa); xa = col_start; }
            if (xb > col_end - 1) xb = col_end - 1;
            uint8_t *row = fb + (uintptr_t)y * SCREEN_W;
            for (int x = xa; x <= xb; x++, u += du, v += dv) {
                if (do_z && depth >= WALL_DIST(x)) continue;
                int tx = u >> FX_SHIFT, ty = v >> FX_SHIFT;
                if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
                if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
                uint8_t s = tex[ty * tw + tx];
                if (s >= 5) {
                    /* CRT bloom: bloom = {elapsed, half-height, unfold
                     * step, glass center ty}. Strike phase paints a white
                     * line; unfold phase gates the band and stretches the
                     * content out of it (one mul per pixel). */
                    if (bloom) {
                        int rel = ty - bloom[3];
                        if (bloom[4] == 2) {   /* the flicker drop: dark */
                            row[x] = lut[5];
                            continue;
                        }
                        if (bloom[4]) {
                            /* POWER-OFF: phosphors losing voltage — a DIM
                             * white band (51 = 50% white, never the
                             * strike's 49) collapses, then a single line
                             * SWEEPS toward the bottom edge, dimmer (52)
                             * as it falls. bloom[3] carries the moving
                             * line position; no frame ever holds still. */
                            row[x] = (rel <= bloom[1] && rel >= -bloom[1])
                                   ? (uint8_t)(bloom[0] >= OFF_FRAMES - 1
                                               ? 52 : 51)
                                   : lut[5];
                            continue;
                        }
                        if (bloom[0] < STRIKE_FRAMES) {
                            row[x] = (rel <= bloom[0] && rel >= -bloom[0])
                                   ? (uint8_t)49 : lut[5];
                            continue;
                        }
                        if (rel > bloom[1] || rel < -bloom[1]) {
                            row[x] = lut[5];
                            continue;
                        }
                        ty = bloom[3] + ((rel * bloom[2]) >> 8);
                        if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
                    }
                    /* Screen. The TELEGRAPH samples by texture coords — the
                     * image must stick to the glass like a picture, unlike
                     * the noise, which hashes SCREEN space (at a distance
                     * several pixels share a texel, and texel-space noise
                     * reads as static behind frosted glass). scr bytes are
                     * final palette indices, emissive — no lut fold. */
                    if (scr) {
                        int ix = tx - tg_bx0, iy = ty - tg_by0;
                        if (ix < 0) ix = 0; else if (ix >= tg_bw) ix = tg_bw - 1;
                        if (iy < 0) iy = 0; else if (iy >= tg_bh) iy = tg_bh - 1;
                        row[x] = scr[iy * tg_bw + ix];
                    } else if (noise_seed)
                        row[x] = lut[10 + STATIC_NOISE(x, y, noise_seed)];
                    else
                        row[x] = (s == 5) ? lut[5] : lut[s - 5];
                } else {
                    row[x] = lut[s];
                }
            }
        }
        xL += dxL; uL += duL; vL += dvL;
        xS += dxS; uS += duS; vS += dvS;
    }
}

/* WORLD-ANCHORED cutout: the neanderthal as a fixed textured quad standing in
 * the world (not a camera-facing billboard). Walk around it and it holds its
 * orientation - front shows the figure, side goes edge-on, back is flat
 * cardboard. Two triangles projected through the live camera. */
static void draw_standup_quad(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb) {
    const sprite_def_t *sd = &sprite_defs[standups[i].kind];
    uint8_t front_base = sd->base, back_c = (uint8_t)(sd->base + 0);
    int focal = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    fx_t bx = standups[i].x, by = standups[i].y;
    uint8_t fa = standups[i].facing_angle;
    /* LOD: swap to the hi-res (column-major) texture up close, like the old
     * billboard, so it's not blocky when you're right in front of it. */
    fx_t bddx = bx - px, bddy = by - py;
    fx_t bdepth = FX_MUL(inv_det, -FX_MUL(planeY, bddx) + FX_MUL(planeX, bddy));
    const uint8_t *tex; int tw, th, col_major;
    if (bdepth < FX(3) && sd->tex_hi) { tex = sd->tex_hi; tw = sd->w_hi; th = sd->h_hi; col_major = 1; }
    else                              { tex = sd->tex;    tw = sd->w;    th = sd->h;    col_major = 0; }
    fx_t pdx = COS_FX((uint8_t)(fa + 64)), pdy = SIN_FX((uint8_t)(fa + 64)); /* width axis */
    /* Height matches the old billboard (2/3 world unit) — sd->world_h is 1.0,
     * which rendered him room-tall and clipping. Half-width from the TEXTURE
     * aspect (1:2, 32x64) so he stays in proportion: width = h*tw/th. */
    fx_t Hh = (fx_t)((FX_ONE * 2) / 3);
    fx_t hw = (fx_t)(((int64_t)Hh * tw) / (2 * th));
    fx_t fwx = COS_FX(fa), fwy = SIN_FX(fa);
    /* Leaning topple: only reached when a wall stopped the fall (fall_angle<64).
     * Tip the quad about its feet, hinging on the WIDTH axis (perp to facing) so
     * the texture polarity is identical to upright — no mirror. The head swings
     * along the stored fall direction (== +/-facing, so the hinge stays rigid),
     * out to STANDUP_FALL_LEN, dropping in height. is_front = face_up. */
    int down = standup_down[i];
    fx_t hbx = bx, hby = by;      /* head-base XY (== feet when upright) */
    fx_t head_h = Hh;
    int is_front;
    if (down) {
        if (bdepth < FX(0.2)) return;   /* base behind/at camera: don't fold it forward */
        int prog = standup_fall_prog[i];
        if (prog > STANDUP_FALL_MAX) prog = STANDUP_FALL_MAX;
        uint8_t theta = (uint8_t)(((int)64 * prog) / STANDUP_FALL_MAX);
        is_front = standup_fall_face[i];
        fx_t fdx = COS_FX(standup_fall_dir[i]), fdy = SIN_FX(standup_fall_dir[i]);
        /* Pivot length eases from the STANDING height (Hh) upright to the lying
         * body length (STANDUP_FALL_LEN) as he tips. Was FALL_LEN flat, so a
         * shallow lean set head_h = FALL_LEN*cos(0) ≈ 0.9 > Hh (0.667) and popped
         * him taller instead of tilting — visible on any short-reach lean (a
         * partition/wall right beside him). */
        fx_t st = SIN_FX(theta), ct = COS_FX(theta);
        fx_t plen = Hh + FX_MUL(STANDUP_FALL_LEN - Hh, st);
        fx_t hf = FX_MUL(plen, st);   /* head reach */
        head_h  = FX_MUL(plen, ct);   /* head height */
        hbx = bx + FX_MUL(fdx, hf);
        hby = by + FX_MUL(fdy, hf);
    } else {
        /* Front when the player is on the facing side: dot(standup-player,fwd)<0. */
        is_front = (FX_MUL(bx - px, fwx) + FX_MUL(by - py, fwy)) < 0;
    }

    fx_t cwx[4] = { bx  - FX_MUL(pdx, hw), bx  + FX_MUL(pdx, hw),
                    hbx + FX_MUL(pdx, hw), hbx - FX_MUL(pdx, hw) };
    fx_t cwy[4] = { by  - FX_MUL(pdy, hw), by  + FX_MUL(pdy, hw),
                    hby + FX_MUL(pdy, hw), hby - FX_MUL(pdy, hw) };
    fx_t cwh[4] = { 0, 0, head_h, head_h };            /* BL BR feet, TR TL head */
    int csx[4], csy[4]; fx_t cdep[4];
    for (int c = 0; c < 4; c++) {
        fx_t ddx = cwx[c] - px, ddy = cwy[c] - py;
        fx_t depth = FX_MUL(inv_det, -FX_MUL(planeY, ddx) + FX_MUL(planeX, ddy));
        if (depth < FX(0.2)) {
            if (!down) return;                         /* standing: collision keeps you out */
            depth = FX(0.2);                           /* leaning/close: clamp, don't vanish */
        }
        cdep[c] = depth;
        fx_t inv_d = fx_div_hw(FX_ONE, depth);
        fx_t lat = FX_MUL(inv_det, FX_MUL(dirY, ddx) - FX_MUL(dirX, ddy));
        csx[c] = (SCREEN_W >> 1) + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(lat, inv_d)) >> FX_SHIFT);
        int floor_y = horizon_y + (int)(((int64_t)focal * inv_d) >> FX_SHIFT);
        csy[c] = floor_y - (int)((FX_MUL(cwh[c], inv_d) * SCREEN_H) >> FX_SHIFT);
    }
    fx_t depth = cdep[0];
    /* Skip the per-pixel wall depth-test when the whole quad is in front of the
     * nearest wall across its column span (the common open-hall case) — read
     * WALL_DIST once per column here instead of once per textured pixel. */
    int skip_z = 1;
    {
        fx_t maxd = cdep[0];
        for (int c = 1; c < 4; c++) if (cdep[c] > maxd) maxd = cdep[c];
        int lo = csx[0], hi = csx[0];
        for (int c = 1; c < 4; c++) { if (csx[c] < lo) lo = csx[c]; if (csx[c] > hi) hi = csx[c]; }
        if (lo < col_start) lo = col_start;
        if (hi > col_end - 1) hi = col_end - 1;
        for (int x = lo; x <= hi; x++) if (WALL_DIST(x) <= maxd) { skip_z = 0; break; }
    }
    /* UVs in TEXEL units (0..tw, 0..th) << FX so tex_tri's per-pixel is a shift,
     * not a mul. BL(0,th) BR(tw,th) TR(tw,0) TL(0,0) — v=th feet, v=0 head. */
    const fx_t QU[4] = { 0, (fx_t)tw << FX_SHIFT, (fx_t)tw << FX_SHIFT, 0 };
    const fx_t QV[4] = { (fx_t)th << FX_SHIFT, (fx_t)th << FX_SHIFT, 0, 0 };
    tex_tri(fb, col_start, col_end, depth, tex, tw, th, col_major, skip_z, 0, front_base, back_c, is_front,
            csx[0],csy[0],QU[0],QV[0], csx[1],csy[1],QU[1],QV[1], csx[2],csy[2],QU[2],QV[2]);
    tex_tri(fb, col_start, col_end, depth, tex, tw, th, col_major, skip_z, 0, front_base, back_c, is_front,
            csx[0],csy[0],QU[0],QV[0], csx[2],csy[2],QU[2],QV[2], csx[3],csy[3],QU[3],QV[3]);
}

/* Projected-silhouette shadow: the figure's own texture laid FLAT on the floor
 * (a second polygon, feet at the base, head STANDUP_SHADOW_LEN away along the
 * world-fixed light direction), rendered in tex_tri shadow-mode so its opaque
 * texels become the dithered shadow colour — a caveman-SHAPED shadow, not a
 * blob, all in software off his existing bitmap. Drawn before the figure so he
 * overpaints its near edge. Skipped once he's settled flat (body covers it). */
/* Unit direction pointing AWAY from the ceiling light nearest to (bx,by);
 * falls back to (fallx,fally) when no lights exist or the caster stands
 * directly under one. Shared by every floor-shadow caster. */
static void nearest_light_dir(fx_t bx, fx_t by, fx_t fallx, fx_t fally,
                              fx_t *odx, fx_t *ody) {
    *odx = fallx; *ody = fally;
    int32_t best_d2 = 0x7FFFFFFF; int best = -1;
    for (int L = 0; L < NUM_LIGHTS; L++) {
        int32_t dxc = (int32_t)((bx - lights[L].x) >> 8);
        int32_t dyc = (int32_t)((by - lights[L].y) >> 8);
        int32_t d2  = dxc * dxc + dyc * dyc;
        if (d2 < best_d2) { best_d2 = d2; best = L; }
    }
    if (best >= 0) {
        fx_t dx = bx - lights[best].x, dy = by - lights[best].y;
        fx_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        fx_t mx = adx > ady ? adx : ady, mn = adx > ady ? ady : adx;
        fx_t mag = (mx - (mx >> 5)) + ((mn >> 1) - (mn >> 3));   /* ~sqrt */
        if (mag > FX(0.06)) {
            fx_t inv = fx_div_hw(FX_ONE, mag);
            *odx = FX_MUL(dx, inv); *ody = FX_MUL(dy, inv);
        }
    }
}

/* Light-FIELD sample at (bx,by): sum the away-vectors of every ceiling light
 * within 4 cells, weighted by distance. Winner-take-all nearest-light was
 * draconian physics: between two fixtures the real contributions CANCEL
 * (leaving only a contact pool under the object), while nearest-light
 * flipped direction at every midpoint and pointed neighbouring chairs'
 * casts AT each other. The net vector's magnitude classifies the shadow
 * KIND: 0 balanced/under-a-light (contact only), 1 moderate imbalance,
 * 2 one-sided — indexes the SHORT/MED/LONG stencil variants. Direction is
 * the normalized net vector (fallback dir for the balanced case, where the
 * near-symmetric SHORT stencil makes it cosmetic). */
static int light_field_dir(fx_t bx, fx_t by, fx_t fallx, fx_t fally,
                           fx_t *odx, fx_t *ody, int *dom) {
    int32_t vx = 0, vy = 0, best_d2 = 0x7FFFFFFF; int best = -1;
    for (int L = 0; L < NUM_LIGHTS; L++) {
        int32_t dxc = (int32_t)((bx - lights[L].x) >> 8);   /* 8.8 cells */
        int32_t dyc = (int32_t)((by - lights[L].y) >> 8);
        int32_t d2 = dxc * dxc + dyc * dyc;
        if (d2 == 0 || d2 >= 1048576) continue;             /* >4 cells: negligible */
        if (d2 < best_d2) { best_d2 = d2; best = L; }        /* dominant (nearest) caster */
        vx += (dxc << 14) / d2;                             /* ~cos/d falloff */
        vy += (dyc << 14) / d2;
    }
    if (dom) *dom = best;
    int32_t ax = vx < 0 ? -vx : vx, ay = vy < 0 ? -vy : vy;
    int32_t mx2 = ax > ay ? ax : ay, mn2 = ax > ay ? ay : ax;
    int32_t mag = (mx2 - (mx2 >> 5)) + ((mn2 >> 1) - (mn2 >> 3));
    if (mag < 24) { *odx = fallx; *ody = fally; return 0; } /* balanced: contact */
    *odx = (fx_t)(((int64_t)vx << FX_SHIFT) / mag);
    *ody = (fx_t)(((int64_t)vy << FX_SHIFT) / mag);
    return (mag < 80) ? 1 : 2;
}

/* Chair floor shadow: the baked plan-silhouette stencil (chair_shadow_tex.h —
 * the box model sheared along the cast axis, v=0 row IS the near feet line)
 * placed as a floor quad CONSTRUCTED FROM THE FEET: near edge anchor_fx
 * toward the light from the chair centre (= the light-side feet line),
 * extending len_fx away, width_fx laterally. Leg contact is geometric —
 * there are no tuned constants here; all three numbers come from the bake. */
static void draw_chair_shadow(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb) {
    fx_t bx = standups[i].x, by = standups[i].y;
    int focal = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    uint8_t fa = standups[i].facing_angle;
    fx_t fc = COS_FX((uint8_t)(fa + 64)), fs = -SIN_FX((uint8_t)(fa + 64));
    fx_t sdx, sdy;
    /* Balanced-field fallback casts toward the chair's BACK (deterministic per
     * chair) — the old camera-based fallback would make the yaw pick below
     * swim as the player circles a chair sitting under a light. */
    int dom = -1;
    int kind = light_field_dir(bx, by, -COS_FX(fa), -SIN_FX(fa), &sdx, &sdy, &dom);
    /* Shadow FLICKERS with its casting light: roll the dominant (nearest) light's
     * flicker with the exact seed draw_lights uses, and when that fixture strobes
     * off/dim, lighten the shadow toward the floor so it flickers away in sync.
     * Gated on LIGHTING_FLICKER; free (one roll, rides the shadow setup). */
    uint8_t shadow_c = STANDUP_SHADOW_COLOR;
    if ((SHARED_UC->lighting_flags & LIGHTING_FLICKER) && dom >= 0) {
        uint32_t r = SHARED_UC->frame_count * 1103515245u + (uint32_t)dom * 12347u;
        int roll = (int)((r >> 24) & 0x1F);
        if      (roll < 2) shadow_c = (uint8_t)(STANDUP_SHADOW_COLOR - 3);  /* off: fade to floor */
        else if (roll < 5) shadow_c = (uint8_t)(STANDUP_SHADOW_COLOR - 1);  /* dim */
        if (shadow_c < FLOOR_BASE) shadow_c = FLOOR_BASE;
    }
    /* Yaw sector: the cast direction expressed against the chair's OWN facing
     * picks WHICH silhouette — light on the back throws the slat tongue, light
     * on the side throws the thin profile. Same argmax-dot picker as the
     * directional billboards; sector tables (angle/yaw/mirror) come from the
     * bake. Each candidate angle a is the cast in MODEL frame (-sin a, cos a),
     * pushed through the same fc/fs rotation draw_chair_3d uses. */
    int best = 0; fx_t bestd = 0; int first = 1;
    for (int k = 0; k < CHAIR_SHADOW_SECTORS; k++) {
        uint8_t a = chair_shadow_sect_a[k];
        fx_t ms = SIN_FX(a), mc = COS_FX(a);
        fx_t wdx = FX_MUL(-ms, fc) + FX_MUL(mc, fs);
        fx_t wdy = FX_MUL(ms, fs) + FX_MUL(mc, fc);
        fx_t d = FX_MUL(sdx, wdx) + FX_MUL(sdy, wdy);
        if (first || d > bestd) { best = k; bestd = d; first = 0; }
    }
    const chair_shadow_t *cs =
        &chair_shadows[chair_shadow_sect_yaw[best]][kind]; /* [yaw][SHORT/MED/LONG] */
    int mir = chair_shadow_sect_mir[best];
    /* Lateral axis is CW-90 of the cast — the bake's u axis. (CCW here mirrored
     * every stencil; invisible on the symmetric y0 shape, sim-caught on the
     * y45/y135 diagonals: IoU 0.58 CCW vs 0.97 CW against the ground-truth
     * sheared footprint.) */
    fx_t lpx = sdy, lpy = -sdx;
    fx_t hw = cs->width_fx >> 1;
    fx_t nx = bx - FX_MUL(sdx, cs->anchor_fx);         /* near feet line centre */
    fx_t ny = by - FX_MUL(sdy, cs->anchor_fx);
    fx_t fxp = nx + FX_MUL(sdx, cs->len_fx);
    fx_t fyp = ny + FX_MUL(sdy, cs->len_fx);
    fx_t cwx[4] = { nx - FX_MUL(lpx, hw), nx + FX_MUL(lpx, hw),
                    fxp + FX_MUL(lpx, hw), fxp - FX_MUL(lpx, hw) };
    fx_t cwy[4] = { ny - FX_MUL(lpy, hw), ny + FX_MUL(lpy, hw),
                    fyp + FX_MUL(lpy, hw), fyp - FX_MUL(lpy, hw) };
    int csx[4], csy[4]; fx_t cdep[4];
    for (int c = 0; c < 4; c++) {
        fx_t ddx = cwx[c] - px, ddy = cwy[c] - py;
        fx_t depth = FX_MUL(inv_det, -FX_MUL(planeY, ddx) + FX_MUL(planeX, ddy));
        if (depth < FX(0.25)) depth = FX(0.25);
        cdep[c] = depth;
        fx_t inv = fx_div_hw(FX_ONE, depth);
        fx_t lat = FX_MUL(inv_det, FX_MUL(dirY, ddx) - FX_MUL(dirX, ddy));
        csx[c] = (SCREEN_W >> 1) + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(lat, inv)) >> FX_SHIFT);
        csy[c] = horizon_y + (int)(((int64_t)focal * inv) >> FX_SHIFT);   /* floor */
    }
    fx_t depth = cdep[0];
    int tw = cs->w, th = cs->h;
    /* Mirror sectors reuse the direct yaw's stencil u-flipped (the chair is
     * left-right symmetric, same trick as the billboard set). */
    const fx_t UA = mir ? ((fx_t)tw << FX_SHIFT) : 0;
    const fx_t UB = mir ? 0 : ((fx_t)tw << FX_SHIFT);
    const fx_t QU[4] = { UA, UB, UB, UA };
    const fx_t QV[4] = { 0, 0, (fx_t)th << FX_SHIFT, (fx_t)th << FX_SHIFT };
    tex_tri(fb, col_start, col_end, depth, cs->tex, tw, th, 0, 0, 1, shadow_c, 0, 0,
            csx[0],csy[0],QU[0],QV[0], csx[1],csy[1],QU[1],QV[1], csx[2],csy[2],QU[2],QV[2]);
    tex_tri(fb, col_start, col_end, depth, cs->tex, tw, th, 0, 0, 1, shadow_c, 0, 0,
            csx[0],csy[0],QU[0],QV[0], csx[2],csy[2],QU[2],QV[2], csx[3],csy[3],QU[3],QV[3]);
}

/* Generic box-model floor shadow: the model's OWN footprint, taken from its box
 * list, rotated by its facing and slid a little along the cast direction.
 *
 * The chair has a baked plan-silhouette; nothing else does, and stamping the
 * chair's stencil under a desk would put a chair-shaped shadow on the floor. A
 * footprint quad needs no bake, no ROM, and no per-asset art, so every future
 * GLB import gets a shadow of the right SHAPE for free — it just can't show
 * detail between the legs. Cheap enough to be unconditional: 4 corners, two
 * dithered triangles.
 *
 * noinline and not RAMTEXT on purpose — draw_standups is RAMTEXT and this is
 * once-per-object setup, not per-pixel work. */
__attribute__((noinline))
static void draw_boxmodel_shadow(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb) {
    fx_t bx = standups[i].x, by = standups[i].y;
    int focal = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    uint8_t fa = standups[i].facing_angle;
    fx_t fc = COS_FX((uint8_t)(fa + 64)), fs = -SIN_FX((uint8_t)(fa + 64));
    fx_t hx, hz;
    boxmodel_footprint_bm(boxmodel_for_standup(i),
                          world_h_for_standup(i), &hx, &hz);
    /* Same cast direction and flicker seed the chair shadow uses, so a desk and
     * a chair under one fixture agree on where the light is and pulse together. */
    fx_t sdx, sdy; int dom = -1;
    light_field_dir(bx, by, -COS_FX(fa), -SIN_FX(fa), &sdx, &sdy, &dom);
    uint8_t shadow_c = STANDUP_SHADOW_COLOR;
    if ((SHARED_UC->lighting_flags & LIGHTING_FLICKER) && dom >= 0) {
        uint32_t r = SHARED_UC->frame_count * 1103515245u + (uint32_t)dom * 12347u;
        int roll = (int)((r >> 24) & 0x1F);
        if      (roll < 2) shadow_c = (uint8_t)(STANDUP_SHADOW_COLOR - 3);
        else if (roll < 5) shadow_c = (uint8_t)(STANDUP_SHADOW_COLOR - 1);
        if (shadow_c < FLOOR_BASE) shadow_c = FLOOR_BASE;
    }
    static const int8_t SGN[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    const fx_t OFF = FX(0.05);                 /* slide off the contact line */
    int csx[4], csy[4]; fx_t cdep[4];
    for (int c = 0; c < 4; c++) {
        fx_t mx = SGN[c][0] > 0 ? hx : -hx;
        fx_t mz = SGN[c][1] > 0 ? hz : -hz;
        fx_t rx =  FX_MUL(mx, fc) + FX_MUL(mz, fs);   /* rotate by facing */
        fx_t rz = -FX_MUL(mx, fs) + FX_MUL(mz, fc);
        fx_t wx = bx + rx + FX_MUL(sdx, OFF);
        fx_t wy = by + rz + FX_MUL(sdy, OFF);
        fx_t ddx = wx - px, ddy = wy - py;
        fx_t depth = FX_MUL(inv_det, -FX_MUL(planeY, ddx) + FX_MUL(planeX, ddy));
        if (depth < FX(0.25)) depth = FX(0.25);
        cdep[c] = depth;
        fx_t inv = fx_div_hw(FX_ONE, depth);
        fx_t lat = FX_MUL(inv_det, FX_MUL(dirY, ddx) - FX_MUL(dirX, ddy));
        csx[c] = (SCREEN_W >> 1)
               + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(lat, inv)) >> FX_SHIFT);
        csy[c] = horizon_y + (int)(((int64_t)focal * inv) >> FX_SHIFT);   /* floor */
    }
    chair_tri_fill(csx[0],csy[0], csx[1],csy[1], csx[2],csy[2],
                   shadow_c, cdep[0], 1, 1, col_start, col_end, fb);
    chair_tri_fill(csx[0],csy[0], csx[2],csy[2], csx[3],csy[3],
                   shadow_c, cdep[0], 1, 1, col_start, col_end, fb);
}

static void draw_standup_shadow(int i, int col_start, int col_end,
        fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY,
        fx_t inv_det, int horizon_y, uint8_t *fb) {
    /* Only vanish once he's SETTLED flat (body covers it). While he tips, the
     * shadow stays and shrinks with him (below). */
    if (standup_down[i] && standup_fall_prog[i] >= STANDUP_FALL_MAX) return;
    const sprite_def_t *sd = &sprite_defs[standups[i].kind];
    const uint8_t *tex = sd->tex; int tw = sd->w, th = sd->h;     /* lo-res silhouette is plenty */
    int focal = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    fx_t bx = standups[i].x, by = standups[i].y;
    uint8_t fa = standups[i].facing_angle;
    fx_t pdx = COS_FX((uint8_t)(fa + 64)), pdy = SIN_FX((uint8_t)(fa + 64));  /* width axis */
    fx_t Hh = (fx_t)((FX_ONE * 2) / 3);
    fx_t hw = (fx_t)(((int64_t)Hh * tw) / (2 * th));
    /* WORLD-ANCHORED cast: the shadow stretches directly AWAY from the nearest
     * ceiling light, so it stays put as you circle him. (This used to cast away
     * from the CAMERA (-dir), which made the shadow swing to follow you —
     * shadows don't do that.) The nearest-light search + unit direction are
     * computed just below. */
    /* As he tips, his height above the floor drops as cos(tilt), so the cast
     * shortens with him — the subtle topple animation (full standing, ~0 flat). */
    uint8_t theta = 0;
    if (standup_down[i]) {
        int prog = standup_fall_prog[i];
        if (prog > STANDUP_FALL_MAX) prog = STANDUP_FALL_MAX;
        theta = (uint8_t)(((int)64 * prog) / STANDUP_FALL_MAX);
    }
    fx_t len = FX_MUL(STANDUP_SHADOW_LEN, COS_FX(theta));
    /* Find the closest ceiling light, then point the shadow feet->away-from-it.
     * >>8 on the deltas keeps the squared distance inside int32 (raw world
     * deltas overflow a 32-bit square). Magnitude via alpha-max-beta-min so we
     * skip a sqrt — a few % length error on a dithered floor blob is invisible.
     * Degenerate cases (no lights, or standing right under one) keep the old
     * camera-relative cast. Cheap: a few hundred int32 checks in the serial
     * sprite tail. KNOWN SIMPLIFICATION: nearest by raw distance, ignores walls. */
    fx_t sdx, sdy;
    nearest_light_dir(bx, by, -dirX, -dirY, &sdx, &sdy);
    /* Feet-edge tucked slightly UNDER him, back toward the light (-sd) — a
     * contact shadow sits a hair beneath the object, pulling both spread feet
     * into the top of the shadow to close the foot gap. Light-anchored like the
     * cast, so the tuck can't reintroduce a camera-relative wobble. */
    fx_t abx = bx - FX_MUL(sdx, STANDUP_SHADOW_TUCK);
    fx_t aby = by - FX_MUL(sdy, STANDUP_SHADOW_TUCK);
    fx_t hbx = abx + FX_MUL(sdx, len);                 /* silhouette head-end on the floor */
    fx_t hby = aby + FX_MUL(sdy, len);
    fx_t cwx[4] = { abx - FX_MUL(pdx, hw), abx + FX_MUL(pdx, hw),
                    hbx + FX_MUL(pdx, hw), hbx - FX_MUL(pdx, hw) };
    fx_t cwy[4] = { aby - FX_MUL(pdy, hw), aby + FX_MUL(pdy, hw),
                    hby + FX_MUL(pdy, hw), hby - FX_MUL(pdy, hw) };
    int csx[4], csy[4]; fx_t cdep[4];
    for (int c = 0; c < 4; c++) {
        fx_t ddx = cwx[c] - px, ddy = cwy[c] - py;
        fx_t depth = FX_MUL(inv_det, -FX_MUL(planeY, ddx) + FX_MUL(planeX, ddy));
        /* The head-end reaches toward the camera and crosses the near plane when
         * you crawl in close — clamp it to the near plane instead of dropping the
         * whole shadow (which made it blink out mid-approach). */
        if (depth < FX(0.25)) depth = FX(0.25);
        cdep[c] = depth;
        fx_t inv_d = fx_div_hw(FX_ONE, depth);
        fx_t lat = FX_MUL(inv_det, FX_MUL(dirY, ddx) - FX_MUL(dirX, ddy));
        csx[c] = (SCREEN_W >> 1) + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(lat, inv_d)) >> FX_SHIFT);
        csy[c] = horizon_y + (int)(((int64_t)focal * inv_d) >> FX_SHIFT);   /* floor (height 0) */
    }
    fx_t depth = cdep[0];
    const fx_t QU[4] = { 0, (fx_t)tw << FX_SHIFT, (fx_t)tw << FX_SHIFT, 0 };  /* feet v=th, head v=0 */
    const fx_t QV[4] = { (fx_t)th << FX_SHIFT, (fx_t)th << FX_SHIFT, 0, 0 };
    tex_tri(fb, col_start, col_end, depth, tex, tw, th, 0, 0, 1, STANDUP_SHADOW_COLOR, 0, 0,
            csx[0],csy[0],QU[0],QV[0], csx[1],csy[1],QU[1],QV[1], csx[2],csy[2],QU[2],QV[2]);
    tex_tri(fb, col_start, col_end, depth, tex, tw, th, 0, 0, 1, STANDUP_SHADOW_COLOR, 0, 0,
            csx[0],csy[0],QU[0],QV[0], csx[2],csy[2],QU[2],QV[2], csx[3],csy[3],QU[3],QV[3]);
}

/* Per-sprite value map: fold front/back/silhouette AND (for box-model
 * billboards) distance fog into a tiny LUT so the inner loop is one table
 * lookup. Split out of draw_standups and noinline for the same RAM reason as
 * draw_boxmodel_shadow: once-per-sprite setup (an int64 fog divide and a
 * 4-way branch loop) that was inlined into the RAMTEXT blob, and the PVM
 * import's per-kind ramp lookup tipped the ram region over. */
__attribute__((noinline))
static void build_standup_vmap(int i, fx_t transformY, const dirset_t *fds,
        const sprite_def_t *sd, int is_silhouette, uint8_t silhouette_color,
        int is_front, uint8_t back_color, uint8_t *vmap) {
    int is_chair = (fds != 0);
    /* Distance + dark-room fog, shared by the chair AND the neanderthal
     * (front figure). Baked into the value-LUT so the inner loop stays a
     * single table read -- fog is effectively free per pixel. */
    int fog = 0;
    {
        fx_t fd = transformY - FX(2);
        if (fd > 0) fog = (int)(((int64_t)fd * 5) / (FOG_RAMP_DIST - FX(2)));
        /* Dark-room: full fog -- a silhouette in the gloom, like the
         * walls around it (mirrors draw_chair_3d's dark clamp). */
        if (cell_is_dark(standups[i].x, standups[i].y)) fog = 5;
    }
    /* Per-kind ramp: chair and desk pin the chair's wood (their
     * boxmodels[] rows carry CHAIR_BASE — the shipped look); an
     * import with its own ramp (PVM gray) decodes into that instead.
     * Must match draw_chair_3d's flat fill or the LOD swap pops. */
    uint8_t rbase = CHAIR_BASE;
    const boxmodel_t *bmr = is_chair ? boxmodel_for_kind(standups[i].kind) : 0;
    if (bmr) rbase = bmr->base;
    for (int k = 1; k < 8; k++) {
        if (is_chair) {
            /* Dark ramp is 4 deep; sprite texels run 1..5, so the
             * brightest collapses into the ramp top. */
            int sh = (k - 1) - fog;
            if (sh < 0) sh = 0; else if (sh > 3) sh = 3;
            vmap[k] = (uint8_t)(rbase + sh);
        } else if (is_silhouette) {
            vmap[k] = silhouette_color;
        } else if (is_front) {
            /* Neanderthal parity: fog the figure toward its dark end as
             * it recedes (shades run 1..7) so it stops floating as a
             * bright cutout in the fog. Hands off to the silhouette at
             * far LOD. If this reads BRIGHTER with distance the ramp is
             * reversed -- flip to (sd->base + (8 - sh)).
             *
             * SPRITE_F_ARTPAL sits this out: a community sprite's 7
             * entries are the artist's COLOURS, so walking them swaps
             * hues rather than dimming them (a red stop sign would fog
             * toward whatever its darkest entry happens to be). Keep
             * the art's own colours until real gloom, then let the
             * dark end carry it. */
            int sh;
            if (sd->flags & SPRITE_F_ARTPAL)
                sh = (fog >= 5) ? 1 : k;
            else
                sh = k - fog;
            if (sh < 1) sh = 1; else if (sh > 7) sh = 7;
            vmap[k] = (uint8_t)(sd->base + sh);
        } else {
            vmap[k] = back_color;
        }
    }
    /* Texels 5..9 are the SCREEN of a panel-textured model (only its bake
     * emits them — flat box faces top out at 4): 5 = glass core, 6..9 = the
     * bezel-opening rim carrying albedo v-5. Too far for per-pixel static, so
     * powered ON the whole opening rides one value — a two-frame shimmer on
     * the RAW ramp (emissive, no fog/dark walk), phase-offset by i so a row
     * of sets never blinks in unison. OFF: core goes dark glass, the rim
     * keeps its fogged albedo — the corner shading that reads as curved
     * glass. The caller's vmap must span 10 entries. */
    /* MULTI-RAMP composite: each extra slot owns four texel values at
     * 10+(s*4), fogged on its OWN base exactly like slot 0 above. Without
     * this the desk and the console decode through the monitor's gray and
     * the far LOD is a gray desk — the bake carries the materials, but only
     * this turns them back into colours. */
    if (fds && fds->vbase) {
        for (int s = 0; fds->vbase[s]; s++)
            for (int k = 0; k < 4; k++) {
                int sh = k - fog;
                if (sh < 0) sh = 0; else if (sh > 3) sh = 3;
                vmap[10 + s * 4 + k] = (uint8_t)(fds->vbase[s] + sh);
            }
    }
    if (bmr && bmr->ftex) {
        if (standup_power[i]) {
            int sh = 1 + (((SHARED_UC->frame_count >> 1) + (unsigned)i) & 2);
            for (int k = 5; k <= 9; k++) vmap[k] = (uint8_t)(rbase + sh);
        } else {
            vmap[5] = (uint8_t)(rbase + 0);
            for (int k = 6; k <= 9; k++) {
                int sh = (k - 5 - 1) - fog;
                if (sh < 0) sh = 0;
                vmap[k] = (uint8_t)(rbase + sh);
            }
        }
    }
}

RAMTEXT static void draw_standups(int col_start, int col_end) {
    /* Self-contained for the dual-CPU split: read the player snapshot and
     * derive the camera basis locally (same as the ceiling/carpet passes) so
     * the secondary can draw its column half [col_start,col_end) coherently
     * without the primary threading dir/plane through shared memory. */
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    uint8_t *fb = fb_pixels();
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = fx_div_hw(FX_ONE, det);   /* det == -0.66 const; bounded */
    /* Standup feet sit on the floor → anchor floor_y to the shifted
     * horizon so they slide with the wall/carpet when the camera pitches. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;

    /* Draw FAR -> NEAR so a nearer sprite paints over a farther one: standups
     * z-test only against walls, not each other, so array order let the chair
     * (later in the list) show through a nearer neanderthal. Insertion-sort
     * indices by squared distance, descending (n<=24, once per half). */
    int order[MAX_STANDUPS];
    int64_t d2[MAX_STANDUPS];
    int on = NUM_STANDUPS;
    for (int i = 0; i < on; i++) {
        fx_t ddx = standups[i].x - px, ddy = standups[i].y - py;
        d2[i] = (int64_t)ddx * ddx + (int64_t)ddy * ddy;
        order[i] = i;
    }
    for (int a = 1; a < on; a++) {
        int io = order[a]; int64_t da = d2[io]; int b = a;
        while (b > 0 && d2[order[b - 1]] < da) { order[b] = order[b - 1]; b--; }
        order[b] = io;
    }

    /* Chair render guard: a true-3D chair is ~2000 ticks, and the frame is
     * vblank-locked, so an unbounded count craters the scene. Render only the
     * nearest CHAIR_RENDER_MAX chairs within CHAIR_CULL_D2 — the map cap can be
     * generous because the ENGINE never draws more than a few at once. (order[]
     * is far->near, so walking it in reverse hits the nearest first.) */
    #define CHAIR_RENDER_MAX 3
    static const int64_t CHAIR_CULL_D2 = (int64_t)FX(4) * FX(4);  /* 4 cells: 3D within reach, billboard beyond (swap is colour-continuous). Bounded by CHAIR_RENDER_MAX, so at most 3 chairs ever pay the 3D cost. */
    uint8_t chair_render[MAX_STANDUPS] = {0};
    {
        int seen = 0;
        for (int oi2 = on - 1; oi2 >= 0 && seen < CHAIR_RENDER_MAX; oi2--) {
            int j = order[oi2];
            if (!boxmodel_for_kind(standups[j].kind)) continue;
            if (d2[j] > CHAIR_CULL_D2) continue;
            chair_render[j] = 1; seen++;
        }
    }

    /* Shadow cap: like the 3D render guard, only the nearest few chairs within
     * shadow range cast — a 16-chair map ran light_field_dir (a loop over every
     * light) for every chair in 7 cells, most of them off-screen. Measured ~3000
     * ticks / +2fps on the test backrooms map. order[] is far->near. */
    #define CHAIR_SHADOW_MAX 4
    static const int64_t CHAIR_SHADOW_D2 = (int64_t)FX(7) * FX(7);
    uint8_t chair_shadow_ok[MAX_STANDUPS] = {0};
    {
        int seen = 0;
        for (int oi2 = on - 1; oi2 >= 0 && seen < CHAIR_SHADOW_MAX; oi2--) {
            int j = order[oi2];
            /* Any BOX MODEL casts. The chair uses its baked silhouette; every
             * other import gets the generic footprint shadow, which is derived
             * from its own box list and so can never be the wrong shape. */
            if (!boxmodel_for_kind(standups[j].kind)) continue;
            if (d2[j] >= CHAIR_SHADOW_D2) continue;
            chair_shadow_ok[j] = 1; seen++;
        }
    }

    /* Isolate just the chair fill for the MODE+A textured/flat A/B. Only the
     * primary half (col_start==0) commits prof_pass_chair to avoid the two
     * SH-2s racing on the global; the toggle doesn't move geometry, so the
     * primary's column share of the chair is identical in both arms and the
     * delta is a clean per-pixel texturing cost. */
    uint16_t chair_ticks = 0;
    for (int oi = 0; oi < on; oi++) {
        int i = order[oi];
        const boxmodel_t *bm = boxmodel_for_standup(i);
        if (bm) {                                     /* near = true 3D, far = billboard */
            /* Floor shadow FIRST so the chair overpaints its near edge — cast
             * in BOTH tiers from the same fixed 3/4 silhouette, so the shadow
             * is identical across the 4-cell LOD swap (no shadow pop). Gated:
             * no light reaches a dark-room chair (no shadow, physically and
             * because the dither would LIGHTEN the darkened floor), and past
             * ~7 cells the cast is sub-pixel fog-food not worth the quad. */
            /* Shadow only when it could possibly be seen: not toggled off,
             * within 7 cells, lit cell, and not well BEHIND the camera —
             * a cast is <=~0.6 cells long, so a chair more than a cell
             * behind the view plane cannot reach the frame. Kills the
             * quads (and their uncached per-pixel z-reads) for the half
             * of the map at your back. */
            fx_t scx = standups[i].x - px, scy = standups[i].y - py;
            fx_t cd = FX_MUL(scx, dirX) + FX_MUL(scy, dirY);
            if (!SHARED_UC->shadows_off
                && chair_shadow_ok[i]              /* nearest-few, within 7 cells */
                && cd > -FX(1)
                && !cell_is_dark(standups[i].x, standups[i].y)) {
                /* FRUSTUM CULL: skip the whole shadow (setup included — the
                 * per-light light_field_dir loop) when the chair projects well
                 * outside the viewport. A half-screen margin covers the shadow's
                 * lateral floor spread. This is the "off-screen = free" the fill
                 * already had, now applied to the shadow SETUP too. */
                fx_t stX = FX_MUL(inv_det, FX_MUL(dirY, scx) - FX_MUL(dirX, scy));
                fx_t stY = FX_MUL(inv_det, FX_MUL(-planeY, scx) + FX_MUL(planeX, scy));
                if (stY >= FX(0.2)) {
                    int sX = (SCREEN_W >> 1)
                           + (int)(((int32_t)(SCREEN_W >> 1) * FX_DIV(stX, stY)) >> FX_SHIFT);
                    if (sX > -(SCREEN_W >> 1) && sX < SCREEN_W + (SCREEN_W >> 1)) {
                        /* Chair has a baked silhouette; every other box model
                         * casts its own footprint (see draw_boxmodel_shadow). */
                        if (standups[i].kind == CHAIR_SPRITE_KIND)
                            draw_chair_shadow(i, col_start, col_end, px, py, dirX, dirY,
                                              planeX, planeY, inv_det, horizon_y, fb);
                        else
                            draw_boxmodel_shadow(i, col_start, col_end, px, py, dirX, dirY,
                                                 planeX, planeY, inv_det, horizon_y, fb);
                    }
                }
            }
            if (chair_render[i]) {
                uint16_t _cf0 = prof_frt_read();
                draw_chair_3d(i, col_start, col_end, px, py, dirX, dirY,
                              planeX, planeY, inv_det, horizon_y, fb,
                              bm, world_h_for_standup(i));
                chair_ticks = (uint16_t)(chair_ticks + (prof_frt_read() - _cf0));
                continue;
            }
            /* far chair (beyond the nearest-3 render ceiling): fall through to the
             * camera-facing billboard path below, which reads
             * sprite_defs[CHAIR_SPRITE_KIND] = the baked 3/4 chair sprite. */
        }
        /* Floor shadow first (kind-2 cutouts), so the figure overpaints its near
         * edge — standing, leaning, or flat. Silhouette watchers cast none. */
        if (standups[i].kind == 2 && !standups[i].silhouette
            && !SHARED_UC->shadows_off
            && d2[i] < (int64_t)FX(8) * FX(8)              /* was ungated by distance */
            && (FX_MUL(standups[i].x - px, dirX)
              + FX_MUL(standups[i].y - py, dirY)) > -FX(1)) { /* not behind the camera */
            /* Same frustum cull as the chair shadow: an off-screen neanderthal
             * within 8 cells used to run its shadow setup (nearest_light_dir)
             * for a shadow you can't see. */
            fx_t nsx = standups[i].x - px, nsy = standups[i].y - py;
            fx_t ntX = FX_MUL(inv_det, FX_MUL(dirY, nsx) - FX_MUL(dirX, nsy));
            fx_t ntY = FX_MUL(inv_det, FX_MUL(-planeY, nsx) + FX_MUL(planeX, nsy));
            if (ntY >= FX(0.2)) {
                int nX = (SCREEN_W >> 1)
                       + (int)(((int32_t)(SCREEN_W >> 1) * FX_DIV(ntX, ntY)) >> FX_SHIFT);
                if (nX > -(SCREEN_W >> 1) && nX < SCREEN_W + (SCREEN_W >> 1))
                    draw_standup_shadow(i, col_start, col_end, px, py, dirX, dirY,
                                        planeX, planeY, inv_det, horizon_y, fb);
            }
        }
        /* Shoved over -> fall FLAT to the floor via the billboard-slice +
         * perspective floor-cast (reads as a real body on the ground, dodges the
         * affine-flat "shrink"). The body length is capped to whatever's ahead
         * (standup_body_len), so a body toppling toward a partition stops AT the
         * slab instead of clipping through it — no permanent mid-air lean. */
        if (standup_down[i]) {
            if (standup_fall_prog[i] < STANDUP_FALL_MAX) {   /* mid tip-over */
                uint8_t theta = (uint8_t)(((int)standup_fall_prog[i] * 64) / STANDUP_FALL_MAX);
                draw_falling_standup(i, col_start, col_end, px, py, dirX, dirY,
                                     planeX, planeY, inv_det, horizon_y, fb, theta);
            } else {                                         /* settled: flat */
                draw_fallen_standup(i, col_start, col_end, px, py, dirX, dirY,
                                    planeX, planeY, inv_det, horizon_y, fb);
            }
            continue;
        }
        /* Standing neanderthal: CHEAPENED. He is a FLAT standee, so the
         * tex_tri world-quad (per-span divides + a per-pixel UV pair — the
         * 7fps screen-filling incident) reduces exactly to the billboard
         * column loop with the width foreshortened by the bearing cosine
         * (sliver edge-on) and the cardboard back past 90 degrees, which
         * the generic path's is_front logic already paints. He now falls
         * through; only the width scale below is neanderthal-specific.
         * The topple/lean paths keep the quad renderer (tilted geometry
         * genuinely needs it). */
        fx_t sx = standups[i].x - px;
        fx_t sy = standups[i].y - py;

        fx_t transformX = FX_MUL(inv_det,
                            FX_MUL( dirY,  sx) - FX_MUL( dirX,  sy));
        fx_t transformY = FX_MUL(inv_det,
                            FX_MUL(-planeY, sx) + FX_MUL( planeX, sy));
        if (transformY < FX(0.2))     continue;     /* behind / right on top of it */
        if (transformY >= MAX_VIEW_DIST) continue;  /* beyond fog */
        /* Watcher: vanishes when you get within 3 cells. Iconic Backrooms
         * "did I see something?" tell. */
        if (standups[i].silhouette && transformY < FX(3)) continue;

        fx_t ratio = FX_DIV(transformX, transformY);
        int screenX = (SCREEN_W >> 1)
                    + (int)(((int32_t)(SCREEN_W >> 1) * ratio) >> FX_SHIFT);

        /* 2/3 world unit tall, 1:2 aspect. Floor-anchored: feet on the floor
         * row, top spriteHeight above. */
        int spriteHeight, spriteWidth;
        int chair_view = -1, mirror = 0;
        const dirset_t *fds = dirset_for_standup(i);
        if (fds) {
            /* DIRECTIONAL billboard: pick the baked view whose relative bearing
             * (chair->player direction vs the chair's facing) is nearest, so a
             * far chair HOLDS its parked orientation as you circle it instead of
             * pivoting to face the camera. 12 facings from 7 sprites: the
             * mirrored sectors reuse a view with texX reversed at blit time.
             * No atan2 — argmax of 12 dot products against sector directions
             * (COS/SIN LUT), ~24 multiplies per far chair. */
            /* Sector tables come FROM the bake (chair_dir_tex.h) — view count
             * and spacing are data, so re-bakes with different pose sets need
             * no engine edits. */
            int best = 0; fx_t bestd = 0; int first = 1;
            for (int k = 0; k < fds->nsect; k++) {
                /* +128: sector centers are OPPOSITE the view yaw — a player
                 * standing along the chair's facing (in FRONT of it) must get
                 * the FRONT view (yaw 128), not the back. Verified in sim. */
                uint8_t a = (uint8_t)(standups[i].facing_angle + 128 + fds->sect_v[k]);
                /* chair->player = -(sx,sy): sx,sy are standup - player above. */
                fx_t d = FX_MUL(-sx, COS_FX(a)) + FX_MUL(-sy, SIN_FX(a));
                if (first || d > bestd) { best = k; bestd = d; first = 0; }
            }
            chair_view = fds->sect_view[best]; mirror = fds->sect_mirror[best];
            /* Height from sprite_defs so the far billboard matches the near 3D
             * model exactly (the LOD swap must not change size); width from the
             * chosen view's own aspect.
             *
             * vspan: the bake's image band is taller than the model's front
             * elevation (the pitched camera sees the top face), so H rows
             * cover MORE world height than world_h. Inflate the blit by the
             * bake-emitted ratio and the body lands at exactly world_h; the
             * extra top-face rows draw above it, like the 3D model's top.
             *
             * Width gets the engine's own lateral scale on top of the view
             * aspect: the projection maps one world unit to (SCREEN_W/2)/0.66
             * px across vs SCREEN_H px down (the 0.66 camera plane), so a
             * width derived purely from the height scale drew every
             * directional billboard 8% narrower than its 3D model. 277 =
             * round(256 * (SCREEN_W/2) / (0.66 * SCREEN_H)). */
            spriteHeight = (int)(((int64_t)SCREEN_H * sprite_defs[standups[i].kind].world_h)
                                 / transformY);
            spriteHeight = (spriteHeight * fds->vspan) >> 8;
            spriteWidth  = (int)(((int32_t)spriteHeight * fds->views[chair_view].w * 277)
                                 / ((int32_t)fds->h << 8));
        } else {
            spriteHeight = (int)((((int32_t)SCREEN_H * 2) << FX_SHIFT) / (transformY * 3));
            spriteWidth  = spriteHeight >> 1;
            if (standups[i].kind == 2 && !standups[i].silhouette) {
                /* Flat-standee foreshortening: width scales by |cos| of the
                 * bearing vs the standee's facing — full face-on, a sliver
                 * edge-on. With the front/back swap this reproduces the
                 * world-quad's look at billboard cost. (The watcher stays a
                 * pure camera-facing billboard: it is MEANT to face you.) */
                fx_t fwX = COS_FX(standups[i].facing_angle);
                fx_t fwY = SIN_FX(standups[i].facing_angle);
                fx_t dp = FX_MUL(sx, fwX) + FX_MUL(sy, fwY);
                if (dp < 0) dp = -dp;
                fx_t ax2 = sx < 0 ? -sx : sx, ay2 = sy < 0 ? -sy : sy;
                fx_t mx3 = ax2 > ay2 ? ax2 : ay2, mn3 = ax2 > ay2 ? ay2 : ax2;
                fx_t mag = (mx3 - (mx3 >> 5)) + ((mn3 >> 1) - (mn3 >> 3));
                if (mag > 0) {
                    fx_t cosrel = (fx_t)(((int64_t)dp << FX_SHIFT) / mag);
                    if (cosrel > FX_ONE) cosrel = FX_ONE;
                    spriteWidth = (int)(((int64_t)spriteWidth * cosrel) >> FX_SHIFT);
                    if (spriteWidth < 2) spriteWidth = 2;   /* edge-on sliver */
                }
                /* Match the WORLD-anchored U direction of the quad/fall
                 * renderers: their texture runs along the standee's width
                 * axis (facing+64). If that axis projects to screen-LEFT
                 * from where the viewer stands, mirror the blit — or the
                 * push-to-topple handoff X-flips the figure mid-shove. */
                fx_t wpx = COS_FX((uint8_t)(standups[i].facing_angle + 64));
                fx_t wpy = SIN_FX((uint8_t)(standups[i].facing_angle + 64));
                if (FX_MUL(inv_det,
                        FX_MUL(dirY, wpx) - FX_MUL(dirX, wpy)) < 0)
                    mirror = 1;
            }
        }
        if (spriteWidth < 1) spriteWidth = 1;
        if (spriteHeight < 1) continue;

        /* Floor row at this distance: horizon_y + (focal·eyeH)/transformY.
         * horizon_y shifts with pitch; the focal·eyeH term drops with the
         * crouch so the standup's feet ride the floor up toward the horizon
         * in lockstep with the wall floor-edge (no floating). */
        int floor_y = horizon_y
                    + (int)(((int32_t)((SCREEN_H * (int)SHARED_UC->eye_h) >> 8) << FX_SHIFT) / transformY);
        int drawEndY_u   = floor_y;
        int drawStartY_u = floor_y - spriteHeight;
        int drawStartX_u = screenX - (spriteWidth >> 1);
        int drawEndX_u   = screenX + (spriteWidth >> 1);
        int drawStartY = drawStartY_u < 0 ? 0 : drawStartY_u;
        int drawEndY   = drawEndY_u >= SCREEN_H ? SCREEN_H - 1 : drawEndY_u;
        int drawStartX = drawStartX_u < 0 ? 0 : drawStartX_u;
        int drawEndX   = drawEndX_u >= SCREEN_W ? SCREEN_W - 1 : drawEndX_u;
        /* Clip to this CPU's column half so the two SH-2s draw disjoint spans. */
        if (drawStartX < col_start) drawStartX = col_start;
        if (drawEndX   > col_end - 1) drawEndX = col_end - 1;
        if (drawStartX > drawEndX) continue;

        /* Front/back: standup forward is (cos angle, sin angle).
         * sx, sy = standup - player, so player - standup = -sx, -sy.
         * dot(player - standup, forward) > 0 means player is in front;
         * equivalently (sx*fx + sy*fy) < 0. */
        fx_t fwdX = COS_FX(standups[i].facing_angle);
        fx_t fwdY = SIN_FX(standups[i].facing_angle);
        int is_front = (FX_MUL(sx, fwdX) + FX_MUL(sy, fwdY)) < 0;
        const sprite_def_t *sd = &sprite_defs[standups[i].kind];
        uint8_t back_color = sd->base + 0;
        /* Watcher silhouette: every non-transparent texture pixel becomes
         * the darkest figure shade. No front/back distinction — just a
         * flat dark outline against the wallpaper. */
        int is_silhouette = standups[i].silhouette;
        uint8_t silhouette_color = sd->base + 1;

        /* LOD: swap to the 128x256 hi-res texture when the sprite is
         * close enough that the 32x64 lo-res starts showing block
         * artifacts. At transformY < 3, the sprite is ~75+ px tall —
         * each lo-res texel is ~2.3 screen pixels (visibly chunky).
         * Hi-res cuts that to ~0.6 (silky). Beyond distance 3 the
         * sprite is small enough that lo-res reads as crisp.
         *
         * Hi-res is stored column-major: walking down a screen column
         * walks sequential bytes in memory, so adjacent texY fetches
         * share a cache line. The lo-res 32x64 (2KB) fits entirely in
         * the SH-2 4KB cache so its row-major layout doesn't suffer
         * the same penalty. col_step expresses the row-stride: 1 for
         * column-major (sequential), tex_w for row-major (strided). */
        const uint8_t *tex;
        int tex_w, tex_h, col_step;
        if (chair_view >= 0) {            /* directional view (any set), row-major */
            tex      = fds->views[chair_view].tex;
            tex_w    = fds->views[chair_view].w;
            tex_h    = fds->h;
            col_step = tex_w;
        } else if (transformY < FX(3) && sd->tex_hi) {
            tex      = sd->tex_hi;
            tex_w    = sd->w_hi;
            tex_h    = sd->h_hi;
            col_step = 1;                 /* hi-res stored column-major */
        } else {
            tex      = sd->tex;
            tex_w    = sd->w;
            tex_h    = sd->h;
            col_step = sd->w;             /* lo-res row-major: next row is W bytes */
        }

        /* Precompute texY increment per screen row — same trick as wall
         * texture stepping. Was doing one divide per pixel (~30 cycles each
         * on SH-2), now one divide per sprite + add per pixel. For a sprite
         * at distance 3 (~3000 pixels) this is ~4ms saved per frame. */
        fx_t texY_step    = ((fx_t)tex_h << FX_SHIFT) / spriteHeight;
        fx_t texY_start_v = (fx_t)(drawStartY - drawStartY_u) * texY_step;

        /* Per-sprite value map: fold front/back/silhouette AND (for box-model
         * billboards) distance fog into a tiny LUT so the inner loop is one
         * table lookup. Directional set: v>0 -> CHAIR_BASE + (v-1) - fog, so
         * the far billboard sits in the SAME dark wood band as the near 3D
         * model and fades with distance like the walls (the generic base+v
         * path left it a shade too light AND un-fogged — a bright tan floating
         * in the fog). Fog matches draw_chair_3d. Keyed off the dirset, not
         * CHAIR_SPRITE_KIND: the desk's bake encodes the same face-shade
         * values, and routing it through the standee path drew it bright tan
         * while its 3D pop-in wore fogged wood. */
        uint8_t vmap[18];   /* texels 0..9 as before; a MULTI-RAMP composite
                             * bake adds slot values at 10..17 */
        build_standup_vmap(i, transformY, fds, sd, is_silhouette,
                           silhouette_color, is_front, back_color, vmap);

        /* Big near figure: word-pair columns (2px blocks). A close standee
         * covers most of the screen height, and on the 32X a byte FB write
         * pays ~word cost — so sampling one texel and writing the pair as a
         * 16-bit word halves the store cost of exactly the figures that
         * dominate the sprite pass (a room of them was F:04). At this size the
         * 2px block is masked by the figure's own scale; small/far figures stay
         * full-res. Round the start UP to even so the pair never reads a
         * negative texX, and stripe+1 stays < col_end (drawEndX <= col_end-1,
         * col_end a multiple of 4), so it can't write into the other CPU's half. */
        /* Standing-still snap (same rule as the wall LOD): the word-pair cost
         * cap is for MOTION, where the chunkiness is masked. Stop to look at a
         * figure within 4 cells and he renders full-res -- that's exactly when
         * you're studying him and exactly when there's frame time to spare. */
        int lod2 = (spriteHeight > STANDUP_LOD_H)
                && (SHARED_UC->is_walking || SHARED_UC->is_turning
                    || transformY >= FX(4));
        int sx0 = lod2 ? ((drawStartX + 1) & ~1) : drawStartX;
        for (int stripe = sx0; stripe <= drawEndX; stripe += (lod2 ? 2 : 1)) {
            int clip_bot = drawEndY;
            if (transformY >= WALL_DIST(stripe)) {
                /* Occluded — but by WHAT? A partial (see-over) divider only
                 * hides the rows from its band top down; the standup's head
                 * and shoulders above it stay visible (the neanderthal-
                 * behind-a-partition bug: he vanished column-wide). CHAIRS get
                 * no such peek — furniture behind a wall-like promoted divider
                 * must stay hidden, else the chair paints its top OVER the
                 * divider (the chairs-through-walls bug). Only tall standups peek. */
                int pt = PART_TOP(stripe);
                if (!pt || pt <= drawStartY
                    || standups[i].kind == CHAIR_SPRITE_KIND) continue;  /* full occluder */
                if (pt - 1 < clip_bot) clip_bot = pt - 1;
            }

            int texX = ((stripe - drawStartX_u) * tex_w) / spriteWidth;
            if (texX < 0 || texX >= tex_w) continue;
            if (mirror) texX = tex_w - 1 - texX;   /* mirrored directional view */

            /* Pointer to the start of this texture column. Column-major:
             * column texX begins at tex + texX*tex_h. Row-major: column
             * texX starts at tex + texX, and consecutive rows are tex_w
             * apart. col_step encodes which. */
            const uint8_t *col_base = (col_step == 1)
                ? (tex + texX * tex_h)
                : (tex + texX);

            uint8_t *p = fb + drawStartY * SCREEN_W + stripe;
            fx_t tex_pos = texY_start_v;
            /* texY can only overshoot the bottom of the texture (not go
             * negative — tex_pos starts >= 0 and step is positive).
             * clip_bot < drawEndY when a see-over divider hides the lower
             * rows: only the part above its band draws. */
            if (lod2) {
                for (int y = drawStartY; y <= clip_bot; y++) {
                    int texY = tex_pos >> FX_SHIFT;
                    if (texY >= tex_h) texY = tex_h - 1;
                    uint8_t v = col_base[texY * col_step];
                    if (v != 0) *(uint16_t *)p = WDUP(vmap[v]);   /* stripe & stripe+1 */
                    p += SCREEN_W;
                    tex_pos += texY_step;
                }
            } else {
                for (int y = drawStartY; y <= clip_bot; y++) {
                    int texY = tex_pos >> FX_SHIFT;
                    if (texY >= tex_h) texY = tex_h - 1;
                    uint8_t v = col_base[texY * col_step];
                    if (v != 0) *p = vmap[v];
                    p += SCREEN_W;
                    tex_pos += texY_step;
                }
            }
        }
    }
    if (col_start == 0) prof_pass_chair = chair_ticks;
}

/* ---- In-ROM asset viewer (pause menu ASSETS tab) ---------------------
 * Blit a baked sprite centered + integer-zoomed into the framebuffer, in the
 * real gameplay palette, so low-res assets can be eyeballed on hardware before
 * they're wired into a scene. Decodes offset sprites as base+(v-1); the door's
 * multi-ramp is approximated the same way (preview only). Lives here because
 * sprite_defs[] and the *_BASE palette macros are in this translation unit. */
/* Which directional view a given yaw resolves to, and how many the set has —
 * the viewer HUD reports this so you can confirm every baked frame is
 * reachable by rotating. Returns 0 when the asset has no directional set. */
int raycast_asset_dir_view(int sel, uint8_t yaw, int *nviews) {
    const dirset_t *ds = dirset_for_kind(sel);
    if (!ds) { if (nviews) *nviews = 0; return -1; }
    int best = 0; fx_t bestd = 0; int first = 1;
    for (int k = 0; k < ds->nsect; k++) {
        uint8_t a = (uint8_t)(yaw + 128 + ds->sect_v[k]);
        fx_t dd = SIN_FX(a);
        if (first || dd > bestd) { best = k; bestd = dd; first = 0; }
    }
    if (nviews) {
        int mx = 0;
        for (int k = 0; k < ds->nsect; k++)
            if (ds->sect_view[k] > mx) mx = ds->sect_view[k];
        *nviews = mx + 1;
    }
    return ds->sect_view[best];
}

/* COMPOSITE models (boxmodels[].alt) ride the asset cycle as first-class
 * entries after the sprite kinds — Mike: hiding the desk set behind a
 * B-chord variant meant 'I still don't see all of our assets'. */
static int viewer_alt_parent(int j) {
    for (int i = 0; i < BOXMODEL_COUNT; i++)
        if (boxmodels[i].alt && j-- == 0) return boxmodels[i].kind;
    return -1;
}
static int viewer_alt_count(void) {
    int n = 0;
    for (int i = 0; i < BOXMODEL_COUNT; i++) if (boxmodels[i].alt) n++;
    return n;
}

int raycast_asset_count(void) { return SPRITE_DEF_COUNT + viewer_alt_count(); }

int raycast_asset_valid(int sel) {
    if (sel >= SPRITE_DEF_COUNT)
        return viewer_alt_parent(sel - SPRITE_DEF_COUNT) >= 0;
    if (sel < 0) return 0;
    return sprite_defs[sel].tex != 0;      /* padding rows carry no texture */
}

/* Resolve a viewer selection to a drawable model: returns variant count
 * (0 = sprite-only), fills kind + whether it is the composite alt. */
int raycast_asset_model(int sel, int *kind, int *use_alt) {
    if (sel >= SPRITE_DEF_COUNT) {
        int pk = viewer_alt_parent(sel - SPRITE_DEF_COUNT);
        *kind = pk; *use_alt = 1;
        return pk >= 0 ? 1 : 0;
    }
    *kind = sel; *use_alt = 0;
    return boxmodel_for_kind(sel) ? 1 : 0;
}

const char *raycast_asset_name(int sel) {
    if (sel >= SPRITE_DEF_COUNT) {
        int pk = viewer_alt_parent(sel - SPRITE_DEF_COUNT);
        const boxmodel_t *bm = pk >= 0 ? boxmodel_for_kind(pk) : 0;
        return (bm && bm->alt_name) ? bm->alt_name : "COMBO";
    }
    /* Names come from sprite_defs.h (generated from the registry), so a
     * community upload shows its own id here rather than a generic "ASSET". */
    if (sel < 0 || sel >= (int)(sizeof sprite_names / sizeof sprite_names[0]))
        return "ASSET";
    return sprite_names[sel][0] ? sprite_names[sel] : "ASSET";
}

void raycast_asset_dims(int sel, int *w, int *h) {
    if (sel < 0 || sel >= SPRITE_DEF_COUNT) { *w = *h = 0; return; }
    *w = sprite_defs[sel].w; *h = sprite_defs[sel].h;
}

void raycast_asset_preview(uint8_t *fb, int sel, uint8_t yaw, fx_t dist) {
    if (sel < 0 || sel >= SPRITE_DEF_COUNT) return;
    const sprite_def_t *sd = &sprite_defs[sel];
    /* WORLD-QUAD preview — the sprite on a polygon through tex_tri, exactly
     * the neanderthal's in-game path: perspective projection per corner,
     * smooth continuous distance (no zoom notches), yaw it to edge-on and
     * around to the flat cardboard back, hi-res LOD inside 3 cells. The 32X
     * has no sprite hardware; this rasterizer IS the scaling. skip_z: the
     * viewer owns the screen, the stale wall z-buffer must not clip it. */
    /* Texture layout is a PER-ASSET property (SPRITE_F_COLMAJOR): the door
     * is [W][H] for the wall-column cache pattern, the outlet is wall-mounted
     * yet row-major — inferring layout from mount type transposed it into
     * speckle noise. Hi-res variants are always column-major. */
    const uint8_t *tex = sd->tex;
    int tw = sd->w, th = sd->h;
    int col_major = (sd->flags & SPRITE_F_COLMAJOR) != 0;
    if (dist < FX(3) && sd->tex_hi) {
        tex = sd->tex_hi; tw = sd->w_hi; th = sd->h_hi;
        col_major = 1;
    }
    uint8_t fbase = sd->base;
    if (sd->decode == SPRITE_DECODE_DOOR) {
        /* The door doesn't decode base+v like other sprites — its texels
         * (0..18) index the wall pass's multi-ramp dlut (leaf greys, EXIT
         * green, handle bronze, jamb). The generic path painted them as
         * DOOR_BASE offsets: a pale speckled slab with a scrambled sign.
         * Pre-decode ONCE into a scratch copy holding final CRAM indices
         * (door_shade 1 = the viewer's one-fog-step convention; texel 0 =
         * wall surround in game -> transparent here) and draw with base 0. */
        /* MEMORY OVERLAY (.hero_overlay_lo — see mars.ld): viewer-only preview.
         * door_pv_built deliberately STAYS in .bss: it is the zero-inited gate
         * that makes the one-shot fill below run, and moving it to the NOLOAD
         * overlay would let garbage read as "already built" and skip it. */
        static uint8_t door_pv[DOOR_TEX_WIDTH * DOOR_TEX_HEIGHT]
            __attribute__((section(".hero_overlay_lo")));
        static int door_pv_built = 0;
        if (!door_pv_built) {
            static const uint8_t finegrey[14] = {
                DOOR_DARK_BASE + 0, DOOR_DARK_BASE + 1, DOOR_DARK_BASE + 2, DOOR_DARK_BASE + 3,
                STIPPLE_BASE + 0, DOOR_BASE + 0, STIPPLE_BASE + 1, DOOR_BASE + 1,
                STIPPLE_BASE + 2, DOOR_BASE + 2, STIPPLE_BASE + 3, DOOR_BASE + 3,
                STIPPLE_BASE + 4, DOOR_BASE + 4 };
            const int door_shade = 1, sh2 = 2;
            uint8_t dlut[19];
            dlut[0] = 0;
            for (int g = 0; g < 5; g++) {
                int bi = 5 + 2 * g - sh2; if (bi < 0) bi = 0;
                int si = 4 + 2 * g - sh2; if (si < 0) si = 0;
                dlut[1 + g] = finegrey[bi];
                dlut[9 + g] = finegrey[si];
            }
            dlut[6] = (uint8_t)(SIGN_GREEN_BASE + 1);   /* viewer: near-bright sign */
            dlut[7] = (uint8_t)(SIGN_GREEN_BASE + 0);
            dlut[8] = (uint8_t)(SIGN_WHITE_BASE + 0);
            for (int i2 = 0; i2 < 4; i2++) {
                int hp = i2 - door_shade; if (hp < 0) hp = 0;
                dlut[14 + i2] = (uint8_t)(HANDLE_BASE + hp);
            }
            { int fp = 4 - door_shade; if (fp < 0) fp = 0;
              dlut[18] = (uint8_t)(FRAME_BASE + fp); }
            for (int i2 = 0; i2 < DOOR_TEX_WIDTH * DOOR_TEX_HEIGHT; i2++) {
                uint8_t t = ((const uint8_t *)door_tex)[i2];
                door_pv[i2] = (t > 18) ? 0 : dlut[t];
            }
            door_pv_built = 1;
        }
        tex = door_pv;
        fbase = 0;
    }
    /* CHAIR sprite: rotating in the viewer sweeps through the baked DIRECTIONAL
     * frames (the in-game far-chair look), so it reads as the chair spinning by
     * frame-swap. Pick the view for this yaw with the same argmax-dot picker the
     * game uses (chair facing = yaw, camera fixed in front -> chair->player is a
     * constant, so the dot reduces to sin), decode into a scratch buffer, and
     * render it FLAT (front-facing quad) so only the frame changes. */
    static uint8_t chair_pv[DIRSET_PV_MAX];
    int chair_flat = 0, chair_mir = 0;
    const dirset_t *ds = dirset_for_kind(sel);
    if (ds) {
        int best = 0; fx_t bestd = 0; int first = 1;
        for (int k = 0; k < ds->nsect; k++) {
            uint8_t a = (uint8_t)(yaw + 128 + ds->sect_v[k]);
            fx_t dd = SIN_FX(a);                     /* model->player=(0,1): dot=sin(a) */
            if (first || dd > bestd) { best = k; bestd = dd; first = 0; }
        }
        int view = ds->sect_view[best];
        chair_mir = ds->sect_mirror[best];
        const uint8_t *vt = ds->views[view].tex;
        int vw = ds->views[view].w;
        /* Per-kind ramp, same as build_standup_vmap: the chair/desk rows pin
         * CHAIR_BASE, an import with its own ramp (PVM gray) decodes into
         * that — hardcoded CHAIR_BASE painted the PVM's frames chair-brown. */
        uint8_t rbase = CHAIR_BASE;
        {
            const boxmodel_t *bmr = boxmodel_for_kind(sel);
            if (bmr) rbase = bmr->base;
        }
        unsigned pseed = ((unsigned)SHARED_UC->frame_count * 2654435761u) | 1;
        for (int yy = 0; yy < ds->h; yy++)
            for (int xx = 0; xx < vw; xx++) {
                uint8_t v = vt[yy * vw + xx];
                if (v >= 5) {          /* screen texels (core + rim): live
                                        * static — the viewer previews the
                                        * powered-on set */
                    chair_pv[yy * vw + xx] =
                        (uint8_t)(rbase + STATIC_NOISE(xx, yy, pseed));
                    continue;
                }
                int sh = (int)v - 1; if (sh > 3) sh = 3;
                chair_pv[yy * vw + xx] = v ? (uint8_t)(rbase + sh) : 0;
            }
        tex = chair_pv; tw = vw; th = ds->h; col_major = 0; fbase = 0;
        chair_flat = 1;
    }
    if (dist < FX(0.4)) dist = FX(0.4);
    fx_t Hh = sd->world_h;
    fx_t hw = (fx_t)(((int64_t)Hh * sd->w) / (2 * sd->h));   /* half-width, lo-res aspect */
    if (chair_flat) hw = (fx_t)(((int64_t)Hh * tw) / (2 * th));  /* directional-view aspect */
    fx_t cyw = COS_FX(yaw), syw = SIN_FX(yaw);
    if (chair_flat) { cyw = FX_ONE; syw = 0; }              /* flat front billboard */
    const int oxc = SCREEN_W / 2, oyc = SCREEN_H / 2 + 12;
    int is_front = (cyw >= 0);
    uint8_t back_c = (uint8_t)(sd->base + 0);
    /* EXACT per-column render. The preview quad is VERTICAL and screen
     * columns are vertical, so each screen column meets the quad at ONE
     * depth: invert the projection per column (solve the quad's lateral
     * coordinate l from screen x) and both U and V map perspective-exactly —
     * the same reason the wall raycaster never warps. This replaced the
     * strip-subdivided tex_tri pair, whose residual affine error still
     * showed as serrated silhouette edges and a sheared handle: there is
     * no interpolation left to be wrong, and two divides per column beat
     * the strips' per-scanline setup anyway. */
    for (int x = 0; x < SCREEN_W; x++) {
        fx_t xp = ((fx_t)(x - oxc) << FX_SHIFT) / (SCREEN_W / 2);
        fx_t den = cyw - FX_MUL(xp, syw);
        if (den > -FX(0.02) && den < FX(0.02)) continue;   /* column parallel to quad */
        fx_t l = (fx_t)(((int64_t)FX_MUL(xp, dist) << FX_SHIFT) / den);
        if (l < -hw || l > hw) continue;                   /* misses the quad */
        fx_t d = dist + FX_MUL(l, syw);
        if (d < FX(0.2)) continue;                         /* behind/at the near plane */
        fx_t inv = fx_div_hw(FX_ONE, d);
        int dy = (int)((FX_MUL(Hh >> 1, inv) * (int64_t)SCREEN_H) >> FX_SHIFT);
        int ytop = oyc - dy, ybot = oyc + dy;
        int u = (int)(((int64_t)(l + hw) * tw) / ((int64_t)hw << 1));
        if (u < 0) u = 0; else if (u >= tw) u = tw - 1;
        /* The door texture is authored pre-mirrored (the game's wall pass draws
         * it horizontally flipped). This viewer path does NOT flip, so match it
         * here or the EXIT sign reads reversed. */
        if (sd->decode == SPRITE_DECODE_DOOR) u = tw - 1 - u;
        if (chair_flat && chair_mir) u = tw - 1 - u;   /* mirrored directional view */
        const uint8_t *cb = col_major ? tex + u * th : tex + u;
        int cstep = col_major ? 1 : tw;
        int y0c = ytop < 0 ? 0 : ytop;
        int y1c = ybot >= SCREEN_H ? SCREEN_H - 1 : ybot;
        int span = ybot - ytop; if (span < 1) span = 1;
        fx_t vstep = ((fx_t)th << FX_SHIFT) / span;
        fx_t vpos = (fx_t)(y0c - ytop) * vstep;
        uint8_t *p = fb + y0c * SCREEN_W + x;
        for (int y = y0c; y <= y1c; y++) {
            int tv = vpos >> FX_SHIFT;
            if (tv >= th) tv = th - 1;
            uint8_t t = cb[tv * cstep];
            if (t) *p = is_front ? (uint8_t)(fbase + t) : back_c;
            p += SCREEN_W;
            vpos += vstep;
        }
    }
}

/* ---- Live 3D mesh viewer (asset viewer) ------------------------------
 * Rotate + orthographic-project + painter-sort + flat-fill the baked chair
 * tri-mesh into the framebuffer. rotY/rotX are 0..255 angles. The caller
 * clears the backdrop first. Dedicated-screen use (one model, no raycasting)
 * so the 457-tri mesh is affordable. Reuses chair_tri_fill for the polygons. */
/* MEMORY OVERLAY (.hero_overlay_lo — see mars.ld): asset-viewer scratch, reached
 * only via raycast_asset_preview() from the menu, and dead during gameplay. Each
 * frame writes entries [0, nv) / [0, ntr) and reads only through that same range,
 * so the missing .bss zero-init is never observed. _lo, not .hero_overlay: these
 * are LIVE while the viewer runs, so they must sit away from the stack. */
#define MV_OVL __attribute__((section(".hero_overlay_lo")))
/* Sized for the BOX meshes now — the hero tri-mesh (1,692 tris) that used
 * to dictate these is out of the build, so the viewer buffers shrink to the
 * largest box model (BX_MAXBOXES * 8 verts / * 12 tris). */
static int   mv_px[BX_MAXBOXES * 8] MV_OVL;
static int   mv_py[BX_MAXBOXES * 8] MV_OVL;
static fx_t  mv_pz[BX_MAXBOXES * 8] MV_OVL;
static uint16_t mv_order[BX_MAXBOXES * 12] MV_OVL;
static fx_t     mv_dep[BX_MAXBOXES * 12] MV_OVL;

/* The in-game 7-box chair expanded to a tri list — the viewer's GAME variant.
 * Same 8.8 y-up model space as the baked box lists (height 1.0 = 256), so the
 * two variants render at identical scale and the comparison is honest. Shades
 * follow chair_face_shade's fixed axis mapping (top bright, bottom dark). */
/* Sized for the LARGEST box model in ROM. Every model that build_box_mesh can
 * be handed MUST be counted here — an imported model with more boxes than the
 * chair silently overflows bx_verts into neighbouring .bss otherwise (caught
 * exactly that way in the host sim, with a 12-box desk against 9-box arrays).
 * The static assert makes the next import fail the BUILD instead. */
#define BXV (BX_MAXBOXES * 8)
#define BXT (BX_MAXBOXES * 12)
static int16_t  bx_verts[BXV][3];
static uint16_t bx_tri[BXT][3];
static uint8_t  bx_shade[BXT];
static uint8_t  bx_box[BXT];    /* owning box per tri: per-box ramp override */
static int      bx_nv = 0, bx_nt = 0;
static int      bx_built_for = -1;      /* which model the arrays currently hold */
static void build_box_mesh(const cbox_t *boxes, int nboxes) {
    /* bx_shade stores the FACE AXIS (0..5), not a shade: the GAME variant is
     * shaded live through chair_face_shade at render time, fed the viewer's
     * own yaw — so spinning the chair sweeps exactly the in-game shades
     * (fixed world light), instead of a static per-axis approximation. */
    int nt = 0;
    for (int b = 0; b < nboxes; b++) {
        const cbox_t *bx = &boxes[b];
        for (int v = 0; v < 8; v++) {
            cbox_corner(bx, v, &bx_verts[b*8+v][0], &bx_verts[b*8+v][1],
                               &bx_verts[b*8+v][2]);   /* wedge-aware */
        }
        for (int f = 0; f < 6; f++) {
            const uint8_t *vi = chair_face_v[f];
            bx_tri[nt][0]=(uint16_t)(b*8+vi[0]); bx_tri[nt][1]=(uint16_t)(b*8+vi[1]);
            bx_tri[nt][2]=(uint16_t)(b*8+vi[2]); bx_box[nt]=(uint8_t)b; bx_shade[nt++] = (uint8_t)f;
            bx_tri[nt][0]=(uint16_t)(b*8+vi[0]); bx_tri[nt][1]=(uint16_t)(b*8+vi[2]);
            bx_tri[nt][2]=(uint16_t)(b*8+vi[3]); bx_box[nt]=(uint8_t)b; bx_shade[nt++] = (uint8_t)f;
        }
    }
    bx_nv = nboxes * 8;
    bx_nt = nt;
}

/* Clipped Bresenham for the wireframe view — writes are bounds-checked per
 * pixel so off-screen verts at high zoom are safe. */
static void mv_line(uint8_t *fb, int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if ((unsigned)x0 < SCREEN_W && (unsigned)y0 < SCREEN_H)
            fb[y0 * SCREEN_W + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

int raycast_kind_model_variants(int kind) {
    /* 0 = sprite-only, 1 = has a box mesh, 2 = mesh + composite alt.
     * TABLE-DRIVEN: the viewer shows whatever boxmodels[] holds — adding
     * a model (or an alt) never touches the viewer again (Mike: 'we're
     * burning code and copies of models'). */
    const boxmodel_t *bm = boxmodel_for_kind(kind);
    return bm ? (bm->alt ? 2 : 1) : 0;
}

void raycast_model_view(uint8_t *fb, uint8_t rotY, uint8_t rotX, int zoom_px, int variant, int wire,
                        int kind, int use_alt) {
    (void)variant;
    /* Viewer shade ramp tracks the in-game one (boxmodels[].base) so the
     * BOXES view previews the real colour; mbm also carries the panel
     * textures and the stand bias. Same table the renderer draws from. */
    const boxmodel_t *mbm = boxmodel_for_kind(kind);
    if (!mbm) return;
    if (use_alt && mbm->alt) mbm = (const boxmodel_t *)mbm->alt;
    uint8_t vbase = mbm->base;
    const cbox_t *boxes = mbm->boxes; int nb = mbm->nboxes;
    int mesh_key = (kind << 1) | (use_alt ? 1 : 0);
    if (bx_built_for != mesh_key) { build_box_mesh(boxes, nb); bx_built_for = mesh_key; }
    const int16_t  (*mverts)[3] = (const int16_t(*)[3])bx_verts; int nv = bx_nv;
    const uint16_t (*mtris)[3]  = (const uint16_t(*)[3])bx_tri;  int ntr = bx_nt;
    const uint8_t   *msh        = bx_shade;
    fx_t cyy = COS_FX(rotY), syy = SIN_FX(rotY);
    fx_t cxx = COS_FX(rotX), sxx = SIN_FX(rotX);
    fx_t ZOOM = FX(zoom_px);                     /* screen px per world unit */
    const int ox = SCREEN_W / 2, oy = SCREEN_H / 2 + 12;
    for (int v = 0; v < nv; v++) {
        fx_t x = (fx_t)mverts[v][0] << 8;                    /* 8.8 -> 16.16 */
        fx_t y = ((fx_t)mverts[v][1] << 8) - FX(0.5);        /* centre (height 1.0) */
        fx_t z = (fx_t)mverts[v][2] << 8;
        fx_t x1 =  FX_MUL(x, cyy) + FX_MUL(z, syy);          /* yaw */
        fx_t z1 = -FX_MUL(x, syy) + FX_MUL(z, cyy);
        fx_t y2 =  FX_MUL(y, cxx) - FX_MUL(z1, sxx);         /* pitch */
        fx_t z2 =  FX_MUL(y, sxx) + FX_MUL(z1, cxx);
        mv_px[v] = ox + FX_INT(FX_MUL(x1, ZOOM));
        mv_py[v] = oy - FX_INT(FX_MUL(y2, ZOOM));
        mv_pz[v] = z2;
    }
    if (wire) {
        /* WIREFRAME (Z toggles): skips the O(n^2) painter sort AND the fill —
         * box fills don't need it and edges need neither (no
         * occlusion to get wrong). Shade-coded so the form still reads. */
        for (int t = 0; t < ntr; t++) {
            const uint16_t *ti = mtris[t];
            int sh = msh[t];
            /* GAME variant: bx_shade holds the face AXIS — shade through the
             * in-game path itself, with the viewer yaw standing in for the
             * chair's facing (the two rotations share one formula). Hero mesh
             * keeps its baked 1..7 lum, remapped onto the same 0..3 ramp. */
            int r = chair_face_shade(sh, cyy, syy);   /* live in-game shading */
            if (mbm) r -= boxmodel_bias(mbm, bx_box[t]);
            r -= 1;                     /* one fog step: the game's typical viewing
                                         * distance (>2 cells) — raw ramp read tan */
            if (r < 0) r = 0; else if (r > 3) r = 3;
            uint8_t tb = mbm->box_base ? mbm->box_base[bx_box[t]] : vbase;
            uint8_t c = (uint8_t)(tb + r);
            mv_line(fb, mv_px[ti[0]], mv_py[ti[0]], mv_px[ti[1]], mv_py[ti[1]], c);
            mv_line(fb, mv_px[ti[1]], mv_py[ti[1]], mv_px[ti[2]], mv_py[ti[2]], c);
            mv_line(fb, mv_px[ti[2]], mv_py[ti[2]], mv_px[ti[0]], mv_py[ti[0]], c);
        }
        return;
    }
    for (int t = 0; t < ntr; t++) {
        const uint16_t *ti = mtris[t];
        mv_dep[t] = mv_pz[ti[0]] + mv_pz[ti[1]] + mv_pz[ti[2]];
        mv_order[t] = (uint16_t)t;
    }
    for (int a = 1; a < ntr; a++) {                          /* painter: far -> near */
        uint16_t io = mv_order[a]; fx_t d = mv_dep[io]; int b = a;
        while (b > 0 && mv_dep[mv_order[b-1]] < d) { mv_order[b] = mv_order[b-1]; b--; }
        mv_order[b] = io;
    }
    /* Panel texture in the viewer too — same face the in-game path textures
     * (box 0, -z: build_box_mesh emits it as tris 10 and 11), same UV-by-
     * corner-identity, decoded at the viewer's fixed one-fog-step shade. The
     * caveman quad and the intro box already proved textured tris here. */
    const boxmodel_t *vbm = mbm;
    uint8_t vlut[14];
    if (vbm && vbm->ftex)
        for (int v = 1; v <= 4; v++) {
            int s2 = (v - 1) - 1;
            if (s2 < 0) s2 = 0;
            vlut[v] = (uint8_t)(vbase + s2);
            vlut[5 + v] = vlut[v];
            vlut[9 + v] = (uint8_t)(vbase + (v - 1));   /* raw ramp: static */
        }
    vlut[5] = (uint8_t)(vbase + 0);                     /* dark glass */
    /* The viewer previews the powered-on set: live static, new seed per frame. */
    unsigned vseed = ((unsigned)SHARED_UC->frame_count * 2654435761u) | 1;
    for (int oi = 0; oi < ntr; oi++) {
        int t = mv_order[oi];
        const uint16_t *ti = mtris[t];
        int x0=mv_px[ti[0]],y0=mv_py[ti[0]], x1=mv_px[ti[1]],y1=mv_py[ti[1]], x2=mv_px[ti[2]],y2=mv_py[ti[2]];
        if (vbm && vbm->ftex && (t == 10 || t == 11)) {
            fx_t TW = FX(vbm->ftw), TH = FX(vbm->fth);
            if (t == 10)                       /* bl, tl, tr */
                tex_tri_lut(fb, 0, SCREEN_W, 0, vbm->ftex, vbm->ftw, vbm->fth,
                            0, vlut, vseed, 0, 0,
                            x0,y0, 0,TH, x1,y1, 0,0, x2,y2, TW,0);
            else                               /* bl, tr, br */
                tex_tri_lut(fb, 0, SCREEN_W, 0, vbm->ftex, vbm->ftw, vbm->fth,
                            0, vlut, vseed, 0, 0,
                            x0,y0, 0,TH, x1,y1, TW,0, x2,y2, TW,TH);
            continue;
        }
        if (vbm && vbm->rtex && (t == 8 || t == 9)) {
            /* REAR panel (face 4 = tris 8/9): its own UV corner order —
             * chair_face_v[4] runs bl,br,tr,tl seen from behind. */
            fx_t TW = FX(vbm->ftw), TH = FX(vbm->fth);
            if (t == 8)
                tex_tri_lut(fb, 0, SCREEN_W, 0, vbm->rtex, vbm->ftw, vbm->fth,
                            0, vlut, 0, 0, 0,
                            x0,y0, TW,TH, x1,y1, 0,TH, x2,y2, 0,0);
            else
                tex_tri_lut(fb, 0, SCREEN_W, 0, vbm->rtex, vbm->ftw, vbm->fth,
                            0, vlut, 0, 0, 0,
                            x0,y0, TW,TH, x1,y1, 0,0, x2,y2, TW,0);
            continue;
        }
        int sh = msh[t];
        int r = chair_face_shade(sh, cyy, syy);   /* live in-game shading */
        if (vbm) r -= boxmodel_bias(vbm, bx_box[t]);
        r -= 1;                                   /* one fog step, as seen in game */
        if (r < 0) r = 0; else if (r > 3) r = 3;
        uint8_t tb2 = (vbm && vbm->box_base) ? vbm->box_base[bx_box[t]] : vbase;
        chair_tri_fill(x0,y0, x1,y1, x2,y2, (uint8_t)(tb2 + r),
                       0, 0, 0, 0, SCREEN_W, fb);
    }
}


/* Doom-style segment projection — borrowed from r_phase2.c (32X Doom
 * Resurrection / d32xr) and r_segs.c (PC Doom). Transforms a wall
 * segment's two endpoints to camera space, near-plane clips, projects
 * to a screen-X range, and walks every column in that range writing
 * (inv_z, u_over_z) into PART_INV_Z[] / PART_U_OVER_Z[] if the segment
 * is closer than any previously projected partition for this column.
 *
 * The structural cure for the gap bug: NO per-column division by a
 * ray direction. The only per-segment divides (FX_DIV by ty1/ty2 for
 * the screen-X projection, and inv_z_step/u_step) operate on values
 * bounded by the near-plane clip (ty >= FX(0.5)), so int32 fixed-point
 * never overflows.
 *
 * u1_world/u2_world are the long-axis world coordinates at the segment
 * endpoints — passing the partition's X for horizontal segments (Y for
 * vertical) gives chevron-continuous tiling around the 4 sides. */

/* Project a WORLD-space axis-aligned quad lying in the ceiling plane (4 corners
 * wx[]/wy[]) and scanline-fill it with `color`, z-tested per column against the
 * walls. Used for the light PANEL and its two tube strips — because the tubes
 * are world geometry (not a screen-space band pattern), they rotate WITH the
 * ceiling instead of staying screen-horizontal. eye_h drives the crouch offset. */
RAMTEXT static void ceil_quad_fill(const fx_t *wx, const fx_t *wy, uint8_t color,
        fx_t centerY, fx_t px, fx_t py, fx_t dirX, fx_t dirY, fx_t planeX,
        fx_t planeY, fx_t inv_det, int horizon_y, int eye_h,
        int col_start, int col_end, uint8_t *fb) {
    int sx[4], sy[4], min_y = SCREEN_H, max_y = -1;
    for (int k = 0; k < 4; k++) {
        fx_t rx = wx[k] - px, ry = wy[k] - py;
        fx_t tX = FX_MUL(inv_det, FX_MUL( dirY,  rx) - FX_MUL( dirX,  ry));
        fx_t tY = FX_MUL(inv_det, FX_MUL(-planeY, rx) + FX_MUL( planeX, ry));
        if (tY < FX(0.2)) return;                 /* a corner behind the plane */
        fx_t ratio = FX_DIV(tX, tY);
        sx[k] = (SCREEN_W >> 1) + (int)(((int32_t)(SCREEN_W >> 1) * ratio) >> FX_SHIFT);
        int yoff = (int)(((int32_t)((SCREEN_H * (256 - eye_h)) >> 8) << FX_SHIFT) / tY);
        sy[k] = horizon_y - yoff;
        if (sy[k] < min_y) min_y = sy[k];
        if (sy[k] > max_y) max_y = sy[k];
    }
    if (min_y < 0) min_y = 0;
    if (max_y >= SCREEN_H) max_y = SCREEN_H - 1;
    if (max_y < min_y) return;
    fx_t edge_dx[4];
    for (int e = 0; e < 4; e++) {
        int e1 = (e + 1) & 3, dy = sy[e1] - sy[e];
        edge_dx[e] = dy ? (((fx_t)(sx[e1] - sx[e]) << FX_SHIFT) / dy) : 0;
    }
    for (int y = min_y; y <= max_y; y++) {
        int xs[2], n = 0;
        for (int e = 0; e < 4 && n < 2; e++) {
            int e1 = (e + 1) & 3;
            int lo = sy[e] < sy[e1] ? sy[e] : sy[e1];
            int hi = sy[e] < sy[e1] ? sy[e1] : sy[e];
            if (y < lo || y > hi || sy[e] == sy[e1]) continue;
            xs[n++] = sx[e] + (int)(((fx_t)(y - sy[e]) * edge_dx[e]) >> FX_SHIFT);
        }
        if (n < 2) continue;
        int l = xs[0] < xs[1] ? xs[0] : xs[1], r = xs[0] > xs[1] ? xs[0] : xs[1];
        if (l < 0) l = 0; if (r >= SCREEN_W) r = SCREEN_W - 1;
        if (l < col_start) l = col_start; if (r > col_end - 1) r = col_end - 1;
        if (l > r) continue;
        uint8_t *p = fb + y * SCREEN_W + l;
        for (int x = l; x <= r; x++) { if (centerY < WALL_DIST(x)) *p = color; p++; }
    }
}

/* Project each ceiling light as a flat tile in the ceiling plane: a soft-white
 * PANEL quad plus two bright-white TUBE quads (world geometry, so they rotate
 * with the ceiling), z-tested against wall_dist, per-light flicker + fog. */
RAMTEXT static void draw_lights(int col_start, int col_end) {
    /* Self-contained for the dual-CPU split (see draw_standups): snapshot the
     * player + derive the basis locally so the secondary can fill its column
     * half [col_start,col_end). */
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    uint8_t *fb = fb_pixels();
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = fx_div_hw(FX_ONE, det);   /* det == -0.66 const; bounded */
    /* Ceiling tiles project against the shifted horizon so they stay
     * on the ceiling when the camera pitches up or down. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;

    /* Flicker RNG seed: use the shared per-frame counter, not a local static.
     * Both CPUs run this pass now (column split), so a per-CPU counter would
     * desync the flicker roll across the seam for a light straddling it —
     * same fix the wall strobe uses. */
    uint32_t light_frame = SHARED_UC->frame_count;

    /* Lights are now CEILING TILES — flat axis-aligned rectangles in
     * the ceiling plane at world position (lx, ly). Each tile spans
     * (lx-HALF .. lx+HALF) × (ly-HALF .. ly+HALF). We project all 4
     * corners to screen and fill the bounding rectangle, so the lit
     * area tracks the same perspective as the surrounding ceiling
     * grid lines instead of being a separate billboard sprite. */
    /* A light fills exactly ONE square grid tile, SNAPPED to the grid so it sits
     * INSIDE the grid lines (a fluorescent panel replacing a tile), not a
     * centered rectangle straddling them. CEIL_GRID_DENSITY = 4 -> tile 0.25. */
    const fx_t TILE = FX_ONE / CEIL_GRID_DENSITY;   /* 0.25 */

    for (int i = 0; i < NUM_LIGHTS; i++) {
        fx_t lx = lights[i].x;
        fx_t ly = lights[i].y;
        /* A fixture whose own cell has a low ceiling (crawl or bulkhead) sits
         * ABOVE the slab -- from under the slab it's hidden, and the flat light
         * quad z-fights the slab's stamped depth and pokes through. Cull it: no
         * fixture renders through a low ceiling. Fixes the light-bleed in the
         * crawl tunnels (and any bulkhead cell), gated so lit maps pay nothing. */
        if (g_lowceil_active && ceil_is_low(lx, ly)) continue;
        /* Floor to the fixture's tile; span TWO tiles in X, one in Y = a 2:1
         * troffer rectangle (the tubes run the long X axis). */
        fx_t tx0 = (lx / TILE) * TILE, tx1 = tx0 + 2 * TILE;
        fx_t ty0 = (ly / TILE) * TILE, ty1 = ty0 + TILE;

        /* Center-distance check for view culling. */
        fx_t cx = lx - px;
        fx_t cy = ly - py;
        fx_t centerY = FX_MUL(inv_det,
                              FX_MUL(-planeY, cx) + FX_MUL(planeX, cy));
        if (centerY < FX(0.5)) continue;
        if (centerY >= MAX_VIEW_DIST) continue;

        /* LATERAL frustum cull (before the 3 troffer quads). The forward
         * distance cull alone still processed EVERY light in the 9-cell band
         * across the whole map WIDTH — on an open map most are off to the sides
         * and off-screen. Skip a light whose projected screen column sits well
         * outside this CPU's span [col_start,col_end). The 80px margin clears
         * the widest a tile ever projects at the near cull edge, so nothing
         * pops. Same lever as the chair-shadow frustum cull. */
        fx_t stX = FX_MUL(inv_det, FX_MUL(dirY, cx) - FX_MUL(dirX, cy));
        /* int64 mul: a light far to the SIDE isn't laterally pre-culled, so
         * stX/centerY can be huge and overflow a 32-bit intermediate. */
        int sX = (SCREEN_W >> 1)
               + (int)(((int64_t)(SCREEN_W >> 1) * FX_DIV(stX, centerY)) >> FX_SHIFT);
        if (sX < col_start - 80 || sX >= col_end + 80) continue;

        /* Per-light flicker + distance fog folded into one brightness offset
         * (0 full .. up) added to the panel/tube ramp index. The tile lives IN
         * the ceiling, so it must dim on the ceiling's fog curve or it reads as
         * pixels floating on the fog; gone by 5 cells. */
        int flicker_off = 0;
        if (SHARED_UC->lighting_flags & LIGHTING_FLICKER) {
            uint32_t r = light_frame * 1103515245u + i * 12347u;
            int roll = (r >> 24) & 0x1F;
            if      (roll < 2)  flicker_off = 2;
            else if (roll < 5)  flicker_off = 1;
        }
        /* A fluorescent fixture is an EMITTER — it punches through the fog, so
         * only a gentle far dimming (not the ceiling's hard fog curve). Bright
         * to ~5 cells, one notch to 7, gone by 9. */
        if      (centerY >= FX(9.0)) continue;
        else if (centerY >= FX(7.0)) flicker_off += 2;
        else if (centerY >= FX(5.0)) flicker_off += 1;

        int eye_h = (int)SHARED_UC->eye_h;
        /* Soft-white PANEL (ramp index 1) then two bright-white TUBE strips
         * (index 0), no dark frame. Tubes run along world X, world geometry so
         * they ROTATE WITH THE CEILING (the old screen-Y bands never did). */
        int panel_idx = 1 + flicker_off; if (panel_idx > 3) panel_idx = 3;
        int tube_idx  = 0 + flicker_off; if (tube_idx  > 3) tube_idx  = 3;
        uint8_t panel_c = (uint8_t)(LIGHT_BASE + panel_idx);
        uint8_t tube_c  = (uint8_t)(LIGHT_BASE + tube_idx);

        fx_t pwx[4] = { tx0, tx1, tx1, tx0 };
        fx_t pwy[4] = { ty0, ty0, ty1, ty1 };
        ceil_quad_fill(pwx, pwy, panel_c, centerY, px, py, dirX, dirY,
                       planeX, planeY, inv_det, horizon_y, eye_h, col_start, col_end, fb);

        /* Two tube strips: inset from the X ends, thin in Y, at ~1/3 and 2/3. */
        fx_t ex   = TILE >> 3;                     /* x inset from the frame */
        fx_t x0 = tx0 + ex, x1 = tx1 - ex;
        fx_t t1a = ty0 + (TILE * 9) / 32, t1b = ty0 + (TILE * 13) / 32;
        fx_t t2a = ty0 + (TILE * 19) / 32, t2b = ty0 + (TILE * 23) / 32;
        fx_t a1x[4] = { x0, x1, x1, x0 }, a1y[4] = { t1a, t1a, t1b, t1b };
        fx_t a2x[4] = { x0, x1, x1, x0 }, a2y[4] = { t2a, t2a, t2b, t2b };
        ceil_quad_fill(a1x, a1y, tube_c, centerY, px, py, dirX, dirY,
                       planeX, planeY, inv_det, horizon_y, eye_h, col_start, col_end, fb);
        ceil_quad_fill(a2x, a2y, tube_c, centerY, px, py, dirX, dirY,
                       planeX, planeY, inv_det, horizon_y, eye_h, col_start, col_end, fb);
    }

}

/* Combined sprite pass for a column range. The secondary calls this from its
 * CMD_TAIL handler to draw [split, SCREEN_W); the primary calls the two halves
 * directly (for separate L/P profiling). Lights first, then standups, so the
 * foreground neanderthal overpaints any ceiling-panel pixels in shared rows. */
void raycast_draw_sprites(int col_start, int col_end) {
    draw_lights(col_start, col_end);
    draw_standups(col_start, col_end);
}

/* Choose the sprite-pass column split. A near, screen-tall world-quad standup
 * (kind 2) is ~30k textured pixels; drawn on one cpu it serializes the frame
 * (the other idles in the post-wall barrier). Bisect it so both SH-2s draw half
 * in parallel. Only bias for a figure taller than ~half the screen (depth <~1.3
 * cells); otherwise the sprites are cheap and we keep the wall split so the tail
 * pass and this pass share a boundary. Returns a word-aligned column. */
RAMTEXT int raycast_sprite_split(int wall_split) {
    fx_t px = SHARED_UC->player.x, py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX = COS_FX(angle), dirY = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66)), planeY = FX_MUL(dirX, FX(0.66));
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return wall_split;
    fx_t inv_det = fx_div_hw(FX_ONE, det);
    int best_cx = -1; fx_t best_inv = 0;
    for (int i = 0; i < NUM_STANDUPS; i++) {
        if (!(standups[i].kind == 2 && !standups[i].silhouette)) continue;
        fx_t ddx = standups[i].x - px, ddy = standups[i].y - py;
        fx_t depth = FX_MUL(inv_det, -FX_MUL(planeY, ddx) + FX_MUL(planeX, ddy));
        if (depth < FX(0.3)) continue;
        fx_t inv_d = fx_div_hw(FX_ONE, depth);
        if (inv_d < FX(0.75)) continue;             /* < ~half-screen tall: leave split */
        if (inv_d <= best_inv) continue;            /* keep the nearest/tallest */
        best_inv = inv_d;
        fx_t lat = FX_MUL(inv_det, FX_MUL(dirY, ddx) - FX_MUL(dirX, ddy));
        best_cx = (SCREEN_W >> 1) + (int)(((int32_t)(SCREEN_W >> 1) * FX_MUL(lat, inv_d)) >> FX_SHIFT);
    }
    if (best_cx < 0) return wall_split;
    if (best_cx < 16) best_cx = 16; else if (best_cx > SCREEN_W - 16) best_cx = SCREEN_W - 16;
    return best_cx & ~3;
}

/* Secondary: drop stale lights[] AND standups[] before drawing sprites. Both
 * are rebuilt on the primary at each map load, and standup_down[] changes
 * mid-level when the player shoves a cutout over — without the purge the
 * secondary would keep drawing a toppled cutout on its half of the screen.
 * standups[] used to be const (ROM) and needed no purge; it's per-map RAM now.
 * WALL_DIST stays cache-through. */
void raycast_purge_sprite_cache(void) {
    /* Only the LIVE entries: sizeof lights is 4KB now, and purging all of it
     * every frame would tax every map for the benefit of the densest one. */
    purge_cache_range(lights,        (unsigned)num_lights * sizeof lights[0]);
    purge_cache_range(&num_lights,   sizeof num_lights);
    /* Game-on-glass: the primary rebakes glass_buf EVERY frame, so the
     * secondary's copy goes stale every frame — unlike tg_buf, which only
     * changes at map load and rides the gen purge. Flag first, buffer
     * only while a session is live. */
    purge_cache_range(&glass_active, sizeof glass_active);
    if (glass_active)
        purge_cache_range(glass_buf, sizeof glass_buf);
    purge_cache_range(standups,      sizeof standups);
    purge_cache_range(&num_standups, sizeof num_standups);
    purge_cache_range(standup_down,  sizeof standup_down);
    purge_cache_range(standup_fall_dir, sizeof standup_fall_dir);
    purge_cache_range(standup_fall_prog, sizeof standup_fall_prog);
    purge_cache_range(standup_fall_face, sizeof standup_fall_face);
    purge_cache_range(standup_fall_len_q, sizeof standup_fall_len_q);
    purge_cache_range(standup_power, sizeof standup_power);
    purge_cache_range(standup_scr_mode, sizeof standup_scr_mode);
    purge_cache_range(standup_bloom_start, sizeof standup_bloom_start);
    purge_cache_range(standup_on_desk, sizeof standup_on_desk);
}

/* Drop-ceiling grid pass — called from the secondary SH-2's dispatch loop
 * after the primary writes the player snapshot and signals
 * MARS_CMD_CEILING on COMM4. Primary can also call this directly during
 * single-CPU testing; in both cases the function reads player state
 * from SHARED_UC (cache-through alias) so the secondary sees the primary's
 * latest writes without explicit cache flushes.
 *
 * Writes only to the top half of the framebuffer (rows 0..SCREEN_H/2-1).
 * Disjoint from the carpet pass (bottom half) so they run in parallel
 * without races. Walls overwrite grid pixels where they intersect,
 * matching the previous sequential behavior. */
RAMTEXT void raycast_draw_ceiling_grid(int col_start, int col_end) {
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;

    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    fx_t leftDirX  = dirX - planeX;
    fx_t leftDirY  = dirY - planeY;
    fx_t rightDirX = dirX + planeX;
    fx_t rightDirY = dirY + planeY;
    if (SHARED_UC->ultra_twin == 2) {   /* ULTRA pass B: half-column shift (merged statically) */
        fx_t jx = FX_MUL(planeX, ULTRA_JIT_FX), jy = FX_MUL(planeY, ULTRA_JIT_FX);
        leftDirX += jx; rightDirX += jx;
        leftDirY += jy; rightDirY += jy;
    }

    uint8_t *fb = fb_pixels();
    /* horizon_y shifts with pitch; focal_const stays unshifted so
     * depth math stays calibrated regardless of head tilt. */
    int horizon_y   = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    /* Ceiling depth scales with focal·(1-eyeH): crouch -> ceiling recedes,
     * grid lines spread, in step with the wall ceiling-edge. Both CPUs read
     * eye_h here, so the secondary's column half stays aligned. */
    const int focal_const = (SCREEN_H * (256 - (int)SHARED_UC->eye_h)) >> 8;
    /* Same trick as the carpet: rebase row_color sampling so the
     * ceiling fog gradient and grid-line shade follow the shifted
     * horizon instead of staying glued to absolute screen Y. The crouch
     * term (matching raycast_clear_half) fogs the ceiling sooner as it
     * looms, in step with the floor brightening. */
    int sample_bias = (SCREEN_H / 2 - horizon_y)
                    + CROUCH_GRAD_SHIFT(SHARED_UC->eye_h);

    /* For band detection when dX or dY is exactly 0 (facing cardinal):
     * we track wxL_s / wyL_s across rows and emit a full-width band
     * whenever the integer part crosses between adjacent rows. */
    fx_t prev_wxL_s = 0;
    fx_t prev_wyL_s = 0;
    int  has_prev   = 0;

    for (int y = 0; y < horizon_y && y < SCREEN_H; y++) {
        int p = horizon_y - y;
        /* rowDist always positive; DIVU is ~3× faster than software. */
        fx_t rowDist = (fx_t)divu_u32((uint32_t)((fx_t)focal_const << FX_SHIFT),
                                      (uint32_t)p);
        fx_t wxL = px + FX_MUL(rowDist, leftDirX);
        fx_t wxR = px + FX_MUL(rowDist, rightDirX);
        fx_t wyL = py + FX_MUL(rowDist, leftDirY);
        fx_t wyR = py + FX_MUL(rowDist, rightDirY);

        fx_t wxL_s = wxL * CEIL_GRID_DENSITY;
        fx_t wxR_s = wxR * CEIL_GRID_DENSITY;
        fx_t wyL_s = wyL * CEIL_GRID_DENSITY;
        fx_t wyR_s = wyR * CEIL_GRID_DENSITY;

        int sy = y + sample_bias;
        /* Clamp to the fog midpoint so the crouch shift can't pull floor
         * colors into the ceiling sampling — that bypassed the fog-skip below
         * and drew dense grid lines near the horizon (the speckle band). */
        if (sy > SCREEN_H / 2) sy = SCREEN_H / 2;
        if (sy < 0)            sy = 0;
        int base_shade = row_color[sy] - CEIL_BASE;
        if (base_shade >= SHADE_LEVELS - 1) {
            /* Skip drawing but keep prev_* coherent for next row's band test. */
            prev_wxL_s = wxL_s; prev_wyL_s = wyL_s; has_prev = 1;
            continue;
        }
        int shade = base_shade + 3;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
        uint8_t grid_c = CEIL_BASE + shade;

        uint8_t *row_p = fb + y * SCREEN_W;

        /* World-X grid lines. Per-pixel crossings when the line is near-VERTICAL
         * (large screen span, converging to the vanishing point); full-width
         * BAND when near-HORIZONTAL (small span — the "rungs"). The band used to
         * fire only at EXACTLY dX==0, so a hair off cardinal dropped the rungs to
         * one lonely pixel per row (the lost-X-lines bug). Threshold on |dX|
         * routes it: a screen spanning <1 scaled unit of world-X is a rung. */
        fx_t dX = wxR_s - wxL_s;
        fx_t adX = dX < 0 ? -dX : dX;
        if (adX >= FX_ONE) {
            int lo = FX_INT(wxL_s), hi = FX_INT(wxR_s);
            if (lo > hi) { int t = lo; lo = hi; hi = t; }
            if (lo + 1 <= hi) {
                fx_t scale = FX_DIV((fx_t)SCREEN_W << FX_SHIFT, dX);
                for (int target = lo + 1; target <= hi; target++) {
                    fx_t num = ((fx_t)target << FX_SHIFT) - wxL_s;
                    int col = mul_hi32_s(num, scale);
                    if (col >= col_start && col < col_end) row_p[col] = grid_c;
                }
            }
        } else if (has_prev && FX_INT(wxL_s) != FX_INT(prev_wxL_s)) {
            for (int col = col_start; col < col_end; col++) row_p[col] = grid_c;
        }

        /* World-Y grid lines: same near-cardinal fix as X — band on small |dY|
         * (near-horizontal rungs) instead of only exactly dY==0. */
        fx_t dY = wyR_s - wyL_s;
        fx_t adY = dY < 0 ? -dY : dY;
        if (adY >= FX_ONE) {
            int lo = FX_INT(wyL_s), hi = FX_INT(wyR_s);
            if (lo > hi) { int t = lo; lo = hi; hi = t; }
            if (lo + 1 <= hi) {
                fx_t scale = FX_DIV((fx_t)SCREEN_W << FX_SHIFT, dY);
                for (int target = lo + 1; target <= hi; target++) {
                    fx_t num = ((fx_t)target << FX_SHIFT) - wyL_s;
                    int col = mul_hi32_s(num, scale);
                    if (col >= col_start && col < col_end) row_p[col] = grid_c;
                }
            }
        } else if (has_prev && FX_INT(wyL_s) != FX_INT(prev_wyL_s)) {
            for (int col = col_start; col < col_end; col++) row_p[col] = grid_c;
        }

        /* DARK ROOM ceiling. The base ceiling colour comes from the CLEAR pass
         * (a row fill with no world coords, so it can't know about cells) —
         * this is the first pass up here that HAS world coords, so the darkness
         * lands here. Drawn AFTER the grid lines on purpose: in an unlit room
         * you shouldn't be able to read the tile grid, so covering it is the
         * correct look and it saves darkening grid_c per cell. Same bbox ->
         * column-clip -> half-res fill as the carpet. */
        if (g_dark_active) {
            fx_t stepX = (wxR - wxL) / SCREEN_W;
            fx_t stepY = (wyR - wyL) / SCREEN_W;
            fx_t minx = wxL < wxR ? wxL : wxR, maxx = wxL < wxR ? wxR : wxL;
            fx_t miny = wyL < wyR ? wyL : wyR, maxy = wyL < wyR ? wyR : wyL;
            if (!(maxx < g_dark_x0 || minx > g_dark_x1 ||
                  maxy < g_dark_y0 || miny > g_dark_y1)) {
                int cA = col_start, cB = col_end - 1;
                const fx_t EPS = 16;
                if (stepX > EPS || stepX < -EPS) {
                    int a = (int)(fx_div_hw(g_dark_x0 - wxL, stepX) >> FX_SHIFT);
                    int b = (int)(fx_div_hw(g_dark_x1 - wxL, stepX) >> FX_SHIFT);
                    if (a > b) { int t = a; a = b; b = t; }
                    if (a > cA) cA = a; if (b < cB) cB = b;
                }
                if (stepY > EPS || stepY < -EPS) {
                    int a = (int)(fx_div_hw(g_dark_y0 - wyL, stepY) >> FX_SHIFT);
                    int b = (int)(fx_div_hw(g_dark_y1 - wyL, stepY) >> FX_SHIFT);
                    if (a > b) { int t = a; a = b; b = t; }
                    if (a > cA) cA = a; if (b < cB) cB = b;
                }
                if (cA < col_start) cA = col_start;
                if (cB > col_end - 1) cB = col_end - 1;
                int dsh = base_shade + DARK_ROOM_SHADE;
                if (dsh > SHADE_LEVELS - 1) dsh = SHADE_LEVELS - 1;
                uint8_t ddk = (uint8_t)(CEIL_BASE + dsh);
                int c0 = cA & ~1, c1 = cB | 1;
                if (c1 > col_end - 1) c1 = col_end - 1;
                fx_t dwx = wxL + stepX * c0, dwy = wyL + stepY * c0;
                for (int col = c0; col <= c1; col += 2, dwx += stepX * 2, dwy += stepY * 2) {
                    if (!cell_is_dark(dwx, dwy)) continue;
                    *(uint16_t *)(row_p + col) = WDUP(ddk);
                }
            }
        }

        prev_wxL_s = wxL_s;
        prev_wyL_s = wyL_s;
        has_prev   = 1;
    }
}

/* ── Low-ceiling zone ───────────────────────────────────────────────────────
 * Cast the low ceiling as a horizontal plane at height g_lowceil_h over the
 * zone rectangle. This is REAL floor/ceiling casting (each screen row maps to
 * one world distance), the same primitive the open ceiling/carpet use — which
 * is why it reads as a genuine 3D slab overhead, unlike the vertical-band crawl
 * tricks. Run AFTER the wall pass (WALL_DIST valid) and z-tested per column so
 * the slab occludes the far wall-tops/ceiling behind it and is hidden by nearer
 * walls — a sector ceiling. One full-width pass on the primary post-sync. */
RAMTEXT static void raycast_draw_low_ceiling(int col_start, int col_end, int slab_h) {
    if (!g_lowceil_active) return;
    int eye = (int)SHARED_UC->eye_h;
    if (eye >= slab_h) return;    /* eye at/above the slab: not overhead */

    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    fx_t leftDirX  = dirX - planeX, leftDirY  = dirY - planeY;
    fx_t rightDirX = dirX + planeX, rightDirY = dirY + planeY;
    if (SHARED_UC->ultra_twin == 2) {   /* ULTRA pass B: half-column shift (merged statically) */
        fx_t jx = FX_MUL(planeX, ULTRA_JIT_FX), jy = FX_MUL(planeY, ULTRA_JIT_FX);
        leftDirX += jx; rightDirX += jx;
        leftDirY += jy; rightDirY += jy;
    }

    uint8_t *fb = fb_pixels();
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    /* Depth-per-row uses the slab's height above the eye, same focal form as the
     * open ceiling (which uses 256-eye). A lower slab => nearer => looms larger. */
    int focal = (SCREEN_H * (slab_h - eye)) >> 8;   /* > 0 here */

    /* Bbox of THIS height's rects only. The old shared union bbox merged the
     * crawl tunnels with a distant bulkhead into one giant scan region, so both
     * passes walked ~2.5x the columns per row for nothing (the F:05 slideshow).
     * n_lowceil_rect <= 8, so this is a handful of compares per frame. */
    fx_t zx0 = 0, zx1 = 0, zy0 = 0, zy1 = 0;
    {
        int any = 0;
        for (int r = 0; r < n_lowceil_rect; r++) {
            if (lowceil_rect_h[r] != slab_h) continue;
            if (!any) {
                zx0 = lowceil_rect[r][0]; zy0 = lowceil_rect[r][1];
                zx1 = lowceil_rect[r][2]; zy1 = lowceil_rect[r][3];
                any = 1;
            } else {
                if (lowceil_rect[r][0] < zx0) zx0 = lowceil_rect[r][0];
                if (lowceil_rect[r][1] < zy0) zy0 = lowceil_rect[r][1];
                if (lowceil_rect[r][2] > zx1) zx1 = lowceil_rect[r][2];
                if (lowceil_rect[r][3] > zy1) zy1 = lowceil_rect[r][3];
            }
        }
        if (!any) return;   /* no zone at this height */
    }

    /* Per-column farthest slab depth, captured so we can stamp the z-buffer
     * AFTER the row loop (writing it mid-loop would corrupt the wall-occlusion
     * read below). Without this the slab never occludes the distant corridor
     * ceiling lights, which then draw straight through the ceiling.
     * wd[] snapshots the per-column wall depth (the z-test) into cache ONCE, so
     * the per-pixel test below isn't an uncached WALL_DIST read every pixel —
     * the slab fills a lot of screen inside a tunnel, so that read dominated. */
    static int16_t slab_far[SCREEN_W];   /* depth >> 4, wd[] precision -- fx_t was
                                          * 640B of SDRAM we needed back for BG_DIST */
    /* Cached WALL_DIST (>>4, int16) for the per-pixel z-test — reading the
     * cache-through WALL_DIST uncached per crawl pixel tanked the crawl pass.
     * int16 keeps it half the RAM of the old fx_t copy; 1/16-cell z is plenty. */
    static int16_t wd[SCREEN_W];
    for (int c = col_start; c < col_end; c++) {
        slab_far[c] = 0;
        int32_t d = WALL_DIST(c) >> 4;
        int16_t w16 = (int16_t)(d > 32767 ? 32767 : d);
        /* See-over fold: when a half-height counter owns this column's z, every
         * row this pass draws (y < horizon) sits ABOVE the counter's silhouette
         * top, so the ceiling slab z-tests against the BACKGROUND depth instead
         * -- otherwise a bulkhead vanished column-wide behind any counter. Only
         * when the silhouette top is below the horizon (standing): crouched
         * behind a counter its band covers ceiling rows and must keep occluding.
         * Folded into wd[] at snapshot time: zero extra arrays, zero per-pixel
         * cost. */
        {
            int pt = PART_TOP(c);
            if (pt && pt >= horizon_y) {
                int32_t bgw = (int32_t)BG_DIST(c) << 8;   /* >>12 -> wd's >>4 scale */
                w16 = (int16_t)(bgw > 32767 ? 32767 : bgw);
            }
        }
        wd[c] = w16;
    }

    for (int y = 0; y < horizon_y && y < SCREEN_H; y++) {
        int prow = horizon_y - y;
        if (prow <= 0) continue;
        fx_t rowDist = (fx_t)divu_u32((uint32_t)((fx_t)focal << FX_SHIFT),
                                      (uint32_t)prow);
        int32_t rd16v = rowDist >> 4;
        int16_t row16 = (int16_t)(rd16v > 32767 ? 32767 : rd16v);

        /* Distance fog for the unlit tile: darken toward the fog end of the
         * CEIL_BASE ramp with depth, so the crawlspace ceiling fades like the
         * room's (it's just lightless, not flat). ~2 shade steps per cell. */
        int tile_shade = LOWCEIL_TILE_SHADE + (int)(rowDist >> 15);
        if (tile_shade > SHADE_LEVELS - 1) tile_shade = SHADE_LEVELS - 1;
        int grid_shade = tile_shade + 3;
        if (grid_shade > SHADE_LEVELS - 1) grid_shade = SHADE_LEVELS - 1;
        uint8_t lc_tile = (uint8_t)(CEIL_BASE + tile_shade);
        uint8_t lc_grid = (uint8_t)(CEIL_BASE + grid_shade);
        /* Dark-room variants: the crawlspace ceiling is a real surface, so it must
         * honor a dark room like the walls/floor do. Push it DARK_ROOM_SHADE deeper
         * into the CEIL fog when the cell is dark (picked per-column below). */
        int tsd = tile_shade + DARK_ROOM_SHADE; if (tsd > SHADE_LEVELS - 1) tsd = SHADE_LEVELS - 1;
        int gsd = grid_shade + DARK_ROOM_SHADE; if (gsd > SHADE_LEVELS - 1) gsd = SHADE_LEVELS - 1;
        uint8_t lc_tile_d = (uint8_t)(CEIL_BASE + tsd);
        uint8_t lc_grid_d = (uint8_t)(CEIL_BASE + gsd);

        fx_t wxL = px + FX_MUL(rowDist, leftDirX);
        fx_t wyL = py + FX_MUL(rowDist, leftDirY);
        fx_t wxR = px + FX_MUL(rowDist, rightDirX);
        fx_t wyR = py + FX_MUL(rowDist, rightDirY);

        /* Per-row reject: skip rows whose entire world span misses the zone. */
        fx_t minx = wxL < wxR ? wxL : wxR, maxx = wxL < wxR ? wxR : wxL;
        fx_t miny = wyL < wyR ? wyL : wyR, maxy = wyL < wyR ? wyR : wyL;
        if (maxx < zx0 || minx > zx1 || maxy < zy0 || miny > zy1) continue;

        fx_t stepx = (wxR - wxL) / SCREEN_W;
        fx_t stepy = (wyR - wyL) / SCREEN_W;

        /* Column-clip: solve for the column sub-range whose world position lands
         * in the zone bbox, instead of scanning all 320 columns per row. The
         * crawlspace is narrow, so this is THE win — it turns a full-width
         * per-pixel scan into a handful of columns. Two hardware divides per axis
         * give the entry/exit columns; per-pixel ceil_is_low still confirms inside
         * the clipped span so multi-cell / disjoint zones stay correct. */
        int cA = col_start, cB = col_end - 1;
        const fx_t SLAB_EPS = 16;   /* treat |step| below this as "constant across row" */
        if (stepx > SLAB_EPS || stepx < -SLAB_EPS) {
            int a = (int)(fx_div_hw(zx0 - wxL, stepx) >> FX_SHIFT);
            int b = (int)(fx_div_hw(zx1 - wxL, stepx) >> FX_SHIFT);
            if (a > b) { int t = a; a = b; b = t; }
            if (a > cA) cA = a;
            if (b < cB) cB = b;
        } else if (wxL < zx0 || wxL > zx1) {
            continue;
        }
        if (stepy > SLAB_EPS || stepy < -SLAB_EPS) {
            int a = (int)(fx_div_hw(zy0 - wyL, stepy) >> FX_SHIFT);
            int b = (int)(fx_div_hw(zy1 - wyL, stepy) >> FX_SHIFT);
            if (a > b) { int t = a; a = b; b = t; }
            if (a > cA) cA = a;
            if (b < cB) cB = b;
        } else if (wyL < zy0 || wyL > zy1) {
            continue;
        }
        if (cA < col_start) cA = col_start;
        if (cB > col_end - 1) cB = col_end - 1;
        if (cA > cB) continue;      /* no zone columns in this row */

        /* Dark-beige drop-panel: fill + darker seam on a half-cell world lattice
         * (a parallax/structure cue so it reads as a solid ceiling).
         * HALF-RES: compute one column, emit a 2-pixel word. The slab is flat
         * beige, so the horizontal chunkiness is invisible — and it halves both
         * the per-pixel work and the uncached framebuffer transactions (one
         * mov.w vs two mov.b), which is the bulk of the L: crawl cost. The odd
         * partner inherits the even column's test (a 1px edge approximation). */
        uint8_t *row_p = fb + y * SCREEN_W;
        int c0 = cA & ~1;                  /* even-align for the word store */
        int c1 = cB | 1;                   /* include cB's odd partner      */
        if (c1 > col_end - 1) c1 = col_end - 1;
        fx_t wx = wxL + stepx * c0;
        fx_t wy = wyL + stepy * c0;
        for (int col = c0; col <= c1; col += 2, wx += stepx * 2, wy += stepy * 2) {
            if (ceil_h_at(wx, wy) != slab_h) continue;  /* only cells at THIS pass's height */
            if ((rowDist >> 4) >= wd[col]) continue;    /* behind a nearer occluder
                                                         * (see-over fold already
                                                         * baked into wd[]) */
            /* Lightless ceiling TILE: dim CEIL_BASE fill with a darker grid line
             * on the 0.25-cell tile lattice (matches the open ceiling's tiles). */
            int grid = (((int)wx & CEIL_TILE_MASK) < CEIL_TILE_LINE) ||
                       (((int)wy & CEIL_TILE_MASK) < CEIL_TILE_LINE);
            int dk = g_dark_active && cell_is_dark(wx, wy);   /* dark room dims the panel */
            uint8_t cv = grid ? (dk ? lc_grid_d : lc_grid)
                              : (dk ? lc_tile_d : lc_tile);
            *(uint16_t *)(row_p + col) = ((uint16_t)cv << 8) | cv;  /* col & col+1 */
            slab_far[col] = row16; slab_far[col + 1] = row16;
        }
    }
    /* Stamp the z-buffer with the slab's far edge so draw_lights occludes any
     * fixture beyond it — the distant corridor lights no longer bleed through. */
    for (int c = col_start; c < col_end; c++) {
        /* only if NEARER: via the see-over fold the slab can draw while FARTHER
         * than a counter that owns this column's z — the counter must keep it
         * (sprites still hide behind the counter). */
        fx_t sf = (fx_t)slab_far[c] << 4;
        if (sf && sf < WALL_DIST(c)) WALL_DIST(c) = sf;
    }
}

/* ── Low-ceiling edge caps ───────────────────────────────────────────────────
 * (Historically "bulkheads" -- renamed: it caps the boundary faces of ANY
 * lowered-ceiling zone at that zone's height, duct or doorway header alike.)
 * Cap the crawlspace at its four boundary faces with a vertical wall band from
 * the slab (g_lowceil_h) up to the full ceiling (256), so the entrance reads as
 * a solid low header you crawl under — not a floating slab / invisible barrier.
 * Each face is a short vertical wall; for every column we take the nearest face
 * the ray crosses (in front of any real wall) and fill its [h, 256] band. Real
 * corridor walls (e.g. the west boundary) z-cull the side faces automatically;
 * the open ends and any open side get a visible cap. Runs post-sync with the
 * slab, both reading the committed WALL_DIST z-buffer. */
RAMTEXT static void raycast_draw_ceil_caps(int col_start, int col_end, int slab_h) {
    if (!g_lowceil_active) return;
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX = COS_FX(angle), dirY = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66)), planeY = FX_MUL(dirX, FX(0.66));

    uint8_t *fb = fb_pixels();
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    int eye = (int)SHARED_UC->eye_h;
    int ch  = slab_h;
    const fx_t PAR  = FX(0.01);   /* skip near-parallel rays (no crossing) */
    const fx_t STEP = FX(0.03);   /* probe just past a crossing for entry/exit */

    /* Column-clip: project each crawlspace rect to a screen-column span and scan
     * only the union. When no crawlspace is in view (common — it exists in the
     * map but you're not looking at it), this skips the whole per-column
     * rect/face/divide loop. When you're standing inside one a corner projects
     * behind the camera, so we fall back to full width (no clip, no harm). */
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    int bcA = col_end, bcB = col_start - 1;
    if (det == 0) { bcA = col_start; bcB = col_end - 1; }
    else {
        fx_t inv_det = fx_div_hw(FX_ONE, det);
        for (int r = 0; r < n_lowceil_rect; r++) {
            if (lowceil_rect_h[r] != slab_h) continue;   /* this pass's height only */
            fx_t rcx[2] = { lowceil_rect[r][0], lowceil_rect[r][2] };
            fx_t rcy[2] = { lowceil_rect[r][1], lowceil_rect[r][3] };
            int rmin = col_end, rmax = col_start - 1, behind = 0;
            for (int a = 0; a < 2 && !behind; a++)
            for (int b = 0; b < 2; b++) {
                fx_t rx = rcx[a] - px, ry = rcy[b] - py;
                fx_t tY = FX_MUL(inv_det, FX_MUL(-planeY, rx) + FX_MUL(planeX, ry));
                if (tY < FX(0.2)) { behind = 1; break; }
                fx_t tX = FX_MUL(inv_det, FX_MUL(dirY, rx) - FX_MUL(dirX, ry));
                int sc = (SCREEN_W >> 1)
                       + (int)(((int64_t)(SCREEN_W >> 1) * fx_div_hw(tX, tY)) >> FX_SHIFT);
                if (sc < rmin) rmin = sc;
                if (sc > rmax) rmax = sc;
            }
            if (behind) { bcA = col_start; bcB = col_end - 1; break; }
            if (rmin < col_start)   rmin = col_start;
            if (rmax > col_end - 1) rmax = col_end - 1;
            if (rmin > rmax) continue;          /* this rect is off-screen */
            if (rmin < bcA) bcA = rmin;
            if (rmax > bcB) bcB = rmax;
        }
    }
    if (bcA > bcB) return;                       /* no crawlspace on screen */
    if (--bcA < col_start)   bcA = col_start;    /* widen 1px for cap edges */
    if (++bcB > col_end - 1) bcB = col_end - 1;

    for (int col = bcA; col <= bcB; col++) {
        fx_t camX = ((fx_t)(2 * col - SCREEN_W) << FX_SHIFT) / SCREEN_W;
        fx_t rdx = dirX + FX_MUL(planeX, camX);
        fx_t rdy = dirY + FX_MUL(planeY, camX);

        fx_t best = 0x7FFFFFFF;
        fx_t hit_along = 0;      /* world coord along the nearest face → texX */
        /* Cap EACH crawlspace rect independently (a single union bbox caps the
         * wrong faces for disjoint crawlspaces — that's why one tunnel had no
         * endcaps). A face is a header only where the ray ENTERS that rect
         * (ceiling steps down ahead); the far exit step is occluded by the slab,
         * so probe a point just past the crossing and skip it if it's outside
         * the rect. Take the nearest entry face over all rects. */
        for (int r = 0; r < n_lowceil_rect; r++) {
            if (lowceil_rect_h[r] != slab_h) continue;   /* this pass's height only */
            fx_t zx0 = lowceil_rect[r][0], zy0 = lowceil_rect[r][1];
            fx_t zx1 = lowceil_rect[r][2], zy1 = lowceil_rect[r][3];
            if (rdy > PAR || rdy < -PAR) {
                for (int e = 0; e < 2; e++) {
                    fx_t yf = e ? zy1 : zy0;
                    fx_t t  = fx_div_hw(yf - py, rdy);
                    if (t <= PAR) continue;
                    fx_t hx = px + FX_MUL(t, rdx);
                    if (hx < zx0 || hx > zx1) continue;
                    fx_t ex = px + FX_MUL(t + STEP, rdx);
                    fx_t ey = py + FX_MUL(t + STEP, rdy);
                    if (ex < zx0 || ex > zx1 || ey < zy0 || ey > zy1) continue;
                    /* Only cap where the ceiling actually STEPS DOWN — i.e. the cell
                     * the ray came FROM is open. If it's another crawl cell (two
                     * runs side by side), this is an interior edge with no header;
                     * capping it split the ceiling with phantom vertical walls. */
                    if (ceil_h_at(px + FX_MUL(t - STEP, rdx), py + FX_MUL(t - STEP, rdy)) == slab_h) continue;
                    if (t < best) { best = t; hit_along = hx; }
                }
            }
            if (rdx > PAR || rdx < -PAR) {
                for (int e = 0; e < 2; e++) {
                    fx_t xf = e ? zx1 : zx0;
                    fx_t t  = fx_div_hw(xf - px, rdx);
                    if (t <= PAR) continue;
                    fx_t hy = py + FX_MUL(t, rdy);
                    if (hy < zy0 || hy > zy1) continue;
                    fx_t ex = px + FX_MUL(t + STEP, rdx);
                    fx_t ey = py + FX_MUL(t + STEP, rdy);
                    if (ex < zx0 || ex > zx1 || ey < zy0 || ey > zy1) continue;
                    if (ceil_h_at(px + FX_MUL(t - STEP, rdx), py + FX_MUL(t - STEP, rdy)) == slab_h) continue;
                    if (t < best) { best = t; hit_along = hy; }
                }
            }
        }
        if (best == 0x7FFFFFFF) continue;
        int see_over_clip = -1;                        /* no clip by default */
        if (best >= WALL_DIST(col)) {                  /* behind a nearer occluder */
            /* See-over exemption (same as the slab fill): a half-height counter
             * owns this column's z, but the cap band lives ABOVE its silhouette.
             * Draw against the background depth and clip to above the counter. */
            int pt = PART_TOP(col);
            if (!(pt && (best >> 12) < (fx_t)BG_DIST(col))) continue;
            see_over_clip = pt - 1;
        }

        int lineHeight = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT),
                                       (uint32_t)best);
        int wall_bot  = horizon_y + ((lineHeight * eye) >> 8);
        int wall_top  = wall_bot - lineHeight;         /* height 256 (unclamped) */
        int top = wall_top < 0 ? 0 : wall_top;
        int bot = wall_bot - ((lineHeight * ch) >> 8); /* slab, height ch */
        if (bot > SCREEN_H - 1) bot = SCREEN_H - 1;
        if (see_over_clip >= 0 && bot > see_over_clip) bot = see_over_clip;
        if (top > bot) continue;

        /* Distance-fog shade, a touch darker (a header in shadow). */
        int bsh;
        if (best < FX(2.5)) bsh = (int)((best * 2) / FX(2.5));
        else { fx_t past = best - FX(2.5); fx_t span = FOG_RAMP_DIST - FX(2.5);
               bsh = 2 + (int)((past * 13) / span); }
        bsh += 2;
        if (bsh > SHADE_LEVELS - 1) bsh = SHADE_LEVELS - 1;
        /* Dark room: the crawlspace MOUTH cap is a real surface too, so push it
         * DARK_ROOM_SHADE deeper into fog like the walls/floor/slab (the low-
         * ceiling slab already does; this is the outer-edge header). */
        if (g_dark_active) {
            fx_t hitx = px + FX_MUL(best, rdx), hity = py + FX_MUL(best, rdy);
            if (cell_is_dark(hitx, hity)) {
                bsh += DARK_ROOM_SHADE;
                if (bsh > SHADE_LEVELS - 1) bsh = SHADE_LEVELS - 1;
            }
        }

        /* Chevron wallpaper, same texture as the walls, so the header reads as
         * part of the structure. The texture maps over the FULL wall height; the
         * band only draws its upper [ch,256] slice, aligning with adjacent walls.
         * LOD + chevron detail-fade mirror the main wall pass. */
        const uint8_t *btex; int btw, bth, btlx, btly;
        if (best < WALL_LOD_THRESHOLD) {
            btex = (const uint8_t *)wall_tex_hi_ram;
            btw = WALL_TEX_HI_WIDTH; bth = WALL_TEX_HI_HEIGHT;
            btlx = WALL_TILE_HI_X;   btly = WALL_TILE_HI_Y;
        } else {
            btex = (const uint8_t *)wall_tex_ram;
            btw = WALL_TEX_WIDTH; bth = WALL_TEX_HEIGHT;
            btlx = WALL_TILE_X;   btly = WALL_TILE_Y;
        }
        int bdetail;
        if (best < FX(2)) bdetail = WALL_PATTERN_MAX;
        else if (best < FX(3.5)) { bdetail = (int)(((FX(3.5) - best) * WALL_PATTERN_MAX) / FX(1.5)); if (bdetail < 0) bdetail = 0; }
        else bdetail = 0;
        fx_t bwh = hit_along - ((fx_t)FX_INT(hit_along) << FX_SHIFT);
        int btexX = (int)(((uint32_t)bwh * (uint32_t)(btw * btlx)) >> FX_SHIFT) & (btw - 1);
        const uint8_t *bcol = btex + btexX * bth;
        uint8_t blut[5];
        for (int v = 0; v < 5; v++) {
            int s = bsh + ((v * bdetail) >> 4);
            if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
            blut[v] = (uint8_t)(WALL_BASE + s);
        }
        int bmask = bth - 1;
        fx_t bstep = ((fx_t)(bth * btly) << FX_SHIFT) / lineHeight;
        fx_t bpos  = (fx_t)(top - wall_top) * bstep;   /* texel at the first drawn row */
        uint8_t *p = fb + col + top * SCREEN_W;
        for (int yy = top; yy <= bot; yy++) {
            *p = blut[bcol[(bpos >> FX_SHIFT) & bmask]];
            p += SCREEN_W; bpos += bstep;
        }
        /* Occlude lights/sprites behind the header — but only when the header
         * really is the nearest occluder. Via the see-over exemption this cap
         * can draw while FARTHER than a counter that owns the column's z;
         * stamping the far cap depth (and clearing the counter's PART_TOP)
         * made full sprite bodies bleed through the band. */
        if (best < WALL_DIST(col)) {
            WALL_DIST(col) = best;   /* header is the nearest occluder */
            PART_TOP(col) = 0;       /* full occlusion: no see-over */
        }
    }
}

/* EXIT HOLE render: a carved void in the wall face, chest height, centered.
 * Runs in the tail (walls committed, WALL_DIST final). Per column crossing
 * the face within the opening's width, three bands from TRUE near/far plane
 * projections give the cavity crawlspace-style depth:
 *   head reveal — the opening's underside, in shadow
 *   void        — the far opening, deepest shade (the exit reads as nothing)
 *   sill ledge  — the top surface you pull up onto, lit
 * Shallow rays clamp the far plane to the cavity's side wall so the interior
 * never reads deeper than the hole is wide. */
RAMTEXT static void draw_exit_hole(int col_start, int col_end) {
    if (g_exit_hole_cx < 0) return;
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;
    fx_t dirX = COS_FX(angle), dirY = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66)), planeY = FX_MUL(dirX, FX(0.66));
    uint8_t *fb = fb_pixels();
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    int eye = (int)SHARED_UC->eye_h;
    const fx_t PAR = FX(0.01);
    int jambs = SHARED_UC->hole_jamb;    /* TESTING>HOLEJAMB A/Bs the close-up work */
    /* ENCLOSURE. Fog law says close = lit, which is exactly wrong INSIDE a
     * shaft: as the camera closes on the plane the tunnel must wrap dark
     * around you with the light concentrated at the exit. Within 0.15 cells
     * of the face, the side/head/sill panels take up to +5 ramp steps of
     * dark (the back panel and peek take none -- they ARE the exit), and
     * the peek blit gains a centered ZOOM (up to ~1.9x) so the crawl reads
     * as entering the destination's viewpoint, not approaching a poster.
     * Both are zero at normal viewing distances. */
    fx_t cam_pp = g_exit_hole_axis ? py : px;
    fx_t cam_d = g_exit_hole_dir > 0 ? (g_exit_hole_plane - cam_pp)
                                     : (cam_pp - g_exit_hole_plane);
    int enc8 = 0, z256 = 256;
    if (cam_d > 0 && cam_d < FX(0.15)) {
        enc8 = (int)(((int64_t)(FX(0.15) - cam_d) * (5 * 256)) / FX(0.13));
        if (enc8 > 5 * 256) enc8 = 5 * 256;
        z256 = 256 + (int)(((int64_t)(FX(0.15) - cam_d) * 230) / FX(0.13));
    }
    int iz256 = (256 * 256) / z256;      /* peek zoom: sample window shrink */

    /* Column-clip: project the opening's two endpoints on the face plane. */
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    int bcA = col_start, bcB = col_end - 1;
    if (det != 0) {
        fx_t inv_det = fx_div_hw(FX_ONE, det);
        int rmin = col_end, rmax = col_start - 1, behind = 0;
        for (int e = 0; e < 2; e++) {
            fx_t wx = g_exit_hole_axis ? (g_exit_hole_c0 + (e ? HOLE_HW : -HOLE_HW))
                                       : g_exit_hole_plane;
            fx_t wy = g_exit_hole_axis ? g_exit_hole_plane
                                       : (g_exit_hole_c0 + (e ? HOLE_HW : -HOLE_HW));
            fx_t rx = wx - px, ry = wy - py;
            fx_t tY = FX_MUL(inv_det, FX_MUL(-planeY, rx) + FX_MUL(planeX, ry));
            if (tY < FX(0.2)) { behind = 1; break; }
            fx_t tX = FX_MUL(inv_det, FX_MUL(dirY, rx) - FX_MUL(dirX, ry));
            int sc = (SCREEN_W >> 1)
                   + (int)(((int64_t)(SCREEN_W >> 1) * fx_div_hw(tX, tY)) >> FX_SHIFT);
            if (sc < rmin) rmin = sc;
            if (sc > rmax) rmax = sc;
        }
        if (!behind) {
            bcA = rmin - 1 < col_start ? col_start : rmin - 1;
            bcB = rmax + 1 > col_end - 1 ? col_end - 1 : rmax + 1;
        }
    }
    if (bcA > bcB) return;

    for (int col = bcA; col <= bcB; col++) {
        fx_t camX = ((fx_t)(2 * col - SCREEN_W) << FX_SHIFT) / SCREEN_W;
        fx_t rdx = dirX + FX_MUL(planeX, camX);
        fx_t rdy = dirY + FX_MUL(planeY, camX);
        fx_t rdp = g_exit_hole_axis ? rdy : rdx;      /* toward the plane */
        fx_t rda = g_exit_hole_axis ? rdx : rdy;      /* along the face   */
        if (rdp < PAR && rdp > -PAR) continue;
        fx_t pp = g_exit_hole_axis ? py : px;
        fx_t pa = g_exit_hole_axis ? px : py;
        fx_t t = fx_div_hw(g_exit_hole_plane - pp, rdp);
        if (t <= PAR) continue;
        fx_t off = pa + FX_MUL(t, rda) - g_exit_hole_c0;   /* across the face */
        if (off < -HOLE_HW || off > HOLE_HW) continue;
        if (t >= WALL_DIST(col) + FX(0.05)) continue;      /* face occluded */

        /* Far plane (one cell deep), clamped to the cavity side the ray
         * drifts into so shallow views don't tunnel past the hole's width. */
        int side_hit = 0;
        fx_t t2_back = fx_div_hw(g_exit_hole_plane + g_exit_hole_dir * FX_ONE - pp, rdp);
        fx_t t2 = t2_back;
        if (rda > 64 || rda < -64) {
            fx_t adrift = rda < 0 ? -rda : rda;
            fx_t edge = (rda > 0) ? (HOLE_HW - off) : (off + HOLE_HW);
            if (edge < 0) edge = 0;
            fx_t ts = t + fx_div_hw(edge, adrift);
            if (ts < t2) { t2 = ts; side_hit = 1; }
        }

        int lh_n = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT), (uint32_t)t);
        int lh_f = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT), (uint32_t)t2);
        int wb_n = horizon_y + ((lh_n * eye) >> 8);
        int wb_f = horizon_y + ((lh_f * eye) >> 8);
        int hn = wb_n - ((lh_n * HOLE_Z1) >> 8), hf = wb_f - ((lh_f * HOLE_Z1) >> 8);
        int sn = wb_n - ((lh_n * HOLE_Z0) >> 8), sf = wb_f - ((lh_f * HOLE_Z0) >> 8);
        int head_lo = hn < hf ? hn : hf, head_hi = hn < hf ? hf : hn;
        int sill_lo = sn < sf ? sn : sf, sill_hi = sn < sf ? sf : sn;

        int bsh;                                   /* fog shade from the face */
        if (t < FX(2.5)) bsh = (int)((t * 2) / FX(2.5));
        else { fx_t past = t - FX(2.5); fx_t span = FOG_RAMP_DIST - FX(2.5);
               bsh = 2 + (int)((past * 13) / span); }
        /* CRAWLSPACE LAW. The interior no longer eases toward a manufactured
         * murk -- that spent the cavity's last third in a wide dithered
         * transition whose front floated mid-panel, anchored to an easing
         * curve instead of any edge (Mike's blue lines). Instead every
         * interior surface shades by the SAME fog curve as the rest of the
         * world, evaluated at its actual ray distance, with the film's
         * asymmetry kept as flat offsets (lit side +1, shadow side +5, back
         * run-in +2 -- rda sign picks the side). Steps land at fog-band
         * depths like a crawlspace duct: the darkness reads as the same air,
         * deeper. The only special dark left is the back panel's own. */
        int murk = HOLE_DARKEST - (bsh >> 2);
        if (murk < HOLE_DARKEST - 2) murk = HOLE_DARKEST - 2;
        /* Fog in 8.8: the continuous value BETWEEN the world's fog bands.
         * Where it sits between two bands, the 2x2 dither resolves it -- so
         * the mixing starts at the OUTER EDGE where the hole begins and
         * spreads evenly with depth (linear in the fog law, no ease), never
         * bunching into a floating front. The in-between Mike asked for:
         * crawlspace endpoints, dithered continuum across them. */
        int f0_8 = hole_fog8(t);               /* at the lip */
        int f2_8 = hole_fog8(t2);              /* at this column's far hit */
        int s_off = (side_hit && rda < 0) ? 1 : side_hit ? 5 : 2;
        int sv8 = f2_8 + (s_off << 8) + enc8;   /* enclosure wraps the shaft */
        /* Nothing but the BACK panel may reach murk: every other surface
         * stops short, so the corners around it stay corners. Enclosure
         * raises the ceiling of the fall along with the values. */
        int cap8 = ((murk - 3) << 8) + enc8;
        if (cap8 > 15 << 8) cap8 = 15 << 8;
        if (sv8 > cap8) sv8 = cap8;
        /* The head/sill fall toward fog at THIS COLUMN's far distance (t2:
         * the side exit over a side panel, the back plane over the back).
         * t2 is continuous across that column boundary, so the bands carry
         * no seam onto the panels below them -- one global back-plane
         * target with per-class clamps put a vertical shade cliff down the
         * head exactly at the back panel's corner column. The old seam rule
         * is now structural: over a side panel the band lands at the side's
         * own fog, one offset step apart, by construction. */
        const uint8_t *bay = hole_bayer + (col & 1);   /* this column's rows */
        #define HOLE_BAY(y_)  bay[((y_) & 1) << 1]
        uint8_t *base = fb + col;
        int y0, y1;
        /* Head underside: shadowed from the lip (film ref: the top reveal
         * gets no room light) -- a +3-step reveal shadow at the lip decaying
         * to the +2 run-in by the back, over the fog lerp. ONLY when the eye
         * is BELOW the lintel: from above, its underside faces away. */
        if (eye < HOLE_Z1) {
            int h = head_hi - head_lo; if (h < 1) h = 1;
            int kstep = (int)divu_u32((uint32_t)(256 << 8), (uint32_t)h);
            y0 = head_lo < 0 ? 0 : head_lo;
            y1 = head_hi > SCREEN_H - 1 ? SCREEN_H - 1 : head_hi;
            int kacc = kstep * (y0 - head_lo);      /* clamped-top catch-up */
            for (int y = y0; y <= y1; y++, kacc += kstep) {
                int k = kacc >> 8;                  /* 0..256 lip -> back */
                int s8 = f0_8 + (((f2_8 - f0_8) * k) >> 8)
                       + (3 << 8) + 3 * (256 - k) + enc8;   /* overhead light
                        * falls past the cut: interior +1 step vs the wall */
                if (s8 > cap8) s8 = cap8;
                base[y * SCREEN_W] = hole_shade(s8, HOLE_BAY(y));
            }
        }
        y0 = head_hi + 1 < 0 ? 0 : head_hi + 1;    /* the core */
        y1 = sill_lo - 1 > SCREEN_H - 1 ? SCREEN_H - 1 : sill_lo - 1;
        /* (The old VERTICAL JAMB branch is gone: its flat stepped strip ran a
         * visible vertical seam against the fog-continuum side panels (Mike's
         * blue lines). The side-panel formula already evaluates correctly at
         * jamb depths -- fog8 of a shallow ts is nearly the face shade -- so
         * one formula now covers the panel from the cut edge inward.) */
        if (!side_hit && y0 <= y1) {
            /* BACK WALL: no wallpaper — MYSTERY. Textureless gloom at the tail
             * of hole_ramp, checker-dithered against the step above it. The
             * only surface allowed this deep, which is what makes the corners
             * around it read as corners. */
            /* Not a flat oblong. Deepest in shadow at the top, lifted where
             * the sill bounces into its foot, and darker toward the side the
             * room's light does NOT come from — signed by the ray's own offset
             * across the aperture, the same quantity the side panels take
             * their lit/shadow sense from. Two steps of range in total, but
             * they are the steps that make it a SURFACE instead of a sticker. */
            int span = sill_lo - head_hi; if (span < 1) span = 1;
            int hx = (int)((off << 8) / HOLE_HW);       /* -256..+256 */
            /* DESTINATION PEEK: once the climb is committed the next map is
             * real and rendered, and the back panel becomes a window onto its
             * spawn POV -- the faint light below resolving into the place it
             * was hinting at. Scaled straight onto the far plane's projected
             * rect (hf..sf), so it swells as the crawl closes the distance. */
            /* GLOW accumulators run regardless: the glow is both the normal
             * look and the DISSOLVE floor the peek fades up from. The back
             * panel draws ONLY the hole's own cool run (140..143) -- never
             * the wall ramp. Deepest at the top, a FAINT interior light at
             * the foot biased to one side: light from something you cannot
             * see, the hint that the hole goes somewhere. */
            /* LIGHTER than it was: the panel's darkest is now deep-mid (142),
             * not the near-black 143 -- a shadowed room, not a void. The
             * deepest entry stays the ramp tail's business. */
            int lift = (1 << 8) + (((hx + 256) * 160) >> 9);   /* 256..416 */
            int kstep = (int)divu_u32((uint32_t)(256 << 8), (uint32_t)span);
            int kacc = kstep * (y0 - head_hi);
            /* DESTINATION PEEK: once the climb commits the next map is real
             * and rendered, and the back panel becomes a window onto its
             * spawn POV -- the glow resolving into the place it hinted at.
             * Scaled onto the far plane's rect (hf..sf) so it swells as the
             * crawl closes. It DISSOLVES in over the glow across its first
             * 4 frames (the flag carries its birth frame) -- the cut from
             * dark to image was too stark as a single-frame pop. */
            int plvl = 0;
            if (g_hole_peek_on) {
                plvl = (int)SHARED_UC->frame_count - (g_hole_peek_on - 1);
                if (plvl > 4) plvl = 4; else if (plvl < 0) plvl = 0;
            }
            if (plvl > 0) {
                /* Sample u by the ray's position on the BACK plane, not the
                 * face: from the room, only the centre of the aperture's
                 * rays reach the panel (the rest go side-wall), so a
                 * face-plane mapping only ever showed the bitmap's middle
                 * strip -- the centre column stretched wide, which is why
                 * every preview collapsed into horizontal bands. */
                fx_t offb = off + FX_MUL(t2_back - t, rda);
                int u = (int)(((offb + HOLE_HW) * PEEK_W) / (2 * HOLE_HW));
                /* off is WORLD lateral; for half the hole orientations
                 * world-left lands on screen-right, mirroring the bitmap
                 * against the (screen-space) corridor -- the lit wall
                 * flipped sides at the swap. The mirror condition is the
                 * complement of the captured lit-left flag. */
                if (!g_corr_lit_left) u = PEEK_W - 1 - u;
                /* Camera-push: zoom the sample toward the image's center --
                 * the destination view's own axis -- as the crawl closes. */
                u = (PEEK_W >> 1) + (((u - (PEEK_W >> 1)) * iz256) >> 8);
                if (u < 0) u = 0; else if (u > PEEK_W - 1) u = PEEK_W - 1;
                int denom = sf - hf; if (denom < 1) denom = 1;
                int vstep = (int)divu_u32((uint32_t)(PEEK_H << 8),
                                          (uint32_t)denom);
                int vacc = vstep * (y0 - hf);
                const uint8_t *pcol = peek_buf + u;
                for (int y = y0; y <= y1; y++, vacc += vstep, kacc += kstep) {
                    if (plvl >= 4 || ((y + (col << 1)) & 3) < plvl) {
                        int v = vacc >> 8;
                        v = (PEEK_H >> 1) + (((v - (PEEK_H >> 1)) * iz256) >> 8);
                        if (v < 0) v = 0; else if (v > PEEK_H - 1) v = PEEK_H - 1;
                        base[y * SCREEN_W] = pcol[v * PEEK_W];
                    } else {
                        int kk = (kacc >> 8) * (kacc >> 8) >> 8;
                        int v = ((2 << 8) - ((kk * lift) >> 8) + 128) >> 8;
                        if (v < 0) v = 0; else if (v > 3) v = 3;
                        base[y * SCREEN_W] = (uint8_t)(HOLE_DEEP_BASE + v);
                    }
                }
                goto back_done;
            }
            /* FOG HONOR. The panel's cool run is fixed CRAM, so distance fog
             * never touched it -- at range the hole glowed through the murk
             * as a blue patch. Crossfade (dithered, graded) into the plain
             * fogged tunnel tone starting around 3 cells; by ~3.6 the panel
             * IS fog. The cool-room look stays pure in the near zone. */
            int fogmix = f0_8 - (4 << 8);
            fogmix = fogmix <= 0 ? 0 : (fogmix >> 7);
            if (fogmix > 4) fogmix = 4;
            for (int y = y0; y <= y1; y++, kacc += kstep) {
                if (fogmix && ((y + (col << 1)) & 3) < fogmix) {
                    int s8 = f2_8 + (3 << 8); if (s8 > cap8) s8 = cap8;
                    base[y * SCREEN_W] = hole_shade(s8, HOLE_BAY(y));
                    continue;
                }
                int kk = (kacc >> 8) * (kacc >> 8) >> 8;   /* 0..256, eased */
                int v = ((2 << 8) - ((kk * lift) >> 8) + 128) >> 8;
                if (v < 0) v = 0; else if (v > 3) v = 3;
                base[y * SCREEN_W] = (uint8_t)(HOLE_DEEP_BASE + v);
            }
            back_done:;
        } else {
            /* CAVITY SIDE WALL: the same fade, one step darker so the
             * side/back junction reads as a corner. No wallpaper — the chevron
             * belongs to the room's skin, and inside the cut it read as a
             * pattern that had no business being there. Plus the vertical AO,
             * without which this panel is one flat value top to bottom. */
            int acc = sv8 + (jambs ? (1 << 8) : 0);
            if (jambs) {
                int span = sill_lo - head_hi; if (span < 1) span = 1;
                int astep = -(int)divu_u32((uint32_t)((AO_TOP - AO_BOT) << 8),
                                           (uint32_t)span);
                acc += (AO_TOP << 8) + astep * (y0 - head_hi);
                for (int y = y0; y <= y1; y++, acc += astep)
                    base[y * SCREEN_W] = hole_shade(acc, HOLE_BAY(y));
            } else {
                for (int y = y0; y <= y1; y++)
                    base[y * SCREEN_W] = hole_shade(acc, HOLE_BAY(y));
            }
        }
        /* Sill: lit at the near lip (sill_hi), fog-dark by sill_lo — the
         * same fog lerp as the head, minus the reveal shadow (the ledge
         * catches the room's light). ONLY when the eye is ABOVE the ledge:
         * the lo/hi sort below makes the band unconditional, so a CROUCHED
         * eye under the sill plane got the ledge's TOP hallucinated through
         * the solid wall — Mike's dashed strips crawling the screen bottom
         * (the projection flips and half-res leaves gaps between columns). */
        if (eye > HOLE_Z0) {
            int h = sill_hi - sill_lo; if (h < 1) h = 1;
            int kstep = (int)divu_u32((uint32_t)(256 << 8), (uint32_t)h);
            y0 = sill_lo < 0 ? 0 : sill_lo;
            y1 = sill_hi > SCREEN_H - 1 ? SCREEN_H - 1 : sill_hi;
            int kacc = kstep * (sill_hi - y0);     /* 0 at the near lip */
            for (int y = y0; y <= y1; y++, kacc -= kstep) {
                int k = kacc >> 8;                 /* 0..256 lip -> back */
                int s8 = f0_8 + (((f2_8 - f0_8) * k) >> 8) + (3 << 8) + enc8;
                if (s8 > cap8) s8 = cap8;
                base[y * SCREEN_W] = hole_shade(s8, HOLE_BAY(y));
            }
        }
        #undef HOLE_BAY
    }
}

/* Crawlspace tail: the low-ceiling slab + its bulkhead caps for a column range.
 * Both passes z-test the combined WALL_DIST (filled by both halves' wall pass),
 * so this runs AFTER the wall barrier. Split across both SH-2s via CMD_TAIL —
 * the primary calls [0,split), the secondary [split,SCREEN_W) — so the
 * crawlspace cost (which spiked single-CPU) is shared instead of serial. */
void raycast_draw_tail(int col_start, int col_end) {
    /* One single-height plane + its boundary caps per ceiling height present.
     * Crawl (135) always; bulkheads (200) only when the map has any. The caps
     * are the vertical faces that frame each slab's edges (slab height -> full
     * ceiling), so a bulkhead reads as a solid soffit, not a floating patch. */
    for (int i = 0; i < n_ceil_hs; i++) {
        int h = ceil_hs[i];
        /* TESTING>BULKHEAD A/B: OFF skips every non-crawl height's pass-pair. */
        if (h != CRAWL_CEIL_H && SHARED_UC->bulk_kill) continue;
        raycast_draw_low_ceiling(col_start, col_end, h);
        raycast_draw_ceil_caps(col_start, col_end, h);
    }
    draw_exit_hole(col_start, col_end);
}

/* Carpet wear pass — stamps dark "stains" across the floor (bottom
 * half of screen) at world-position-hashed locations. Reads player
 * from the shared snapshot so the secondary can run it alongside the
 * ceiling grid pass on the top half (disjoint framebuffer regions,
 * no race). */
RAMTEXT void raycast_draw_carpet(int col_start, int col_end) {
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;

    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    fx_t leftDirX  = dirX - planeX;
    fx_t leftDirY  = dirY - planeY;
    fx_t rightDirX = dirX + planeX;
    fx_t rightDirY = dirY + planeY;
    if (SHARED_UC->ultra_twin == 2) {   /* ULTRA pass B: half-column shift (merged statically) */
        fx_t jx = FX_MUL(planeX, ULTRA_JIT_FX), jy = FX_MUL(planeY, ULTRA_JIT_FX);
        leftDirX += jx; rightDirX += jx;
        leftDirY += jy; rightDirY += jy;
    }

    uint8_t *fb = fb_pixels();
    /* horizon_y is the on-screen dividing line between ceiling and floor;
     * shifts with pitch. focal_const stays at SCREEN_H/2 (the
     * camera-height·focal-length product in the perspective formula) so
     * depth-per-row remains calibrated when the camera pitches. */
    int horizon_y   = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    /* Floor depth scales with focal·eyeH: crouch -> the carpet comes up
     * close, stains land at the nearer world distance, in step with the
     * wall floor-edge. Both CPUs read eye_h, keeping the secondary's half aligned. */
    const int focal_const = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    /* sample_bias rebases row_color sampling so its color gradient
     * (and the stain-LOD derived from base_shade) travels with the
     * shifted horizon. Without this the gradient stayed glued to
     * absolute Y while the geometry math moved — visible as static
     * stain density bands and a non-perspective color band when
     * tilting up or down. The crouch term (matching raycast_clear_half)
     * slides the carpet tone gradient with eye height: lower eye => more
     * bright carpet before the fade, geometry untouched. */
    int geo_bias    = SCREEN_H / 2 - horizon_y;        /* unshifted: skip + LOD */
    int sample_bias = geo_bias + CROUCH_GRAD_SHIFT(SHARED_UC->eye_h);
    /* THE CENTER SEAM: the ceiling pass stops BEFORE horizon_y and this
     * loop starts AFTER it (both would divide by zero at p=0), so the
     * horizon row itself was never painted — a 1px stale line across the
     * screen center, visible wherever no wall covers the horizon: across
     * the fog band, floating just above distant see-over counters.
     * Paint it the deepest fog shade; walls and bands overdraw it. */
    if (horizon_y >= 0 && horizon_y < SCREEN_H) {
        uint8_t *hp = fb + (uintptr_t)horizon_y * SCREEN_W + col_start;
        uint8_t fogc = (uint8_t)(FLOOR_BASE + SHADE_LEVELS - 1);
        for (int c = col_start; c < col_end; c++) *hp++ = fogc;
    }
    /* VERT: floor-cast only the even rows (line table duplicates them down).
     * Halves the per-row DIVU + stain stamps. Start on the first even row past
     * the horizon so the even grid matches the wall/clear passes. */
    /* CARPET VERTICAL DEPTH LOD. vstep is the blunt all-rows VERT toggle; cystep
     * is the PER-ROW step, doubled once a row's fog shade reaches the mid band --
     * the same geo_shade banding x_step already uses (4/8/16), applied to the
     * other axis. Every row pays a DIVU plus ~6 muls of setup before a single
     * stain lands, so a far row was paying full setup to place ~9 samples;
     * x_step cannot touch that, this can. Measured with the blunt VERT toggle
     * (all rows): carpet 2,068 -> 888 ticks, -57%.
     *
     * cystep is reassigned at the TOP of each iteration, so the two `continue`s
     * below advance by THIS row's step rather than a stale one -- which also
     * means fog-skipped far rows skip two at a time. */
    #define CARPET_VLOD_SHADE 4
    const int vstep = SHARED_UC->wall_vert ? 2 : 1;
    const int vlod  = SHARED_UC->carpet_vlod;
    int cov_lo, cov_hi;
    covered_rows(col_start, col_end, &cov_lo, &cov_hi);
    int cystep = vstep;
    int y0 = (vstep == 2) ? ((horizon_y + 2) & ~1) : (horizon_y + 1);
    for (int y = y0; y < SCREEN_H; y += cystep) {
        cystep = vstep;
        if (y < 0) continue;        /* extreme positive pitch */
        /* Skip + LOD key off the geometric (unshifted) distance so far rows
         * stay fog-skipped. The crouch color shift only brightens what we DO
         * draw — it must NOT un-skip the aliased near-horizon rows (that was
         * the noisy "broken horizon" band). */
        int gy = y + geo_bias;
        if (gy < 0)         gy = 0;
        if (gy >= SCREEN_H) gy = SCREEN_H - 1;
        int geo_shade = row_color[gy] - FLOOR_BASE;
        /* Vertical LOD band. Set BEFORE the fog-skip below so skipped rows also
         * advance two at a time. Keyed to geo (unshifted) shade for the same
         * reason x_step is: crouch must not un-band the near-horizon rows. */
        if (vlod && geo_shade >= CARPET_VLOD_SHADE) cystep = vstep * 2;
        if (geo_shade >= SHADE_LEVELS - 2) continue;
        if (y >= cov_lo && y <= cov_hi) continue;   /* buried by walls in every column */
        int sy = y + sample_bias;
        if (sy < 0)         sy = 0;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        int base_shade = row_color[sy] - FLOOR_BASE;   /* shifted: stain brightness */
        /* The row the vertical LOD is about to skip, or 0 when it isn't
         * skipping. The STAIN pass below is happy to be thinned — sparser dots
         * just read as lighter wear. A SOLID zone fill is not: the skipped row
         * keeps the lit gradient underneath and the whole zone stripes from the
         * LOD band outward. So the two zone fills paint the pair. Only the
         * stores double; the per-row DIVU and setup the LOD exists to skip are
         * still skipped. */
        int lod_pair = (cystep > vstep && y + vstep < SCREEN_H) ? vstep : 0;

        int p = y - horizon_y;
        /* rowDist always positive (y > horizon_y); DIVU. */
        fx_t rowDist = (fx_t)divu_u32((uint32_t)((fx_t)focal_const << FX_SHIFT),
                                      (uint32_t)p);
        fx_t worldX = px + FX_MUL(rowDist, leftDirX);
        fx_t worldY = py + FX_MUL(rowDist, leftDirY);
        fx_t stepX  = FX_MUL(rowDist, rightDirX - leftDirX) / SCREEN_W;
        fx_t stepY  = FX_MUL(rowDist, rightDirY - leftDirY) / SCREEN_W;
        uint8_t dark_c = (uint8_t)(FLOOR_BASE + base_shade + 2);

        /* Distance-based screen-stamp step (LOD). Each carpet stain is
         * defined in world space — the screen step is just how often we
         * sample. At distance, fewer screen pixels cover the same world
         * area so we can sample sparser without losing visible density.
         *   base_shade 0-3 (close):  every 4th  px (densest, normal pass)
         *   base_shade 4-7 (mid):    every 8th  px
         *   base_shade 8+  (far):    every 16th px
         * Saves ~40% of carpet pass work on a typical scene where most
         * rows are mid/far range. */
        int x_step = 4;
        if      (geo_shade >= 8) x_step = 16;
        else if (geo_shade >= 4) x_step = 8;
        fx_t stepWX = stepX * x_step;
        fx_t stepWY = stepY * x_step;

        /* Pre-advance worldX/Y to col_start so this CPU only does the
         * iterations covering its column range. col_start is a multiple
         * of 4; we round it down to a multiple of x_step too. */
        int x_start = (col_start / x_step) * x_step;
        if (x_start < col_start) x_start += x_step;
        int skip = x_start / x_step;
        worldX += stepWX * skip;
        worldY += stepWY * skip;
        for (int x = x_start; x < col_end; x += x_step) {
            int wx = (int)(worldX >> 13) & 0xFF;
            int wy = (int)(worldY >> 13) & 0xFF;
            int hash = (wx * 73 + wy * 31) & 0xF;
            if (hash < 6) fb[y * SCREEN_W + x] = dark_c;
            worldX += stepWX;
            worldY += stepWY;
        }

        /* DARK ROOM floor: no fixtures overhead, so the carpet reads near fog.
         * Same shape as the crawlspace block below — reject the row by bbox,
         * clip to the columns the zone actually spans, then fill at the
         * carpet's own LOD. g_dark_active means a lit map never enters. */
        if (g_dark_active && !SHARED_UC->unlit_kill) {
            fx_t wx0 = px + FX_MUL(rowDist, leftDirX);
            fx_t wy0 = py + FX_MUL(rowDist, leftDirY);
            fx_t wxR = px + FX_MUL(rowDist, rightDirX);
            fx_t wyR = py + FX_MUL(rowDist, rightDirY);
            fx_t minx = wx0 < wxR ? wx0 : wxR, maxx = wx0 < wxR ? wxR : wx0;
            fx_t miny = wy0 < wyR ? wy0 : wyR, maxy = wy0 < wyR ? wyR : wy0;
            if (!(maxx < g_dark_x0 || minx > g_dark_x1 ||
                  maxy < g_dark_y0 || miny > g_dark_y1)) {
                int cA = col_start, cB = col_end - 1;
                const fx_t EPS = 16;
                if (stepX > EPS || stepX < -EPS) {
                    int a = (int)(fx_div_hw(g_dark_x0 - wx0, stepX) >> FX_SHIFT);
                    int b = (int)(fx_div_hw(g_dark_x1 - wx0, stepX) >> FX_SHIFT);
                    if (a > b) { int t = a; a = b; b = t; }
                    if (a > cA) cA = a; if (b < cB) cB = b;
                }
                if (stepY > EPS || stepY < -EPS) {
                    int a = (int)(fx_div_hw(g_dark_y0 - wy0, stepY) >> FX_SHIFT);
                    int b = (int)(fx_div_hw(g_dark_y1 - wy0, stepY) >> FX_SHIFT);
                    if (a > b) { int t = a; a = b; b = t; }
                    if (a > cA) cA = a; if (b < cB) cB = b;
                }
                if (cA < col_start) cA = col_start;
                if (cB > col_end - 1) cB = col_end - 1;
                int dsh = base_shade + DARK_ROOM_SHADE;
                if (dsh > SHADE_LEVELS - 1) dsh = SHADE_LEVELS - 1;
                uint8_t ddk = (uint8_t)(FLOOR_BASE + dsh);
                /* 2px fill keeps the dark/lit floor boundary fine (4px staircased
                 * it). The real win is skipping cell_is_dark when there's a single
                 * dark room: the column-clip above already bounds us to its bbox,
                 * so every column in range is dark. Only disjoint dark rooms (union
                 * bbox spans the gaps) need the per-column test. */
                int test_dark = (g_map_n_dark > 1);
                int c0 = cA & ~1, c1 = cB | 1;
                if (c1 > col_end - 1) c1 = col_end - 1;
                uint8_t *drow = fb + y * SCREEN_W;
                fx_t dwx = wx0 + stepX * c0, dwy = wy0 + stepY * c0;
                /* CELL-RUN walk (UNLITF A/B, 2026-08-09: the zone fills were
                 * 6,383 of the crawl's R:8,582 — dominated by a grid test per
                 * 2px pair). A cell spans many pairs at any depth, so retest
                 * only when the pair CROSSES a cell boundary and ride the
                 * verdict across the run. Exact per-cell result, ~6-15x fewer
                 * grid reads. */
                int dcx = -0x7000, dcy = -0x7000, don = !test_dark;
                for (int col = c0; col <= c1; col += 2, dwx += stepX * 2, dwy += stepY * 2) {
                    if (test_dark) {
                        int ncx = FX_INT(dwx), ncy = FX_INT(dwy);
                        if (ncx != dcx || ncy != dcy) {
                            dcx = ncx; dcy = ncy;
                            don = cell_is_dark(dwx, dwy);
                        }
                        if (!don) continue;
                    }
                    *(uint16_t *)(drow + col) = WDUP(ddk);
                    if (lod_pair)
                        *(uint16_t *)(drow + lod_pair * SCREEN_W + col) = WDUP(ddk);
                }
            }
        }
        /* Lightless crawlspace floor: cells under a low (unlit) ceiling get no
         * light from above, so darken the carpet there. Column-clip to the zone
         * bbox like the slab so it stays cheap; the carpet draws before the
         * walls, so they overpaint it — no z-test. Re-stamps the stain pattern
         * (a yet-darker shade) so the carpet stains survive into the shade. */
        if (g_lowceil_active && !SHARED_UC->unlit_kill) {
            fx_t wx0 = px + FX_MUL(rowDist, leftDirX);
            fx_t wy0 = py + FX_MUL(rowDist, leftDirY);
            fx_t wxR = px + FX_MUL(rowDist, rightDirX);
            fx_t wyR = py + FX_MUL(rowDist, rightDirY);
            fx_t zx0 = g_lowceil_x0, zx1 = g_lowceil_x1;
            fx_t zy0 = g_lowceil_y0, zy1 = g_lowceil_y1;
            fx_t minx = wx0 < wxR ? wx0 : wxR, maxx = wx0 < wxR ? wxR : wx0;
            fx_t miny = wy0 < wyR ? wy0 : wyR, maxy = wy0 < wyR ? wyR : wy0;
            if (!(maxx < zx0 || minx > zx1 || maxy < zy0 || miny > zy1)) {
                int cA = col_start, cB = col_end - 1;
                const fx_t EPS = 16;
                if (stepX > EPS || stepX < -EPS) {
                    int a = (int)(fx_div_hw(zx0 - wx0, stepX) >> FX_SHIFT);
                    int b = (int)(fx_div_hw(zx1 - wx0, stepX) >> FX_SHIFT);
                    if (a > b) { int t = a; a = b; b = t; }
                    if (a > cA) cA = a; if (b < cB) cB = b;
                }
                if (stepY > EPS || stepY < -EPS) {
                    int a = (int)(fx_div_hw(zy0 - wy0, stepY) >> FX_SHIFT);
                    int b = (int)(fx_div_hw(zy1 - wy0, stepY) >> FX_SHIFT);
                    if (a > b) { int t = a; a = b; b = t; }
                    if (a > cA) cA = a; if (b < cB) cB = b;
                }
                if (cA < col_start) cA = col_start;
                if (cB > col_end - 1) cB = col_end - 1;
                int fsh = base_shade + 3; if (fsh > SHADE_LEVELS - 1) fsh = SHADE_LEVELS - 1;
                int fss = fsh + 2; if (fss > SHADE_LEVELS - 1) fss = SHADE_LEVELS - 1;
                uint8_t fdk = (uint8_t)(FLOOR_BASE + fsh);   /* dark floor */
                uint8_t fst = (uint8_t)(FLOOR_BASE + fss);   /* darker stain */
                /* HALF-RES like the slab: test one column, word-store the pair.
                 * The dark flat floor hides the horizontal chunkiness, and it
                 * halves the uncached FB writes + per-pixel tests. (F:17 in tunnels
                 * already — no need to go chunkier than this.) */
                int xmask = x_step - 1;   /* stain-test only at the carpet's LOD (cheap) */
                int c0 = cA & ~1, c1 = cB | 1;
                if (c1 > col_end - 1) c1 = col_end - 1;
                uint8_t *frow = fb + y * SCREEN_W;
                fx_t fwx = wx0 + stepX * c0, fwy = wy0 + stepY * c0;
                /* CELL-RUN walk — same trade as the dark-room block above:
                 * ceil_is_low re-evaluated only on cell-boundary crossings,
                 * its verdict ridden across the run. */
                int fcx = -0x7000, fcy = -0x7000, fon = 0;
                for (int col = c0; col <= c1; col += 2, fwx += stepX * 2, fwy += stepY * 2) {
                    int ncx = FX_INT(fwx), ncy = FX_INT(fwy);
                    if (ncx != fcx || ncy != fcy) {
                        fcx = ncx; fcy = ncy;
                        fon = ceil_is_low(fwx, fwy);
                    }
                    if (!fon) continue;
                    uint8_t cv = fdk;
                    if ((col & xmask) == 0) {
                        int hwx = (int)(fwx >> 13) & 0xFF;
                        int hwy = (int)(fwy >> 13) & 0xFF;
                        if (((hwx * 73 + hwy * 31) & 0xF) < 6) cv = fst;   /* same hash as the carpet */
                    }
                    uint16_t fv = ((uint16_t)cv << 8) | cv;   /* col & col+1 */
                    *(uint16_t *)(frow + col) = fv;
                    if (lod_pair)                             /* see lod_pair above */
                        *(uint16_t *)(frow + lod_pair * SCREEN_W + col) = fv;
                }
            }
        }
    }
}

/* Wall column pass — DDA, perspective-correct textured columns, fog
 * cutoff, distance-based detail falloff. Caller-supplied half-open
 * column range [col_start, col_end) lets the primary and secondary divide
 * the screen — primary does [0, SCREEN_W/2), secondary does [SCREEN_W/2,
 * SCREEN_W). Writes the per-column z-buffer (WALL_DIST) through the
 * cache-through alias so the sprite passes on primary see secondary's
 * writes after the COMM4 sync. Reads player from SHARED_UC. */

/* Draw ONE door column as the wall (replaces the chevron). Kept OUT of
 * raycast_draw_walls on purpose: that function is the hot wall loop and must
 * fit the SH-2's 4 KB instruction cache. Inlining the ~3 KB of door code
 * blew draw_walls past the cache and thrashed EVERY column every frame. As a
 * separate function the door code only loads into I-cache on the handful of
 * columns a door actually covers. `along` = the hit's offset across the door
 * footprint (0..2*DECAL_DOOR_HW). Renders the static frame/sign, the swinging
 * leaf, and the one-cell recess behind it. */

RAMTEXT __attribute__((noinline)) static void
draw_door_column(uint8_t *fb, int col, int hr, fx_t along, int flip,
                 int drawStart, int drawEnd, int wall_top,
                 int draw_lineHeight, fx_t perpDist, int wall_shade,
                 int horizon_y, int eye_h, fx_t side_d, fx_t back_d,
                 fx_t drift, fx_t cell_lo, fx_t dpu) {
    /* The door footprint stays full width — frame, hinge edge and the EXIT
     * sign are painted flat on the wall and never move. `flip` mirrors the whole
     * render (texture + swing) when the door is viewed from the side that the
     * world->screen mapping would otherwise reverse, so it reads the same way
     * (handle, sign, hinge) on every wall orientation. */
    int otx = (int)(((int64_t)along * DOOR_TEX_WIDTH) / (2 * DECAL_DOOR_HW));
    if (otx < 0) otx = 0; else if (otx >= DOOR_TEX_WIDTH) otx = DOOR_TEX_WIDTH - 1;
    if (flip) otx = DOOR_TEX_WIDTH - 1 - otx;
    const uint8_t *col_base = door_tex_ram[otx];   /* static (frame/sign) column — SDRAM-staged */

    /* DISTANCE/LIGHT FADE: like the walls + the outlet decal, the door darkens
     * with wall_shade — each material ramp shifts down by `door_shade`. The 9-step
     * grey ramp (4 extra-dark + 5 door) gives the body its fade range; the frame,
     * handle and (close-up) stipple fold in too. door_shade tracks ~half the wall
     * shade so the door fogs at a similar visual rate. */
    /* FINE 14-step grey ramp: the 4 extra-darks, then the soft stipple shade and
     * the door shade interleaved (stipple sits HALF a step below its door shade).
     * Body texel g -> index 5+2g; its stipple -> 4+2g (one fine step softer). The
     * fade subtracts 2 per door_shade, so the stipple stays soft AND fogs WITH the
     * body at every distance. */
    static const uint8_t finegrey[14] = {
        DOOR_DARK_BASE + 0, DOOR_DARK_BASE + 1, DOOR_DARK_BASE + 2, DOOR_DARK_BASE + 3,
        STIPPLE_BASE + 0, DOOR_BASE + 0, STIPPLE_BASE + 1, DOOR_BASE + 1,
        STIPPLE_BASE + 2, DOOR_BASE + 2, STIPPLE_BASE + 3, DOOR_BASE + 3,
        STIPPLE_BASE + 4, DOOR_BASE + 4 };
    int door_shade = wall_shade >> 1; if (door_shade > 6) door_shade = 6;
    int sh2 = door_shade * 2;

    uint8_t dlut[19];
    dlut[0] = (uint8_t)(WALL_BASE + wall_shade);                 /* surround = wall */
    for (int g = 0; g < 5; g++) {
        int bi = 5 + 2 * g - sh2; if (bi < 0) bi = 0;            /* body grey, faded */
        int si = 4 + 2 * g - sh2; if (si < 0) si = 0;            /* stipple (1 fine step softer) */
        dlut[1 + g] = finegrey[bi];
        dlut[9 + g] = finegrey[si];
    }
    /* EXIT sign FOGS with distance now (was kept lit — it glowed on a fogged
     * door). sg = door_shade halved into the 4-step sign ramps; dark-green
     * texel sits one step deeper than light-green. */
    int sg = door_shade >> 1; if (sg > 3) sg = 3;
    int sgd = sg + 1; if (sgd > 3) sgd = 3;
    dlut[6] = (uint8_t)(SIGN_GREEN_BASE + sgd);                  /* dark-green letter */
    dlut[7] = (uint8_t)(SIGN_GREEN_BASE + sg);                   /* light-green letter */
    dlut[8] = (uint8_t)(SIGN_WHITE_BASE + sg);                  /* white plate */
    for (int i = 0; i < 4; i++) {
        int hp = i - door_shade; if (hp < 0) hp = 0;
        dlut[14 + i] = (uint8_t)(HANDLE_BASE + hp);              /* bronze handle, faded */
    }
    { int fp = 4 - door_shade; if (fp < 0) fp = 0;
      dlut[18] = (uint8_t)(FRAME_BASE + fp); }                  /* muted-brown jamb, faded */

    int dspan = draw_lineHeight; if (dspan < 1) dspan = 1;
    fx_t ty_step = ((fx_t)DOOR_TEX_HEIGHT << FX_SHIFT) / dspan;
    fx_t ty = (fx_t)(drawStart - wall_top) * ty_step;
    uint8_t *pd = (uint8_t *)fb + col + drawStart * SCREEN_W;

    /* CLOSED door (the common "just looking at it" view): pure static texture —
     * skip ALL the leaf/recess setup (several divides) and the per-pixel region
     * test, run a tight column loop. */
    int dopen = (int)SHARED_UC->door_open;
    if (dopen == 0) {
        for (int y = drawStart; y <= drawEnd; y++) {
            int oty = (int)(ty >> FX_SHIFT);
            if (oty >= DOOR_TEX_HEIGHT) oty = DOOR_TEX_HEIGHT - 1;
            uint8_t c8 = dlut[col_base[oty]];
            if (hr >= 2) *(uint32_t *)pd = LDUP(c8); else if (hr) *(uint16_t *)pd = WDUP(c8); else *pd = c8;
            pd += SCREEN_W;
            ty += ty_step;
        }
        return;
    }

    /* OPEN: the leaf swings and the recess shows. Per-column setup (the divides)
     * is paid only while the door is actually moving/open. Hinge at LEAF_X1,
     * latch at LEAF_X0; the leaf compresses toward the hinge and tapers toward
     * the receding latch, the recess showing through where it pulls away. */
    int leaf_col = (otx >= LEAF_X0 && otx <= LEAF_X1);
    int void_leaf = 0;
    const uint8_t *leaf_base = col_base;
    int leaf_cy = (LEAF_Y0 + LEAF_Y1) / 2;
    int leaf_half = (LEAF_Y1 - LEAF_Y0) / 2;
    int leaf_halff = leaf_half;
    fx_t leaf_rscale = FX(1.0);
    if (leaf_col) {
        int leaf_w = LEAF_X1 - LEAF_X0;
        int latch_now = LEAF_X1 - leaf_w * (DOOR_OPEN_MAX - dopen) / DOOR_OPEN_MAX;
        if (otx < latch_now) {
            void_leaf = 1;
        } else {
            int vis_w = LEAF_X1 - latch_now; if (vis_w < 1) vis_w = 1;
            int src_lx = LEAF_X0 + (otx - latch_now) * leaf_w / vis_w;
            if (src_lx < LEAF_X0) src_lx = LEAF_X0;
            else if (src_lx > LEAF_X1) src_lx = LEAF_X1;
            leaf_base = door_tex_ram[src_lx];
            int p_num = LEAF_X1 - otx;
            fx_t openf = ((fx_t)dopen << FX_SHIFT) / DOOR_OPEN_MAX;
            fx_t taper = FX_MUL(openf, FX(0.45));
            fx_t fore = FX(1.0) - (fx_t)(((int64_t)p_num * taper) / vis_w);
            leaf_halff = (int)(((int64_t)leaf_half * fore) >> FX_SHIFT);
            if (leaf_halff < 1) leaf_halff = 1;
            leaf_rscale = ((fx_t)leaf_half << FX_SHIFT) / leaf_halff;
        }
    }

    /* Frame-edge columns (the jambs, outside the swinging leaf) never show the
     * recess and never move — pure static texture. Run the same tight loop as
     * the closed door and skip the recess + leaf-LUT setup (a divide + a whole
     * table build) entirely. */
    if (!leaf_col) {
        for (int y = drawStart; y <= drawEnd; y++) {
            int oty = (int)(ty >> FX_SHIFT);
            if (oty >= DOOR_TEX_HEIGHT) oty = DOOR_TEX_HEIGHT - 1;
            uint8_t c8 = dlut[col_base[oty]];
            if (hr >= 2) *(uint32_t *)pd = LDUP(c8); else if (hr) *(uint16_t *)pd = WDUP(c8); else *pd = c8;
            pd += SCREEN_W;
            ty += ty_step;
        }
        return;
    }

    /* REAL recess: the door cell's actual interior. Which face this column's
     * ray sees is a fact the DDA already computed: at the door-plane hit,
     * sideDist holds the NEXT crossing per axis, and inside a one-cell cavity
     * the next along-axis crossing IS the cavity side wall and the next
     * depth-axis crossing IS the back wall. side_d/back_d are those two,
     * passed in free -- whichever is nearer is the surface (no divide, no
     * tie-race: the old recomputed t_c tied against exit_d per column and
     * lost at random, interleaving side/back columns into stripe noise).
     * Only the thin wood REVEAL still needs its ray-vs-plane divide: the
     * first DOOR_REVEAL_D of thickness apertures at DOOR width, so a ray
     * crossing the FOOTPRINT edge that early hits the frame, not wallpaper. */
    fx_t rec_d = back_d;                      /* depth of the surface we see */
    int  jamb  = 0;
    if (drift > 64 || drift < -64) {          /* near-parallel: never meets the frame */
        fx_t adrift = drift < 0 ? -drift : drift;
        fx_t edge_f = (drift > 0) ? (2 * DECAL_DOOR_HW - along) : along;
        if (edge_f < 0) edge_f = 0;
        fx_t t_f = perpDist + fx_div_hw(edge_f, adrift);
        if (t_f - perpDist < DOOR_REVEAL_D && t_f < side_d && t_f < back_d) {
            rec_d = t_f; jamb = 1;            /* wood reveal */
        }
    }
    if (jamb == 0 && side_d < back_d) {
        rec_d = side_d; jamb = 2;             /* cavity side wall */
    }
    if (rec_d < FX(0.1)) rec_d = FX(0.1);
    int back_h  = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT), (uint32_t)rec_d);
    /* Floor line at the eye-split below the horizon, ceiling above -- honors
     * free-look (horizon_y = pitch) AND crouch (eye_h), same as real walls. */
    int rec_bot = horizon_y + ((back_h * eye_h) >> 8);
    int rec_top = rec_bot - back_h;

    /* Flat-shaded surfaces (no texture): ceiling tile above, the cell's far
     * face (or the jamb's FRAME wood) ahead, carpet below. Depth-shaded so a
     * near-open door reads brighter inside than one across the room. */
    int rsh = RECESS_SHADE_BASE + door_shade;
    if (rsh > SHADE_LEVELS - 3) rsh = SHADE_LEVELS - 3;
    uint8_t rec_wall = 0, rec_wall_b = 0, rec_wall_dk = 0;
    int ao_top = 0, ao_bot = 0;
    const uint8_t *bw_col = 0;
    fx_t bw_step = 0;
    if (jamb == 1) {
        /* CHAIR-STYLE form shading for the reveal -- the flat one-colour band
         * read as a shapeless brown blob. Three cues, all off the true
         * geometry, dithered (checker, like the chair shadow) between the
         * ramp's fine steps so nothing bands:
         *   depth AO   -- q quarter-steps into the reveal darken it inward
         *                 (recess ambient occlusion; gradient across the band)
         *   header AO  -- the top eighth sits in the head jamb's shadow
         *   contact AO -- the bottom sliver darkens where it meets the carpet */
        int fp = 4 - door_shade; if (fp < 2) fp = 2;    /* room to dip below */
        int q = (int)(((rec_d - perpDist) * (4 * FX_ONE / DOOR_REVEAL_D))
                      >> FX_SHIFT);
        if (q < 0) q = 0; else if (q > 3) q = 3;
        int fa = fp - (q >> 1);
        int fb = fa - (q & 1); if (fb < 0) fb = 0;      /* dither partner */
        rec_wall    = (uint8_t)(FRAME_BASE + fa);        /* jamb: door-frame wood */
        rec_wall_b  = (uint8_t)(FRAME_BASE + fb);
        rec_wall_dk = (uint8_t)(FRAME_BASE + (fa > 0 ? fa - 1 : 0));
        int rh = back_h;
        ao_top = rec_top + (rh >> 3);
        ao_bot = rec_bot - (rh >> 4);
    } else {
        /* The cell's interior wears the CHEVRON WALLPAPER -- the "inverse cell"
         * read: through the doorway you see the room's own skin, not a flat
         * band. Back face (jamb 0): the ray's exit position in CELL space
         * (along - cell_lo = the world-fraction across the cell, same anchor
         * as the normal wall pass). Door-footprint space went negative left
         * of the door and the old clamp pinned that whole span to texture
         * column 0 -- a band of identical columns reading as horizontal
         * stripes. Cavity side wall (jamb 2): the ray's WORLD position along
         * the depth axis, mod one cell. The door plane sits on an integer
         * grid line, so depth * dpu (the depth-axis rayDir component) IS the
         * world fraction; the fx mask handles either approach sign. The old
         * perp-space depth stretched the pattern by 1/|dpu| with view angle.
         * Vertical maps the full wall tiling over the true projected height. */
        fx_t ax = (jamb == 2)
            ? (FX_MUL(rec_d - perpDist, dpu) & (FX_ONE - 1))
            : (along - cell_lo + FX_MUL(drift, rec_d - perpDist));
        if (ax < 0) ax = 0;
        int btx = (int)(((uint32_t)ax * (WALL_TEX_HI_WIDTH * WALL_TILE_HI_X))
                        >> FX_SHIFT) & (WALL_TEX_HI_WIDTH - 1);
        bw_col  = wall_tex_hi_ram[btx];
        bw_step = (fx_t)divu_u32(
            (uint32_t)((WALL_TEX_HI_HEIGHT * WALL_TILE_HI_Y) << FX_SHIFT),
            (uint32_t)back_h);
    }

    /* Leaf LUT: the swinging LEAF dims as it rotates into the recess (leaf_darken)
     * AND with distance/light (door_shade) — both shift down the shared grey ramp.
     * Only the leaf is shaded here; the static frame/sign use dlut. */
    int leaf_darken = (dopen * 4) / DOOR_OPEN_MAX;       /* 0 shut .. 4 wide open */
    int lt2 = (leaf_darken + door_shade) * 2;            /* swing + distance, fine steps */
    uint8_t llut[19];
    llut[0] = (uint8_t)(WALL_BASE + wall_shade);         /* surround (rare in the leaf) */
    for (int g = 0; g < 5; g++) {
        int bi = 5 + 2 * g - lt2; if (bi < 0) bi = 0;
        int si = 4 + 2 * g - lt2; if (si < 0) si = 0;
        llut[1 + g] = finegrey[bi];
        llut[9 + g] = finegrey[si];                      /* stipple stays soft + fades */
    }
    llut[6] = (uint8_t)(DOOR_BASE + 5);                  /* green/white unchanged */
    llut[7] = (uint8_t)(DOOR_BASE + 6);
    llut[8] = (uint8_t)(DOOR_BASE + 7);
    for (int i = 0; i <  4; i++) { int hp = i - door_shade; if (hp < 0) hp = 0;
        llut[14 + i] = (uint8_t)(HANDLE_BASE + hp); }   /* handle, faded */
    { int fp = 4 - door_shade; if (fp < 0) fp = 0; llut[18] = (uint8_t)(FRAME_BASE + fp); }

    /* Leaf foreshorten as an ACCUMULATOR: srow advances by a fixed step each
     * scanline instead of a per-pixel multiply (d * leaf_rscale). Tracking the
     * continuous ty (not the quantized oty) is also a touch smoother. leaf_col
     * is guaranteed here, so the per-pixel test drops too. */
    fx_t leaf_cy_fx = (fx_t)leaf_cy << FX_SHIFT;
    fx_t srow_step  = FX_MUL(ty_step, leaf_rscale);
    fx_t srow_fx    = leaf_cy_fx + FX_MUL(ty - leaf_cy_fx, leaf_rscale);
    for (int y = drawStart; y <= drawEnd; y++, ty += ty_step, srow_fx += srow_step) {
        int oty = (int)(ty >> FX_SHIFT);
        if (oty >= DOOR_TEX_HEIGHT) oty = DOOR_TEX_HEIGHT - 1;
        uint8_t c8;
        if (oty >= LEAF_Y0 && oty <= LEAF_Y1) {
            int d = oty - leaf_cy;
            if (void_leaf || d < -leaf_halff || d > leaf_halff) {
                /* See past the leaf into the recess: ceiling / far face / carpet. */
                if (y < rec_top || y > rec_bot) {
                    /* REAL carpet + ceiling: the clear/ceiling/carpet passes
                     * already painted this pixel at this row's true floor/
                     * ceiling depth -- and the recess interior IS the room's
                     * floor and ceiling continuing through the doorway (same
                     * trick as the void-exit opening). Keep the painted texel
                     * (grid dots, carpet stains, row fade all correct) and
                     * just dim it a couple of fine steps so the inside still
                     * reads recessed. Panels/lights outside both ramps pass
                     * through untouched. */
                    uint8_t c = *pd;
                    if (c >= FLOOR_BASE && c < FLOOR_BASE + SHADE_LEVELS) {
                        c += 2;
                        if (c > FLOOR_BASE + SHADE_LEVELS - 1)
                            c = FLOOR_BASE + SHADE_LEVELS - 1;
                    } else if (c >= CEIL_BASE && c < CEIL_BASE + SHADE_LEVELS) {
                        c += 2;
                        if (c > CEIL_BASE + SHADE_LEVELS - 1)
                            c = CEIL_BASE + SHADE_LEVELS - 1;
                    }
                    c8 = c;
                }
                else if (jamb == 1)
                    c8 = (y < ao_top || y > ao_bot)
                       ? rec_wall_dk
                       : (((y ^ col) & 1) ? rec_wall_b : rec_wall);
                else {
                    int bv = (int)(((fx_t)(y - rec_top) * bw_step) >> FX_SHIFT)
                             & (WALL_TEX_HI_HEIGHT - 1);
                    int ws = rsh + (bw_col[bv] >> 1)    /* chevron texel 0..4 */
                           + (jamb == 2);               /* side-wall form cue: one
                                                         * step darker so the cavity
                                                         * corner READS as a corner */
                    if (ws > SHADE_LEVELS - 1) ws = SHADE_LEVELS - 1;
                    c8 = (uint8_t)(WALL_BASE + ws);
                }
            } else {
                int srow = (int)(srow_fx >> FX_SHIFT);
                if (srow < LEAF_Y0) srow = LEAF_Y0;
                else if (srow > LEAF_Y1) srow = LEAF_Y1;
                c8 = llut[leaf_base[srow]];   /* leaf: swing-shaded (stipple baked in) */
            }
        } else {
            c8 = dlut[col_base[oty]];
        }
        if (hr >= 2) *(uint32_t *)pd = LDUP(c8); else if (hr) *(uint16_t *)pd = WDUP(c8); else *pd = c8;
        pd += SCREEN_W;
    }
}

RAMTEXT void raycast_draw_walls(int col_start, int col_end) {
    int dda_steps = 0, dda_fat = 0;   /* per-frame DDA profiling (HUD D/E) */
    int efg_kept = 0, runwalk = 0, ovl_cols = 0, promote_cols = 0;
    uint16_t ovl_px_acc = 0;          /* overlay pixel-loop ticks (setup = O - this) */
    /* Partition diag mode, hoisted once per pass (uncached read): 0 normal,
     * 1 run-extent LUT, 2 slab gate off (diagnostic pricing). */
    int part_diag = SHARED_UC->part_diag;
    int ovl_acc = 0;                  /* FRT ticks in the see-over overlay (HUD O) */
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;

    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));

    uint8_t *fb = fb_pixels();

    /* Fixed col_start..col_end split — work-stealing via TAS + COMM6
     * was attempted but the ~190K atomic bus ops/sec during the wall
     * pass drowned the 68K→SH2 bridge that carries joypad reads back
     * to MARS_SYS_COMM8, re-introducing the controller-drop stall.
     * Sticking with the primary 0..SCREEN_W/2 / secondary SCREEN_W/2..
     * SCREEN_W static split until we have a low-contention work-
     * stealing pattern (e.g. primary pre-chunking into 8-column
     * batches and the secondary just reading a written-once index). */
    /* Load pitch once — read via cache-through alias so primary's
     * latest write is visible. Walls center on the shifted horizon. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    /* Eye height once: splits the wall column about the horizon. 128 =
     * standing (symmetric, lineHeight/2 below); lower drops the eye toward
     * the floor (crawling) so the floor sits close and the ceiling looms. */
    int eye_h = (int)SHARED_UC->eye_h;

    /* Half-res: step 2 columns at a time, drawing col's result into the
     * (col,col+1) pair via word-stores. col_start/col_end are multiples of 4
     * (split is), so col+1 always stays inside this CPU's half. */
    /* Effective half-res, resolved by the primary each frame (menu mode + lobby
     * override + AUTO frame-time decision) and published via cache-through so
     * both CPUs draw the same resolution. */
    /* hr0/cstep0 are the GLOBAL (non-LOD) resolution. In WALLS=LOD the loop below
     * shadows hr/cstep per quad from depth; hr0/cstep0 stay the fallback and drive
     * the gates here. */
    const int hr0 = SHARED_UC->wall_halfres;  /* 0 full, 1 half, 2 quarter */
    const int vert = SHARED_UC->wall_vert;    /* 1 = store even rows only (vertical half-res) */
    /* ULTRA pass B: shift every ray half a column (hoisted — uncached read). */
    const fx_t ujit = (SHARED_UC->ultra_twin == 2) ? ULTRA_JIT_FX : 0;
    const int cstep0 = (hr0 >= 2) ? 4 : (hr0 ? 2 : 1);
    /* SEAMS=SMOOTH only bites when the fill is coarsened (cstep>1). The half→full
     * dissolve runs at FULL res (cstep==1) and re-doubles pairs over the wall
     * band, so it needs the SAME silhouette record. Record for either; clear the
     * validity for this half up front, anchors set it as they fill. */
    const int seam_smooth = SHARED_UC->wall_seam_smooth && cstep0 > 1;
    const int dissolve    = (cstep0 == 1) ? (int)SHARED_UC->wall_dissolve : 0;
    const int qdither     = (cstep0 == 4) ? (int)SHARED_UC->wall_qdither : 0;
    const int lod         = (cstep0 == 1) ? (int)SHARED_UC->wall_lod : 0;
    const int seam_rec    = seam_smooth || dissolve || qdither || lod;
    g_seam_rec_on = seam_rec;   /* gates the covered-row skip against stale seams */
    if (seam_rec)
        for (int c = col_start; c < col_end; c++) seam_top[c] = -1;

    /* Decal broad-phase: filter the wall-embedded decals down to the ones
     * actually in front of the camera and within this CPU's screen span,
     * ONCE, so the per-column footprint test below walks a short list instead
     * of every decal on the map. The hit-point test still validates each hit,
     * so nothing is mis-drawn — we just stop testing decals that can't be
     * reached this frame (behind you, or off to the side). Turns the per-frame
     * decal cost from O(cols x total_decals) toward O(cols x visible_decals),
     * so a dense-decal map stops taxing every column for outlets off-screen. */
    int8_t active_decal[16];
    int    n_active = 0;
    /* Screen span of any visible DOOR decal (union), for the LOD veto below.
     * The door is the one wall surface dense with 1-2 texel features (sign
     * glyphs, handle, jamb slivers); at the LOD near-band's half res those
     * alias and CRAWL as the sampling phase slides with each step — the EXIT
     * sign garbled into a different word every frame of the approach. */
    int door_lo = SCREEN_W, door_hi = -1;
    if (num_decals > 0) {
        fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
        if (det != 0) {
            fx_t inv_det = fx_div_hw(FX_ONE, det);
            for (int d = 0; d < num_decals && n_active < 16; d++) {
                /* Both kinds go in the list (outlet=0, door=1); the two scans
                 * below each filter by kind. */
                fx_t cx = decals[d].x - px, cy = decals[d].y - py;
                fx_t depth = FX_MUL(inv_det, FX_MUL(-planeY, cx) + FX_MUL(planeX, cy));
                if (depth < FX(0.2)) continue;        /* behind the camera */
                fx_t tX = FX_MUL(inv_det, FX_MUL(dirY, cx) - FX_MUL(dirX, cy));
                int sX = (SCREEN_W >> 1)
                       + (int)(((int64_t)(SCREEN_W >> 1) * FX_DIV(tX, depth)) >> FX_SHIFT);
                /* Widen by the decal's projected half-width (worst case, face-on)
                 * so a big/near decal like the full-width door isn't dropped when
                 * its centre is off the edge but its face still spans into view.
                 * PLANE units, not world: sX comes from the plane-basis transform
                 * (tX), where one unit is the 0.66-long camera plane — a world
                 * half-width used raw comes out 34% narrow. That sliver put the
                 * door's frame/jamb columns OUTSIDE the LOD veto: the sign held
                 * full-res while the edges kept garbling at half, so the veto
                 * read as a no-op from the player's side of the screen. */
                fx_t dhw = decals[d].kind ? DECAL_DOOR_HW : DECAL_OUTLET_HW;
                dhw = FX_MUL(dhw, FX(1.0 / 0.66));
                int sHW = (int)(((int64_t)(SCREEN_W >> 1) * FX_DIV(dhw, depth)) >> FX_SHIFT);
                if (sX + sHW < col_start || sX - sHW >= col_end) continue;  /* span off this half */
                if (decals[d].kind == 1) {
                    if (sX - sHW - 2 < door_lo) door_lo = sX - sHW - 2;
                    if (sX + sHW + 2 > door_hi) door_hi = sX + sHW + 2;
                }
                active_decal[n_active++] = (int8_t)d;
            }
        }
    }

    /* WALLS=LOD wraps the column loop in 4-col quads: each quad picks its render
     * resolution from the PREVIOUS quad's depth (walls are spatially coherent, so
     * the one-quad lag is invisible except a frame of the wrong res at a hard
     * corner), and the inner loop casts 1/2/4 rays accordingly — the real perf
     * win: far quads raycast once and word-fill 4px. For every non-LOD mode
     * cstep_q == cstep0 for all quads, so this reproduces the old loop exactly. */
    fx_t lod_prev_depth = 0;   /* 0 => nearest (full res) for the first quad */
    fx_t lod_prev_pt = 0x7FFFFFFF;   /* nearest see-over slab in prev quad; big => none */
    /* Motion drops the NEAR band from full to half: chunkiness is masked while
     * moving, and it's the biggest (most expensive) columns, so it's the best
     * place to reclaim time. Snaps back to full the instant you stand still to
     * take stock. (is_walking = any motion; swap to is_running for sprint-only.)
     * ALSO drops on wall_dense: a partition-heavy view where full-res-standing is
     * the F:05 pit (~84% of the pass is near slabs). Standing in a normal room
     * stays full — wall_dense only latches when last frame's wall cost was high. */
    int lod_near_cs = (lod && (SHARED_UC->is_walking || SHARED_UC->is_turning
                               || SHARED_UC->wall_dense)) ? 2 : 1;
    for (int qcol = col_start; qcol < col_end; qcol += 4) {
        int cstep_q = cstep0;
        if (lod) {
            /* QUARTER NERFED OUT OF LOD (measured 2026-08-06). The far tier used
             * to be 4 (quarter blocks). Same-ROM A/B in the test corridor:
             *   HALF             W 6,283  T 18,121  F:10
             *   QUARTER+dither   W 4,816  T 17,978  F:10   <- ties half, looks worse
             *   LOD (4 far)      W 8,856  T 20,941  F:08
             * Quarter's raw saving is real but the boundary dither hands half of
             * it back, and what survives does not clear an fps step. Half both
             * ties it and looks better, so the far tier is now 2 as well. Note
             * the far-quad dither below assumes 4px blocks, so it must stay OFF
             * for LOD now that no tier produces them. */
            cstep_q = (lod_prev_depth < LOD_T_NEAR) ? lod_near_cs : 2;
            /* B-fix: a near see-over slab pulls this quad back to full res when
             * you STOP to look, overriding the near-band drop that pixel-doubles
             * it. While MOVING we keep the drop (same cstep the walls take), so
             * orbiting a slab-heavy scene stays cheap; the sharp snap is for
             * standing still — which is when the stair-steps are worth spending
             * on and when there's no motion cost to spare it. */
            if (lod_prev_pt < PART_SHARP_D && !SHARED_UC->is_walking
                && !SHARED_UC->is_turning) cstep_q = 1;
        }
        /* DOOR veto — BOTH adaptive styles, not just LOD. AUTO=SCALE drops
         * the whole frame to half/quarter via hr0/cstep0, which the first cut
         * of this veto (inside the lod branch) never touched — the door kept
         * garbling and the fix looked like a no-op on the testbed. Quads
         * across a visible door render full-res at any depth, moving or not:
         * the span is a handful of quads, the payoff is a sign you can read
         * and a handle that holds its shape while you walk at it. */
        if (door_hi >= 0 && qcol <= door_hi && qcol + 3 >= door_lo)
            cstep_q = 1;
        fx_t quad_depth = 0x7FFFFFFF;
        fx_t quad_pt = 0x7FFFFFFF;   /* nearest see-over slab in THIS quad */
        for (int col = qcol; col < qcol + 4 && col < col_end; col += cstep_q) {
        int cstep = cstep_q;
        /* hr must track the ACTUAL step (not hr0): a door-vetoed column at
         * cstep 1 inside a global-half frame must byte-write — word-writing
         * from every column would land misaligned stores on the odd ones. */
        int hr = (cstep_q >= 4) ? 2 : (cstep_q >= 2) ? 1 : 0;
        WALL_DIST(col) = 0x7FFFFFFF;
        for (int j = 1; j < cstep; j++) WALL_DIST(col + j) = 0x7FFFFFFF;
        PART_TOP(col) = 0;
        for (int j = 1; j < cstep; j++) PART_TOP(col + j) = 0;
        BG_DIST(col) = 255;
        for (int j = 1; j < cstep; j++) BG_DIST(col + j) = 255;
        fx_t cameraX = cameraX_table[col] + ujit;
        fx_t rayDirX = dirX + FX_MUL(planeX, cameraX);
        fx_t rayDirY = dirY + FX_MUL(planeY, cameraX);

        int mapX = FX_INT(px);
        int mapY = FX_INT(py);

        /* deltaDist = 1/|rayDir| in 16.16. Hardware DIVU (fx_div_hw, ~39 cyc)
         * vs the libgcc int64 FX_DIV (~200 cyc) — runs twice per column. The
         * |rayDir| < 4 guard (was == 0) keeps fx_div_hw's quotient inside 31
         * bits (it has no overflow saturation); a sub-0.00006 ray is treated as
         * axis-parallel ("never crosses"), same intent as the old zero guard. */
        fx_t deltaDistX = (FX_ABS(rayDirX) < 4) ? 0x7FFFFFFF
                                                : fx_div_hw(FX_ONE, FX_ABS(rayDirX));
        fx_t deltaDistY = (FX_ABS(rayDirY) < 4) ? 0x7FFFFFFF
                                                : fx_div_hw(FX_ONE, FX_ABS(rayDirY));

        int stepX, stepY;
        fx_t sideDistX, sideDistY;
        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = FX_MUL(px - ((fx_t)mapX << FX_SHIFT), deltaDistX);
        } else {
            stepX = 1;
            sideDistX = FX_MUL(((fx_t)(mapX + 1) << FX_SHIFT) - px, deltaDistX);
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = FX_MUL(py - ((fx_t)mapY << FX_SHIFT), deltaDistY);
        } else {
            stepY = 1;
            sideDistY = FX_MUL(((fx_t)(mapY + 1) << FX_SHIFT) - py, deltaDistY);
        }

        /* Slab constants for this column: the pullback from a centerline
         * crossing to the face plane (per crossing axis) and the matching
         * along-line drift of the ray over that pullback. */
        fx_t slab_pbx = 0, slab_pby = 0, slab_offx = 0, slab_offy = 0;
        if (g_pedge_any) {
            slab_pbx  = FX_MUL(deltaDistX, PART_HALF_THICK);
            slab_pby  = FX_MUL(deltaDistY, PART_HALF_THICK);
            slab_offx = FX_MUL(rayDirY, slab_pbx);
            slab_offy = FX_MUL(rayDirX, slab_pby);
        }
        int side = 0;
        int hit = 0;
        int hit_cell = 0;          /* world_map value at the hit (2 = black exit) */
        uint8_t edge_f = 0;        /* slab partition hit (0 = none) */
        fx_t edge_capt = -1;       /* cap hit: exact depth (mitered or lattice) */
        int n_efg = 0;             /* partial slab crossings (overlay bands) */
        uint8_t efg_f[2], efg_sd[2], efg_ax[2], efg_x[2], efg_y[2];
        fx_t efg_t[2];
        int efg_id_last = -1;      /* (axis<<8)|line of the last recorded run —
                                    * a ray traveling inside a slab's thickness
                                    * re-meets the same run at every step; only
                                    * the first contact records */
        for (int i = 0; i < 64 && !hit; i++) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            /* The world's outer shell is SOLID. Collision has always said so
             * (cell_passable returns 0 out of bounds) and the editor's preview
             * draws it as wall, so the RENDERER was the odd one out: it let the
             * ray escape and you saw the infinite void through the map's edge.
             * Authors built against the editor and left their borders open —
             * every community map is affected (SEGA POWER BASE has 121 open
             * border cells). The ray already crossed the boundary plane, so
             * side/sideDist are set for exactly that face: treat it as a wall
             * hit and the shell renders at the right depth for free. */
            if (mapX < 0 || mapX >= MAP_W || mapY < 0 || mapY >= MAP_H) {
                hit = 1; hit_cell = 1;
                break;
            }
            /* First-class edge partitions: the DDA just crossed one boundary
             * line — test its flag byte. Stepping +X into mapX crosses the
             * line x = mapX (cell's west edge); -X crosses x = mapX+1. Same
             * for Y against the north-edge plane. A full-height divider
             * terminates the ray EXACTLY like a wall — perpDist, side, fog,
             * everything downstream reuses the wall math. Cost: one cached
             * byte read per step, only on maps that have edges at all. The
             * pedge_cell gate skips ALL the slab math on cells with no
             * partition in reach — the difference between a per-step tax on
             * every column of every frame and paying it only near partitions. */
            dda_steps++;
            int gb = (side << 1) | (((side == 0) ? stepX : stepY) > 0);
            uint8_t pgate = (uint8_t)(pedge_cell[mapY][mapX] >> gb);
            if ((pgate & 0x11) && part_diag != 2) {   /* diag 2: price the pass with no slabs */
                if (pgate & 0x10) dda_fat++;
                /* SIDE FACE: the DDA just crossed a flagged centerline — the
                 * slab's near face sits PART_HALF_THICK before it along the
                 * crossing axis (t_face = t_line - HALF*deltaDist, applied
                 * after the loop for the terminating hit / at record time
                 * for partials). */
                int elx = (side == 0) ? (stepX > 0 ? mapX : mapX + 1) : mapX;
                int ely = (side == 0) ? mapY : (stepY > 0 ? mapY : mapY + 1);
                uint8_t ef = (pgate & 1)
                    ? ((side == 0) ? pedge_w[ely][elx] : pedge_n[ely][elx]) : 0;
                if (!(ef & CM_PEDGE_PRESENT) && (pgate & 0x10)) {
                    /* EXACT near-end recovery: a ray can pierce a slab's face
                     * inside the run's last panel yet cross the centerline in
                     * the unflagged cell beyond it. Compute where the ray
                     * pierces the candidate's FACE PLANE (crossing U minus
                     * the drift over that candidate's pullback) and index the
                     * edge array there; the plane must lie in front of the
                     * ray. Pullback/drift are per-flag selects (flush-shifted
                     * slabs have asymmetric faces). Verified against analytic
                     * ray-vs-box ground truth. */
                    if (side == 0) {
                        fx_t tc = sideDistX - deltaDistX;
                        uint8_t ep = (ely > 0)         ? pedge_w[ely - 1][elx] : 0;
                        uint8_t eq = (ely + 1 < MAP_H) ? pedge_w[ely + 1][elx] : 0;
                        for (int c2 = 0; c2 < 2 && !(ef & CM_PEDGE_PRESENT); c2++) {
                            uint8_t cd = c2 ? eq : ep;
                            int    dl = c2 ? 1 : -1;
                            if (cd & CM_PEDGE_PRESENT) {
                                fx_t pl = SLAB_PULL(cd, slab_pbx, stepX > 0);
                                if (tc > pl) {
                                    fx_t dr = (pl == 0) ? 0
                                            : (pl == slab_pbx) ? slab_offx
                                                               : (slab_offx << 1);
                                    fx_t uf = (py + FX_MUL(tc, rayDirY)) - dr;
                                    int  cu = FX_INT(uf);
                                    if (cu == ely + dl) { ef = cd; ely += dl; }
                                    else if (cu == ely && !c2) {
                                        /* junction miter strip (centered runs) */
                                        fx_t fu = uf - ((fx_t)cu << FX_SHIFT);
                                        uint8_t j0 = (elx < MAP_W ? pedge_n[ely][elx] : 0);
                                        uint8_t j1 = (elx > 0 ? pedge_n[ely][elx - 1] : 0);
                                        if (fu < PART_HALF_THICK &&
                                            ((j0 | j1) & CM_PEDGE_PRESENT)) { ef = cd; ely += dl; }
                                    } else if (cu == ely && c2) {
                                        fx_t fu = uf - ((fx_t)cu << FX_SHIFT);
                                        uint8_t j2 = (elx < MAP_W ? pedge_n[ely + 1][elx] : 0);
                                        uint8_t j3 = (elx > 0 ? pedge_n[ely + 1][elx - 1] : 0);
                                        if (fu > FX_ONE - PART_HALF_THICK &&
                                            ((j2 | j3) & CM_PEDGE_PRESENT)) { ef = cd; ely += dl; }
                                    }
                                }
                            }
                        }
                        if (!(ef & CM_PEDGE_PRESENT) && part_diag != 1) {
                            /* GLANCING MISS FIX: the pierce can sit >1 cell
                             * past the run end at shallow angles — index the
                             * exact pierce cell instead of guessing +/-1.
                             * J:1 runs the legacy +/-1-only recovery for A/B. */
                            fx_t uf0 = py + FX_MUL(tc, rayDirY);
                            int  cu0 = FX_INT(uf0);
                            if ((unsigned)cu0 < (unsigned)MAP_H && cu0 != ely) {
                                uint8_t cd = pedge_w[cu0][elx];
                                if (cd & CM_PEDGE_PRESENT) {
                                    fx_t pl = SLAB_PULL(cd, slab_pbx, stepX > 0);
                                    if (tc > pl) {
                                        fx_t dr = (pl == 0) ? 0
                                                : (pl == slab_pbx) ? slab_offx
                                                                   : (slab_offx << 1);
                                        int cu = FX_INT(uf0 - dr);
                                        if ((unsigned)cu < (unsigned)MAP_H &&
                                            (pedge_w[cu][elx] & CM_PEDGE_PRESENT)) {
                                            ef = pedge_w[cu][elx]; ely = cu;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        fx_t tc = sideDistY - deltaDistY;
                        uint8_t ep = (elx > 0)         ? pedge_n[ely][elx - 1] : 0;
                        uint8_t eq = (elx + 1 < MAP_W) ? pedge_n[ely][elx + 1] : 0;
                        for (int c2 = 0; c2 < 2 && !(ef & CM_PEDGE_PRESENT); c2++) {
                            uint8_t cd = c2 ? eq : ep;
                            int    dl = c2 ? 1 : -1;
                            if (cd & CM_PEDGE_PRESENT) {
                                fx_t pl = SLAB_PULL(cd, slab_pby, stepY > 0);
                                if (tc > pl) {
                                    fx_t dr = (pl == 0) ? 0
                                            : (pl == slab_pby) ? slab_offy
                                                               : (slab_offy << 1);
                                    fx_t uf = (px + FX_MUL(tc, rayDirX)) - dr;
                                    int  cu = FX_INT(uf);
                                    if (cu == elx + dl) { ef = cd; elx += dl; }
                                    else if (cu == elx && !c2) {
                                        fx_t fu = uf - ((fx_t)cu << FX_SHIFT);
                                        uint8_t j0 = pedge_w[ely][elx];
                                        uint8_t j1 = (ely > 0 ? pedge_w[ely - 1][elx] : 0);
                                        if (fu < PART_HALF_THICK &&
                                            ((j0 | j1) & CM_PEDGE_PRESENT)) { ef = cd; elx += dl; }
                                    } else if (cu == elx && c2) {
                                        fx_t fu = uf - ((fx_t)cu << FX_SHIFT);
                                        uint8_t j2 = pedge_w[ely][elx + 1];
                                        uint8_t j3 = (ely > 0 ? pedge_w[ely - 1][elx + 1] : 0);
                                        if (fu > FX_ONE - PART_HALF_THICK &&
                                            ((j2 | j3) & CM_PEDGE_PRESENT)) { ef = cd; elx += dl; }
                                    }
                                }
                            }
                        }
                        if (!(ef & CM_PEDGE_PRESENT) && part_diag != 1) {
                            /* Glancing-miss fix, horizontal twin. */
                            fx_t uf0 = px + FX_MUL(tc, rayDirX);
                            int  cu0 = FX_INT(uf0);
                            if ((unsigned)cu0 < (unsigned)MAP_W && cu0 != elx) {
                                uint8_t cd = pedge_n[ely][cu0];
                                if (cd & CM_PEDGE_PRESENT) {
                                    fx_t pl = SLAB_PULL(cd, slab_pby, stepY > 0);
                                    if (tc > pl) {
                                        fx_t dr = (pl == 0) ? 0
                                                : (pl == slab_pby) ? slab_offy
                                                                   : (slab_offy << 1);
                                        int cu = FX_INT(uf0 - dr);
                                        if ((unsigned)cu < (unsigned)MAP_W &&
                                            (pedge_n[ely][cu] & CM_PEDGE_PRESENT)) {
                                            ef = pedge_n[ely][cu]; elx = cu;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (ef & CM_PEDGE_PRESENT) {
                    int rid = (side << 8) | (side == 0 ? elx : ely);
                    if (CM_PEDGE_HCLASS(ef) == 0) {
                        edge_f = ef; hit = 1; hit_cell = 1; break;
                    }
                    if (rid != efg_id_last && n_efg < 2) {
                        efg_f[n_efg]  = ef;
                        efg_sd[n_efg] = (uint8_t)side;
                        efg_ax[n_efg] = (uint8_t)side;   /* run axis == crossing side here */
                        efg_x[n_efg]  = (uint8_t)elx;
                        efg_y[n_efg]  = (uint8_t)ely;
                        efg_t[n_efg]  = ((side == 0) ? (sideDistX - deltaDistX)
                                                     : (sideDistY - deltaDistY))
                                        - (side == 0
                                           ? SLAB_PULL(ef, slab_pbx, stepX > 0)
                                           : SLAB_PULL(ef, slab_pby, stepY > 0));
                        efg_id_last = rid;
                        n_efg++;
                    }
                }
                /* END CAP: entering a cell that carries a PERPENDICULAR
                 * flagged line within HALF_THICK of the crossing point —
                 * the ray meets the slab's capped end, and the cap face IS
                 * this crossing plane (exact t, correct side shading). Flag
                 * bytes gate the multiply, so open floor costs 2 cached
                 * reads. */
                if ((pgate & 0x10) && side == 1) {
                    /* Caps: ENTERED-side only — a ray leaving a run has no
                     * face on this plane (leaving-fires were the phantom
                     * pillars). At a junction with a perpendicular slab the
                     * cap is MITERED: pulled back HALF_THICK to sit flush
                     * with the adjoining face plane, and the width window is
                     * evaluated AT that plane (the ray drifts along the cap
                     * over the pullback). Isolated ends cap on the lattice. */
                    int xrow = mapY - stepY;
                    int yp   = (stepY > 0) ? mapY : mapY + 1;
                    uint8_t aw = pedge_w[mapY][mapX];
                    uint8_t ae = pedge_w[mapY][mapX + 1];
                    uint8_t bw = ((unsigned)xrow < (unsigned)MAP_H) ? pedge_w[xrow][mapX] : 0;
                    uint8_t be = ((unsigned)xrow < (unsigned)MAP_H) ? pedge_w[xrow][mapX + 1] : 0;
                    uint8_t cw = ((aw & CM_PEDGE_PRESENT) && !(bw & CM_PEDGE_PRESENT)) ? aw : 0;
                    uint8_t ce = ((ae & CM_PEDGE_PRESENT) && !(be & CM_PEDGE_PRESENT)) ? ae : 0;
                    if (cw | ce) {
                        fx_t tc = sideDistY - deltaDistY;
                        fx_t fr = (px + FX_MUL(tc, rayDirX))
                                - ((fx_t)mapX << FX_SHIFT);
                        uint8_t cf = 0; int cl = mapX; fx_t ct = tc;
                        if (cw) {
                            fx_t fo  = SLAB_FO(cw);
                            int cen  = !(cw & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI));
                            int mit  = cen && ((pedge_n[yp][mapX] |
                                        (mapX > 0 ? pedge_n[yp][mapX - 1] : 0))
                                       & CM_PEDGE_PRESENT);
                            fx_t frf = fr - (mit ? slab_offy : 0);
                            if (frf > -fo && frf < PART_HALF_THICK * 2 - fo) {
                                cf = cw;
                                if (mit && tc > slab_pby) ct = tc - slab_pby;
                            }
                        }
                        if (!cf && ce) {
                            fx_t fo  = SLAB_FO(ce);
                            int cen  = !(ce & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI));
                            int mit  = cen && (((mapX + 1 < MAP_W ? pedge_n[yp][mapX + 1] : 0) |
                                        pedge_n[yp][mapX])
                                       & CM_PEDGE_PRESENT);
                            fx_t frf = fr - (mit ? slab_offy : 0) - FX_ONE;
                            if (frf > -fo && frf < PART_HALF_THICK * 2 - fo) {
                                cf = ce; cl = mapX + 1;
                                if (mit && tc > slab_pby) ct = tc - slab_pby;
                            }
                        }
                        if (cf) {
                            int rid = (0 << 8) | cl;
                            if (CM_PEDGE_HCLASS(cf) == 0) {
                                edge_f = cf; edge_capt = ct; hit = 1; hit_cell = 1; break;
                            }
                            if (rid != efg_id_last && n_efg < 2) {
                                efg_f[n_efg]  = cf;
                                efg_sd[n_efg] = 1;   /* shade/texture: Y-facing cap */
                                efg_ax[n_efg] = 0;   /* run itself is vertical */
                                efg_x[n_efg]  = (uint8_t)cl;
                                efg_y[n_efg]  = (uint8_t)mapY;
                                efg_t[n_efg]  = ct;
                                efg_id_last = rid;
                                n_efg++;
                            }
                        }
                    }
                } else if (pgate & 0x10) {
                    /* Twin of the side==1 block for horizontal runs. */
                    int xcol = mapX - stepX;
                    int xp   = (stepX > 0) ? mapX : mapX + 1;
                    uint8_t an  = pedge_n[mapY][mapX];
                    uint8_t as2 = pedge_n[mapY + 1][mapX];
                    uint8_t bn  = ((unsigned)xcol < (unsigned)MAP_W) ? pedge_n[mapY][xcol] : 0;
                    uint8_t bs  = ((unsigned)xcol < (unsigned)MAP_W) ? pedge_n[mapY + 1][xcol] : 0;
                    uint8_t cn = ((an & CM_PEDGE_PRESENT) && !(bn & CM_PEDGE_PRESENT)) ? an : 0;
                    uint8_t cs = ((as2 & CM_PEDGE_PRESENT) && !(bs & CM_PEDGE_PRESENT)) ? as2 : 0;
                    if (cn | cs) {
                        fx_t tc = sideDistX - deltaDistX;
                        fx_t fr = (py + FX_MUL(tc, rayDirY))
                                - ((fx_t)mapY << FX_SHIFT);
                        uint8_t cf = 0; int cl = mapY; fx_t ct = tc;
                        if (cn) {
                            fx_t fo  = SLAB_FO(cn);
                            int cen  = !(cn & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI));
                            int mit  = cen && ((pedge_w[mapY][xp] |
                                        (mapY > 0 ? pedge_w[mapY - 1][xp] : 0))
                                       & CM_PEDGE_PRESENT);
                            fx_t frf = fr - (mit ? slab_offx : 0);
                            if (frf > -fo && frf < PART_HALF_THICK * 2 - fo) {
                                cf = cn;
                                if (mit && tc > slab_pbx) ct = tc - slab_pbx;
                            }
                        }
                        if (!cf && cs) {
                            fx_t fo  = SLAB_FO(cs);
                            int cen  = !(cs & (CM_PEDGE_FLUSH_LO | CM_PEDGE_FLUSH_HI));
                            int mit  = cen && (((mapY + 1 < MAP_H ? pedge_w[mapY + 1][xp] : 0) |
                                        pedge_w[mapY][xp])
                                       & CM_PEDGE_PRESENT);
                            fx_t frf = fr - (mit ? slab_offx : 0) - FX_ONE;
                            if (frf > -fo && frf < PART_HALF_THICK * 2 - fo) {
                                cf = cs; cl = mapY + 1;
                                if (mit && tc > slab_pbx) ct = tc - slab_pbx;
                            }
                        }
                        if (cf) {
                            int rid = (1 << 8) | cl;
                            if (CM_PEDGE_HCLASS(cf) == 0) {
                                edge_f = cf; edge_capt = ct; hit = 1; hit_cell = 1; break;
                            }
                            if (rid != efg_id_last && n_efg < 2) {
                                efg_f[n_efg]  = cf;
                                efg_sd[n_efg] = 0;   /* shade/texture: X-facing cap */
                                efg_ax[n_efg] = 1;   /* run itself is horizontal */
                                efg_x[n_efg]  = (uint8_t)mapX;
                                efg_y[n_efg]  = (uint8_t)cl;
                                efg_t[n_efg]  = ct;
                                efg_id_last = rid;
                                n_efg++;
                            }
                        }
                    }
                }
            }
            if (world_map[mapY][mapX]) { hit = 1; hit_cell = world_map[mapY][mapX]; break; }
            if (sideDistX > MAX_VIEW_DIST && sideDistY > MAX_VIEW_DIST) break;
        }
        /* Do NOT continue on !hit yet — even when the DDA bails because
         * the nearest wall is past MAX_VIEW_DIST, a partition can still
         * be right in front of the player. Initialize perpDist to
         * effectively-infinity in that case so the partition override
         * below has something to beat. The !hit && !partition_hit case
         * gets skipped after the override test. */
        fx_t perpDist;
        if (hit) {
            perpDist = (side == 0) ? (sideDistX - deltaDistX)
                                   : (sideDistY - deltaDistY);
            /* Side faces pull back to the face plane; caps carry the exact
             * depth computed at the test (mitered = flush with the adjoining
             * face plane, isolated = on the lattice). */
            if (edge_f & CM_PEDGE_PRESENT)
                perpDist = (edge_capt >= 0)
                    ? edge_capt
                    : perpDist - ((side == 0)
                                  ? SLAB_PULL(edge_f, slab_pbx, stepX > 0)
                                  : SLAB_PULL(edge_f, slab_pby, stepY > 0));
            if (perpDist < FX(0.1)) perpDist = FX(0.1);
        } else {
            perpDist = 0x7FFFFFFF;
        }
        /* Slab hit: dress the wall hit as a partition (style + texture U) —
         * the draw path takes it from here with zero partition-specific math. */
        int edge_part = (edge_f & CM_PEDGE_PRESENT) != 0;

        /* Per-ray partition intersection. Test this column's ray against
         * each visible partition face (line segment in world space) using
         * standard 2D ray-segment intersection. Take the closest hit. No
         * cross-column interpolation, so no wedge artifact possible — each
         * column gets its own independent t (= perpDist, since dot(rayDir,
         * dir) = 1 by construction of rayDir = dir + cameraX*plane).
         *
         * Saturated divide guards against denom underflow at glancing
         * angles where the wall DDA would similarly lose precision — same
         * trap that originally pushed us to projection. */
        int  partition_hit       = 0;   /* a FULL-height partition is the background */
        int  part_style          = 0;
        int  part_height         = 0;   /* background is always full height (0) */
        fx_t partition_wallhit_w = 0;
        if (edge_part) {                /* first-class edge divider IS the background */
            partition_hit = 1;
            part_style = edge_f & CM_PEDGE_SPOTTED;
            partition_wallhit_w = (side == 0)
                ? (py + FX_MUL(perpDist, rayDirY))
                : (px + FX_MUL(perpDist, rayDirX));
        }
        /* Foreground = the nearest PARTIAL-height partitions (floor-anchored).
         * Drawn as a band OVER the background (solid wall / full partition) after
         * the main column draw, so a surface behind it (e.g. the lobby T-stem)
         * shows above it. */
        int  fg_n = 0;                     /* partials kept: 0..2, sorted near->far —
                                            * two slots so a counter doesn't erase the
                                            * divider visible over its top */
        int  fg_style[2] = {0,0}, fg_height[2] = {0,0};
        int  fg_side[2]  = {0,0}, fg_ax[2] = {0,0};
        fx_t fg_t[2] = {0,0}, fg_wallhit[2] = {0,0};
        fx_t fg_line[2] = {0,0};                   /* slab centerline world coord */
        fx_t fg_fo[2]   = {0,0};                   /* slab band offset (flush shift) */
        fx_t fg_ua[2] = {0,0}, fg_ub[2] = {0,0};   /* run extent along the line */
        /* Partial slabs -> band slots. The DDA delivered face/cap contacts
         * nearest-first, each with an EXACT t — no per-column intersection
         * math. The band and wood countertop hang off these. */
        for (int ei = 0; ei < n_efg && fg_n < 2; ei++) {
            uint8_t ef = efg_f[ei];
            fx_t et = efg_t[ei];
            if (et >= perpDist) break;             /* at/behind the background */
            if (et < FX(0.1)) et = FX(0.1);
            int k = fg_n++;
            efg_kept++;
            fg_t[k] = et;
            fg_side[k] = efg_sd[ei];               /* facing: shade + texture U */
            fg_ax[k]   = efg_ax[ei];               /* run axis: countertop depth */
            fg_wallhit[k] = efg_sd[ei] ? (px + FX_MUL(et, rayDirX))
                                       : (py + FX_MUL(et, rayDirY));
            fg_line[k] = (fx_t)(efg_ax[ei] ? efg_y[ei] : efg_x[ei]) << FX_SHIFT;
            fg_fo[k]   = SLAB_FO(ef);
            fg_style[k]  = ef & CM_PEDGE_SPOTTED;
            fg_height[k] = (CM_PEDGE_HCLASS(ef) == 1) ? 192 : 96;
            {   /* Run extent along the line (countertop end clip):
                 * walk the edge bytes both ways while the flag matches. */
                int ex = efg_x[ei], ey = efg_y[ei], lo, hi;
                if (part_diag != 1) {              /* DEFAULT: LUT, two byte reads.
                                                    * Measured J0-vs-LUT: -299 ticks
                                                    * (2.6% of W), pixel-identical.
                                                    * J:1 = legacy walk (regression
                                                    * pricing only). */
                    if (efg_ax[ei] == 0) { lo = prun_lo_w[ey][ex]; hi = prun_hi_w[ey][ex]; }
                    else                 { lo = prun_lo_n[ey][ex]; hi = prun_hi_n[ey][ex]; }
                } else if (efg_ax[ei] == 0) {      /* vertical line: run in Y */
                    lo = ey; while (lo > 0 && pedge_w[lo - 1][ex] == ef) { lo--; runwalk++; }
                    hi = ey; while (hi + 1 < MAP_H && pedge_w[hi + 1][ex] == ef) { hi++; runwalk++; }
                } else {                           /* horizontal line: run in X */
                    lo = ex; while (lo > 0 && pedge_n[ey][lo - 1] == ef) { lo--; runwalk++; }
                    hi = ex; while (hi + 1 < MAP_W && pedge_n[ey][hi + 1] == ef) { hi++; runwalk++; }
                }
                fg_ua[k] = (fx_t)lo << FX_SHIFT;
                fg_ub[k] = (fx_t)(hi + 1) << FX_SHIFT;
            }
        }
        /* Partials only show in front of the final background (sorted, so
         * truncate at the first one at/behind it). */
        for (int fk = 0; fk < fg_n; fk++)
            if (fg_t[fk] >= perpDist) { fg_n = fk; break; }
        int spotted = partition_hit && part_style;

        /* Nothing in range — leave the ceiling/floor earlier passes painted. */
        if (!hit && !partition_hit && fg_n == 0) continue;

        /* See-over decision. The slow per-pixel overlay is only needed when the
         * background pokes ABOVE the partial (the lobby T-stem behind the low
         * arm). For every free-standing divider — background a far wall the
         * band fully covers — PROMOTE the partial to the fast MAIN draw path
         * (asm inner loop, full shade) and skip the overlay entirely. A point at
         * world height h/256 at distance d sits above the horizon ~(h-eye)/d, so
         * the full (h=256) background pokes above the partial (h=fg_height) iff
         * (256-eye)*fg_t > (fg_height-eye)*perpDist. */
        if (fg_n == 1 && fg_height[0] >= STAND_EYE) {
            /* A floating beam ALWAYS overlays — it never fills floor-to-ceiling,
             * so it can't be promoted to the fast main path (that would erase the
             * see-over above and the crawl gap below). Only floor-anchored
             * partials whose TOP sits at/above the STANDING eye line (the
             * true see-over dividers, 192) reach this promote test: the
             * promoted main path paints CEILING above the band, an
             * approximation that only reads right when you can't see over
             * the top by much. A HALF-height counter must NEVER promote at
             * ANY eye height — it takes the overlay path, whose band math is
             * exact whether the eye is above the top (standing: you look
             * down over it at the real room) or below it (crouched: it
             * occludes the lower band, background visible above). The gate
             * was briefly eye-RELATIVE (>= eye_h), which flipped counters
             * onto the promote path the moment the player crouched and
             * filled the space above them with the unpainted-background
             * garbage the crouch screenshots caught.
             *
             * See-over ONLY when the background is a FULL-height PARTITION (the
             * lobby T-stem) — not a solid wall. A partial divider with a wall
             * behind it should read as a plain low divider (ceiling above), the
             * simple look; revealing the wall over it is unwanted. So require
             * partition_hit (a full partition), and that it pokes above. */
            int bg_pokes = partition_hit &&
                ((int64_t)(256 - eye_h) * fg_t[0]
                 > (int64_t)(fg_height[0] - eye_h) * perpDist);
            if (!bg_pokes) {
                perpDist = fg_t[0]; partition_wallhit_w = fg_wallhit[0]; partition_hit = 1;
                part_style = fg_style[0]; part_height = fg_height[0]; side = fg_side[0];
                spotted = part_style;     /* partition_hit is now 1 */
                fg_n = 0;                 /* drawn by the fast main path; no overlay */
                promote_cols++;
            }
        }

        WALL_DIST(col) = perpDist;
        for (int j = 1; j < cstep; j++) WALL_DIST(col + j) = perpDist;
        /* See-over occlusion clip: the nearest partial band is opaque from its
         * top edge to the floor, so every background pixel at or below that
         * edge gets repainted by the overlay. Counter-filling views measured
         * FILL-BOUND (full-res W:32k vs half-res W:30k at the same pose —
         * halving the ray count barely moved the pass), so the win is not
         * casting fewer rays but writing fewer pixels: clamp every background
         * fill in this column to the rows above the band. The projected band
         * top costs one HW divide, reused as the overlay's own flh below. */
        int fg_clip = SCREEN_H;
        int fg_flh0 = 0;
        if (fg_n) {
            fg_flh0 = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT),
                                    (uint32_t)fg_t[0]);
            int fb0 = horizon_y + ((fg_flh0 * eye_h) >> 8);
            int ft0 = fb0 - ((fg_flh0 * fg_height[0]) >> 8);
            fg_clip = ft0 < 0 ? 0 : ft0;
        }
        /* See-over band with NOTHING behind it (background beyond the fog):
         * skip the wall draw entirely. This column used to fall through with
         * perpDist=0x7FFFFFFF, where lineHeight degenerates to 0 and the
         * draw loop painted EXACTLY ONE ROW of wall texture at the horizon
         * with an overflowed fog shade — the 1px light band riding the
         * screen center through every half-height see-over view, immune to
         * every countertop/light fix because it was neither. (Identified by
         * palette forensics: the band row decodes as WALL+9 surrounded by
         * FLOOR+15.) */
        if (!hit && !partition_hit) goto overlay_pass;
        /* No hard cutoff at MAX_VIEW_DIST — let walls render through
         * fog. The shade ramp clamps them to shade 15 past FOG_RAMP_DIST
         * so they're already fog-colored before they would have popped
         * in/out abruptly. */

        /* DIVU latency hide #1: start lineHeight = (SCREEN_H << 16) /
         * perpDist, then do wall_shade + wall_hit + texX in parallel
         * — none of those depend on lineHeight. 39 cycles of divide
         * disappear under ~40 cycles of column setup. */
        divu_start_u32((uint32_t)(SCREEN_H << FX_SHIFT),
                       (uint32_t)perpDist);

        /* Inflection at FX(2.5) (was 3.5) so mid-distance walls darken
         * sooner into the fog. The close ramp (0..2.5) is unchanged at
         * the rounding level so the wallpaper region stays bright; the
         * far ramp now covers a longer span (2.5..6 instead of 3.5..6),
         * pulling perpDist 3-5 walls down 2-3 shade levels — they read
         * as "in the fog" instead of "lit yellow far away". */
        /* Shade via surface_shade() so this face darkens by the SAME rule as
         * every other surface. We derive only the cell-bools here (wall-specific:
         * the DDA hit cell, back-stepped to the viewer-side OPEN cell for the
         * crawl/dark tests since the wall cell itself is solid); the ramp/cap/
         * push math lives in the helper. */
        int lit, lowceil_dark = 0, room_dark = 0;
        {
            int litX, litY;
            if (partition_hit) {
                litX = FX_INT(px + FX_MUL(perpDist, rayDirX));
                litY = FX_INT(py + FX_MUL(perpDist, rayDirY));
                if ((unsigned)litX >= (unsigned)MAP_W ||
                    (unsigned)litY >= (unsigned)MAP_H) { litX = mapX; litY = mapY; }
            } else {
                litX = mapX; litY = mapY;
            }
            /* Clamp before sampling: the outer shell / partition fallback can
             * land out of bounds, and an unguarded cell_light read returned
             * garbage that wrapped the palette (indigo corners in Ares, black on
             * MiSTer). */
            if (litX < 0) litX = 0; else if (litX >= MAP_W) litX = MAP_W - 1;
            if (litY < 0) litY = 0; else if (litY >= MAP_H) litY = MAP_H - 1;
            lit = CELL_LIGHT(litY, litX) & LIGHT_BOOST_MAX;
            if (g_lowceil_active) {
                int cfx = litX, cfy = litY;
                if (!partition_hit) {
                    if (side == 0) cfx = mapX - stepX;
                    else           cfy = mapY - stepY;
                }
                if ((unsigned)cfx < (unsigned)MAP_W &&
                    (unsigned)cfy < (unsigned)MAP_H &&
                    CEIL_H(cfy, cfx) != CEIL_H_FULL) lowceil_dark = 1;
            }
            if (g_dark_active) {
                int dfx = litX, dfy = litY;
                if (!partition_hit) {
                    if (side == 0) dfx = mapX - stepX;
                    else           dfy = mapY - stepY;
                }
                if ((unsigned)dfx < (unsigned)MAP_W &&
                    (unsigned)dfy < (unsigned)MAP_H &&
                    (CELL_LIGHT(dfy, dfx) & CELL_DARK)) room_dark = 1;
            }
        }
        int wall_shade = surface_shade(perpDist, side, lit, lowceil_dark, room_dark);

        /* Distant fluorescent strobe burst. Each cell past
         * FOG_RAMP_DIST has its own pseudo-random phase. Most of the
         * time it sits dark (shade 15). Phase 0..5 of every 256-frame
         * cycle (~2.3% per cell) it enters a 6-frame "burst" where it
         * flickers on/off at a per-frame coin flip — reads as a
         * fluorescent panel struggling to start, not a single-frame
         * pop. Cell offset (cell_seed >> 4) staggers which cells fire
         * when, so distant scenes get a steady drip of flickers from
         * different positions over time. */
        if (perpDist >= FOG_RAMP_DIST
            && (SHARED_UC->lighting_flags & LIGHTING_STROBE)) {
            uint32_t cell_seed = (uint32_t)mapX * 12347u
                               + (uint32_t)mapY * 7919u;
            uint32_t phase = (SHARED_UC->frame_count + (cell_seed >> 4))
                             & 0xFF;
            if (phase < 6) {
                uint32_t flicker = SHARED_UC->frame_count * 1103515245u
                                 + cell_seed;
                if (flicker & 0x800000) wall_shade = 12;
            }
        }

        /* Per-column LOD: hi-res chevron when the wall hit is close
         * enough to read the motif, lo-res noise otherwise. Adjacent
         * columns can land on opposite sides of the threshold — that's
         * the seam tradeoff we accepted up front. */
        const uint8_t *tex_data;
        int tex_w, tex_h, tile_x, tile_y;
        if (spotted) {
            /* Spotted partitions use the olive polka-dot wallpaper. */
            tex_data = (const uint8_t *)partition_tex_ram;   /* SDRAM-staged */
            tex_w    = PARTITION_TEX_WIDTH;
            tex_h    = PARTITION_TEX_HEIGHT;
            tile_x   = PARTITION_TILE_X;
            tile_y   = PARTITION_TILE_Y;
        } else if (perpDist < WALL_LOD_THRESHOLD) {
            tex_data = (const uint8_t *)wall_tex_hi_ram;     /* SDRAM-staged */
            tex_w    = WALL_TEX_HI_WIDTH;
            tex_h    = WALL_TEX_HI_HEIGHT;
            tile_x   = WALL_TILE_HI_X;
            tile_y   = WALL_TILE_HI_Y;
        } else {
            tex_data = (const uint8_t *)wall_tex_ram;        /* SDRAM-staged */
            tex_w    = WALL_TEX_WIDTH;
            tex_h    = WALL_TEX_HEIGHT;
            tile_x   = WALL_TILE_X;
            tile_y   = WALL_TILE_Y;
        }
        const int tex_w_mask = tex_w - 1;
        const int tex_h_mask = tex_h - 1;

        fx_t wall_hit = partition_hit
            ? partition_wallhit_w
            : (side == 0)
                ? (py + FX_MUL(perpDist, rayDirY))
                : (px + FX_MUL(perpDist, rayDirX));
        wall_hit -= (fx_t)FX_INT(wall_hit) << FX_SHIFT;
        /* wall_hit ∈ [0, FX_ONE), tex_w*tile_x ≤ 1024 → product ≤ 67M
         * which doesn't fit in int32 signed but does fit in uint32. The
         * uint32 cast lets SH-2 use a single MUL.L (~3 cycles) instead
         * of the int64 software multiply (~40 cycles). Mask wraps. */
        int texX = (int)(((uint32_t)wall_hit * (uint32_t)(tex_w * tile_x))
                          >> FX_SHIFT)
                   & tex_w_mask;

        int lineHeight = (int)divu_read();

        int wall_bot  = horizon_y + ((lineHeight * eye_h) >> 8);
        /* Partial-height partition: a low divider anchored at the floor. Keep
         * the floor line (wall_bot) but shorten the drawn column so the ceiling
         * shows above it; the texture maps over the reduced band. part_height is
         * a fraction*256 (0 = full). */
        int draw_lineHeight = (part_height)
                            ? ((lineHeight * part_height) >> 8)
                            : lineHeight;
        int wall_top  = wall_bot - draw_lineHeight;
        /* Partial divider owns this column's z: record its band top so the
         * sprite pass can draw a standup's rows ABOVE it (see-over). */
        if (part_height) {
            int16_t pt = (int16_t)(wall_top < 1 ? 1 : wall_top);
            PART_TOP(col) = pt;
            for (int j = 1; j < cstep; j++) PART_TOP(col + j) = pt;
        }
        int drawStart = wall_top < 0 ? 0 : wall_top;
        int drawEnd   = wall_bot >= SCREEN_H ? SCREEN_H - 1 : wall_bot;
        if (drawEnd >= fg_clip) drawEnd = fg_clip - 1;   /* band-covered rows */
        if (drawEnd < drawStart) goto overlay_pass;      /* fully hidden */

        /* VOID EXIT cell (world_map == 2): a missing wall — an OPENING, not a
         * black fill. Draw no wall at all, so the ceiling grid and carpet
         * already painted for this column show through, floor and ceiling
         * running out and fogging into the distance: the see-through expanse
         * of the lobby doorway. The cell is passable (see cell_passable) and
         * stepping up to it portals out (raycast_door_portal_check). */
        if (hit_cell == 2 && !partition_hit) {
            WALL_DIST(col) = 0x7FFFFFFF;             /* no wall: nothing to occlude */
            for (int j = 1; j < cstep; j++) WALL_DIST(col + j) = 0x7FFFFFFF;
            goto overlay_pass;
        }

        /* Embedded DOOR (decal kind 1): on the columns it covers, draw the door
         * texture full-height AS THE WALL — replacing the chevron, not overlaying
         * it — so there is no overdraw. Opaque + column-major (sequential reads
         * down the column). Falls through to the normal chevron everywhere else. */
        if (!partition_hit && n_active > 0) {
            fx_t hx = px + FX_MUL(perpDist, rayDirX);
            fx_t hy = py + FX_MUL(perpDist, rayDirY);
            int door_drawn = 0;
            for (int ai = 0; ai < n_active; ai++) {
                int d = active_decal[ai];
                if (decals[d].kind != 1) continue;
                fx_t along; int flip;
                if (decals[d].axis) {
                    if (FX_ABS(hy - decals[d].y) > FX(0.2)) continue;
                    along = hx - (decals[d].x - DECAL_DOOR_HW);
                    flip = (py > decals[d].y);      /* viewed from the south (looking N) */
                } else {
                    if (FX_ABS(hx - decals[d].x) > FX(0.2)) continue;
                    along = hy - (decals[d].y - DECAL_DOOR_HW);
                    flip = (px < decals[d].x);      /* viewed from the west (looking E) */
                }
                if (along < 0 || along > 2 * DECAL_DOOR_HW) continue;
                /* REAL recess geometry: at the hit break each sideDist holds
                 * the NEXT crossing along its axis -- inside the one-cell
                 * cavity that IS the side-wall depth (along axis) and the
                 * back-wall depth (depth axis). Drift across the footprint
                 * per unit depth is the along-axis rayDir component; dpu is
                 * the depth-axis one (world-anchors the side wallpaper). All
                 * free here; the door fn classifies by comparing the pair. */
                fx_t side_d = decals[d].axis ? sideDistX : sideDistY;
                fx_t back_d = decals[d].axis ? sideDistY : sideDistX;
                if (partition_hit) {            /* slab: keep the 1-deep look */
                    side_d = 0x7FFFFFFF;
                    back_d = perpDist + FX_ONE;
                }
                fx_t drift = decals[d].axis ? rayDirX : rayDirY;
                fx_t dpu   = decals[d].axis ? rayDirY : rayDirX;
                /* Low edge of the DOOR CELL in along-space (offset from the
                 * footprint origin, <= 0): the cavity behind the thin reveal
                 * opens to the CELL's full width, not the door's. */
                fx_t cell_lo = decals[d].axis
                    ? (((fx_t)mapX << FX_SHIFT) - (decals[d].x - DECAL_DOOR_HW))
                    : (((fx_t)mapY << FX_SHIFT) - (decals[d].y - DECAL_DOOR_HW));
                /* Big door fill lives in its own function so this hot loop stays
                 * inside the SH-2 I-cache (see draw_door_column's comment). */
                draw_door_column(fb, col, hr, along, flip, drawStart, drawEnd,
                                 wall_top, draw_lineHeight, perpDist, wall_shade,
                                 horizon_y, eye_h, side_d, back_d, drift,
                                 cell_lo, dpu);
                door_drawn = 1;
                break;
            }
            /* Door drawn as the wall: skip the chevron draw but FALL THROUGH
             * to the partial-partition overlay — skipping it left a vertical
             * SEAM in any band crossing the door's columns (the beam split
             * exactly at the door in the crouch screenshots). */
            if (door_drawn) goto overlay_pass;
        }

        /* DIVU latency hide #2: start tex_step = (tex_h*tile_y
         * << 16) / lineHeight, then do detail_factor + shade_lut in
         * parallel. The shade_lut loop alone is ~256 cycles, far
         * exceeding the 39-cycle DIVU latency. */
        divu_start_u32((uint32_t)((tex_h * tile_y) << FX_SHIFT),
                       (uint32_t)draw_lineHeight);   /* texture maps over the drawn band */

        int detail_factor;
        if (spotted) {
            /* Subtle up close, fading to nothing with distance — you only
             * notice the dots on the near partition; far ones go plain yellow. */
            if (perpDist < FX(2)) {
                detail_factor = PARTITION_DETAIL;
            } else if (perpDist < FX(3.5)) {
                detail_factor = (int)(((FX(3.5) - perpDist) * PARTITION_DETAIL) / FX(1.5));
                if (detail_factor < 0) detail_factor = 0;
            } else {
                detail_factor = 0;
            }
        } else if (perpDist < FX(2)) {
            detail_factor = WALL_PATTERN_MAX;
        } else if (perpDist < FX(3.5)) {
            fx_t remaining = FX(3.5) - perpDist;
            fx_t span      = FX(1.5);
            detail_factor  = (int)((remaining * WALL_PATTERN_MAX) / span);
            if (detail_factor < 0)               detail_factor = 0;
            if (detail_factor > WALL_PATTERN_MAX) detail_factor = WALL_PATTERN_MAX;
        } else {
            detail_factor = 0;
        }
        /* Column-major wall_tex: tex_data[texX * tex_h ..] is the
         * contiguous tex_h-byte strip this loop walks. Sized for the
         * largest possible TEX_H (hi-res 64), tiny stack cost. */
        const uint8_t *wall_col = tex_data + texX * tex_h;

        /* Two-stage LUT build. wall_tex values are 0..4 (5 buckets
         * from bake_wall.py --levels 5). Precompute the final palette
         * byte for each bucket once per column, then the per-ty loop
         * becomes a pure table indirection — no multiply, shift, or
         * clamp per ty. At hi-res LOD (tex_h=64) this cuts ~5 cycles
         * × 64 ty ≈ 320 cycles per column × 160 cols per CPU ≈ ~2ms
         * per CPU per frame on wall-heavy scenes. */
        uint8_t lut5[5];
        /* Both chevron walls and the spotted partitions use the yellow
         * WALL_BASE ramp — same color scheme, only the motif differs (the
         * spotted partition just swaps the chevron texture for the dots). */
        uint8_t lut_base = WALL_BASE;
        for (int v = 0; v < 5; v++) {
            int pattern = (v * detail_factor) >> 4;
            int s = wall_shade + pattern;
            if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
            lut5[v] = (uint8_t)(lut_base + s);
        }
        uint8_t shade_lut[WALL_TEX_HI_HEIGHT];
        if (detail_factor) {   /* uniform when faded — skip the build, flat-fill below */
            for (int ty = 0; ty < tex_h; ty++) shade_lut[ty] = lut5[wall_col[ty]];
        }

        /* Baseboard molding: bottom ~3% of the wall in world space gets
         * a darker flat-shade band, the iconic Backrooms wood-trim look.
         * Anchored to wall_bot (unclipped) so the strip sits at the same
         * world height regardless of whether the wall extends off-screen.
         * Split the inner pixel loop in two so the per-pixel hot path
         * stays branch-free: wall portion runs the textured loop, then
         * the baseboard portion writes the flat color. */
        int base_h = draw_lineHeight >> 5;
        if (base_h < 1) base_h = 1;
        int base_y = wall_bot - base_h;
        int wall_end;
        if      (base_y > drawEnd)    wall_end = drawEnd;
        else if (base_y <= drawStart) wall_end = drawStart - 1;
        else                          wall_end = base_y - 1;
        /* Molding color = the yellow wall background (no chevron/dot offset),
         * the same on the main walls and the spotted partitions. */
        uint8_t mold_base  = WALL_BASE;
        uint8_t base_color = (uint8_t)(mold_base + wall_shade);
        /* 1-pixel darker line at the wall/molding boundary suggests
         * the shadow gap of a recessed baseboard — a small depth cue
         * that reads as the molding standing slightly proud of the
         * wall. Two shades darker than the molding base. */
        int shadow_shade = wall_shade + 2;
        if (shadow_shade > SHADE_LEVELS - 1) shadow_shade = SHADE_LEVELS - 1;
        uint8_t shadow_color = (uint8_t)(mold_base + shadow_shade);

        fx_t tex_step = (fx_t)divu_read();
        fx_t tex_pos  = (fx_t)(drawStart - wall_top) * tex_step;
        uint8_t *p = (uint8_t *)fb + col + drawStart * SCREEN_W;
        /* Record this anchor's drawn silhouette for the SEAMS=SMOOTH post-pass.
         * We're past every void/hidden/see-over goto, so a solid wall WILL fill
         * here — exactly the columns worth smoothing. */
        if (seam_rec) {
            seam_top[col] = (int16_t)drawStart;
            seam_bot[col] = (int16_t)drawEnd;
            /* seam_top[col] set above is the validity flag; depth from WALL_DIST(col) */
        }
        if (lod && perpDist < quad_depth) quad_depth = perpDist;  /* drives next quad's res */
        /* Hand-rolled SH-2 asm wall column inner loop. 4 pixels per
         * iteration, no spills, indexed byte load via @(R0,Rm), DT-
         * driven count-down for one cmp/bra per 4 pixels.
         *
         * Per pixel: mov+shlr16+and+mov.b(load)+add+mov.b(store)+add
         * = 7 instructions. 4 pixels + dt + bf = 30 instructions per
         * 4-pixel iter. GCC's auto-unrolled C version was generating
         * around 36-40 with some spills; the asm version both keeps
         * tex_pos/p/lut/step/mask in registers across the unroll AND
         * schedules the load-use chains tightly so SH-2's narrow
         * pipeline stays full.
         *
         * Preserves the original post-loop state of p and tex_pos
         * (advances exactly (wall_end - drawStart + 1) writes) so the
         * baseboard loop below picks up at the right framebuffer row. */
        int total = wall_end - drawStart + 1;
        if (hr) {
            /* Half-res path: word-store the (col,col+1) pair per row. A single C
             * loop covers both flat and textured (no asm) — we already pay only
             * half the columns, and the word-store halves the byte-traffic, so
             * the per-pixel C overhead is dwarfed by what we skipped. */
            /* hr is loop-invariant, so each hr>=2 test hoists out — one 4px
             * longword store per row in quarter, one 2px word in half. Only this
             * main fill goes quarter; overlay/embed below stay half (lean). */
            if (total > 0) {
                if (detail_factor == 0) {
                    if (hr >= 2) {
                        uint32_t fl = LDUP(lut5[0]);
                        for (int k = 0; k < total; k++) { *(uint32_t *)p = fl; p += SCREEN_W; }
                    } else {
                        uint16_t fw = WDUP(lut5[0]);
                        for (int k = 0; k < total; k++) { *(uint16_t *)p = fw; p += SCREEN_W; }
                    }
                } else if (hr >= 2) {
                    for (int k = 0; k < total; k++) {
                        uint8_t pix = shade_lut[(tex_pos >> FX_SHIFT) & tex_h_mask];
                        *(uint32_t *)p = LDUP(pix);
                        p += SCREEN_W;
                        tex_pos += tex_step;
                    }
                } else {
                    for (int k = 0; k < total; k++) {
                        uint8_t pix = shade_lut[(tex_pos >> FX_SHIFT) & tex_h_mask];
                        *(uint16_t *)p = WDUP(pix);
                        p += SCREEN_W;
                        tex_pos += tex_step;
                    }
                }
            }
            int by = wall_end + 1;
            if (hr >= 2) {
                if (by <= drawEnd) { *(uint32_t *)p = LDUP(shadow_color); p += SCREEN_W; by++; }
                for (; by <= drawEnd; by++) { *(uint32_t *)p = LDUP(base_color); p += SCREEN_W; }
            } else {
                if (by <= drawEnd) { *(uint16_t *)p = WDUP(shadow_color); p += SCREEN_W; by++; }
                for (; by <= drawEnd; by++) { *(uint16_t *)p = WDUP(base_color); p += SCREEN_W; }
            }
        } else if (vert) {
            /* VERTICAL half-res: full-width columns, but store only EVEN rows;
             * the line table shows each even row twice (2px-tall blocks). This
             * halves the 320-strided, uncached framebuffer stores that BIND this
             * loop (per PERF_30FPS: the store is ~60% of per-pixel cost). C loop,
             * not the 4px asm — we've already halved the stores (the bottleneck),
             * so a second row-strided asm variant isn't worth the surface area.
             * Falls through to the molding/baseboard on the same even grid. */
            const int RS = SCREEN_W << 1;              /* even-row byte stride */
            int y = (drawStart + 1) & ~1;              /* first even row >= drawStart */
            uint8_t *pv = (uint8_t *)fb + col + y * SCREEN_W;
            if (total > 0 && detail_factor == 0) {
                uint8_t flat = lut5[0];
                for (; y <= wall_end; y += 2) { *pv = flat; pv += RS; }
            } else if (total > 0) {
                fx_t tstep2 = tex_step << 1;
                fx_t tp = (fx_t)(y - wall_top) * tex_step;
                for (; y <= wall_end; y += 2) {
                    *pv = shade_lut[(tp >> FX_SHIFT) & tex_h_mask];
                    pv += RS;
                    tp += tstep2;
                }
            }
            /* Molding shadow line then baseboard, snapped onto the even grid. */
            if (y <= drawEnd) { *pv = shadow_color; pv += RS; y += 2; }
            for (; y <= drawEnd; y += 2) { *pv = base_color; pv += RS; }
        } else {
        if (total > 0 && detail_factor == 0) {
            /* Faded distance: the pattern adds nothing, so the whole wall
             * column is one flat color. Skip the shade_lut build (above) and
             * the per-pixel texture sampling — just fill. Advances p like the
             * textured path so the baseboard below lines up. Big win for far
             * walls and (64-tall) spotted partitions. */
            uint8_t flat = lut5[0];
            for (int k = 0; k < total; k++) { *p = flat; p += SCREEN_W; }
        } else if (total > 0) {
            int iters4 = total >> 2;
            int tail   = total & 3;
            if (iters4 > 0) {
                /* clang LSP runs with host-arch register widths and
                 * flags every 32-bit fx_t operand below as
                 * "size doesn't match register width". sh-elf-gcc
                 * (32-bit SH-2 registers) sees no mismatch. Silence
                 * the LSP-only noise here. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wasm-operand-widths"
#endif
                __asm__ __volatile__ (
                    "1:\n\t"
                    /* pixel A */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* pixel B */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* pixel C */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* pixel D */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* DT decrements iters4 and sets T when zero. */
                    "dt    %[it4]\n\t"
                    "bf    1b\n\t"
                    : [tp] "+r"(tex_pos), [p] "+r"(p), [it4] "+r"(iters4)
                    : [step] "r"(tex_step), [mask] "r"(tex_h_mask),
                      [lut] "r"(shade_lut), [sw] "r"((int)SCREEN_W)
                    : "r0", "r1", "memory"
                );
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
            }
            while (tail-- > 0) {
                *p = shade_lut[(tex_pos >> FX_SHIFT) & tex_h_mask];
                p += SCREEN_W;
                tex_pos += tex_step;
            }
        }
        /* Top row of the molding gets the shadow-line color, rest of
         * the strip stays at base_color. */
        int by = wall_end + 1;
        if (by <= drawEnd) {
            *p = shadow_color;
            p += SCREEN_W;
            by++;
        }
        for (; by <= drawEnd; by++) {
            *p = base_color;
            p += SCREEN_W;
        }
        }   /* end full-res fill path (else of `if (hr)`) */

        /* ── Wall-embedded outlet ───────────────────────────────────────────
         * Paint the outlet plate INTO this finished wall column when the hit
         * point lands on a decal's footprint. It draws at the wall's own depth,
         * so it's genuinely part of the surface (correct perspective + occlusion)
         * instead of a billboard. Cheap: only runs when the map placed a decal,
         * and the footprint test rejects almost every column with one compare. */
        if (n_active > 0) {
            fx_t hx = px + FX_MUL(perpDist, rayDirX);
            fx_t hy = py + FX_MUL(perpDist, rayDirY);
            /* Plane tolerance must exceed the partition HALF_THICK (0.15) so a
             * decal placed on the partition CENTRE line still matches its thick
             * face (the visible face sits +/-0.15 off centre). Solid walls hit
             * exact integer planes, so this wide band never false-matches them
             * (the nearest other plane is a whole cell away). */
            for (int ai = 0; ai < n_active; ai++) {
                int d = active_decal[ai];
                /* 0 = small outlet plate (opaque fill, shaded with the wall),
                 * 1 = the full-height fire DOOR (drawn as the wall, skipped),
                 * others = GENERIC wall-mounted sprites from sprite_defs —
                 * community wall decals (bake_sprite.py --mount wall) render
                 * through the outlet's machinery: offset decode against the
                 * sprite's ramp, texel 0 transparent, wall-shade fold. */
                int dk = decals[d].kind;
                if (dk == 1) continue;   /* door drawn as the wall, not an overlay */
                const sprite_def_t *sd = 0;
                if (dk != 0) {
                    if (dk >= SPRITE_DEF_COUNT) continue;
                    sd = &sprite_defs[dk];
                    if (!sd->tex || sd->mount != SPRITE_MOUNT_WALL) continue;
                }
                fx_t dhw = sd ? sd->world_hw : DECAL_OUTLET_HW;
                fx_t dH  = sd ? sd->world_h  : DECAL_OUTLET_H;
                int dtw  = sd ? sd->w : OUTLET_TEX_WIDTH;
                int dth  = sd ? sd->h : OUTLET_TEX_HEIGHT;
                const uint8_t *dtex = sd ? sd->tex : (const uint8_t *)outlet_tex;

                fx_t along;
                if (decals[d].axis) {                 /* wall plane = Y, spans X */
                    if (FX_ABS(hy - decals[d].y) > FX(0.2)) continue;
                    along = hx - (decals[d].x - dhw);
                } else {                              /* wall plane = X, spans Y */
                    if (FX_ABS(hx - decals[d].x) > FX(0.2)) continue;
                    along = hy - (decals[d].y - dhw);
                }
                if (along < 0 || along > 2 * dhw) continue;
                int otx = (int)(((int64_t)along * dtw) / (2 * dhw));
                if (otx < 0) otx = 0;
                else if (otx >= dtw) otx = dtw - 1;
                /* Outlet + community wall sprites are ROW-major (the door,
                 * the one column-major texture, never reaches this overlay). */
                const uint8_t *col_base = dtex + otx;
                int col_step = dtw;

                /* Vertical band: centre at height fraction z, height dH, through
                 * lineHeight. wall_bot is the floor line; up the wall subtracts. */
                int oc = wall_bot - (int)(((int64_t)lineHeight * decals[d].z) >> FX_SHIFT);
                int oh = (int)(((int64_t)lineHeight * dH) >> (FX_SHIFT + 1));
                if (oh < 1) oh = 1;
                int oy0  = oc - oh, oy1 = oc + oh;
                int span = oy1 - oy0;
                if (span < 1) span = 1;
                int ylo  = oy0 < drawStart ? drawStart : oy0;
                int yhi  = oy1 > drawEnd   ? drawEnd   : oy1;
                uint8_t *po = (uint8_t *)fb + col + ylo * SCREEN_W;
                int oshade = (wall_shade * 5) >> 4;   /* outlet fog/light fold */
                /* Precomputed texture-row step instead of a per-pixel software
                 * divide (SH-2 has no fast integer divide) — one divide per
                 * column, then add+shift per pixel. The big door fill made the
                 * old per-pixel /span the frame-killer. */
                fx_t oty_step = ((fx_t)dth << FX_SHIFT) / span;
                fx_t oty_fx   = (fx_t)(ylo - oy0) * oty_step;
                for (int yy = ylo; yy <= yhi; yy++) {
                    int oty = (int)(oty_fx >> FX_SHIFT);
                    if ((unsigned)oty < (unsigned)dth) {
                        int tv = col_base[oty * col_step];
                        if (sd) {
                            /* Community wall sprite: 0 transparent; its own
                             * palette runs base+1(dark)..base+7(bright), so
                             * the wall-shade fold SUBTRACTS toward dark,
                             * exactly like the outlet's. */
                            if (tv) {
                                /* SPRITE_F_ARTPAL: the palette is the ARTIST'S
                                 * colours, not a fog ramp of one material, so
                                 * walking the index does not darken it — it
                                 * RECOLOURS it. A cream-over-grey-tan wallpaper
                                 * tear lost its cream and read as a sage blob
                                 * (verified by replaying this fold over the
                                 * baked texels at every shade level). Art keeps
                                 * its own colours; only genuine gloom (the
                                 * deepest fold) drops it to its dark end. */
                                int ob;
                                if (sd->flags & SPRITE_F_ARTPAL)
                                    ob = (oshade >= 4) ? 1 : tv;
                                else
                                    ob = tv - oshade;
                                if (ob < 1) ob = 1;
                                uint8_t oc8 = (uint8_t)(sd->base + ob);
                                if (hr >= 2) *(uint32_t *)po = LDUP(oc8); else if (hr) *(uint16_t *)po = WDUP(oc8); else *po = oc8;
                            }
                        } else {
                            int ob = tv - oshade; if (ob < 0) ob = 0;
                            uint8_t oc8 = (uint8_t)(OUTLET_BASE + ob);
                            if (hr >= 2) *(uint32_t *)po = LDUP(oc8); else if (hr) *(uint16_t *)po = WDUP(oc8); else *po = oc8;
                        }
                    }
                    oty_fx += oty_step;   /* advance the texture row per screen pixel */
                    po += SCREEN_W;
                }
                break;   /* one decal per column */
            }
        }

        /* ── Partial-height partition overlay ───────────────────────────────
         * Draw the foreground partial partition as a textured band OVER the
         * background just drawn, so a wall/stem behind it shows above it. It's
         * validated to be in front (fg_t < perpDist), so it draws over the
         * column unconditionally. Replicates the wall pass's shade/texture/
         * baseboard so it reads identical — just shorter. Only reached for the
         * see-over case (lobby T-stem); free-standing dividers were promoted to
         * the fast main path above (fg_hit cleared). */
        overlay_pass: ;
        {
            uint16_t ovl_t0 = fg_n ? prof_frt_read() : 0;
        if (fg_n) {
            /* The nearest band is the closest solid surface in this column, so
             * the sprite z-buffer must read its depth — otherwise the ceiling
             * lights (drawn later, z-tested per column) bleed through it.
             * Save what WALL_DIST held first (the background wall) into
             * BG_DIST so the ceiling-tail passes can still z-test the rows
             * ABOVE a see-over band against the real backdrop. */
            {
                int32_t bgd = WALL_DIST(col) >> 12;
                uint8_t bg8 = (uint8_t)(bgd > 255 ? 255 : bgd);
                BG_DIST(col) = bg8;
                for (int j = 1; j < cstep; j++) BG_DIST(col + j) = bg8;
            }
            WALL_DIST(col) = fg_t[0];
            for (int j = 1; j < cstep; j++) WALL_DIST(col + j) = fg_t[0];
        }
        /* B-fix: remember the nearest slab in this quad (index 0 = nearest in
         * painter order) so the NEXT quad's cstep can veto the LOD drop. */
        if (lod && fg_n && fg_t[0] < quad_pt) quad_pt = fg_t[0];
        /* Draw the kept partials FAR to NEAR (painter's order): the divider
         * behind a counter renders first, the counter over it. */
        for (int fk = fg_n - 1; fk >= 0; fk--) {
            fx_t ft  = fg_t[fk];
            int  fht = fg_height[fk];
            int  fst = fg_style[fk], fsd = fg_side[fk];
            int flh  = fk ? (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT),
                                          (uint32_t)ft)
                          : fg_flh0;                     /* computed at clip time */
            int fdlh = (flh * fht) >> 8;                /* band height in px */
            if (fdlh > 0) {
                /* fbot = bottom edge of the band: the partial sits on the
                 * floor line. */
                int fbot = horizon_y + ((flh * eye_h) >> 8);
                int ftop = fbot - fdlh;
                int fds  = ftop < 0 ? 0 : ftop;
                int fde  = fbot >= SCREEN_H ? SCREEN_H - 1 : fbot;
                /* #7 parity: the nearest see-over counter owns this column's
                 * PART_TOP, so the sprite pass lets a standup's head show ABOVE
                 * the band instead of vanishing column-wide (mirrors the wall
                 * path's promoted-divider write near line 5969). Only the
                 * nearest slab (fk==0) and only when see-over (fht < eye_h) — a
                 * full-height T-stem stays a full occluder (PART_TOP left 0). */
                if (fk == 0 && fht < eye_h) {
                    int16_t pt = (int16_t)(ftop < 1 ? 1 : ftop);
                    PART_TOP(col) = pt;
                    for (int j = 1; j < cstep; j++) PART_TOP(col + j) = pt;
                }
                if (fk && fde >= fg_clip) fde = fg_clip - 1;  /* behind near band */
                /* Shade: distance ramp + uniform + N/S side-shade, then cell-
                 * light cap — matching the wall pass exactly so the arm reads
                 * one step darker than the (E/W-facing) stem, not the same. */
                /* Same surface_shade() as the walls -- so a slab fogs, caps to
                 * fixture light, and (the part it USED to skip) darkens under a
                 * low ceiling and in a dark room like everything around it (#12:
                 * counters no longer glow in the gloom). A free-standing slab's
                 * own hit cell IS the open cell, so no viewer-side back-step. */
                int lit = 0, lowceil_dark = 0, room_dark = 0;
                {
                    int lx = FX_INT(px + FX_MUL(ft, rayDirX));
                    int ly = FX_INT(py + FX_MUL(ft, rayDirY));
                    if ((unsigned)lx < (unsigned)MAP_W && (unsigned)ly < (unsigned)MAP_H) {
                        lit = CELL_LIGHT(ly, lx) & LIGHT_BOOST_MAX;
                        if (g_lowceil_active && CEIL_H(ly, lx) != CEIL_H_FULL) lowceil_dark = 1;
                        if (g_dark_active && (CELL_LIGHT(ly, lx) & CELL_DARK)) room_dark = 1;
                    }
                }
                int fsh = surface_shade(ft, fsd, lit, lowceil_dark, room_dark);
                /* Texture + detail (spotted dots fade with distance). */
                const uint8_t *ftex; int ftw, fth, ftlx, ftly, fdetail;
                if (fst) {
                    ftex = (const uint8_t *)partition_tex_ram;
                    ftw = PARTITION_TEX_WIDTH;  fth = PARTITION_TEX_HEIGHT;
                    ftlx = PARTITION_TILE_X;    ftly = PARTITION_TILE_Y;
                    if (ft < FX(2))        fdetail = PARTITION_DETAIL;
                    else if (ft < FX(3.5)) { fdetail = (int)(((FX(3.5) - ft) * PARTITION_DETAIL) / FX(1.5)); if (fdetail < 0) fdetail = 0; }
                    else                     fdetail = 0;
                } else {
                    /* Chevron: keep the 64x64 texture at ALL distances, like the
                     * spotted branch above, and let fdetail carry the distance
                     * fade. Half-height counters ONLY render through this overlay
                     * path (they never promote to the main wall pass), so the
                     * main pass's hi->lo LOD swap at WALL_LOD_THRESHOLD = FX(2)
                     * was firing at arm's length here and dropping every counter
                     * past 2 cells to the 16x16 wall_tex — a quarter-texel-density
                     * "stair-stepped" look across the whole room. The swap only
                     * ever paid off on genuinely distant FULL-height walls; on a
                     * draw-bound partition the texture size is ~free. */
                    ftex = (const uint8_t *)wall_tex_hi_ram;
                    ftw = WALL_TEX_HI_WIDTH; fth = WALL_TEX_HI_HEIGHT;
                    ftlx = WALL_TILE_HI_X;   ftly = WALL_TILE_HI_Y;
                    if (ft < FX(2)) {
                        fdetail = WALL_PATTERN_MAX;
                    } else if (ft < FX(3.5)) {
                        fdetail = (int)(((FX(3.5) - ft) * WALL_PATTERN_MAX) / FX(1.5));
                        if (fdetail < 0) fdetail = 0;
                        if (fdetail > WALL_PATTERN_MAX) fdetail = WALL_PATTERN_MAX;
                    } else {
                        fdetail = 0;
                    }
                }
                fx_t fwh = fg_wallhit[fk] - ((fx_t)FX_INT(fg_wallhit[fk]) << FX_SHIFT);
                int ftexX = (int)(((uint32_t)fwh * (uint32_t)(ftw * ftlx)) >> FX_SHIFT) & (ftw - 1);
                const uint8_t *fcol = ftex + ftexX * fth;
                uint8_t flut[5];
                for (int v = 0; v < 5; v++) {
                    int s = fsh + ((v * fdetail) >> 4);
                    if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
                    flut[v] = (uint8_t)(WALL_BASE + s);
                }
                int fbase_h  = fdlh >> 5; if (fbase_h < 1) fbase_h = 1;
                int ftex_end = (fbot - fbase_h) - 1;     /* texture above, molding below */
                if (ftex_end > fde) ftex_end = fde;
                int fmask = fth - 1;
                fx_t fstep = (fx_t)divu_u32((uint32_t)((fth * ftly) << FX_SHIFT),
                                            (uint32_t)fdlh);
                fx_t fpos  = (fx_t)(fds - ftop) * fstep;
                uint8_t *fp = (uint8_t *)fb + col + fds * SCREEN_W;
                int y = fds;
                int frows = ftex_end - y + 1;
                uint16_t ovl_px_t0 = prof_frt_read();
                if (frows > 0 && fdetail == 0 && part_diag != 1) {
                    /* Zero detail: all five flut entries collapse to flut[0],
                     * so texel loads buy nothing — flat-fill, the main path's
                     * detail_factor==0 shortcut brought to the overlay. fpos
                     * is dead after this band (shadow/molding are flat). */
                    if (hr >= 2) {
                        uint32_t fl = LDUP(flut[0]);
                        for (; y <= ftex_end; y++) { *(uint32_t *)fp = fl; fp += SCREEN_W; }
                    } else if (hr) {
                        uint16_t fw = WDUP(flut[0]);
                        for (; y <= ftex_end; y++) { *(uint16_t *)fp = fw; fp += SCREEN_W; }
                    } else {
                        uint8_t f8 = flut[0];
                        for (; y <= ftex_end; y++) { *fp = f8; fp += SCREEN_W; }
                    }
                } else if (frows > 0 && !hr && part_diag != 1) {
                    /* Textured band, full-res: the main wall loop's 4x-unrolled
                     * asm shape with the extra texel->flut hop (two indexed
                     * byte loads per pixel; texel values are 0..4 so the
                     * sign-extending mov.b is safe). One dt/bf per 4 pixels. */
                    int it4 = frows >> 2, ftl = frows & 3;
                    y = ftex_end + 1;              /* loop consumes every row */
                    if (it4 > 0) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wasm-operand-widths"
#endif
                        __asm__ __volatile__ (
                            "1:\n\t"
                            /* pixel A */
                            "mov   %[tp], r0\n\t"
                            "shlr16 r0\n\t"
                            "and   %[mask], r0\n\t"
                            "mov.b @(r0,%[tex]), r1\n\t"
                            "add   %[step], %[tp]\n\t"
                            "mov   r1, r0\n\t"
                            "mov.b @(r0,%[lut]), r1\n\t"
                            "mov.b r1, @%[p]\n\t"
                            "add   %[sw], %[p]\n\t"
                            /* pixel B */
                            "mov   %[tp], r0\n\t"
                            "shlr16 r0\n\t"
                            "and   %[mask], r0\n\t"
                            "mov.b @(r0,%[tex]), r1\n\t"
                            "add   %[step], %[tp]\n\t"
                            "mov   r1, r0\n\t"
                            "mov.b @(r0,%[lut]), r1\n\t"
                            "mov.b r1, @%[p]\n\t"
                            "add   %[sw], %[p]\n\t"
                            /* pixel C */
                            "mov   %[tp], r0\n\t"
                            "shlr16 r0\n\t"
                            "and   %[mask], r0\n\t"
                            "mov.b @(r0,%[tex]), r1\n\t"
                            "add   %[step], %[tp]\n\t"
                            "mov   r1, r0\n\t"
                            "mov.b @(r0,%[lut]), r1\n\t"
                            "mov.b r1, @%[p]\n\t"
                            "add   %[sw], %[p]\n\t"
                            /* pixel D */
                            "mov   %[tp], r0\n\t"
                            "shlr16 r0\n\t"
                            "and   %[mask], r0\n\t"
                            "mov.b @(r0,%[tex]), r1\n\t"
                            "add   %[step], %[tp]\n\t"
                            "mov   r1, r0\n\t"
                            "mov.b @(r0,%[lut]), r1\n\t"
                            "mov.b r1, @%[p]\n\t"
                            "add   %[sw], %[p]\n\t"
                            "dt    %[it4]\n\t"
                            "bf    1b\n\t"
                            : [tp] "+r"(fpos), [p] "+r"(fp), [it4] "+r"(it4)
                            : [step] "r"(fstep), [mask] "r"(fmask),
                              [tex] "r"(fcol), [lut] "r"(flut),
                              [sw] "r"((int)SCREEN_W)
                            : "r0", "r1", "memory"
                        );
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
                    }
                    while (ftl-- > 0) {
                        *fp = flut[fcol[(fpos >> FX_SHIFT) & fmask]];
                        fp += SCREEN_W; fpos += fstep;
                    }
                } else {
                    /* hr textured band, or J:1 legacy pricing. */
                    for (; y <= ftex_end; y++) {
                        uint8_t fc8 = flut[fcol[(fpos >> FX_SHIFT) & fmask]];
                        if (hr >= 2) *(uint32_t *)fp = LDUP(fc8); else if (hr) *(uint16_t *)fp = WDUP(fc8); else *fp = fc8;
                        fp += SCREEN_W; fpos += fstep;
                    }
                }
                int fshadow = fsh + 2; if (fshadow > SHADE_LEVELS - 1) fshadow = SHADE_LEVELS - 1;
                if (y <= fde) {
                    uint8_t fs8 = (uint8_t)(WALL_BASE + fshadow);
                    if (hr >= 2) *(uint32_t *)fp = LDUP(fs8); else if (hr) *(uint16_t *)fp = WDUP(fs8); else *fp = fs8;
                    fp += SCREEN_W; y++;
                }
                uint8_t fmold = (uint8_t)(WALL_BASE + fsh);
                for (; y <= fde; y++) {
                    if (hr >= 2) *(uint32_t *)fp = LDUP(fmold); else if (hr) *(uint16_t *)fp = WDUP(fmold); else *fp = fmold;
                    fp += SCREEN_W;
                }
                ovl_px_acc += (uint16_t)(prof_frt_read() - ovl_px_t0);

                /* COUNTERTOP as a bounded strip. Entry = the band's own top
                 * edge (attached by construction); exit = the ray's chord out
                 * of the slab footprint — ray-box exit across the thickness
                 * (one HW divide) vs out the run's end (one HW divide). This
                 * is geometrically IDENTICAL to sampling the plane per row,
                 * but with no per-row division: the sampled loop cost several
                 * whole frame budgets when a counter filled the view (the
                 * half-height sluggishness), and its brightness-capped far
                 * rows painted the light seam floating in the fog. The far
                 * end now fogs dark on the wall ramp like everything else. */
                if (fht < eye_h) {
                    int  fax   = fg_ax[fk];
                    fx_t dirU  = fax ? rayDirX : rayDirY;   /* along the run  */
                    fx_t dirN  = fax ? rayDirY : rayDirX;   /* across         */
                    fx_t adirN = FX_ABS(dirN), adirU = FX_ABS(dirU);
                    fx_t uw  = fax ? (px + FX_MUL(ft, rayDirX))
                                   : (py + FX_MUL(ft, rayDirY));
                    fx_t un0 = (fax ? (py + FX_MUL(ft, rayDirY))
                                    : (px + FX_MUL(ft, rayDirX))) - fg_line[fk];
                    /* Across-depth from the entry point to the exit face. */
                    fx_t depN = (dirN >= 0)
                        ? (PART_HALF_THICK * 2 - fg_fo[fk]) - un0
                        : un0 + fg_fo[fk];
                    if (depN < 0) depN = 0;
                    fx_t rem = (dirU >= 0) ? (fg_ub[fk] - uw) : (uw - fg_ua[fk]);
                    if (rem < 0) rem = 0;
                    fx_t dtn = (adirN > 64) ? fx_div_hw(depN, adirN) : (fx_t)FX(12);
                    fx_t dte = (adirU > 64) ? fx_div_hw(rem,  adirU) : (fx_t)FX(12);
                    fx_t dt  = dtn < dte ? dtn : dte;
                    /* LIP clamp — the build-127 look. The physically-true chord
                     * runs the counter's whole length into the fog, rendering a
                     * wrong-contrast band along the top (bright over dark fog,
                     * dark over bright walls: THE seam). The approved read is a
                     * thin wood lip at the front edge with the subtle gradient;
                     * the rest of the top belongs to the scene behind it. */
                    if (dt > FX(0.35)) dt = FX(0.35);
                    if (dt > 0) {
                        int flh_b = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT),
                                                  (uint32_t)(ft + dt));
                        int rowB = horizon_y + ((flh_b * (eye_h - fht)) >> 8);
                        int cs0 = rowB < 0 ? 0 : rowB;
                        int ce0 = (ftop <= fde ? ftop : fde) - 1;
                        if (ce0 >= SCREEN_H) ce0 = SCREEN_H - 1;
                        if (fk && ce0 >= fg_clip) ce0 = fg_clip - 1;
                        if (cs0 <= ce0) {
                            /* #7 occlusion: the counter's silhouette TOP is the
                             * cap's back edge (cs0), not the band top (ftop, set
                             * as PART_TOP earlier). A sprite behind must clip
                             * ABOVE the cap or it paints over the top plane — the
                             * "neanderthal seen through the counter" bug. Tighten
                             * PART_TOP to the cap for the nearest see-over slab. */
                            if (fk == 0) {
                                int16_t pt = (int16_t)(cs0 < 1 ? 1 : cs0);
                                PART_TOP(col) = pt;
                                for (int j = 1; j < cstep; j++) PART_TOP(col + j) = pt;
                            }
                            int s0 = fsh;                 /* front edge shade */
                            int s1;                       /* back edge: wall fog ramp */
                            {
                                fx_t tb = ft + dt;
                                if (tb < FX(2.5)) s1 = (int)((tb * 2) / FX(2.5));
                                else { fx_t past = tb - FX(2.5);
                                       fx_t span = FOG_RAMP_DIST - FX(2.5);
                                       s1 = 2 + (int)((past * 13) / span); }
                                s1 += 1;
                                /* The far end FOGS like the walls do — no
                                 * brightness cap here: capping it near the
                                 * band's shade is what painted the bright
                                 * seam floating over black fog. */
                                if (s1 > SHADE_LEVELS - 1) s1 = SHADE_LEVELS - 1;
                                if (s1 < s0) s1 = s0;
                            }
                            int rows  = ce0 - cs0;
                            int sfx   = s1 << 8;          /* back (top) -> front */
                            /* s1 >= s0 by the clamp above; negate the unsigned
                             * HW quotient instead of a signed soft divide. */
                            int sstep = rows > 0
                                ? -(int)divu_u32((uint32_t)((s1 - s0) << 8),
                                                 (uint32_t)rows) : 0;
                            uint8_t *cp = (uint8_t *)fb + col + cs0 * SCREEN_W;
                            uint16_t ct_px_t0 = prof_frt_read();
                            for (int cy2 = cs0; cy2 <= ce0; cy2++) {
                                int w = sfx >> 9;         /* shade 0..15 -> wood 0..7 */
                                if (w < 0) w = 0; else if (w > 7) w = 7;
                                uint8_t wood = (uint8_t)(WOODTOP_BASE + w);
                                if (hr >= 2) *(uint32_t *)cp = LDUP(wood); else if (hr) *(uint16_t *)cp = WDUP(wood); else *cp = wood;
                                cp += SCREEN_W; sfx += sstep;
                            }
                            ovl_px_acc += (uint16_t)(prof_frt_read() - ct_px_t0);
                        }
                    }
                }
            }
        }
            if (fg_n) { ovl_acc += (uint16_t)(prof_frt_read() - ovl_t0); ovl_cols++; }
        }
        }   /* inner per-sub-block loop */
        if (lod && quad_depth != 0x7FFFFFFF) lod_prev_depth = quad_depth;
        if (lod) lod_prev_pt = quad_pt;   /* big when no slab => no veto next quad */
    }       /* per-quad loop */
    if (col_start == 0) {              /* primary half only: HUD reads these */
        prof_dda_steps = (uint16_t)dda_steps;
        prof_dda_fat   = (uint16_t)dda_fat;
        prof_pass_ovl  = (uint16_t)ovl_acc;
        prof_efg_kept  = (uint16_t)efg_kept;
        prof_runwalk   = (uint16_t)runwalk;
        prof_ovl_cols  = (uint16_t)ovl_cols;
        prof_promote_cols = (uint16_t)promote_cols;
        prof_ovl_px    = ovl_px_acc;
    }

    /* ── SEAMS=SMOOTH: silhouette anti-staircase ────────────────────────────
     * The coarse fill gave every column in a cstep-block the anchor's top and
     * bottom, so the wall→ceiling and wall→floor edges staircase. A flat face's
     * edges are straight lines on screen, so interpolate each block's edges
     * between its two anchors and repaint just the fringe: erase the overshoot
     * back to the ceiling/floor gradient (row_color, same as the clear pass),
     * or extend the wall into the undershoot. The coarse interior is untouched —
     * only the 1px silhouette moves, which is the part the eye actually reads. */
    if (seam_smooth) {
        int sample_bias = (SCREEN_H / 2 - horizon_y) + CROUCH_GRAD_SHIFT(eye_h);
        int last = col_end - cstep0;              /* need the next anchor in-half */
        for (int ac = col_start; ac < last; ac += cstep0) {
            int an = ac + cstep0;
            if (!SEAM_VALID(ac) || !SEAM_VALID(an)) continue;
            int t0 = seam_top[ac], b0 = seam_bot[ac];
            int dt = seam_top[an] - t0, db = seam_bot[an] - b0;
            if (!dt && !db) continue;            /* block already flat */
            /* Only smooth when both anchors sit on the SAME surface — i.e. their
             * depths are close. A near wall's edge can drop steeply across 4
             * columns yet still be one continuous face (small depth delta); a
             * corner or an opening is a depth JUMP. Testing depth (not the screen
             * pixel jump) lets steep-but-continuous edges smooth while leaving
             * genuine silhouette breaks hard, so we don't eat a near wall's
             * pixels into a far one behind it. Threshold: half the nearer depth. */
            fx_t d0 = WALL_DIST(ac), d1 = WALL_DIST(an);
            fx_t dd = d1 - d0; if (dd < 0) dd = -dd;
            if (dd > (d0 >> 1)) continue;        /* depth step → corner, leave hard */
            for (int j = 1; j < cstep0; j++) {
                int x  = ac + j;
                int tt = t0 + dt * j / cstep0;
                int bb = b0 + db * j / cstep0;
                if (tt < 0) tt = 0; else if (tt >= SCREEN_H) tt = SCREEN_H - 1;
                if (bb < 0) bb = 0; else if (bb >= SCREEN_H) bb = SCREEN_H - 1;
                uint8_t *fx = fb + x;
                /* TOP: shift the wall's top edge from t0 to tt. */
                if (tt > t0) {                   /* overshoot → restore ceiling */
                    for (int y = t0; y < tt; y++) {
                        int sy = y + sample_bias;
                        if (y <= horizon_y && sy > SCREEN_H / 2) sy = SCREEN_H / 2;
                        if (sy < 0) sy = 0; else if (sy >= SCREEN_H) sy = SCREEN_H - 1;
                        fx[y * SCREEN_W] = row_color[sy];
                    }
                } else if (tt < t0) {            /* undershoot → extend wall up */
                    uint8_t wc = fx[t0 * SCREEN_W];
                    for (int y = tt; y < t0; y++) fx[y * SCREEN_W] = wc;
                }
                /* BOTTOM: shift the wall's bottom edge from b0 to bb. */
                if (bb < b0) {                   /* overshoot → restore floor */
                    for (int y = bb + 1; y <= b0; y++) {
                        int sy = y + sample_bias;
                        if (sy < 0) sy = 0; else if (sy >= SCREEN_H) sy = SCREEN_H - 1;
                        fx[y * SCREEN_W] = row_color[sy];
                    }
                } else if (bb > b0) {            /* undershoot → extend wall down */
                    uint8_t wc = fx[b0 * SCREEN_W];
                    for (int y = b0 + 1; y <= bb; y++) fx[y * SCREEN_W] = wc;
                }
            }
        }
    }

    /* ── Half→full dither DISSOLVE ──────────────────────────────────────────
     * Runs only at full res during the up-transition (transient, while still —
     * budget is relaxed). Re-double a dither-selected, shrinking fraction of
     * column pairs back to half over their wall band: the even column is copied
     * onto the odd, so that pair reads 2px like half-res. As the level decays
     * 3→2→1→0, fewer pairs are faked and the wall focus-pulls up to full. The
     * dith4 order spreads the faked pairs so the sweep isn't banded. */
    if (dissolve) {
        static const uint8_t dith4[4] = { 0, 2, 1, 3 };
        for (int x = col_start; x + 1 < col_end; x += 2) {
            if (!SEAM_VALID(x)) continue;
            if (door_hi >= 0 && x <= door_hi && x + 1 >= door_lo)
                continue;                        /* door stays sharp through the pull */
            if (dith4[(x >> 1) & 3] >= dissolve) continue;   /* this pair stays full */
            int y0 = seam_top[x], y1 = seam_bot[x];
            uint8_t *sx = fb + x;
            for (int y = y0; y <= y1; y++) sx[y * SCREEN_W + 1] = sx[y * SCREEN_W];
        }
    }

    /* ── Quarter-res boundary DITHER ────────────────────────────────────────
     * Real spatial ordered-dither: ramp the right side of each 4px block toward
     * the NEXT block's color with a 4x4 Bayer threshold that rises left→right
     * (~10/30/60% at cols +1/+2/+3). The flat 4px block becomes a dithered
     * gradient into its neighbour, so the hard shade step softens. Static per
     * frame (no stutter); the Bayer blends on both axes so it reads smooth on the
     * PVM and as an intentional dither texture (not a blocky step) on sharp HDMI.
     * Runs only at quarter (cstep==4) and only over each block's wall band.
     *
     * DITHER_COL collapses the Bayer evaluation to a lookup, BIT-IDENTICAL to the
     * three-threshold form it replaces. Walking the table shows only ONE of the
     * three tests can ever pass for a given row, and which one is a fixed
     * function of y&3:
     *     y&3=0 {0,8,2,10}  -> br[3]=10<9 no, br[2]=2<5 YES, br[1]=8<2 no  -> col 2
     *     y&3=1 {12,4,14,6} -> br[3]=6<9 YES                               -> col 3
     *     y&3=2 {3,11,1,9}  -> br[3]=9<9 no, br[2]=1<5 YES                 -> col 2
     *     y&3=3 {15,7,13,5} -> br[3]=5<9 YES                               -> col 3
     * So the old inner loop spent 3 table loads, 3 compares and 2 untaken
     * branches per row to re-derive a constant, plus a y*SCREEN_W multiply. This
     * pass MEASURED 1,804 ticks at quarter -- it was consuming quarter-res's
     * entire advantage over half (F:10 both), which is why quarter looked
     * pointless. Aliasing is safe: a block writes cols +1..+3 and reads +4, the
     * next block's left edge, which this pass never writes. */
    static const uint8_t DITHER_COL[4] = { 2, 3, 2, 3 };
    if (qdither) {
        for (int ac = col_start; ac + 4 < col_end; ac += 4) {
            if (!SEAM_VALID(ac)) continue;
            if (door_hi >= 0 && ac <= door_hi && ac + 4 >= door_lo)
                continue;                        /* door columns are real — don't smear */
            int y0 = seam_top[ac], y1 = seam_bot[ac];
            uint8_t *row = fb + ac + y0 * SCREEN_W;
            for (int y = y0; y <= y1; y++, row += SCREEN_W)
                row[DITHER_COL[y & 3]] = row[4];   /* toward next block's left col */
        }
    }

    /* ── WALLS=LOD: dither the FAR quads ────────────────────────────────────
     * The render already drew each quad at its own resolution (near = 4 full
     * cols, mid = 2 half-pairs, far = 1 quarter block — the actual perf win), so
     * nothing needs re-blocking here. This just softens the far quarter blocks:
     * a Bayer ramp of the block's right side toward the next quad, same as the
     * global DITHER but keyed to depth (WALL_DIST >= LOD_T_FAR). */
    /* DEAD while LOD's far tier is 2 (half-pairs): this loop blends columns +1..+3
     * of a 4px block, which only exists at cstep 4, so running it now would smear
     * half-pairs. It measured 314 ticks when it did run, so it was never why LOD
     * underperformed. Kept and compiled out, so a future quarter tier flips it
     * back on with this one constant. */
    #define LOD_FAR_QUARTER 0
    if (lod && LOD_FAR_QUARTER) {
        /* Same DITHER_COL collapse as the quarter pass above, identical output. */
        for (int q = col_start; q + 4 < col_end; q += 4) {
            if (!SEAM_VALID(q) || WALL_DIST(q) < LOD_T_FAR) continue;  /* far quads only */
            int y0 = seam_top[q], y1 = seam_bot[q];
            uint8_t *r = fb + q + y0 * SCREEN_W;
            for (int y = y0; y <= y1; y++, r += SCREEN_W)
                r[DITHER_COL[y & 3]] = r[4];   /* toward next quad's left col */
        }
    }
}

/* The other tex_pos reference in this file is in the old fixed-split
 * placeholder. Compiler dead-code-eliminates it once raycast_render
 * stops calling fixed-range walls. */

/* Profile counters. Both written by raycast_render, read by m_main.c
 * for the on-screen overlay. half = primary's parallel work
 * (clear+ceiling+carpet+walls of its column range); idle = time
 * spent spinning on the secondary-done sync after that. */
volatile uint16_t prof_primary_idle_ticks = 0;
volatile uint16_t prof_primary_half_ticks = 0;
/* Per-pass FRT breakdown of the primary's half (clear/ceiling/carpet/walls). */
volatile uint16_t prof_pass_clear = 0, prof_pass_ceil = 0,
                  prof_pass_carpet = 0, prof_pass_walls = 0;
/* SERIAL TAIL — the primary-only passes that run AFTER the sync barrier while
 * the secondary idles, previously unmeasured (~25% of the frame). slab =
 * low_ceiling + bulkheads (crawlspace, scene-dependent); sprite = lights +
 * standups. The line-table head-bob (~0.05ms) is left out. */
volatile uint16_t prof_pass_slab = 0, prof_pass_sprite = 0;
/* draw_lights split out of prof_pass_sprite: the light loop visits ALL
 * NUM_LIGHTS (procgen lays a fixture every 2 cells = 225 on a 32x32) and culls
 * INSIDE the body, so off-screen fixtures still cost an iteration + ~6 muls
 * each. This counter says whether that rejection sweep is worth indexing away.
 * With this split, prof_pass_sprite is STANDUPS ONLY. */
volatile uint16_t prof_pass_lights = 0;
/* OD: percent of the primary's screen area covered by wall strips. The ceiling
 * pass paints rows [0,horizon) and the carpet pass paints [horizon,SCREEN_H),
 * both for EVERY column and both BEFORE the wall pass -- so whatever the walls
 * later cover was drawn and immediately buried. That makes this percentage the
 * share of G+R that is pure overdraw. Costs ~100 ticks to compute (one pass over
 * split columns, after the W bracket closes so it does not inflate W). */
volatile uint16_t prof_wall_cover = 0;
static inline uint16_t prof_frt_read(void) {
    uint8_t hi = SH2_FRT_FRCH;
    uint8_t lo = SH2_FRT_FRCL;
    return ((uint16_t)hi << 8) | lo;
}

/* Clear the framebuffer for a column range, using row_color[] for the
 * per-row constant fill. Each CPU calls this on its own half so the
 * clear runs in parallel and no row is touched twice. col_start must
 * be a multiple of 4 (we use 32-bit stores = 4 pixels per write). */
RAMTEXT void raycast_clear_half(int col_start, int col_end) {
    uint8_t *fb = fb_pixels();
    uint32_t *fb32 = (uint32_t *)fb;
    int col_words = (col_end - col_start) >> 2;
    int col_word_start = col_start >> 2;
    /* VERT: only even rows are ever displayed (line table), so clear only those
     * — halves this pass's stores. Odd rows keep stale content, unseen. */
    int ystep = SHARED_UC->wall_vert ? 2 : 1;
    /* Background fill must follow the shifted horizon — otherwise the
     * ceiling-vs-floor color split stays pinned at SCREEN_H/2 while the
     * wall draws and grid overlays move with pitch, producing a visible
     * band of "ceiling gray" below the walls (when looking down) or
     * "floor orange" above them (looking up). Sample row_color at
     * (y - horizon_y + SCREEN_H/2) so the table's ceiling/floor gradient
     * stays centered on the shifted horizon. */
    int horizon_y   = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    /* Crouch slides the row_color gradient UP — the SAME shift the look-pitch
     * applies via sample_bias, just decoupled from horizon_y so the geometry
     * stays flat to the player. Lower eye => more bright carpet before the
     * fade, the fade-point creeps toward the horizon (and the ceiling fogs
     * sooner as it looms). Both CPUs run this on their half, so the secondary
     * matches. The carpet and ceiling-grid passes apply the identical term so
     * the whole tone gradient travels together. */
    int sample_bias = (SCREEN_H / 2 - horizon_y)
                    + CROUCH_GRAD_SHIFT(SHARED_UC->eye_h);
    for (int y = 0; y < SCREEN_H; y += ystep) {
        int sy = y + sample_bias;
        /* Ceiling rows must never sample floor colors: the crouch shift would
         * otherwise pull floor (mustard) up above the horizon — the bleed band
         * the ceiling-grid then drew dense lines into. Clamp the ceiling side
         * to the fog midpoint; the floor side keeps the full shift. */
        if (y <= horizon_y && sy > SCREEN_H / 2) sy = SCREEN_H / 2;
        if (sy < 0)         sy = 0;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        uint8_t  c   = row_color[sy];
        uint32_t c32 = ((uint32_t)c << 24) | ((uint32_t)c << 16)
                     | ((uint32_t)c <<  8) |  (uint32_t)c;
        uint32_t *row = fb32 + y * (SCREEN_W / 4) + col_word_start;
        for (int x = 0; x < col_words; x++) row[x] = c32;
    }
}

/* ── AUTO resolution boundaries (the "boundaries we establish") ─────────────
 * frame_ema is the smoothed frame period — HIGHER = slower frame. These are the
 * trigger points, in one place to tune. Half is the normal moving tier; QUARTER
 * is last-resort — it only arms when the frame is much slower than the half
 * boundary, AND you're already at half, AND moving. Raised QTR_ON for tolerance:
 * quarter now needs a genuinely struggling frame, not just "a bit heavy". */
#define AUTO_HALF_ON   13500   /* above → drop to half   (~sub-F:13) */
#define AUTO_HALF_OFF  11250   /* below → back to full   (~F:16+)    */
#define AUTO_QTR_ON    19000   /* above → allow quarter  (was 16000; more tolerant) */
#define AUTO_QTR_OFF   15500   /* below → release quarter back to half (stays in half band) */

/* Frames of held-still before AUTO heads to full res. Tiny: the instant input
 * stops we snap to half, then collapse straight to full. Nonzero so a one-frame
 * is_walking dropout mid-stride can't strobe. */
#define STILL_FULL_FRAMES 2
/* Per-level dwell while COARSENING down (full→half→quarter) — kept gentle so the
 * drop into low-res never pops while moving. Sharpening UP uses RES_STEP_UP. */
#define RES_STEP_FRAMES 4
/* Sharpen-UP dwell: 1 = collapse the up-transition to full as fast as frames
 * allow. A snap-guard below still shows the half breather for one frame first. */
#define RES_STEP_UP 1
/* Half→full dither dissolve (the one transition with no intermediate res).
 * Collapsed to a single ~50% softener frame: kick 2, peel 2/frame → one dither
 * frame then full. Pure frame-count, no compute cost. */
#define DISSOLVE_STEPS 2
#define DISSOLVE_DECAY 2

void raycast_render(void) {
    uint16_t prof_start = prof_frt_read();
    /* Re-asserted by draw_panel_face if this frame paints live tube noise;
     * the ULTRA park reads it after the render to decide whether parking
     * would freeze something that is supposed to move. */
    SHARED_UC->pvm_static_live = 0;

    /* ULTRA TWIN: m_main's rest-pair render — the same world state again with
     * the half-column camera jitter. Every piece of adaptive state below is
     * FROZEN (no EMA sample, no AUTO/ratchet/dissolve stepping, no split
     * nudge, no dense latch): the pass functions re-read last frame's
     * published values, so the twin differs from its partner by the jitter
     * and nothing else. A res decision flipping between the pair would show
     * as 30Hz wobble in the parked flip. */
    const int ultra_twin = SHARED_UC->ultra_twin != 0;

    /* Frame-period EMA for adaptive resolution (WALLS=AUTO). Delta between
     * successive raycast_render entries = full frame period in FRT ticks. The
     * 16-bit FRT wraps past 65536 (sub-11fps), so unwrap a single overflow like
     * the on-screen T: readout. EMA (7/8 + 1/8) smooths it so AUTO reacts over a
     * few frames, not to one-frame spikes. */
    static uint16_t frame_prev_frt = 0;
    static uint32_t frame_ema = 9000;    /* ~F:20 seed so AUTO doesn't lurch at boot */
    if (!ultra_twin) {
        uint16_t fdraw = (uint16_t)(prof_start - frame_prev_frt);
        frame_prev_frt = prof_start;
        uint32_t fdelta = (fdraw < 3000) ? (uint32_t)fdraw + 65536u : fdraw;
        frame_ema = (frame_ema - (frame_ema >> 3)) + (fdelta >> 3);
    }

    if (!ultra_twin) {
    /* Resolve the effective wall half-res from the menu mode. Lobby always full.
     * AUTO: hysteresis on the frame period — drop to half above ~F:13.3, return
     * to full below ~F:16; the deadband stops per-frame flip-flop (and the loop
     * self-stabilizes since switching res moves the period across the band). */
    static int auto_hr = 1;
    int eff_hr;
    int vert = 0;                          /* VERT: skip odd rows + row-double via line table */
    int lod  = 0;                          /* LOD: full render + depth-banded re-block post-pass */
    int dissolve_out = 0;                  /* AUTO half→full dither dissolve level (0 = off) */
    if (g_lobby_ceiling) {
        eff_hr = 0;
    } else {
        uint8_t mode = SHARED_UC->wall_res_mode;
        if      (mode == 0) eff_hr = 0;
        else if (mode == 1) eff_hr = 1;
        else if (mode == 3) eff_hr = 0;   /* SERIAL diagnostic: full res */
        else if (mode == 4) eff_hr = 2;   /* QTR: force quarter — inspect the chunk in isolation */
        else if (mode == 5) { eff_hr = 0; vert = 1; }  /* VERT: full horizontal + vertical half-res */
        else if (mode == 6) { eff_hr = 0; lod  = 1; }  /* LOD: full render + depth-banded re-block */
        else {
            if      (frame_ema > AUTO_HALF_ON)  auto_hr = 1;   /* slower → half */
            else if (frame_ema < AUTO_HALF_OFF) auto_hr = 0;   /* faster → full */
            eff_hr = auto_hr;
            /* MOTION-GATED QUARTER (Mike's notion): on a VERY heavy frame, drop to
             * quarter-res ONLY while the player is moving — the 4px chunk is masked
             * by motion, and it clips the worst spikes; snap back to half/full the
             * instant they stand still. Own deadband so it can't flip-flop: arm at
             * frame_ema>16000 (~F:11 or worse), disarm once it eases below 13000.
             * Requires we're already at half (auto_hr==1); the overlay's 70KB of
             * stack headroom makes quarter safe everywhere now. */
            /* QUARTER was nerfed out of AUTO on 2026-08-06 and is back as a
             * RUNTIME toggle (TESTING>AUTOQTR, default off = the shipped
             * behaviour), because the measurement that removed it did not
             * cover the case it existed for.
             *
             * That A/B was the test corridor, standing, WALLS pinned, F:07-11:
             * quarter+dither tied half at F:10, so it looked worse for nothing.
             * But the rung only ever arms at frame_ema > AUTO_QTR_ON (~F:09 or
             * worse) AND while moving, which is a regime the capture never
             * entered. And "tied" is what 1,467 saved ticks looks like when the
             * frame is vblank-locked at ~2,800 a vblank and is not sitting near
             * a boundary — the same 1,467 is a whole vblank when it is.
             *
             * So: same binary, flip it mid-scene, and trust the feel of a heavy
             * moving frame over a standing corridor number. */
            static int auto_q = 0;
            if      (frame_ema > AUTO_QTR_ON)  auto_q = 1;
            else if (frame_ema < AUTO_QTR_OFF) auto_q = 0;
            if (SHARED_UC->auto_qtr && auto_q && auto_hr == 1 && is_walking)
                eff_hr = 2;
            /* STILLNESS RATCHET (Mike): motion masks the chunk, so AUTO happily
             * sits at half/quarter while you move through a busy room. But the
             * instant you STOP to take stock, judder can't hide anything and fps
             * matters less (nothing's moving) — so spend the frame on detail.
             * Count stationary frames and step resolution UP the longer we hold
             * still, OVERRIDING the frame_ema clamp that otherwise pins a busy
             * room at half. Beat 1 (quarter→half) is already instant on stop
             * because quarter is motion-gated above; beat 2 lifts to full once we
             * hold past the "taking stock" pause. Moving resets it to 0, snapping
             * straight back to the responsive low-res the instant you step off. */
            static int still_frames = 0;
            if (is_walking) still_frames = 0;
            else if (still_frames < STILL_FULL_FRAMES) still_frames++;
            int still_full = (still_frames >= STILL_FULL_FRAMES);
            if (still_full) eff_hr = 0;   /* held still → full (taking stock) */

            /* SMOOTH the transition. A hard full→quarter pop is jarring even while
             * moving, so ease the published res one level per RES_STEP_FRAMES —
             * it steps full→half→quarter over <1s and motion hides the small
             * steps. The stillness sharpen stays snappy: once held still and
             * heading to full, step every frame so it reads as a deliberate snap. */
            static int cur_hr = 0, step_ctr = 0;
            int target = eff_hr;
            /* The instant input stops, hop straight to half as a 1-frame step —
             * never sit at quarter while still. `snapped` holds the ramp off for
             * that one frame so the half breather is actually visible before we
             * collapse to full. Quarter is motion-only, so this fires only on the
             * moving→still edge. */
            int snapped = 0;
            if (!is_walking && cur_hr > 1) { cur_hr = 1; step_ctr = 0; snapped = 1; }
            /* Asymmetric ease: coarsening DOWN stays gentle (RES_STEP_FRAMES) so the
             * drop into low-res never pops while moving; sharpening UP collapses
             * (RES_STEP_UP=1) since going to full costs no visible perf. */
            int step_frames = (target < cur_hr) ? RES_STEP_UP : RES_STEP_FRAMES;
            if (!snapped && cur_hr != target) {
                if (++step_ctr >= step_frames) { step_ctr = 0; cur_hr += (target > cur_hr) ? 1 : -1; }
            } else if (cur_hr == target) {
                step_ctr = 0;
            }
            eff_hr = cur_hr;

            /* Dither dissolve: half→full is the one step with no intermediate
             * resolution, so kick a focus-pull the frame we reach full FROM half
             * and decay it over DISSOLVE_STEPS frames of full-res rendering. */
            static int prev_cur = 0, dissolve_ctr = 0;
            if (prev_cur == 1 && cur_hr == 0)      dissolve_ctr = DISSOLVE_STEPS;
            else if (cur_hr != 0)                  dissolve_ctr = 0;   /* coarsened again: abort */
            else if (dissolve_ctr > 0)             dissolve_ctr -= DISSOLVE_DECAY;
            if (dissolve_ctr < 0) dissolve_ctr = 0;
            prev_cur = cur_hr;
            dissolve_out = dissolve_ctr;
        }
    }
    SHARED_UC->wall_halfres = (uint8_t)eff_hr;
    SHARED_UC->wall_vert    = (uint8_t)vert;
    SHARED_UC->wall_lod     = (uint8_t)lod;
    SHARED_UC->wall_dissolve = (uint8_t)dissolve_out;
    /* Partition-dense latch from LAST frame's wall pass. Only meaningful in LOD
     * (the near-band drop is a LOD lever); wide hysteresis so halving the near
     * band — which lowers the cost the latch reads — can't oscillate it. */
    {
        static uint8_t dense_latch = 0;
        if      (prof_pass_walls > WALL_DENSE_ON)  dense_latch = 1;
        else if (prof_pass_walls < WALL_DENSE_OFF) dense_latch = 0;
        SHARED_UC->wall_dense = lod ? dense_latch : 0;
    }
    }   /* !ultra_twin: end of the frozen adaptive-state block */

    /* Vertical head bob is applied below via the framebuffer line table —
     * no position translation needed (lateral sway felt like drunk
     * swagger, not walking). */

    /* The camera basis (dir/plane) is derived per-pass from the player snapshot
     * now — the wall pass and the self-contained sprite/tail passes each build
     * their own, so raycast_render no longer threads it through. */

    /* Snapshot player state for the secondary to read via cache-through.
     * Must land before COMM4 wakes the secondary so it sees the new frame. */
    SHARED_UC->player.x     = player.x;
    SHARED_UC->player.y     = player.y;
    SHARED_UC->player.angle = player.angle;
    SHARED_UC->is_walking   = is_walking;   /* gates carpet footsteps in pump */
    SHARED_UC->is_running   = is_running;   /* pump plays them faster when sprinting */

    /* Camera pitch — eased manual hold-C tilt (pitch_smooth_y) plus
     * the ±1 walking pitch bob from bob_phase. The bob couples to the
     * same phase as the vertical line-table bob so foot-strike dips
     * both pitch AND vertical image in lockstep. Clamp the combined
     * value to int8_t before publishing to SHARED_UC. */
    int pitch_combined = pitch_smooth_y;
    if (is_walking) {
        pitch_combined += (int)((SIN_FX(bob_phase) * 1) >> FX_SHIFT);
    }
    /* Stand-up head dip — transient look-down while rising out of a crouch
     * (positive = look down). Eased in player_update; 0 at rest. */
    pitch_combined += standup_dip;
    if (pitch_combined > 127)  pitch_combined = 127;
    if (pitch_combined < -128) pitch_combined = -128;
    SHARED_UC->pitch_y = (int8_t)pitch_combined;

    __asm__ __volatile__("" ::: "memory");

    /* Single dispatch: secondary does clear + ceiling + carpet + walls for
     * cols 160..319, primary does the same for cols 0..159 in parallel.
     * Column ownership eliminates the previous CEILING→WALLS sequential
     * dependency (primary used to idle ~26ms waiting for secondary's ceiling
     * before walls could start). One sync point at the end. */
    /* Adaptive load balance: nudge the split column to equalize last frame's
     * primary (H) and secondary (S) half-render FRT times. Feedback controller,
     * no per-column cost model — converges in a few frames as the view changes.
     * On emulators H/S read 0, so split stays at SCREEN_W/2 (static 50/50). */
    static int split = SCREEN_W / 2;
    if (!ultra_twin) {   /* twin reuses the partner frame's split unchanged */
        int h = (int)prof_primary_half_ticks;            /* last frame, primary  */
        int s = (int)SHARED_UC->secondary_render_ticks;  /* last frame, secondary */
        int sum = h + s;
        if (sum > 500) {                                 /* valid FRT reading */
            int shift = ((h - s) * SCREEN_W) / (sum << 1);  /* full balancing step */
            shift >>= 1;                                 /* damp to avoid oscillation */
            if      (shift >  16) shift =  16;
            else if (shift < -16) shift = -16;
            split -= shift;                              /* h>s: primary overloaded -> shrink */
            /* Clamp was [64, 256] — but the pinned-split matrix measured a
             * ~7x per-column cost spread across the screen at counter poses
             * (leftmost overlay+door columns ~650 ticks/col vs ~94 at the
             * right), so true balance can want a split well under 64. The
             * controller kept pushing into the old clamp and stuck there,
             * one CPU 1.8x busier than the other. 16 keeps the 4-px word
             * alignment and a nonzero share for each CPU. */
            if      (split < 16)            split = 16;
            else if (split > SCREEN_W - 16) split = SCREEN_W - 16;
            split &= ~3;                                 /* clear pass writes 4-px words */
        }
        SHARED_UC->split_col = (uint16_t)split;          /* secondary reads this */
        prof_split_col = (uint16_t)split;
    }

    MARS_SYS_COMM4 = MARS_CMD_HALF;

    /* WALLS=SERL diagnostic: let the secondary finish its whole half BEFORE
     * the primary draws — zero render overlap, so the primary's per-pass
     * ticks measure pure work with no cross-CPU framebuffer-bus contention.
     * Comparing the same pose in FULL vs SERL splits "real per-column cost"
     * from "the two CPUs stalling each other": identical W in both means the
     * work is real; W collapsing in SERL convicts the bus. */
    if (SHARED_UC->wall_res_mode == 3) {
        while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
            __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                                 "nop\n\tnop\n\tnop\n\tnop\n\t"
                                 "nop\n\tnop\n\tnop\n\tnop\n\t"
                                 "nop\n\tnop\n\tnop\n\tnop");
        }
    }

    uint16_t pp = prof_frt_read();
    raycast_clear_half(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_clear  = (uint16_t)(n - pp); pp = n; }
    raycast_draw_ceiling_grid(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_ceil   = (uint16_t)(n - pp); pp = n; }
    raycast_draw_carpet(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_carpet = (uint16_t)(n - pp); pp = n; }
    raycast_draw_walls(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_walls  = (uint16_t)(n - pp); }
    /* OD — wall coverage as a percent of this half's screen area. seam_top/bot
     * hold each column's wall extent and are reset to -1 per frame, so this is a
     * read of work already done. Outside the W bracket on purpose. */
    {
        uint32_t covered = 0;
        for (int c = 0; c < split; c++) {
            if (!SEAM_VALID(c)) continue;
            int t = seam_top[c], b = seam_bot[c];
            if (t < 0) t = 0;
            if (b >= SCREEN_H) b = SCREEN_H - 1;
            if (b >= t) covered += (uint32_t)(b - t + 1);
        }
        uint32_t area = (uint32_t)split * SCREEN_H;
        prof_wall_cover = area ? (uint16_t)((covered * 100u) / area) : 0;
    }

    uint16_t idle_start = prof_frt_read();
    prof_primary_half_ticks = (uint16_t)(idle_start - prof_start);
    while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
        /* Throttle the primary-side ACK wait to reduce 68K-bridge
         * pressure — bare-loop polling at ~5M reads/sec was lining
         * up with the 68K's joypad-read window often enough to drop
         * COMM8 updates. ~30 cycles of NOPs per loop iteration brings
         * the primary poll rate down to a friendlier ~700K/sec while
         * keeping latency well under one frame. */
        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop");
    }
    prof_primary_idle_ticks = (uint16_t)(prof_frt_read() - idle_start);

    /* Commit any reordered stores from the non-volatile draw loops before
     * the next swapBuffers() makes them visible via the VDP page flip. */
    __asm__ __volatile__("" ::: "memory");

    /* Crawlspace tail (slab + bulkhead caps) AND the sprite pass, both
     * parallelized across the two SH-2s. WALL_DIST is committed for every
     * column, so dispatch CMD_TAIL: the secondary draws its half — tail THEN
     * lights+standups, in that order so the slab's z-stamp occludes sprites —
     * while the primary does the same for [0,split) here. Sprites alone were
     * ~10k serial ticks in a normal room; splitting reclaims most of it, so we
     * dispatch unconditionally now (the tail early-outs cheaply with no
     * crawlspace, and the round-trip pays for itself on the sprites). */
    /* Decouple the sprite split from the wall split so a near screen-filling
     * standup is shared, not dumped on one cpu (write BEFORE raising CMD_TAIL,
     * like split_col, so the secondary reads a settled value). The tail/slab
     * pass below still uses the wall split. */
    int sprite_split = raycast_sprite_split(split);
    SHARED_UC->sprite_split = (uint16_t)sprite_split;

    uint16_t ps = prof_frt_read();
    MARS_SYS_COMM4 = MARS_CMD_TAIL;          /* secondary: tail [split,W) + sprites [sprite_split,W) */
    raycast_draw_tail(0, split);             /* primary:   tail        [0,split)  */
    { uint16_t n = prof_frt_read(); prof_pass_slab = (uint16_t)(n - ps); ps = n; }
    /* Lights first, then standups — foreground sprites overpaint ceiling-panel
     * pixels in shared rows; the per-column z-test handles walls. */
    draw_lights(0, sprite_split);
    { uint16_t n = prof_frt_read(); prof_pass_lights = (uint16_t)(n - ps); ps = n; }
    draw_standups(0, sprite_split);
    { uint16_t n = prof_frt_read(); prof_pass_sprite = (uint16_t)(n - ps); }
    /* Barrier: wait for the secondary to finish its tail+sprite half before the
     * page flip makes the frame visible. */
    while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop");
    }

    /* Vertical head bob via framebuffer line table.
     *
     * The 32X displays pixel data through a 224-entry line table that
     * maps screen row i to an arbitrary pixel-line offset. By rewriting
     * the table each frame to map row i -> (i + bob_y), the entire
     * displayed image shifts vertically by bob_y pixels without
     * re-rendering anything. Cost is 224 word writes (~0.05ms).
     *
     * Source lines past the ends are clamped to the first/last visible
     * line; the resulting 1-3 row duplication at the bob boundary is
     * invisible against the smooth ceiling/floor gradient. */
    int bob_y = 0;
    if (is_walking) {
        /* sin in -FX_ONE..+FX_ONE, scaled to ±2 pixels for a tight micro-bob. */
        bob_y = (int)((SIN_FX(bob_phase) * 2) >> FX_SHIFT);
    }
    /* VERT (vertical half-res): map each display row-pair to a single EVEN
     * framebuffer row (i & ~1). The passes only ever write those even rows, so
     * the odd rows are never sampled — this is what turns row-skipped rendering
     * into a coherent 2px-tall-block image. bob still shifts the whole table;
     * snapping to even just quantizes the bob to the block grid (invisible). */
    int vert_lt = SHARED_UC->wall_vert;
    volatile uint16_t *line_table = &MARS_FRAMEBUFFER;
    for (int i = 0; i < SCREEN_H; i++) {
        int src = i + bob_y;
        if (vert_lt)         src &= ~1;
        if (src < 0)         src = 0;
        if (src >= SCREEN_H) src = SCREEN_H - 1;
        line_table[i] = (uint16_t)(src * 160 + 0x100);
    }
}
