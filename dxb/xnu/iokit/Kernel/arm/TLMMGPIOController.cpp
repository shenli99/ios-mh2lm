/*
 * SM8150 TLMM GPIO controller.
 *
 * Hardware facts (hexagon pinctrl-sm8150.c / pinctrl-msm.c, audited against
 * the MH2LM live tree):
 *
 *   - 175 pads in four 1 MiB tiles at TLMM base + 0x00100000 (west),
 *     +0x00500000 (east), +0x00900000 (north), +0x00d00000 (south).
 *   - A pad's registers are tile_base + 0x1000 * pin (the global pin number)
 *     plus 0x0 CFG, 0x4 IO, 0x8 INTR_CFG, 0xc INTR_STATUS. The slot index is
 *     global, which is why GPIO122 in the south tile sits at 0x03d7a000.
 *   - CFG: pull [1:0], mux function [4:2], drive strength [7:5], output
 *     enable [9]. IO: pad level [0] (read-only), output latch [1].
 *
 * The pad-to-tile map below is sparse by pin number, not a range formula that
 * could be derived from the tile sizes; tileForPin() carries the exact table
 * and is the single place a wrong tile assignment could come from.
 *
 * Nothing here writes INTR_CFG/INTR_STATUS: the TLMM summary interrupt is
 * still driven by the consumer that registered it. The fields are documented
 * in <pexpert/pexpert/arm64/tlmm_gpio.h> for the step that moves the summary
 * line into the interrupt controller.
 */

#if defined(ARM64_BOARD_CONFIG_MH2LM) || defined(ARM64_BOARD_CONFIG_GUACAMOLE)

#include "TLMMGPIOController.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/libkern.h>
/* Distributor mapping: VM_WIMG_IO comes from this header; the map KPI below
 * is not exported by machine/machine_routines.h, so it is declared here the
 * same way the SPMI mux declares it for its own GICD map. */
#include <vm/memory_types.h>
#include <pexpert/pexpert.h>

extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t phys, vm_size_t size,
    unsigned int flags);
#define SWEET_LOG_SINK IOLog
#include "../../../bsd/dev/sweet_log_level.h"

OSDefineMetaClassAndStructors(AGPIO, OSObject);
OSDefineMetaClassAndStructors(TLMMGPIOController, IOService);

/*
 * Pad register ordering: the store barrier must complete before the engine
 * samples the write (dsb osh), and a load that depends on an earlier write
 * needs dmb oshld first. Same pairing as the display and panel drivers.
 */
