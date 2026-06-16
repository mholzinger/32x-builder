#ifndef BOX3D_H
#define BOX3D_H

/* Native real-time renderer for the cardboard box title. The mesh +
 * animation are imported from box_model.h (exported from the Blender
 * scene by tools/export_box.py) and transformed/rasterized live on the
 * SH-2 every frame — flat-shaded convex polygons, painter-sorted, with
 * near-plane clipping so the camera can dive inside the box. NOT a
 * prerendered flipbook.
 *
 * Runs before raycast_init (owns its own CRAM palette + a shimmer-free
 * flip) so the gameplay palette build reclaims CRAM afterwards. */

/* Bright CRAM slot, clear of the cardboard ramp, for menu text. */
#define BOX_TEXT_IDX 200

/* Cardboard tan ramp + text white into CRAM. Call once up front. */
void box3d_load_palette(void);

/* Play the box animation once (~1.5s), skippable with START. */
void box3d_play(void);

/* Render the menu-backdrop frame (camera at the box mouth) into the back
 * buffer. No flip. */
void box3d_show_final(void);

/* Play the trap-door fall to completion: the floor splits open and the
 * camera plummets into the void. Call on menu commit, before raycast_init
 * — the "fall into the backrooms" payoff. Ends on black. */
void box3d_play_fall(void);

/* Shimmer-free buffer flip (mirrors swapBuffers minus raycast_shimmer)
 * so the cardboard CRAM palette is left intact. */
void box3d_flip(void);

/* Rasterize the master-built shared draw-list into one framebuffer
 * half. band 0 = top, band 1 = bottom. The box renders at half
 * resolution (160x112) and each logical pixel is written as a 2x2
 * block, so this owns full-res rows [0,112) for band 0 / [112,224) for
 * band 1. Called by BOTH SH-2s — master takes band 0, slave band 1
 * (via MARS_CMD_BOX in s_main). */
void box3d_render_band(int band);

#endif
