#!/usr/bin/env python3
"""Run actual battery policy, sequencer, sampler and monitor lifecycle on a host.
Usage: test-mh2lm-battery-runtime.py EXTRACTED_XNU
Models IOKit scheduling/publication and SPMI results; does not prove charging.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1])
source = (root/'iokit/Kernel/arm/SweetBattery.cpp').read_text()
header = (root/'bsd/dev/sweet_battery_runtime.h').read_text()
spmi = (root/'bsd/dev/sweet_spmi.h').read_text()

def function(name):
    match = re.search(r'\n'+re.escape(name)+r'\([^)]*\)\n\{',source)
    assert match,name
    start=source.rfind('\n\n',0,match.start())+2
    end=source.index('{',match.start())+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'

prelude=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <set>
#include <string>
#include <vector>
#define ARM64_BOARD_CONFIG_MH2LM 1
static std::mutex logMutex;
static std::string logs;
static void recordLog(const char *fmt,...) __attribute__((format(printf,1,2)));
static void recordLog(const char *fmt,...) {
    char buffer[2048];va_list args;va_start(args,fmt);
    int n=vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);
    assert(n>=0 && static_cast<size_t>(n)<sizeof(buffer));
    if(std::string(fmt).find("SWEET BATT SAMPLE:")!=std::string::npos ||
       std::string(fmt).find(": v3 ")!=std::string::npos){
        // Include actual "[INFO] " prefix and newline in the 255-byte budget.
        assert(n+7<=255 && n>0 && buffer[n-1]=='\n');
    }
    std::lock_guard<std::mutex> guard(logMutex);logs+=buffer;
}
#define SWEET_LOG_INFO(...) recordLog(__VA_ARGS__)
#define SWEET_LOG_ERROR(...) ((void)0)
using UInt32=uint32_t;
using IOReturn=int;using vm_size_t=size_t;
using IOLock=std::mutex;
static void IOLockLock(IOLock *l){l->lock();}static void IOLockUnlock(IOLock *l){l->unlock();}
struct IOService {};
#define kIOReturnUnsupported -2
#define kIOMessageSystemWillSleep 1
#define kIOMessageSystemWillRestart 2
#define kIOMessageSystemWillPowerOff 3
struct sleepWakeNote {void *powerRef;unsigned long returnValue;};
static unsigned acknowledged;
static void acknowledgeSleepWakeNotification(void *){++acknowledged;}
struct OSNumber {uint32_t value;static OSNumber *withNumber(uint32_t v,unsigned){return new OSNumber{v};}void release(){delete this;}};
struct OSDictionary {
 std::map<std::string,uint32_t> values;
 static OSDictionary *withCapacity(unsigned){return new OSDictionary;}
 void setObject(const char *k,OSNumber *v){values[k]=v->value;}
 bool getObject(const char *k){return values.count(k);}
 void removeObject(const char *k){values.erase(k);}
 void release(){delete this;}
};
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
#define kIOBatteryCurrentChargeKey "Current"
#define kIOBatteryCapacityKey "Capacity"
#define kIOBatteryVoltageKey "Voltage"
#define kIOBatteryAmperageKey "Amperage"
#define kIOBatteryCycleCountKey "CycleCount"
#define kIOBatteryFlagsKey "Flags"
#define kIOBatteryInstalled 1
#define kIOBatteryChargerConnect 2
#define kIOBatteryCharge 4
#define kIOReturnSuccess 0
#define kIOReturnNotReady -1
struct OSObject {virtual ~OSObject()=default;};
#define OSDynamicCast(type,obj) dynamic_cast<type *>(obj)
static uint64_t timeNs=1000000000ULL;
static void clock_get_uptime(uint64_t *v){*v=timeNs;}
static uint64_t testClock(void *){return timeNs;}
static void absolutetime_to_nanoseconds(uint64_t v,uint64_t *n){*n=v;}
struct IOTimerEventSource {
    bool armed=false,disabled=false;unsigned arms=0;
    void setTimeoutMS(unsigned n){assert(n==1000);armed=true;++arms;}
    void cancelTimeout(){armed=false;}
    void disable(){disabled=true;}
};
'''
shim=r'''
static std::map<unsigned,uint8_t> regs;
static std::map<unsigned,uint16_t> fg;
static std::vector<unsigned> reads,writes;
static std::set<unsigned> failures;
static bool pairFailure=false;
static unsigned injectedAt=0,updateCount=0,delayAt=0;
static std::map<unsigned,unsigned> changed;
extern "C" int sweet_spmi_charger_read(unsigned sid,unsigned addr,uint8_t *out){
    assert(sid==2);reads.push_back(addr);if(failures.count(addr))return -9;
    *out=regs[addr];return 0;
}
extern "C" int sweet_spmi_read(unsigned sid,unsigned addr,uint8_t *out){
    return sweet_spmi_charger_read(sid,addr,out);
}
extern "C" int sweet_spmi_fg_read(unsigned field,uint16_t *out){
    reads.push_back(0x10000+field);if(failures.count(0x10000+field))return -10;
    *out=fg[field];return 0;
}
extern "C" int sweet_spmi_charger_update(unsigned sid,unsigned addr,unsigned mask,unsigned value,uint8_t *out){
    assert(sid==2);writes.push_back(addr);++updateCount;
    if(updateCount==delayAt)timeNs+=4000000000ULL;
    // A failed transaction may already have reached hardware.
    changed[addr]=(changed[addr]&~mask)|value;
    if(updateCount==injectedAt)return -9;
    *out=changed[addr];return 0;
}
class SweetBattery : public OSObject {
public:
 unsigned fRuntimeMode=2,fRuntimeRequested=2,fMonitorSamples=0,fLastPercent=70,fLastMv=4000;
 uint32_t fLastReasons=~0u;
 bool fRunning=true,fPercentValid=true;
 IOTimerEventSource *fMonitor;
 IOLock controlLock;IOLock *fControlLock=&controlLock;bool fPowerStopping=false,fControlClaimed=false;
 static IOReturn runtimePowerNotice(void *,void *,UInt32,IOService *,void *,vm_size_t);
 SweetBattSnapshot fSnapshot={};SweetBattLimits fLimits={};SweetBattController fController={};
 unsigned publishes=0;int publishedMa=0;bool publishedCurrentValid=false;
 void sampleRuntime(bool);void runRuntimePolicy();void stopRuntime();void publishRuntime();
 static uint64_t runtimeClock(void *);
 OSDictionary storage;OSDictionary *properties=&storage;
 bool settingsChangedSinceUpdate=false,fUiDiag=false,fUiPublished=false,fUiExternal=false,
      fUiCapable=false,fUiCharging=false,fUiFull=false,fSid2=true;
 unsigned fUiSequence=0;int fLastMode=-1;
 std::map<std::string,uint32_t> registry,legacy;
 void putBool(const char *k,bool v){properties->values[k]=v;}
 void putNumber(const char *k,UInt32 v){properties->values[k]=v;}
 void removeProperty(const char *k){registry.erase(k);}
 void removePowerValue(const char *);
 void setBatteryInstalled(bool v){putBool("Installed",v);}
 void setCurrentCapacity(unsigned v){putNumber("CurrentCapacity",v);}
 void setMaxCapacity(unsigned v){putNumber("MaxCapacity",v);}
 void setExternalConnected(bool v){putBool("External",v);}
 void setExternalChargeCapable(bool v){putBool("Capable",v);}
 void setIsCharging(bool v){putBool("Charging",v);}
 void setAtWarnLevel(bool){} void setAtCriticalLevel(bool){}
 void setVoltage(unsigned v){putNumber("Voltage",v);}
 void setAmperage(int v){putNumber("Amperage",(uint32_t)v);}
 void setTimeRemaining(int){}void setCycleCount(unsigned v){putNumber("CycleCount",v);}
 void setLegacyIOBatteryInfo(OSDictionary *v){legacy=v->values;}
 void updateStatus(){++publishes;registry=properties->values;
  publishedMa=registry.count("Amperage")?(int32_t)registry["Amperage"]:0;
  publishedCurrentValid=registry["SweetBatteryCurrentValid"];
 }
 static int runtimeUpdate(void *,unsigned,unsigned,unsigned,uint8_t *);
 static IOReturn startRuntime(OSObject *,void *,void *,void *,void *);
 static void monitorFired(OSObject *,IOTimerEventSource *);
 static bool readWordPair(unsigned,unsigned addr,unsigned cp,uint16_t *out){
    assert(cp==addr+6);reads.push_back(addr);if(pairFailure)return false;
    *out=static_cast<uint16_t>(regs[addr]|(regs[addr+1]<<8));return true;
 }
 static bool readBytePair(unsigned,unsigned a,unsigned cp,uint8_t *out){
    assert(a==0x4009 && cp==0x400a);*out=180;return !pairFailure;
 }
 static unsigned scaleMsoc(uint8_t){return 70;}
 void publish(unsigned,unsigned,int,bool,bool,bool,bool,bool,bool);
};
'''
checks=r'''
static SweetBattSnapshot sample(){
    SweetBattSnapshot s={};s.valid=(1ULL<<SB_REG_COUNT)-1;
    s.startedNs=s.completedNs=timeNs;s.voltageValid=s.currentValid=s.tempValid=true;
    s.presentValid=s.present=s.socValid=s.profileValid=true;
    s.mv=4000;s.tempCenti=2500;s.percent=70;s.integrity=1;
    s.raw[SB_PATH]=0x95;s.raw[SB_USB]=16;s.raw[SB_APSD]=1;s.raw[SB_STAGE]=7;
    return s;
}
static SweetBattLimits limits(){return {true,true,true,3300,4430,1500,5000,timeNs};}
static void policyTests(){
    auto s=sample();auto l=limits();SweetBattController c={};SweetBattery control;
    auto d=sb_decide(c,s,l,timeNs);assert(d.allow && !d.reasons && d.fccMa==500 && d.iclMa==500 && d.fvMv==4200);
    updateCount=0;writes.clear();changed[0x1042]=1;
    assert(sb_apply(c,d,timeNs,true,SweetBattery::runtimeUpdate,&control,testClock));
    assert(!c.owned && writes.empty());
    assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(c.owned && c.enabled && writes.front()==0x1042 && writes.back()==0x1042 && writes.size()==11);
    assert(changed[0x1042]==1 && changed[0x1061]==10 && changed[0x1070]==60);
    for(unsigned fail=1;fail<=11;++fail){
        c={};injectedAt=fail;updateCount=0;writes.clear();
        bool ok=sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock);
        assert(!ok && c.fault && c.owned);
        assert(changed[0x1042]==0 || fail==1); // ambiguous first disable is explicitly faulted
    }
    injectedAt=0;
    c={};l=limits();
    for(unsigned field:{SB_STAGE,SB_ERROR,SB_THERMAL,SB_PATH,SB_USB,SB_ENABLE,SB_PAUSE,
                       SB_WDOG,SB_WDOG_TIME,SB_APSD,SB_RESULT,SB_ICL,SB_FCC,SB_FV,SB_SUSPEND,SB_MODE,SB_LOAD,SB_OVERRIDE}){
        auto invalid=s;invalid.valid&=~(1ULL<<field);
        auto decision=sb_decide(c,invalid,l,timeNs);assert(!decision.allow && (decision.reasons&SB_R_TELEMETRY));
    }
    for(unsigned bit:{SB_R_BATTERY_ID,SB_R_PARALLEL,SB_R_INPUT}){
        l=limits();if(bit==SB_R_BATTERY_ID)l.batteryVerified=false;
        if(bit==SB_R_PARALLEL)l.parallelVerified=false;if(bit==SB_R_INPUT)l.inputVerified=false;
        auto decision=sb_decide(c,s,l,timeNs);assert(!decision.allow && (decision.reasons&bit));
    }
    l=limits();s.tempCenti=4100;d=sb_decide(c,s,l,timeNs);assert(!d.allow && c.thermalHold);
    s.tempCenti=3900;assert(!sb_decide(c,s,l,timeNs).allow);
    s.tempCenti=3800;assert(sb_decide(c,s,l,timeNs).allow && !c.thermalHold);
    s.tempValid=false;assert(!sb_decide(c,s,l,timeNs).allow && c.thermalHold);
    s=sample();assert(sb_decide(c,s,l,timeNs).allow);
    s.raw[SB_WDOG]=2;assert(sb_decide(c,s,l,timeNs).reasons&SB_R_WATCHDOG);s=sample();
    s.raw[SB_RESULT]=0x20;assert(sb_decide(c,s,l,timeNs).reasons&SB_R_INPUT);s=sample();
    s.ma=700;assert(sb_decide(c,s,l,timeNs).reasons&SB_R_HARDWARE);s=sample();
    l.verifiedNs=1;l.vbusMv=9000;assert(sb_decide(c,s,l,timeNs).reasons&SB_R_INPUT);l=limits();
    assert(sb_decide(c,s,l,timeNs+4000000000ULL).reasons&SB_R_STALE);
    c={};d=sb_decide(c,s,l,timeNs);assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    d=sb_decide(c,s,l,c.deadlineNs);assert(d.reasons&SB_R_EXPIRED);
    assert(sb_apply(c,d,c.deadlineNs,false,SweetBattery::runtimeUpdate,&control,testClock) && c.stopped && !c.enabled && c.stopConfirmed);
    unsigned prior=writes.size();assert(!sb_apply(c,SweetBattDecision{0,500,4200,500,true,SB_PREPARE},timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(writes.size()==prior+1 && writes.back()==0x1042 && changed[0x1042]==0);
    // Decision expires while programming, before enable. The real clock must
    // prevent using the old decision even though the 60s session is still live.
    c={};s=sample();l=limits();d=sb_decide(c,s,l,timeNs);
    updateCount=0;delayAt=5;writes.clear();
    assert(!sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(c.stopped && !c.enabled && changed[0x1042]==0);
    assert(writes.size()==6);delayAt=0;
    c={};s=sample();l=limits();d=sb_decide(c,s,l,timeNs);
    assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    // Hardware unexpectedly clears enable: disable/confirm and latch a fault.
    d=sb_decide(c,s,l,timeNs);assert(!d.allow && (d.reasons&SB_R_HARDWARE));
    assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(c.fault && !c.enabled);
    puts("PASS: policy missing inputs/staleness/thermal hysteresis, dry-run, ordered control, every write failure and expiry");
}
int main(){
    policyTests();reads.clear();writes.clear();changed.clear();
    regs={{0x41a0,0xff},{0x41a1,0x7f},{0x41a2,0x60},{0x41a3,0xed},
          {0x4010,2},{0x4107,0},{0x1310,16},{0x1006,3},{0x110b,0x95},{0x1307,1}};
    fg={{0,100},{1,4000},{2,1},{3,3900}};
    IOTimerEventSource timer;SweetBattery b;b.fMonitor=&timer;
    assert(!SweetBattery::startRuntime(&b,nullptr,nullptr,nullptr,nullptr));
    assert(b.fSnapshot.tempValid && b.fSnapshot.tempCenti==2500 && b.fSnapshot.currentValid && b.fSnapshot.ma>2300);
    assert(b.fSnapshot.nominalValid && b.fSnapshot.nominalMah==4000 && b.fSnapshot.learnedValid && b.fSnapshot.learnedMah==3900);
    assert(!b.fController.owned && writes.empty() && timer.armed);
    assert(b.registry["Temperature"]==2500 && b.registry["MaxCapacity"]==3900);
    assert(b.registry["DesignCapacity"]==4000 && b.registry["CurrentCapacity"]==2730);
    assert(!b.registry.count("CycleCount") && !b.registry["SweetBatterySOCEstimated"]);
    assert(!b.legacy.count("CycleCount"));
    // A telemetry tick must not discover USB/APSD changes: those require IRQ.
    regs[0x1310]=0;reads.clear();++timeNs;
    SweetBattery::monitorFired(&b,&timer);
    for(auto a:reads)assert(a!=0x1310 && a!=0x1307 && a!=0x1308 && a!=0x1506 && a!=0x1509 && a!=0x150b);
    assert((b.fSnapshot.raw[SB_USB]&16) && b.publishes==2 && b.publishedCurrentValid);
    b.sampleRuntime(true);assert(!b.fSnapshot.raw[SB_USB]);
    failures={0x10000};pairFailure=true;reads.clear();
    SweetBattery::monitorFired(&b,&timer);
    assert(!b.fSnapshot.tempValid && !b.fSnapshot.currentValid && !b.publishedCurrentValid);
    assert(b.fLastReasons&SB_R_TELEMETRY);
    assert(!b.registry.count("Temperature") && !b.registry.count("Amperage") && !b.registry.count("InstantAmperage"));
    assert(!b.legacy.count("Amperage"));
    failures={0x10001,0x10002,0x10003};pairFailure=false;logs.clear();b.sampleRuntime(true);b.publishRuntime();
    assert(!b.registry.count("DesignCapacity") && b.registry["MaxCapacity"]==100 && b.registry["SweetBatteryCapacityUsesPercent"]);
    assert(logs.find("n-error=-10")!=std::string::npos && logs.find("l-error=-10")!=std::string::npos && logs.find("p-error=-10")!=std::string::npos);
    assert(logs.find("SWEET BATT CONFIG: v3 id=")!=std::string::npos);
    assert(logs.find("SWEET BATT SAMPLE: v2 id=")!=std::string::npos && logs.find("SWEET BATT TIME: v3 id=")!=std::string::npos);
    regs[0x1651]=0xc2;regs[0x1653]=0x87;failures={0x1042};logs.clear();b.sampleRuntime(true);
    assert(logs.find("enable-error=-9")!=std::string::npos && logs.find("wdog=0xc2 wdog-error=0 wdog-time=0x87 wdog-time-error=0")!=std::string::npos);
    // A protection tick does not reprint the event-only config diagnostic.
    logs.clear();b.sampleRuntime(false);assert(logs.find("SWEET BATT CONFIG:")==std::string::npos);
    failures.clear();pairFailure=false;fg[0]=0x3fc;b.sampleRuntime(false);
    assert(b.fSnapshot.tempValid && b.fSnapshot.tempCenti==-100);
    fg[0]=0xffff;b.sampleRuntime(false);assert(!b.fSnapshot.tempValid);
    // Largest sample id and timestamp must retain the complete time record.
    auto savedTime=timeNs;auto savedSamples=b.fMonitorSamples;
    timeNs=UINT64_MAX-1;b.fMonitorSamples=UINT32_MAX-1;logs.clear();b.sampleRuntime(true);
    assert(logs.find("SWEET BATT TIME: v3 id=4294967295 started-ns=18446744073709551614 completed-ns=18446744073709551614\n")!=std::string::npos);
    timeNs=savedTime;b.fMonitorSamples=savedSamples;
    // Stop cancels the actual monitor and leaves it unable to rearm/submit IO.
    b.fController.owned=b.fController.enabled=true;b.fController.deadlineNs=timeNs+1000000;
    b.stopRuntime();assert(timer.disabled && !timer.armed && !b.fController.enabled);
    auto arms=timer.arms;auto samples=b.fMonitorSamples;SweetBattery::monitorFired(&b,&timer);
    assert(timer.arms==arms && b.fMonitorSamples==samples);
    SweetBattery shutdown;uint8_t verified=0;
    assert(!SweetBattery::runtimeUpdate(&shutdown,0x1042,1,1,&verified));
    std::thread power([&]{assert(!SweetBattery::runtimePowerNotice(&shutdown,nullptr,kIOMessageSystemWillRestart,nullptr,nullptr,0));});
    power.join();assert(shutdown.fPowerStopping && changed[0x1042]==0);
    auto prior=writes.size();assert(SweetBattery::runtimeUpdate(&shutdown,0x1042,1,1,&verified)==SWEET_SPMI_NOT_READY);
    assert(writes.size()==prior);
    for(unsigned round=0;round<100;++round){
        SweetBattery racing;racing.fControlClaimed=true;std::atomic<bool> go{false};
        std::thread submit([&]{while(!go.load()){}for(unsigned i=0;i<20;++i){uint8_t v;
            int rc=SweetBattery::runtimeUpdate(&racing,0x1042,1,1,&v);assert(rc==0 || rc==SWEET_SPMI_NOT_READY);}});
        std::thread notify([&]{while(!go.load()){}SweetBattery::runtimePowerNotice(&racing,nullptr,kIOMessageSystemWillPowerOff,nullptr,nullptr,0);});
        go=true;submit.join();notify.join();assert(racing.fPowerStopping && changed[0x1042]==0);
    }
    sleepWakeNote note={nullptr,1};
    assert(!SweetBattery::runtimePowerNotice(&shutdown,nullptr,kIOMessageSystemWillSleep,nullptr,&note,sizeof(note)));
    assert(acknowledged==1 && note.returnValue==0);
    puts("PASS: actual runtime sampler, units/validity, IRQ-only input acquisition, monitor publication and stop/no rearm");
}
'''
names=['sweet_batt_now_ns','SweetBattery::sampleRuntime','SweetBattery::runtimeUpdate',
       'SweetBattery::runRuntimePolicy','SweetBattery::startRuntime',
       'SweetBattery::monitorFired','SweetBattery::stopRuntime','SweetBattery::publishRuntime','SweetBattery::runtimeClock','SweetBattery::publish','SweetBattery::removePowerValue','SweetBattery::runtimePowerNotice']
with tempfile.TemporaryDirectory(prefix='mh2lm-runtime-') as directory:
    cpp=Path(directory)/'test.cpp';binary=Path(directory)/'test'
    cpp.write_text(prelude+header+spmi+shim+''.join(function(n) for n in names)+checks)
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-O1','-g',
        '-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True,timeout=30)
