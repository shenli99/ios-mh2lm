/* Local branch controller under SweetMDSS's sole MMIO ownership/workloop.
 * Register facts: LG dispcc-sm8150.c, gcc-sm8150.c, clk-branch.c,
 * clk-rcg2.c and gdsc-regulator.c. Original implementation, no Linux API shim.
 * Never use PWR_ON or a running AXI clock as proof of an owned MMCX/NoC vote.
 */
static unsigned mdssClockWriter; /* 0 free, 1 owned, 2 retired; NOT an instance pointer */
struct SweetClockBranch { uint32_t cbcr, root; unsigned sources; bool link, voted, managedAHB; };
static const SweetClockBranch mdssClockBranches[SweetClockCount]={
 {0x2080,0x22bc,(1u<<0)|(1u<<4)|(1u<<5),false,false,false}, /* AHB */
 {0x200c,0x20c8,(1u<<0)|(1u<<1)|(1u<<4)|(1u<<5),false,false,false}, /* MDP */
 {0x2024,0x20f8,1u<<0,false,false,false}, /* VSYNC: 19.2MHz XO */
 {0x2038,0x2148,1u<<0,false,false,false}, /* ESC0: 19.2MHz XO */
 {0x2028,0x2110,1u<<1,true,false,false}, /* BYTE0: already-running PHY0 only */
 {0x202c,0x2110,1u<<1,true,false,false}, /* BYTE0_INTF: same root + divider */
 {0x2004,0x2098,1u<<1,true,false,false}, /* PCLK0: already-running PHY0 only */
 {0x4004,0x22bc,(1u<<0)|(1u<<4)|(1u<<5),false,true,true}, /* NON_GDSC_AHB: BRANCH_VOTED */
 {0x400c,0x22bc,(1u<<0)|(1u<<4)|(1u<<5),false,false,true}, /* RSCC_AHB: BRANCH_HALT */
 {0x4008,0x20f8,1u<<0,false,false,false}, /* RSCC_VSYNC: BRANCH_HALT */
};
static const unsigned mdssClockMasks[SweetClockUseCount]={0x07u,0x7fu,0x381u};

