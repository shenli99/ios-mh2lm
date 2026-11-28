/* MH2LM ACTIVE-only regulator provider. No singleton or raw-address API. */
#ifndef SWEET_RPMH_H
#define SWEET_RPMH_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include "SweetAppsDoor.h"
#include "SweetAppsQueue.h"
#include "SweetRPMhExecutor.h"

enum SweetRail { SweetRailVDDI, SweetRailVPNL, SweetRailHost, SweetRailPHY, SweetRailCount };
struct SweetRailVote { bool enabled; unsigned minMV, maxMV, loadUA; };
struct SweetRailInfo {
 unsigned minMV, maxMV, modeThresholdUA;
 bool available;
 /* A last XNU vote is not an exclusive physical owner. Known UFS/USB
  * users still rely on inherited firmware state, outside this client ledger.
  * False means no known shared-user veto, not permission/proof of rail-off. */
 bool lastDisableRequiresHandoff;
};
enum SweetRPMhState { SweetRPMhIdle, SweetRPMhPending, SweetRPMhComplete, SweetRPMhFailed };
struct SweetRPMhStatus {
 uint64_t token;
 unsigned state, rail, millivolts, mode;
 IOReturn result;
 bool enabled, uncertain;
};

/* Semantic display corners, never millivolts or caller-supplied ARC indices.
 * The provider derives MX/CX dependencies from the LG SM8150 supply graph. */
enum SweetDisplayCorner : unsigned { SweetDisplayOff, SweetDisplayLowSVS, SweetDisplaySVS,
 SweetDisplayNominal, SweetDisplayTurbo, SweetDisplayCornerCount };
enum SweetDisplaySupply { SweetDisplayCX, SweetDisplayMX, SweetDisplayMMCX,
 SweetDisplaySupplyCount };
struct SweetDisplayPowerInfo {
 bool available, supported[SweetDisplayCornerCount];
};
struct SweetDisplayPowerStatus {
 uint64_t token;
 unsigned state;
 SweetDisplayCorner corner; /* Aggregate requested corner, not a clock rate. */
 IOReturn result;
 bool enabled, uncertain;
 unsigned levels[SweetDisplaySupplyCount]; /* Selected raw DB semantic levels. */
 unsigned acknowledged; /* Supplies ACKed in this request; not voltage proof. */
 unsigned appliedLevels[SweetDisplaySupplyCount], known; /* Last confirmed votes. */
};

enum SweetDisplayBCM : unsigned { SweetBCMCN0, SweetBCMMM1, SweetBCMMM0,
 SweetBCMSH0, SweetBCMMC0, SweetBCMACV, SweetDisplayBCMCount };
/* Hardware metadata is exchanged only between the actual MMIO owners.
 * General consumers cannot construct commands or obtain a raw-address API. */
class SweetBCMInfo {
 friend class SweetRPMh;
 friend class SweetMDSS;
 uint32_t address, unit;
 unsigned width, vcd, enableMask;
 bool encode(uint64_t,uint64_t,unsigned,unsigned,uint32_t *) const;
 bool scale(uint64_t,unsigned,unsigned,unsigned *) const;
public:
 SweetBCMInfo() : address(0), unit(0), width(0), vcd(0), enableMask(0) {}
};
struct SweetRegisterBandwidthStatus {
 uint64_t token, bytesPerSecond;
 unsigned state;
 IOReturn result;
 bool uncertain;
};
struct SweetBandwidth { uint64_t average, peak; }; /* Bytes/second. */
struct SweetDisplayBandwidth {
 SweetBandwidth port[2], llcc, ebi;
 uint64_t registerPeak;
};
enum SweetNoCState { SweetNoCEmpty, SweetNoCPending, SweetNoCHeld,
 SweetNoCReleasing, SweetNoCReleased, SweetNoCFailed };
struct SweetNoCStatus {
 uint64_t token;
 unsigned state, acknowledged;
 IOReturn result;
 bool uncertain, registerAcknowledged;
};

