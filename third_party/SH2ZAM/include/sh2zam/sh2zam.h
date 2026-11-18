/* SH2ZAM - Saturn-native fixed-point mathematics.
 * Copyright (c) 2026 SH2ZAM Contributors. MIT; see root LICENSE.
 * Inspired by SH4ZAM by Falco Girgis and SH4ZAM Contributors.
 */
#ifndef SH2ZAM_H
#define SH2ZAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SH2Z_VERSION "0.2.0"
typedef int32_t sh2z_fx_t;     /**< Signed Q16.16; one is 65536. */
typedef uint16_t sh2z_angle_t; /**< Unsigned turns; one turn wraps at 65536. */
#define SH2Z_ONE ((sh2z_fx_t)65536)
#define SH2Z_HALF ((sh2z_fx_t)32768)
#define SH2Z_ZERO ((sh2z_fx_t)0)
#define SH2Z_ANGLE_QUARTER ((sh2z_angle_t)16384)
#define SH2Z_ANGLE_HALF ((sh2z_angle_t)32768)

typedef struct { sh2z_fx_t x,y; } sh2z_vec2_t;
typedef struct { sh2z_fx_t x,y,z; } sh2z_vec3_t;
typedef struct { sh2z_fx_t x,y,z,w; } sh2z_vec4_t;
typedef struct { sh2z_fx_t m[2][2]; } sh2z_mat22_t;
typedef struct { sh2z_fx_t m[3][3]; } sh2z_mat33_t;
typedef struct { sh2z_fx_t m[3][4]; } sh2z_mat34_t;
typedef struct { sh2z_fx_t m[4][4]; } sh2z_mat44_t;
typedef struct { sh2z_fx_t x,y,z,w; } sh2z_quat_t;
typedef struct { sh2z_vec3_t min,max; } sh2z_aabb_t;
typedef struct { sh2z_vec3_t center; sh2z_fx_t radius; } sh2z_sphere_t;
typedef struct { int16_t x,y; } sh2z_screen_t;
typedef struct { size_t begin,end; } sh2z_range_t; /**< Half-open range. */
/** Prepared immutable mesh. Initialize with sh2z_mesh_view_init; all fields are
 *  read-only implementation data after initialization. No storage is owned.
 *  The positions array must remain alive and unchanged while this view or any
 *  plan made from it is used. Reinitialize after changing the source vertices. */
typedef struct {
    const sh2z_vec3_t *positions;
    size_t count;
    uint32_t max_abs[3];
} sh2z_mesh_view_t;
/** Prepared transform. Initialize with sh2z_transform_plan_init; all fields are
 *  read-only after initialization. The matrix is copied; the mesh is not.
 *  fast_path reports whether the scanned mesh bounds prove that direct raw
 *  accumulation and the final Q16.16 result fit without saturation checks. */
typedef struct {
    sh2z_mat34_t matrix;
    const sh2z_vec3_t *positions;
    size_t count;
    bool fast_path;
} sh2z_transform_plan_t;
typedef struct {
    sh2z_mat34_t *storage;
    size_t size,capacity;
} sh2z_stack_t;

/** Backend identification; does not imply hardware qualification. */
const char *sh2z_backend(void);
/** Saturating arithmetic. Multiply/dot round down ONCE; divide truncates.
 *  Divide by zero returns signed limit; 0/0 = 0. No runtime float or allocation.
 *  Matrix layout: row-major, COLUMN vectors, A*B applies B first.
 *  Quaternion layout: x,y,z,w. These differ from SH4ZAM: no binary/API parity. */
