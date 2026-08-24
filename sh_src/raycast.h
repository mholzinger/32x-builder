#ifndef RAYCAST_H
#define RAYCAST_H

#include <stdint.h>

#define MAP_W      32
#define MAP_H      32

/* Automap overlay palette slots (first free indices past FRAME_BASE+5=122).
 * The Backrooms palette has no red by design — these exist ONLY so the
 * debug automap reads unmistakably against the yellow world. */
#define AMAP_RED        122   /* wall / partition lines */
#define AMAP_RED_BRIGHT 123   /* player arrow */
/* Asset-viewer label ink. Shared with m_main.c, painted by raycast_pal_apply.
 * Outside every ramp so no fog or shade walk can land on it, same as the
 * automap reds above. */
#define VIEWER_INK      194
/* The hand-authored entry maps (fixed_map, lobby_map) are 32x32. They load into
 * the top-left of the larger live grid with the rest filled solid wall (they're
 * self-sealed entry rooms). Procgen fills the full MAP_W x MAP_H. */
#define AUTH_W     32
#define AUTH_H     32
#define SCREEN_W   320
#define SCREEN_H   224

/* 16.16 fixed point */
typedef int32_t fx_t;
#define FX_SHIFT    16
#define FX_ONE      (1 << FX_SHIFT)
#define FX(d)       ((fx_t)((d) * 65536.0))   /* compile-time constants only */
#define FX_INT(x)   ((int32_t)(x) >> FX_SHIFT)
#define FX_MUL(a,b) ((fx_t)(((int64_t)(a) * (b)) >> FX_SHIFT))
#define FX_DIV(a,b) ((fx_t)(((int64_t)(a) << FX_SHIFT) / (b)))
#define FX_ABS(x)   ((x) < 0 ? -(x) : (x))

typedef struct {
    fx_t x, y;       /* world-space position */
    uint8_t angle;   /* 0..255 = 0..2pi */
} player_t;

extern player_t player;
/* Non-const so procgen can overwrite it at boot. The hand-tuned default
 * still lives in the .data section initializer in raycast.c. */
extern uint8_t world_map[MAP_H][MAP_W];

/* Free-standing wallpaper partitions. Same data shape as in raycast.c.
 * procgen writes into partitions[] / sets num_partitions at boot. */
#define NUM_PARTITIONS_MAX  64
typedef struct { fx_t x1, y1, x2, y2; } partition_t;
extern partition_t partitions[NUM_PARTITIONS_MAX];
/* First-class edge partitions (see raycast.c): per-cell-edge flag bytes. */
extern uint8_t pedge_w[MAP_H][MAP_W + 1];
extern uint8_t pedge_n[MAP_H + 1][MAP_W];
extern int g_pedge_any;
void pedge_clear(void);
void raycast_stamp_partition_edges(void);
extern int num_partitions;
/* Per-partition wallpaper: 0 = chevron (like the walls), 1 = spotted olive. */
extern uint8_t partition_style[NUM_PARTITIONS_MAX];
/* Per-partition render height: 0 = full, else fraction*256 (192 = 3/4 height)
 * for a low cubicle-style divider you see over (ceiling shows above). */
extern uint8_t partition_height[NUM_PARTITIONS_MAX];
/* Per-partition crawl-under: 1 = open gap at the foot, solid above; collides
 * only when standing (crouch low to crawl under). */
/* When set, the ceiling uses the lobby's hand-authored fluorescent runs. */
extern int g_lobby_ceiling;
/* Authored ceiling fixtures for the loaded map (NULL => procedural grid).
 * Declared here so the loaders in procgen.c can clear them. cm_light_t comes
 * from custom_maps.h, which includes this header — forward-declare instead. */
struct cm_light_s;
extern const struct cm_light_s *g_map_lights;
extern uint16_t                 g_map_n_lights;
struct cm_dark_s;
extern const struct cm_dark_s  *g_map_dark;
extern uint8_t                  g_map_n_dark;
/* When set, at least one low-ceiling crawlspace cell exists this map. */
extern int g_lowceil_active;
/* Per-cell ceiling height (255 = full open ceiling, lower = crawlspace slab).
 * The first-class crawlspace data model: collision, forced-crouch, light
 * culling and the slab render all read this. Loaders/procgen author it. */
extern uint8_t ceil_h[MAP_H][MAP_W];
#define CEIL_H_FULL 255           /* sentinel: cell has the full-height ceiling */
/* Named slab heights (ceil_h values; wall = 256). ONE primitive: any lowered
 * cell forces crouch-through; heights differ visually. Mirrors registry.json
 * crawl.h -- change there for maps, here for procgen/fixed callers. Each
 * DISTINCT height in a map costs one render pass-pair (engine cap: 3). */
