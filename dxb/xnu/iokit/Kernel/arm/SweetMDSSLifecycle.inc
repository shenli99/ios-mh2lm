/* One MDSS gate, no cross-gate waits or reverse callbacks. Mode1 preserves
 * warm DisplayOff/On. Mode2 follows R66456 Lineage suspend-ULPS, PHY idle,
 * reset/pinctrl and panel supply requests. Shared core/config power stays held. Mode3 parks managed DATA leases only;
 * a separate config floor and all power/boot/link resources stay live. Mode4
 * cycles host/PHY/PLL hardware while retaining ALL resource tickets. Mode5
 * composes data parking and link shutdown, retaining the config/power path.
 * Mode6 adds reset-low and panel-only vote withdrawal/restore; L3/L5 stay held.
 * Mode7 composes mode2 with the fixed single-pipeline rebuild. */
/* Software-only breadcrumbs around potentially blocking calls. A printed entry
 * never proves return/completion. Keep real resource, pin and DMA fences intact.
 * Pin phases are short (100ms); trace each poll so a post-reset stall cannot be
 * hidden by a first-poll-only marker. Other phases log only their milestones. */
void SweetMDSS::traceWake(const char *stage,unsigned value)
{
 if (!fLifecycleEnabled || fLifecycle.operation!=SweetPanelDisplayOn || !lifecycleBusy()) return;
 if (fWakeTraceCount>=1024) return;
 ++fWakeTraceCount;
 SWEET_LOG_INFO("SWEET PANEL WAKE: token=%llu phase=%u step=%s value=%#x command=%llu dsi=%llu completed=%u/%u; diagnostic only\n",
     fLifecycle.token,fLifecycle.phase,stage,value,fLifecycle.commandToken,fDsiStatus.token,fDsiStatus.completed,fDsiStatus.total);
 if (fWakeTraceCount==1024) SWEET_LOG_INFO("SWEET PANEL WAKE: token=%llu trace limit reached\n",fLifecycle.token);
}
bool SweetMDSS::setupLifecycle(IOService *provider)
{
 OSData *identity=OSDynamicCast(OSData,provider->getProperty(MH2LM_PANEL_PROPERTY));
 unsigned source=identity ? mh2lm_panel_source_status((const uint8_t *)identity->getBytesNoCopy(),identity->getLength()) : 0;
 unsigned profile=identity ? mh2lm_panel_profile((const uint8_t *)identity->getBytesNoCopy(),identity->getLength()) : 0;
 fPanelProfile=sweet_panel_profile(profile);
 setProperty("SweetPanelSelectionSource",(unsigned long long)source,32);
 setProperty("SweetPanelSelectedProfile",(unsigned long long)profile,32);
 SWEET_LOG_INFO("SWEET PANEL: ABL selection source=%u profile=%u; not a physical DDIC ID\n",source,profile);
 int mode=0,touch=0,own=0,pins=0;
 (void)PE_parse_boot_argn("sweetpanellifecycle",&mode,sizeof(mode));
 if (!mode) return true;
 (void)PE_parse_boot_argn("sweettouchpanel",&touch,sizeof(touch));
 (void)PE_parse_boot_argn("sweetfbown",&own,sizeof(own));
 (void)PE_parse_boot_argn("sweetpanelpins",&pins,sizeof(pins));
 if ((mode!=1 && mode!=2 && mode!=3 && mode!=4 && mode!=5 && mode!=6 && mode!=7) || touch!=2 || own!=2 || !fTouchPanelClient || !fManagedEnabled ||
     !fDsiEnabled || !fPanelPowerEnabled || fIrqMode!=2) return false;
 if (mode>=2 && (!fPanelProfile || !sweetPanelPinsExclusive(pins) || !fPanelPinsClient ||
     !sweet_dsi_sequence_valid(fPanelProfile->on,fPanelProfile->onSize,NULL) ||
     !sweet_dsi_sequence_valid(fPanelProfile->off,fPanelProfile->offSize,NULL))) {
  SWEET_LOG_ERROR("SWEET PANEL: profile lifecycle requires live-ABL profile and pins2-or-3; refused before side effects\n");
  return false;
 }
 fLifecycleLineage=mode==2 || mode==7;
 if (fLifecycleLineage) {
  const SweetPanelSDRColor *color=sweet_panel_sdr_color(profile);unsigned count=0;
  if (!color || !sweet_dsi_sequence_valid(color->commands,color->size,&count) || count!=6) return false;
  setProperty("SweetPanelColorPolicy","SDR Auto RGB preset2 offsets0 sharpness-off");
 }
 fLifecycleSupplyCycle=mode==6 || fLifecycleLineage;
 fLifecycleLinkCycle=mode==4 || mode==5 || mode==6 || fLifecycleLineage;
 if (fLifecycleLinkCycle && (fCoreProfile!=2 || fHostProfile!=1 || fLinkProfile!=2)) return false;
 if (mode==7 && !setupPipeline(provider)) return false;
 setProperty("SweetPanelLinkCycleEnabled",fLifecycleLinkCycle);
 setProperty("SweetPanelSupplyCycleEnabled",fLifecycleSupplyCycle);
 fLifecycleDataParking=mode==3 || mode==5 || mode==6 || fLifecycleLineage;
 setProperty("SweetPanelLineageSuspendULPS",fLifecycleLineage);
 if (fLifecycleLineage) SWEET_LOG_INFO("SWEET PANEL LINEAGE: suspend-ULPS/PHY-idle panel supply and scanout lifecycle enabled; shared core/config power retained\n");
 if (fLifecycleDataParking && fCoreProfile!=2) return false;
 fLifecycleFull=mode>=2;setProperty("SweetPanelSleepResetEnabled",fLifecycleFull);
 fLifecycleTimer=IOTimerEventSource::timerEventSource(this,lifecycleTimerAction);
 if (!fLifecycleTimer || fLoop->addEventSource(fLifecycleTimer)!=kIOReturnSuccess) return false;
 fLifecycleTimerAdded=true;fLifecycleEnabled=true;
 publishLifecycle();
 SWEET_LOG_INFO("SWEET PANEL: lifecycle mode=%u; full sleep/reset=%u data-parking=%u link-cycle=%u panel-supply=%u; physical rail-off unverified\n",(unsigned)mode,fLifecycleFull,fLifecycleDataParking,fLifecycleLinkCycle,fLifecycleSupplyCycle);
 return true;
}
bool SweetMDSS::lifecycleBusy(void) const
{
 switch (fLifecycle.phase) {
 case SweetPanelTouchPause:case SweetPanelDrain:case SweetPanelTakeoverPending:
 case SweetPanelCommand:case SweetPanelFrame:case SweetPanelTouchResume:
 case SweetPanelPinActivate:case SweetPanelPinReset:case SweetPanelBrightness:
 case SweetPanelResourcePark:case SweetPanelResourceRestore:
 case SweetPanelLinkPowerDown:case SweetPanelLinkRebuild:
 case SweetPanelPinParkActivate:case SweetPanelPinParkWait:case SweetPanelPinPark:
 case SweetPanelSupplyActivate:case SweetPanelSupplyPark:case SweetPanelSupplyRestore:
 case SweetPanelPipelineRestore:case SweetPanelBrightnessDrain:case SweetPanelColorRestore: return true;
 default: return false;
 }
}
bool SweetMDSS::lifecycleBlocked(void) const
{ return fLifecycleEnabled && (lifecycleBusy() || fLifecycleParked || fLifecycle.phase==SweetPanelFault || fLifecycle.phase==SweetPanelStopped); }
unsigned SweetMDSS::panelLifecycleCapabilities(void)
{
 return fLifecycleEnabled ? (1u<<SweetPanelTakeover)|(1u<<SweetPanelDisplayOff)|(1u<<SweetPanelDisplayOn)|(fLifecycleFull ? SweetPanelRetainedSleepReset : 0u)|(fLifecycleDataParking ? SweetPanelDataParking : 0u)|(fLifecycleLinkCycle ? SweetPanelLinkPowerCycle : 0u)|(fLifecycleSupplyCycle ? SweetPanelSupplyCycle : 0u)|(fPipelineEnabled ? SweetPanelPipelineRebuild : 0u) : 0;
}
void SweetMDSS::publishLifecycle(void)
{
 setProperty("SweetDisplayManagedState",(unsigned long long)fManagedState,32);
 setProperty("SweetPanelLifecycleToken",(unsigned long long)fLifecycle.token,64);
 setProperty("SweetPanelLifecycleOperation",(unsigned long long)fLifecycle.operation,32);
 setProperty("SweetPanelLifecyclePhase",(unsigned long long)fLifecycle.phase,32);
 setProperty("SweetPanelLifecycleResult",(unsigned long long)(uint32_t)fLifecycle.result,32);
 setProperty("SweetPanelLifecycleUncertain",fLifecycle.uncertain);
 setProperty("SweetPanelLifecycleSleeping",fLifecycle.sleeping);
 setProperty("SweetPanelLifecycleResetComplete",fLifecycle.resetComplete);
 setProperty("SweetPanelLifecycleBrightnessRestored",fLifecycle.brightnessRestored);
 setProperty("SweetPanelLifecycleColorRestored",fLifecycleColorRestored);
 setProperty("SweetPanelPipelineGeneration",(unsigned long long)fLifecycle.pipelineGeneration,64);
 fLifecycle.rails=fManagedRails;
 setProperty("SweetPanelLifecycleRails",(unsigned long long)fLifecycle.rails,32);
 setProperty("SweetPanelLifecyclePanelReleasedMask",(unsigned long long)fLifecycle.panelReleasedMask,32);
 setProperty("SweetPanelLifecyclePanelAggregateEnabledMask",(unsigned long long)fLifecycle.panelAggregateEnabledMask,32);
 setProperty("SweetPanelLifecycleSupplyToken",(unsigned long long)fLifecycle.supplyToken,64);
 setProperty("SweetPanelLifecycleResetHeldLow",fLifecycle.resetHeldLow);
 setProperty("SweetPanelLifecyclePanelVotesParked",fLifecycle.panelVotesParked);
 setProperty("SweetPanelLifecyclePanelVotesRestored",fLifecycle.panelVotesRestored);
 fLifecycle.configToken=fManagedConfigBandwidth;fLifecycle.dataToken=fManagedBandwidth;fLifecycle.clockToken=fManagedClock;
 setProperty("SweetPanelLifecycleDataParked",fLifecycle.resourcesParked);
 setProperty("SweetPanelLifecycleRetainedPower",fLifecycle.retainedPower);
 setProperty("SweetPanelLifecycleConfigHeld",fLifecycle.configHeld);
 setProperty("SweetPanelLifecycleLinkPoweredDown",fLifecycle.linkPoweredDown);
 setProperty("SweetPanelLifecycleLinkRebuilt",fLifecycle.linkRebuilt);
 setProperty("SweetPanelLifecycleDataRestored",fLifecycle.resourcesRestored);
 setProperty("SweetPanelLifecycleConfigToken",(unsigned long long)fLifecycle.configToken,64);
 setProperty("SweetPanelLifecycleDataToken",(unsigned long long)fLifecycle.dataToken,64);
 setProperty("SweetPanelLifecycleClockToken",(unsigned long long)fLifecycle.clockToken,64);
}
/* Client withdrawal is checked even before its queued changed() event runs.
 * Participants remain retained by MDSS until that event faults this generation
 * BEFORE detach/release. No replacement may inherit an old completion token. */
