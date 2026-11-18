> Overnight follow-up completed: [results for all five candidates](sh2zam-nightly-2026-11-16/README.md).
> The list below records the original test queue; it is not an unstarted work list.

# Next SH2ZAM integrations

The current library is available in full through the vendor headers; selected
objects link into the 32X experiment build. The candidates below must beat the
actual engine path, including conversion/setup, rather than a generic library
baseline. All gameplay candidates retain native rows and the current BALANCED
quality profile.

1. **Furniture projection / explicit DIVU overlap — first trial complete.**
   Compare existing inline hardware division, library blocking Q16 and library
   split Q16 in the actual corner and textured-face-center projection loops.
   Includes chair, desk and PVM close views, active audio, fixed split, eight
   warmups / 64 samples and opening/closing baselines. Geometry-region pixel
   identity is required; report furniture time and total renderer time. The
   fixed-view results are effectively tied for split mode, slower for blocking;
   no default promotion. [Measurements](furniture-divu-2026-11-16/README.md).
2. **Immutable furniture mesh + prepared MAC.** Cache/compile corner expansion
   and mesh bounds once; include matrix construction, world-height conversion
   and per-frame plan cost. Actual models include tapered/wedge geometry and
   the current 64-vertex desk/PVM composite, not just repeated AABBs. Measure ROM-backed
   versus SDRAM-backed vertices, primary/secondary availability and stack/RAM
   high-water. The synthetic MAC win does not establish a win over the engine's
   current scale/rotation code. A fused matrix changes its multiple floor
   operations: quantify screen-coordinate and clipping differences before any
   image-quality acceptance or default change.
3. **Fused transform / projection API.** Upstream has this already. Assess its
   Q1.14 basis, whole-unit int16 input and Q18.14 camera format against our 8.8
   model coordinates, Q16 camera, reciprocal and screen-height conventions.
   Avoid a conversion that throws away model fractions. If the contract cannot
   express the engine faithfully, propose an explicit fractional-model API
   upstream and test it before trying a game adapter.
4. **Near-plane polygon clipping.** Current furniture discards a face when any
   corner crosses NEARC. Upstream clipping is a correctness/visual candidate,
   not an assumed speed optimization. Test contact, crawl, large monitors and
   textured centers, face winding, UVs and stack limits independently.
5. **Shared work / coordinate reuse.** Both CPUs currently transform furniture
   for their own draw halves. Test whether a bounded per-frame geometry cache
   saves more than synchronization/cache traffic costs. Then consider repeated
   larger meshes. Keep generic reuse's matrix/table setup in timing; lowering
   its 128-vertex gate alone is not justified by the current small-batch data.

Promotion requires a measurable repeatable benefit in a real scene, no
unexplained geometry/occlusion changes and a demonstrated RAM/stack budget.
Record rejected candidates as well as winners. MiSTer FPGA results do not
qualify physical SH7604/Saturn behavior or establish universal frame-rate gains.
