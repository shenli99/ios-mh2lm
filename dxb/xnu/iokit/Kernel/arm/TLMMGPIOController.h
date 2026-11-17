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
 *   AGPIO — acquirePin() hands out an OSObject that cached the pad's CFG and
 *   IO addresses when it was created. Its methods write those pointers
 *   directly: no tile lookup, no lock, no controller call. The pin stays
 *   exclusive to the handle until release() (or free()) returns it, and no
 *   other caller can acquire it in the meantime.
 *
 *   Plain mode — setPinValue()/getPinValue()/setPinDirection()/
 *   setPinConfig()/... take a pin number, resolve the tile, and refuse pads
 *   that an AGPIO holds (kIOReturnBusy). The C API in
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

	/* Return the pin to the controller. Idempotent: a released handle keeps
	 * failing every operation and frees itself normally. */
	void release(void);

	unsigned int getPin(void) const { return fPin; }
	const char *getOwner(void) const { return fOwner; }
	bool isValid(void) const { return fValid; }

private:
	/*
	 * Private binder: acquirePin() is a friend. It also refuses to bind an
	 * object twice, so a handle can never end up owning two pads.
	 */
	bool initWithPin(TLMMGPIOController *controller, unsigned int pin,
	    vm_offset_t cfg, vm_offset_t io, const char *owner);

	/* Controller teardown: the MMIO windows are going away, so the handle
	 * must stop writing them instead of faulting on a stale address. */
	void invalidate(void);

	uint32_t readCfg(void) const;
	void writeCfg(uint32_t value);
	uint32_t readIo(void) const;
	void writeIo(uint32_t value);

	TLMMGPIOController *fController;
	const char *fOwner;
	volatile uint32_t *fCfg;
	volatile uint32_t *fIo;
	unsigned int fPin;
	bool fValid;
};

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

	/* Plain mode: kIOReturnBusy when an AGPIO holds the pad. */
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
	/* Diagnostics: the owner string of an exclusively held pad, or NULL.
	 * Read without the state lock, so treat it as a hint. */
	const char *getPinOwner(unsigned int pin) const;

	static TLMMGPIOController *getController(void);

private:
	bool mapTiles(IOService *provider);
	bool readPinCount(IOService *provider);
	vm_offset_t pinAddress(unsigned int pin, uint32_t offset) const;
	static unsigned int tileForPin(unsigned int pin);

	IOMemoryMap *fMaps[TLMM_TILE_COUNT];
	vm_offset_t fTiles[TLMM_TILE_COUNT];
	IOLock *fLock;
	unsigned int fPinCount;
	TLMMPinState fPins[TLMM_PIN_MAX];
};

#endif /* ! _IOKIT_ARM_TLMM_GPIO_CONTROLLER_H */
