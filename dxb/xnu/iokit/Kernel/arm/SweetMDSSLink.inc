/* DPHY v4.0 / PLL 7nm v2, standalone PHY0. Explicit board profile, not a
 * guessed revision decoder. LG sm8150-sde.dtsi + v2 overlay and R66456 common
 * timing define the target. Same MDSS owner/gate/writer and existing DSI timer.
 * Warm, quiesced reconstruction only; no panel reset/vendor commands here. */
struct SweetLinkRegister { uint32_t offset,value; };
#include "SweetMDSSLinkData.inc"
static const SweetLinkRegister linkPHYCommon[]={
 {0x024,0x60}, {0x03c,0}, {0x01c,0}, {0x034,0x21}, {0x038,0x84},
 {0x018,0}, {0x020,0x5b}, {0x110,0x5c}, {0x030,0}, {0x10c,3},
 {0x0ec,0x66}, {0x104,0}, {0x0f4,3}, {0x0f8,0x3c}, {0x100,0x55},
 {0x024,0x7f}, {0x0a0,0x1f}, {0x02c,0x40}, {0x014,0}
};
static const uint8_t linkTiming[14]={0,0x21,8,8,0x25,0x22,9,8,6,3,4,0,0x1b,0x18};
static const uint32_t linkGates[3]={0x202c,0x2004,0x2028}; /* OFF: intf,pixel,byte; ON: byte,pixel,intf */
static const uint32_t linkRoots[2]={0x2110,0x2098};
/* LG v2.2 ops used by v2.3: wrapper bit0=1 masks PHY reset propagation;
 * bits5/7/9 permit PHY0 pixel/byte/PHY dynamic gating. PHY1 bits untouched. */
