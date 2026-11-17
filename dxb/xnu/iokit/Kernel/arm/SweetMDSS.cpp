/*
 * SM8150 display hardware owner. F1a preserves ABL scanout and DCS order.
 * Probe/DCS engine moved from bsd/dev/sweet_display_probe.c. Register sequence
 * follows the HakiOS Redmi K20 Pro port (SM8150, APSL 2.0), raphael_display_*
 * and hakios.raphael-dsi.h, derived there from LineageOS sm8150
 * sde_hw_intf.c / dsi_ctrl_hw_cmn.c. MH2LM addresses come from its Android DT.
 */
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "SweetDisplayBackend.h"
#include "SweetScanoutPool.h"
#include "SweetRPMh.h"
#include "SweetPanelPins.h"
#include "SweetTouchPanel.h"
#include "SweetDisplayClock.h"
#include "SweetDSILink.h"
#include "SweetPanelProfile.h"
#include "SweetPanelSDRColor.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/sweet_dev.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

OSDefineMetaClassAndAbstractStructors(SweetDisplayBackend, IOService);
OSDefineMetaClassAndStructors(SweetDisplayClient, OSObject);

IOReturn
SweetDisplayBackend::setBrightness(uint32_t level)
{
 if (!level) level=1;
 if (level>0xfffu) level=0xfffu;
 const uint32_t words[2]={0xc0390003u,
     0xff000051u | ((level >> 8) << 8) | ((level & 0xffu) << 16)};
 return dcsWrite(words,2);
}

SweetDisplayClient *
SweetDisplayClient::withOwner(OSObject *owner, IOWorkLoop *loop, void (*changed)(OSObject *))
{
 if (!owner || !loop || !changed) return NULL;
 SweetDisplayClient *self=OSTypeAlloc(SweetDisplayClient);
 if (!self) return NULL;
 if (!self->init()) { self->release(); return NULL; }
 self->fOwner=owner; self->fChanged=changed; self->fLoop=loop; loop->retain();
 self->fLock=IOLockAlloc();
 self->fEvent=IOInterruptEventSource::interruptEventSource(self, SweetDisplayClient::changed);
 if (!self->fLock || !self->fEvent) { self->release(); return NULL; }
 self->fEvent->disable();
 if (loop->addEventSource(self->fEvent)!=kIOReturnSuccess) { self->release(); return NULL; }
 self->fEventAdded=true;
 return self;
}

bool
SweetDisplayClient::watch(void)
{
 OSDictionary *matching=IOService::serviceMatching("SweetMDSS");
 if (!matching) return false;
 fTerminated=IOService::addMatchingNotification(gIOTerminatedNotification,matching,terminated,this);
 if (fTerminated) fMatched=IOService::addMatchingNotification(gIOFirstMatchNotification,matching,matched,this);
 matching->release(); /* API retains the dictionary */
 if (!fTerminated || !fMatched) { stop(); return false; }
 fEvent->enable(); fEvent->interruptOccurred(NULL,NULL,0);
 return true;
}

bool
SweetDisplayClient::matched(void *target, void *, IOService *service, IONotifier *)
{
 SweetDisplayClient *self=(SweetDisplayClient *)target;
 SweetDisplayBackend *backend=OSDynamicCast(SweetDisplayBackend,service);
 if (!backend) return true;
 IOLockLock(self->fLock);
 if (!self->fStopping && !self->fBackend && !backend->isInactive()) {
  backend->retain(); self->fBackend=backend;
  self->fEvent->interruptOccurred(NULL,NULL,0);
 }
 IOLockUnlock(self->fLock);
 return true;
}

bool
SweetDisplayClient::terminated(void *target, void *, IOService *service, IONotifier *)
{
 SweetDisplayClient *self=(SweetDisplayClient *)target;
 SweetDisplayBackend *old=NULL;
 IOLockLock(self->fLock);
 if (self->fBackend==service) {
  old=self->fBackend; self->fBackend=NULL;
  if (!self->fStopping) self->fEvent->interruptOccurred(NULL,NULL,0);
 }
 IOLockUnlock(self->fLock);
 /* No provider method or register access in notification context. In-flight
  * callers hold their own reference; provider checks inactive under its gate. */
 OSSafeReleaseNULL(old);
 return true;
}

void
SweetDisplayClient::changed(OSObject *owner, IOInterruptEventSource *, int)
{
 SweetDisplayClient *self=(SweetDisplayClient *)owner;
 IOLockLock(self->fLock); bool active=!self->fStopping; IOLockUnlock(self->fLock);
 if (!active) return;
 SweetDisplayBackend *backend=self->copyBackend();
 if (backend!=self->fObserved) {
  if (self->fInterest) {self->fInterest->remove();self->fInterest=NULL;}
  OSSafeReleaseNULL(self->fObserved);
  if (backend && !backend->isInactive()) {
   self->fObserved=backend;backend=NULL;
   self->fInterest=self->fObserved->registerInterest(gIOGeneralInterest,interest,self);
   if (!self->fInterest) SWEET_LOG_ERROR("SWEET DISPLAY CLIENT: readiness interest unavailable; deferred requests await a new client event\n");
  }
 }
 OSSafeReleaseNULL(backend);
 self->fChanged(self->fOwner);
}

/* Native interest notification only queues work; never enters the consumer
 * gate from MDSS's gate. Notification is not a readiness proof: the next
 * typed request still checks admission under the provider gate. */
IOReturn SweetDisplayClient::interest(void *target,void *,uint32_t type,IOService *provider,void *,vm_size_t)
{
 SweetDisplayClient *self=(SweetDisplayClient *)target;
 if (type!=kIOMessageServicePropertyChange) return kIOReturnSuccess;
 IOLockLock(self->fLock);
 if (!self->fStopping && self->fBackend==provider) self->fEvent->interruptOccurred(NULL,NULL,0);
 IOLockUnlock(self->fLock);return kIOReturnSuccess;
}
SweetDisplayBackend *
SweetDisplayClient::copyBackend(void)
{
 IOLockLock(fLock);
 SweetDisplayBackend *backend=!fStopping ? fBackend : NULL;
 if (backend) backend->retain();
 IOLockUnlock(fLock);
 return backend;
}

void
SweetDisplayClient::stop(void)
{
 if (fLock) { IOLockLock(fLock); fStopping=true; IOLockUnlock(fLock); }
 /* remove consumes the notifier and synchronizes callbacks. Never hold a
  * consumer lock/gate here: a replaying notification may be waiting on it. */
 if (fMatched) { fMatched->remove(); fMatched=NULL; }
 if (fTerminated) { fTerminated->remove(); fTerminated=NULL; }
 if (fEventAdded) {
  fEvent->disable(); fLoop->removeEventSource(fEvent); fEventAdded=false;
 }
 /* changed() is now synchronized. Interest callback uses only fLock and
  * queues the disabled event, so removal outside the consumer gate is safe. */
 if (fInterest) {fInterest->remove();fInterest=NULL;}
 OSSafeReleaseNULL(fObserved);
 if (fLock) {
  IOLockLock(fLock); SweetDisplayBackend *old=fBackend; fBackend=NULL; IOLockUnlock(fLock);
  OSSafeReleaseNULL(old);
 }
}

void
SweetDisplayClient::free(void)
{
 stop(); OSSafeReleaseNULL(fEvent); OSSafeReleaseNULL(fLoop);
 if (fLock) { IOLockFree(fLock); fLock=NULL; }
 OSObject::free();
}

#define GCC_DISP_AHB 0x00cu
#define DISPCC_AHB_CBCR 0x2080u
#define CBCR_ENABLE (1u << 0)
#define CBCR_OFF (1u << 31)
#define SMMU_SID 0x0800u

