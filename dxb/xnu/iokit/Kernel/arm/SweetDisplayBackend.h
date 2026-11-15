/* Typed per-consumer display backend. No global lookup or BSD MMIO bridge. */
#ifndef SWEET_DISPLAY_BACKEND_H
#define SWEET_DISPLAY_BACKEND_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include "SweetDSICommand.h"

/* Warm transactions preserve rails and panel initialization. Full prepare,
 * reset and unprepare remain separate operations. Mode2 follows Lineage
 * suspend ULPS: profile sleep/reset, DATA/link parking and panel-only supply
 * vote withdrawal/restore, while shared core/config power stays held. Mode3
 * additionally releases/reacquires managed DATA leases, retaining a separate
 * config floor and all boot/core/link/power ownership. Mode4 shuts down and
 * reconstructs host/PHY/PLL, retaining all resource tickets. Mode7 adds a
 * new software pipeline generation to mode2; fresh pixels remain separate. */
enum SweetPanelOperation { SweetPanelTakeover=1, SweetPanelDisplayOff,
 SweetPanelDisplayOn, SweetPanelPrepare, SweetPanelReset, SweetPanelUnprepare };
enum { SweetPanelRetainedSleepReset=1u<<16, SweetPanelDataParking=1u<<17, SweetPanelLinkPowerCycle=1u<<18, SweetPanelSupplyCycle=1u<<19, SweetPanelPipelineRebuild=1u<<20 };
enum SweetPanelPhase { SweetPanelIdle, SweetPanelTouchPause, SweetPanelDrain,
 SweetPanelTakeoverPending, SweetPanelCommand, SweetPanelFrame,
 SweetPanelTouchResume, SweetPanelComplete, SweetPanelFault, SweetPanelStopped,
 SweetPanelPinActivate, SweetPanelPinReset, SweetPanelBrightness,
 SweetPanelResourcePark, SweetPanelResourceRestore,
 SweetPanelLinkPowerDown, SweetPanelLinkRebuild,
 SweetPanelPinParkActivate, SweetPanelPinParkWait, SweetPanelPinPark,
 SweetPanelSupplyActivate, SweetPanelSupplyPark, SweetPanelSupplyRestore, SweetPanelPipelineRestore,
 SweetPanelBrightnessDrain, SweetPanelColorRestore };
struct SweetPanelLifecycleStatus {
 uint64_t token, touchToken, commandToken, pinToken;
 unsigned operation, phase, rails, profile;
 IOReturn result;
 bool touchQuiesced, displayDrained, resourcesHeld, pixelsComplete, uncertain;
 bool sleeping, resetComplete, brightnessRestored;
 /* Data parking releases only managed data/clock leases; power/config and
  * the boot clock lease stay held. Neither field implies physical rail-off. */
 bool resourcesParked, resourcesRestored, retainedPower, configHeld;
 bool linkPoweredDown, linkRebuilt;
 uint64_t configToken, dataToken, clockToken;
 /* This consumer's ACKed withdrawals, NOT physical rail-off. Shared L3/L5
  * stay held. Aggregate enabled may remain set because of other consumers. */
 unsigned panelReleasedMask, panelAggregateEnabledMask, supplyRail;
 uint64_t supplyToken, panelVoteTokens[2];
 bool resetHeldLow, panelVotesParked, panelVotesRestored;
 /* Monotonic software configuration generation; pixelsComplete is separate. */
 uint64_t pipelineGeneration;
};
class SweetMDSS;
class SweetScanoutPool;
enum SweetScanoutState { SweetScanoutIdle, SweetScanoutQuiescing,
 SweetScanoutSubmitted, SweetScanoutActive, SweetScanoutFailed, SweetScanoutResources, SweetScanoutCoordinating, SweetScanoutRebuilding };
struct SweetScanoutStatus {
 unsigned state;
 bool hardwareReachable;
 uint64_t address, frame;
 uint32_t flush, done, fsr;
};

/* F4 entry: a gate-serialized, read-only hardware observation. No DMA address
 * authorization or completion fence is inferred from these register values. */
struct SweetScanoutSnapshot {
 uint64_t frame, ticks;
 uint32_t size, xy, out, outXY, address, stride, format, unpack, op, secure;
 uint32_t flush, start, ctlTop, intfCfg, intfStatus, te, autoRefresh, wrCount, line, outLine;
 uint32_t mdpEnable, mdpStatus, tearEnable, tearStatus;
 uint32_t smmuID0, smmuID1, smmuCR0, smmuFault, matches, streamIndex, smr, s2cr;
 uint32_t cbar, cba2r, sctlr, ttbcr, ttbrLo, ttbrHi, mairLo, mairHi, cbFault, farLo, farHi;
 bool knownSmmu, cb2Observed;
};
enum SweetVsyncState { SweetVsyncWaiting, SweetVsyncFrame, SweetVsyncOff, SweetVsyncFailed, SweetVsyncStopped };
struct SweetVsyncSample {
 uint64_t sequence, frame, ticks;
 unsigned state;
};
/* A retained, single-consumer mailbox. Provider only queues a software IES;
 * it never acquires the consumer workloop gate or calls consumer code.
 * close synchronizes publication before releasing the event source. */