static const uint32_t linkMiscMask=0x2a1u;
bool SweetMDSS::setupLink(void)
{
 int mode=0; (void)PE_parse_boot_argn("sweetdsilink",&mode,sizeof(mode));
 if (!mode) return true;
 if (mode!=2 || fHostProfile!=1 || fCoreProfile!=2 || !fManagedEnabled ||
     !fMaps[12] || fMaps[12]->getPhysicalAddress()!=0x0ae94400ULL ||
     fMaps[12]->getLength()<0x760 || !sweet_dsi_link_plan(1056619200ULL,&fLinkPlan)) {
  SWEET_LOG_ERROR("SWEET MDSS LINK: profile2 requires host1/core2/managed1/PHY0 ABI; refused\n"); return false;
 }
 fLinkProfile=2;
 setProperty("SweetDSILinkSelectedProfile",2ULL,32);
 setProperty("SweetDSILinkState",(unsigned long long)LinkIdle,32);
 return true; /* Selected v4.0/7nm-v2; no claim of revision detection. */
}
bool SweetMDSS::linkBusy(void) const
{ return (fLinkPhase>LinkIdle && fLinkPhase<LinkReady) || fLinkPhase==LinkLPStop || fLinkPhase==LinkLPStart; }
unsigned SweetMDSS::linkParkedMask(void) const
{ return fLifecycleLineage ? 0x78u : 0x70u; }
bool SweetMDSS::linkOtherUsers(void)
{
 /* DSI1 roots can also select PHY0. Reject a live or changing alias, including
  * an orphan root with no software branch vote. No PHY1 mapping/writes. */
 const uint32_t roots[]={0x212c,0x20b0}, gates[]={0x2030,0x2034,0x2008};
 for (unsigned n=0;n<2;++n) if (((dp_rd(dp_dispcc,roots[n]+4)>>8)&7u)==1u) {
  if ((dp_rd(dp_dispcc,roots[n])&0x800000f3u)!=0x80000000u) return true;
  if (dp_rd(dp_dispcc,gates[n==0 ? 0 : 2])&1u) return true;
  if (!n && (dp_rd(dp_dispcc,gates[1])&1u)) return true;
 }
 return false;
}
bool SweetMDSS::linkResourcesReady(void)
{
 if (fLifecycleLinkCycle && (fLifecycle.phase==SweetPanelLinkPowerDown || fLifecycle.phase==SweetPanelLinkRebuild))
  return lifecycleLinkStepAllowed();
 if (!fAccepting || isInactive() || !fRPMh || !fRPMhConsumer ||
     fManagedState!=ManagedLink || fManagedRails!=15u || !coreReady() ||
     fClockFault || fNoCFault || clockBusy() || noCBusy() || dsiBusy() || hostBusy() ||
     fDsiPinned || fDsiPoisoned || fHostPhase!=HostIdle ||
     __atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE)!=IrqIdle || linkOtherUsers()) return false;
 if (fLinkTouched) observeLineage("link-misc-preserved",dp_dispcc,0x8000,fLinkMisc,~linkMiscMask);
 SweetClockLease clock={}; SweetNoCStatus noc={}; SweetDisplayPowerStatus power={};
 if (!fManagedClock || localClockStatus(fManagedClock,&clock)!=kIOReturnSuccess ||
     clock.state!=SweetClockHeld || clock.mask!=mdssClockMasks[SweetClockDSI0] ||
     !fManagedBandwidth || bandwidthStatus(fManagedBandwidth,&noc)!=kIOReturnSuccess ||
     noc.state!=SweetNoCHeld || !fManagedARC ||
     fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&power)!=kIOReturnSuccess ||
     power.state!=SweetRPMhComplete || power.corner!=SweetDisplayTurbo) return false;
 /* Validate only the unaffected local branches/parents. BYTE/PCLK are
  * deliberately unavailable during rebuild; an old Held token is not proof
  * that these physically run. Their snapshots are updated only at completion. */
 for (unsigned n=0;n<SweetClockCount;++n) if ((fClockKnownMask&(1u<<n)) && !(0x70u&(1u<<n))) {
  uint32_t p[6]={}; uint32_t branch=dp_rd(dp_dispcc,mdssClockBranches[n].cbcr);
  if (!clockParent(n,p,true) || !clockBranchRunning(n,branch)) return false;
  observeClockParent(n,p,fClockParents[n]);
 }
 return !(dp_rd(dp_intf1,0x288)&(1u<<20)) && !dp_rd(dp_intf1,0x2b4);
}
bool SweetMDSS::linkWarmAdmission(void)
{
 if (fLinkProfile!=2 || fLinkPhase!=LinkIdle || !fMaps[12] ||
     !localClocksReady() || !clockCanGate() || !hostPreflight() || !scanoutConfiguration(true)) return false;
 vm_offset_t p=fMaps[12]->getVirtualAddress();
 /* Physical bytes are recorded without inventing their revision encoding.
  * Linux also selects this driver by compatible. This mode is an explicit
  * board-profile experiment, not automatic authorization on unknown boards. */
 for (unsigned n=0;n<4;++n) if (dp_rd(p,4*n)>255u) return false;
 if (dp_rd(dp_dispcc,0x8000)==~0u || !(dp_rd(p,0x6b0)&1u) || !(dp_rd(p,0x140)&1u) ||
     (dp_rd(p,0x18)&(1u<<6))) return false; /* Known DPHY, powered/locked. */
 const SweetLinkRegister inherited[]={{0x24,0x7f},{0xa0,0x1f},{0x3c,1},{0x14,0x20},
     {0x34,0x21},{0x38,0x84},{0xa4,0},{0xa8,0},{0xac,0}};
 for (unsigned n=0;n<sizeof(inherited)/sizeof(inherited[0]);++n)
  observeLineage("link-inherited",p,inherited[n].offset,inherited[n].value,~0u,true);
 for (unsigned n=0;n<5;++n) observeLineage("link-inherited-swap",p,0x210+0x80*n,0,~0u,true);
 return true;
}
bool SweetMDSS::linkWrite(uint32_t offset,uint32_t value)
{
 if (offset>=0x760 || (offset&3u)) return false;
 vm_offset_t p=fMaps[12]->getVirtualAddress();
 dp_wr(p,offset,value); observeLineage("phy-pll-write",p,offset,value,~0u,true); return true;
}
IOReturn SweetMDSS::beginLink(bool powerDown)
{
 if (powerDown) {
  if (!fLifecycleLinkCycle || fLifecycle.phase!=SweetPanelLinkPowerDown || fLinkPhase!=LinkReady ||
      (!(fLifecycleDataParking && fManagedState==ManagedParkLink) && !lifecycleResourcesHeld()) ||
      !linkConfiguration() || !lifecycleLinkStepAllowed()) return kIOReturnNotReady;
 } else if (!linkResourcesReady() || !linkWarmAdmission()) return kIOReturnNotReady;
 if (!setupDSI()) return kIOReturnNoResources;
 vm_offset_t p=fMaps[12]->getVirtualAddress();
 SWEET_LOG_INFO("SWEET MDSS LINK: selected v4.0/7nm-v2 rev-bytes=%02x:%02x:%02x:%02x target-bit=%llu VCO=%llu actualVCO=%llu; operation=%s\n",
     dp_rd(p,0),dp_rd(p,4),dp_rd(p,8),dp_rd(p,12),fLinkPlan.bitHz,fLinkPlan.vcoHz,fLinkPlan.actualVcoHz,powerDown ? "power-down" : "warm rebuild");
 fLinkPowerDown=powerDown;fLinkResume=false;fLinkWait=0;
 fLinkMisc=dp_rd(dp_dispcc,0x8000);
 fLinkTouched=fClockTouched=true;
 uint32_t misc=fLifecycleLineage && powerDown ? fLinkMisc : fLinkMisc&~linkMiscMask;
 dp_wr(dp_dispcc,0x8000,misc);
 observeLineage("link-wrapper-request",dp_dispcc,0x8000,misc,~0u,true);
 if (powerDown) {
  IOReturn rc=disableHostForLink();
  if (rc!=kIOReturnSuccess) {finishLink(rc,"host shutdown");return rc;}
 } else {
  dp_wr(dp_dsi,0x110,0); /* enable bits only, no W1C acknowledgements */
  dp_wr(dp_dsi,4,dp_rd(dp_dsi,4)&~7u);
 }
 observeLineage("link-host-irq-disable",dp_dsi,0x110,0,hostInterruptEnables,true);
 observeLineage("link-host-disable",dp_dsi,4,0,7u,true);
 fLinkDeadline=scanout_now()+250000000ULL;
 fLinkPhase=LinkLaneIdle; fLinkStepDeadline=scanout_now()+100000ULL;
 setProperty("SweetDSILinkState",(unsigned long long)fLinkPhase,32);
 fDsiTimer->setTimeoutUS(10); return kIOReturnSuccess;
}
bool SweetMDSS::linkConfiguration(void)
{
 if (!fLinkProfile || fLinkPhase!=LinkReady) return false;
 vm_offset_t p=fMaps[12]->getVirtualAddress();
 /* REFGEN and PLL lock are real Lineage status checks. Configuration
  * echoes are observations, including when revalidated by host/lifecycle. */
 if (!(dp_rd(p,0x140)&1u) || !(dp_rd(p,0x6b0)&1u)) return false;
 observeLineage("link-ready-misc",dp_dispcc,0x8000,(fLinkMisc&~linkMiscMask)|0x2a0u);
 const SweetLinkRegister expected[]={
  {0x3c,1},{0x24,0x7f},{0x14,0x20},{0x10,0x62},{0x30,4},
  {0x1c,1},{0x34,0x21},{0x38,0x84},{0x5e0,fLinkPlan.decimal},
  {0x5e4,fLinkPlan.fraction&255u},{0x5e8,(fLinkPlan.fraction>>8)&255u},{0x5ec,fLinkPlan.fraction>>16},
  {0x654,0},{0x650,2},{0x620,fLinkPlan.sscStep&255u},{0x624,fLinkPlan.sscStep>>8},
  {0x628,fLinkPlan.sscPeriod&255u},{0x62c,fLinkPlan.sscPeriod>>8},{0x630,2},{0x634,0}
 };
 for (unsigned n=0;n<sizeof(expected)/sizeof(expected[0]);++n)
  observeLineage("link-ready",p,expected[n].offset,expected[n].value);
 for (unsigned n=0;n<14;++n) observeLineage("link-timing",p,0xb4+4*n,linkTiming[n]);
 for (unsigned n=0;n<5;++n) {
  uint32_t b=0x200+0x80*n;
  const SweetLinkRegister lane[]={{b,0},{b+4,0},{b+8,n==4 ? 0x8au : 0xau},
      {b+0x10,0},{b+0x14,n ? 0u : 3u},{b+0x18,n==4 ? 1u : (n==3 ? 4u : 0u)}};
  for (unsigned i=0;i<sizeof(lane)/sizeof(lane[0]);++i)
   observeLineage("link-lane",p,lane[i].offset,lane[i].value);
 }
 return true; /* Configuration echoes never prove fresh pixels. */
}
void SweetMDSS::advanceLink(void)
{
 if (!linkBusy()) return;
 uint64_t now=scanout_now();
 if (!linkResourcesReady()) { finishLink(kIOReturnNotReady,"resource/alias/idle admission lost"); return; }
 if (now>=fLinkDeadline) { finishLink(kIOReturnTimeout,"link rebuild deadline"); return; }
 if (now<fLinkWait) { fDsiTimer->setTimeoutUS(1); return; }
 vm_offset_t p=fMaps[12]->getVirtualAddress();
 switch (fLinkPhase) {
 case LinkLaneIdle:
  if ((dp_rd(p,0x14c)&0x1fu)!=0x1fu || (dp_rd(dp_dsi,8)&6u)) {
   if (now>=fLinkStepDeadline) { finishLink(kIOReturnTimeout,"lane/engine stop state"); return; }
   break;
  }
  /* dsi_update_clk_state stops HS before invoking the LP pre-off ULPS
   * callback. Its HS pre-off callback first disables PHY clock gating. */
  if (fLifecycleLineage && fLinkPowerDown)
   dp_wr(dp_dispcc,0x8000,dp_rd(dp_dispcc,0x8000)&~0x2a0u);
  if (fLinkPowerDown) {
   fLinkSuspendedMask=0x70u;
   setProperty("SweetDisplaySuspendedClockMask",(unsigned long long)fLinkSuspendedMask,32);
  }
  for (unsigned n=0;n<3;++n) {
   uint32_t b=linkGates[n]; dp_wr(dp_dispcc,b,dp_rd(dp_dispcc,b)&~1u);
   observeLineage("link-gate-disable",dp_dispcc,b,0,1u,true);
  }
  fLinkPhase=LinkGatesOff; fLinkStepDeadline=now+10000000ULL; break;
 case LinkULPSIdle:
  /* The earlier pre-HS idle sample is insufficient: v4 checks all five
   * lanes immediately before a non-clamped ULPS entry request. LP/core
   * leases remain held, and every timer step revalidates ownership/idle. */
  if ((dp_rd(p,0x14c)&0x1fu)!=0x1fu) {
   if (now>=fLinkStepDeadline) {finishLink(kIOReturnTimeout,"post-HS ULPS lane idle");return;}
   break;
  }
  linkWrite(0xa4,0x1f);
  fLinkPhase=LinkULPSEntry;fLinkWait=scanout_now()+100000ULL;break;
 case LinkULPSEntry:
  linkWrite(0x214,0); /* LPRX/CDRX off after >=100us */
  {
   uint32_t status=dp_rd(p,0x148);
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: ULPS entry lane-status=%#x mask=0x1f want=0\n",status);
   if (status&0x1fu) {finishLink(kIOReturnIOError,"ULPS entry status");return;}
  }
  /* dsi_update_clk_state: HS OFF -> LP pre-off ULPS -> ESC OFF ->
   * LP post-off PHY idle. The retained boot lease is now suspended for
   * ESC too; neither its Held state nor bit0 alone proves physical halt. */
  fLinkSuspendedMask=linkParkedMask();
  setProperty("SweetDisplaySuspendedClockMask",(unsigned long long)fLinkSuspendedMask,32);
  dp_wr(dp_dispcc,0x2038,dp_rd(dp_dispcc,0x2038)&~1u);
  observeLineage("link-esc-disable",dp_dispcc,0x2038,0,1u,true);
  SWEET_LOG_INFO("SWEET PANEL LINEAGE: ULPS entered after HS stop; ESC0 halt pending\n");
  fLinkPhase=LinkLPStop;fLinkStepDeadline=now+10000000ULL;break;
 case LinkLPStop: {
  uint32_t parent[6]={};
  if (!clockParent(3,parent,false)) {finishLink(kIOReturnNotReady,"ESC0 parent changed while stopping");return;}
  if (!(dp_rd(dp_dispcc,0x2038)&CBCR_OFF)) {
   if (now>=fLinkStepDeadline) {finishLink(kIOReturnTimeout,"ESC0 did not halt");return;}
   break;
  }
  observeClockParent(3,parent,fClockParents[3]);
  SWEET_LOG_INFO("SWEET PANEL LINEAGE: ESC0 halted after ULPS; PHY shutdown next\n");
  fLinkPhase=LinkPHYOff;break;
 }
 case LinkLPStart: {
  uint32_t parent[6]={};
  if (!clockParent(3,parent,false)) {finishLink(kIOReturnNotReady,"ESC0 parent changed while starting");return;}
  if (dp_rd(dp_dispcc,0x2038)&CBCR_OFF) {
   if (now>=fLinkStepDeadline) {finishLink(kIOReturnTimeout,"ESC0 did not start");return;}
   break;
  }
  if (!clockParent(3,parent,true)) {finishLink(kIOReturnNotReady,"ESC0 running parent unavailable");return;}
  observeClockParent(3,parent,fClockParents[3]);
  /* LP post-on configures host/PHY only after the ESC branch is running.
   * HS remains unavailable until the existing PLL/root/ULPS-exit tail. */
  fLinkSuspendedMask=0x70u;
  setProperty("SweetDisplaySuspendedClockMask",(unsigned long long)fLinkSuspendedMask,32);
  SWEET_LOG_INFO("SWEET PANEL LINEAGE: ESC0 running before ULPS host setup\n");
  configureHostForULPS();
  fLinkPhase=LinkRefgen;fLinkStepDeadline=now+1000000ULL;break;
 }
 case LinkGatesOff: {
  bool off=true;
  for (unsigned n=0;n<3;++n) if (!(dp_rd(dp_dispcc,linkGates[n])&CBCR_OFF)) off=false;
  /* Retain the real UPDATE/dirty exclusion. ROOT_OFF is an additional
   * snapshot condition absent from Lineage branch disable, so log it. */
  for (unsigned n=0;n<2;++n) {
   uint32_t cmd=dp_rd(dp_dispcc,linkRoots[n]);
   if (cmd&0xf1u) off=false;
   observeLineageValue("link-root-stopped",linkRoots[n],0x80000000u,cmd,0x800000f1u,false);
  }
  if (!off) {
   if (now>=fLinkStepDeadline) { finishLink(kIOReturnTimeout,"link gates/roots did not halt"); return; }
   break;
  }
  /* No downstream reader remains. Clock leases/reference counts stay retained,
   * while physical readiness is explicitly closed for this transaction. */
  if (!linkWrite(0x14,dp_rd(p,0x14)&~(1u<<5)) || !linkWrite(0x3c,0)) {
   finishLink(kIOReturnIOError,"PLL stop readback"); return;
  }
  fLinkPhase=LinkPLLStop; fLinkStepDeadline=now+5000000ULL; break;
 }
 case LinkPLLStop:
  /* Lineage 7nm disable writes PLL/bias off without polling unlock. */
  observeLineage("link-pll-stop-lock",p,0x6b0,0,1u,true);
  /* dsi_pll_disable_sub stops the resync FIFO before withdrawing bias. */
  if (!linkWrite(0x1c,0) || !linkWrite(0x528,0) || !linkWrite(0x24,dp_rd(p,0x24)&~(1u<<5))) {
   finishLink(kIOReturnIOError,"PLL bias off"); return;
  }
  fLinkPhase=LinkBiasOff; fLinkWait=scanout_now()+250; break;
 case LinkBiasOff:
  if (fLifecycleLineage && fLinkPowerDown) {
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: HS branches halted; PLL/bias stop issued; checking ULPS lane idle\n");
   fLinkPhase=LinkULPSIdle;fLinkStepDeadline=now+100000ULL;break;
  }
  fLinkPhase=LinkPHYOff;
  /* fall through */
 case LinkPHYOff:
  if (fLinkPowerDown && (!linkWrite(0x214,0) || !linkWrite(0x24,dp_rd(p,0x24)&~0x1fu))) {
   finishLink(kIOReturnIOError,"PHY LPRX/lane shutdown");return;
  }
  if (!linkWrite(0xa0,0) || !linkWrite(0x24,0)) { finishLink(kIOReturnIOError,"PHY disable"); return; }
  if (fLinkPowerDown) {
   __asm__ volatile("dsb oshst" ::: "memory");
   if (fLifecycleLineage) {
    /* v2.3 reset_config(false) SETS bit0. v4 clamp callbacks are NULL:
     * no invented clamp register. Paired resume still re-enters ULPS. */
    dp_wr(dp_dispcc,0x8000,dp_rd(dp_dispcc,0x8000)|1u);
    observeLineage("ulps-reset-mask",dp_dispcc,0x8000,1u,1u,true);
   }
   fLinkPhase=LinkDormant;fLinkResult=kIOReturnSuccess;
   if (!linkDormantConfiguration()) {finishLink(kIOReturnIOError,"PHY shutdown proof");return;}
   if (fDsiTimer) fDsiTimer->cancelTimeout();
   setProperty("SweetDSILinkState",(unsigned long long)fLinkPhase,32);
   SWEET_LOG_INFO("SWEET MDSS LINK: shutdown writes issued and branches halted; configuration echoes diagnostic; votes retained, not rail-off\n");
   return;
  }
  dp_wr(dp_dsi,0x12c,0x01000001u);
  __asm__ volatile("dsb oshst" ::: "memory");
  observeLineage("phy-reset-assert",dp_dsi,0x12c,0x01000001u,~0u,true);
  fLinkPhase=LinkResetHold; fLinkWait=scanout_now()+1000000ULL; break;
 case LinkResetHold:
  dp_wr(dp_dsi,0x12c,0);
  __asm__ volatile("dsb oshst" ::: "memory");
  observeLineage("phy-reset-release",dp_dsi,0x12c,0,~0u,true);
  fLinkPhase=LinkResetSettle; fLinkWait=scanout_now()+100000ULL; break;
 case LinkResetSettle:
  fLinkPhase=LinkRefgen; fLinkStepDeadline=now+1000000ULL; break;
 case LinkRefgen:
  if (!(dp_rd(p,0x140)&1u)) {
   if (now>=fLinkStepDeadline) { finishLink(kIOReturnTimeout,"PHY REFGEN not ready"); return; }
   break;
  }
  fLinkPhase=LinkPHYConfig; break;
 case LinkPHYConfig:
  for (unsigned n=0;n<sizeof(linkPHYCommon)/sizeof(linkPHYCommon[0]);++n)
   if (!linkWrite(linkPHYCommon[n].offset,linkPHYCommon[n].value)) { finishLink(kIOReturnIOError,"PHY common config"); return; }
  for (unsigned n=0;n<14;++n)
   if (!linkWrite(0xb4+4*n,linkTiming[n])) { finishLink(kIOReturnIOError,"PHY timing"); return; }
  for (unsigned n=0;n<5;++n) {
   uint32_t b=0x200+0x80*n;
   /* DTS supplies four lane-config bytes; LG v4 writes the first THREE.
    * Do not manufacture a CFG3 register from the unused fourth byte. */
   if (!linkWrite(b,0) || !linkWrite(b+4,0) || !linkWrite(b+8,n==4 ? 0x8a : 0xa) ||
       !linkWrite(b+0x14,n ? 0 : 3) || !linkWrite(b+0x18,n==4 ? 1 : (n==3 ? 4 : 0)) ||
       !linkWrite(b+0x10,0)) { finishLink(kIOReturnIOError,"PHY lane config"); return; }
  }
  fLinkCursor=0;
  if (fLifecycleLineage && fLinkResume) {
   /* idle_on reconfigured PHY. Synchronize ULPS while the display's clamp
    * state is still set; Lineage skips the lane-idle poll on this entry. */
   linkWrite(0xa4,0x1f);fLinkPhase=LinkULPSReentry;fLinkWait=scanout_now()+100000ULL;
  } else fLinkPhase=LinkPLLConfig;
  break;
 case LinkULPSReentry:
  linkWrite(0x214,0);
  {
   uint32_t status=dp_rd(p,0x148);
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: ULPS wake reentry lane-status=%#x mask=0x1f want=0\n",status);
   if (status&0x1fu) {finishLink(kIOReturnIOError,"ULPS wake reentry status");return;}
  }
  /* reset_config(true); software clamp state is now released. */
  dp_wr(dp_dispcc,0x8000,dp_rd(dp_dispcc,0x8000)&~1u);
  observeLineage("ulps-reset-unmask",dp_dispcc,0x8000,0,1u,true);
  fLinkPhase=LinkPLLConfig;break;
 case LinkPLLConfig: {
  unsigned count=(unsigned)(sizeof(linkPLLInitial)/sizeof(linkPLLInitial[0])),end=fLinkCursor+8;
  if (end>count) end=count;
  while (fLinkCursor<end) {
   const SweetLinkRegister &r=linkPLLInitial[fLinkCursor++];
   if (!linkWrite(r.offset,r.value)) { finishLink(kIOReturnIOError,"PLL initial config"); return; }
  }
  if (fLinkCursor<count) break;
  const SweetLinkRegister rate[]={
   {0x5bc,0x12}, {0x5e0,fLinkPlan.decimal}, {0x5e4,fLinkPlan.fraction&255u},
   {0x5e8,(fLinkPlan.fraction>>8)&255u}, {0x5ec,fLinkPlan.fraction>>16},
   {0x658,0x40}, {0x694,6}, {0x750,0x10}, {0x748,0x28}
  };
  for (unsigned n=0;n<sizeof(rate)/sizeof(rate[0]);++n)
   if (!linkWrite(rate[n].offset,rate[n].value)) { finishLink(kIOReturnIOError,"PLL fractional rate"); return; }
  for (unsigned n=0;n<sizeof(linkPLLFinal)/sizeof(linkPLLFinal[0]);++n)
   if (!linkWrite(linkPLLFinal[n].offset,linkPLLFinal[n].value)) { finishLink(kIOReturnIOError,"PLL v2 final config"); return; }
  const SweetLinkRegister div[]={
   {0x620,fLinkPlan.sscStep&255u},{0x624,fLinkPlan.sscStep>>8},
   {0x628,fLinkPlan.sscPeriod&255u},{0x62c,fLinkPlan.sscPeriod>>8},{0x630,2},{0x634,0},
   {0x650,2},{0x654,0},{0x010,0x62},{0x014,0},{0x024,0x7f},{0x528,0xc0}
  };
  for (unsigned n=0;n<sizeof(div)/sizeof(div[0]);++n)
   if (!linkWrite(div[n].offset,div[n].value)) { finishLink(kIOReturnIOError,"PLL SSC/dividers/bias"); return; }
  fLinkPhase=LinkBiasOn; fLinkWait=scanout_now()+250; break;
 }
 case LinkBiasOn:
  if (!linkWrite(0x3c,1)) { finishLink(kIOReturnIOError,"PLL start"); return; }
  fLinkPhase=LinkLock; fLinkStepDeadline=scanout_now()+5000000ULL; break;
 case LinkLock:
  if (!(dp_rd(p,0x6b0)&1u)) {
   if (now>=fLinkStepDeadline) { finishLink(kIOReturnTimeout,"fresh PLL lock"); return; }
   break;
  }
  if (!linkWrite(0x128,1) || !linkWrite(0x128,0) || !linkWrite(0x30,4) || !linkWrite(0x14,0x30)) {
   finishLink(kIOReturnIOError,"PHY digital reset/global clock"); return;
  }
  fLinkRoot=0; fLinkPhase=LinkRoots; break;
 case LinkRoots: {
  uint32_t root=linkRoots[fLinkRoot],cmd=dp_rd(dp_dispcc,root);
  if (cmd&0xf1u) { finishLink(kIOReturnBusy,"DSI RCG already updating"); return; }
  uint32_t previous[6]={};unsigned parent=fLinkRoot ? 6u : 4u;
  if (!clockParent(parent,previous,false)) {
   finishLink(kIOReturnNotReady,"DSI root unavailable before update");return;
  }
  observeClockParent(parent,previous,fClockParents[parent]);
  uint32_t cfg=(fClockParents[parent][0]&~0x10371fu)|0x101u;
  dp_wr(dp_dispcc,root+4,cfg);
  if (fLinkRoot) { dp_wr(dp_dispcc,root+8,0); dp_wr(dp_dispcc,root+12,0); dp_wr(dp_dispcc,root+16,0); }
  observeLineage("link-rcg-config",dp_dispcc,root+4,cfg,~0u,true);
  dp_wr(dp_dispcc,root,cmd|1u); fLinkPhase=LinkRootWait; fLinkStepDeadline=now+500000ULL; break;
 }
 case LinkRootWait: {
  uint32_t root=linkRoots[fLinkRoot];
  if (dp_rd(dp_dispcc,root)&0xf1u) {
   if (now>=fLinkStepDeadline) { finishLink(kIOReturnTimeout,"DSI RCG update"); return; }
   break;
  }
  if (++fLinkRoot<2) { fLinkPhase=LinkRoots; break; }
  observeLineage("link-byte-prior-divider",dp_dispcc,0x2128,fClockParents[5][4]);
  uint32_t divider=(fClockParents[5][4]&~3u)|1u; /* DPHY byte_intf = byte / 2 */ dp_wr(dp_dispcc,0x2128,divider);
  observeLineage("link-byte-divider",dp_dispcc,0x2128,divider,~0u,true);
  /* Reverse gate order: byte -> pixel -> intf. Check actual CLK_OFF below. */
  for (unsigned n=3;n>0;--n) {
   uint32_t b=linkGates[n-1]; dp_wr(dp_dispcc,b,dp_rd(dp_dispcc,b)|1u);
   observeLineage("link-gate-enable",dp_dispcc,b,1u,1u,true);
  }
  fLinkPhase=LinkGatesOn; fLinkStepDeadline=now+10000000ULL; break;
 }
 case LinkGatesOn: {
  bool on=true;
  for (unsigned n=0;n<3;++n) if (dp_rd(dp_dispcc,linkGates[n])&CBCR_OFF) on=false;
  for (unsigned n=0;n<2;++n) observeLineage("link-root-running",dp_dispcc,linkRoots[n],0,0x800000f1u);
  if (!on) {
   if (now>=fLinkStepDeadline) { finishLink(kIOReturnTimeout,"link gates/root not running"); return; }
   break;
  }
  fLinkPhase=LinkResync; break;
 }
 case LinkResync: {
  if (!linkWrite(0x1c,0) || !linkWrite(0x1c,1) || !linkWrite(0x14,0x20)) {
   finishLink(kIOReturnIOError,"PHY resync"); return;
  }
  if (fLifecycleLineage && fLinkResume) {
   if (dp_rd(p,0x148)&0x1fu) {finishLink(kIOReturnIOError,"ULPS pre-exit status");return;}
   linkWrite(0x214,3);linkWrite(0xa8,0x1f);
   fLinkPhase=LinkULPSExit;fLinkWait=scanout_now()+1000000ULL;break;
  }
  fLinkPhase=LinkResumeComplete;break;
 }
 case LinkULPSExit:
  linkWrite(0xa4,0);linkWrite(0xa8,0);
  linkWrite(0xac,0x1f);linkWrite(0xac,0);
  fLinkPhase=LinkULPSStop;fLinkWait=scanout_now()+100000ULL;break;
 case LinkULPSStop:
  /* Same predicate as dsi_phy_disable_ulps: zero means still in ULPS. */
  {
   uint32_t status=dp_rd(p,0x148);
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: ULPS exit lane-status=%#x mask=0x1f want=nonzero\n",status);
   if (!(status&0x1fu)) {finishLink(kIOReturnIOError,"lanes stuck in ULPS");return;}
  }
  SWEET_LOG_INFO("SWEET PANEL LINEAGE: ULPS exited; resync complete, host soft reset pending\n");
  fLinkPhase=LinkResumeComplete;break;
 case LinkResumeComplete: {
  /* Retain requested root fields; diagnose snapshot differences while
   * preserving actual parent availability and branch completion checks. */
  uint32_t expected[3][6]={};
  for (unsigned n=0;n<3;++n) {
   unsigned i=4+n; memcpy(expected[n],fClockParents[i],sizeof(expected[n]));
   expected[n][0]=(expected[n][0]&~0x10371fu)|0x101u; expected[n][5]=1;
   if (i==5) expected[n][4]=(expected[n][4]&~3u)|1u;
   if (i==6) expected[n][1]=expected[n][2]=expected[n][3]=0;
   uint32_t actual[6]={};
   if (!clockParent(i,actual,true)) {
    finishLink(kIOReturnIOError,"controlled root/parent unavailable"); return;
   }
   observeClockParent(i,actual,expected[n]);
  }
  dp_wr(dp_dispcc,0x8000,(fLinkMisc&~linkMiscMask)|0x2a0u);
  fLinkPhase=LinkReady;
  if (!linkConfiguration()) { finishLink(kIOReturnIOError,"PHY/PLL completion validation"); return; }
  for (unsigned n=0;n<3;++n) memcpy(fClockParents[4+n],expected[n],sizeof(expected[n]));
  if (fLinkResume) {
   fLinkSuspendedMask=0;
   setProperty("SweetDisplaySuspendedClockMask",0ULL,32);
  }
  finishLink(kIOReturnSuccess,"configured"); return;
 }
 default: finishLink(kIOReturnIOError,"invalid link phase"); return;
 }
 setProperty("SweetDSILinkState",(unsigned long long)fLinkPhase,32);
 fDsiTimer->setTimeoutUS(10);
}
void SweetMDSS::finishLink(IOReturn rc,const char *reason)
{
 if (fDsiTimer) fDsiTimer->cancelTimeout();
 unsigned phase=fLinkPhase;
 fLinkResult=rc; fLinkPhase=rc==kIOReturnSuccess ? LinkReady : LinkFault;
 if (rc!=kIOReturnSuccess) {
  if (fLinkTouched) fDsiPoisoned=true;
  SWEET_LOG_ERROR("SWEET MDSS LINK: FAILED %s phase=%u rc=%#x touched=%u; votes/partial link retained, no rollback/retry/panel reset\n",
      reason,phase,(unsigned)rc,fLinkTouched);
 } else {
  SWEET_LOG_INFO("SWEET MDSS LINK: v4.0/7nm-v2 configured VCO=%llu bit=%llu byte=%llu byte-intf=%llu pixel=%llu DOWN-SSC; lock/branch completion observed; configuration echoes diagnostic; host rebuild/fresh pixels pending\n",
      fLinkPlan.actualVcoHz,fLinkPlan.actualVcoHz/2,fLinkPlan.actualVcoHz/16,fLinkPlan.actualVcoHz/32,fLinkPlan.actualVcoHz/12);
 }
 setProperty("SweetDSILinkState",(unsigned long long)fLinkPhase,32);
 setProperty("SweetDSILinkFault",rc!=kIOReturnSuccess);
}
void SweetMDSS::stopLink(void)
{
 if (!linkBusy()) return;
 if (fDsiTimer) fDsiTimer->cancelTimeout();
 fLinkPhase=LinkStopped; fLinkResult=kIOReturnAborted;
 if (fLinkTouched) fDsiPoisoned=true;
 setProperty("SweetDSILinkState",(unsigned long long)fLinkPhase,32);
 /* No reset release, speculative rollback, PLL stop or rail withdrawal here. */
}

