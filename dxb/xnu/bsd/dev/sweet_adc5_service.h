/* Typed ADC provider interface. Discover/retain SweetPmicAdc5 through IOKit.
 * requestSample is asynchronous; copySample returns only a completed restored
 * measurement. Clients can register general interest for property-change
 * messages, then queue their own workloop and call copySample. No callbacks
 * enter a client gate from this provider's gate. Caller owns its service retain.
 * No charging limits/identity are implied by a voltage measurement. */
#ifndef SWEET_ADC5_SERVICE_H
#define SWEET_ADC5_SERVICE_H
#if defined(__cplusplus) && defined(ARM64_BOARD_CONFIG_MH2LM)
#include "sweet_adc5.h"
#include "sweet_spmi_mux_client.h"
#include <IOKit/IOTimerEventSource.h>
class SweetPmicAdc5 : public IOService {
 OSDeclareDefaultStructors(SweetPmicAdc5);
public:
 bool start(IOService *) APPLE_KEXT_OVERRIDE;
 bool willTerminate(IOService *, IOOptionBits) APPLE_KEXT_OVERRIDE;
 void stop(IOService *) APPLE_KEXT_OVERRIDE;
 void free(void) APPLE_KEXT_OVERRIDE;
 IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE { return loop; }
 /* A successful request assigns token; copySample.sequence must match it.
  * Failed/Busy requests leave token unchanged. Completion/READY wake interest. */
 virtual IOReturn requestSample(enum sweet_adc5_channel channel, unsigned *token = NULL);
 virtual IOReturn status(void); /* Success ready, NotReady pending/stop, IOError failed */
 virtual IOReturn copySample(enum sweet_adc5_channel channel,
     struct sweet_adc5_measurement *out);
private:
 IOWorkLoop *loop = NULL;
 IOTimerEventSource *timer = NULL;
 SweetSpmiMuxClient *connection = NULL;
 struct sweet_adc5_session session = {};
 struct sweet_adc5_measurement samples[2] = {};
 bool valid[2] = {}, timerAdded = false, ready = false, failed = false;
 volatile unsigned stopping = 0;
 unsigned diagnostics = 0, sequence = 0, strayEoc = 0;
 unsigned traceBaselineHits = 0;
 uint64_t lastCallbackNs = 0, lastTimerNs = 0;
 void captureFailure(int);
 static IOReturn subscribeAction(OSObject *, void *, void *, void *, void *);
 static IOReturn statusAction(OSObject *, void *, void *, void *, void *);
 static IOReturn sampleAction(OSObject *, void *, void *, void *, void *);
 static IOReturn copyAction(OSObject *, void *, void *, void *, void *);
 static IOReturn stopAction(OSObject *, void *, void *, void *, void *);
 static void muxEvent(void *, unsigned);
 static void timerFired(OSObject *, IOTimerEventSource *);
 void armDeadline(void);
 void complete(int, const struct sweet_adc5_measurement *);
 void shutdown(void);
};
#endif
#endif
