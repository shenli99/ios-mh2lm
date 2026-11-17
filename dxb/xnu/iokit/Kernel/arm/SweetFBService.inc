/* F1b: shared legacy handlers, standard dispatch validation and one state gate.
 * No new register writes, wait protocol, frame lifetime, or panel power policy. */

bool
AppleCLCD::setupServiceState(void)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 fStateGate=IOCommandGate::commandGate(this);
 if (!fStateGate || fWorkLoop->addEventSource(fStateGate)!=kIOReturnSuccess) return false;
 fStateGateAdded=true;
 int lifecycle=0; (void)PE_parse_boot_argn("sweetpanellifecycle",&lifecycle,sizeof(lifecycle));
 fPanelLifecycleRequested=lifecycle>=1 && lifecycle<=7;fPanelLifecycleFull=lifecycle>=2 && lifecycle<=7;
 /* Mode2 follows the MDSS Lineage suspend path: parked DATA/link and this
  * panel's withdrawn supply votes are the successful OFF state. Use the
  * same full supply/restore proofs as mode6, not the old all-held contract. */
 fPanelLifecycleDataParking=lifecycle==7 || lifecycle==2 || lifecycle==3 || lifecycle==5 || lifecycle==6;
 fPanelLifecycleLinkCycle=lifecycle==7 || lifecycle==2 || lifecycle==4 || lifecycle==5 || lifecycle==6;
 fPanelLifecycleSupplyCycle=lifecycle==7 || lifecycle==2 || lifecycle==6;
 fPanelLifecyclePipeline=lifecycle==7;
 if (fPanelLifecyclePipeline) SWEET_LOG_INFO("SWEET FB PANEL: mode7 requires a new pipeline generation and fresh pixels on wake\n");
 if (lifecycle==2) SWEET_LOG_INFO("SWEET FB PANEL: Lineage mode2 supply/link/data completion enabled\n");
 if (fPanelLifecycleRequested && (!fHardwareVsyncRequested || !fOwnScanoutRequested)) return false;
 if (fHardwareVsyncRequested && !setupHardwareVsync()) return false;
 fDisplayClient=SweetDisplayClient::withOwner(this,fWorkLoop,backendChanged);
 if (!fDisplayClient || !fDisplayClient->watch()) return false;
 setProperty("SweetFramebufferService",true);
 setProperty("SweetDisplayBackend","SweetMDSS");
 SWEET_LOG_INFO("SWEET FB SERVICE: F1b v1 dispatch and state gate enabled; pixel policy unchanged\n");
 return true;
#else
 return false;
#endif
}

void
AppleCLCD::backendChanged(OSObject *owner)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 AppleCLCD *fb=(AppleCLCD *)owner;
 if (fb->fServiceStopped) return;
 SweetDisplayBackend *backend=fb->fDisplayClient->copyBackend();
 bool bound=backend!=NULL;
 if (fb->fPanelLifecycleBackend && backend!=fb->fPanelLifecycleBackend) {
  fb->stopPanelLifecycle();fb->fPanelLifecycleFault=true;fb->fPowerOff=true;
  fb->setProperty("SweetPanelPowerLastResult",(unsigned long long)(uint32_t)kIOReturnNotReady,32);
 }
 if (fb->fHardwareVsyncRequested) fb->bindHardwareVsync(backend);
 OSSafeReleaseNULL(backend);
 fb->setProperty("SweetDisplayBackendBound",bound);
 if (bound && !fb->fBackendBound)
  SWEET_LOG_INFO("SWEET FB SERVICE: registry backend matched on framebuffer workloop\n");
 if (!bound && fb->fBackendBound)
  SWEET_LOG_WARN("SWEET FB SERVICE: registry backend withdrawn; ABL scanout retained\n");
 fb->fBackendBound=bound;
 /* Matching is not hardware readiness. No provider calls, waits, or MMIO.
  * F1b still uses inherited ABL scanout and does not need MDSS to copy pixels.
  * Future F2 hardware calls must copyBackend and use provider admission. */
#else
 (void)owner;
#endif
}

IOReturn
AppleCLCD::stateControl(unsigned operation,IOMobileFramebufferUserClient *client,
 IOExternalMethodArguments *args,uint32_t selector,mach_port_t port)
{
 if (!fStateGateAdded) return kIOReturnOffline;
 StateRequest request={operation,client,args,selector,port};
 return fStateGate->runAction(stateAction,&request);
}