bool SweetMDSS::lifecycleParticipants(void)
{
 if (!displayClientsValid() || !fAccepting || isInactive() || !fTouchPanel || !fRPMh ||
     fTouchPanel->isInactive() || fRPMh->isInactive() || !fTouchPanelConsumer || !fRPMhConsumer) return false;
 SweetTouchPanel *t=fTouchPanelClient->copyBackend();
 SweetRPMh *r=fRPMhClient->copyBackend();
 SweetPanelPins *p=fPanelPinsClient ? fPanelPinsClient->copyBackend() : NULL;
 bool ok=t==fTouchPanel && r==fRPMh && p==fPanelPins && (!p || !p->isInactive()) &&
     (!fLifecycleFull || (fPanelProfile && p && fPanelPinsConsumer));
 if (ok && fLifecycle.touchQuiesced) {
  SweetTouchPanelStatus fence={};
  ok=t->copyStatus(fTouchPanelConsumer,fLifecycle.touchToken,&fence)==kIOReturnSuccess &&
      fence.token==fLifecycle.touchToken && fence.quiesced && !fence.ready && !fence.uncertain && fence.result==kIOReturnSuccess;
 }
 if (ok && fLifecycleSupplyCycle && fLifecycle.resetHeldLow) {
  SweetPanelPinStatus pin={};
  ok=p && p->copyLowStatus(fPanelPinsConsumer,fLifecycle.pinToken,&pin)==kIOReturnSuccess;
 }
 OSSafeReleaseNULL(t);OSSafeReleaseNULL(r);OSSafeReleaseNULL(p);
 return ok;
}
bool SweetMDSS::lifecycleIRQDrained(void)
{
 if (!fMaps[11] || __atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE)!=IrqIdle) return false;
 vm_offset_t g=fMaps[11]->getVirtualAddress();const uint32_t bit=1u<<19;
 /* RWP, enable AND active must each settle. Software IrqIdle alone cannot
  * prove the GIC top half has executed its final ICC_EOIR1_EL1. */
 return !(dp_rd(g,0)&(1u<<31)) && !(dp_rd(g,0x10c)&bit) && !(dp_rd(g,0x30c)&bit) &&
     !dp_rd(fMaps[10]->getVirtualAddress(),0);
}
bool SweetMDSS::lifecycleResourcesHeld(void)
{
 bool tracePin=fLifecycle.phase==SweetPanelPinActivate || fLifecycle.phase==SweetPanelPinReset;
 if (tracePin) traceWake("resources-local-enter");
 if (fManagedState!=ManagedReady || fManagedRails!=15 || !fManagedARC || !fManagedBandwidth ||
     !fManagedClock || fNoCFault || fClockFault || !localClocksReady() ||
     (fCoreProfile && !coreReady()) || (fHostProfile && fHostPhase!=HostReady) ||
     (fLinkProfile && (fLinkPhase!=LinkReady || !linkConfiguration()))) return false;
 SweetClockLease c={};SweetNoCStatus n={};SweetDisplayPowerStatus a={};
 if (tracePin) traceWake("resources-providers-enter");
 bool held=localClockStatus(fManagedClock,&c)==kIOReturnSuccess && c.state==SweetClockHeld &&
     c.mask==mdssClockMasks[SweetClockDSI0] && bandwidthStatus(fManagedBandwidth,&n)==kIOReturnSuccess &&
     n.state==SweetNoCHeld && fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&a)==kIOReturnSuccess &&
     a.state==SweetRPMhComplete && !a.uncertain && a.corner==SweetDisplayTurbo;
 if (tracePin) traceWake("resources-return",held);
 return held;
}
struct SweetLifecycleRequest { unsigned action,operation; uint64_t token,*out; SweetPanelLifecycleStatus *status; };
IOReturn SweetMDSS::beginPanelLifecycle(unsigned op,uint64_t *token)
{
 if (!token) return kIOReturnBadArgument;*token=0;
 SweetLifecycleRequest r={};r.operation=op;r.out=token;
 return fGate ? fGate->runAction(lifecycleAction,&r) : kIOReturnNotReady;
}
IOReturn SweetMDSS::panelLifecycleStatus(uint64_t token,SweetPanelLifecycleStatus *status)
{
 if (!status) return kIOReturnBadArgument;*status={};
 SweetLifecycleRequest r={};r.action=1;r.token=token;r.status=status;
 return fGate ? fGate->runAction(lifecycleAction,&r) : kIOReturnNotReady;
}
void SweetMDSS::cancelPanelLifecycle(uint64_t token)
{
 SweetLifecycleRequest r={};r.action=2;r.token=token;
 if (fGate) (void)fGate->runAction(lifecycleAction,&r);
}
IOReturn SweetMDSS::lifecycleAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;SweetLifecycleRequest &r=*(SweetLifecycleRequest *)arg;
 if (r.action) {
  if (!r.token || r.token!=s->fLifecycle.token) return kIOReturnNotFound;
  if (r.action==1) { *r.status=s->fLifecycle;return kIOReturnSuccess; }
  if (s->lifecycleBusy() || s->fLifecycleParked)
   s->finishLifecycle(kIOReturnAborted,"consumer cancelled");
  return kIOReturnSuccess;
 }
 /* Takeover needs the retained pool and channel from beginScanout(). */
 if (r.operation==SweetPanelTakeover) return kIOReturnBadArgument;
 return s->beginLifecycle(r.operation,NULL,r.out);
}
IOReturn SweetMDSS::beginLifecycle(unsigned op,SweetScanoutPool *pool,uint64_t *token)
{
 *token=0;
 if (!fLifecycleEnabled) return kIOReturnUnsupported;
 /* Full supply prepare/unprepare and standalone reset are not supported. */
 if (op!=SweetPanelTakeover && op!=SweetPanelDisplayOff && op!=SweetPanelDisplayOn) return kIOReturnUnsupported;
 if (!fAccepting || isInactive() || fDsiPoisoned ||
     fLifecycle.phase==SweetPanelFault || fLifecycle.phase==SweetPanelStopped) return kIOReturnNotReady;
 if (lifecycleBusy() || dsiBusy() || clockBusy() || noCBusy() || hostBusy() || linkBusy()) return kIOReturnBusy;
 if (fLifecycleNextToken==~0ULL) return kIOReturnNoResources;
 if (!lifecycleParticipants()) {
  SWEET_LOG_ERROR("SWEET PANEL: begin rejected operation=%u participant unavailable; no touch/display request\n",op);
  return kIOReturnNotReady;
 }
 if (fLifecycleFull && op!=SweetPanelTakeover && !fPanelBrightnessKnown) return kIOReturnNotReady;
 bool available=false;
 IOReturn worker=fTouchPanel->copyWorkerAvailable(&available);
 if (worker!=kIOReturnSuccess || (!available && op!=SweetPanelTakeover)) {
  SWEET_LOG_ERROR("SWEET PANEL: begin rejected operation=%u touch-worker rc=%#x available=%u; no display request\n",op,(unsigned)worker,available);
  return worker ? worker : kIOReturnNotReady;
 }
 /* Matching the native provider precedes the asynchronous TouchComm worker.
  * Only FIRST takeover may wait without a touch token or display effects.
  * Power transitions still require their established worker/fence. */
 if (op==SweetPanelTakeover) {
  if (!pool || fLifecycle.token || fScanoutUsed || fLifecycleParked) return kIOReturnBusy;
 } else {
  if (fLifecycleLinkCycle && op==SweetPanelDisplayOff && !lifecycleLinkResourcesHeld()) return kIOReturnNotReady;
  bool poweredDown=fLifecycleLinkCycle && op==SweetPanelDisplayOn;
  bool parked=fLifecycleDataParking && op==SweetPanelDisplayOn;
  if (fScanoutState!=SweetScanoutActive || !scanoutConfiguration(false) ||
      (poweredDown ? ((parked ? (fManagedState!=ManagedParked || !managedConfigHeld() || !managedRetainedResources()) : !lifecycleLinkResourcesHeld()) || !linkDormantConfiguration()) :
       (parked ? (fManagedState!=ManagedParked || !managedConfigHeld() || !managedRetainedResources()) : !lifecycleResourcesHeld()))) return kIOReturnNotReady;
  if ((op==SweetPanelDisplayOff && (fPanelState!=PanelOn || fLifecycleParked)) ||
      (op==SweetPanelDisplayOn && (fPanelState!=PanelOff || !fLifecycleParked))) return kIOReturnNotReady;
 }
 uint64_t touch=0;
 if (op==SweetPanelDisplayOn) {
  SweetTouchPanelStatus t={};
  if (fTouchPanel->copyStatus(fTouchPanelConsumer,fLifecycle.touchToken,&t)!=kIOReturnSuccess ||
      !t.quiesced || t.uncertain || t.result!=kIOReturnSuccess) return kIOReturnNotReady;
  touch=t.token;
 } else if (available) {
  IOReturn rc=fTouchPanel->quiesce(fTouchPanelConsumer,&touch);
  if (rc!=kIOReturnSuccess) return rc; /* no lifecycle token, no display effect */
 }
 if (fLifecycleSupplyCycle && op==SweetPanelDisplayOn &&
     (!fLifecycle.panelVotesParked || fLifecycle.panelReleasedMask!=3u || fManagedRails!=12u || !fLifecycle.resetHeldLow)) return kIOReturnNotReady;
 uint64_t pinToken=fLifecycle.pinToken;
 unsigned released=fLifecycleSupplyCycle && op==SweetPanelDisplayOn ? fLifecycle.panelReleasedMask : 0u;
 unsigned aggregate=fLifecycleSupplyCycle && op==SweetPanelDisplayOn ? fLifecycle.panelAggregateEnabledMask : 3u;
 uint64_t previousVotes[2]={fLifecycle.panelVoteTokens[0],fLifecycle.panelVoteTokens[1]};
 fLifecycleColorRestored=false;
 fLifecycle={};fWakeTraceCount=0;fLifecycle.token=++fLifecycleNextToken;fLifecycle.operation=op;
 fLifecycle.pinToken=pinToken;
 fLifecycle.pipelineGeneration=fPipelineEnabled && op==SweetPanelDisplayOff ? fPipelineGeneration : 0;
 fLifecycle.panelReleasedMask=released;fLifecycle.panelAggregateEnabledMask=aggregate;
 if (released) {fLifecycle.panelVoteTokens[0]=previousVotes[0];fLifecycle.panelVoteTokens[1]=previousVotes[1];}
 fLifecycle.panelVotesParked=released==3u;fLifecycle.resetHeldLow=released==3u;fLifecycleSupplyWait=0;fLifecycle.profile=fPanelProfile ? fPanelProfile->id : 0;
 fLifecycle.sleeping=fLifecycleFull && op==SweetPanelDisplayOn;
 fLifecycle.touchToken=touch;fLifecycle.result=kIOReturnNotReady;
 fLifecycle.phase=op==SweetPanelDisplayOn ? SweetPanelDrain : SweetPanelTouchPause;
 fLifecycle.touchQuiesced=op==SweetPanelDisplayOn;
 fLifecycle.resourcesParked=fLifecycleDataParking && op==SweetPanelDisplayOn;
 fLifecycle.linkPoweredDown=fLifecycleLinkCycle && op==SweetPanelDisplayOn;
 fLifecycle.resourcesHeld=op!=SweetPanelTakeover && !fLifecycle.resourcesParked && !fLifecycle.linkPoweredDown;fLifecycle.rails=fManagedRails;
 fLifecycle.retainedPower=fLifecycle.resourcesParked || fLifecycle.linkPoweredDown;fLifecycle.configHeld=fLifecycle.resourcesParked;
 fLifecycleDeadline=scanout_now()+16000000000ULL;
 fLifecycleStepDeadline=scanout_now()+(op==SweetPanelDisplayOn ? 50000000ULL : 6500000000ULL);fLifecycleIdleSamples=0;
 if (!available) {
  /* An initial dependency wait has its own bound. The original 16s lifecycle
   * and 6.5s touch-pause budgets start when the worker accepts quiesce. */
  fLifecycleDeadline=fLifecycleStepDeadline=scanout_now()+30000000000ULL;
  SWEET_LOG_INFO("SWEET PANEL: takeover waiting for touch worker; startup bound=30000ms, no touch/display request\n");
 }
 if (fLifecycleSupplyCycle && op==SweetPanelDisplayOn) {
  fLifecycle.displayDrained=true;fLifecycle.retainedPower=false;
  /* Enter the existing parked-rail admission phase before taking DSI's
   * suspended reference. No pin or supply request has been issued yet. */
  fLifecycle.phase=SweetPanelSupplyActivate;
  if (!prepareDisplayClients(true)) {
   finishLifecycle(kIOReturnNotReady,"DSI prepare suspended resource admission");*token=fLifecycle.token;return kIOReturnSuccess;
  }
  lifecycleStartSupplyRestore();
  if (!lifecycleBusy()) {*token=fLifecycle.token;return kIOReturnSuccess;}
 } else if (fLifecycle.linkPoweredDown && !fLifecycle.resourcesParked) {
  fLifecycle.phase=SweetPanelLinkRebuild;fLifecycle.displayDrained=true;
  fLifecycleStepDeadline=scanout_now()+1000000000ULL;
  IOReturn rc=resumeLink();
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"link resume admission");*token=fLifecycle.token;return kIOReturnSuccess;}
 } else if (fLifecycle.resourcesParked) {
  fLifecycle.phase=SweetPanelResourceRestore;fLifecycle.displayDrained=true;
  fManagedDeadline=fLifecycleStepDeadline=scanout_now()+4000000000ULL;
  IOReturn rc=beginManagedParking(true);
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"data restore admission");*token=fLifecycle.token;return kIOReturnSuccess;}
  fLifecycleStepDeadline=scanout_now()+4000000000ULL;
 }
 if (fLifecycleLineage && op==SweetPanelDisplayOff && !prepareDisplayClients()) {
  finishLifecycle(kIOReturnNotReady,"DSI pre-disable resource admission");*token=fLifecycle.token;return kIOReturnSuccess;
 }
 if (pool) {pool->retain();fLifecyclePool=pool;}
 *token=fLifecycle.token;publishLifecycle();fLifecycleTimer->setTimeoutMS(available ? 1 : 50);
 SWEET_LOG_INFO("SWEET PANEL: begin token=%llu operation=%u; admitted, not completed\n",fLifecycle.token,op);
 return kIOReturnSuccess;
}
void SweetMDSS::finishLifecycle(IOReturn result,const char *why,bool stopped)
{
 if (!fLifecycle.token || fLifecycle.phase==SweetPanelFault || fLifecycle.phase==SweetPanelStopped) return;
 if (fLifecycleTimer) fLifecycleTimer->cancelTimeout();
 if (result!=kIOReturnSuccess) {
  /* Invalidate before cancelling children. No callback can advance a dead
   * generation. Keep all votes/pins and any DMA lifetime pin, no auto resume,
   * old TE replay, reset, powerdown or background transport continuation. */
  fLifecycle.phase=stopped ? SweetPanelStopped : SweetPanelFault;
  fLifecycle.result=result;fLifecycle.uncertain=true;fPanelState=PanelUnknown;
  fPipelinePending=false;fPipelineConfigured=false;
  lifecycleCancelSupply(); /* synchronize child BEFORE invalidating leaf tokens */
  if (fPanelPins && fLifecycle.pinToken)
   (void)fPanelPins->cancelRequest(fPanelPinsConsumer,fLifecycle.pinToken);
  if (fTouchPanel && fLifecycle.touchToken)
   (void)fTouchPanel->cancelRequest(fTouchPanelConsumer,fLifecycle.touchToken);
  if (dsiBusy()) finishDSI(result,SweetDSICancelled,true);
  cancelScanout(why);
  failManagedResources(result,why);
  pauseIRQ();
  fDsiPoisoned=true;fLifecycle.touchQuiesced=false;
  publishPanelState();
  SWEET_LOG_ERROR("SWEET PANEL: token=%llu failed rc=%#x %s; remaining resources retained, no recovery\n",fLifecycle.token,(unsigned)result,why);
 } else {
  fLifecycle.phase=SweetPanelComplete;fLifecycle.result=result;
  fLifecycleParked=fLifecycle.operation==SweetPanelDisplayOff;
  SWEET_LOG_INFO("SWEET PANEL: token=%llu complete operation=%u parked=%u data-parked=%u data-restored=%u link-down=%u link-rebuilt=%u own-rails=%#x panel-released=%#x; no physical rail-off claim\n",fLifecycle.token,fLifecycle.operation,fLifecycleParked,fLifecycle.resourcesParked,fLifecycle.resourcesRestored,fLifecycle.linkPoweredDown,fLifecycle.linkRebuilt,fManagedRails,fLifecycle.panelReleasedMask);
 }
 publishLifecycle();
 /* Wake deferred typed clients (e.g. brightness) without a reverse gate
  * call. Their interest callback only queues their own software event. */
 if (result==kIOReturnSuccess) messageClients(kIOMessageServicePropertyChange);
}
void SweetMDSS::restoreLifecycleStream(void)
{
 /* A post-brightness restart reuses this wake generation. Rebuilding it
  * again would invalidate the first-frame proof and reset ownership. */
 if (fLifecycleLineage && fLifecycle.operation==SweetPanelDisplayOn && !fLifecycleColorRestored) {
  finishLifecycle(kIOReturnNotReady,"SDR color restore absent before pixels");return;
 }
 if (!startMDPClient()) {finishLifecycle(kIOReturnNotReady,"MDP scan resource admission");return;}
 bool brightnessResume=fLifecycleLineage && fLifecycle.phase==SweetPanelBrightness;
 if (fPipelineEnabled && fLifecycle.phase!=SweetPanelPipelineRestore && !brightnessResume) {
  IOReturn rc=beginPipeline();
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"pipeline wake configuration admission");return;}
  fLifecycle.phase=SweetPanelPipelineRestore;fLifecycleStepDeadline=scanout_now()+100000000ULL;
  return;
 }
 if (fLifecycleLineage && !fPipelineEnabled) dp_wr(dp_intf1,0x284,1);
 const vm_offset_t top=fMaps[9]->getVirtualAddress();
 dp_wr(top,0x1018,1u<<8);
 if (dp_rd(top,0x1014)&(1u<<8)) { finishLifecycle(kIOReturnIOError,"stale pixel done did not clear");return; }
 fLifecycle.pixelsComplete=false;
 fLifecycleFrame=fFrameCount;fPanelState=PanelOn;fIrqDisplayOn=true;publishPanelState();
 if (!armIRQ()) { finishLifecycle(kIOReturnIOError,"pixel IRQ rearm");return; }
 if (fPipelineEnabled) {
  if (!fPipelineConfigured || fLifecycle.pipelineGeneration!=fPipelineGeneration || !scanoutConfiguration(false)) {
   finishLifecycle(kIOReturnNotReady,"pipeline wake generation absent");return;
  }
  fLifecycleTE=(1u<<20)|(1u<<19)|135u;fLifecycleAuto=0x80000001u;
  kickoffPipeline();
 } else if (fLifecycleLineage) {
  if (!fHardwarePool || !fHardwarePool->hardwarePinned() || !scanoutConfiguration(false) ||
      dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c)) {
   finishLifecycle(kIOReturnNotReady,"owned pipeline wake admission");return;
  }
  /* Recommit the lifetime-pinned owned source, then flush before kickoff.
   * Command-mode autorefresh is the kickoff: Lineage does not CTL_START it. */
  dp_wr(dp_sspp,8,0x00300016u);dp_wr(dp_sspp,0x24,4608);
  dp_wr(dp_sspp,0x14,(uint32_t)fHardwarePool->physical(0));
  dp_wr(dp_intf1,0x288,fLifecycleTE); /* encoder enable before KMS commit */
  dp_wr(dp_ctl,0x18,1u);
  uint32_t frameCount=fLifecycleAuto&0xffffu;
  fLifecycleAuto=frameCount ? (1u<<31)|frameCount : 0u;
  dp_wr(dp_intf1,0x2b4,fLifecycleAuto);
  if (!frameCount) dp_wr(dp_ctl,0x1c,1u);
  SWEET_LOG_INFO("SWEET PANEL LINEAGE: owned source recommitted/flush issued; auto=%#x, fresh pixels pending\n",fLifecycleAuto);
 } else {
  dp_wr(dp_intf1,0x2b4,fLifecycleAuto);dp_wr(dp_intf1,0x288,fLifecycleTE);
 }
 fLifecycle.phase=SweetPanelFrame;fLifecycleStepDeadline=scanout_now()+100000000ULL;
}
void SweetMDSS::lifecycleTimerAction(OSObject *owner,IOTimerEventSource *sender)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!s->lifecycleBusy()) return;
 s->advanceLifecycle();
 if (s->lifecycleBusy()) sender->setTimeoutMS(s->fLifecycle.phase==SweetPanelTouchPause && !s->fLifecycle.touchToken ? 50 : 1);
}
void SweetMDSS::advanceLifecycle(void)
{
 bool pinPhase=fLifecycle.phase==SweetPanelPinActivate || fLifecycle.phase==SweetPanelPinReset;
 if (pinPhase) traceWake("participants-enter");
 if (!lifecycleParticipants()) {finishLifecycle(kIOReturnNotReady,"participant unavailable");return;}
 if (pinPhase) traceWake("participants-return");
 uint64_t now=scanout_now();
 if (now>=fLifecycleDeadline || now>=fLifecycleStepDeadline) {
  SWEET_LOG_ERROR("SWEET PANEL: deadline token=%llu operation=%u phase=%u command=%llu completed=%u/%u\n",
      fLifecycle.token,fLifecycle.operation,fLifecycle.phase,fLifecycle.commandToken,fDsiStatus.completed,fDsiStatus.total);
  finishLifecycle(kIOReturnTimeout,fLifecycle.phase==SweetPanelTouchPause && !fLifecycle.touchToken ?
      "touch worker startup deadline" : "lifecycle deadline");return;
 }
 if (lifecycleSupplyPhase()) {advanceLifecycleSupply();return;}
 if (fLifecycle.phase==SweetPanelTouchPause || fLifecycle.phase==SweetPanelTouchResume) {
  if (!fLifecycle.touchToken) {
   if (fLifecycle.operation!=SweetPanelTakeover) {finishLifecycle(kIOReturnIOError,"touch token absent");return;}
   bool available=false;
   IOReturn rc=fTouchPanel->copyWorkerAvailable(&available);
   if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"touch startup provider unavailable");return;}
   if (!available) return; /* Software snapshot only; no transport/MMIO poll. */
   uint64_t touch=0;rc=fTouchPanel->quiesce(fTouchPanelConsumer,&touch);
   if (rc!=kIOReturnSuccess || !touch) {finishLifecycle(rc ? rc : kIOReturnIOError,"touch startup quiesce refused");return;}
   fLifecycle.touchToken=touch;fLifecycleDeadline=now+16000000000ULL;
   fLifecycleStepDeadline=now+6500000000ULL;publishLifecycle();
   SWEET_LOG_INFO("SWEET PANEL: touch worker ready; takeover quiesce token=%llu, normal deadlines started\n",touch);
   return;
  }
  SweetTouchPanelStatus t={};
  if (fTouchPanel->copyStatus(fTouchPanelConsumer,fLifecycle.touchToken,&t)!=kIOReturnSuccess ||
      t.uncertain || (t.result!=kIOReturnSuccess && t.result!=kIOReturnNotReady)) {
   finishLifecycle(kIOReturnIOError,"touch fence failed");return;
  }
  if (t.result==kIOReturnNotReady) return;
  if (fLifecycle.phase==SweetPanelTouchResume) {
   if (!t.ready || t.quiesced) {finishLifecycle(kIOReturnIOError,"touch resume proof absent");return;}
   fLifecycle.touchQuiesced=false;finishLifecycle(kIOReturnSuccess,"warm lifecycle ready");return;
  }
  if (!t.quiesced || t.ready) {finishLifecycle(kIOReturnIOError,"touch pause proof absent");return;}
  fLifecycle.touchQuiesced=true;
  if (fLifecycle.operation==SweetPanelTakeover) {
   fLifecycleInternal=true;
   IOReturn rc=scanoutBeginAction(this,fLifecyclePool,fVsyncChannel,NULL,NULL);
   fLifecycleInternal=false;
   if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"takeover continuation refused");return;}
   fLifecycle.phase=SweetPanelTakeoverPending;fLifecycleStepDeadline=now+5000000000ULL;
  } else {
   fLifecycleTE=dp_rd(dp_intf1,0x288);fLifecycleAuto=dp_rd(dp_intf1,0x2b4);
   dp_wr(dp_intf1,0x288,fLifecycleTE&~(1u<<20));dp_wr(dp_intf1,0x2b4,0);
   pauseIRQ();fLifecycle.phase=SweetPanelDrain;fLifecycleStepDeadline=now+50000000ULL;
  }
  publishLifecycle();return;
 }
 if (fLifecycle.phase==SweetPanelTakeoverPending) {
  if (fScanoutState==SweetScanoutFailed || fManagedState==ManagedFault) {finishLifecycle(kIOReturnIOError,"takeover child failed");return;}
  if (fScanoutState!=SweetScanoutActive) return;
  if (!lifecycleResourcesHeld()) {finishLifecycle(kIOReturnNotReady,"takeover resources not held");return;}
  fLifecycle.resourcesHeld=fLifecycle.pixelsComplete=true;fLifecycle.rails=fManagedRails;
 } else if (fLifecycle.phase==SweetPanelDrain) {
  if (!lifecycleResourcesHeld() || !dp_clocks_on()) {finishLifecycle(kIOReturnNotReady,"warm resource lease lost");return;}
  if (!lifecycleIRQDrained() || (dp_rd(dp_dsi,8)&6u)) return;
  if (dp_rd(dp_intf1,0x2b4) || (dp_rd(dp_intf1,0x288)&(1u<<20))) {finishLifecycle(kIOReturnIOError,"stream pause drift");return;}
  uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
  if (line>0 && line<2340) fLifecycleIdleSamples=0;else ++fLifecycleIdleSamples;
  if (fLifecycleIdleSamples<2) return;
  fLifecycle.displayDrained=true;
  if (fLifecycleLineage && fLifecycle.operation==SweetPanelDisplayOff) {
   if (!stopMDPClient()) {finishLifecycle(kIOReturnNotReady,"MDP drained resource release");return;}
   /* command encoder disable precedes bridge/display disable and OFF table. */
   dp_wr(dp_intf1,0x284,0);
   if (!setDSIDisplayEngine(false)) {finishLifecycle(kIOReturnNotReady,"display engine release");return;}
  }
  if (fLifecycleFull && fLifecycle.operation==SweetPanelDisplayOn) {
   lifecycleStartReset();
  } else lifecycleCommand(fLifecycle.operation==SweetPanelDisplayOn);
  publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelLinkPowerDown || fLifecycle.phase==SweetPanelLinkRebuild) {
  if (!lifecycleLinkStepAllowed() || fLinkPhase==LinkFault || fLinkPhase==LinkStopped ||
      fHostPhase==HostFault || fHostPhase==HostStopped) {finishLifecycle(kIOReturnNotReady,"link child/dependency lost");return;}
  if (linkBusy() || hostBusy()) return;
  if (fLifecycle.phase==SweetPanelLinkPowerDown) {
   if (fLinkResult!=kIOReturnSuccess || !linkDormantConfiguration()) {finishLifecycle(kIOReturnIOError,"link shutdown completion absent");return;}
   if (!unprepareDisplayClients()) {finishLifecycle(kIOReturnNotReady,"DSI unprepare resource release");return;}
   fLifecycle.linkPoweredDown=fLifecycle.retainedPower=true;
   if (fLifecycleDataParking) {
    if (fManagedState!=ManagedParkLink) {finishLifecycle(kIOReturnIOError,"park link handoff absent");return;}
    fManagedState=ManagedParkNoC;fLifecycle.phase=SweetPanelResourcePark;
    fManagedDeadline=fLifecycleStepDeadline=now+4000000000ULL;publishLifecycle();
   } else finishLifecycle(kIOReturnSuccess,"host/PHY powered down; votes retained");
   return;
  }
  if (fLinkPhase!=LinkReady || fLinkResult!=kIOReturnSuccess || !linkConfiguration()) {finishLifecycle(kIOReturnIOError,"link rebuild completion absent");return;}
  if (fLifecycleDataParking && fManagedState==ManagedRestoreLink) {
   fManagedState=ManagedRestoreClock;fLifecycle.phase=SweetPanelResourceRestore;
   fManagedDeadline=fLifecycleStepDeadline=now+4000000000ULL;publishLifecycle();return;
  }
  if (fHostPhase==HostDormant) {
   IOReturn rc=beginHost();if (rc!=kIOReturnSuccess) finishLifecycle(rc,"host resume admission");
   return;
  }
  if (fHostPhase!=HostReady || fHostResult!=kIOReturnSuccess || !hostConfiguration() || !lifecycleResourcesHeld()) {
   finishLifecycle(kIOReturnIOError,"host rebuild completion absent");return;
  }
  fLifecycle.linkPoweredDown=false;fLifecycle.linkRebuilt=fLifecycle.retainedPower=fLifecycle.resourcesHeld=true;
  lifecycleStartReset();publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelResourcePark || fLifecycle.phase==SweetPanelResourceRestore) {
  advanceManagedParking();
  if (!lifecycleBusy()) return;
  bool restore=fLifecycle.phase==SweetPanelResourceRestore;
  if (fLifecycleLinkCycle && fManagedState==(restore ? ManagedRestoreLink : ManagedParkLink)) {
   fLifecycle.phase=restore ? SweetPanelLinkRebuild : SweetPanelLinkPowerDown;
   fLifecycleStepDeadline=now+1000000000ULL;
   IOReturn rc=restore ? resumeLink() : beginLink(true);
   if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"resource/link handoff refused");return;}
   publishLifecycle();return;
  }
  if (fManagedState!=(restore ? ManagedReady : ManagedParked)) {publishLifecycle();return;}
  if (!managedRetainedResources() || !managedConfigHeld() ||
      (restore ? (fLifecycleLinkCycle ? !lifecycleLinkResourcesHeld() : !lifecycleResourcesHeld()) :
       (fManagedClock || fManagedBandwidth || (fLifecycleLinkCycle && !linkDormantConfiguration())))) {
   finishLifecycle(kIOReturnIOError,"data resource terminal proof absent");return;
  }
  fLifecycle.retainedPower=fLifecycle.configHeld=true;fLifecycle.resourcesParked=!restore;
  fLifecycle.resourcesHeld=restore && !fLifecycleLinkCycle;fLifecycle.resourcesRestored=restore;
  if (restore && fLifecycleLinkCycle) {
   fLifecycle.phase=SweetPanelLinkRebuild;fLifecycleStepDeadline=now+1000000000ULL;
   /* DATA and both clock tickets are Held before host reset can begin. */
  } else if (restore) lifecycleStartReset();
  else if (fLifecycleSupplyCycle) {
   fLifecycle.phase=SweetPanelSupplyPark;fLifecycle.supplyRail=SweetRailVPNL;
   fLifecycleStepDeadline=now+400000000ULL;fLifecycleSupplyWait=0;
  } else finishLifecycle(kIOReturnSuccess,"data leases parked; power/config retained");
  publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelPinActivate || fLifecycle.phase==SweetPanelPinReset) {
  traceWake("reset-dependencies-enter");
  if (!lifecycleResourcesHeld() || !lifecycleIRQDrained() || (dp_rd(dp_dsi,8)&6u)) {
   finishLifecycle(kIOReturnNotReady,"reset dependency lost");return;
  }
  traceWake("pin-status-enter");
  SweetPanelPinStatus pin={};
  if (fPanelPins->copyStatus(fPanelPinsConsumer,fLifecycle.pinToken,&pin)!=kIOReturnSuccess || pin.uncertain ||
      (pin.result!=kIOReturnNotReady && pin.result!=kIOReturnSuccess)) {finishLifecycle(kIOReturnIOError,"pin operation failed");return;}
  traceWake("pin-status-return",pin.state);
  if (pin.result==kIOReturnNotReady) return;
  unsigned expected=fLifecycle.phase==SweetPanelPinActivate ? SweetPinsActive : SweetPinsResetComplete;
  if (pin.state!=expected || !pin.resetKnown || !pin.resetHigh) {finishLifecycle(kIOReturnIOError,"reset proof absent");return;}
  if (fLifecycle.phase==SweetPanelPinActivate) {fLifecyclePinsActive=true;lifecycleReset();}
  else {fLifecycle.resetComplete=true;traceWake("reset-proof-ready");lifecycleCommand(true);}
  publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelCommand || fLifecycle.phase==SweetPanelBrightness || fLifecycle.phase==SweetPanelColorRestore) {
  if (!lifecycleResourcesHeld()) {finishLifecycle(kIOReturnNotReady,"command resource lease lost");return;}
  if (dsiBusy()) return;
  if (fDsiStatus.token!=fLifecycle.commandToken || fDsiStatus.state!=SweetDSIComplete ||
      fDsiStatus.result!=kIOReturnSuccess || fDsiStatus.hardwareUncertain) {finishLifecycle(kIOReturnIOError,"display command failed");return;}
  if (fLifecycle.operation==SweetPanelDisplayOff) {
   fPanelState=PanelOff;fIrqDisplayOn=false;publishPanelState();
   fLifecycle.sleeping=fLifecycleFull;
   if (fLifecycleSupplyCycle) {
    lifecycleStartPinPark();publishLifecycle();
   } else if (fLifecycleLinkCycle && !fLifecycleDataParking) {
    fLifecycle.phase=SweetPanelLinkPowerDown;fLifecycleStepDeadline=now+1000000000ULL;
    IOReturn rc=beginLink(true);
    if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"link shutdown admission");return;}
    fLifecycle.resourcesHeld=false;publishLifecycle();
   } else if (fLifecycleDataParking) {
    fLifecycle.phase=SweetPanelResourcePark;
    fManagedDeadline=fLifecycleStepDeadline=now+4000000000ULL;
    IOReturn rc=beginManagedParking(false);
    if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"data park admission");return;}
    fLifecycle.resourcesHeld=false;fLifecycleStepDeadline=now+4000000000ULL;publishLifecycle();
   } else finishLifecycle(kIOReturnSuccess,fLifecycleFull ? "full sleep in; all votes retained" : "display off, not sleep/rail-off");
   return;
  }
  if (fLifecycleFull && fLifecycle.phase==SweetPanelCommand) {
   /* dsi_display_enable acquires its persistent reference AFTER panel ON. */
   if (!setDSIDisplayEngine(true)) {finishLifecycle(kIOReturnNotReady,"display engine acquire");return;}
   fLifecycle.sleeping=false;
   if (fLifecycleLineage) lifecycleColor();else lifecycleBrightness();
   publishLifecycle();return;
  }
  if (fLifecycle.phase==SweetPanelColorRestore) {
   if (!lifecycleResourceStepAllowed() || fDsiStatus.completed!=6 || fDsiStatus.total!=6) {
    finishLifecycle(kIOReturnNotReady,"SDR color completion proof absent");return;
   }
   fLifecycleColorRestored=true;
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: SDR Auto color/sharpness HS complete; pixels pending\n");
  }
  if (fLifecycle.phase==SweetPanelBrightness) {
   /* A newer target can arrive while DMA is in flight. Keep the original
    * brightness deadline, and acknowledge only the most recent completed one. */
   if (fLifecycleLineage && fLifecycleBrightnessTarget!=fPanelBrightnessTarget) {
    lifecycleBrightness();publishLifecycle();return;
   }
   fLifecycle.brightnessRestored=true;
   if (fLifecycleLineage) SWEET_LOG_INFO("SWEET PANEL LINEAGE: post-frame HS brightness complete target=%#x; stream restart pending\n",fLifecycleBrightnessTarget);
  }
  restoreLifecycleStream();publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelBrightnessDrain) {
  if (!lifecycleResourcesHeld() || !scanoutConfiguration(false) ||
      (fPipelineEnabled && (!fPipelineConfigured || fLifecycle.pipelineGeneration!=fPipelineGeneration))) {
   finishLifecycle(kIOReturnNotReady,"post-frame brightness resources lost");return;
  }
  if (!lifecycleIRQDrained() || (dp_rd(dp_dsi,8)&6u) || dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c)) return;
  if ((dp_rd(dp_intf1,0x288)&(1u<<20)) || dp_rd(dp_intf1,0x2b4)) {
   finishLifecycle(kIOReturnIOError,"post-frame brightness pause drift");return;
  }
  uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
  if (line>0 && line<2340) fLifecycleIdleSamples=0;else ++fLifecycleIdleSamples;
  if (fLifecycleIdleSamples<2) return;
  lifecycleBrightness();publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelPipelineRestore) {
  IOReturn rc=advancePipeline();
  if (rc==kIOReturnBusy) return;
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"pipeline wake configuration failed");return;}
  restoreLifecycleStream();publishLifecycle();return;
 } else if (fLifecycle.phase==SweetPanelFrame) {
  unsigned irq=__atomic_load_n(&fIrqState,__ATOMIC_ACQUIRE);
  if ((fPipelineEnabled && (!fPipelineConfigured || fLifecycle.pipelineGeneration!=fPipelineGeneration)) || !lifecycleResourcesHeld() || !scanoutConfiguration(false) || (irq!=IrqArmed && irq!=IrqPending)) {finishLifecycle(kIOReturnNotReady,"resumed pixel participants lost");return;}
  observeLineage("lifecycle-auto",dp_intf1,0x2b4,fLifecycleAuto);
  observeLineage("lifecycle-te",dp_intf1,0x288,fLifecycleTE);
  if (dp_rd(dp_ctl,0x18) || !(dp_rd(fMaps[9]->getVirtualAddress(),0x1014)&(1u<<8)) || fFrameCount<=fLifecycleFrame) return;
  fLifecycle.pixelsComplete=true;
  if (fLifecycleLineage && fLifecycle.operation==SweetPanelDisplayOn &&
      (!fLifecycle.brightnessRestored || fLifecycleBrightnessTarget!=fPanelBrightnessTarget)) {
   /* LG complete_commit -> post_kickoff unlocks pending brightness only
    * after pixels. Our sequencer additionally needs bounded IRQ/MDP drain. */
   fLifecycle.brightnessRestored=false;
   dp_wr(dp_intf1,0x288,fLifecycleTE&~(1u<<20));dp_wr(dp_intf1,0x2b4,0);
   pauseIRQ();fLifecycleIdleSamples=0;
   fLifecycle.phase=SweetPanelBrightnessDrain;fLifecycleStepDeadline=now+50000000ULL;
   SWEET_LOG_INFO("SWEET PANEL LINEAGE: fresh frame before brightness; draining IRQ/MDP target=%#x\n",fPanelBrightnessTarget);
   publishLifecycle();return;
  }
 } else return;
 if (!postEnableDisplayClients()) {finishLifecycle(kIOReturnNotReady,"DSI post-enable resource handoff");return;}
 /* A retained rail lease plus fresh pixel transfer precedes touch recovery.
  * This is NOT a proof that reset/shared-rail power-cycling is safe. */
 uint64_t touch=0;IOReturn rc=fTouchPanel->resume(fTouchPanelConsumer,&touch);
 if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"touch resume admission");return;}
 fLifecycle.touchToken=touch;fLifecycle.touchQuiesced=false;fLifecycle.phase=SweetPanelTouchResume;
 fLifecycleStepDeadline=scanout_now()+6500000000ULL;publishLifecycle();
}