class SweetVsyncChannel : public OSObject {
 OSDeclareDefaultStructors(SweetVsyncChannel);
 friend class SweetMDSS;
 IOSimpleLock *fLock;
 IOInterruptEventSource *fEvent;
 SweetVsyncSample fSample;
 void publish(uint64_t frame,uint64_t ticks,unsigned state);
public:
 static SweetVsyncChannel *withEvent(IOInterruptEventSource *);
 bool copySample(SweetVsyncSample *);
 void close(void);
 void free(void) APPLE_KEXT_OVERRIDE;
};

class SweetDisplayBackend : public IOService {
 OSDeclareAbstractStructors(SweetDisplayBackend);
public:
 /* F2: Display Off/On only; no sleep, rails, reset, brightness or scanout.
  * Synchronous, gate-serialized, no callbacks into a consumer. */
 virtual IOReturn setPanelPower(bool on) = 0;
 /* Thread context; caller retains the provider/channel until detach. One
  * framebuffer per pipeline. detach synchronizes all mailbox publications. */
 virtual IOReturn attachVsync(SweetVsyncChannel *) = 0;
 virtual void detachVsync(SweetVsyncChannel *) = 0;
 /* Nonblocking: queue shutdown if the consumer detects delivery starvation. */
 virtual void requestVsyncFallback(void) = 0;
 /* Caller supplies stack output and holds a retained provider. Thread context,
  * provider gate only; no MMIO mapping, callback or wait in the consumer. */
 virtual IOReturn inspectScanout(SweetScanoutSnapshot *) = 0;
 /* One opt-in handoff per provider. No callback/wait across consumer gates.
  * Pool is retained before admission; hardware gets a separate lifetime pin
  * before the first source write. Status is an observation, NOT a flip fence. */
 virtual IOReturn beginScanout(SweetScanoutPool *,SweetVsyncChannel *) { return kIOReturnUnsupported; }
 virtual IOReturn scanoutStatus(SweetScanoutPool *,SweetScanoutStatus *) { return kIOReturnUnsupported; }
 virtual void abandonScanout(SweetScanoutPool *) {}
 /* Asynchronous LP write sequence, LG 7-byte records. Copies the entire
  * immutable request before returning; token/status are provider-owned.
  * Internal kernel clients only: profile authorization belongs to the panel
  * lifecycle layer, not to an untrusted user-client selector. No callbacks.
  * A successful begin means admitted, not completed. All payloads prevalidated.
  * Lifecycle caller must quiesce the frame/IRQ stream before a full table;
  * transport does not rearm it or infer panel power from opaque bytes.
  * Default disabled (sweetdsi=1 opts in); legacy brightness/F2 stay unchanged. */
 virtual IOReturn beginDSISequence(const uint8_t *,size_t,uint64_t *) { return kIOReturnUnsupported; }
 virtual IOReturn dsiSequenceStatus(uint64_t,SweetDSIStatus *) { return kIOReturnUnsupported; }
 virtual void cancelDSISequence(uint64_t) {}
 /* Nonblocking lifecycle requests. Admission is not completion. Zero caps
  * means this backend uses the legacy F2 path. Tokens never wrap or resume
  * through a replacement provider. Status remains readable after stop. */
 virtual unsigned panelLifecycleCapabilities(void) { return 0; }
 virtual IOReturn beginPanelLifecycle(unsigned,uint64_t *) { return kIOReturnUnsupported; }
 virtual IOReturn panelLifecycleStatus(uint64_t,SweetPanelLifecycleStatus *) { return kIOReturnUnsupported; }
 virtual void cancelPanelLifecycle(uint64_t) {}
 virtual IOReturn brightnessReady(void) = 0;
 virtual IOReturn dcsWrite(const uint32_t *words, unsigned count) = 0;
 virtual IOReturn setBrightness(uint32_t level);
};

/* Owner must stop the client outside its workloop gate, before destroying
 * callback context. Matching callbacks only retain/withdraw and queue an event.
 * copyBackend returns a retained reference; every operation still checks
 * provider admission under its gate. Notification != hardware readiness. */
class SweetDisplayClient : public OSObject {
 OSDeclareDefaultStructors(SweetDisplayClient);
 IOLock *fLock;
 IOWorkLoop *fLoop;
 IOInterruptEventSource *fEvent;
 IONotifier *fMatched, *fTerminated, *fInterest;
 SweetDisplayBackend *fObserved;
 SweetDisplayBackend *fBackend;
 OSObject *fOwner;                 /* borrowed until stop synchronizes event */
 void (*fChanged)(OSObject *);
 bool fStopping, fEventAdded;
 static bool matched(void *, void *, IOService *, IONotifier *);
 static bool terminated(void *, void *, IOService *, IONotifier *);
 static IOReturn interest(void *,void *,uint32_t,IOService *,void *,vm_size_t);
 static void changed(OSObject *, IOInterruptEventSource *, int);
public:
 static SweetDisplayClient *withOwner(OSObject *, IOWorkLoop *, void (*)(OSObject *));
 bool watch(void); /* call after owner stores the client */
 SweetDisplayBackend *copyBackend(void);
 void stop(void);
 void free(void) APPLE_KEXT_OVERRIDE;
};
#endif
#endif
