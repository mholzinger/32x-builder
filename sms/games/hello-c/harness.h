/* The in-game Z80 harness contract as a C API (see sms/DESIGN.md §2).
 * Same fixed addresses the asm games use; the 68K bridge is identical —
 * it neither knows nor cares what language the blob came from.
 *
 * This is the devkitSMS-shaped path Mike asked about: SMSlib assumes a
 * real Master System (OUT-based VDP, mode 4, a mapper), none of which
 * exist for a Genesis Z80 in RAM — so this header IS the "SMSlib" of our
 * harness. Game logic written against it (or ported from an open-source
 * SMS game by swapping its SMSlib calls for these) compiles with SDCC
 * straight into a bootable blob. */
#ifndef HARNESS_H
#define HARNESS_H

#define TILEBUF   ((volatile unsigned char *)0x1900)  /* 24 rows x 32 cols */
#define MAP_BITS  ((const unsigned char *)0x1C00)     /* 1bpp, $80>>(x&7) */
#define SPAWN_X   (*(const unsigned char *)0x1C80)
#define SPAWN_Y   (*(const unsigned char *)0x1C81)
#define EXIT_X    (*(const unsigned char *)0x1C82)
#define EXIT_Y    (*(const unsigned char *)0x1C83)
#define HEART     (*(volatile unsigned char *)0x1F00)
#define PAD_MBX   (*(volatile unsigned char *)0x1FF4)
#define DIRTY_MBX (*(volatile unsigned char *)0x1FF5)
#define STATE_MBX (*(volatile unsigned char *)0x1FF6)
#define FRAME_MBX (*(volatile unsigned char *)0x1FF7)
#define PSG       (*(volatile unsigned char *)0x7F11)

/* pad byte bits (SEGA low byte) */
#define PAD_UP    0x01
#define PAD_DOWN  0x02
#define PAD_LEFT  0x04
#define PAD_RIGHT 0x08
#define PAD_B     0x10
#define PAD_C     0x20
#define PAD_A     0x40
#define PAD_START 0x80

/* boot-font tile ids (mars.c NextChr): digits 2-11, A-Z 12-37 */
#define T_BLANK  0
#define T_DOT    1
#define TILE_DIGIT(n) ((n) + 2)
#define TILE_LETTER(c) ((c) - 'A' + 12)

/* wait for the next 68K frame tick; returns the fresh pad byte */
static unsigned char frame_wait(void) {
    static unsigned char last;
    while (FRAME_MBX == last) HEART++;
    last = FRAME_MBX;
    return PAD_MBX;
}

#endif
