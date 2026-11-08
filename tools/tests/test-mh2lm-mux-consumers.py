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
adc=read('iokit/Kernel/arm/SweetPmicAdc5.cpp')
adc_header=read('bsd/dev/sweet_adc5.h')
adc_service_header=read('bsd/dev/sweet_adc5_service.h')
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
prelude=prelude.replace('kIOReturnNotPermitted=2,','kIOReturnBusy=6, kIOReturnNoMemory=5, kIOReturnNotPermitted=2,')
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
    unsigned messages=0;
    void messageClients(unsigned) { ++messages; }
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
if 'controller transactions serialized across keys and battery' in mux:
    prelude=prelude.replace('static unsigned lockDepth, lockedWrites, failReadPer, timeoutApid;',
        'static thread_local unsigned lockDepth; static unsigned lockedWrites, failReadPer, timeoutApid;\nstatic std::mutex controllerLock;')
    prelude=prelude.replace('extern "C" void sweet_spmi_irq_lock() { assert(lockDepth++ == 0); }',
        'extern "C" int sweet_spmi_init() { return 0; }\nextern "C" void sweet_spmi_irq_lock() { controllerLock.lock(); assert(lockDepth++ == 0); }')
    prelude=prelude.replace('extern "C" void sweet_spmi_irq_unlock() { assert(--lockDepth == 0); }',
        'extern "C" void sweet_spmi_irq_unlock() { assert(--lockDepth == 0); controllerLock.unlock(); }')
    prelude=prelude.replace('if (reg==0 && ppids.count(apid)) {',
        'if (reg==0 && ppids.count(apid)) {\n            assert(lockDepth==1); // key RW commands must use the same lock too')
    prelude=prelude.replace('unsigned apid=(a-obs)/0x80, byte=(value>>4)&255;',
        'assert(lockDepth==1); // key observer command through final data read\n        unsigned apid=(a-obs)/0x80, byte=(value>>4)&255;')
prelude=prelude.replace('static bool dropPdcEnable;', 'static bool dropPdcEnable, dropKeyTypeWrite;')
prelude=prelude.replace('else pmic[address]=dataByte;',
    'else if (dropKeyTypeWrite && apid==261 && byte+i==0x11) { dropKeyTypeWrite=false; }\n                else pmic[address]=dataByte;')
prelude=prelude.replace('static bool dropPdcEnable, dropKeyTypeWrite;',
    'static bool dropPdcEnable, dropKeyTypeWrite;\nstatic unsigned failPmicApid=~0u, failPmicReg, failPmicData=256, failCloseApid=~0u, stickyPendingApid=~0u;')
prelude=prelude.replace('    mmio[a]=value;\n    if (a>=ch',
    '    if (a>=ch && a<ch+512*0x10000ULL) {\n'
    '        unsigned apid=(a-ch)/0x10000, reg=(a-ch)%0x10000;\n'
    '        if (reg==0x100 && value==0 && apid==failCloseApid) return;\n'
    '        if (reg==0x108 && apid==stickyPendingApid) return;\n'
    '    }\n    mmio[a]=value;\n    if (a>=ch')
prelude=prelude.replace('            uint32_t data=mmio[b+0x10];',
    '            uint32_t data=mmio[b+0x10];\n'
    '            if (apid==failPmicApid && byte==failPmicReg && (failPmicData==256 || (data&255)==failPmicData)) { mmio[b+8]=3; return; }')
prelude=prelude.replace('#define SWEET_LOG_WARN(...) ((void)0)', r'''
static std::atomic<unsigned> boundShutdownWarnings{0};
template<typename... Args> static void testWarn(const char *format, Args...) {
    if (std::strstr(format, "shutdown revoking attached consumer group=")) ++boundShutdownWarnings;
}
#define SWEET_LOG_WARN(...) testWarn(__VA_ARGS__)
''')

startup_adapter=startup_adapter.replace('class SweetBattery : public OSObject','class SweetBattery : public IOService')
startup_adapter=startup_adapter.replace('fMuxWatching=false','fTimerAdded=true')
# This suite substitutes sampling, not the new runtime's protection timer. The
# runtime/controller suite separately exercises actual monitor and stop methods.
if 'SweetBattery::stopRuntime' in battery:
    startup_adapter=startup_adapter.replace('public:', '''public: void stopRuntime() {} unsigned fRuntimeMode=0, fRuntimeRequested=0;
    SweetBattController fController={}; SweetBattSnapshot fSnapshot={}; unsigned fLastReasons=0,fLastPercent=70,fLastMv=4000;
    unsigned withdrawals=0;
    void publishRuntime() {
        assert(!fSnapshot.valid && !fSnapshot.ccFromEvent && !fSnapshot.apsdFromSource);
        assert(!fSnapshot.currentValid && !fSnapshot.socValid && !fSnapshot.voltageValid);
        assert(fController.fault && fController.state==SB_FAULT); ++withdrawals;
    }
    void publish(unsigned,unsigned,int,bool external,bool capable,bool charging,bool full,bool measured,bool currentValid) {
        assert(!external && !capable && !charging && !full && !measured && !currentValid);++withdrawals;
    }
''', 1)
    startup_adapter=read('bsd/dev/sweet_charger.h')+'\n'+strip(read('bsd/dev/sweet_battery_runtime.h'))+'\n'+startup_adapter
    startup_adapter=startup_adapter.replace('IOInterruptEventSource *fIrqEvent;',
        'IOInterruptEventSource *fIrqEvent; IOTimerEventSource *fMonitor=nullptr;')

startup_adapter=startup_adapter.replace('IOInterruptEventSource *fIrqEvent;', 'IOInterruptEventSource *fIrqEvent;\n    SweetSpmiMuxClient *fMuxClient=nullptr;')
# ADC subscription is covered by its own actual-client suite; this adapter tests
# only battery's mux/IRQ lifecycle, keeping the optional ADC binding absent.
if 'SweetBattery::publishAdc' in battery:
    startup_adapter=startup_adapter.replace('public:', 'public: struct AdcStub { void invalidate() {} }; AdcStub *fAdcClient=nullptr; void publishAdc() {}\n',1)

