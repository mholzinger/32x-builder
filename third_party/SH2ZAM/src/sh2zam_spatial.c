/* Exact static AABB index and moving-sphere queries. MIT; SH2ZAM Contributors. */
#include "sh2zam/spatial.h"
#include <limits.h>

static bool spatial_valid_box(const sh2z_aabb_t *b) {
    return b->min.x<=b->max.x&&b->min.y<=b->max.y&&b->min.z<=b->max.z;
}
static bool spatial_buffer(const void *p,size_t count,size_t size,size_t alignment) {
    if(count>SIZE_MAX/size) return false;
    if(!count) return true;
    if(!p||(uintptr_t)p%alignment) return false;
    const size_t bytes=count*size;
    return (uintptr_t)p<=UINTPTR_MAX-(bytes-1);
}
static bool spatial_overlap(const void *a,size_t an,const void *b,size_t bn) {
    if(!an||!bn) return false;
    const uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    return aa>=bb?aa-bb<bn:bb-aa<an;
}
static bool spatial_hits_box(const sh2z_aabb_t *a,const sh2z_aabb_t *b) {
    return a->min.x<=b->max.x&&b->min.x<=a->max.x&&
           a->min.y<=b->max.y&&b->min.y<=a->max.y&&
           a->min.z<=b->max.z&&b->min.z<=a->max.z;
}
static bool spatial_contains_box(const sh2z_aabb_t *outer,const sh2z_aabb_t *inner) {
    return outer->min.x<=inner->min.x&&outer->max.x>=inner->max.x&&
           outer->min.y<=inner->min.y&&outer->max.y>=inner->max.y&&
           outer->min.z<=inner->min.z&&outer->max.z>=inner->max.z;
}
static uint32_t spatial_distance(int32_t a,int32_t b) {
    return a>=b?(uint32_t)a-(uint32_t)b:(uint32_t)b-(uint32_t)a;
}
struct spatial_sphere {
    sh2z_sphere_t value;
    uint64_t square;
};
static inline uint32_t spatial_interval_distance(int32_t value,int32_t low,int32_t high) {
    if(value<low) return (uint32_t)low-(uint32_t)value;
    if(value>high) return (uint32_t)value-(uint32_t)high;
    return 0;
}
/* Callers already know the box is valid and the radius is nonnegative.
 * The component checks bound each square by INT32_MAX^2, so the sum of
 * three squares is below 3*2^62 < 2^64. Unlike the two-sphere primitive,
 * this radius cannot exceed INT32_MAX: one unsigned sum is safe. */
static inline bool spatial_sphere_hits_box(const struct spatial_sphere *s,const sh2z_aabb_t *b) {
    const uint32_t radius=(uint32_t)s->value.radius;
    const uint32_t x=spatial_interval_distance(s->value.center.x,b->min.x,b->max.x);
    if(x>radius) return false;
    const uint32_t y=spatial_interval_distance(s->value.center.y,b->min.y,b->max.y);
    if(y>radius) return false;
    const uint32_t z=spatial_interval_distance(s->value.center.z,b->min.z,b->max.z);
    if(z>radius) return false;
    return (uint64_t)x*x+(uint64_t)y*y+(uint64_t)z*z<=s->square;
}
/* The farthest AABB corner maximizes squared distance from the center.
 * The same component bound as the narrow phase makes the sum safe. */
