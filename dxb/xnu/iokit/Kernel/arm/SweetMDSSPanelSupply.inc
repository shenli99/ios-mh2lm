/* Mode6: only this panel consumer's L14/L17 votes, never shared L3/L5.
 * Same MDSS owner/gate/timer and existing Pins/RPMh execution services.
 * LG lge_dsi_panel.c: OFF pre-reset5ms + low5ms, reverse supply order then
 * post-off5ms; ON active-low + pre5ms, VDDI post20ms, VPNL, post-rails5ms.
 * ACKed withdrawal is not physical rail-off or exclusive supply ownership. */
bool SweetMDSS::lifecycleSupplyPhase(void) const
{
 return fLifecycle.phase==SweetPanelPinParkActivate || fLifecycle.phase==SweetPanelPinParkWait ||
     fLifecycle.phase==SweetPanelPinPark || fLifecycle.phase==SweetPanelSupplyActivate ||
     fLifecycle.phase==SweetPanelSupplyPark || fLifecycle.phase==SweetPanelSupplyRestore;
}
bool SweetMDSS::lifecyclePanelRailsValid(void)
{
 if (!fLifecycleSupplyCycle) return fManagedRails==15u;
 if ((fLifecycle.panelReleasedMask&~3u) || fManagedRails!=(15u&~fLifecycle.panelReleasedMask)) return false;
 if (!fLifecycle.panelReleasedMask) return true;
 /* Reduced rails are legal only AFTER DATA/link parking, before restore.
  * Do not weaken normal resource/link admission or the one-time takeover. */
 bool phase=fLifecycle.phase==SweetPanelSupplyPark || fLifecycle.phase==SweetPanelSupplyRestore ||
     fLifecycle.phase==SweetPanelSupplyActivate ||
     (fLifecycle.phase==SweetPanelComplete && fLifecycleParked && fLifecycle.operation==SweetPanelDisplayOff);
 return phase && fManagedState==ManagedParked && !fManagedClock && !fManagedBandwidth && !fManagedToken &&
     fLifecycle.resourcesParked && fLifecycle.linkPoweredDown && fLinkSuspendedMask==linkParkedMask() && managedConfigHeld() &&
     fHostPhase==HostDormant && fLinkPhase==LinkDormant;
}
void SweetMDSS::lifecycleStartDataParking(bool restore)
{
 fLifecycle.phase=restore ? SweetPanelResourceRestore : SweetPanelResourcePark;
 fManagedDeadline=fLifecycleStepDeadline=scanout_now()+4000000000ULL;
 IOReturn rc=beginManagedParking(restore);
 if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"panel supply/data handoff refused");return;}
 fLifecycle.resourcesHeld=false;publishLifecycle();
}
void SweetMDSS::lifecycleStartPinPark(void)
{
 fLifecycleStepDeadline=scanout_now()+200000000ULL;
 if (!fLifecyclePinsActive) {
  IOReturn rc=fPanelPins->activate(fPanelPinsConsumer,&fLifecycle.pinToken);
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"pre-park active pin admission");return;}
  fLifecycle.phase=SweetPanelPinParkActivate;
 } else {
  fLifecycle.phase=SweetPanelPinParkWait;fLifecycleSupplyWait=scanout_now()+5000000ULL;
 }
}
void SweetMDSS::lifecycleStartSupplyRestore(void)
{
 if (!fLifecycleSupplyCycle || fLifecyclePinsActive || fManagedRails!=12u ||
     fLifecycle.panelReleasedMask!=3u || !fLifecycle.panelVotesParked) {
  finishLifecycle(kIOReturnNotReady,"panel supply restore ledger absent");return;
 }
 /* Parked -> active-low does not pulse reset. The old Parked token was
  * checked at ON admission; the new token must complete before rail writes. */
 fLifecycle.resetHeldLow=false;fLifecycle.phase=SweetPanelSupplyActivate;
 fLifecycleStepDeadline=scanout_now()+100000000ULL;
 IOReturn rc=fPanelPins->activate(fPanelPinsConsumer,&fLifecycle.pinToken);
 if (rc!=kIOReturnSuccess) finishLifecycle(rc,"supply active-low admission");
}
bool SweetMDSS::lifecycleSupplySafe(void)
{
 if (!fLifecycleSupplyCycle || !lifecycleParticipants() || !fLifecycle.sleeping ||
     !fLifecycle.touchQuiesced || !fLifecycle.displayDrained ||
     !fLifecycle.resourcesParked || !fLifecycle.linkPoweredDown ||
     fManagedState!=ManagedParked || fManagedClock || fManagedBandwidth || fManagedToken ||
     !managedRetainedResources() || !managedConfigHeld() || !linkDormantConfiguration() ||
     clockBusy() || noCBusy() || linkBusy() || hostBusy() ||
     !lifecycleIRQDrained() || !clockCanGate() || !scanoutConfiguration(false) ||
     (dp_rd(dp_dsi,8)&6u) || dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c) ||
     dp_rd(dp_intf1,0x2b4) || (dp_rd(dp_intf1,0x288)&(1u<<20)) ||
     scanout_now()>=fLifecycleDeadline || scanout_now()>=fLifecycleStepDeadline) return false;
 uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
 return !(line>0 && line<2340);
}
bool SweetMDSS::lifecycleRecordSupply(const SweetRPMhStatus &s)
{
 unsigned rail=fLifecycle.supplyRail;
 bool on=fLifecycle.operation==SweetPanelDisplayOn;
 if (rail>SweetRailVPNL || !fLifecycle.supplyToken || s.token!=fLifecycle.supplyToken ||
     s.rail!=rail || s.state!=SweetRPMhComplete || s.result!=kIOReturnSuccess || s.uncertain ||
     (on && !s.enabled)) return false;
 unsigned bit=1u<<rail;
 if (on) {fManagedRails|=bit;fLifecycle.panelReleasedMask&=~bit;fLifecycle.panelVotesParked=false;}
 else {fManagedRails&=~bit;fLifecycle.panelReleasedMask|=bit;}
 if (s.enabled) fLifecycle.panelAggregateEnabledMask|=bit;
 else fLifecycle.panelAggregateEnabledMask&=~bit;
 fLifecycle.panelVoteTokens[rail]=s.token;fLifecycle.supplyToken=0;fLifecycle.rails=fManagedRails;
 fLifecycle.retainedPower=fManagedRails==15u;
 SWEET_LOG_INFO("SWEET PANEL SUPPLY: token=%llu %s rail=%u ACK=%llu own-held=%#x released=%#x aggregate=%#x; physical rail state unverified\n",
     fLifecycle.token,on ? "restore" : "withdraw",rail,s.token,fManagedRails,fLifecycle.panelReleasedMask,fLifecycle.panelAggregateEnabledMask);
 return true;
}
void SweetMDSS::lifecycleCancelSupply(void)
{
 if (!fLifecycleSupplyCycle || !fLifecycle.supplyToken || !fRPMh || !fRPMhConsumer) return;
 /* Stop the child's writer under its gate BEFORE invalidating Pins/Touch.
  * An ACK already committed by that gate is still real: reconcile it rather
  * than preserving an obsolete mask. A triggered but unconfirmed write stays
  * in supplyToken/rail and uncertain; cancellation cannot undo it. */
 (void)fRPMh->cancelPanelVote(fRPMhConsumer,fLifecycle.supplyToken);
 SweetRPMhStatus s={};
 if (fRPMh->copyStatus(fRPMhConsumer,fLifecycle.supplyToken,&s)==kIOReturnSuccess &&
     s.state==SweetRPMhComplete) (void)lifecycleRecordSupply(s);
}
void SweetMDSS::advanceLifecycleSupply(void)
{
 uint64_t now=scanout_now();
 bool prePark=fLifecycle.phase==SweetPanelPinParkActivate || fLifecycle.phase==SweetPanelPinParkWait ||
     fLifecycle.phase==SweetPanelPinPark;
 if (prePark) {
  if (fLifecycle.operation!=SweetPanelDisplayOff || !lifecycleResourcesHeld() || !fLifecycle.sleeping ||
      !fLifecycle.touchQuiesced || !fLifecycle.displayDrained || !lifecycleIRQDrained() ||
      (dp_rd(dp_dsi,8)&6u) || !clockCanGate() || !scanoutConfiguration(false) ||
      dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c)) {finishLifecycle(kIOReturnNotReady,"pre-park dependency lost");return;}
  if (fLifecycle.phase==SweetPanelPinParkWait) {
   if (now<fLifecycleSupplyWait) return;
   IOReturn rc=fPanelPins->park(fPanelPinsConsumer,&fLifecycle.pinToken);
   if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"reset-low park admission");return;}
   fLifecycle.phase=SweetPanelPinPark;publishLifecycle();return;
  }
  SweetPanelPinStatus p={};
  if (fPanelPins->copyStatus(fPanelPinsConsumer,fLifecycle.pinToken,&p)!=kIOReturnSuccess || p.uncertain ||
      (p.result!=kIOReturnNotReady && p.result!=kIOReturnSuccess)) {finishLifecycle(kIOReturnIOError,"pre-park pin failure");return;}
  if (p.result==kIOReturnNotReady) return;
  if (fLifecycle.phase==SweetPanelPinParkActivate) {
   if (p.state!=SweetPinsActive || !p.resetKnown || !p.resetHigh) {finishLifecycle(kIOReturnNotReady,"warm pre-park pin proof absent");return;}
   fLifecyclePinsActive=true;fLifecycle.phase=SweetPanelPinParkWait;fLifecycleSupplyWait=now+5000000ULL;
  } else {
   if (p.state!=SweetPinsParked || fPanelPins->copyLowStatus(fPanelPinsConsumer,fLifecycle.pinToken,&p)!=kIOReturnSuccess) {
    finishLifecycle(kIOReturnNotReady,"reset-low hold proof absent");return;
   }
   fLifecyclePinsActive=false;fLifecycle.resetHeldLow=true;lifecycleStartDataParking(false);
  }
  publishLifecycle();return;
 }
 if (!lifecycleSupplySafe()) {finishLifecycle(kIOReturnNotReady,"panel supply safety/deadline lost");return;}
 if (fLifecycle.phase==SweetPanelSupplyActivate) {
  SweetPanelPinStatus p={};
  if (fPanelPins->copyStatus(fPanelPinsConsumer,fLifecycle.pinToken,&p)!=kIOReturnSuccess || p.uncertain ||
      (p.result!=kIOReturnNotReady && p.result!=kIOReturnSuccess)) {finishLifecycle(kIOReturnIOError,"active-low operation failed");return;}
  if (p.result==kIOReturnNotReady) return;
  if (p.state!=SweetPinsActive || fPanelPins->copyLowStatus(fPanelPinsConsumer,fLifecycle.pinToken,&p)!=kIOReturnSuccess) {
   finishLifecycle(kIOReturnNotReady,"active-low proof absent");return;
  }
  fLifecyclePinsActive=fLifecycle.resetHeldLow=true;fLifecycle.phase=SweetPanelSupplyRestore;
  fLifecycle.supplyRail=SweetRailVDDI;fLifecycleSupplyWait=now+5000000ULL;
  fLifecycleStepDeadline=now+400000000ULL;publishLifecycle();return;
 }
 bool on=fLifecycle.phase==SweetPanelSupplyRestore;
 if (!fLifecycle.resetHeldLow || (on ? fLifecycle.operation!=SweetPanelDisplayOn : fLifecycle.operation!=SweetPanelDisplayOff)) {
  finishLifecycle(kIOReturnNotReady,"panel supply phase mismatch");return;
 }
 if (now<fLifecycleSupplyWait) return;
 if (fLifecycle.supplyRail==SweetRailCount) {
  if (fLifecycle.supplyToken || (on ? (fManagedRails!=15u || fLifecycle.panelReleasedMask) :
      (fManagedRails!=12u || fLifecycle.panelReleasedMask!=3u))) {finishLifecycle(kIOReturnIOError,"panel vote terminal ledger absent");return;}
  fLifecycle.panelVotesParked=!on;fLifecycle.panelVotesRestored=on;
  if (on) lifecycleStartDataParking(true);
  else finishLifecycle(kIOReturnSuccess,"panel votes withdrawn; shared power/config held");
  publishLifecycle();return;
 }
 unsigned rail=fLifecycle.supplyRail;
 if (rail>SweetRailVPNL) {finishLifecycle(kIOReturnBadArgument,"panel supply rail outside allowlist");return;}
 unsigned bit=1u<<rail;
 if (on ? (!(fLifecycle.panelReleasedMask&bit) || (fManagedRails&bit)) :
     ((fLifecycle.panelReleasedMask&bit) || !(fManagedRails&bit))) {finishLifecycle(kIOReturnNotReady,"panel vote ledger drift");return;}
 if (!fLifecycle.supplyToken) {
  SweetPanelSupplyFence fence={fPanelPins,fTouchPanel,fPanelPinsConsumer,fLifecycle.pinToken,fTouchPanelConsumer,fLifecycle.touchToken};
  SweetRailVote vote=on ? managedRailVotes[rail] : SweetRailVote{};
  IOReturn rc=fRPMh->requestPanelVote(fRPMhConsumer,(SweetRail)rail,vote,fence,&fLifecycle.supplyToken);
  if (rc!=kIOReturnSuccess) {finishLifecycle(rc,"panel vote admission refused");return;}
 }
 SweetRPMhStatus s={};
 if (fRPMh->copyStatus(fRPMhConsumer,fLifecycle.supplyToken,&s)!=kIOReturnSuccess || s.uncertain ||
     (s.state!=SweetRPMhPending && s.state!=SweetRPMhComplete)) {finishLifecycle(kIOReturnIOError,"panel vote completion failed");return;}
 if (s.state==SweetRPMhPending) {publishLifecycle();return;}
 if (!lifecycleRecordSupply(s)) {finishLifecycle(kIOReturnIOError,"panel vote ACK proof absent");return;}
 /* No elapsed-time guess can replace ACK; waits start from the observed ACK.
  * Final off/on5ms is separate from VDDI's post-on20ms. */
 fLifecycleSupplyWait=now+(on && rail==SweetRailVDDI ? 20000000ULL : 0ULL);
 fLifecycle.supplyRail=on ? (rail==SweetRailVDDI ? SweetRailVPNL : SweetRailCount) :
     (rail==SweetRailVPNL ? SweetRailVDDI : SweetRailCount);
 if (fLifecycle.supplyRail==SweetRailCount) fLifecycleSupplyWait=now+5000000ULL;
 publishLifecycle();
}
