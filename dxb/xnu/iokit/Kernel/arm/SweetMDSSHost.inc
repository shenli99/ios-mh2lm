/* D1 DSI0 host v2.3 rebuild. Register facts: LG dsi_ctrl_hw_cmn.c,
 * dsi_ctrl_hw_2_0.c, dsi_ctrl_reg.h and the four R66456 command panels.
 * Original state machine on MDSS's sole gate/MMIO writer and DSI timer.
 * This operation intentionally retains the live PHY/PLL and panel state.
 */
static const uint32_t hostForceClocks=0x200b3fu;
static const uint32_t hostInterruptEnables=0xa222aa02u;
struct SweetHostRegister { uint32_t offset,value; };
/* Common to PP1/DV1/DV2/PV: VC0, RGB888, 1080x2340, 4 DPHY lanes,
 * one-hot identity map, no DSC/split/CPHY; T_CLK_POST=18/PRE=1b.
 * No vendor command, DDIC revision or reset GPIO is inferred from this table.
 * TRIG bit31 follows LG te-pin-select=1 (ABL observed only SW trigger=4).
 * It selects external TE; MDP trigger remains NONE, DMA trigger remains SW.
 */
static const SweetHostRegister hostCommandProfile[]={
 {0x004,0x1f0}, {0x03c,0x14000000}, {0x040,8}, {0x044,0x13c2c},
 {0x048,0}, {0x04c,0}, {0x050,0},
 {0x058,0x0ca90039}, {0x05c,0x09240438},
 {0x060,0x0ca90039}, {0x064,0x09240438},
 {0x084,0x80000004}, {0x0ac,0}, {0x0b0,0x8421},
 {0x0bc,0x3fd08}, {0x0c4,0x181b}, {0x0cc,1},
 {0x10c,0}, {0x15c,0}, {0x180,0}, {0x1b8,1u<<16},
 {0x2a8,0}, {0x2ac,0}, {0x2b4,0}, {0x2d8,0}, {0x330,0}
};
void SweetMDSS::configureHostForULPS(void)
{
 /* dsi_display_ctrl_setup before phy_idle_on. No soft reset here; the
  * display prepare soft reset belongs AFTER HS clocks and ULPS exit. */
 fHostTouched=fClockTouched=true;
 dp_wr(dp_dsi,0x110,0);
 for (unsigned n=0;n<sizeof(hostCommandProfile)/sizeof(hostCommandProfile[0]);++n) {
  const SweetHostRegister &r=hostCommandProfile[n];
  dp_wr(dp_dsi,r.offset,r.value);
  observeLineage("ulps-host-setup",dp_dsi,r.offset,r.value,~0u,true);
 }
 dp_wr(dp_dsi,0x11c,0x23f);dp_wr(dp_dsi,4,0x1f1);
}
bool SweetMDSS::setupHost(void)
{
 int mode=0; (void)PE_parse_boot_argn("sweetdsihost",&mode,sizeof(mode));
 if (!mode) return true;
 if (mode!=1 || !fManagedEnabled || fCoreProfile!=2) {
  SWEET_LOG_ERROR("SWEET MDSS HOST: host1 requires managed1/core2 and their prerequisites; refused\n");
  return false;
 }
 fHostProfile=1;
 setProperty("SweetDSIHostProfile",1ULL,32);
 setProperty("SweetDSIHostState",(unsigned long long)HostIdle,32);
 return true; /* No reset/configuration on startup/matched. */
}
bool SweetMDSS::hostBusy(void) const
{ return fHostPhase==HostResetHold || fHostPhase==HostConfigure; }
bool SweetMDSS::hostPreflight(void)
{
 /* Warm admission while inherited scanout may still run; this is not an
  * idle fence. The real beginHost repeats it after resource ACK/quiesce. */
 if (fHostProfile!=1 || (fHostPhase!=HostIdle &&
     !(fLifecycleLinkCycle && fLifecycle.phase==SweetPanelLinkRebuild && fHostPhase==HostDormant)) || dsiBusy() || fDsiPinned ||
     fDsiPoisoned || dp_rd(dp_dsi,0)!=0x20030000u || dp_rd(dp_dsi,0x15c) ||
     (dp_rd(dp_dsi,0x110)&(1u<<24)) || dp_rd(dp_dsi,0x68)) return false;
 observeLineage("host-prior-control",dp_dsi,4,fLinkPhase==LinkReady ? 0u : 5u,7u);
 return true;
}
bool SweetMDSS::hostResourcesReady(void)
{
 if (!fAccepting || isInactive() || !fRPMh || !fRPMhConsumer ||
     (fManagedState!=ManagedHost && !(fLifecycleLinkCycle && fLifecycle.phase==SweetPanelLinkRebuild && fManagedState==ManagedReady)) || fManagedRails!=15u ||
     !coreReady() || !localClocksReady() || fClockFault || fNoCFault ||
     clockBusy() || noCBusy() || dsiBusy() || fDsiPinned || fDsiPoisoned ||
     __atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE)!=IrqIdle) return false;
 if (fLinkProfile && !linkConfiguration()) return false;
 SweetClockLease clocks={}; SweetNoCStatus noc={}; SweetDisplayPowerStatus power={};
 if (!fManagedClock || localClockStatus(fManagedClock,&clocks)!=kIOReturnSuccess ||
     clocks.state!=SweetClockHeld || clocks.mask!=mdssClockMasks[SweetClockDSI0] ||
     !fManagedBandwidth || bandwidthStatus(fManagedBandwidth,&noc)!=kIOReturnSuccess ||
     noc.state!=SweetNoCHeld || !fManagedARC ||
     fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&power)!=kIOReturnSuccess ||
     power.state!=SweetRPMhComplete || power.corner!=SweetDisplayTurbo) return false;
 /* Do not inspect reset-domain host registers while reset is asserted. */
 return !(dp_rd(dp_intf1,0x288)&(1u<<20)) && !dp_rd(dp_intf1,0x2b4);
}
IOReturn SweetMDSS::beginHost(void)
{
 bool resume=fLifecycleLinkCycle && fLifecycle.phase==SweetPanelLinkRebuild && fHostPhase==HostDormant;
 if ((!resume && fHostPhase!=HostIdle) || !hostResourcesReady() || !hostPreflight() ||
     !clockCanGate() || !scanoutConfiguration(!resume) || (resume && !lifecycleLinkStepAllowed())) return kIOReturnNotReady;
 if (!setupDSI()) return kIOReturnNoResources;
 fHostDeadline=scanout_now()+20000000ULL;
 /* Claim/retire the existing writer ledger before the first effective write.
  * No second mapping, singleton or independent reset owner is introduced. */
 fHostTouched=true; fClockTouched=true;
 fHostLineageReset=resume && fLifecycleLineage;
 fHostSavedControl=dp_rd(dp_dsi,4);fHostSavedClocks=dp_rd(dp_dsi,0x11c);
 dp_wr(dp_dsi,0x110,0); /* disables only enables; zero never ACKs W1C status */
 observeLineage("host-irq-disable",dp_dsi,0x110,0,hostInterruptEnables,true);
 dp_wr(dp_dsi,4,dp_rd(dp_dsi,4)&~7u);
 observeLineage("host-disable",dp_dsi,4,0,7u,true);
 uint32_t force=fHostLineageReset ? fHostSavedClocks|hostForceClocks : hostForceClocks;
 dp_wr(dp_dsi,0x11c,force);
 observeLineage("host-force-clocks",dp_dsi,0x11c,force,~0u,true);
 dp_wr(dp_dsi,0x118,1);
 __asm__ volatile("dsb oshst" ::: "memory");
 fHostWait=scanout_now()+1000ULL; /* >=1us; timer may schedule later */
 fHostPhase=HostResetHold;
 setProperty("SweetDSIHostState",(unsigned long long)fHostPhase,32);
 fDsiTimer->setTimeoutUS(1);
 return kIOReturnSuccess;
}
bool SweetMDSS::hostConfiguration(void)
{
 if (dp_rd(dp_dsi,0)!=0x20030000u || (dp_rd(dp_dsi,8)&6u) ||
     (dp_rd(dp_dsi,0x110)&(1u<<24)) || dp_rd(dp_dsi,0x68)) return false;
 observeLineage("host-ready-reset",dp_dsi,0x118,0);
 observeLineage("host-ready-clocks",dp_dsi,0x11c,0x23fu);
 observeLineage("host-ready-irq",dp_dsi,0x110,0,hostInterruptEnables);
 for (unsigned n=0;n<sizeof(hostCommandProfile)/sizeof(hostCommandProfile[0]);++n) {
  const SweetHostRegister &r=hostCommandProfile[n];
  observeLineage("host-ready-config",dp_dsi,r.offset,r.offset==4 ? r.value|(fLifecycleLineage && !fDsiDisplayEngine ? 1u : 5u) : r.value);
 }
 return true; /* Real transfer completion remains independent. */
}
void SweetMDSS::advanceHost(void)
{
 if (!hostBusy()) return;
 uint64_t now=scanout_now();
 if (!hostResourcesReady()) { finishHost(kIOReturnNotReady,"resource/idle admission lost"); return; }
 if (now>=fHostDeadline) { finishHost(kIOReturnTimeout,"host rebuild deadline"); return; }
 if (now<fHostWait) { fDsiTimer->setTimeoutUS(1); return; }
 if (fHostPhase==HostResetHold) {
  dp_wr(dp_dsi,0x118,0);
  __asm__ volatile("dsb oshst" ::: "memory");
  observeLineage("host-reset-release",dp_dsi,0x118,0,~0u,true);
  if (dp_rd(dp_dsi,0)!=0x20030000u ||
      (dp_rd(dp_dsi,8)&6u) || (dp_rd(dp_dsi,0x110)&(1u<<24)) || dp_rd(dp_dsi,0x68)) {
   finishHost(kIOReturnIOError,"reset exit/idle/error"); return;
  }
  fHostPhase=HostConfigure;
  if (fHostLineageReset) {
   /* cmn_soft_reset restores control exactly. Command engine stays off
    * during DDIC reset; ON transfers acquire their own reference. */
   dp_wr(dp_dsi,0x11c,fHostSavedClocks);dp_wr(dp_dsi,4,fHostSavedControl);
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: host soft reset after ULPS exit; DDIC reset pending\n");
   fDsiTimer->setTimeoutUS(1);return;
  }
  /* Explicit target configuration, never replay an ABL host snapshot. */
  dp_wr(dp_dsi,0x11c,hostForceClocks);
  for (unsigned n=0;n<sizeof(hostCommandProfile)/sizeof(hostCommandProfile[0]);++n) {
   const SweetHostRegister &op=hostCommandProfile[n];
   dp_wr(dp_dsi,op.offset,op.value);
   observeLineage("host-config-write",dp_dsi,op.offset,op.value,~0u,true);
  }
  dp_wr(dp_dsi,0x110,0); /* no DSI IRQ owner yet, including ERROR mask */
  __asm__ volatile("dsb oshst" ::: "memory");
  dp_wr(dp_dsi,4,0x1f1); /* controller enabled under forced clocks */
  __asm__ volatile("dsb oshst" ::: "memory");
  dp_wr(dp_dsi,0x11c,0x23f);
  dp_wr(dp_dsi,4,0x1f5); /* command engine; VIDEO remains disabled */
  __asm__ volatile("dsb oshst" ::: "memory");
  setProperty("SweetDSIHostState",(unsigned long long)fHostPhase,32);
  fDsiTimer->setTimeoutUS(1); return; /* independent final observation */
 }
 if (!hostConfiguration()) { finishHost(kIOReturnIOError,"host idle/error validation"); return; }
 finishHost(kIOReturnSuccess,"configured");
}
void SweetMDSS::finishHost(IOReturn rc,const char *reason)
{
 if (fDsiTimer) fDsiTimer->cancelTimeout();
 fHostResult=rc; fHostPhase=rc==kIOReturnSuccess ? HostReady : HostFault;
 if (rc!=kIOReturnSuccess) {
  if (fHostTouched) fDsiPoisoned=true;
  SWEET_LOG_ERROR("SWEET MDSS HOST: FAILED %s rc=%#x touched=%u; partial host state/resources retained, no rollback/retry/PHY reset\n",
      reason,(unsigned)rc,fHostTouched);
 } else {
  SWEET_LOG_INFO("SWEET MDSS HOST: v2.3 soft-reset/rebuilt RGB888 1080x2340 VC0 lane-map8421 TE-ext; writes issued/idle observed, echo diagnostic; fresh pixels pending; link-profile=%u panel state retained\n",fLinkProfile);
 }
 setProperty("SweetDSIHostState",(unsigned long long)fHostPhase,32);
 setProperty("SweetDSIHostFault",rc!=kIOReturnSuccess);
}
void SweetMDSS::stopHost(void)
{
 if (!hostBusy()) return;
 if (fDsiTimer) fDsiTimer->cancelTimeout();
 fHostPhase=HostStopped; fHostResult=kIOReturnAborted;
 if (fHostTouched) fDsiPoisoned=true;
 setProperty("SweetDSIHostState",(unsigned long long)fHostPhase,32);
 /* No late MMIO, reset release or power withdrawal after admission closes. */
}

