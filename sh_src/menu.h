#ifndef MENU_H
#define MENU_H

#include <stdint.h>

/* Pause / settings menu.
 *
 * Flow (called from m_main's frame loop):
 *   1. menu_update(pad)  — call every frame with the current joypad
 *                          word. Handles START-edge toggle and, while
 *                          the menu is open, UP/DOWN row selection
 *                          and LEFT/RIGHT value adjust.
 *   2. menu_is_active()  — when 1, the caller should SKIP player_update
 *                          so the player doesn't move while the menu
 *                          is up. Rendering still runs so the game
 *                          screen is visible behind the menu.
 *   3. menu_render(fb)   — call after raycast_render and before the
 *                          framebuffer flip. Draws the menu overlay
 *                          on top of the rendered scene.
 *
 * The menu writes its settings directly to shared.amb_volume and
 * shared.step_volume — see shared.h. */

void menu_update(uint16_t pad);
int  menu_is_active(void);
void menu_render(uint8_t *fb);

#endif
