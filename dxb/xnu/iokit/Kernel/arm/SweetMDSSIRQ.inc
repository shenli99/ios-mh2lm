/* F3: SM8150 INTF1 TE RD_PTR, hardware-owner workloop only.
 * LG sde_hw_interrupts.c: TE IRQ +0x6e800, EN/STATUS/W1C=0/4/8, bit2.
 * No Linux IRQ domain. Physical dispatch follows the established sleh callback.
 */
extern "C" bool sweet_mdss_irq(unsigned int intid);
OSDefineMetaClassAndStructors(SweetVsyncChannel,OSObject);

SweetVsyncChannel *
SweetVsyncChannel::withEvent(IOInterruptEventSource *event)
{
 if (!event) return NULL;
 SweetVsyncChannel *self=OSTypeAlloc(SweetVsyncChannel);
 if (!self) return NULL;
 if (!self->init()) { self->release(); return NULL; }
 self->fLock=IOSimpleLockAlloc();
 if (!self->fLock) { self->release(); return NULL; }
 event->retain(); self->fEvent=event;
 return self;
}
void
SweetVsyncChannel::publish(uint64_t frame,uint64_t ticks,unsigned state)
{
 IOInterruptState saved=IOSimpleLockLockDisableInterrupt(fLock);
 if (fEvent) {
  ++fSample.sequence; fSample.frame=frame; fSample.ticks=ticks; fSample.state=state;
  fEvent->interruptOccurred(NULL,NULL,0); /* queue only; no consumer gate */
 }
 IOSimpleLockUnlockEnableInterrupt(fLock,saved);
}
bool
SweetVsyncChannel::copySample(SweetVsyncSample *sample)
{
 if (!sample || !fLock) return false;
 IOInterruptState saved=IOSimpleLockLockDisableInterrupt(fLock);
 bool valid=fEvent && fSample.sequence;
 if (valid) *sample=fSample;
 IOSimpleLockUnlockEnableInterrupt(fLock,saved);
 return valid;
}
void
SweetVsyncChannel::close(void)
{
 if (!fLock) return;
 IOInterruptState saved=IOSimpleLockLockDisableInterrupt(fLock);
 IOInterruptEventSource *old=fEvent; fEvent=NULL;
 IOSimpleLockUnlockEnableInterrupt(fLock,saved);
 OSSafeReleaseNULL(old);
}
void
SweetVsyncChannel::free(void)
{
 close(); if (fLock) { IOSimpleLockFree(fLock); fLock=NULL; }
 OSObject::free();
}

/* Permanent dispatch lock, no global consumer discovery. Gate -> dispatch lock;
 * top half only takes dispatch lock, never a workloop gate. */
