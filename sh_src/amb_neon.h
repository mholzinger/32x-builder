/* Auto-generated (requantized 16->8 bit, normalized, TPDF dither)
 * from the shipped 16-bit bake — exact same trim/volume/pre-emphasis.
 * Mixer reconstructs the PWM-word delta as (s8 * RESCALE) >> 7. */
/* Source: ES_Electricity, Buzz & Hum, Electric, Neon, Light - Epidemic Sound.wav */
#ifndef AMB_NEON_H
#define AMB_NEON_H
#include <stdint.h>

#define AMB_NEON_SAMPLE_COUNT 32000
#define AMB_NEON_SAMPLE_RATE  16000
#define AMB_NEON_SAMPLE_BITS  8
#define AMB_NEON_RESCALE     319

extern const int8_t amb_neon_samples[32000];

#endif
