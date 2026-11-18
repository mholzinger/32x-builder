Source: https://github.com/lamb2k/SH2ZAM
Revision: fac62ff50699c57279a3fa27381b792ec0ef4e8e

saturn/src and saturn/include plus upstream LICENSE and NOTICE.md.
Local experimental additions: box_experimental.h, sh2zam_box.c and explicit
Q16 wrappers in divu_experimental.h/sh2zam_divu.c. The complete upstream diff,
tests and small-batch fixtures are in contributions/sh2zam-small-batch/.
The selected build subset also links reuse, explicit DIVU, box and view kernels.
View/projection/clipping are diagnostic candidates, not gameplay replacements.
Diagnostic 31 runs the standalone suites; diagnostic 29 retains divider tests.
The real-renderer MAC adapter additionally requires FURNITURE_MAC_EXPERIMENT=1.
The tested .ramtext placement experiment was reverted; MAC source placement is
unmodified upstream. See devdoc/sh2zam-nightly-2026-11-16/README.md for outcomes.
Default gameplay retains the previously measured paths and BALANCED quality.