static bool spatial_sphere_contains_box(const struct spatial_sphere *s,const sh2z_aabb_t *b) {
    const uint32_t radius=(uint32_t)s->value.radius;
    uint32_t a=spatial_distance(s->value.center.x,b->min.x),c=spatial_distance(s->value.center.x,b->max.x);
    const uint32_t x=a>c?a:c;
    if(x>radius) return false;
    a=spatial_distance(s->value.center.y,b->min.y); c=spatial_distance(s->value.center.y,b->max.y);
    const uint32_t y=a>c?a:c;
    if(y>radius) return false;
    a=spatial_distance(s->value.center.z,b->min.z); c=spatial_distance(s->value.center.z,b->max.z);
    const uint32_t z=a>c?a:c;
    if(z>radius) return false;
    return (uint64_t)x*x+(uint64_t)y*y+(uint64_t)z*z<=s->square;
}
static int32_t spatial_low(int32_t center,int32_t radius) {
    return center<INT32_MIN+radius?INT32_MIN:center-radius;
}
static int32_t spatial_high(int32_t center,int32_t radius) {
    return center>INT32_MAX-radius?INT32_MAX:center+radius;
}
static sh2z_aabb_t spatial_sphere_bounds(sh2z_sphere_t s) {
    return (sh2z_aabb_t){
        {spatial_low(s.center.x,s.radius),spatial_low(s.center.y,s.radius),spatial_low(s.center.z,s.radius)},
        {spatial_high(s.center.x,s.radius),spatial_high(s.center.y,s.radius),spatial_high(s.center.z,s.radius)}};
}
/* A cost heuristic only: covering >=3/4 of each root interval usually leaves
 * little useful AABB pruning. Clipped unsigned spans handle INT32 endpoints
 * without overflow or a volume multiply. Both choices remain exact; this
 * threshold cannot guarantee that scanning is faster for every distribution. */
static bool spatial_covers_axis(int32_t qlo,int32_t qhi,int32_t lo,int32_t hi) {
    if(qhi<lo||qlo>hi) return false;
    const int32_t a=qlo>lo?qlo:lo,b=qhi<hi?qhi:hi;
    const uint32_t extent=(uint32_t)hi-(uint32_t)lo;
    const uint32_t covered=(uint32_t)b-(uint32_t)a;
    return covered>=extent-(extent>>2);
}
static bool spatial_broad_query(const sh2z_aabb_t *query,const sh2z_aabb_t *root) {
    return spatial_covers_axis(query->min.x,query->max.x,root->min.x,root->max.x)&&
           spatial_covers_axis(query->min.y,query->max.y,root->min.y,root->max.y)&&
           spatial_covers_axis(query->min.z,query->max.z,root->min.z,root->max.z);
}

/* The base points to the selected named scalar member of the first box.
 * Advancing by sizeof(box) preserves its alignment and effective type; the
 * max member is at the same offset in the second vector. Hoisting the member
 * offset removes an axis branch from every median-selection comparison. */
static uint32_t spatial_center(const unsigned char *base,uint32_t id) {
    const unsigned char *p=base+(size_t)id*sizeof(sh2z_aabb_t);
    const int32_t lo=*(const int32_t *)(const void *)p;
    const int32_t hi=*(const int32_t *)(const void *)(p+offsetof(sh2z_aabb_t,max));
    /* Unsigned modular arithmetic gives the exact signed interval midpoint;
     * flipping the sign bit then preserves signed order as an unsigned key. */
    const uint32_t middle=(uint32_t)lo+((uint32_t)hi-(uint32_t)lo)/2u;
    return middle^UINT32_C(0x80000000);
}
static void spatial_swap(uint32_t *a,uint32_t *b) {
    const uint32_t t=*a;*a=*b;*b=t;
}
static uint32_t spatial_median(uint32_t a,uint32_t b,uint32_t c) {
    if(a>b) {const uint32_t t=a;a=b;b=t;}
    if(b>c) b=c;
    return a>b?a:b;
}
/* Three-way median selection keeps duplicate centers from degenerating into
 * repeated single-element partitions. Every recursive tree split has exactly
 * floor(n/2) and ceil(n/2) leaves, independent of the spatial distribution. */
