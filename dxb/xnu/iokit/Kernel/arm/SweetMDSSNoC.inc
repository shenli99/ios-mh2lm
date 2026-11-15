/* Display-data RSC lives with the actual MDSS clock/GDSC owner. Config CN0
 * stays in SweetRPMh/apps-DRV. No reverse synchronous provider callback. */
static unsigned mdssNoCWriter; /* Ownership state only, never an instance. */
enum { NIdle, NAppsRaise, NClock, NData, NClockRelease, NAppsLower, NRSC };
enum { NStride=0x2a0, NCommandStride=0x14, NIRQEnable=0, NIRQStatus=4, NIRQClear=8,
 NWait=0x10, NControl=0x14, NStatus=0x18, NEnable=0x1c, NMessage=0x30,
 NAddress=0x34, NDataRegister=0x38, NCommandStatus=0x3c,
 NAMC=1<<16, NTrigger=1<<24, NIssued=1<<8, NComplete=1<<16 };
struct SweetNoCRequest {
 unsigned operation; uint64_t token; const SweetDisplayBandwidth *vote;
 uint64_t *output; SweetNoCStatus *status; bool lifecycleData;
};

/* LG G8X display RSC: DTS SPI129 -> GIC INTID161, level high. The
 * permanent dispatch lock fences event/map destruction against the top half. */
extern "C" bool sweet_mdss_noc_irq(unsigned int intid);
static IOSimpleLock *mdssNoCIRQDispatch;
static SweetMDSS *mdssNoCIRQService;
static IOSimpleLock *mdss_noc_irq_lock(void)
{
 IOSimpleLock *lock=__atomic_load_n(&mdssNoCIRQDispatch,__ATOMIC_ACQUIRE);
 if (!lock) {
  IOSimpleLock *candidate=IOSimpleLockAlloc();
  if (!candidate) return NULL;
  if (__atomic_compare_exchange_n(&mdssNoCIRQDispatch,&lock,candidate,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) lock=candidate;
  else IOSimpleLockFree(candidate);
 }
 return lock;
}
bool SweetMDSS::setupNoCIRQ(IOService *provider)
{
 OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,display-noc-irq-abi"));
 OSData *intid=OSDynamicCast(OSData,provider->getProperty("sweet,display-noc-intid"));
 const uint32_t one=1, expected=161;
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),&one,4) ||
     !intid || intid->getLength()!=4 || memcmp(intid->getBytesNoCopy(),&expected,4)) return false;
 if (!fMaps[11]) fMaps[11]=provider->mapDeviceMemoryWithIndex(11);
 if (!fMaps[11] || fMaps[11]->getPhysicalAddress()!=0x17a00000 || fMaps[11]->getLength()!=0x10000) return false;
 vm_offset_t gic=fMaps[11]->getVirtualAddress();
 if ((dp_rd(gic,0xffe8)&0xf0u)!=0x30u || ((dp_rd(gic,4)&31u)+1u)*32u<=161u ||
     !(dp_rd(gic,0)&2u) || ((dp_rd(gic,0x114)|dp_rd(gic,0x214)|dp_rd(gic,0x314))&2u)) return false;
 fNoCIRQEvent=IOInterruptEventSource::interruptEventSource(this,noCIRQAction);
 if (!fNoCIRQEvent || fLoop->addEventSource(fNoCIRQEvent)!=kIOReturnSuccess) return false;
 fNoCIRQEventAdded=true;
 IOSimpleLock *lock=mdss_noc_irq_lock();
 if (!lock) return false;
 IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
 if (!mdssNoCIRQService) { mdssNoCIRQService=this; fNoCIRQClaimed=true; }
 IOSimpleLockUnlockEnableInterrupt(lock,saved);
 return fNoCIRQClaimed;
}
IOReturn SweetMDSS::prepareNoCIRQ(void)
{
 vm_offset_t gic=fMaps[11]->getVirtualAddress();
 if (!fNoCIRQTouched) {
  uint32_t irq=dp_rd(fNoCTCSBase,NIRQStatus), enabled=dp_rd(fNoCTCSBase,NIRQEnable);
  if (irq==~0u || enabled==~0u || (irq&enabled) ||
      ((dp_rd(gic,0x114)|dp_rd(gic,0x214)|dp_rd(gic,0x314))&2u)) return kIOReturnBusy;
  fNoCIRQTouched=true;
  dp_wr(gic,0x194,2);
  dp_wr(gic,0x94,dp_rd(gic,0x94)|2u);
  dp_wr(gic,0xc28,dp_rd(gic,0xc28)&~0xcu);
  dp_wr(gic,0x4a0,(dp_rd(gic,0x4a0)&~0xff00u)|0xa000u);
  dp_wr64(gic,0x6508,0);
  fNoCIRQDeadline=scanout_now()+1000000ULL;
 }
 if (!fNoCIRQConfigured && (dp_rd(gic,0)&(1u<<31)))
  return scanout_now()<fNoCIRQDeadline ? kIOReturnNotReady : kIOReturnIOError;
 if (!fNoCIRQConfigured) {
  observeLineage("gic-trigger",gic,0xc28,0,0xcu,true);
  observeLineage("gic-priority",gic,0x4a0,0xa000u,0xff00u,true);
  observeLineage("gic-route-lo",gic,0x6508,0,~0u,true);
  observeLineage("gic-route-hi",gic,0x650c,0,~0u,true);
 }
 if (!fNoCIRQConfigured) {
  fNoCIRQConfigured=true; __atomic_store_n(&fNoCIRQOnline,1u,__ATOMIC_RELEASE);
  SWEET_LOG_INFO("SWEET MDSS NOC IRQ: INTID161 configured; workloop completion, timer watchdog only\n");
 }
 return kIOReturnSuccess;
}
bool SweetMDSS::handleNoCIRQ(void)
{
 /* Dispatch lock held; no peripheral access, workloop wait or ACK here. */
 if (!__atomic_load_n(&fNoCIRQOnline,__ATOMIC_ACQUIRE)) return true;
 dp_wr(fMaps[11]->getVirtualAddress(),0x194,2);
 __atomic_add_fetch(&fNoCIRQEntries,1u,__ATOMIC_RELAXED);
 if (!__atomic_exchange_n(&fNoCIRQArmed,0u,__ATOMIC_ACQ_REL)) return true;
 __atomic_store_n(&fNoCIRQPosted,1u,__ATOMIC_RELEASE);
 fNoCIRQEvent->interruptOccurred(NULL,NULL,0);
 return true;
}
extern "C" bool sweet_mdss_noc_irq(unsigned int intid)
{
 if (intid!=161u) return false;
 IOSimpleLock *lock=__atomic_load_n(&mdssNoCIRQDispatch,__ATOMIC_ACQUIRE);
 if (lock) {
  IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
  if (mdssNoCIRQService) (void)mdssNoCIRQService->handleNoCIRQ();
  IOSimpleLockUnlockEnableInterrupt(lock,saved);
 }
 return true;
}
void SweetMDSS::noCIRQAction(OSObject *owner,IOInterruptEventSource *,int)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!__atomic_exchange_n(&s->fNoCIRQPosted,0u,__ATOMIC_ACQ_REL) || !s->fAccepting || s->isInactive()) return;
 if (!s->noCBusy()) return;
 /* Completion follows the same resource participant checks as timer work. */
 if (s->fLifecycleEnabled && s->lifecycleBusy() && !s->lifecycleResourceStepAllowed()) {
  s->finishLifecycle(kIOReturnNotReady,"resource participant lost"); return;
 }
 if (s->fNoCPhase!=NData || !s->fNoCExecuting || s->fNoCOps || !s->noCAdmission()) {
  s->finishNoC(kIOReturnNotReady,true,"irq-owner"); return;
 }
 uint32_t irq=s->dp_rd(s->fNoCTCSBase,NIRQStatus);
 uint32_t enabled=s->dp_rd(s->fNoCTCSBase,NIRQEnable), bit=1u<<s->fNoCTCS;
 if (irq==~0u || enabled==~0u || !(irq&bit)) {
  s->finishNoC(kIOReturnIOError,true,"irq-source"); return;
 }
 s->observeLineageValue("noc-irq-enable",NIRQEnable,bit,enabled,bit,false);
 s->fNoCIRQObserved=true;
 s->advanceNoC();
}
void SweetMDSS::quietNoCIRQ(void)
{
 __atomic_store_n(&fNoCIRQArmed,0u,__ATOMIC_RELEASE);
 /* GIC is independent of the display domain. Never read/clear a possibly
  * inaccessible RSC source on failure/stop; retain its outstanding payload. */
 if (fNoCIRQTouched) dp_wr(fMaps[11]->getVirtualAddress(),0x194,2);
}
void SweetMDSS::stopNoCIRQ(void)
{
 __atomic_store_n(&fNoCIRQOnline,0u,__ATOMIC_RELEASE);
 if (fNoCIRQClaimed) {
  IOSimpleLock *lock=__atomic_load_n(&mdssNoCIRQDispatch,__ATOMIC_ACQUIRE);
  IOInterruptState saved=IOSimpleLockLockDisableInterrupt(lock);
  if (mdssNoCIRQService==this) mdssNoCIRQService=NULL;
  fNoCIRQClaimed=false;
  IOSimpleLockUnlockEnableInterrupt(lock,saved);
 }
 if (fNoCIRQEventAdded) { fNoCIRQEvent->disable(); fLoop->removeEventSource(fNoCIRQEvent); fNoCIRQEventAdded=false; }
}
void SweetMDSS::waitNoCCompletion(void)
{
 uint64_t now=scanout_now(), deadline=fNoCStepDeadline<fNoCDeadline ? fNoCStepDeadline : fNoCDeadline;
 fNoCTimer->setTimeoutUS((unsigned)(deadline>now ? (deadline-now+999)/1000 : 1));
}