class SweetPanelPins;
class SweetTouchPanel;
/* Exact exclusive leaf-provider leases for ONE panel vote. The caller owns
 * these references for admission; RPMh retains them only while pending.
 * IDs/tokens are issued by the actual providers, never raw MMIO or callbacks.
 * Lock order: consumer -> RPMh -> Pins/Touch. Neither leaf calls RPMh/consumer.
 * Reset-low is the commanded GPIO ledger; raw electrical echoes are diagnostic.
 * Quiesced touch is a software/IRQ fence, not physical supply isolation. */
struct SweetPanelSupplyFence {
 SweetPanelPins *pins;
 SweetTouchPanel *touch;
 uint64_t pinsConsumer, pinsToken, touchConsumer, touchToken;
};

extern "C" bool sweet_rpmh_irq(unsigned int);

/* C3-05: which entry point a submission belongs to. A panel vote is
 * deliberately absent: its fence hands over exclusive leaf-provider references
 * that would have to outlive the wait, and cancelling or detaching a waiting
 * request is the contract C3-06 owns. Those requests are therefore still
 * refused while anything is in flight or waiting, instead of being queued
 * behind a vote that may never retire. */
enum SweetAppsRequestKind : unsigned { SweetAppsRequestNone,
 SweetAppsRequestRailVote, SweetAppsRequestDisplayCorner,
 SweetAppsRequestRegisterBandwidth };

