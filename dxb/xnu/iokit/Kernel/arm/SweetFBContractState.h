/* MH2LM CPU display experiment. Pure state/geometry shared with host tests. */
#ifndef SWEET_FB_CONTRACT_STATE_H
#define SWEET_FB_CONTRACT_STATE_H
#include <stdint.h>
#include <stddef.h>
#define SFC_SLOTS 64
#define SFC_OK 0U
#define SFC_BAD 0xe00002c2U
#define SFC_BUSY 0xe00002d5U
#define SFC_ABORT 0xe00002ebU
#define SFC_ERROR 0xe00002bcU
#define SFC_UNSUPPORTED 0xe00002c7U
#define SFC_OFFLINE 0xe00002d7U

enum SFCPhase { SFC_FREE, SFC_BEGUN, SFC_READING, SFC_CACHED, SFC_RETIRED, SFC_CANCELLED, SFC_FAILED };
struct SFCEntry {
    uint64_t sequence, stamp;
    uintptr_t owner;
    uint32_t id, phase, error, waiters;
    bool cancel;
};
struct SFCState {
    struct SFCEntry entries[SFC_SLOTS];
    uint64_t sequence;
    uint32_t nextID, displayID;
    bool ready, stopping;
};
static inline struct SFCEntry *sfc_find(struct SFCState *s, uint32_t id) {
    if (!id) return NULL;
    for (unsigned i=0;i<SFC_SLOTS;++i) if (s->entries[i].phase && s->entries[i].id==id) return &s->entries[i];
    return NULL;
}
static inline bool sfc_terminal(const struct SFCEntry *e) {
    return e->phase==SFC_FREE || e->phase==SFC_RETIRED || e->phase==SFC_CANCELLED || e->phase==SFC_FAILED;
}
static inline struct SFCEntry *sfc_begin(struct SFCState *s, uintptr_t owner, uint64_t stamp) {
    if (!owner || s->stopping || s->sequence==UINT64_MAX) return NULL;
    struct SFCEntry *slot=NULL;
    for (unsigned i=0;i<SFC_SLOTS;++i) {
        struct SFCEntry *e=&s->entries[i];
        if (!e->waiters && sfc_terminal(e) && (!e->id || e->id!=s->displayID) && (!slot || e->sequence<slot->sequence)) slot=e;
    }
    if (!slot) return NULL;
    uint32_t id=s->nextID;
    for (unsigned i=0;i<=SFC_SLOTS;++i) {
        if (++id==0) ++id;
        if (!sfc_find(s,id)) {
            struct SFCEntry fresh={}; fresh.sequence=++s->sequence;fresh.stamp=stamp;fresh.owner=owner;
            fresh.id=id;fresh.phase=SFC_BEGUN;*slot=fresh;s->nextID=id;return slot;
        }
    }
    return NULL;
}
static inline uint32_t sfc_query(struct SFCState *s, uintptr_t owner, uint32_t id, uint32_t options) {
    if (s->stopping) return SFC_OFFLINE;
    uint32_t mode=options&0x7fffffffU;
    if (mode==3) return s->ready?SFC_OK:SFC_BAD;
    if (mode==4) return s->ready?SFC_BAD:SFC_OK;
    if (mode>4) return SFC_BAD;
    if (mode==2) {
        for (unsigned i=0;i<SFC_SLOTS;++i) if (s->entries[i].phase==SFC_READING) return SFC_BUSY;
        return SFC_OK;
    }
    struct SFCEntry *e=sfc_find(s,id);
    if (e && e->owner!=owner) return SFC_BAD;
    if (e && e->phase==SFC_FAILED) return e->error?e->error:SFC_ERROR;
    /* Cancellation cannot release a reader until the executor has stopped. */
    if (e && e->phase==SFC_READING) return SFC_BUSY;
    if (e && e->phase==SFC_CANCELLED) return SFC_ABORT;
    return mode==0 && id && id==s->displayID?SFC_BUSY:SFC_OK;
}
/* Strict BGRA crop; all arithmetic checked before pointer formation. */
struct SFCRect { uint32_t sx,sy,dx,dy,w,h; };
static inline bool sfc_rect(int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t w,int32_t h,
    int32_t ox,int32_t oy,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,struct SFCRect *r) {
    if (!sw || !sh || !dw || !dh || sw>16384 || sh>16384 || dw>16384 || dh>16384 ||
        sx<0 || sy<0 || w<=0 || h<=0 || (uint64_t)(uint32_t)sx+(uint32_t)w>sw ||
        (uint64_t)(uint32_t)sy+(uint32_t)h>sh) return false;
    int64_t x=(int64_t)dx-ox,y=(int64_t)dy-oy,a=sx,b=sy,c=w,d=h;
    if (x<0) {a-=x;c+=x;x=0;} if (y<0) {b-=y;d+=y;y=0;}
    if (x>=dw || y>=dh || c<=0 || d<=0) { *r={};return true; }
    if (c>(int64_t)dw-x)c=dw-x;if (d>(int64_t)dh-y)d=dh-y;
    r->sx=(uint32_t)a;r->sy=(uint32_t)b;r->dx=(uint32_t)x;r->dy=(uint32_t)y;
    r->w=(uint32_t)c;r->h=(uint32_t)d;return true;
}
static inline bool sfc_bounds(uintptr_t base,uint64_t length,uint64_t offset,uint32_t width,uint32_t height,uint32_t stride) {
    if (!base || !width || !height || width>16384 || height>16384 || stride<(uint64_t)width*4 ||
        length>UINTPTR_MAX || base>UINTPTR_MAX-length || offset>length) return false;
    return (uint64_t)(height-1)*stride+(uint64_t)width*4<=length-offset;
}
#endif
