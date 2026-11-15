/* Per-consumer IOKit subscription, not a global provider rendezvous.
 * Match/terminated callbacks only retain/mark/queue. All route and owner work
 * runs on the consumer's loop. Call stop OUTSIDE that loop before owner free. */
#ifndef SWEET_SPMI_MUX_CLIENT_H
#define SWEET_SPMI_MUX_CLIENT_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include "sweet_spmi_mux.h"
#include <IOKit/IOLocks.h>
#include <IOKit/IONotifier.h>
/* Optional owner preparation. Runs on the consumer loop after a live provider
 * match, before attach/READY; never in registry notification or mux context.
 * Owner retains partial-resource cleanup responsibility until stop returns. */
typedef bool (*SweetMuxPrepare)(void *context);
class SweetSpmiMuxClient : public OSObject {
	OSDeclareDefaultStructors(SweetSpmiMuxClient);
public:
	static SweetSpmiMuxClient *withOwner(IOService *owner, IOWorkLoop *loop,
	    unsigned group, unsigned events, SweetMuxNotify notify, void *context,
	    SweetMuxPrepare prepare = NULL);
	void detach(void); /* consumer-loop action: terminal, no automatic rebind */
	/* Owner-loop read-only fence, including events queued by the mux but not
	 * yet delivered on this consumer's loop. No provider gate or SPMI access. */
	bool hasPendingEvents(unsigned mask);
 /* Owner loop only. Diagnostic observation, never substitute event delivery. */
 bool copyInterruptTrace(unsigned token, SweetMuxIrqTrace *out);
	void stop(void);   /* remove notifiers synchronously, then detach/drain */
	void free(void) APPLE_KEXT_OVERRIDE;
private:
	IOService *owner = NULL; /* owner outlives stop; no reference cycle */
	IOWorkLoop *loop = NULL;
	IOLock *lock = NULL;
	IOInterruptEventSource *event = NULL;
	IONotifier *matched = NULL, *terminated = NULL;
	SweetSpmiInterruptMux *provider = NULL; /* retained until gated detach */
	SweetMuxNotify notify = NULL;
	SweetMuxPrepare prepare = NULL;
	void *context = NULL;
	unsigned group = 0, events = 0;
	volatile unsigned pending = 0;
	bool stopping = false, terminal = false, providerGone = false;
	bool attached = false, eventAdded = false;
	static bool serviceMatched(void *, void *, IOService *, IONotifier *);
	static bool serviceTerminated(void *, void *, IOService *, IONotifier *);
	static void routeNotify(void *, unsigned);
	static void eventAction(OSObject *, IOInterruptEventSource *, int);
	static IOReturn detachAction(OSObject *, void *, void *, void *, void *);
	void closeBinding(void);
	void process(void);
};
#endif
#endif
