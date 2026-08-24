/* HELLO-C: proof that an SMS-style game compiles FROM C SOURCE into the
 * in-game Z80 hook (SDCC -mz80, no SMSlib — see harness.h). Draws a
 * border, prints HELLO C, and lets the D-pad walk a P tile around.
 * Not wired into the 68K yet: booting a game other than maze is the
 * multi-game milestone (DESIGN.md M3). */
#include "harness.h"

static const unsigned char msg[] = {
    TILE_LETTER('H'), TILE_LETTER('E'), TILE_LETTER('L'), TILE_LETTER('L'),
    TILE_LETTER('O'), T_BLANK, TILE_LETTER('C')
};

void main(void) {
    unsigned char x = 16, y = 12, prev = 0, pad, pressed, i;

    for (i = 0; i < 32; i++) {              /* top + bottom border */
        TILEBUF[i] = T_DOT;
        TILEBUF[23 * 32 + i] = T_DOT;
    }
    for (i = 1; i < 23; i++) {              /* side borders */
        TILEBUF[i * 32] = T_DOT;
        TILEBUF[i * 32 + 31] = T_DOT;
    }
    for (i = 0; i < sizeof msg; i++)
        TILEBUF[2 * 32 + 12 + i] = msg[i];
    TILEBUF[y * 32 + x] = TILE_LETTER('P');
    DIRTY_MBX = 1;

    for (;;) {
        pad = frame_wait();
        pressed = pad & (unsigned char)~prev;
        prev = pad;
        if (!(pressed & 0x0F))
            continue;
        TILEBUF[y * 32 + x] = T_BLANK;
        if ((pressed & PAD_UP) && y > 1) y--;
        if ((pressed & PAD_DOWN) && y < 22) y++;
        if ((pressed & PAD_LEFT) && x > 1) x--;
        if ((pressed & PAD_RIGHT) && x < 30) x++;
        TILEBUF[y * 32 + x] = TILE_LETTER('P');
        DIRTY_MBX = 1;
    }
}
