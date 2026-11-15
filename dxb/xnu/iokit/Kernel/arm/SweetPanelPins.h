/* Restricted SM8150 panel GPIO6/reset and GPIO8/TE provider. No raw GPIO API. */
#ifndef SWEET_PANEL_PINS_H
#define SWEET_PANEL_PINS_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>

enum SweetPanelPinState { SweetPinsIdle, SweetPinsPending, SweetPinsActive,
 SweetPinsResetComplete, SweetPinsParked, SweetPinsFailed, SweetPinsStopped };
struct SweetPanelPinStatus {
 uint64_t token;
 unsigned state, phase;
 IOReturn result;
 /* resetKnown/resetHigh describe the commanded sequence, not measured voltage. */
 bool uncertain, resetKnown, resetHigh;
};
class SweetPanelPins : public IOService {
 OSDeclareDefaultStructors(SweetPanelPins);
 IOWorkLoop *fLoop;
 IOCommandGate *fGate;
 IOTimerEventSource *fTimer;
 IOMemoryMap *fMaps[2];
 vm_offset_t fBase[2];
 bool fGateAdded, fTimerAdded, fAccepting, fClaimed, fTouched, fPoisoned, fBusy;
 bool fConfigured, fParked;
 unsigned fMode, fOperation, fPhase, fDiagnosticCount;
 void observe(unsigned,uint32_t,uint32_t,uint32_t,uint32_t,bool = false);
 uint64_t fClient, fNextClient, fNextToken, fDeadline, fWait;
 uint32_t fControl[2], fInterrupt[2], fOutput;
 SweetPanelPinStatus fStatus;
 enum Operation { OpAttach, OpDetach, OpQuery, OpActivate, OpReset, OpPark, OpCancel, OpStop, OpQueryLow };
 enum Phase { Configure, HighConfirm, HighHold, LowConfirm, LowHold,
  FinalHighConfirm, FinalHighHold, Settle, ParkConfirm, ParkHold };
 static uint32_t read(vm_offset_t,uint32_t);
 static void write(vm_offset_t,uint32_t,uint32_t);
 static uint64_t nowNS(void);
 static IOReturn action(OSObject *,void *,void *,void *,void *);
 static void timerAction(OSObject *,IOTimerEventSource *);
 bool sample(bool);
 bool writeControl(unsigned,uint32_t);
 bool writeReset(bool);
 void observeLevel(bool);
 void advance(void);
 void finish(IOReturn,unsigned,const char *);
 void shutdown(void);
 IOReturn request(unsigned,uint64_t,uint64_t *);
public:
 bool start(IOService *) APPLE_KEXT_OVERRIDE;
 bool willTerminate(IOService *,IOOptionBits) APPLE_KEXT_OVERRIDE;
 void stop(IOService *) APPLE_KEXT_OVERRIDE;
 void free(void) APPLE_KEXT_OVERRIDE;
 IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE { return fLoop; }
 /* Single exclusive consumer. No retain of consumer object; caller retains us.
  * Attach/query/start/matching never reconfigure a pin. IDs/tokens do not wrap. */
 IOReturn attachConsumer(uint64_t *);
 IOReturn detachConsumer(uint64_t);
 IOReturn copyStatus(uint64_t,uint64_t,SweetPanelPinStatus *);
 /* Live owner/commanded-low fence under this provider's gate. Successful
  * same-client Parked or Active-low token required. CTL/IRQ/latch/input raw
  * values are diagnostic; eGPIO/IRQ ownership and provider loss still reject.
  * This does not prove physical reset voltage or physical rail-off. */
 IOReturn copyLowStatus(uint64_t,uint64_t,SweetPanelPinStatus *);
 /* Requests only queue work on this service's loop. Mode2 explicitly permits
  * writes. First warm activate records the inherited level; a parked lease
  * reactivates with reset held low. No takeover of unknown cold state.
  * Caller must authorize panel identity, own rails and quiesce DMA/IRQ/touch
  * before activate/reset/park. This provider cannot prove those dependencies.
  * reset: high10ms -> low2ms -> high10ms -> settle5ms. Latch/input readback is diagnostic, with no additional follow deadline; no rail/DSI/consumer callbacks.
  * park: suspend pull/drive, command reset low, >=5ms; TE stays mdp_vsync.
  * Detach cannot silently release an active/uncertain pin. No automatic reset,
  * park, rollback, retry or pin transition on termination. */
 IOReturn activate(uint64_t,uint64_t *);
 IOReturn reset(uint64_t,uint64_t *);
 IOReturn park(uint64_t,uint64_t *);
 /* Cancel only this consumer/token. Stop its timer, poison the lease and
  * retain the exact partial pin state; never park/reset as cleanup. */
 IOReturn cancelRequest(uint64_t,uint64_t);
};

class SweetPanelPinsClient : public OSObject {
 OSDeclareDefaultStructors(SweetPanelPinsClient);
 IOLock *fLock;
 IOWorkLoop *fLoop;
 IOInterruptEventSource *fEvent;
 IONotifier *fMatched, *fTerminated;
 SweetPanelPins *fBackend;
 OSObject *fOwner;
 void (*fChanged)(OSObject *);
 bool fStopping, fEventAdded;
 static bool matched(void *,void *,IOService *,IONotifier *);
 static bool terminated(void *,void *,IOService *,IONotifier *);
 static void changed(OSObject *,IOInterruptEventSource *,int);
public:
 static SweetPanelPinsClient *withOwner(OSObject *,IOWorkLoop *,void (*)(OSObject *));
 bool watch(void);
 SweetPanelPins *copyBackend(void);
 void stop(void); /* outside consumer gate, synchronizes callbacks/events */
 void free(void) APPLE_KEXT_OVERRIDE;
};
#endif
#endif
