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
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
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

	/* Invalidate first: a concurrent method sees an inert handle, and the
	 * controller frees the table slot knowing no user can write it again. */
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
	}

	gTLMMController = this;
	SWEET_LOG_INFO(
	    "SWEET TLMM: %u pads; west=%#llx east=%#llx north=%#llx south=%#llx\n",
	    fPinCount, fTiles[TLMM_TILE_WEST], fTiles[TLMM_TILE_EAST],
	    fTiles[TLMM_TILE_NORTH], fTiles[TLMM_TILE_SOUTH]);
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
	return fPins[pin].owner;
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
	/* Write side of the split: an AGPIO owns every store to this pad. */
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
	case kIOReturnUnsupported:
		return TLMM_GPIO_ENODEV;
	case kIOReturnNotPermitted:
		return TLMM_GPIO_EBUSY;
	default:
		return TLMM_GPIO_EIO;
	}
}

extern "C" int
tlmm_gpio_available(void)
{
	return TLMMGPIOController::getController() ? 1 : 0;
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
