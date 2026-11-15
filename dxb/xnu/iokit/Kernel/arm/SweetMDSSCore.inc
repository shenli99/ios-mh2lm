/* SM8150-v2 core resources, under the existing MDSS clock owner/gate/timer.
 * Register facts: LG gdsc-regulator.c, dispcc-sm8150.c, clk-alpha-pll.c and
 * clk-rcg2.c. This is an original asynchronous implementation, not Linux CCF.
 * No PHY PLL, pixel root, reset, solver, automatic collapse or rail withdrawal.
 */
bool SweetMDSS::coreBusy(void) const
{ return fCorePhase>CoreIdle && fCorePhase<CoreReady; }
bool SweetMDSS::coreConfigAccessible(void)
{
 /* LG's GDSC ahb_clk is GCC_DISP_AHB, outside the switchable MDSS domain.
  * Requiring DISPCC_AHB/MDP here would make a powered-off domain impossible
  * to enable. Do not read MDSS, DSI, display RSC or PHY through this check. */
 return dp_gcc && dp_dispcc && (dp_rd(dp_gcc,GCC_DISP_AHB)&CBCR_ENABLE);
}
bool SweetMDSS::corePLLReady(unsigned pll)
{
 if (pll>1) return false;
 uint32_t b=pll*0x1000u, mode=dp_rd(dp_dispcc,b);
 bool configured=(fCoreProgrammedPLL&(1u<<pll))!=0;
 if (!(mode&0x80000000u) || (mode&0x00700080u)) return false;
 if (configured) {
  observeLineage("core-pll-mode",dp_dispcc,b,5u,5u);
  observeLineage("core-pll-opmode",dp_dispcc,b+0x38,1u);
  observeLineage("core-pll-output",dp_dispcc,b+0xc,1u,1u);
  observeLineage("core-pll-L",dp_dispcc,b+4,pll ? 0x1fu : 0x47u);
  observeLineage("core-pll-alpha",dp_dispcc,b+0x40,pll ? 0x4000u : 0xe000u);
  return true;
 }
 return (mode&5u)==5u && dp_rd(dp_dispcc,b+0x38)==1u && (dp_rd(dp_dispcc,b+0xc)&1u) &&
     dp_rd(dp_dispcc,b+4)==(pll ? 0x1fu : 0x47u) && dp_rd(dp_dispcc,b+0x40)==(pll ? 0x4000u : 0xe000u);
}
bool SweetMDSS::corePLLAdmissible(unsigned pll)
{
 if (corePLLReady(pll)) return true; /* Keep a live PLL; never retune it. */
 uint32_t b=pll*0x1000u, mode=dp_rd(dp_dispcc,b);
 /* Partially running/FSM-controlled/unknown state is not a cold PLL. */
 if ((mode&0xf0700081u) || dp_rd(dp_dispcc,b+0x38)!=0) return false;
 /* All DISPCC roots which can select these core PLLs, per LG's parent maps.
  * An enabled orphan root is a fault, not permission to restart its parent. */
 const uint32_t roots[]={0x22bc,0x22a4,0x20c8,0x20e0};
 for (unsigned i=0;i<4;++i) {
  uint32_t cmd=dp_rd(dp_dispcc,roots[i]);
  unsigned source=(dp_rd(dp_dispcc,roots[i]+4)>>8)&7u;
  bool selected=pll ? source==4 || source==5 : (i>=2 && source==1);
  if (selected && (!(cmd&0x80000000u) || (cmd&0xf3u))) return false;
 }
 return true;
}
bool SweetMDSS::corePreflight(void)
{
 if (fCoreProfile!=2 || !coreConfigAccessible() || clockBusy() || noCBusy() || dsiBusy()) return false;
 if (dp_rd(dp_dispcc,0x3000)&6u) return false; /* No forced HW_CONTROL/OVERRIDE takeover. */
 for (unsigned pll=0;pll<2;++pll) if (!corePLLAdmissible(pll)) return false;
 const uint32_t roots[]={0x22bc,0x20c8};
 for (unsigned r=0;r<2;++r)
  if (dp_rd(dp_dispcc,roots[r])&0xf1u) return false;
 return true;
}
bool SweetMDSS::coreReady(void)
{
 if (fCorePhase!=CoreReady || !coreConfigAccessible() ||
     (dp_rd(dp_dispcc,0x3000)&0x80000007u)!=0x80000000u ||
     !corePLLReady(0) || !corePLLReady(1)) return false;
 const uint32_t roots[]={0x22bc,0x20c8}, cfg[]={0x40fu,0x100105u};
 for (unsigned i=0;i<2;++i) {
  if (dp_rd(dp_dispcc,roots[i])&0x800000f1u) return false;
  observeLineage("core-ready-root",dp_dispcc,roots[i]+4,cfg[i],0x10371fu);
 }
 const uint32_t branches[]={0x2080,0x200c};
 for (unsigned i=0;i<2;++i) {
  uint32_t v=dp_rd(dp_dispcc,branches[i]);
  if (v&CBCR_OFF) return false;
  observeLineage("core-branch-request",dp_dispcc,branches[i],1u,1u);
 }
 return true;
}
IOReturn SweetMDSS::beginCoreResources(SweetCoreStart kind)
{
 /* Internal typed lifecycle entry; all callers run on the MDSS gate. Cold
  * bring-up is reserved for a future link-init entry, never loss recovery of
  * an existing splash/DMA. The current real takeover calls warm only. */
 if (!fAccepting || isInactive() || fCorePhase!=CoreIdle || !fClockEnabled ||
     !fRPMh || !fRPMhConsumer || (kind!=SweetCoreWarm && kind!=SweetCoreCold)) return kIOReturnNotReady;
 SweetDisplayPowerStatus power={};
 if (!fManagedARC || fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&power)!=kIOReturnSuccess ||
     power.state!=SweetRPMhComplete || power.corner!=SweetDisplayTurbo) return kIOReturnNotReady;
 bool cold=kind==SweetCoreCold;
 if (cold) {
  /* No domain-register idle reads while powered off. Refuse ANY previous
   * clock/DMA/IRQ ownership; this is not a resume or failed-transfer retry. */
  if (fClockTouched || fIrqClaimed || fGicTouched || fSourceOwned || fDsiPoisoned ||
      fClockKnownMask || fClockBootToken || fScanoutUsed ||
      fDsiPinned || fDsiPrepared || dsiBusy() || fIrqPhase || fHardwarePool) return kIOReturnBusy;
 } else {
  if (!corePreflight()) return kIOReturnNotReady;
  if ((dp_rd(dp_dispcc,0x3000)&0x80000007u)!=0x80000000u || !clockCanGate()) return kIOReturnBusy;
 }
 /* Warm admission already checked GCC access, domain and idle. Read only
  * the DISPCC local context before this operation changes core resources.
  * Never add this diagnostic register access to the cold-domain path. */
 fClockWarmLocal={};
 if (!cold) captureClockLocal(fClockWarmLocal);
 fCoreCold=cold; fCorePhase=CoreConfigVote;
 fCoreDeadline=scanout_now()+500000000ULL; fCoreResult=kIOReturnSuccess;
 fCoreWait=0; fCorePLL=fCoreCursor=fCoreRoot=0;
 fClockTimer->setTimeoutUS(1);
 setProperty("SweetDisplayCoreState",(unsigned long long)fCorePhase,32);
 return kIOReturnSuccess;
}
void SweetMDSS::advanceCoreResources(void)
{
 if (!coreBusy()) return;
 uint64_t now=scanout_now();
 if (now>=fCoreDeadline) { finishCoreResources(kIOReturnTimeout); return; }
 if (!coreConfigAccessible() || !fRPMh || !fRPMhConsumer) {
  finishCoreResources(kIOReturnNotReady); return;
 }
 SweetDisplayPowerStatus power={};
 if (fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&power)!=kIOReturnSuccess ||
     power.state!=SweetRPMhComplete || power.corner!=SweetDisplayTurbo) {
  finishCoreResources(kIOReturnNotReady); return;
 }
 if (now<fCoreWait) { fClockTimer->setTimeoutUS(1); return; }
 if (fCorePhase==CoreConfigVote) {
  if (!fCoreCN0) {
   IOReturn rc=fRPMh->requestRegisterBandwidth(fRPMhConsumer,300000000ULL,&fCoreCN0);
   if (rc!=kIOReturnSuccess) { finishCoreResources(rc); return; }
  }
  SweetRegisterBandwidthStatus v={};
  IOReturn rc=fRPMh->copyRegisterBandwidthStatus(fRPMhConsumer,fCoreCN0,&v);
  if (rc!=kIOReturnSuccess || v.state==SweetRPMhFailed) {
   finishCoreResources(rc ? rc : kIOReturnNotReady); return;
  }
  if (v.state!=SweetRPMhComplete) { fClockTimer->setTimeoutUS(100); return; }
  if (v.bytesPerSecond<300000000ULL) { finishCoreResources(kIOReturnNotReady); return; }
  /* Cold GDSC/PLL accesses start only AFTER both actual upstream ACKs. */
  if (!corePreflight() || (fCoreCold && (dp_rd(dp_dispcc,0x3000)&0x80000000u))) {
   finishCoreResources(kIOReturnNotReady); return;
  }
  fCorePhase=CoreDomain;
 } else {
  /* CN0 cannot be overwritten/withdrawn while operating on this config path.
   * After CoreReady the NoC module replaces this same consumer's CN0 request
   * with an equal floor and owns its newer status token. */
  SweetRegisterBandwidthStatus v={};
  if (fRPMh->copyRegisterBandwidthStatus(fRPMhConsumer,fCoreCN0,&v)!=kIOReturnSuccess ||
      v.state!=SweetRPMhComplete || v.bytesPerSecond<300000000ULL) {
   finishCoreResources(kIOReturnNotReady); return;
  }
  uint32_t gdsc=dp_rd(dp_dispcc,0x3000);
  if ((gdsc&6u) || (!fCoreCold && !(gdsc&0x80000000u))) {
   finishCoreResources(kIOReturnNotReady); return;
  }
  if (fCorePhase>=CoreStable && (gdsc&0x80000007u)!=0x80000000u) {
   finishCoreResources(kIOReturnNotReady); return;
  }
  uint32_t b=fCorePLL*0x1000u;
  switch (fCorePhase) {
   case CoreDomain:
    fClockTouched=true;
    dp_wr(dp_dispcc,0x3000,gdsc&~1u); /* Only the HLOS SW_COLLAPSE vote. */
    fCorePhase=CoreDomainWait; fCoreStepDeadline=now+100000ULL;
    fCoreWait=now+1000ULL; break; /* >=8 XO cycles before first PWR_ON poll. */
   case CoreDomainWait:
    if ((gdsc&0x80000007u)!=0x80000000u) {
     if (now>=fCoreStepDeadline) { finishCoreResources(kIOReturnTimeout); return; }
     break;
    }
    fCorePhase=CoreStable; fCoreWait=now+2000ULL; break;
   case CoreStable:
    fCorePhase=CorePLL; break;
   case CorePLL:
    if (corePLLReady(fCorePLL)) {
     fCoreInheritedPLL|=1u<<fCorePLL;
     if (++fCorePLL==2) { fCorePLL=0; fCorePhase=CoreRoot; }
    } else if (!corePLLAdmissible(fCorePLL)) { finishCoreResources(kIOReturnNotReady); return; }
    else { fCoreProgrammedPLL|=1u<<fCorePLL; fCorePhase=CorePLLConfig; fCoreCursor=0; }
    break;
   case CorePLLConfig: {
    /* TRION v2: no v1 TEST_CTL_HI=2 write. One bounded write per timer.
     * Configure only STANDBY/output-disabled PLLs; never reset a live parent. */
    const uint32_t offsets[]={4,8,0x40,0x18,0x1c,0x20,0x10,0x14,0,0,0x38,0};
    const uint32_t values[]={fCorePLL ? 0x1fu : 0x47u,0x44u,
        fCorePLL ? 0x4000u : 0xe000u,0x20485699u,0x2267u,0x24u,0x805u,0xd0u,0,0,0,0};
    unsigned c=fCoreCursor; uint32_t value=values[c];
    if (c==8) value=dp_rd(dp_dispcc,b)|(1u<<23);
    if (c==9) value=dp_rd(dp_dispcc,b)&~1u;
    if (c==11) value=dp_rd(dp_dispcc,b)|4u;
    fClockTouched=true; dp_wr(dp_dispcc,b+offsets[c],value);
    /* Configuration readback is diagnostic; LOCK remains the completion. */
    uint32_t mask=c==8 ? 1u<<23 : (c==9 ? 1u : (c==11 ? 4u : ~0u));
    observeLineage("core-pll-write",dp_dispcc,b+offsets[c],value,mask,true);
    if (++fCoreCursor==12) fCorePhase=CorePLLRun;
    break;
   }
   case CorePLLRun:
    fClockTouched=true; dp_wr(dp_dispcc,b+0x38,1u);
    observeLineage("core-pll-run",dp_dispcc,b+0x38,1u,~0u,true);
    fCorePhase=CorePLLLock; fCoreStepDeadline=now+1000000ULL; break;
   case CorePLLLock:
    if (dp_rd(dp_dispcc,b)&0x80000000u) fCorePhase=CorePLLOut;
    else if (now>=fCoreStepDeadline) { finishCoreResources(kIOReturnTimeout); return; }
    break;
   case CorePLLOut:
    dp_wr(dp_dispcc,b+0xc,dp_rd(dp_dispcc,b+0xc)|7u);
    fCorePhase=CorePLLGlobal; break;
   case CorePLLGlobal:
    dp_wr(dp_dispcc,b,dp_rd(dp_dispcc,b)|1u);
    if (!corePLLReady(fCorePLL)) { finishCoreResources(kIOReturnIOError); return; }
    if (++fCorePLL==2) { fCorePLL=0; fCorePhase=CoreRoot; }
    else fCorePhase=CorePLL;
    break;
   case CoreRoot: {
    if (!corePLLReady(0) || !corePLLReady(1)) { finishCoreResources(kIOReturnNotReady); return; }
    uint32_t root=fCoreRoot ? 0x20c8u : 0x22bcu;
    uint32_t cmd=dp_rd(dp_dispcc,root);
    if (cmd&0xf1u) { finishCoreResources(kIOReturnBusy); return; }
    /* LG's highest AHB75MHz: PLL1(600)/8; MDP460MHz: PLL0(1380)/3.
     * MDP uses HW_CLK_CTRL_MODE. Preserve unrelated/reserved CFG bits. */
    uint32_t cfg=dp_rd(dp_dispcc,root+4);
    fCoreRootCFG=(cfg&~0x10371fu)|(fCoreRoot ? 0x100105u : 0x40fu);
    fClockTouched=true; dp_wr(dp_dispcc,root+4,fCoreRootCFG);
    observeLineage("core-rcg-config",dp_dispcc,root+4,fCoreRootCFG,~0u,true);
    fCorePhase=CoreRootUpdate; break;
   }
   case CoreRootUpdate: {
    uint32_t root=fCoreRoot ? 0x20c8u : 0x22bcu;
    dp_wr(dp_dispcc,root,dp_rd(dp_dispcc,root)|1u);
    fCoreStepDeadline=now+500000ULL; fCorePhase=CoreRootWait; break;
   }
   case CoreRootWait: {
    uint32_t root=fCoreRoot ? 0x20c8u : 0x22bcu, cmd=dp_rd(dp_dispcc,root);
    if (cmd&0xf1u) {
     if (now>=fCoreStepDeadline) { finishCoreResources(kIOReturnTimeout); return; }
     break;
    }
    observeLineage("core-rcg-updated",dp_dispcc,root+4,fCoreRootCFG);
    if (!fCoreCold && (cmd&0x80000000u)) {
     finishCoreResources(kIOReturnNotReady); return;
    }
    /* Deliberately commit ONLY this controlled root to existing leases.
     * Other parent changes are diagnosed; retain each requested snapshot. */
    for (unsigned i=0;i<SweetClockCount;++i)
     if ((fClockKnownMask&(1u<<i)) && mdssClockBranches[i].root==root) {
      fClockParents[i][0]=fCoreRootCFG; fClockParents[i][5]=(fCoreRootCFG>>8)&7u;
     }
    if (++fCoreRoot==2) { fCorePhase=CoreBranches; fCoreCursor=0; break; }
    fCorePhase=CoreRoot; break;
   }
   case CoreBranches: {
    const uint32_t branches[]={0x2080,0x200c};
    unsigned i=fCoreCursor/2; uint32_t off=branches[i], v=dp_rd(dp_dispcc,off);
    if (!(fCoreCursor&1u)) {
     if (!(v&1u)) {
      if (!fCoreCold || !(v&CBCR_OFF)) { finishCoreResources(kIOReturnNotReady); return; }
      fCoreOwnedBranches|=1u<<i; fClockTouched=true;
      dp_wr(dp_dispcc,off,v|1u);
     }
     ++fCoreCursor; fCoreStepDeadline=now+10000000ULL;
    } else {
     uint32_t root=i ? 0x20c8u : 0x22bcu;
     if ((v&CBCR_OFF) || (dp_rd(dp_dispcc,root)&0x800000f1u)) {
      if (now>=fCoreStepDeadline) { finishCoreResources(kIOReturnTimeout); return; }
      break;
     }
     if (++fCoreCursor==4) { finishCoreResources(kIOReturnSuccess); return; }
    }
    break;
   }
   default: finishCoreResources(kIOReturnIOError); return;
  }
 }
 setProperty("SweetDisplayCoreState",(unsigned long long)fCorePhase,32);
 fClockTimer->setTimeoutUS(1); /* Asynchronous, no busy wait under the gate. */
}
void SweetMDSS::finishCoreResources(IOReturn rc)
{
 unsigned phase=fCorePhase;
 if (fClockTimer) fClockTimer->cancelTimeout();
 if (rc==kIOReturnSuccess) {
  fCorePhase=CoreReady;
  /* Both core branches, physical roots, PLLs and domain must be live.
   * This does not claim that DSI/PHY or a framebuffer is initialized. */
  if (!coreReady()) rc=kIOReturnNotReady;
 }
 fCoreResult=rc;
 if (rc!=kIOReturnSuccess) {
  fCorePhase=CoreFault; fClockFault=true;
  SWEET_LOG_ERROR("SWEET MDSS CORE: failed phase=%u rc=%#x pll=%u root=%u; votes/partial effects retained, no reset/rollback/collapse\n",
      phase,(unsigned)rc,fCorePLL,fCoreRoot);
 } else {
  SWEET_LOG_INFO("SWEET MDSS CORE: profile2 GDSC held, PLL0=1380MHz PLL1=600MHz inherited-mask=%#x AHB=75MHz MDP=460MHz cold=%u; PHY/link outside this operation\n",
      fCoreInheritedPLL,fCoreCold);
 }
 setProperty("SweetDisplayCoreState",(unsigned long long)fCorePhase,32);
 setProperty("SweetDisplayCoreFault",rc!=kIOReturnSuccess);
}
void SweetMDSS::stopCoreResources(void)
{
 if (fCorePhase!=CoreIdle && fCorePhase!=CoreFault && fCorePhase!=CoreStopped) {
  fCorePhase=CoreStopped; fCoreResult=kIOReturnAborted;
  setProperty("SweetDisplayCoreState",(unsigned long long)fCorePhase,32);
 }
 /* Parent votes and hardware are retained until a full link teardown exists. */
}
