/* Bounded observations of owned CPU pixels; never a hardware display fence. */
#ifndef SWEET_FB_PROOF_H
#define SWEET_FB_PROOF_H
#include "SweetFBContractState.h"
#define SFP_WINDOW_NS 30000000000ULL
#define SFP_INTERVAL_NS 1000000000ULL
#define SFP_MAX_WINDOWS 12U
#define SFP_MAX_PER_WINDOW 32U
#define SFP_MAX_TOTAL 256U
struct SFPBudget {
    uint64_t until, next;
    uint32_t window, used, total;
    bool firstSwap, active;
};
static inline uint64_t sfp_add(uint64_t a,uint64_t b) { return a>UINT64_MAX-b?UINT64_MAX:a+b; }
static inline bool sfp_open(SFPBudget *s,uint64_t now) {
    s->active=false;
    if (s->window>=SFP_MAX_WINDOWS || s->total>=SFP_MAX_TOTAL) return false;
    ++s->window;s->used=0;s->until=sfp_add(now,SFP_WINDOW_NS);s->next=now;s->firstSwap=true;s->active=true;
    return true;
}
static inline bool sfp_take(SFPBudget *s,uint64_t now,bool swap) {
    if (!s->active || now>=s->until || s->used>=SFP_MAX_PER_WINDOW || s->total>=SFP_MAX_TOTAL) return false;
    if (swap && !s->firstSwap && now<s->next) return false;
    ++s->used;++s->total;s->next=sfp_add(now,SFP_INTERVAL_NS);
    if (swap) s->firstSwap=false;
    return true;
}
struct SFPRect { uint32_t x,y,w,h; };
static inline SFPRect sfp_clock(uint32_t w,uint32_t h) {
    SFPRect r={w/6,h/12,w*2/3,h/4};
    if (!r.w && w) r.w=1;if (!r.h && h) r.h=1;
    return r;
}
struct SFPStats { uint64_t hash;uint32_t pixels,rgb,a0,amid,a255; };
struct SFPResult {
    SFPRect roi;
    SFPStats layer,scene,panel;
    uint32_t differentRGB,differentAlpha;
};
static inline void sfp_pixel(SFPStats *s,uint32_t p) {
    s->hash=(s->hash^p)*1099511628211ULL;++s->pixels;s->rgb+=(p&0xffffffU)!=0;
    uint32_t a=p>>24;s->a0+=a==0;s->amid+=a!=0 && a!=255;s->a255+=a==255;
}
/* Panel is an exact packed memcpy readback of this ROI. Layer/scene remain owned
 * and stable until Finish. Pixels outside layerRect are absent, not stale data. */
static inline bool sfp_measure(uint32_t width,uint32_t height,const SFCRect *layerRect,
    const uint32_t *layer,const uint32_t *scene,const uint32_t *panel,SFPResult *out) {
    *out={};out->roi=sfp_clock(width,height);SFPRect r=out->roi;
    if (!width || !height || width>4096 || height>4096 || !scene || !panel || !r.w || !r.h ||
        r.x+r.w>width || r.y+r.h>height) return false;
    if (layerRect && (!layer || layerRect->dx>width || layerRect->dy>height ||
        layerRect->w>width-layerRect->dx || layerRect->h>height-layerRect->dy)) return false;
    out->layer.hash=out->scene.hash=out->panel.hash=1469598103934665603ULL;
    for (uint32_t y=0;y<r.h;++y) for (uint32_t x=0;x<r.w;++x) {
        uint64_t at=(uint64_t)(r.y+y)*width+r.x+x;
        uint32_t a=scene[at],b=panel[(uint64_t)y*r.w+x];
        sfp_pixel(&out->scene,a);sfp_pixel(&out->panel,b);
        out->differentRGB+=((a^b)&0xffffffU)!=0;out->differentAlpha+=((a^b)>>24)!=0;
        if (layerRect && r.x+x>=layerRect->dx && r.x+x-layerRect->dx<layerRect->w &&
            r.y+y>=layerRect->dy && r.y+y-layerRect->dy<layerRect->h) sfp_pixel(&out->layer,layer[at]);
    }
    return true;
}
#endif
