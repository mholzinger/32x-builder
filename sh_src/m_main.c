#include "mars.h"
#include "menu.h"
#include "raycast.h"

static uint32_t lastTick = 0;
static uint16_t currentFB = 0;

static void swapBuffers(void) {
    while (lastTick == MARS_SYS_COMM12);
    /* In vblank now — safe palette-write window. */
    raycast_shimmer();
    MARS_VDP_FBCTL = currentFB ^ 1;
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

int m_main(void) {
    /* Release the slave SH-2. The crt0 (mars_start.s:271-273) intends
     * to do this after the init JSR but uses a stale r0 — the write
     * to "clear slave status" goes to ROM and is silently dropped.
     * Without this, the slave loops forever in its S_OK wait at
     * 0x20004024 (= MARS_SYS_COMM4) and never reaches s_main().
     *
     * Writing 0 to COMM4 changes the upper half of the 32-bit word
     * the slave is polling for "S_OK" (0x535F4F4B) → cmp/eq fails →
     * slave exits the wait and jumps to s_main. */
    MARS_SYS_COMM4 = 0;

    Hw32xInit(MARS_VDP_MODE_256, 0);
    Hw32xDelay(1);    /* wait for first vblank — palette is writable now */
    raycast_init();

    for (;;) {
        /* Read the joypad up-front so the menu can both react to
         * START and tell player_update to skip movement when open. */
        HwMdReadPad(0);
        uint16_t pad = MARS_SYS_COMM8;

        menu_update(pad);

        if (!menu_is_active()) {
            player_update();
        }
        raycast_render();
        menu_render((uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200));
        raycast_debug_overlay();    /* SH-2 step-0 sanity check */
        swapBuffers();
    }
    return 0;
}
