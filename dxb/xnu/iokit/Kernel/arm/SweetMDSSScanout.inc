/* F4 prerequisite. Register meanings checked against LG's DRM/SDE drivers,
 * not its unused fbdev stack. SSPP +0x70 is SECURITY, never an active address.
 * Every access is a read under SweetMDSS's existing workloop gate. */
IOReturn
SweetMDSS::inspectScanout(SweetScanoutSnapshot *snapshot)
{
 if (!snapshot) return kIOReturnBadArgument;
 bzero(snapshot,sizeof(*snapshot));
 return fGate ? fGate->runAction(scanoutInspectAction,snapshot) : kIOReturnNotReady;
}
IOReturn
SweetMDSS::scanoutInspectAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 SweetScanoutSnapshot *v=(SweetScanoutSnapshot *)arg;
 if (!v) return kIOReturnBadArgument;
 if (!s->fAccepting || s->isInactive() ||
     (s->fLinkPhase!=LinkIdle && s->fLinkPhase!=LinkReady && !s->lifecycleLinkAccessible()) ||
     (s->fHostPhase!=HostIdle && s->fHostPhase!=HostReady && !s->lifecycleLinkAccessible()) || (s->fCorePhase!=CoreIdle && !s->coreReady()) || !s->dp_clocks_on() ||
     !s->dp_smmu || !s->dp_smmu_cb2 || !s->fMaps[9] || !s->fMaps[10]) return kIOReturnNotReady;
 /* Sequential observations, NOT an atomic hardware snapshot. The top half
  * may update its timestamp independently; use the last processed frame. */
 v->frame=s->fFrameCount; v->ticks=s->fFrameTicks;
 vm_offset_t p=s->dp_sspp,c=s->dp_ctl,i=s->dp_intf1;
 v->size=dp_rd(p,0); v->xy=dp_rd(p,8); v->out=dp_rd(p,0xc); v->outXY=dp_rd(p,0x10);
 v->address=dp_rd(p,0x14); v->stride=dp_rd(p,0x24); v->format=dp_rd(p,0x30);
 v->unpack=dp_rd(p,0x34); v->op=dp_rd(p,0x38); v->secure=dp_rd(p,0x70);
 v->flush=dp_rd(c,0x18); v->start=dp_rd(c,0x1c); v->ctlTop=dp_rd(c,0x14);
 v->intfCfg=dp_rd(i,4); v->intfStatus=dp_rd(i,0x26c); v->te=dp_rd(i,0x284);
 v->autoRefresh=dp_rd(i,0x2b4); v->wrCount=dp_rd(i,0x290);
 v->line=dp_rd(i,0x2b0); v->outLine=dp_rd(i,0x2ac);
 v->mdpEnable=dp_rd(s->fMaps[9]->getVirtualAddress(),0x1010);
 v->mdpStatus=dp_rd(s->fMaps[9]->getVirtualAddress(),0x1014);
 v->tearEnable=dp_rd(s->fMaps[10]->getVirtualAddress(),0);
 v->tearStatus=dp_rd(s->fMaps[10]->getVirtualAddress(),4);
 p=s->dp_smmu;
 v->smmuID0=dp_rd(p,0x20); v->smmuID1=dp_rd(p,0x24);
 v->knownSmmu=v->smmuID0==0x4c017e5eu && v->smmuID1==0x60000038u;
 if (!v->knownSmmu) return kIOReturnSuccess; /* no guessed layout reads */
 v->smmuCR0=dp_rd(p,0); v->smmuFault=dp_rd(p,0x48);
 const unsigned count=v->smmuID0&0xffu;
 for (unsigned n=0;n<count;++n) {
  uint32_t smr=dp_rd(p,0x800u+4u*n),mask=(smr>>16)&0x7fffu;
  if (!(smr&0x80000000u) || ((smr&0x7fffu&~mask)!=(SMMU_SID&~mask))) continue;
  ++v->matches;
  if (v->matches==1) { v->streamIndex=n; v->smr=smr; v->s2cr=dp_rd(p,0xc00u+4u*n); }
 }
 /* Only CB2 is mapped/validated in the ADT. A changed or ambiguous route
  * is reported as such, never redirected or followed through a raw mapping. */
 if (v->matches!=1 || ((v->s2cr>>16)&3u)!=0 || (v->s2cr&0xffu)!=2) return kIOReturnSuccess;
 v->cb2Observed=true;
 v->cbar=dp_rd(s->dp_smmu_gr1,8); v->cba2r=dp_rd(s->dp_smmu_gr1,0x808);
 p=s->dp_smmu_cb2;
 v->sctlr=dp_rd(p,0); v->ttbcr=dp_rd(p,0x30);
 v->ttbrLo=dp_rd(p,0x20); v->ttbrHi=dp_rd(p,0x24);
 v->mairLo=dp_rd(p,0x38); v->mairHi=dp_rd(p,0x3c);
 v->cbFault=dp_rd(p,0x58); v->farLo=dp_rd(p,0x60); v->farHi=dp_rd(p,0x64);
 return kIOReturnSuccess;
}

