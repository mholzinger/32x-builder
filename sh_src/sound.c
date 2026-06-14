#include "mars.h"
#include "sound.h"
#include "amb_buzz.h"

/* PWM duty-cycle compare range — d32xr's standard 10-bit-in-16-bit
 * envelope. The DC center is (MAX+MIN)/2 = 517. Speaker at silence
 * holds this value. */
#define SAMPLE_MIN 2
#define SAMPLE_MAX 1032

/* Mars_InitPWM — lifted verbatim from d32xr's marshw.c. Three writes
 * to the MONO register flush the FIFO; PWM_CYCLE sets the period
 * (= SH-2 clock / sample_rate); CTRL=0x0185 enables mono output with
 * DREQ on FIFO-empty + PWM interrupt. The trailing ramp from
 * min_sample up to center slowly biases the speaker's DC voltage —
 * skipping it produces a loud pop on power-on.
 *
 * NTSC clock (23.0114 MHz); PAL would need the 22.8015 MHz variant,
 * but MiSTer ships NTSC and we haven't validated PAL yet. */
static void Mars_InitPWM(int sample_rate, int min_sample, int max_sample) {
    int centre_sample = (max_sample - min_sample) / 2;

    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;

    MARS_PWM_CYCLE = (uint16_t)((((23011361 << 1) / sample_rate + 1) >> 1) + 1);

    /* CTRL = 0x0185:
     *   [1:0] TM  = 01 -> mono output
     *   [3:2] RMD = 01 -> right = pulse
     *   [5:4] LMD = 00 -> left  = same as right (mono mode)
     *   [7]   AL  = 1  -> DREQ enable on FIFO-empty
     *   [8]   --  = 1  -> PWM interrupt enable on FIFO-empty
     *   [11:8] TM-count = 1
     */
    MARS_PWM_CTRL = 0x0185;

    /* DC-bias ramp to suppress power-on pop. */
    int sample = min_sample;
    while (sample < centre_sample) {
        int reps = (sample_rate * 2) / (centre_sample - min_sample);
        for (int ix = 0; ix < reps; ix++) {
            while (MARS_PWM_MONO & 0x8000) {}   /* wait for FIFO slot */
            MARS_PWM_MONO = (uint16_t)sample;
        }
        sample++;
    }
}

/* Re-arm DMA channel 1 to stream the buffer through PWM_MONO again.
 * Called from the slave's DMA-complete IRQ handler, so each completion
 * immediately starts a fresh transfer of the same samples. The buffer
 * itself never changes — it's a const array in ROM. */
void amb_dma_handler(void) {
    SH2_DMA_SAR1  = (uint32_t)amb_buzz_samples;
    SH2_DMA_TCR1  = AMB_BUZZ_SAMPLE_COUNT;
    SH2_DMA_CHCR1 = 0x14E5;
}

/* CHCR1 = 0x14E5 (mono word transfer):
 *   [1:0]   = 01 -> DE=1 (enable), TE=0
 *   [2]     = 1  -> IE (interrupt enable on transfer end)
 *   [6]     = 1  -> DS (DREQ edge-triggered)
 *   [7]     = 1  -> AL (auto-request level)
 *   [11:10] = 01 -> TS = word (16-bit transfer)
 *   [13:12] = 01 -> SM = source-increment
 *   [15:14] = 00 -> DM = destination-fixed (PWM register)
 *   All other bits 0.
 */

void amb_sound_init(void) {
    Mars_InitPWM(AMB_BUZZ_SAMPLE_RATE, SAMPLE_MIN, SAMPLE_MAX);

    /* DMA destination = PWM mono register, fixed. */
    SH2_DMA_DAR1  = (uint32_t)&MARS_PWM_MONO;
    SH2_DMA_DRCR1 = 0;                /* external DREQ source = PWM */
    SH2_DMA_DMAOR = 1;                /* master enable for the DMAC */

    /* SH-2 IPRA layout (SH7095): [15:12]=DIVU, [11:8]=DMAC, [7:4]=WDT,
     * [3:0]=REF. Setting DMA priority to 4 — slave's SR mask is 2, so
     * priority 4 is high enough to be taken. */
    SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;

    /* DMA1 uses a USER-DEFINED interrupt vector. VCR1 holds the vector
     * number; SH-2 reads VBR[VCR1*4] to get the handler. We point it
     * at vector slot 66 = "Level 4 & 5" entry in the slave vector
     * table (mars_start.s line 203), which already holds slav_irq.
     * slav_irq's dispatch chain has the new `cmp/eq #0x10` branch to
     * slav_dma_irq for level-4 source = DMA. */
    SH2_DMA_VCR1 = 66;

    /* Fire the first transfer. Subsequent transfers chained from the
     * DMA-complete IRQ handler in mars_start.s::slav_dma_irq. */
    amb_dma_handler();
}