IOReturn
AppleCLCD::stateAction(OSObject *owner,void *arg0,void *,void *,void *)
{
 AppleCLCD *fb=(AppleCLCD *)owner;
 StateRequest *r=(StateRequest *)arg0;
 IOMobileFramebufferUserClient *client=r->client;
 if (r->operation==StateCloseClient) {
  if (client) client->serviceClose();
  return kIOReturnSuccess;
 }
 if (r->operation==StateStop) {
  if (fb->fServiceStopped) return kIOReturnSuccess;
  fb->fServiceStopped=true;
  if (fb->fVsync) fb->fVsync->cancelTimeout();
#if defined(ARM64_BOARD_CONFIG_MH2LM)
  fb->stopPanelLifecycle();
  fb->shutdownScanout();
  if (fb->fHardwareVsyncRequested) fb->shutdownHardwareVsync();
  OSSafeReleaseNULL(fb->fScanoutPool); /* submitted pools carry an independent hardware pin */
#endif
  while (fb->fClients && fb->fClients->getCount()) {
   IOMobileFramebufferUserClient *c=(IOMobileFramebufferUserClient *)fb->fClients->getObject(0);
   c->retain(); c->serviceClose(); c->release();
  }
  fb->fDirty=false; fb->fBackendBound=false;
  fb->setProperty("SweetDisplayBackendBound",false);
  SWEET_LOG_INFO("SWEET FB SERVICE: state admission closed; clients and ports withdrawn\n");
  return kIOReturnSuccess;
 }
 if (fb->fServiceStopped || fb->isInactive() || !client || client->fServiceClosed)
  return kIOReturnOffline;
 switch (r->operation) {
 case StateAddClient:
  if (fb->fClients->getNextIndexOfObject(client,0)!=(unsigned)-1) return kIOReturnSuccess;
  return fb->fClients->setObject(client)?kIOReturnSuccess:kIOReturnNoMemory;
 case StatePort: {
  mach_port_t old=client->fPort;
  client->fPort=r->port;
  if (old!=MACH_PORT_NULL) client->releaseNotificationPort(old);
  return kIOReturnSuccess;
 }
 case StateCall: {
  IOReturn result=client->legacyMethod(r->selector,r->args);
  /* One line per distinct selector, with bounded per-client storage/budget.
   * Unknown selectors retain legacy success/zero-inline-output behaviour. */
  unsigned i;
  for (i=0;i<client->fServiceSelectorCount;++i)
   if (client->fServiceSelectors[i]==r->selector) break;
  if (i==client->fServiceSelectorCount && i<32) {
   client->fServiceSelectors[client->fServiceSelectorCount++]=r->selector;
   SWEET_LOG_INFO("SWEET FB SERVICE: sel=%u in=%u/%u out=%u/%u rc=%#x\n",
    r->selector,r->args->scalarInputCount,r->args->structureInputSize,
    r->args->scalarOutputCount,r->args->structureOutputSize,(uint32_t)result);
  }
  return result;
 }
 default: return kIOReturnBadArgument;
 }
}

void
IOMobileFramebufferUserClient::serviceClose(void)
{
 if (fServiceClosed) return;
 fServiceClosed=true;
 mach_port_t old=fPort; fPort=MACH_PORT_NULL;
 bzero(fFunc,sizeof(fFunc)); bzero(fRefcon,sizeof(fRefcon));
 unsigned index=fFB->fClients->getNextIndexOfObject(this,0);
 if (index!=(unsigned)-1) fFB->fClients->removeObject(index);
 if (old!=MACH_PORT_NULL) releaseNotificationPort(old);
}

void
IOMobileFramebufferUserClient::stop(IOService *provider)
{
 if (fServiceProviderRetained) (void)fFB->stateControl(AppleCLCD::StateCloseClient,this);
 IOUserClient::stop(provider);
}

void
IOMobileFramebufferUserClient::free(void)
{
 if (fServiceProviderRetained) {
  /* stop/clientClose or provider stop already removed us from its array. */
  fFB->release(); fServiceProviderRetained=false;
 }
 IOUserClient::free();
}

