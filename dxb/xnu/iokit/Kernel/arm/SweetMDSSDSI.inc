/* D1 command sequencer: profile tables LP, typed Lineage brightness HS. All actions run on the existing MDSS workloop.
 * No busy wait for idle/DMA/delays; no synchronous consumer callback.
 * This transport does NOT select/authorize an unknown vendor panel profile.
 */
bool
SweetMDSS::dsiBusy(void) const
{
 return fDsiStatus.state==SweetDSIQuiescing || fDsiStatus.state==SweetDSITransferring ||
     fDsiStatus.state==SweetDSIDelay;
}
bool
SweetMDSS::dsiDMAReady(void)
{
 /* LG allocates DSI command memory in MSM_SMMU_DOMAIN_UNSECURE, shared
  * with SDE. 0x801 in the DTS is the SECURE domain, not a DSI stream ID.
  * Validate the known unsecure route, never invent a new SID/IOVA mapping.
  * The first new DSI DMA still needs real transfer evidence on the device. */
 traceWake("dma-route-enter");
 SweetScanoutSnapshot v={};
 if (scanoutInspectAction(this,&v,NULL,NULL,NULL)!=kIOReturnSuccess) return false;
 traceWake("dma-route-snapshot",v.cbFault);
 return v.knownSmmu && v.matches==1 && v.cb2Observed && !v.secure &&
     v.smmuCR0==0x2b0406u && !v.smmuFault && v.smr==0x84200800u && v.s2cr==2u &&
     v.streamIndex==3 && !(v.cbFault&~0x400u) && v.sctlr==0xe0u &&
     v.cbar==0x1f000u && v.cba2r==1u && !v.ttbcr && !v.ttbrLo && !v.ttbrHi &&
     !v.mairLo && !v.mairHi;
}
bool
SweetMDSS::setupDSI(void)
{
 if (!fDsiTimer) {
  fDsiTimer=IOTimerEventSource::timerEventSource(this,dsiTimerAction);
  if (!fDsiTimer) return false;
  if (fLoop->addEventSource(fDsiTimer)!=kIOReturnSuccess) {
   OSSafeReleaseNULL(fDsiTimer); return false;
  }
  fDsiTimerAdded=true;
 }
 return true;
}
IOReturn
SweetMDSS::prepareDSICommandBuffer(void)
{
 if (fDsiBuffer) return fDsiPrepared ? kIOReturnSuccess : kIOReturnNotReady;
 SWEET_LOG_INFO("SWEET MDSS DSI: command buffer allocate/prepare enter bytes=4096\n");
 traceWake("dma-allocate-enter");
 fDsiBuffer=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
     kIODirectionOut | kIOMemoryPhysicallyContiguous | kIOMemoryMapperNone |
     kIOMapWriteCombineCache,4096,0xffffffffULL);
 traceWake("dma-allocate-return",fDsiBuffer!=NULL);
 if (!fDsiBuffer) return kIOReturnNoMemory;
 traceWake("dma-prepare-enter");
 if (!fDsiBuffer->getBytesNoCopy() || fDsiBuffer->getLength()<4096 ||
     fDsiBuffer->prepare(kIODirectionOut)!=kIOReturnSuccess) {
  OSSafeReleaseNULL(fDsiBuffer); return kIOReturnNoMemory;
 }
 fDsiPrepared=true;
 traceWake("dma-prepare-return");
 IOByteCount contiguous=0;
 traceWake("dma-physical-enter");
 fDsiPhysical=fDsiBuffer->getPhysicalSegment(0,&contiguous,kIOMemoryMapperNone);
 uint64_t pa=fDsiPhysical;
 traceWake("dma-physical-return",(uint32_t)pa);
 if (!pa || (pa&7u) || pa>=0x100000000ULL || 4096>0x100000000ULL-pa || contiguous<4096 ||
     (pa<0x9e400000ULL && 0x9d200000ULL<pa+4096)) {
  (void)fDsiBuffer->complete(kIODirectionOut); fDsiPrepared=false;
  OSSafeReleaseNULL(fDsiBuffer); return kIOReturnBadArgument;
 }
 SWEET_LOG_INFO("SWEET MDSS DSI: command buffer ready bytes=4096 pa=%#llx; retained for FIFO and physical DMA\n",fDsiPhysical);
 return kIOReturnSuccess;
}
/* At most one transfer can run under this gate. These two references are the
 * display and host-transfer users of Lineage's command-engine refcount. A
 * transfer reference survives uncertain DMA; stop is not completion proof. */
