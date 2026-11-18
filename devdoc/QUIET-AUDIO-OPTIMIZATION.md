# Exact quiet PWM-buffer fill

The YM2612 supplies the ambient bed; the PWM mixer supplies event voices.
When no PWM voice is active or audible, the old mixer still executes its full
per-sample loop for all 256/1024 samples and emits SAMPLE_CENTER (719) each time.
The candidate fills the same words directly in that case. Diagnostic byte 30
selects the shortcut in a same-binary A/B; byte 28 stays enabled in both arms.

## Equivalence and ownership

The shortcut requires all of: not walking, no death tape, no active neon sting,
no slide voice, no CRT voice, and hello attenuation exactly zero. Running or
crouch flags alone do not advance a locomotion cursor without walking. There
is no volume-based shortcut: muted active voices still need to advance.

Under the guard, delta and step_delta are zero for every sample. The old loop
therefore emits 719, which is inside both soft/hard clip ranges, and advances
no voice cursor. Neon PRNG/triggering and primary event-request latching stay
BEFORE the guard. Buffer selection and the fresh-read DMA request-bit clear
stay common to both paths AFTER the fill. New requests arriving mid-buffer
still take effect on the next fill, as before. No rate, gain, sample data,
resampling, codec, DMA-buffer size or voice mixing rule changes.

amb_pump and voice-state updates are secondary-only. The DMA interrupt consumes
buffers and changes request bits; it does not produce voice samples or change
the tested voice cursors. The shortcut uses the existing coherent stores.
There is no added allocation, shared state or RAM-text relocation.

## Correctness checks

`make sound-mix-test` compiles the REAL sound.c mixer twice, with the shortcut
flag off/on, and compares every output sample and playback-state word for
2,048 fixtures (two fills each). It links the actual PCM assets. Only host
shared-address translation is replaced; no MMIO initialization/IRQ runs.

Fixtures cover inactivity, 256/1024-sample buffers, quiet distant scenes, nearby
hello, 90/100/150% voice speed, walking/running/crawling, all death-tape phases,
neon ending/triggering, slide/CRT requests and overlaps, cursor wraps, and gain
variation. Silent fixtures independently assert the expected center word. Both
samples AND cursor/PRNG/request state match exactly. AddressSanitizer passed a
candidate run as well. The later addition of voice-speed fixtures passed the
ordinary old/new comparison. This is functional evidence, not SH-2 timing.

The host address-translation test seam compiled to a byte-identical production
ROM versus the build timed on MiSTer.

## Hardware method

```
make release MARSDEV=/path/to/marsdev TARGET=/tmp/quiet-audio
python3 tools/gameplay_benchmark_ab.py /tmp/quiet-audio.32x \
  --experiment quiet-audio --output-dir /tmp/quiet-full
python3 tools/gameplay_benchmark_ab.py /tmp/quiet-audio.32x \
  --experiment quiet-audio --resolution half --scene 2 --scene 4 \
  --output-dir /tmp/quiet-half
```

Each pair differs only at byte 30. Authored fixed cameras, pinned split, frozen
world clock, governor disabled, audio enabled, eight warmup frames plus 64
measured frames; allow 35 seconds after launch. G/W/L are primary pass means,
R is complete renderer time including the secondary barrier, and V is mean
instrumented delivered vblanks multiplied by 100. HUD work remains in delivery.
The corridor is inside the hello's audible radius and is a fallback control;
room/crawl/partition views are outside it and are eligible when no events play.
Walking remains a fallback, so stationary results are not a moving-play claim.

## Office MiSTer result — enabled by default

64-frame means, after warmup. Renderer ticks include the secondary barrier;
delivered vblanks include diagnostic HUD work. Lower is better.

| Fixed view | Resolution | Renderer ticks, old → new | Delivered vblanks, old → new |
|---|---|---:|---:|
| Audible-hello corridor control | FULL | 19430 → 19499 | 8.11 → 8.11 |
| Quiet room | FULL | 18592 → 17842 | 8.00 → 8.00 |
| Crawl stress | FULL | 27071 → 26283 | 11.00 → 10.31 |
| Partitions | FULL | 36647 → 35111 | 14.00 → 13.08 |
| Quiet room | HALF | 11270 → 10834 | 5.41 → 5.00 |
| Partitions | HALF | 21529 → 20431 | 9.00 → 8.52 |

Verdict: SHIP the exact quiet-fill shortcut. Eligible views save about 3–5%
renderer time. Delivered average gains range from none to about 8%, depending
on the vblank boundary. The active-voice control varies by 0.4% in renderer time
and stays in the same delivery bucket; the shortcut is not eligible there.
All sampled AU counters remained zero. These are stationary, fixed-resolution
views; moving/active-voice paths retain the old mixer and have no promised gain.
No claim is made that these instrumented FPS values equal ordinary gameplay.

Byte 30 defaults to 1, byte 28 stays 1, and all benchmark bytes stay zero in the
normal build. Original box transform remains selected. The normal ROM differs
from the exact binary timed above at only the quiet-fill enable byte.

[Captures, exact-font decoder, CSV and hashes](quiet-audio-hardware-2026-11-16/).
Captions are decoded against font.c, with the HALF line-table sampling modeled;
all accepted digits have zero pixel mismatch and no ambiguous match. The
reader avoids estimating the partly doubled HALF-resolution text by eye.