startup_adapter=startup_adapter.replace('fIrqEvent=IOInterruptEventSource::interruptEventSource(this, irqAction, nullptr, 0);','fIrqEvent=IOInterruptEventSource::interruptEventSource(this, irqAction, nullptr, 0);\n        assert(fLoop->addEventSource(fIrqEvent)==0);')
startup_adapter=re.sub(r'struct IOTimerEventSource \{.*?\n\};', r"""
struct IOTimerEventSource : IOInterruptEventSource {
    unsigned requests=0, delayMS=0; bool armed=false;
    using TimerAction=void (*)(OSObject *,IOTimerEventSource *);
    TimerAction timerAction=nullptr;
    IOTimerEventSource() : IOInterruptEventSource(nullptr,nullptr) {}
    static IOTimerEventSource *timerEventSource(OSObject *o,TimerAction a) {
        auto *t=new IOTimerEventSource;t->owner=o;t->timerAction=a;return t;
    }
    void setTimeoutMS(unsigned ms) { ++requests;delayMS=ms;armed=true; }
    void cancelTimeout() { armed=false; }
};
""",startup_adapter,flags=re.S)
startup_adapter='using IOPMPowerSource=IOService;\n'+re.search(r'^#define SWEET_BATT_POLL_MS\b.*$',battery,re.M)[0]+'\n'+startup_adapter
for name in ['failIrq','irqNotify','irqAction','tryAttachIrq','startIrqAction','timerFired','stopPolling']:
    startup_adapter+=function(battery,'SweetBattery::'+name)

adapters=r'''
static SweetSpmiInterruptMux *testInstance;
static bool testDriverInitFail;
static bool hasIrqOwner() { return irqService!=nullptr; }
static unsigned backendPrepares,backendStarts,backendStops,backendEvents;
static bool holdPrepare;
static std::atomic<bool> prepareEntered{false},prepareProceed{false};
static bool backendPrepare=true,backendStart=true,backendRunning=false;
static unsigned adcMode;
static bool PE_parse_boot_argn(const char *key,void *value,size_t size) {
    if (!std::strcmp(key,"sweetbattadcconv")) { assert(size==sizeof(adcMode)); std::memcpy(value,&adcMode,size); return true; }
    return false;
}
extern "C" bool sweet_keys_prepare() {
    ++backendPrepares;
    assert(!testInstance->bindings[0].notify && !backendStarts);
    if (holdPrepare) {
        prepareEntered=true;
        while (!prepareProceed.load()) std::this_thread::yield();
    }
    return backendPrepare;
}
extern "C" bool sweet_keys_start() { assert(backendPrepares==1 && testInstance->bindings[0].notify); ++backendStarts;backendRunning=backendStart;return backendStart; }
extern "C" void sweet_keys_stop() { ++backendStops;backendRunning=false; }
extern "C" void sweet_keys_event(unsigned) { assert(backendRunning); ++backendEvents; }
'''