class SweetRPMh : public IOService {
 friend bool sweet_rpmh_irq(unsigned int);
 OSDeclareDefaultStructors(SweetRPMh);
 IOWorkLoop *fLoop;
 IOCommandGate *fGate;
 IOTimerEventSource *fTimer;
 IOMemoryMap *fMaps[4];
 vm_offset_t fRSC, fTCS, fDB;
 unsigned fDBSize, fNCPT, fMode;
 uint32_t fAddresses[SweetRailCount];
 bool fGateAdded, fTimerAdded, fAccepting, fClaimed, fEverWritten, fPoisoned;
 struct Client {
  uint64_t id; SweetRailVote votes[SweetRailCount]; SweetRPMhStatus status;
  SweetDisplayCorner displayVote; SweetDisplayPowerStatus displayStatus;
  uint64_t registerVote; SweetRegisterBandwidthStatus registerStatus;
 };
 Client fClients[8];
 SweetRailVote fAggregate[SweetRailCount];
 bool fOwned[SweetRailCount];
 uint64_t fNextClient, fNextToken, fDeadline, fWriteDeadline;
 unsigned fClientIndex, fRail, fTCSIndex, fCommandCount, fOperation, fOperationCount;
 uint32_t fSavedIRQ, fCommandMask;
 bool fBusy, fIRQObserved, fWriteIssued, fExecuting;
 /* Physical IRQ dispatch owns no consumer access. Gate -> dispatch lock only;
  * the top half masks one SPI and queues this service's software event. */
 IOMemoryMap *fIRQMap;
 IOInterruptEventSource *fIRQEvent;
 bool fIRQEventAdded, fIRQClaimed, fIRQTouched, fIRQConfigured;
 uint64_t fIRQConfigureDeadline;
 unsigned fIRQOnline, fIRQArmed, fIRQPosted, fIRQEntries, fIRQSourceMask;
 /* C3-04: the owner is decided from the ADT before the first register access.
  * fDelegated means the channel owns the array and the line; this service then
  * maps no TCS window, registers no handler and reaches the hardware only
  * through the published table. fTable is this service's own table in the
  * other mode, so both modes have exactly one write path. */
 bool fDelegated, fTablePublished, fIdentityReady, fInstalled;
 /* C3-05: one transaction at a time, in arrival order. A second legal request
  * waits in fQueue instead of being refused, and fCurrent is the copy the
  * machine is working on - including the multi-flight display chain, so a
  * request holds its place until its whole chain retires. The record lives in
  * this service's own storage, so a dispatched request no longer depends on
  * the caller's stack or arguments. A request that becomes illegal while it
  * waits fails against its own token and row; it never overwrites the older
  * one, and a poisoned service drains the queue instead of crossing that
  * state. fQueueSelfTest is the explicit ADT request to demonstrate two
  * admissions; absent means nothing runs. */
 SweetAppsQueue fQueue;
 SweetAppsRequest fCurrent;
 bool fCurrentValid, fDispatching, fQueueSelfTest, fQueueTestRun;
 /* C3-06: the second explicit request. It implies the pair demonstration, and
  * once that pair has retired it withdraws the two votes the pair left behind,
  * detaches those clients, then runs one more pair whose waiting half is
  * cancelled and whose client exits before it could ever be dispatched.
  * fCancelStep says which half has already happened, so a boot that loses the
  * queue to an isolation state never reports a demonstration it did not run. */
 bool fQueueCancel;
 unsigned fCancelStep;
 /* The identities the two demonstrations created, and the tokens the second
  * pair was handed, kept so the second half acts on exactly those requests
  * instead of guessing from the ledger. */
 bool fCancelLedger;
 unsigned fSelfClients[2], fCancelClients[3];
 uint64_t fCancelTokens[3], fCancelLower;
 unsigned fQueueRefused, fQuarantined, fQueueTestTries;
 IOReturn admit(SweetAppsRequest &);
 IOReturn validate(const SweetAppsRequest &);
 IOReturn vetoRail(unsigned,unsigned,const SweetRailVote &,SweetRailVote *) const;
 IOReturn launch(const SweetAppsRequest &);
 void retireFlight(void);
 void dispatchNext(void);
 void drainQueue(IOReturn,const char *);
 void markPending(const SweetAppsRequest &);
 void markFailure(const SweetAppsRequest &,IOReturn);
 bool queueSelfTest(void);
 uint64_t cn0Step(void) const;
 bool cancelSelfTest(void);
 bool cancelSelfTestCancel(void);
 bool cancelSelfTestFinish(void);
 IOReturn cancelWaiting(unsigned,uint64_t,const char *);
 unsigned cancelClientQueue(unsigned,const char *);
 IOReturn detachClient(unsigned,unsigned *);
 IOReturn withdrawRegisterVote(unsigned,uint64_t *,uint64_t *);
 IOReturn launchRail(const SweetAppsRequest &);
 IOReturn launchDisplayPower(const SweetAppsRequest &);
 IOReturn launchRegisterBandwidth(const SweetAppsRequest &);
 unsigned fSelfStores;
 uint32_t fIdentityId, fIdentitySolver, fIdentityConfig;
 SweetAppsDoor fTable;
 bool delegatedRequested(IOService *);
 bool ensureExecutor(void);
 bool adoptDelegatedIdentity(void);
 bool publishTable(void);
 const SweetAppsDoor *executorDoor(void);
 uint32_t tcsRead(uint32_t);
 void tcsWrite(uint32_t,uint32_t);
 static uint32_t tableRead(void *,uint32_t);
 static void tableWrite(void *,uint32_t,uint32_t);
 static bool tableIdentity(void *,uint32_t *,uint32_t *,uint32_t *);
 static IOReturn tableTriggerReady(void *);
 static void tableArm(void *);
 static void tableUnmask(void *);
 static void tableQuiet(void *,uint32_t);
 static void tableStop(void *);
 static bool tableIrq(void *);
 static bool tableInstall(void *,void *,bool (*)(void *));
 static unsigned tableStores(void *);
 static unsigned tableIrqs(void *);
 static bool completionThunk(void *);
 static IOReturn completionAction(OSObject *,void *,void *,void *,void *);
 void completionArrived(void);
 bool setupIRQ(IOService *);
 IOReturn prepareIRQ(void);
 IOReturn admitIRQ(vm_offset_t);
 bool handleIRQ(void);
 void quietIRQ(void);
 void stopIRQ(void);
 void waitCompletion(void);
 static void irqAction(OSObject *,IOInterruptEventSource *,int);
 SweetRailVote fProposal, fTarget;
 SweetPanelSupplyFence fPanelFence;
 bool panelFenceValid(const SweetPanelSupplyFence &);
 void clearPanelFence(void);
 /* Queued TCS words stay here: this service owns the storage and every
  * policy decision. SweetRPMhExecutor only moves the words and judges the
  * readback/completion they produce. */
 SweetRPMhWord fOperations[SweetRPMhExecutor::kCapacity];
 bool fExecutorReported;
 struct ArcResource { uint32_t address; unsigned count, levels[16]; };
 ArcResource fArc[SweetDisplaySupplyCount];
 bool fArcMetadataTried, fArcAvailable, fArcRequest;
 bool fArcOwned[SweetDisplaySupplyCount];
 unsigned fArcApplied[SweetDisplaySupplyCount];
 struct ArcStage { unsigned supply, selector; };
 ArcStage fArcStages[5]; /* Two inherited-parent guards + up to three targets. */
 unsigned fArcStage, fArcStageCount;
 SweetDisplayCorner fArcProposal, fArcAggregate, fArcNext;
 uint64_t fStageDeadline;
 SweetBCMInfo fBCM[SweetDisplayBCMCount];
 unsigned fBCMTried, fBCMAvailable;
 bool fRegisterRequest, fRegisterOwned;
 uint64_t fRegisterProposal, fRegisterTarget, fRegisterAggregate;
 static uint32_t read(vm_offset_t,uint32_t);
 static void write(vm_offset_t,uint32_t,uint32_t);
 /* MMIO ports handed to the executor; they never outlive one call. */
 static uint32_t portRead(void *,uint32_t);
 static void portWrite(void *,uint32_t,uint32_t);
 static uint64_t nowNS(void);
 uint16_t db16(uint32_t);
 bool range(uint32_t,uint32_t);
 bool lookup(unsigned);
 bool lookupArc(unsigned);
 bool lookupBCM(unsigned);
 IOReturn beginRegisterBandwidth(unsigned,uint64_t);
 bool loadDisplayPower(void);
 bool displayTargets(SweetDisplayCorner,unsigned *);
 IOReturn beginDisplayPower(unsigned,SweetDisplayCorner);
 IOReturn selectArcTCS(unsigned *);
 IOReturn queueArcStage(void);
 void completeTransaction(void);
 bool mapResources(IOService *);
 bool mapDatabase(IOService *);
 bool adoptIdentity(uint32_t,uint32_t,uint32_t);
 bool installDelegated(void);
 bool emptyTCS(unsigned);
 IOReturn begin(unsigned,unsigned,const SweetRailVote &);
 void addOperation(uint32_t,uint32_t,bool = false);
 void advance(void);
 void finish(IOReturn,bool);
 static IOReturn action(OSObject *,void *,void *,void *,void *);
 static void timerAction(OSObject *,IOTimerEventSource *);
 void shutdown(void);
public:
 bool start(IOService *) APPLE_KEXT_OVERRIDE;
 bool willTerminate(IOService *,IOOptionBits) APPLE_KEXT_OVERRIDE;
 void stop(IOService *) APPLE_KEXT_OVERRIDE;
 void free(void) APPLE_KEXT_OVERRIDE;
 IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE { return fLoop; }
 /* IDs never retain/dereference consumer objects. Client owns a provider ref.
  * Metadata/attach never writes hardware. No provider -> consumer callbacks. */
 IOReturn attachConsumer(uint64_t *);
 IOReturn detachConsumer(uint64_t);
 IOReturn resourceInfo(SweetRail,SweetRailInfo *);
 /* Caller must stop link/DMA before withdrawing its last vote. Known shared
  * rails reject a final aggregate disable until their users have a coordinated
  * ownership handoff; rejection leaves the old vote/status/token intact.
  * Completion is
  * ACTIVE ACK, not physical rail voltage/off. One in-flight transaction across
  * all clients; Busy is explicit, no overwrite, hidden retry or fallback. */
 IOReturn requestVote(uint64_t,SweetRail,const SweetRailVote &,uint64_t *);
 IOReturn copyStatus(uint64_t,uint64_t,SweetRPMhStatus *);
 /* VDDI/VPNL only: same aggregation/ACK engine, with live commanded-reset-low and
  * quiesced-touch checks at admission and before every queued write/trigger.
  * No-op votes also need a valid fence. This does not authorize link/DMA
  * shutdown or prove physical isolation; caller must establish those first.
  * Failure/cancel stops progression, retains partial TCS and last ACKed vote,
  * never undoes an already-triggered transaction. No implicit retry/cleanup. */
 IOReturn requestPanelVote(uint64_t,SweetRail,const SweetRailVote &,
     const SweetPanelSupplyFence &,uint64_t *);
 IOReturn cancelPanelVote(uint64_t,uint64_t);
 /* Same apps-DRV owner, gate and timer as LDO votes. Metadata is lazy/read-only;
  * missing ARC resources do not break the existing four-rail/default binding.
  * Mode 2 permits requests but never automatically acquires a display vote.
  * Caller must lower dependent clock rates before lowering the corner; last
  * withdrawal requires stopping DMA/link and collapsing dependent domains.
  * NoC votes and cold clock/GDSC readiness are separate. */
 IOReturn displayPowerInfo(SweetDisplayPowerInfo *);
 IOReturn requestDisplayPower(uint64_t,SweetDisplayCorner,uint64_t *);
 IOReturn copyDisplayPowerStatus(uint64_t,uint64_t,SweetDisplayPowerStatus *);
 IOReturn copyBCMInfo(SweetDisplayBCM,SweetBCMInfo *);
 /* CN0 config path uses apps RSC; pixel data must use display RSC instead.
  * Same client/gate/completion engine. Bytes/sec, no raw BCM bitfields. */
 IOReturn requestRegisterBandwidth(uint64_t,uint64_t,uint64_t *);
 IOReturn copyRegisterBandwidthStatus(uint64_t,uint64_t,SweetRegisterBandwidthStatus *);
 /* C3-06: withdraw one request that is still waiting, by the token the caller
  * already holds. A request that has been triggered is not cancellable here and
  * is refused instead: its storage and its TCS slot belong to the provider,
  * which finalizes or isolates it. Cancelling never sends a compensating vote,
  * never touches another client's ledger, and never frees in-flight storage.
  * Attached clients are also detached only after their waiting requests have
  * been withdrawn this way, so no record can be dispatched against a slot that
  * the exit has already released. */
 IOReturn cancelRequest(uint64_t,uint64_t);
 /* Drop this client's own CN0 vote from the ledger. Refused when this client is
  * what holds the acknowledged aggregate up, because lowering the array from
  * there is a new transaction and therefore the caller's explicit vote - not a
  * side effect of a withdrawal. Answered with the previous and resulting
  * aggregate so a caller can report what the hardware is still being told. */
 IOReturn withdrawRegisterBandwidth(uint64_t,uint64_t *,uint64_t *);
};

/* Matching only retain/withdraw + queues owner event. stop outside owner gate.
 * Actual attach/metadata/votes run on the consumer workloop. */
class SweetRPMhClient : public OSObject {
 OSDeclareDefaultStructors(SweetRPMhClient);
 IOLock *fLock;
 IOWorkLoop *fLoop;
 IOInterruptEventSource *fEvent;
 IONotifier *fMatched, *fTerminated;
 SweetRPMh *fBackend;
 OSObject *fOwner;
 void (*fChanged)(OSObject *);
 bool fStopping, fEventAdded;
 static bool matched(void *,void *,IOService *,IONotifier *);
 static bool terminated(void *,void *,IOService *,IONotifier *);
 static void changed(OSObject *,IOInterruptEventSource *,int);
public:
 static SweetRPMhClient *withOwner(OSObject *,IOWorkLoop *,void (*)(OSObject *));
 bool watch(void);
 SweetRPMh *copyBackend(void);
 void stop(void);
 void free(void) APPLE_KEXT_OVERRIDE;
};
#endif
#endif
