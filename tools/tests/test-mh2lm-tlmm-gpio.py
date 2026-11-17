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
using IOPhysicalAddress = uintptr_t;
using IOReturn = int;
constexpr int kIOReturnSuccess = 0;
constexpr int kIOReturnNotOpen = 1;
constexpr int kIOReturnBadArgument = 2;
constexpr int kIOReturnIOError = 3;
constexpr int kIOReturnUnsupported = 4;
constexpr int kIOReturnBusy = 5;
constexpr int kIOReturnNotPermitted = 6;

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

struct IOLock {
    std::recursive_mutex mutex;
};
static IOLock *IOLockAlloc() { return new IOLock; }
static void IOLockFree(IOLock *lock) { delete lock; }
static void IOLockLock(IOLock *lock) { lock->mutex.lock(); }
static void IOLockUnlock(IOLock *lock) { lock->mutex.unlock(); }

struct IOService : OSObject {
    std::vector<IOMemoryMap *> maps;
    std::vector<std::pair<std::string, OSObject *>> props;
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

    printf("PASS: TLMM GPIO controller (tile map, exclusive AGPIO, plain mode, C API)\n");
    return 0;
}
'''


def main():
    source = (PRELUDE + strip_includes(HEADER_C) + strip_includes(HEADER_CPP) +
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