/* Register facts and ordering from LG/Lineage sde_rsc_hw.c v2 init and
 * sde_rsc.c timer_calculate(NULL, IDLE). Defaults clamp prefill 25 to 35
 * with vtotal 505, 60 Hz and XO period 52 ns. All times use the source's
 * integer division order, including the PDC backoff's different unit. */
struct MDSSRSCInit { uint32_t wrapper, offset, value; };
static const MDSSRSCInit mdssRSCV2Init[]={
 /* rsc_hw_qtimer_init */
 {1,0x1fe0,0xffffffffu},
 {1,0x1ff0,0xffffffffu},
 {1,0x1040,0x00000001u},
 {1,0x1044,0x00000001u},
 {1,0x2020,0xffffffffu},
 {1,0x2024,0xffffffffu},
 {1,0x3020,0xffffffffu},
 {1,0x3024,0xffffffffu},
 {1,0x202c,0x00000001u},
 {1,0x302c,0x00000001u},
 /* rsc_hw_wrapper_init */
 {1,0x0008,0x00048d3bu},
 {1,0x000c,0x00004612u},
 {1,0x0004,0x00000100u},
 /* rsc_hw_seq_memory_init_v2 */
 {0,0x0600,0xe0bb9ebeu},
 {0,0x0604,0x9ebeff39u},
 {0,0x0608,0x2020209bu},
 {0,0x060c,0x38bb9ebeu},
 {0,0x0610,0xbeff39e0u},
 {0,0x0614,0x20209b9eu},
 {0,0x0618,0xb9bae5a0u},
 {0,0x061c,0xbdbbf9fau},
 {0,0x0620,0x38999afeu},
 {0,0x0624,0xac81e1a1u},
 {0,0x0628,0x82e2a2e0u},
 {0,0x062c,0x8cfd9d39u},
 {0,0x0630,0xbc20209bu},
 {0,0x0634,0xe601a6fcu},
 {0,0x0638,0xbc20209cu},
 {0,0x063c,0xe701a7fcu},
 {0,0x0640,0x0000209cu},
 {0,0x0410,0x00000033u},
 {0,0x0414,0x0000003bu},
 {0,0x0c14,0x00000000u},
 {0,0x0c30,0x00000000u},
 {0,0x0c4c,0x0000000cu},
 {0,0x0c68,0x00000018u},
 /* rsc_hw_solver_init */
 {0,0x0c04,0xffffffffu},
 {0,0x0c08,0xffffffffu},
 {0,0x0c0c,0xefffffffu},
 {0,0x1000,0x00000000u},
 {0,0x1004,0x00008bbbu},
 {0,0x1008,0x0004e400u},
 {0,0x100c,0xffffffffu},
 {0,0x0c20,0x00000007u},
 {0,0x1080,0x00000000u},
 {0,0x1100,0x00000001u},
 {0,0x110c,0x00000001u},
 {0,0x1180,0x00000002u},
 {0,0x118c,0x00000002u},
 {0,0x0c18,0x00000000u},
 {0,0x0c00,0x00000001u},
 {0,0x0c1c,0x01000010u},
 {0,0x0c34,0x80000000u},
 {0,0x0c38,0x000045deu},
 {0,0x0c40,0x0002d6a4u},
 {0,0x0c50,0x80000000u},
 {0,0x0c54,0x000045deu},
 {0,0x0c5c,0x0002d6a4u},
 {0,0x0c6c,0x80000000u},
 {0,0x0c70,0x00000000u},
 {0,0x0c78,0x0002d6a4u},
 /* rsc_hw_pdc_init */
 {0,0x0020,0x00004520u},
 {0,0x0024,0x00004510u},
 {0,0x0028,0x00004514u},
 {0,0x002c,0x00000001u},
};
IOReturn SweetMDSS::initNoCRSC(void)
{
 while (fNoCRSCInitCursor<sizeof(mdssRSCV2Init)/sizeof(mdssRSCV2Init[0])) {
  const MDSSRSCInit &op=mdssRSCV2Init[fNoCRSCInitCursor];
  vm_offset_t base=op.wrapper ? fNoCWrapper : fNoCRSC;
  /* sde_rsc_hw_init's five helpers use dss_reg_w, not echo polling. */
  dp_wr(base,op.offset,op.value);
  observeLineage(op.wrapper ? "rsc-wrapper-init" : "rsc-drv-init",base,op.offset,op.value,~0u,true);
  if (op.wrapper && (op.offset==0x2020 || op.offset==0x2024 || op.offset==0x202c ||
                     op.offset==0x3020 || op.offset==0x3024 || op.offset==0x302c))
   SWEET_LOG_INFO("SWEET MDSS RSC: qtimer write issued offset=%#x value=%#x; observation above, not hardware ACK\n",op.offset,op.value);
  fNoCRSCWriteIssued=false; ++fNoCRSCInitCursor;
 }
 fNoCRSCInitialized=true;
 SWEET_LOG_INFO("SWEET MDSS RSC: v2 qtimer/wrapper/sequencer/solver/PDC initialized; CLK/AMC path; write sequence issued, not timer hardware ACK\n");
 return kIOReturnSuccess;
}
IOReturn SweetMDSS::takeNoCRSC(void)
{
 if (!noCAdmission(true)) return kIOReturnNotPermitted;
 uint64_t now=scanout_now();
 if (!fNoCRSCStage) {
  /* RSC init must not replace sequences used by inherited ACTIVE traffic.
   * Sleep/wake payloads remain untouched and solver stays disabled. */
  uint32_t irq=dp_rd(fNoCTCSBase,NIRQStatus), enabled=dp_rd(fNoCTCSBase,NIRQEnable);
  if (irq==~0u || enabled==~0u || (irq&enabled)) return kIOReturnBusy;
  for (unsigned t=2;t<4;++t) {
   uint32_t b=t*NStride, idle=dp_rd(fNoCTCSBase,b+NStatus);
   if (!idle || idle==~0u || (!(fNoCSlotsOwned&(1u<<t)) && dp_rd(fNoCTCSBase,b+NEnable))) return kIOReturnBusy;
  }
  uint32_t override=dp_rd(fNoCWrapper,4);
  uint32_t solverOverride=dp_rd(fNoCRSC,0xc14), modes=dp_rd(fNoCRSC,0xc20);
  uint32_t status0=dp_rd(fNoCRSC,0xc24), status1=dp_rd(fNoCRSC,0xc28), status2=dp_rd(fNoCRSC,0xc2c);
  SWEET_LOG_INFO("SWEET MDSS RSC: enter CLK override=%#x solver-override=%#x modes=%#x status0=%#x status1=%#x status2=%#x\n",
      override,solverOverride,modes,status0,status1,status2);
  /* Status2 is the last selected mode, not an all-zero idle predicate. */
  if (override==~0u || solverOverride==~0u || modes==~0u || status0==~0u || status1==~0u || status2==~0u) return kIOReturnIOError;
  fNoCTouched=true;
  dp_wr(fNoCWrapper,4,override&~1u); /* sde_rsc_state_update(CLK) + dp_wr barrier */
  fNoCRSCDeadline=now+1000000ULL; fNoCRSCStage=1;
 }
 if (fNoCRSCStage==1) {
  observeLineage("rsc-clk-request",fNoCWrapper,4,0,1u,true);
  dp_wr(fNoCWrapper,0,0); /* tcs_wait: withhold TCS_USE_OK before checking SEQ_BUSY */
  if (dp_rd(fNoCWrapper,0)&2u) {
   dp_wr(fNoCWrapper,0,1);
   fNoCRSCRetryAt=now+200000ULL; fNoCRSCStage=2;
   return kIOReturnNotReady;
  }
  fNoCRSCDeadline=now+1000000ULL; fNoCRSCStage=3;
 }
 if (fNoCRSCStage==2) {
  if (now<fNoCRSCRetryAt) return kIOReturnNotReady;
  dp_wr(fNoCWrapper,0,0);
  fNoCRSCDeadline=now+1000000ULL; fNoCRSCStage=3;
 }
 if (fNoCRSCStage==3) {
  uint32_t ctrl=dp_rd(fNoCWrapper,0), busy=dp_rd(fNoCRSC,0x404);
  if (ctrl==~0u || busy==~0u) return kIOReturnIOError;
  if ((ctrl&2u) || busy) {
   if (now<fNoCRSCDeadline) return kIOReturnNotReady;
   /* Like Lineage trigger_vote failure: no payload/init has been written
    * during this handoff, so return TCS_USE_OK to the sequencer. */
   dp_wr(fNoCWrapper,0,1);
   SWEET_LOG_ERROR("SWEET MDSS RSC: TCS handoff timeout ctrl=%#x seq-busy=%#x\n",ctrl,busy);
   return kIOReturnTimeout;
  }
  observeLineageValue("rsc-tcs-use-ok",0,0,ctrl,1u,true);
  fNoCTCSOwned=true; fNoCRSCStage=4;
 }
 if (fNoCRSCStage==4) {
  if (!noCAdmission()) return kIOReturnNotPermitted;
  if (!fNoCRSCInitialized) {
   IOReturn result=initNoCRSC();
   if (result!=kIOReturnSuccess) return result;
  }
  fNoCRSCStage=5;
  SWEET_LOG_INFO("SWEET MDSS RSC: CLK state and TCS ownership ready\n");
 }
 return noCAdmission() ? kIOReturnSuccess : kIOReturnNotPermitted;
}