class SweetMDSS : public SweetDisplayBackend {
 OSDeclareDefaultStructors(SweetMDSS);
 IOWorkLoop *fLoop;
 IOCommandGate *fGate;
 IOTimerEventSource *fProbe, *fIrqProbe, *fIrqTimer;
 IOInterruptEventSource *fIrqEvent;
 SweetVsyncChannel *fVsyncChannel;
 enum { IrqIdle, IrqProbe, IrqArmed, IrqPending, IrqFailed, IrqStopped };
 unsigned fIrqMode, fIrqPhase;
 volatile unsigned fIrqState, fProbeHits, fIrqEntries, fFallbackRequested;
 uint64_t fIrqTicks, fFrameTicks;
 uint64_t fFrameCount, fEpochFrames, fLastFrameNs, fLastTopNs, fStormStartNs, fWindowStartNs;
 unsigned fStormFrames, fWindowFrames, fIntervalCount, fIntervalDropped;
 uint32_t *fIntervals;
 uint32_t fOldGroup, fOldCfg, fOldPriority;
 uint64_t fOldRoute;
 bool fIrqEventAdded, fIrqTimerAdded, fIrqClaimed, fGicTouched, fSourceOwned, fIrqDisplayOn;
 bool mapIrqResources(IOService *,bool);
 void setupIRQ(IOService *);
 bool configureIRQ(void);
 bool waitIRQ(void);
 bool armIRQ(void);
 void pauseIRQ(void);
 void stopIRQ(void);
 void failIRQ(const char *);
 void publishVsync(unsigned);
 void reportIRQ(bool);
 static void irqAction(OSObject *,IOInterruptEventSource *,int);
 static void irqTimerAction(OSObject *,IOTimerEventSource *);
 static IOReturn vsyncBindingAction(OSObject *,void *,void *,void *,void *);
 static void dp_wr64(vm_offset_t,uint32_t,uint64_t);
 IOTimerEventSource *fScanoutTimer;
 bool fScanoutTimerAdded, fScanoutTouched, fScanoutUsed;
 unsigned fScanoutState, fScanoutIdleSamples;
 uint64_t fScanoutDeadline, fScanoutSubmitFrame;
 uint32_t fScanoutTE, fScanoutAuto;
 SweetScanoutPool *fHardwarePool;
 SweetScanoutStatus fScanoutStatus;
 static IOReturn scanoutBeginAction(OSObject *,void *,void *,void *,void *);
 static IOReturn scanoutStatusAction(OSObject *,void *,void *,void *,void *);
 static IOReturn scanoutAbandonAction(OSObject *,void *,void *,void *,void *);
 static void scanoutTimerAction(OSObject *,IOTimerEventSource *);
 bool scanoutDomain(SweetScanoutSnapshot * = NULL);
 bool scanoutConfiguration(bool);
 bool fPipelineEnabled, fPipelinePending, fPipelineConfigured;
 uint64_t fPipelineGeneration;
 bool setupPipeline(IOService *);
 bool pipelineExclusive(void);
 IOReturn beginPipeline(void);
 IOReturn advancePipeline(void);
 void kickoffPipeline(void);
 void cancelScanout(const char *);
 /* D1 command transport: same owner/gate as display, separate from pixels. */
 IOTimerEventSource *fDsiTimer;
 OSData *fDsiSequence;
 IOBufferMemoryDescriptor *fDsiBuffer;
 SweetDSIStatus fDsiStatus;
 bool fDsiHS; /* selected explicitly by typed lifecycle callers */
 IOReturn beginDSISequenceMode(const uint8_t *,size_t,uint64_t *,bool);
 bool fDsiEnabled, fDsiTimerAdded, fDsiPrepared, fDsiPinned, fDsiPaused, fDsiSaved, fDsiPoisoned;
 uint64_t fDsiNextToken, fDsiDeadline, fDsiPhysical;
 size_t fDsiOffset;
 unsigned fDsiWaitMS;
 uint32_t fDsiTE, fDsiAuto, fDsiDMA, fDsiLength, fDsiTrigger, fDsiFIFO, fDsiAddress;
 bool dsiBusy(void) const;
 bool dsiDMAReady(void);
 bool setupDSI(void);
 IOReturn prepareDSICommandBuffer(void);
 bool fDsiDisplayEngine, fDsiTransferEngine;
 uint64_t fDsiTransferClock;
 unsigned fDsiClockLogged;
 bool dsiClockContextReady(void);
 bool dsiTransferClockReady(void);
 bool retainDSITransferClock(void);
 bool releaseDSITransferClock(void);
 bool setDSIDisplayEngine(bool);
 bool acquireDSITransfer(void);
 bool releaseDSITransfer(void);
 void finishDSI(IOReturn,unsigned,bool = false);
 void restoreDSICommand(void);
 void advanceDSI(void);
 static IOReturn dsiBeginAction(OSObject *,void *,void *,void *,void *);
 static IOReturn dsiStatusAction(OSObject *,void *,void *,void *,void *);
 static IOReturn dsiCancelAction(OSObject *,void *,void *,void *,void *);
 static void dsiTimerAction(OSObject *,IOTimerEventSource *);
 /* Host reset/config uses the original DSI timer and MDSS gate/MMIO owner.
  * Profile1 is the common R66456 link format, not DDIC identification. */
 enum HostPhase { HostIdle, HostResetHold, HostConfigure, HostReady, HostFault, HostStopped, HostDormant };
 unsigned fHostProfile, fHostPhase;
 bool fHostTouched, fHostLineageReset;
 uint32_t fHostSavedControl, fHostSavedClocks;
 void configureHostForULPS(void);
 uint64_t fHostDeadline, fHostWait;
 IOReturn fHostResult;
 bool setupHost(void);
 bool hostBusy(void) const;
 bool hostResourcesReady(void);
 bool hostPreflight(void);
 bool hostConfiguration(void);
 IOReturn disableHostForLink(void);
 IOReturn beginHost(void);
 void advanceHost(void);
 void finishHost(IOReturn,const char *);
 void stopHost(void);
 /* PHY/PLL and their downstream roots share the clock writer and DSI timer. */
 enum LinkPhase { LinkIdle, LinkLaneIdle, LinkGatesOff, LinkPLLStop, LinkBiasOff,
     LinkResetHold, LinkResetSettle, LinkRefgen, LinkPHYConfig, LinkPLLConfig,
     LinkBiasOn, LinkLock, LinkRoots, LinkRootWait, LinkGatesOn, LinkResync,
     LinkULPSEntry, LinkULPSReentry, LinkULPSExit, LinkULPSStop, LinkResumeComplete, LinkULPSIdle, LinkPHYOff,
     LinkReady, LinkFault, LinkStopped, LinkDormant, LinkLPStop, LinkLPStart };
 unsigned fLinkProfile, fLinkPhase, fLinkCursor, fLinkRoot;
 uint32_t fLinkMisc;
 bool fLinkTouched, fLinkPowerDown, fLinkResume;
 unsigned fLinkSuspendedMask;
 uint64_t fLinkDeadline, fLinkWait, fLinkStepDeadline;
 SweetDSILinkPlan fLinkPlan;
 IOReturn fLinkResult;
 bool setupLink(void);
 bool linkBusy(void) const;
 unsigned linkParkedMask(void) const;
 bool linkResourcesReady(void);
 bool linkOtherUsers(void);
 bool linkWarmAdmission(void);
 bool linkConfiguration(void);
 bool linkWrite(uint32_t,uint32_t);
 IOReturn beginLink(bool = false);
 IOReturn resumeLink(void);
 bool linkDormantConfiguration(void);
 void advanceLink(void);
 void finishLink(IOReturn,const char *);
 void stopLink(void);
 /* Managed warm takeover, driven by the existing scanout timer/gate. */
 enum ManagedState { ManagedIdle, ManagedRails, ManagedARC, ManagedNoC,
     ManagedClock, ManagedReady, ManagedFault, ManagedStopped, ManagedCore, ManagedHost, ManagedLink,
     ManagedParkConfig, ManagedParkClock, ManagedParkNoC, ManagedParked,
     ManagedRestoreNoC, ManagedRestoreClock, ManagedParkLink, ManagedRestoreLink };
 bool fManagedEnabled, fManagedStarted;
 unsigned fManagedState, fManagedRail, fManagedRails;
 uint64_t fManagedToken, fManagedBandwidth, fManagedClock, fManagedWait, fManagedDeadline;
 uint64_t fManagedARC, fManagedConfigBandwidth;
 bool managedParkingBusy(void) const;
 bool managedConfigHeld(void);
 bool managedRetainedResources(void);
 IOReturn beginManagedParking(bool);
 void advanceManagedParking(void);
 IOReturn lifecycleDataBandwidth(bool);
 IOReturn fManagedResult;
 bool setupManagedResources(void);
 IOReturn managedPreflight(void);
 bool managedAccessReady(void) const;
 void beginManagedResources(void);
 void advanceManagedResources(void);
 void failManagedResources(IOReturn,const char *);
 void stopManagedResources(void);
 IOMemoryMap *fMaps[19];
 SweetRPMhClient *fRPMhClient;
 SweetRPMh *fRPMh;
 uint64_t fRPMhConsumer;
 unsigned fDisplayResourceMode;
 IOTimerEventSource *fClockTimer;
 bool fClockTimerAdded, fClockEnabled, fClockClaimed, fClockTouched, fClockFault;
 uint64_t fClockNextToken, fClockBootToken, fClockDeadline, fClockWaitDeadline, fClockVoteSettleAt;
 enum { ClockLeaseSlots=6 };
 SweetClockLease fClockLeases[ClockLeaseSlots];
 /* Owner tags describe the real ledger slots; there is no second refcount. */
 unsigned fClockClient[ClockLeaseSlots];
 enum DisplayClientState { ClientsBoot, ClientsPreparing, ClientsActive, ClientsStopping, ClientsOff };
 unsigned fDisplayClientState;
 uint64_t fPanelDSIClock, fMdpClock;
 bool displayClientHeld(uint64_t,unsigned,bool = false);
 bool displayClientsValid(void);
 bool retainDisplayClient(unsigned,bool = false);
 bool releaseDisplayClient(unsigned);
 bool prepareDisplayClients(bool = false);
 bool startMDPClient(void);
 bool stopMDPClient(void);
 bool postEnableDisplayClients(void);
 bool unprepareDisplayClients(void);
 void publishDisplayClients(const char *);

 unsigned fClockReferences[SweetClockCount], fClockOwnedMask, fClockKnownMask, fClockIssuedMask;
 uint32_t fClockParents[SweetClockCount][6];
 unsigned fClockOperation, fClockCursor, fClockReleaseMask;
 bool fClockReleasing, fClockWaiting;
 /* Diagnostic samples only; never authorize writes or complete a lease. */
 struct ClockLocalSnapshot {
  uint64_t sampledNS;
  uint32_t bcr, nonGDSC, rsccAHB, rsccVsync, ahbCMD, ahbCFG;
  bool valid;
 } fClockWarmLocal;
 struct ClockTrace {
  uint64_t writeEntryNS, writeIssuedNS, sampleNS;
  unsigned writeBranch, sampleBranch;
  uint32_t writeBefore, writeWant, sampleCBCR, parentCFG, parentSource;
  bool writeValid, sampleValid, parentValid;
  uint64_t immediateNS, physicalAddress, firstWaitNS, lastEnabledNS;
  uint32_t immediateCBCR, firstWaitCBCR, observedBits, rscAdmission[3];
  unsigned waitSamples;
  bool immediateValid, rscAdmissionValid;
  ClockLocalSnapshot warmLocal, writeLocal;
  uint64_t resetSampleNS;
  uint32_t resetAtTimeout;
  bool resetValid;
 } fClockTrace;
 /* Core resource control shares the clock writer, gate and timer. Profile2
  * is an explicit SM8150-v2 selection, not a hardware revision detector. */
 unsigned fCoreProfile, fCorePhase, fCorePLL, fCoreCursor, fCoreRoot;
 unsigned fCoreInheritedPLL, fCoreOwnedBranches;
 unsigned fCoreProgrammedPLL;
 uint64_t fCoreCN0, fCoreDeadline, fCoreStepDeadline, fCoreWait;
 uint32_t fCoreRootCFG;
 bool fCoreCold;
 IOReturn fCoreResult;
 bool coreBusy(void) const;
 bool coreConfigAccessible(void);
 bool corePLLReady(unsigned);
 bool corePLLAdmissible(unsigned);
 bool corePreflight(void);
 bool coreReady(void);
 IOReturn beginCoreResources(SweetCoreStart);
 void advanceCoreResources(void);
 void finishCoreResources(IOReturn);
 void stopCoreResources(void);
 bool setupLocalClocks(void);
 enum ClockDiagnostics { ClockSilent, ClockAdmission, ClockFailure };
 bool clockPrerequisites(ClockDiagnostics = ClockSilent);
 bool clockParent(unsigned,uint32_t [6],bool = true,ClockDiagnostics = ClockSilent);
 bool clockEnableParentReady(unsigned,const uint32_t [6]);
 bool clockBranchRequested(unsigned,uint32_t) const;
 bool clockBranchRunning(unsigned,uint32_t) const;
 bool clockCanGate(void);
 bool clockBusy(void) const;
 void captureClockLocal(ClockLocalSnapshot &);
 void captureClockTimeoutReset(void);
 void logClockLocal(const char *,const ClockLocalSnapshot &);
 bool localClocksReady(void);
 IOReturn retainLocalClocks(SweetClockUse,uint64_t *);
 IOReturn releaseLocalClocks(uint64_t);
 IOReturn localClockStatus(uint64_t,SweetClockLease *);
 static IOReturn clockRetainAction(OSObject *,void *,void *,void *,void *);
 static IOReturn clockReleaseAction(OSObject *,void *,void *,void *,void *);
 static IOReturn clockStatusAction(OSObject *,void *,void *,void *,void *);
 static void clockTimerAction(OSObject *,IOTimerEventSource *);
 void advanceLocalClocks(void);
 void logClockTimeout(const char *,uint64_t,bool);
 void finishLocalClocks(IOReturn);
 void stopLocalClocks(void);
 /* Same owner/gate as clocks/GDSC; display RSC is never mapped by RPMh. */
 struct NoCLease { SweetDisplayBandwidth vote; SweetNoCStatus status; bool active; };
 NoCLease fNoCLeases[4];
 IOTimerEventSource *fNoCTimer;
 bool fNoCTimerAdded, fNoCEnabled, fNoCClaimed, fNoCTouched, fNoCFault, fNoCReleasing;
 uint64_t fNoCNextToken, fNoCDeadline, fNoCStepDeadline, fNoCWriteDeadline, fNoCClockToken, fNoCAppsToken, fNoCRegisterToken;
 unsigned fNoCSlot, fNoCPhase, fNoCGroup, fNoCCursor, fNoCCount, fNoCTCS, fNoCOp, fNoCOps;
 unsigned fNoCOwned;
 uint32_t fNoCSavedIRQ, fNoCEnable, fNoCWait;
 bool fNoCExecuting, fNoCProgress, fNoCWriteIssued, fNoCAppsLower;
 SweetDisplayBandwidth fNoCTarget, fNoCAggregate;
 SweetBCMInfo fNoCInfo[SweetDisplayBCMCount];
 uint32_t fNoCValues[SweetDisplayBCMCount];
 uint32_t fNoCApplied[SweetDisplayBCMCount];
 unsigned fNoCOrder[5];
 struct NoCOperation { uint32_t offset, value; bool clear; };
 NoCOperation fNoCOperations[24];
 /* Evidence only: never consumed by admission, completion or cleanup. */
 struct NoCRegisterSample {
  bool valid; uint64_t at;
  uint32_t irqEnable, control, enable, wait;
 };
 struct NoCCommandEvidence {
  unsigned resource, vcd;
  uint32_t address, message, data, required, first, last;
 };
 struct NoCEvidence {
  uint64_t requestAt, groupAt, groupDeadline, triggerAt, firstAt, lastAt, readAt;
  unsigned cursor, count, tcs, polls;
  bool groupValid, readValid, seenBusy, seenIncomplete, seenIRQ, lastComplete;
  uint32_t firstIRQ, lastIRQ, firstIdle, lastIdle, writeOffset, readOffset, want, got;
  NoCRegisterSample trigger, timeout;
  NoCCommandEvidence commands[5];
 } fNoCEvidence;
 vm_offset_t fNoCRSC, fNoCTCSBase, fNoCWrapper;
 unsigned fNoCNCPT;
 IOInterruptEventSource *fNoCIRQEvent;
 bool fNoCIRQEventAdded, fNoCIRQClaimed, fNoCIRQTouched, fNoCIRQConfigured, fNoCIRQObserved;
 unsigned fNoCIRQOnline, fNoCIRQArmed, fNoCIRQPosted, fNoCIRQEntries, fNoCSlotsOwned;
 uint64_t fNoCIRQDeadline, fNoCRSCDeadline, fNoCRSCRetryAt;
 unsigned fNoCRSCStage, fNoCRSCInitCursor;
 bool fNoCRSCInitialized, fNoCTCSOwned, fNoCRSCWriteIssued;
 bool setupNoCIRQ(IOService *);
 IOReturn prepareNoCIRQ(void);
 void quietNoCIRQ(void);
 void stopNoCIRQ(void);
 void waitNoCCompletion(void);
 static void noCIRQAction(OSObject *,IOInterruptEventSource *,int);
 IOReturn takeNoCRSC(void);
 IOReturn initNoCRSC(void);
 unsigned fLineageObservations;
 unsigned fLineageMatches;
 void observeClockParent(unsigned,const uint32_t *,const uint32_t *);
 void observeLineageValue(const char *,uint32_t,uint32_t,uint32_t,uint32_t,bool);
 void observeLineage(const char *,vm_offset_t,uint32_t,uint32_t,uint32_t mask=~0u,bool always=false);