static uint32_t
tlmmRead32(vm_offset_t address)
{
	uint32_t value = *(volatile uint32_t *)address;

	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static void
tlmmWrite32(vm_offset_t address, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)address = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

/* Encode the CFG field set without touching EGPIO or reserved bits. */
static uint32_t
tlmmEncodeCfg(unsigned int func, unsigned int pull, unsigned int drive,
    bool output)
{
	uint32_t cfg = ((uint32_t)func << TLMM_GPIO_MUX_SHIFT) |
	    ((uint32_t)pull << TLMM_GPIO_PULL_SHIFT) |
	    ((uint32_t)drive << TLMM_GPIO_DRIVE_SHIFT);

	if (output) {
		cfg |= TLMM_GPIO_OE;
	}
	return cfg;
}

static bool
tlmmValidCfg(unsigned int func, unsigned int pull, unsigned int drive)
{
	return func <= TLMM_GPIO_MUX_MASK && pull <= TLMM_GPIO_PULL_MASK &&
	       drive <= TLMM_GPIO_DRIVE_MASK;
}

/* ---------------------------------------------------------------- AGPIO */

bool
AGPIO::initWithPin(TLMMGPIOController *controller, unsigned int pin,
    vm_offset_t cfg, vm_offset_t io, vm_offset_t intr, const char *owner)
{
	if (!controller || !owner || !owner[0] || !cfg || !io || !intr) {
		return false;
	}
	if (fValid) {
		/* An object owns at most one pad, for its whole lifetime. */
		return false;
	}
	if (!OSObject::init()) {
		return false;
	}

	fController = controller;
	fOwner = owner;
	fCfg = (volatile uint32_t *)cfg;
	fIo = (volatile uint32_t *)io;
	fIntr = (volatile uint32_t *)intr;
	fPin = pin;
	fValid = true;
	return true;
}

void
AGPIO::free(void)
{
	/* A driver that forgot release() still returns the pad on teardown. */
	if (fValid) {
		release();
	}
	OSObject::free();
}

uint32_t
AGPIO::readCfg(void) const
{
	uint32_t value = *fCfg;

	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

void
AGPIO::writeCfg(uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*fCfg = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

uint32_t
AGPIO::readIo(void) const
{
	uint32_t value = *fIo;

	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

void
AGPIO::writeIo(uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*fIo = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

uint32_t
AGPIO::readIntrCfg(void) const
{
	uint32_t value = *fIntr;

	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

/* INTID of the TLMM summary SPI: hardware SPI 208 on this SoC. */
#define TLMM_GICD_PA        0x17a00000u
#define TLMM_GICD_SIZE      0x10000u
#define TLMM_IRQ_WORD(id)   (4u * ((id) / 32u))
#define TLMM_IRQ_BIT(id)    (1u << ((id) % 32u))

IOReturn
AGPIO::setInterrupt(unsigned int detect, tlmm_gpio_irq_handler handler,
    void *refcon)
{
	if (!fValid || !fController) {
		return kIOReturnNotOpen;
	}
	return fController->requestPinIRQ(this, detect, handler, refcon);
}

IOReturn
AGPIO::prepareInterrupt(void)
{
	if (!fValid || !fController) {
		return kIOReturnNotOpen;
	}
	return fController->preparePinIRQ(this);
}

IOReturn
AGPIO::disableInterrupt(void)
{
	if (!fValid || !fController) {
		return kIOReturnNotOpen;
	}
	return fController->releasePinIRQ(this);
}

IOReturn
AGPIO::maskInterrupt(void)
{
	if (!fValid || !fController) {
		return kIOReturnNotOpen;
	}
	return fController->maskPinIRQ(fPin);
}

IOReturn
AGPIO::resumeInterrupt(void)
{
	if (!fValid || !fController) {
		return kIOReturnNotOpen;
	}
	return fController->resumePinIRQ(fPin);
}

IOReturn
AGPIO::ackInterrupt(void)
{
	if (!fValid || !fController) {
		return kIOReturnNotOpen;
	}
	return fController->ackPinIRQ(fPin);
}

bool
AGPIO::interruptEnabled(void) const
{
	return fValid && fController && fController->pinIRQEnabled(fPin);
}

IOReturn
AGPIO::getConfig(struct tlmm_pin_config *config)
{
	uint32_t cfg;
	uint32_t io;

	if (!fValid) {
		return kIOReturnNotOpen;
	}
	if (!config) {
		return kIOReturnBadArgument;
	}

	cfg = readCfg();
	io = readIo();
	config->cfg = cfg;
	config->io = io;
	config->intr_cfg = readIntrCfg();
	config->func = (cfg >> TLMM_GPIO_MUX_SHIFT) & TLMM_GPIO_MUX_MASK;
	config->pull = (cfg >> TLMM_GPIO_PULL_SHIFT) & TLMM_GPIO_PULL_MASK;
	config->drive = (cfg >> TLMM_GPIO_DRIVE_SHIFT) & TLMM_GPIO_DRIVE_MASK;
	config->output = (cfg & TLMM_GPIO_OE) != 0;
	config->high = (io & TLMM_GPIO_OUT) != 0;
	return kIOReturnSuccess;
}

IOReturn
AGPIO::setValue(bool high)
{
	if (!fValid) {
		return kIOReturnNotOpen;
	}

	/*
	 * Write the latch alone. IO bit 0 is the pad level and read-only, so
	 * there is nothing to preserve and one store is enough. An explicit level
	 * command always writes, even when the value is unchanged: the caller
	 * asked for the line to be driven, not left as it was found.
	 */
	writeIo(high ? TLMM_GPIO_OUT : 0u);
	return kIOReturnSuccess;
}

bool
AGPIO::getValue(void) const
{
	if (!fValid) {
		return false;
	}

	/* Bit 0 reads the pad, whether or not the driver is enabled. */
	return (readIo() & TLMM_GPIO_IN) != 0;
}

IOReturn
AGPIO::setDirection(bool output)
{
	uint32_t cfg;

	if (!fValid) {
		return kIOReturnNotOpen;
	}

	cfg = readCfg() & ~TLMM_GPIO_OE;
	if (output) {
		/* The latch was set by setValue()/configure(); enabling the driver
		 * here releases the pad at that level instead of at an old one. */
		cfg |= TLMM_GPIO_OE;
	}
	writeCfg(cfg);
	return kIOReturnSuccess;
}

IOReturn
AGPIO::setMux(unsigned int func)
{
	uint32_t cfg;

	if (!fValid) {
		return kIOReturnNotOpen;
	}
	if (func > TLMM_GPIO_MUX_MASK) {
		return kIOReturnBadArgument;
	}

	cfg = readCfg() & ~((uint32_t)TLMM_GPIO_MUX_MASK << TLMM_GPIO_MUX_SHIFT);
	cfg |= (uint32_t)func << TLMM_GPIO_MUX_SHIFT;
	writeCfg(cfg);
	return kIOReturnSuccess;
}

IOReturn
AGPIO::setPull(unsigned int pull)
{
	uint32_t cfg;

	if (!fValid) {
		return kIOReturnNotOpen;
	}
	if (pull > TLMM_GPIO_PULL_MASK) {
		return kIOReturnBadArgument;
	}

	cfg = readCfg() & ~((uint32_t)TLMM_GPIO_PULL_MASK << TLMM_GPIO_PULL_SHIFT);
	cfg |= (uint32_t)pull << TLMM_GPIO_PULL_SHIFT;
	writeCfg(cfg);
	return kIOReturnSuccess;
}

IOReturn
AGPIO::setDrive(unsigned int drive)
{
	uint32_t cfg;

	if (!fValid) {
		return kIOReturnNotOpen;
	}
	if (drive > TLMM_GPIO_DRIVE_MASK) {
		return kIOReturnBadArgument;
	}

	cfg = readCfg() &
	    ~((uint32_t)TLMM_GPIO_DRIVE_MASK << TLMM_GPIO_DRIVE_SHIFT);
	cfg |= (uint32_t)drive << TLMM_GPIO_DRIVE_SHIFT;
	writeCfg(cfg);
	return kIOReturnSuccess;
}

IOReturn
AGPIO::configure(unsigned int func, unsigned int pull, unsigned int drive,
    bool output, bool high)
{
	uint32_t cfg;

	if (!fValid) {
		return kIOReturnNotOpen;
	}
	if (!tlmmValidCfg(func, pull, drive)) {
		return kIOReturnBadArgument;
	}

	/*
	 * Latch the output level first: CFG bit 9 makes the pad an output the
	 * moment it is written, so the level must already be the requested one.
	 * Only write when the level really changes: a redundant store is a
	 * spurious edge on the wire, and callers that re-assert a configuration
	 * (activate keeps the inherited level) must not produce one.
	 */
	if (output) {
		uint32_t level = high ? TLMM_GPIO_OUT : 0u;

		if ((readIo() & TLMM_GPIO_OUT) != level) {
			writeIo(level);
		}
	}

	cfg = (readCfg() & ~TLMM_GPIO_CFG_MASK) |
	    tlmmEncodeCfg(func, pull, drive, output);
	writeCfg(cfg);
	return kIOReturnSuccess;
}

void
AGPIO::release(void)
{
	TLMMGPIOController *controller;
	const char *owner;
	unsigned int pin;

	if (!fValid) {
		return;
	}

	/* Returning the handle also retires its interrupt before the pad becomes
	 * available to another owner. Direct releasePin() refuses live slots. */
	if (fController) {
		(void)fController->releasePinIRQ(this);
	}
	/* The caller serializes this handle's lifecycle against its methods.
	 * Invalidate before returning the now-unregistered pad to the table. */
	fValid = false;
	controller = fController;
	owner = fOwner;
	pin = fPin;
	fController = NULL;
	fOwner = NULL;
	fCfg = NULL;
	fIo = NULL;
	fIntr = NULL;

	(void)controller->releasePin(pin, owner);
}

void
AGPIO::put(void)
{
	/* release() drops the pad, then OSObject::release() drops the reference
	 * (and frees this object when it was the last one). */
	release();
	OSObject::release();
}

void
AGPIO::invalidate(void)
{
	fValid = false;
	fController = NULL;
	fOwner = NULL;
	fCfg = NULL;
	fIo = NULL;
	fIntr = NULL;
}

/* ----------------------------------------------------------- controller */

static TLMMGPIOController *gTLMMController;

TLMMGPIOController *
TLMMGPIOController::getController(void)
{
	return gTLMMController;
}

/*
 * Pad-to-tile map. SM8150's layout is sparse: NORTH owns the low pins, the
 * wide 51..154 span is SOUTH, and the last pads go back to NORTH. This table
 * is the driver's single source for a pad address, so it is written out
 * explicitly rather than derived from the tile sizes.
 */
unsigned int
TLMMGPIOController::tileForPin(unsigned int pin)
{
	if (pin <= 7 || pin == 38 || (pin >= 51 && pin <= 82) ||
	    (pin >= 98 && pin <= 101) || (pin >= 114 && pin <= 124) ||
	    (pin >= 126 && pin <= 154)) {
		return TLMM_TILE_SOUTH;
	}
	if ((pin >= 8 && pin <= 20) || (pin >= 31 && pin <= 37) ||
	    (pin >= 39 && pin <= 42) || (pin >= 83 && pin <= 86) ||
	    (pin >= 88 && pin <= 97) || (pin >= 102 && pin <= 104) ||
	    pin >= 169) {
		return TLMM_TILE_NORTH;
	}
	if ((pin >= 21 && pin <= 30) || (pin >= 43 && pin <= 50) || pin == 87) {
		return TLMM_TILE_EAST;
	}
	return TLMM_TILE_WEST;
}

vm_offset_t
TLMMGPIOController::pinAddress(unsigned int pin, uint32_t offset) const
{
	return fTiles[tileForPin(pin)] + (vm_offset_t)pin * TLMM_PIN_STRIDE +
	       (vm_offset_t)offset;
}

bool
TLMMGPIOController::readPinCount(IOService *provider)
{
	OSData *value;
	unsigned int pins = TLMM_PINS_SM8150;

	value = OSDynamicCast(OSData, provider->getProperty(
	    TLMM_ADT_PIN_COUNT_PROP));
	if (value) {
		if (value->getLength() != sizeof(uint32_t)) {
			SWEET_LOG_ERROR("SWEET TLMM: %s must be one cell\n",
			    TLMM_ADT_PIN_COUNT_PROP);
			return false;
		}
		pins = *(const uint32_t *)value->getBytesNoCopy();
	}
	if (pins == 0 || pins > TLMM_PIN_MAX) {
		SWEET_LOG_ERROR("SWEET TLMM: %u pads do not fit the %u-entry table\n",
		    pins, TLMM_PIN_MAX);
		return false;
	}

	fPinCount = pins;
	return true;
}

bool
TLMMGPIOController::mapTiles(IOService *provider)
{
	/* The node's reg list is west, east, north, south (tile order). */
	static const uint32_t layout[TLMM_TILE_COUNT] = {
		TLMM_TILE_WEST_OFFSET,
		TLMM_TILE_EAST_OFFSET,
		TLMM_TILE_NORTH_OFFSET,
		TLMM_TILE_SOUTH_OFFSET
	};
	IOPhysicalAddress first;
	unsigned int i;

	if (provider->getResources() != kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET TLMM: provider has no resources\n");
		return false;
	}

	for (i = 0; i < TLMM_TILE_COUNT; i++) {
		IOMemoryMap *map = provider->mapDeviceMemoryWithIndex(i);

		if (!map) {
			SWEET_LOG_ERROR("SWEET TLMM: tile %u has no memory map\n", i);
			return false;
		}
		fMaps[i] = map;
		fTiles[i] = (vm_offset_t)map->getVirtualAddress();
		if (!fTiles[i]) {
			SWEET_LOG_ERROR("SWEET TLMM: tile %u did not map\n", i);
			return false;
		}
	}

	/*
	 * Prove the four windows really are the TLMM tiles before any pad is
	 * touched: the SoC places them at fixed offsets a constant distance
	 * apart, and a mis-wired node would otherwise write unrelated MMIO.
	 */
	first = fMaps[0]->getPhysicalAddress();
	for (i = 1; i < TLMM_TILE_COUNT; i++) {
		IOPhysicalAddress here = fMaps[i]->getPhysicalAddress();

		if (here - first !=
		    (IOPhysicalAddress)(layout[i] - layout[0])) {
			SWEET_LOG_ERROR(
			    "SWEET TLMM: tile %u at %#llx is not %#x from %#llx\n",
			    i, here, layout[i] - layout[0], first);
			return false;
		}
	}

	/*
	 * Every pad this controller accepts must fit inside its window: the
	 * slot offset is the global pin number, so a short window shows up here
	 * rather than as a fault on the first write.
	 */
	for (i = 0; i < fPinCount; i++) {
		unsigned int tile = tileForPin(i);
		vm_offset_t need = (vm_offset_t)i * TLMM_PIN_STRIDE +
		    (vm_offset_t)TLMM_GPIO_INTR_STATUS + sizeof(uint32_t);

		if (need > (vm_offset_t)fMaps[tile]->getLength()) {
			SWEET_LOG_ERROR(
			    "SWEET TLMM: gpio%u needs %#llx in tile %u, window is %#llx\n",
			    i, need, tile,
			    (vm_offset_t)fMaps[tile]->getLength());
			return false;
		}
	}

	return true;
}

bool
TLMMGPIOController::start(IOService *provider)
{
	unsigned int i;

	if (!IOService::start(provider)) {
		return false;
	}
	if (!readPinCount(provider)) {
		return false;
	}
	if (!mapTiles(provider)) {
		return false;
	}

	fLock = IOLockAlloc();
	if (!fLock) {
		SWEET_LOG_ERROR("SWEET TLMM: cannot allocate the state lock\n");
		return false;
	}
	for (i = 0; i < fPinCount; i++) {
		fPins[i].handle = NULL;
		fPins[i].owner = NULL;
		fPins[i].pin = i;
		fPins[i].used = false;
		fPins[i].exclusive = false;
		fPins[i].readOnly = false;
	}
	/* Before registerService(): the first consumer can be matched from here
	 * on, and an island-owned pad must already be refused by then. */
	readReadOnlyPins(provider);

	/*
	 * Pad interrupts: the summary line (INTID 240) is claimed lazily, on
	 * the first pad registration, so a boot that never uses a pad
	 * interrupt leaves the GIC exactly as before. sweetgpioirq=1 opts in;
	 * without it every registration returns unsupported and the client
	 * keeps its own legacy path.
	 */
	/* Explicit init: the host fixture allocates on the stack, and a
	 * half-initialised interrupt table must never be visible. */
	fIrqProvider = provider;
	fGicd = NULL;
	fIrqClaimed = false;
	fIrqMasked = false;
	fIrqClientMask = 0;
	fIrqCount = 0;
	fIrqDispatches = 0;
	fIrqStrays = 0;
	for (i = 0; i < TLMM_IRQ_MAX; i++) {
		fIrqPins[i] = 0;
		fIrqHandlers[i] = NULL;
		fIrqRefcons[i] = NULL;
	}
	fIrqLock = IOSimpleLockAlloc();
	if (!fIrqLock) {
		SWEET_LOG_ERROR("SWEET TLMM: cannot allocate the IRQ lock\n");
		return false;
	}
	SWEET_LOG_INFO("SWEET TLMM: pad IRQ lock %p\n", fIrqLock);
	for (i = 0; i < TLMM_PIN_MAX; i++) {
		fIrqSlot[i] = -1;
	}
	gTLMMController = this;
	SWEET_LOG_INFO(
	    "SWEET TLMM: %u pads; west=%#llx east=%#llx north=%#llx south=%#llx\n",
	    fPinCount, fTiles[TLMM_TILE_WEST], fTiles[TLMM_TILE_EAST],
	    fTiles[TLMM_TILE_NORTH], fTiles[TLMM_TILE_SOUTH]);
	SWEET_LOG_INFO("SWEET TLMM: summary INTID %u is claimed on the first pad "
	    "interrupt request, not at start\n", TLMM_SUMMARY_INTID);
	registerService();
	return true;
}

void
TLMMGPIOController::stop(IOService *provider)
{
	unsigned int held = 0;
	unsigned int i;

	if (fLock) {
		IOLockLock(fLock);
		for (i = 0; i < fPinCount; i++) {
			if (fPins[i].exclusive) {
				held++;
			}
		}
		IOLockUnlock(fLock);
	}
	if (held) {
		SWEET_LOG_WARN("SWEET TLMM: stopping with %u exclusive pads held\n",
		    held);
	}

	releaseSummary();

	/*
	 * Invalidate every outstanding handle before the windows are released:
	 * a driver that outlives the controller then fails its next call
	 * (kIOReturnNotOpen) instead of touching unmapped memory.
	 */
	if (fLock) {
		IOLockLock(fLock);
		for (i = 0; i < fPinCount; i++) {
			if (fPins[i].handle) {
				fPins[i].handle->invalidate();
				fPins[i].handle = NULL;
			}
			fPins[i].owner = NULL;
			fPins[i].exclusive = false;
		}
		IOLockUnlock(fLock);
	}

	if (gTLMMController == this) {
		gTLMMController = NULL;
	}
	IOService::stop(provider);
}

void
TLMMGPIOController::free(void)
{
	unsigned int i;

	if (gTLMMController == this) {
		gTLMMController = NULL;
	}
	for (i = 0; i < TLMM_TILE_COUNT; i++) {
		if (fMaps[i]) {
			fMaps[i]->release();
			fMaps[i] = NULL;
		}
		fTiles[i] = 0;
	}
	if (fLock) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	if (fIrqLock) {
		IOSimpleLockFree(fIrqLock);
		fIrqLock = NULL;
	}
	IOService::free();
}

AGPIO *
TLMMGPIOController::acquirePin(unsigned int pin, const char *owner)
{
	AGPIO *handle;

	if (!fLock || pin >= fPinCount || !owner || !owner[0]) {
		return NULL;
	}

	/*
	 * Reserve the pad before allocating the handle: new AGPIO can take a
	 * mutex, and a second caller must already see the pad as taken while
	 * the first one is inside the allocator.
	 */
	IOLockLock(fLock);
	if (fPins[pin].exclusive) {
		const char *holder = fPins[pin].owner;

		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u is owned by %s\n", pin,
		    holder ? holder : "?");
		return NULL;
	}
	/*
	 * A pad whose interrupt is registered belongs to that interrupt owner:
	 * handing out a handle too would let a second component reprogram the
	 * pad under the handler. Same rule in the other direction below.
	 */
	if (fIrqSlot[pin] >= 0) {
		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u has a registered pad interrupt; "
		    "no handle\n", pin);
		return NULL;
	}
	/*
	 * The island drives this pad: hand out read access only. No AGPIO, so
	 * there is no handle that could write it later by accident.
	 */
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u is island-owned; read-only, "
		    "no handle\n", pin);
		return NULL;
	}
	fPins[pin].exclusive = true;
	fPins[pin].owner = owner;
	IOLockUnlock(fLock);

	handle = new AGPIO;
	if (handle && !handle->initWithPin(this, pin,
	    pinAddress(pin, TLMM_GPIO_CFG), pinAddress(pin, TLMM_GPIO_IO),
	    pinAddress(pin, TLMM_GPIO_INTR_CFG), owner)) {
		handle->release();
		handle = NULL;
	}

	IOLockLock(fLock);
	if (handle) {
		fPins[pin].handle = handle;
		fPins[pin].used = true;
	} else {
		fPins[pin].exclusive = false;
		fPins[pin].owner = NULL;
	}
	IOLockUnlock(fLock);

	if (!handle) {
		SWEET_LOG_ERROR("SWEET TLMM: no handle for gpio%u\n", pin);
	}
	return handle;
}

/*
 * Island-owned pads. Two sources agree here:
 *
 *   - the ADT list (sweet,read-only-pins), for pads the firmware owns from
 *     the start but has not steered through EGPIO yet;
 *   - the live CFG bit 12, for pads the firmware enabled after we started.
 *
 * Either way the answer is the same: readable, never writable. Callers holding
 * fLock use pinReadOnlyLocked(); everyone else goes through isPinReadOnly().
 */
bool
TLMMGPIOController::pinReadOnlyLocked(unsigned int pin) const
{
	if (pin >= fPinCount || pin >= TLMM_PIN_MAX) {
		return false;
	}
	if (fPins[pin].readOnly) {
		return true;
	}
	return (tlmmRead32(pinAddress(pin, TLMM_GPIO_CFG)) &
	    TLMM_GPIO_EGPIO_ENABLE) != 0;
}

bool
TLMMGPIOController::isPinReadOnly(unsigned int pin) const
{
	bool readOnly;

	if (!fLock || pin >= fPinCount) {
		return false;
	}
	IOLockLock(fLock);
	readOnly = pinReadOnlyLocked(pin);
	IOLockUnlock(fLock);
	return readOnly;
}

/* Parse the optional u32 array of declared island-owned pads. A malformed
 * property is ignored: it is a hint, and the live EGPIO bit still protects. */
void
TLMMGPIOController::readReadOnlyPins(IOService *provider)
{
	OSData *data = OSDynamicCast(OSData,
	    provider->getProperty(TLMM_ADT_READ_ONLY_PROP));
	const uint32_t *pins;
	unsigned int count;
	unsigned int declared = 0;

	if (!data) {
		return;
	}
	if (data->getLength() % sizeof(uint32_t) != 0) {
		SWEET_LOG_WARN("SWEET TLMM: %s is not a u32 array; ignored\n",
		    TLMM_ADT_READ_ONLY_PROP);
		return;
	}
	pins = (const uint32_t *)data->getBytesNoCopy();
	count = data->getLength() / sizeof(uint32_t);
	for (unsigned int i = 0; i < count; i++) {
		if (pins[i] >= fPinCount || pins[i] >= TLMM_PIN_MAX) {
			continue;
		}
		fPins[pins[i]].readOnly = true;
		declared++;
	}
	SWEET_LOG_INFO("SWEET TLMM: %u pads declared island-owned (read-only)\n",
	    declared);
}

IOReturn
TLMMGPIOController::releasePin(unsigned int pin, const char *owner)
{
	bool mine;

	if (!fLock || pin >= fPinCount || !owner) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	mine = fPins[pin].exclusive &&
	    (fPins[pin].owner == owner ||
	    (fPins[pin].owner && strcmp(fPins[pin].owner, owner) == 0));
	if (!mine) {
		IOLockUnlock(fLock);
		SWEET_LOG_ERROR("SWEET TLMM: gpio%u released by a non-owner\n", pin);
		return kIOReturnNotPermitted;
	}
	if (irqSlotForPin(pin) >= 0) {
		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u released with a live pad interrupt; "
		    "disableInterrupt() first\n", pin);
		return kIOReturnBusy;
	}
	fPins[pin].handle = NULL;
	fPins[pin].owner = NULL;
	fPins[pin].exclusive = false;
	IOLockUnlock(fLock);

	SWEET_LOG_DEBUG("SWEET TLMM: gpio%u released\n", pin);
	return kIOReturnSuccess;
}

const char *
TLMMGPIOController::getPinOwner(unsigned int pin) const
{
	if (pin >= fPinCount) {
		return NULL;
	}
	if (fPins[pin].owner) {
		return fPins[pin].owner;
	}
	/* Diagnostic hint: a pad the island drives has no owner, but saying so is
	 * more useful than reporting it free. */
	if (fPins[pin].readOnly) {
		return "island";
	}
	return NULL;
}


/* ------------------------------------------------------- plain-mode path */

IOReturn
TLMMGPIOController::setPinValue(unsigned int pin, bool high)
{
	vm_offset_t address;
	uint32_t io;

	if (!fLock || pin >= fPinCount) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	/* Write side of the split: an AGPIO owns every store to this pad, and an
	 * island-owned pad accepts no store at all. */
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	if (fPins[pin].exclusive) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	address = pinAddress(pin, TLMM_GPIO_IO);
	io = tlmmRead32(address) & ~TLMM_GPIO_OUT;
	if (high) {
		io |= TLMM_GPIO_OUT;
	}
	tlmmWrite32(address, io);
	fPins[pin].used = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::getPinValue(unsigned int pin, bool *high)
{
	uint32_t io;

	if (!fLock || pin >= fPinCount || !high) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	io = tlmmRead32(pinAddress(pin, TLMM_GPIO_IO));
	IOLockUnlock(fLock);

	*high = (io & TLMM_GPIO_IN) != 0;
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::setPinDirection(unsigned int pin, bool output)
{
	vm_offset_t address;
	uint32_t cfg;

	if (!fLock || pin >= fPinCount) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	if (fPins[pin].exclusive) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	address = pinAddress(pin, TLMM_GPIO_CFG);
	cfg = tlmmRead32(address) & ~TLMM_GPIO_OE;
	if (output) {
		cfg |= TLMM_GPIO_OE;
	}
	tlmmWrite32(address, cfg);
	fPins[pin].used = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::setPinMux(unsigned int pin, unsigned int func)
{
	vm_offset_t address;
	uint32_t cfg;

	if (!fLock || pin >= fPinCount || func > TLMM_GPIO_MUX_MASK) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	if (fPins[pin].exclusive) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	address = pinAddress(pin, TLMM_GPIO_CFG);
	cfg = tlmmRead32(address) &
	    ~((uint32_t)TLMM_GPIO_MUX_MASK << TLMM_GPIO_MUX_SHIFT);
	cfg |= (uint32_t)func << TLMM_GPIO_MUX_SHIFT;
	tlmmWrite32(address, cfg);
	fPins[pin].used = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::setPinPull(unsigned int pin, unsigned int pull)
{
	vm_offset_t address;
	uint32_t cfg;

	if (!fLock || pin >= fPinCount || pull > TLMM_GPIO_PULL_MASK) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	if (fPins[pin].exclusive) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	address = pinAddress(pin, TLMM_GPIO_CFG);
	cfg = tlmmRead32(address) &
	    ~((uint32_t)TLMM_GPIO_PULL_MASK << TLMM_GPIO_PULL_SHIFT);
	cfg |= (uint32_t)pull << TLMM_GPIO_PULL_SHIFT;
	tlmmWrite32(address, cfg);
	fPins[pin].used = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::setPinDrive(unsigned int pin, unsigned int drive)
{
	vm_offset_t address;
	uint32_t cfg;

	if (!fLock || pin >= fPinCount || drive > TLMM_GPIO_DRIVE_MASK) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	if (fPins[pin].exclusive) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	address = pinAddress(pin, TLMM_GPIO_CFG);
	cfg = tlmmRead32(address) &
	    ~((uint32_t)TLMM_GPIO_DRIVE_MASK << TLMM_GPIO_DRIVE_SHIFT);
	cfg |= (uint32_t)drive << TLMM_GPIO_DRIVE_SHIFT;
	tlmmWrite32(address, cfg);
	fPins[pin].used = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::setPinConfig(unsigned int pin, unsigned int func,
    unsigned int pull, unsigned int drive, bool output, bool high)
{
	vm_offset_t cfgAddress;
	uint32_t cfg;

	if (!fLock || pin >= fPinCount || !tlmmValidCfg(func, pull, drive)) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	if (fPins[pin].exclusive) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	if (output) {
		/* Latch the level before the pad driver is enabled. */
		vm_offset_t ioAddress = pinAddress(pin, TLMM_GPIO_IO);
		uint32_t io = tlmmRead32(ioAddress) & ~TLMM_GPIO_OUT;

		if (high) {
			io |= TLMM_GPIO_OUT;
		}
		tlmmWrite32(ioAddress, io);
	}
	cfgAddress = pinAddress(pin, TLMM_GPIO_CFG);
	cfg = (tlmmRead32(cfgAddress) & ~TLMM_GPIO_CFG_MASK) |
	    tlmmEncodeCfg(func, pull, drive, output);
	tlmmWrite32(cfgAddress, cfg);
	fPins[pin].used = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::getPinConfig(unsigned int pin, struct tlmm_pin_config *config)
{
	uint32_t cfg;
	uint32_t io;
	uint32_t intr;

	if (!fLock || pin >= fPinCount || !config) {
		return kIOReturnBadArgument;
	}

	IOLockLock(fLock);
	cfg = tlmmRead32(pinAddress(pin, TLMM_GPIO_CFG));
	io = tlmmRead32(pinAddress(pin, TLMM_GPIO_IO));
	intr = tlmmRead32(pinAddress(pin, TLMM_GPIO_INTR_CFG));
	IOLockUnlock(fLock);

	config->cfg = cfg;
	config->io = io;
	config->intr_cfg = intr;
	config->func = (cfg >> TLMM_GPIO_MUX_SHIFT) & TLMM_GPIO_MUX_MASK;
	config->pull = (cfg >> TLMM_GPIO_PULL_SHIFT) & TLMM_GPIO_PULL_MASK;
	config->drive = (cfg >> TLMM_GPIO_DRIVE_SHIFT) & TLMM_GPIO_DRIVE_MASK;
	config->output = (cfg & TLMM_GPIO_OE) != 0;
	config->high = (io & TLMM_GPIO_OUT) != 0;
	return kIOReturnSuccess;
}

/* ------------------------------------------------------------- C API */

static int
tlmmCResult(IOReturn status)
{
	switch (status) {
	case kIOReturnSuccess:
		return TLMM_GPIO_OK;
	case kIOReturnBusy:
		return TLMM_GPIO_EBUSY;
	case kIOReturnBadArgument:
		return TLMM_GPIO_EINVAL;
	case kIOReturnNotOpen:
		return TLMM_GPIO_ENODEV;
	case kIOReturnUnsupported:
		/* Island-owned pad: the controller exists, the write does not. */
		return TLMM_GPIO_EOPNOTSUPP;
	case kIOReturnNotPermitted:
		return TLMM_GPIO_EBUSY;
	case kIOReturnNoResources:
		/* No pad slot left, or the summary line could not be claimed. */
		return TLMM_GPIO_ENOSPC;
	default:
		return TLMM_GPIO_EIO;
	}
}

/* --------------------------------------------------------- pad interrupts */

/*
 * One GIC line, many pads. Ownership sits here so that a client cannot mask a
 * line it does not own, and so that the per-pad status is read in the same
 * place the pad is programmed. Dispatch order: mask the line, clear and serve
 * every asserting pad, then release the line unless a handler asked to keep it
 * (level-low pads such as touch ATTN defer their bottom half).
 */
extern "C" void
sweet_tlmm_summary_irq(void *target, void *refCon, void *nub, int source)
{
	(void)refCon;
	(void)nub;
	(void)source;
	if (target) {
		((TLMMGPIOController *)target)->dispatchSummary();
	}
}

uint32_t
TLMMGPIOController::readIntrStatus(unsigned int pin) const
{
	volatile uint32_t *status;

	status = (volatile uint32_t *)pinAddress(pin, TLMM_GPIO_INTR_STATUS);
	return *status;
}

void
TLMMGPIOController::writeIntrStatus(unsigned int pin, uint32_t value)
{
	volatile uint32_t *status;

	status = (volatile uint32_t *)pinAddress(pin, TLMM_GPIO_INTR_STATUS);
	*status = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

bool
TLMMGPIOController::irqPinReadOnly(unsigned int pin) const
{
	/* The ADT flag is immutable after start. This live check must not take
	 * the sleeping pin-state lock when called from a pad's top half. */
	return fPins[pin].readOnly ||
	       (tlmmRead32(pinAddress(pin, TLMM_GPIO_CFG)) & TLMM_GPIO_EGPIO_ENABLE);
}

int
TLMMGPIOController::irqSlotForPin(unsigned int pin) const
{
	if (pin >= fPinCount) {
		return -1;
	}
	return __atomic_load_n(&fIrqSlot[pin], __ATOMIC_ACQUIRE);
}

/*
 * The controller's enable/disable are IOInterruptController entry points and
 * may take IOLocks, so they must never run inside a spin-lock section (or with
 * interrupts disabled): the state flip happens under fIrqLock, the call itself
 * happens after the lock is dropped. That ordering is also what makes a
 * recursive acquisition impossible no matter what the provider does.
 */
/*
 * The summary SPI belongs to this controller, so holding and releasing it is
 * two distributor writes. Going back through the provider per packet cost a
 * vector bookkeeping pass plus two RWP waits, and -- worse -- it let two
 * writers share one bit: a client that wrote ICENABLER directly left
 * fIrqMasked untouched, so the matching enable became a no-op and the line
 * stayed dead. One owner, one writer.
 */
bool
TLMMGPIOController::mapDistributor(void)
{
	vm_offset_t va;

	if (fGicd) {
		return true;
	}
	va = ml_io_map_unmappable((vm_offset_t)TLMM_GICD_PA, TLMM_GICD_SIZE,
	    VM_WIMG_IO);
	if (!va) {
		SWEET_LOG_WARN("SWEET TLMM: distributor map failed; the summary "
		    "line falls back to provider enable/disable\n");
		return false;
	}
	fGicd = (volatile uint32_t *)va;
	return true;
}

/*
 * The distributor is device memory: a store is posted, so a load issued right
 * after it can still return the old value. Both registers are write-1-set, so
 * the zero half of each write is a hardware no-op; it exists only so the
 * fixture's plain buffer tracks the same state a real distributor reports.
 */
void
TLMMGPIOController::gicdWrite(uint32_t offset, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)((vm_offset_t)fGicd + offset) = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

uint32_t
TLMMGPIOController::gicdRead(uint32_t offset)
{
	uint32_t value = *(volatile uint32_t *)((vm_offset_t)fGicd + offset);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

void
TLMMGPIOController::maskSummaryLine(void)
{
	uint32_t bit = TLMM_IRQ_BIT(TLMM_SUMMARY_INTID);
	uint32_t enable = 0x100u + TLMM_IRQ_WORD(TLMM_SUMMARY_INTID);
	uint32_t disable = 0x180u + TLMM_IRQ_WORD(TLMM_SUMMARY_INTID);

	if (fGicd) {
		gicdWrite(enable, gicdRead(enable) & ~bit); /* W1S: 0 is a no-op */
		gicdWrite(disable, bit);
	} else if (fIrqProvider) {
		(void)fIrqProvider->disableInterrupt(0);
	}
}

void
TLMMGPIOController::unmaskSummaryLine(void)
{
	uint32_t bit = TLMM_IRQ_BIT(TLMM_SUMMARY_INTID);
	uint32_t enable = 0x100u + TLMM_IRQ_WORD(TLMM_SUMMARY_INTID);
	uint32_t disable = 0x180u + TLMM_IRQ_WORD(TLMM_SUMMARY_INTID);

	if (fGicd) {
		gicdWrite(enable, gicdRead(enable) | bit);
		gicdWrite(disable, 0u); /* W1S: 0 is a no-op */
		/* The dsb above means this read is now the truth, not a posted-write
		 * ghost. A line that stays disabled is the failure this ownership
		 * split exists to prevent, so name it if it happens. */
		if (!(gicdRead(enable) & bit)) {
			SWEET_LOG_ERROR("SWEET TLMM: summary INTID %u did not enable "
			    "(ISENABLER=%#x); pad interrupts stay off\n",
			    TLMM_SUMMARY_INTID, gicdRead(enable));
		}
	} else if (fIrqProvider) {
		(void)fIrqProvider->enableInterrupt(0);
	}
}

void
TLMMGPIOController::setSummaryMasked(bool masked)
{
	IOInterruptState saved;
	bool act = false;

	if (!fIrqProvider) {
		return;
	}
	saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	if (masked && !fIrqMasked) {
		fIrqMasked = true;
		act = true;
	} else if (!masked && fIrqMasked && fIrqClientMask == 0) {
		fIrqMasked = false;
		act = true;
	}
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);

	if (!act) {
		return;
	}
	if (masked) {
		maskSummaryLine();
	} else {
		unmaskSummaryLine();
	}
}

bool
TLMMGPIOController::claimSummary(void)
{
	IOReturn status;

	if (fIrqClaimed) {
		return true;
	}
	if (!fIrqProvider || !fIrqProvider->propertyExists("IOInterruptSpecifiers")) {
		SWEET_LOG_ERROR("SWEET TLMM: no interrupt specifier on the ADT node; "
		    "pad interrupts stay with the client\n");
		return false;
	}
	(void)mapDistributor();
	status = fIrqProvider->registerInterrupt(0, this,
	    (IOInterruptAction)sweet_tlmm_summary_irq, NULL);
	if (status != kIOReturnSuccess) {
		SWEET_LOG_ERROR("SWEET TLMM: INTID %u claim failed rc=%#x; pad "
		    "interrupts stay with the client\n", TLMM_SUMMARY_INTID,
		    (unsigned)status);
		return false;
	}
	/* The controller has to clear its own software disable exactly once;
	 * a distributor write never does (that omission refused every dispatch
	 * until BUG-013). After that the line is held by the distributor bit,
	 * which this controller owns, rather than by the provider's vector. */
	fIrqClaimed = true;
	(void)fIrqProvider->enableInterrupt(0);
	/* Start masked: a pad interrupt is only meaningful once a pad is armed,
	 * and the client arms by calling resume. */
	maskSummaryLine();
	fIrqMasked = true;
	SWEET_LOG_INFO("SWEET TLMM: summary INTID %u claimed for %u pads; "
	    "provider enabled once, the distributor bit is ours (%s)\n",
	    TLMM_SUMMARY_INTID, fIrqCount, fGicd ? "mapped" : "provider fallback");
	return true;
}

void
TLMMGPIOController::releaseSummary(void)
{
	if (!fIrqClaimed || !fIrqProvider) {
		return;
	}
	maskSummaryLine();
	(void)fIrqProvider->disableInterrupt(0);
	(void)fIrqProvider->unregisterInterrupt(0);
	fIrqClaimed = false;
	fIrqMasked = false;
	SWEET_LOG_INFO("SWEET TLMM: summary INTID %u released; dispatches=%lu "
	    "strays=%lu\n", TLMM_SUMMARY_INTID, fIrqDispatches, fIrqStrays);
}

void
TLMMGPIOController::dispatchSummary(void)
{
	unsigned int pins[TLMM_IRQ_MAX];
	tlmm_gpio_irq_handler handlers[TLMM_IRQ_MAX];
	void *refcons[TLMM_IRQ_MAX];
	unsigned int count;
	unsigned int served = 0;
	unsigned int i;
	IOInterruptState saved;

	if (!fIrqProvider || !fIrqLock) {
		return;
	}
	/* Level line: mask first, so the aggregate cannot re-enter while the
	 * status of every pad is being drained. */
	setSummaryMasked(true);

	saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	count = fIrqCount > TLMM_IRQ_MAX ? TLMM_IRQ_MAX : fIrqCount;
	for (i = 0; i < count; i++) {
		pins[i] = fIrqPins[i];
		handlers[i] = fIrqHandlers[i];
		refcons[i] = fIrqRefcons[i];
	}
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);

	for (i = 0; i < count; i++) {
		uint32_t status;

		if (!handlers[i]) {
			continue;
		}
		status = readIntrStatus(pins[i]);
		if (!(status & TLMM_INTR_STATUS_RAW)) {
			continue;
		}
		/* A client-masked pad is quiet: its latched status belongs to the
		 * worker that owns it, and one packet must not run the handler twice.
		 * The summary itself is already masked, so this costs no interrupt. */
		bool masked;
		saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
		masked = ((fIrqClientMask >> i) & 1u) != 0;
		IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
		if (masked) {
			continue;
		}
		served++;
		handlers[i](pins[i], refcons[i]);
		/* The client owns this pad's status, not the summary. A handler that
		 * deferred to its own worker (it masked the pad) needs the latch as
		 * the only surviving evidence that a packet is queued: clearing it
		 * here loses the packet whenever the controller deasserts ATTENTION
		 * before the worker samples the pin. Only a handler that kept no
		 * claim (no client mask) lets the controller ack for it. */
		bool deferred;
		saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
		deferred = ((fIrqClientMask >> i) & 1u) != 0; /* snapshot index == slot */
		IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
		if (!deferred) {
			uint32_t after = readIntrStatus(pins[i]);
			if (after & TLMM_INTR_STATUS_RAW) {
				writeIntrStatus(pins[i], after & ~TLMM_INTR_STATUS_RAW);
			}
		}
	}

	fIrqDispatches++;
	if (!served) {
		fIrqStrays++;
		if ((fIrqStrays & (fIrqStrays - 1)) == 0) {
			SWEET_LOG_DEBUG("SWEET TLMM: summary without pad status; "
			    "strays=%lu\n", fIrqStrays);
		}
	}

	setSummaryMasked(false);
}

IOReturn
TLMMGPIOController::requestPinIRQ(AGPIO *owner, unsigned int detect,
    tlmm_gpio_irq_handler handler, void *refcon)
{
	volatile uint32_t *intrCfg;
	uint32_t cfg;
	uint32_t want;
	int slot;
	unsigned int i;
	unsigned int pin;
	bool mine;
	IOInterruptState saved;

	if (!owner || !owner->isValid() || !handler ||
	    detect > TLMM_IRQ_EDGE_BOTH) {
		return kIOReturnBadArgument;
	}
	pin = owner->getPin();
	if (pin >= fPinCount) {
		return kIOReturnBadArgument;
	}

	/* The claim must come from the handle that owns the pad: without that,
	 * a pin number would be enough to take over someone else's pad. */
	IOLockLock(fLock);
	mine = fPins[pin].exclusive && fPins[pin].handle == owner;
	if (!mine) {
		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u is not owned by this handle; "
		    "no pad interrupt\n", pin);
		return kIOReturnNotPermitted;
	}
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u is island-owned; no pad "
		    "interrupt\n", pin);
		return kIOReturnUnsupported;
	}
	if (fIrqSlot[pin] >= 0) {
		IOLockUnlock(fLock);
		SWEET_LOG_WARN("SWEET TLMM: gpio%u already has a pad interrupt\n",
		    pin);
		return kIOReturnBusy;
	}
	IOLockUnlock(fLock);

	if (!claimSummary()) {
		return kIOReturnNoResources;
	}

	/*
	 * Routing bits are the controller's: target KPSS and raw status are
	 * what put the pad on the summary line at all. Polarity, detect and
	 * the enable bit stay with the client until its own programming is
	 * proven reproducible here; the request is recorded and logged so the
	 * follow-up migration has the intent on record.
	 */
	intrCfg = (volatile uint32_t *)pinAddress(pin, TLMM_GPIO_INTR_CFG);
	cfg = *intrCfg;
	want = (cfg & ~TLMM_INTR_ROUTE_MASK) |
	    (TLMM_INTR_TARGET_KPSS << TLMM_INTR_TARGET_SHIFT) | TLMM_INTR_ROUTE_RAW;
	if (want != cfg) {
		__asm__ volatile("dmb oshst" ::: "memory");
		*intrCfg = want;
		__asm__ volatile("dsb osh" ::: "memory");
	}
	writeIntrStatus(pin, 0);

	saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	slot = -1;
	for (i = 0; i < TLMM_IRQ_MAX; i++) {
		if (!fIrqHandlers[i]) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0) {
		IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
		SWEET_LOG_WARN("SWEET TLMM: pad interrupt table full (%u)\n",
		    TLMM_IRQ_MAX);
		return kIOReturnNoResources;
	}
	fIrqPins[slot] = pin;
	fIrqDetect[slot] = detect;
	fIrqHandlers[slot] = handler;
	fIrqRefcons[slot] = refcon;
	fIrqSlot[pin] = slot;
	if ((unsigned int)slot + 1u > fIrqCount) {
		fIrqCount = (unsigned int)slot + 1u;
	}
	/* The line stays masked until the client arms and calls resume. */
	fIrqClientMask |= 1u << slot;
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);

	cfg = *intrCfg;
	SWEET_LOG_INFO("SWEET TLMM: gpio%u pad interrupt slot=%d detect=%u "
	    "intr_cfg=%#x (route owned, pad config with the client)\n",
	    pin, slot, detect, cfg);
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::preparePinIRQ(AGPIO *owner)
{
	if (!owner || !owner->isValid()) {
		return kIOReturnBadArgument;
	}
	if (getPlatform()->atInterruptLevel()) {
		return kIOReturnNotPermitted;
	}
	unsigned int pin = owner->getPin();
	if (pin >= fPinCount) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fLock);
	if (!fPins[pin].exclusive || fPins[pin].handle != owner) {
		IOLockUnlock(fLock);
		return kIOReturnNotPermitted;
	}
	if (pinReadOnlyLocked(pin)) {
		IOLockUnlock(fLock);
		return kIOReturnUnsupported;
	}
	int slot = irqSlotForPin(pin);
	if (slot < 0) {
		IOLockUnlock(fLock);
		return kIOReturnNotFound;
	}
	IOInterruptState saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	bool masked = (fIrqClientMask & (1u << (unsigned int)slot)) != 0;
	unsigned int detect = fIrqDetect[slot];
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
	if (!masked) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}

	uint32_t trigger = 0;
	switch (detect) {
	case TLMM_IRQ_LEVEL_HIGH: trigger = TLMM_INTR_POLARITY; break;
	case TLMM_IRQ_LEVEL_LOW: break;
	case TLMM_IRQ_EDGE_RISING:
		trigger = TLMM_INTR_POLARITY | (1u << TLMM_INTR_DETECT_SHIFT); break;
	case TLMM_IRQ_EDGE_FALLING: trigger = 1u << TLMM_INTR_DETECT_SHIFT; break;
	case TLMM_IRQ_EDGE_BOTH: trigger = 3u << TLMM_INTR_DETECT_SHIFT; break;
	}
	vm_offset_t address = pinAddress(pin, TLMM_GPIO_INTR_CFG);
	uint32_t want = (tlmmRead32(address) & ~0xffu) |
	    TLMM_INTR_ROUTE_TARGET | TLMM_INTR_ROUTE_RAW | trigger;
	tlmmWrite32(address, want);
	uint32_t status = readIntrStatus(pin);
	writeIntrStatus(pin, status & ~TLMM_INTR_STATUS_RAW);
	bool matched = (tlmmRead32(address) & 0xffu) == (want & 0xffu);
	IOLockUnlock(fLock);
	return matched ? kIOReturnSuccess : kIOReturnIOError;
}

IOReturn
TLMMGPIOController::releasePinIRQ(AGPIO *owner)
{
	unsigned int pin;

	if (!owner || !owner->isValid()) {
		return kIOReturnNotOpen;
	}
	pin = owner->getPin();
	return freePinIRQ(pin);
}

IOReturn
TLMMGPIOController::freePinIRQ(unsigned int pin)
{
	int slot;
	IOInterruptState saved;

	if (pin >= fPinCount) {
		return kIOReturnBadArgument;
	}
	slot = irqSlotForPin(pin);
	if (slot < 0) {
		return kIOReturnNotFound;
	}

	/* Stop this pad contributing before removing the slot that services it.
	 * Retain the b22 summary gate; another pad's resume decides when it opens.
	 * Do not ack here: unregistration is not permission to consume a packet. */
	(void)maskPinIRQ(pin);
	saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	fIrqHandlers[slot] = NULL;
	fIrqRefcons[slot] = NULL;
	fIrqClientMask &= ~(1u << (unsigned int)slot);
	__atomic_store_n(&fIrqSlot[pin], -1, __ATOMIC_RELEASE);
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);

	SWEET_LOG_INFO("SWEET TLMM: gpio%u pad interrupt freed slot=%d\n", pin,
	    slot);

	/* Last registered pad: hand the line back so a boot that no longer uses
	 * a pad interrupt leaves the GIC as it found it. */
	{
		bool any = false;
		unsigned int i;

		saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
		for (i = 0; i < TLMM_IRQ_MAX; i++) {
			if (fIrqHandlers[i]) {
				any = true;
			}
		}
		IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
		if (!any) {
			releaseSummary();
		}
	}
	return kIOReturnSuccess;
}

IOReturn
TLMMGPIOController::maskPinIRQ(unsigned int pin)
{
	int slot;
	IOInterruptState saved;

	slot = irqSlotForPin(pin);
	if (slot < 0) {
		return kIOReturnNotFound;
	}
	saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	fIrqClientMask |= 1u << (unsigned int)slot;
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
	if (irqPinReadOnly(pin)) {
		/* Gate the summary, but never write a pad now owned by island. */
		setSummaryMasked(true);
		return kIOReturnUnsupported;
	}
	/* Clear this pad's own enable bit first: the status latch below stays
	 * readable for the client, and the shared summary deasserts for this pad
	 * without touching any other one. */
	{
		vm_offset_t intrCfg = pinAddress(pin, TLMM_GPIO_INTR_CFG);
		if (intrCfg) {
			/* Barrier helpers: the worker reads this bit back right after
			 * resume, and a posted pad write would read stale. */
			tlmmWrite32(intrCfg, tlmmRead32(intrCfg) & ~TLMM_INTR_ENABLE);
		}
	}
	setSummaryMasked(true);
	return kIOReturnSuccess;
}

/*
 * Clear this pad's status latch. The client that owns the pad acks it (the
 * latch is its evidence that a packet is queued), so the controller only
 * offers the register write; it never acks behind the client's back.
 */
IOReturn
TLMMGPIOController::ackPinIRQ(unsigned int pin)
{
	uint32_t status;

	if (irqSlotForPin(pin) < 0) {
		return kIOReturnNotFound;
	}
	if (irqPinReadOnly(pin)) {
		return kIOReturnUnsupported;
	}
	status = readIntrStatus(pin);
	if (status & TLMM_INTR_STATUS_RAW) {
		writeIntrStatus(pin, status & ~TLMM_INTR_STATUS_RAW);
	}
	return kIOReturnSuccess;
}

bool
TLMMGPIOController::pinIRQEnabled(unsigned int pin) const
{
	vm_offset_t intrCfg;

	if (irqSlotForPin(pin) < 0) {
		return false;
	}
	intrCfg = pinAddress(pin, TLMM_GPIO_INTR_CFG);
	return intrCfg && (tlmmRead32(intrCfg) & TLMM_INTR_ENABLE) != 0;
}

IOReturn
TLMMGPIOController::resumePinIRQ(unsigned int pin)
{
	int slot;
	IOInterruptState saved;

	slot = irqSlotForPin(pin);
	if (slot < 0) {
		return kIOReturnNotFound;
	}
	if (irqPinReadOnly(pin)) {
		return kIOReturnUnsupported;
	}
	saved = IOSimpleLockLockDisableInterrupt(fIrqLock);
	fIrqClientMask &= ~(1u << (unsigned int)slot);
	IOSimpleLockUnlockEnableInterrupt(fIrqLock, saved);
	{
		vm_offset_t intrCfg = pinAddress(pin, TLMM_GPIO_INTR_CFG);
		/* A level pad re-asserts immediately; the stale latch would make
		 * the first dispatch look like a duplicate, so drop it here. */
		(void)ackPinIRQ(pin);
		if (intrCfg) {
			tlmmWrite32(intrCfg, tlmmRead32(intrCfg) | TLMM_INTR_ENABLE);
		}
	}
	setSummaryMasked(false);
	return kIOReturnSuccess;
}

extern "C" int
tlmm_gpio_available(void)
{
	return TLMMGPIOController::getController() ? 1 : 0;
}

extern "C" int
gpio_is_read_only(unsigned int pin)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return 0;
	}
	return controller->isPinReadOnly(pin) ? 1 : 0;
}

