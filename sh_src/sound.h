#ifndef SOUND_H
#define SOUND_H

/* Ambient electrical-buzz looping audio. The slave SH-2 owns audio:
 * - amb_sound_init() runs once on slave boot. Programs the PWM hardware
 *   for the target sample rate, ramps the DC bias to avoid a pop, sets
 *   up DMA channel 1 to stream samples from ROM into MARS_PWM_MONO,
 *   then kicks off the first DMA.
 * - amb_dma_handler() runs from the slave's DMA-complete IRQ. It just
 *   re-arms DMA1 from the start of the same buffer, producing an
 *   endless loop. The amb_buzz_samples[] array is in ROM .rodata —
 *   never modified, so DMA-from-ROM is safe (no cache coherency).
 *
 * Both functions must run on the SLAVE SH-2. See sh_src/mars_start.s
 * for the slave-side DMA IRQ dispatch that calls amb_dma_handler. */

void amb_sound_init(void);
void amb_dma_handler(void);

#endif
