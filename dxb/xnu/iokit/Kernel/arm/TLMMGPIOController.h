/*
 * SM8150 TLMM GPIO controller: exclusive pin handles (AGPIO) plus a plain
 * mode for callers that just want to poke a pad.
 *
 * The controller matches the /arm-io/tlmm IOPlatformDevice node through
 * IONameMatch 'qcom,tlmm-sm8150' and maps the four tile windows that the
 * node's reg property describes (west, east, north, south). It owns the only
 * copy of the tile bases, so every other driver goes through one of two
 * access paths:
 *
 *   AGPIO — acquirePin() hands out an OSObject that cached the pad's CFG,
 *   IO and INTR_CFG addresses when it was created. Its methods write those
 *   pointers directly: no tile lookup, no lock, no controller call.
 *
 * The rule between the two paths is exclusive write, shared read:
 *
 *   - A pin with a live AGPIO cannot be acquired again, and every plain-mode
 *     writer refuses it (kIOReturnBusy). Neither another driver nor the
 *     plain hardware path can take control of the pad while the handle lives.
 *   - Reading is always allowed: getPinValue()/getPinConfig() keep working on
 *     an owned pin, which is how a monitor cross-checks an owner's writes.
 *     Reads have no side effects, so sharing them costs nothing.
 *
 *   Plain mode — setPinValue()/getPinValue()/setPinDirection()/
 *   setPinConfig()/... take a pin number and resolve the tile. The C API in
 *   <pexpert/pexpert/arm64/tlmm_gpio.h> is the C-visible form of this path.
 *
 * Register layout and field masks live in that pexpert header; this one only
 * declares the objects. MMIO ordering is dmb oshst before a store, dsb osh
 * after it, dmb oshld after a load, the same pairing the display and panel
 * drivers use.
 */

#ifndef _IOKIT_ARM_TLMM_GPIO_CONTROLLER_H
#define _IOKIT_ARM_TLMM_GPIO_CONTROLLER_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <pexpert/arm64/tlmm_gpio.h>

class TLMMGPIOController;

/*
 * One exclusive pad. Only TLMMGPIOController::acquirePin() can initialise an
 * instance: the default constructor exists because OSMetaClass requires one,
 * and it leaves the handle invalid, so every method fails with
 * kIOReturnNotOpen until acquirePin() has bound it to a pin.
 */
class AGPIO : public OSObject {
	OSDeclareDefaultStructors(AGPIO);
	friend class TLMMGPIOController;

public:
	void free(void) APPLE_KEXT_OVERRIDE;

	/* Drive the output latch. Valid regardless of the pin direction, so a
	 * later setDirection(true) releases the pad at a known level. */
	IOReturn setValue(bool high);
	bool getValue(void) const;

	/* Enable (output true) or disable the pad driver. */
	IOReturn setDirection(bool output);

	/* Single-field updates; every value is range checked. */
	IOReturn setMux(unsigned int func);
	IOReturn setPull(unsigned int pull);
	IOReturn setDrive(unsigned int drive);

	/* One read-modify-write of the whole CFG field set. The output level is
	 * latched before the pin becomes an output. */
	IOReturn configure(unsigned int func, unsigned int pull, unsigned int drive,
	    bool output, bool high);

	/* Read back the pad as this owner sees it: decoded fields plus the raw
	 * CFG/IO/INTR_CFG registers. This is the read-back an exclusive owner
	 * uses to verify its own writes; the plain-mode getter cannot be used
	 * because it refuses the pins this handle owns. */
	IOReturn getConfig(struct tlmm_pin_config *config);

	/*
	 * Pad interrupt. Only the handle that owns the pad may claim one: the
	 * interrupt lives in that pad's INTR_CFG/INTR_STATUS and its enable bit,
	 * while the controller owns the summary SPI every pad shares. There is
	 * no pin-number entry point any more, so "an interrupt needs the pad"
	 * is a type-level rule rather than a convention.
	 */
	IOReturn setInterrupt(unsigned int detect, tlmm_gpio_irq_handler handler,
	    void *refcon);
	IOReturn disableInterrupt(void);
	/* Thread context, while this client is masked: program the registered
	 * trigger and clear its stale latch, but do not enable the pad or line.
	 * Keeping preparation separate preserves the client's arm-time ordering. */
	IOReturn prepareInterrupt(void);
	/* Diagnostic only, thread context: the controller may inject/clear a
	 * summary pending bit only while this is its sole registered pad.
	 * true reserves that probe; false clears and ends the reservation. */
	IOReturn setProbePending(bool pending);

