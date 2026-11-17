/* Register facts: LG pinctrl-sm8150.c / pinctrl-msm.c, sm8150-pinctrl.dtsi,
 * sm8150_common-panel.dtsi and lge_dsi_panel.c. Original gated/timer engine.
 * GPIO6 is SOUTH, GPIO8 NORTH. Neither GPIO7 nor any IRQ register is written.
 *
 * The pads belong to TLMMGPIOController: this service takes an exclusive
 * AGPIO handle for each of them instead of mapping the two tile windows
 * itself, so it is the only client that can drive them and the controller is
 * the only mapper. The node keeps sweet,panel-pins-abi as its identity; its
 * reg windows are gone.
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetPanelPins.h"
#include "TLMMGPIOController.h"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"
OSDefineMetaClassAndStructors(SweetPanelPins,IOService);
OSDefineMetaClassAndStructors(SweetPanelPinsClient,OSObject);
/* Ownership latch only: free=0, owned=1, retired after any write=2. No pointer. */
static unsigned panelPinsOwner;
struct PanelPinsRequest { unsigned op; uint64_t client,token; uint64_t *out; SweetPanelPinStatus *status; };
static const uint32_t pinControlMask=0x3ffu, pinOutput=2u;
/* GPIO6 = reset (SOUTH), GPIO8 = TE (NORTH); the pins are board constants,
 * the addresses come from the controller's tile table. */