 bool setupNoC(IOService *);
 bool noCBusy(void) const;
 bool noCClockDiagnostics(void) const;
 bool noCClocksReady(void);
 bool noCCanReleaseClock(void) const;
 bool noCAdmission(bool = false);
 IOReturn retainBandwidth(const SweetDisplayBandwidth &,uint64_t *);
 IOReturn releaseBandwidth(uint64_t);
 IOReturn bandwidthStatus(uint64_t,SweetNoCStatus *);
 IOReturn beginBandwidth(unsigned,const SweetDisplayBandwidth &,bool);
 bool noCPlan(const SweetDisplayBandwidth &);
 IOReturn noCQueueGroup(void);
 void noCAdd(uint32_t,uint32_t,bool = false);
 void captureNoCRegisters(NoCRegisterSample &);
 void logNoCEvidence(const char *);
 void advanceNoC(void);
 void finishNoC(IOReturn,bool,const char * = "provider");
 void stopNoC(void);
 static IOReturn noCAction(OSObject *,void *,void *,void *,void *);
 static void noCTimerAction(OSObject *,IOTimerEventSource *);
 /* Unified lifecycle module, same gate/owner as scanout, resources and DSI.
  * No provider callback ever waits on this gate. Delays use one timer. */
 IOTimerEventSource *fLifecycleTimer;
 bool fLifecycleEnabled, fLifecycleTimerAdded, fLifecycleInternal, fLifecycleParked;
 SweetPanelLifecycleStatus fLifecycle;
 uint64_t fLifecycleNextToken, fLifecycleDeadline, fLifecycleStepDeadline, fLifecycleFrame;
 uint32_t fLifecycleTE, fLifecycleAuto;
 unsigned fLifecycleIdleSamples, fWakeTraceCount;
 SweetScanoutPool *fLifecyclePool;
 const SweetPanelProfile *fPanelProfile;
 bool fLifecycleLineage, fLifecycleFull, fLifecycleDataParking, fLifecycleLinkCycle, fLifecycleSupplyCycle, fLifecyclePinsActive, fPanelBrightnessKnown;
 uint64_t fLifecycleSupplyWait;
 uint32_t fPanelBrightnessTarget, fLifecycleBrightnessTarget;
 bool setupLifecycle(IOService *);
 void traceWake(const char *,unsigned = 0);
 void lifecycleCommand(bool);
 void lifecycleReset(void);
 bool fLifecycleColorRestored; /* current ordinary SDR policy, not a user color API */
 void lifecycleColor(void);
 void lifecycleBrightness(void);
 static IOReturn brightnessAction(OSObject *,void *,void *,void *,void *);
 bool lifecycleBusy(void) const;
 bool lifecycleBlocked(void) const;
 bool lifecycleParticipants(void);
 bool lifecycleIRQDrained(void);
 bool lifecycleResourcesHeld(void);
 bool lifecycleResourceStepAllowed(void);
 bool lifecycleLinkAccessible(void) const;
 bool lifecycleLinkResourcesHeld(void);
 bool lifecycleLinkStepAllowed(void);
 void lifecycleStartReset(void);
 void lifecycleStartPinPark(void);
 void lifecycleStartSupplyRestore(void);
 void lifecycleStartDataParking(bool);
 void advanceLifecycleSupply(void);
 bool lifecycleSupplyPhase(void) const;
 bool lifecycleSupplySafe(void);
 bool lifecyclePanelRailsValid(void);
 bool lifecycleRecordSupply(const SweetRPMhStatus &);
 void lifecycleCancelSupply(void);
 IOReturn beginLifecycle(unsigned,SweetScanoutPool *,uint64_t *);
 void advanceLifecycle(void);
 void finishLifecycle(IOReturn,const char *,bool = false);
 void publishLifecycle(void);
 void restoreLifecycleStream(void);
 static IOReturn lifecycleAction(OSObject *,void *,void *,void *,void *);
 static void lifecycleTimerAction(OSObject *,IOTimerEventSource *);
 SweetTouchPanelClient *fTouchPanelClient;
 SweetTouchPanel *fTouchPanel;
 uint64_t fTouchPanelConsumer;
 bool setupTouchPanel(void);
 static void touchPanelChanged(OSObject *);
 void closeTouchPanel(void);
 SweetPanelPinsClient *fPanelPinsClient;
 SweetPanelPins *fPanelPins;
 uint64_t fPanelPinsConsumer;
 bool setupPanelPins(void);
 static void panelPinsChanged(OSObject *);
 void closePanelPins(void);
 bool setupDisplayResources(IOService *);
 static void resourcesChanged(OSObject *);
 void closeDisplayResources(void);
 bool fGateAdded, fProbeAdded, fIrqProbeAdded, fAccepting;
 unsigned fPass, fIrqProbePass, fIrqProbeMode;
 enum PanelState { PanelOn, PanelOffPending, PanelOff, PanelOnPending, PanelUnknown };
 PanelState fPanelState;
 bool fPanelPowerEnabled, fPanelFallback;
 static IOReturn panelAction(OSObject *,void *,void *,void *,void *);
 IOReturn panelCommand(bool);
 void publishPanelState(void);
 int dp_failed;
 unsigned dp_logged;
 uint32_t dp_last_frame[2];
 uint64_t dp_last_ns[2];
 vm_offset_t dp_gcc,dp_dispcc,dp_sspp,dp_ctl,dp_intf1,dp_intf2,dp_dsi,dp_smmu,dp_smmu_gr1,dp_smmu_cb2;
 static uint32_t dp_rd(vm_offset_t,uint32_t);
 static void dp_wr(vm_offset_t,uint32_t,uint32_t);
 static int dp_poll(vm_offset_t,uint32_t,uint32_t,uint32_t,unsigned);
 static void dp_dump(const char *,vm_offset_t,uint32_t,uint32_t);
 void dp_smmu_context(uint32_t,uint32_t,uint32_t);
 void dp_probe(unsigned,vm_offset_t,vm_offset_t,vm_offset_t,vm_offset_t,
     vm_offset_t,vm_offset_t,vm_offset_t,vm_offset_t);
 int dp_dcs_write(vm_offset_t,vm_offset_t,const uint32_t *,unsigned,bool = false,bool = false);
 int dp_clocks_on(void);
 static IOReturn scanoutInspectAction(OSObject *,void *,void *,void *,void *);
 static IOReturn readyAction(OSObject *,void *,void *,void *,void *);
 static IOReturn writeAction(OSObject *,void *,void *,void *,void *);
 static IOReturn stopAction(OSObject *,void *,void *,void *,void *);
 static void probeAction(OSObject *,IOTimerEventSource *);
 void setupIrqProbe(IOService *);
 static void irqProbeAction(OSObject *,IOTimerEventSource *);
public:
 bool start(IOService *) APPLE_KEXT_OVERRIDE;
 bool willTerminate(IOService *,IOOptionBits) APPLE_KEXT_OVERRIDE;
 bool handleIRQ(void);
 bool handleNoCIRQ(void);
 IOReturn attachVsync(SweetVsyncChannel *) APPLE_KEXT_OVERRIDE;
 void detachVsync(SweetVsyncChannel *) APPLE_KEXT_OVERRIDE;
 void requestVsyncFallback(void) APPLE_KEXT_OVERRIDE;
 void stop(IOService *) APPLE_KEXT_OVERRIDE;
 void free(void) APPLE_KEXT_OVERRIDE;
 unsigned panelLifecycleCapabilities(void) APPLE_KEXT_OVERRIDE;
 IOReturn beginPanelLifecycle(unsigned,uint64_t *) APPLE_KEXT_OVERRIDE;
 IOReturn panelLifecycleStatus(uint64_t,SweetPanelLifecycleStatus *) APPLE_KEXT_OVERRIDE;
 void cancelPanelLifecycle(uint64_t) APPLE_KEXT_OVERRIDE;
 IOReturn setPanelPower(bool) APPLE_KEXT_OVERRIDE;
 IOReturn inspectScanout(SweetScanoutSnapshot *) APPLE_KEXT_OVERRIDE;
 IOReturn beginScanout(SweetScanoutPool *,SweetVsyncChannel *) APPLE_KEXT_OVERRIDE;
 IOReturn scanoutStatus(SweetScanoutPool *,SweetScanoutStatus *) APPLE_KEXT_OVERRIDE;
 void abandonScanout(SweetScanoutPool *) APPLE_KEXT_OVERRIDE;
 IOReturn beginDSISequence(const uint8_t *,size_t,uint64_t *) APPLE_KEXT_OVERRIDE;
 IOReturn dsiSequenceStatus(uint64_t,SweetDSIStatus *) APPLE_KEXT_OVERRIDE;
 void cancelDSISequence(uint64_t) APPLE_KEXT_OVERRIDE;
 IOReturn brightnessReady(void) APPLE_KEXT_OVERRIDE;
 IOReturn setBrightness(uint32_t) APPLE_KEXT_OVERRIDE;
 IOReturn dcsWrite(const uint32_t *,unsigned) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(SweetMDSS,SweetDisplayBackend);

bool
SweetMDSS::start(IOService *provider)
{
 int enabled=0;
 (void)PE_parse_boot_argn("sweetmdss",&enabled,sizeof(enabled));
 if (!enabled || !SweetDisplayBackend::start(provider)) return false;
 /* Ordering is the ADT reg ABI. Validate the resolved physical windows so a
  * bad ranges translation fails visibly before the first register read. */
 const uint64_t pa[9]={0x0010b000,0x0af00000,0x0ae05000,0x0ae02000,
     0x0ae6b800,0x0ae6c000,0x0ae94000,0x15000000,0x15082000};
 const uint64_t size[9]={0x1000,0x3000,0x1000,0x1000,0x800,0x800,0x400,0x2000,0x1000};
 if (provider->getResources()!=kIOReturnSuccess) return false;
 for (unsigned i=0;i<9;++i) {
  fMaps[i]=provider->mapDeviceMemoryWithIndex(i);
  if (fMaps[i] && (fMaps[i]->getPhysicalAddress()!=pa[i] || fMaps[i]->getLength()<size[i])) {
   SWEET_LOG_ERROR("SWEET MDSS: resource %u address/length mismatch\n",i);
   return false;
  }
  if (!fMaps[i] && i<7) {
   SWEET_LOG_ERROR("SWEET MDSS: required resource %u unavailable\n",i);
   return false;
  }
  SWEET_LOG_INFO("SWEET MDSS: resource %u pa=%#llx length=%#llx mapped=%u\n",i,pa[i],size[i],fMaps[i]!=NULL);
 }
 dp_gcc=fMaps[0]->getVirtualAddress(); dp_dispcc=fMaps[1]->getVirtualAddress();
 dp_sspp=fMaps[2]->getVirtualAddress(); dp_ctl=fMaps[3]->getVirtualAddress();
 dp_intf1=fMaps[4]->getVirtualAddress(); dp_intf2=fMaps[5]->getVirtualAddress();
 dp_dsi=fMaps[6]->getVirtualAddress();
 dp_smmu=fMaps[7] ? fMaps[7]->getVirtualAddress() : 0;
 dp_smmu_gr1=dp_smmu ? dp_smmu+0x1000u : 0;
 dp_smmu_cb2=fMaps[8] ? fMaps[8]->getVirtualAddress() : 0;
 fLoop=IOWorkLoop::workLoop(); fGate=IOCommandGate::commandGate(this);
 fProbe=IOTimerEventSource::timerEventSource(this,probeAction);
 if (!fLoop || !fGate || !fProbe) return false;
 if (fLoop->addEventSource(fGate)!=kIOReturnSuccess) return false;
 fGateAdded=true;
 if (fLoop->addEventSource(fProbe)!=kIOReturnSuccess) return false;
 fProbeAdded=true; fAccepting=true;
 if (!setupDisplayResources(provider)) return false;
 int power=0; (void)PE_parse_boot_argn("sweetfbpower",&power,sizeof(power));
 fPanelPowerEnabled=power!=0;
 int dsi=0; (void)PE_parse_boot_argn("sweetdsi",&dsi,sizeof(dsi));
 fDsiEnabled=dsi==1;
 setProperty("SweetDSISequenceEnabled",fDsiEnabled);
 setProperty("SweetDSISequenceVersion",1ULL,32);
 /* Continuous splash is already on. No reset/init sequence is replayed. */
 fPanelState=PanelOn;
 publishPanelState();
 int probe=1; (void)PE_parse_boot_argn("sweetdispprobe",&probe,sizeof(probe));
 if (probe) fProbe->setTimeoutMS(30000);
 setProperty("SweetDisplayBackendVersion",1ULL,32);
 setProperty("SweetDisplayInterruptEnabled",false);
 setupIrqProbe(provider);
 setupIRQ(provider);
 if (!setupLocalClocks()) return false;
 if (!setupNoC(provider)) return false;
 if (!setupManagedResources()) return false;
 if (!setupHost()) return false;
 if (!setupLink()) return false;
 if (!setupPanelPins()) return false;
 if (!setupTouchPanel()) return false;
 if (!setupLifecycle(provider)) return false;
 /*
  * One 4KiB command buffer for every mode that can reach the DSI sequencer,
  * not only Lineage. The physical allocation can block, so it happens here,
  * once, before the backend is published and outside the lifecycle gate; the
  * sequencer itself never allocates. a32 and the mode6 wake hung inside the
  * gate on exactly this allocation, and no lifecycle timer could fire.
  */
 if (fDsiEnabled && !fLifecycleLineage) {
  /* Non-Lineage modes reach the same sequencer, so they get the same one-shot
   * startup buffer outside the gate. Losing it only degrades them to refusing
   * long DCS sequences; start is not failed for a mode that may never need it. */
  IOReturn dsiBuffer=prepareDSICommandBuffer();
  if (dsiBuffer!=kIOReturnSuccess)
   SWEET_LOG_WARN("SWEET MDSS DSI: startup command buffer unavailable rc=%#x; "
       "long DCS sequences will be refused, not allocated inside the gate\n",(unsigned)dsiBuffer);
  else
   SWEET_LOG_INFO("SWEET MDSS DSI: command buffer preallocated once at startup "
       "outside the lifecycle gate pa=%#llx; the sequencer never allocates\n",
       fDsiPhysical);
 }
 if (fLifecycleLineage) {
  /* Lineage retains one 4KiB command buffer even for short commands. Our
   * physical allocation can block: do it before publishing to consumers,
   * outside the lifecycle gate, instead of Linux's first-transfer lazy GEM.
   * No DMA address is exposed here; each long packet rechecks its route. */
  if (prepareDSICommandBuffer()!=kIOReturnSuccess) return false;
  fDsiDisplayEngine=true; /* adopt the already active continuous splash */
  SWEET_LOG_INFO("SWEET MDSS DSI: command buffer preallocated once at startup "
      "outside the lifecycle gate pa=%#llx; the sequencer never allocates\n",
      fDsiPhysical);
  SWEET_LOG_INFO("SWEET MDSS DSI CLOCK: per-transfer leases enabled; shared BOOT/core retained\n");
  SWEET_LOG_INFO("SWEET MDSS CLIENT CLOCK: DSI prepare/post-enable/pre-disable and MDP scan/stop enabled; shared core floor retained\n");
 }
 SWEET_LOG_INFO("SWEET MDSS: F1a v1 IOService backend; ABL scanout unchanged\n");
 registerService();
 return true;
}

/* Resource ABI v1: sole MDSS owner of expanded DISPCC + one PHY/PLL
 * window. Mapping is not clock acquisition or permission to reset the domain.
 * Defaults keep the old nine/twelve resource ABI byte-for-byte unchanged. */
bool
SweetMDSS::setupDisplayResources(IOService *provider)
{
 int mode=0; (void)PE_parse_boot_argn("sweetdisplayresources",&mode,sizeof(mode));
 if (!mode) return true;
 if (mode<1 || mode>2) return false;
 OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,display-resource-abi"));
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),"\1\0\0\0",4) ||
     fMaps[1]->getLength()!=0x20000 || !mapIrqResources(provider,true)) {
  SWEET_LOG_ERROR("SWEET MDSS: display resource ABI v1 required; admission refused\n"); return false;
 }
 fMaps[12]=provider->mapDeviceMemoryWithIndex(12);
 if (!fMaps[12] || fMaps[12]->getPhysicalAddress()!=0x0ae94400 || fMaps[12]->getLength()!=0x760) {
  SWEET_LOG_ERROR("SWEET MDSS: PHY/PLL window mismatch\n"); return false;
 }
 fDisplayResourceMode=(unsigned)mode;
 /* Merely mapping never reads the PHY or changes the domain. */
 fRPMhClient=SweetRPMhClient::withOwner(this,fLoop,resourcesChanged);
 if (!fRPMhClient || !fRPMhClient->watch()) return false;
 setProperty("SweetDisplayResourceABI",1ULL,32);
 setProperty("SweetDisplayClockOwnership","MDSS: local warm leases opt-in; upstream votes inherited");
 return true;
}