bool SweetMDSS::setupNoC(IOService *provider)
{
 OSObject *property=provider->getProperty("sweet,display-noc-abi");
 if (!property) return true;
 OSData *abi=OSDynamicCast(OSData,property);
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4) || fDisplayResourceMode!=2 || !fClockEnabled)
  return false;
 const uint64_t pa[]={0x0af20000,0x0af21c00,0x0af30000}, size[]={0x1c00,0x3000,0x3fd4};
 for (unsigned n=0;n<3;++n) {
  fMaps[13+n]=provider->mapDeviceMemoryWithIndex(13+n);
  if (!fMaps[13+n] || fMaps[13+n]->getPhysicalAddress()!=pa[n] || fMaps[13+n]->getLength()!=size[n]) return false;
 }
 fNoCRSC=fMaps[13]->getVirtualAddress(); fNoCTCSBase=fMaps[14]->getVirtualAddress();
 fNoCWrapper=fMaps[15]->getVirtualAddress();
 fNoCTimer=IOTimerEventSource::timerEventSource(this,noCTimerAction);
 if (!fNoCTimer || fLoop->addEventSource(fNoCTimer)!=kIOReturnSuccess) return false;
 fNoCTimerAdded=true;
 if (!setupNoCIRQ(provider)) return false;
 fNoCEnabled=true;
 setProperty("SweetDisplayNoCOwnership","MDSS display RSC; RPMh apps CN0; explicit warm requests only");
 return true; /* Mapping never reads RSC, enables clocks or sends a vote. */
}
bool SweetMDSS::noCBusy(void) const { return fNoCPhase!=NIdle; }
bool SweetMDSS::noCClockDiagnostics(void) const { return fNoCPhase==NClock; }
bool SweetMDSS::noCCanReleaseClock(void) const
{ return fNoCPhase==NData && !fNoCTarget.registerPeak && fNoCCursor==fNoCCount; }
bool SweetMDSS::noCClocksReady(void)
{
 if (!fAccepting || isInactive() || fClockFault || !fNoCClockToken) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-service accepting=%u inactive=%u fault=%u clock-token=%llu\n",
      (unsigned)fAccepting,(unsigned)isInactive(),(unsigned)fClockFault,fNoCClockToken);
  return false;
 }
 if (!clockPrerequisites(ClockFailure)) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-prerequisites\n"); return false;
 }
 SweetClockLease lease={};
 IOReturn rc=clockStatusAction(this,&fNoCClockToken,&lease,NULL,NULL);
 if (rc!=kIOReturnSuccess || lease.state!=SweetClockHeld || lease.mask!=mdssClockMasks[SweetClockRSC]) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-lease rc=%#x token=%llu state=%u mask=%#x expected=%#x\n",
      (unsigned)rc,fNoCClockToken,lease.state,lease.mask,mdssClockMasks[SweetClockRSC]);
  return false;
 }
 for (unsigned i=0;i<SweetClockCount;++i) if (lease.mask&(1u<<i)) {
  uint32_t parent[6]={}, v=dp_rd(dp_dispcc,mdssClockBranches[i].cbcr);
  if (!clockParent(i,parent,true,ClockFailure)) {
   SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-parent branch=%u cbcr=%#x\n",i,v); return false;
  }
  observeClockParent(i,parent,fClockParents[i]);
  if ((fClockOwnedMask&(1u<<i)) && !clockEnableParentReady(i,parent)) {
   SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-owned-parent branch=%u cbcr=%#x\n",i,v); return false;
  }
  if (!clockBranchRunning(i,v)) {
   SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-branch branch=%u offset=%#x cbcr=%#x\n",
       i,mdssClockBranches[i].cbcr,v); return false;
  }
 }
 return true;
}
bool SweetMDSS::noCAdmission(bool takeover)
{
 if (!noCClocksReady()) return false; /* No RSC access before clock/domain lease. */
 uint32_t id=dp_rd(fNoCRSC,0), config=dp_rd(fNoCRSC,12);
 unsigned major=(id>>16)&255, count=config&63u;
 fNoCNCPT=(config>>27)&31u;
 if ((major!=1 && major!=2) || count<4 || count>0x3000/NStride || !fNoCNCPT || fNoCNCPT>16) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=rsc-layout id=%#x config=%#x major=%u count=%u ncpt=%u\n",
      id,config,major,count,fNoCNCPT); return false;
 }
 uint32_t override=dp_rd(fNoCWrapper,4), power=dp_rd(fNoCWrapper,0x24), ctrl=dp_rd(fNoCWrapper,0);
 /* Preserve the original read/check order. These are the samples used by
  * the failed gate, not a post-failure rescan of RSC/TCS/wrapper. */
 if (override==~0u) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=wrapper-override override=%#x power=%#x ctrl=%#x\n",override,power,ctrl); return false;
 }
 if (power==~0u || (power&(1u<<12))) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=wrapper-power override=%#x power=%#x ctrl=%#x\n",override,power,ctrl); return false;
 }
 if (ctrl==~0u || (!takeover && (ctrl&2u))) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=wrapper-control override=%#x power=%#x ctrl=%#x\n",override,power,ctrl); return false;
 }
 uint32_t busy=dp_rd(fNoCRSC,0x404);
 if (busy==~0u || (!takeover && busy)) {
  SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=sequencer-busy value=%#x\n",busy); return false;
 }
 observeLineageValue("rsc-clk-override",4,0,override,1u,false);
 uint32_t error=dp_rd(fNoCRSC,0xd0);
 if (error) observeLineageValue("rsc-error-status",0xd0,0,error,~0u,false);
 return true;
}