void SweetMDSS::observeClockParent(unsigned branch,const uint32_t *got,const uint32_t *wanted)
{
 for (unsigned n=0;n<6;++n) if (got[n]!=wanted[n] && fLineageObservations<1024u) {
  ++fLineageObservations;
  SWEET_LOG_INFO("SWEET MDSS CONFIG: stage=clock-parent branch=%u field=%u want=%#x got=%#x; diagnostic only\n",
      branch,n,wanted[n],got[n]);
 }
}
bool SweetMDSS::clockBranchRequested(unsigned i,uint32_t value) const
{
 /* An inherited enable still needs readback. For our own request, record
  * submission after dp_wr's barrier, separately from the pre-write failure
  * ownership ledger. This is software request state, not a hardware ACK. */
 return (value&1u) || (fClockOwnedMask&fClockIssuedMask&(1u<<i));
}
bool SweetMDSS::clockBranchRunning(unsigned i,uint32_t value) const
{
 /* LG branch2 enable completion checks CLK_OFF, not enable readback. The
  * caller must also validate the parent and its Pending/Held lease. Our
  * issued requests may run without bit0 readback. */
 return clockBranchRequested(i,value) && !(value&CBCR_OFF);
}
bool SweetMDSS::clockBusy(void) const
{
 return fClockOperation && (fClockLeases[fClockOperation-1].state==SweetClockPending ||
     fClockLeases[fClockOperation-1].state==SweetClockReleasing);
}
bool SweetMDSS::clockPrerequisites(ClockDiagnostics diagnose)
{
 /* Admission logs warm boot details; Failure logs only a failed gate for NoC.
  * Both use the original sample/check order, never resample on failure. */
 if (linkBusy() || fLinkPhase==LinkFault || fLinkPhase==LinkStopped) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=link-state phase=%u\n",(unsigned)fLinkPhase);
  return false;
 }
 if (coreBusy() || fCorePhase==CoreFault || fCorePhase==CoreStopped) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=core-state phase=%u\n",(unsigned)fCorePhase);
  return false;
 }
 if (fCorePhase==CoreReady && !coreReady()) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=core-ready\n");
  return false;
 }
 /* Same short-circuit ordering as dp_clocks_on(): no DISPCC read without the
  * GCC AHB software vote. GCC AHB is critical/HWCG; its OFF bit is not a gate. */
 uint32_t ahb=dp_rd(dp_gcc,GCC_DISP_AHB);
 if (!(ahb&CBCR_ENABLE)) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=gcc-ahb cbcr=%#x\n",ahb);
  return false;
 }
 uint32_t mahb=dp_rd(dp_dispcc,DISPCC_AHB_CBCR);
 if (mahb&CBCR_OFF) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=dispcc-ahb gcc=%#x cbcr=%#x\n",ahb,mahb);
  return false;
 }
 uint32_t hf=dp_rd(dp_gcc,0x38);
 if (!(hf&1u) || ((hf&0xf0000000u)!=0x20000000u && (hf&CBCR_OFF))) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=gcc-hf gcc-ahb=%#x dispcc-ahb=%#x hf=%#x\n",ahb,mahb,hf);
  return false;
 }
 uint32_t gdsc=dp_rd(dp_dispcc,0x3000);
 /* A running domain does not grant permission to override another owner. */
 if ((gdsc&0x80000007u)!=0x80000000u) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=gdsc gcc-ahb=%#x dispcc-ahb=%#x hf=%#x gdsc=%#x\n",ahb,mahb,hf,gdsc);
  return false;
 }
 if (diagnose==ClockAdmission) SWEET_LOG_INFO("SWEET MDSS CLOCK: admit prerequisites gcc-ahb=%#x dispcc-ahb=%#x hf=%#x gdsc=%#x\n",ahb,mahb,hf,gdsc);
 return true;
}
bool SweetMDSS::clockParent(unsigned i,uint32_t out[6],bool requireOn,ClockDiagnostics diagnose)
{
 const SweetClockBranch &b=mdssClockBranches[i];
 uint32_t cmd=dp_rd(dp_dispcc,b.root);
 if ((cmd&0xf1u) || (requireOn && (cmd&0x80000000u))) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=root-state branch=%u root=%#x cmd=%#x require-on=%u\n",i,b.root,cmd,(unsigned)requireOn);
  return false;
 }
 out[0]=dp_rd(dp_dispcc,b.root+4);
 unsigned source=(out[0]>>8)&7u;
 if (!(b.sources&(1u<<source))) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=root-source branch=%u root=%#x cmd=%#x cfg=%#x source=%u allowed=%#x\n",i,b.root,cmd,out[0],source,b.sources);
  return false;
 }
 out[1]=b.link ? dp_rd(dp_dispcc,b.root+8) : 0;
 out[2]=b.link ? dp_rd(dp_dispcc,b.root+12) : 0;
 out[3]=b.link ? dp_rd(dp_dispcc,b.root+16) : 0;
 out[4]=i==5 ? dp_rd(dp_dispcc,0x2128) : 0;
 out[5]=source;
 /* LG clk_rcg2_calc_rate(): HID=0 bypasses the divider, HID=1 gives
  * parent*2/(1+1). Both are exact XO rate in integer mode. Keep the raw
  * parent snapshot for diagnostics, including equivalent-rate changes. */
 uint32_t xoConfig=out[0]&0x371fu;
 if (!source && xoConfig>1u) {
  if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=xo-config branch=%u root=%#x cmd=%#x cfg=%#x\n",i,b.root,cmd,out[0]);
  return false;
 }
 if (diagnose==ClockAdmission) SWEET_LOG_INFO("SWEET MDSS CLOCK: admit parent branch=%u root=%#x cmd=%#x cfg=%#x m=%#x n=%#x d=%#x intf-div=%#x\n",i,b.root,cmd,out[0],out[1],out[2],out[3],out[4]);
 return true;
}
bool SweetMDSS::clockEnableParentReady(unsigned i,const uint32_t parent[6])
{
 if (mdssClockBranches[i].link) return false; /* No inherited PHY acquisition. */
 if (!parent[5]) return true; /* Existing exact-XO policy. */
 /* Only the two RSC AHB leaves may use the controlled PLL1 AHB parent.
  * A live inherited PLL or a requested corner is not an ownership proof. */
 if (!mdssClockBranches[i].managedAHB || parent[5]!=4 ||
     fCoreProfile!=2 || !fRPMh || !fRPMhConsumer || !fManagedARC || !fNoCRegisterToken || !coreReady()) return false;
 SweetDisplayPowerStatus power={}; SweetRegisterBandwidthStatus config={};
 return fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&power)==kIOReturnSuccess &&
     power.state==SweetRPMhComplete && power.result==kIOReturnSuccess && !power.uncertain &&
     power.enabled && power.corner==SweetDisplayTurbo && power.acknowledged==7u &&
     fRPMh->copyRegisterBandwidthStatus(fRPMhConsumer,fNoCRegisterToken,&config)==kIOReturnSuccess &&
     config.state==SweetRPMhComplete && config.result==kIOReturnSuccess && !config.uncertain &&
     config.bytesPerSecond>=300000000ULL;
}
bool SweetMDSS::clockCanGate(void)
{
 if (fPipelinePending || linkBusy() || hostBusy() || dsiBusy() || fDsiPoisoned || fDsiPinned ||
     fDsiTransferClock ||
     fScanoutState==SweetScanoutQuiescing || fScanoutState==SweetScanoutSubmitted ||
     (fIrqMode>=2 && __atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE)!=IrqIdle)) return false;
 /* No boolean supplied by a consumer can substitute for hardware idle.
  * Require TE/autorefresh disabled and no command/MDP engine in flight. */
 return !(dp_rd(dp_intf1,0x288)&(1u<<20)) && !dp_rd(dp_intf1,0x2b4) &&
     !(dp_rd(dp_dsi,8)&6u);
}
bool SweetMDSS::setupLocalClocks(void)
{
 int mode=0; (void)PE_parse_boot_argn("sweetdisplayclocks",&mode,sizeof(mode));
 if (!mode) {
  int core=0; (void)PE_parse_boot_argn("sweetdisplaycore",&core,sizeof(core));
  if (core) { SWEET_LOG_ERROR("SWEET MDSS CORE: requires clock1; refused\n"); return false; }
  return true;
 }
 if (mode!=1 || fDisplayResourceMode!=2) {
  SWEET_LOG_ERROR("SWEET MDSS CLOCK: warm mode1 requires display resource mode2; refused\n");
  return false;
 }
 unsigned expected=0;
 if (!__atomic_compare_exchange_n(&mdssClockWriter,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  SWEET_LOG_ERROR("SWEET MDSS CLOCK: writer owned/retired; refused\n"); return false;
 }
 int profile=0,managed=0;
 (void)PE_parse_boot_argn("sweetdisplaycore",&profile,sizeof(profile));
 (void)PE_parse_boot_argn("sweetdisplaymanaged",&managed,sizeof(managed));
 if (profile && (profile!=2 || managed!=1)) {
  __atomic_store_n(&mdssClockWriter,0u,__ATOMIC_RELEASE);
  SWEET_LOG_ERROR("SWEET MDSS CORE: explicit profile2 requires managed1; refused\n"); return false;
 }
 fCoreProfile=(unsigned)profile;
 setProperty("SweetDisplayCoreSelectedProfile",(unsigned long long)fCoreProfile,32);
 fClockClaimed=true; fClockEnabled=true;
 fClockTimer=IOTimerEventSource::timerEventSource(this,clockTimerAction);
 if (!fClockTimer || fLoop->addEventSource(fClockTimer)!=kIOReturnSuccess) return false;
 fClockTimerAdded=true;
 setProperty("SweetDisplayClockOwnership","MDSS: warm local leases; MMCX/NoC/parents inherited");
 /* A real internal lease for the inherited display path, not a probe-only
  * service. Defaults and resource mode1 never enter this path. */
 IOReturn rc=retainLocalClocks(SweetClockDSI0,&fClockBootToken);
 if (rc!=kIOReturnSuccess) {
  SWEET_LOG_ERROR("SWEET MDSS CLOCK: warm lease admission rc=%#x; no cold fallback\n",(unsigned)rc);
  return false;
 }
 return true;
}
IOReturn SweetMDSS::retainLocalClocks(SweetClockUse use,uint64_t *token)
{
 if (!token) return kIOReturnBadArgument;
 *token=0;
 return fGate ? fGate->runAction(clockRetainAction,&use,token) : kIOReturnNotReady;
}
IOReturn SweetMDSS::clockRetainAction(OSObject *owner,void *arg,void *output,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 unsigned use=*(SweetClockUse *)arg;
 const ClockDiagnostics diagnose=!s->fClockBootToken && !s->fClockNextToken ? ClockAdmission :
     (s->noCClockDiagnostics() ? ClockFailure : ClockSilent);
 if (diagnose==ClockAdmission) SWEET_LOG_INFO("SWEET MDSS CLOCK: warm admission use=%u accepting=%u inactive=%u enabled=%u fault=%u link=%u host=%u core=%u suspended=%#x\n",
     use,(unsigned)s->fAccepting,(unsigned)s->isInactive(),(unsigned)s->fClockEnabled,
     (unsigned)s->fClockFault,(unsigned)s->fLinkPhase,(unsigned)s->fHostPhase,
     (unsigned)s->fCorePhase,s->fLinkSuspendedMask);
 if (use>=SweetClockUseCount) return kIOReturnBadArgument;
 if (s->fLinkSuspendedMask) return kIOReturnNotReady;
 if (!s->fClockEnabled) return kIOReturnUnsupported;
 if (!s->fAccepting || s->isInactive() || s->fClockFault || s->fLinkPhase==LinkFault || s->fLinkPhase==LinkStopped ||
     (s->fHostTouched && (s->fHostPhase==HostFault || s->fHostPhase==HostStopped))) return kIOReturnNotReady;
 if (s->fLinkSuspendedMask) return kIOReturnNotReady;
 if (s->clockBusy() || s->coreBusy() || s->linkBusy() || s->hostBusy()) return kIOReturnBusy;
 unsigned slot=ClockLeaseSlots;
 for (unsigned n=0;n<ClockLeaseSlots;++n)
  if (!s->fClockLeases[n].token || s->fClockLeases[n].state==SweetClockReleased) { slot=n; break; }
 if (slot==ClockLeaseSlots || s->fClockNextToken==~(uint64_t)0) return kIOReturnNoResources;
 if (!s->clockPrerequisites(diagnose)) return kIOReturnNotReady;
 unsigned mask=mdssClockMasks[use]; uint32_t parents[SweetClockCount][6]={};
 uint32_t admittedRSC[3]={};
 for (unsigned i=0;i<SweetClockCount;++i) if (mask&(1u<<i)) {
  uint32_t v=dp_rd(s->dp_dispcc,mdssClockBranches[i].cbcr);
  if (i>=7) admittedRSC[i-7]=v; /* Existing preflight reads, not fresh failure samples. */
  if (diagnose==ClockAdmission) SWEET_LOG_INFO("SWEET MDSS CLOCK: admit branch=%u cbcr-offset=%#x cbcr=%#x\n",i,mdssClockBranches[i].cbcr,v);
  bool requested=s->clockBranchRequested(i,v);
  if (!s->clockParent(i,parents[i],requested,diagnose)) return kIOReturnNotReady;
  if (s->fClockKnownMask&(1u<<i)) s->observeClockParent(i,parents[i],s->fClockParents[i]);
  if (requested) {
   if (v&CBCR_OFF) {
    if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=branch-off branch=%u cbcr=%#x\n",i,v);
    return kIOReturnNotReady;
   }
  } else {
   if ((!mdssClockBranches[i].voted && !(v&CBCR_OFF)) || !s->clockEnableParentReady(i,parents[i])) {
    if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=branch-unowned branch=%u cbcr=%#x source=%u link=%u\n",i,v,parents[i][5],(unsigned)mdssClockBranches[i].link);
    return kIOReturnNotReady;
   }
   if (!s->clockCanGate()) {
    if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: admit gate=branch-busy branch=%u\n",i);
    return kIOReturnBusy;
   }
  }
 }
 /* Preflight is all-or-nothing: no first branch write before the last
  * resource/parent/idle check. Bookkeeping preserves inherited votes. */
 for (unsigned i=0;i<SweetClockCount;++i) if (mask&(1u<<i))
  memcpy(s->fClockParents[i],parents[i],sizeof(parents[i]));
 s->fClockKnownMask|=mask;
 SweetClockLease &lease=s->fClockLeases[slot]; lease={};s->fClockClient[slot]=0;
 lease.token=++s->fClockNextToken; lease.mask=mask; lease.state=SweetClockPending;
 lease.inheritedUpstream=true;
 s->fClockOperation=slot+1; s->fClockCursor=0; s->fClockReleaseMask=0;
 s->fClockReleasing=false; s->fClockWaiting=false; s->fClockVoteSettleAt=0;
 s->fClockTrace={};
 if (use==SweetClockRSC) s->fClockTrace.warmLocal=s->fClockWarmLocal;
 memcpy(s->fClockTrace.rscAdmission,admittedRSC,sizeof(admittedRSC));
 s->fClockTrace.rscAdmissionValid=use==SweetClockRSC;
 s->fClockDeadline=scanout_now()+200000000ULL;
 *(uint64_t *)output=lease.token; s->fClockTimer->setTimeoutUS(100);
 return kIOReturnSuccess;
}
IOReturn SweetMDSS::releaseLocalClocks(uint64_t token)
{
 return fGate ? fGate->runAction(clockReleaseAction,&token) : kIOReturnNotReady;
}
IOReturn SweetMDSS::clockReleaseAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner; uint64_t token=*(uint64_t *)arg;
 if (!s->fAccepting || s->isInactive() || s->fClockFault || s->fLinkPhase==LinkFault || s->fLinkPhase==LinkStopped ||
     (s->fHostTouched && (s->fHostPhase==HostFault || s->fHostPhase==HostStopped))) return kIOReturnNotReady;
 if (s->fLinkSuspendedMask) return kIOReturnNotReady;
 if (s->clockBusy() || s->coreBusy() || s->linkBusy() || s->hostBusy()) return kIOReturnBusy;
 unsigned slot=ClockLeaseSlots;
 for (unsigned n=0;n<ClockLeaseSlots;++n) if (token && s->fClockLeases[n].token==token) { slot=n; break; }
 if (slot==ClockLeaseSlots || s->fClockLeases[slot].state!=SweetClockHeld) return kIOReturnBadArgument;
 /* Only command completion may return its lease. Retain its prerequisites
  * even before CMD_DMA starts, when the hardware may still report idle. */
 if (s->fDsiTransferClock && (token==s->fDsiTransferClock ||
     token==s->fClockBootToken || token==s->fManagedClock)) return kIOReturnBusy;
 if (s->fClockClient[slot] ||
     ((s->fPanelDSIClock || s->fMdpClock) && token==s->fClockBootToken) ||
     (s->fMdpClock && token==s->fManagedClock)) return kIOReturnBusy;
 if (token==s->fNoCClockToken && !s->noCCanReleaseClock()) return kIOReturnBusy;
 if (!s->clockPrerequisites()) return kIOReturnNotReady;
 unsigned disable=0;
 for (unsigned i=0;i<SweetClockCount;++i)
  if ((s->fClockLeases[slot].mask&s->fClockOwnedMask&(1u<<i)) && s->fClockReferences[i]==1) disable|=1u<<i;
 /* The boot lease is also the admission proof for live DCS/scanout. Do
  * not revoke that proof under an in-flight transfer, even without writes. */
 if ((disable || token==s->fClockBootToken) && !s->clockCanGate()) return kIOReturnBusy;
 s->fClockLeases[slot].state=SweetClockReleasing;
 s->fClockReleaseMask=disable; s->fClockVoteSettleAt=0;
 s->fClockTrace={};
 s->fClockOperation=slot+1; s->fClockCursor=SweetClockCount-1; s->fClockReleasing=true; s->fClockWaiting=false;
 s->fClockDeadline=scanout_now()+200000000ULL;
 s->fClockTimer->setTimeoutUS(100); return kIOReturnSuccess;
}
IOReturn SweetMDSS::localClockStatus(uint64_t token,SweetClockLease *status)
{
 if (!status) return kIOReturnBadArgument;
 *status={};
 return fGate ? fGate->runAction(clockStatusAction,&token,status) : kIOReturnNotReady;
}
IOReturn SweetMDSS::clockStatusAction(OSObject *owner,void *arg,void *out,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner; uint64_t token=*(uint64_t *)arg;
 for (unsigned n=0;n<ClockLeaseSlots;++n) if (token && s->fClockLeases[n].token==token) {
  *(SweetClockLease *)out=s->fClockLeases[n];
  ((SweetClockLease *)out)->hardwareSuspended=(s->fClockLeases[n].mask&s->fLinkSuspendedMask)!=0;
  return kIOReturnSuccess;
 }
 return kIOReturnBadArgument; /* queries never touch MMIO */
}
void SweetMDSS::clockTimerAction(OSObject *owner,IOTimerEventSource *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (s->fLifecycleEnabled && s->lifecycleBusy() && !s->lifecycleResourceStepAllowed()) { s->finishLifecycle(kIOReturnNotReady,"resource participant lost");return; }
 if (!s->fAccepting || s->isInactive()) return;
 if (s->coreBusy()) s->advanceCoreResources();
 else if (s->clockBusy()) s->advanceLocalClocks();
}
void SweetMDSS::captureClockLocal(ClockLocalSnapshot &out)
{
 /* Callers hold the owner gate after warm/core/clock access admission.
  * LG RSCC_BCR +4000 and the three CBCRs are in the mapped DISPCC window;
  * this is not a read of the display RSC/TCS/wrapper behind its clock lease. */
 out={};
 out.bcr=dp_rd(dp_dispcc,0x4000);
 out.nonGDSC=dp_rd(dp_dispcc,0x4004);
 out.rsccAHB=dp_rd(dp_dispcc,0x400c);
 out.rsccVsync=dp_rd(dp_dispcc,0x4008);
 out.ahbCMD=dp_rd(dp_dispcc,0x22bc);
 out.ahbCFG=dp_rd(dp_dispcc,0x22c0);
 out.sampledNS=scanout_now(); out.valid=true;
}
void SweetMDSS::captureClockTimeoutReset(void)
{
 /* Both timeout callers just passed clockPrerequisites(). Limit this one
  * read to the actual branch7 retain write in the NoC clock phase. Overall
  * expiry still does not invent a new CBCR sample for CLOCK SAMPLE. */
 if (fClockReleasing || fClockCursor!=7 || !noCClockDiagnostics() || !fClockTrace.writeLocal.valid) return;
 fClockTrace.resetAtTimeout=dp_rd(dp_dispcc,0x4000);
 fClockTrace.resetSampleNS=scanout_now(); fClockTrace.resetValid=true;
}
void SweetMDSS::logClockLocal(const char *stage,const ClockLocalSnapshot &v)
{
 SWEET_LOG_ERROR("SWEET MDSS CLOCK LOCAL: stage=%s valid=%u bcr=%#x non-gdsc=%#x rscc-ahb=%#x rscc-vsync=%#x ahb-cmd=%#x ahb-cfg=%#x sampled-ns=%llu; ordered context reads, not atomic\n",
     stage,(unsigned)v.valid,v.bcr,v.nonGDSC,v.rsccAHB,v.rsccVsync,v.ahbCMD,v.ahbCFG,v.sampledNS);
}
void SweetMDSS::logClockTimeout(const char *reason,uint64_t now,bool currentSample)
{
 /* No MMIO, clock/parent helpers, provider calls or timer changes here.
  * An overall deadline can expire before any branch sample in this tick. */
 const ClockTrace &t=fClockTrace;
 const SweetClockLease &lease=fClockLeases[fClockOperation-1];
 uint32_t offset=fClockCursor<SweetClockCount ? mdssClockBranches[fClockCursor].cbcr : 0;
 SWEET_LOG_ERROR("SWEET MDSS CLOCK TIMEOUT: reason=%s token=%llu branch=%u offset=%#x release=%u waiting=%u now-ns=%llu request-deadline-ns=%llu wait-deadline-ns=%llu request-late-ns=%llu wait-late-ns=%llu\n",
     reason,lease.token,fClockCursor,offset,(unsigned)fClockReleasing,(unsigned)fClockWaiting,
     now,fClockDeadline,fClockWaiting ? fClockWaitDeadline : 0,
     now>=fClockDeadline ? now-fClockDeadline : 0,
     fClockWaiting && now>=fClockWaitDeadline ? now-fClockWaitDeadline : 0);
 SWEET_LOG_ERROR("SWEET MDSS CLOCK WRITE: valid=%u branch=%u before=%#x want=%#x entry-ns=%llu issued-ns=%llu\n",
     (unsigned)t.writeValid,t.writeBranch,t.writeBefore,t.writeWant,t.writeEntryNS,t.writeIssuedNS);
 SWEET_LOG_ERROR("SWEET MDSS CLOCK SAMPLE: valid=%u current=%u branch=%u cbcr=%#x enable=%u off=%u sample-ns=%llu age-ns=%llu parent-valid=%u cfg=%#x source=%u\n",
     (unsigned)t.sampleValid,(unsigned)(currentSample && t.sampleValid && t.sampleBranch==fClockCursor),
     t.sampleBranch,t.sampleCBCR,(t.sampleCBCR&1u)!=0,(t.sampleCBCR&CBCR_OFF)!=0,
     t.sampleNS,t.sampleValid && now>=t.sampleNS ? now-t.sampleNS : 0,
     (unsigned)t.parentValid,t.parentCFG,t.parentSource);
 SWEET_LOG_ERROR("SWEET MDSS CLOCK READBACK: valid=%u branch=%u pa=%#llx cbcr=%#x enable=%u off=%u sampled-ns=%llu\n",
     (unsigned)t.immediateValid,t.writeBranch,t.physicalAddress,t.immediateCBCR,
     (t.immediateCBCR&1u)!=0,(t.immediateCBCR&CBCR_OFF)!=0,t.immediateNS);
 SWEET_LOG_ERROR("SWEET MDSS CLOCK OBSERVED: branch=%u wait-samples=%u first=%#x first-ns=%llu seen-enable=%u seen-off=%u last-enabled-ns=%llu\n",
     t.writeBranch,t.waitSamples,t.firstWaitCBCR,t.firstWaitNS,
     (t.observedBits&1u)!=0,(t.observedBits&CBCR_OFF)!=0,t.lastEnabledNS);
 SWEET_LOG_ERROR("SWEET MDSS CLOCK RSC PREFLIGHT: valid=%u non-gdsc=%#x rscc-ahb=%#x rscc-vsync=%#x; admission samples only\n",
     (unsigned)t.rscAdmissionValid,t.rscAdmission[0],t.rscAdmission[1],t.rscAdmission[2]);
 logClockLocal("warm-entry",t.warmLocal);
 logClockLocal("pre-write",t.writeLocal);
 SWEET_LOG_ERROR("SWEET MDSS CLOCK RESET: valid=%u bcr=%#x sampled-ns=%llu; timeout reset sample only\n",
     (unsigned)t.resetValid,t.resetAtTimeout,t.resetSampleNS);
}
void SweetMDSS::advanceLocalClocks(void)
{
 const ClockDiagnostics diagnose=noCClockDiagnostics() ? ClockFailure : ClockSilent;
 if (!clockPrerequisites(diagnose)) { finishLocalClocks(kIOReturnNotReady); return; }
 uint64_t now=scanout_now();
 if (now>=fClockDeadline) {
  captureClockTimeoutReset();
  logClockTimeout("overall",now,false); finishLocalClocks(kIOReturnTimeout); return;
 }
 SweetClockLease &lease=fClockLeases[fClockOperation-1]; unsigned i=fClockCursor;
 unsigned bit=1u<<i; const SweetClockBranch &b=mdssClockBranches[i];
 if (lease.mask&bit) {
  uint32_t parent[6]={}, v=dp_rd(dp_dispcc,b.cbcr);
  fClockTrace.sampleValid=true; fClockTrace.sampleBranch=i; fClockTrace.sampleCBCR=v;
  fClockTrace.sampleNS=scanout_now(); fClockTrace.parentValid=false;
  fClockTrace.parentCFG=fClockTrace.parentSource=0;
  if (fClockWaiting && fClockTrace.writeValid && fClockTrace.writeBranch==i) {
   if (!fClockTrace.waitSamples) { fClockTrace.firstWaitCBCR=v; fClockTrace.firstWaitNS=fClockTrace.sampleNS; }
   if (fClockTrace.waitSamples!=~0u) ++fClockTrace.waitSamples;
   fClockTrace.observedBits|=v&(1u|CBCR_OFF);
   if (v&1u) fClockTrace.lastEnabledNS=fClockTrace.sampleNS;
  }
  bool rootNeeded=!fClockReleasing && clockBranchRunning(i,v);
  if (!clockParent(i,parent,rootNeeded,diagnose)) {
   if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: NoC gate=worker-parent branch=%u cbcr=%#x\n",i,v);
   finishLocalClocks(kIOReturnNotReady); return;
  }
  observeClockParent(i,parent,fClockParents[i]);
  fClockTrace.parentValid=true; fClockTrace.parentCFG=parent[0]; fClockTrace.parentSource=parent[5];
  if (!fClockReleasing && (fClockOwnedMask&bit) && !clockEnableParentReady(i,parent)) {
   if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: NoC gate=owned-parent branch=%u cbcr=%#x\n",i,v);
   finishLocalClocks(kIOReturnNotReady); return;
  }
  bool toggle=fClockReleasing ? ((fClockOwnedMask&bit) && fClockReferences[i]==1) : !clockBranchRequested(i,v);
  if (fClockWaiting) {
   observeLineageValue("clock-branch-request",b.cbcr,fClockReleasing ? 0u : 1u,v,1u,false);
   bool ready=fClockReleasing ? (v&CBCR_OFF) : clockBranchRunning(i,v);
   if (fClockReleasing && b.voted) {
    /* LG BRANCH_VOTED: release our SW vote, not the aggregate clock.
     * Retain the Lineage 10us settle; enable echo is diagnostic. */
    if (!fClockVoteSettleAt) fClockVoteSettleAt=now+10000ULL;
    observeLineageValue("clock-vote-release",b.cbcr,0,v,1u,false);
    ready=fClockVoteSettleAt && now>=fClockVoteSettleAt;
   }
   if (!ready) {
    if (now>=fClockWaitDeadline) {
     captureClockTimeoutReset();
     logClockTimeout("branch-wait",now,true); finishLocalClocks(kIOReturnTimeout); return;
    }
    fClockTimer->setTimeoutUS(100); return;
   }
   if (fClockReleasing) { fClockOwnedMask&=~bit; fClockIssuedMask&=~bit; }
   else if (b.voted) SWEET_LOG_INFO("SWEET MDSS CLOCK: voted halt complete branch=%u cbcr=%#x request-issued=%u; enable readback diagnostic only\n",
       i,v,(unsigned)((fClockIssuedMask&bit)!=0));
   fClockWaiting=false;
  } else if (toggle) {
   bool idle=clockCanGate();
   bool parentReady=fClockReleasing || (idle && clockEnableParentReady(i,parent));
   bool halted=fClockReleasing || b.voted || (v&CBCR_OFF);
   if (!idle || !parentReady || !halted) {
    if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: NoC gate=worker-enable branch=%u cbcr=%#x release=%u idle=%u parent-ready=%u halted=%u\n",
        i,v,(unsigned)fClockReleasing,(unsigned)idle,(unsigned)parentReady,(unsigned)halted);
    finishLocalClocks(kIOReturnBusy); return;
   }
   fClockTouched=true; lease.uncertain=true;
   if (!fClockReleasing) fClockOwnedMask|=bit; /* retain even if posted write fails */
   if (!fClockReleasing && i==7 && noCClockDiagnostics()) captureClockLocal(fClockTrace.writeLocal);
   dp_wr(dp_dispcc,b.cbcr,fClockReleasing ? v&~1u : v|1u);
   if (!fClockReleasing) fClockIssuedMask|=bit;
   fClockTrace.writeValid=true; fClockTrace.writeBranch=i;
   fClockTrace.writeBefore=v; fClockTrace.writeWant=fClockReleasing ? v&~1u : v|1u;
   fClockTrace.writeEntryNS=now; fClockTrace.writeIssuedNS=scanout_now();
   fClockTrace.immediateValid=false; fClockTrace.immediateCBCR=0;
   fClockTrace.immediateNS=fClockTrace.physicalAddress=0;
   fClockTrace.waitSamples=0; fClockTrace.firstWaitCBCR=fClockTrace.observedBits=0;
   fClockTrace.firstWaitNS=fClockTrace.lastEnabledNS=0;
   if (b.voted && b.managedAHB) {
    /* One read of the same known CBCR after dp_wr's DSB; no retry or control
     * decision. Translation reports the CPU's mapping, not device ownership. */
    fClockTrace.immediateCBCR=dp_rd(dp_dispcc,b.cbcr);
    fClockTrace.immediateNS=scanout_now();
    fClockTrace.physicalAddress=(uint64_t)ml_vtophys(dp_dispcc+b.cbcr);
    fClockTrace.immediateValid=true;
   }
   fClockWaiting=true; fClockWaitDeadline=now+10000000ULL;
   fClockVoteSettleAt=0;
   if (b.voted) SWEET_LOG_INFO("SWEET MDSS CLOCK: voted branch=%u enable=%u; awaiting %s\n",
       i,(unsigned)!fClockReleasing,fClockReleasing ? "vote clear issued and settle; aggregate OFF not required" : "CLK_OFF clear after issued request");
   fClockTimer->setTimeoutUS(100); return;
  } else if (!fClockReleasing && !clockBranchRunning(i,v)) {
   if (diagnose) SWEET_LOG_ERROR("SWEET MDSS CLOCK: NoC gate=worker-branch branch=%u cbcr=%#x\n",i,v);
   finishLocalClocks(kIOReturnNotReady); return;
  }
 }
 if ((!fClockReleasing && fClockCursor==SweetClockCount-1) ||
     (fClockReleasing && fClockCursor==0)) {
  finishLocalClocks(kIOReturnSuccess); return;
 }
 if (fClockReleasing) --fClockCursor; else ++fClockCursor;
 fClockTimer->setTimeoutUS(100);
}
void SweetMDSS::finishLocalClocks(IOReturn rc)
{
 if (fClockTimer) fClockTimer->cancelTimeout();
 SweetClockLease &lease=fClockLeases[fClockOperation-1];
 if (rc==kIOReturnSuccess) {
  /* Earlier branches may have changed while later ones were being awaited. */
  for (unsigned i=0;i<SweetClockCount;++i) if (lease.mask&(1u<<i)) {
   uint32_t parent[6]={}, v=dp_rd(dp_dispcc,mdssClockBranches[i].cbcr);
   bool released=fClockReleasing && (fClockReleaseMask&(1u<<i));
   bool ready=released ? (mdssClockBranches[i].voted || (v&CBCR_OFF)) : clockBranchRunning(i,v);
   if (!clockParent(i,parent,!released,noCClockDiagnostics() ? ClockFailure : ClockSilent) ||
       !ready ||
       (!fClockReleasing && (fClockOwnedMask&(1u<<i)) && !clockEnableParentReady(i,parent))) {
    if (noCClockDiagnostics()) SWEET_LOG_ERROR("SWEET MDSS CLOCK: NoC gate=final-branch branch=%u cbcr=%#x\n",i,v);
    rc=kIOReturnNotReady; break;
   }
   observeClockParent(i,parent,fClockParents[i]);
   if (released || (fClockOwnedMask&(1u<<i)))
    observeLineageValue("clock-final-request",mdssClockBranches[i].cbcr,released ? 0u : 1u,v,1u,false);
  }
 }
 if (rc==kIOReturnSuccess) {
  for (unsigned i=0;i<SweetClockCount;++i) if (lease.mask&(1u<<i)) {
   if (fClockReleasing) --fClockReferences[i]; else ++fClockReferences[i];
   if (!fClockReferences[i] && !(fClockOwnedMask&(1u<<i))) fClockKnownMask&=~(1u<<i);
  }
  lease.state=fClockReleasing ? SweetClockReleased : SweetClockHeld; lease.uncertain=false;
  SWEET_LOG_INFO("SWEET MDSS CLOCK: token=%llu state=%u mask=%#x owned=%#x; core-profile=%u; PHY/link inherited, supply/NoC votes tracked separately\n",
      lease.token,lease.state,lease.mask,fClockOwnedMask,fCoreProfile);
 } else {
  if (fClockReleasing) fClockOwnedMask|=fClockReleaseMask; /* Preserve uncertain withdrawal ownership. */
  fClockFault=true; lease.state=SweetClockFailed;
  /* No blind reverse writes on failure: DMA/state may have changed. Keep
   * touched gates and reject all subsequent writes; stop is not DMA exit. */
  SWEET_LOG_ERROR("SWEET MDSS CLOCK: token=%llu rc=%#x owned=%#x uncertain=%u; writes closed, no powerdown\n",
      lease.token,(unsigned)rc,fClockOwnedMask,lease.uncertain);
 }
 lease.result=(int)rc; fClockOperation=0;
 setProperty("SweetDisplayLocalClockFault",fClockFault);
 setProperty("SweetDisplayLocalClockState",(unsigned long long)lease.state,32);
}
bool SweetMDSS::localClocksReady(void)
{
 if (!displayClientsValid() || fLinkSuspendedMask || noCBusy() || (fNoCEnabled && fNoCFault)) return false; /* No re-entry during transition/fault. */
 if (!fAccepting || isInactive() || !fClockEnabled || fClockFault || !fClockBootToken || !clockPrerequisites()) return false;
 SweetClockLease status={};
 if (clockStatusAction(this,&fClockBootToken,&status,NULL,NULL)!=kIOReturnSuccess || status.state!=SweetClockHeld)
  return false;
 for (unsigned i=0;i<SweetClockCount;++i) if (status.mask&(1u<<i)) {
  uint32_t parent[6]={}, v=dp_rd(dp_dispcc,mdssClockBranches[i].cbcr);
  if (!clockParent(i,parent) || !clockBranchRunning(i,v)) return false;
  observeClockParent(i,parent,fClockParents[i]);
 }
 return true;
}
/* Command references share this controller's real leases/counters. Existing
 * BOOT/display owners keep all branches running; joining them must not write
 * clocks or wait under the synchronous brightness caller's gate. Last-user
 * transitions remain the asynchronous controller's responsibility. */
