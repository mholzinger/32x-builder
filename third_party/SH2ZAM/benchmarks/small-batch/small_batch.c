/* MIT; all per-matrix setup is inside sb_run except EXEC-ONLY. */
#include "small_batch.h"
const char *const sb_names[SB_KERNELS]={"GENERAL","BATCH","COLD-PREP","WARM-PREP",
    "EXEC-ONLY","MAC-PREP","DMUL-PREP","REUSE","BOX8"};
bool sb_init(sb_workspace *w,size_t count) {
    if(!w||!count||count>SB_MAX)return false;
    w->count=count;
    w->matrix=(sh2z_mat34_t){{{56756,-8192,31744,12345},
        {16384,61517,-8192,-8193},{-30000,15000,56756,200000}}};
    for(size_t b=0;b<(count+7)/8;b++) {
        /* Repeated dimensions keep a deliberately reuse-friendly fixture.
         * Batches <128 still exercise the current generic cost fallback. */
        sh2z_aabb_t box={{-131073,-65537,-98305},{131071,196607,65535}};
        w->boxes[b]=box;
        for(unsigned i=0;i<8&&b*8+i<count;i++)w->in[b*8+i]=(sh2z_vec3_t){
            i&1?box.max.x:box.min.x,i&2?box.max.y:box.min.y,i&4?box.max.z:box.min.z};
    }
    return sh2z_mat34_transform_points_general(w->reference,&w->matrix,w->in,count)&&
        sh2z_mesh_view_init(&w->view,w->in,count)&&
        sh2z_transform_plan_init(&w->plan,&w->view,&w->matrix)&&
        sh2z_reuse_mesh_init(&w->reuse,w->in,count,w->indices,SB_MAX,
            w->x_keys,SB_MAX,w->yz_keys,SB_MAX)&&
        sh2z_reuse_plan_init(&w->reuse_plan,&w->reuse,&w->matrix,
            w->x_terms,SB_MAX,w->yz_terms,SB_MAX);
}
bool sb_run(sb_workspace *w,unsigned kernel) {
    switch(kernel) {
    case 0:return sh2z_mat34_transform_points_general(w->out,&w->matrix,w->in,w->count);
    case 1:return sh2z_mat34_transform_points(w->out,&w->matrix,w->in,w->count);
    case 2:if(!sh2z_mesh_view_init(&w->view,w->in,w->count))return false;
        /* fall through */
    case 3:if(!sh2z_transform_plan_init(&w->plan,&w->view,&w->matrix))return false;
        /* fall through */
    case 4:return sh2z_transform_plan_execute(w->out,&w->plan);
    case 5:return sh2z_transform_plan_init(&w->plan,&w->view,&w->matrix)&&
        sh2z_transform_plan_execute_mac(w->out,&w->plan,0,w->count);
    case 6:return sh2z_transform_plan_init(&w->plan,&w->view,&w->matrix)&&
        sh2z_transform_plan_execute_dmuls(w->out,&w->plan,0,w->count);
    case 7:return sh2z_reuse_plan_update_matrix(&w->reuse_plan,&w->matrix)&&
        sh2z_reuse_plan_execute(w->out,&w->reuse_plan);
    case 8: {
        size_t b;
        for(b=0;b<w->count/8;b++)
            if(!sh2z_mat34_transform_box8(w->out+b*8,&w->matrix,w->boxes+b))return false;
        return sh2z_mat34_transform_points_general(w->out+b*8,&w->matrix,w->in+b*8,w->count-b*8);
    }
    default:return false;
    }
}
uint32_t sb_hash(const sh2z_vec3_t *p,size_t count) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<count;i++) {
        h=(h^(uint32_t)p[i].x)*16777619u;
        h=(h^(uint32_t)p[i].y)*16777619u;
        h=(h^(uint32_t)p[i].z)*16777619u;
    }
    return h;
}
bool sb_correct(const sb_workspace *w) {
    for(size_t i=0;i<w->count;i++)if(w->out[i].x!=w->reference[i].x||
        w->out[i].y!=w->reference[i].y||w->out[i].z!=w->reference[i].z)return false;
    return true;
}
unsigned sb_check_divu(void) {
    const int32_t e[]={INT32_MIN,INT32_MIN+1,-65536,-1,0,1,65536,INT32_MAX};
    unsigned failures=0;uint32_t seed=7169;
    for(unsigned i=0;i<320;i++) {
        int32_t a,b;
        if(i<64){a=e[i/8];b=e[i%8];}
        else {seed=seed*1664525u+1013904223u;a=(int32_t)seed;
            seed=seed*1664525u+1013904223u;b=(int32_t)seed;}
        int32_t ref=sh2z_div(a,b);
        sh2z_divu_t t;sh2z_divu_start16(&t,a,b);
        if(sh2z_divu_finish(&t,0)!=ref)failures++;
        if(sh2z_divu_div16(a,b,0)!=ref)failures++;
    }
    sh2z_divu_t outer,inner;sh2z_divu_state_t saved;
    sh2z_divu_start16(&outer,1234567,98765);sh2z_divu_save(&saved);
    sh2z_divu_start16(&inner,-7654321,123456);(void)sh2z_divu_finish(&inner,0);
    sh2z_divu_restore(&saved);
    if(sh2z_divu_finish(&outer,0)!=sh2z_div(1234567,98765))failures++;
    return failures;
}
