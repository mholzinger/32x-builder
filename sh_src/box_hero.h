#ifndef BOX_HERO_H
#define BOX_HERO_H

/* High-res static splash: the closed cardboard box with the SEGA CORE
 * label, rendered offline (scripts/genhero.py) and baked to one 320x224
 * paletted RLE frame (box_hero_data.h). Loads its own 256-colour palette,
 * blits the frame to both buffer pages, and holds until START — then the
 * caller hands off to the live low-res 3D box for the open + dive. A
 * static frame is nearly free, so it can be full-res and photoreal. */
void box_hero_show(void);
void box_hero_palette(void);   /* repaint the hero palette only */
/* Title-only decode scratch in the .hero_overlay (see box_hero.c). The exit
 * hole's destination peek aliases its low end during the climb. */
extern uint8_t hero_scratch[];

#endif