bool SweetMDSS::setDSIDisplayEngine(bool on)
{
 if (!fLifecycleLineage) return true;
 if (fDsiTransferEngine || fDsiTransferClock || dsiBusy() || fDsiPinned || fDsiPoisoned ||
     !dp_clocks_on() || (dp_rd(dp_dsi,8)&6u)) return false;
 if (fDsiDisplayEngine==on) return true;
 dp_wr(dp_dsi,4,on ? dp_rd(dp_dsi,4)|4u : dp_rd(dp_dsi,4)&~4u);
 fDsiDisplayEngine=on;
 SWEET_LOG_INFO("SWEET MDSS DSI: display command-engine reference=%u\n",on);
 return true;
}
bool SweetMDSS::acquireDSITransfer(void)
{
 if (!fLifecycleLineage) return true;
 if (fDsiTransferEngine || fDsiPoisoned || fDsiPinned || !fDsiPrepared || !fDsiBuffer ||
     !retainDSITransferClock()) return false;
 if (!fDsiDisplayEngine) dp_wr(dp_dsi,4,dp_rd(dp_dsi,4)|4u);
 fDsiTransferEngine=true;
 return true;
}
bool SweetMDSS::releaseDSITransfer(void)
{
 if (!fLifecycleLineage) return true;
 if (!fDsiTransferEngine && !fDsiTransferClock) return !fDsiPoisoned;
 if (!fDsiTransferEngine || fDsiPinned || !dsiTransferClockReady()) {
  fDsiPoisoned=true;return false;
 }
 if (!fDsiDisplayEngine) dp_wr(dp_dsi,4,dp_rd(dp_dsi,4)&~4u);
 fDsiTransferEngine=false;
 if (!releaseDSITransferClock()) {
  fDsiPoisoned=true;
  SWEET_LOG_ERROR("SWEET MDSS DSI CLOCK: release failed; lease retained, command admission closed\n");
  return false;
 }
 return true;
}
IOReturn
SweetMDSS::beginDSISequence(const uint8_t *bytes,size_t length,uint64_t *token)
{ return beginDSISequenceMode(bytes,length,token,false); }
IOReturn
SweetMDSS::beginDSISequenceMode(const uint8_t *bytes,size_t length,uint64_t *token,bool hs)
{
 if (!token) return kIOReturnBadArgument;
 *token=0;
 if (!bytes || !length || length>SweetDSIMaxSequence) return kIOReturnBadArgument;
 /* Copy BEFORE validation: the asynchronously executed bytes must be the
  * very bytes checked here, even if the caller later modifies its buffer. */
 OSData *copy=OSData::withBytes(bytes,(unsigned)length);
 if (!copy) return kIOReturnNoMemory;
 IOReturn rc=kIOReturnBadArgument;
 if (sweet_dsi_sequence_valid((const uint8_t *)copy->getBytesNoCopy(),copy->getLength(),NULL))
  rc=fGate ? fGate->runAction(dsiBeginAction,copy,token,(void *)(uintptr_t)hs) : kIOReturnNotReady;
 copy->release(); return rc;
}
IOReturn
SweetMDSS::dsiBeginAction(OSObject *owner,void *arg,void *output,void *hs,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 OSData *data=(OSData *)arg;
 s->traceWake("dsi-admission-enter");
 if (hs && (!s->fLifecycleInternal || !s->fLifecycleLineage)) return kIOReturnBadArgument;
 if (!s->fLifecycleInternal && s->lifecycleBlocked()) return kIOReturnBusy;
 if (!s->fAccepting || s->isInactive() || s->fDsiPoisoned) return kIOReturnNotReady;
 if (!s->fDsiEnabled) return kIOReturnUnsupported;
 s->traceWake("dsi-managed-enter");
 if (!s->managedAccessReady()) return kIOReturnNotReady;
 s->traceWake("dsi-managed-return");
 if (s->dsiBusy() || s->fScanoutState==SweetScanoutQuiescing ||
     s->fScanoutState==SweetScanoutSubmitted) return kIOReturnBusy;
 /* Full ON/OFF tables can exceed the250ms RD_PTR watchdog. The panel
  * lifecycle caller must pause the frame/IRQ stream first and rearm it only
  * when the reconstructed pipeline is ready. Do not silently mask that
  * watchdog or infer panel power from opaque command bytes here. */
 if (s->fIrqMode>=2 && __atomic_load_n(&s->fIrqState,__ATOMIC_ACQUIRE)!=IrqIdle)
  return kIOReturnBusy;
 if (!s->dp_clocks_on() || (s->fClockEnabled && !s->localClocksReady())) return kIOReturnNotReady;
 /* This revision is already observed in 0121a12 (+0=20030000).
  * Recheck live before writes; DT compatible alone does not authorize them. */
 s->traceWake("dsi-registers-enter");
 s->observeLineage("dsi-command-control",s->dp_dsi,4,s->fLifecycleLineage && !s->fDsiDisplayEngine ? 1u : 5u,7u);
 if (dp_rd(s->dp_dsi,0)!=0x20030000u ||
     dp_rd(s->dp_dsi,0x15c) || (dp_rd(s->dp_dsi,0x110)&(1u<<24)) ||
     dp_rd(s->dp_dsi,0x68)) return kIOReturnNotReady;
 unsigned commands=0;
 const uint8_t *bytes=(const uint8_t *)data->getBytesNoCopy();
 if (!sweet_dsi_sequence_valid(bytes,data->getLength(),&commands)) return kIOReturnBadArgument;
 bool needsDMA=false; size_t off=0;
 while (off<data->getLength()) {
  SweetDSIMessage m={};
  if (!sweet_dsi_next(bytes,data->getLength(),&off,&m)) return kIOReturnBadArgument;
  if (m.type==0x39 && m.length+4>SweetDSIFifoBytes) needsDMA=true;
 }
 s->traceWake("dsi-sequence-ready",commands);
 if (needsDMA && !s->dsiDMAReady()) return kIOReturnNotReady;
 s->traceWake("dsi-timer-enter",needsDMA);
 if (s->fDsiNextToken==~(uint64_t)0 || !s->setupDSI()) return kIOReturnNoResources;
 s->traceWake("dsi-timer-return");
 /* Preparation is required for FIFO too, as in dsi_host_transfer. Lineage
  * modes must already own the startup buffer; never allocate in OFF/ON. */
 if (s->fLifecycleLineage) {
  if (!s->fDsiBuffer || !s->fDsiPrepared) return kIOReturnNotReady;
 } else {
  IOReturn rc=s->prepareDSICommandBuffer();if (rc!=kIOReturnSuccess) return rc;
 }
 s->traceWake("dsi-arm-enter");
 data->retain(); s->fDsiSequence=data; s->fDsiOffset=0; s->fDsiHS=hs!=NULL;
 s->fDsiStatus={}; s->fDsiStatus.token=++s->fDsiNextToken;
 s->fDsiStatus.total=commands; s->fDsiStatus.state=SweetDSIQuiescing;
 *(uint64_t *)output=s->fDsiStatus.token;
 s->fDsiTE=dp_rd(s->dp_intf1,0x288); s->fDsiAuto=dp_rd(s->dp_intf1,0x2b4);
 s->fDsiPaused=true;
 dp_wr(s->dp_intf1,0x288,s->fDsiTE&~(1u<<20));
 dp_wr(s->dp_intf1,0x2b4,0);
 s->fDsiDeadline=scanout_now()+200000000ULL;
 s->setProperty("SweetDSISequenceState",(unsigned long long)SweetDSIQuiescing,32);
 s->fDsiTimer->setTimeoutMS(1);
 SWEET_LOG_INFO("SWEET MDSS DSI: D1 v1 admitted token=%llu commands=%u memory-dma=%u; %s/timer sequence\n",
     s->fDsiStatus.token,commands,needsDMA,s->fDsiHS ? "HS" : "LP");
 return kIOReturnSuccess;
}
IOReturn
SweetMDSS::dsiSequenceStatus(uint64_t token,SweetDSIStatus *status)
{
 if (!status) return kIOReturnBadArgument;
 *status={};
 return fGate ? fGate->runAction(dsiStatusAction,&token,status) : kIOReturnNotReady;
}
IOReturn
SweetMDSS::dsiStatusAction(OSObject *owner,void *arg,void *output,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!*(uint64_t *)arg || *(uint64_t *)arg!=s->fDsiStatus.token) return kIOReturnBadArgument;
 *(SweetDSIStatus *)output=s->fDsiStatus; return kIOReturnSuccess;
}
void
SweetMDSS::cancelDSISequence(uint64_t token)
{
 if (fGate) (void)fGate->runAction(dsiCancelAction,&token);
}
IOReturn
SweetMDSS::dsiCancelAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (s->fLifecycleEnabled && s->lifecycleBusy()) return kIOReturnBusy;
 if (*(uint64_t *)arg!=s->fDsiStatus.token) return kIOReturnBadArgument;
 if (s->dsiBusy()) s->finishDSI(kIOReturnAborted,SweetDSICancelled);
 return kIOReturnSuccess;
}
void
SweetMDSS::restoreDSICommand(void)
{
 if (!fDsiSaved) return;
 const uint32_t statusBits=0x51115501u;
 dp_wr(dp_dsi,0x110,(dp_rd(dp_dsi,0x110)&~statusBits)|1u|(1u<<20));
 dp_wr(dp_dsi,0x15c,0);
 dp_wr(dp_dsi,0x1ec,1); delay(1); dp_wr(dp_dsi,0x1ec,0);
 dp_wr(dp_dsi,0x48,fDsiAddress); dp_wr(dp_dsi,0x4c,fDsiLength);
 dp_wr(dp_dsi,0x50,fDsiFIFO); dp_wr(dp_dsi,0x3c,fDsiDMA);
 dp_wr(dp_dsi,0x84,fDsiTrigger);
 fDsiSaved=false;
}
void
SweetMDSS::finishDSI(IOReturn result,unsigned terminal,bool noRestore)
{
 if (fDsiTimer) fDsiTimer->cancelTimeout();
 /* Once triggered, cancellation or timeout is not proof of DMA exit.
  * Keep its independent descriptor pin, close all DCS admission, and leave
  * TE/auto paused. No reset/clock-off pretending to be a completion fence.
  * A partially applied table is also an unknown DDIC state, even when its
  * command DMA is already idle/unpinned. Recovery belongs to full rebuild. */
 bool clocks=!noRestore && dp_clocks_on() && (!fLifecycleLineage || dsiClockContextReady());
 bool uncertain=noRestore || fDsiStatus.state==SweetDSITransferring || (fDsiPaused && !clocks) ||
     (result!=kIOReturnSuccess && (fDsiStatus.completed || result==kIOReturnIOError));
 if (uncertain) fDsiPoisoned=true;
 else if (clocks) {
  restoreDSICommand();
  if (!releaseDSITransfer()) uncertain=true;
  if (!uncertain && fDsiPaused) {
   dp_wr(dp_intf1,0x2b4,fDsiAuto); dp_wr(dp_intf1,0x288,fDsiTE);
  }
 }
 if (uncertain) {
  fDsiPoisoned=true;
  if (result==kIOReturnSuccess) {result=kIOReturnNotReady;terminal=SweetDSIFailed;}
 }
 fDsiStatus.hardwareUncertain=uncertain;
 fDsiPaused=false;
 fDsiStatus.result=(int)result; fDsiStatus.state=terminal;
 OSSafeReleaseNULL(fDsiSequence);
 setProperty("SweetDSISequenceState",(unsigned long long)terminal,32);
 setProperty("SweetDSIHardwareUncertain",uncertain);
 if (result!=kIOReturnSuccess && (uncertain || terminal!=SweetDSICancelled))
  SWEET_LOG_ERROR("SWEET MDSS DSI: token=%llu terminal=%u rc=%#x completed=%u/%u irq=%#x status=%#x ack=%#x uncertain=%u dma-pin=%u\n",
      fDsiStatus.token,terminal,(unsigned)result,fDsiStatus.completed,fDsiStatus.total,
      fDsiStatus.irq,fDsiStatus.hardwareStatus,fDsiStatus.ack,uncertain,fDsiPinned);
 else if (terminal==SweetDSICancelled)
  SWEET_LOG_INFO("SWEET MDSS DSI: token=%llu cancelled before any command; admission restored\n",fDsiStatus.token);
 else
  SWEET_LOG_INFO("SWEET MDSS DSI: token=%llu complete commands=%u; fresh DMA_DONE and idle observed\n",
      fDsiStatus.token,fDsiStatus.completed);
}
void
SweetMDSS::dsiTimerAction(OSObject *owner,IOTimerEventSource *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!s->fAccepting || s->isInactive()) return;
 if (s->fLifecycleEnabled && s->lifecycleBusy() && !s->lifecycleResourceStepAllowed()) { s->finishLifecycle(kIOReturnNotReady,"DSI participant lost"); return; }
 if (s->linkBusy()) s->advanceLink();
 else if (s->hostBusy()) s->advanceHost();
 else if (s->dsiBusy()) s->advanceDSI();
}
void
SweetMDSS::advanceDSI(void)
{
 uint64_t now=scanout_now();
 if (!dp_clocks_on() || (fClockEnabled && !localClocksReady())) { finishDSI(kIOReturnNotReady,SweetDSIFailed); return; }
 if (fDsiStatus.state==SweetDSIDelay) {
  if (now<fDsiDeadline) { fDsiTimer->setTimeoutMS(1); return; }
  if (fDsiStatus.completed==fDsiStatus.total) { finishDSI(kIOReturnSuccess,SweetDSIComplete); return; }
  fDsiStatus.state=SweetDSIQuiescing; fDsiDeadline=now+200000000ULL;
 }
 if (fDsiStatus.state==SweetDSITransferring) {
  if (fLifecycleLineage && !dsiTransferClockReady()) {finishDSI(kIOReturnNotReady,SweetDSIFailed,true);return;}
  fDsiStatus.irq=dp_rd(dp_dsi,0x110); fDsiStatus.hardwareStatus=dp_rd(dp_dsi,8);
  fDsiStatus.ack=dp_rd(dp_dsi,0x68);
  if ((fDsiStatus.irq&(1u<<24)) || fDsiStatus.ack) {
   finishDSI(kIOReturnIOError,SweetDSIFailed); return;
  }
  if ((fDsiStatus.irq&1u) && !(fDsiStatus.hardwareStatus&6u)) {
   /* Only a fresh done PLUS DMA/MDP idle grants command memory reuse. */
   if (fDsiPinned) { fDsiPinned=false; fDsiBuffer->release(); }
   traceWake("packet-done",fDsiStatus.completed+1);
   restoreDSICommand();
   if (!releaseDSITransfer()) {finishDSI(kIOReturnNotReady,SweetDSIFailed,true);return;}
   ++fDsiStatus.completed;
   fDsiStatus.state=SweetDSIDelay; fDsiDeadline=now+(uint64_t)fDsiWaitMS*1000000ULL;
   fDsiTimer->setTimeoutMS(fDsiWaitMS ? fDsiWaitMS : 1); return;
  }
  if (now>=fDsiDeadline) { finishDSI(kIOReturnTimeout,SweetDSIFailed); return; }
  fDsiTimer->setTimeoutMS(1); return;
 }
 /* Like F2/LG, do not require TE/line progress from a sleeping panel.
  * No pixel source address changes here: DSI DMA/MDP idle is sufficient. */
 if (dp_rd(dp_dsi,8)&6u) {
  if (now>=fDsiDeadline) { finishDSI(kIOReturnTimeout,SweetDSIFailed); return; }
  fDsiTimer->setTimeoutMS(1); return;
 }
 SweetDSIMessage m={}; size_t next=fDsiOffset;
 if (!fDsiSequence || !sweet_dsi_next((const uint8_t *)fDsiSequence->getBytesNoCopy(),
     fDsiSequence->getLength(),&next,&m)) { finishDSI(kIOReturnBadArgument,SweetDSIFailed); return; }
 traceWake("packet-encode-enter",(unsigned)m.length);
 uint32_t words[SweetDSIEmbeddedBytes/4]={};
 size_t bytes=sweet_dsi_encode(m,(uint8_t *)words,sizeof(words));
 bool memory=bytes>SweetDSIFifoBytes;
 if (!bytes || (memory && (!fDsiBuffer || !dsiDMAReady()))) {
  finishDSI(kIOReturnNotReady,SweetDSIFailed); return;
 }
 if (!acquireDSITransfer()) {finishDSI(kIOReturnNotReady,SweetDSIFailed);return;}
 traceWake("packet-registers-enter",m.type);
 fDsiDMA=dp_rd(dp_dsi,0x3c); fDsiLength=dp_rd(dp_dsi,0x4c);
 fDsiTrigger=dp_rd(dp_dsi,0x84); fDsiFIFO=dp_rd(dp_dsi,0x50);
 fDsiAddress=dp_rd(dp_dsi,0x48); fDsiSaved=true;
 const uint32_t statusBits=0x51115501u;
 dp_wr(dp_dsi,0x110,(dp_rd(dp_dsi,0x110)&~statusBits)|1u|(1u<<20));
 if (dp_rd(dp_dsi,0x110)&(1u|(1u<<24))) { finishDSI(kIOReturnIOError,SweetDSIFailed); return; }
 dp_wr(dp_dsi,0x84,(fDsiTrigger&~7u)|4u);
 if (memory) {
  memcpy(fDsiBuffer->getBytesNoCopy(),words,bytes);
  /* Take the pin before hardware can see this address. */
  fDsiBuffer->retain(); fDsiPinned=true;
  dp_wr(dp_dsi,0x15c,0);
  dp_wr(dp_dsi,0x50,fDsiFIFO|(1u<<20)|(1u<<16));
  dp_wr(dp_dsi,0x48,(uint32_t)fDsiPhysical);
 } else {
  dp_wr(dp_dsi,0x15c,(1u<<1)|(1u<<2)|(3u<<16));
  for (unsigned i=0;i<bytes/4;++i) dp_wr(dp_dsi,0x17c,words[i]);
  if ((bytes/4)&1u) dp_wr(dp_dsi,0x17c,0);
 }
 /* Both paths use EMBEDDED mode; no non-embedded->embedded soft reset.
  * Longest supported payload=252; PV's149-byte payload fits intact. */
 dp_wr(dp_dsi,0x3c,(fDsiDMA&~((1u<<31)|(1u<<30)|(1u<<29)|(1u<<26)|(1u<<24)))|(1u<<28)|(fDsiHS ? 0u : (1u<<26)));
 dp_wr(dp_dsi,0x4c,(uint32_t)bytes);
 __asm__ volatile("dsb oshst" ::: "memory");
 fDsiOffset=next; fDsiWaitMS=m.waitMS;
 fDsiStatus.state=SweetDSITransferring; fDsiDeadline=now+200000000ULL;
 traceWake("packet-trigger-enter",memory);
 dp_wr(dp_dsi,0x90,1); fDsiTimer->setTimeoutMS(1);
 traceWake("packet-trigger-return");
}