	/* Interrupt-context safe. mask clears this pad's enable bit, so the
	 * shared summary deasserts without touching any other pad; resume acks
	 * the stale status and re-arms; ack clears the pad status latch. */
	IOReturn maskInterrupt(void);
	IOReturn resumeInterrupt(void);
	IOReturn ackInterrupt(void);
	bool interruptEnabled(void) const;

	/*
	 * Return the pin to the controller. Idempotent: a released handle keeps
	 * failing every operation and frees itself normally.
	 *
	 * Careful: this deliberately hides OSObject::release() (different
	 * cv-qualifier, so it is not an override). A caller that also wants to
	 * drop its reference must use put() rather than release() plus a
	 * refcount release.
	 */
	void release(void);

	/* Thread context only. Mask/unregister and drain dispatch snapshots before
	 * returning the pad. IRQ-context put is refused and retains its reference.
	 * The owner must serialize teardown against its non-callback methods. */
	void put(void);

	unsigned int getPin(void) const { return fPin; }
	const char *getOwner(void) const { return fOwner; }
	bool isValid(void) const { return fValid; }

private:
	/*
	 * Private binder: acquirePin() is a friend. It also refuses to bind an
	 * object twice, so a handle can never end up owning two pads.
	 */
	bool initWithPin(TLMMGPIOController *controller, unsigned int pin,
	    vm_offset_t cfg, vm_offset_t io, vm_offset_t intr,
	    const char *owner);

	/* Controller teardown: the MMIO windows are going away, so the handle
	 * must stop writing them instead of faulting on a stale address. */
	void invalidate(void);

	uint32_t readCfg(void) const;
	void writeCfg(uint32_t value);
	uint32_t readIo(void) const;
	void writeIo(uint32_t value);
	uint32_t readIntrCfg(void) const;

	TLMMGPIOController *fController;
	const char *fOwner;
	volatile uint32_t *fCfg;
	volatile uint32_t *fIo;
	volatile uint32_t *fIntr;
	unsigned int fPin;
	bool fValid;
};

/* Registered pad interrupts: one GIC line (INTID 240) serves every pad, and
 * a boot needs a handful at most. A fixed table keeps dispatch allocation
 * free and lets the interrupt path run under a simple lock. */
#define TLMM_IRQ_MAX 8u

/*
 * The pin table entry. 'handle' is set only while an AGPIO owns the pad;
 * 'used' records that the kernel has driven or configured the pad at all,
 * which is what a bring-up dump wants to see.
 */
struct TLMMPinState {
	AGPIO *handle;
	const char *owner;
	unsigned int pin;
	bool used;
	bool exclusive;
	/* Declared island-owned by the ADT. The live EGPIO enable bit is checked
	 * separately, because firmware can set it after we start. */
	bool readOnly;
};

class TLMMGPIOController : public IOService {
	OSDeclareDefaultStructors(TLMMGPIOController);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;

	/* Exclusive access. Returns NULL for a bad pin, an empty owner string,
	 * or a pad that another handle already owns. */
	AGPIO *acquirePin(unsigned int pin, const char *owner);

	/* Verify the owner and drop the handle. The owner string must be the
	 * same text that acquirePin() received (a static literal, by
	 * convention), because AGPIO keeps the pointer rather than copying it. */
	IOReturn releasePin(unsigned int pin, const char *owner);

	/* Plain mode. Writers return kIOReturnBusy when an AGPIO holds the pad;
	 * the two readers below always work, because an owned pad stays readable
	 * for diagnostics and cross-checks. */
	IOReturn setPinValue(unsigned int pin, bool high);
	IOReturn getPinValue(unsigned int pin, bool *high);
	IOReturn setPinDirection(unsigned int pin, bool output);
	IOReturn setPinMux(unsigned int pin, unsigned int func);
	IOReturn setPinPull(unsigned int pin, unsigned int pull);
	IOReturn setPinDrive(unsigned int pin, unsigned int drive);
	IOReturn setPinConfig(unsigned int pin, unsigned int func,
	    unsigned int pull, unsigned int drive, bool output, bool high);
	IOReturn getPinConfig(unsigned int pin, struct tlmm_pin_config *config);

	unsigned int getPinCount(void) const { return fPinCount; }

	/* True when the island drives the pad: declared in the node's
	 * sweet,read-only-pins list, or EGPIO enable is set in its CFG right now.
	 * Such a pad can be read but never handed out or written. */
	bool isPinReadOnly(unsigned int pin) const;
	/* Diagnostics: the owner string of an exclusively held pad, or NULL.
	 * Read without the state lock, so treat it as a hint. */
	const char *getPinOwner(unsigned int pin) const;