sh2z_fx_t sh2z_from_int(int32_t number);
int32_t sh2z_to_int(sh2z_fx_t number);
sh2z_fx_t sh2z_add(sh2z_fx_t a,sh2z_fx_t b);
sh2z_fx_t sh2z_sub(sh2z_fx_t a,sh2z_fx_t b);
sh2z_fx_t sh2z_mul(sh2z_fx_t a,sh2z_fx_t b);
sh2z_fx_t sh2z_div(sh2z_fx_t a,sh2z_fx_t b);
/** Computes a*b/c without saturating the intermediate product. */
sh2z_fx_t sh2z_muldiv(sh2z_fx_t a,sh2z_fx_t b,sh2z_fx_t c);
sh2z_fx_t sh2z_neg(sh2z_fx_t a);
sh2z_fx_t sh2z_abs(sh2z_fx_t a);
sh2z_fx_t sh2z_min(sh2z_fx_t a,sh2z_fx_t b);
sh2z_fx_t sh2z_max(sh2z_fx_t a,sh2z_fx_t b);
/** Bounds may be supplied in either order. */
sh2z_fx_t sh2z_clamp(sh2z_fx_t a,sh2z_fx_t lo,sh2z_fx_t hi);
sh2z_fx_t sh2z_floor(sh2z_fx_t a);
sh2z_fx_t sh2z_ceil(sh2z_fx_t a);
sh2z_fx_t sh2z_round(sh2z_fx_t a); /**< Ties away from zero. */
sh2z_fx_t sh2z_sqrt(sh2z_fx_t a);  /**< Nonpositive input returns zero. */
sh2z_fx_t sh2z_recip(sh2z_fx_t a);
sh2z_fx_t sh2z_rsqrt(sh2z_fx_t a); /**< Nonpositive input returns zero. */
/** Unclamped interpolation; t outside [0,1] extrapolates. */
sh2z_fx_t sh2z_lerp(sh2z_fx_t a,sh2z_fx_t b,sh2z_fx_t t);
sh2z_fx_t sh2z_smoothstep(sh2z_fx_t t); /**< Clamps t to [0,1]. */

sh2z_angle_t sh2z_angle_from_degrees(int32_t degrees);
/** Shortest signed angular delta; half-turn ties choose -32768. */
int32_t sh2z_angle_delta(sh2z_angle_t a,sh2z_angle_t b);
sh2z_angle_t sh2z_angle_lerp(sh2z_angle_t a,sh2z_angle_t b,sh2z_fx_t t);
sh2z_fx_t sh2z_sin(sh2z_angle_t a);
sh2z_fx_t sh2z_cos(sh2z_angle_t a);
void sh2z_sincos(sh2z_angle_t a,sh2z_fx_t *s,sh2z_fx_t *c);
sh2z_fx_t sh2z_tan(sh2z_angle_t a); /**< Poles saturate by sine's sign. */
sh2z_angle_t sh2z_atan2(sh2z_fx_t y,sh2z_fx_t x); /**< atan2(0,0)=0. */
sh2z_angle_t sh2z_atan(sh2z_fx_t x);
sh2z_angle_t sh2z_asin(sh2z_fx_t x); /**< Domain clamped to [-1,1]. */
sh2z_angle_t sh2z_acos(sh2z_fx_t x); /**< Domain clamped to [-1,1]. */

/** All vector widths have the same operations. Length uses raw squared
 *  components, NOT a saturated fixed-point dot. Normalize rescales first,
 *  handling both one-LSB and full-range inputs. Zero normalizes to zero. */
#define SH2Z_DECLARE_VECTOR(N) \
sh2z_vec##N##_t sh2z_vec##N##_add(sh2z_vec##N##_t a,sh2z_vec##N##_t b); \
sh2z_vec##N##_t sh2z_vec##N##_sub(sh2z_vec##N##_t a,sh2z_vec##N##_t b); \
sh2z_vec##N##_t sh2z_vec##N##_scale(sh2z_vec##N##_t a,sh2z_fx_t s); \
sh2z_fx_t sh2z_vec##N##_dot(sh2z_vec##N##_t a,sh2z_vec##N##_t b); \
sh2z_fx_t sh2z_vec##N##_length(sh2z_vec##N##_t a); \
sh2z_vec##N##_t sh2z_vec##N##_normalize(sh2z_vec##N##_t a); \
sh2z_vec##N##_t sh2z_vec##N##_lerp(sh2z_vec##N##_t a,sh2z_vec##N##_t b,sh2z_fx_t t)
SH2Z_DECLARE_VECTOR(2);
SH2Z_DECLARE_VECTOR(3);
SH2Z_DECLARE_VECTOR(4);
#undef SH2Z_DECLARE_VECTOR
sh2z_fx_t sh2z_vec2_cross(sh2z_vec2_t a,sh2z_vec2_t b);
sh2z_vec3_t sh2z_vec3_cross(sh2z_vec3_t a,sh2z_vec3_t b);

