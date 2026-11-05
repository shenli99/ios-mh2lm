#!/usr/bin/env python3
"""Exercise the actual mux source with a simulated PMIC/GIC backend.

Usage: test-mh2lm-battery-irq.py EXTRACTED_XNU_DIRECTORY
Substitutes MMIO/time/IOKit scheduling and, for the selected actual battery
lifecycle methods, sampling/property publication. These checks do not validate
real IRQ delivery, PMIC timing, or the complete XNU battery service.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
source = (root / 'iokit/Kernel/arm/SweetSpmiInterruptMux.cpp').read_text()
header = (root / 'bsd/dev/sweet_spmi_mux.h').read_text()
source = re.sub(r'^#include .*$', '', source, flags=re.M)
startup_tests = 'sweet_spmi_mux_watch_ready' in source

def substitute_function(text, name, replacement):
    match = re.search(r'\n' + name + r'\([^)]*\)\n\{', text)
    if not match:
        raise RuntimeError('missing hardware adapter: ' + name)
    opening = text.index('{', match.start())
    level, closing = 1, opening + 1
    while level:
        level += (text[closing] == '{') - (text[closing] == '}')
        closing += 1
    return text[:opening] + '{ ' + replacement + ' }' + text[closing:]

for name, body in {
    'read32': 'return hwRead(base, off);',
    'write32': 'hwWrite(base, off, value);',
    'read64': 'return hwRead(base, off) | (uint64_t(hwRead(base, off + 4)) << 32);',
    'write64': 'hwWrite(base, off, uint32_t(value)); hwWrite(base, off + 4, uint32_t(value >> 32));',
}.items():
    source = substitute_function(source, name, body)

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
#define ARM64_BOARD_CONFIG_MH2LM 1
#define SWEET_DEV_SPMI_CORE_PA 0x0c440000u
#define SWEET_DEV_SPMI_CFG_PA 0x0c40a000u
#define SWEET_DEV_SPMI_OBS_PA 0x0e600000u
#define SWEET_DEV_SPMI_CHNLS_PA 0x0c600000u
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
struct OSObject {};
#define OSDynamicCast(type, object) static_cast<type *>(object)
struct IOInterruptEventSource;
struct IOWorkLoop : OSObject {
    using Action = IOReturn (*)(OSObject *, void *, void *, void *, void *);
    static IOWorkLoop *workLoop() { return new IOWorkLoop; }
    int addEventSource(IOInterruptEventSource *) { return 0; }
    int runAction(Action a, OSObject *o, void *x=nullptr, void *y=nullptr) {
        return a(o,x,y,nullptr,nullptr);
    }
};
static std::vector<IOInterruptEventSource *> queue;
struct IOInterruptEventSource {
    using Action = void (*)(OSObject *, IOInterruptEventSource *, int);
    OSObject *owner; Action action;
    bool enabled = true;
    static IOInterruptEventSource *interruptEventSource(OSObject *o, Action a, void *, int) {
        return new IOInterruptEventSource{o,a};
    }
    void interruptOccurred(void *, void *, int) { queue.push_back(this); }
    void disable() { enabled=false; }
};
static void pump() {
    while (!queue.empty()) {
        auto *e=queue.front(); queue.erase(queue.begin());
        if (e->enabled) e->action(e->owner,e,1);
    }
}
#define SWEET_LOG_INFO(...) ((void)0)
#define SWEET_LOG_DEBUG(...) ((void)0)
#define SWEET_LOG_WARN(...) ((void)0)
#define SWEET_LOG_ERROR(...) ((void)0)
static uint64_t fakeNs=1000000000ULL;
static void clock_get_uptime(uint64_t *v) { *v=fakeNs; }
static void absolutetime_to_nanoseconds(uint64_t v, uint64_t *n) { *n=v; }
static void delay(unsigned) {}
static std::map<uintptr_t,uint32_t> mmio;
static std::map<unsigned,uint8_t> pmic;
static std::map<unsigned,unsigned> ppids;
static unsigned keyEvents, keyFailures, batteryEvents, batteryFailures;
static unsigned lockDepth, lockedWrites, failReadPer, timeoutApid;
extern "C" bool sweet_spmi_mux_irq(unsigned);
extern "C" void sweet_keys_irq_notify(unsigned) { ++keyEvents; }
extern "C" void sweet_keys_irq_failed() { ++keyFailures; }
extern "C" vm_offset_t ml_io_map(vm_offset_t a, vm_size_t) { return a; }
extern "C" void sweet_spmi_irq_lock() { assert(lockDepth++ == 0); }
extern "C" void sweet_spmi_irq_unlock() { assert(--lockDepth == 0); }
extern "C" int sweet_spmi_read(unsigned sid, unsigned a, uint8_t *v) {
    if ((a >> 8) == failReadPer) return -1;
    assert(lockDepth == 0); // the API itself acquires the observer lock
    *v=pmic[(sid<<16)|a]; return 0;
}
static void batteryNotify(void *, unsigned event) {
    if (event == 0x80000000u) ++batteryFailures; else batteryEvents |= event;
}
static uint32_t hwRead(uintptr_t b,unsigned off) { return mmio[b+off]; }
static void hwWrite(uintptr_t b,unsigned off,uint32_t value) {
    uintptr_t a=b+off;
    const uintptr_t gd=0x17a00000, core=SWEET_DEV_SPMI_CORE_PA;
    const uintptr_t ch=SWEET_DEV_SPMI_CHNLS_PA, obs=SWEET_DEV_SPMI_OBS_PA;
    unsigned spiOff=4*(513/32), spiBit=1u<<(513%32);
    if (a == gd+0x100+spiOff) { mmio[a] |= value; return; }
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
        mmio[a+8]=1; mmio[a+0x18]=pmic[(ppids[apid]<<8)|byte];
    }
    (void)core;
}
static void initialize() {
    ppids={{248,0x008},{261,0x0c5},{266,0x210},{269,0x213},{300,0x240}};
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
startup_checks = r'''
struct ReadyContext {
    std::atomic<unsigned> result{0};
    std::atomic<bool> dead{false};
};
static void readyCallback(void *context, unsigned event) {
    auto *c=static_cast<ReadyContext *>(context);
    assert(!c->dead.load());
    assert(!c->result.exchange(event)); // one-shot, never duplicate
}
static void readinessRaces() {
    for (unsigned i=0;i<200;++i) {
        publishReadiness(0);
        ReadyContext c;
        std::thread publisher([&] { publishReadiness(1); });
        assert(sweet_spmi_mux_watch_ready(readyCallback,&c)==0);
        publisher.join();
        assert(c.result.load()==SWEET_MUX_READY);
        sweet_spmi_mux_unwatch_ready(&c);
        publishReadiness(0);
        ReadyContext stopped;
        assert(sweet_spmi_mux_watch_ready(readyCallback,&stopped)==0);
        std::thread late([&] { publishReadiness(1); });
        sweet_spmi_mux_unwatch_ready(&stopped);
        stopped.dead.store(true); // callback after unwatch would assert
        late.join();
    }
    std::puts("PASS: readiness registration/publication and cancellation races");
}
static int startupCheck(const char *scenario) {
    if (!std::strcmp(scenario,"startup-races")) { readinessRaces(); return 0; }
    bool early=!std::strcmp(scenario,"startup-early");
    bool fail=!std::strcmp(scenario,"startup-fail");
    bool failedFirst=!std::strcmp(scenario,"startup-failed-first");
    bool cancel=!std::strcmp(scenario,"startup-cancel");
    bool queuedStop=!std::strcmp(scenario,"startup-queued-stop");
    if (early) assert(sweet_spmi_mux_start(3)==0);
    if (failedFirst) {
        mmio[SWEET_DEV_SPMI_CORE_PA]=0;
        assert(sweet_spmi_mux_start(3)==-1);
    }
    SweetBattery battery;
    battery.poll(); // the complete service's initial sample before registration
    assert(SweetBattery::startIrqAction(&battery,nullptr,nullptr,nullptr,nullptr)==0);
    assert(battery.waiting && !battery.fIrqFailed && battery.fTimer->requests==0);
    if (!early && !failedFirst) {
        fakeNs+=30000000000ULL; // the real failure: BSD mux starts after 10s
        pump();
        assert(battery.reads==1 && !battery.fIrqAttached && !battery.fIrqFailed);
        assert(!lockedWrites && !battery.fTimer->requests);
        if (cancel) {
            SweetBattery::stopPolling(&battery,nullptr,nullptr,nullptr,nullptr);
        }
        if (fail) mmio[SWEET_DEV_SPMI_CORE_PA]=0;
        assert(sweet_spmi_mux_start(3)==(fail?-1:0));
        if (queuedStop) {
            SweetBattery::stopPolling(&battery,nullptr,nullptr,nullptr,nullptr);
        }
    }
    pump();
    if (fail || failedFirst) {
        assert(battery.fIrqFailed && !battery.fRunning && !battery.healthy);
        assert(!battery.waiting && !battery.valid && !battery.fIrqAttached);
        assert(battery.reads==1 && !battery.fTimer->requests);
    } else if (cancel || queuedStop) {
        assert(!battery.fIrqAttached && !bindings[1].notify);
        assert(battery.reads==1 && !lockedWrites);
    } else {
        assert(battery.fIrqAttached && battery.healthy && !battery.waiting);
        assert(battery.reads==2 && battery.fIrqNotifications==0);
        assert(!battery.fTimer->requests && !battery.fIrqFailed);
        mmio[SWEET_DEV_SPMI_CHNLS_PA+269*0x10000+0x104]=0x10;
        assert(sweet_spmi_mux_irq(513)); pump();
        assert(battery.reads==3 && battery.fIrqNotifications==1 && battery.fIrqSamples==1);
        // Stop with a battery IRQ queued in the mux worker.
        mmio[SWEET_DEV_SPMI_CHNLS_PA+269*0x10000+0x104]=0x10;
        assert(sweet_spmi_mux_irq(513));
        SweetBattery::stopPolling(&battery,nullptr,nullptr,nullptr,nullptr); pump();
        assert(state==1 && !keyFailures && !bindings[1].notify && battery.reads==3);
    }
    std::printf("PASS: actual battery methods %s; no startup timer/poll fallback\n",scenario);
    return 0;
}
'''

if startup_tests:
    battery_source = (root / 'iokit/Kernel/arm/SweetBattery.cpp').read_text()
    startup_adapter = re.search(r'^#define SWEET_BATT_POLL_MS\b.*$',
                                battery_source, re.M).group(0) + '\n' + startup_adapter
    for name in ['failIrq', 'irqNotify', 'irqAction', 'tryAttachIrq',
                 'startIrqAction', 'timerFired', 'stopPolling']:
        match = re.search(r'\nSweetBattery::' + name + r'\([^)]*\)\n\{', battery_source)
        if not match:
            raise RuntimeError('missing battery method: ' + name)
        opening = battery_source.index('{', match.start())
        level, closing = 1, opening + 1
        while level:
            level += (battery_source[closing] == '{') - (battery_source[closing] == '}')
            closing += 1
        definition = battery_source.rfind('\n\n', 0, match.start()) + 2
        startup_adapter += battery_source[definition:closing] + '\n'
else:
    startup_adapter = startup_checks = ''

checks = r'''
static void inject(unsigned apid,unsigned bits) {
    mmio[SWEET_DEV_SPMI_CHNLS_PA+apid*0x10000+0x104] |= bits;
    assert(sweet_spmi_mux_irq(513));
}
int main(int argc,char **argv) {
    initialize();
#ifdef SWEET_MUX_READY
    if (argc>1 && !std::strncmp(argv[1],"startup-",8)) return startupCheck(argv[1]);
#endif
    assert(sweet_spmi_mux_start(3)==0); assert(state==1);
    int context=7;
    if (argc>1 && std::strcmp(argv[1],"timeout")==0) {
        assert(sweet_spmi_mux_attach(1,1,batteryNotify,&context)==0);
        timeoutApid=269; inject(269,0x10); pump();
        assert(state==3 && keyFailures==1 && batteryFailures==1);
        assert(routes[3].writePoisoned);
        auto writes=lockedWrites;
        assert(!ackRoute(routes[3])); assert(lockedWrites==writes);
        sweet_spmi_mux_detach(1,&context);
        assert(!bindings[1].notify && !bindings[1].context);
        std::puts("PASS: timeout poisons RW lane; failure reported to both consumers");
        return 0;
    }
    // A setup failure after USB was enabled must roll back only battery bits.
    failReadPer=0x10;
    assert(sweet_spmi_mux_attach(1,3,batteryNotify,&context)==-1);
    assert(state==1 && !keyFailures && !bindings[1].notify);
    assert(!(pmic[(0x213<<8)|0x15]&0x10));
    failReadPer=0;
    // A foreign IRQ owner must never be mapped/claimed as our route.
    mmio[SWEET_DEV_SPMI_CFG_PA+0x700+4*300]=1;
    assert(sweet_spmi_mux_attach(1,15,batteryNotify,&context)==-1);
    assert(state==1 && !keyFailures);
    mmio[SWEET_DEV_SPMI_CFG_PA+0x700+4*300]=0;
    assert(sweet_spmi_mux_attach(1,15,batteryNotify,&context)==0);
    assert((pmic[(0x210<<8)|0x13]&0x82)==0x80); // CHGR rising only
    assert((pmic[(0x213<<8)|0x13]&0x10)==0x10); // USB both edges
    assert((pmic[(0x240<<8)|0x13]&0x0a)==0x02); // MSOC rising, READY both
    inject(269,0x10);
    mmio[SWEET_DEV_SPMI_CHNLS_PA+300*0x10000+0x104]=0x0a;
    pump(); assert(batteryEvents==13 && state==1);
    // >256 normal lifetime IRQs must not trigger the old lifetime cutoff.
    for (unsigned i=0;i<600;++i) {
        fakeNs+=50000000; inject(248,1); pump(); assert(state==1);
    }
    assert(keyEvents==600 && !keyFailures && !batteryFailures);
    // Detach while its IRQ worker is queued: clearing the pending battery
    // source must not be mistaken for an unhandled key/shared IRQ.
    inject(269,0x10); sweet_spmi_mux_detach(1,&context); pump();
    assert(state==1 && !keyFailures && !bindings[1].notify);
    assert(pmic[(0x210<<8)|0x13]==0x80);
    assert((pmic[(0x008<<8)|0x15]&3)==3);
    // A dense burst still must trip storm protection and notify both groups.
    assert(sweet_spmi_mux_attach(1,1,batteryNotify,&context)==0);
    for (unsigned i=0;i<300 && state!=3;++i) { inject(248,1); pump(); }
    assert(state==3 && keyFailures==1 && batteryFailures==1);
    sweet_spmi_mux_detach(1,&context);
    assert(!bindings[1].notify && !bindings[1].context);
    assert(lockedWrites>0 && lockDepth==0);
    std::puts("PASS: trigger masks, owned routes, rollback, queued detach, 600 IRQs and storm shutdown");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-batt-irq-') as temporary:
    test = Path(temporary) / 'mux.cpp'
    binary = Path(temporary) / 'mux-test'
    test.write_text(prelude + header + source + startup_adapter + startup_checks + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-pthread', '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
    # Minimal IOKit mocks intentionally live until process exit.
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    subprocess.run([str(binary)], env=env, check=True)
    subprocess.run([str(binary), 'timeout'], env=env, check=True)
    if startup_tests:
        for scenario in ['late', 'early', 'fail', 'failed-first', 'cancel',
                         'queued-stop', 'races']:
            subprocess.run([str(binary), 'startup-' + scenario], env=env, check=True)
