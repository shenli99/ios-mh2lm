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
 bool setupIRQ(IOService *);
 IOReturn prepareIRQ(void);
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