#define CRAWL_CEIL_H    135   /* 0.53 of wall height -- classic duct slab */
#define BULKHEAD_CEIL_H 200   /* 0.78 -- taller doorway header/soffit */
void ceil_h_clear(void);          /* reset all cells to full-height ceiling */
/* Mark a straight run of `len` cells from (cx,cy) along (dx,dy) as ONE lowered
 * ceiling of height h (mouth capped as a unit). dx,dy in {0,1}. */
void ceil_h_add_run_h(int cx, int cy, int dx, int dy, int len, int h);

/* Wall-mounted decals (the lobby outlet). Count is reset per-map so the
 * outlet only renders in the lobby; the array itself lives in raycast.c. */
extern int num_decals;
/* Pepper outlets across the live world_map's visible wall faces until decals[]
 * holds `target` total. Map-agnostic; loaders and procgen both call it. */
void raycast_place_outlets(int target);

void raycast_init(void);
/* Scale the gameplay palette to brightness 0..FADE_STEPS (full..black) for
 * the lobby->map fade. Call inside vblank. */
#define FADE_STEPS 16
/* The exit-hole peek's claim on the LOW end of hero_scratch, exported so
 * other hero_scratch tenants (the Voyager PCM ring in sound.c) can start
 * PAST it instead of sizing against a stale comment. The 2026-08-08 window
 * band: the ring parked at +3KB because the peek's comment still said
 * 64x44 (~2.8KB) after the peek had grown to 96x64 (6KB) — live PCM
 * decoded straight into the peek's middle rows, and the crawl's end
 * window played it as confetti. */
#define RAYCAST_PEEK_CLAIM 6144
void raycast_set_brightness(int lvl);
void raycast_backdrop_wall(int on);   /* CRAM 0: tuned wall yellow / back to black */
void raycast_paint_wallpaper_index(int idx, int scale256);  /* wall yellow, any entry, dimmable */
void raycast_paint_chair_ramp(void);   /* chair CRAM entries, full bright */

/* Live-tunable palette (COLOR menu tab). flush() repaints in vblank when dirty;
 * the ch/warmth/sat calls are the tuner controls; the _get variants feed the UI.
 * Surface s: 0=WALL 1=FLOOR 2=CEIL 3=LIGHT. ch: 0=R 1=G 2=B. */
void raycast_pal_apply(int lvl);
void raycast_pal_flush(void);
void raycast_pal_ch(int s, int ch, int dir);
int  raycast_pal_ch_get(int s, int ch);
void raycast_pal_warmth(int dir);
int  raycast_pal_warmth_get(void);
void raycast_pal_sat(int dir);
int  raycast_pal_sat_get(void);
void raycast_pal_reset(void);   /* COLOR tab: A button — back to defaults */
/* Fill world_map/partitions and park the player. Call before raycast_init
 * (or before re-calling init_lights) so the lighting grid matches. */
void raycast_load_fixed(void);
void raycast_load_lobby(void);
/* Load a hand-authored map (index into the generated custom_maps[] table). */
void raycast_load_custom(int idx);
void raycast_render(void);
/* In-ROM asset viewer (pause menu ASSETS tab): preview baked sprite art on
 * hardware in the real palette. count/name/dims describe the catalog; preview
 * blits sprite `sel` centered at integer `zoom` into the framebuffer. */
int         raycast_asset_count(void);
const char *raycast_asset_name(int sel);
void        raycast_asset_dims(int sel, int *w, int *h);
/* Sprite assets preview as a WORLD QUAD (the neanderthal's tex_tri path):
 * yaw spins it, dist is continuous — smooth scaling via the real rasterizer. */
void        raycast_asset_preview(uint8_t *fb, int sel, uint8_t yaw, fx_t dist);
/* Live 3D mesh viewer: rotate (rotY/rotX 0..255) + project + flat-fill a model
 * into fb at zoom_px screen-pixels per world unit. variant 0 = the hero GLB
 * mesh, 1 = the in-game box model (same scale, so the comparison is exact).
 * `model` is a MODEL_* id; imported models have boxes only, so they ignore
 * variant and always draw boxes. Caller clears the background. */
/* MODEL_* enums retired: the viewer is table-driven off boxmodels[] —
 * raycast_kind_model_variants(kind) says what a kind can show. */
/* sprite_defs[] is indexed BY KIND and is sparse — retired kinds leave null
 * padding rows. Must match registry.json assets.sprites[].kind. */
/* The ONLY topple-able asset. Everything else is furniture or scenery and
 * stays put: the topple pipeline is billboard/flat-bitmap only, so a fallen
 * box model cannot even render, and a toppled object is deliberately
 * walk-through -- which is how the desk was losing its collision. A whitelist
 * on purpose, so a future imported billboard does not inherit toppling. */
#define NEANDER_ASSET_KIND 2
#define CHAIR_ASSET_KIND 3
#define DESK_ASSET_KIND  5
#define PVM_ASSET_KIND   10
/* 0 for a padding row (no texture): the viewer skips these when cycling. */
int         raycast_asset_valid(int sel);
/* Directional billboard set: which baked view `yaw` resolves to, and how many
 * views exist. -1 / *nviews = 0 when the asset has no set. */