sh2z_mat22_t sh2z_mat22_identity(void);
sh2z_mat33_t sh2z_mat33_identity(void);
sh2z_mat34_t sh2z_mat34_identity(void);
sh2z_mat44_t sh2z_mat44_identity(void);
/** All matrix multiply/transpose outputs may alias either input. */
void sh2z_mat22_multiply(sh2z_mat22_t *out,const sh2z_mat22_t *a,const sh2z_mat22_t *b);
void sh2z_mat33_multiply(sh2z_mat33_t *out,const sh2z_mat33_t *a,const sh2z_mat33_t *b);
void sh2z_mat34_multiply(sh2z_mat34_t *out,const sh2z_mat34_t *a,const sh2z_mat34_t *b);
void sh2z_mat44_multiply(sh2z_mat44_t *out,const sh2z_mat44_t *a,const sh2z_mat44_t *b);
void sh2z_mat33_transpose(sh2z_mat33_t *out,const sh2z_mat33_t *m);
void sh2z_mat44_transpose(sh2z_mat44_t *out,const sh2z_mat44_t *m);
sh2z_vec2_t sh2z_mat22_transform(const sh2z_mat22_t *m,sh2z_vec2_t v);
sh2z_vec3_t sh2z_mat33_transform(const sh2z_mat33_t *m,sh2z_vec3_t v);
sh2z_vec3_t sh2z_mat34_transform_point(const sh2z_mat34_t *m,sh2z_vec3_t v);
sh2z_vec3_t sh2z_mat34_transform_direction(const sh2z_mat34_t *m,sh2z_vec3_t v);
sh2z_vec4_t sh2z_mat44_transform(const sh2z_mat44_t *m,sh2z_vec4_t v);
sh2z_mat34_t sh2z_mat34_translation(sh2z_vec3_t v);
sh2z_mat34_t sh2z_mat34_scale(sh2z_vec3_t v);
sh2z_mat34_t sh2z_mat34_rotation_x(sh2z_angle_t a);
sh2z_mat34_t sh2z_mat34_rotation_y(sh2z_angle_t a);
sh2z_mat34_t sh2z_mat34_rotation_z(sh2z_angle_t a);
sh2z_mat44_t sh2z_mat44_from_mat34(const sh2z_mat34_t *m);
/** RH view: camera looks along -Z. Returns false for degenerate basis or
 *  unrepresentable translation. Failure leaves out unchanged. */
bool sh2z_mat34_look_at(sh2z_mat34_t *out,sh2z_vec3_t eye,sh2z_vec3_t target,sh2z_vec3_t up);
/** Inverts ONLY an orthonormal basis (tolerance 32 raw units); no scale/shear.
 *  Out unchanged on failure. Translation must be representable. */
bool sh2z_mat34_inverse_rigid(sh2z_mat34_t *out,const sh2z_mat34_t *m);
/** RH -Z, NDC depth [-1,1]; requires 0<fov<half-turn, aspect>0, 0<near<far.
 *  Rejects coefficients outside Q16.16. Out unchanged on failure. */
bool sh2z_mat44_perspective(sh2z_mat44_t *out,sh2z_angle_t fov,sh2z_fx_t aspect,sh2z_fx_t near_z,sh2z_fx_t far_z);
/** Clip-space vertex to top-left-origin pixels. Rejects outside all six planes
 *  or w<=0. This is vertex rejection, NOT polygon clipping. Dimensions 1..32767.
 *  Out unchanged on failure. */
bool sh2z_project_clip(sh2z_screen_t *out,sh2z_vec4_t clip,uint16_t width,uint16_t height);
/** Exact in-place operation allowed; other overlapping input/output forbidden.
 *  Zero count permits null pointers. No allocation, scheduling, cache or DMA work.
 *  Checks the matrix once and uses a fused batch kernel when each basis row's
 *  sum of absolute RAW coefficients is <= UINT32_MAX. All Q16.16 input values
 *  remain valid: no vertex prescan or caller-supplied bounds are required.
 *  Other matrices use the full-range general path. Both paths floor each row
 *  once, then add translation and saturate, with identical fixed-point results. */
bool sh2z_mat34_transform_points(sh2z_vec3_t *out,const sh2z_mat34_t *m,const sh2z_vec3_t *in,size_t count);
/** Full-range scalar batch implementation, also used as the automatic fallback.
 *  Same pointer, overlap and numeric contracts as transform_points. Exposed for
 *  direct baseline comparisons and applications that need this implementation. */
bool sh2z_mat34_transform_points_general(sh2z_vec3_t *out,const sh2z_mat34_t *m,const sh2z_vec3_t *in,size_t count);
/** Scans actual positions once to cache their per-axis raw magnitudes.
 *  Zero count permits null positions. Returns false for null view, a nonempty
 *  null input, an unrepresentable byte length, or view/input overlap; failure
 *  leaves view unchanged. The positions are borrowed and must remain immutable
 *  until every plan using them is discarded or rebuilt from a new view. */
