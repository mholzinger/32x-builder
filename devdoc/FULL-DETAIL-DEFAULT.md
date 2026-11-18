> Superseded later on 2026-11-16 by [BALANCED walking](BALANCED-WALKING.md).
> FULL-only addressed the image complaint but not the smooth-movement goal.
> The measurements and hash below describe the earlier FULL-only build.

# Native resolution by default — 2026-11-16

User rejected the walking HALF image quality. FULL is now the initial menu
mode and effective resolution: walking and standing retain native 320x224
sampling in the full viewport. HALF/AUTO/LOW remain explicit performance
choices; none is a 75%-per-axis implementation. No renderer or resolution-
controller algorithm changed in this update.

The compiled ROM differs from the preceding quiet-audio build at exactly two
initializer bytes: effective halfres 1->0 and resolution mode AUTO(2)->FULL(0).
Ceiling-divider and quiet-audio optimizations stay enabled; benchmark flags
stay off. README now describes the quality-first default accurately.

Validation: release build succeeded; a 6,000-frame ares attract/playthrough
trace captured 3103 moving samples after startup. Every one had FULL selected and
both horizontal and vertical reductions disabled. Initial uninitialized boot
memory was excluded. The office hardware rig was loaded with this normal build
and its SHA-256 matched locally. Full detail can cost delivery rate in heavy
scenes; this is an explicit image-quality preference, not a performance win.
See the quiet-audio FULL/HALF matrix for fixed-scene costs.

ROM SHA-256: `a7ba6801a861f4292f24ef73d02113d67c382e401e6df2cf8c9ad21ec4f90178`.