int         raycast_asset_dir_view(int sel, uint8_t yaw, int *nviews);
int         raycast_kind_model_variants(int kind);
int         raycast_asset_model(int sel, int *kind, int *use_alt);
void        raycast_model_view(uint8_t *fb, uint8_t rotY, uint8_t rotX, int zoom_px, int variant, int wire,
                               int kind, int use_alt);
void player_update(uint16_t pad);
/* 1 when the player has stepped into the open EXIT door — fire the procgen portal. */
int  raycast_door_portal_check(void);
/* Stamp the recurring exit door (behind spawn) into the live map. procgen calls it. */
void raycast_place_exit_door(void);
void raycast_place_exit_hole(void);   /* alternate exit: dark 1-cell ceiling hole */
int  raycast_exit_hole_check(void);   /* 1 = standing centered under the hole */
int  raycast_pvm_use(void);           /* A near a PVM: toggle its power; 1 = toggled */
void raycast_exit_pullup(int t, int total);  /* climb-out camera, progress t/total */
void raycast_crawl_corridor(int t, int total); /* interior duct set piece: draws
                                * OVER the frame, three cells to the peek */
void raycast_corridor_orient(void); /* capture the hole's lit side for the
                                * corridor; call BEFORE the map flush */
void raycast_corridor_travel_init(fx_t sx, fx_t sy, uint8_t ang); /* park the
                                * live camera behind the spawn (post-flush) */
void raycast_corridor_travel(int t, int total); /* advance it with the crawl */
void raycast_duct_preview(void); /* through-panel duct facets into the peek
                                * bitmap + dissolve; call at climb commit */
void raycast_arrival_drop(int t, int total); /* fall-in camera on the far side */
#define AD_IMPACT_EYE 56   /* eye_h the drop lands compressed at (raycast.c) */
void raycast_eye_settle(void); /* stand NOW: clears the drop's compression
                                * when a scripted stand-up already played */
/* Destination peek: one low-res walls-only frame from (px,py,angle) into the
 * peek bitmap; the exit hole's back panel blits it until the next portal.
 * Call with the NEXT map live (see climb_commit's generate/peek/restore). */
void raycast_peek_render(fx_t px, fx_t py, uint8_t angle);
void raycast_peek_clear(void);
void raycast_shimmer(void);
void raycast_draw_ceiling_grid(int col_start, int col_end);
void raycast_draw_carpet(int col_start, int col_end);
void raycast_draw_walls(int col_start, int col_end);
/* Secondary CPU: drop stale partition-face cache lines before the wall pass so
 * it re-reads the primary's fresh per-frame writes. Primary never calls this. */
/* Secondary CPU: purge cell_light once per map-load (gen change) before walls. */
void raycast_purge_cell_light(void);
void raycast_clear_half(int col_start, int col_end);
/* Crawlspace tail (low-ceiling slab + bulkhead caps) for a column range — runs
 * after the wall barrier, split across both SH-2s via CMD_TAIL. */
void raycast_draw_tail(int col_start, int col_end);
/* Secondary CPU: drop stale crawlspace-geometry cache lines before CMD_TAIL. */
void raycast_purge_lowceil_cache(void);
/* Sprite pass (ceiling lights + standups) for a column range — runs after the
 * tail, split across both SH-2s in the same CMD_TAIL phase. */
void raycast_draw_sprites(int col_start, int col_end);
/* Secondary CPU: drop stale lights[] cache lines before the sprite pass. */
void raycast_purge_sprite_cache(void);
void standups_clear(void);   /* reset free-standing objects (neanderthals/chairs) */
void raycast_add_standup(fx_t x, fx_t y, uint8_t facing, uint8_t kind);
void raycast_standup_make_desk(void);   /* newest standup -> desk-PVM composite */
void raycast_pvm_desk_off(void);        /* power down the console on game exit */
int  raycast_standup_in_cell(int cx, int cy);  /* procgen spawn-overlap guard */
int  raycast_exit_path_cell(int x, int y);     /* protected spawn->exit corridor:
                                                * placers must keep these cells
                                                * traversable (exit solvability) */
int  raycast_exit_cell(int *cx, int *cy);      /* the win cell for the SMS mini-
                                                * game (door wall cell / hole
                                                * approach); 0 = no exit here */
/* Game-on-glass (DESIGN.md 4c): live SMS TILEBUF on every powered PVM.
 * set_active(1) after the headless 68K boot; sample() on the primary once
 * per frame reads the COMM broadcast and bakes the phosphor-cell frame. */
void raycast_glass_set_active(int on);
int  raycast_glass_active(void);
void raycast_glass_sample(void);
void raycast_add_dark_room(int x0, int y0, int x1, int y1);

#endif
