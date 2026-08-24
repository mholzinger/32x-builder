#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* Tiny 8x8 monospace font for the settings menu.
 *
 * Glyph data covers the printable chars we use in the menu — uppercase
 * A-Z, 0-9, space, '>', '+', '-', '.', ':', '|'. Anything else maps to
 * the space glyph.
 *
 * font_draw_char stamps a single glyph at (x, y) on the framebuffer
 * pointed at by `fb`. font_draw_string walks an ASCII C-string and
 * stamps glyphs 8 pixels apart horizontally.
 *
 * Background pixels (bit = 0) are LEFT UNTOUCHED — the menu renders
 * its background first with a solid fill, then stamps text on top in
 * one color. No alpha blending, no transparency. */

void font_draw_char(uint8_t *fb, int x, int y, char c, uint8_t color);
/* Same stamp, but the caller supplies the 8 row bytes instead of naming a
 * glyph from THIS font. The SMS picture draws through this with rows from
 * sms_font.h — the MD boot font, generated so both renderers share one
 * source — not from the menu glyphs above. */
void font_draw_rows(uint8_t *fb, int x, int y, const uint8_t *rows,
                    uint8_t color);
void font_draw_string(uint8_t *fb, int x, int y, const char *s, uint8_t color);

#endif
