"""Shared host PMIC/GIC and scheduling fixtures; not a hardware emulator."""
prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <algorithm>
#include <string>
#define ARM64_BOARD_CONFIG_MH2LM 1
#define SWEET_DEV_SPMI_CORE_PA 0x0c440000u
#define SWEET_DEV_SPMI_CFG_PA 0x0c40a000u
#define SWEET_DEV_SPMI_OBS_PA 0x0e600000u
#define SWEET_DEV_SPMI_CHNLS_PA 0x0c600000u
#define PAGE_MASK 0x3fffu
#define trunc_page(x) ((x)&~uintptr_t(PAGE_MASK))
#define round_page(x) (((x)+PAGE_MASK)&~uintptr_t(PAGE_MASK))
#define VM_WIMG_IO 7u
using vm_offset_t = uintptr_t;
using vm_size_t = size_t;
using IOReturn = int;
static constexpr int kIOReturnSuccess=0, kIOReturnNotReady=1,
    kIOReturnNotPermitted=2, kIOReturnIOError=3, kIOReturnBadArgument=4;
using IOLock = std::mutex;
static IOLock *IOLockAlloc() { return new IOLock; }
static void IOLockFree(IOLock *p) { delete p; }
static void IOLockLock(IOLock *p) { p->lock(); }
static void IOLockUnlock(IOLock *p) { p->unlock(); }
#define OSDeclareDefaultStructors(name)
#define OSDefineMetaClassAndStructors(name, base)
#define APPLE_KEXT_OVERRIDE override
struct OSObject {
    std::atomic<unsigned> refs{1};
    virtual ~OSObject() = default;
    virtual void free() {}
    void retain() { assert(refs.fetch_add(1)>0); }
    void release() { if (refs.fetch_sub(1)==1) { free(); delete this; } }
};
struct OSDictionary {};
using IOSimpleLock = std::mutex;
using IOInterruptState = bool;
static IOSimpleLock *IOSimpleLockAlloc() { return new IOSimpleLock; }
static void IOSimpleLockFree(IOSimpleLock *p) { delete p; }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p) { p->lock(); return false; }
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p, IOInterruptState) { p->unlock(); }
#define OSDynamicCast(type, object) static_cast<type *>(object)
struct IOInterruptEventSource;
static unsigned testLoopFail, testAddFail;
struct IOWorkLoop : OSObject {
    std::recursive_mutex gate;
    using Action = IOReturn (*)(OSObject *, void *, void *, void *, void *);
    static IOWorkLoop *workLoop() {
        if (testLoopFail && --testLoopFail==0) return nullptr;
        return new IOWorkLoop;
    }
    int addEventSource(IOInterruptEventSource *);
    void removeEventSource(IOInterruptEventSource *);
    int runAction(Action a, OSObject *o, void *x=nullptr, void *y=nullptr) {
        std::lock_guard<std::recursive_mutex> lock(gate);
        return a(o,x,y,nullptr,nullptr);
    }
};
static std::vector<IOInterruptEventSource *> queue;
static std::mutex queueLock;
struct IOInterruptEventSource : OSObject {
    using Action = void (*)(OSObject *, IOInterruptEventSource *, int);
    OSObject *owner; Action action;
    IOWorkLoop *loop=nullptr;
    std::atomic<bool> enabled{true};
    IOInterruptEventSource(OSObject *o, Action a) : owner(o), action(a) {}
    static IOInterruptEventSource *interruptEventSource(OSObject *o, Action a, void *, int) {
        return new IOInterruptEventSource(o,a);
    }
    void interruptOccurred(void *, void *, int) {
        std::lock_guard<std::mutex> lock(queueLock); queue.push_back(this);
    }
    void disable() { enabled=false; }
};
int IOWorkLoop::addEventSource(IOInterruptEventSource *e) {
    if (testAddFail && --testAddFail==0) return -1;
    std::lock_guard<std::mutex> lock(queueLock); e->loop=this; return 0;
}
void IOWorkLoop::removeEventSource(IOInterruptEventSource *e) {
    std::lock_guard<std::mutex> lock(queueLock); e->loop=nullptr;
    queue.erase(std::remove(queue.begin(),queue.end(),e),queue.end());
}
static void pump() {
    for (;;) {
        IOInterruptEventSource *e; IOWorkLoop *loop;
        {
            std::lock_guard<std::mutex> lock(queueLock);
            if (queue.empty()) return;
            e=queue.front(); queue.erase(queue.begin()); e->retain();
            loop=e->loop; if (loop) loop->retain();
        }
        if (loop) {
            {
                std::lock_guard<std::recursive_mutex> lock(loop->gate);
                if (e->enabled && e->loop==loop) e->action(e->owner,e,1);
            }
            loop->release();
        } else if (e->enabled) {
            e->action(e->owner,e,1); // legacy battery adapter has no loop binding
        }
        e->release();
    }
}
struct IOPlatformExpert;
static constexpr unsigned kIOServiceSynchronous=2;
static bool testMatchDriver=true, testInitFail=false;
static unsigned testMapFail, testStops, testFrees;
static std::map<uintptr_t,unsigned> mappings;
static std::map<uintptr_t,size_t> mappingSizes;
struct IOService : OSObject {
    IOService *parent=nullptr;
    std::vector<IOService *> children;
    std::map<std::string,std::string> properties;
    bool mockStarted=false;
    virtual bool init(OSDictionary * = nullptr) { return !testInitFail; }
    virtual bool start(IOService *) { return true; }
    virtual void stop(IOService *) {}
    virtual IOWorkLoop *getWorkLoop() const { return nullptr; }
    void free() override { ++testFrees; }
    bool attach(IOService *p) { parent=p; p->children.push_back(this); retain(); return true; }
    void registerService(unsigned=0);
    static IOPlatformExpert *getPlatform();
    void setName(const char *) {}
    bool setProperty(const char *k,const char *v) { properties[k]=v; return true; }
    bool setProperty(const char *k,bool v) { properties[k]=v?"true":"false"; return true; }
    bool setProperty(const char *k,uint64_t v,unsigned) { properties[k]=std::to_string(v); return true; }
    bool terminate(unsigned) {
        retain();
        while (!children.empty()) children.back()->terminate(2);
        if (mockStarted) { ++testStops; stop(parent); mockStarted=false; }
        if (parent) {
            auto &v=parent->children;
            v.erase(std::remove(v.begin(),v.end(),this),v.end());
            parent=nullptr; release();
        }
        release(); return true;
    }
};
struct IOPlatformExpert : IOService {};
static IOPlatformExpert platform;
IOPlatformExpert *IOService::getPlatform() { return &platform; }
#define SWEET_LOG_INFO(...) ((void)0)
#define SWEET_LOG_DEBUG(...) ((void)0)
#define SWEET_LOG_WARN(...) ((void)0)
#define SWEET_LOG_ERROR(...) ((void)0)
static uint64_t fakeNs=1000000000ULL;
static void clock_get_uptime(uint64_t *v) { *v=fakeNs; }
static void absolutetime_to_nanoseconds(uint64_t v, uint64_t *n) { *n=v; }
static void delay(unsigned) {}
static std::map<uintptr_t,uint32_t> mmio;
static std::recursive_mutex hwLock;
static std::map<unsigned,uint8_t> pmic;
static std::map<unsigned,unsigned> ppids;
static unsigned keyEvents, keyFailures, batteryEvents, batteryFailures;
static unsigned lockDepth, lockedWrites, failReadPer, timeoutApid;
static bool dropPdcEnable;
#ifdef SWEET_MUX_SERVICE_TEST
static bool hasIrqOwner();
#endif
extern "C" bool sweet_spmi_mux_irq(unsigned);
extern "C" void sweet_keys_irq_notify(unsigned) { ++keyEvents; }
extern "C" void sweet_keys_irq_failed() { ++keyFailures; }
extern "C" vm_offset_t ml_io_map(vm_offset_t a, vm_size_t) {
    if (testMapFail && --testMapFail==0) return 0;
    ++mappings[a]; return a;
}
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t a, vm_size_t n, unsigned flags) {
    assert(flags==VM_WIMG_IO);
    if (testMapFail && --testMapFail==0) return 0;
    ++mappings[trunc_page(a)]; mappingSizes[trunc_page(a)]=round_page(n+(a&PAGE_MASK));
    return a;
}
extern "C" void ml_io_unmap(vm_offset_t a, vm_size_t n) {
    assert(!(a&PAGE_MASK) && !(n&PAGE_MASK) && n==mappingSizes.at(a));
    auto p=mappings.find(a); assert(p!=mappings.end() && p->second);
    if (!--p->second) mappings.erase(p);
}
extern "C" void sweet_spmi_irq_lock() { assert(lockDepth++ == 0); }
extern "C" void sweet_spmi_irq_unlock() { assert(--lockDepth == 0); }
static bool rwLanePoison[512];
extern "C" int sweet_spmi_rw_poisoned_locked(unsigned apid) {
    assert(lockDepth==1);return apid>=512 || rwLanePoison[apid];
}
extern "C" void sweet_spmi_rw_poison_locked(unsigned apid) {
    assert(lockDepth==1 && apid<512);rwLanePoison[apid]=true;
}
extern "C" int sweet_spmi_read(unsigned sid, unsigned a, uint8_t *v) {
    if ((a >> 8) == 0x15) return -1; // production legacy API rejects Type-C
    if ((a >> 8) == failReadPer) return -1;
    assert(lockDepth == 0); // the API itself acquires the observer lock
    *v=pmic[(sid<<16)|a]; return 0;
}
extern "C" int sweet_spmi_battery_irq_read(unsigned peripheral, unsigned reg, uint8_t *v) {
    if (!v || (peripheral!=0x10 && peripheral!=0x13 && peripheral!=0x15 && peripheral!=0x16 && peripheral!=0x40) ||
        (reg!=0x11 && reg!=0x12 && reg!=0x13 && reg!=0x15)) return -1;
    assert(lockDepth==0);
    if (peripheral==failReadPer) return -1;
    *v=pmic[(2u<<16)|(peripheral<<8)|reg];return 0;
}
static void batteryNotify(void *, unsigned event) {
    if (event == 0x80000000u) ++batteryFailures; else batteryEvents |= event;
}
static void keyNotify(void *, unsigned event) {
    if (event == 0x80000000u) ++keyFailures; else ++keyEvents;
}
static uint32_t hwRead(uintptr_t b,unsigned off) {
    std::lock_guard<std::recursive_mutex> lock(hwLock); return mmio[b+off];
}
static void hwWrite(uintptr_t b,unsigned off,uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(hwLock);
    uintptr_t a=b+off;
    const uintptr_t gd=0x17a00000, core=SWEET_DEV_SPMI_CORE_PA;
    const uintptr_t ch=SWEET_DEV_SPMI_CHNLS_PA, obs=SWEET_DEV_SPMI_OBS_PA;
    unsigned spiOff=4*(513/32), spiBit=1u<<(513%32);
    if (a==0x0b220000+0x10 && dropPdcEnable && (value&2)) return;
    if (a == gd+0x100+spiOff) {
#ifdef SWEET_MUX_SERVICE_TEST
        assert(hasIrqOwner()); // attach/drain cannot reopen SPI after unpublish
#endif
        mmio[a] |= value; return;
    }
    if (a == gd+0x180+spiOff) { mmio[gd+0x100+spiOff] &= ~value; return; }
    if (a == gd+0x280+spiOff) { mmio[gd+0x200+spiOff] &= ~value; return; }
    if (a == gd+0x200+spiOff) {
        mmio[a] |= value;
        if (mmio[gd+0x100+spiOff]&spiBit) sweet_spmi_mux_irq(513);
        return;
    }
    mmio[a]=value;
    if (a>=ch && a<ch+512*0x10000ULL) {
        unsigned apid=(a-ch)/0x10000, reg=(a-ch)%0x10000;
        if (reg==0x108) mmio[ch+apid*0x10000+0x104] &= ~value;
        if (reg==0 && ppids.count(apid)) {
            unsigned ppid=ppids[apid], byte=(value>>4)&255, n=(value&7)+1;
            if ((ppid>>8)==2) {
                assert(lockDepth==1); assert(byte>=0x11 && byte<=0x16);
                ++lockedWrites;
            }
            mmio[b+8]=(apid==timeoutApid)?0:1;
            if (apid==timeoutApid) return;
            uint32_t data=mmio[b+0x10];
            for (unsigned i=0;i<n;++i) {
                unsigned address=(ppid<<8)|((byte+i)&255), dataByte=(data>>(8*i))&255;
                if (byte+i==0x14) pmic[(ppid<<8)|0x18] &= ~dataByte;
                else if (byte+i==0x15) pmic[(ppid<<8)|0x15] |= dataByte;
                else if (byte+i==0x16) pmic[(ppid<<8)|0x15] &= ~dataByte;
                else pmic[address]=dataByte;
            }
        }
    } else if (a>=obs && a<obs+0x10000 && (a-obs)%0x80==0) {
        unsigned apid=(a-obs)/0x80, byte=(value>>4)&255;
        mmio[a+8]=((ppids[apid]&255)==failReadPer)?3:1;
        mmio[a+0x18]=pmic[(ppids[apid]<<8)|byte];
    }
    (void)core;
}
static void initialize() {
    ppids={{248,0x008},{261,0x0c5},{266,0x210},{269,0x213},{271,0x215},{273,0x216},{300,0x240}};
    mmio[SWEET_DEV_SPMI_CORE_PA]=0x50000000;
    mmio[SWEET_DEV_SPMI_CORE_PA+4]=512;
    for (auto e:ppids) {
        mmio[SWEET_DEV_SPMI_CORE_PA+0x900+4*e.first]=0x01000000|(e.second<<8);
    }
    mmio[0x17a00000+4]=31; mmio[0x17a00000]=2;
    mmio[0x0b220000+0x114]=4;
    // Neighboring, unregistered trigger bits must survive attach and detach.
    pmic[(0x210<<8)|0x12]=0x80; pmic[(0x210<<8)|0x13]=0x80;
}
'''

startup_adapter = r'''
// Compile the real battery readiness/IRQ/lifecycle methods below. Only PMIC
// sampling/property publication and the IOKit scheduler are adapters.
struct IOTimerEventSource {
    unsigned requests=0;
    bool enabled=true;
    void setTimeoutMS(unsigned) { ++requests; }
    void cancelTimeout() {}
    void disable() { enabled=false; }
};
class SweetBattery : public OSObject {
public:
    IOWorkLoop *fLoop=IOWorkLoop::workLoop();
    IOTimerEventSource *fTimer=new IOTimerEventSource;
    IOInterruptEventSource *fIrqEvent;
    bool fRunning=true, fIrqAttached=false, fIrqFailed=false, fMuxWatching=false;
    bool fSettlePending=false, healthy=false, waiting=false, valid=true;
    unsigned fIrqMode=2, fIrqSamples=0, fSettleBudget=0, reads=0, publications=0;
    unsigned fRuntimeInputEvents=0,fDependencyMode=0;
    bool fWatchdogPending=false;
    volatile unsigned fIrqPending=0, fIrqNotifications=0;
    volatile uint64_t fIrqNotifyTime=0;
    SweetBattery() {
        fIrqEvent=IOInterruptEventSource::interruptEventSource(this, irqAction, nullptr, 0);
    }
    void poll() { ++reads; }
    void updateStatus() { ++publications; }
    void putNumber(const char *, unsigned) {}
    void putBool(const char *key, bool value) {
        if (!std::strcmp(key,"SweetBatteryIRQHealthy")) healthy=value;
        if (!std::strcmp(key,"SweetBatteryIRQWaitingForMux")) waiting=value;
        if (!std::strcmp(key,"SweetBatteryHardwareSampleValid")) valid=value;
    }
    void failIrq(const char *);
    static void irqNotify(void *, unsigned);
    static void irqAction(OSObject *, IOInterruptEventSource *, int);
    void tryAttachIrq();
    static IOReturn startIrqAction(OSObject *, void *, void *, void *, void *);
    static void timerFired(OSObject *, IOTimerEventSource *);
    static IOReturn stopPolling(OSObject *, void *, void *, void *, void *);
};
'''
