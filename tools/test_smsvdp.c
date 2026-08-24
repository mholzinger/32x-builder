/* Host harness for the shadow VDP (sh_src/smsvdp.c) — the procgen-harness
 * pattern: compile the REAL engine code on the Mac, drive it exactly the way
 * a Master System program drives the real chip (port writes only), render,
 * assert pixels, and drop a PPM for human eyes.
 *
 *   cc -Ish_src -o /tmp/vdptest tools/test_smsvdp.c sh_src/smsvdp.c
 *   /tmp/vdptest out.ppm
 *
 * The scene: border colour + full charset-free test card — a 4-tile checker
 * background with h/v-flipped variants, both palettes exercised, X/Y scroll,
 * a priority tile that must cover sprites, and three sprites (one under the
 * priority tile, two overlapping to prove lowest-number-wins). */
#include <stdio.h>
#include <string.h>
#include "smsvdp.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

/* Drive the ports like Z80 code would. */
static void ctrl(uint8_t lo, uint8_t hi) { smsvdp_write_ctrl(lo); smsvdp_write_ctrl(hi); }
static void vaddr(uint16_t a) { ctrl((uint8_t)a, (uint8_t)(0x40 | (a >> 8))); }
static void caddr(uint8_t a)  { ctrl(a, 0xC0); }
static void reg(uint8_t r, uint8_t v) { ctrl(v, (uint8_t)(0x80 | r)); }
static void data(uint8_t b) { smsvdp_write_data(b); }

/* A solid-colour 8x8 tile: colour c in all pixels (planar). */
static void tile_solid(int t, uint8_t c) {
    vaddr((uint16_t)(t * 32));
    for (int row = 0; row < 8; row++)
        for (int pl = 0; pl < 4; pl++)
            data((uint8_t)(((c >> pl) & 1) ? 0xFF : 0x00));
}
/* A tile with only its leftmost column set to colour c (flip detector). */
static void tile_leftcol(int t, uint8_t c) {
    vaddr((uint16_t)(t * 32));
    for (int row = 0; row < 8; row++)
        for (int pl = 0; pl < 4; pl++)
            data((uint8_t)(((c >> pl) & 1) ? 0x80 : 0x00));
}