/* Children only queue work. Every advance validates participants and the same
 * touch fence; hardware operations remain in their original service owner. */
void SweetMDSS::lifecycleReset(void)
{
 fLifecycle.resetHeldLow=false;
 IOReturn rc=fPanelPins->reset(fPanelPinsConsumer,&fLifecycle.pinToken);
 if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"reset admission");return;}
 fLifecycle.phase=SweetPanelPinReset;fLifecycleStepDeadline=scanout_now()+100000000ULL;
}
void SweetMDSS::lifecycleCommand(bool on)
{
 const uint8_t warm[]={5,1,0,0,0,0,1,(uint8_t)(on ? 0x29 : 0x28)};
 const uint8_t *bytes=warm;size_t length=sizeof(warm);
 if (fLifecycleFull) {
  bytes=on ? fPanelProfile->on : fPanelProfile->off;
  length=on ? fPanelProfile->onSize : fPanelProfile->offSize;
 }
 traceWake("command-enter",(unsigned)length);
 fLifecycleInternal=true;
 IOReturn rc=beginDSISequence(bytes,length,&fLifecycle.commandToken);
 fLifecycleInternal=false;
 traceWake("command-return",(unsigned)rc);
 if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"display command admission");return;}
 fPanelState=on ? PanelOnPending : PanelOffPending;publishPanelState();
 fLifecycle.phase=SweetPanelCommand;
 fLifecycleStepDeadline=scanout_now()+(fLifecycleFull ? 5000000000ULL : 500000000ULL);
}
void SweetMDSS::lifecycleColor(void)
{
 const SweetPanelSDRColor *color=fPanelProfile ? sweet_panel_sdr_color(fPanelProfile->id) : NULL;
 fLifecycle.phase=SweetPanelColorRestore;fLifecycleStepDeadline=scanout_now()+500000000ULL;
 if (!color || !lifecycleResourceStepAllowed()) {
  finishLifecycle(kIOReturnNotReady,"SDR color restore admission");return;
 }
 /* LG mode_set runs after ON/display-engine acquire, before first kickoff.
  * Keep both three-packet sets and their per-profile delays in one bounded
  * transaction. Any error stops before pixels; no best-effort sharpness. */
 fLifecycleInternal=true;
 IOReturn rc=beginDSISequenceMode(color->commands,color->size,&fLifecycle.commandToken,true);
 fLifecycleInternal=false;
 if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"SDR color sequence admission");return;}
 SWEET_LOG_INFO("SWEET PANEL LINEAGE: SDR Auto color/sharpness HS admitted profile=%u; six commands before pixels\n",color->id);
}
void SweetMDSS::lifecycleBrightness(void)
{
 if (!fPanelBrightnessKnown) {finishLifecycle(kIOReturnNotReady,"no typed brightness target");return;}
 bool retry=fLifecycle.phase==SweetPanelBrightness;
 uint32_t level=fPanelBrightnessTarget;fLifecycleBrightnessTarget=level;
 const uint8_t command[]={0x39,1,0,0,0,0,3,0x51,(uint8_t)(level>>8),(uint8_t)level};
 fLifecycleInternal=true;
 IOReturn rc=beginDSISequenceMode(command,sizeof(command),&fLifecycle.commandToken,fLifecycleLineage);
 fLifecycleInternal=false;
 if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"brightness restore admission");return;}
 fLifecycle.phase=SweetPanelBrightness;
 if (!retry) fLifecycleStepDeadline=scanout_now()+500000000ULL;
}