static IOSimpleLock *mdssIRQDispatch;
static SweetMDSS *mdssIRQService;
static IOSimpleLock *
mdss_irq_lock(void)
{
 IOSimpleLock *lock=__atomic_load_n(&mdssIRQDispatch,__ATOMIC_ACQUIRE);
 if (!lock) {
  IOSimpleLock *candidate=IOSimpleLockAlloc();
  if (!candidate) return NULL;
  if (__atomic_compare_exchange_n(&mdssIRQDispatch,&lock,candidate,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) lock=candidate;
  else IOSimpleLockFree(candidate);
 }
 return lock;
}
#define MDSS_IRQ_BIT (1u<<19)
#define MDSS_RDPTR_BIT (1u<<2)
static uint64_t
mdss_irq_ns(uint64_t ticks)
{
 uint64_t ns; absolutetime_to_nanoseconds(ticks,&ns); return ns;
}
void
SweetMDSS::dp_wr64(vm_offset_t base,uint32_t off,uint64_t value)
{
 __asm__ volatile("dmb oshst" ::: "memory");
 *(volatile uint64_t *)(base+off)=value;
 __asm__ volatile("dsb sy" ::: "memory");
}

void
SweetMDSS::setupIRQ(IOService *provider)
{
 int mode=0; (void)PE_parse_boot_argn("sweetmdssirq",&mode,sizeof(mode));
 if (!mode) return;
 if (mode<1 || mode>2) {
  SWEET_LOG_ERROR("SWEET MDSS IRQ: invalid mode=%d; IRQ disabled\n",mode); return;
 }
 fIrqMode=(unsigned)mode; fIrqDisplayOn=true;
 __atomic_store_n(&fIrqState,IrqFailed,__ATOMIC_RELEASE);
 if ((mode==2 && !fPanelPowerEnabled) || !mapIrqResources(provider,true)) {
  failIRQ("requires validated resources and power1 for RD_PTR"); return;
 }
 if (mode==2) {
  fIntervals=(uint32_t *)IOMallocData(4096*sizeof(*fIntervals));
  if (!fIntervals) { failIRQ("interval buffer allocation"); return; }
 }
 fIrqEvent=IOInterruptEventSource::interruptEventSource(this,irqAction);
 fIrqTimer=IOTimerEventSource::timerEventSource(this,irqTimerAction);
 if (!fIrqEvent || !fIrqTimer) { failIRQ("event allocation"); return; }
 if (fLoop->addEventSource(fIrqEvent)!=kIOReturnSuccess) { failIRQ("event add"); return; }
 fIrqEventAdded=true;
 if (fLoop->addEventSource(fIrqTimer)!=kIOReturnSuccess) { failIRQ("timer add"); return; }
 fIrqTimerAdded=true;
 IOSimpleLock *lock=mdss_irq_lock();
 if (!lock) { failIRQ("dispatch lock allocation"); return; }
 IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
 if (!mdssIRQService) { mdssIRQService=this; fIrqClaimed=true; }
 IOSimpleLockUnlockEnableInterrupt(lock,saved);
 if (!fIrqClaimed) { failIRQ("INTID115 already claimed"); return; }
 __atomic_store_n(&fIrqState,IrqIdle,__ATOMIC_RELEASE);
 setProperty("SweetDisplayIRQMode",(unsigned long long)fIrqMode,32);
 SWEET_LOG_INFO("SWEET MDSS IRQ: F3 v2 staged mode=%u intid=115; GIC probe at35s, RD_PTR bit2 only\n",fIrqMode);
 fIrqTimer->setTimeoutMS(35000);
}

bool
SweetMDSS::waitIRQ(void)
{
 const vm_offset_t gic=fMaps[11]->getVirtualAddress();
 /* Bounded worker-only RWP wait, never under dispatch spin lock/top half. */
 for (unsigned us=0;us<1000;++us) {
  if (!(dp_rd(gic,0)&(1u<<31))) return true;
  delay(1);
 }
 return false;
}
bool
SweetMDSS::configureIRQ(void)
{
 const vm_offset_t gic=fMaps[11]->getVirtualAddress();
 const vm_offset_t top=fMaps[9]->getVirtualAddress(), tear=fMaps[10]->getVirtualAddress();
 if (!dp_clocks_on() || (dp_rd(gic,0xffe8)&0xf0u)!=0x30u ||
     ((dp_rd(gic,4)&31u)+1u)*32u<=115u || !(dp_rd(gic,0)&2u)) return false;
 /* Refuse an inherited owner. Pending raw disabled TE bits are harmless;
  * never clear every raw bit just because it was left latched by ABL. */
 if ((dp_rd(gic,0x10c)|dp_rd(gic,0x20c)|dp_rd(gic,0x30c))&MDSS_IRQ_BIT) return false;
 if (dp_rd(top,0)!=0x50000001u || dp_rd(top,0x10) || dp_rd(top,0x1010) ||
     dp_rd(top,0x1008) || dp_rd(top,0x101c) || dp_rd(tear,0) || dp_rd(tear,0x100) ||
     dp_rd(dp_intf1,0x1c0) || dp_rd(dp_intf2,0x1c0)) return false;
 if (dp_rd(dp_intf1,0x284)!=1u || dp_rd(dp_intf1,0x288)!=0x180088u ||
     dp_rd(dp_intf1,0x2a4)!=0x925u || dp_rd(dp_intf1,0x2b4)!=0x80000001u) return false;
 fOldGroup=dp_rd(gic,0x08c)&MDSS_IRQ_BIT;
 fOldCfg=dp_rd(gic,0xc1c)&0xc0u;
 fOldPriority=dp_rd(gic,0x470)&0xff000000u;
 fOldRoute=(uint64_t)dp_rd(gic,0x6398)|((uint64_t)dp_rd(gic,0x639c)<<32);
 fGicTouched=true;
 dp_wr(gic,0x18c,MDSS_IRQ_BIT);
 if (!waitIRQ()) return false;
 dp_wr(gic,0x08c,dp_rd(gic,0x08c)|MDSS_IRQ_BIT);
 dp_wr(gic,0xc1c,dp_rd(gic,0xc1c)&~0xc0u);
 dp_wr(gic,0x470,(dp_rd(gic,0x470)&0x00ffffffu)|0xa0000000u);
 dp_wr64(gic,0x6398,0); /* established boot CPU MPIDR0 route */
 if (!waitIRQ() || (dp_rd(gic,0xc1c)&0xc0u) ||
     (dp_rd(gic,0x470)>>24)!=0xa0u || dp_rd(gic,0x6398) || dp_rd(gic,0x639c)) return false;
 /* Group may RAZ in this nonsecure view; delivery proof is asynchronous. */
 __atomic_store_n(&fProbeHits,0u,__ATOMIC_RELEASE);
 __atomic_store_n(&fIrqState,IrqProbe,__ATOMIC_RELEASE);
 dp_wr(gic,0x10c,MDSS_IRQ_BIT);
 dp_wr(gic,0x20c,MDSS_IRQ_BIT); /* inject only this owned SPI, not a peripheral */
 fIrqPhase=1;
 fIrqTimer->setTimeoutMS(50);
 return true;
}

bool
SweetMDSS::handleIRQ(void)
{
 /* Invoked with the physical dispatch lock held: mappings/event stay alive. */
 dp_wr(fMaps[11]->getVirtualAddress(),0x18c,MDSS_IRQ_BIT);
 __atomic_add_fetch(&fIrqEntries,1u,__ATOMIC_RELAXED);
 unsigned state=__atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE);
 if (state==IrqProbe) {
  __atomic_add_fetch(&fProbeHits,1u,__ATOMIC_RELEASE); return true;
 }
 unsigned expected=IrqArmed;
 if (!__atomic_compare_exchange_n(&fIrqState,&expected,IrqPending,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return true;
 __atomic_store_n(&fIrqTicks,mach_absolute_time(),__ATOMIC_RELEASE);
 fIrqEvent->interruptOccurred(NULL,NULL,0);
 return true;
}
extern "C" bool
sweet_mdss_irq(unsigned int intid)
{
 if (intid!=115u) return false;
 IOSimpleLock *lock=__atomic_load_n(&mdssIRQDispatch,__ATOMIC_ACQUIRE);
 if (lock) {
  IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
  if (mdssIRQService) (void)mdssIRQService->handleIRQ();
  IOSimpleLockUnlockEnableInterrupt(lock,saved);
 }
 return true; /* reserved MDSS SPI; late stopped delivery still receives EOI */
}

void
SweetMDSS::publishVsync(unsigned state)
{
 if (fVsyncChannel) fVsyncChannel->publish(fFrameCount,state==SweetVsyncFrame ?
     fFrameTicks : 0,state);
}
void
SweetMDSS::pauseIRQ(void)
{
 if (!fIrqMode || !fIrqClaimed) return;
 setProperty("SweetDisplayInterruptEnabled",false);
 unsigned state=__atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE);
 /* A pending software probe is independent of logical display OFF. */
 if (fIrqPhase<2 && state!=IrqFailed && state!=IrqStopped) {
  if (!fIrqDisplayOn) publishVsync(SweetVsyncOff);
  return;
 }
 if (state!=IrqFailed && state!=IrqStopped) __atomic_store_n(&fIrqState,IrqIdle,__ATOMIC_RELEASE);
 if (fGicTouched) dp_wr(fMaps[11]->getVirtualAddress(),0x18c,MDSS_IRQ_BIT);
 if (fSourceOwned && (fCorePhase==CoreIdle || coreReady()) && dp_clocks_on()) {
  const vm_offset_t tear=fMaps[10]->getVirtualAddress();
  dp_wr(tear,0,dp_rd(tear,0)&~MDSS_RDPTR_BIT);
  dp_wr(tear,8,MDSS_RDPTR_BIT);
 }
 if (fIrqPhase==2 && fIrqTimer) fIrqTimer->cancelTimeout();
 if (fWindowFrames) reportIRQ(true);
 if (fIrqPhase==2 && fIrqMode>=2) {
  SWEET_LOG_INFO("SWEET MDSS IRQ: paused frames=%llu entries=%u\n",fFrameCount,__atomic_load_n(&fIrqEntries,__ATOMIC_ACQUIRE));
  if (state!=IrqFailed && state!=IrqStopped) publishVsync(SweetVsyncOff);
 }
}
void
SweetMDSS::failIRQ(const char *reason)
{
 __atomic_store_n(&fIrqState,IrqFailed,__ATOMIC_RELEASE);
 pauseIRQ();
 if (fIrqTimer) fIrqTimer->cancelTimeout();
 setProperty("SweetDisplayInterruptEnabled",false);
 setProperty("SweetDisplayIRQFailed",true);
 SWEET_LOG_ERROR("SWEET MDSS IRQ: FAILED %s frames=%llu entries=%u; explicit software-vsync fallback\n",
     reason,fFrameCount,__atomic_load_n(&fIrqEntries,__ATOMIC_ACQUIRE));
 publishVsync(SweetVsyncFailed);
}

bool
SweetMDSS::armIRQ(void)
{
 if (fIrqMode!=2 || fIrqPhase!=2 || !fIrqDisplayOn || !fAccepting || isInactive() ||
     __atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE)!=IrqIdle) return false;
 const vm_offset_t top=fMaps[9]->getVirtualAddress(),tear=fMaps[10]->getVirtualAddress();
 if (!dp_clocks_on() || fPanelState!=PanelOn || dp_rd(top,0x1010) || dp_rd(top,0x1008) ||
     dp_rd(top,0x101c) || (dp_rd(tear,0)&~MDSS_RDPTR_BIT) || dp_rd(tear,0x100) ||
     dp_rd(dp_intf1,0x1c0) || dp_rd(dp_intf2,0x1c0) || (dp_rd(top,0x10)&~1u)) {
  failIRQ("clock/panel/non-target source at arm"); return false;
 }
 uint64_t now=mdss_irq_ns(mach_absolute_time());
 fEpochFrames=0; fLastFrameNs=now; fLastTopNs=0;
 fStormStartNs=now; fStormFrames=0; fWindowStartNs=now; fWindowFrames=0; fIntervalCount=0; fIntervalDropped=0;
 dp_wr(tear,8,MDSS_RDPTR_BIT); /* drop only inherited RD_PTR, not all raw 0xf */
 fSourceOwned=true;
 dp_wr(tear,0,MDSS_RDPTR_BIT);
 if (dp_rd(tear,0)!=MDSS_RDPTR_BIT) { failIRQ("RD_PTR enable readback"); return false; }
 __atomic_store_n(&fIrqState,IrqArmed,__ATOMIC_RELEASE);
 dp_wr(fMaps[11]->getVirtualAddress(),0x10c,MDSS_IRQ_BIT);
 if (!waitIRQ()) { failIRQ("GIC rearm RWP"); return false; }
 setProperty("SweetDisplayInterruptEnabled",true);
 SWEET_LOG_INFO("SWEET MDSS IRQ: RD_PTR armed frames=%llu entries=%u\n",fFrameCount,__atomic_load_n(&fIrqEntries,__ATOMIC_ACQUIRE));
 publishVsync(SweetVsyncWaiting);
 fIrqTimer->setTimeoutMS(100);
 return true;
}