IOReturn SweetMDSS::retainBandwidth(const SweetDisplayBandwidth &vote,uint64_t *token)
{
 if (!token) return kIOReturnBadArgument;
 *token=0; SweetNoCRequest r={}; r.vote=&vote; r.output=token;
 return fGate ? fGate->runAction(noCAction,&r) : kIOReturnNotReady;
}
IOReturn SweetMDSS::releaseBandwidth(uint64_t token)
{
 SweetNoCRequest r={}; r.operation=1; r.token=token;
 return fGate ? fGate->runAction(noCAction,&r) : kIOReturnNotReady;
}
IOReturn SweetMDSS::bandwidthStatus(uint64_t token,SweetNoCStatus *status)
{
 if (!status) return kIOReturnBadArgument;
 *status={}; SweetNoCRequest r={}; r.operation=2; r.token=token; r.status=status;
 return fGate ? fGate->runAction(noCAction,&r) : kIOReturnNotReady;
}
IOReturn SweetMDSS::noCAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner; SweetNoCRequest &r=*(SweetNoCRequest *)arg;
 unsigned slot=4;
 if (r.operation) {
  for (unsigned n=0;n<4;++n) if (r.token && s->fNoCLeases[n].status.token==r.token) { slot=n; break; }
  if (slot==4) return kIOReturnNotFound;
  if (r.operation==2) { *r.status=s->fNoCLeases[slot].status; return kIOReturnSuccess; }
 } else {
  for (unsigned n=0;n<4;++n) if (s->fNoCLeases[n].status.state==SweetNoCEmpty ||
      s->fNoCLeases[n].status.state==SweetNoCReleased) { slot=n; break; }
  if (slot==4 || s->fNoCNextToken==~0ULL) return kIOReturnNoResources;
 }
 /* A sleeping link does not grant public clients access to its vote ledger.
  * Only the private coordinator request may change its exact DATA lease,
  * after revalidating the config floor, boot/RSC clocks and physical fence. */
 if (s->fLinkSuspendedMask && (!r.lifecycleData || !s->fLifecycleLinkCycle || !s->fLifecycleDataParking ||
     !s->lifecycleResourceStepAllowed() || s->fLinkPhase!=LinkDormant || s->fHostPhase!=HostDormant ||
     !s->managedConfigHeld() || s->fManagedClock || s->fManagedToken ||
     (r.operation ? (r.token!=s->fManagedBandwidth || s->fManagedState!=ManagedParkNoC ||
       s->fLifecycle.phase!=SweetPanelResourcePark) :
      (s->fManagedState!=ManagedRestoreNoC || s->fManagedBandwidth || r.output!=&s->fManagedBandwidth ||
       s->fLifecycle.phase!=SweetPanelResourceRestore)))) return kIOReturnNotReady;
 if (!s->fNoCEnabled || !s->fAccepting || s->isInactive() || s->fNoCFault || s->fLinkPhase==LinkFault || s->fLinkPhase==LinkStopped ||
     (s->fHostTouched && (s->fHostPhase==HostFault || s->fHostPhase==HostStopped))) return kIOReturnNotReady;
 if (s->noCBusy() || s->clockBusy() || s->linkBusy() || s->hostBusy() || s->dsiBusy()) return kIOReturnBusy;
 if (r.operation && !s->fNoCLeases[slot].active) return kIOReturnBadArgument;
 SweetDisplayBandwidth empty={};
 IOReturn result=s->beginBandwidth(slot,r.operation ? empty : *r.vote,r.operation!=0);
 if (result==kIOReturnSuccess && r.output) *r.output=s->fNoCLeases[slot].status.token;
 return result;
}