static void spatial_select(uint32_t *ids,size_t count,size_t middle,
                           const sh2z_aabb_t *boxes,unsigned axis) {
    const size_t offset=axis==0?offsetof(sh2z_vec3_t,x):
                        axis==1?offsetof(sh2z_vec3_t,y):offsetof(sh2z_vec3_t,z);
    const unsigned char *base=(const unsigned char *)(const void *)boxes+offset;
    size_t first=0,last=count;
    while(last-first>1) {
        const uint32_t pivot=spatial_median(spatial_center(base,ids[first]),
            spatial_center(base,ids[first+(last-first)/2]),
            spatial_center(base,ids[last-1]));
        size_t low=first,scan=first,high=last;
        while(scan<high) {
            const uint32_t key=spatial_center(base,ids[scan]);
            if(key<pivot) {spatial_swap(&ids[low],&ids[scan]);++low;++scan;}
            else if(key>pivot) {--high;spatial_swap(&ids[scan],&ids[high]);}
            else ++scan;
        }
        if(middle<low) last=low;
        else if(middle>=high) first=high;
        else return;
    }
}
static void spatial_build_node(sh2z_aabb_node_t *nodes,size_t node,
                               uint32_t *ids,size_t count,const sh2z_aabb_t *boxes) {
    sh2z_aabb_t bounds=boxes[ids[0]];
    for(size_t i=1;i<count;i++) {
        const sh2z_aabb_t *b=&boxes[ids[i]];
        if(b->min.x<bounds.min.x) bounds.min.x=b->min.x;
        if(b->min.y<bounds.min.y) bounds.min.y=b->min.y;
        if(b->min.z<bounds.min.z) bounds.min.z=b->min.z;
        if(b->max.x>bounds.max.x) bounds.max.x=b->max.x;
        if(b->max.y>bounds.max.y) bounds.max.y=b->max.y;
        if(b->max.z>bounds.max.z) bounds.max.z=b->max.z;
    }
    const size_t end=node+count+(count-1);
    nodes[node]=(sh2z_aabb_node_t){bounds,(uint32_t)end,UINT32_MAX};
    if(count==1) {nodes[node].object=ids[0];return;}
    if(count==2) {
        /* Sorting a terminal pair only swaps its two independent leaves;
         * it cannot tighten either bound. Preserve the incoming ID order
         * and avoid choosing an axis or comparing centers. Query results
         * use original-ID bits and do not depend on traversal order. */
        const uint32_t left=ids[0],right=ids[1];
        nodes[node+1]=(sh2z_aabb_node_t){boxes[left],(uint32_t)(node+2),left};
        nodes[node+2]=(sh2z_aabb_node_t){boxes[right],(uint32_t)(node+3),right};
        return;
    }
    const uint32_t x=(uint32_t)bounds.max.x-(uint32_t)bounds.min.x;
    const uint32_t y=(uint32_t)bounds.max.y-(uint32_t)bounds.min.y;
    const uint32_t z=(uint32_t)bounds.max.z-(uint32_t)bounds.min.z;
    const unsigned axis=y>x?(z>y?2u:1u):(z>x?2u:0u);
    const size_t middle=count/2;
    spatial_select(ids,count,middle,boxes,axis);
    /* Selection has already fixed each child's IDs. A singleton needs only
     * its source bounds, original ID and escape index; avoid entering the
     * builder's large recursive frame for that eight-word write. count==2
     * was handled above, so the remaining right child has at least two IDs. */
    if(middle==1) {
        const uint32_t left=ids[0];
        nodes[node+1]=(sh2z_aabb_node_t){boxes[left],(uint32_t)(node+2),left};
    } else spatial_build_node(nodes,node+1,ids,middle,boxes);
    spatial_build_node(nodes,node+middle+middle,ids+middle,count-middle,boxes);
}

