#ifndef SOUND_H
#define SOUND_H

/* Ambient electrical-buzz looping audio. The secondary SH-2 owns audio:
 * - amb_sound_init() runs once on secondary boot. Programs the PWM hardware
 *   for the target sample rate, ramps the DC bias to avoid a pop, sets
 *   up DMA channel 1 to stream samples from ROM into MARS_PWM_MONO,
 *   then kicks off the first DMA.
 * - amb_dma_handler() runs from the secondary's DMA-complete IRQ. It just
 *   re-arms DMA1 from the start of the same buffer, producing an
 *   endless loop. The amb_buzz_samples[] array is in ROM .rodata —
 *   never modified, so DMA-from-ROM is safe (no cache coherency).
 *
 * Both functions must run on the SECONDARY SH-2. See sh_src/mars_start.s
 * for the secondary-side DMA IRQ dispatch that calls amb_dma_handler. */

void amb_sound_init(void);
void amb_dma_handler(void);

/* Called from the secondary's idle COMM4-poll loop. Checks the ping-pong
 * "needs fill" flag; if either buffer has been drained, fills it from
 * the ROM sample source (eventually: synthesis math). Cheap — ~150 μs
 * per fill at 256 samples. Safe to call as often as you want; returns
 * immediately when no fill is needed. */
void amb_pump(void);

/* Primary flips the ambient pump on when the game world loads. Until
 * then amb_pump() is a no-op — no secondary cycles, true silence — keeping
 * the title quiet and the PWM free for title SFX. */
void amb_set_active(int on);

#endif