bool SweetMDSS::dsiClockContextReady(void)
{
 if (!fLifecycleLineage || !displayClientsValid() || (!fPanelDSIClock && !fMdpClock) || clockBusy() || hostBusy() || fDsiPoisoned ||
     fHostPhase==HostFault || fHostPhase==HostStopped || !localClocksReady()) return false;
 SweetClockLease boot={};
 if (localClockStatus(fClockBootToken,&boot)!=kIOReturnSuccess ||
     boot.state!=SweetClockHeld || boot.uncertain || boot.hardwareSuspended ||
     boot.result!=kIOReturnSuccess || boot.mask!=mdssClockMasks[SweetClockDSI0]) return false;
 unsigned mask=mdssClockMasks[SweetClockDSI0];
 if ((fClockKnownMask&mask)!=mask) return false;
 for (unsigned i=0;i<SweetClockCount;++i)
  if ((mask&(1u<<i)) && !fClockReferences[i]) return false;
 return true;
}
bool SweetMDSS::dsiTransferClockReady(void)
{
 if (!fDsiTransferClock || !dsiClockContextReady()) return false;
 SweetClockLease lease={};
 if (localClockStatus(fDsiTransferClock,&lease)!=kIOReturnSuccess ||
     lease.state!=SweetClockHeld || lease.uncertain || lease.hardwareSuspended ||
     lease.result!=kIOReturnSuccess || lease.mask!=mdssClockMasks[SweetClockDSI0]) return false;
 for (unsigned i=0;i<SweetClockCount;++i)
  if ((lease.mask&(1u<<i)) && fClockReferences[i]<2u) return false;
 return true;
}
bool SweetMDSS::retainDSITransferClock(void)
{
 if (fDsiTransferClock || !dsiClockContextReady() || fClockNextToken==~(uint64_t)0) return false;
 unsigned slot=ClockLeaseSlots,mask=mdssClockMasks[SweetClockDSI0];
 for (unsigned n=0;n<ClockLeaseSlots;++n)
  if (!fClockLeases[n].token || fClockLeases[n].state==SweetClockReleased) {slot=n;break;}
 if (slot==ClockLeaseSlots) return false;
 for (unsigned i=0;i<SweetClockCount;++i)
  if ((mask&(1u<<i)) && fClockReferences[i]==~0u) return false;
 /* Validate everything before changing the existing ledger. */
 SweetClockLease &lease=fClockLeases[slot];lease={};fClockClient[slot]=0;
 lease.token=++fClockNextToken;lease.mask=mask;lease.state=SweetClockHeld;
 lease.result=kIOReturnSuccess;lease.inheritedUpstream=true;
 for (unsigned i=0;i<SweetClockCount;++i) if (mask&(1u<<i)) ++fClockReferences[i];
 fDsiTransferClock=lease.token;
 traceWake("command-clock-held");
 if (fDsiClockLogged<8) SWEET_LOG_INFO("SWEET MDSS DSI CLOCK: acquired token=%llu mask=%#x; running branches shared\n",lease.token,mask);
 return true;
}
bool SweetMDSS::releaseDSITransferClock(void)
{
 if (fDsiTransferEngine || fDsiPinned || fDsiSaved || !dsiTransferClockReady()) return false;
 unsigned slot=ClockLeaseSlots;
 for (unsigned n=0;n<ClockLeaseSlots;++n) if (fClockLeases[n].token==fDsiTransferClock) {slot=n;break;}
 if (slot==ClockLeaseSlots) return false;
 SweetClockLease &lease=fClockLeases[slot];
 /* A retained BOOT/display reference prevents last-user physical OFF. */
 for (unsigned i=0;i<SweetClockCount;++i) if (lease.mask&(1u<<i)) --fClockReferences[i];
 lease.state=SweetClockReleased;fDsiTransferClock=0;
 traceWake("command-clock-released");
 if (fDsiClockLogged<8) {
  ++fDsiClockLogged;
  SWEET_LOG_INFO("SWEET MDSS DSI CLOCK: released token=%llu after command idle; shared owners retained\n",lease.token);
 }
 return true;
}
void SweetMDSS::stopLocalClocks(void)
{
 stopCoreResources();
 if (fClockTimer) fClockTimer->cancelTimeout();
 for (unsigned i=0;i<ClockLeaseSlots;++i) {
  SweetClockLease &lease=fClockLeases[i];
  if (lease.state==SweetClockPending || lease.state==SweetClockReleasing || lease.state==SweetClockHeld) {
   lease.state=SweetClockStopped; lease.result=(int)kIOReturnAborted;
  }
 }
 fClockOperation=0;
 /* No register access or vote withdrawal. Existing scanout/DSI may still
  * read memory, so stop/free never powers down its inherited dependencies. */
}