void
SweetMDSS::irqAction(OSObject *owner,IOInterruptEventSource *,int)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 if (!self->fAccepting || self->isInactive()) return;
 if (__atomic_load_n(&self->fFallbackRequested,__ATOMIC_ACQUIRE)) {
  if (__atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)!=IrqFailed) self->failIRQ("consumer delivery watchdog"); return;
 }
 if (__atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)!=IrqPending) return;
 if (!self->fIrqDisplayOn) { self->pauseIRQ(); return; }
 if (!self->dp_clocks_on()) { self->failIRQ("IRQ clocks unavailable"); return; }
 const vm_offset_t top=self->fMaps[9]->getVirtualAddress(),tear=self->fMaps[10]->getVirtualAddress();
 uint32_t sources=dp_rd(top,0x10),status=dp_rd(tear,4),enabled=dp_rd(tear,0);
 if ((sources&~1u) || !(status&MDSS_RDPTR_BIT) || enabled!=MDSS_RDPTR_BIT ||
     dp_rd(top,0x1010) || dp_rd(top,0x1008) || dp_rd(top,0x101c) || dp_rd(tear,0x100) ||
     dp_rd(self->dp_intf1,0x1c0) || dp_rd(self->dp_intf2,0x1c0)) {
  self->failIRQ("unexpected/empty enabled source"); return;
 }
 /* No status-still-set fault test: a fresh frame may arrive after this W1C. */
 dp_wr(tear,8,MDSS_RDPTR_BIT);
 uint64_t ticks=__atomic_load_n(&self->fIrqTicks,__ATOMIC_ACQUIRE),ns=mdss_irq_ns(ticks);
 if (ns-self->fStormStartNs>=1000000000ULL) { self->fStormStartNs=ns; self->fStormFrames=0; }
 if (++self->fStormFrames>200) { self->failIRQ("RD_PTR storm >200/s"); return; }
 if (self->fLastTopNs) {
  uint64_t us=(ns-self->fLastTopNs)/1000ULL;
  if (self->fIntervalCount<4096) self->fIntervals[self->fIntervalCount++]=(uint32_t)(us>0xffffffffULL ? 0xffffffffu : us);
  else ++self->fIntervalDropped;
 }
 self->fLastTopNs=ns; self->fLastFrameNs=ns; self->fFrameTicks=ticks;
 ++self->fFrameCount; ++self->fEpochFrames; ++self->fWindowFrames;
 __atomic_store_n(&self->fIrqState,IrqArmed,__ATOMIC_RELEASE);
 dp_wr(self->fMaps[11]->getVirtualAddress(),0x10c,MDSS_IRQ_BIT);
 self->publishVsync(SweetVsyncFrame);
}
/* In-place heapsort: bounded O(n log n), no extra allocation or libc ABI. */
static void
mdss_interval_sift(uint32_t *values,unsigned root,unsigned count)
{
 while (root<count/2) {
  unsigned child=2*root+1;
  if (child+1<count && values[child]<values[child+1]) ++child;
  if (values[root]>=values[child]) break;
  uint32_t old=values[root]; values[root]=values[child]; values[child]=old;
  root=child;
 }
}
static void
mdss_interval_sort(uint32_t *values,unsigned count)
{
 for (unsigned start=count/2;start;--start) mdss_interval_sift(values,start-1,count);
 for (unsigned end=count;end>1;--end) {
  uint32_t old=values[0]; values[0]=values[end-1]; values[end-1]=old;
  mdss_interval_sift(values,0,end-1);
 }
}
void
SweetMDSS::reportIRQ(bool partial)
{
 uint64_t now=mdss_irq_ns(mach_absolute_time()),elapsed=now-fWindowStartNs;
 if (!elapsed) return;
 uint32_t p50=0,p99=0;
 if (fIntervalCount) {
  mdss_interval_sort(fIntervals,fIntervalCount);
  p50=fIntervals[(fIntervalCount-1)/2]; p99=fIntervals[((uint64_t)fIntervalCount*99+99)/100-1];
 }
 SWEET_LOG_INFO("SWEET MDSS IRQ: stats partial=%u window-ms=%llu frames=%u milli-hz=%llu p50-us=%u p99-us=%u samples=%u dropped=%u total=%llu entries=%u\n",
     partial,elapsed/1000000ULL,fWindowFrames,(uint64_t)fWindowFrames*1000000000000ULL/elapsed,
     p50,p99,fIntervalCount,fIntervalDropped,fFrameCount,__atomic_load_n(&fIrqEntries,__ATOMIC_ACQUIRE));
 setProperty("SweetDisplayIRQFrames",(unsigned long long)fFrameCount,64);
 fWindowStartNs=now; fWindowFrames=0; fIntervalCount=0; fIntervalDropped=0;
}
void
SweetMDSS::irqTimerAction(OSObject *owner,IOTimerEventSource *sender)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 if (!self->fAccepting || self->isInactive() || __atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)==IrqFailed || __atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)==IrqStopped) return;
 if (self->fIrqPhase==0) {
  if (!self->configureIRQ()) self->failIRQ("GIC/source configuration refused");
  return;
 }
 if (self->fIrqPhase==1) {
  const vm_offset_t gic=self->fMaps[11]->getVirtualAddress();
  dp_wr(gic,0x18c,MDSS_IRQ_BIT); dp_wr(gic,0x28c,MDSS_IRQ_BIT);
  unsigned hits=__atomic_load_n(&self->fProbeHits,__ATOMIC_ACQUIRE);
  uint32_t pending=dp_rd(gic,0x20c)&MDSS_IRQ_BIT,active=dp_rd(gic,0x30c)&MDSS_IRQ_BIT;
  if (!self->waitIRQ() || hits!=1 || pending || active) { self->failIRQ("GIC software-pending delivery"); return; }
  self->fIrqPhase=2; __atomic_store_n(&self->fIrqState,IrqIdle,__ATOMIC_RELEASE);
  SWEET_LOG_INFO("SWEET MDSS IRQ: GIC probe passed hits=%u mode=%u; peripheral untouched\n",hits,self->fIrqMode);
  if (self->fIrqMode==2) {
   if (self->fIrqDisplayOn) (void)self->armIRQ();
   else self->publishVsync(SweetVsyncOff);
  }
  return;
 }
 if (!self->fIrqDisplayOn || self->fIrqMode!=2 ||
     __atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)==IrqIdle) return;
 uint64_t now=mdss_irq_ns(mach_absolute_time());
 if (now-self->fLastFrameNs>250000000ULL) { self->failIRQ("RD_PTR watchdog >250ms"); return; }
 if (now-self->fWindowStartNs>=60000000000ULL) self->reportIRQ(false);
 sender->setTimeoutMS(100);
}

