/* F3 framebuffer consumer: all state/notifications/present stay on its workloop.
 * FB gate -> provider gate for attach/detach/power; reverse direction only queues.
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
bool
AppleCLCD::setupHardwareVsync(void)
{
 fHardwareEvent=IOInterruptEventSource::interruptEventSource(this,hardwareVsyncFired);
 if (!fHardwareEvent) return false;
 if (fWorkLoop->addEventSource(fHardwareEvent)!=kIOReturnSuccess) return false;
 fHardwareEventAdded=true;
 fVsyncChannel=SweetVsyncChannel::withEvent(fHardwareEvent);
 if (!fVsyncChannel) return false;
 setProperty("SweetFramebufferHardwareVsync",false);
 SWEET_LOG_INFO("SWEET FB VSYNC: F3 v2 mailbox consumer; software until first RD_PTR, watchdog250ms\n");
 return true;
}
void
AppleCLCD::bindHardwareVsync(SweetDisplayBackend *backend)
{
 if (backend==fVsyncBackend) return;
 if (fVsyncBackend) {
  fVsyncBackend->detachVsync(fVsyncChannel);
  OSSafeReleaseNULL(fVsyncBackend);
 }
 bool wasActive=fHardwareVsyncActive;
 fHardwareVsyncActive=false; fHardwareSeenFrame=0;
 setProperty("SweetFramebufferHardwareVsync",false);
 if (backend && !backend->isInactive()) {
  IOReturn rc=backend->attachVsync(fVsyncChannel);
  if (rc==kIOReturnSuccess) {
   backend->retain(); fVsyncBackend=backend; fVsyncFallback=false;
   SWEET_LOG_INFO("SWEET FB VSYNC: subscribed through typed backend; waiting for real frame\n");
  } else fallbackHardwareVsync("backend subscribe refused");
 } else if (wasActive) fallbackHardwareVsync("backend withdrawn");
 if (fVsync && !fPowerOff && !fServiceStopped) fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);
}
void
AppleCLCD::fallbackHardwareVsync(const char *reason)
{
 bool first=!fVsyncFallback;
 fVsyncFallback=true; fHardwareVsyncActive=false;
 setProperty("SweetFramebufferHardwareVsync",false);
 setProperty("SweetFramebufferVsyncFallback",true);
 if (first) {
  SWEET_LOG_ERROR("SWEET FB VSYNC: FAILED %s; explicit software-vsync fallback coalesced=%llu\n",reason,fHardwareCoalesced);
  if (fVsyncBackend) fVsyncBackend->requestVsyncFallback();
 }
 if (fVsync && !fPowerOff && !fServiceStopped) fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);
}
void
AppleCLCD::hardwareVsyncFired(OSObject *owner,IOInterruptEventSource *,int)
{
 AppleCLCD *fb=(AppleCLCD *)owner;
 if (fb->fServiceStopped || fb->isInactive() || fb->fPanelLifecycleBackend || fb->fPanelLifecycleFault) return;
 SweetDisplayBackend *current=fb->fDisplayClient->copyBackend();
 bool bound=current && current==fb->fVsyncBackend && !current->isInactive();
 OSSafeReleaseNULL(current);
 if (!bound) { fb->bindHardwareVsync(NULL); return; }
 SweetVsyncSample sample={};
 if (!fb->fVsyncChannel->copySample(&sample) || sample.sequence==fb->fVsyncSequence) return;
 fb->fVsyncSequence=sample.sequence;
 if (sample.state!=SweetVsyncFrame) {
  fb->fHardwareSeenFrame=sample.frame;
  fb->fHardwareVsyncActive=false;
  fb->setProperty("SweetFramebufferHardwareVsync",false);
  if (sample.state==SweetVsyncFailed || sample.state==SweetVsyncStopped)
   fb->fallbackHardwareVsync(sample.state==SweetVsyncFailed ? "provider failed" : "provider stopped");
  else if (!fb->fPowerOff) fb->fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);
  return;
 }
 if (fb->fPowerOff || fb->fVsyncFallback || !sample.ticks || sample.frame<=fb->fHardwareSeenFrame) return;
 uint64_t advance=fb->fHardwareSeenFrame ? sample.frame-fb->fHardwareSeenFrame : 1;
 fb->fHardwareSeenFrame=sample.frame;
 fb->fHardwareTicks=sample.ticks; fb->fHardwareAdvance=advance;
 fb->fHardwareCoalesced+=advance-1;
 if (!fb->fHardwareVsyncActive) {
  fb->fHardwareVsyncActive=true;
  fb->fVsync->cancelTimeout(); fb->fVsync->setTimeoutMS(100);
  fb->setProperty("SweetFramebufferHardwareVsync",true);
  fb->setProperty("SweetFramebufferVsyncFallback",false);
  SWEET_LOG_INFO("SWEET FB VSYNC: hardware active frame=%llu ticks=%llu coalesced=%llu\n",sample.frame,sample.ticks,fb->fHardwareCoalesced);
 }
 if (fb->fOwnScanoutPreflight && fb->fScanoutPool && fb->fScanoutAuditCount<4 &&
     (fb->fScanoutAuditResume || fb->fScanoutAuditCount==0 ||
      (fb->fScanoutAuditCount<3 && sample.frame-fb->fScanoutAuditFrame>=60))) {
  fb->fScanoutAuditResume=false;
  fb->fScanoutAuditFrame=sample.frame;
  fb->auditScanout();
 }
 AppleCLCD::vsyncFired(fb,NULL);
}
void
AppleCLCD::shutdownHardwareVsync(void)
{
 fHardwareVsyncActive=false;
 if (fVsyncBackend) {
  fVsyncBackend->detachVsync(fVsyncChannel);
  OSSafeReleaseNULL(fVsyncBackend);
 }
 if (fVsyncChannel) fVsyncChannel->close();
 if (fHardwareEventAdded) {
  fHardwareEvent->disable(); fWorkLoop->removeEventSource(fHardwareEvent); fHardwareEventAdded=false;
 }
 OSSafeReleaseNULL(fVsyncChannel); OSSafeReleaseNULL(fHardwareEvent);
 setProperty("SweetFramebufferHardwareVsync",false);
}
#endif