bool SweetMDSS::noCPlan(const SweetDisplayBandwidth &v)
{
 if (v.port[0].average>~0ULL-v.port[1].average) return false;
 uint64_t average=v.port[0].average+v.port[1].average;
 uint64_t peak=v.port[0].peak>v.port[1].peak ? v.port[0].peak : v.port[1].peak;
 unsigned x=0,y=0,a=0,b=0;
 if (!fNoCInfo[SweetBCMMM1].scale(v.port[0].average,32,1,&x) ||
     !fNoCInfo[SweetBCMMM1].scale(v.port[1].average,32,1,&a) ||
     !fNoCInfo[SweetBCMMM1].scale(v.port[0].peak,32,1,&y) ||
     !fNoCInfo[SweetBCMMM1].scale(v.port[1].peak,32,1,&b)) return false;
 if (a>x) x=a;
 if (b>y) y=b;
 fNoCValues[SweetBCMMM1]=(x||y ? 1u<<29 : 0u)|(x<<14)|y;
 for (unsigned r=SweetBCMMM0;r<SweetDisplayBCMCount;++r) {
  unsigned width=r==SweetBCMMM0 ? 32 : (r==SweetBCMSH0 ? 16 : 4);
  unsigned ports=r==SweetBCMMM0 ? 2 : 4;
  SweetBandwidth bandwidth=r==SweetBCMMM0 ? SweetBandwidth{average,peak} :
      (r==SweetBCMSH0 ? v.llcc : v.ebi);
  /* Ordinary BCMs retain bandwidth scaling; ACV's typed resource policy
   * encodes the aggregate EBI demand as an enable mask, including width=0. */
  if (!fNoCInfo[r].encode(bandwidth.average,bandwidth.peak,width,ports,&fNoCValues[r])) return false;
 }
 fNoCCount=0;
 for (unsigned r=1;r<SweetDisplayBCMCount;++r)
  if (((fNoCOwned&(1u<<r)) && fNoCApplied[r]!=fNoCValues[r]) ||
      (!(fNoCOwned&(1u<<r)) && fNoCValues[r])) fNoCOrder[fNoCCount++]=r;
 for (unsigned i=1;i<fNoCCount;++i) {
  unsigned value=fNoCOrder[i], j=i;
  while (j && fNoCInfo[fNoCOrder[j-1]].vcd>fNoCInfo[value].vcd) {
   fNoCOrder[j]=fNoCOrder[j-1]; --j;
  }
  fNoCOrder[j]=value;
 }
 return true;
}

