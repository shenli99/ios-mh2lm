/* Included once after AppleCLCD/UserClient declarations. No MMIO or real-vsync claims. */
#include <IOKit/IOCommandGate.h>
#include "SweetFBContractState.h"
#include "SweetFBProof.h"

class SweetFBContract : public OSObject {
    OSDeclareDefaultStructors(SweetFBContract);
    struct Proof {
        SFPResult pixels;
        SFCRect layerRect;
        uint32_t background;
        uint64_t sequence,sceneSequence,layerSequence,readStart,readEnd,reserved,start,end;
        uint32_t sample,window,id,sceneID,layerID,surfaceID,update,disable;
        bool selected,valid,swap,updated;
    };
    struct Job {
        SFCEntry *entry;
        SweetSurface *surface;
        SFCRect rect;
        uint32_t background,sourceID,update,disable;
        uint64_t readStart,readEnd;
        Proof proof;
        bool layer, updateLayer, off, cancel, power, committed;
    };
    AppleCLCD *fFB; /* provider keeps this object until shutdown joins all invocations */
    IOCommandGate *fGate;
    IOWorkLoop *fLoop;
    SFCState fState;
    Job *fActive;
    uint32_t *fLayer, *fScratch, *fFrame;
    size_t fBytes;
    SFCRect fRect;
    uint32_t fBackground, fSceneID, fCalls, fLogs, fTransitionLogs;
    bool fLayerEnabled, fOff, fValid, fGateAttached, fProofEnabled;
    SFPBudget fProofBudget;
    uint32_t fLayerID,fLayerSurfaceID;
    uint64_t fLayerSequence,fSceneSequence,fLayerReadStart,fLayerReadEnd;

