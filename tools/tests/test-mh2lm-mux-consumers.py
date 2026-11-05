#!/usr/bin/env python3
"""Run actual mux, registry connection, keys service and selected battery methods.

Uses host models for matching, termination, scheduling, PMIC/GIC and the keys
worker/sampling. ASAN/UBSAN exposes stale references/callbacks; this cannot prove
native matching lock order, real IRQ timing or the C worker's hardware behavior.
Usage: test-mh2lm-mux-consumers.py EXTRACTED_XNU
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from sweet_mux_test_support import prelude, startup_adapter

if len(sys.argv) != 2:
    sys.exit(__doc__)
r = Path(sys.argv[1]).resolve()
def read(path):
    return (r/path).read_text()
def strip(text):
    return re.sub(r'^#include .*$', '', text, flags=re.M)
def function(text, name):
    m=re.search(r'\n'+re.escape(name)+r'\([^)]*\)\n\{',text)
    assert m, name
    a=text.rfind('\n\n',0,m.start())+2
    end=text.index('{',m.start())+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[a:end]+'\n'

mux=read('iokit/Kernel/arm/SweetSpmiInterruptMux.cpp')
header=read('bsd/dev/sweet_spmi_mux.h')
client=read('iokit/Kernel/arm/SweetSpmiMuxClient.cpp')
client_header=read('bsd/dev/sweet_spmi_mux_client.h')
keys=read('iokit/Kernel/arm/SweetPmicKeys.cpp')
battery=read('iokit/Kernel/arm/SweetBattery.cpp')
for removed in ['sweet_spmi_mux_attach','sweet_spmi_mux_detach','sweet_spmi_mux_watch_ready',
                'sweet_spmi_mux_unwatch_ready','muxService','readyLock','publishReadiness']:
    assert removed not in mux+header+client+battery+read('bsd/dev/sweet_keys.c'), removed
assert 'gIOFirstMatchNotification' in client and 'gIOTerminatedNotification' in client
assert 'IOService *consumer' in header
assert 'static const MuxRoute routeTemplates' in mux
assert 'sweet_spmi_mux' not in read('bsd/dev/sweet_keys.c')
assert 'keys_worker_running' in read('bsd/dev/sweet_keys.c')
# Replace only MMIO primitives. All route/ack/rollback and instance code is real.
for name,body in {'read32':'return hwRead(base, off);',
                  'write32':'hwWrite(base, off, value);',
                  'read64':'return hwRead(base, off) | (uint64_t(hwRead(base, off + 4)) << 32);',
                  'write64':'hwWrite(base, off, uint32_t(value)); hwWrite(base, off + 4, uint32_t(value >> 32));'}.items():
    old=function(mux,name); a=old.index('{'); mux=mux.replace(old,old[:a]+'{ '+body+' }\n')

# Add registry capabilities to the shared historical hardware fixture.
prelude=prelude.replace('using IOReturn = int;', 'using IOReturn = int; using IOOptionBits = unsigned;')
prelude=prelude.replace('kIOReturnNotPermitted=2,','kIOReturnNoMemory=5, kIOReturnNotPermitted=2,')
prelude=prelude.replace('virtual void free() {}','virtual bool init() { return true; }\n    virtual void free() {}')
prelude=prelude.replace('struct OSDictionary {};','struct OSDictionary : OSObject {};')
prelude=prelude.replace('struct OSObject {', 'static std::atomic<int> liveObjects{0};\nstruct OSObject {\n    OSObject() { ++liveObjects; }')
prelude=prelude.replace('virtual ~OSObject() = default;', 'virtual ~OSObject() { --liveObjects; }')
prelude=prelude.replace('void disable() { enabled=false; }','void disable() { enabled=false; }\n    void enable() { enabled=true; }')
a=prelude.index('struct IOService : OSObject {');b=prelude.index('#define SWEET_LOG_INFO',a)
registry=r'''
struct IONotifier;
static OSObject trueObject;
static OSObject *kOSBooleanTrue=&trueObject;
static const char *gIOFirstMatchNotification="matched", *gIOTerminatedNotification="terminated";
using MatchHandler=bool (*)(void *,void *,IOService *,IONotifier *);
static void registryTerminated(IOService *);
struct IOService : OSObject {
    IOService *parent=nullptr;
    std::vector<IOService *> children;
    std::map<std::string,std::string> properties;
    std::atomic<bool> inactive{false};
    bool mockStarted=false;
    virtual bool init(OSDictionary * = nullptr) { return !testInitFail; }
    virtual bool start(IOService *) { return true; }
    virtual bool willTerminate(IOService *,IOOptionBits) { return true; }
    virtual void stop(IOService *) {}
    virtual IOWorkLoop *getWorkLoop() const { return nullptr; }
    bool isInactive() const { return inactive.load(); }
    IOService *getProvider() const { return parent; }
    void free() override { ++testFrees; }
    bool attach(IOService *p) { parent=p; p->children.push_back(this); retain(); return true; }
    void registerService(unsigned=0);
    static IOPlatformExpert *getPlatform();
    static OSDictionary *serviceMatching(const char *) { return new OSDictionary; }
    static IONotifier *addMatchingNotification(const char *,OSDictionary *,MatchHandler,void *);
    void setName(const char *) {}
    OSObject *getProperty(const char *k) { return properties[k]=="true"?kOSBooleanTrue:nullptr; }
    bool setProperty(const char *k,const char *v) { properties[k]=v; return true; }
    bool setProperty(const char *k,bool v) { properties[k]=v?"true":"false"; return true; }
    bool setProperty(const char *k,uint64_t v,unsigned) { properties[k]=std::to_string(v); return true; }
    bool terminate(unsigned) {
        retain();
        inactive=true; registryTerminated(this);
        while (!children.empty()) children.back()->terminate(2);
        willTerminate(parent,2);
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
static std::recursive_mutex registryLock;
static std::vector<IONotifier *> notifiers;
static std::vector<IOService *> registry;
static unsigned failNotifierAt, notifierCalls;
struct IONotifier {
    const char *type; OSDictionary *dict; MatchHandler handler; void *target;
    void remove() {
        std::lock_guard<std::recursive_mutex> guard(registryLock);
        notifiers.erase(std::remove(notifiers.begin(),notifiers.end(),this),notifiers.end());
        dict->release(); delete this;
    }
};
IONotifier *IOService::addMatchingNotification(const char *type, OSDictionary *dict, MatchHandler handler,void *target) {
    std::lock_guard<std::recursive_mutex> guard(registryLock);
    if (failNotifierAt && --failNotifierAt==0) return nullptr;
    dict->retain();
    auto *n=new IONotifier{type,dict,handler,target};notifiers.push_back(n);
    if (type==gIOFirstMatchNotification)
        for (auto *s:registry) { ++notifierCalls; handler(target,nullptr,s,n); }
    return n;
}
static void deliver(const char *type, IOService *service) {
    std::lock_guard<std::recursive_mutex> guard(registryLock);
    for (auto *n:notifiers) if (n->type==type) {
        ++notifierCalls;n->handler(n->target,nullptr,service,n);
    }
}
static void registryTerminated(IOService *s) {
    std::lock_guard<std::recursive_mutex> guard(registryLock);
    deliver(gIOTerminatedNotification,s);
    registry.erase(std::remove(registry.begin(),registry.end(),s),registry.end());
}
#define OSSafeReleaseNULL(p) do { if (p) { (p)->release(); (p)=nullptr; } } while (0)
'''
# IOService is referenced by the notification callback typedef.
registry=registry.replace('struct IONotifier;', 'struct IOService;\nstruct IONotifier;',1)
prelude=prelude[:a]+registry+prelude[b:]

startup_adapter=startup_adapter.replace('class SweetBattery : public OSObject','class SweetBattery : public IOService')
startup_adapter=startup_adapter.replace('fMuxWatching=false','fTimerAdded=true')
startup_adapter=startup_adapter.replace('IOInterruptEventSource *fIrqEvent;', 'IOInterruptEventSource *fIrqEvent;\n    SweetSpmiMuxClient *fMuxClient=nullptr;')
startup_adapter=startup_adapter.replace('fIrqEvent=IOInterruptEventSource::interruptEventSource(this, irqAction, nullptr, 0);','fIrqEvent=IOInterruptEventSource::interruptEventSource(this, irqAction, nullptr, 0);\n        assert(fLoop->addEventSource(fIrqEvent)==0);')
startup_adapter='using IOPMPowerSource=IOService;\n'+re.search(r'^#define SWEET_BATT_POLL_MS\b.*$',battery,re.M)[0]+'\n'+startup_adapter
for name in ['failIrq','irqNotify','irqAction','tryAttachIrq','startIrqAction','timerFired','stopPolling']:
    startup_adapter+=function(battery,'SweetBattery::'+name)

adapters=r'''
static SweetSpmiInterruptMux *testInstance;
static bool testDriverInitFail;
static bool hasIrqOwner() { return irqService!=nullptr; }
static unsigned backendStarts,backendStops,backendEvents;
static bool backendPrepare=true,backendStart=true,backendRunning=false;
static bool PE_parse_boot_argn(const char *,void *,size_t) { return false; }
extern "C" bool sweet_keys_prepare() { return backendPrepare; }
extern "C" bool sweet_keys_start() { ++backendStarts;backendRunning=backendStart;return backendStart; }
extern "C" void sweet_keys_stop() { ++backendStops;backendRunning=false; }
extern "C" void sweet_keys_event(unsigned) { assert(backendRunning); ++backendEvents; }
'''
registry_impl=r'''
void IOService::registerService(unsigned) {
    if (dynamic_cast<SweetSpmiMuxDevice *>(this)) {
        if (!testMatchDriver) return;
        auto *service=new SweetSpmiInterruptMux;
        testInitFail=testDriverInitFail;
        bool initialized=service->init();testInitFail=false;
        if (!initialized) { service->release();return; }
        testInstance=service; assert(service->attach(this));
        if (service->start(this)) service->mockStarted=true;
        else service->terminate(2);
        service->release(); return;
    }
    if (dynamic_cast<SweetPmicKeysDevice *>(this)) {
        auto *service=new SweetPmicKeys; assert(service->init()); assert(service->attach(this));
        if (service->start(this)) service->mockStarted=true;
        else service->terminate(2);
        service->release(); return;
    }
    if (dynamic_cast<SweetSpmiInterruptMux *>(this)) {
        std::lock_guard<std::recursive_mutex> guard(registryLock);
        registry.push_back(this); deliver(gIOFirstMatchNotification,this);
    }
}
static IOService keyOwner,battOwner;
static int keyContext;
static int startMux() { return sweet_spmi_mux_start(); }
static int testAttach(unsigned group,unsigned events,SweetMuxNotify cb,void *context) {
    return testInstance->attachConsumer(group?&battOwner:&keyOwner,group,events,cb,context)==0?0:-1;
}
static void testDetach(unsigned group,void *context) {
    testInstance->detachConsumer(group?&battOwner:&keyOwner,group,context);
}
static int attachKeys() { return testAttach(0,7,keyNotify,&keyContext); }
static void inject(unsigned apid,unsigned bits) {
    { std::lock_guard<std::recursive_mutex> guard(hwLock);
      mmio[SWEET_DEV_SPMI_CHNLS_PA+apid*0x10000+0x104] |= bits; }
    assert(sweet_spmi_mux_irq(513));
}
static void endProvider() {
    if (!platform.children.empty()) platform.children.front()->terminate(2);
    pump();
}
static void cleanupBattery(SweetBattery &b) {
    b.fLoop->runAction(SweetBattery::stopPolling,&b);
    if (b.fMuxClient) { b.fMuxClient->stop();b.fMuxClient->release();b.fMuxClient=nullptr; }
    b.fLoop->removeEventSource(b.fIrqEvent);b.fIrqEvent->release();
    b.fLoop->release();delete b.fTimer;pump();
}
'''
# Reuse the existing real hardware regression scenarios, using typed references
# owned by the fixture rather than resurrecting any production C bridge.
legacy=Path(__file__).with_name('test-mh2lm-battery-irq.py').read_text()
legacy_checks=re.search(r"^checks = r'''(.*?)'''\nconsumer_adapter",legacy,re.S|re.M)[1]
legacy_board=re.search(r"^consumer_adapter = r'''(.*?)'''\nservice_adapter",legacy,re.S|re.M)[1]
legacy_board=legacy_board[legacy_board.index('static int boardCheck'):]
legacy_checks=legacy_checks[legacy_checks.index('int main'):].replace('int main(', 'int hardwareCheck(')
legacy_checks=re.sub(r'#ifdef SWEET_MUX_SERVICE_TEST.*?#endif','',legacy_checks,flags=re.S)
legacy_checks=re.sub(r'#ifdef SWEET_MUX_READY.*?#endif','',legacy_checks,flags=re.S)
legacy_checks=legacy_checks.replace('    initialize();\n','')
legacy_checks=legacy_checks.rsplit('}',1)[0]+'    return 0;\n}\n'
def direct_calls(text):
    return text.replace('sweet_spmi_mux_attach','testAttach').replace(
        'sweet_spmi_mux_detach','testDetach').replace(' && published==-1','')
legacy_checks=direct_calls(legacy_checks)
legacy_board=direct_calls(legacy_board)

checks=r'''
static void startBattery(SweetBattery &b) {
    b.poll();b.fLoop->runAction(SweetBattery::startIrqAction,&b);
}
static void assertNoLeaks() { pump();assert(notifiers.empty() && mappings.empty() && platform.children.empty() && registry.empty()); }
static void ownerCleanup(const char *mode) {
    bool early=!std::strcmp(mode,"early");
    if (early) assert(startMux()==0);
    SweetBattery b;startBattery(b);pump();
    if (!early) { assert(b.waiting && !b.fIrqAttached && b.reads==1);fakeNs+=30000000000ULL;assert(startMux()==0); }
    pump();assert(b.fIrqAttached && b.healthy && !b.waiting && b.reads==2);
    assert(!testInstance->bindings[0].notify && testInstance->bindings[1].notify);
    inject(269,0x10);pump();assert(b.reads==3 && b.fIrqNotifications==1 && !b.fTimer->requests);
    inject(269,0x10); // queued hardware IRQ, then owner cancellation
    cleanupBattery(b);pump();assert(!testInstance->bindings[1].notify && !testInstance->bindings[1].context);
    assert(!(hwRead(0x17a00000,0x100+4*(513/32))&(1u<<(513%32))));
    endProvider();assertNoLeaks();
}
int main(int argc,char **argv) {
    assert(argc>1);const char *mode=argv[1];initialize();
    if (!std::strcmp(mode,"early") || !std::strcmp(mode,"late")) ownerCleanup(mode);
    else if (!std::strcmp(mode,"cancel-before-match")) {
        SweetBattery b;startBattery(b);pump();cleanupBattery(b);assert(startMux()==0);pump();
        assert(!testInstance->bindings[1].notify && b.reads==1);endProvider();assertNoLeaks();
    } else if (!std::strcmp(mode,"term-before-attach") || !std::strcmp(mode,"term-after-attach")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);
        if (!std::strcmp(mode,"term-after-attach")) {
            pump();assert(b.fIrqAttached);inject(269,0x10); // stale queued IRQ must not revive it
        }
        endProvider();assert(b.fIrqFailed && !b.fIrqAttached && !b.fRunning && !b.valid);
        assert(b.reads<=2 && b.fIrqNotifications==0);
        cleanupBattery(b);assertNoLeaks();
    } else if (!std::strcmp(mode,"notifier-fail-first") || !std::strcmp(mode,"notifier-fail-second")) {
        assert(startMux()==0);
        failNotifierAt=!std::strcmp(mode,"notifier-fail-first")?1:2;
        SweetBattery b;startBattery(b);pump();assert(b.fIrqFailed && !b.fMuxClient && notifiers.empty());
        cleanupBattery(b);endProvider();assertNoLeaks();
    } else if (!std::strcmp(mode,"attach-fail")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);
        mmio[SWEET_DEV_SPMI_CFG_PA+0x700+4*269]=1;pump();
        assert(b.fIrqFailed && !testInstance->bindings[1].notify);
        cleanupBattery(b);endProvider();assertNoLeaks();
    } else if (!std::strcmp(mode,"client-event-fail")) {
        assert(startMux()==0);SweetBattery b;
        testAddFail=1;startBattery(b);pump();
        assert(b.fIrqFailed && !b.fMuxClient && notifiers.empty());
        cleanupBattery(b);endProvider();assertNoLeaks();
    } else if (!std::strncmp(mode,"mux-fail-",9)) {
        SweetBattery b;startBattery(b);pump();
        if (!std::strcmp(mode,"mux-fail-missing")) testMatchDriver=false;
        if (!std::strcmp(mode,"mux-fail-init")) testDriverInitFail=true;
        if (!std::strcmp(mode,"mux-fail-loop")) testLoopFail=1;
        if (!std::strcmp(mode,"mux-fail-event")) testAddFail=1;
        if (!std::strcmp(mode,"mux-fail-map")) testMapFail=3;
        assert(startMux()==-1);pump();
        // An unpublished failed provider cannot generate a registry match.
        // Consumer stays explicitly waiting; no timer, samples or stale ref.
        assert(b.waiting && !b.fIrqAttached && b.reads==1 && !b.fTimer->requests);
        assert(mappings.empty() && !irqService && platform.children.empty());
        cleanupBattery(b);assertNoLeaks();
    } else if (!std::strcmp(mode,"keys") || !std::strcmp(mode,"keys-worker-fail")) {
        assert(startMux()==0);backendStart=std::strcmp(mode,"keys-worker-fail")!=0;
        sweet_keys_init();pump();assert(backendStarts==1);
        if (backendStart) {
            assert(testInstance->bindings[0].notify && backendRunning);
            inject(248,1);pump();assert(backendEvents==1);
        } else assert(!testInstance->bindings[0].notify && !backendRunning);
        // Terminate mux before keys; connection must join backend and release provider.
        endProvider();assert(!backendRunning);
        if (backendStart) assert(backendStops>0);
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        assert(backendStops>0);
        assertNoLeaks();
    } else if (!std::strcmp(mode,"parallel")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);sweet_keys_init();pump();
        assert(b.fIrqAttached && backendRunning && testInstance->bindings[0].notify);
        inject(248,1);inject(269,0x10);pump();
        assert(backendEvents && b.fIrqSamples==1);
        cleanupBattery(b);inject(248,1);pump();assert(backendRunning && backendEvents==2);
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strcmp(mode,"notifier-stop-race")) {
        assert(startMux()==0);SweetBattery b;startBattery(b);pump();
        auto *mux=testInstance;mux->retain();
        std::atomic<bool> go{true};
        std::thread notifier([&] { while (go.load()) { deliver(gIOFirstMatchNotification,mux);deliver(gIOTerminatedNotification,mux); } });
        cleanupBattery(b);go=false;notifier.join();
        unsigned before=notifierCalls;deliver(gIOTerminatedNotification,mux);assert(notifierCalls==before);
        endProvider();mux->release();assertNoLeaks();
    } else if (!std::strcmp(mode,"provider-stop-race")) {
        assert(startMux()==0);SweetBattery b;startBattery(b);pump();
        std::atomic<bool> run{true};
        std::thread irq([&] { while (run.load()) sweet_spmi_mux_irq(513); });
        std::thread consumer([&] { cleanupBattery(b); });
        endProvider();consumer.join();run=false;irq.join();assertNoLeaks();
    } else if (!std::strcmp(mode,"identity")) {
        assert(startMux()==0);int context=7;
        assert(testAttach(1,1,batteryNotify,&context)==0);
        testInstance->detachConsumer(&keyOwner,1,&context);assert(testInstance->bindings[1].notify);
        testDetach(1,&context);assert(!testInstance->bindings[1].consumer);
        endProvider();assertNoLeaks();
    } else if (!std::strcmp(mode,"hardware") || !std::strcmp(mode,"timeout") || !std::strncmp(mode,"board-",6)) {
        char name[]="mux-test";char *args[]={name,const_cast<char *>(mode)};
        hardwareCheck(2,args);endProvider();assertNoLeaks();
    } else assert(false);
    assert(liveObjects==4); // static platform, bool and two direct-test owners
    std::printf("PASS: registry consumers %s\n",mode);
}
'''
# Private layout is exposed only for host assertions; actual sources stay private.
units=strip(header).replace('private:','public:')+strip(mux)+strip(client_header).replace('private:','public:')+strip(client)
units+=adapters+strip(keys)+startup_adapter+registry_impl
units+='\n#define state (testInstance->state)\n#define routes (testInstance->routes)\n#define bindings (testInstance->bindings)\n#define ackRoute(r) testInstance->ackRoute(r)\n'
# Field aliases are only for unchanged legacy scenarios, never new lifecycle code.
units+=legacy_board+legacy_checks+'\n#undef state\n#undef routes\n#undef bindings\n#undef ackRoute\n'+checks
with tempfile.TemporaryDirectory(prefix='mh2lm-mux-consumers-') as d:
    src=Path(d)/'test.cpp';binary=Path(d)/'test'
    src.write_text('#define SWEET_MUX_SERVICE_TEST 1\n#define ARM64_BOARD_CONFIG_SWEET 1\n'+prelude+units)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-pthread',
                    '-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
    scenarios=['early','late','cancel-before-match','term-before-attach','term-after-attach',
               'notifier-fail-first','notifier-fail-second','attach-fail','keys','keys-worker-fail',
               'parallel','identity','notifier-stop-race','provider-stop-race','client-event-fail',
               'mux-fail-missing','mux-fail-init','mux-fail-loop','mux-fail-event','mux-fail-map',
               'hardware','timeout','board-no-keys','board-key-fail','board-key-rollback',
               'board-key-detach','board-arm-fail']
    # LSan cannot operate in this sandbox's ptrace environment. Explicit
    # object/registry/notifier/MMIO accounting accompanies ASAN/UBSAN instead.
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    for case in scenarios:
        subprocess.run([str(binary),case],env=env,check=True)
    for _ in range(20):
        for case in ['notifier-stop-race','provider-stop-race']:
            subprocess.run([str(binary),case],env=env,check=True,stdout=subprocess.DEVNULL)
    print('PASS: 40 additional notification/IRQ/provider/consumer teardown races')