void
AppleCLCD::free(void)
{
 if (fServiceMode) {
  /* Also covers partially failed start, before any client can attach. */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
  if (fDisplayClient) fDisplayClient->stop();
  OSSafeReleaseNULL(fDisplayClient);
  if (fHardwareVsyncRequested) {
   if (fStateGateAdded) fStateGate->runAction([](OSObject *owner,void *,void *,void *,void *) -> IOReturn {
    AppleCLCD *fb=(AppleCLCD *)owner;
    fb->fServiceStopped=true; fb->stopPanelLifecycle(); fb->shutdownScanout(); fb->shutdownHardwareVsync(); return kIOReturnSuccess;
   });
   else { stopPanelLifecycle(); shutdownScanout(); shutdownHardwareVsync(); }
  }
#endif
  if (fVsyncAdded) { fVsync->cancelTimeout(); fWorkLoop->removeEventSource(fVsync); }
  OSSafeReleaseNULL(fVsync);
  if (fStateGateAdded) fWorkLoop->removeEventSource(fStateGate);
  OSSafeReleaseNULL(fStateGate);
  OSSafeReleaseNULL(fWorkLoop);
#if defined(ARM64_BOARD_CONFIG_MH2LM)
  OSSafeReleaseNULL(fScanoutPool); /* hardware pin, if any, survives service teardown */
#endif
  OSSafeReleaseNULL(fClients);
  OSSafeReleaseNULL(fSurface);
  OSSafeReleaseNULL(fMap);
  if (fLock) { IOLockFree(fLock); fLock=NULL; }
 }
 IOMobileFramebuffer::free();
}

IOReturn
IOMobileFramebufferUserClient::serviceDispatch(OSObject *target,void *reference,
 IOExternalMethodArguments *args)
{
 IOMobileFramebufferUserClient *client=(IOMobileFramebufferUserClient *)target;
 uint32_t selector=*(const uint32_t *)reference;
 return client->fFB->stateControl(AppleCLCD::StateCall,client,args,selector);
}

IOReturn
IOMobileFramebufferUserClient::serviceMethod(uint32_t selector,IOExternalMethodArguments *args)
{
 if (!args || args->scalarInputCount>kIOExternalMethodScalarInputCountMax ||
     args->scalarOutputCount>kIOExternalMethodScalarOutputCountMax ||
     (args->scalarInputCount && !args->scalarInput) ||
     (args->scalarOutputCount && !args->scalarOutput) ||
     (args->structureInputSize && !args->structureInput && !args->structureInputDescriptor) ||
     (args->structureOutputSize && !args->structureOutput && !args->structureOutputDescriptor))
  return kIOReturnBadArgument;
 struct Entry { uint32_t selector; IOExternalMethodDispatch dispatch; };
 static const Entry entries[]={
  {3,{serviceDispatch,2,0,1,0}},
  {4,{serviceDispatch,0,0,1,0}},
  {5,{serviceDispatch,0,SWEET_FB_SWAP_REC_SIZE,0,0}},
  {6,{serviceDispatch,3,0,0,0}},
  /* 7/18 have no input dereference; keep unobserved input shapes permissive. */
  {7,{serviceDispatch,kIOUCVariableStructureSize,0,1,0}},
  {8,{serviceDispatch,0,0,2,0}},
  {12,{serviceDispatch,1,0,0,0}},
  {18,{serviceDispatch,kIOUCVariableStructureSize,0,1,0}},
  {28,{serviceDispatch,0,0,1,0}},
  {72,{serviceDispatch,kIOUCVariableStructureSize,0,0,0}},
 };
 IOExternalMethodDispatch dispatch={serviceDispatch,kIOUCVariableStructureSize,
  kIOUCVariableStructureSize,kIOUCVariableStructureSize,kIOUCVariableStructureSize};
 bool known=false;
 for (unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);++i) {
  if (entries[i].selector==selector) {dispatch=entries[i].dispatch;known=true;break;}
 }
 if (known && (args->structureInputDescriptor || args->structureOutputDescriptor))
  return kIOReturnBadArgument; /* Existing implemented ABI is inline/scalar only. */
 if (selector==72 && ((args->scalarInputCount!=3 && args->scalarInputCount!=4) ||
     args->scalarInput[2]>=SWEET_FB_NOTIFY_TYPES)) return kIOReturnBadArgument;
 /* Use IOUserClient's real count/size checks before the gate/handler. */
 return IOUserClient::externalMethod(selector,args,&dispatch,this,&selector);
}


