/* Exact caller-owned spatial queries. MIT; SH2ZAM Contributors. */
#ifndef SH2ZAM_SPATIAL_H
#define SH2ZAM_SPATIAL_H
#include "sh2zam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialized by index_build. Treat node/index fields as read-only afterward.
 * A preorder node stores the first index after its subtree in escape; object
 * is an original box index for a leaf, or UINT32_MAX for an interior node. */
typedef struct {
    sh2z_aabb_t bounds;
    uint32_t escape,object;
} sh2z_aabb_node_t;
typedef struct {
    const sh2z_aabb_t *boxes;
    const sh2z_aabb_node_t *nodes;
    size_t count,node_count;
} sh2z_aabb_index_t;

/** Build a balanced immutable AABB index without allocation. Invalid boxes
 *  (any min > max) are omitted, matching sphere_intersects_aabb's false result.
 *  For count valid boxes, at most 2*count-1 nodes and count scratch IDs are
 *  required; zero valid boxes need neither buffer. count must be <= 2^31.
 *  Buffers must be naturally aligned, have representable byte lengths and be
 *  disjoint from boxes, index, and each other. Arguments are checked before
 *  any output/workspace write; failure leaves all of them unchanged.
 *
 *  boxes and the populated nodes must stay alive and unchanged while index is
 *  queried. Rebuild after modifying a box. Scratch IDs may be reused after
 *  build returns. Empty input permits null boxes/nodes/scratch. Building and
 *  querying separate indexes is reentrant; the caller owns synchronization. */
bool sh2z_aabb_index_build(sh2z_aabb_index_t *index,
    const sh2z_aabb_t *boxes,size_t count,
    sh2z_aabb_node_t *nodes,size_t node_capacity,
    uint32_t *scratch,size_t scratch_capacity);

/** Find the exact sphere/AABB intersections as a bitset in original box order:
 *  bit (i&31) in hits[i>>5] is the result for boxes[i]. The first
 *  ceil(count/32) words are written and unused bits in the last word are zero.
 *  Additional output capacity is not touched. Negative radii produce no hits.
 *  hit_count is optional and receives the number of set bits on success.
 *
 *  index must have been initialized and its borrowed storage must be intact.
 *  Output storage must be naturally aligned and disjoint from index, all
 *  borrowed boxes/nodes, and hit_count. A nonempty bitset needs a nonnull hits
 *  buffer with sufficient capacity. Empty indexes permit null hits. Invalid
 *  arguments return false before any output write. Only candidate leaves use
 *  the exact sphere/AABB predicate; bounding boxes conservatively prune other
 *  subtrees. A sphere containing a subtree's farthest corner accepts every
 *  leaf in that subtree. Containment of an all-valid root fills the bitset
 *  directly. A query AABB covering the root otherwise selects a direct scan. */
bool sh2z_aabb_index_query_sphere(uint32_t *hits,size_t hit_word_capacity,
    size_t *hit_count,const sh2z_aabb_index_t *index,sh2z_sphere_t sphere);

#ifdef __cplusplus
}
#endif
#endif
