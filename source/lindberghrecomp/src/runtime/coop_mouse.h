#ifndef HOTD4_COOP_MOUSE_H
#define HOTD4_COOP_MOUSE_H
#include <math.h>
#include <stdint.h>
/* Private P2 aim. Physical cursor updates only seed the next remote delta. */
typedef struct {
    float x,y,target_x,target_y;
    int screen_x,screen_y,have_screen;
    uint32_t tick;
} CoopMouse;
static inline void coop_mouse_init(CoopMouse *m,int w,int h,uint32_t tick)
{
    *m=(CoopMouse){.x=w/2.0f,.y=h/2.0f,.target_x=w/2.0f,.target_y=h/2.0f,.tick=tick};
}
static inline void coop_mouse_motion(CoopMouse *m,int remote,int relative,int sx,int sy,
                                    float gx,float gy,float scale_x,float scale_y,
                                    float speed,int w,int h)
{
    if(remote) {
        if(relative) {
            if(m->have_screen) {
                m->target_x+=(sx-m->screen_x)*scale_x*speed;
                m->target_y+=(sy-m->screen_y)*scale_y*speed;
                m->target_x=fmaxf(0,fminf(w-1,m->target_x));
                m->target_y=fmaxf(0,fminf(h-1,m->target_y));
            }
        } else {m->target_x=gx;m->target_y=gy;}
    }
    m->screen_x=sx;m->screen_y=sy;m->have_screen=1;
}
static inline void coop_mouse_publish(CoopMouse *m,int smoothing,uint32_t tick)
{
    float dt=(uint32_t)(tick-m->tick)/1000.0f;m->tick=tick;
    if(!smoothing){m->x=m->target_x;m->y=m->target_y;return;}
    float weight=1-expf(-dt/(smoothing==1?.008f:.016f));
    m->x+=(m->target_x-m->x)*weight;m->y+=(m->target_y-m->y)*weight;
    if(fabsf(m->x-m->target_x)<.25f)m->x=m->target_x;
    if(fabsf(m->y-m->target_y)<.25f)m->y=m->target_y;
}
#endif
