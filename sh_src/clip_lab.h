#ifndef CLIP_LAB_H
#define CLIP_LAB_H
#include "view_lab.h"
#define CL_MAX_FACES (FL_MAX/8*6)
typedef struct {
 view_lab scene;
 sh2z_view_vertex_t vertices[CL_MAX_FACES*6];
 unsigned counts[CL_MAX_FACES],faces,emitted,partial;
} clip_lab;
bool cl_init(clip_lab *w,unsigned model,unsigned facing,unsigned yaw,int32_t distance,unsigned eye);
bool cl_run(clip_lab *w,unsigned kernel);
uint32_t cl_hash(const clip_lab *w);
#endif
