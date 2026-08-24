#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

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

/* Called from the secondary's idle COMM4-poll loop AND as checkpoints
 * between the secondary's render passes (s_main.c). Checks the ping-pong
 * "needs fill" flag; if either buffer has been drained, fills it from
 * the ROM sample source (eventually: synthesis math). Cheap — ~0.6 ms
 * per fill at 1024 samples, once per ~64 ms. Safe to call as often as
 * you want; returns immediately when no fill is needed. SECONDARY ONLY:
 * the mix-position statics aren't cross-CPU safe. */
void amb_pump(void);

/* ONE Speex frame of hello decode, IDLE TIME ONLY — call from the
 * secondary's COMM4 wait loop alongside amb_pump, NEVER from the
 * between-render-pass checkpoints: a decode burst there pushes the next
 * pass across the vblank edge and eats a whole frame (the hardware-
 * confirmed fps hit near the neanderthal). Instant return when the ring
 * is topped up, the player is out of range, the tape-death owns the
 * channel, or ambient is inactive. SECONDARY ONLY. */
void amb_audio_idle(void);

/* Primary flips the ambient pump on when the game world loads. Until
 * then amb_pump() is a no-op — no secondary cycles, true silence — keeping
 * the title quiet and the PWM free for title SFX. */
void amb_set_active(int on);

/* Underrun-fix A/B knob + diagnostic, callable from the PRIMARY (all
 * three read/write through the cache-through alias):
 * - amb_toggle_buf_len(): flip between the 1024-sample (64 ms) fixed
 *   buffers and the old 256-sample (16 ms) chop-prone arm. AUDIO menu
 *   tab, BUFFER row.
 * - amb_buf_len_is_big(): 1 when on the 64 ms arm (menu display).
 * - amb_get_underruns(): running count of DMA swaps into a never-
 *   refilled buffer — each one is a replayed stale fragment you can
 *   hear as chop. Metrics HUD "AU:". Should sit frozen on the 64 ms
 *   arm and climb on the 16 ms arm in dense scenes. */
void     amb_toggle_buf_len(void);
int      amb_buf_len_is_big(void);
uint16_t amb_get_underruns(void);

/* Voyager-hello playback-speed trim (AUDIO menu, VOICE row). On hardware
 * the hellos drag slower than in Ares while the buzz is fine, so this
 * scales ONLY the voice's read step — buzz/neon/steps untouched.
 * - amb_voice_speed_adjust(dir): dir -1/+1 steps ~1%.
 * - amb_voice_speed_pct(): 100 = baseline, for the menu readout.
 * Bake the tuned value into HELLO_STEP_FX and drop the row when settled. */
void amb_voice_speed_adjust(int dir);
int  amb_voice_speed_pct(void);

#endif