/* Called only after dispatch validation, under the AppleCLCD gate. State and
 * CPU presentation remain serialized through the synchronous provider call.
 * Provider never calls back into AppleCLCD while holding its own gate. */
IOReturn
AppleCLCD::requestPanelPower(bool off)
{
#if defined(ARM64_BOARD_CONFIG_MH2LM)
 /* One line per request: the 0121b10 freeze showed a power key whose request
  * never reached a provider and left no trace at all. */
 SWEET_LOG_INFO("SWEET FB POWER PATH: off=%u coordinated=%u fault=%u stopped=%u\n",
     off ? 1u : 0u, fPanelLifecycleRequested ? 1u : 0u,
     fPanelLifecycleFault ? 1u : 0u, fServiceStopped ? 1u : 0u);
 if (fPanelLifecycleRequested) return requestCoordinatedPower(off);
 uint64_t start=mach_absolute_time(), elapsed;
 bool changed=fPowerOff!=off;
 if (changed && !off && fOwnScanoutPreflight) fScanoutAuditResume=true;
 if (changed && fPerf.enabled) fPerf.lastPresentNS=0; /* OFF may suppress every vsync callback. */
 fPowerOff=off; /* stop presenting before sending 28; keep latest pending swap */
 if (fHardwareVsyncRequested) {
  fHardwareVsyncActive=false;
  setProperty("SweetFramebufferHardwareVsync",false);
  fVsync->cancelTimeout();
 }
 fFadeStep=0;
 if (changed) fDirty=true;
 SweetDisplayBackend *backend=fDisplayClient->copyBackend();
 IOReturn rc=backend ? backend->setPanelPower(!off) : kIOReturnNotReady;
 OSSafeReleaseNULL(backend);
 setProperty("SweetPanelPowerLastResult",(unsigned long long)(uint32_t)rc,32);
 absolutetime_to_nanoseconds(mach_absolute_time()-start,&elapsed);
 if (rc!=kIOReturnSuccess)
  SWEET_LOG_ERROR("SWEET FB POWER: %s rc=%#x elapsed-ms=%llu fallback=preserve-pixels; panel state may be unknown\n",
      off ? "off" : "on",(unsigned)rc,elapsed/1000000ULL);
 else
  SWEET_LOG_INFO("SWEET FB POWER: %s rc=0 elapsed-ms=%llu\n",off ? "off" : "on",elapsed/1000000ULL);
 /* Explicit legacy logical/brightness fallback; hardware failure is logged
  * and published, not reported as a successful DCS transition. Same-state ON
  * must still reach the provider, which may be recovering an unknown state. */
 if (fHardwareVsyncRequested && !off) fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);
 IOLog("SWEET FB: display %s\n",off ? "off" : "on");
 return kIOReturnSuccess;
#else
 (void)off;
 return kIOReturnUnsupported;
#endif
}

#if defined(ARM64_BOARD_CONFIG_MH2LM)
/* Asynchronous consumer: no MMIO, waits, sleeps or callback from MDSS gate.
 * A rapid opposite request is coalesced, then submitted after the current
 * token completes. A failure freezes presentation and never silently falls
 * back to synchronous F2 or adopts a new provider's token. */