	static TLMMGPIOController *getController(void);

	/*
	 * Pad interrupt registration. The controller owns the single GIC
	 * summary line once the first pad asks for an interrupt; the caller
	 * never names INTID 240 and never programs GIC registers. See
	 * tlmm_gpio.h for the C API and the mask/resume contract.
	 */
	/* Both take the handle that owns the pad: the pad's interrupt
	 * registers belong to that handle, and there is no pin-number form. */
	IOReturn requestPinIRQ(AGPIO *owner, unsigned int detect,
	    tlmm_gpio_irq_handler handler, void *refcon);
	IOReturn releasePinIRQ(AGPIO *owner);
	IOReturn preparePinIRQ(AGPIO *owner);
	IOReturn setProbePending(AGPIO *owner, bool pending);
	/* Teardown helper: releases by pin once the handle is going away. */
	IOReturn freePinIRQ(unsigned int pin);
	/* Registration/teardown are thread-context operations. Successful
	 * unregister drains every dispatch snapshot that could hold the refcon. */
	/* Interrupt-context safe. mask keeps the line for this pad; resume
	 * releases it once no registered pad holds it. */
	IOReturn maskPinIRQ(unsigned int pin);
	IOReturn resumePinIRQ(unsigned int pin);
	IOReturn ackPinIRQ(unsigned int pin);
	bool pinIRQEnabled(unsigned int pin) const;
	bool summaryClaimed(void) const { return fIrqClaimed; }
	/* Interrupt action: dispatched by the GIC for the summary line. Public
	 * because the action thunk has no friendship with this class. */
	void dispatchSummary(void);

private:
	IOReturn requestPinIRQLocked(AGPIO *, unsigned int, tlmm_gpio_irq_handler, void *);
	IOReturn freePinIRQLocked(unsigned int pin);
	bool mapTiles(IOService *provider);
	void readReadOnlyPins(IOService *provider);
	bool pinReadOnlyLocked(unsigned int pin) const;
	bool readPinCount(IOService *provider);
	vm_offset_t pinAddress(unsigned int pin, uint32_t offset) const;
	static unsigned int tileForPin(unsigned int pin);

	bool claimSummary(void);
	void releaseSummary(void);
	void setSummaryMasked(bool masked);
	/* The summary line is this controller's own SPI; toggling it needs two
	 * distributor writes, not a provider round-trip with its RWP waits. */
	bool mapDistributor(void);
	void maskSummaryLine(void);
	void unmaskSummaryLine(void);
	/* Device-memory access with the barriers a posted write needs. */
	void gicdWrite(uint32_t offset, uint32_t value);
	uint32_t gicdRead(uint32_t offset);
	uint32_t readIntrStatus(unsigned int pin) const;
	void writeIntrStatus(unsigned int pin, uint32_t value);
	int irqSlotForPin(unsigned int pin) const;
	bool irqPinReadOnly(unsigned int pin) const;

	IOMemoryMap *fMaps[TLMM_TILE_COUNT];
	vm_offset_t fTiles[TLMM_TILE_COUNT];
	volatile uint32_t *fGicd;
	IOLock *fLock;
	unsigned int fPinCount;
	TLMMPinState fPins[TLMM_PIN_MAX];

	/* Interrupt ownership. fIrqProvider is the ADT nub whose
	 * IOInterruptSpecifiers names INTID 240; the controller registers the
	 * action on it and masks through it, exactly like a driver would. */
	IOService *fIrqProvider;
	IOSimpleLock *fIrqLock;
	IOLock *fIrqRegistrationLock;
	bool fIrqClaimed;
	bool fIrqStopping;
	bool fIrqMasked;
	uint32_t fIrqClientMask;
	unsigned int fIrqCount;
	int fProbeSlot;
	unsigned int fIrqPins[TLMM_IRQ_MAX];
	unsigned int fIrqDetect[TLMM_IRQ_MAX];
	unsigned int fIrqInFlight[TLMM_IRQ_MAX];
	bool fIrqRetiring[TLMM_IRQ_MAX];
	tlmm_gpio_irq_handler fIrqHandlers[TLMM_IRQ_MAX];
	void *fIrqRefcons[TLMM_IRQ_MAX];
	int fIrqSlot[TLMM_PIN_MAX];
	unsigned long fIrqDispatches;
	unsigned long fIrqStrays;
};

#endif /* ! _IOKIT_ARM_TLMM_GPIO_CONTROLLER_H */