    enum { Enter, Leave, Begin, Wait, Acquire, Commit, Finish, Cancel, Close, Shutdown, Reject };
    struct Request {
        IOMobileFramebufferUserClient *client;
        IOExternalMethodArguments *args;
        uint32_t operation, selector;
        Job *job;
        IOReturn result;
    };
    static IOReturn action(OSObject *object, void *arg0, void *, void *, void *) {
        return ((SweetFBContract *)object)->gated((Request *)arg0);
    }
    IOReturn run(Request *r, uint32_t operation) {
        r->operation=operation; return fGate->runAction(&SweetFBContract::action,r);
    }
    IOReturn sleepUntil(uint64_t deadline) {
        if (fLoop->onThread()) return kIOReturnCannotLock;
        return fGate->commandSleep(&fState,deadline,THREAD_UNINT);
    }
    void wake() { fGate->commandWakeup(&fState); }
    static uint32_t word(const uint8_t *p,uint32_t off) { uint32_t v;memcpy(&v,p+off,4);return v; }
    void note(const char *event,uint32_t id,uint32_t options,IOReturn result,bool transition=false) {
        if (result!=kIOReturnSuccess && result!=kIOReturnBusy && result!=kIOReturnAborted) {
            SWEET_LOG_ERROR("SWEET FB CONTRACT: %s id=%u options=%#x rc=%#x\n",event,id,options,(unsigned)result);
        } else if (transition?fTransitionLogs++<64:fLogs++<96) {
            SWEET_LOG_INFO("SWEET FB CONTRACT: %s id=%u options=%#x display=%u rc=%#x\n",event,id,options,fState.displayID,(unsigned)result);
        }
    }
    IOReturn wait(Request *r) {
        auto *a=r->args;
        if (a->scalarInputCount!=3 || a->scalarOutputCount || a->scalarInput[0]>UINT32_MAX ||
            a->scalarInput[1]>UINT32_MAX || a->scalarInput[2]>10000) return kIOReturnBadArgument;
        uint32_t id=(uint32_t)a->scalarInput[0],options=(uint32_t)a->scalarInput[1];
        SFCEntry *entry=sfc_find(&fState,id);
        if (entry) ++entry->waiters; /* prevent eviction while commandSleep opens the gate */
        uint64_t deadline;clock_interval_to_deadline(a->scalarInput[2]?(uint32_t)a->scalarInput[2]:1000,kMillisecondScale,&deadline);
        IOReturn result;
        for (;;) {
            result=r->client->fContractClosed?kIOReturnAborted:(IOReturn)sfc_query(&fState,(uintptr_t)r->client,id,options);
            if (result!=kIOReturnBusy || (options&0x80000000U)) break;
            IOReturn slept=sleepUntil(deadline);
            if (slept!=THREAD_AWAKENED) {
                result=(IOReturn)sfc_query(&fState,(uintptr_t)r->client,id,options);
                if (result==kIOReturnBusy) result=slept==THREAD_TIMED_OUT?kIOReturnTimeout:kIOReturnAborted;
                break;
            }
        }
        if (entry) --entry->waiters;
        note("wait",id,options,(unsigned)result);return result;
    }
    IOReturn acquire(Request *r) {
        uint64_t deadline;clock_interval_to_deadline(1000,kMillisecondScale,&deadline);
        while (fActive && !fState.stopping && !r->client->fContractClosed) {
            IOReturn slept=sleepUntil(deadline);
            if (slept!=THREAD_AWAKENED && fActive) return slept==THREAD_TIMED_OUT?kIOReturnTimeout:kIOReturnAborted;
        }
        if (fState.stopping || r->client->fContractClosed) return kIOReturnOffline;
        Job *j=r->job; auto *a=r->args;
        j->off=fOff;j->layer=fLayerEnabled;j->rect=fRect;j->background=fBackground;
        if (r->selector==12) {
            if (a->scalarInputCount!=1 || a->scalarOutputCount) return kIOReturnBadArgument;
            j->power=true;j->off=a->scalarInput[0]==0;fActive=j;return kIOReturnSuccess;
        }
        if (a->scalarInputCount || a->scalarOutputCount || a->structureInputDescriptor || !a->structureInput || a->structureInputSize!=SWEET_FB_SWAP_REC_SIZE) return kIOReturnBadArgument;
        const uint8_t *rec=(const uint8_t *)a->structureInput;
        uint32_t id=word(rec,0x50),updated=word(rec,0x104),disabled=word(rec,0x108);
        SFCEntry *e=sfc_find(&fState,id);
        if (!e || e->owner!=(uintptr_t)r->client) return kIOReturnBadArgument;
        if (e->phase==SFC_CANCELLED) return kIOReturnAborted;
        if (e->phase!=SFC_BEGUN) return kIOReturnBadArgument;
        j->entry=e;++e->waiters;
        if (((updated|disabled)&~0x8000000fU) || (disabled&~updated)) return kIOReturnUnsupported;
        /* One complete CPU-composited layer only. Never pretend memcpy is multi-layer blending. */
        if ((updated&~disabled)&0xeU) return kIOReturnUnsupported;
        j->update=updated;j->disable=disabled;
        if (updated&0x80000000U) j->background=word(rec,0x10c);
        if ((j->background>>24)!=255) return kIOReturnUnsupported;
        if (updated&1) {
            j->updateLayer=true;j->layer=!(disabled&1);
            if (j->layer) {
                uint32_t sid=word(rec,0x54);
                j->sourceID=sid;j->surface=sweet_surface_copy(sid);
                SweetSurface *s=j->surface;
                if (!s || s==fFB->fSurface || s->pixelFormat!=SWEET_FB_FORMAT_BGRA ||
                    s->bytesPerElement!=4 || s->elementWidth!=1 || s->elementHeight!=1 || !s->memory || s->planeCount>1)
                    return kIOReturnUnsupported;
                if (word(rec,0x6c)!=word(rec,0xcc) || word(rec,0x70)!=word(rec,0xd0)) return kIOReturnUnsupported;
                if (!sfc_rect((int32_t)word(rec,0x64),(int32_t)word(rec,0x68),
                    (int32_t)word(rec,0xc4),(int32_t)word(rec,0xc8),(int32_t)word(rec,0xcc),(int32_t)word(rec,0xd0),
                    fFB->fOffX,fFB->fOffY,s->width,s->height,fFB->fWidth,fFB->fHeight,&j->rect)) return kIOReturnBadArgument;
                if (!sfc_bounds(1,s->memory->getLength(),s->offset,s->width,s->height,s->bytesPerRow) ||
                    s->allocSize>s->memory->getLength()) return kIOReturnBadArgument;
            } else j->rect={};
        }
        j->entry=e;e->phase=SFC_READING;fActive=j;
        note("reading",id,0,kIOReturnSuccess);return kIOReturnSuccess;
    }
    void cancelOwner(uintptr_t owner,uint32_t id,uint64_t cutoff) {
        for (unsigned i=0;i<SFC_SLOTS;++i) {
            SFCEntry *e=&fState.entries[i];
            if (!e->phase || e->owner!=owner || (id && e->id!=id) || e->stamp>cutoff) continue;
            if (e->phase==SFC_READING) {e->cancel=true;if (fActive && fActive->entry==e) fActive->cancel=true;}
            else if (e->phase!=SFC_FAILED) e->phase=SFC_CANCELLED;
            if (fState.displayID==e->id) fState.displayID=0;
            if (fSceneID==e->id) fSceneID=0; /* pixels remain owned; replay never reads CA memory */
        }
        wake();
    }
    IOReturn gated(Request *r) {
        switch (r->operation) {
        case Enter:
            if (fState.stopping || r->client->fContractClosed) return kIOReturnOffline;
            ++fCalls;return kIOReturnSuccess;
        case Leave: --fCalls;wake();return kIOReturnSuccess;
        case Begin: {
            if (fState.stopping || r->client->fContractClosed) return kIOReturnOffline;
            if (r->args->scalarInputCount || r->args->scalarOutputCount!=1) return kIOReturnBadArgument;
            SFCEntry *e=sfc_begin(&fState,(uintptr_t)r->client,mach_absolute_time());
            if (!e) return kIOReturnNoResources;
            r->args->scalarOutput[0]=e->id;return kIOReturnSuccess;
        }
        case Wait: return wait(r);
        case Acquire: return acquire(r);
        case Commit: {
            Job *j=r->job;
            if (fState.stopping || j->cancel || r->client->fContractClosed) return kIOReturnAborted;
            if (j->updateLayer && j->layer) {uint32_t *p=fLayer;fLayer=fScratch;fScratch=p;}
            if (!j->power) {
                /* Retire the previous cached scene only when its replacement is fully captured. */
                SFCEntry *old=sfc_find(&fState,fSceneID);
                if (old && old!=j->entry && old->phase==SFC_CACHED) old->phase=SFC_RETIRED;
                fLayerEnabled=j->layer;fRect=j->rect;fBackground=j->background;fSceneID=j->entry->id;fValid=true;
                fSceneSequence=j->entry->sequence;
                if (j->updateLayer) {
                    fLayerID=j->layer?j->entry->id:0;fLayerSequence=j->layer?j->entry->sequence:0;
                    fLayerSurfaceID=j->layer?j->sourceID:0;
                    fLayerReadStart=j->layer?j->readStart:0;fLayerReadEnd=j->layer?j->readEnd:0;
                }
            }
            if (fProofEnabled && j->power && !j->off && (fOff || !fProofBudget.window)) {
                uint64_t now=sweet_fb_trace_now();
                bool opened=sfp_open(&fProofBudget,now);
                SWEET_LOG_INFO("SWEET FB PROOF: window=%u open=%u ns=%llu until=%llu total=%u\n",
                    fProofBudget.window,(unsigned)opened,now,fProofBudget.until,fProofBudget.total);
            }
            fOff=j->off;j->committed=true;
            if (fProofEnabled && !fOff && fValid) {
                uint64_t now=sweet_fb_trace_now();
                if (sfp_take(&fProofBudget,now,!j->power)) {
                    Proof *p=&j->proof;p->selected=true;p->sample=fProofBudget.total;p->window=fProofBudget.window;
                    p->swap=!j->power;p->id=j->entry?j->entry->id:0;p->sequence=j->entry?j->entry->sequence:0;
                    p->background=fBackground;p->layerRect=fRect;
                    p->sceneID=fSceneID;p->sceneSequence=fSceneSequence;p->layerID=fLayerID;p->layerSequence=fLayerSequence;
                    p->surfaceID=fLayerSurfaceID;p->updated=j->updateLayer;p->update=j->update;p->disable=j->disable;
                    p->readStart=fLayerReadStart;p->readEnd=fLayerReadEnd;p->reserved=now;
                }
            }
            IOLockLock(fFB->fLock);fFB->fPowerOff=fOff;IOLockUnlock(fFB->fLock);
            return kIOReturnSuccess;
        }
        case Finish: {
            Job *j=r->job;
            if (j->entry) {
                bool cancelled=j->cancel || fState.stopping || r->client->fContractClosed;
                j->entry->phase=cancelled?SFC_CANCELLED:r->result?SFC_FAILED:SFC_CACHED;
                j->entry->error=r->result;
                --j->entry->waiters;
                if (cancelled && fSceneID==j->entry->id) fSceneID=0;
            }
            if (j->committed) fState.displayID=fOff?0:fSceneID;
            fActive=NULL;wake();
            note(j->power?(j->off?"power-off":"power-on"):!j->layer?"blank":"source-released",
                j->entry?j->entry->id:0,0,r->result,j->power || !j->layer);
            return (j->cancel || fState.stopping || r->client->fContractClosed)?kIOReturnAborted:r->result;
        }
        case Reject:
            if (r->job->entry) {
                if (r->job->entry->phase==SFC_BEGUN) {r->job->entry->phase=SFC_FAILED;r->job->entry->error=r->result;wake();}
                --r->job->entry->waiters;
            }
            note("reject",r->job->entry?r->job->entry->id:0,0,r->result);return r->result;
        case Cancel: {
            uint32_t id=0;uint64_t cutoff=UINT64_MAX;
            auto *a=r->args;
            if (r->selector==52) {
                if (a->scalarInputCount!=1 || a->scalarInput[0]>UINT32_MAX || !a->scalarInput[0]) return kIOReturnBadArgument;
                id=(uint32_t)a->scalarInput[0];
                SFCEntry *e=sfc_find(&fState,id);if (e && e->owner!=(uintptr_t)r->client) return kIOReturnBadArgument;
            } else {
                if (a->scalarInputCount!=2 || a->scalarInput[0]>UINT32_MAX || a->scalarInput[1]>UINT32_MAX) return kIOReturnBadArgument;
                cutoff=(a->scalarInput[0]<<32)|a->scalarInput[1];
            }
            cancelOwner((uintptr_t)r->client,id,cutoff);
            /* The cancel result cannot imply source release while an executor is still reading. */
            uint64_t deadline;clock_interval_to_deadline(1000,kMillisecondScale,&deadline);
            while (fActive && fActive->entry && fActive->entry->owner==(uintptr_t)r->client &&
                fActive->cancel && (!id || fActive->entry->id==id)) {
                IOReturn slept=sleepUntil(deadline);
                if (slept!=THREAD_AWAKENED && fActive) {note("cancel",id,0,kIOReturnTimeout);return kIOReturnTimeout;}
            }
            note("cancel",id,0,kIOReturnSuccess,true);return kIOReturnSuccess;
        }
        case Close:
            r->client->fContractClosed=true;cancelOwner((uintptr_t)r->client,0,UINT64_MAX);return kIOReturnSuccess;
        case Shutdown:
            fState.stopping=true;fState.ready=false;if (fActive) fActive->cancel=true;wake();
            /* Every invocation is retained and its only executor is a bounded CPU copy. */
            while (fCalls) {
                uint64_t deadline;clock_interval_to_deadline(1000,kMillisecondScale,&deadline);
                if (fGate->commandSleep(&fState,deadline,THREAD_UNINT)==THREAD_TIMED_OUT) SWEET_LOG_ERROR("SWEET FB CONTRACT: stop waiting for %u invocations\n",fCalls);
            }
            return kIOReturnSuccess;
        default: return kIOReturnBadArgument;
        }
    }
    IOReturn execute(Job *j) {
        /* Called by the external caller, after runAction has opened the command gate. */
        if (j->surface) {
            if (fProofEnabled) j->readStart=sweet_fb_trace_now();
            SweetSurface *s=j->surface;uintptr_t src=s->kernelAddress();
            if (!sfc_bounds(src,s->memory->getLength(),s->offset,s->width,s->height,s->bytesPerRow)) return kIOReturnBadArgument;
            for (uint32_t y=0;y<j->rect.h;++y)
                memcpy(fScratch+(uint64_t)(j->rect.dy+y)*fFB->fWidth+j->rect.dx,
                    (const void *)(src+s->offset+(uint64_t)(j->rect.sy+y)*s->bytesPerRow+(uint64_t)j->rect.sx*4),j->rect.w*4);
            if (fProofEnabled) j->readEnd=sweet_fb_trace_now();
        }
        return kIOReturnSuccess;
    }
    void output() {
        if (!fValid) return;
        for (size_t i=0;i<fBytes/4;++i) fFrame[i]=fBackground;
        if (fLayerEnabled) for (uint32_t y=0;y<fRect.h;++y)
            memcpy(fFrame+(uint64_t)(fRect.dy+y)*fFB->fWidth+fRect.dx,
                fLayer+(uint64_t)(fRect.dy+y)*fFB->fWidth+fRect.dx,fRect.w*4);
        if (fOff && fFB->fPreserveOff) return;
        for (uint32_t y=0;y<fFB->fHeight;++y) {
            void *dst=(void *)(fFB->fPixels+(uint64_t)y*fFB->fRowBytes);
            if (fOff) bzero(dst,fFB->fWidth*4);
            else memcpy(dst,fFrame+(uint64_t)y*fFB->fWidth,fFB->fWidth*4);
        }
    }
    void gatherProof(Job *j) {
        Proof *p=&j->proof;if (!p->selected) return;
        p->start=sweet_fb_trace_now();
        SFPRect roi=sfp_clock(fFB->fWidth,fFB->fHeight);
        /* Reuse the idle scratch buffer. The current layer lives in fLayer and
         * every replacement rect is fully overwritten before the next Commit. */
        for (uint32_t y=0;y<roi.h;++y)
            memcpy(fScratch+(uint64_t)y*roi.w,(const void *)(fFB->fPixels+
                (uint64_t)(roi.y+y)*fFB->fRowBytes+(uint64_t)roi.x*4),roi.w*4);
        p->valid=sfp_measure(fFB->fWidth,fFB->fHeight,fLayerEnabled?&fRect:NULL,fLayer,fFrame,fScratch,&p->pixels);
        p->end=sweet_fb_trace_now();
    }
    void emitProof(const Proof *p,IOReturn result) {
        if (!p->selected) return;
        if (!p->valid) {SWEET_LOG_ERROR("SWEET FB PROOF: invalid sample=%u\n",p->sample);return;}
        const SFPResult *v=&p->pixels;
        if (v->differentRGB || v->differentAlpha) SWEET_LOG_ERROR("SWEET FB PROOF: copy mismatch sample=%u diff=%u/%u\n",p->sample,v->differentRGB,v->differentAlpha);
        SWEET_LOG_INFO("SWEET FB PROOF: sample=%u window=%u kind=%s id=%u seq=%llu scene=%u/%llu layer=%u/%llu surface=%u update=%#x disable=%#x fresh-layer=%u read-ns=%llu/%llu reserved-ns=%llu observed-ns=%llu/%llu cost-us=%llu rc=%#x\n",
            p->sample,p->window,p->swap?"swap":"replay",p->id,p->sequence,p->sceneID,p->sceneSequence,
            p->layerID,p->layerSequence,p->surfaceID,p->update,p->disable,(unsigned)p->updated,
            p->readStart,p->readEnd,p->reserved,p->start,p->end,(p->end-p->start)/1000,(unsigned)result);
        SWEET_LOG_INFO("SWEET FB PROOF: sample=%u roi=%u,%u,%u,%u rect=%u,%u/%u,%u/%u,%u background=%#x layer pixels=%u rgb=%u a=%u/%u/%u hash=%llx\n",
            p->sample,v->roi.x,v->roi.y,v->roi.w,v->roi.h,p->layerRect.sx,p->layerRect.sy,
            p->layerRect.dx,p->layerRect.dy,p->layerRect.w,p->layerRect.h,p->background,v->layer.pixels,v->layer.rgb,v->layer.a0,v->layer.amid,v->layer.a255,v->layer.hash);
        SWEET_LOG_INFO("SWEET FB PROOF: sample=%u scene pixels=%u rgb=%u a=%u/%u/%u hash=%llx panel rgb=%u a=%u/%u/%u hash=%llx diff=%u/%u\n",
            p->sample,v->scene.pixels,v->scene.rgb,v->scene.a0,v->scene.amid,v->scene.a255,v->scene.hash,
            v->panel.rgb,v->panel.a0,v->panel.amid,v->panel.a255,v->panel.hash,v->differentRGB,v->differentAlpha);
    }

public:
    bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
    bool setup(AppleCLCD *fb,IOWorkLoop *loop) {
        fFB=fb;fLoop=loop;fLoop->retain();fBackground=0xff000000U;
        if (!fb->fWidth || !fb->fHeight || fb->fWidth>4096 || fb->fHeight>4096 ||
            !sfc_bounds(fb->fPixels,(uint64_t)fb->fRowBytes*fb->fHeight,0,fb->fWidth,fb->fHeight,fb->fRowBytes)) return false;
        fBytes=(size_t)fb->fWidth*fb->fHeight*4;
        fLayer=(uint32_t *)IOMallocZeroData(fBytes);fScratch=(uint32_t *)IOMallocZeroData(fBytes);fFrame=(uint32_t *)IOMallocZeroData(fBytes);
        fGate=IOCommandGate::commandGate(this);
        if (!fLayer || !fScratch || !fFrame || !fGate) return false;
        if (loop->addEventSource(fGate)!=kIOReturnSuccess) {OSSafeReleaseNULL(fGate);return false;}
        fGateAttached=true;
        int proof=0;(void)PE_parse_boot_argn("sweetfbproof",&proof,sizeof(proof));fProofEnabled=proof==1;
        if (fProofEnabled) SWEET_LOG_INFO("SWEET FB PROOF: v1 enabled; dense clock ROI after owned output; 30s windows, 1Hz plus replay/first swap, max 32/window 256 total; no hardware fence\n");
        /* Fixed boot canvas is configured independently of RequestPowerChange/backlight. */
        fState.ready=true;
        SWEET_LOG_INFO("SWEET FB CONTRACT: v1 enabled; synchronous owned snapshots bytes=%lu; no hardware fence\n",(unsigned long)(fBytes*3));
        return true;
    }
    IOReturn invoke(IOMobileFramebufferUserClient *client,uint32_t selector,IOExternalMethodArguments *a) {
        if (fLoop->onThread()) return kIOReturnCannotLock;
        Request r={};r.client=client;r.selector=selector;r.args=a;
        IOReturn result=run(&r,Enter);if (result) return result;
        if (selector==4) result=run(&r,Begin);
        else if (selector==6) result=run(&r,Wait);
        else if (selector==52 || selector==81) result=run(&r,Cancel);
        else if (selector==77) {
            if (a->scalarInputCount || a->scalarOutputCount!=1) result=kIOReturnBadArgument;
            else a->scalarOutput[0]=mach_absolute_time();
        } else {
            Job job={};r.job=&job;
            result=run(&r,Acquire);
            if (!result) {
                int quiet=0;
                if (PE_parse_boot_argn("sweetfb_noconsole",&quiet,sizeof(quiet)) && quiet) sweet_console_muted=1;
                result=execute(&job);
                if (!result) result=run(&r,Commit);
                /* No source access after releasing this reference. Output uses only owned bytes. */
                OSSafeReleaseNULL(job.surface);
                if (!result) {output();gatherProof(&job);}
                r.result=result;result=run(&r,Finish);
                emitProof(&job.proof,result);
            }
            else {
                /* Validation failed before a reader was published. Persist that failure under the gate. */
                r.result=result;(void)run(&r,Reject);
            }
            OSSafeReleaseNULL(job.surface);
        }
        (void)run(&r,Leave);return result;
    }
    void close(IOMobileFramebufferUserClient *client) {Request r={};r.client=client;(void)run(&r,Close);}
    void shutdown() {if (fGate) {Request r={};(void)run(&r,Shutdown);}}
    void free() APPLE_KEXT_OVERRIDE {
        if (fGate) {if (fGateAttached) fLoop->removeEventSource(fGate);OSSafeReleaseNULL(fGate);}
        if (fLayer) IOFreeData(fLayer,fBytes);if (fScratch) IOFreeData(fScratch,fBytes);if (fFrame) IOFreeData(fFrame,fBytes);
        OSSafeReleaseNULL(fLoop);
        OSObject::free();
    }
};
OSDefineMetaClassAndStructors(SweetFBContract,OSObject);
