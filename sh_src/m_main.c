#include "mars.h"
#include "raycast.h"

static uint32_t lastTick = 0;
static uint16_t currentFB = 0;

static void swapBuffers(void) {
    while (lastTick == MARS_SYS_COMM12);
    MARS_VDP_FBCTL = currentFB ^ 1;
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == currentFB);
    currentFB ^= 1;
    lastTick = MARS_SYS_COMM12;
}

int m_main(void) {
    Hw32xInit(MARS_VDP_MODE_256, 0);
    Hw32xDelay(1);    /* wait for first vblank — palette is writable now */
    raycast_init();

    for (;;) {
        player_update();
        raycast_render();
        swapBuffers();
    }
    return 0;
}