void
SweetMDSS::resourcesChanged(OSObject *owner)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 if (!self->fAccepting) return;
 SweetRPMh *provider=self->fRPMhClient->copyBackend();
 if (provider==self->fRPMh) { OSSafeReleaseNULL(provider); return; }
 if (self->fRPMh) {
  if (self->fLifecycleEnabled && self->fLifecycle.token)
   self->finishLifecycle(kIOReturnNotReady,"resourcesChanged: participant withdrawn");
  if (self->fManagedStarted) {
   self->failManagedResources(kIOReturnNotReady,"RPMh provider changed");
   self->cancelScanout("managed RPMh provider changed");
  }
  if (self->fNoCEnabled && (self->noCBusy() || self->fNoCOwned)) {
   if (self->noCBusy()) self->finishNoC(kIOReturnNotReady,true);
   self->fNoCFault=true; /* Lost config-path owner; preserve resources. */
  }
  /* No vote is emitted by this D1 binding. Later lifecycle must explicitly
   * withdraw votes only after stopping DMA/link; failed detach never powers off. */
  (void)self->fRPMh->detachConsumer(self->fRPMhConsumer);
  OSSafeReleaseNULL(self->fRPMh); self->fRPMhConsumer=0;
 }
 self->setProperty("SweetDisplayRPMhBound",false);
 if (!provider) return;
 uint64_t id=0;
 if (provider->attachConsumer(&id)!=kIOReturnSuccess) { provider->release(); return; }
 self->fRPMh=provider; self->fRPMhConsumer=id;
 for (unsigned r=0;r<SweetRailCount;++r) {
  SweetRailInfo info={};
  if (provider->resourceInfo((SweetRail)r,&info)!=kIOReturnSuccess || !info.available) {
   (void)provider->detachConsumer(id);
   OSSafeReleaseNULL(self->fRPMh); self->fRPMhConsumer=0;
   return;
  }
 }
 self->setProperty("SweetDisplayRPMhBound",true);
 SWEET_LOG_INFO("SWEET MDSS: matched RPMh consumer=%llu; metadata ready, no automatic votes\n",id);
}

