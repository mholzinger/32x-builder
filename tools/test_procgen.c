/* Procgen invariant harness — compile the REAL generator for the host and
 * run it over thousands of seeds, checking the promises the code itself
 * makes in its comments.
 *
 *   cc -Ish_src -o /tmp/pgtest tools/test_procgen.c sh_src/procgen.c
 *   /tmp/pgtest 5000
 *
 * Why this exists: the desk console required an all-open 3x3 footprint AND a
 * wall to back onto, which cannot both hold, so no generated level ever
 * received one — and nothing looked broken, because the console worked
 * perfectly whenever an authored map supplied it. A promise nobody measures
 * is a promise nobody keeps. Every invariant below is quoted from procgen.c;
 * if a placement rule tightens and silently un-ships a set piece, this says
 * so in seconds instead of shipping quietly.
 *
 * Fidelity note: the exit door is stubbed, so exit_path_bits stays clear and
 * the "never block the way out" rejections never fire here. That makes the
 * placement tests slightly OPTIMISTIC — a real level rejects strictly more
 * cells than this harness does. Failures found here are therefore real;
 * passes are strong but not absolute.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define memset __builtin_memset          /* sh_src/string.h shadows the system one */
#include "raycast.h"
#include "procgen.h"

#define MAX_STANDUPS_ENGINE 36           /* raycast.c MAX_STANDUPS */

/* ---- engine state procgen writes into ---- */
player_t player;
uint8_t  world_map[MAP_H][MAP_W];
uint8_t  ceil_h[MAP_H][MAP_W];
partition_t partitions[NUM_PARTITIONS_MAX];
uint8_t  partition_style[NUM_PARTITIONS_MAX];
uint8_t  partition_height[NUM_PARTITIONS_MAX];
uint8_t  partition_decor[NUM_PARTITIONS_MAX];
int      num_partitions;
uint8_t  pedge_w[MAP_H][MAP_W + 1];
uint8_t  pedge_n[MAP_H + 1][MAP_W];
int      g_pedge_any;
int      g_lobby_ceiling;
const struct cm_light_s *g_map_lights; uint16_t g_map_n_lights;
const struct cm_dark_s  *g_map_dark;   uint8_t  g_map_n_dark;
int      g_lowceil_active;
int      num_decals;

/* ---- standup table: the parts the placers consult ---- */
#define MAXS 128
static struct { int x, y; uint8_t kind, desk; } sus[MAXS];
static int nsus;
int  num_standups;

void raycast_add_standup(fx_t x, fx_t y, uint8_t facing, uint8_t kind) {
    (void)facing;
    if (nsus < MAXS) {
        sus[nsus].x = FX_INT(x); sus[nsus].y = FX_INT(y);
        sus[nsus].kind = kind;   sus[nsus].desk = 0;
        nsus++;
    }
    num_standups = nsus;
}
void standups_clear(void) { nsus = 0; num_standups = 0; }
void raycast_standup_make_desk(void) { if (nsus) sus[nsus - 1].desk = 1; }
int  raycast_standup_in_cell(int x, int y) {
    for (int i = 0; i < nsus; i++) if (sus[i].x == x && sus[i].y == y) return 1;
    return 0;
}

