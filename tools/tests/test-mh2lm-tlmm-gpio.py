#!/usr/bin/env python3
"""Run the actual TLMM GPIO controller against a stubbed IOKit and fake MMIO.

usage: test-mh2lm-tlmm-gpio.py EXTRACTED_XNU

The real IOKit headers cannot be linked on the host, so the two driver files
are compiled with a small stand-in for OSObject/IOService/IOMemoryMap/IOLock
and four zero-filled 1 MiB tile windows. Everything under test -- tile
resolution, exclusive handles, plain-mode busy checks, CFG/IO encoding -- is
the shipped code; only the platform around it is fake. The ARM64 barriers are
removed because this host is not aarch64; the real compile check covers them.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
arm = root / 'iokit/Kernel/arm'
pex = root / 'pexpert/pexpert/arm64'

HEADER_C = (pex / 'tlmm_gpio.h').read_text()
HEADER_CPP = (arm / 'TLMMGPIOController.h').read_text()
SOURCE = (arm / 'TLMMGPIOController.cpp').read_text()


def strip_includes(text):
    return re.sub(r'^#include.*\n', '', text, flags=re.M)


def strip_barriers(text):
    return re.sub(r'__asm__ volatile\("(?:dmb|dsb)[^"]*"[^;]*;', '', text)


PRELUDE = r'''
static bool fixtureAtInterrupt = false;
struct FixturePlatform { bool atInterruptLevel() const { return fixtureAtInterrupt; } };
static FixturePlatform fixturePlatform;
static FixturePlatform *getPlatform() { return &fixturePlatform; }

#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#define ARM64_BOARD_CONFIG_MH2LM 1
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(x) public: x(); virtual ~x();
#define OSDefineMetaClassAndStructors(x, y) x::x() : y() {} x::~x() {}
#define OSDynamicCast(t, p) dynamic_cast<t *>(p)

using vm_offset_t = uintptr_t;
using vm_size_t = size_t;
#define VM_WIMG_IO 7u
using IOPhysicalAddress = uintptr_t;
using IOReturn = int;
constexpr int kIOReturnSuccess = 0;
constexpr int kIOReturnNotOpen = 1;
constexpr int kIOReturnBadArgument = 2;
constexpr int kIOReturnIOError = 3;
constexpr int kIOReturnUnsupported = 4;
constexpr int kIOReturnBusy = 5;
constexpr int kIOReturnNotPermitted = 6;
constexpr int kIOReturnNoResources = 7;
constexpr int kIOReturnNotFound = 8;

struct OSObject {
    std::atomic<unsigned> refs{1};
    OSObject() = default;
    virtual ~OSObject() {}
    virtual bool init() { return true; }
    virtual void free() {}
    void retain() { refs.fetch_add(1); }
    void release() { if (refs.fetch_sub(1) == 1) { free(); delete this; } }
};

struct OSData : OSObject {
    std::vector<uint8_t> bytes;
    size_t getLength() { return bytes.size(); }
    const void *getBytesNoCopy() { return bytes.data(); }
};

struct IOMemoryMap : OSObject {
    uintptr_t phys = 0;
    size_t length = 0;
    uint8_t *va = nullptr;
    uintptr_t getVirtualAddress() { return (uintptr_t)va; }
    uintptr_t getPhysicalAddress() { return phys; }
    uint64_t getLength() { return length; }
};

struct IOInterruptState { unsigned flags; };
struct IOSimpleLock {
    std::mutex mutex;
    unsigned nest = 0;
    void lock() { mutex.lock(); ++nest; }
    void unlock() { --nest; mutex.unlock(); }
};
static IOSimpleLock *IOSimpleLockAlloc() { return new IOSimpleLock; }
static void IOSimpleLockFree(IOSimpleLock *lock) { delete lock; }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    IOInterruptState state{0};
    if (lock) { lock->lock(); }
    return state;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock,
    IOInterruptState) {
    if (lock) { lock->unlock(); }
}

struct IOLock {
    std::recursive_mutex mutex;
};
static IOLock *IOLockAlloc() { return new IOLock; }
static void IOLockFree(IOLock *lock) { delete lock; }
static void IOLockLock(IOLock *lock) { lock->mutex.lock(); }
static void IOLockUnlock(IOLock *lock) { lock->mutex.unlock(); }

typedef void (*IOInterruptAction)(void *, void *, void *, int);

struct IOService : OSObject {
    std::vector<IOMemoryMap *> maps;
    std::vector<std::pair<std::string, OSObject *>> props;
    /* One interrupt source, like the ADT nub a driver would register on. */
    bool hasSpecifier = false;
    bool registered = false;
    bool lineEnabled = false;
    bool enabled = true;      /* GICD_CTLR.EnableGrp1 stand-in */
    int registeredSource = -1;
    void *interruptAction = nullptr;
    void *interruptTarget = nullptr;
    int registerFailures = 0;
    int registerCount = 0;
    int unregisterCount = 0;
    int disableCount = 0;
    int enableCount = 0;
    virtual bool start(IOService *) { return true; }
    virtual void stop(IOService *) {}
    virtual void free() {}
    void registerService() {}
    void setProperty(const char *, unsigned long long, int) {}
    IOReturn getResources() { return maps.empty() ? 1 : kIOReturnSuccess; }
    IOMemoryMap *mapDeviceMemoryWithIndex(unsigned index) {
        return index < maps.size() ? maps[index] : nullptr;
    }
    OSObject *getProperty(const char *key) {
        for (auto &entry : props) {
            if (entry.first == key) { return entry.second; }
        }
        return nullptr;
    }
    bool propertyExists(const char *key) { return getProperty(key) != nullptr; }

    int registerInterrupt(int source, OSObject *target, IOInterruptAction action,
        void *refCon) {
        (void)refCon;
        (void)target;
        if (!propertyExists("IOInterruptSpecifiers")) {
            return kIOReturnBadArgument;
        }
        if (registerFailures) { --registerFailures; return kIOReturnBusy; }
        registered = true;
        registeredSource = source;
        interruptAction = (void *)action;
        interruptTarget = target;
        ++registerCount;
        return kIOReturnSuccess;
    }
    int unregisterInterrupt(int source) {
        (void)source;
        registered = false;
        interruptAction = nullptr;
        ++unregisterCount;
        return kIOReturnSuccess;
    }
    int disableInterrupt(int source) {
        (void)source;
        ++disableCount;
        lineEnabled = false;
        return kIOReturnSuccess;
    }
    int enableInterrupt(int source) {
        (void)source;
        ++enableCount;
        return kIOReturnSuccess;
    }
};



static int logLevel = 0;
extern "C" int sweet_log_level_enabled(unsigned level) { return (int)level <= logLevel; }
static void IOLog(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// bsd/dev/sweet_log_level.h, whose include the fixture strips.
extern "C" int sweet_log_level_enabled(unsigned level);
#define SWEET_LOG_ERROR(...) do { printf("[ERROR] " __VA_ARGS__); } while (0)
#define SWEET_LOG_WARN(...) do { if (sweet_log_level_enabled(1u)) { printf("[WARN] " __VA_ARGS__); } } while (0)
#define SWEET_LOG_INFO(...) do { if (sweet_log_level_enabled(2u)) { printf("[INFO] " __VA_ARGS__); } } while (0)
#define SWEET_LOG_DEBUG(...) do { if (sweet_log_level_enabled(3u)) { printf("[DEBUG] " __VA_ARGS__); } } while (0)
'''

FIXTURE = r'''
// ---------------------------------------------------------------- fixture

static constexpr uint32_t kTileSize = 0x100000;
static std::vector<uint8_t> gTiles[4];
static IOMemoryMap *gMaps[4];
static IOService gProvider;

static const uintptr_t kTilePhys[4] = {
    0x03100000, 0x03500000, 0x03900000, 0x03d00000
};

/* The controller owns the summary SPI now: it toggles the distributor bit
 * itself instead of a provider round-trip per packet. The fixture models the
 * two W1S registers the way a real distributor reads back. */
static std::vector<uint8_t> gGicd;
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t physical, vm_size_t size,
    unsigned flags) {
    (void)flags;
    if (physical != 0x17a00000) { return 0; }
    gGicd.assign(size, 0);
    return (vm_offset_t)gGicd.data();
}
static uint32_t gicdWord(uint32_t off) {
    return gGicd.empty() ? 0 : *(volatile uint32_t *)(gGicd.data() + off);
}
static bool gicdLineEnabled() { return (gicdWord(0x11c) & (1u << 16)) != 0; }
static bool gicdLineMasked() { return (gicdWord(0x19c) & (1u << 16)) != 0; }

static uint32_t tileReg(unsigned tile, unsigned pin, uint32_t offset) {
    return *(volatile uint32_t *)(gTiles[tile].data() + pin * 0x1000 + offset);
}

static void setTileReg(unsigned tile, unsigned pin, uint32_t offset, uint32_t v) {
    *(volatile uint32_t *)(gTiles[tile].data() + pin * 0x1000 + offset) = v;
}

static void bringUp(bool withPinCount = true) {
    for (unsigned i = 0; i < 4; i++) {
        gTiles[i].assign(kTileSize, 0);
        gMaps[i] = new IOMemoryMap;
        gMaps[i]->phys = kTilePhys[i];
        gMaps[i]->length = kTileSize;
        gMaps[i]->va = gTiles[i].data();
        gProvider.maps.push_back(gMaps[i]);
    }
    if (withPinCount) {
        OSData *pins = new OSData;
        uint32_t value = 175;
        pins->bytes.assign((uint8_t *)&value, (uint8_t *)&value + 4);
        gProvider.props.push_back({"qcom,pin-count", pins});
    }
}

// Only the expected tile may change: this is the pin->tile check.
static void expectTile(TLMMGPIOController *controller, unsigned pin,
    unsigned expected) {
    for (unsigned tile = 0; tile < 4; tile++) {
        for (unsigned offset = 0; offset < kTileSize; offset += 4) {
            *(volatile uint32_t *)(gTiles[tile].data() + offset) = 0;
        }
    }
    assert(controller->setPinPull(pin, TLMM_GPIO_PULL_UP) == kIOReturnSuccess);
    for (unsigned tile = 0; tile < 4; tile++) {
        uint32_t cfg = tileReg(tile, pin, TLMM_GPIO_CFG);
        if (tile == expected) {
            assert(cfg == TLMM_GPIO_PULL_UP);
        } else {
            assert(cfg == 0);
        }
    }
}

static void addReadOnlyProp(const std::vector<uint32_t> &pins) {
    OSData *data = new OSData;
    data->bytes.assign((const uint8_t *)pins.data(),
        (const uint8_t *)pins.data() + pins.size() * 4);
    gProvider.props.push_back({TLMM_ADT_READ_ONLY_PROP, data});
}

static void addRawReadOnlyProp(const std::vector<uint8_t> &bytes) {
    OSData *data = new OSData;
    data->bytes = bytes;
    gProvider.props.push_back({TLMM_ADT_READ_ONLY_PROP, data});
}

// ------------------------------------------------------ pad interrupts

static unsigned gIrqHits = 0;
static unsigned gIrqLastPin = 999;
static bool gIrqDefer = false;

static AGPIO *gIrqPins[200];

static void irqHandler(unsigned int pin, void *refcon) {
    (void)refcon;
    ++gIrqHits;
    gIrqLastPin = pin;
    if (gIrqDefer) {
        /* The handler masks through the handle it belongs to, never by pin. */
        AGPIO *handle = pin < 200 ? gIrqPins[pin] : nullptr;
        assert(handle && handle->getPin() == pin);
        assert(handle->maskInterrupt() == kIOReturnSuccess);
    }
}

static void resetProvider(void) {
    gProvider.maps.clear();
    for (auto &entry : gProvider.props) {
        if (entry.second) { entry.second->release(); }
    }
    gProvider.props.clear();
    gProvider.registered = false;
    gProvider.lineEnabled = false;
    gProvider.enabled = true;
    gProvider.registeredSource = -1;
    gProvider.interruptAction = nullptr;
    gProvider.interruptTarget = nullptr;
    gProvider.registerFailures = 0;
    gProvider.registerCount = 0;
    gProvider.unregisterCount = 0;
    gProvider.disableCount = 0;
    gProvider.enableCount = 0;
}

static void addSpecifier(void) {
    OSData *data = new OSData;
    uint32_t cells[3] = {TLMM_SUMMARY_INTID, 0, 4};
    data->bytes.assign((uint8_t *)cells, (uint8_t *)cells + sizeof(cells));
    gProvider.props.push_back({"IOInterruptSpecifiers", data});
}

static void runSummaryAction() {
    assert(gProvider.interruptAction != nullptr);
    ((void (*)(void *, void *, void *, int))gProvider.interruptAction)(
        gProvider.interruptTarget, nullptr, &gProvider, 0);
}

static void interruptCases(void) {
    /* (1) No interrupt specifier on the ADT node: the claim fails and the
     *     controller leaves the GIC alone, so the caller keeps its path. */
    resetProvider();
    bringUp();
    TLMMGPIOController off;
    assert(off.init());
    assert(off.start(&gProvider));
    {
        AGPIO *pin = off.acquirePin(122, "irq-test");
        assert(pin);
        assert(pin->setInterrupt(TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) ==
            kIOReturnNoResources);
        assert(!off.summaryClaimed());
        assert(!gProvider.registered);
        pin->put();
    }
    off.stop(&gProvider);

    /* (2) Opted in: one claim, line masked until a pad is armed. The pad is
     *     claimed through its handle: no pin-number entry point exists. */
    resetProvider();
    bringUp();
    addSpecifier();
    TLMMGPIOController on;
    assert(on.init());
    assert(on.start(&gProvider));
    assert(!on.summaryClaimed());
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_CFG, TLMM_GPIO_MUX_GPIO);
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_CFG, 0);
    gIrqHits = 0;
    gIrqDefer = false;
    AGPIO *pin = on.acquirePin(122, "irq-test");
    assert(pin);
    gIrqPins[122] = pin;
    assert(pin->setInterrupt(TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) ==
        kIOReturnSuccess);
    assert(on.summaryClaimed());
    assert(gProvider.registerCount == 1 && gProvider.registered);
    assert(gProvider.registeredSource == 0);
    assert(!gicdLineEnabled() && gicdLineMasked());  /* masked at claim */
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_CFG) ==
        TLMM_INTR_ROUTE_TARGET | TLMM_INTR_ROUTE_RAW);
    assert(!pin->interruptEnabled());

    // Preparing at arm time owns trigger writes without changing claim time.
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    assert(pin->prepareInterrupt() == kIOReturnSuccess);
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_CFG) == 0x70u);
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS) == 0);
    assert(!gicdLineEnabled());
    fixtureAtInterrupt = true;
    assert(pin->prepareInterrupt() == kIOReturnNotPermitted);
    fixtureAtInterrupt = false;
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_CFG, TLMM_GPIO_EGPIO_ENABLE);
    assert(pin->prepareInterrupt() == kIOReturnUnsupported);
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    assert(pin->maskInterrupt() == kIOReturnUnsupported);
    assert(pin->resumeInterrupt() == kIOReturnUnsupported);
    assert(pin->ackInterrupt() == kIOReturnUnsupported);
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_CFG) == 0x70u);
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS) == TLMM_INTR_STATUS_RAW);
    assert(!gicdLineEnabled());
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_CFG, TLMM_GPIO_MUX_GPIO);

    // Only the summary owner may write pending, on behalf of the sole pad.
    assert(pin->setProbePending(false) == kIOReturnNotPermitted);
    fixtureAtInterrupt = true;
    assert(pin->setProbePending(true) == kIOReturnNotPermitted);
    fixtureAtInterrupt = false;
    assert(pin->setProbePending(true) == kIOReturnSuccess);
    assert(gicdWord(0x21c) == (1u << 16));
    assert(pin->setProbePending(true) == kIOReturnBusy);
    AGPIO *probeOther = on.acquirePin(98, "probe-other");
    assert(probeOther);
    uint32_t untouched98 = tileReg(TLMM_TILE_SOUTH, 98, TLMM_GPIO_INTR_CFG);
    assert(probeOther->setInterrupt(TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) == kIOReturnBusy);
    assert(tileReg(TLMM_TILE_SOUTH, 98, TLMM_GPIO_INTR_CFG) == untouched98);
    assert(pin->setProbePending(false) == kIOReturnSuccess);
    assert(gicdWord(0x29c) == (1u << 16));
    probeOther->put();

    /* (3) The handle arms the pad: the distributor bit is released, and the
     *     alias with the provider is over -- one enable at claim, none after. */
    assert(pin->resumeInterrupt() == kIOReturnSuccess);
    assert(gicdLineEnabled() && !gicdLineMasked());
    assert(pin->interruptEnabled());
    assert(pin->prepareInterrupt() == kIOReturnBusy); // no reconfiguration live
    assert(gProvider.enableCount == 1);
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    runSummaryAction();
    assert(gIrqHits == 1 && gIrqLastPin == 122);
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS) == 0);
    assert(gicdLineEnabled());
    assert(gProvider.enableCount == 1 && gProvider.disableCount == 0);

    /* (4) A pad with no status must not call its handler. */
    runSummaryAction();
    assert(gIrqHits == 1);
    assert(gicdLineEnabled());

    /* (5) Deferred bottom half: the handler masks through its own handle and
     *     the pad status survives for the worker that will read it. */
    gIrqDefer = true;
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    runSummaryAction();
    assert(gIrqHits == 2);
    assert(!gicdLineEnabled());
    assert(!pin->interruptEnabled());
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS) ==
        TLMM_INTR_STATUS_RAW);
    assert(pin->resumeInterrupt() == kIOReturnSuccess);
    assert(gicdLineEnabled() && pin->interruptEnabled());
    assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS) == 0);
          /* resume acks the stale latch before re-arming */
    gIrqDefer = false;

    /* (6) Two pads on the same line: the deferred pad keeps it masked until
     *     the second one resumes. A pad that never armed stays quiet: its
     *     status is not dispatched while its owner holds the mask. */
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_INTR_CFG, 0);
    AGPIO *pin100 = on.acquirePin(100, "irq-test-100");
    assert(pin100);
    gIrqPins[100] = pin100;
    assert(pin100->setInterrupt(TLMM_IRQ_EDGE_RISING, irqHandler, nullptr) ==
        kIOReturnSuccess);
    gIrqDefer = true;
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    runSummaryAction();
    assert(gIrqHits == 2);              /* the never-armed pad stays quiet */
    assert(tileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_INTR_STATUS) ==
        TLMM_INTR_STATUS_RAW);
    assert(pin100->resumeInterrupt() == kIOReturnSuccess);
    assert(pin->setProbePending(true) == kIOReturnBusy); // shared line, no injection
    assert(pin100->setProbePending(true) == kIOReturnBusy);
    assert(gicdLineEnabled());
    setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    runSummaryAction();
    assert(gIrqHits == 4);              /* both pads ran */
    assert(!gicdLineEnabled());         /* both deferred */
    assert(pin->resumeInterrupt() == kIOReturnSuccess);
    assert(!gicdLineEnabled());         /* pad 100 still holds the line */
    assert(pin100->resumeInterrupt() == kIOReturnSuccess);
    assert(gicdLineEnabled());
    gIrqDefer = false;

    /* (7) Ownership rules: a held pad and a second request on the same pad are
     *     refused, and the interrupt dies with its handle. */
    assert(on.acquirePin(122, "test") == nullptr);
    assert(on.requestPinIRQ(nullptr, TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) ==
        kIOReturnBadArgument);
    AGPIO *pin96 = on.acquirePin(96, "irq-test-96");
    assert(pin96);
    assert(pin96->setInterrupt(TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) ==
        kIOReturnSuccess);
    assert(pin96->resumeInterrupt() == kIOReturnSuccess);
    assert(pin96->interruptEnabled());
    setTileReg(TLMM_TILE_NORTH, 96, TLMM_GPIO_INTR_STATUS, TLMM_INTR_STATUS_RAW);
    assert(on.releasePin(96, "irq-test-96") == kIOReturnBusy);
    assert(pin96->isValid());
    assert(on.acquirePin(96, "new-owner") == nullptr);
    assert(pin96->disableInterrupt() == kIOReturnSuccess);
    assert(pin96->prepareInterrupt() == kIOReturnNotFound);
    assert(!pin96->interruptEnabled());
    assert((tileReg(TLMM_TILE_NORTH, 96, TLMM_GPIO_INTR_CFG) & TLMM_INTR_ENABLE) == 0);
    assert(tileReg(TLMM_TILE_NORTH, 96, TLMM_GPIO_INTR_STATUS) == TLMM_INTR_STATUS_RAW);
    unsigned beforeFreeDispatch = gIrqHits;
    runSummaryAction();
    assert(gIrqHits == beforeFreeDispatch); /* removed handler cannot run */
    pin96->put();
    AGPIO *new96 = on.acquirePin(96, "new-owner");
    assert(new96 && new96->isValid());
    new96->put();
    assert(on.maskPinIRQ(7) == kIOReturnNotFound);

    /* Dropping a handle retires its live interrupt before another owner can
     * acquire the pad, even if the caller forgot disableInterrupt(). */
    AGPIO *droppedIRQ = on.acquirePin(99, "dropped-irq");
    assert(droppedIRQ);
    assert(droppedIRQ->setInterrupt(TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) ==
        kIOReturnSuccess);
    assert(droppedIRQ->resumeInterrupt() == kIOReturnSuccess);
    droppedIRQ->put();
    assert((tileReg(TLMM_TILE_SOUTH, 99, TLMM_GPIO_INTR_CFG) & TLMM_INTR_ENABLE) == 0);
    AGPIO *new99 = on.acquirePin(99, "replacement-irq");
    assert(new99);
    assert(new99->setInterrupt(TLMM_IRQ_LEVEL_LOW, irqHandler, nullptr) ==
        kIOReturnSuccess);
    assert(new99->disableInterrupt() == kIOReturnSuccess);
    new99->put();

    assert(pin->disableInterrupt() == kIOReturnSuccess);
    pin->put();
    assert(pin100->disableInterrupt() == kIOReturnSuccess);
    pin100->put();
    gIrqPins[122] = gIrqPins[100] = nullptr;
    assert(!gProvider.registered);      /* last free released the summary */
    assert(gProvider.unregisterCount == 1);
    on.stop(&gProvider);

    // Trigger encodings are hardware expectations, separate from the worker.
    const uint32_t expected[] = {0x72u, 0x70u, 0x76u, 0x74u, 0x7cu};
    for (unsigned detect = 0; detect <= TLMM_IRQ_EDGE_BOTH; ++detect) {
        resetProvider();bringUp();addSpecifier();
        TLMMGPIOController prepared;
        assert(prepared.init() && prepared.start(&gProvider));
        AGPIO *p = prepared.acquirePin(122, "prepare-test");
        assert(p && p->prepareInterrupt() == kIOReturnNotFound);
        assert(p->setInterrupt(detect, irqHandler, nullptr) == kIOReturnSuccess);
        setTileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_CFG, 0xab00u | 0x72u);
        assert(p->prepareInterrupt() == kIOReturnSuccess);
        assert(tileReg(TLMM_TILE_SOUTH, 122, TLMM_GPIO_INTR_CFG) == (0xab00u | expected[detect]));
        assert(!gicdLineEnabled());
        assert(p->setProbePending(true) == kIOReturnSuccess);
        p->put(); // cancel reservation through unregister, with no live callback
        assert(gicdWord(0x29c) == (1u << 16));
        prepared.stop(&gProvider);
    }
}

static void islandCases(void) {
    const uint32_t declared[] = {96, 100};

    /* (1) ADT-declared: refused from the first access, before any EGPIO bit. */
    bringUp();
    addReadOnlyProp({declared[0], declared[1]});
    TLMMGPIOController declaredController;
    assert(declaredController.init());
    assert(declaredController.start(&gProvider));
    assert(declaredController.isPinReadOnly(96));
    assert(declaredController.isPinReadOnly(100));
    assert(!declaredController.isPinReadOnly(101));
    assert(declaredController.acquirePin(96, "panel") == nullptr);
    assert(strcmp(declaredController.getPinOwner(96), "island") == 0);
    assert(declaredController.getPinOwner(54) == nullptr);
    assert(declaredController.setPinValue(96, true) == kIOReturnUnsupported);
    assert(declaredController.setPinDirection(96, true) == kIOReturnUnsupported);
    assert(declaredController.setPinMux(96, 1) == kIOReturnUnsupported);
    assert(declaredController.setPinPull(96, TLMM_GPIO_PULL_UP) == kIOReturnUnsupported);
    assert(declaredController.setPinDrive(96, TLMM_GPIO_DRIVE_4MA) == kIOReturnUnsupported);
    assert(declaredController.setPinConfig(96, 0, 0, TLMM_GPIO_DRIVE_4MA, 1, 1) ==
        kIOReturnUnsupported);
    assert(gpio_set_value(96, 1) == TLMM_GPIO_EOPNOTSUPP);
    assert(gpio_set_config(96, 0, 0, 0, 1, 1) == TLMM_GPIO_EOPNOTSUPP);
    assert(gpio_is_read_only(96) == 1);
    assert(gpio_is_read_only(54) == 0);
    /* Reads keep working and report the untouched hardware state. GPIO100 is
     * in SOUTH; GPIO96 is declared only, so its tile is not touched here. */
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_CFG,
        TLMM_GPIO_PULL_KEEPER | TLMM_GPIO_MUX_GPIO | TLMM_GPIO_EGPIO_ENABLE);
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_IO, TLMM_GPIO_IN);
    struct tlmm_pin_config seen = {};
    int high = 0;
    assert(gpio_get_config(100, &seen) == TLMM_GPIO_OK);
    assert(seen.pull == TLMM_GPIO_PULL_KEEPER && seen.output == 0);
    assert(seen.cfg == tileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_CFG));
    assert(gpio_get_value(100, &high) == TLMM_GPIO_OK && high == 1);
    /* Nothing was written by any of the refused calls. */
    assert(tileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_CFG) ==
        (TLMM_GPIO_PULL_KEEPER | TLMM_GPIO_MUX_GPIO | TLMM_GPIO_EGPIO_ENABLE));
    assert(tileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_IO) == TLMM_GPIO_IN);
    assert(declaredController.acquirePin(100, "panel") == nullptr);
    assert(declaredController.setPinConfig(100, 0, 0, TLMM_GPIO_DRIVE_2MA, 1, 0) ==
        kIOReturnUnsupported);

    /* (2) Live EGPIO enable: firmware claims a pad after we started. */
    assert(declaredController.acquirePin(54, "touch") != nullptr);
    declaredController.releasePin(54, "touch");
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG, TLMM_GPIO_EGPIO_ENABLE);
    assert(declaredController.isPinReadOnly(54));
    assert(declaredController.acquirePin(54, "touch") == nullptr);
    assert(declaredController.setPinValue(54, true) == kIOReturnUnsupported);
    assert(gpio_set_direction(54, 1) == TLMM_GPIO_EOPNOTSUPP);
    assert(tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG) == TLMM_GPIO_EGPIO_ENABLE);
    /* Cleared by firmware: the pad becomes controllable again. */
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG, 0);
    assert(!declaredController.isPinReadOnly(54));
    assert(declaredController.acquirePin(54, "touch") != nullptr);
    declaredController.releasePin(54, "touch");

    /* (3) A malformed property is a no-op, not a crash and not a guess. This
     * needs its own provider: getProperty() returns the first match, so the
     * valid list above would shadow it. */
    declaredController.stop(&gProvider);
    /* Clear the live EGPIO bit case (1) left behind, so the assertions below
     * test the malformed list alone. */
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_CFG, 0);
    setTileReg(TLMM_TILE_SOUTH, 100, TLMM_GPIO_IO, 0);
    IOService badProvider;
    for (unsigned i = 0; i < 4; i++) {
        badProvider.maps.push_back(gMaps[i]);
    }
    OSData *pinCount = new OSData;
    uint32_t pins175 = 175;
    pinCount->bytes.assign((uint8_t *)&pins175, (uint8_t *)&pins175 + 4);
    badProvider.props.push_back({"qcom,pin-count", pinCount});
    OSData *malformed = new OSData;
    malformed->bytes = {0x60, 0x00, 0x00, 0x00, 0x64, 0x00};    /* 6 bytes */
    badProvider.props.push_back({TLMM_ADT_READ_ONLY_PROP, malformed});
    TLMMGPIOController malformedController;
    assert(malformedController.init());
    assert(malformedController.start(&badProvider));
    assert(!malformedController.isPinReadOnly(96));
    assert(!malformedController.isPinReadOnly(100));
    assert(malformedController.acquirePin(96, "panel") != nullptr);
    malformedController.releasePin(96, "panel");
    malformedController.stop(&badProvider);
}

int main(void) {
    // No controller yet: the C API must stay inert.
    assert(tlmm_gpio_available() == 0);
    assert(gpio_set_value(1, 1) == TLMM_GPIO_ENODEV);
    assert(gpio_get_value(1, nullptr) == TLMM_GPIO_ENODEV);
    struct tlmm_pin_config unusedConfig;
    assert(gpio_get_config(1, &unusedConfig) == TLMM_GPIO_ENODEV);

    bringUp();
    TLMMGPIOController controller;
    assert(controller.init());
    assert(controller.start(&gProvider));
    assert(tlmm_gpio_available() == 1);
    assert(TLMMGPIOController::getController() == &controller);
    assert(controller.getPinCount() == 175);

    // Sparse tile map, straight from pinctrl-sm8150.c.
    expectTile(&controller, 0, TLMM_TILE_SOUTH);
    expectTile(&controller, 7, TLMM_TILE_SOUTH);
    expectTile(&controller, 8, TLMM_TILE_NORTH);
    expectTile(&controller, 20, TLMM_TILE_NORTH);
    expectTile(&controller, 21, TLMM_TILE_EAST);
    expectTile(&controller, 30, TLMM_TILE_EAST);
    expectTile(&controller, 31, TLMM_TILE_NORTH);
    expectTile(&controller, 38, TLMM_TILE_SOUTH);
    expectTile(&controller, 43, TLMM_TILE_EAST);
    expectTile(&controller, 50, TLMM_TILE_EAST);
    expectTile(&controller, 51, TLMM_TILE_SOUTH);
    expectTile(&controller, 54, TLMM_TILE_SOUTH);
    expectTile(&controller, 87, TLMM_TILE_EAST);
    expectTile(&controller, 100, TLMM_TILE_SOUTH);
    expectTile(&controller, 105, TLMM_TILE_WEST);
    expectTile(&controller, 122, TLMM_TILE_SOUTH);
    expectTile(&controller, 154, TLMM_TILE_SOUTH);
    expectTile(&controller, 168, TLMM_TILE_WEST);
    expectTile(&controller, 169, TLMM_TILE_NORTH);
    expectTile(&controller, 174, TLMM_TILE_NORTH);

    // GPIO122 lives at SOUTH + 0x7a000, the address the touch driver uses.
    assert((uintptr_t)gTiles[TLMM_TILE_SOUTH].data() + 122 * 0x1000 + 0x8 ==
           (uintptr_t)gTiles[TLMM_TILE_SOUTH].data() + 0x7a008);

    // Exclusive write, shared read. While the handle lives no other driver
    // can acquire the pad and every plain-mode writer is refused, but both
    // plain-mode readers keep working: that is how a monitor cross-checks an
    // owner's programming.
    AGPIO *touch = controller.acquirePin(54, "touch");
    assert(touch && touch->isValid());
    assert(touch->getPin() == 54);
    assert(strcmp(touch->getOwner(), "touch") == 0);
    assert(controller.acquirePin(54, "other") == nullptr);
    assert(controller.setPinValue(54, 1) == kIOReturnBusy);
    assert(controller.setPinDirection(54, true) == kIOReturnBusy);
    assert(controller.setPinMux(54, TLMM_GPIO_MUX_GPIO) == kIOReturnBusy);
    assert(controller.setPinPull(54, TLMM_GPIO_PULL_UP) == kIOReturnBusy);
    assert(controller.setPinDrive(54, TLMM_GPIO_DRIVE_8MA) == kIOReturnBusy);
    assert(controller.setPinConfig(54, TLMM_GPIO_MUX_GPIO, TLMM_GPIO_PULL_UP,
        TLMM_GPIO_DRIVE_8MA, true, true) == kIOReturnBusy);
    assert(gpio_set_value(54, 1) == TLMM_GPIO_EBUSY);
    assert(gpio_set_config(54, 0, 0, 0, 0, 0) == TLMM_GPIO_EBUSY);
    assert(controller.getPinOwner(54) != nullptr);

    // configure(): latches the level before enabling the driver.
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG, 0);
    assert(touch->configure(TLMM_GPIO_MUX_GPIO, TLMM_GPIO_PULL_UP,
        TLMM_GPIO_DRIVE_8MA, true, true) == kIOReturnSuccess);
    uint32_t cfg = tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG);
    uint32_t io = tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO);
    assert(cfg == (TLMM_GPIO_PULL_UP | TLMM_GPIO_OE |
        (TLMM_GPIO_DRIVE_8MA << TLMM_GPIO_DRIVE_SHIFT)));
    assert((io & TLMM_GPIO_OUT) != 0);

    // Field updates keep the rest of CFG.
    assert(touch->setMux(3) == kIOReturnSuccess);
    assert(touch->setPull(TLMM_GPIO_PULL_DOWN) == kIOReturnSuccess);
    assert(touch->setDrive(TLMM_GPIO_DRIVE_2MA) == kIOReturnSuccess);
    cfg = tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG);
    assert(((cfg >> TLMM_GPIO_MUX_SHIFT) & TLMM_GPIO_MUX_MASK) == 3);
    assert((cfg & TLMM_GPIO_PULL_MASK) == TLMM_GPIO_PULL_DOWN);
    assert(((cfg >> TLMM_GPIO_DRIVE_SHIFT) & TLMM_GPIO_DRIVE_MASK) ==
        TLMM_GPIO_DRIVE_2MA);
    assert((cfg & TLMM_GPIO_OE) != 0);
    assert(touch->setMux(8) == kIOReturnBadArgument);
    assert(touch->setPull(4) == kIOReturnBadArgument);
    assert(touch->setDrive(8) == kIOReturnBadArgument);

    // setValue/getValue are the pad latch and the pad level.
    assert(touch->setValue(false) == kIOReturnSuccess);
    assert((tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO) & TLMM_GPIO_OUT) == 0);
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO, TLMM_GPIO_IN);
    assert(touch->getValue());
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO, 0);
    assert(!touch->getValue());

    // The shared-read side sees exactly what the handle programmed, while the
    // write side stays refused for as long as the handle exists.
    struct tlmm_pin_config seen = {};
    bool seenHigh = false;

    assert(controller.getPinConfig(54, &seen) == kIOReturnSuccess);
    assert(seen.func == 3 && seen.pull == TLMM_GPIO_PULL_DOWN &&
        seen.drive == TLMM_GPIO_DRIVE_2MA && seen.output == 1);
    assert(seen.cfg == tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG));
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO, TLMM_GPIO_IN);
    assert(controller.getPinValue(54, &seenHigh) == kIOReturnSuccess && seenHigh);
    int seenInt = 0;
    assert(gpio_get_value(54, &seenInt) == TLMM_GPIO_OK && seenInt == 1);
    assert(gpio_get_config(54, &seen) == TLMM_GPIO_OK &&
        seen.func == 3 && seen.output == 1);
    assert(gpio_set_direction(54, 0) == TLMM_GPIO_EBUSY);

    // Direction toggles only bit 9.
    assert(touch->setDirection(false) == kIOReturnSuccess);
    assert((tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG) & TLMM_GPIO_OE) == 0);
    assert(touch->setDirection(true) == kIOReturnSuccess);
    assert((tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG) & TLMM_GPIO_OE) != 0);

    // Owner check, then a plain-mode window.
    assert(controller.releasePin(54, "impostor") == kIOReturnNotPermitted);
    touch->release();
    assert(!touch->isValid());
    assert(controller.getPinOwner(54) == nullptr);
    touch->release();                          // idempotent
    assert(gpio_set_config(54, TLMM_GPIO_MUX_GPIO, TLMM_GPIO_PULL_NONE,
        TLMM_GPIO_DRIVE_8MA, 1, 0) == TLMM_GPIO_OK);
    cfg = tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_CFG);
    assert((cfg & TLMM_GPIO_OE) != 0);
    assert((tileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO) & TLMM_GPIO_OUT) == 0);

    // Plain-mode getters and range checks.
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO, TLMM_GPIO_IN | TLMM_GPIO_OUT);
    int high = -1;
    assert(gpio_get_value(54, &high) == TLMM_GPIO_OK && high == 1);
    setTileReg(TLMM_TILE_SOUTH, 54, TLMM_GPIO_IO, 0);
    assert(gpio_get_value(54, &high) == TLMM_GPIO_OK && high == 0);
    struct tlmm_pin_config config;
    assert(gpio_get_config(54, &config) == TLMM_GPIO_OK);
    assert(config.func == TLMM_GPIO_MUX_GPIO && config.pull == TLMM_GPIO_PULL_NONE);
    assert(config.drive == TLMM_GPIO_DRIVE_8MA && config.output == 1 && config.high == 0);
    assert(gpio_set_value(175, 1) == TLMM_GPIO_EINVAL);
    assert(gpio_set_mux(1, 8) == TLMM_GPIO_EINVAL);
    assert(gpio_set_pull(1, 4) == TLMM_GPIO_EINVAL);
    assert(gpio_set_drive(1, 8) == TLMM_GPIO_EINVAL);
    assert(gpio_get_value(1, nullptr) == TLMM_GPIO_EINVAL);
    assert(gpio_get_config(1, nullptr) == TLMM_GPIO_EINVAL);

    // A dropped handle returns its pad through free().
    AGPIO *dropped = controller.acquirePin(122, "irq");
    assert(dropped != nullptr);
    dropped->release();
    assert(gpio_set_pull(122, TLMM_GPIO_PULL_UP) == TLMM_GPIO_OK);
    AGPIO *leaked = controller.acquirePin(122, "leak");
    assert(leaked != nullptr);
    leaked->release();                         // release, then free() is inert

    // A handle held across stop() is invalidated, not left pointing at the
    // windows that are about to be unmapped.
    AGPIO *survivor = controller.acquirePin(7, "late");
    assert(survivor != nullptr);
    assert(survivor->setPull(TLMM_GPIO_PULL_UP) == kIOReturnSuccess);

    // stop() clears the singleton, the C API goes inert again.
    controller.stop(&gProvider);
    assert(!survivor->isValid());
    assert(survivor->setValue(true) == kIOReturnNotOpen);
    survivor->release();                       // inert after invalidation
    assert(TLMMGPIOController::getController() == nullptr);
    assert(tlmm_gpio_available() == 0);
    assert(gpio_set_value(1, 1) == TLMM_GPIO_ENODEV);
    assert(gpio_is_read_only(1) == 0);

    /* Island-owned pads: readable, never controllable. Two sources, and the
     * answer must be the same for both. */
    islandCases();
    interruptCases();

    printf("PASS: TLMM GPIO controller (tile map, exclusive AGPIO, plain mode, "
        "write-exclusive/read-shared, island read-only, pad interrupts, C API)\n");
    return 0;
}
'''


def main():
    shipped = (Path(__file__).parent.parent.parent /
               'dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp').read_text()
    # The host fixture defines VM_WIMG_IO itself, so a source that forgot the
    # kernel header compiled here and failed on CI. Check the include instead.
    assert '#include <vm/memory_types.h>' in shipped
    # The host platform stub must not hide an incomplete kernel platform type.
    assert '#include <IOKit/IOPlatformExpert.h>' in shipped
    # machine/machine_routines.h does not export this KPI, so the source has
    # to declare it itself -- exactly what the SPMI mux does.
    assert 'extern "C" vm_offset_t ml_io_map_unmappable' in shipped
    source = (PRELUDE +
              'extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned);\n' +
              strip_includes(HEADER_C) + strip_includes(HEADER_CPP) +
              strip_barriers(strip_includes(SOURCE)) + FIXTURE)
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'test.cpp'
        binary = Path(tmp) / 'test'
        src.write_text(source)
        build = subprocess.run(
            ['clang++-19', '-std=c++17', '-O0', '-g', '-w',
             '-fno-omit-frame-pointer', str(src), '-o', str(binary)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if build.returncode:
            print(build.stdout)
            return 1
        run = subprocess.run([str(binary)], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(run.stdout, end='')
        return run.returncode


if __name__ == '__main__':
    sys.exit(main())