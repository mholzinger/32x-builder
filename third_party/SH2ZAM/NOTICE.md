# Attribution and provenance

SH2ZAM is an independent Saturn-native math adaptation inspired by SH4ZAM's functional categories. It is not an official SH4ZAM release and does not imply endorsement.

**Original SH4ZAM:** Copyright (c) 2025, 2026 Falco Girgis and SH4ZAM Contributors. Original source, Git ancestry, the unmodified root MIT LICENSE and README.SH4ZAM.md acknowledgements are retained. Upstream: https://github.com/gyrovorbis/sh4zam .

**New SH2ZAM implementation:** Copyright (c) 2026 SH2ZAM Contributors. The Saturn math library, tests, target fixtures and browser application are distributed under the same MIT terms. No SH4 FPU routine has been relabeled as an SH-2 implementation. Keep this notice and all applicable original notices when redistributing.

**Optional instruction-model tools:** The separately marked GPL-2.0-or-later files in `saturn/benchmarks/sh2-model/`, plus `saturn/benchmarks/math-model/capture.h` and `collect.py`, use an unmodified, separately fetched Yabause interpreter. They are not linked into the MIT library or browser module. Their license and Yabause attribution remain separate; see the model directories' documentation and SPDX notices.

**Research and integration references:** SaturnMath++ by Roberto Daniel Alves Duarte (https://github.com/robertoduarte/SaturnMathPP), and Yaul/libyaul and its examples/toolchain (https://github.com/yaul-org). SaturnMath++ implementation code was not copied. The Yaul sample uses the SDK's public APIs and documented make integration; SDK licensing remains separate. Third-party runtime/toolchain components are not vendored here.

Upstream's guidance prefers synchronized whole-library use and contributions rather than disconnected snippets. Preserve provenance and propose applicable fixes upstream. New Saturn numeric contracts and implementation are deliberately separate, so a shared feature name is not a claim of equivalent precision, layout, performance or instruction behavior.