bool sh2z_aabb_index_build(sh2z_aabb_index_t *index,
    const sh2z_aabb_t *boxes,size_t count,
    sh2z_aabb_node_t *nodes,size_t node_capacity,
    uint32_t *scratch,size_t scratch_capacity) {
    if(count>UINT32_C(0x80000000)||
       !spatial_buffer(index,1,sizeof(*index),_Alignof(sh2z_aabb_index_t))||
       !spatial_buffer(boxes,count,sizeof(*boxes),_Alignof(sh2z_aabb_t))||
       !spatial_buffer(nodes,node_capacity,sizeof(*nodes),_Alignof(sh2z_aabb_node_t))||
       !spatial_buffer(scratch,scratch_capacity,sizeof(*scratch),_Alignof(uint32_t))) return false;
    const size_t box_bytes=count*sizeof(*boxes),node_bytes=node_capacity*sizeof(*nodes);
    const size_t scratch_bytes=scratch_capacity*sizeof(*scratch);
    if(spatial_overlap(index,sizeof(*index),boxes,box_bytes)||
       spatial_overlap(index,sizeof(*index),nodes,node_bytes)||
       spatial_overlap(index,sizeof(*index),scratch,scratch_bytes)||
       spatial_overlap(boxes,box_bytes,nodes,node_bytes)||
       spatial_overlap(boxes,box_bytes,scratch,scratch_bytes)||
       spatial_overlap(nodes,node_bytes,scratch,scratch_bytes)) return false;
    size_t valid=0;
    const size_t maximum=count?count+(count-1):0;
    if(node_capacity>=maximum&&scratch_capacity>=count) {
        /* With worst-case storage already checked, no later input can cause
         * failure. Compact IDs in the validation pass instead of scanning
         * immutable input twice. */
        for(size_t i=0;i<count;i++) if(spatial_valid_box(&boxes[i])) scratch[valid++]=(uint32_t)i;
    } else {
        for(size_t i=0;i<count;i++) if(spatial_valid_box(&boxes[i])) ++valid;
        const size_t needed=valid?valid+(valid-1):0;
        if(node_capacity<needed||scratch_capacity<valid) return false;
        size_t at=0;
        for(size_t i=0;i<count;i++) if(spatial_valid_box(&boxes[i])) scratch[at++]=(uint32_t)i;
    }
    const size_t needed=valid?valid+(valid-1):0;
    if(valid) spatial_build_node(nodes,0,scratch,valid,boxes);
    *index=(sh2z_aabb_index_t){boxes,valid?nodes:NULL,count,needed};
    return true;
}

static bool spatial_query_valid(uint32_t *hits,size_t capacity,size_t *hit_count,
                                const sh2z_aabb_index_t *index) {
    if(!spatial_buffer(index,1,sizeof(*index),_Alignof(sh2z_aabb_index_t))) return false;
    if(index->count>UINT32_C(0x80000000)||
       index->node_count>(index->count?index->count+(index->count-1):0)||
       (index->node_count&&!(index->node_count&1u))||
       !spatial_buffer(index->boxes,index->count,sizeof(*index->boxes),_Alignof(sh2z_aabb_t))||
       !spatial_buffer(index->nodes,index->node_count,sizeof(*index->nodes),_Alignof(sh2z_aabb_node_t))||
       !spatial_buffer(hits,capacity,sizeof(*hits),_Alignof(uint32_t))||
       (hit_count&&!spatial_buffer(hit_count,1,sizeof(*hit_count),_Alignof(size_t)))) return false;
    const size_t words=index->count/32+(index->count%32!=0);
    if(capacity<words) return false;
    const size_t bytes=capacity*sizeof(*hits),boxes=index->count*sizeof(*index->boxes);
    const size_t nodes=index->node_count*sizeof(*index->nodes);
    if(spatial_overlap(hits,bytes,index,sizeof(*index))||
       spatial_overlap(hits,bytes,index->boxes,boxes)||
       spatial_overlap(hits,bytes,index->nodes,nodes)) return false;
    if(hit_count&&(spatial_overlap(hit_count,sizeof(*hit_count),hits,bytes)||
       spatial_overlap(hit_count,sizeof(*hit_count),index,sizeof(*index))||
       spatial_overlap(hit_count,sizeof(*hit_count),index->boxes,boxes)||
       spatial_overlap(hit_count,sizeof(*hit_count),index->nodes,nodes))) return false;
    return true;
}
static void spatial_set_hit(uint32_t *hits,uint32_t object) {
    /* SH-2 has no variable logical shift. The first native capture called a
     * shift helper for every hit; a 128-byte table avoids that call. */
    static const uint32_t masks[32]={
        UINT32_C(0x00000001),UINT32_C(0x00000002),UINT32_C(0x00000004),UINT32_C(0x00000008),
        UINT32_C(0x00000010),UINT32_C(0x00000020),UINT32_C(0x00000040),UINT32_C(0x00000080),
        UINT32_C(0x00000100),UINT32_C(0x00000200),UINT32_C(0x00000400),UINT32_C(0x00000800),
        UINT32_C(0x00001000),UINT32_C(0x00002000),UINT32_C(0x00004000),UINT32_C(0x00008000),
        UINT32_C(0x00010000),UINT32_C(0x00020000),UINT32_C(0x00040000),UINT32_C(0x00080000),
        UINT32_C(0x00100000),UINT32_C(0x00200000),UINT32_C(0x00400000),UINT32_C(0x00800000),
        UINT32_C(0x01000000),UINT32_C(0x02000000),UINT32_C(0x04000000),UINT32_C(0x08000000),
        UINT32_C(0x10000000),UINT32_C(0x20000000),UINT32_C(0x40000000),UINT32_C(0x80000000)};
    hits[object>>5]|=masks[object&31u];
}
static size_t spatial_accept_subtree(uint32_t *hits,const sh2z_aabb_node_t *nodes,
                                     size_t first,size_t last) {
    size_t count=0;
    for(size_t i=first;i<last;i++) if(nodes[i].object!=UINT32_MAX) {
        spatial_set_hit(hits,nodes[i].object);++count;
    }
    return count;
}
/* Sequential scans build one complete output word in a register. This avoids
 * per-hit read/modify/write traffic and the native variable-shift helper. */
