#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetTouchPanel.h"
/* Pad-interrupt API; the mask/unmask helpers below are defined before the rest
 * of the includes, so it has to come first. */
#include <pexpert/arm64/tlmm_gpio.h>
#include "TLMMGPIOController.h"

extern "C" void (*ts_irq_mask_fn)(void);
extern "C" void (*ts_irq_unmask_fn)(void);
/* The worker masks and re-arms the distributor bit itself; the shared
 * controller's disableInterrupt cannot run at interrupt level. */
extern "C" void sweet_touch_irq_own_line(void);
extern "C" void sweet_touch_irq_delegate_line(unsigned);
extern "C" void sweet_touch_irq_release_line(void);

static SweetTouchPanel *gTouchPanel;

/*
 * Pad-interrupt entry: the controller already masked the summary line and
 * cleared this pad's status, so the existing top half runs unchanged.
 */
extern "C" void
sweet_touch_tlmm_irq(unsigned int pin, void *refcon)
{
	(void)pin;
	(void)refcon;
	sweet_touch_irq_action(NULL, NULL, NULL, 0);
}

static void
sweet_touch_panel_mask(void)
{
	if (gTouchPanel) {
		gTouchPanel->irqMask();
	}
}

static void
sweet_touch_panel_unmask(void)
{
	if (gTouchPanel) {
		gTouchPanel->irqUnmask();
	}
}

static void
sweet_touch_panel_ack(void)
{
	if (gTouchPanel) {
		gTouchPanel->irqAck();
	}
}

/*
 * Mask/unmask always go through whoever owns INTID 240. With the pad path the
 * controller already masked the line for the dispatch; these calls only keep it
 * masked until the worker stops, which is the same deferral as before, but the
 * controller owns the GIC registers.
 */
void
SweetTouchPanel::irqMask(void)
{
	/* Pad path: the handle owns the pad's interrupt enable bit, so masking
	 * cannot name a pin this object does not hold. */
	if (fIRQViaTLMM && fIRQPin) {
		(void) fIRQPin->maskInterrupt();
	} else if (fIRQProvider) {
		(void) fIRQProvider->disableInterrupt(0);
	}
}

void
SweetTouchPanel::irqUnmask(void)
{
	if (fIRQViaTLMM && fIRQPin) {
		(void) fIRQPin->resumeInterrupt();
	} else if (fIRQProvider) {
		(void) fIRQProvider->enableInterrupt(0);
	}
}

