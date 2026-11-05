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
if (root / 'iokit/Kernel/arm/SweetSpmiMuxClient.cpp').exists():
    sys.exit(subprocess.run([sys.executable, str(Path(__file__).with_name(
        'test-mh2lm-mux-consumers.py')), str(root)]).returncode)
header = (root / 'bsd/dev/sweet_spmi_mux.h').read_text()
source = re.sub(r'^#include .*$', '', source, flags=re.M)
startup_tests = 'sweet_spmi_mux_watch_ready' in source
board_startup = 'sweet_spmi_mux_start(void)' in source
service_mux = 'class SweetSpmiInterruptMux : public IOService' in source
if service_mux:
    # Expose fields only to the host assertions; production stays private.
    source = source.replace('private:', 'public:')

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

from sweet_mux_test_support import prelude

from sweet_mux_test_support import startup_adapter

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
    if (early) { assert(startMux()==0); assert(attachKeys()==0); }
    if (failedFirst) {
        mmio[SWEET_DEV_SPMI_CORE_PA]=0;
        assert(startMux()==-1);
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
        assert(startMux()==(fail?-1:0));
        if (!fail) assert(attachKeys()==0);
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
#ifdef SWEET_MUX_SERVICE_TEST
    if (argc>1 && !std::strncmp(argv[1],"service-",8)) return serviceCheck(argv[1]);
#endif
#ifdef SWEET_MUX_READY
    if (argc>1 && !std::strncmp(argv[1],"startup-",8)) return startupCheck(argv[1]);
#endif
    assert(startMux()==0); assert(state==1);
    if (argc>1 && !std::strncmp(argv[1],"board-",6)) return boardCheck(argv[1]);
    assert(attachKeys()==0);
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
consumer_adapter = r'''
static int keyContext;
static int startMux() { return sweet_spmi_mux_start(); }
static int attachKeys() {
    return sweet_spmi_mux_attach(SWEET_MUX_KEYS, SWEET_KEYS_IRQ_ALL, keyNotify, &keyContext);
}
static void inject(unsigned apid, unsigned bits);
static int boardCheck(const char *scenario) {
    // Starting infrastructure never touches a key channel or enables its IRQs.
    assert(!bindings[0].notify && !bindings[1].notify);
    for (auto &r:routes) assert(!r.active && !r.touched && !r.channel);
    unsigned spiOff=4*(513/32), spiBit=1u<<(513%32);
    assert(!(mmio[0x17a00000+0x100+spiOff]&spiBit));
    assert(!(mmio[0x0b220000+0x10]&2));
    int context=9;
    if (!std::strcmp(scenario,"board-arm-fail")) {
        dropPdcEnable=true;
        assert(sweet_spmi_mux_attach(1,1,batteryNotify,&context)==-1);
        assert(state==3 && published==-1 && batteryFailures==1);
        assert(!bindings[1].notify && !bindings[1].context);
        for (auto &r:routes) assert(!r.active);
        std::puts("PASS: upstream arm failure leaves no failed-attach context");
        return 0;
    }
    assert(sweet_spmi_mux_attach(1,1,batteryNotify,&context)==0);
    if (!std::strcmp(scenario,"board-key-fail")) {
        // A missing/foreign key APID cannot suppress the battery consumer.
        mmio[SWEET_DEV_SPMI_CFG_PA+0x700+4*261]=1;
        assert(attachKeys()==-1);
        assert(state==1 && !bindings[0].notify && bindings[1].notify);
    } else if (!std::strcmp(scenario,"board-key-rollback")) {
        // Power/down configured first, then GPIO6 fails before its writes.
        failReadPer=0xc5;
        assert(attachKeys()==-1);
        failReadPer=0;
        assert(state==1 && !bindings[0].notify && bindings[1].notify);
        assert(!(pmic[(0x008<<8)|0x15]&3));
    } else if (!std::strcmp(scenario,"board-key-detach")) {
        assert(attachKeys()==0);
        assert(sweet_spmi_mux_attach(0,7,keyNotify,&keyContext)==-1);
        inject(248,1); sweet_spmi_mux_detach(0,&keyContext); pump();
        assert(state==1 && !bindings[0].notify && bindings[1].notify);
    }
    // Also covers sweetkeys=0: battery IRQ works without any key binding.
    inject(269,0x10); pump();
    assert(batteryEvents==1 && !keyEvents && !keyFailures && !batteryFailures);
    inject(269,0x10); sweet_spmi_mux_detach(1,&context); pump();
    assert(state==1 && !bindings[1].notify);
    assert(!(mmio[0x17a00000+0x100+spiOff]&spiBit));
    assert(attachKeys()==0 || !std::strcmp(scenario,"board-key-fail"));
    std::printf("PASS: %s; idle mux / independent consumers / last queued detach\n",scenario);
    return 0;
}
'''
service_adapter = r'''
#define SWEET_MUX_SERVICE_TEST 1
static SweetSpmiInterruptMux *testInstance;
static bool testServiceInitFail;
static bool hasIrqOwner() { return irqService!=nullptr; }
void IOService::registerService(unsigned) {
    if (!dynamic_cast<SweetSpmiMuxDevice *>(this) || !testMatchDriver) return;
    auto *service=new SweetSpmiInterruptMux;
    testInstance=service;
    testInitFail=testServiceInitFail;
    bool initialized=service->init(); testInitFail=false;
    if (!initialized) { service->release(); return; }
    assert(service->attach(this));
    if (service->start(this)) service->mockStarted=true;
    else service->terminate(2);
    service->release();
}
// Assertions inspect the actual instance fields, not a shadow implementation.
#define state (testInstance->state)
#define routes (testInstance->routes)
#define bindings (testInstance->bindings)
#define ackRoute(r) testInstance->ackRoute(r)
'''
service_checks = r'''
static int serviceCheck(const char *scenario) {
    bool failure=std::strstr(scenario,"fail") || std::strstr(scenario,"missing");
    if (!std::strcmp(scenario,"service-match-missing")) testMatchDriver=false;
    if (!std::strcmp(scenario,"service-init-fail")) testServiceInitFail=true;
    if (!std::strcmp(scenario,"service-loop-fail")) testLoopFail=1;
    if (!std::strcmp(scenario,"service-event-fail")) testAddFail=1;
    if (!std::strcmp(scenario,"service-map-fail")) testMapFail=3;
    if (failure) {
        assert(sweet_spmi_mux_watch_ready(batteryNotify,&batteryEvents)==0);
        assert(startMux()==-1);
        assert(published==-1 && !muxService && !irqService);
        assert(mappings.empty() && platform.children.empty() && batteryFailures==1);
        pump();
        std::printf("PASS: %s; FAILED delivered, partial resources released\n",scenario);
        return 0;
    }
    assert(startMux()==0 && muxService && irqService);
    assert(testInstance->getWorkLoop());
    assert(testInstance->properties["SweetMuxLifecycle"]=="ready");
    assert(testInstance->properties["SweetMuxHardwareProvider"]=="false");
    assert(startMux()==0); // publishing twice cannot create another owner
    assert(platform.children.size()==1);
    assert(attachKeys()==0);
    if (!std::strcmp(scenario,"service-stop-pending") ||
        !std::strcmp(scenario,"service-stop-battery-race")) {
        SweetBattery battery;
        battery.poll();
        assert(SweetBattery::startIrqAction(&battery,nullptr,nullptr,nullptr,nullptr)==0);
        pump(); assert(battery.fIrqAttached && battery.reads==2);
        auto mapped=mappings;
        sweet_spmi_mux_detach(0,&keyContext);
        assert(attachKeys()==0 && mappings==mapped); // cached channels do not leak
        bool pending=!std::strcmp(scenario,"service-stop-pending");
        if (pending) {
            inject(248,1); // stop with mux action queued; consumer still alive
            platform.children.back()->terminate(2);
        } else {
            std::atomic<bool> go{false};
            std::thread consumer([&] {
                while (!go.load()) std::this_thread::yield();
                battery.fLoop->runAction(SweetBattery::stopPolling,&battery);
            });
            go=true;
            platform.children.back()->terminate(2);
            consumer.join();
        }
        assert(!muxService && !irqService && mappings.empty());
        pump();
        assert(keyFailures==1 && !battery.fRunning && battery.reads==2);
        if (pending) assert(battery.fIrqFailed);
        assert(testStops==1 && testFrees==2);
    } else {
        // API references and the IRQ dispatch pointer must survive/reject stop
        // safely; ASAN sees real deletion of the mock service and event source.
        std::atomic<bool> running{true};
        std::atomic<unsigned> calls{0};
        std::thread client([&] {
            int context=3;
            while (running.load()) {
                if (sweet_spmi_mux_attach(1,1,batteryNotify,&context)==0)
                    sweet_spmi_mux_detach(1,&context);
                ++calls;
            }
        });
        std::thread irq([&] { while (running.load()) sweet_spmi_mux_irq(513); });
        while (calls.load()<10) std::this_thread::yield();
        platform.children.back()->terminate(2);
        running=false; client.join(); irq.join();
        assert(!muxService && !irqService && mappings.empty());
        assert(testStops==1 && testFrees==2);
        auto events=keyEvents+keyFailures+batteryEvents+batteryFailures;
        for (unsigned i=0;i<100;++i) assert(sweet_spmi_mux_irq(513));
        pump(); assert(events==keyEvents+keyFailures+batteryEvents+batteryFailures);
        int context=0;
        assert(sweet_spmi_mux_attach(1,1,batteryNotify,&context)==-1);
        sweet_spmi_mux_detach(1,&context);
    }
    assert(platform.children.empty());
    std::printf("PASS: %s; stop clears IRQ/callbacks, final free releases all MMIO\n",scenario);
    return 0;
}
'''
if not service_mux:
    service_adapter = service_checks = ''
if not board_startup:
    consumer_adapter = '''static int startMux() { return sweet_spmi_mux_start(3); }
static int attachKeys() { return 0; }
static int boardCheck(const char *) { return 1; }
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-batt-irq-') as temporary:
    test = Path(temporary) / 'mux.cpp'
    binary = Path(temporary) / 'mux-test'
    prefix = '#define SWEET_MUX_SERVICE_TEST 1\n' if service_mux else ''
    test.write_text(prefix + prelude + header + source + service_adapter + consumer_adapter +
                    startup_adapter + startup_checks + service_checks + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-pthread', '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
    # Minimal IOKit mocks intentionally live until process exit.
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    subprocess.run([str(binary)], env=env, check=True)
    subprocess.run([str(binary), 'timeout'], env=env, check=True)
    if board_startup:
        for scenario in ['no-keys', 'key-fail', 'key-rollback', 'key-detach', 'arm-fail']:
            subprocess.run([str(binary), 'board-' + scenario], env=env, check=True)
    if service_mux:
        for scenario in ['match-missing', 'init-fail', 'loop-fail', 'event-fail',
                         'map-fail', 'stop-pending', 'stop-battery-race', 'stop-race']:
            subprocess.run([str(binary), 'service-' + scenario], env=env, check=True)
        for _ in range(20):
            subprocess.run([str(binary), 'service-stop-race'], env=env,
                           stdout=subprocess.DEVNULL, check=True)
        print('PASS: 20 additional IRQ/API/stop lifetime races')
    if startup_tests:
        for scenario in ['late', 'early', 'fail', 'failed-first', 'cancel',
                         'queued-stop', 'races']:
            subprocess.run([str(binary), 'startup-' + scenario], env=env, check=True)