extern "C" int
gpio_set_value(unsigned int pin, int high)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->setPinValue(pin, high != 0));
}

extern "C" int
gpio_get_value(unsigned int pin, int *high)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();
	bool value = false;
	IOReturn status;

	if (!controller || !high) {
		return controller ? TLMM_GPIO_EINVAL : TLMM_GPIO_ENODEV;
	}
	status = controller->getPinValue(pin, &value);
	if (status != kIOReturnSuccess) {
		return tlmmCResult(status);
	}
	*high = value ? 1 : 0;
	return TLMM_GPIO_OK;
}

extern "C" int
gpio_set_direction(unsigned int pin, int output)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->setPinDirection(pin, output != 0));
}

extern "C" int
gpio_set_mux(unsigned int pin, unsigned int func)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->setPinMux(pin, func));
}

extern "C" int
gpio_set_pull(unsigned int pin, unsigned int pull)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->setPinPull(pin, pull));
}

extern "C" int
gpio_set_drive(unsigned int pin, unsigned int drive)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->setPinDrive(pin, drive));
}

extern "C" int
gpio_set_config(unsigned int pin, unsigned int func, unsigned int pull,
    unsigned int drive, int output, int high)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller) {
		return TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->setPinConfig(pin, func, pull, drive,
	    output != 0, high != 0));
}






extern "C" int
gpio_get_config(unsigned int pin, struct tlmm_pin_config *config)
{
	TLMMGPIOController *controller = TLMMGPIOController::getController();

	if (!controller || !config) {
		return controller ? TLMM_GPIO_EINVAL : TLMM_GPIO_ENODEV;
	}
	return tlmmCResult(controller->getPinConfig(pin, config));
}

#endif /* ARM64_BOARD_CONFIG_MH2LM || ARM64_BOARD_CONFIG_GUACAMOLE */