IOReturn SweetMDSS::beginBandwidth(unsigned slot,const SweetDisplayBandwidth &proposal,bool release)
{
 if (!fRPMh || !fRPMhConsumer) return kIOReturnNotReady;
 if ((!fNoCAggregate.registerPeak || release) && !clockCanGate()) return kIOReturnBusy;
 if (!clockPrerequisites()) return kIOReturnNotReady; /* Warm only; no cold fallback. */
 if (!release) {
  /* A config-only lease keeps CN0/RSC reachable while data leases are
   * withdrawn. Mixed/incomplete data paths remain invalid. */
  bool configOnly=!proposal.port[0].peak && !proposal.port[1].peak && !proposal.llcc.peak && !proposal.ebi.peak;
  bool dataPath=(proposal.port[0].peak || proposal.port[1].peak) && proposal.llcc.peak && proposal.ebi.peak;
  if (proposal.registerPeak<76800000ULL || (!configOnly && !dataPath)) return kIOReturnBadArgument;
  for (unsigned i=0;i<4;++i) {
   const SweetBandwidth &v=i<2 ? proposal.port[i] : (i==2 ? proposal.llcc : proposal.ebi);
   if (v.average>v.peak) return kIOReturnBadArgument;
  }
 }
 SweetDisplayBandwidth target={};
 for (unsigned n=0;n<4;++n) {
  const SweetDisplayBandwidth *v=NULL;
  if (n==slot) { if (!release) v=&proposal; }
  else if (fNoCLeases[n].active) v=&fNoCLeases[n].vote;
  if (!v) continue;
  for (unsigned i=0;i<4;++i) {
   const SweetBandwidth &from=i<2 ? v->port[i] : (i==2 ? v->llcc : v->ebi);
   SweetBandwidth &to=i<2 ? target.port[i] : (i==2 ? target.llcc : target.ebi);
   if (to.average>~0ULL-from.average) return kIOReturnBadArgument;
   to.average+=from.average; if (to.peak<from.peak) to.peak=from.peak;
  }
  if (target.registerPeak<v->registerPeak) target.registerPeak=v->registerPeak;
 }
 for (unsigned r=0;r<SweetDisplayBCMCount;++r)
  if (fRPMh->copyBCMInfo((SweetDisplayBCM)r,&fNoCInfo[r])!=kIOReturnSuccess) return kIOReturnNotReady;
 unsigned check=0;
 if (!fNoCInfo[0].scale(target.registerPeak,4,1,&check) || !noCPlan(target)) return kIOReturnBadArgument;
 if (!fNoCClaimed) {
  unsigned expected=0;
  if (!__atomic_compare_exchange_n(&mdssNoCWriter,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return kIOReturnNotReady;
  fNoCClaimed=true;
 }
 fNoCSlot=slot; fNoCTarget=target; fNoCReleasing=release;
 fNoCCursor=0; fNoCExecuting=false; fNoCAppsToken=0;
 fNoCRSCStage=0; fNoCRSCWriteIssued=false;
 fNoCAppsLower=target.registerPeak<fNoCAggregate.registerPeak;
 NoCLease &lease=fNoCLeases[slot];
 if (!release) { lease={}; lease.vote=proposal; lease.status.token=++fNoCNextToken; }
 lease.status.state=release ? SweetNoCReleasing : SweetNoCPending;
 lease.status.acknowledged=0; lease.status.registerAcknowledged=false; lease.status.uncertain=false;
 fNoCPhase=NAppsRaise; fNoCDeadline=scanout_now()+1500000000ULL;
 fNoCEvidence={}; fNoCEvidence.requestAt=fNoCDeadline-1500000000ULL;
 fNoCTimer->setTimeoutUS(1); return kIOReturnSuccess;
}
void SweetMDSS::noCAdd(uint32_t offset,uint32_t value,bool clear)
{ fNoCOperations[fNoCOps++]={offset,value,clear}; }

IOReturn SweetMDSS::noCQueueGroup(void)
{
 if (!noCAdmission()) return kIOReturnNotReady;
 int chosen=-1;
 for (unsigned t=2;t<4;++t) {
  uint32_t b=t*NStride, enabled=dp_rd(fNoCTCSBase,b+NEnable), irq=dp_rd(fNoCTCSBase,NIRQStatus);
  if (enabled&~((1u<<fNoCNCPT)-1u)) return kIOReturnIOError;
  for (unsigned c=0;c<fNoCNCPT;++c) if (!(fNoCSlotsOwned&(1u<<t)) && (enabled&(1u<<c))) {
   uint32_t address=dp_rd(fNoCTCSBase,b+NAddress+c*NCommandStride);
   for (unsigned r=1;r<SweetDisplayBCMCount;++r) if (address==fNoCInfo[r].address) return kIOReturnBusy;
  }
  uint32_t idle=dp_rd(fNoCTCSBase,b+NStatus);
  if (chosen<0 && idle && idle!=~0u && irq!=~0u && !(irq&(1u<<t)) && (!enabled || (fNoCSlotsOwned&(1u<<t)))) chosen=(int)t;
 }
 if (chosen<0) return kIOReturnBusy;
 fNoCGroup=1;
 unsigned vcd=fNoCInfo[fNoCOrder[fNoCCursor]].vcd;
 while (fNoCCursor+fNoCGroup<fNoCCount && fNoCInfo[fNoCOrder[fNoCCursor+fNoCGroup]].vcd==vcd) ++fNoCGroup;
 if (fNoCGroup>fNoCNCPT) return kIOReturnNoResources; /* Never split a VCD commit. */
 fNoCTCS=(unsigned)chosen; fNoCSavedIRQ=dp_rd(fNoCTCSBase,NIRQEnable);
 if (fNoCSavedIRQ==~0u) return kIOReturnIOError;
 fNoCOp=fNoCOps=0; fNoCExecuting=fNoCProgress=fNoCWriteIssued=false;
 uint32_t b=fNoCTCS*NStride, bit=1u<<fNoCTCS;
 fNoCEnable=(1u<<fNoCGroup)-1u; fNoCWait=1u<<(fNoCGroup-1);
 noCAdd(NIRQEnable,fNoCSavedIRQ&~bit);
 for (unsigned c=0;c<fNoCGroup;++c) {
  unsigned r=fNoCOrder[fNoCCursor+c]; bool last=c+1==fNoCGroup;
  noCAdd(b+NMessage+c*NCommandStride,8u|(1u<<16)|(last ? 1u<<8 : 0u));
  noCAdd(b+NAddress+c*NCommandStride,fNoCInfo[r].address);
  noCAdd(b+NDataRegister+c*NCommandStride,fNoCValues[r]|(last ? 1u<<30 : 0u));
 }
 noCAdd(b+NWait,fNoCWait); noCAdd(b+NEnable,fNoCEnable);
 uint32_t control=dp_rd(fNoCTCSBase,b+NControl)&~NTrigger;
 noCAdd(b+NControl,control);
 noCAdd(b+NControl,control&~NAMC); noCAdd(b+NControl,control|NAMC);
 noCAdd(NIRQEnable,0);
 fNoCStepDeadline=scanout_now()+100000000ULL;
 uint64_t requestAt=fNoCEvidence.requestAt;
 fNoCEvidence={}; fNoCEvidence.requestAt=requestAt;
 fNoCEvidence.groupValid=true; fNoCEvidence.groupAt=fNoCStepDeadline-100000000ULL;
 fNoCEvidence.groupDeadline=fNoCStepDeadline;
 fNoCEvidence.cursor=fNoCCursor; fNoCEvidence.count=fNoCGroup; fNoCEvidence.tcs=fNoCTCS;
 for (unsigned c=0;c<fNoCGroup;++c) {
  unsigned r=fNoCOrder[fNoCCursor+c]; bool last=c+1==fNoCGroup;
  fNoCEvidence.commands[c]={r,fNoCInfo[r].vcd,fNoCInfo[r].address,
      8u|(1u<<16)|(last ? 1u<<8 : 0u),fNoCValues[r]|(last ? 1u<<30 : 0u),
      NIssued|(last ? NComplete : 0u),0,0};
 }
 return kIOReturnSuccess;
}

void SweetMDSS::captureNoCRegisters(NoCRegisterSample &sample)
{
 /* Only after this tick's normal noCAdmission(), for the selected ACTIVE
  * slot. Sequential, non-atomic reads; no IRQ clear or additional status
  * polling. Never called by the logger, overall deadline or stop path. */
 uint32_t base=fNoCTCS*NStride;
 sample.irqEnable=dp_rd(fNoCTCSBase,NIRQEnable);
 sample.control=dp_rd(fNoCTCSBase,base+NControl);
 sample.enable=dp_rd(fNoCTCSBase,base+NEnable);
 sample.wait=dp_rd(fNoCTCSBase,base+NWait);
 sample.at=scanout_now(); sample.valid=true;
}
void SweetMDSS::logNoCEvidence(const char *reason)
{
 /* Cached evidence only. In particular, the overall deadline precedes
  * admission and may run after clocks or the provider have been lost. */
 const NoCEvidence &e=fNoCEvidence;
 SWEET_LOG_ERROR("SWEET MDSS NOC EVIDENCE: reason=%s now-ns=%llu phase=%u request-ns=%llu deadline-ns=%llu group-valid=%u group-ns=%llu group-deadline-ns=%llu group-cursor=%u group-count=%u tcs=%u trigger-issued-ns=%llu; software times, not hardware ACK\n",
     reason,scanout_now(),fNoCPhase,e.requestAt,fNoCDeadline,(unsigned)e.groupValid,e.groupAt,e.groupDeadline,
     e.cursor,e.count,e.tcs,e.triggerAt);
 if (!e.groupValid) return;
 SWEET_LOG_ERROR("SWEET MDSS NOC REGISTERS: stage=trigger valid=%u sampled-ns=%llu irq-enable=%#x control=%#x cmd-enable=%#x wait=%#x; cached sequential reads\n",
     (unsigned)e.trigger.valid,e.trigger.at,e.trigger.irqEnable,e.trigger.control,e.trigger.enable,e.trigger.wait);
 SWEET_LOG_ERROR("SWEET MDSS NOC REGISTERS: stage=group-timeout valid=%u sampled-ns=%llu irq-enable=%#x control=%#x cmd-enable=%#x wait=%#x; cached sequential reads\n",
     (unsigned)e.timeout.valid,e.timeout.at,e.timeout.irqEnable,e.timeout.control,e.timeout.enable,e.timeout.wait);
 SWEET_LOG_ERROR("SWEET MDSS NOC POLL: samples=%u first-ns=%llu last-ns=%llu first-irq=%#x last-irq=%#x first-idle=%#x last-idle=%#x seen-busy=%u seen-incomplete=%u seen-irq=%u last-complete=%u progress=%u; cached completion checks\n",
     e.polls,e.firstAt,e.lastAt,e.firstIRQ,e.lastIRQ,e.firstIdle,e.lastIdle,(unsigned)e.seenBusy,
     (unsigned)e.seenIncomplete,(unsigned)e.seenIRQ,(unsigned)e.lastComplete,(unsigned)fNoCProgress);
 SWEET_LOG_ERROR("SWEET MDSS NOC READBACK: valid=%u sampled-ns=%llu write-offset=%#x read-offset=%#x want=%#x got=%#x posted=%u deadline-ns=%llu; cached programming or cleanup check\n",
     (unsigned)e.readValid,e.readAt,e.writeOffset,e.readOffset,e.want,e.got,(unsigned)fNoCWriteIssued,fNoCWriteDeadline);
 for (unsigned c=0;c<e.count;++c) {
  const NoCCommandEvidence &cmd=e.commands[c];
  SWEET_LOG_ERROR("SWEET MDSS NOC COMMAND: index=%u resource=%u vcd=%u address=%#x planned-msg=%#x planned-data=%#x required=%#x first-status=%#x last-status=%#x status-valid=%u\n",
      c,cmd.resource,cmd.vcd,cmd.address,cmd.message,cmd.data,cmd.required,cmd.first,cmd.last,(unsigned)(e.polls!=0));
 }
}

void SweetMDSS::noCTimerAction(OSObject *owner,IOTimerEventSource *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (s->fLifecycleEnabled && s->lifecycleBusy() && !s->lifecycleResourceStepAllowed()) { s->finishLifecycle(kIOReturnNotReady,"resource participant lost");return; }
 if (s->fAccepting && !s->isInactive() && s->noCBusy()) s->advanceNoC();
}
void SweetMDSS::advanceNoC(void)
{
 uint64_t now=scanout_now();
 if (now>=fNoCDeadline) { finishNoC(kIOReturnTimeout,fNoCTouched,"overall-deadline"); return; }
 if (!fRPMh || !fRPMhConsumer) { finishNoC(kIOReturnNotReady,fNoCTouched); return; }
 SweetNoCStatus &status=fNoCLeases[fNoCSlot].status;
 if (fNoCPhase==NAppsRaise || fNoCPhase==NAppsLower) {
  if (fNoCPhase==NAppsRaise && fNoCAppsLower) { fNoCPhase=NClock; }
  else {
   if (!fNoCAppsToken) {
    IOReturn rc=fRPMh->requestRegisterBandwidth(fRPMhConsumer,fNoCTarget.registerPeak,&fNoCAppsToken);
    if (rc!=kIOReturnSuccess) { finishNoC(rc,fNoCTouched); return; }
    fNoCRegisterToken=0; /* Old ACK cannot authorize a newly pending config request. */
    fNoCTouched=true; /* Admitted foreign work may write before our next tick. */
   }
   SweetRegisterBandwidthStatus apps={};
   IOReturn rc=fRPMh->copyRegisterBandwidthStatus(fRPMhConsumer,fNoCAppsToken,&apps);
   if (rc!=kIOReturnSuccess || apps.state==SweetRPMhFailed) { finishNoC(rc ? rc : apps.result,true); return; }
   if (apps.state!=SweetRPMhComplete) { fNoCTimer->setTimeoutUS(100); return; }
   status.registerAcknowledged=true; fNoCRegisterToken=fNoCAppsToken; fNoCAppsToken=0;
   if (fNoCPhase==NAppsLower) { finishNoC(kIOReturnSuccess,false); return; }
   fNoCPhase=NClock;
  }
 }
 if (fNoCPhase==NClock || fNoCPhase==NClockRelease) {
  SweetClockLease clock={};
  if (fNoCPhase==NClock && !fNoCClockToken) {
   IOReturn rc=retainLocalClocks(SweetClockRSC,&fNoCClockToken);
   if (rc!=kIOReturnSuccess) {
    SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-retain rc=%#x\n",(unsigned)rc);
    finishNoC(rc,fNoCTouched); return;
   }
  }
  IOReturn rc=localClockStatus(fNoCClockToken,&clock);
  if (rc!=kIOReturnSuccess || clock.state==SweetClockFailed || clock.state==SweetClockStopped) {
   SWEET_LOG_ERROR("SWEET MDSS NOC ADMIT: gate=clock-status rc=%#x token=%llu state=%u mask=%#x result=%#x\n",
       (unsigned)rc,fNoCClockToken,clock.state,clock.mask,(unsigned)clock.result);
   finishNoC(rc ? rc : kIOReturnNotReady,true); return;
  }
  unsigned expected=fNoCPhase==NClock ? SweetClockHeld : SweetClockReleased;
  if (clock.state!=expected) { fNoCTimer->setTimeoutUS(100); return; }
  if (fNoCPhase==NClockRelease) {
   fNoCClockToken=0;
   if (fNoCAppsLower) { fNoCPhase=NAppsLower; fNoCTimer->setTimeoutUS(1); return; }
   finishNoC(kIOReturnSuccess,false); return;
  }
  fNoCPhase=NRSC;
 }
 if (fNoCPhase==NRSC) {
  IOReturn rc=takeNoCRSC();
  if (rc==kIOReturnNotReady) { fNoCTimer->setTimeoutUS(2); return; }
  if (rc!=kIOReturnSuccess) { finishNoC(rc,fNoCTouched,"rsc-takeover"); return; }
  rc=prepareNoCIRQ();
  if (rc==kIOReturnNotReady) { fNoCTimer->setTimeoutUS(100); return; }
  if (rc!=kIOReturnSuccess) { finishNoC(rc,fNoCTouched,"irq-admission"); return; }
  fNoCPhase=NData;
  if (fNoCCount) {
   rc=noCQueueGroup();
   if (rc!=kIOReturnSuccess) { finishNoC(rc,fNoCTouched); return; }
  }
 }
 if (fNoCPhase!=NData) return;
 if (!noCAdmission()) { finishNoC(kIOReturnNotReady,fNoCTouched); return; }
 if (fNoCCursor==fNoCCount) {
  if (fNoCTCSOwned) {
   /* Lineage tcs_use_ok, after every VCD completed and before clock put. */
   dp_wr(fNoCWrapper,0,1); fNoCTCSOwned=false;
   SWEET_LOG_INFO("SWEET MDSS RSC: TCS_USE_OK returned; CLK state retained\n");
  }
  if (!fNoCTarget.registerPeak) {
   IOReturn rc=releaseLocalClocks(fNoCClockToken);
   if (rc!=kIOReturnSuccess) { finishNoC(rc,fNoCTouched); return; }
   fNoCPhase=NClockRelease;
  } else if (fNoCAppsLower) fNoCPhase=NAppsLower;
  else { finishNoC(kIOReturnSuccess,false); return; }
  fNoCTimer->setTimeoutUS(100); return;
 }
 if (now>=fNoCStepDeadline) {
  captureNoCRegisters(fNoCEvidence.timeout);
  finishNoC(kIOReturnTimeout,true,"group-deadline"); return;
 }
 uint32_t base=fNoCTCS*NStride, bit=1u<<fNoCTCS;
 if (!fNoCExecuting && !fNoCOp && !fNoCWriteIssued) {
  /* Drain the previous GIC EOI before reusing its slot/generation. */
  vm_offset_t gic=fMaps[11]->getVirtualAddress();
  if ((dp_rd(gic,0)&(1u<<31)) || (dp_rd(gic,0x314)&2u)) { fNoCTimer->setTimeoutUS(100); return; }
  if (dp_rd(gic,0x214)&2u) {
   if (dp_rd(fNoCTCSBase,NIRQStatus)&dp_rd(fNoCTCSBase,NIRQEnable)) { finishNoC(kIOReturnBusy,true); return; }
   dp_wr(gic,0x294,2);
   if (dp_rd(gic,0x214)&2u) { fNoCTimer->setTimeoutUS(100); return; }
  }
  __atomic_store_n(&fNoCIRQPosted,0u,__ATOMIC_RELEASE);
 }
 while (fNoCOp<fNoCOps) {
  NoCOperation &op=fNoCOperations[fNoCOp];
  if (!fNoCWriteIssued) {
   if (fNoCOp==1 && !fNoCExecuting) {
    /* A new inherited request may have arrived after queue construction. */
    for (unsigned t=2;t<4;++t) {
     uint32_t enabled=dp_rd(fNoCTCSBase,t*NStride+NEnable);
     if (enabled&~((1u<<fNoCNCPT)-1u)) { finishNoC(kIOReturnIOError,true); return; }
     uint32_t idle=dp_rd(fNoCTCSBase,t*NStride+NStatus);
     if (t==fNoCTCS && ((enabled && !(fNoCSlotsOwned&bit)) ||
         !idle || idle==~0u || (dp_rd(fNoCTCSBase,NIRQStatus)&bit))) { finishNoC(kIOReturnBusy,true); return; }
     for (unsigned c=0;c<fNoCNCPT;++c) if (!(fNoCSlotsOwned&(1u<<t)) && (enabled&(1u<<c)))
      for (unsigned r=1;r<SweetDisplayBCMCount;++r)
       if (dp_rd(fNoCTCSBase,t*NStride+NAddress+c*NCommandStride)==fNoCInfo[r].address) { finishNoC(kIOReturnBusy,true); return; }
    }
   }
   if (op.offset==NIRQEnable) {
    uint32_t current=dp_rd(fNoCTCSBase,NIRQEnable);
    if (current==~0u) { finishNoC(kIOReturnIOError,true); return; }
    op.value=fNoCOp ? current|bit : current&~bit;
   }
   fNoCTouched=true; dp_wr(fNoCTCSBase,op.offset,op.value);
   fNoCWriteDeadline=now+1000000ULL; fNoCWriteIssued=true;
  }
  uint32_t value=dp_rd(fNoCTCSBase,op.clear ? NIRQStatus : op.offset);
  fNoCEvidence.readValid=true; fNoCEvidence.readAt=scanout_now();
  fNoCEvidence.writeOffset=op.offset; fNoCEvidence.readOffset=op.clear ? NIRQStatus : op.offset;
  fNoCEvidence.want=op.value; fNoCEvidence.got=value;
  /* Lineage synchronizes only the three pre-trigger CONTROL writes.
   * Payload, WAIT/ENABLE, IRQ source and ACTIVE cleanup are plain writes. */
  bool sync=!op.clear && op.offset==base+NControl;
  if (sync && value!=op.value) {
   if (now>=fNoCWriteDeadline) finishNoC(kIOReturnIOError,true,"write-readback");
   else fNoCTimer->setTimeoutUS(100);
   return;
  }
  if (!sync) observeLineageValue(op.clear ? "noc-irq-clear" : "noc-write",
      op.clear ? NIRQStatus : op.offset,op.clear ? 0u : op.value,value,op.clear ? op.value : ~0u,true);
  ++fNoCOp; fNoCWriteIssued=false;
 }
 if (!fNoCExecuting) {
  uint32_t irq=dp_rd(fNoCTCSBase,NIRQStatus), enabled=dp_rd(fNoCTCSBase,NIRQEnable);
  if (irq==~0u || enabled==~0u || (irq&enabled)) { finishNoC(kIOReturnIOError,true,"stale-before-trigger"); return; }
  fNoCIRQObserved=false; fNoCExecuting=true;
  fNoCOp=fNoCOps=0;
  __atomic_store_n(&fNoCIRQArmed,1u,__ATOMIC_RELEASE);
  dp_wr(fNoCTCSBase,base+NControl,dp_rd(fNoCTCSBase,base+NControl)|NAMC|NTrigger);
  /* Level IRQ latched during trigger must survive until GIC unmask. */
  dp_wr(fMaps[11]->getVirtualAddress(),0x114,2);
  fNoCEvidence.triggerAt=scanout_now(); /* MMIO write returned, not an ACK. */
  captureNoCRegisters(fNoCEvidence.trigger);
  waitNoCCompletion(); return;
 }
 if (fNoCOps) {
  fNoCSlotsOwned|=bit;
  for (unsigned c=0;c<fNoCGroup;++c) {
   unsigned r=fNoCOrder[fNoCCursor+c];
   fNoCApplied[r]=fNoCValues[r]; fNoCOwned|=1u<<r; status.acknowledged|=1u<<r;
  }
  fNoCCursor+=fNoCGroup;
  if (fNoCCursor<fNoCCount) {
   IOReturn rc=noCQueueGroup(); if (rc!=kIOReturnSuccess) { finishNoC(rc,true); return; }
  }
  fNoCTimer->setTimeoutUS(1); return;
 }
 if (!fNoCIRQObserved) { waitNoCCompletion(); return; }
 uint32_t irq=dp_rd(fNoCTCSBase,NIRQStatus), idle=dp_rd(fNoCTCSBase,base+NStatus);
 bool complete=true;
 for (unsigned c=0;c<fNoCGroup;++c) {
  uint32_t cmd=dp_rd(fNoCTCSBase,base+NCommandStatus+c*NCommandStride);
  unsigned required=NIssued|(c+1==fNoCGroup ? NComplete : 0u);
  complete&=cmd!=~0u && (cmd&required)==required;
  if (!fNoCEvidence.polls) fNoCEvidence.commands[c].first=cmd;
  fNoCEvidence.commands[c].last=cmd;
 }
 fNoCEvidence.lastAt=scanout_now();
 if (!fNoCEvidence.polls) {
  fNoCEvidence.firstAt=fNoCEvidence.lastAt; fNoCEvidence.firstIRQ=irq; fNoCEvidence.firstIdle=idle;
 }
 ++fNoCEvidence.polls; fNoCEvidence.lastIRQ=irq; fNoCEvidence.lastIdle=idle;
 fNoCEvidence.seenBusy|=idle==0; fNoCEvidence.seenIncomplete|=!complete;
 fNoCEvidence.seenIRQ|=irq!=~0u && (irq&bit)!=0; fNoCEvidence.lastComplete=complete;
 fNoCProgress|=idle==0 || !complete;
 if (irq==~0u) { finishNoC(kIOReturnIOError,true,"invalid-status"); return; }
 if (!complete || !(irq&bit)) { finishNoC(kIOReturnIOError,true,"incomplete-irq"); return; }
 observeLineageValue("noc-completion-idle",base+NStatus,1,idle,~0u,false);
 SWEET_LOG_INFO("SWEET MDSS NOC IRQ: fresh ACTIVE completion tcs=%u commands=%u entries=%u; cleanup pending\n",
     fNoCTCS,fNoCGroup,__atomic_load_n(&fNoCIRQEntries,__ATOMIC_ACQUIRE));
 /* Lineage ACTIVE completion preserves control (AMC + trigger). The next
  * reuse clears trigger, clears AMC, then sets AMC and triggers again. */
 noCAdd(base+NEnable,0); noCAdd(base+NWait,0); noCAdd(NIRQClear,bit,true);
 fNoCTimer->setTimeoutUS(1);
}

void SweetMDSS::finishNoC(IOReturn result,bool uncertain,const char *reason)
{
 quietNoCIRQ();
 NoCLease &lease=fNoCLeases[fNoCSlot];
 lease.status.result=result; lease.status.uncertain=uncertain;
 if (result==kIOReturnSuccess) {
  lease.active=!fNoCReleasing;
  lease.status.state=fNoCReleasing ? SweetNoCReleased : SweetNoCHeld;
  fNoCAggregate=fNoCTarget;
  SWEET_LOG_INFO("SWEET MDSS NOC: token=%llu state=%u ACK mask=%#x; ACTIVE only, no solver/physical bandwidth proof\n",
      lease.status.token,lease.status.state,lease.status.acknowledged);
 } else {
  lease.status.state=SweetNoCFailed; fNoCFault=true;
  if (result!=kIOReturnAborted) {
   logNoCEvidence(reason);
   SWEET_LOG_ERROR("SWEET MDSS NOC CONTEXT: phase=%u clock-token=%llu apps-token=%llu register-token=%llu register-acked=%u cursor=%u/%u group=%u tcs=%u executing=%u operation=%u/%u posted=%u clock-fault=%u clock-operation=%u clock-cursor=%u clock-waiting=%u\n",
       fNoCPhase,fNoCClockToken,fNoCAppsToken,fNoCRegisterToken,(unsigned)lease.status.registerAcknowledged,
       fNoCCursor,fNoCCount,fNoCGroup,fNoCTCS,(unsigned)fNoCExecuting,fNoCOp,fNoCOps,(unsigned)fNoCWriteIssued,
       (unsigned)fClockFault,fClockOperation,fClockCursor,(unsigned)fClockWaiting);
  }
  SWEET_LOG_ERROR("SWEET MDSS NOC: token=%llu rc=%#x ACK mask=%#x uncertain=%u; votes/clocks retained, no fallback\n",
      lease.status.token,(unsigned)result,lease.status.acknowledged,uncertain);
 }
 fNoCPhase=NIdle; if (fNoCTimer) fNoCTimer->cancelTimeout();
}
void SweetMDSS::stopNoC(void)
{
 quietNoCIRQ();
 if (noCBusy()) finishNoC(kIOReturnAborted,fNoCTouched);
 if (fNoCTimer) fNoCTimer->cancelTimeout();
 /* No implicit CN0/BCM withdrawal or clock/domain collapse during teardown. */
}