bool SweetMDSS::linkDormantConfiguration(void)
{
 if (!fMaps[12] || fLinkPhase!=LinkDormant || fHostPhase!=HostDormant || fLinkSuspendedMask!=linkParkedMask() ||
     (dp_rd(dp_dsi,8)&6u) || (dp_rd(dp_dsi,0x110)&(1u<<24))) return false;
 observeLineage("dormant-host-control",dp_dsi,4,0,7u);
 observeLineage("dormant-host-reset",dp_dsi,0x118,0);
 observeLineage("dormant-phy-reset",dp_dsi,0x12c,0);
 observeLineage("dormant-host-clocks",dp_dsi,0x11c,0x23fu);
 observeLineage("dormant-host-irq",dp_dsi,0x110,0,hostInterruptEnables);
 vm_offset_t p=fMaps[12]->getVirtualAddress();
 const SweetLinkRegister off[]={{0x14,0},{0x3c,0},{0x528,0},{0x24,0},{0xa0,0},{0x214,0}};
 for (unsigned n=0;n<sizeof(off)/sizeof(off[0]);++n)
  observeLineage("dormant-phy",p,off[n].offset,off[n].value,off[n].offset==0x14 ? 1u<<5 : ~0u);
 observeLineage("dormant-pll-lock",p,0x6b0,0,1u);
 for (unsigned n=0;n<3;++n) {
  if (!(dp_rd(dp_dispcc,linkGates[n])&CBCR_OFF)) return false;
  observeLineage("dormant-gate-enable",dp_dispcc,linkGates[n],0,1u);
 }
 for (unsigned n=0;n<2;++n)
  observeLineage("dormant-root",dp_dispcc,linkRoots[n],0x80000000u,0x800000f1u);
 for (unsigned n=4;n<=6;++n) {
  uint32_t parent[6]={};
  if (!clockParent(n,parent,false)) return false;
  observeClockParent(n,parent,fClockParents[n]);
 }
 if (fLifecycleLineage) {
  uint32_t parent[6]={};
  if (!(dp_rd(dp_dispcc,0x2038)&CBCR_OFF) || !clockParent(3,parent,false)) return false;
  observeClockParent(3,parent,fClockParents[3]);
 }
 observeLineage("dormant-misc",dp_dispcc,0x8000,fLifecycleLineage ? 1u : 0u,linkMiscMask);
 return !linkOtherUsers();
}
IOReturn SweetMDSS::resumeLink(void)
{
 if (!fLifecycleLinkCycle || fLifecycle.phase!=SweetPanelLinkRebuild || !linkDormantConfiguration() ||
     !lifecycleLinkStepAllowed() || fDsiPoisoned || !setupDSI()) return kIOReturnNotReady;
 /* Only our successful, same-provider shutdown admits this path. No clearing
  * Fault/Stopped/poison, no adoption of an unknown cold PHY. Reuse the actual
  * reset/config/rate/lock/root execution tail; never copy old PHY registers. */
 fLinkPowerDown=false;fLinkResume=true;fLinkCursor=fLinkRoot=0;fLinkWait=0;
 if (fLifecycleLineage) {
  uint32_t parent[6]={};
  if (!clockParent(3,parent,false) || !clockEnableParentReady(3,parent)) return kIOReturnNotReady;
  dp_wr(dp_dispcc,0x2038,dp_rd(dp_dispcc,0x2038)|1u);
  observeLineage("link-esc-enable",dp_dispcc,0x2038,1u,1u,true);
 }
 fLinkPhase=fLifecycleLineage ? LinkLPStart : LinkBiasOff;
 fLinkStepDeadline=scanout_now()+(fLifecycleLineage ? 10000000ULL : 1000000ULL);
 fLinkDeadline=scanout_now()+250000000ULL;
 fDsiTimer->setTimeoutUS(10);return kIOReturnSuccess;
}