/* Binding only; actual reset/ON/OFF will require confirmed identity and the
 * full lifecycle coordinator. No pin operations in startup/matched/change. */
bool SweetMDSS::setupPanelPins(void)
{
 int mode=0; (void)PE_parse_boot_argn("sweetpanelpins",&mode,sizeof(mode));
 if (!mode) return true;
 if ((mode!=1 && !sweetPanelPinsExclusive(mode)) || !fDisplayResourceMode) return false;
 fPanelPinsClient=SweetPanelPinsClient::withOwner(this,fLoop,panelPinsChanged);
 return fPanelPinsClient && fPanelPinsClient->watch();
}
void SweetMDSS::panelPinsChanged(OSObject *owner)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!s->fAccepting) return;
 SweetPanelPins *p=s->fPanelPinsClient->copyBackend();
 if (p==s->fPanelPins) { OSSafeReleaseNULL(p); return; }
 if (s->fPanelPins) {
  if (s->fLifecycleEnabled && s->fLifecycle.token)
   s->finishLifecycle(kIOReturnNotReady,"panelPinsChanged: participant withdrawn");
  (void)s->fPanelPins->detachConsumer(s->fPanelPinsConsumer);
  OSSafeReleaseNULL(s->fPanelPins); s->fPanelPinsConsumer=0;
 }
 s->setProperty("SweetDisplayPanelPinsBound",false);
 if (!p) return;
 uint64_t id=0;
 if (p->attachConsumer(&id)!=kIOReturnSuccess) { p->release(); return; }
 s->fPanelPins=p; s->fPanelPinsConsumer=id;
 s->setProperty("SweetDisplayPanelPinsBound",true);
 SWEET_LOG_INFO("SWEET MDSS: matched panel pins consumer=%llu; no reset or pinctrl writes\n",id);
}
void SweetMDSS::closePanelPins(void)
{
 if (fPanelPinsClient) fPanelPinsClient->stop();
 OSSafeReleaseNULL(fPanelPinsClient);
 if (fPanelPins) {
  IOReturn rc=fPanelPins->detachConsumer(fPanelPinsConsumer);
  if (rc!=kIOReturnSuccess && rc!=kIOReturnNotReady)
   SWEET_LOG_WARN("SWEET MDSS: panel pins detach rc=%#x; retained, no implicit reset\n",(unsigned)rc);
  OSSafeReleaseNULL(fPanelPins); fPanelPinsConsumer=0;
 }
}

/* Metadata binding only; lifecycle requests remain explicit. */
bool SweetMDSS::setupTouchPanel(void)
{
 int mode=0; (void)PE_parse_boot_argn("sweettouchpanel",&mode,sizeof(mode));
 if (!mode) return true;
 if ((mode!=1 && !sweetTouchPanelNative(mode)) || !fDisplayResourceMode) return false;
 fTouchPanelClient=SweetTouchPanelClient::withOwner(this,fLoop,touchPanelChanged);
 return fTouchPanelClient && fTouchPanelClient->watch();
}
void SweetMDSS::touchPanelChanged(OSObject *owner)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!s->fAccepting) return;
 SweetTouchPanel *p=s->fTouchPanelClient->copyBackend();
 if (p==s->fTouchPanel) { OSSafeReleaseNULL(p); return; }
 if (s->fTouchPanel) {
  if (s->fLifecycleEnabled && s->fLifecycle.token)
   s->finishLifecycle(kIOReturnNotReady,"touchPanelChanged: participant withdrawn");
  (void)s->fTouchPanel->detachConsumer(s->fTouchPanelConsumer);
  OSSafeReleaseNULL(s->fTouchPanel); s->fTouchPanelConsumer=0;
 }
 s->setProperty("SweetDisplayTouchPanelBound",false);
 if (!p) return;
 uint64_t id=0;
 if (p->attachConsumer(&id)!=kIOReturnSuccess) { p->release(); return; }
 s->fTouchPanel=p; s->fTouchPanelConsumer=id;
 s->setProperty("SweetDisplayTouchPanelBound",true);
 SWEET_LOG_INFO("SWEET MDSS: matched touch panel consumer=%llu; no automatic quiesce/resume\n",id);
}
void SweetMDSS::closeTouchPanel(void)
{
 if (fTouchPanelClient) fTouchPanelClient->stop();
 OSSafeReleaseNULL(fTouchPanelClient);
 if (fTouchPanel) {
  IOReturn rc=fTouchPanel->detachConsumer(fTouchPanelConsumer);
  if (rc!=kIOReturnSuccess && rc!=kIOReturnNotReady)
   SWEET_LOG_WARN("SWEET MDSS: touch panel detach rc=%#x; retained, no implicit resume\n",(unsigned)rc);
  OSSafeReleaseNULL(fTouchPanel); fTouchPanelConsumer=0;
 }
}

void
SweetMDSS::closeDisplayResources(void)
{
 closeTouchPanel();
 closePanelPins();
 /* Called outside owner gate: remove synchronizes matching callbacks, then
  * removeEventSource synchronizes the owner's queued work before release. */
 if (fRPMhClient) fRPMhClient->stop();
 OSSafeReleaseNULL(fRPMhClient);
 if (fRPMh) {
  IOReturn rc=fRPMh->detachConsumer(fRPMhConsumer);
  if (rc!=kIOReturnSuccess && rc!=kIOReturnNotReady)
   SWEET_LOG_WARN("SWEET MDSS: RPMh detach rc=%#x; votes preserved, no implicit powerdown\n",(unsigned)rc);
  OSSafeReleaseNULL(fRPMh); fRPMhConsumer=0;
 }
}

void
SweetMDSS::setupIrqProbe(IOService *provider)
{
 int mode=0;
 (void)PE_parse_boot_argn("sweetmdssirqprobe",&mode,sizeof(mode));
 setProperty("SweetDisplayIRQPreflightReady",false);
 if (!mode) return; /* Default: neither map nor read the additional windows. */
 if (mode<1 || mode>2) {
  SWEET_LOG_ERROR("SWEET MDSS IRQ PREFLIGHT: invalid mode=%d; probe disabled\n",mode);
  return;
 }
 /* Append-only ADT ABI: 9=MDSS/MDP top, 10=INTF1/2 TE IRQ, 11=GICD.
  * All are provider resources, never consumer-side physical mappings.
  * A diagnostic failure must not take down the working display service. */
 if (!mapIrqResources(provider,mode==2)) {
  SWEET_LOG_ERROR("SWEET MDSS IRQ PREFLIGHT: resource missing/mismatch; probe disabled, display retained\n");
  for (unsigned i=9;i<12;++i) OSSafeReleaseNULL(fMaps[i]);
  return;
 }
 /* Reuse these validated maps if real IRQ mode is requested as well. */
 fIrqProbe=IOTimerEventSource::timerEventSource(this,irqProbeAction);
 if (!fIrqProbe || fLoop->addEventSource(fIrqProbe)!=kIOReturnSuccess) {
  SWEET_LOG_ERROR("SWEET MDSS IRQ PREFLIGHT: timer unavailable; probe disabled, display retained\n");
  OSSafeReleaseNULL(fIrqProbe);
  for (unsigned i=9;i<12;++i) OSSafeReleaseNULL(fMaps[i]);
  return;
 }
 fIrqProbeAdded=true; fIrqProbeMode=(unsigned)mode;
 setProperty("SweetDisplayIRQPreflightMode",(unsigned long long)mode,32);
 setProperty("SweetDisplayIRQPreflightReady",true);
 SWEET_LOG_INFO("SWEET MDSS IRQ PREFLIGHT: F3 v1 read-only mode=%u intid=115; no handler/enable/clear, software vsync retained\n",fIrqProbeMode);
 fIrqProbe->setTimeoutMS(30000);
}

bool
SweetMDSS::mapIrqResources(IOService *provider,bool all)
{
 const uint64_t pa[3]={0x0ae00000,0x0ae6e800,0x17a00000};
 const uint64_t size[3]={0x2000,0x200,0x10000};
 for (unsigned i=(all ? 9u : 11u);i<12;++i) {
  if (!fMaps[i]) fMaps[i]=provider->mapDeviceMemoryWithIndex(i);
  if (!fMaps[i] || fMaps[i]->getPhysicalAddress()!=pa[i-9] || fMaps[i]->getLength()<size[i-9]) {
   SWEET_LOG_ERROR("SWEET MDSS IRQ: resource %u missing/mismatch\n",i);
   return false;
  }
 }
 return true;
}

void
SweetMDSS::irqProbeAction(OSObject *owner,IOTimerEventSource *sender)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 /* Timer actions share the provider gate with power/brightness/stop. */
 if (!self->fAccepting || self->isInactive() || !self->fIrqProbeMode || self->fIrqProbePass>=3) return;
 const unsigned pass=self->fIrqProbePass++;
 const vm_offset_t gic=self->fMaps[11]->getVirtualAddress();
 const uint32_t ctl=dp_rd(gic,0x000), typer=dp_rd(gic,0x004), iid=dp_rd(gic,0x008);
 const uint32_t pidr2=dp_rd(gic,0xffe8);
 /* INTID115: word3 bit19; ICFGR7 bits7:6; IROUTER115 at0x6398.
  * Nonsecure group/groupmod reads may RAZ: log observations, no ownership claim. */
 const uint32_t group=dp_rd(gic,0x08c), groupmod=dp_rd(gic,0xd0c);
 const uint32_t en=dp_rd(gic,0x10c), pending=dp_rd(gic,0x20c), active=dp_rd(gic,0x30c);
 const uint32_t cfg=dp_rd(gic,0xc1c), priority=dp_rd(gic,0x470);
 const uint32_t routeLo=dp_rd(gic,0x6398), routeHi=dp_rd(gic,0x639c);
 SWEET_LOG_INFO("SWEET MDSS IRQ PREFLIGHT: v1 pass=%u GIC intid=115 ctl=%#x typer=%#x iid=%#x pidr2=%#x group-word=%#x groupmod-word=%#x en=%u pending=%u active=%u cfg=%u priority=%#x route=%#x:%08x\n",
     pass,ctl,typer,iid,pidr2,group,groupmod,(en>>19)&1u,(pending>>19)&1u,(active>>19)&1u,
     (cfg>>6)&3u,(priority>>24)&0xffu,routeHi,routeLo);
 if (self->fIrqProbeMode==2) {
  if ((self->fCorePhase==CoreIdle || self->coreReady()) && self->dp_clocks_on()) {
   const vm_offset_t top=self->fMaps[9]->getVirtualAddress();
   const vm_offset_t tear=self->fMaps[10]->getVirtualAddress();
   const uint32_t rev=dp_rd(top,0), sources=dp_rd(top,0x10);
   const uint32_t mdpEn=dp_rd(top,0x1010), mdpStatus=dp_rd(top,0x1014);
   const uint32_t mdpEn2=dp_rd(top,0x1008), mdpStatus2=dp_rd(top,0x100c);
   const uint32_t en1=dp_rd(tear,0), status1=dp_rd(tear,4);
   const uint32_t en2=dp_rd(tear,0x100), status2=dp_rd(tear,0x104);
   const uint32_t te=dp_rd(self->dp_intf1,0x284), cfg1=dp_rd(self->dp_intf1,0x288);
   const uint32_t rdptr=dp_rd(self->dp_intf1,0x2a4), line=dp_rd(self->dp_intf1,0x2b0);
   const uint32_t autorefresh=dp_rd(self->dp_intf1,0x2b4);
   SWEET_LOG_INFO("SWEET MDSS IRQ PREFLIGHT: v1 pass=%u MDSS rev=%#x sources=%#x mdp-en=%#x mdp-status=%#x mdp-en2=%#x mdp-status2=%#x tear1-en=%#x tear1-status=%#x tear2-en=%#x tear2-status=%#x panel=%u\n",
       pass,rev,sources,mdpEn,mdpStatus,mdpEn2,mdpStatus2,en1,status1,en2,status2,(unsigned)self->fPanelState);
   SWEET_LOG_INFO("SWEET MDSS IRQ PREFLIGHT: v1 pass=%u INTF1 te=%#x cfg=%#x rdptr=%#x line=%#x auto=%#x rdptr-enabled=%u rdptr-latched=%u\n",
       pass,te,cfg1,rdptr,line,autorefresh,(en1>>2)&1u,(status1>>2)&1u);
  } else {
   SWEET_LOG_WARN("SWEET MDSS IRQ PREFLIGHT: pass=%u MDSS clocks unavailable; display registers not read\n",pass);
  }
 }
 self->setProperty("SweetDisplayIRQPreflightSamples",(unsigned long long)self->fIrqProbePass,32);
 if (self->fIrqProbePass<3) sender->setTimeoutMS(1000);
}

