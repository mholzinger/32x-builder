#ifndef SMSVDP_H
#define SMSVDP_H

#include <stdint.h>

/* Shadow VDP — a true Master System mode-4 video pipeline with the silicon
 * replaced by an SH-2 (ROADMAP "Shadow VDP: real mode 4 on the 32X").
 *
 * The real VDP is write-only in practice: SMS code feeds two ports and never
 * reads back. So the whole chip reduces to (a) this state machine consuming
 * exactly those port writes, and (b) a renderer that turns its VRAM/CRAM/
 * registers into pixels. The Z80 side will push its port writes into a ring
 * the 68K drains over the dirty-epoch channel; this module neither knows nor
 * cares — it eats (port, byte) pairs from anywhere, which is what makes it
 * host-testable (tools/test_smsvdp.c renders to a PPM on the Mac).
 *
 * Mode 4, 256x192: 32x28 name table (9-bit pattern index, h/v flip, palette
 * select, priority), 4bpp planar patterns, two 16-colour palettes from
 * 6-bit --BBGGRR CRAM, sprite attribute table (8x8 or 8x16, x/index pairs,
 * 0xD0 terminator), X/Y scroll, left-column blank, display enable.
 * Not yet: zoomed sprites, per-line raster effects, the scroll-lock bits
 * (reg0 b6/b7), 8-sprites-per-line overflow flag. Renderer emits palette
 * indices 0..31 (16..31 = sprite palette); the caller maps them to CRAM. */

void smsvdp_reset(void);
void smsvdp_write_ctrl(uint8_t b);   /* port $BF */
void smsvdp_write_data(uint8_t b);   /* port $BE */
/* Render the full frame: dst is a 256-wide, 192-tall byte surface with
 * `pitch` bytes per row. Pixels are palette indices 0..31. */
void smsvdp_render(uint8_t *dst, int pitch);
/* CRAM entry as SMS 6-bit --BBGGRR, for the caller's palette bridge. */
uint8_t smsvdp_cram(int i);
/* Register read for the bridge/debug (display-enable gate etc). */
uint8_t smsvdp_reg(int r);

#endif
