/* Auto-generated (requantized 16->8 bit, normalized, TPDF dither)
 * from the shipped 16-bit bake — exact same trim/volume/pre-emphasis.
 * Mixer reconstructs the PWM-word delta as (s8 * RESCALE) >> 7. */
/* Source: ES_Footsteps, Human, Carpet, Walking, Medium - Epidemic Sound.wav */
#ifndef AMB_STEP_H
#define AMB_STEP_H
#include <stdint.h>

#define AMB_STEP_SAMPLE_COUNT 33075
#define AMB_STEP_SAMPLE_RATE  11025
#define AMB_STEP_SAMPLE_BITS  8
#define AMB_STEP_RESCALE     717

extern const int8_t amb_step_samples[33075];

#endif