void
SweetMDSS::publishPanelState(void)
{
 setProperty("SweetPanelDisplayState",(unsigned long long)fPanelState,32);
 setProperty("SweetPanelPowerFallback",fPanelFallback);
}

IOReturn
SweetMDSS::panelCommand(bool on)
{
 if (!dp_clocks_on() || (fClockEnabled && !localClocksReady())) return kIOReturnNotReady;
 /* LG R66456 PV: dtype05, last1, VC0, ack0, LP. Qualcomm's embedded
  * short header is [DCS, 0, dtype, last<<7], not a long packet payload.
  * dp_dcs_write already selects embedded/LP (DMA bits28/26). */
 const uint32_t word=0x80050000u | (on ? 0x29u : 0x28u);
 return dp_dcs_write(dp_intf1,dp_dsi,&word,1,true) ? kIOReturnIOError : kIOReturnSuccess;
}

IOReturn
SweetMDSS::panelAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 bool on=(uintptr_t)arg!=0;
 if (self->fLifecycleEnabled) return kIOReturnUnsupported; /* use asynchronous typed lifecycle */
 if (self->fManagedEnabled && !self->managedAccessReady()) return kIOReturnNotReady;
 if (self->fScanoutState==SweetScanoutQuiescing || self->fScanoutState==SweetScanoutSubmitted)
  self->cancelScanout("panel transition interrupted handoff");
 if (!self->fAccepting || self->isInactive()) return kIOReturnNotReady;
 if (self->dsiBusy()) return kIOReturnBusy;
 if (self->fDsiPoisoned) return kIOReturnNotReady;
 if (!self->fPanelPowerEnabled) return kIOReturnUnsupported;
 if (self->fClockEnabled && !self->localClocksReady()) return kIOReturnNotReady;
 /* A failed transaction may have reached the DDIC. Stop sending new OFFs
  * after any fault, but never suppress a needed ON/recovery request. This
  * path has its own state, independent of the brightness failure budget. */
 if (self->fIrqMode>=2) {
  self->fIrqDisplayOn=on;
  if (!on) self->pauseIRQ();
 }
 if (!on && self->fPanelFallback) return kIOReturnUnsupported;
 if (self->fPanelState==(on ? PanelOn : PanelOff)) {
  if (on && self->fIrqMode>=2 && self->fIrqPhase==2 && __atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)==IrqIdle)
   (void)self->armIRQ();
  return kIOReturnSuccess;
 }
 uint64_t start=mach_absolute_time(), elapsed;
 self->fPanelState=on ? PanelOnPending : PanelOffPending;
 self->publishPanelState();
 IOReturn rc=self->panelCommand(on), recovery=kIOReturnUnsupported;
 if (rc==kIOReturnSuccess) self->fPanelState=on ? PanelOn : PanelOff;
 else {
  self->fPanelState=PanelUnknown;
  self->fPanelFallback=true;
  if (!on) {
   /* One bounded compensation, in this same provider gate. Only a
    * successful 29 allows us to label the hardware ON again. */
   recovery=self->panelCommand(true);
   if (recovery==kIOReturnSuccess) self->fPanelState=PanelOn;
  }
 }
 self->publishPanelState();
 if (on && self->fIrqMode>=2 && self->fIrqPhase==2 && __atomic_load_n(&self->fIrqState,__ATOMIC_ACQUIRE)==IrqIdle) {
  if (rc==kIOReturnSuccess) (void)self->armIRQ();
  else self->failIRQ("panel ON failed");
 }
 absolutetime_to_nanoseconds(mach_absolute_time()-start,&elapsed);
 if (rc!=kIOReturnSuccess)
  SWEET_LOG_ERROR("SWEET MDSS POWER: %s rc=%#x recovery-attempted=%u recovery-on-rc=%#x state=%u fallback=1 elapsed-ms=%llu\n",
      on ? "on" : "off",(unsigned)rc,!on,(unsigned)recovery,(unsigned)self->fPanelState,elapsed/1000000ULL);
 else
  SWEET_LOG_INFO("SWEET MDSS POWER: %s rc=0 state=%u elapsed-ms=%llu\n",
      on ? "on" : "off",(unsigned)self->fPanelState,elapsed/1000000ULL);
 return rc;
}

IOReturn
SweetMDSS::setPanelPower(bool on)
{
 return fGate ? fGate->runAction(panelAction,(void *)(uintptr_t)on) : kIOReturnNotReady;
}

/* Configuration writes follow Lineage; observations never authorize DMA or
 * substitute for a hardware completion. Called only under the existing gate
 * and resource admission. Bound repeated diagnostics across provider lifetime. */
void SweetMDSS::observeLineage(const char *stage,vm_offset_t base,uint32_t offset,
    uint32_t wanted,uint32_t mask,bool always)
{
 observeLineageValue(stage,offset,wanted,dp_rd(base,offset),mask,always);
}
void SweetMDSS::observeLineageValue(const char *stage,uint32_t offset,uint32_t wanted,
    uint32_t observed,uint32_t mask,bool always)
{
 bool mismatch=((observed^wanted)&mask)!=0;
 if (!mismatch && (!always || fLineageMatches>=768u)) return;
 if (!mismatch) ++fLineageMatches;
 if (fLineageObservations>=1024u) return;
 ++fLineageObservations;
 SWEET_LOG_INFO("SWEET MDSS CONFIG: stage=%s offset=%#x want=%#x got=%#x mask=%#x mismatch=%u; diagnostic only\n",
     stage,offset,wanted,observed,mask,(unsigned)(((observed^wanted)&mask)!=0));
 if (fLineageObservations==1024u) SWEET_LOG_INFO("SWEET MDSS CONFIG: observation limit reached\n");
}

#include "SweetMDSSScanout.inc"
#include "SweetMDSSPipeline.inc"
#include "SweetMDSSClock.inc"
#include "SweetMDSSCore.inc"
#include "SweetMDSSNoC.inc"
#include "SweetMDSSResources.inc"
#include "SweetMDSSDSI.inc"
#include "SweetMDSSHost.inc"
#include "SweetMDSSLink.inc"
#include "SweetMDSSLifecycle.inc"
#include "SweetMDSSPanelSupply.inc"

IOReturn
SweetMDSS::readyAction(OSObject *owner,void *,void *,void *,void *)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 if (self->lifecycleBlocked()) return kIOReturnBusy;
 if (!self->fAccepting || self->isInactive()) return kIOReturnNotReady;
 if (self->dsiBusy()) return kIOReturnBusy;
 return self->managedAccessReady() && !self->fDsiPoisoned && self->dp_failed<3 && self->dp_clocks_on() &&
     (!self->fClockEnabled || self->localClocksReady()) ? kIOReturnSuccess : kIOReturnNotReady;
}
/* The backlight owns policy/fades. Retain only its latest typed target so a
 * full vendor OFF (51=0) and DDIC reset can restore it after the first frame.
 * Generic DCS remains LP; only this typed entry selects ordinary HS in the
 * pinned Lineage modes. Admission and completion still use writeAction. */