void SweetMDSS::lifecycleStartReset(void)
{
 if (!fLifecyclePinsActive) {
  IOReturn rc=fPanelPins->activate(fPanelPinsConsumer,&fLifecycle.pinToken);
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"active pin admission");return;}
  fLifecycle.phase=SweetPanelPinActivate;fLifecycleStepDeadline=scanout_now()+100000000ULL;
 } else lifecycleReset();
}
/* Also used by child clock/NoC timers before advancing any queued write. The
 * sleeping panel never grants permission to revoke boot/core/link resources. */
bool SweetMDSS::lifecycleResourceStepAllowed(void)
{
 if (!lifecycleParticipants()) return false;
 if (fLifecycle.phase==SweetPanelColorRestore) {
  return fLifecycleLineage && fLifecycle.operation==SweetPanelDisplayOn &&
      scanout_now()<fLifecycleDeadline && scanout_now()<fLifecycleStepDeadline &&
      fLifecycle.resetComplete && !fLifecycle.pixelsComplete && !fLifecycleColorRestored &&
      fLifecycle.touchQuiesced && fLifecycle.displayDrained && fDsiDisplayEngine &&
      lifecycleResourcesHeld() && lifecycleIRQDrained() &&
      !(dp_rd(dp_intf1,0x288)&(1u<<20)) && !dp_rd(dp_intf1,0x2b4) &&
      !dp_rd(dp_ctl,0x18) && !dp_rd(dp_ctl,0x1c);
 }
 if (fLifecycleLineage && fLifecycle.phase==SweetPanelBrightness) {
  return scanout_now()<fLifecycleDeadline && scanout_now()<fLifecycleStepDeadline &&
      fLifecycle.pixelsComplete && lifecycleResourcesHeld() && lifecycleIRQDrained() &&
      scanoutConfiguration(false) && (!fPipelineEnabled || (fPipelineConfigured && fLifecycle.pipelineGeneration==fPipelineGeneration)) &&
      !(dp_rd(dp_intf1,0x288)&(1u<<20)) && !dp_rd(dp_intf1,0x2b4) &&
      !dp_rd(dp_ctl,0x18) && !dp_rd(dp_ctl,0x1c);
 }
 if (fLifecycleLinkCycle && (fLifecycle.phase==SweetPanelLinkPowerDown || fLifecycle.phase==SweetPanelLinkRebuild))
  return lifecycleLinkStepAllowed();
 if (fLifecycle.phase!=SweetPanelResourcePark && fLifecycle.phase!=SweetPanelResourceRestore) return true;
 if (scanout_now()>=fLifecycleDeadline || scanout_now()>=fLifecycleStepDeadline ||
     scanout_now()>=fManagedDeadline) return false;
 if ((fLifecycleSupplyCycle && !fLifecycle.resetHeldLow) || !fLifecycleDataParking || !fLifecycle.sleeping || !fLifecycle.touchQuiesced || !fLifecycle.displayDrained ||
     !managedRetainedResources() || !lifecycleIRQDrained() || !clockCanGate() || !scanoutConfiguration(false) ||
     dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c)) return false;
 uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
 if (line>0 && line<2340) return false;
 bool initial=fLifecycle.phase==SweetPanelResourcePark && fManagedState==ManagedReady && !fManagedConfigBandwidth;
 if (!initial && fManagedState!=ManagedParkConfig && !managedConfigHeld()) return false;
 return true;
}