IOReturn AppleCLCD::requestCoordinatedPower(bool off)
{
 fPanelLifecycleDesiredOff=off;
 /* Every early return here is a decision the panel cannot see; name each one. */
 if (fPanelLifecycleFault || fServiceStopped) {
  SWEET_LOG_INFO("SWEET FB PANEL: coordinated off=%u refused: fault=%u stopped=%u\n",
      off ? 1u : 0u, fPanelLifecycleFault ? 1u : 0u, fServiceStopped ? 1u : 0u);
  return kIOReturnNotReady;
 }
 if (fPanelLifecycleBackend) {
  SWEET_LOG_INFO("SWEET FB PANEL: coordinated off=%u already pending token=%llu\n",
      off ? 1u : 0u, (unsigned long long)fPanelLifecycleToken);
  return kIOReturnSuccess;
 }
 if (off==fPanelLifecycleDisplayOff) {
  SWEET_LOG_INFO("SWEET FB PANEL: coordinated off=%u is the current state\n",off ? 1u : 0u);
  return kIOReturnSuccess;
 }
 SweetDisplayBackend *b=fDisplayClient->copyBackend();
 IOReturn rc=kIOReturnNotReady;uint64_t token=0;
 uint32_t need=(1u<<(off ? SweetPanelDisplayOff : SweetPanelDisplayOn)) |
     (fPanelLifecyclePipeline ? SweetPanelPipelineRebuild : 0u) |
     (fPanelLifecycleSupplyCycle ? SweetPanelSupplyCycle : 0u) |
     (fPanelLifecycleLinkCycle ? SweetPanelLinkPowerCycle : 0u) |
     (fPanelLifecycleDataParking ? SweetPanelDataParking : 0u) |
     (fPanelLifecycleFull ? SweetPanelRetainedSleepReset : 0u);
 if (!b) {
  SWEET_LOG_INFO("SWEET FB PANEL: coordinated off=%u has no display backend; refusing\n",
      off ? 1u : 0u);
 } else if ((b->panelLifecycleCapabilities()&need)!=need) {
  SWEET_LOG_INFO("SWEET FB PANEL: coordinated off=%u capability miss have=%#x need=%#x; refusing\n",
      off ? 1u : 0u, (unsigned)b->panelLifecycleCapabilities(), (unsigned)need);
 }
 if (b && (b->panelLifecycleCapabilities()&need)==need)
  rc=b->beginPanelLifecycle(off ? SweetPanelDisplayOff : SweetPanelDisplayOn,&token);
 if (rc==kIOReturnSuccess && token) {
  SWEET_LOG_INFO("SWEET FB PANEL: coordinated off=%u admitted token=%llu need=%#x\n",
      off ? 1u : 0u, (unsigned long long)token, (unsigned)need);
  fPanelLifecycleBackend=b;fPanelLifecycleToken=token;fPanelLifecycleRequestOff=off;fPanelLifecycleStart=mach_absolute_time();
  fPowerOff=true;fHardwareVsyncActive=false;fFadeStep=0;
  if (fPerf.enabled) fPerf.lastPresentNS=0;
  setProperty("SweetFramebufferHardwareVsync",false);
  setProperty("SweetPanelPowerLastResult",(unsigned long long)(uint32_t)kIOReturnNotReady,32);
  if (fVsync) {fVsync->cancelTimeout();fVsync->setTimeoutMS(1);}
  return kIOReturnSuccess; /* admitted only; result published on completion */
 }
 OSSafeReleaseNULL(b);
 if (rc==kIOReturnSuccess) rc=kIOReturnIOError;
 fPanelLifecycleFault=true;fPowerOff=true;fHardwareVsyncActive=false;
 setProperty("SweetPanelPowerLastResult",(unsigned long long)(uint32_t)rc,32);
 SWEET_LOG_ERROR("SWEET FB PANEL: lifecycle admission failed rc=%#x; pixels frozen\n",(unsigned)rc);
 return rc;
}
void AppleCLCD::servicePanelLifecycle(void)
{
 if (!fPanelLifecycleBackend) return;
 SweetDisplayBackend *current=fDisplayClient->copyBackend();
 bool same=current==fPanelLifecycleBackend && !fPanelLifecycleBackend->isInactive();
 OSSafeReleaseNULL(current);
 SweetPanelLifecycleStatus v={};
 IOReturn rc=same ? fPanelLifecycleBackend->panelLifecycleStatus(fPanelLifecycleToken,&v) : kIOReturnNotReady;
 uint64_t elapsed=0;absolutetime_to_nanoseconds(mach_absolute_time()-fPanelLifecycleStart,&elapsed);
 if (elapsed>=17000000000ULL) rc=kIOReturnTimeout;
 if (rc==kIOReturnSuccess && v.token==fPanelLifecycleToken &&
     v.phase!=SweetPanelComplete && v.phase!=SweetPanelFault && v.phase!=SweetPanelStopped) {
  if (fVsync) fVsync->setTimeoutMS(1);return;
 }
 bool completed=rc==kIOReturnSuccess && v.token==fPanelLifecycleToken &&
     v.phase==SweetPanelComplete && v.result==kIOReturnSuccess && !v.uncertain &&
     v.operation==(fPanelLifecycleRequestOff ? SweetPanelDisplayOff : SweetPanelDisplayOn) &&
     (!fPanelLifecyclePipeline || (v.pipelineGeneration &&
       (fPanelLifecycleRequestOff ? (!fPanelPipelineGeneration || v.pipelineGeneration==fPanelPipelineGeneration) :
        v.pipelineGeneration>fPanelPipelineGeneration))) &&
     (!fPanelLifecycleFull || (v.profile>=1 && v.profile<=4 &&
       (fPanelLifecycleRequestOff ? v.sleeping : (!v.sleeping && v.resetComplete && v.brightnessRestored)))) &&
     (!fPanelLifecycleSupplyCycle ||
       (v.configHeld && v.configToken && !v.supplyToken && v.panelVoteTokens[0] && v.panelVoteTokens[1] &&
        (fPanelLifecycleRequestOff ? (v.rails==12u && v.panelReleasedMask==3u && v.resetHeldLow && v.panelVotesParked &&
          !v.panelVotesRestored && !v.retainedPower && v.linkPoweredDown && v.resourcesParked && !v.resourcesHeld && !v.dataToken && !v.clockToken) :
         (v.rails==15u && !v.panelReleasedMask && !v.resetHeldLow && !v.panelVotesParked && v.panelVotesRestored &&
          v.panelAggregateEnabledMask==3u && v.retainedPower && !v.linkPoweredDown && v.linkRebuilt && !v.resourcesParked &&
          v.resourcesRestored && v.resourcesHeld && v.dataToken && v.clockToken)))) &&
     (fPanelLifecycleSupplyCycle ? true : (fPanelLifecycleLinkCycle && fPanelLifecycleDataParking ?
       (v.retainedPower && v.configHeld && v.configToken &&
        (fPanelLifecycleRequestOff ? (v.linkPoweredDown && v.resourcesParked && !v.resourcesHeld && !v.dataToken && !v.clockToken) :
         (!v.linkPoweredDown && v.linkRebuilt && !v.resourcesParked && v.resourcesRestored && v.resourcesHeld && v.dataToken && v.clockToken))) :
      (fPanelLifecycleLinkCycle ? (v.retainedPower && v.dataToken && v.clockToken &&
       (fPanelLifecycleRequestOff ? (v.linkPoweredDown && !v.resourcesHeld) :
        (!v.linkPoweredDown && v.linkRebuilt && v.resourcesHeld))) :
      (fPanelLifecycleDataParking ? (v.retainedPower && v.configHeld && v.configToken &&
       (fPanelLifecycleRequestOff ? (v.resourcesParked && !v.resourcesHeld && !v.dataToken && !v.clockToken) :
        (!v.resourcesParked && v.resourcesRestored && v.resourcesHeld && v.dataToken && v.clockToken))) : v.resourcesHeld)))) &&
     (fPanelLifecycleRequestOff ? (v.touchQuiesced && v.displayDrained) : (v.pixelsComplete && !v.touchQuiesced));
 if (!completed) {
  if (rc==kIOReturnSuccess) rc=v.result==kIOReturnSuccess ? kIOReturnIOError : v.result;
  stopPanelLifecycle();fPanelLifecycleFault=true;fPowerOff=true;
  setProperty("SweetPanelPowerLastResult",(unsigned long long)(uint32_t)rc,32);
  SWEET_LOG_ERROR("SWEET FB PANEL: lifecycle completion failed rc=%#x; no legacy recovery\n",(unsigned)rc);
  return;
 }
 fPanelLifecycleDisplayOff=v.operation==SweetPanelDisplayOff;
 if (fPanelLifecyclePipeline) fPanelPipelineGeneration=v.pipelineGeneration;
 OSSafeReleaseNULL(fPanelLifecycleBackend);fPanelLifecycleToken=0;
 fPowerOff=fPanelLifecycleDisplayOff;fDirty=true;
 setProperty("SweetPanelPowerLastResult",(unsigned long long)(uint32_t)kIOReturnSuccess,32);
 if (fPanelLifecycleDesiredOff!=fPanelLifecycleDisplayOff) {
  (void)requestCoordinatedPower(fPanelLifecycleDesiredOff);return;
 }
 if (!fPowerOff && fVsync) {fScanoutAuditResume=true;fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);}
}
void AppleCLCD::stopPanelLifecycle(void)
{
 if (fPanelLifecycleBackend) fPanelLifecycleBackend->cancelPanelLifecycle(fPanelLifecycleToken);
 OSSafeReleaseNULL(fPanelLifecycleBackend);fPanelLifecycleToken=0;
}
#endif