enum { PanelPinReset = 0, PanelPinTE = 1 };
static const unsigned panelPinNumber[2]={6u,8u};
bool SweetPanelPins::readPin(unsigned n,struct tlmm_pin_config *cfg)
{
 if (n>1 || !fPins[n]) return false;
 return fPins[n]->getConfig(cfg)==kIOReturnSuccess;
}
void SweetPanelPins::releasePins(void)
{
 for (unsigned n=0;n<2;++n) {
  AGPIO *pin=fPins[n];
  if (!pin) continue;
  fPins[n]=NULL;
  pin->put(); /* returns the pad and drops our reference in one step */
 }
}
uint64_t SweetPanelPins::nowNS(void)
{ uint64_t n=0; absolutetime_to_nanoseconds(mach_absolute_time(),&n); return n; }
bool SweetPanelPins::start(IOService *provider)
{
 int mode=0; (void)PE_parse_boot_argn("sweetpanelpins",&mode,sizeof(mode));
 if ((mode!=1 && mode!=2) || !IOService::start(provider)) return false;
 OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,panel-pins-abi"));
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) return false;
 /*
  * The pads come from the TLMM controller. tlmm is published before this node
  * in the ADT, so it is normally already started; the bounded wait only covers
  * a different publication order. There is deliberately no second writer to
  * fall back to.
  */
 TLMMGPIOController *gpio=NULL;
 for (unsigned n=0;n<20 && !(gpio=TLMMGPIOController::getController());++n) IOSleep(10);
 if (!gpio) {
  SWEET_LOG_ERROR("SWEET PANEL PINS: TLMM controller unavailable; pins untouched\n"); return false;
 }
 unsigned expected=0;
 if (!__atomic_compare_exchange_n(&panelPinsOwner,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  SWEET_LOG_ERROR("SWEET PANEL PINS: owner live/retired; refused\n"); return false;
 }
 fClaimed=true; fMode=(unsigned)mode;
 for (unsigned n=0;n<2;++n) {
  fPins[n]=gpio->acquirePin(panelPinNumber[n],"panel-pins");
  if (!fPins[n]) {
   SWEET_LOG_ERROR("SWEET PANEL PINS: gpio%u is not available from the TLMM controller\n",
       panelPinNumber[n]);
   releasePins();
   return false;
  }
 }
 fLoop=IOWorkLoop::workLoop(); fGate=IOCommandGate::commandGate(this);
 fTimer=IOTimerEventSource::timerEventSource(this,timerAction);
 if (!fLoop || !fGate || !fTimer) return false;
 if (fLoop->addEventSource(fGate)!=kIOReturnSuccess) return false;
 fGateAdded=true;
 if (fLoop->addEventSource(fTimer)!=kIOReturnSuccess) return false;
 fTimerAdded=true; fAccepting=true;
 setProperty("SweetPanelPinsABI",1ULL,32);
 setProperty("SweetPanelPinsWritesEnabled",mode==2);
 SWEET_LOG_INFO("SWEET PANEL PINS: GPIO6/SOUTH reset, GPIO8/NORTH TE; mode=%u, owned through the TLMM controller, no automatic pin access\n",fMode);
 registerService(); return true;
}
void SweetPanelPins::observe(unsigned pin,uint32_t offset,uint32_t want,uint32_t got,uint32_t mask,bool always)
{
 if ((!always && !((want^got)&mask)) || fDiagnosticCount>=64u) return;
 ++fDiagnosticCount;
 SWEET_LOG_INFO("SWEET PANEL PINS CONFIG: token=%llu phase=%u gpio=%u offset=%#x want=%#x got=%#x mask=%#x; observation only\n",
     fStatus.token,fPhase,pin ? 8u : 6u,offset,want,got,mask);
}
bool SweetPanelPins::sample(bool capture)
{
 struct tlmm_pin_config pin;
 for (unsigned n=0;n<2;++n) {
  if (!readPin(n,&pin)) return false;
  uint32_t ctl=pin.cfg,irq=pin.intr_cfg;
  /* Preserve actual owner exclusions. Other CTL/IRQ echoes, including mux,
   * pull/drive and reserved fields, are diagnostic like Lineage pinctrl. */
  if (ctl==~0u || irq==~0u || (ctl&(1u<<12)) || (irq&1u)) return false;
  if (capture) {fControl[n]=ctl;fInterrupt[n]=irq;}
  else {observe(n,0,fControl[n],ctl,~0u);observe(n,8,fInterrupt[n],irq,~0u);}
 }
 if (!readPin(PanelPinReset,&pin)) return false;
 uint32_t io=pin.io;
 if (capture) fOutput=io&pinOutput;
 else observe(0,4,fOutput,io,pinOutput);
 return true;
}
bool SweetPanelPins::writeControl(unsigned n,uint32_t bits)
{
 /* RMW the current upper fields, as pinctrl-msm does. Do not replay an old
  * snapshot over unrelated fields that hardware may legitimately update. */
 struct tlmm_pin_config pin;
 if (!readPin(n,&pin)) return false;
 uint32_t value=(pin.cfg&~pinControlMask)|bits;
 unsigned func=(bits>>TLMM_GPIO_MUX_SHIFT)&TLMM_GPIO_MUX_MASK;
 unsigned pull=bits&TLMM_GPIO_PULL_MASK;
 unsigned drive=(bits>>TLMM_GPIO_DRIVE_SHIFT)&TLMM_GPIO_DRIVE_MASK;
 bool output=(bits&TLMM_GPIO_OE)!=0;
 fTouched=true;
 if (fPins[n]->configure(func,pull,drive,output,pin.high)!=kIOReturnSuccess) return false;
 if (!readPin(n,&pin)) return false;
 observe(n,0,value,pin.cfg,~0u,true);
 fControl[n]=value;return true;
}
bool SweetPanelPins::writeReset(bool high)
{
 /* Lineage gpio_set_value has no latch/input echo gate. Record commanded
  * level separately from the raw observation; success is not voltage proof. */
 struct tlmm_pin_config pin;
 fTouched=true;
 if (fPins[PanelPinReset]->setValue(high)!=kIOReturnSuccess) return false;
 fOutput=high ? pinOutput : 0u;
 if (!readPin(PanelPinReset,&pin)) return false;
 observe(0,4,high ? 3u : 0u,pin.io,3u,true);
 fStatus.resetKnown=false;return true;
}
void SweetPanelPins::observeLevel(bool high)
{
 struct tlmm_pin_config pin;
 if (!readPin(PanelPinReset,&pin)) return;
 observe(0,4,high ? 3u : 0u,pin.io,3u);
 /* Input/latch samples never impose an extra follow deadline. */
}
IOReturn SweetPanelPins::action(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetPanelPins *s=(SweetPanelPins *)owner; PanelPinsRequest &r=*(PanelPinsRequest *)arg;
 if (r.op==OpStop) {
  if (!s->fAccepting) return kIOReturnSuccess;
  s->fAccepting=false;
  if (s->fTimer) s->fTimer->cancelTimeout();
  if (s->fStatus.token) s->finish(kIOReturnAborted,SweetPinsStopped,"owner stopped");
  return kIOReturnSuccess;
 }
 if (r.op==OpQuery) {
  if (!r.client || r.client!=s->fClient || !r.token || r.token!=s->fStatus.token) return kIOReturnNotFound;
  *r.status=s->fStatus; return kIOReturnSuccess; /* no MMIO, even after stop */
 }
 if (r.op==OpQueryLow) {
  if (!r.client || r.client!=s->fClient || !r.token || r.token!=s->fStatus.token) return kIOReturnNotFound;
  if (!s->fAccepting || s->isInactive() || s->fPoisoned || s->fBusy ||
      s->fStatus.result!=kIOReturnSuccess || s->fStatus.uncertain ||
      !s->fStatus.resetKnown || s->fStatus.resetHigh ||
      !((s->fParked && !s->fConfigured && s->fStatus.state==SweetPinsParked) ||
        (!s->fParked && s->fConfigured && s->fStatus.state==SweetPinsActive))) return kIOReturnNotReady;
  if (!s->sample(false)) {
   s->finish(kIOReturnNotReady,SweetPinsFailed,"low-hold owner lost");
   return kIOReturnNotReady;
  }
  s->observeLevel(false);
  *r.status=s->fStatus; return kIOReturnSuccess;
 }
 if (r.op==OpCancel) {
  if (!r.client || r.client!=s->fClient || !r.token || r.token!=s->fStatus.token) return kIOReturnNotFound;
  if (s->fStatus.state!=SweetPinsStopped && s->fStatus.state!=SweetPinsFailed)
   s->finish(kIOReturnAborted,SweetPinsFailed,"consumer cancelled");
  return kIOReturnSuccess;
 }
 if (!s->fAccepting || s->isInactive()) return kIOReturnNotReady;
 if (r.op==OpAttach) {
  if (s->fPoisoned) return kIOReturnNotReady;
  if (s->fClient) return kIOReturnBusy;
  if (s->fNextClient==~0ULL) return kIOReturnNoResources;
  s->fClient=++s->fNextClient; *r.out=s->fClient; return kIOReturnSuccess;
 }
 if (!r.client || r.client!=s->fClient) return kIOReturnNotFound;
 if (r.op==OpDetach) {
  if (s->fBusy || (s->fPoisoned && s->fTouched) || s->fConfigured) return kIOReturnBusy;
  s->fClient=0; s->fStatus={}; return kIOReturnSuccess;
 }
 if (s->fMode!=2) return kIOReturnNotPermitted;
 if (s->fPoisoned) return kIOReturnNotReady;
 if (s->fBusy) return kIOReturnBusy;
 if (s->fNextToken==~0ULL) return kIOReturnNoResources;
 if (r.op==OpActivate) {
  if (s->fConfigured) return kIOReturnBusy;
 } else if (r.op==OpReset || r.op==OpPark) {
  if (!s->fConfigured) return kIOReturnNotReady;
 } else return kIOReturnBadArgument;
 /* Admission only queues; pin sampling/writes execute on this workloop. */
 s->fOperation=r.op;s->fPhase=Configure;s->fBusy=true;s->fDiagnosticCount=0;
 s->fDeadline=nowNS()+100000000ULL; s->fWait=0;
 s->fStatus={}; s->fStatus.token=++s->fNextToken;
 s->fStatus.state=SweetPinsPending; s->fStatus.result=kIOReturnNotReady;
 *r.out=s->fStatus.token; s->fTimer->setTimeoutUS(1); return kIOReturnSuccess;
}
void SweetPanelPins::advance(void)
{
 if (!fAccepting || isInactive() || !fBusy) return;
 uint64_t now=nowNS();
 if (now>=fDeadline) { finish(kIOReturnTimeout,SweetPinsFailed,"operation deadline"); return; }
 bool capture=fOperation==OpActivate && fPhase==Configure && !fTouched;
 if (!sample(capture)) { finish(kIOReturnNotReady,SweetPinsFailed,"pin owner/configuration changed"); return; }
 if (fPhase==HighHold || fPhase==LowHold || fPhase==FinalHighHold || fPhase==Settle || fPhase==ParkHold)
  observeLevel(fPhase!=LowHold && fPhase!=ParkHold);
 if (now<fWait) { fTimer->setTimeoutUS(100); return; }
 if (fPhase==Configure) {
  if (fOperation==OpActivate) {
   observeLevel(!fParked);
   if (!writeControl(0,0x2c0) || !writeControl(1,5)) {
    finish(kIOReturnIOError,SweetPinsFailed,"active pinctrl readback"); return;
   }
   fConfigured=true; fStatus.resetKnown=true; fStatus.resetHigh=!fParked; fParked=false;
   finish(kIOReturnSuccess,SweetPinsActive,"active"); return;
  }
  bool high=fOperation==OpReset;
  /* LG reset_off selects suspend pinctrl before driving reset low. */
  if (!high && (!writeControl(0,0x201) || !writeControl(1,5))) {
   finish(kIOReturnIOError,SweetPinsFailed,"suspend pinctrl");return;
  }
  if (!writeReset(high)) { finish(kIOReturnIOError,SweetPinsFailed,"reset output latch"); return; }
  fPhase=high ? HighConfirm : ParkConfirm;
 } else if (fPhase==HighConfirm || fPhase==LowConfirm || fPhase==FinalHighConfirm || fPhase==ParkConfirm) {
  bool high=fPhase==HighConfirm || fPhase==FinalHighConfirm;
  observeLevel(high);
  fStatus.resetKnown=true;fStatus.resetHigh=high;
  fWait=now+(high ? 10000000ULL : (fPhase==ParkConfirm ? 5000000ULL : 2000000ULL));
  fPhase=fPhase==HighConfirm ? HighHold : (fPhase==LowConfirm ? LowHold :
      (fPhase==FinalHighConfirm ? FinalHighHold : ParkHold));
 } else if (fPhase==HighHold || fPhase==LowHold) {
  bool high=fPhase==LowHold;
  observeLevel(!high);
  if (!writeReset(high)) { finish(kIOReturnIOError,SweetPinsFailed,"reset hold/output latch"); return; }
  fPhase=high ? FinalHighConfirm : LowConfirm;
 } else if (fPhase==FinalHighHold) {
  observeLevel(true);
  fPhase=Settle; fWait=now+5000000ULL;
 } else if (fPhase==Settle) {
  observeLevel(true);
  finish(kIOReturnSuccess,SweetPinsResetComplete,"reset complete"); return;
 } else if (fPhase==ParkHold) {
  observeLevel(false);
  fConfigured=false; fParked=true; finish(kIOReturnSuccess,SweetPinsParked,"parked"); return;
 } else { finish(kIOReturnIOError,SweetPinsFailed,"invalid phase"); return; }
 fStatus.phase=fPhase; fTimer->setTimeoutUS(100);
}
void SweetPanelPins::timerAction(OSObject *owner,IOTimerEventSource *)
{ ((SweetPanelPins *)owner)->advance(); }
void SweetPanelPins::finish(IOReturn rc,unsigned state,const char *reason)
{
 if (fTimer) fTimer->cancelTimeout();
 fBusy=false; fStatus.result=rc; fStatus.state=state; fStatus.phase=fPhase;
 fStatus.uncertain=rc!=kIOReturnSuccess && fTouched;
 if (rc!=kIOReturnSuccess) {
  fPoisoned=true;
  if (fStatus.uncertain) fStatus.resetKnown=false;
  if (rc==kIOReturnAborted) SWEET_LOG_INFO("SWEET PANEL PINS: stopped phase=%u touched=%u; pins retained\n",fPhase,fTouched);
  else SWEET_LOG_ERROR("SWEET PANEL PINS: FAILED %s rc=%#x phase=%u touched=%u; pins retained, no rollback/retry\n",
      reason,(unsigned)rc,fPhase,fTouched);
 } else SWEET_LOG_INFO("SWEET PANEL PINS: token=%llu %s; reset commanded=%u, input/latch diagnostic only; no voltage/DSI/power proof\n",
      fStatus.token,reason,fStatus.resetHigh);
 setProperty("SweetPanelPinsState",(unsigned long long)state,32);
 setProperty("SweetPanelPinsUncertain",fStatus.uncertain);
}
IOReturn SweetPanelPins::request(unsigned op,uint64_t id,uint64_t *token)
{ if (!token) return kIOReturnBadArgument; *token=0; PanelPinsRequest r={}; r.op=op; r.client=id; r.out=token; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetPanelPins::attachConsumer(uint64_t *id)
{ return request(OpAttach,0,id); }
IOReturn SweetPanelPins::detachConsumer(uint64_t id)
{ PanelPinsRequest r={}; r.op=OpDetach; r.client=id; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetPanelPins::activate(uint64_t id,uint64_t *token)
{ return request(OpActivate,id,token); }
IOReturn SweetPanelPins::reset(uint64_t id,uint64_t *token)
{ return request(OpReset,id,token); }
IOReturn SweetPanelPins::park(uint64_t id,uint64_t *token)
{ return request(OpPark,id,token); }
IOReturn SweetPanelPins::copyStatus(uint64_t id,uint64_t token,SweetPanelPinStatus *status)
{ if (!status) return kIOReturnBadArgument; *status={}; PanelPinsRequest r={}; r.op=OpQuery; r.client=id; r.token=token; r.status=status; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetPanelPins::copyLowStatus(uint64_t id,uint64_t token,SweetPanelPinStatus *status)
{ if (!status) return kIOReturnBadArgument; *status={}; PanelPinsRequest r={}; r.op=OpQueryLow; r.client=id; r.token=token; r.status=status; return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetPanelPins::cancelRequest(uint64_t id,uint64_t token)
{ PanelPinsRequest r={};r.op=OpCancel;r.client=id;r.token=token;return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
void SweetPanelPins::shutdown(void)
{
 if (fGateAdded) { PanelPinsRequest r={}; r.op=OpStop; (void)fGate->runAction(action,&r); }
 if (fTimerAdded) { fLoop->removeEventSource(fTimer); fTimerAdded=false; }
}
bool SweetPanelPins::willTerminate(IOService *p,IOOptionBits o)
{ shutdown(); return IOService::willTerminate(p,o); }
void SweetPanelPins::stop(IOService *p)
{
 shutdown();
 if (fClient) SWEET_LOG_WARN("SWEET PANEL PINS: consumer %llu still attached at stop; no implicit pin transition\n",fClient);
 /* Release, do not park: the levels stay as commanded, only ownership moves. */
 releasePins();
 IOService::stop(p);
}
void SweetPanelPins::free(void)
{
 shutdown();
 if (fGateAdded) { fLoop->removeEventSource(fGate); fGateAdded=false; }
 OSSafeReleaseNULL(fTimer); OSSafeReleaseNULL(fGate); OSSafeReleaseNULL(fLoop);
 releasePins();
 if (fClaimed) { __atomic_store_n(&panelPinsOwner,fTouched ? 2u : 0u,__ATOMIC_RELEASE); fClaimed=false; }
 IOService::free();
}
SweetPanelPinsClient *
SweetPanelPinsClient::withOwner(OSObject *owner, IOWorkLoop *loop, void (*changed)(OSObject *))
{
 if (!owner || !loop || !changed) return NULL;
 SweetPanelPinsClient *self=OSTypeAlloc(SweetPanelPinsClient);
 if (!self) return NULL;
 if (!self->init()) { self->release(); return NULL; }
 self->fOwner=owner; self->fChanged=changed; self->fLoop=loop; loop->retain();
 self->fLock=IOLockAlloc();
 self->fEvent=IOInterruptEventSource::interruptEventSource(self, SweetPanelPinsClient::changed);
 if (!self->fLock || !self->fEvent) { self->release(); return NULL; }
 self->fEvent->disable();
 if (loop->addEventSource(self->fEvent)!=kIOReturnSuccess) { self->release(); return NULL; }
 self->fEventAdded=true;
 return self;
}

bool
SweetPanelPinsClient::watch(void)
{
 OSDictionary *matching=IOService::serviceMatching("SweetPanelPins");
 if (!matching) return false;
 fTerminated=IOService::addMatchingNotification(gIOTerminatedNotification,matching,terminated,this);
 if (fTerminated) fMatched=IOService::addMatchingNotification(gIOFirstMatchNotification,matching,matched,this);
 matching->release(); /* API retains the dictionary */
 if (!fTerminated || !fMatched) { stop(); return false; }
 fEvent->enable(); fEvent->interruptOccurred(NULL,NULL,0);
 return true;
}

bool
SweetPanelPinsClient::matched(void *target, void *, IOService *service, IONotifier *)
{
 SweetPanelPinsClient *self=(SweetPanelPinsClient *)target;
 SweetPanelPins *backend=OSDynamicCast(SweetPanelPins,service);
 if (!backend) return true;
 IOLockLock(self->fLock);
 if (!self->fStopping && !self->fBackend && !backend->isInactive()) {
  backend->retain(); self->fBackend=backend;
  self->fEvent->interruptOccurred(NULL,NULL,0);
 }
 IOLockUnlock(self->fLock);
 return true;
}

bool
SweetPanelPinsClient::terminated(void *target, void *, IOService *service, IONotifier *)
{
 SweetPanelPinsClient *self=(SweetPanelPinsClient *)target;
 SweetPanelPins *old=NULL;
 IOLockLock(self->fLock);
 if (self->fBackend==service) {
  old=self->fBackend; self->fBackend=NULL;
  if (!self->fStopping) self->fEvent->interruptOccurred(NULL,NULL,0);
 }
 IOLockUnlock(self->fLock);
 /* No provider method or register access in notification context. In-flight
  * callers hold their own reference; provider checks inactive under its gate. */
 OSSafeReleaseNULL(old);
 return true;
}

void
SweetPanelPinsClient::changed(OSObject *owner, IOInterruptEventSource *, int)
{
 SweetPanelPinsClient *self=(SweetPanelPinsClient *)owner;
 IOLockLock(self->fLock); bool active=!self->fStopping; IOLockUnlock(self->fLock);
 if (active) self->fChanged(self->fOwner);
}

SweetPanelPins *
SweetPanelPinsClient::copyBackend(void)
{
 IOLockLock(fLock);
 SweetPanelPins *backend=!fStopping ? fBackend : NULL;
 if (backend) backend->retain();
 IOLockUnlock(fLock);
 return backend;
}

void
SweetPanelPinsClient::stop(void)
{
 if (fLock) { IOLockLock(fLock); fStopping=true; IOLockUnlock(fLock); }
 /* remove consumes the notifier and synchronizes callbacks. Never hold a
  * consumer lock/gate here: a replaying notification may be waiting on it. */
 if (fMatched) { fMatched->remove(); fMatched=NULL; }
 if (fTerminated) { fTerminated->remove(); fTerminated=NULL; }
 if (fEventAdded) {
  fEvent->disable(); fLoop->removeEventSource(fEvent); fEventAdded=false;
 }
 if (fLock) {
  IOLockLock(fLock); SweetPanelPins *old=fBackend; fBackend=NULL; IOLockUnlock(fLock);
  OSSafeReleaseNULL(old);
 }
}

void
SweetPanelPinsClient::free(void)
{
 stop(); OSSafeReleaseNULL(fEvent); OSSafeReleaseNULL(fLoop);
 if (fLock) { IOLockFree(fLock); fLock=NULL; }
 OSObject::free();
}

#endif