IOReturn SweetMDSS::brightnessAction(OSObject *owner,void *arg,void *,void *,void *)
{
 SweetMDSS *s=(SweetMDSS *)owner;
 if (!s->fAccepting || s->isInactive() || s->fDsiPoisoned) return kIOReturnNotReady;
 uint32_t level=*(uint32_t *)arg;
 const uint32_t words[2]={0xc0390003u,
     0xff000051u | ((level >> 8) << 8) | ((level & 0xffu) << 16)};
 IOReturn rc=writeAction(s,(void *)words,(void *)(uintptr_t)2,(void *)(uintptr_t)1,NULL);
 if (rc==kIOReturnSuccess || (rc==kIOReturnBusy && s->fPanelBrightnessKnown)) s->fPanelBrightnessTarget=level;
 if (rc==kIOReturnSuccess) s->fPanelBrightnessKnown=true;
 return rc;
}
IOReturn SweetMDSS::setBrightness(uint32_t level)
{
 if (!level) level=1;if (level>0xfffu) level=0xfffu;
 return fGate ? fGate->runAction(brightnessAction,&level) : kIOReturnNotReady;
}
IOReturn
SweetMDSS::brightnessReady(void)
{
 return fGate ? fGate->runAction(readyAction) : kIOReturnNotReady;
}
IOReturn
SweetMDSS::writeAction(OSObject *owner,void *words,void *n,void *typedBrightness,void *)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 if (self->lifecycleBlocked()) return kIOReturnBusy;
 if (!self->fAccepting || self->isInactive() || self->dp_failed>=3 || self->fDsiPoisoned) return kIOReturnNotReady;
 if (self->dsiBusy()) return kIOReturnBusy;
 if (!self->managedAccessReady()) return kIOReturnNotReady;
 if (self->fScanoutState==SweetScanoutQuiescing || self->fScanoutState==SweetScanoutSubmitted) return kIOReturnBusy;
 if (!self->dp_clocks_on() || (self->fClockEnabled && !self->localClocksReady())) {
  SWEET_LOG_WARN("SWEET MDSS: dcs skipped: MDSS clock gate not met\n");
  return kIOReturnNotReady;
 }
 int rc=self->dp_dcs_write(self->dp_intf1,self->dp_dsi,(const uint32_t *)words,(unsigned)(uintptr_t)n,
     self->fPanelPowerEnabled && (self->fPanelState==PanelOff || self->fPanelState==PanelUnknown),
     typedBrightness && self->fLifecycleLineage);
 self->dp_logged++;
 if (rc && ++self->dp_failed>=3) {
  SWEET_LOG_ERROR("SWEET MDSS: dcs failed %d times: writes disabled\n",self->dp_failed);
 } else if (!rc) self->dp_failed=0;
 return rc ? kIOReturnIOError : kIOReturnSuccess;
}
IOReturn
SweetMDSS::dcsWrite(const uint32_t *words,unsigned count)
{
 if (!words || !count || count>16) return kIOReturnBadArgument;
 /* Synchronous call: words remain owned by the retained caller throughout
  * runAction. No callback to a consumer while the provider gate is held. */
 return fGate ? fGate->runAction(writeAction,(void *)words,(void *)(uintptr_t)count) : kIOReturnNotReady;
}
void
SweetMDSS::probeAction(OSObject *owner,IOTimerEventSource *sender)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 if (!self->fAccepting || self->isInactive()) return;
 if (self->fLinkPhase!=LinkIdle && self->fLinkPhase!=LinkReady) return;
 if (self->fHostPhase!=HostIdle && self->fHostPhase!=HostReady) return;
 if (self->fCorePhase!=CoreIdle && !self->coreReady()) return;
 self->dp_probe(self->fPass,self->dp_gcc,self->dp_dispcc,self->dp_sspp,self->dp_ctl,
     self->dp_intf1,self->dp_intf2,self->dp_dsi,self->dp_smmu);
 if (++self->fPass<3) sender->setTimeoutMS(1000);
}
IOReturn
SweetMDSS::stopAction(OSObject *owner,void *,void *,void *,void *)
{
 SweetMDSS *self=(SweetMDSS *)owner;
 self->fAccepting=false;
 if (self->fLifecycleEnabled && self->fLifecycle.token)
  self->finishLifecycle(kIOReturnAborted,"provider stopped",true);
 if (self->fLifecycleTimer) self->fLifecycleTimer->cancelTimeout();
 self->stopManagedResources();
 self->stopNoC();
 self->stopLocalClocks();
 if (self->dsiBusy()) self->finishDSI(kIOReturnAborted,SweetDSICancelled);
 self->cancelScanout("provider stopped");
 self->stopIRQ();
 self->setProperty("SweetDisplayIRQPreflightReady",false);
 if (self->fProbe) self->fProbe->cancelTimeout();
 if (self->fIrqProbe) self->fIrqProbe->cancelTimeout();
 return kIOReturnSuccess;
}
bool
SweetMDSS::willTerminate(IOService *provider,IOOptionBits options)
{
 if (fGateAdded) (void)fGate->runAction(stopAction);
 return SweetDisplayBackend::willTerminate(provider,options);
}
void
SweetMDSS::stop(IOService *provider)
{
 if (fGateAdded) (void)fGate->runAction(stopAction);
 stopNoCIRQ();
 closeDisplayResources();
 if (fLifecycleTimerAdded) { fLoop->removeEventSource(fLifecycleTimer); fLifecycleTimerAdded=false; }
 if (fScanoutTimerAdded) { fLoop->removeEventSource(fScanoutTimer); fScanoutTimerAdded=false; }
 if (fProbeAdded) { fLoop->removeEventSource(fProbe); fProbeAdded=false; }
 if (fIrqProbeAdded) { fLoop->removeEventSource(fIrqProbe); fIrqProbeAdded=false; }
 if (fIrqEventAdded) { fLoop->removeEventSource(fIrqEvent); fIrqEventAdded=false; }
 if (fIrqTimerAdded) { fLoop->removeEventSource(fIrqTimer); fIrqTimerAdded=false; }
 if (fClockTimerAdded) { fLoop->removeEventSource(fClockTimer); fClockTimerAdded=false; }
 if (fNoCTimerAdded) { fLoop->removeEventSource(fNoCTimer); fNoCTimerAdded=false; }
 if (fDsiTimerAdded) { fLoop->removeEventSource(fDsiTimer); fDsiTimerAdded=false; }
 /* Gate/maps stay valid through free, including retained pre-stop callers.
  * Admission is closed; no MMIO may run after stopAction. */
 SWEET_LOG_INFO("SWEET MDSS: backend stopped; hardware admission closed\n");
 SweetDisplayBackend::stop(provider);
}
void
SweetMDSS::free(void)
{
 /* Covers partial start too; any hardware-reachable pool has its own pin. */
 if (fGateAdded) (void)fGate->runAction(stopAction);
 stopNoCIRQ();
 closeDisplayResources();
 if (fLifecycleTimerAdded) { fLoop->removeEventSource(fLifecycleTimer); fLifecycleTimerAdded=false; }
 if (fScanoutTimerAdded) { fLoop->removeEventSource(fScanoutTimer); fScanoutTimerAdded=false; }
 OSSafeReleaseNULL(fLifecycleTimer);
 OSSafeReleaseNULL(fLifecyclePool);
 OSSafeReleaseNULL(fScanoutTimer);
 OSSafeReleaseNULL(fHardwarePool);
 if (fClockTimerAdded) { fLoop->removeEventSource(fClockTimer); fClockTimerAdded=false; }
 if (fDsiTimerAdded) { fLoop->removeEventSource(fDsiTimer); fDsiTimerAdded=false; }
 OSSafeReleaseNULL(fClockTimer);
 if (fNoCTimerAdded) { fLoop->removeEventSource(fNoCTimer); fNoCTimerAdded=false; }
 OSSafeReleaseNULL(fNoCTimer);
 OSSafeReleaseNULL(fNoCIRQEvent);
 if (fNoCClaimed) { __atomic_store_n(&mdssNoCWriter,fNoCTouched ? 2u : 0u,__ATOMIC_RELEASE); fNoCClaimed=false; }
 if (fClockClaimed) { __atomic_store_n(&mdssClockWriter,fClockTouched ? 2u : 0u,__ATOMIC_RELEASE); fClockClaimed=false; }
 OSSafeReleaseNULL(fDsiTimer); OSSafeReleaseNULL(fDsiSequence);
 /* A timed-out/cancelled DMA keeps an independent prepared descriptor
  * reference until reboot. Never infer DMA exit from service termination. */
 if (fDsiPrepared && !fDsiPinned) { (void)fDsiBuffer->complete(kIODirectionOut); fDsiPrepared=false; }
 OSSafeReleaseNULL(fDsiBuffer);
 if (fProbeAdded) fLoop->removeEventSource(fProbe);
 if (fIrqProbeAdded) fLoop->removeEventSource(fIrqProbe);
 if (fIrqEventAdded) fLoop->removeEventSource(fIrqEvent);
 if (fIrqTimerAdded) fLoop->removeEventSource(fIrqTimer);
 if (fGateAdded) fLoop->removeEventSource(fGate);
 if (fIntervals) { IOFreeData(fIntervals,4096*sizeof(*fIntervals)); fIntervals=NULL; }
 OSSafeReleaseNULL(fIrqEvent); OSSafeReleaseNULL(fIrqTimer);
 OSSafeReleaseNULL(fIrqProbe); OSSafeReleaseNULL(fProbe); OSSafeReleaseNULL(fGate); OSSafeReleaseNULL(fLoop);
 for (unsigned i=0;i<19;++i) OSSafeReleaseNULL(fMaps[i]);
 SweetDisplayBackend::free();
}

#include "SweetMDSSIRQ.inc"

uint32_t
SweetMDSS::dp_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

void
SweetMDSS::dp_dump(const char *what, vm_offset_t base, uint32_t from, uint32_t to)
{
	for (uint32_t o = from; o < to; o += 0x20) {
		SWEET_LOG_INFO("SWEET MDSS: %s +%03x: %08x %08x %08x %08x %08x %08x %08x %08x\n", what, o,
		    dp_rd(base, o), dp_rd(base, o + 4), dp_rd(base, o + 8), dp_rd(base, o + 12),
		    dp_rd(base, o + 16), dp_rd(base, o + 20), dp_rd(base, o + 24), dp_rd(base, o + 28));
	}
}

void
SweetMDSS::dp_smmu_context(uint32_t id0, uint32_t id1, uint32_t s2cr)
{
	int trace = 0;
	(void)PE_parse_boot_argn("sweetfbtrace", &trace, sizeof(trace));
	if (trace < 2 || !sweet_log_level_enabled(2u)) { return; }
	if (id0 != 0x4c017e5eu || id1 != 0x60000038u ||
	    ((s2cr >> 16) & 3u) != 0 || (s2cr & 0xffu) != 2) {
		SWEET_LOG_WARN("SWEET MDSS: F0 v2 unknown SMMU layout/route, skip CB config\n");
		return;
	}
	/* dp_smmu already maps both GR0 and GR1 pages. */
	dp_smmu_gr1 = dp_smmu + 0x1000u;
	/* Optional CB2 resource was mapped by the provider; never map behind its back. */
	if (!dp_smmu_gr1 || !dp_smmu_cb2) {
		SWEET_LOG_WARN("SWEET MDSS: F0 v2 optional CB2 mapping unavailable\n");
		return;
	}
	vm_offset_t cb = dp_smmu_cb2;
	SWEET_LOG_INFO("SWEET MDSS: F0 v2 cb2 pa=0x15082000 cbar=%#x cba2r=%#x sctlr=%#x actlr=%#x ttbcr2=%#x ttbcr=%#x ttbr0=%08x:%08x ttbr1=%08x:%08x mair=%08x:%08x\n",
	    dp_rd(dp_smmu_gr1, 2u * 4u), dp_rd(dp_smmu_gr1, 0x800u + 2u * 4u),
	    dp_rd(cb, 0x00), dp_rd(cb, 0x04), dp_rd(cb, 0x10), dp_rd(cb, 0x30),
	    dp_rd(cb, 0x24), dp_rd(cb, 0x20), dp_rd(cb, 0x2c), dp_rd(cb, 0x28),
	    dp_rd(cb, 0x3c), dp_rd(cb, 0x38));
}