/* Stage2 is one controlled transfer, not a per-frame fence implementation.
 * LG prepare_commit: disconnect external TE, disable autorefresh, wait until
 * INTF_TEAR_LINE_COUNT is outside (0,vdisplay), then program the source.
 * Polls are timer actions: never spin/sleep waiting under either service gate.
 */
#define SWEET_SMMU_KNOWN_FSR 0xc00001feu
static uint64_t
scanout_now(void)
{
 uint64_t ns; absolutetime_to_nanoseconds(mach_absolute_time(),&ns); return ns;
}
bool
SweetMDSS::scanoutDomain(SweetScanoutSnapshot *snapshot)
{
 SweetScanoutSnapshot local={};SweetScanoutSnapshot &v=snapshot ? *snapshot : local;
 if (scanoutInspectAction(this,&v,NULL,NULL,NULL)!=kIOReturnSuccess) return false;
 if (!v.knownSmmu || v.matches!=1 || !v.cb2Observed || v.smmuFault ||
     v.smmuCR0!=0x2b0406u || v.smr!=0x84200800u || v.s2cr!=2u || v.streamIndex!=3 ||
     (v.cbFault&SWEET_SMMU_KNOWN_FSR) || (v.cbFault&~0x400u) ||
     v.sctlr!=0xe0u || v.cbar!=0x1f000u || v.cba2r!=1u ||
     v.ttbcr || v.ttbrLo || v.ttbrHi || v.mairLo || v.mairHi) return false;
 fScanoutStatus.fsr=v.cbFault;
 return true;
}
bool
SweetMDSS::scanoutConfiguration(bool inherited)
{
 SweetScanoutSnapshot v={};
 if (!scanoutDomain(&v)) return false;
 if (fPipelineEnabled && !inherited) {
  /* Domain/ownership remains mandatory while configuration is lost or parked.
   * Pipeline generation + fresh frame fences govern the rebuilt submission. */
  return fHardwarePool && fScanoutTouched && fScanoutStatus.hardwareReachable;
 }
 if (v.secure || v.size!=0x09240438u || v.out!=0x09240438u || v.outXY ||
     v.format!=0x237ffu || v.unpack!=0x03020001u || v.op!=0x80000000u ||
     v.ctlTop!=0x20000u || v.start || (v.te!=1u && (inherited || !fLifecycleLineage))) return false;
 fScanoutStatus.fsr=v.cbFault; /* raw bit10 is retained, never silently cleared */
 if (inherited) return v.address==0x9d200000u && v.stride==4320u && !v.xy && !v.flush;
 if (!fHardwarePool || !fScanoutTouched || !fScanoutStatus.hardwareReachable) return false;
 observeLineageValue("scanout-source",0x14,(uint32_t)fHardwarePool->physical(0),v.address,~0u,false);
 observeLineageValue("scanout-stride",0x24,4608,v.stride,~0u,false);
 observeLineageValue("scanout-crop",8,0x00300016u,v.xy,~0u,false);
 return true; /* SMMU/layout and real frame completion remain checked. */
}
IOReturn
SweetMDSS::beginScanout(SweetScanoutPool *pool,SweetVsyncChannel *channel)
{
 return fGate ? fGate->runAction(scanoutBeginAction,pool,channel) : kIOReturnNotReady;
}
IOReturn
SweetMDSS::scanoutBeginAction(OSObject *owner,void *arg0,void *arg1,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 SweetScanoutPool *pool=(SweetScanoutPool *)arg0;
 if (s->fLifecycleEnabled && !s->fLifecycleInternal && s->lifecycleBlocked()) return kIOReturnBusy;
 int mode=0; (void)PE_parse_boot_argn("sweetfbown",&mode,sizeof(mode));
 if (mode!=2) return kIOReturnUnsupported;
 if (s->dsiBusy()) return kIOReturnBusy;
 if (s->fDsiPoisoned) return kIOReturnNotReady;
 if (s->fClockEnabled && !s->localClocksReady()) return kIOReturnNotReady;
 if (!s->fAccepting || s->isInactive() || !s->fIrqDisplayOn || s->fPanelState!=PanelOn ||
     s->fIrqMode!=2 || s->fIrqPhase!=2 || !s->fEpochFrames || !arg1 || arg1!=s->fVsyncChannel)
  return kIOReturnNotReady;
 unsigned irq=__atomic_load_n(&s->fIrqState,__ATOMIC_ACQUIRE);
 if (irq!=IrqArmed && irq!=IrqPending) return kIOReturnNotReady;
 if (s->fScanoutUsed) return kIOReturnBusy;
 if (!pool || pool->hardwarePinned() || pool->width()!=1125 || pool->height()!=2436 ||
     pool->stride()!=4608 || pool->bytes()!=11225088 || !pool->address(0)) return kIOReturnBadArgument;
 for (unsigned n=0;n<2;++n) {
  uint64_t pa=pool->physical(n),bytes=pool->bytes();
  if (!pa || (pa&4095u) || pa>=0x100000000ULL || bytes>0x100000000ULL-pa ||
      (pa<0x9e400000ULL && 0x9d200000ULL<pa+bytes)) return kIOReturnBadArgument;
 }
 if (!s->scanoutConfiguration(true) || dp_rd(s->dp_intf1,0x288)!=0x180088u ||
     dp_rd(s->dp_intf1,0x2b4)!=0x80000001u || dp_rd(s->fMaps[9]->getVirtualAddress(),0x1010))
  return kIOReturnNotReady;
 if (s->fManagedEnabled) {
  IOReturn rc=s->managedPreflight();
  if (rc!=kIOReturnSuccess) return rc; /* Before quiesce or any resource vote. */
 }
 if (s->fLifecycleEnabled && !s->fLifecycleInternal) {
  uint64_t token=0; return s->beginLifecycle(SweetPanelTakeover,pool,&token);
 }
 s->fScanoutTimer=IOTimerEventSource::timerEventSource(s,scanoutTimerAction);
 if (!s->fScanoutTimer) return kIOReturnNoMemory;
 if (s->fLoop->addEventSource(s->fScanoutTimer)!=kIOReturnSuccess) {
  OSSafeReleaseNULL(s->fScanoutTimer); return kIOReturnNoMemory;
 }
 s->fScanoutTimerAdded=true;
 pool->retain(); s->fHardwarePool=pool; s->fScanoutUsed=true;
 s->fScanoutState=SweetScanoutQuiescing;
 s->fScanoutTE=dp_rd(s->dp_intf1,0x288); s->fScanoutAuto=dp_rd(s->dp_intf1,0x2b4);
 s->fScanoutDeadline=scanout_now()+50000000ULL;
 dp_wr(s->dp_intf1,0x288,s->fScanoutTE&~(1u<<20));
 dp_wr(s->dp_intf1,0x2b4,0);
 s->setProperty("SweetScanoutHandoffState",(unsigned long long)s->fScanoutState,32);
 SWEET_LOG_INFO("SWEET MDSS SCANOUT: F4 v2 quiescing; TE disconnected/autorefresh disabled; raw-fsr=%#x timer-only wait\n",s->fScanoutStatus.fsr);
 s->fScanoutTimer->setTimeoutMS(1);
 return kIOReturnSuccess;
}
void
SweetMDSS::cancelScanout(const char *reason)
{
 if (!fScanoutUsed || fScanoutState==SweetScanoutFailed) return;
 if (fScanoutTimer) fScanoutTimer->cancelTimeout();
 if (fManagedStarted) failManagedResources(kIOReturnAborted,reason);
 /* Before any source register write the pool is CPU-only: restore inherited
  * TE/auto if accessible. After a write, never roll back/free on readback alone. */
 if (!fLifecycleEnabled && !fScanoutTouched && !fManagedStarted && dp_clocks_on()) {
  dp_wr(dp_intf1,0x2b4,fScanoutAuto);
  dp_wr(dp_intf1,0x288,fScanoutTE);
 }
 fScanoutState=SweetScanoutFailed;
 fScanoutStatus.state=fScanoutState;
 fScanoutStatus.hardwareReachable=fScanoutTouched;
 setProperty("SweetScanoutHandoffState",(unsigned long long)fScanoutState,32);
 setProperty("SweetScanoutHardwarePinned",fScanoutTouched);
 SWEET_LOG_ERROR("SWEET MDSS SCANOUT: FAILED %s touched=%u; %s\n",reason,fScanoutTouched,
     fScanoutTouched ? "hardware pin retained until reboot; no inferred DMA release" : "no new DMA address submitted");
}
void
SweetMDSS::scanoutTimerAction(OSObject *owner,IOTimerEventSource *sender)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (s->fLifecycleEnabled && s->lifecycleBusy() && !s->lifecycleParticipants()) {
  s->finishLifecycle(kIOReturnNotReady,"scanout participant lost"); return;
 }
 if (s->fScanoutState==SweetScanoutResources) {
  if (s->fLifecycleEnabled && !s->fManagedStarted) {
   if (scanout_now()>=s->fScanoutDeadline) { s->finishLifecycle(kIOReturnTimeout,"takeover IRQ drain"); return; }
   if (!s->lifecycleIRQDrained() || (dp_rd(s->dp_dsi,8)&6u)) { sender->setTimeoutMS(1); return; }
   s->fLifecycle.displayDrained=true; s->beginManagedResources();
  }
  if (!s->fAccepting || s->isInactive() || !s->fIrqDisplayOn || s->fPanelState!=PanelOn) {
   s->cancelScanout("resource takeover admission lost"); return;
  }
  s->advanceManagedResources();
  if (s->fManagedState==ManagedFault) { s->cancelScanout("managed resource transaction failed"); return; }
  if (s->fManagedState!=ManagedReady) { sender->setTimeoutMS(1); return; }
  if (!s->startMDPClient()) {s->cancelScanout("initial MDP scan resource admission");return;}
  if (s->fPipelineEnabled) {
   IOReturn rc=s->beginPipeline();
   if (rc!=kIOReturnSuccess) { s->cancelScanout("pipeline takeover configuration admission");return; }
   s->fScanoutState=SweetScanoutRebuilding;s->fScanoutDeadline=scanout_now()+100000000ULL;
   sender->setTimeoutMS(1);return;
  }
  /* Re-check the retained pipeline before exposing the new source address. */
  if (!s->scanoutConfiguration(true) || !s->clockCanGate() || !s->localClocksReady()) {
   s->cancelScanout("resource-complete pipeline changed"); return;
  }
  if (!s->armIRQ()) { s->cancelScanout("resource-complete IRQ rearm failed"); return; }
  s->fScanoutState=SweetScanoutQuiescing;
  s->fScanoutIdleSamples=0; s->fScanoutDeadline=scanout_now()+50000000ULL;
  sender->setTimeoutMS(1); return;
 }
 if (s->fScanoutState==SweetScanoutRebuilding) {
  if (scanout_now()>=s->fScanoutDeadline) { s->cancelScanout("pipeline takeover deadline");return; }
  IOReturn rc=s->advancePipeline();
  if (rc==kIOReturnBusy) {sender->setTimeoutMS(1);return;}
  if (rc!=kIOReturnSuccess) {s->cancelScanout("pipeline takeover configuration failed");return;}
  const vm_offset_t top=s->fMaps[9]->getVirtualAddress();
  dp_wr(top,0x1018,1u<<8);
  if (dp_rd(top,0x1014)&(1u<<8)) {s->cancelScanout("pipeline stale PP done");return;}
  if (!s->armIRQ()) {s->cancelScanout("pipeline pixel IRQ rearm");return;}
  s->fScanoutTE=(1u<<20)|(1u<<19)|135u;s->fScanoutAuto=0x80000001u;
  s->fScanoutSubmitFrame=s->fFrameCount;s->fScanoutStatus.address=s->fHardwarePool->physical(0);
  s->kickoffPipeline();s->fScanoutState=SweetScanoutSubmitted;
  s->fScanoutDeadline=scanout_now()+100000000ULL;
  sender->setTimeoutMS(1);return;
 }
 if (s->fScanoutState!=SweetScanoutQuiescing && s->fScanoutState!=SweetScanoutSubmitted) return;
 unsigned irq=__atomic_load_n(&s->fIrqState,__ATOMIC_ACQUIRE);
 if (!s->fAccepting || s->isInactive() || !s->fIrqDisplayOn || s->fPanelState!=PanelOn ||
     (irq!=IrqArmed && irq!=IrqPending) || !s->dp_clocks_on()) {
  s->cancelScanout("handoff admission/power lost"); return;
 }
 uint64_t now=scanout_now();
 if (now>=s->fScanoutDeadline) { s->cancelScanout("bounded quiesce/transfer timeout"); return; }
 const vm_offset_t top=s->fMaps[9]->getVirtualAddress();
 if (s->fScanoutState==SweetScanoutQuiescing) {
  if (dp_rd(s->dp_intf1,0x2b4) || (dp_rd(s->dp_intf1,0x288)&(1u<<20)) ||
      !s->scanoutConfiguration(true)) { s->cancelScanout("quiesce configuration changed"); return; }
  uint32_t line=dp_rd(s->dp_intf1,0x2b0)&0xffffu;
  if (line>0 && line<2340) s->fScanoutIdleSamples=0;
  else ++s->fScanoutIdleSamples;
  if (s->fScanoutIdleSamples<2) { sender->setTimeoutMS(1); return; }
  if (s->fManagedEnabled && !s->fManagedStarted) {
   s->pauseIRQ(); /* publication only; no reverse consumer-gate callback */
   s->fScanoutState=SweetScanoutResources;
   if (!s->fLifecycleEnabled) s->beginManagedResources();
   else s->fScanoutDeadline=now+50000000ULL;
   sender->setTimeoutMS(1); return;
  }
  /* Separate hardware lifetime pin precedes the FIRST possibly effective
   * source write. Stop/timeout/free can never return this pool to the VM. */
  dp_wr(top,0x1018,1u<<8); /* PP0 DONE W1C only; no completion IRQ enabled */
  if (dp_rd(top,0x1014)&(1u<<8)) { s->cancelScanout("inherited PP done did not clear"); return; }
  s->fHardwarePool->pinHardware(); s->fScanoutTouched=true;
  s->fScanoutStatus.hardwareReachable=true;
  s->setProperty("SweetScanoutHardwarePinned",true);
  dp_wr(s->dp_sspp,0x8,0x00300016u);
  dp_wr(s->dp_sspp,0x24,4608);
  dp_wr(s->dp_sspp,0x14,(uint32_t)s->fHardwarePool->physical(0));
  s->fScanoutSubmitFrame=s->fFrameCount;
  dp_wr(s->dp_ctl,0x18,1u); /* VIG0 only; no CTL_START in autorefresh mode */
  s->fScanoutState=SweetScanoutSubmitted;
  s->fScanoutStatus.address=s->fHardwarePool->physical(0);
  if (!s->scanoutConfiguration(false)) { s->cancelScanout("source readback/SMMU validation"); return; }
  /* No old transfer was ongoing at the programming point. Restore inherited
   * auto/TE, then require a NEW PP done, cleared flush and advancing RD_PTR.
   * This is evidence for this one attempt, never a general buffer reuse fence. */
  dp_wr(s->dp_intf1,0x2b4,s->fScanoutAuto);
  dp_wr(s->dp_intf1,0x288,s->fScanoutTE);
  s->fScanoutDeadline=now+100000000ULL;
  s->setProperty("SweetScanoutHandoffState",(unsigned long long)s->fScanoutState,32);
  SWEET_LOG_INFO("SWEET MDSS SCANOUT: submitted A=%#llx stride=4608 crop=22,48 idle-line=%u frame=%llu; pool pinned; readback is not completion\n",
      s->fScanoutStatus.address,line,s->fScanoutSubmitFrame);
  sender->setTimeoutMS(1); return;
 }
 if (!s->scanoutConfiguration(false) || (s->fPipelineEnabled && (!s->fPipelineConfigured || !s->fLifecycle.pipelineGeneration))) { s->cancelScanout("submitted SMMU/layout changed"); return; }
 s->observeLineage("scanout-te",s->dp_intf1,0x288,s->fScanoutTE);
 s->observeLineage("scanout-auto",s->dp_intf1,0x2b4,s->fScanoutAuto);
 s->fScanoutStatus.flush=dp_rd(s->dp_ctl,0x18);
 s->fScanoutStatus.done=dp_rd(top,0x1014)&(1u<<8);
 s->fScanoutStatus.frame=s->fFrameCount;
 if (!s->fScanoutStatus.flush && s->fScanoutStatus.done && s->fFrameCount>s->fScanoutSubmitFrame) {
  s->fScanoutState=SweetScanoutActive;
  s->setProperty("SweetScanoutHandoffState",(unsigned long long)s->fScanoutState,32);
  SWEET_LOG_INFO("SWEET MDSS SCANOUT: transfer observed A=%#llx frame=%llu flush=%#x fresh-pp-done=%#x raw-fsr=%#x; requested source, echo diagnostic; lifetime pin remains\n",
      s->fScanoutStatus.address,s->fFrameCount,s->fScanoutStatus.flush,s->fScanoutStatus.done,s->fScanoutStatus.fsr);
  return;
 }
 sender->setTimeoutMS(1);
}
IOReturn
SweetMDSS::scanoutStatus(SweetScanoutPool *pool,SweetScanoutStatus *status)
{
 return fGate ? fGate->runAction(scanoutStatusAction,pool,status) : kIOReturnNotReady;
}
IOReturn
SweetMDSS::scanoutStatusAction(OSObject *owner,void *arg0,void *arg1,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!arg0 || (arg0!=s->fHardwarePool && arg0!=s->fLifecyclePool) || !arg1) return kIOReturnBadArgument;
 if (s->fLifecycleEnabled && s->fLifecyclePool && s->fLifecycle.phase==SweetPanelTouchPause) {
  *(SweetScanoutStatus *)arg1={}; ((SweetScanoutStatus *)arg1)->state=SweetScanoutCoordinating;
  return kIOReturnSuccess;
 }
 unsigned irq=__atomic_load_n(&s->fIrqState,__ATOMIC_ACQUIRE);
 if (!s->displayClientsValid()) {
  s->finishLifecycle(kIOReturnNotReady,"display client lease lost");
  s->cancelScanout("display client lease lost");
 }
 if (s->fManagedEnabled && s->fManagedState==ManagedFault)
  s->cancelScanout("managed resources unavailable");
 if (s->fScanoutState==SweetScanoutActive && (irq==IrqFailed || irq==IrqStopped))
  s->cancelScanout("owned scanout IRQ unavailable"); /* touched=true: no MMIO */
 s->fScanoutStatus.state=s->fScanoutState;
 *(SweetScanoutStatus *)arg1=s->fScanoutStatus;
 if (s->fLifecycleEnabled && s->fLifecycle.operation==SweetPanelTakeover && s->lifecycleBusy())
  ((SweetScanoutStatus *)arg1)->state=SweetScanoutCoordinating;
 if (s->fLifecycleEnabled && (s->fLifecycle.phase==SweetPanelFault || s->fLifecycle.phase==SweetPanelStopped))
  ((SweetScanoutStatus *)arg1)->state=SweetScanoutFailed;
 return kIOReturnSuccess; /* legal after stop: pure retained state, no MMIO */
}
void
SweetMDSS::abandonScanout(SweetScanoutPool *pool)
{
 if (fGate) (void)fGate->runAction(scanoutAbandonAction,pool);
}
IOReturn
SweetMDSS::scanoutAbandonAction(OSObject *owner,void *arg0,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (s->fLifecycleEnabled && arg0 && (arg0==s->fLifecyclePool || arg0==s->fHardwarePool))
  s->finishLifecycle(kIOReturnAborted,"scanout consumer detached");
 if (arg0 && arg0==s->fHardwarePool) s->cancelScanout("consumer detached");
 return kIOReturnSuccess;
}
