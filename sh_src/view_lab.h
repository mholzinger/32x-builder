#ifndef VIEW_LAB_H
#define VIEW_LAB_H
#include "furniture_lab.h"
typedef struct {
 int32_t dirx,diry,planex,planey,invdet,fc,fs,px,py;
 int32_t eye,nearz,height;
} vl_camera;
typedef struct {
 sh2z_pos16_t raw[FL_MAX];
 sh2z_view_vertex_t out[FL_MAX],reference[FL_MAX],legacy[FL_MAX];
 sh2z_view_t view;
 vl_camera camera;
 unsigned model,count;
} view_lab;
bool vl_init(view_lab *w,unsigned model,unsigned facing,unsigned yaw,int32_t distance,unsigned eye);
bool vl_setup_view(sh2z_view_t *view,const vl_camera *camera);
void vl_engine(sh2z_view_vertex_t *out,const sh2z_pos16_t *raw,unsigned count,const vl_camera *camera);
bool vl_run(view_lab *w,unsigned kernel);
uint32_t vl_hash(const sh2z_view_vertex_t *points,unsigned count);
#endif