int main(int argc, char **argv) {
    static uint8_t fb[192 * 256];
    smsvdp_reset();

    /* Palettes: bg pal = ramp of blues, sprite pal = ramp of reds. */
    caddr(0);
    for (int i = 0; i < 16; i++) data((uint8_t)((i & 3) << 4));        /* bg: blue ramp */
    for (int i = 0; i < 16; i++) data((uint8_t)(i & 3));               /* spr: red ramp */

    /* Tiles: 0 = colour 1 solid, 1 = colour 2 solid, 2 = left column c3,
     * 3 = solid c3 (the priority tile), 4 = sprite tile solid c2. */
    tile_solid(0, 1);
    tile_solid(1, 2);
    tile_leftcol(2, 3);
    tile_solid(3, 3);
    tile_solid(4, 2);

    /* Name table at 0x3800 (reg2 = 0x0E << ... = 0xFF gives 0x3800). */
    reg(2, 0xFF);
    vaddr(0x3800);
    for (int row = 0; row < 28; row++)
        for (int col = 0; col < 32; col++) {
            uint16_t ent;
            if (row == 6 && col == 6)      ent = 3 | 0x1000;   /* priority   */
            else if (row == 4 && col == 4) ent = 2 | 0x0200;   /* hflip      */
            else if (row == 4 && col == 6) ent = 2 | 0x0800;   /* sprite pal */
            else                           ent = (uint16_t)((row + col) & 1);
            data((uint8_t)ent); data((uint8_t)(ent >> 8));
        }

    /* Sprites: SAT at 0x3F00 (reg5 = 0x7E<<7 = 0x3F00). #0 and #1 overlap
     * (0 must win); #2 sits under the priority tile (bg must win). */
    reg(5, 0xFF);
    vaddr(0x3F00);
    data((uint8_t)(100 - 1)); data((uint8_t)(100 - 1)); data((uint8_t)(48 - 1));
    data(0xD0);                                    /* terminator */
    vaddr(0x3F80);
    data(100); data(4);                            /* #0 at (100,100) */
    data(104); data(4);                            /* #1 at (104,100), loses */
    data(52);  data(4);                            /* #2 half under priority
                                                    * tile (48..55): covered
                                                    * 52..55, visible 56..59 */

    reg(7, 0x05);           /* border colour = sprite-pal entry 5 */
    reg(0, 0x20);           /* left column blank */
    reg(1, 0x40);           /* display on */

    smsvdp_render(fb, 256);

    /* --- Assertions -------------------------------------------------- */
    /* Checker: (0,8) is row0,col1 -> tile 1 -> colour 2, bg palette. */
    CHECK(fb[0 * 256 + 12] == 2, "checker tile colour");
    /* Left-column blank painted border. */
    CHECK(fb[10 * 256 + 2] == (16 + 5), "left column blank uses border");
    /* hflip: tile 2's left column flipped to the RIGHT edge of its cell.
     * Cell (4,4): x 32..39, leftcol tile flipped -> pixel at x=39 is c3. */
    CHECK(fb[(4 * 8) * 256 + 39] == 3, "hflip moves column to right edge");
    CHECK(fb[(4 * 8) * 256 + 32] != 3, "hflip clears left edge");
    /* Palette select: cell (6,4) same tile unflipped, sprite palette:
     * left column -> 16 + 3. */
    CHECK(fb[(4 * 8) * 256 + 6 * 8] == 16 + 3, "bg tile on sprite palette");
    /* Sprite overlap: at (102,100) sprite #0 (drawn c2 -> 16+2) wins. */
    CHECK(fb[100 * 256 + 102] == 16 + 2, "lowest sprite number wins");
    /* And #1 shows where #0 ends: x=110 belongs to #1 alone. */
    CHECK(fb[100 * 256 + 110] == 16 + 2, "second sprite visible past first");
    /* Priority tile covers sprite #2: cell (6,6) = x 48..55, y 48..55. */
    CHECK(fb[50 * 256 + 50] == 3, "bg priority covers sprite");
    /* Sprite #2 visible just outside the priority cell (x=56). */
    CHECK(fb[50 * 256 + 56] == 16 + 2, "sprite emerges past priority tile");
    /* Y scroll smoke: set 8 and re-render — row 0 now shows old row 1. */
    reg(9, 8);
    smsvdp_render(fb, 256);
    CHECK(fb[0 * 256 + 12] == 1, "y scroll shifts checker phase");
    reg(9, 0);
    /* X scroll: +8 moves content right — screen x=20 shows source x=12,
     * checker col 1 = tile 1 = colour 2. */
    reg(8, 8);
    smsvdp_render(fb, 256);
    CHECK(fb[0 * 256 + 20] == 2, "x scroll shifts checker phase");
    reg(8, 0);

    /* PPM for eyes: SMS 6-bit --BBGGRR -> 24-bit. */
    if (argc > 1) {
        smsvdp_render(fb, 256);
        FILE *f = fopen(argv[1], "wb");
        fprintf(f, "P6\n256 192\n255\n");
        for (int i = 0; i < 192 * 256; i++) {
            uint8_t c = smsvdp_cram(fb[i]);
            unsigned char px[3] = {
                (unsigned char)((c & 0x03) * 85),
                (unsigned char)(((c >> 2) & 0x03) * 85),
                (unsigned char)(((c >> 4) & 0x03) * 85)
            };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        printf("wrote %s\n", argv[1]);
    }

    printf(fails ? "FAILURES: %d\n" : "all checks pass\n", fails);
    return fails ? 1 : 0;
}
