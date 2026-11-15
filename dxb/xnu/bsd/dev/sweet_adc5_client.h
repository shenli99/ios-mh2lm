/* Per-consumer ADC service discovery. Owner calls update/copyFresh on its own
 * workloop. Registry/interest callbacks only mark/queue, never enter a gate.
 * stop must run outside that gate and precede owner/event-source destruction. */
#ifndef SWEET_ADC5_CLIENT_H
#define SWEET_ADC5_CLIENT_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "sweet_adc5_service.h"
typedef void (*SweetAdcNotify)(void *);
class SweetAdc5Client : public OSObject {
 OSDeclareDefaultStructors(SweetAdc5Client);
public:
 static SweetAdc5Client *withOwner(IOWorkLoop *, SweetAdcNotify, void *);
 void update(bool inputPresent, uint64_t inputEpoch, bool batteryChanged, uint64_t now);
 bool copyFresh(enum sweet_adc5_channel, uint64_t now, struct sweet_adc5_measurement *);
 /* USBIN must belong to this exact battery input acquisition, not merely be
  * a recently completed conversion from before a source/protection event. */
 bool copyFreshInput(uint64_t inputEpoch, uint64_t now, struct sweet_adc5_measurement *);
 void invalidate(void); /* own loop, terminal; keeps notifiers until stop */
 void stop(void); /* outside gate, synchronizes notifiers and queued work */
 void free(void) APPLE_KEXT_OVERRIDE;
private:
 IOWorkLoop *loop = NULL;
 IOInterruptEventSource *event = NULL;
 IOLock *lock = NULL;
 SweetPmicAdc5 *provider = NULL;
 IONotifier *matched = NULL, *terminated = NULL, *interest = NULL;
 SweetAdcNotify notify = NULL;
 void *context = NULL;
 bool stopping = false, terminal = false, gone = false, eventAdded = false;
 bool present = false, blocked = false, providerReady = false, valid[2] = {};
 unsigned needed = 1, active = 0, token = 0, epoch[2] = {}, activeEpoch = 0;
 uint64_t inputEpoch = 0, activeInputEpoch = 0, sampleInputEpoch = 0;
 struct sweet_adc5_measurement samples[2] = {};
 static bool serviceMatched(void *, void *, IOService *, IONotifier *);
 static bool serviceTerminated(void *, void *, IOService *, IONotifier *);
 static IOReturn serviceMessage(void *, void *, UInt32, IOService *, void *, vm_size_t);
 static void eventAction(OSObject *, IOInterruptEventSource *, int);
 static IOReturn stopAction(OSObject *, void *, void *, void *, void *);
 bool live(void);
 void process(void);
};
#endif
#endif