IOReturn
SweetMDSS::vsyncBindingAction(OSObject *owner,void *arg0,void *arg1,void *,void *)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 SweetVsyncChannel *channel=(SweetVsyncChannel *)arg0;
 if (arg1) { /* detach is legal after admission closes */
  if (self->fVsyncChannel==channel) OSSafeReleaseNULL(self->fVsyncChannel);
  return kIOReturnSuccess;
 }
 if (!channel) return kIOReturnBadArgument;
 if (!self->fAccepting || self->isInactive() || self->fIrqMode!=2) return kIOReturnNotReady;
 if (self->fVsyncChannel && self->fVsyncChannel!=channel) return kIOReturnBusy;
 if (!self->fVsyncChannel) { channel->retain(); self->fVsyncChannel=channel; }
 unsigned state=__atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE);
 self->publishVsync(state==IrqFailed ? SweetVsyncFailed : !self->fIrqDisplayOn ? SweetVsyncOff :
     self->fEpochFrames && self->fIrqPhase==2 ? SweetVsyncFrame : SweetVsyncWaiting);
 return kIOReturnSuccess;
}
IOReturn
SweetMDSS::attachVsync(SweetVsyncChannel *channel)
{
 return fGate ? fGate->runAction(vsyncBindingAction,channel) : kIOReturnNotReady;
}
void
SweetMDSS::detachVsync(SweetVsyncChannel *channel)
{
 if (fGate) (void)fGate->runAction(vsyncBindingAction,channel,(void *)1);
}
void
SweetMDSS::requestVsyncFallback(void)
{
 __atomic_store_n(&fFallbackRequested,1u,__ATOMIC_RELEASE);
 /* Retained caller and provider free contract keep the event object alive. */
 if (fIrqEvent) fIrqEvent->interruptOccurred(NULL,NULL,0);
}
void
SweetMDSS::stopIRQ(void)
{
 if (!fIrqMode) return;
 __atomic_store_n(&fIrqState,IrqStopped,__ATOMIC_RELEASE);
 pauseIRQ();
 if (fIrqTimer) fIrqTimer->cancelTimeout();
 if (fIrqEvent) fIrqEvent->disable();
 publishVsync(SweetVsyncStopped); OSSafeReleaseNULL(fVsyncChannel);
 if (fGicTouched) {
  const vm_offset_t gic=fMaps[11]->getVirtualAddress();
  dp_wr(gic,0x18c,MDSS_IRQ_BIT);
  if (waitIRQ()) {
   dp_wr(gic,0x28c,MDSS_IRQ_BIT);
   dp_wr(gic,0x08c,(dp_rd(gic,0x08c)&~MDSS_IRQ_BIT)|fOldGroup);
   dp_wr(gic,0xc1c,(dp_rd(gic,0xc1c)&~0xc0u)|fOldCfg);
   dp_wr(gic,0x470,(dp_rd(gic,0x470)&0x00ffffffu)|fOldPriority);
   dp_wr64(gic,0x6398,fOldRoute);
  } else SWEET_LOG_ERROR("SWEET MDSS IRQ: stop RWP timeout; parent remains masked\n");
  fGicTouched=false;
 }
 if (fIrqClaimed) {
  IOSimpleLock *lock=__atomic_load_n(&mdssIRQDispatch,__ATOMIC_ACQUIRE);
  IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
  if (mdssIRQService==this) mdssIRQService=NULL;
  IOSimpleLockUnlockEnableInterrupt(lock,saved);
  fIrqClaimed=false;
 }
 setProperty("SweetDisplayInterruptEnabled",false);
}
