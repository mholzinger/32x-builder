# Office MiSTer transform test — 2026-11-16

Verdict: retain the original transform default. No measured speed win for any
SH2ZAM candidate on this workload. The original FPS conclusion is withdrawn:
the box overlay read COMM12's low word (SMS status), not its high-word
vblank clock. Its V/FPS fields are invalid; X transform ticks remain valid.
The counter is corrected in source for subsequent builds.
This establishes neither a gameplay speedup nor whole-animation equivalence.

Rig: mister.office.local, S32X_20260603.rbf. Marsdev SH-2 GCC 15.2.0,
release O2/LTO, identical ROM layout, fixed baked pose 45, metrics enabled.
Arms: 0 original, 1 prepared, 2 experimental native MAC, 3 native DMULS.
Byte 25 selects the arm; byte 26 is 46 (repeat pose 45). Each arm was freshly
launched, allowed 10 seconds to boot/warm, then sampled twice one second apart.
Order was 0,1,2,3,0. The final baseline repeat checks drift. PNG filenames carry
the MiSTer timestamp; samples.csv transcribes their visible counters. Timestamp
strings are MiSTer EST; samples.csv converts them to UTC. Session local time
was approximately 19:01 EST on November 16 (00:01 UTC November 17).

| Path | Transform ticks | Approximate time |
|---|---:|---:|
| Original, including repeat | 63–65 | 0.35–0.36 ms |
| Prepared | 145–146 | 0.81 ms |
| Native MAC candidate | 129 | 0.72 ms |
| Native DMULS candidate | 141–142 | 0.78–0.79 ms |

FRT is Phi/128 (~180 kHz). The measured bracket includes camera subtraction,
mesh bounds, plan construction, execution and output conversion/world copying.
Prepared overhead and memory traffic matter for this small 28-vertex workload.
These timings do not identify one particular internal cost as the sole cause.
Primary total varies more than the transform bracket; no claim of a precise
whole-frame delta is supported by two samples/arm. Fixed-pose repeated frames
are warmer than normal morphing playback, and only one pose was measured.

Native compiled-code correctness check: ares ran the same fixed-pose variants,
and the 336-byte transform-output buffers for prepared/MAC/DMULS were identical
and nonzero. Host oracle checks all 120 poses for the prepared adapter; that
host test does not execute native assembly. Native full-animation hardware
qualification is still pending. No candidate is selected by default.

The next optimization should target a measured gameplay bottleneck. Near
furniture uses live box transforms, far furniture uses directional sprites,
and the engine caps the number of near 3D chairs. This box result gives no basis
for replacing all gameplay fixed-point arithmetic.

Unpatched benchmark build SHA-256: `1bdf565191a05634e84663ba9e7d628336a339c1b90feeaea825876f61dee689`.