static size_t spatial_scan(uint32_t *hits,const sh2z_aabb_index_t *index,
                           const struct spatial_sphere *sphere) {
    const bool all_valid=index->node_count==index->count+(index->count-1);
    const sh2z_aabb_t *box=index->boxes;
    size_t remaining=index->count,total=0;
    while(remaining) {
        size_t amount=remaining<32?remaining:32;
        remaining-=amount;
        uint32_t bits=0,mask=1;
        do {
            if((all_valid||spatial_valid_box(box))&&spatial_sphere_hits_box(sphere,box)) {
                bits|=mask;++total;
            }
            ++box;mask<<=1;
        } while(--amount);
        *hits++=bits;
    }
    return total;
}
bool sh2z_aabb_index_query_sphere(uint32_t *hits,size_t hit_word_capacity,
    size_t *hit_count,const sh2z_aabb_index_t *index,sh2z_sphere_t sphere) {
    if(!spatial_query_valid(hits,hit_word_capacity,hit_count,index)) return false;
    const size_t words=index->count/32+(index->count%32!=0);
    size_t count=0;
    if(sphere.radius<0||!index->node_count) {
        for(size_t i=0;i<words;i++) hits[i]=0;
        if(hit_count) *hit_count=0;
        return true;
    }
    const uint32_t radius=(uint32_t)sphere.radius;
    const struct spatial_sphere prepared={sphere,(uint64_t)radius*radius};
    /* Exact whole-tree acceptance is especially useful for dense queries.
     * If every original box is valid, original-index output is a packed mask. */
    const bool all=spatial_sphere_contains_box(&prepared,&index->nodes[0].bounds);
    if(all&&index->node_count==index->count+(index->count-1)) {
        for(size_t i=0;i<words;i++) hits[i]=UINT32_MAX;
        const unsigned remainder=(unsigned)(index->count%32);
        if(remainder) hits[words-1]=(UINT32_C(1)<<remainder)-1u;
        if(hit_count) *hit_count=index->count;
        return true;
    }
    if(all) {
        for(size_t i=0;i<words;i++) hits[i]=0;
        count=spatial_accept_subtree(hits,index->nodes,0,index->node_count);
    }
    else {
        const sh2z_aabb_t query=spatial_sphere_bounds(sphere);
        if(spatial_broad_query(&query,&index->nodes[0].bounds)) count=spatial_scan(hits,index,&prepared);
        else {
            for(size_t i=0;i<words;i++) hits[i]=0;
            size_t node=0;
            while(node<index->node_count) {
                const sh2z_aabb_node_t *n=&index->nodes[node];
                if(!spatial_hits_box(&query,&n->bounds)) {node=n->escape;continue;}
                if(n->object!=UINT32_MAX) {
                    if(spatial_sphere_hits_box(&prepared,&n->bounds)) {
                        spatial_set_hit(hits,n->object);++count;
                    }
                    ++node;
                } else if(n->escape-node>=15&&spatial_contains_box(&query,&n->bounds)&&
                          spatial_sphere_contains_box(&prepared,&n->bounds)) {
                    count+=spatial_accept_subtree(hits,index->nodes,node,n->escape);
                    node=n->escape;
                } else ++node;
            }
        }
    }
    if(hit_count) *hit_count=count;
    return true;
}