uint32_t exit_path_bits[MAP_H];
int  raycast_exit_path_cell(int x, int y) {
    return (int)((exit_path_bits[y] >> x) & 1u);
}
void raycast_place_exit_door(void) {}
void raycast_place_exit_hole(void) {}
void raycast_place_outlets(int t) { (void)t; }
void raycast_add_dark_room(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
void raycast_stamp_partition_edges(void) {}
void pedge_clear(void) {}
void ceil_h_clear(void) { memset(ceil_h, CEIL_H_FULL, sizeof ceil_h); }
void ceil_h_add_run_h(int cx,int cy,int dx,int dy,int len,int h){
    for (int i=0;i<len;i++){int x=cx+dx*i,y=cy+dy*i;
        if((unsigned)x<MAP_W&&(unsigned)y<MAP_H) ceil_h[y][x]=(uint8_t)h;}
}

/* ---- invariants ---- */
#define SPAWN_CX 16                      /* procgen.c */
#define SPAWN_CY 28

enum { I_SPAWN, I_REACH, I_PVM1, I_DESK, I_NEANDER, I_CAP,
       I_OVERLAP, I_INWALL, I_CRAWL, I_PARTCAP, I_COUNT };
static const char *inv_name[I_COUNT] = {
    "spawn cell is open floor",
    "every open cell reachable from spawn",
    "exactly one PVM per level",
    "the PVM is the desk console",
    "at least one neanderthal",
    "standups within the 36-slot table",
    "no two standups share a cell",
    "no standup inside a wall",
    "at least one crawlspace cell",
    "partitions within cap",
};
static int fails[I_COUNT], first_seed[I_COUNT];

static int reach_all(void) {
    static uint8_t seen[MAP_H][MAP_W];
    static int16_t qx[MAP_W*MAP_H], qy[MAP_W*MAP_H];
    memset(seen, 0, sizeof seen);
    if (world_map[SPAWN_CY][SPAWN_CX]) return 0;
    int head = 0, tail = 0;
    qx[tail] = SPAWN_CX; qy[tail] = SPAWN_CY; tail++;
    seen[SPAWN_CY][SPAWN_CX] = 1;
    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        static const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if ((unsigned)nx >= MAP_W || (unsigned)ny >= MAP_H) continue;
            if (world_map[ny][nx] || seen[ny][nx]) continue;
            seen[ny][nx] = 1; qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (!world_map[y][x] && !seen[y][x]) return 0;
    return 1;
}

static void note(int inv, int ok, int seed) {
    if (ok) return;
    if (!fails[inv]) first_seed[inv] = seed;
    fails[inv]++;
}

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 5000;
    for (int s = 0; s < n; s++) {
        int seed = s + 1;
        standups_clear();
        memset(sus, 0, sizeof sus);
        memset(exit_path_bits, 0, sizeof exit_path_bits);
        ceil_h_clear();
        num_partitions = 0;
        procgen_params_default();
        procgen_run((uint32_t)seed);

        int pvms = 0, desks = 0, neander = 0, inwall = 0, overlap = 0, crawl = 0;
        for (int i = 0; i < nsus; i++) {
            if (sus[i].kind == PVM_ASSET_KIND) { pvms++; if (sus[i].desk) desks++; }
            if (sus[i].kind == NEANDER_ASSET_KIND) neander++;
            if ((unsigned)sus[i].x < MAP_W && (unsigned)sus[i].y < MAP_H
                && world_map[sus[i].y][sus[i].x]) inwall++;
            for (int j = i + 1; j < nsus; j++)
                if (sus[i].x == sus[j].x && sus[i].y == sus[j].y) overlap++;
        }
        for (int y = 0; y < MAP_H && !crawl; y++)
            for (int x = 0; x < MAP_W; x++)
                if (ceil_h[y][x] != CEIL_H_FULL) { crawl = 1; break; }

        note(I_SPAWN,   world_map[SPAWN_CY][SPAWN_CX] == 0,      seed);
        note(I_REACH,   reach_all(),                             seed);
        note(I_PVM1,    pvms == 1,                               seed);
        note(I_DESK,    desks == 1,                              seed);
        note(I_NEANDER, neander >= 1,                            seed);
        note(I_CAP,     nsus <= MAX_STANDUPS_ENGINE,             seed);
        note(I_OVERLAP, overlap == 0,                            seed);
        note(I_INWALL,  inwall == 0,                             seed);
        note(I_CRAWL,   crawl == 1,                              seed);
        note(I_PARTCAP, num_partitions <= NUM_PARTITIONS_MAX,    seed);
    }

    int bad = 0;
    printf("procgen invariants over %d seeds\n\n", n);
    for (int i = 0; i < I_COUNT; i++) {
        if (fails[i]) {
            bad = 1;
            printf("  FAIL  %-38s %d/%d  (first seed %d)\n",
                   inv_name[i], fails[i], n, first_seed[i]);
        } else {
            printf("  ok    %-38s %d/%d\n", inv_name[i], n, n);
        }
    }
    printf("\n%s\n", bad ? "INVARIANTS BROKEN" : "all invariants hold");
    return bad;
}