IOReturn SweetMDSS::disableHostForLink(void)
{
 if (!fLifecycleLinkCycle || fLifecycle.phase!=SweetPanelLinkPowerDown || fHostPhase!=HostReady ||
     !lifecycleLinkStepAllowed() || (fLifecycleLineage && (fDsiDisplayEngine || fDsiTransferEngine))) return kIOReturnNotReady;
 observeLineage("host-stop-prior-reset",dp_dsi,0x118,0);
 observeLineage("host-stop-prior-phy-reset",dp_dsi,0x12c,0);
 uint32_t clocks=dp_rd(dp_dsi,0x11c),ctrl=dp_rd(dp_dsi,4);
 observeLineage("host-stop-prior-clocks",dp_dsi,0x11c,0x23fu);
 observeLineage("host-stop-prior-control",dp_dsi,4,fLifecycleLineage ? 1u : 5u,7u);
 fHostTouched=fClockTouched=true;
 dp_wr(dp_dsi,0x110,0); /* disable enables; never W1C stale status by replay */
 observeLineage("host-stop-irq",dp_dsi,0x110,0,hostInterruptEnables,true);
 if (dp_rd(dp_dsi,0x110)&(1u<<24)) {finishHost(kIOReturnIOError,"host shutdown error");return kIOReturnIOError;}
 dp_wr(dp_dsi,4,ctrl&~4u); /* command engine before controller */
 observeLineage("host-stop-command",dp_dsi,4,ctrl&~4u,~0u,true);
 dp_wr(dp_dsi,0x11c,clocks|hostForceClocks);
 __asm__ volatile("dsb oshst" ::: "memory");
 observeLineage("host-stop-force-clocks",dp_dsi,0x11c,clocks|hostForceClocks,~0u,true);
 dp_wr(dp_dsi,4,ctrl&~7u);
 __asm__ volatile("dsb oshst" ::: "memory");
 observeLineage("host-stop-controller",dp_dsi,4,ctrl&~7u,~0u,true);
 dp_wr(dp_dsi,0x11c,clocks);
 __asm__ volatile("dsb oshst" ::: "memory");
 observeLineage("host-stop-clocks",dp_dsi,0x11c,clocks,~0u,true);
 if (dp_rd(dp_dsi,8)&6u) {finishHost(kIOReturnIOError,"host shutdown busy");return kIOReturnIOError;}
 fHostPhase=HostDormant;fHostResult=kIOReturnSuccess;
 setProperty("SweetDSIHostState",(unsigned long long)fHostPhase,32);
 return kIOReturnSuccess;
}
