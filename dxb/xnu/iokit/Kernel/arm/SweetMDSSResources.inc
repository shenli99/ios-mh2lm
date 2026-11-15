/* D1 managed WARM takeover. The real scanout continuation calls these methods
 * on MDSS's gate after TE/autorefresh stop and observed DMA idle. All resource
 * writes go through the existing typed owners; no new MMIO or singleton.
 * Cold domain/link setup and full rail withdrawal are deliberately separate. */
static const SweetRailVote managedRailVotes[SweetRailCount]={
 {true,1800,1800,62000}, {true,3000,3000,857000},
 {true,1200,1200,21800}, {true,880,880,36000}
};
static SweetDisplayBandwidth managedBandwidth(void)
{
 /* LG sm8150-sde.dtsi's enabled RSC usecase: each MNOC port and the LLCC/
  * EBI path has IB=6400000 KB/s, AB=0. Config uses its highest 300000 KB/s
  * usecase during takeover; this is a conservative bootstrap, not DVFS. */
 SweetDisplayBandwidth v={};
 v.port[0].peak=v.port[1].peak=v.llcc.peak=v.ebi.peak=6400000000ULL;
 v.registerPeak=300000000ULL; return v;
}
bool SweetMDSS::setupManagedResources(void)
{
 int mode=0,own=0; (void)PE_parse_boot_argn("sweetdisplaymanaged",&mode,sizeof(mode));
 if (!mode) return true;
 (void)PE_parse_boot_argn("sweetfbown",&own,sizeof(own));
 if (mode!=1 || own!=2 || !fPanelPowerEnabled || fIrqMode!=2 || !fIrqClaimed ||
     fDisplayResourceMode!=2 || !fClockEnabled || !fNoCEnabled) {
  SWEET_LOG_ERROR("SWEET MDSS RESOURCES: managed1 requires own2/power1/IRQ2/resource2/clock1/NoC ABI; refused\n");
  return false;
 }
 fManagedEnabled=true;
 setProperty("SweetDisplayManagedResources",true);
 setProperty("SweetDisplayManagedState",(unsigned long long)ManagedIdle,32);
 return true; /* No resource requests in startup or matching callbacks. */
}
bool SweetMDSS::managedAccessReady(void) const
{ return !fManagedEnabled || fManagedState==ManagedIdle || fManagedState==ManagedReady; }
IOReturn SweetMDSS::managedPreflight(void)
{
 if (!fRPMh || !fRPMhConsumer || !fNoCEnabled || fNoCFault || fClockFault) {
  SWEET_LOG_ERROR("SWEET MDSS RESOURCES: preflight owner absent/faulted; no votes\n"); return kIOReturnNotReady;
 }
 if (fManagedStarted || clockBusy() || noCBusy() || dsiBusy()) {
  SWEET_LOG_WARN("SWEET MDSS RESOURCES: preflight transaction busy; no votes\n"); return kIOReturnBusy;
 }
 SweetDisplayPowerInfo power={};
 if (fRPMh->displayPowerInfo(&power)!=kIOReturnSuccess || !power.available ||
     !power.supported[SweetDisplayTurbo]) {
  SWEET_LOG_ERROR("SWEET MDSS RESOURCES: preflight ARC Turbo chain unavailable; no votes\n"); return kIOReturnNotReady;
 }
 for (unsigned r=0;r<SweetRailCount;++r) {
  SweetRailInfo info={};
  if (fRPMh->resourceInfo((SweetRail)r,&info)!=kIOReturnSuccess || !info.available ||
      managedRailVotes[r].minMV<info.minMV || managedRailVotes[r].maxMV>info.maxMV) {
   SWEET_LOG_ERROR("SWEET MDSS RESOURCES: preflight LDO %u constraints unavailable; no votes\n",r); return kIOReturnNotReady;
  }
 }
 for (unsigned r=0;r<SweetDisplayBCMCount;++r)
  if (fRPMh->copyBCMInfo((SweetDisplayBCM)r,&fNoCInfo[r])!=kIOReturnSuccess) {
   SWEET_LOG_ERROR("SWEET MDSS RESOURCES: preflight BCM %u metadata unavailable; no votes\n",r); return kIOReturnNotReady;
  }
 SweetDisplayBandwidth v=managedBandwidth(); unsigned y=0;
 if (!fNoCInfo[SweetBCMCN0].scale(v.registerPeak,4,1,&y) || !noCPlan(v)) {
  SWEET_LOG_ERROR("SWEET MDSS RESOURCES: preflight LG bootstrap bandwidth unrepresentable; no clamping/votes\n"); return kIOReturnBadArgument;
 }
 /* Do not lower/reprogram inherited clocks while taking over their supply.
  * Turbo is the existing typed SM8150 upper corner, not a raw voltage guess.
  * It is held until the later rate/domain owner can safely lower it. */
 if (!localClocksReady() || (fCoreProfile && !corePreflight()) ||
     (fHostProfile && !hostPreflight())) {
  SWEET_LOG_ERROR("SWEET MDSS RESOURCES: preflight inherited local clocks not held; no votes\n"); return kIOReturnNotReady;
 }
 return kIOReturnSuccess;
}
void SweetMDSS::beginManagedResources(void)
{
 fManagedStarted=true; fManagedState=ManagedRails;
 fManagedDeadline=scanout_now()+4000000000ULL;
 if (!clockCanGate()) { failManagedResources(kIOReturnBusy,"quiesce/IRQ idle not confirmed"); return; }
 setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
 SWEET_LOG_INFO("SWEET MDSS RESOURCES: warm takeover paused; LDO -> ARC Turbo -> core-profile%u -> NoC -> DSI0 clock; no panel reset\n",fCoreProfile);
}
void SweetMDSS::advanceManagedResources(void)
{
 if (!fManagedStarted || fManagedState==ManagedReady || fManagedState==ManagedFault || fManagedState==ManagedStopped ||
     managedParkingBusy() || fManagedState==ManagedParked ||
     fManagedState==ManagedParkLink || fManagedState==ManagedRestoreLink) return;
 uint64_t now=scanout_now();
 if (now>=fManagedDeadline) { failManagedResources(kIOReturnTimeout,"resource-chain deadline"); return; }
 /* The core timer owns transient GDSC/RCG states. Do not read a domain
  * pipeline or compare old root snapshots while it is changing them. */
 if (!fRPMh || !fRPMhConsumer || fNoCFault || fClockFault ||
     (fManagedState!=ManagedCore && fManagedState!=ManagedHost && fManagedState!=ManagedLink && (!clockPrerequisites() || !clockCanGate() || !scanoutConfiguration(true)))) {
  failManagedResources(kIOReturnNotReady,"provider/pipeline/clock admission changed"); return;
 }
 if (now<fManagedWait) return; /* No sleep or long wait under the gate. */
 IOReturn rc=kIOReturnSuccess;
 if (fManagedState==ManagedRails) {
  if (!fManagedToken) {
   rc=fRPMh->requestVote(fRPMhConsumer,(SweetRail)fManagedRail,managedRailVotes[fManagedRail],&fManagedToken);
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"LDO request refused"); return; }
  }
  SweetRPMhStatus status={}; rc=fRPMh->copyStatus(fRPMhConsumer,fManagedToken,&status);
  if (rc!=kIOReturnSuccess || status.state==SweetRPMhFailed) {
   failManagedResources(rc ? rc : status.result,"LDO transaction failed"); return;
  }
  if (status.state!=SweetRPMhComplete) return;
  fManagedRails|=1u<<fManagedRail; fManagedToken=0;
  /* LG panel VDDI post-on 20ms and pre-prepare post-rails 5ms. Warm
   * takeover keeps the panel reset/state intact; no ON table is guessed. */
  fManagedWait=now+(fManagedRail==SweetRailVDDI ? 20000000ULL :
      (fManagedRail==SweetRailVPNL ? 5000000ULL : 0ULL));
  if (++fManagedRail==SweetRailCount) fManagedState=ManagedARC;
 } else if (fManagedState==ManagedARC) {
  if (!fManagedToken) {
   rc=fRPMh->requestDisplayPower(fRPMhConsumer,SweetDisplayTurbo,&fManagedToken);
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"ARC request refused"); return; }
  }
  SweetDisplayPowerStatus status={}; rc=fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedToken,&status);
  if (rc!=kIOReturnSuccess || status.state==SweetRPMhFailed) {
   failManagedResources(rc ? rc : status.result,"ARC transaction failed"); return;
  }
  if (status.state!=SweetRPMhComplete) return;
  fManagedARC=fManagedToken; fManagedToken=0;
  fManagedState=fCoreProfile ? ManagedCore : ManagedNoC;
 } else if (fManagedState==ManagedCore) {
  if (fCorePhase==CoreIdle) {
   rc=beginCoreResources(SweetCoreWarm);
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"core domain/rate request refused"); return; }
  }
  if (fCorePhase==CoreFault || fCorePhase==CoreStopped) {
   failManagedResources(fCoreResult,"core domain/rate transaction failed"); return;
  }
  if (fCorePhase!=CoreReady) return;
  if (!coreReady()) { failManagedResources(kIOReturnNotReady,"core completion lost"); return; }
  fManagedState=ManagedNoC;
 } else if (fManagedState==ManagedNoC) {
  if (!fManagedBandwidth) {
   rc=retainBandwidth(managedBandwidth(),&fManagedBandwidth);
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"NoC request refused"); return; }
  }
  SweetNoCStatus status={}; rc=bandwidthStatus(fManagedBandwidth,&status);
  if (rc!=kIOReturnSuccess || status.state==SweetNoCFailed) {
   failManagedResources(rc ? rc : status.result,"NoC transaction failed"); return;
  }
  if (status.state!=SweetNoCHeld) return;
  fManagedState=ManagedClock;
 } else if (fManagedState==ManagedClock) {
  if (!fManagedClock) {
   rc=retainLocalClocks(SweetClockDSI0,&fManagedClock);
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"DSI0 clock request refused"); return; }
  }
  SweetClockLease status={}; rc=localClockStatus(fManagedClock,&status);
  if (rc!=kIOReturnSuccess || status.state==SweetClockFailed || status.state==SweetClockStopped) {
   failManagedResources(rc ? rc : kIOReturnNotReady,"DSI0 clock transaction failed"); return;
  }
  if (status.state!=SweetClockHeld) return;
  if (fManagedRails!=15u || !localClocksReady()) {
   failManagedResources(kIOReturnNotReady,"final resource validation failed"); return;
  }
  if (fHostProfile) {
   if (!prepareDisplayClients()) {failManagedResources(kIOReturnNotReady,"initial DSI client prepare");return;}
  fManagedState=fLinkProfile ? ManagedLink : ManagedHost;
   setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32); return;
  }
  fManagedState=ManagedReady; fManagedResult=kIOReturnSuccess;
  setProperty("SweetDisplayClockOwnership",fCoreProfile ?
      "MDSS: LDO/ARC/NoC/GDSC/AHB75/MDP460 held; DSI PHY/link inherited" :
      "MDSS: typed LDO/ARC/NoC/local clock held; GDSC/non-XO parents inherited");
  SWEET_LOG_INFO("SWEET MDSS RESOURCES: warm resources acquired rails=%#x ARC=Turbo NoC=%llu clock=%llu; core-profile=%u; retained across DCS OFF, PHY/link cold setup pending\n",
      fManagedRails,fManagedBandwidth,fManagedClock,fCoreProfile);
 } else if (fManagedState==ManagedLink) {
  if (fLinkPhase==LinkIdle) {
   rc=beginLink();
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"DSI link rebuild refused"); return; }
  }
  if (fLinkPhase==LinkFault || fLinkPhase==LinkStopped) {
   failManagedResources(fLinkResult,"DSI link rebuild failed"); return;
  }
  if (fLinkPhase!=LinkReady) return;
  if (!localClocksReady() || !linkConfiguration()) {
   failManagedResources(kIOReturnNotReady,"DSI link final validation failed"); return;
  }
  fManagedState=ManagedHost;
 } else if (fManagedState==ManagedHost) {
  if (fHostPhase==HostIdle) {
   rc=beginHost();
   if (rc!=kIOReturnSuccess) { failManagedResources(rc,"DSI host rebuild refused"); return; }
  }
  if (fHostPhase==HostFault || fHostPhase==HostStopped) {
   failManagedResources(fHostResult,"DSI host rebuild failed"); return;
  }
  if (fHostPhase!=HostReady) return;
  if (!hostResourcesReady() || !hostConfiguration() || !clockCanGate() || !scanoutConfiguration(true)) {
   failManagedResources(kIOReturnNotReady,"DSI host final validation failed"); return;
  }
  fManagedState=ManagedReady; fManagedResult=kIOReturnSuccess;
  setProperty("SweetDisplayClockOwnership",fLinkProfile ? "MDSS: resources/core/DSI0/PHY/PLL/host configured; panel lifecycle inherited" :
      "MDSS: LDO/ARC/NoC/core/DSI0 clocks and host configured; PHY/PLL inherited");
  SWEET_LOG_INFO("SWEET MDSS RESOURCES: host rebuilt, retained pipeline checked; fresh scanout transfer still required; link-profile=%u\n",fLinkProfile);
 }
 setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
}
void SweetMDSS::failManagedResources(IOReturn rc,const char *reason)
{
 if (!fManagedStarted || fManagedState==ManagedFault || fManagedState==ManagedStopped) return;
 fManagedState=ManagedFault; fManagedResult=rc;
 stopLink();
 stopHost();
 if (noCBusy()) stopNoC();
 if (clockBusy() || coreBusy()) stopLocalClocks();
 setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
 setProperty("SweetDisplayManagedFault",true);
 pauseIRQ();
 SWEET_LOG_ERROR("SWEET MDSS RESOURCES: FAILED %s rc=%#x rails=%#x token=%llu NoC=%llu clock=%llu; confirmed/in-flight votes retained, no resume/rollback\n",
     reason,(unsigned)rc,fManagedRails,fManagedToken,fManagedBandwidth,fManagedClock);
}
void SweetMDSS::stopManagedResources(void)
{
 stopLink();
 stopHost();
 if (fManagedStarted && fManagedState!=ManagedFault && fManagedState!=ManagedStopped) {
  fManagedState=ManagedStopped; fManagedResult=kIOReturnAborted;
  setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
  SWEET_LOG_INFO("SWEET MDSS RESOURCES: owner stopped; votes retained, no implicit powerdown\n");
 }
 /* DCS Display Off and service termination are not proof of domain/link
  * collapse. Never withdraw LDO/ARC or NoC beneath inherited active hardware. */
}