void
SweetMDSS::dp_probe(unsigned pass, vm_offset_t gcc, vm_offset_t dispcc, vm_offset_t sspp, vm_offset_t ctl,
    vm_offset_t intf1, vm_offset_t intf2, vm_offset_t dsi, vm_offset_t smmu)
{
	/*
	 * Same gate as the K20 Pro code (raphael_display_init), without its enable
	 * writes: gcc_disp_ahb only needs CLK_ENABLE (it is hardware-gated, so
	 * CLK_OFF reads 1 while the bus is idle: 0x80000003 on mh2lm, 0108 run 1),
	 * and dispcc +0x2080 must report CLK_OFF clear.
	 */
	uint32_t ahb = dp_rd(gcc, GCC_DISP_AHB);
	SWEET_LOG_INFO("SWEET MDSS: pass %u gcc disp_ahb %#x\n", pass, ahb);
	if (!(ahb & CBCR_ENABLE)) {
		SWEET_LOG_INFO("SWEET MDSS: gcc disp_ahb not enabled: no DISPCC/MDSS read\n");
		return;
	}
	uint32_t mahb = dp_rd(dispcc, DISPCC_AHB_CBCR);
	SWEET_LOG_INFO("SWEET MDSS: dispcc +0x2080 %#x\n", mahb);
	if (mahb & CBCR_OFF) {
		SWEET_LOG_INFO("SWEET MDSS: dispcc +0x2080 clock off: no MDSS read\n");
		return;
	}
	/* VIG0 source: read-only geometry, format and address latch. */
	SWEET_LOG_INFO("SWEET MDSS: vig0 size %#x xy %#x out %#x addr %#x stride %#x fmt %#x unpack %#x op %#x addr-status %#x\n",
	    dp_rd(sspp, 0x00), dp_rd(sspp, 0x08), dp_rd(sspp, 0x0c), dp_rd(sspp, 0x14),
	    dp_rd(sspp, 0x24), dp_rd(sspp, 0x30), dp_rd(sspp, 0x34), dp_rd(sspp, 0x38),
	    dp_rd(sspp, 0x70));
	SWEET_LOG_INFO("SWEET MDSS: vig0 expect size %#x addr %#x stride %#x\n",
	    (uint32_t)(SWEET_DEV_FB_H << 16 | SWEET_DEV_FB_W), (uint32_t)SWEET_DEV_FB_PA,
	    (uint32_t)SWEET_DEV_FB_STRIDE);
	SWEET_LOG_INFO("SWEET MDSS: ctl0 flush %#x start %#x\n", dp_rd(ctl, 0x18), dp_rd(ctl, 0x1c));
	vm_offset_t intf[2] = {intf1, intf2};
	for (unsigned i = 0; i < 2; i++) {
		vm_offset_t p = intf[i];
		SWEET_LOG_INFO("SWEET MDSS: intf%u te-en %#x cfg %#x height %#x init %#x count %#x line %#x auto %#x frame %#x\n",
		    i + 1, dp_rd(p, 0x284), dp_rd(p, 0x288), dp_rd(p, 0x28c), dp_rd(p, 0x294),
		    dp_rd(p, 0x298), dp_rd(p, 0x2b0), dp_rd(p, 0x2b4), dp_rd(p, 0xac));
		uint32_t frame = dp_rd(p, 0xac), line = dp_rd(p, 0x2b0);
		uint64_t ns;
		absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
		if (dp_last_ns[i] != 0) {
			SWEET_LOG_INFO("SWEET MDSS: F0 v2 intf%u elapsed-ns=%llu frame-delta=%u tear-line-phase=%#x frame-en=%#x raster-line=%#x command-mode-not-TE-rate\n",
			    i + 1, ns - dp_last_ns[i], frame - dp_last_frame[i], line, dp_rd(p, 0xa8), dp_rd(p, 0xb0));
		}
		dp_last_frame[i] = frame; dp_last_ns[i] = ns;
	}
	/* Read-only ARM SMMU v2 stream matching. Never write or probe a gated status FIFO. */
	if (smmu != 0) {
		uint32_t id0 = dp_rd(smmu, 0x20), id1 = dp_rd(smmu, 0x24);
		uint32_t nsmr = id0 & 0xffu;
		SWEET_LOG_INFO("SWEET MDSS: smmu id0 %#x id1 %#x num-smr=%u sid=%#x\n", id0, id1, nsmr, SMMU_SID);
		for (uint32_t i = 0; i < nsmr && i < 256; i++) {
			uint32_t smr = dp_rd(smmu, 0x800u + i * 4u);
			uint32_t smask = (smr >> 16) & 0x7fffu;
			if ((smr & 0x80000000u) && ((smr & 0x7fffu & ~smask) == (SMMU_SID & ~smask))) {
				uint32_t s2cr = dp_rd(smmu, 0xc00u + i * 4u);
				SWEET_LOG_INFO("SWEET MDSS: smmu match idx=%u smr=%#x s2cr=%#x type=%u cb=%u\n",
				    i, smr, s2cr, (s2cr >> 16) & 3u, s2cr & 0xffu);
				if (pass == 0) { dp_smmu_context(id0, id1, s2cr); }
			}
		}
	} else {
		SWEET_LOG_INFO("SWEET MDSS: smmu mapping unavailable\n");
	}
	/* DSI0: +0x004 ctrl (mode bits), +0x008 status, +0x03c cmd DMA ctrl, +0x04c length,
	 * +0x084 trigger, +0x15c embedded cmd FIFO ctrl (the K20 Pro code reads these). */
	SWEET_LOG_INFO("SWEET MDSS: dsi0 hw %#x ctrl %#x status %#x dma %#x len %#x trig %#x fifo %#x\n",
	    dp_rd(dsi, 0x000), dp_rd(dsi, 0x004), dp_rd(dsi, 0x008), dp_rd(dsi, 0x03c),
	    dp_rd(dsi, 0x04c), dp_rd(dsi, 0x084), dp_rd(dsi, 0x15c));
	if (pass == 0) {
		dp_dump("dsi0", dsi, 0x000, 0x0a0);
		dp_dump("intf1", intf1, 0x280, 0x2b8);
		dp_dump("intf2", intf2, 0x280, 0x2b8);
	}
}

void
SweetMDSS::dp_wr(vm_offset_t base, uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + off) = v;
	__asm__ volatile("dsb sy" ::: "memory");
}

int
SweetMDSS::dp_poll(vm_offset_t base, uint32_t off, uint32_t mask, uint32_t want, unsigned ms)
{
	for (unsigned us = 0; us < ms * 1000u; us += 50) {
		if ((dp_rd(base, off) & mask) == want) {
			return 0;
		}
		delay(50);
	}
	return -1;
}

int
SweetMDSS::dp_dcs_write(vm_offset_t intf, vm_offset_t dsi, const uint32_t *words, unsigned count, bool idleOnly, bool hs)
{
	if ((fLifecycleLineage && (!fDsiPrepared || !fDsiBuffer || fDsiTransferEngine || fDsiPoisoned)) ||
	    ((dp_rd(dsi, 0x004) & (fLifecycleLineage ? 3u : 7u)) != (fLifecycleLineage ? 1u : 5u)) || dp_rd(dsi, 0x15c) != 0 || (dp_rd(dsi, 0x110) & (1u << 24))) {
		SWEET_LOG_ERROR("SWEET MDSS: dcs: DSI not in the expected state (ctrl %#x fifo %#x irq %#x)\n",
		    dp_rd(dsi, 0x004), dp_rd(dsi, 0x15c), dp_rd(dsi, 0x110));
		return -1;
	}
	uint32_t sync = dp_rd(intf, 0x288), refresh = dp_rd(intf, 0x2b4);
	dp_wr(intf, 0x288, sync & ~(1u << 20));         /* no new TE kickoff */
	dp_wr(intf, 0x2b4, 0);                          /* autorefresh off */
	int rc = 0;
	uint32_t dma=0,length=0,trigger=0,irq=0,status=0,ack=0;
	const uint32_t statuses=0x51115501u;
	/* F2 commands cannot depend on TE/line progress from a panel that may
	 * be off. After stopping new kickoff/autorefresh above, DSI busy bits
	 * are the admission condition. LG likewise polls DSI_STATUS for cmd
	 * mode MDP idle. Legacy brightness retains its exact line-wait trace. */
	unsigned us=0;
	for (; !idleOnly && us < 100000u; us += 50) {
		uint32_t line = dp_rd(intf, 0x2b0) & 0xffffu;
		if (!line || line >= SWEET_DEV_FB_H) {
			break;
		}
		delay(50);
	}
	if (us >= 100000u || dp_poll(dsi, 0x008, (1u << 2) | (1u << 1), 0, 200)) {
		SWEET_LOG_ERROR("SWEET MDSS: dcs: DSI did not go idle (status %#x line %#x)\n",
		    dp_rd(dsi, 0x008), dp_rd(intf, 0x2b0));
		rc = -2;
		goto restore;
	}
	if (!acquireDSITransfer()) { rc=-1;goto restore; }
	dma = dp_rd(dsi, 0x03c); length = dp_rd(dsi, 0x04c); trigger = dp_rd(dsi, 0x084);
	/* W1C only the DMA/BTA status bits. */
	dp_wr(dsi, 0x110, (dp_rd(dsi, 0x110) & ~statuses) | 1u | (1u << 20));
	if (fLifecycleLineage && (dp_rd(dsi,0x110)&(1u|(1u<<24)))) {
		fDsiPoisoned=true; /* stale status is not fresh completion */
		return -4;
	}
	dp_wr(dsi, 0x084, (trigger & ~7u) | 4u);        /* software DMA trigger */
	dp_wr(dsi, 0x15c, (1u << 1) | (1u << 2) | (3u << 16));
	for (unsigned i = 0; i < count; i++) {
		dp_wr(dsi, 0x17c, words[i]);
	}
	if (count & 1u) {
		dp_wr(dsi, 0x17c, 0);
	}
	dp_wr(dsi, 0x03c, (dma & ~((1u << 31) | (1u << 30) | (1u << 26))) | (1u << 28) | (hs ? 0u : (1u << 26)));
	dp_wr(dsi, 0x04c, count * 4u);
	dp_wr(dsi, 0x090, 1);                           /* CMD_DMA trigger */
	if (dp_poll(dsi, 0x110, 1u, 1u, 200)) {
		rc = -3;
	} else if (dp_rd(dsi, 0x110) & (1u << 24)) {
		rc = -4;
	}
	if (fLifecycleLineage && !rc && dp_poll(dsi,0x008,6u,0,200)) rc=-3;
	irq = dp_rd(dsi, 0x110); status = dp_rd(dsi, 0x008); ack = dp_rd(dsi, 0x068);
	if (fLifecycleLineage && !dsiTransferClockReady()) {
		fDsiPoisoned=true;return -4; /* lost lease/clock: no cleanup writes */
	}
	if (fLifecycleLineage && (rc || !(irq&1u) || (irq&(1u<<24)) || ack || (status&6u))) {
		/* Same completion rule as asynchronous packets: retain the reference,
		 * keep TE paused and close admission. No FIFO reset on uncertain DMA. */
		fDsiPoisoned=true;
		SWEET_LOG_ERROR("SWEET MDSS DSI: synchronous transfer uncertain; engine reference retained\n");
		return rc ? rc : -4;
	}
	dp_wr(dsi, 0x110, (dp_rd(dsi, 0x110) & ~statuses) | 1u | (1u << 20));
	dp_wr(dsi, 0x15c, 0);
	dp_wr(dsi, 0x1ec, 1);                           /* FIFO reset, as the native timeout path */
	delay(1);
	dp_wr(dsi, 0x1ec, 0);
	dp_wr(dsi, 0x04c, length);
	dp_wr(dsi, 0x03c, dma);
	dp_wr(dsi, 0x084, trigger);
	if (rc != 0 || dp_logged < 8) {
		if (rc) {
            SWEET_LOG_ERROR("SWEET MDSS: dcs: rc %d irq %#x status %#x ack %#x\n", rc, irq, status, ack);
        } else {
            SWEET_LOG_INFO("SWEET MDSS: dcs: rc %d irq %#x status %#x ack %#x\n", rc, irq, status, ack);
        }
	}
restore:
	if (!releaseDSITransfer() || (fLifecycleLineage &&
	    (fDsiPoisoned || !dsiClockContextReady()))) return rc ? rc : -4;
	dp_wr(intf, 0x288, sync);
	dp_wr(intf, 0x2b4, refresh);
	return rc;
}

int
SweetMDSS::dp_clocks_on(void)
{
	return (dp_rd(dp_gcc, GCC_DISP_AHB) & CBCR_ENABLE) &&
	       !(dp_rd(dp_dispcc, DISPCC_AHB_CBCR) & CBCR_OFF);
}

#endif