void
SweetTouchPanel::irqAck(void)
{
	if (fIRQViaTLMM && fIRQPin) {
		(void) fIRQPin->ackInterrupt();
	}
}
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <sys/errno.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"
OSDefineMetaClassAndStructors(SweetTouchPanel,IOService);
OSDefineMetaClassAndStructors(SweetTouchPanelClient,OSObject);
struct TouchPanelRequest { unsigned op; uint64_t client,token; uint64_t *out; SweetTouchPanelStatus *status; bool *available; };
static IOReturn touchPanelReturn(int rc)
{
 switch (rc) {
 case 0:return kIOReturnSuccess;
 case EBUSY:return kIOReturnBusy;
 case ENOENT:return kIOReturnNotFound;
 case EINVAL:return kIOReturnBadArgument;
 case EOVERFLOW:return kIOReturnNoResources;
 case EAGAIN:return kIOReturnNotReady;
 default:return kIOReturnIOError;
 }
}
uint64_t SweetTouchPanel::nowNS(void)
{ uint64_t n=0; absolutetime_to_nanoseconds(mach_absolute_time(),&n); return n; }
bool SweetTouchPanel::start(IOService *p)
{
 int mode=0;(void)PE_parse_boot_argn("sweettouchpanel",&mode,sizeof(mode));
 /* Mode 3 is mode 2 plus the delegated pad interrupt (the ATTENTION line is
  * the TLMM summary, not a direct-connect GIC source). */
 if ((mode!=1 && !sweetTouchPanelNative(mode)) || !IOService::start(p)) return false;
 OSData *abi=OSDynamicCast(OSData,p->getProperty("sweet,touch-panel-abi"));
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4)) return false;
 if (sweet_touch_panel_open(&fSession)!=0) return false;
 fMode=(unsigned)mode;
 fLoop=IOWorkLoop::workLoop();fGate=IOCommandGate::commandGate(this);
 fTimer=IOTimerEventSource::timerEventSource(this,timerAction);
 if (!fLoop || !fGate || !fTimer) return false;
 if (fLoop->addEventSource(fGate)!=kIOReturnSuccess) return false;
 fGateAdded=true;
 if (fLoop->addEventSource(fTimer)!=kIOReturnSuccess) return false;
 fTimerAdded=true;fAccepting=true;
 gTouchPanel=this;
 /*
  * The ADT node carries <INTID TYPE FLAGS> plus interrupt-parent; the provider
  * (its IOPlatformDevice nub) owns the resolved source. Registering here makes
  * the controller dispatch this driver instead of the old sleh predicate, and
  * the hooks below route enable/mask back through IOService::*Interrupt.
  */
 /*
  * INTID 240 is the TLMM summary line, not a dedicated touch line. When the
  * pin controller owns it, the pad interrupt is requested here and no GIC
  * source is registered on this nub: one owner per line, per-pad status.
  * Without sweetgpioirq=1 the request is unsupported and the proven nub path
  * below stays in charge.
  */
 TLMMGPIOController *pinController = TLMMGPIOController::getController();
 if (fMode==3 && pinController &&
     (fIRQPin=pinController->acquirePin(SWEET_TOUCH_IRQ_PIN,
      "sweet-touch-attention"))!=NULL &&
     fIRQPin->setInterrupt(TLMM_IRQ_LEVEL_LOW, sweet_touch_tlmm_irq,
      this)==kIOReturnSuccess) {
  fIRQViaTLMM=true;fIRQRegistered=true;
  /* The summary SPI belongs to the pin controller; this facade owns the pad. */
  sweet_touch_irq_delegate_line(1);
  ts_irq_mask_fn=sweet_touch_panel_mask;
  ts_irq_unmask_fn=sweet_touch_panel_unmask;
  ts_irq_ack_fn=sweet_touch_panel_ack;
  SWEET_LOG_INFO("SWEET TOUCH PANEL: ATTENTION pad gpio%u claimed through its AGPIO handle and delegated to the TLMM summary line; no GIC source on the nub\n",SWEET_TOUCH_IRQ_PIN);
 } else if (p->propertyExists("IOInterruptSpecifiers")) {
  if (p->registerInterrupt(0, NULL, (IOInterruptAction)sweet_touch_irq_action, NULL)==kIOReturnSuccess) {
   fIRQProvider=p;fIRQRegistered=true;
   ts_irq_mask_fn=sweet_touch_panel_mask;
   ts_irq_unmask_fn=sweet_touch_panel_unmask;
   /* INTID 240 is this nub's own source: the worker keeps its distributor
    * bit, because masking through the shared controller re-enters its lock. */
   sweet_touch_irq_own_line();
   SWEET_LOG_INFO("SWEET TOUCH PANEL: GIC source registered on the ADT nub\n");
  } else {
   SWEET_LOG_ERROR("SWEET TOUCH PANEL: GIC registration failed; touch IRQ unavailable\n");
  }
 } else {
  SWEET_LOG_ERROR("SWEET TOUCH PANEL: ADT node has no interrupt specifier; touch IRQ unavailable\n");
 }
 /* A pad handle that did not end up owning the interrupt must go back: the
  * nub path above and the pad path are exclusive, never both. */
 if (!fIRQViaTLMM && fIRQPin) { fIRQPin->put(); fIRQPin=NULL; }
 setProperty("SweetTouchPanelABI",1ULL,32);
 setProperty("SweetTouchPanelWritesEnabled",sweetTouchPanelNative(mode));
 SWEET_LOG_INFO("SWEET TOUCH PANEL: native binding mode=%u; sole legacy worker retained, no automatic pause/resume\n",fMode);
 registerService();return true;
}
void SweetTouchPanel::refresh(void)
{
 if (!fSession || !fStatus.token) return;
 sweet_touch_panel_status p={};
 int rc=sweet_touch_panel_snapshot(fSession,fStatus.token,&p);
 if (rc) { fStatus.state=SWEET_TOUCH_FAILED;fStatus.result=touchPanelReturn(rc); }
 else {
  fStatus.state=p.available ? p.state : (unsigned)SWEET_TOUCH_FAILED;
  bool pending=p.state==SWEET_TOUCH_PAUSE_REQUESTED || p.state==SWEET_TOUCH_PAUSING ||
      p.state==SWEET_TOUCH_RESUME_REQUESTED || p.state==SWEET_TOUCH_RESUMING;
  fStatus.quiesced=p.available && p.state==SWEET_TOUCH_QUIESCED;
  fStatus.ready=p.available && p.state==SWEET_TOUCH_RUNNING;
  fStatus.result=(fStatus.quiesced || fStatus.ready) ? kIOReturnSuccess :
      (pending && p.available ? kIOReturnNotReady : kIOReturnIOError);
  if (pending && p.available) return;
 }
 fBusy=false;
 if (fTimer) fTimer->cancelTimeout();
 if (fStatus.result!=kIOReturnSuccess) {
  fPoisoned=true;fStatus.uncertain=fTouched;fStatus.quiesced=fStatus.ready=false;
  SWEET_LOG_ERROR("SWEET TOUCH PANEL: worker unavailable/failed token=%llu state=%u; no implicit resume\n",fStatus.token,fStatus.state);
 }
}
IOReturn SweetTouchPanel::action(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetTouchPanel *s=(SweetTouchPanel *)owner;TouchPanelRequest &r=*(TouchPanelRequest *)arg;
 if (r.op==OpStop) {
  s->fAccepting=false;
  if (s->fTimer) s->fTimer->cancelTimeout();
  if (s->fSession) { (void)sweet_touch_panel_close(s->fSession);s->fSession=0; }
  if (s->fStatus.token) {
   s->fStatus.state=SWEET_TOUCH_STOPPED;s->fStatus.result=kIOReturnAborted;
   s->fStatus.quiesced=s->fStatus.ready=false;s->fStatus.uncertain=s->fTouched;
  }
  s->fBusy=false;return kIOReturnSuccess;
 }
 if (r.op==OpQuery) {
  if (!r.client || r.client!=s->fClient || !r.token || r.token!=s->fStatus.token) return kIOReturnNotFound;
  if (s->fAccepting && !s->fPoisoned) s->refresh();
  *r.status=s->fStatus;return kIOReturnSuccess;
 }
 if (r.op==OpCancel) {
  if (!r.client || r.client!=s->fClient || !r.token || r.token!=s->fStatus.token) return kIOReturnNotFound;
  if (s->fTimer) s->fTimer->cancelTimeout();
  if (s->fSession) { (void)sweet_touch_panel_close(s->fSession);s->fSession=0; }
  s->fBusy=false;s->fPoisoned=true;
  s->fStatus.state=SWEET_TOUCH_STOPPED;s->fStatus.result=kIOReturnAborted;
  s->fStatus.quiesced=s->fStatus.ready=false;s->fStatus.uncertain=s->fTouched;
  return kIOReturnSuccess;
 }
 if (!s->fAccepting || s->isInactive()) return kIOReturnNotReady;
 if (r.op==OpInfo) {
  sweet_touch_panel_status p={};
  IOReturn rc=touchPanelReturn(sweet_touch_panel_snapshot(s->fSession,0,&p));
  *r.available=!s->fPoisoned && rc==kIOReturnSuccess && p.available && p.state!=SWEET_TOUCH_FAILED && p.state!=SWEET_TOUCH_STOPPED;
  return rc;
 }
 if (r.op==OpAttach) {
  if (s->fPoisoned) return kIOReturnNotReady;
  if (s->fClient) return kIOReturnBusy;
  if (s->fNextClient==~0ULL) return kIOReturnNoResources;
  s->fClient=++s->fNextClient;*r.out=s->fClient;return kIOReturnSuccess;
 }
 if (!r.client || r.client!=s->fClient) return kIOReturnNotFound;
 if (r.op==OpDetach) {
  if (!s->fPoisoned) s->refresh();
  if (s->fBusy || s->fPoisoned || s->fStatus.quiesced) return kIOReturnBusy;
  s->fClient=0;s->fStatus={};return kIOReturnSuccess;
 }
 /* Mode 3 is mode 2 plus the delegated pad interrupt; only the IRQ delegate
  * differs, so the worker operations the panel lifecycle needs stay permitted.
  * Mode 1 remains metadata-only. */
 if (!sweetTouchPanelNative(s->fMode)) return kIOReturnNotPermitted;
 if (s->fPoisoned) return kIOReturnNotReady;
 if (s->fBusy) return kIOReturnBusy;
 uint64_t token=0;
 IOReturn rc=touchPanelReturn(sweet_touch_panel_request(s->fSession,r.op==OpResume,&token));
 if (rc!=kIOReturnSuccess) return rc;
 s->fStatus={};s->fStatus.token=token;
 s->fStatus.state=r.op==OpResume ? SWEET_TOUCH_RESUME_REQUESTED : SWEET_TOUCH_PAUSE_REQUESTED;
 s->fStatus.result=kIOReturnNotReady;s->fBusy=s->fTouched=true;
 s->fDeadline=nowNS()+6000000000ULL;*r.out=token;s->fTimer->setTimeoutMS(1);
 return kIOReturnSuccess;
}
void SweetTouchPanel::timerAction(OSObject *owner,IOTimerEventSource *)
{
 SweetTouchPanel *s=(SweetTouchPanel *)owner;
 if (!s->fAccepting || s->isInactive() || !s->fBusy) return;
 s->refresh();
 if (!s->fBusy) return;
 if (nowNS()>=s->fDeadline) {
  (void)sweet_touch_panel_close(s->fSession);s->fSession=0;
  s->fBusy=false;s->fPoisoned=true;
  s->fStatus.state=SWEET_TOUCH_FAILED;s->fStatus.result=kIOReturnTimeout;
  s->fStatus.ready=s->fStatus.quiesced=false;s->fStatus.uncertain=true;
  SWEET_LOG_ERROR("SWEET TOUCH PANEL: worker deadline; cancelled, no quiescence assumed\n");
  return;
 }
 s->fTimer->setTimeoutMS(1);
}
IOReturn SweetTouchPanel::request(unsigned op,uint64_t id,uint64_t *token)
{ if (!token) return kIOReturnBadArgument;*token=0;TouchPanelRequest r={};r.op=op;r.client=id;r.out=token;return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetTouchPanel::attachConsumer(uint64_t *id) { return request(OpAttach,0,id); }
IOReturn SweetTouchPanel::quiesce(uint64_t id,uint64_t *token) { return request(OpQuiesce,id,token); }
IOReturn SweetTouchPanel::resume(uint64_t id,uint64_t *token) { return request(OpResume,id,token); }
IOReturn SweetTouchPanel::detachConsumer(uint64_t id)
{ TouchPanelRequest r={};r.op=OpDetach;r.client=id;return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
/* A worker that exits before going live publishes SWEET_TOUCH_FAILED and
 * clears availability; it never recovers, so callers may stop waiting. */
IOReturn SweetTouchPanel::copyWorkerFailed(bool *failed)
{ if (!failed) return kIOReturnBadArgument;*failed=false;
  if (!fSession) return kIOReturnNotReady;
  sweet_touch_panel_status p={};
  int rc=sweet_touch_panel_snapshot(fSession,0,&p);
  if (rc) return touchPanelReturn(rc);
  *failed=!p.available && p.state==(unsigned)SWEET_TOUCH_FAILED;
  return kIOReturnSuccess; }
IOReturn SweetTouchPanel::copyWorkerAvailable(bool *available)
{ if (!available) return kIOReturnBadArgument;*available=false;TouchPanelRequest r={};r.op=OpInfo;r.available=available;return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetTouchPanel::copyStatus(uint64_t id,uint64_t token,SweetTouchPanelStatus *status)
{ if (!status) return kIOReturnBadArgument;*status={};TouchPanelRequest r={};r.op=OpQuery;r.client=id;r.token=token;r.status=status;return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
IOReturn SweetTouchPanel::cancelRequest(uint64_t id,uint64_t token)
{ TouchPanelRequest r={};r.op=OpCancel;r.client=id;r.token=token;return fGateAdded ? fGate->runAction(action,&r) : kIOReturnNotReady; }
void SweetTouchPanel::shutdown(void)
{
 if (fGateAdded) { TouchPanelRequest r={};r.op=OpStop;(void)fGate->runAction(action,&r); }
 else if (fSession) { (void)sweet_touch_panel_close(fSession);fSession=0; }
 if (fTimerAdded) { fLoop->removeEventSource(fTimer);fTimerAdded=false; }
}
bool SweetTouchPanel::willTerminate(IOService *p,IOOptionBits o)
{ shutdown();return IOService::willTerminate(p,o); }
void SweetTouchPanel::stop(IOService *p)
{
 shutdown();
 if (fIRQRegistered) {
  sweet_touch_irq_release_line();
  if (ts_irq_mask_fn==sweet_touch_panel_mask) ts_irq_mask_fn=NULL;
  if (ts_irq_unmask_fn==sweet_touch_panel_unmask) ts_irq_unmask_fn=NULL;
  if (fIRQViaTLMM) {
   sweet_touch_irq_delegate_line(0);
   if (ts_irq_ack_fn==sweet_touch_panel_ack) ts_irq_ack_fn=NULL;
   if (fIRQPin) { (void)fIRQPin->disableInterrupt(); fIRQPin->put(); fIRQPin=NULL; }
  }
  else if (fIRQProvider) {
   (void)fIRQProvider->disableInterrupt(0);
   (void)fIRQProvider->unregisterInterrupt(0);
  }
  fIRQRegistered=false;fIRQProvider=NULL;fIRQViaTLMM=false;
 }
 if (fClient) SWEET_LOG_WARN("SWEET TOUCH PANEL: consumer still attached at stop; worker cancellation retained\n");
 IOService::stop(p);
}
void SweetTouchPanel::free(void)
{
 shutdown();
 if (fIRQRegistered) {
  sweet_touch_irq_release_line();
  if (ts_irq_mask_fn==sweet_touch_panel_mask) ts_irq_mask_fn=NULL;
  if (ts_irq_unmask_fn==sweet_touch_panel_unmask) ts_irq_unmask_fn=NULL;
  if (fIRQViaTLMM) {
   sweet_touch_irq_delegate_line(0);
   if (ts_irq_ack_fn==sweet_touch_panel_ack) ts_irq_ack_fn=NULL;
   if (fIRQPin) { (void)fIRQPin->disableInterrupt(); fIRQPin->put(); fIRQPin=NULL; }
  }
  else if (fIRQProvider) {
   (void)fIRQProvider->disableInterrupt(0);
   (void)fIRQProvider->unregisterInterrupt(0);
  }
  fIRQRegistered=false;fIRQProvider=NULL;fIRQViaTLMM=false;
 }
 if (gTouchPanel==this) gTouchPanel=NULL;
 if (fGateAdded) { fLoop->removeEventSource(fGate);fGateAdded=false; }
 OSSafeReleaseNULL(fTimer);OSSafeReleaseNULL(fGate);OSSafeReleaseNULL(fLoop);
 IOService::free();
}
SweetTouchPanelClient *
SweetTouchPanelClient::withOwner(OSObject *owner, IOWorkLoop *loop, void (*changed)(OSObject *))
{
 if (!owner || !loop || !changed) return NULL;
 SweetTouchPanelClient *self=OSTypeAlloc(SweetTouchPanelClient);
 if (!self) return NULL;
 if (!self->init()) { self->release(); return NULL; }
 self->fOwner=owner; self->fChanged=changed; self->fLoop=loop; loop->retain();
 self->fLock=IOLockAlloc();
 self->fEvent=IOInterruptEventSource::interruptEventSource(self, SweetTouchPanelClient::changed);
 if (!self->fLock || !self->fEvent) { self->release(); return NULL; }
 self->fEvent->disable();
 if (loop->addEventSource(self->fEvent)!=kIOReturnSuccess) { self->release(); return NULL; }
 self->fEventAdded=true;
 return self;
}

bool
SweetTouchPanelClient::watch(void)
{
 OSDictionary *matching=IOService::serviceMatching("SweetTouchPanel");
 if (!matching) return false;
 fTerminated=IOService::addMatchingNotification(gIOTerminatedNotification,matching,terminated,this);
 if (fTerminated) fMatched=IOService::addMatchingNotification(gIOFirstMatchNotification,matching,matched,this);
 matching->release(); /* API retains the dictionary */
 if (!fTerminated || !fMatched) { stop(); return false; }
 fEvent->enable(); fEvent->interruptOccurred(NULL,NULL,0);
 return true;
}

bool
SweetTouchPanelClient::matched(void *target, void *, IOService *service, IONotifier *)
{
 SweetTouchPanelClient *self=(SweetTouchPanelClient *)target;
 SweetTouchPanel *backend=OSDynamicCast(SweetTouchPanel,service);
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
SweetTouchPanelClient::terminated(void *target, void *, IOService *service, IONotifier *)
{
 SweetTouchPanelClient *self=(SweetTouchPanelClient *)target;
 SweetTouchPanel *old=NULL;
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
SweetTouchPanelClient::changed(OSObject *owner, IOInterruptEventSource *, int)
{
 SweetTouchPanelClient *self=(SweetTouchPanelClient *)owner;
 IOLockLock(self->fLock); bool active=!self->fStopping; IOLockUnlock(self->fLock);
 if (active) self->fChanged(self->fOwner);
}

SweetTouchPanel *
SweetTouchPanelClient::copyBackend(void)
{
 IOLockLock(fLock);
 SweetTouchPanel *backend=!fStopping ? fBackend : NULL;
 if (backend) backend->retain();
 IOLockUnlock(fLock);
 return backend;
}

void
SweetTouchPanelClient::stop(void)
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
  IOLockLock(fLock); SweetTouchPanel *old=fBackend; fBackend=NULL; IOLockUnlock(fLock);
  OSSafeReleaseNULL(old);
 }
}

void
SweetTouchPanelClient::free(void)
{
 stop(); OSSafeReleaseNULL(fEvent); OSSafeReleaseNULL(fLoop);
 if (fLock) { IOLockFree(fLock); fLock=NULL; }
 OSObject::free();
}


#endif