/* Separate DSI-panel and MDP consumers, matching the two Lineage handles.
 * This stage joins the retained core/config floor. It does not implement
 * last-owner core collapse. Only DSI prepare can span the established
 * suspended-link transaction; hardwareSuspended never grants MMIO access. */
bool SweetMDSS::displayClientHeld(uint64_t token,unsigned client,bool suspended)
{
 if (!token || token==fClockBootToken || token==fManagedClock ||
     token==fNoCClockToken || token==fDsiTransferClock) return false;
 for (unsigned i=0;i<ClockLeaseSlots;++i) if (fClockLeases[i].token==token) {
  const SweetClockLease &v=fClockLeases[i];
  return fClockClient[i]==client && v.state==SweetClockHeld && !v.uncertain &&
      v.result==kIOReturnSuccess && v.mask==mdssClockMasks[SweetClockDSI0] &&
      (suspended || !fLinkSuspendedMask);
 }
 return false;
}
bool SweetMDSS::displayClientsValid(void)
{
 if (!fLifecycleLineage) return true;
 /* A stale role token must not hide a live reference, and a lost aggregate
  * count must never authorize a last-owner clock transition. Pending joins
  * are not counted until finishClock; pending withdrawals still own refs. */
 unsigned counts[SweetClockCount]={};
 for (unsigned n=0;n<ClockLeaseSlots;++n) {
  const SweetClockLease &v=fClockLeases[n];unsigned client=fClockClient[n];
  if (client && (client>2 || !v.token || v.token!=(client==1 ? fPanelDSIClock : fMdpClock))) return false;
  if (v.state==SweetClockHeld || v.state==SweetClockReleasing)
   for (unsigned i=0;i<SweetClockCount;++i) if (v.mask&(1u<<i)) ++counts[i];
 }
 for (unsigned i=0;i<SweetClockCount;++i) if (counts[i]!=fClockReferences[i]) return false;
 if (fPanelDSIClock && !displayClientHeld(fPanelDSIClock,1,true)) return false;
 if (fMdpClock && !displayClientHeld(fMdpClock,2)) return false;
 switch (fDisplayClientState) {
 case ClientsBoot:case ClientsOff: return !fPanelDSIClock && !fMdpClock;
 case ClientsPreparing:case ClientsStopping: return fPanelDSIClock!=0;
 case ClientsActive: return !fPanelDSIClock && fMdpClock;
 default: return false;
 }
}
void SweetMDSS::publishDisplayClients(const char *step)
{
 setProperty("SweetDisplayClientState",(unsigned long long)fDisplayClientState,32);
 setProperty("SweetPanelDSIClockToken",(unsigned long long)fPanelDSIClock,64);
 setProperty("SweetMDPClockToken",(unsigned long long)fMdpClock,64);
 SWEET_LOG_INFO("SWEET MDSS CLIENT CLOCK: %s state=%u dsi=%llu mdp=%llu suspended=%#x; shared core floor retained\n",
     step,fDisplayClientState,fPanelDSIClock,fMdpClock,fLinkSuspendedMask);
}
bool SweetMDSS::retainDisplayClient(unsigned client,bool dormant)
{
 if (!fLifecycleLineage || (client!=1 && client!=2) || !displayClientsValid() ||
     !fAccepting || isInactive() || fClockFault || fNoCFault || fDsiPoisoned ||
     clockBusy() || coreBusy() || linkBusy() || hostBusy() || noCBusy() ||
     fDsiTransferClock || fDsiTransferEngine || fDsiPinned || fDsiSaved || dsiBusy()) return false;
 uint64_t &token=client==1 ? fPanelDSIClock : fMdpClock;
 if (token || fClockNextToken==~0ULL) return false;
 if (dormant) {
  if (client!=1 || fDisplayClientState!=ClientsOff || !fLifecycleParked ||
      fLifecycle.operation!=SweetPanelDisplayOn || !fLifecycle.touchQuiesced ||
      !fLifecycle.displayDrained || !fLifecycle.resetHeldLow ||
      fManagedState!=ManagedParked || fManagedClock || fManagedBandwidth ||
      !managedConfigHeld() || !managedRetainedResources() || !linkDormantConfiguration()) return false;
 } else if (!localClocksReady()) return false;
 SweetClockLease boot={};
 if (localClockStatus(fClockBootToken,&boot)!=kIOReturnSuccess || boot.state!=SweetClockHeld ||
     boot.result!=kIOReturnSuccess || boot.uncertain || boot.mask!=mdssClockMasks[SweetClockDSI0]) return false;
 unsigned slot=ClockLeaseSlots,mask=mdssClockMasks[SweetClockDSI0];
 for (unsigned i=0;i<ClockLeaseSlots;++i)
  if (!fClockLeases[i].token || fClockLeases[i].state==SweetClockReleased) {slot=i;break;}
 if (slot==ClockLeaseSlots || (fClockKnownMask&mask)!=mask) return false;
 for (unsigned i=0;i<SweetClockCount;++i)
  if ((mask&(1u<<i)) && (!fClockReferences[i] || fClockReferences[i]==~0u)) return false;
 SweetClockLease &v=fClockLeases[slot];v={};
 v.token=++fClockNextToken;v.mask=mask;v.state=SweetClockHeld;
 v.result=kIOReturnSuccess;v.inheritedUpstream=true;fClockClient[slot]=client;
 for (unsigned i=0;i<SweetClockCount;++i) if (mask&(1u<<i)) ++fClockReferences[i];
 token=v.token;return true;
}
bool SweetMDSS::releaseDisplayClient(unsigned client)
{
 uint64_t &token=client==1 ? fPanelDSIClock : fMdpClock;
 if ((client!=1 && client!=2) || !displayClientsValid() ||
     !displayClientHeld(token,client,client==1) || fDsiTransferClock || fDsiTransferEngine ||
     fDsiPinned || fDsiSaved || fDsiPoisoned || dsiBusy() ||
     !fAccepting || isInactive() || fClockFault || fNoCFault ||
     clockBusy() || coreBusy() || noCBusy() || linkBusy() || hostBusy()) return false;
 unsigned slot=ClockLeaseSlots;
 for (unsigned i=0;i<ClockLeaseSlots;++i) if (fClockLeases[i].token==token) {slot=i;break;}
 if (slot==ClockLeaseSlots) return false;
 const unsigned mask=mdssClockMasks[SweetClockDSI0];
 for (unsigned i=0;i<SweetClockCount;++i)
  if ((mask&(1u<<i)) && fClockReferences[i]<2u) return false;
 for (unsigned i=0;i<SweetClockCount;++i) if (mask&(1u<<i)) --fClockReferences[i];
 fClockLeases[slot].state=SweetClockReleased;fClockClient[slot]=0;token=0;
 return true;
}
bool SweetMDSS::prepareDisplayClients(bool dormant)
{
 if (!fLifecycleLineage) return true;
 bool off=fLifecycle.operation==SweetPanelDisplayOff;
 if (!lifecycleBusy() || !lifecycleParticipants() ||
     (off ? (fDisplayClientState!=ClientsActive || dormant) :
      (fDisplayClientState!=(dormant ? ClientsOff : ClientsBoot)))) return false;
 if (!retainDisplayClient(1,dormant)) return false;
 fDisplayClientState=off ? ClientsStopping : ClientsPreparing;
 publishDisplayClients(off ? "pre-disable DSI held before MDP drain" : "prepare DSI held; hardware readiness required");
 return true;
}
bool SweetMDSS::startMDPClient(void)
{
 if (!fLifecycleLineage) return true;
 if (!displayClientsValid() || !localClocksReady() || !lifecycleParticipants()) return false;
 if (fMdpClock) return displayClientHeld(fMdpClock,2);
 if (fDisplayClientState!=ClientsPreparing || !displayClientHeld(fPanelDSIClock,1) ||
     !fLifecycle.touchQuiesced || !fLifecycle.displayDrained || !lifecycleIRQDrained() ||
     !clockCanGate() || dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c) ||
     !retainDisplayClient(2)) return false;
 publishDisplayClients("MDP held before pipeline/IRQ/kickoff; autorefresh retains vote");return true;
}
bool SweetMDSS::stopMDPClient(void)
{
 if (!fLifecycleLineage) return true;
 if (fDisplayClientState!=ClientsStopping || !displayClientHeld(fPanelDSIClock,1) ||
     !lifecycleParticipants() || !fLifecycle.displayDrained || !fLifecycle.touchQuiesced ||
     !lifecycleIRQDrained() || !clockCanGate() || dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c)) return false;
 uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
 if ((line>0 && line<2340) || !releaseDisplayClient(2)) return false;
 publishDisplayClients("MDP released after scan/IRQ drain; DSI retained for OFF");return true;
}
bool SweetMDSS::postEnableDisplayClients(void)
{
 if (!fLifecycleLineage) return true;
 if (fDisplayClientState!=ClientsPreparing || !displayClientHeld(fMdpClock,2) ||
     !lifecycleParticipants() || !lifecycleResourcesHeld() || !fLifecycle.pixelsComplete ||
     (fLifecycle.operation==SweetPanelDisplayOn && (!fLifecycle.brightnessRestored || !fLifecycleColorRestored)) ||
     !releaseDisplayClient(1)) return false;
 fDisplayClientState=ClientsActive;
 publishDisplayClients("post-enable DSI released; MDP owns continuous scan");return true;
}
bool SweetMDSS::unprepareDisplayClients(void)
{
 if (!fLifecycleLineage) return true;
 if (fDisplayClientState!=ClientsStopping || fMdpClock || !lifecycleParticipants() ||
     !fLifecycle.displayDrained || !lifecycleIRQDrained() || !linkDormantConfiguration() ||
     !managedRetainedResources() || !releaseDisplayClient(1)) return false;
 fDisplayClientState=ClientsOff;
 publishDisplayClients("unprepare DSI released after host/PHY stop");return true;
}