/* Reversible DATA lease parking, not link/core shutdown or rail-off. The boot
 * DSI0 clock lease protects inherited link branches; the config-only NoC lease
 * protects CN0 while the core/host/PHY and all LDO/ARC votes remain live. */
bool SweetMDSS::managedParkingBusy(void) const
{
 return fManagedState==ManagedParkConfig || fManagedState==ManagedParkClock ||
     fManagedState==ManagedParkNoC || fManagedState==ManagedRestoreNoC || fManagedState==ManagedRestoreClock;
}
bool SweetMDSS::managedConfigHeld(void)
{
 for (unsigned n=0;n<4;++n) {
  const NoCLease &v=fNoCLeases[n];
  if (fManagedConfigBandwidth && v.status.token==fManagedConfigBandwidth)
   return v.active && v.status.state==SweetNoCHeld && !v.status.uncertain && v.status.result==kIOReturnSuccess &&
       v.vote.registerPeak==300000000ULL && !v.vote.port[0].peak && !v.vote.port[1].peak && !v.vote.llcc.peak && !v.vote.ebi.peak;
 }
 return false;
}
bool SweetMDSS::managedRetainedResources(void)
{
 if (!displayClientsValid() || !fRPMh || !fRPMhConsumer || !lifecyclePanelRailsValid() || !fManagedARC || fNoCFault || fClockFault ||
     !clockPrerequisites() || (fCoreProfile && !coreReady()) ||
     (fHostProfile && fHostPhase!=HostReady && !(fLifecycleLinkCycle && fHostPhase==HostDormant)) ||
     (fLinkProfile && (fLinkPhase!=LinkReady || !linkConfiguration()) &&
      !(fLifecycleLinkCycle && fLinkPhase==LinkDormant && linkDormantConfiguration()))) return false;
 SweetDisplayPowerStatus a={};SweetClockLease boot={};
 if (fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&a)!=kIOReturnSuccess ||
     a.state!=SweetRPMhComplete || a.uncertain || a.corner!=SweetDisplayTurbo ||
     localClockStatus(fClockBootToken,&boot)!=kIOReturnSuccess || boot.state!=SweetClockHeld ||
     boot.uncertain || boot.result!=kIOReturnSuccess || boot.mask!=mdssClockMasks[SweetClockDSI0]) return false;
 /* localClocksReady intentionally rejects an in-flight NoC operation. Here
  * validate the independent BOOT lease/physical branches during that operation. */
 if (fLifecycleLinkCycle && fLifecycleDataParking) {
  if (linkOtherUsers()) return false;
  for (unsigned n=0;n<ClockLeaseSlots;++n) {
   const SweetClockLease &v=fClockLeases[n];
   if (v.state==SweetClockHeld && (v.mask&linkParkedMask()) && v.token!=fClockBootToken && v.token!=fManagedClock && v.token!=fPanelDSIClock && v.token!=fMdpClock) return false;
  }
  if (fLinkSuspendedMask) for (unsigned n=fLifecycleLineage ? 3u : 4u;n<=6;++n) if (fClockReferences[n]!=1u+(fPanelDSIClock ? 1u : 0u)) return false;
 }
 for (unsigned n=0;n<SweetClockCount;++n) if ((boot.mask&(1u<<n)) && !(fLinkSuspendedMask&(1u<<n))) {
  uint32_t parent[6]={},v=dp_rd(dp_dispcc,mdssClockBranches[n].cbcr);
  if (!clockParent(n,parent) || !clockBranchRunning(n,v)) return false;
  observeClockParent(n,parent,fClockParents[n]);
 }
 return true;
}
IOReturn SweetMDSS::beginManagedParking(bool restore)
{
 if (!fLifecycleDataParking || !lifecycleResourceStepAllowed() || fManagedToken ||
     clockBusy() || noCBusy() || coreBusy() || hostBusy() || linkBusy()) return kIOReturnNotReady;
 if (restore) {
  if (fManagedState!=ManagedParked || fManagedClock || fManagedBandwidth || !managedConfigHeld()) return kIOReturnNotReady;
  fManagedState=ManagedRestoreNoC;
 } else {
  if (!lifecycleResourcesHeld()) return kIOReturnNotReady;
  fManagedState=ManagedParkConfig;
 }
 fManagedDeadline=scanout_now()+4000000000ULL;fManagedResult=kIOReturnNotReady;
 setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
 return kIOReturnSuccess;
}
void SweetMDSS::advanceManagedParking(void)
{
 if (!managedParkingBusy()) return;
 if (!lifecycleResourceStepAllowed() || scanout_now()>=fManagedDeadline) {
  finishLifecycle(kIOReturnNotReady,"data resource safety/deadline");return;
 }
 IOReturn rc=kIOReturnSuccess;
 if (fManagedState==ManagedParkConfig || fManagedState==ManagedParkNoC || fManagedState==ManagedRestoreNoC) {
  bool config=fManagedState==ManagedParkConfig,release=fManagedState==ManagedParkNoC;
  uint64_t &token=config ? fManagedConfigBandwidth : fManagedBandwidth;
  if (release ? !fManagedToken : !token) {
   SweetDisplayBandwidth v={};v.registerPeak=300000000ULL;
   rc=fLifecycleLinkCycle && !config && fLinkSuspendedMask ? lifecycleDataBandwidth(release) :
       (release ? releaseBandwidth(token) : retainBandwidth(config ? v : managedBandwidth(),&token));
   if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"data bandwidth request refused");return;}
   if (release) fManagedToken=token; /* retain original lease until Released */
  }
  SweetNoCStatus n={};rc=bandwidthStatus(token,&n);
  if (rc!=kIOReturnSuccess || n.uncertain || n.state==SweetNoCFailed ||
      (n.state!=(release ? SweetNoCReleasing : SweetNoCPending) && n.state!=(release ? SweetNoCReleased : SweetNoCHeld))) {
   finishLifecycle(rc ? rc : kIOReturnIOError,"data bandwidth completion failed");return;
  }
  if (n.state!=(release ? SweetNoCReleased : SweetNoCHeld)) return;
  if (n.result!=kIOReturnSuccess || (config && !managedConfigHeld())) {finishLifecycle(kIOReturnIOError,"config/data ACK proof absent");return;}
  if (config) fManagedState=ManagedParkClock;
  else if (release) {fManagedBandwidth=fManagedToken=0;fManagedState=ManagedParked;fManagedResult=kIOReturnSuccess;}
  else fManagedState=fLifecycleLinkCycle ? ManagedRestoreLink : ManagedRestoreClock;
 } else {
  bool release=fManagedState==ManagedParkClock;
  if (release ? !fManagedToken : !fManagedClock) {
   rc=release ? releaseLocalClocks(fManagedClock) : retainLocalClocks(SweetClockDSI0,&fManagedClock);
   if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"data clock request refused");return;}
   if (release) fManagedToken=fManagedClock;
  }
  SweetClockLease c={};rc=localClockStatus(fManagedClock,&c);
  /* uncertain marks a posted branch write; wait for the terminal readback. */
  if (rc!=kIOReturnSuccess || c.state==SweetClockFailed || c.state==SweetClockStopped ||
      (c.state!=(release ? SweetClockReleasing : SweetClockPending) && c.state!=(release ? SweetClockReleased : SweetClockHeld))) {
   finishLifecycle(rc ? rc : kIOReturnIOError,"data clock completion failed");return;
  }
  if (c.state!=(release ? SweetClockReleased : SweetClockHeld)) return;
  if (c.uncertain || c.result!=kIOReturnSuccess || c.mask!=mdssClockMasks[SweetClockDSI0]) {finishLifecycle(kIOReturnIOError,"data clock proof absent");return;}
  if (release) {fManagedClock=fManagedToken=0;fManagedState=fLifecycleLinkCycle ? ManagedParkLink : ManagedParkNoC;}
  else {fManagedState=ManagedReady;fManagedResult=kIOReturnSuccess;}
 }
 setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
}

/* Private typed request: no public flag or ambient gate-wide bypass. The
 * exact managed DATA token/output is rechecked by noCAction below. */
IOReturn SweetMDSS::lifecycleDataBandwidth(bool release)
{
 if (!fLifecycleDataParking || !fLifecycleLinkCycle || fLinkSuspendedMask!=linkParkedMask()) return kIOReturnNotReady;
 SweetDisplayBandwidth vote=managedBandwidth();SweetNoCRequest r={};
 r.lifecycleData=true;r.operation=release ? 1u : 0u;r.token=fManagedBandwidth;
 r.vote=&vote;r.output=release ? NULL : &fManagedBandwidth;
 return fGate ? fGate->runAction(noCAction,&r) : kIOReturnNotReady;
}