/* Only MDP/SMMU/AHB snapshots are allowed during this explicitly admitted
 * link transaction. The wrapper masks PHY reset propagation into that domain. */
bool SweetMDSS::lifecycleLinkAccessible(void) const
{
 return fLifecycleLinkCycle && !fLifecycle.uncertain &&
     fLinkPhase!=LinkFault && fLinkPhase!=LinkStopped && fHostPhase!=HostFault && fHostPhase!=HostStopped &&
     (fLifecycle.phase==SweetPanelLinkPowerDown || fLifecycle.phase==SweetPanelLinkRebuild ||
      (fLifecycleSupplyCycle && (fLifecycle.phase==SweetPanelSupplyPark || fLifecycle.phase==SweetPanelSupplyRestore || fLifecycle.phase==SweetPanelSupplyActivate)) ||
      (fLifecycleDataParking && (fLifecycle.phase==SweetPanelResourcePark || fLifecycle.phase==SweetPanelResourceRestore)) ||
      (fLifecycle.phase==SweetPanelComplete && fLifecycleParked && fLinkPhase==LinkDormant));
}
bool SweetMDSS::lifecycleLinkResourcesHeld(void)
{
 bool bridge=fLifecycleDataParking && (fManagedState==ManagedParkLink || fManagedState==ManagedRestoreLink);
 if (!fAccepting || isInactive() || (!bridge && fManagedState!=ManagedReady) || fManagedRails!=15u ||
     !fRPMh || !fRPMhConsumer || !fManagedARC || fCoreProfile!=2 || !coreReady() ||
     fHostProfile!=1 || fLinkProfile!=2 || fClockFault || fNoCFault || clockBusy() || noCBusy() ||
     dsiBusy() || fDsiPinned || fDsiPoisoned || linkOtherUsers()) return false;
 SweetClockLease c={},b={};SweetNoCStatus n={};SweetDisplayPowerStatus a={};
 if ((!bridge && (localClockStatus(fManagedClock,&c)!=kIOReturnSuccess || c.state!=SweetClockHeld || c.uncertain || c.result!=kIOReturnSuccess ||
      c.mask!=mdssClockMasks[SweetClockDSI0])) || (bridge && (fManagedClock || fManagedToken || !managedConfigHeld())) ||
     localClockStatus(fClockBootToken,&b)!=kIOReturnSuccess || b.state!=SweetClockHeld || b.uncertain || b.result!=kIOReturnSuccess ||
     b.mask!=mdssClockMasks[SweetClockDSI0] ||
     bandwidthStatus(fManagedBandwidth,&n)!=kIOReturnSuccess || n.state!=SweetNoCHeld || n.uncertain || n.result!=kIOReturnSuccess ||
     fRPMh->copyDisplayPowerStatus(fRPMhConsumer,fManagedARC,&a)!=kIOReturnSuccess ||
     a.state!=SweetRPMhComplete || a.uncertain || a.corner!=SweetDisplayTurbo) return false;
 /* An additional link lease is another user, not permission to override its
  * Held promise. Only admitted BOOT/managed and panel-DSI tickets may be
  * suspended; lifecycleLinkStepAllowed requires MDP to have stopped. */
 if (!displayClientsValid()) return false;
 unsigned clients=(fPanelDSIClock ? 1u : 0u)+(fMdpClock ? 1u : 0u);
 for (unsigned i=fLifecycleLineage ? 3u : 4u;i<=6;++i) if (fClockReferences[i]!=(bridge ? 1u : 2u)+clients) return false;
 for (unsigned i=0;i<ClockLeaseSlots;++i) {
  const SweetClockLease &v=fClockLeases[i];
  if (v.state==SweetClockHeld && (v.mask&linkParkedMask()) && v.token!=fClockBootToken && v.token!=fManagedClock && v.token!=fPanelDSIClock && v.token!=fMdpClock) return false;
 }
 for (unsigned i=0;i<SweetClockCount;++i) if ((fClockKnownMask&(1u<<i)) && !((0x70u|(fLinkSuspendedMask&8u))&(1u<<i))) {
  uint32_t parent[6]={},value=dp_rd(dp_dispcc,mdssClockBranches[i].cbcr);
  if (!clockParent(i,parent,true) || !clockBranchRunning(i,value)) return false;
  observeClockParent(i,parent,fClockParents[i]);
 }
 return true;
}
bool SweetMDSS::lifecycleLinkStepAllowed(void)
{
 if (fMdpClock || (fLifecycleSupplyCycle && !fLifecycle.resetHeldLow) || !fLifecycleLinkCycle || !lifecycleParticipants() || !fLifecycle.sleeping || !fLifecycle.touchQuiesced ||
     !fLifecycle.displayDrained || !lifecycleLinkResourcesHeld() || !lifecycleIRQDrained() ||
     scanout_now()>=fLifecycleDeadline || scanout_now()>=fLifecycleStepDeadline ||
     (dp_rd(dp_intf1,0x288)&(1u<<20)) || dp_rd(dp_intf1,0x2b4) ||
     (dp_rd(dp_dsi,8)&6u) || dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c) || !scanoutConfiguration(false)) return false;
 uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
 if (line>0 && line<2340) return false;
 if (fLinkTouched) observeLineage("lifecycle-misc-preserved",dp_dispcc,0x8000,fLinkMisc,~linkMiscMask);
 return true;
}
