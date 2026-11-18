/* Exact coordinate-reuse transforms. MIT; SH2ZAM Contributors. */
#ifndef SH2ZAM_REUSE_EXPERIMENTAL_H
#define SH2ZAM_REUSE_EXPERIMENTAL_H
#include "sh2zam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialized by mesh_init; callers must not modify these offsets or keys. */
typedef struct { uint32_t x_offset,yz_offset; } sh2z_reuse_index_t;
typedef struct { int32_t y,z; } sh2z_reuse_yz_t;
typedef struct { int64_t x,y,z; } sh2z_reuse_term_t;

typedef struct {
    sh2z_mesh_view_t view;
    sh2z_reuse_index_t *indices;
    sh2z_fx_t *x_keys;
    sh2z_reuse_yz_t *yz_keys;
    size_t index_capacity,x_capacity,yz_capacity,x_count,yz_count;
    bool reusable;
    bool blocks8; /* Complete aligned octets have the proved X/YZ pattern. */
} sh2z_reuse_mesh_t;

typedef struct {
    sh2z_transform_plan_t base;
    const sh2z_reuse_mesh_t *mesh;
    sh2z_reuse_term_t *x_terms,*yz_terms;
    size_t x_term_capacity,yz_term_capacity;
    bool enabled;
} sh2z_reuse_plan_t;

/** Scans actual immutable positions and prepares X / (Y,Z) lookup metadata.
 *  No allocation. indices needs count elements; key capacities are caller
 *  budgets and may be zero. Nonzero capacities require nonnull buffers.
 *  All buffers, positions and mesh storage must be disjoint, with natural
 *  alignment and representable byte lengths. Invalid arguments return false
 *  before any write, leaving mesh and workspace unchanged.
 *
 *  Key-budget exhaustion is a successful preparation with reusable=false.
 *  The same applies when the generic cost gate fails: count>=128 and
 *  2*uniqueYZ+uniqueX<=count/2 are required. This gate predicts reduced work,
 *  not a guaranteed speedup. Key counts can be partial when reusable=false;
 *  those keys/indices are never used by execution.
 *  When all complete source octets have X offsets [a,b,b,a,a,b,b,a] and
 *  matching YZ offsets within each adjacent pair, blocks8 is true. Execution
 *  may reuse their table loads. Partial octets have no pattern requirement.
 *
 *  The source, metadata, key buffers and mesh object must remain unchanged
 *  and alive while any plan borrows this mesh. Rebuild after source edits.
 *  Zero count permits null positions/indices with zero capacities. */
bool sh2z_reuse_mesh_init(sh2z_reuse_mesh_t *mesh,
    const sh2z_vec3_t *positions,size_t count,
    sh2z_reuse_index_t *indices,size_t index_capacity,
    sh2z_fx_t *x_keys,size_t x_capacity,
    sh2z_reuse_yz_t *yz_keys,size_t yz_capacity);

/** Snapshots matrix, proves the ordinary prepared bounds, and builds all
 *  contribution tables for this matrix. Both table-build cost and execution
 *  belong in a per-matrix benchmark. Tables are caller-owned, exclusive to
 *  this plan and must stay unchanged between initialization/update calls.
 *
 *  Insufficient table capacities, an ineligible mesh, or a failed numeric
 *  proof produce a valid prepared fallback (enabled=false), not an error.
 *  Nonzero capacities require nonnull naturally aligned buffers. Plan and
 *  tables must be disjoint from each other, mesh/source/key/index storage,
 *  and the input matrix. Invalid arguments return false before any write.
 *  Source and mesh lifetime requirements continue through execution. */
bool sh2z_reuse_plan_init(sh2z_reuse_plan_t *plan,
    const sh2z_reuse_mesh_t *mesh,const sh2z_mat34_t *matrix,
    sh2z_reuse_term_t *x_terms,size_t x_term_capacity,
    sh2z_reuse_term_t *yz_terms,size_t yz_term_capacity);

/** Replaces the matrix of an already initialized plan while retaining its
 *  validated workspace binding. Reproves the numeric bounds and rebuilds ALL
 *  used contribution entries, including when matrix values are unchanged.
 *  A failed numeric proof is a successful prepared fallback (enabled=false).
 *
 *  Plan bindings/capacities, borrowed mesh/source/metadata and workspace
 *  allocations must be unchanged since plan_init. To change any binding,
 *  call plan_init again. The caller synchronizes updates and executions.
 *  Null arguments or matrix overlap with plan/term storage return false
 *  before writes, leaving plan/tables unchanged. Other source/metadata
 *  aliasing is read-only. Matrix is snapshotted; it need not remain alive.
 *  This operation belongs inside per-matrix transform timing. */
bool sh2z_reuse_plan_update_matrix(sh2z_reuse_plan_t *plan,
    const sh2z_mat34_t *matrix);

/** Exact Q16.16 affine output in original vertex order. Same floor and
 *  saturation result as transform_points_general, including fallback.
 *  Output must be disjoint from the entire source, mesh/plan storage and
 *  every borrowed workspace buffer, including unused capacity. Invalid
 *  ranges/overlap/null output fail before writing. Valid empty ranges accept
 *  null output. No preparation or table modification occurs here. */
bool sh2z_reuse_plan_execute(sh2z_vec3_t *out,
    const sh2z_reuse_plan_t *plan);
bool sh2z_reuse_plan_execute_range(sh2z_vec3_t *out,
    const sh2z_reuse_plan_t *plan,size_t first,size_t count);

#ifdef __cplusplus
}
#endif
#endif
