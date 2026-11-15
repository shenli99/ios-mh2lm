/* MH2LM mux service interface. All methods are thread-context only.
 * Callbacks run on the mux workloop and MUST only queue work, never wait
 * for the consumer workloop. detach synchronizes with all callbacks. */
#ifndef SWEET_SPMI_MUX_H
#define SWEET_SPMI_MUX_H
#ifdef __cplusplus
extern "C" {
#endif
/* Board publishes the late nub; IOKit matches/starts the IOService.
 * Synchronous matching reports board startup success through the nub property.
 * Consumers own only their route gates, never the parent lifecycle. */
int sweet_spmi_mux_start(void);
#define SWEET_MUX_KEYS 0u
#define SWEET_KEYS_IRQ_ALL 0x07u
#define SWEET_MUX_BATTERY 1u
#define SWEET_BATT_IRQ_USB   0x01u
#define SWEET_BATT_IRQ_CHG   0x02u
#define SWEET_BATT_IRQ_MSOC  0x04u
#define SWEET_BATT_IRQ_READY 0x08u
#define SWEET_BATT_IRQ_SOURCE 0x10u
#define SWEET_BATT_IRQ_FAULT  0x20u
#define SWEET_BATT_IRQ_TYPEC  0x40u
#define SWEET_BATT_IRQ_INPUT_FAULT 0x80u
#define SWEET_BATT_IRQ_WDOG   0x100u
#define SWEET_BATT_IRQ_TEMP   0x200u
#define SWEET_BATT_IRQ_ALL    0x3ffu
#define SWEET_MUX_ADC5 2u
#define SWEET_ADC5_IRQ_EOC 0x100u
#define SWEET_MUX_READY     0x40000000u
#define SWEET_MUX_FAILED    0x80000000u
typedef void (*SweetMuxNotify)(void *context, unsigned events);
/* Consumers discover and retain the IOService through matching notifications.
 * Only board publication and the GIC dispatch entry remain C interfaces. */
#ifdef __cplusplus
}
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOService.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOWorkLoop.h>
#define SWEET_MUX_ROUTE_COUNT 20u
enum { MUX_KEYS = SWEET_MUX_KEYS, MUX_BATTERY = SWEET_MUX_BATTERY, MUX_ADC5 = SWEET_MUX_ADC5, MUX_GROUPS = 3 };
struct MuxRoute {
	unsigned sid, per, bit, token, group;
	/* Bit0 rising, bit1 falling. Configuration is route data, not a switch. */
	unsigned edges;
	const char *name;
	uint16_t apid;
	vm_offset_t channel;
	bool active, touched, writePoisoned;
	uint8_t oldType, oldHigh, oldLow;
	uint32_t oldAccess;
	unsigned hits;
};
/* Last completed ACK/notify observation per table route. Timestamp is the
 * parent GIC entry for the whole drain, NOT this peripheral's hardware EOC.
 * Atomic publication permits a bounded, read-only copy without the mux gate. */
struct SweetMuxIrqTrace {
 unsigned sequence, hits, irq, work, status, apid;
 uint64_t gicNs, scanNs, ackNs, notifyNs, ackWaitNs, ackHoldNs;
};
struct MuxBinding {
	IOService *consumer;
	SweetMuxNotify notify;
	void *context;
	unsigned events;
};

struct BindingRequest {
	unsigned group, events;
	IOService *consumer;
	SweetMuxNotify notify;
	void *context;
};

class SweetSpmiInterruptMux : public IOService {
	OSDeclareDefaultStructors(SweetSpmiInterruptMux);
public:
	bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	bool willTerminate(IOService *provider, IOOptionBits options) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
	IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE { return workLoop; }
	bool irq(unsigned int intid);
	/* Caller retains this service and owns consumer/context through detach.
	 * detach synchronizes all callbacks. Both methods require thread context. */
	virtual IOReturn attachConsumer(IOService *consumer, unsigned group, unsigned events, SweetMuxNotify notify, void *context);
	virtual void detachConsumer(IOService *consumer, unsigned group, void *context);
 bool copyInterruptTrace(unsigned group, unsigned token, SweetMuxIrqTrace *out) const;
private:
	MuxBinding bindings[MUX_GROUPS] = {};
 SweetMuxIrqTrace traces[SWEET_MUX_ROUTE_COUNT] = {};
 /* Bounded workloop-owned history avoids IRQ-time allocation and large stack
  * frames. Emit logs only after the entire drain has rearmed or failed. */
 SweetMuxIrqTrace drainTraces[4][SWEET_MUX_ROUTE_COUNT] = {};
 unsigned drainMasks[4] = {};
 uint64_t lastGicNs = 0;
	MuxRoute routes[SWEET_MUX_ROUTE_COUNT] = {};
	vm_offset_t gicd = 0, pdc = 0, core = 0, cfg = 0, obs = 0;
	IOWorkLoop *workLoop = NULL;
	IOInterruptEventSource *eventSource = NULL;
	volatile unsigned state = 0; /* 0 off, 1 ready/idle, 2 pending, 3 failed, 4 probe, 5 maintenance */
	volatile unsigned probeHits = 0;
	unsigned irqCount = 0, workCount = 0, routeCount = 0, faultCount = 0;
	uint64_t stormStartNs = 0;
	unsigned stormBase = 0, apidCount = 0;
	unsigned isolatedGroups = 0; /* gate-owned; no reattach until reboot */
	bool allowEmptyDrain = false;
	bool eventAdded = false, stopping = false, quiesced = false;
	bool claimed = false, superStarted = false, hardwareTouched = false;
	int initializeHardware(void);
	void shutdown(void);
	void disconnectIRQ(void);
	void releaseResources(void);
	bool waitGic(void);
	uint16_t findOwnedApid(unsigned sid, unsigned per, unsigned count);
	bool readPeripheral(const MuxRoute &route, unsigned reg, uint8_t *out);
	bool writePeripheral(MuxRoute &route, unsigned reg, uint16_t data, unsigned bytes,
    uint64_t *waitNs = NULL, uint64_t *holdNs = NULL);
	bool ackRoute(MuxRoute &route, uint64_t *waitNs = NULL, uint64_t *holdNs = NULL);
	void publishBatch(SweetMuxIrqTrace *batch, unsigned acknowledged);
    void logBatch(const SweetMuxIrqTrace *batch, unsigned acknowledged);
    void drainPending(void);
	uint32_t allowedMaskForApid(uint16_t apid);
	void disableHardware(void);
	bool configureGic(void);
	bool probeGicDelivery(void);
	bool configureRoute(MuxRoute &route);
	void drainInterrupt(void);
	bool beginRouteUpdate(void);
	bool endRouteUpdate(void);
	bool removeGroup(unsigned group);
	bool isolateGroup(unsigned group);
	bool quarantinePendingGroups(unsigned groups);
	IOReturn updateBinding(BindingRequest *request, bool detach);
	static void workAction(OSObject *owner, IOInterruptEventSource *source, int count);
	static IOReturn bindingAction(OSObject *owner, void *, void *, void *, void *);
	static IOReturn stopAction(OSObject *owner, void *, void *, void *, void *);
};
#endif /* ARM64_BOARD_CONFIG_MH2LM */
#endif /* __cplusplus */
#endif /* SWEET_SPMI_MUX_H */