adc_adapter=r'''
static constexpr unsigned kIOMessageServicePropertyChange=1;
static SweetPmicAdc5 *adcInstance;
static unsigned adcBegins,adcAborts,adcAdvances,adcCaptures;
static bool adcCaptureFail;
static bool adcBeginFail,adcResultFail,adcAbortFail;
int sweet_spmi_adc5_begin(sweet_adc5_session *s,sweet_adc5_channel c) {
    ++adcBegins;if(adcBeginFail)return -1;
    assert(!s->active);s->active=1;s->channel=c;s->eoc_count=0;s->started_ns=fakeNs;
    s->deadline_ns=fakeNs+100000000ULL;s->settle_until_ns=fakeNs+20000000ULL;
    s->phase=c==SWEET_ADC5_USBIN?SWEET_ADC5_WAIT_SETTLE:SWEET_ADC5_RESULT;return 0;
}
int sweet_spmi_adc5_abort(sweet_adc5_session *s) {
    if(s->active)++adcAborts;s->active=0;s->phase=SWEET_ADC5_FAILED;
    s->restore_error=adcAbortFail?-9:0;return s->restore_error;
}
int sweet_spmi_adc5_advance(sweet_adc5_session *s,sweet_adc5_cause cause,sweet_adc5_measurement *out) {
    assert(s->active);++adcAdvances;
    if(adcResultFail) { s->active=0;return -8; }
    if(cause==SWEET_ADC5_SETTLED) {
        assert(s->phase==SWEET_ADC5_WAIT_SETTLE && fakeNs>=s->settle_until_ns);
        s->phase=SWEET_ADC5_GROUND;s->deadline_ns=fakeNs+100000000ULL;return SWEET_ADC5_MORE;
    }
    assert(cause==SWEET_ADC5_EOC);++s->eoc_count;
    if(s->phase==SWEET_ADC5_GROUND) { s->phase=SWEET_ADC5_RESULT;s->deadline_ns=fakeNs+100000000ULL;return SWEET_ADC5_MORE; }
    assert(s->phase==SWEET_ADC5_RESULT);
    *out={};out->channel=s->channel;out->microvolts=s->channel==SWEET_ADC5_USBIN?5000000:800000;
    out->raw=1234;out->eoc_count=s->eoc_count;out->started_ns=s->started_ns;out->completed_ns=fakeNs;
    s->active=0;s->phase=SWEET_ADC5_DONE;return 0;
}
int sweet_spmi_adc5_capture_fault(sweet_adc5_fault *out) {
    ++adcCaptures;assert(adcInstance && adcInstance->failed && !adcInstance->ready);
    assert(!adcInstance->valid[0] && !adcInstance->valid[1] && !adcAborts);
    if(adcCaptureFail)return SWEET_SPMI_IO;
    *out={};out->entered_ns=out->locked_ns=out->completed_ns=fakeNs;return 0;
}
'''
registry_impl=r'''
void IOService::registerService(unsigned) {
    if (dynamic_cast<SweetPmicAdc5Device *>(this)) {
        auto *service=new SweetPmicAdc5;assert(service->init());assert(service->attach(this));
        adcInstance=service;
        if(service->start(this))service->mockStarted=true;
        else {adcInstance=nullptr;service->terminate(2);}
        service->release();return;
    }
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
static IOReturn fireAdcTimer(OSObject *owner,void *,void *,void *,void *) {
    auto *a=static_cast<SweetPmicAdc5 *>(owner);assert(a->timer->armed);
    fakeNs+=uint64_t(a->timer->delayMS)*1000000ULL;a->timer->armed=false;
    a->timer->timerAction(a,a->timer);return 0;
}
static void adcCheck(const char *mode) {
    // Register a new owned EOC lane without disturbing the established key/battery lanes.
    ppids[280]=0x231;mmio[SWEET_DEV_SPMI_CORE_PA+0x900+4*280]=0x01000000|(0x231<<8);
    assert(startMux()==0);SweetBattery b;startBattery(b);sweet_keys_init();pump();
    assert(b.fIrqAttached && backendRunning);
    adcMode=!std::strcmp(mode,"adc-idle")?0:!std::strcmp(mode,"adc-usb")?2:!std::strcmp(mode,"adc-sequence")?3:1;
    bool sequence=adcMode==3;
    bool earlyTimer=!std::strcmp(mode,"adc-early-timer");
    bool phaseTimer=!std::strcmp(mode,"adc-phase-timer");
    bool captureFail=!std::strcmp(mode,"adc-capture-fail");
    if(phaseTimer)adcMode=2;
    adcCaptureFail=captureFail;
    adcBeginFail=!std::strcmp(mode,"adc-begin-fail");
    bool setupFail=!std::strcmp(mode,"adc-loop-fail") || !std::strcmp(mode,"adc-timer-add-fail") || !std::strcmp(mode,"adc-notifier-fail");
    if(!std::strcmp(mode,"adc-loop-fail"))testLoopFail=1;
    if(!std::strcmp(mode,"adc-timer-add-fail"))testAddFail=1;
    if(!std::strcmp(mode,"adc-notifier-fail"))failNotifierAt=2;
    if(setupFail) {
        assert(sweet_adc5_init()==-1 && !adcInstance);pump();
        assert(!adcBegins && !testInstance->bindings[2].notify && b.fIrqAttached && backendRunning);
        cleanupBattery(b);while(!platform.children.empty())platform.children.back()->terminate(2);
        pump();assertNoLeaks();return;
    }
    assert(!sweet_adc5_init());auto *a=adcInstance;assert(a);a->retain();
    // Subscribe is gated, so immediate registry replay cannot call into an unassigned connection.
    assert(a->connection);pump();
    if(adcBeginFail) {
        assert(a->failed && !a->session.active && adcBegins==1 && !testInstance->bindings[2].notify);
    } else if (!std::strcmp(mode,"adc-idle")) {
        assert(a->ready && !a->session.active && !adcBegins);
        unsigned token=0;assert(!a->requestSample(SWEET_ADC5_BAT_ID,&token));assert(adcBegins==1 && token==1);
    } else assert(a->ready && a->session.active && adcBegins==1);
    if(!adcBeginFail) {
        assert(a->requestSample(SWEET_ADC5_BAT_ID)==kIOReturnBusy);
        assert(adcBegins==1);
        sweet_adc5_measurement sample{};sample.raw=0xcafe;
        assert(a->copySample(SWEET_ADC5_BAT_ID,&sample)==kIOReturnNotReady && sample.raw==0xcafe);
        if(earlyTimer) {
            auto due=a->session.deadline_ns;auto advances=adcAdvances;
            a->timer->armed=false;a->timer->timerAction(a,a->timer);
            assert(a->timer->armed && a->session.deadline_ns==due && !adcCaptures && adcAdvances==advances && a->ready);
        }
        if(!std::strcmp(mode,"adc-timeout") || !std::strcmp(mode,"adc-restore-fail") || captureFail) {
            adcAbortFail=!std::strcmp(mode,"adc-restore-fail");
            a->loop->runAction(fireAdcTimer,a);
            assert(a->failed && adcAborts==1 && adcCaptures==1 && !testInstance->bindings[2].notify);
            assert(!a->valid[0] && !a->valid[1] && !a->timer->armed);
        } else if(!std::strcmp(mode,"adc-stop-active")) {
            inject(280,1); // queued EOC is cancelled/synchronized before owner release
            platform.children.back()->terminate(2);pump();
            assert(adcAborts==1 && !a->session.active && !testInstance->bindings[2].notify);
        } else if(!std::strcmp(mode,"adc-provider-term")) {
            testInstance->inactive=true;registryTerminated(testInstance);
            assert(a->session.active);pump();
            assert(a->failed && adcAborts==1 && !a->session.active && !testInstance->bindings[2].notify);
        } else {
            if(adcMode==2) {
                a->loop->runAction(fireAdcTimer,a);inject(280,1);pump();assert(a->session.active && !a->valid[1]);
                if(phaseTimer) {
                    // An old timer delivery must rearm the RESULT deadline, never abort.
                    auto due=a->session.deadline_ns;auto advances=adcAdvances;
                    a->timer->armed=false;a->timer->timerAction(a,a->timer);
                    assert(a->session.phase==SWEET_ADC5_RESULT && a->session.deadline_ns==due && a->timer->armed);
                    assert(!adcCaptures && adcAdvances==advances && a->ready);
                }
            }
            adcResultFail=!std::strcmp(mode,"adc-result-fail");
            inject(280,1);pump();
            if(adcResultFail) assert(a->failed && !testInstance->bindings[2].notify);
            else {
                SweetMuxIrqTrace trace={};
                assert(a->connection->copyInterruptTrace(SWEET_ADC5_IRQ_EOC,&trace));
                assert(trace.hits==(adcMode==2?2u:1u) && (sequence?trace.hits==a->traceBaselineHits:trace.hits>a->traceBaselineHits));
                assert(trace.gicNs<=trace.scanNs && trace.scanNs<=trace.ackNs && trace.ackNs<=trace.notifyNs);
                auto saved=trace;
                assert(!a->connection->copyInterruptTrace(0,&trace) && !memcmp(&trace,&saved,sizeof(trace)));
                // Writer in-progress: three bounded attempts, no gate/IO, untouched output.
                auto seq=testInstance->traces[15].sequence;
                __atomic_store_n(&testInstance->traces[15].sequence,seq|1u,__ATOMIC_RELEASE);
                assert(!a->connection->copyInterruptTrace(SWEET_ADC5_IRQ_EOC,&trace) && !memcmp(&trace,&saved,sizeof(trace)));
                __atomic_store_n(&testInstance->traces[15].sequence,seq,__ATOMIC_RELEASE);
                unsigned channel=adcMode==2?2:1;
                assert(!a->copySample(static_cast<sweet_adc5_channel>(channel),&sample));
                assert(sample.sequence==1 && a->status()==kIOReturnSuccess);
                assert(sample.microvolts==(channel==2?5000000u:800000u) && (sequence || (!a->session.active && !a->timer->armed)));
                assert(a->messages==2); // READY plus first completed sample
                assert(a->properties[adcMode==2?"SweetADC5USBINValid":"SweetADC5BATIDValid"]=="true");
                if(sequence) {
                    assert(a->session.active && a->session.channel==SWEET_ADC5_USBIN && adcBegins==2);
                    a->loop->runAction(fireAdcTimer,a);inject(280,1);pump();inject(280,1);pump();
                    assert(a->valid[0] && a->valid[1] && !a->session.active && a->messages==3);
                }
                unsigned extra=0;
                if(!std::strcmp(mode,"adc-trace-race")) {
                    std::atomic<bool> done{false};std::atomic<unsigned> observations{0};
                    std::thread reader([&]{
                        while(!done.load()) {
                            SweetMuxIrqTrace got={};
                            if(testInstance->copyInterruptTrace(SWEET_MUX_ADC5,SWEET_ADC5_IRQ_EOC,&got)) {
                                assert(got.sequence==2*got.hits && got.apid==280);
                                assert(got.gicNs<=got.scanNs && got.scanNs<=got.ackNs && got.ackNs<=got.notifyNs);
                                ++observations;
                            }
                        }
                    });
                    while(!observations.load())std::this_thread::yield();
                    for(extra=0;extra<200;++extra){inject(280,1);pump();}
                    done=true;reader.join();assert(observations>0);
                    assert(!a->session.active && adcBegins==1 && a->messages==2 && !adcCaptures);
                }
                // Unsolicited EOC cannot overwrite the cached sample or start another request.
                inject(280,1);pump();assert(a->strayEoc==extra+1 && adcBegins==(sequence?2u:1u) && a->messages==(sequence?3u:2u));
            }
        }
    }
    if(std::strcmp(mode,"adc-provider-term")) {
        inject(269,0x10);inject(248,1);pump();
        assert(!b.fIrqFailed && b.fIrqNotifications==1 && backendEvents==1 && backendRunning);
    }
    if(!a->isInactive())platform.children.back()->terminate(2);
    assert(a->properties["SweetADC5IRQHealthy"]=="false" && a->properties["SweetADC5BATIDValid"]=="false" && a->properties["SweetADC5USBINValid"]=="false");
    a->release();cleanupBattery(b);
    while(!platform.children.empty())platform.children.back()->terminate(2);
    pump();assertNoLeaks();
}

static unsigned batchCalls[MUX_GROUPS],batchMasks[MUX_GROUPS];
static bool batchArmed,batchReassert;
static void batchNotify(void *context,unsigned events) {
    if(!batchArmed)return;
    unsigned group=*static_cast<unsigned *>(context);
    assert(!(events&SWEET_MUX_FAILED));++batchCalls[group];batchMasks[group]|=events;
    // All first-pass IRQ latches must be cleared before ANY consumer is
    // allowed to begin its own telemetry; assert this inside real notify.
    if(!batchReassert)for(unsigned apid:{248u,261u,269u,271u,273u,280u})
        { auto pending=hwRead(SWEET_DEV_SPMI_CHNLS_PA+apid*0x10000,0x104);
          if(pending)fprintf(stderr,"batch group=%u apid=%u pending=%#x\n",group,apid,pending);
          assert(!pending); }
    if(group==MUX_BATTERY && batchCalls[group]==1) {
        batchReassert=true;
        mmio[SWEET_DEV_SPMI_CHNLS_PA+271*0x10000+0x104]|=4;
    }
}
static void batchCheck() {
    ppids[280]=0x231;mmio[SWEET_DEV_SPMI_CORE_PA+0x900+4*280]=0x01000000|(0x231<<8);
    assert(startMux()==0);unsigned contexts[]={0,1,2};
    for(unsigned group=0;group<MUX_GROUPS;++group)
        assert(!testAttach(group,group==0?7u:group==1?SWEET_BATT_IRQ_ALL:SWEET_ADC5_IRQ_EOC,batchNotify,&contexts[group]));
    pump();batchArmed=true;
    inject(248,3);inject(261,1);inject(269,0x50);inject(271,0x7d);inject(273,2);inject(280,1);pump();
    assert(batchCalls[0]==1 && batchMasks[0]==7);
    assert(batchCalls[1]==2 && batchMasks[1]==(SWEET_BATT_IRQ_USB|SWEET_BATT_IRQ_SOURCE|SWEET_BATT_IRQ_TYPEC|SWEET_BATT_IRQ_WDOG));
    assert(batchCalls[2]==1 && batchMasks[2]==SWEET_ADC5_IRQ_EOC);
    assert(testInstance->state==1 && testInstance->traces[15].hits==1);
    SweetMuxIrqTrace trace={};assert(testInstance->copyInterruptTrace(2,SWEET_ADC5_IRQ_EOC,&trace));
    assert(trace.scanNs<=trace.ackNs && trace.ackNs<=trace.notifyNs);
    batchArmed=false;
    for(unsigned group=0;group<MUX_GROUPS;++group)testDetach(group,&contexts[group]);
    endProvider();assertNoLeaks();
}
int main(int argc,char **argv) {
    assert(argc>1);const char *mode=argv[1];initialize();
    if (!std::strcmp(mode,"ack-batch")) batchCheck();
    else if (!std::strncmp(mode,"adc-",4)) adcCheck(mode);
    else if (!std::strcmp(mode,"early") || !std::strcmp(mode,"late")) ownerCleanup(mode);
    else if (!std::strcmp(mode,"term-before-provider-stop")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);sweet_keys_init();pump();
        auto *mux=testInstance;mux->retain();
        assert(b.fIrqAttached && backendRunning && mux->bindings[0].notify && mux->bindings[1].notify);
        // Native phase 1 can notify inactive before provider willTerminate/stop.
        // The notification itself must only queue, retaining both bindings.
        mux->inactive=true;registryTerminated(mux);
        assert(mux->bindings[0].notify && mux->bindings[1].notify && backendRunning);
        pump();assert(!mux->bindings[0].notify && !mux->bindings[1].notify);
        assert(b.fIrqFailed && !backendRunning && !boundShutdownWarnings);
        cleanupBattery(b);endProvider();
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        mux->release();assert(!boundShutdownWarnings);assertNoLeaks();
    } else if (!std::strcmp(mode,"provider-stop-with-consumers")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);sweet_keys_init();pump();
        auto *mux=testInstance;mux->retain();
        // Opposite scheduling: provider stops before consumer loops can detach.
        endProvider();assert(boundShutdownWarnings==2);
        assert(!mux->bindings[0].consumer && !mux->bindings[0].notify && !mux->bindings[0].context);
        assert(!mux->bindings[1].consumer && !mux->bindings[1].notify && !mux->bindings[1].context);
        assert(b.fIrqFailed && !backendRunning);
        cleanupBattery(b);
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        mux->release();assertNoLeaks();
    } else if (!std::strcmp(mode,"owner-stop-before-provider")) {
        assert(startMux()==0);sweet_keys_init();pump();assert(backendRunning);
        platform.children.back()->terminate(2);pump();
        assert(!backendRunning && !testInstance->bindings[0].notify);
        endProvider();assert(!boundShutdownWarnings);assertNoLeaks();
    } else if (!std::strcmp(mode,"key-type-mismatch")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);pump();assert(b.fIrqAttached);
        // Model this phone's exact symptom: successful command status but
        // SET_TYPE remains 0x42 instead of 0x43. Never accept a wrong edge.
        pmic[(0x0c5<<8)|0x11]=0x42;dropKeyTypeWrite=true;
        sweet_keys_init();pump();
        assert(!backendRunning && !backendStarts && !testInstance->bindings[0].notify);
        assert(pmic[(0x0c5<<8)|0x11]==0x42);
        assert(testInstance->bindings[1].notify && b.healthy);
        inject(269,0x10);pump();assert(b.fIrqSamples==1 && !b.fIrqFailed);
        cleanupBattery(b);endProvider();
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        assertNoLeaks();
    }
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
        assert(b.withdrawals==1); // failure also withdraws the UI source state
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
        sweet_keys_init();assert(!backendPrepares && !backendStarts);
        pump();assert(backendPrepares==1 && backendStarts==1);
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
    } else if (!std::strcmp(mode,"keys-prepare-fail")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);pump();assert(b.fIrqAttached);
        backendPrepare=false;sweet_keys_init();assert(!backendPrepares);pump();
        assert(backendPrepares==1 && !backendStarts && !backendRunning);
        assert(!testInstance->bindings[0].notify && testInstance->bindings[1].notify);
        inject(269,0x10);pump();assert(b.fIrqSamples==1 && b.healthy);
        cleanupBattery(b);endProvider();
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strcmp(mode,"keys-late-match") || !std::strcmp(mode,"keys-cancel-before-match")) {
        sweet_keys_init();pump();assert(!backendPrepares && !backendStarts);
        bool cancel=!std::strcmp(mode,"keys-cancel-before-match");
        if (cancel) platform.children.back()->terminate(2);
        assert(startMux()==0);assert(!backendPrepares);pump();
        assert(backendPrepares==(cancel?0u:1u) && backendRunning==!cancel);
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        pump();assertNoLeaks();
    } else if (!std::strcmp(mode,"keys-term-during-prepare") || !std::strcmp(mode,"keys-stop-during-prepare")) {
        assert(startMux()==0);holdPrepare=true;sweet_keys_init();assert(!backendPrepares);
        auto *keys=static_cast<SweetPmicKeys *>(platform.children.back()->children.front());
        auto *client=keys->connection;
        std::thread dispatch([] { pump(); });
        while (!prepareEntered.load()) std::this_thread::yield();
        // Preparation holds the consumer gate, but not registry/mux locks.
        assert(!backendStarts && !testInstance->bindings[0].notify);
        if (!std::strcmp(mode,"keys-term-during-prepare")) {
            testInstance->inactive=true;registryTerminated(testInstance);
            prepareProceed=true;dispatch.join();
        } else {
            std::thread stop([&] { keys->stop(keys->getProvider()); });
            for (;;) {
                IOLockLock(client->lock);bool stopping=client->stopping;IOLockUnlock(client->lock);
                if (stopping) break;
                std::this_thread::yield();
            }
            prepareProceed=true;dispatch.join();stop.join();
        }
        assert(backendPrepares==1 && !backendStarts && !backendRunning);
        assert(!testInstance->bindings[0].notify);
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        pump();assert(backendStops>0);assertNoLeaks();
    } else if (!std::strcmp(mode,"rollback-isolate-keys")) {
        SweetBattery b;startBattery(b);assert(startMux()==0);pump();assert(b.fIrqAttached);
        pmic[(0x0c5<<8)|0x11]=0x42;dropKeyTypeWrite=true;
        // Desired SET_TYPE write is dropped; restoring the old byte also fails.
        failPmicApid=261;failPmicReg=0x11;failPmicData=0x42;
        sweet_keys_init();pump();
        assert(testInstance->state==1 && testInstance->isolatedGroups==1);
        assert(!backendStarts && !backendRunning && !testInstance->bindings[0].notify);
        assert(hwRead(SWEET_DEV_SPMI_CHNLS_PA+248*0x10000,0x100)==0);
        assert(hwRead(SWEET_DEV_SPMI_CHNLS_PA+261*0x10000,0x100)==0);
        assert(testInstance->bindings[1].notify && b.healthy);
        inject(269,0x10);pump();assert(b.fIrqSamples==1 && !b.fIrqFailed);
        assert(testAttach(0,7,keyNotify,&keyContext)!=0); // no silent rebind
        failPmicApid=~0u;cleanupBattery(b);endProvider();
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strncmp(mode,"detach-isolate-",15)) {
        SweetBattery b;startBattery(b);assert(startMux()==0);sweet_keys_init();pump();
        assert(backendRunning && b.fIrqAttached);
        bool timedOut=!std::strcmp(mode,"detach-isolate-timeout");
        bool gateFail=!std::strcmp(mode,"detach-isolate-gate-fail");
        bool pendingFail=!std::strcmp(mode,"detach-isolate-pending-fail");
        if (timedOut) timeoutApid=269;
        if (gateFail) failCloseApid=269;
        if (pendingFail) stickyPendingApid=269;
        // EN_CLR fails while an IRQ is already queued. Containment must clear
        // the pending local source and preserve the queued worker's rearm.
        failPmicApid=269;failPmicReg=0x16;inject(269,0x10);
        cleanupBattery(b);pump();
        assert(!testInstance->bindings[1].notify);
        if (gateFail || pendingFail) {
            assert(testInstance->state==3 && !backendRunning);
            assert(!hwRead(0x17a00000,0x100+4*(513/32)));
        } else {
            assert(testInstance->state==1 && backendRunning && testInstance->isolatedGroups==2);
            assert(hwRead(SWEET_DEV_SPMI_CHNLS_PA+269*0x10000,0x100)==0);
            assert(!hwRead(SWEET_DEV_SPMI_CHNLS_PA+269*0x10000,0x104));
            if (timedOut) {
                assert(testInstance->routes[3].writePoisoned);unsigned before=lockedWrites;
                assert(!testInstance->ackRoute(testInstance->routes[3]));assert(lockedWrites==before);
            }
            unsigned before=backendEvents;inject(248,1);pump();assert(backendEvents==before+1);
            int context=1;assert(testAttach(1,1,batteryNotify,&context)!=0);
        }
        timeoutApid=0;failPmicApid=failCloseApid=stickyPendingApid=~0u;
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        pump();assertNoLeaks();
    } else if (!std::strcmp(mode,"isolated-apid-rebind")) {
        assert(startMux()==0);int context=1;assert(testAttach(1,1,batteryNotify,&context)==0);
        failPmicApid=269;failPmicReg=0x16;testDetach(1,&context);
        assert(testInstance->isolatedGroups==2 && testInstance->state==1);
        // A future route in another group must not reopen an isolated APID.
        testInstance->routes[0].sid=2;testInstance->routes[0].per=0x13;
        assert(attachKeys()!=0 && !testInstance->bindings[0].notify);
        assert(hwRead(SWEET_DEV_SPMI_CHNLS_PA+269*0x10000,0x100)==0);
        failPmicApid=~0u;endProvider();assertNoLeaks();
    } else if (!std::strcmp(mode,"isolate-shared-apid")) {
        assert(startMux()==0);assert(attachKeys()==0);int context=1;
        assert(testAttach(1,1,batteryNotify,&context)==0);
        auto &route=testInstance->routes[3];route.active=false;
        unsigned original=route.apid;route.apid=248; // synthetic future cross-group APID
        assert(!testInstance->isolateGroup(1));
        assert(hwRead(SWEET_DEV_SPMI_CHNLS_PA+248*0x10000,0x100)==1);
        route.apid=original;testDetach(1,&context);testDetach(0,&keyContext);
        endProvider();assertNoLeaks();
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
    } else if (!std::strcmp(mode,"battery-typec-read-fail")) {
        assert(startMux()==0);sweet_keys_init();pump();assert(backendRunning);
        failReadPer=0x15;
        SweetBattery b;b.fIrqMode=4;b.fRuntimeRequested=b.fRuntimeMode=1;
        b.fSnapshot.valid=~0ULL;b.fSnapshot.ccFromEvent=b.fSnapshot.apsdFromSource=true;
        b.fSnapshot.currentValid=b.fSnapshot.voltageValid=b.fSnapshot.socValid=true;
        startBattery(b);pump();
        assert(b.fIrqFailed && !b.fRunning && !b.fIrqAttached && b.withdrawals==1);
        assert(!testInstance->bindings[1].notify && !b.fSnapshot.valid);
        assert(testInstance->bindings[0].notify && backendRunning);
        inject(248,1);pump();assert(backendRunning && testInstance->routes[0].hits==1);
        cleanupBattery(b);endProvider();
        while (!platform.children.empty()) platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strncmp(mode,"battery-watchdog",16)) {
        assert(startMux()==0);sweet_keys_init();pump();assert(backendRunning);
        bool bad=std::strcmp(mode,"battery-watchdog")!=0;
        pmic[(0x216<<8)|0x11]=pmic[(0x216<<8)|0x12]=pmic[(0x216<<8)|0x13]=0x80;
        if (!std::strcmp(mode,"battery-watchdog-read-fail")) failReadPer=0x16;
        if (!std::strcmp(mode,"battery-watchdog-foreign")) mmio[SWEET_DEV_SPMI_CFG_PA+0x700+4*273]=1;
        if (!std::strcmp(mode,"battery-watchdog-enabled")) pmic[(0x216<<8)|0x15]=2;
        SweetBattery b;b.fIrqMode=4;b.fRuntimeRequested=b.fRuntimeMode=1;b.fDependencyMode=4;
        startBattery(b);pump();
        if(bad){
            assert(b.fIrqFailed && !b.fRunning && !testInstance->routes[18].active);
            assert(!testInstance->bindings[1].notify);
        }else{
            assert(b.fIrqAttached && testInstance->routes[18].active);
            assert(pmic[(0x216<<8)|0x11]==0x82 && pmic[(0x216<<8)|0x12]==0x82);
            assert(pmic[(0x216<<8)|0x13]==0x80 && (pmic[(0x216<<8)|0x15]&2));
            auto count=b.fIrqSamples;inject(273,2);pump();
            assert(b.fWatchdogPending && b.fIrqSamples==count+1 && !b.fRuntimeInputEvents);
            assert(testInstance->routes[18].hits==1);
        }
        inject(248,1);pump();assert(backendRunning && testInstance->routes[0].hits==1);
        cleanupBattery(b);
        assert(!testInstance->routes[18].active && testInstance->bindings[0].notify);
        assert(pmic[(0x216<<8)|0x11]==0x80 && pmic[(0x216<<8)|0x12]==0x80 && pmic[(0x216<<8)|0x13]==0x80);
        endProvider();while(!platform.children.empty())platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strncmp(mode,"battery-temp",12)) {
        assert(startMux()==0);sweet_keys_init();pump();
        bool bad=std::strcmp(mode,"battery-temp")!=0;
        pmic[(0x242<<8)|0x11]=pmic[(0x242<<8)|0x12]=pmic[(0x242<<8)|0x13]=0x01;
        if (!std::strcmp(mode,"battery-temp-read-fail")) failReadPer=0x42;
        if (!std::strcmp(mode,"battery-temp-enabled")) pmic[(0x242<<8)|0x15]=4;
        SweetBattery b;b.fIrqMode=4;b.fRuntimeRequested=b.fRuntimeMode=3;b.fTempIrq=true;
        startBattery(b);pump();
        if(bad){
            // Refused takeover or unreadable route: whole battery group fails visibly.
            assert(b.fIrqFailed && !b.fRunning && !testInstance->routes[19].active);
            assert(!testInstance->bindings[1].notify);
        }else{
            assert(b.fIrqAttached && testInstance->routes[19].active && testInstance->routes[18].active);
            assert(testInstance->bindings[1].events==SWEET_BATT_IRQ_ALL);
            // Both edges on bit2; unrelated FG RRADC bits untouched.
            assert(pmic[(0x242<<8)|0x11]==0x05 && pmic[(0x242<<8)|0x12]==0x05);
            assert(pmic[(0x242<<8)|0x13]==0x05 && (pmic[(0x242<<8)|0x15]&4));
            auto count=b.fIrqSamples;inject(285,4);pump();
            assert(b.fTempIrqs==1 && !b.fWatchdogPending && b.fIrqSamples==count+1 && !b.fRuntimeInputEvents);
            assert(testInstance->routes[19].hits==1);
        }
        inject(248,1);pump();assert(backendRunning && testInstance->routes[0].hits==1);
        cleanupBattery(b);
        assert(!testInstance->routes[19].active && testInstance->bindings[0].notify);
        assert(pmic[(0x242<<8)|0x11]==0x01 && pmic[(0x242<<8)|0x12]==0x01 && pmic[(0x242<<8)|0x13]==0x01);
        endProvider();while(!platform.children.empty())platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strcmp(mode,"battery-runtime-events")) {
        assert(startMux()==0);sweet_keys_init();pump();
        // Unregistered VPD/try bits must survive Type-C attach and detach.
        pmic[(0x215<<8)|0x11]=pmic[(0x215<<8)|0x12]=pmic[(0x215<<8)|0x13]=0x82;
        SweetBattery b;b.fIrqMode=4;b.fRuntimeRequested=b.fRuntimeMode=2;startBattery(b);pump();
        assert(b.fIrqAttached && backendRunning);
        assert(testInstance->bindings[1].events==(SWEET_BATT_IRQ_ALL & ~(SWEET_BATT_IRQ_WDOG | SWEET_BATT_IRQ_TEMP)));
        assert(!testInstance->routes[19].active); // temp-delta is opt-in (sweetbatttempirq=1)
        assert(!testInstance->routes[18].active); // default observer never takes bark ownership
        assert(testInstance->routes[7].active && testInstance->routes[8].active);
        assert(testInstance->routes[16].active && testInstance->routes[17].active);
        assert((pmic[(0x213<<8)|0x11]&0x5c)==0x5c);
        assert((pmic[(0x213<<8)|0x12]&0x5c)==0x5c);
        assert((pmic[(0x213<<8)|0x13]&0x5c)==0x1c);
        assert((pmic[(0x213<<8)|0x15]&0x5c)==0x5c);
        assert(!b.fMuxClient->hasPendingEvents(SWEET_BATT_IRQ_ALL));
        // Run the actual mux callback without yet draining the consumer loop.
        SweetSpmiMuxClient::routeNotify(b.fMuxClient,SWEET_BATT_IRQ_INPUT_FAULT);
        assert(b.fMuxClient->hasPendingEvents(SWEET_BATT_IRQ_ALL));pump();
        assert(!b.fMuxClient->hasPendingEvents(SWEET_BATT_IRQ_ALL));
        for(unsigned bit:{2u,3u}){
            auto before=b.fIrqSamples;inject(269,1u<<bit);pump();
            assert(b.fIrqSamples==before+1 && b.fRuntimeInputEvents==SWEET_BATT_IRQ_INPUT_FAULT);
            assert(!b.fController.fault && backendRunning);
        }
        assert(pmic[(0x215<<8)|0x11]==0xff && pmic[(0x215<<8)|0x12]==0xff);
        assert(pmic[(0x215<<8)|0x13]==0xca && pmic[(0x215<<8)|0x15]==0x7d);
        auto samples=b.fIrqSamples;inject(269,0x40);pump();
        assert(b.fIrqSamples==samples+1 && !b.fController.fault && backendRunning);
        assert(b.fRuntimeInputEvents==SWEET_BATT_IRQ_SOURCE);
        inject(266,1);pump();
        assert(b.fController.fault && b.fController.faultReasons==SB_R_HARDWARE);
        assert(b.fIrqSamples==samples+2 && backendRunning && !b.fIrqFailed);
        assert(b.fRuntimeInputEvents==0); // CHGR must not masquerade as input IRQ
        inject(269,0x10);pump();
        assert(b.fRuntimeInputEvents==SWEET_BATT_IRQ_USB);
        inject(269,0x50);pump();
        assert(b.fRuntimeInputEvents==(SWEET_BATT_IRQ_USB|SWEET_BATT_IRQ_SOURCE));
        for(unsigned bit:{0u,2u,3u,4u,5u,6u}){
            auto before=b.fIrqSamples;inject(271,1u<<bit);pump();
            assert(b.fIrqSamples==before+1 && b.fRuntimeInputEvents==SWEET_BATT_IRQ_TYPEC);
            assert(backendRunning && !b.fIrqFailed);
        }
        inject(271,0x74);inject(269,0x50);pump();
        assert(b.fRuntimeInputEvents==(SWEET_BATT_IRQ_TYPEC|SWEET_BATT_IRQ_USB|SWEET_BATT_IRQ_SOURCE));
        cleanupBattery(b);
        assert(!testInstance->routes[7].active && !testInstance->routes[8].active);
        assert(!testInstance->routes[16].active && !testInstance->routes[17].active);
        assert((pmic[(0x213<<8)|0x15]&0x5c)==0);
        assert((pmic[(0x215<<8)|0x15]&0x7d)==0);
        assert(pmic[(0x215<<8)|0x11]==0x82 && pmic[(0x215<<8)|0x12]==0x82 && pmic[(0x215<<8)|0x13]==0x82);
        endProvider();while(!platform.children.empty())platform.children.back()->terminate(2);
        assertNoLeaks();
    } else if (!std::strcmp(mode,"hardware") || !std::strcmp(mode,"timeout") || !std::strncmp(mode,"board-",6)) {
        char name[]="mux-test";char *args[]={name,const_cast<char *>(mode)};
        hardwareCheck(2,args);endProvider();assertNoLeaks();
    } else assert(false);
    assert(liveObjects==4); // static platform, bool and two direct-test owners
    std::printf("PASS: registry consumers %s\n",mode);
}
'''
# Private layout is exposed only for host assertions; actual sources stay private.
prelude+='\nextern \"C\" int sweet_spmi_adc5_irq_read(unsigned reg,uint8_t *out) { return sweet_spmi_read(2,0x3100|reg,out); }\n'
units=strip(read('bsd/dev/sweet_spmi.h'))+strip(header).replace('private:','public:')+strip(mux)+strip(client_header).replace('private:','public:')+strip(client)
units+=adapters+strip(keys).replace('private:','public:')+startup_adapter
units+=strip(adc_header)+strip(adc_service_header).replace('private:','public:')+adc_adapter+strip(adc)+registry_impl
units+='\n#define state (testInstance->state)\n#define routes (testInstance->routes)\n#define bindings (testInstance->bindings)\n#define ackRoute(r) testInstance->ackRoute(r)\n'
# Field aliases are only for unchanged legacy scenarios, never new lifecycle code.
units+=legacy_board+legacy_checks+'\n#undef state\n#undef routes\n#undef bindings\n#undef ackRoute\n'+checks
with tempfile.TemporaryDirectory(prefix='mh2lm-mux-consumers-') as d:
    src=Path(d)/'test.cpp';binary=Path(d)/'test'
    src.write_text('#define SWEET_MUX_SERVICE_TEST 1\n#define ARM64_BOARD_CONFIG_SWEET 1\n'+prelude+units)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-pthread',
                    '-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
    scenarios=['ack-batch','adc-trace-race','adc-early-timer','adc-phase-timer','adc-capture-fail','adc-idle','adc-bat-id','adc-usb','adc-sequence','adc-loop-fail','adc-timer-add-fail','adc-notifier-fail','adc-begin-fail','adc-timeout','adc-result-fail','adc-restore-fail','adc-stop-active','adc-provider-term','early','late','cancel-before-match','term-before-attach','term-after-attach',
               'rollback-isolate-keys','detach-isolate-ok','detach-isolate-timeout','detach-isolate-gate-fail',
               'detach-isolate-pending-fail','isolate-shared-apid','isolated-apid-rebind',
               'key-type-mismatch','keys-prepare-fail','keys-late-match','keys-cancel-before-match',
               'keys-term-during-prepare','keys-stop-during-prepare',
               'term-before-provider-stop','provider-stop-with-consumers','owner-stop-before-provider',
               'notifier-fail-first','notifier-fail-second','attach-fail','keys','keys-worker-fail',
               'parallel','identity','notifier-stop-race','provider-stop-race','client-event-fail',
               'mux-fail-missing','mux-fail-init','mux-fail-loop','mux-fail-event','mux-fail-map',
               'battery-watchdog','battery-watchdog-read-fail','battery-watchdog-foreign','battery-watchdog-enabled',
               'battery-temp','battery-temp-read-fail','battery-temp-enabled',
               'battery-typec-read-fail','battery-runtime-events','hardware','timeout','board-no-keys','board-key-fail','board-key-rollback',
               'board-key-detach','board-arm-fail']
    # LSan cannot operate in this sandbox's ptrace environment. Explicit
    # object/registry/notifier/MMIO accounting accompanies ASAN/UBSAN instead.
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    for case in scenarios:
        subprocess.run([str(binary),case],env=env,check=True)
    for _ in range(20):
        for case in ['notifier-stop-race','provider-stop-race','keys-term-during-prepare','keys-stop-during-prepare']:
            subprocess.run([str(binary),case],env=env,check=True,stdout=subprocess.DEVNULL)
    print('PASS: 80 additional notification/IRQ/provider/consumer preparation and teardown races')
