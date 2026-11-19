/* Native service facade for the existing sole TouchComm worker. No MMIO: it
 * owns the GIC registration for the node (register/enable/mask through the
 * provider's IOService::*Interrupt path) and everything else stays in the
 * worker. */
#ifndef SWEET_TOUCH_PANEL_SERVICE_H
#define SWEET_TOUCH_PANEL_SERVICE_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include "../../../bsd/dev/sweet_touch_panel.h"
struct SweetTouchPanelStatus {
 uint64_t token;
 unsigned state;
 IOReturn result;
 bool quiesced, ready, uncertain;
};
/*
 * sweettouchpanel: 1 = binding only, 2 = native worker, 3 = 2 plus the
 * delegated TLMM pad interrupt. Every consumer of the same boot argument must
 * accept 3 too; when MDSS did not, the lifecycle refused to start.
 */
static inline bool sweetTouchPanelNative(int mode)
{ return mode==2 || mode==3; }

class AGPIO;
class SweetTouchPanel : public IOService {
 OSDeclareDefaultStructors(SweetTouchPanel);
 IOWorkLoop *fLoop;
 IOCommandGate *fGate;
 IOTimerEventSource *fTimer;
 bool fGateAdded, fTimerAdded, fAccepting, fBusy, fTouched, fPoisoned;
 IOService *fIRQProvider;
 /* Pad path: the exclusive handle that owns gpio122, including its
  * interrupt enable bit and status latch. NULL on the nub path. */
 AGPIO *fIRQPin;
 bool fIRQRegistered;
 /* INTID 240 is the TLMM summary line. When the controller owns it the
  * interrupt is requested per pad and no GIC source touches this nub. */
 bool fIRQViaTLMM;
 unsigned fMode;
 uint64_t fSession, fClient, fNextClient, fDeadline;
 SweetTouchPanelStatus fStatus;
 enum Operation { OpAttach, OpDetach, OpQuery, OpQuiesce, OpResume, OpStop, OpInfo, OpCancel };
 static IOReturn action(OSObject *,void *,void *,void *,void *);
 static void timerAction(OSObject *,IOTimerEventSource *);
 static uint64_t nowNS(void);
 void refresh(void);
 void shutdown(void);
 IOReturn request(unsigned,uint64_t,uint64_t *);
public:
 bool start(IOService *) APPLE_KEXT_OVERRIDE;
 bool willTerminate(IOService *,IOOptionBits) APPLE_KEXT_OVERRIDE;
 void stop(IOService *) APPLE_KEXT_OVERRIDE;
 void free(void) APPLE_KEXT_OVERRIDE;
 IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE { return fLoop; }
 /* GIC line ownership; called by the worker's C hooks. */
 void irqMask(void);
 void irqUnmask(void);
 /* Pad path only: the handle owns this pad's status latch. */
 void irqAck(void);
 IOReturn attachConsumer(uint64_t *);
 IOReturn detachConsumer(uint64_t);
 IOReturn copyWorkerAvailable(bool *);
 IOReturn copyWorkerFailed(bool *);
 IOReturn copyStatus(uint64_t,uint64_t,SweetTouchPanelStatus *);
 /* Asynchronous worker operations, no MMIO in caller/provider. quiesced means
  * worker boundary + peripheral/GIC masked/active drained + HID contacts up.
  * It is not a touch sleep/rail-off proof. Resume requires caller-established
  * panel/rail readiness; re-identifies application and readbacks the format,
  * then rearms IRQ. No touch GPIO reset, supply or firmware write.
  * Stop cancels through the mailbox: an in-flight worker may finish bus STOP
  * and mask asynchronously, but stores no provider pointer and cannot call us.
  * Any submitted request retires this worker session on service close. */
 IOReturn quiesce(uint64_t,uint64_t *);
 IOReturn resume(uint64_t,uint64_t *);
 /* Consumer-scoped cancellation retires the exclusive mailbox session. It
  * does not stop this IOService or claim the worker already drained. */
 IOReturn cancelRequest(uint64_t,uint64_t);
};
class SweetTouchPanelClient : public OSObject {
 OSDeclareDefaultStructors(SweetTouchPanelClient);
 IOLock *fLock;
 IOWorkLoop *fLoop;
 IOInterruptEventSource *fEvent;
 IONotifier *fMatched, *fTerminated;
 SweetTouchPanel *fBackend;
 OSObject *fOwner;
 void (*fChanged)(OSObject *);
 bool fStopping, fEventAdded;
 static bool matched(void *,void *,IOService *,IONotifier *);
 static bool terminated(void *,void *,IOService *,IONotifier *);
 static void changed(OSObject *,IOInterruptEventSource *,int);
public:
 static SweetTouchPanelClient *withOwner(OSObject *,IOWorkLoop *,void (*)(OSObject *));
 bool watch(void);
 SweetTouchPanel *copyBackend(void);
 void stop(void);
 void free(void) APPLE_KEXT_OVERRIDE;
};
#endif
#endif