bool sh2z_mesh_view_init(sh2z_mesh_view_t *view,const sh2z_vec3_t *positions,size_t count);
/** Copies matrix and mesh metadata, then checks the cached bounds once.
 *  All pointers must be nonnull, including for an empty mesh. view must have
 *  been initialized and its fields must not have been changed. Returns true
 *  even if fast_path is false: those plans use the exact full-range fallback.
 *  Changes to matrix or view after this call do not affect the plan. The source
 *  positions must remain alive and unchanged. Failure leaves plan unchanged;
 *  plan storage must not overlap the source positions. No allocation occurs. */
bool sh2z_transform_plan_init(sh2z_transform_plan_t *plan,const sh2z_mesh_view_t *view,const sh2z_mat34_t *matrix);
/** Executes the entire prepared mesh. Floors each row once and adds translation
 *  exactly as transform_points; outputs match its saturation behavior bit for
 *  bit. A valid initialized plan is required even when empty. Empty execution
 *  accepts null out. Nonempty output must have count elements and be disjoint
 *  from the ENTIRE source mesh and from plan storage; overlap is rejected before
 *  any write. Prepared execution is deliberately out-of-place so cached bounds
 *  remain valid. Use transform_points for ordinary in-place transforms. */
bool sh2z_transform_plan_execute(sh2z_vec3_t *out,const sh2z_transform_plan_t *plan);
/** Executes [first,first+count) from the prepared mesh into out[0..count).
 *  The same immutable-source and overlap rules apply to the entire source mesh.
 *  Invalid ranges and nonempty null pointers return false without writing.
 *  A valid empty range permits null out. No bound rescan or matrix setup occurs. */
bool sh2z_transform_plan_execute_range(sh2z_vec3_t *out,const sh2z_transform_plan_t *plan,size_t first,size_t count);
/** Overflow-safe partition, including uneven and empty jobs. No CPU dispatch. */
bool sh2z_partition(size_t count,size_t worker,size_t workers,sh2z_range_t *out);

sh2z_quat_t sh2z_quat_identity(void);
sh2z_quat_t sh2z_quat_multiply(sh2z_quat_t a,sh2z_quat_t b);
sh2z_quat_t sh2z_quat_normalize(sh2z_quat_t q); /**< Zero -> identity. */
sh2z_quat_t sh2z_quat_conjugate(sh2z_quat_t q);
sh2z_quat_t sh2z_quat_axis_angle(sh2z_vec3_t axis,sh2z_angle_t angle);
/** Shortest-path normalized lerp, t clamped to [0,1]; not SLERP. */
sh2z_quat_t sh2z_quat_nlerp(sh2z_quat_t a,sh2z_quat_t b,sh2z_fx_t t);
sh2z_mat34_t sh2z_quat_to_mat34(sh2z_quat_t q);
sh2z_vec3_t sh2z_quat_rotate(sh2z_quat_t q,sh2z_vec3_t v);

/** AABBs include their boundaries; constructor sorts each pair of coordinates. */
sh2z_aabb_t sh2z_aabb(sh2z_vec3_t a,sh2z_vec3_t b);
bool sh2z_aabb_contains(sh2z_aabb_t box,sh2z_vec3_t p);
bool sh2z_aabb_intersects(sh2z_aabb_t a,sh2z_aabb_t b);
/** Negative radii are invalid and return false; touching counts as intersecting. */
bool sh2z_sphere_intersects(sh2z_sphere_t a,sh2z_sphere_t b);
bool sh2z_sphere_intersects_aabb(sh2z_sphere_t sphere,sh2z_aabb_t box);

/** Caller-owned stack; push duplicates top, pop preserves the root identity.
 *  Buffers must remain valid. Separate stacks are reentrant; shared ones require
 *  application synchronization. No hidden XMTRX register state. */
bool sh2z_stack_init(sh2z_stack_t *stack,sh2z_mat34_t *storage,size_t capacity);
bool sh2z_stack_push(sh2z_stack_t *stack);
bool sh2z_stack_pop(sh2z_stack_t *stack);
bool sh2z_stack_multiply(sh2z_stack_t *stack,const sh2z_mat34_t *m);
const sh2z_mat34_t *sh2z_stack_top(const sh2z_stack_t *stack);

#ifdef __cplusplus
}
#endif
#endif
