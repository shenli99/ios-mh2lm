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
charger = (root/'bsd/dev/sweet_charger.h').read_text()
backend = (root/'bsd/dev/sweet_charger.c').read_text()
power = (root/'iokit/Kernel/arm/SweetPmicPower.cpp').read_text()
mux_header = (root/'bsd/dev/sweet_spmi_mux.h').read_text()
event_defines = "\n".join(re.findall(r'^#define SWEET_BATT_IRQ_.*$', mux_header, re.M))+"\n"
power_header = (root/'bsd/dev/sweet_pmic_power.h').read_text()
def strip_includes(text):
    return re.sub(r'^#include[^\n]*\n', '', text, flags=re.M)

def function(name):
    match = re.search(r'\n'+re.escape(name)+r'\([^)]*\)\n\{',source)
    assert match,name
    start=source.rfind('\n',0,match.start())+1
    end=source.index('{',match.start())+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'

for name in ['SweetBattery::sampleRuntime','SweetBattery::runtimeUpdate','SweetBattery::runtimePowerNotice']:
    body=function(name)
    assert not re.search(r'\bsweet_spmi_(read|charger_read|charger_update|fg_read(?:_profile)?)\s*\(',body),name
assert not re.search(r'0x(?:1042|1043|1061|1070|1340|1342|1365|1366|1370)',header)
assert not re.search(r'ml_io_map|volatile\s+.*\*',backend+power)

prelude=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
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
    if(std::string(fmt).find("SWEET BATT ")!=std::string::npos){
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
#define kIOReturnNoMemory -3
#define SWEET_MUX_FAILED 0x80000000u
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
adc_header=strip_includes((root/'bsd/dev/sweet_adc5.h').read_text())
# Actual registry/expiry/concurrency behavior is in test-mh2lm-adc-consumer.py.
# Here the typed provider boundary lets us test battery publication/withdrawal.
adc_model=r'''
class SweetAdc5Client {
public:
 bool valid[2]={},present=false;unsigned updates=0,sourceUpdates=0;uint64_t epoch=0;
 sweet_adc5_measurement samples[2]={};
 static SweetAdc5Client *withOwner(void *,void (*)(void *),void *) {static SweetAdc5Client instance;return &instance;}
 void invalidate(){valid[0]=valid[1]=false;}
 void update(bool input,uint64_t nextEpoch,bool,uint64_t){++updates;if(nextEpoch!=epoch){++sourceUpdates;valid[1]=false;}epoch=nextEpoch;present=input;if(!input)valid[1]=false;}
 bool copyFreshInput(uint64_t expected,uint64_t now,sweet_adc5_measurement *out){return expected && expected!=UINT64_MAX && epoch==expected && present && copyFresh(SWEET_ADC5_USBIN,now,out);}
 bool copyFresh(sweet_adc5_channel c,uint64_t now,sweet_adc5_measurement *out){auto &s=samples[c-1];if(!valid[c-1] || now<s.completed_ns || now<s.started_ns || now-s.started_ns>(c==1?60000000000ULL:3000000000ULL))return false;*out=s;return true;}
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
static unsigned readAdvanceNs;
static unsigned mutateAtAddr,mutateNth,mutateCount,mutateValue;
static std::function<void(unsigned)> sampleHook;
static bool reflectWrites;

static uint8_t profilePrefix[24]={0x09,0x00,0xff,0xe3,0xf1,0x07,0x96,0xda,
 0xb3,0xd5,0x00,0x00,0xa1,0xbd,0xa3,0x8a,0xf8,0x87,0x84,0x92,0x9e,0x94,0x23,0x80};
extern "C" int sweet_spmi_charger_read(unsigned sid,unsigned addr,uint8_t *out){
    timeNs+=readAdvanceNs;
    if(sampleHook)sampleHook(addr);
    assert(sid==2);reads.push_back(addr);if(failures.count(addr))return -9;
    if(addr==mutateAtAddr && ++mutateCount==mutateNth)regs[addr]=mutateValue;
    *out=regs[addr];return 0;
}
extern "C" int sweet_spmi_read(unsigned sid,unsigned addr,uint8_t *out){
    if(pairFailure && ((addr>=0x41a0 && addr<=0x41ab) || addr==0x4009 || addr==0x400a))return -9;
    return sweet_spmi_charger_read(sid,addr,out);
}
extern "C" int sweet_spmi_fg_read(unsigned field,uint16_t *out){
    reads.push_back(0x10000+field);if(failures.count(0x10000+field))return -10;
    *out=fg[field];return 0;
}
extern "C" int sweet_spmi_fg_read_profile(sweet_fg_profile *out){
    reads.push_back(0x10004);
    if(failures.count(0x10004) || failures.count(0x10001) || failures.count(0x10002))return -10;
    sweet_fg_profile next={};next.started_ns=timeNs;timeNs+=readAdvanceNs;
    next.completed_ns=timeNs;next.integrity=fg[2];next.nominal_mah=fg[1];
    memcpy(next.prefix,profilePrefix,24);*out=next;return 0;
}
extern "C" int sweet_spmi_charger_update(unsigned sid,unsigned addr,unsigned mask,unsigned value,uint8_t *out){
    assert(sid==2);writes.push_back(addr);++updateCount;
    if(updateCount==delayAt)timeNs+=4000000000ULL;
    // A failed transaction may already have reached hardware.
    changed[addr]=(changed[addr]&~mask)|value;
    if(reflectWrites)regs[addr]=(regs[addr]&~mask)|value;
    if(updateCount==injectedAt)return -9;
    *out=changed[addr];return 0;
}
extern "C" int sweet_spmi_charger_pet_watchdog(void){
    writes.push_back(0x1643);++updateCount;
    return updateCount==injectedAt?-9:0;
}
class SweetBattery : public OSObject {
public:
 unsigned fDependencyMode=0,fWatchdogPets=0;
 bool fDependencyStarted=false,fDependencyFailed=false,fDependencyFinished=false,fDependencyConfirmed=false,
      fWatchdogArmed=false,fWatchdogPending=false,fWatchdogInput=false;
 uint64_t fDependencyDeadlineNs=0,fDependencyReadAfterNs=0,fWatchdogLastNs=0,fWatchdogInputEpoch=0;
 SweetBattIdentity fIdentity={};
 void refreshDependencies(uint64_t);
 int dependencyCommand(sweet_charger_operation,uint64_t,unsigned *);
 void dependencyFailure(const char *,int);
 unsigned fRuntimeMode=2,fRuntimeRequested=2,fMonitorSamples=0,fLastPercent=70,fLastMv=4000;
 uint32_t fLastReasons=~0u;
 unsigned fRuntimeInputEvents=0;volatile unsigned fIrqPending=0;
 bool fRunning=true,fPercentValid=true;
 IOTimerEventSource *fMonitor; void *fLoop=nullptr;
 SweetAdc5Client *fAdcClient=nullptr;void publishAdc();static void adcChanged(void *);
 IOLock controlLock;IOLock *fControlLock=&controlLock;bool fPowerStopping=false,fControlClaimed=false;
 static IOReturn runtimePowerNotice(void *,void *,UInt32,IOService *,void *,vm_size_t);
 SweetBattSnapshot fSnapshot={};SweetBattLimits fLimits={};SweetBattController fController={};
 SweetBattInputSession fInputSession={};bool inputChangePending();void refreshInputPermit(uint64_t);
 unsigned publishes=0;int publishedMa=0;bool publishedCurrentValid=false;
 IOTimerEventSource *fTimer=nullptr;
 bool fIrqFailed=false,fIrqAttached=true;
 unsigned fIrqMode=4,fIrqSamples=0;volatile unsigned fIrqNotifications=0;
 struct MuxStub {unsigned detachments=0,pending=0;bool unavailable=false;bool hasPendingEvents(unsigned mask){return unavailable || (pending&(mask|SWEET_MUX_FAILED));}void detach(){++detachments;unavailable=true;}};
 MuxStub connection;MuxStub *fMuxClient=&connection;
 void failIrq(const char *);
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
 static int runtimeUpdate(void *,enum sweet_charger_operation,unsigned,uint64_t,unsigned *);
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
static void inputSample(SweetBattery &b,unsigned events=SWEET_BATT_IRQ_USB){
    b.fRuntimeInputEvents=events;b.sampleRuntime(true);
}

static SweetBattSnapshot sample(){
    SweetBattSnapshot s={};s.valid=(1ULL<<SB_REG_COUNT)-1;
    s.startedNs=s.completedNs=timeNs;s.voltageValid=s.currentValid=s.tempValid=true;
    s.presentValid=s.present=s.socValid=s.profileValid=true;
    s.mv=4000;s.tempCenti=2500;s.percent=70;s.integrity=1;
    s.raw[SB_PATH]=0x95;s.raw[SB_USB]=16;s.raw[SB_APSD]=1;s.raw[SB_STAGE]=7;
    s.raw[SB_RESULT]=8;s.raw[SB_TYPEC]=8;s.raw[SB_MISC]=0x29;s.raw[SB_ATTACHED]=0x20;
    s.inputEpoch=1;s.inputStartedNs=s.inputCompletedNs=timeNs;
    s.raw[SB_APSD_OPTIONS]=8;s.usbSession=s.apsdSession=1;
    s.apsdStartedNs=s.apsdCompletedNs=timeNs;s.apsdCoherent=s.apsdFromSource=true;
    s.ccCoherent=true;
    return s;
}
static SweetBattInputPermit permitFor(const SweetBattSnapshot &s){
 SweetBattInputSession manager={};sb_input_observe(manager,s,timeNs);
 SweetBattInputVoltage v={true,5000000,1,s.inputEpoch,timeNs,timeNs};
 return sb_input_permit(manager,s,v,timeNs);
}
static SweetBattLimits limits(const SweetBattSnapshot &s=sample()){
 return {true,true,2800,4430,timeNs,permitFor(s),timeNs+60000000000ULL};
}
static void authorizeControl(SweetBattery &b){
 static SweetAdc5Client adc;
 b.fSnapshot=sample();b.fLimits=limits();b.fAdcClient=&adc;
 adc.present=true;adc.epoch=b.fSnapshot.inputEpoch;adc.valid[1]=true;
 adc.samples[1]={};adc.samples[1].sequence=1;
 adc.samples[1].started_ns=adc.samples[1].completed_ns=timeNs;adc.samples[1].microvolts=5000000;
}
static void ccTests(){
    auto s=sample();SweetBattSnapshot empty={};
    s.raw[SB_TYPEC]=4;s.raw[SB_APSD]=s.raw[SB_RESULT]=s.raw[SB_APSD_OPTIONS]=0;
    sb_note_input(s,empty,0);
    assert(sb_cc_status(s).advertisedMa==1500 && !sb_cc_event_fresh(s,timeNs));
    assert(sb_input_capability(s,timeNs).kind==SB_INPUT_NONE); // startup is diagnostic
    auto previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    assert(sb_cc_event_fresh(s,timeNs) && !sb_apsd_event_fresh(s,timeNs));
    assert(sb_input_capability(s,timeNs).kind==SB_INPUT_CC);
    // CC works without BC1.2 enable/DONE/result, including read failures there.
    s.valid&=~((1ULL<<SB_APSD)|(1ULL<<SB_RESULT)|(1ULL<<SB_APSD_OPTIONS));
    assert(sb_input_capability(s,timeNs).maxMa==1500);
    SweetBattController controller={};auto l=limits(s);
    auto decision=sb_decide(controller,s,l,timeNs);
    assert(!decision.allow && (decision.reasons&SB_R_INPUT)); // formal permission requires coherent APSD metadata to rule out QC
    s.valid|=(1ULL<<SB_APSD)|(1ULL<<SB_RESULT)|(1ULL<<SB_APSD_OPTIONS);l=limits(s);
    decision=sb_decide(controller,s,l,timeNs);
    assert(decision.allow && decision.iclMa==500 && decision.validUntilNs==timeNs+3000000000ULL);
    l.input.valid=false;assert(sb_decide(controller,s,l,timeNs).reasons&SB_R_INPUT);
    l.input.valid=true;l.batteryVerified=false;
    assert(sb_decide(controller,s,l,timeNs).reasons&SB_R_BATTERY_ID);
    l.batteryVerified=true;l.parallelVerified=false;
    assert(sb_decide(controller,s,l,timeNs).reasons&SB_R_PARALLEL);
    // Source/plugin/duplicate CC notifications must not renew unchanged CC time.
    for(unsigned event:{SWEET_BATT_IRQ_SOURCE,SWEET_BATT_IRQ_USB,SWEET_BATT_IRQ_TYPEC}){
        previous=s;s.inputStartedNs=s.inputCompletedNs=timeNs+1000000000ULL;
        sb_note_input(s,previous,event);assert(s.ccStartedNs==timeNs);
    }
    assert(!sb_cc_event_fresh(s,timeNs+3000000001ULL));
    previous=s;s.raw[SB_TYPEC]=2;s.inputStartedNs=s.inputCompletedNs=timeNs+2000000000ULL;
    sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);
    assert(!s.ccFromEvent); // a source change cannot establish changed CC state
    previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    assert(sb_input_capability(s,s.inputCompletedNs).maxMa==3000);
    auto good=s;
    // Revoke on input faults, short/mixed Rp, debug/legacy, error or race.
    for(unsigned flip:{1u,4u,8u,0x20u,0x40u,0x80u}){
        s=good;previous=s;s.raw[SB_MISC]^=flip;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
        assert(sb_cc_status(s).reasons && !s.ccFromEvent && sb_input_capability(s,s.inputCompletedNs).kind==SB_INPUT_NONE);
    }
    s=good;s.raw[SB_MISC]^=2;assert(!sb_cc_status(s).reasons && sb_cc_status(s).orientation==2);
    for(unsigned rp:{0u,1u,0x40u,0x20u,0x10u,6u,0x0cu}){
        s=good;previous=s;s.raw[SB_TYPEC]=rp;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
        assert(!s.ccFromEvent && sb_input_capability(s,s.inputCompletedNs).kind==SB_INPUT_NONE);
    }
    for(unsigned field:{SB_USB,SB_TYPEC,SB_ATTACHED,SB_MISC,SB_DEBUG_ACCESS,SB_LEGACY}){
        s=good;previous=s;s.valid&=~(1ULL<<field);sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
        assert(sb_cc_status(s).reasons&SB_CC_READ);assert(!s.ccFromEvent);
    }
    for(unsigned field:{SB_DEBUG_ACCESS,SB_LEGACY,SB_ATTACHED,SB_USB}){
        s=good;previous=s;s.raw[field]=field==SB_DEBUG_ACCESS||field==SB_LEGACY?1:0;
        sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);assert(!s.ccFromEvent);
    }
    s=good;previous=s;s.ccCoherent=false;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    assert(sb_cc_status(s).reasons&SB_CC_RACE);assert(!s.ccFromEvent);
    s=good;previous=s;s.raw[SB_TYPEC]=8;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    assert(sb_cc_event_fresh(s,s.inputCompletedNs) && !sb_cc_status(s).advertisedMa);
    assert(sb_input_capability(s,s.inputCompletedNs).kind==SB_INPUT_NONE); // default needs USB/BC
    s=good;s.valid|=(1ULL<<SB_APSD)|(1ULL<<SB_RESULT);s.raw[SB_APSD]=3;s.raw[SB_RESULT]=8;
    assert(sb_input_capability(s,s.inputCompletedNs).kind==SB_INPUT_NONE); // no QC authorization
    // Real 0120a10 tuple: classify sink Rp as LG SMB5 does. misc bit4 remains
    // observable but is not the independent USBIN over/undervoltage indicator.
    s=sample();s.raw[SB_TYPEC]=2;s.raw[SB_ATTACHED]=0x23;s.raw[SB_MISC]=0x39;
    s.raw[SB_DEBUG_ACCESS]=0;s.raw[SB_LEGACY]=4;s.raw[SB_RESULT]=0x10;
    auto cc=sb_cc_status(s);
    assert(cc.role==SB_CC_ROLE_SINK && cc.observed==SB_CC_OBS_VBUS_ERROR);
    assert(cc.advertisedMa==3000 && !cc.reasons);
    sb_note_input(s,empty,0);assert(!s.ccFromEvent); // startup still diagnostic
    previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    assert(s.ccFromEvent && sb_input_capability(s,timeNs).kind==SB_INPUT_CC);
    auto observed=s;
    const SweetBattLimits limitsForCc=limits(observed);
    assert(sb_decide(controller,observed,limitsForCc,timeNs).allow);
    for(unsigned fault:{4u,8u,12u}){
        s=observed;previous=s;s.raw[SB_USB]|=fault;
        sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
        assert(sb_cc_status(s).reasons==((fault&4?SB_CC_USB_UV:0)|(fault&8?SB_CC_USB_OV:0)));
        assert(!s.ccFromEvent && sb_input_capability(s,timeNs).kind==SB_INPUT_NONE);
        assert(!sb_decide(controller,s,limitsForCc,timeNs).allow);
    }
    // A source role cannot advertise input current from stale sink Rp bytes.
    s=observed;previous=s;s.raw[SB_MISC]|=0x40;
    sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    cc=sb_cc_status(s);
    assert(cc.role==SB_CC_ROLE_SOURCE && cc.observed==SB_CC_OBS_VBUS_ERROR);
    assert((cc.reasons&SB_CC_SOURCE) && cc.rp==SB_RP_UNKNOWN && !cc.advertisedMa);
    assert(!s.ccFromEvent && sb_input_capability(s,timeNs).kind==SB_INPUT_NONE);
    // Changed raw misc must still revoke provenance on a source-only event.
    s=observed;previous=s;s.raw[SB_MISC]&=~0x10;
    sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);assert(!s.ccFromEvent);
    previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);
    assert(s.ccFromEvent && !sb_cc_status(s).observed);
    s=observed;previous=s;s.raw[SB_USB]=0;
    sb_note_input(s,previous,SWEET_BATT_IRQ_USB);
    assert(!s.ccFromEvent && sb_input_capability(s,timeNs).kind==SB_INPUT_NONE);
    s=observed;assert(!sb_cc_event_fresh(s,timeNs+3000000001ULL));
    s.valid&=~(1ULL<<SB_MISC);cc=sb_cc_status(s);
    assert(cc.role==SB_CC_ROLE_UNKNOWN && cc.reasons==SB_CC_READ && !cc.observed);
    // Even a valid Rp3A observation must not make the device control providers
    // valid. Synthetic limit authorization exists only in this host test.
    s=observed;l=SweetBattLimits{};
    assert(!sb_decide(controller,s,l,timeNs).allow);
    puts("PASS: LG SMB5 sink/source Rp classification, misc observation vs USBIN OV/UV, CC provenance/expiry/revocation and unchanged control interlocks");
}
static void apsdEventTests(){
    auto s=sample();SweetBattSnapshot empty={};
    sb_note_input(s,empty,0);assert(!s.apsdFromSource && !sb_apsd_event_fresh(s,timeNs));
    // Startup DONE is diagnostic only. Plugin without completion remains waiting.
    auto previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_USB);
    assert(!s.apsdFromSource);
    previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);
    assert(sb_apsd_event_fresh(s,timeNs));
    // A same-level plugin delivered after source must not force event ordering
    // or renew the older completion timestamp.
    previous=s;s.inputStartedNs=s.inputCompletedNs=timeNs+1000000000ULL;
    sb_note_input(s,previous,SWEET_BATT_IRQ_USB);
    assert(s.apsdFromSource && s.apsdStartedNs==timeNs);
    assert(!sb_apsd_event_fresh(s,timeNs+3000000001ULL));
    // Source can also be the first indication of insertion, before plugin.
    auto early=sample();SweetBattSnapshot unplugged={};
    unplugged.valid=1ULL<<SB_USB;unplugged.usbSession=7;
    early.usbSession=7;sb_note_input(early,unplugged,SWEET_BATT_IRQ_SOURCE);
    assert(early.usbSession==8 && sb_apsd_event_fresh(early,timeNs));
    auto completed=early;sb_note_input(early,completed,SWEET_BATT_IRQ_USB);
    assert(early.usbSession==8 && early.apsdStartedNs==completed.apsdStartedNs);
    // Physical unplug/replug revokes the observed session, even with DONE held.
    previous=s;s.raw[SB_USB]=0;sb_note_input(s,previous,SWEET_BATT_IRQ_USB);
    assert(!s.apsdFromSource && s.usbSession==previous.usbSession+1);
    previous=s;s.raw[SB_USB]=16;sb_note_input(s,previous,SWEET_BATT_IRQ_USB);
    assert(!s.apsdFromSource && s.usbSession==previous.usbSession+1);
    previous=s;sb_note_input(s,previous,SWEET_BATT_IRQ_USB|SWEET_BATT_IRQ_SOURCE);
    assert(s.apsdFromSource);
    for(unsigned field:{SB_APSD,SB_APSD_OPTIONS,SB_RESULT,SB_USB}){
        s=sample();previous=s;s.valid&=~(1ULL<<field);
        sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);assert(!s.apsdFromSource);
    }
    s=sample();previous=s;s.raw[SB_APSD]=0;
    sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);assert(!s.apsdFromSource);
    s=sample();previous=s;s.raw[SB_APSD_OPTIONS]=0;
    sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);assert(!s.apsdFromSource);
    s=sample();previous=s;s.raw[SB_RESULT]=4;
    sb_note_input(s,previous,SWEET_BATT_IRQ_USB);assert(!s.apsdFromSource);
    s=sample();previous=s;s.apsdCoherent=false;
    sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);assert(!s.apsdFromSource);
    s=sample();previous=s;s.usbSession=UINT64_MAX-1;s.raw[SB_USB]=0;
    sb_note_input(s,previous,SWEET_BATT_IRQ_USB);assert(s.usbSession==UINT64_MAX);
    previous=s;s.raw[SB_USB]=16;
    sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);assert(!s.apsdFromSource);
    puts("PASS: APSD source/plugin both orders, startup residue, observed detach/replug, coalesced causes, stale/disabled/incoherent/error rejection");
}
static void evidenceTests(){
    auto s=sample();auto l=limits();SweetBattController c={};
    assert(sb_source_type(s)==SB_SOURCE_DCP && sb_normal_sink(s) && sb_input_fresh(s,timeNs));
    const unsigned raw[]={0,1,4,8,2,0x10,0x28,0x48};
    for(unsigned kind=0;kind<8;++kind){
        s.raw[SB_RESULT]=raw[kind];assert(sb_source_type(s)==kind);
        auto decision=sb_decide(c,s,l,timeNs);
        assert(decision.allow==(kind==SB_SOURCE_CDP || kind==SB_SOURCE_DCP));
    }
    // QC status overrides an otherwise ordinary DCP result, as in LG.
    s=sample();s.raw[SB_APSD]=3;
    assert(sb_source_type(s)==SB_SOURCE_QC2 && (sb_decide(c,s,l,timeNs).reasons&SB_R_INPUT));
    s.raw[SB_RESULT]=0x48;assert(sb_source_type(s)==SB_SOURCE_QC3);
    s=sample();s.raw[SB_APSD]=0;assert(sb_source_type(s)==SB_SOURCE_UNKNOWN);
    for(unsigned mixed:{0x03u,0x05u,0x0cu,0x18u,0x20u,0x60u,0x7fu}){
        s=sample();s.raw[SB_RESULT]=mixed;assert(sb_source_type(s)==SB_SOURCE_UNKNOWN);
        assert(sb_decide(c,s,l,timeNs).reasons&SB_R_INPUT);
    }
    s=sample();s.raw[SB_RESULT]=0x88;assert(sb_source_type(s)==SB_SOURCE_DCP); // result mask
    for(unsigned value:{0u,1u,0x40u,0x20u,0x10u,0x0cu}){
        s=sample();s.raw[SB_TYPEC]=value;assert(!sb_normal_sink(s));
        assert(sb_decide(c,s,l,timeNs).reasons&SB_R_INPUT);
    }
    for(unsigned flip:{1u,4u,8u,0x20u,0x40u,0x80u}){
        s=sample();s.raw[SB_MISC]^=flip;assert(!sb_normal_sink(s));
    }
    for(unsigned field:{SB_DEBUG_ACCESS,SB_LEGACY,SB_ATTACHED}){
        s=sample();s.raw[field]=field==SB_ATTACHED?0:1;assert(!sb_normal_sink(s));
        s=sample();s.valid&=~(1ULL<<field);assert(!sb_input_fresh(s,timeNs));
    }
    s=sample();s.inputEpoch=0;assert(!sb_input_fresh(s,timeNs));
    s=sample();s.inputCompletedNs=timeNs-1;assert(!sb_input_fresh(s,timeNs));
    s=sample();s.inputCompletedNs=timeNs+1;assert(!sb_input_fresh(s,timeNs));
    // Refreshing telemetry/limits may not silently renew source acquisition.
    s=sample();auto oldInput=s.inputStartedNs;
    s.startedNs=s.completedNs=l.verifiedNs=timeNs+3000000001ULL;
    assert(!sb_input_fresh(s,s.completedNs) && (sb_decide(c,s,l,s.completedNs).reasons&SB_R_INPUT));
    s=sample();s.inputStartedNs=timeNs-500000000ULL;
    auto d=sb_decide(c,s,limits(s),timeNs);
    assert(d.allow && d.validUntilNs==timeNs+3000000000ULL); // voltage lease, no renewal of source time
    s=sample();s.apsdStartedNs=timeNs-750000000ULL;
    d=sb_decide(c,s,limits(s),timeNs);
    assert(d.allow && d.validUntilNs==timeNs+3000000000ULL);
    assert(sb_evidence_deadline(UINT64_MAX-1)==UINT64_MAX);
    (void)oldInput;
    s=sample();s.profilePrefixValid=s.nominalValid=true;s.integrity=0x13;s.nominalMah=4027;
    memcpy(s.profilePrefix,profilePrefix,24);assert(sb_profile_match(s)==1);
    for(unsigned i=0;i<24;++i){s.profilePrefix[i]^=1;assert(!sb_profile_match(s));s.profilePrefix[i]^=1;}
    s.nominalMah=4073;assert(!sb_profile_match(s));s.nominalMah=4027;
    s.integrity=1;assert(!sb_profile_match(s)); // load bit alone is not LG's whitelist
    s.integrity=0x13;s.profilePrefixValid=false;assert(!sb_profile_match(s));
    static const uint8_t lgc[24]={0x09,0x00,0xdf,0xe3,0x8c,0xdc,0x11,0xe3,
     0xa8,0xdc,0x00,0x00,0xca,0xbc,0x3e,0x8b,0xf9,0x87,0x84,0x80,0xb5,0x8d,0x1f,0x80};
    s.profilePrefixValid=true;s.nominalMah=4073;memcpy(s.profilePrefix,lgc,24);assert(sb_profile_match(s)==2);
    l=limits();l.maxFccMa=3300;s=sample();assert(sb_decide(c,s,l,timeNs).reasons&SB_R_LIMITS);
    puts("PASS: exact source/QC/role classification, missing/stale input and decision expiry, real profile prefixes/metadata, board FCC bound");
}
static void inputSessionTests(){
 const auto originalTime=timeNs;
 auto s=sample();s.raw[SB_TYPEC]=2;s.raw[SB_MISC]=0x3b;
 s.ccFromEvent=true;s.ccSession=s.usbSession;
 s.ccStartedNs=s.ccCompletedNs=timeNs;
 SweetBattInputSession m={};sb_input_observe(m,s,timeNs);
 SweetBattInputVoltage v={true,5000000,11,s.inputEpoch,timeNs,timeNs};
 auto p=sb_input_permit(m,s,v,timeNs);
 assert(p.valid && p.kind==SB_INPUT_CC && p.maxMa==3000);
 auto sourceStart=m.sourceStartedNs;
 timeNs+=10000000000ULL;s.startedNs=s.completedNs=timeNs;
 v.startedNs=v.completedNs=timeNs;++v.sequence;
 assert(sb_input_capability(s,timeNs).kind==SB_INPUT_NONE); // diagnostic expired
 p=sb_input_permit(m,s,v,timeNs);assert(p.valid && m.sourceStartedNs==sourceStart);
 auto l=limits();l.input=p;SweetBattController c={};
 assert(sb_decide(c,s,l,timeNs).allow); // durable session + new physical voltage
 assert(!sb_input_permit(m,s,v,timeNs+3000000000ULL).valid);
 for(unsigned uv:{4499999u,5500001u}){auto bad=v;bad.microvolts=uv;assert(!sb_input_permit(m,s,bad,timeNs).valid);}
 for(unsigned uv:{4500000u,5500000u}){auto good=v;good.microvolts=uv;assert(sb_input_permit(m,s,good,timeNs).valid);}
 auto previous=s;++s.inputEpoch;s.inputStartedNs=s.inputCompletedNs=timeNs;
 s.raw[SB_RESULT]=0x10;sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);
 sb_input_observe(m,s,timeNs);assert(m.kind==SB_INPUT_CC && m.sourceStartedNs==sourceStart);
 assert(!sb_input_permit(m,s,v,timeNs).valid); // exact external ADC epoch
 v.epoch=s.inputEpoch;auto old=v;old.startedNs=previous.inputCompletedNs;
 assert(!sb_input_permit(m,s,old,timeNs).valid); // no timestamp relabeling either
 ++v.sequence;assert(sb_input_permit(m,s,v,timeNs).valid); // FLOAT does not defeat valid Rp
 auto stable=s;auto manager=m;
 s.raw[SB_APSD]=2;sb_input_observe(m,s,timeNs);assert(m.kind==SB_INPUT_NONE); // QC flag before DONE
 s=stable;m=manager;
 for(unsigned field:{SB_USB,SB_APSD,SB_RESULT,SB_TYPEC,SB_ATTACHED,SB_MISC,SB_DEBUG_ACCESS,SB_LEGACY,SB_APSD_OPTIONS}){
  s=stable;m=manager;s.valid&=~(1ULL<<field);sb_input_observe(m,s,timeNs);
  assert(m.kind==SB_INPUT_NONE && !sb_input_permit(m,s,v,timeNs).valid);
 }
 for(unsigned fault:{4u,8u,12u}){
  s=stable;m=manager;previous=s;s.raw[SB_USB]|=fault;
  sb_note_input(s,previous,SWEET_BATT_IRQ_INPUT_FAULT);sb_input_observe(m,s,timeNs);
  assert(m.kind==SB_INPUT_NONE && !sb_input_permit(m,s,v,timeNs).valid);
 }
 for(unsigned flip:{1u,4u,8u,0x20u,0x40u,0x80u}){
  s=stable;m=manager;previous=s;s.raw[SB_MISC]^=flip;
  sb_note_input(s,previous,SWEET_BATT_IRQ_TYPEC);sb_input_observe(m,s,timeNs);
  assert(!sb_input_permit(m,s,v,timeNs).valid);
 }
 for(unsigned result:{0x28u,0x48u,0x20u}){
  s=stable;m=manager;previous=s;s.raw[SB_APSD]=1;s.raw[SB_RESULT]=result;
  sb_note_input(s,previous,SWEET_BATT_IRQ_SOURCE);sb_input_observe(m,s,timeNs);
  assert(m.kind==SB_INPUT_NONE);
 }
 s=stable;m=manager;previous=s;s.raw[SB_USB]=0;
 sb_note_input(s,previous,SWEET_BATT_IRQ_USB);sb_input_observe(m,s,timeNs);
 assert(m.kind==SB_INPUT_NONE);
 previous=s;s.raw[SB_USB]=16;++s.inputEpoch;
 sb_note_input(s,previous,SWEET_BATT_IRQ_USB);sb_input_observe(m,s,timeNs);
 v.epoch=s.inputEpoch;auto newPermit=sb_input_permit(m,s,v,timeNs);assert(newPermit.valid);
 assert(!sb_input_permit_valid(p,s,timeNs)); // old observed session not reusable
 timeNs=originalTime;s=sample();m={};sb_input_observe(m,s,timeNs);
 v={true,5000000,21,s.inputEpoch,timeNs,timeNs};
 assert(m.kind==SB_INPUT_BC12 && sb_input_permit(m,s,v,timeNs).maxMa==500);
 timeNs+=10000000000ULL;v.startedNs=v.completedNs=timeNs;
 assert(sb_input_permit(m,s,v,timeNs).valid);
 previous=s;s.inputStartedNs=s.inputCompletedNs=timeNs;++s.inputEpoch;
 sb_note_input(s,previous,SWEET_BATT_IRQ_USB);sb_input_observe(m,s,timeNs);
 assert(m.kind==SB_INPUT_BC12 && m.sourceStartedNs==originalTime); // duplicate does not renew BC event time
 timeNs=originalTime;
 puts("PASS: durable CC/BC sessions beyond 3s, unchanged source timestamps, exact voltage epoch/window, pending-classification prerequisites, detach/role/QC/UV/OV revocation");
}
static void policyTests(){
    auto s=sample();auto l=limits();SweetBattController c={};SweetBattery control;authorizeControl(control);
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
                       SB_WDOG,SB_WDOG_TIME,SB_ICL,SB_FCC,SB_FV,SB_SUSPEND,SB_MODE,SB_LOAD,SB_OVERRIDE}){
        auto invalid=s;invalid.valid&=~(1ULL<<field);
        auto decision=sb_decide(c,invalid,l,timeNs);assert(!decision.allow && (decision.reasons&SB_R_TELEMETRY));
    }
    for(unsigned bit:{SB_R_BATTERY_ID,SB_R_PARALLEL,SB_R_INPUT}){
        l=limits();if(bit==SB_R_BATTERY_ID)l.batteryVerified=false;
        if(bit==SB_R_PARALLEL)l.parallelVerified=false;if(bit==SB_R_INPUT)l.input.valid=false;
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
    l.verifiedNs=1;l.input.vbusMv=9000;assert(sb_decide(c,s,l,timeNs).reasons&SB_R_INPUT);l=limits();
    assert(sb_decide(c,s,l,timeNs+4000000000ULL).reasons&SB_R_STALE);
    c={};d=sb_decide(c,s,l,timeNs);assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    d=sb_decide(c,s,l,c.deadlineNs);assert(d.reasons&SB_R_EXPIRED);
    assert(sb_apply(c,d,c.deadlineNs,false,SweetBattery::runtimeUpdate,&control,testClock) && c.stopped && !c.enabled && c.stopConfirmed);
    unsigned prior=writes.size();assert(!sb_apply(c,SweetBattDecision{0,500,4200,500,true,SB_PREPARE},timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(writes.size()==prior+1 && writes.back()==0x1042 && changed[0x1042]==0);
    // Decision expires while programming, before enable. The real clock must
    // prevent using the old decision even though the 60s session is still live.
    c={};s=sample();l=limits();authorizeControl(control);d=sb_decide(c,s,l,timeNs);
    updateCount=0;delayAt=5;writes.clear();
    assert(!sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(c.stopped && !c.enabled && changed[0x1042]==0);
    assert(writes.size()==6);delayAt=0;
    // Deadline is checked below the typed API before EACH physical write,
    // including all three writes of the software input mode operation.
    for(unsigned expiredAt=1;expiredAt<=11;++expiredAt){
        c={};s=sample();l=limits();authorizeControl(control);d=sb_decide(c,s,l,timeNs);
        updateCount=0;delayAt=expiredAt;writes.clear();
        assert(!sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
        assert(c.stopped && !c.enabled && changed[0x1042]==0);
        assert(writes.size()==expiredAt+1);delayAt=0;
    }

    c={};s=sample();l=limits();authorizeControl(control);d=sb_decide(c,s,l,timeNs);
    assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    // Hardware unexpectedly clears enable: disable/confirm and latch a fault.
    d=sb_decide(c,s,l,timeNs);assert(!d.allow && (d.reasons&SB_R_HARDWARE));
    assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(c.fault && !c.enabled);
    // Loss of authority disables an active controller; the zero approved ICL
    // is not a fabricated permanent hardware fault against its old setting.
    c={};c.enabled=c.owned=true;c.deadlineNs=timeNs+60000000000ULL;
    s=sample();s.raw[SB_ENABLE]=1;s.raw[SB_ICL]=s.raw[SB_FCC]=10;s.raw[SB_FV]=60;
    l=limits(s);l.input={};d=sb_decide(c,s,l,timeNs);
    assert(!d.allow && (d.reasons&SB_R_INPUT) && !(d.reasons&SB_R_HARDWARE));
    assert(sb_apply(c,d,timeNs,false,SweetBattery::runtimeUpdate,&control,testClock));
    assert(!c.enabled && !c.fault && c.stopConfirmed);
    puts("PASS: policy missing inputs/staleness/thermal hysteresis, dry-run, ordered control, every write failure and expiry");
}

static void dependencyTests(){
 const auto savedTime=timeNs;const auto savedRegs=regs;const auto savedFg=fg;
 reflectWrites=true;readAdvanceNs=1000;injectedAt=0;
 auto baseline=[&]{
  regs={{0x41a0,0x3c},{0x41a1,0x7d},{0x41a2,0x60},{0x41a3,0},
        {0x41a6,0x3c},{0x41a7,0x7d},{0x41a8,0x60},{0x41a9,0},
        {0x4107,0},{0x4009,160},{0x400a,160},{0x4010,2},
        {0x1042,1},{0x1310,16},{0x110b,0x95},{0x1362,0x18},
        {0x1506,2},{0x1509,0x23},{0x150b,0x3b},{0x1507,0},{0x150d,4},
        {0x1307,0x21},{0x1308,0x10},{0x1651,0x62},{0x1653,0x8c},{0x1648,0xa4}};
  fg={{0,100},{1,4027},{2,0x13},{3,4027}};
  changed.clear();failures.clear();injectedAt=0;
 };
 auto freshId=[&](SweetBattery &b,SweetAdc5Client &adc){
  adc.valid[0]=true;adc.samples[0]={};auto &id=adc.samples[0];
  id.sequence=100;id.microvolts=889000;id.started_ns=id.completed_ns=timeNs;
  b.fAdcClient=&adc;
 };
 for(unsigned mask:{0u,1u,2u,4u,7u}){
  baseline();SweetBattery b;IOTimerEventSource timer;SweetAdc5Client adc;
  b.fMonitor=&timer;b.fRuntimeMode=1;b.fDependencyMode=mask;
  b.sampleRuntime(true);freshId(b,adc);auto prior=writes.size();b.runRuntimePolicy();
  assert(!b.fLimits.parallelVerified && !b.fController.owned);
  if(mask<=1){assert(writes.size()==prior && !b.fDependencyStarted);}
  else {
   assert(b.fDependencyStarted && !b.fDependencyFailed && !(regs[0x1042]&1));
   assert(writes[prior]==0x1042);
   if(mask&2)assert((regs[0x1648]&0x18)==8 && (regs[0x1648]&~0x18)==(0xa4&~0x18));
   if(mask&4)assert(regs[0x1653]==0xf3 && regs[0x1651]==0x62 && b.fWatchdogPets==1);
   // An ADC callback before post-write telemetry must wait, never fail.
   auto before=writes.size();SweetBattery::adcChanged(&b);
   assert(!b.fDependencyFailed && writes.size()==before);
   timeNs+=1000000000ULL;b.sampleRuntime(false);b.runRuntimePolicy();
   assert(!b.fDependencyFailed);
   if(mask&2)assert(b.properties->values["SweetBatterySecondaryOutputInhibited"]);
  }
  assert(b.fLimits.batteryVerified==bool(mask&1));
  if(mask&1){
   assert(b.fIdentity.profile==1 && b.fIdentity.ohms>=94000 && b.fIdentity.ohms<=96000);
   assert(b.fLimits.maxFccMa==2800 && b.fLimits.maxFvMv==4430);
   auto snap=b.fSnapshot;auto &id=adc.samples[0];
   auto join=[&]{return sb_battery_identity(snap,true,id.microvolts,id.sequence,id.started_ns,id.completed_ns,timeNs);};
   assert(join().valid);
   snap.profilePrefix[0]^=1;assert(!join().valid);snap=b.fSnapshot;
   snap.nominalMah=4073;assert(!join().valid);snap=b.fSnapshot;
   snap.integrity=0;assert(!join().valid);snap=b.fSnapshot;
   id.microvolts=200000;assert(!join().valid);id.microvolts=889000;
   for(unsigned uv:{0u,999u,1825000u,UINT32_MAX}){id.microvolts=uv;assert(!join().valid);}id.microvolts=889000;
   snap.present=false;assert(!join().valid);snap=b.fSnapshot;
   snap.profileStartedNs=timeNs+1;snap.profileCompletedNs=timeNs+1;assert(!join().valid);snap=b.fSnapshot;
   auto ts=timeNs;timeNs=id.started_ns+60000000000ULL;assert(!join().valid);timeNs=ts;
   adc.valid[0]=false;b.runRuntimePolicy();assert(!b.fLimits.batteryVerified && !b.fLimits.maxFccMa);
   freshId(b,adc);
  }
  if(mask&4){
   // Pet exclusively on delivered bark; protection ticks do not pet.
   auto before=writes.size();timeNs+=1000000000ULL;b.sampleRuntime(false);b.runRuntimePolicy();assert(writes.size()==before);
   for(unsigned n=0;n<3;++n){
    timeNs+=16000000000ULL;b.fWatchdogPending=true;b.sampleRuntime(false);b.runRuntimePolicy();
    assert(!b.fDependencyFailed && b.fWatchdogPets==n+2 && writes.back()==0x1643);
   }
   unsigned actual=99;auto count=writes.size();
   assert(SweetBattery::runtimeUpdate(&b,SWEET_CHARGER_WATCHDOG_PET,1,timeNs+1000000,&actual)==SWEET_SPMI_INVALID);
   b.fPowerStopping=true;
   assert(b.dependencyCommand(SWEET_CHARGER_WATCHDOG_PET,b.fDependencyDeadlineNs,&actual)==SWEET_SPMI_NOT_READY);
   assert(writes.size()==count);b.fPowerStopping=false;
   timeNs=b.fDependencyDeadlineNs;b.runRuntimePolicy();assert(b.fDependencyFinished && !b.fWatchdogArmed && !b.fDependencyFailed);
   count=writes.size();b.fWatchdogPending=true;b.runRuntimePolicy();assert(writes.size()==count);
  }
  if(mask&6){b.stopRuntime();assert(!(regs[0x1042]&1) && !b.fWatchdogArmed);}
 }
 // Every preparation write and the first pet may have reached hardware before
 // returning an error. Fail terminally, disable, and never silently retry.
 for(unsigned write=1;write<=5;++write){
  baseline();SweetBattery b;IOTimerEventSource timer;b.fMonitor=&timer;
  b.fRuntimeMode=1;b.fDependencyMode=6;b.sampleRuntime(true);injectedAt=updateCount+write;
  b.runRuntimePolicy();assert(b.fDependencyFailed && !b.fWatchdogArmed && !b.fLimits.parallelVerified);
  injectedAt=0;auto count=writes.size();b.runRuntimePolicy();assert(writes.size()==count);
 }
 for(unsigned fault=0;fault<6;++fault){
  baseline();SweetBattery b;IOTimerEventSource timer;b.fMonitor=&timer;b.fRuntimeMode=1;b.fDependencyMode=6;
  b.sampleRuntime(true);b.runRuntimePolicy();assert(b.fWatchdogArmed);
  timeNs+=1000000000ULL;b.sampleRuntime(false);b.runRuntimePolicy();
  b.fWatchdogPending=true;b.runRuntimePolicy(); // an actual bark establishes the maintenance expectation
  if(fault==0)regs[0x1648]=0;
  if(fault==1)regs[0x1653]=0x8c;
  if(fault==2)regs[0x1042]=1;
  if(fault==3)failures={0x1651};
  if(fault==4)b.connection.unavailable=true;
  if(fault==5)timeNs+=24000000000ULL;
  b.sampleRuntime(false);b.runRuntimePolicy();assert(b.fDependencyFailed && !b.fWatchdogArmed);
  assert(!(regs[0x1042]&1));failures.clear();
 }
 // A cable inherited across configuration does not prove the plugin-enabled
 // timer started. A new source sample must not arm or renew the bark deadline.
 baseline();SweetBattery timing;timing.fRuntimeMode=1;timing.fDependencyMode=4;
 timing.sampleRuntime(true);timing.runRuntimePolicy();
 timeNs+=24000000000ULL;timing.sampleRuntime(false);timing.runRuntimePolicy();
 assert(!timing.fDependencyFailed && !timing.fWatchdogInput);
 inputSample(timing,SWEET_BATT_IRQ_SOURCE);timing.runRuntimePolicy();
 assert(!timing.fWatchdogInput);
 inputSample(timing,SWEET_BATT_IRQ_USB);timing.runRuntimePolicy();assert(timing.fWatchdogInput);
 auto barkDeadline=timing.fWatchdogLastNs;
 timeNs+=16000000000ULL;inputSample(timing,SWEET_BATT_IRQ_SOURCE);timing.runRuntimePolicy();
 assert(timing.fWatchdogLastNs==barkDeadline && !timing.fDependencyFailed);
 timeNs+=7000000000ULL;timing.sampleRuntime(false);timing.runRuntimePolicy();
 assert(timing.fDependencyFailed); // no recurring source/ADC sample extends it
 // The same lock serializes a pet against priority sleep/shutdown disable.
 for(unsigned n=0;n<20;++n){
  baseline();SweetBattery racing;racing.fRuntimeMode=1;racing.fDependencyMode=4;
  racing.sampleRuntime(true);racing.runRuntimePolicy();std::atomic<bool> go{false};
  std::thread pet([&]{while(!go.load())std::this_thread::yield();unsigned v=0;
   int rc=racing.dependencyCommand(SWEET_CHARGER_WATCHDOG_PET,racing.fDependencyDeadlineNs,&v);
   assert(rc==0 || rc==SWEET_SPMI_NOT_READY);});
  std::thread power([&]{while(!go.load())std::this_thread::yield();
   SweetBattery::runtimePowerNotice(&racing,nullptr,kIOMessageSystemWillPowerOff,nullptr,nullptr,0);});
  go=true;pet.join();power.join();assert(racing.fPowerStopping && writes.back()==0x1042 && !(regs[0x1042]&1));
 }
 // No mux means no writes or first-time initialization, and wrong diagnostic
 // operation/mask cannot submit unrelated preparation.
 baseline();SweetBattery waiting;waiting.fRuntimeMode=1;waiting.fDependencyMode=2;waiting.fIrqAttached=false;
 auto before=writes.size();waiting.runRuntimePolicy();assert(writes.size()==before && !waiting.fDependencyStarted);
 unsigned actual=123;
 assert(waiting.dependencyCommand(SWEET_CHARGER_WATCHDOG_PREPARE,timeNs+1000,&actual)==SWEET_SPMI_INVALID);
 auto snapshot=sample();auto bounded=limits(snapshot);SweetBattController controller={};
 bounded.batteryUntilNs=timeNs+100;
 auto decision=sb_decide(controller,snapshot,bounded,timeNs);
 assert(decision.allow && decision.validUntilNs==timeNs+100);
 assert(sb_decide(controller,snapshot,bounded,timeNs+100).reasons&SB_R_BATTERY_ID);
 bounded.batteryUntilNs=0;assert(sb_decide(controller,snapshot,bounded,timeNs).reasons&SB_R_BATTERY_ID);
 reflectWrites=false;readAdvanceNs=0;injectedAt=0;failures.clear();
 regs=savedRegs;fg=savedFg;timeNs=savedTime;writes.clear();changed.clear();updateCount=0;
 puts("PASS: dependency masks, fresh BAT_ID/profile identity and board bounds; no parallel authorization, no implicit main enable");
 puts("PASS: disabled-charge preparation, post-write telemetry, bark-only pulse, finite watchdog window, every write failure and real config/mux loss terminate without retry");
}
static void queuedSourceTests(){
 const auto savedTime=timeNs;const auto savedRegs=regs;const auto savedFg=fg;
 auto writeStart=writes.size();
 regs={{0x41a0,0x3c},{0x41a1,0x7d},{0x41a2,0x60},{0x41a3,0},
       {0x41a6,0x3c},{0x41a7,0x7d},{0x41a8,0x60},{0x41a9,0},
       {0x4107,0},{0x1310,16},{0x110b,0x95},{0x1362,0x18},
       {0x1506,2},{0x1509,0x23},{0x150b,0x3b},{0x150d,4},{0x1307,0},{0x1308,0}};
 fg={{0,100},{1,4027},{2,0x13},{3,4027}};
 SweetBattery b;SweetAdc5Client adc;IOTimerEventSource timer;
 b.fAdcClient=&adc;b.fRuntimeMode=1;b.fMonitor=&timer;
 unsigned token=1000;
 auto voltage=[&]{adc.present=true;adc.epoch=b.fSnapshot.inputEpoch;adc.valid[1]=true;
  auto &v=adc.samples[1];v={};v.sequence=++token;v.microvolts=5090000;
  v.started_ns=v.completed_ns=timeNs;SweetBattery::adcChanged(&b);};
 auto ready=[&]{b.connection.pending=0;b.fIrqPending=0;regs[0x1310]=16;
  regs[0x1506]=2;regs[0x1509]=0x23;regs[0x150b]=0x3b;regs[0x1507]=0;regs[0x150d]=4;
  regs[0x1307]=0;regs[0x1308]=0;
  inputSample(b,SWEET_BATT_IRQ_USB|SWEET_BATT_IRQ_TYPEC);voltage();assert(b.fLimits.input.valid);};
 inputSample(b,0);voltage();assert(!b.fLimits.input.valid); // residue alone never qualifies
 ready();auto sourceTime=b.fSnapshot.ccStartedNs;
 timeNs+=1250000000ULL;regs[0x1307]=0x21;regs[0x1308]=0x10;
 bool delivered=false;
 sampleHook=[&](unsigned addr){if(!delivered && addr==0x1509){delivered=true;b.connection.pending=SWEET_BATT_IRQ_SOURCE;}};
 inputSample(b,SWEET_BATT_IRQ_SOURCE);sampleHook=nullptr;assert(delivered);
 assert(b.fSnapshot.ccFromEvent && b.fSnapshot.apsdFromSource); // failed by 0163
 assert(b.fSnapshot.ccStartedNs==sourceTime && b.fInputSession.kind==SB_INPUT_CC);
 b.runRuntimePolicy();assert(!b.fLimits.input.valid);voltage();assert(!b.fLimits.input.valid);
 unsigned actual=0;auto prior=writes.size();
 assert(SweetBattery::runtimeUpdate(&b,SWEET_CHARGER_ENABLE,1,timeNs+1000,&actual)==SWEET_SPMI_NOT_READY);
 assert(writes.size()==prior);
 // Deliver the second notification, then acquire its replacement input tuple.
 b.connection.pending=0;b.fIrqPending=SWEET_BATT_IRQ_SOURCE;b.runRuntimePolicy();assert(!b.fLimits.input.valid);
 b.fIrqPending=0;inputSample(b,SWEET_BATT_IRQ_SOURCE);b.runRuntimePolicy();assert(!b.fLimits.input.valid);
 assert(b.fSnapshot.ccFromEvent && b.fSnapshot.ccStartedNs==sourceTime);
 voltage();assert(b.fLimits.input.valid && b.fLimits.input.maxMa==3000);
 // A queued source notification during a protection tick is equally harmless
 // to history. It must block use, not rewrite the cached source acquisition.
 auto epoch=b.fSnapshot.inputEpoch;auto inputTime=b.fSnapshot.inputStartedNs;
 for(unsigned n=0;n<10;++n){
  timeNs+=1000000000ULL;
  b.connection.pending=SWEET_BATT_IRQ_SOURCE;b.sampleRuntime(false);b.runRuntimePolicy();assert(!b.fLimits.input.valid);
  assert(b.fSnapshot.ccFromEvent && b.fSnapshot.ccStartedNs==sourceTime && b.fSnapshot.inputEpoch==epoch);
  b.connection.pending=0;inputSample(b,SWEET_BATT_IRQ_SOURCE);++epoch;voltage();
  assert(b.fLimits.input.valid && b.fSnapshot.ccStartedNs==sourceTime);
 }
 timeNs+=10000000000ULL;b.sampleRuntime(false);voltage();
 assert(!sb_cc_event_fresh(b.fSnapshot,timeNs) && b.fLimits.input.valid && b.fSnapshot.ccStartedNs==sourceTime);
 assert(b.fSnapshot.inputStartedNs!=inputTime); // source IRQs acquired; ticks alone never do
 // Repeated source interrupts cannot hide actual invalid readings, a detach,
 // changed role/Rp/accessory, OV/UV or unsupported QC.
 for(unsigned fault=0;fault<7;++fault){
  ready();regs[0x1307]=0x21;regs[0x1308]=0x10;
  if(fault==0)regs[0x1310]=0x26;
  if(fault==1)regs[0x1310]=0x14;
  if(fault==2)regs[0x1310]=0x18;
  if(fault==3)regs[0x150b]|=0x40;
  if(fault==4)regs[0x1506]=4; // changed Rp cannot inherit the old CC event
  if(fault==5){regs[0x1307]=3;regs[0x1308]=0x28;}
  if(fault==6)failures={0x1507};
  b.connection.pending=SWEET_BATT_IRQ_SOURCE;inputSample(b,SWEET_BATT_IRQ_SOURCE);voltage();
  assert(!b.fLimits.input.valid && b.fInputSession.kind==SB_INPUT_NONE);
  b.connection.pending=0;inputSample(b,SWEET_BATT_IRQ_SOURCE);voltage();assert(!b.fLimits.input.valid);
  failures.clear();
 }
 // Standalone source IRQs, even duplicated, cannot establish initial CC on
 // a startup snapshot that has never observed plugin/Type-C provenance.
 b.fSnapshot={};b.fInputSession={};b.fMonitorSamples=0;
 regs[0x1310]=16;regs[0x1506]=2;regs[0x150b]=0x3b;regs[0x1507]=0;regs[0x150d]=4;
 inputSample(b,0);inputSample(b,SWEET_BATT_IRQ_SOURCE);voltage();assert(!b.fLimits.input.valid);
 inputSample(b,SWEET_BATT_IRQ_SOURCE);voltage();assert(!b.fLimits.input.valid);
 assert(writes.size()==writeStart && !b.fController.owned);
 timeNs=savedTime;regs=savedRegs;fg=savedFg;
 puts("PASS: source IRQ queued during actual sampler/protection tick preserves validated history, both queues and new ADC epoch fence use, long-session recovery, real faults still revoke");
}
int main(){
    dependencyTests();queuedSourceTests();evidenceTests();apsdEventTests();ccTests();inputSessionTests();policyTests();reads.clear();writes.clear();changed.clear();
    regs={{0x41a0,0xff},{0x41a1,0x7f},{0x41a2,0x60},{0x41a3,0xed},
          {0x41a6,0xff},{0x41a7,0x7f},{0x41a8,0x60},{0x41a9,0xed},
          {0x4009,180},{0x400a,180},{0x4010,2},{0x4107,0},{0x1310,16},{0x1006,3},{0x110b,0x95},{0x1307,1}};
    fg={{0,100},{1,4000},{2,1},{3,3900}};
    IOTimerEventSource timer;SweetBattery b;b.fMonitor=&timer;
    assert(!SweetBattery::startRuntime(&b,nullptr,nullptr,nullptr,nullptr));
    assert(b.fSnapshot.tempValid && b.fSnapshot.tempCenti==2500 && b.fSnapshot.currentValid && b.fSnapshot.ma>2300);
    assert(b.fSnapshot.nominalValid && b.fSnapshot.nominalMah==4000 && b.fSnapshot.learnedValid && b.fSnapshot.learnedMah==3900);
    assert(b.fSnapshot.profilePrefixValid && b.fSnapshot.inputEpoch==1);
    assert(!b.fController.owned && writes.empty() && timer.armed);
    assert(b.fAdcClient && b.fAdcClient->updates==1 && b.fAdcClient->sourceUpdates==1);
    assert(!b.registry["SweetBatteryVBUSMeasured"] && !b.registry.count("SweetBatteryVBUSMicrovolts"));
    assert(b.registry["Temperature"]==2500 && b.registry["MaxCapacity"]==3900);
    assert(b.registry["DesignCapacity"]==4000 && b.registry["CurrentCapacity"]==2730);
    assert(!b.registry.count("CycleCount") && !b.registry["SweetBatterySOCEstimated"]);
    assert(!b.legacy.count("CycleCount"));
    // A telemetry tick must not discover USB/APSD changes: those require IRQ.
    regs[0x1310]=0;reads.clear();++timeNs;
    auto inputStart=b.fSnapshot.inputStartedNs,inputEnd=b.fSnapshot.inputCompletedNs;
    auto profileStart=b.fSnapshot.profileStartedNs;
    SweetBattery::monitorFired(&b,&timer);
    for(auto a:reads)assert(a!=0x1310 && a!=0x1307 && a!=0x1308 && a!=0x1506 && a!=0x1509 && a!=0x150b && a!=0x1507 && a!=0x150d && a!=0x1362 && a!=0x10004);
    assert(b.fSnapshot.inputEpoch==1 && b.fSnapshot.inputStartedNs==inputStart && b.fSnapshot.inputCompletedNs==inputEnd);
    assert(b.fSnapshot.profileStartedNs==profileStart);
    assert((b.fSnapshot.raw[SB_USB]&16) && b.publishes==2 && b.publishedCurrentValid);
    assert(b.fAdcClient->sourceUpdates==1);
    inputSample(b);assert(!b.fSnapshot.raw[SB_USB]);
    assert(b.fAdcClient->sourceUpdates==2);
    assert(b.fSnapshot.inputEpoch==2);
    failures={0x10000};pairFailure=true;reads.clear();
    SweetBattery::monitorFired(&b,&timer);
    assert(!b.fSnapshot.tempValid && !b.fSnapshot.currentValid && !b.publishedCurrentValid);
    assert(b.fLastReasons&SB_R_TELEMETRY);
    assert(!b.registry.count("Temperature") && !b.registry.count("Amperage") && !b.registry.count("InstantAmperage"));
    assert(!b.legacy.count("Amperage"));
    failures={0x10001,0x10002,0x10003};pairFailure=false;logs.clear();inputSample(b);b.publishRuntime();
    assert(!b.registry.count("DesignCapacity") && b.registry["MaxCapacity"]==100 && b.registry["SweetBatteryCapacityUsesPercent"]);
    assert(logs.find("n-error=-10")!=std::string::npos && logs.find("l-error=-10")!=std::string::npos && logs.find("p-error=-10")!=std::string::npos);
    assert(!b.fSnapshot.profilePrefixValid && !sb_profile_match(b.fSnapshot));
    for(auto byte:b.fSnapshot.profilePrefix)assert(!byte);
    assert(!b.fSnapshot.profileStartedNs && !b.fSnapshot.profileCompletedNs);
    assert(logs.find("SWEET BATT CONFIG: v3 id=")!=std::string::npos);
    assert(logs.find("SWEET BATT SAMPLE: v2 id=")!=std::string::npos && logs.find("SWEET BATT TIME: v3 id=")!=std::string::npos);
    regs[0x1651]=0xc2;regs[0x1653]=0x87;failures={0x1042};logs.clear();inputSample(b);
    assert(logs.find("enable-error=-9")!=std::string::npos && logs.find("wdog=0xc2 wdog-error=0 wdog-time=0x87 wdog-time-error=0")!=std::string::npos);
    // A protection tick does not reprint the event-only config diagnostic.
    logs.clear();b.sampleRuntime(false);assert(logs.find("SWEET BATT CONFIG:")==std::string::npos);
    failures.clear();pairFailure=false;fg[0]=0x3fc;b.sampleRuntime(false);
    assert(b.fSnapshot.tempValid && b.fSnapshot.tempCenti==-100);
    fg[0]=0xffff;b.sampleRuntime(false);assert(!b.fSnapshot.tempValid);
    // Input interval ends before the FG tuple/telemetry, not after it.
    readAdvanceNs=1000;fg[0]=100;fg[1]=4027;fg[2]=0x13;fg[3]=4027;failures.clear();
    logs.clear();inputSample(b);
    assert(b.fSnapshot.inputCompletedNs<b.fSnapshot.profileStartedNs && b.fSnapshot.profileCompletedNs<=b.fSnapshot.completedNs);
    assert(sb_profile_match(b.fSnapshot)==1 && logs.find("match=1 integrity=0x13 nominal=4027 prefix=0900ffe3f10796dab3d50000a1bda38af88784929e942380")!=std::string::npos);
    assert(logs.find("SWEET BATT INPUT: v4")!=std::string::npos && logs.find("SWEET BATT ICL MODE: v1")!=std::string::npos);
    // A failed newly-read source never retains a prior valid raw value.
    failures={0x1507};b.fSnapshot.raw[SB_DEBUG_ACCESS]=0x3f;inputSample(b);
    assert(!sb_valid(b.fSnapshot,SB_DEBUG_ACCESS) && !b.fSnapshot.raw[SB_DEBUG_ACCESS] && !sb_input_fresh(b.fSnapshot,timeNs));
    failures.clear();readAdvanceNs=0;
    // Real sampler integration: source IRQ completion, same-level plugin,
    // unrelated battery refresh, and status/VBUS changes during acquisition.
    regs[0x1310]=16;regs[0x1307]=1;regs[0x1308]=8;regs[0x1362]=8;
    inputSample(b,SWEET_BATT_IRQ_SOURCE);
    assert(b.fSnapshot.apsdFromSource && b.fSnapshot.apsdCoherent);
    auto sourceStart=b.fSnapshot.apsdStartedNs;
    auto sourceEpoch=b.fSnapshot.inputEpoch;
    reads.clear();b.sampleRuntime(true); // CHGR/MSOC/bounded confirm, no input cause
    for(auto a:reads)assert(a!=0x1307 && a!=0x1308 && a!=0x1310 && a!=0x1362);
    assert(b.fSnapshot.inputEpoch==sourceEpoch && b.fSnapshot.apsdStartedNs==sourceStart);
    inputSample(b);assert(b.fSnapshot.apsdStartedNs==sourceStart);
    for(unsigned addr:{0x1307u,0x1310u}){
        regs[0x1307]=1;regs[0x1310]=16;
        mutateAtAddr=addr;mutateNth=2;mutateCount=0;mutateValue=0;
        inputSample(b,SWEET_BATT_IRQ_SOURCE);
        assert(!b.fSnapshot.apsdCoherent && !b.fSnapshot.apsdFromSource);
        assert(b.fSnapshot.errors[addr==0x1307?SB_APSD:SB_USB]==SWEET_SPMI_VERIFY);
    }
    mutateAtAddr=0;regs[0x1310]=16;regs[0x1307]=1;
    failures={0x1362};inputSample(b,SWEET_BATT_IRQ_SOURCE);
    assert(!b.fSnapshot.apsdFromSource && !sb_valid(b.fSnapshot,SB_APSD_OPTIONS));
    failures.clear();b.fIrqPending=SWEET_BATT_IRQ_USB;
    inputSample(b,SWEET_BATT_IRQ_SOURCE);assert(b.fSnapshot.apsdFromSource);
    b.runRuntimePolicy();assert(!b.fLimits.input.valid);
    b.fIrqPending=0;inputSample(b,SWEET_BATT_IRQ_SOURCE);
    assert(sb_apsd_event_fresh(b.fSnapshot,timeNs));
    assert(logs.find("SWEET BATT APSD EVENT: v1")!=std::string::npos);
    // Type-C IRQ acquires coherent CC independently of APSD completion.
    regs[0x1506]=4;regs[0x1509]=0x20;regs[0x150b]=0x29;regs[0x1507]=regs[0x150d]=0;
    regs[0x1307]=regs[0x1308]=regs[0x1362]=0;
    inputSample(b,SWEET_BATT_IRQ_TYPEC);
    assert(b.fSnapshot.ccCoherent && sb_cc_event_fresh(b.fSnapshot,timeNs));
    assert(sb_input_capability(b.fSnapshot,timeNs).kind==SB_INPUT_CC);
    assert(!b.fController.owned && !sb_apsd_event_fresh(b.fSnapshot,timeNs));
    assert(logs.find("SWEET BATT CC: v1")!=std::string::npos && logs.find("SWEET BATT INPUT CAP: v1")!=std::string::npos);
    for(unsigned addr:{0x1506u,0x1509u,0x150bu,0x1507u,0x150du,0x1310u}){
        regs[0x1506]=4;regs[0x1509]=0x20;regs[0x150b]=0x29;regs[0x1507]=regs[0x150d]=0;regs[0x1310]=16;
        mutateAtAddr=addr;mutateNth=2;mutateCount=0;mutateValue=addr==0x1507||addr==0x150d?1:0;
        inputSample(b,SWEET_BATT_IRQ_TYPEC);
        assert(!b.fSnapshot.ccCoherent && !b.fSnapshot.ccFromEvent);
    }
    mutateAtAddr=0;regs[0x1310]=16;regs[0x1506]=4;regs[0x1509]=0x20;regs[0x150b]=0x29;regs[0x1507]=regs[0x150d]=0;
    failures={0x1307,0x1308,0x1362};inputSample(b,SWEET_BATT_IRQ_TYPEC);
    assert(sb_input_capability(b.fSnapshot,timeNs).kind==SB_INPUT_CC);
    failures.clear();b.fIrqPending=SWEET_BATT_IRQ_TYPEC;
    inputSample(b,SWEET_BATT_IRQ_TYPEC);assert(b.fSnapshot.ccFromEvent && !b.fSnapshot.apsdFromSource);
    b.runRuntimePolicy();assert(!b.fLimits.input.valid);
    b.fIrqPending=0;
    inputSample(b,SWEET_BATT_IRQ_TYPEC);b.publishRuntime();
    assert(b.registry["SweetBatteryCCEvidenceFresh"] && b.registry["SweetBatteryCCAdvertisedMA"]==1500);
    assert(b.registry["SweetBatteryObservedInputKind"]==SB_INPUT_CC);
    // Actual sampler -> policy -> publication with the captured Rp3A/misc39
    // tuple. Neither an observation nor registry publication submits a write.
    auto ccWrites=writes.size();
    regs[0x1506]=2;regs[0x1509]=0x23;regs[0x150b]=0x39;regs[0x150d]=4;
    regs[0x1307]=0x21;regs[0x1308]=0x10;regs[0x1362]=0x18;
    inputSample(b,SWEET_BATT_IRQ_TYPEC);b.runRuntimePolicy();b.publishRuntime();
    assert(b.registry["SweetBatteryCCRole"]==SB_CC_ROLE_SINK);
    assert(b.registry["SweetBatteryCCObservedFlags"]==SB_CC_OBS_VBUS_ERROR);
    assert(!b.registry["SweetBatteryCCRejectReasons"] && b.registry["SweetBatteryCCAdvertisedMA"]==3000);
    assert(b.registry["SweetBatteryCCEvidenceFresh"] && b.registry["SweetBatteryObservedInputKind"]==SB_INPUT_CC);
    assert(!b.fController.owned && !b.fController.enabled && writes.size()==ccWrites);
    assert(logs.find("SWEET BATT CC ROLE: v1")!=std::string::npos);
    for(unsigned fault:{4u,8u}){
        regs[0x1310]=16|fault;
        inputSample(b,SWEET_BATT_IRQ_TYPEC);b.publishRuntime();
        assert(!b.registry["SweetBatteryCCEvidenceFresh"]);
        assert(b.registry["SweetBatteryObservedInputKind"]==SB_INPUT_NONE);
        assert(b.registry["SweetBatteryCCRejectReasons"]==(fault==4?SB_CC_USB_UV:SB_CC_USB_OV));
    }
    regs[0x1310]=16;inputSample(b,SWEET_BATT_IRQ_TYPEC);
    b.fIrqPending=SWEET_BATT_IRQ_TYPEC;b.runRuntimePolicy();b.publishRuntime();
    assert(!b.registry["SweetBatteryCCEvidenceFresh"] && !b.fLimits.input.valid);
    assert(b.registry["SweetBatteryObservedInputKind"]==SB_INPUT_NONE);
    // Pending input revokes command submission; an explicit disable still works.
    unsigned actual=0;auto commands=writes.size();
    assert(SweetBattery::runtimeUpdate(&b,SWEET_CHARGER_ENABLE,1,timeNs+1000,&actual)==SWEET_SPMI_NOT_READY);
    assert(writes.size()==commands);
    assert(!SweetBattery::runtimeUpdate(&b,SWEET_CHARGER_ENABLE,0,0,&actual));
    b.fIrqPending=0;
    b.fSnapshot.inputEpoch=UINT64_MAX-1;inputSample(b);assert(b.fSnapshot.inputEpoch==UINT64_MAX);
    inputSample(b);assert(!sb_input_fresh(b.fSnapshot,timeNs) && !b.fSnapshot.inputStartedNs);
    // Largest sample id and timestamp must retain the complete time record.
    auto savedTime=timeNs;auto savedSamples=b.fMonitorSamples;
    timeNs=UINT64_MAX-1;b.fMonitorSamples=UINT32_MAX-1;logs.clear();inputSample(b);
    assert(logs.find("SWEET BATT TIME: v3 id=4294967295 started-ns=18446744073709551614 completed-ns=18446744073709551614\n")!=std::string::npos);
    timeNs=savedTime;b.fMonitorSamples=savedSamples;
    // Stop cancels the actual monitor and leaves it unable to rearm/submit IO.
    b.fController.owned=b.fController.enabled=true;b.fController.deadlineNs=timeNs+1000000;
    b.stopRuntime();assert(timer.disabled && !timer.armed && !b.fController.enabled);
    auto arms=timer.arms;auto samples=b.fMonitorSamples;SweetBattery::monitorFired(&b,&timer);
    assert(timer.arms==arms && b.fMonitorSamples==samples);
    // Actual IRQ failure path must withdraw all modern/legacy source flags,
    // preserve the last percentage, and invalidate measured data without IO.
    SweetBattery failed;IOTimerEventSource failedTimer;SweetBattery::MuxStub connection;
    failed.fRuntimeMode=1;failed.fMonitor=&failedTimer;failed.fTimer=&failedTimer;
    failed.fMuxClient=&connection;failed.fSnapshot=sample();failed.fUiDiag=true;
    failed.fSnapshot.raw[SB_ENABLE]=1;failed.fSnapshot.raw[SB_STAGE]=3;
    failed.publishRuntime();assert(failed.registry["External"] && failed.registry["Charging"]);
    auto beforeReads=reads.size(),beforeWrites=writes.size();auto beforePublications=failed.publishes;
    failed.failIrq("test Type-C attach rejected");
    assert(failed.fIrqFailed && !failed.fRunning && !failed.fIrqAttached && connection.detachments==1);
    assert(failed.publishes==beforePublications+1 && !failed.registry["External"] && !failed.registry["Capable"] && !failed.registry["Charging"]);
    assert(!failed.registry["AppleRawExternalConnected"] && failed.legacy["Flags"]==kIOBatteryInstalled);
    assert(!failed.registry["SweetBatteryExternalValid"] && !failed.registry["SweetBatteryChargeStateValid"]);
    assert(!failed.registry["SweetBatteryHardwareSampleValid"] && !failed.registry["SweetBatteryCurrentValid"]);
    assert(!failed.registry.count("Amperage") && !failed.registry.count("Temperature"));
    assert(failed.registry["SweetBatteryPercent"]==70 && !failed.fSnapshot.valid);
    assert(!failed.fSnapshot.apsdFromSource && !failed.fSnapshot.ccFromEvent);
    assert(reads.size()==beforeReads && writes.size()==beforeWrites);
    failed.failIrq("duplicate");assert(failed.publishes==beforePublications+1);
    puts("PASS: actual IRQ failure withdraws modern/legacy source flags and measurement validity, no fallback/read/retry");
    SweetBattery shutdown;authorizeControl(shutdown);unsigned verified=0;
    assert(!SweetBattery::runtimeUpdate(&shutdown,SWEET_CHARGER_ENABLE,1,timeNs+1000000000,&verified));
    std::thread power([&]{assert(!SweetBattery::runtimePowerNotice(&shutdown,nullptr,kIOMessageSystemWillRestart,nullptr,nullptr,0));});
    power.join();assert(shutdown.fPowerStopping && changed[0x1042]==0);
    auto prior=writes.size();assert(SweetBattery::runtimeUpdate(&shutdown,SWEET_CHARGER_ENABLE,1,timeNs+1000000000,&verified)==SWEET_SPMI_NOT_READY);
    assert(writes.size()==prior);
    for(unsigned round=0;round<100;++round){
        SweetBattery racing;authorizeControl(racing);racing.fControlClaimed=true;std::atomic<bool> go{false};
        std::thread submit([&]{while(!go.load()){}for(unsigned i=0;i<20;++i){unsigned v;
            int rc=SweetBattery::runtimeUpdate(&racing,SWEET_CHARGER_ENABLE,1,timeNs+1000000000,&v);assert(rc==0 || rc==SWEET_SPMI_NOT_READY);}});
        std::thread notify([&]{while(!go.load()){}SweetBattery::runtimePowerNotice(&racing,nullptr,kIOMessageSystemWillPowerOff,nullptr,nullptr,0);});
        go=true;submit.join();notify.join();assert(racing.fPowerStopping && changed[0x1042]==0);
    }
    // Completed ADC values publish as measurements, never charge authority.
    SweetBattery measured;SweetAdc5Client adc;measured.fAdcClient=&adc;measured.fRuntimeMode=1;
    measured.fSnapshot=sample();sb_input_observe(measured.fInputSession,measured.fSnapshot,timeNs);
    adc.epoch=measured.fSnapshot.inputEpoch;adc.present=true;adc.valid[0]=adc.valid[1]=true;
    adc.samples[0].started_ns=adc.samples[0].completed_ns=timeNs;
    adc.samples[1].started_ns=adc.samples[1].completed_ns=timeNs;
    adc.samples[0].microvolts=889684;adc.samples[1].microvolts=4831136;adc.samples[1].sequence=17;
    SweetBattery::adcChanged(&measured);
    assert(measured.registry["SweetBatteryVBUSMeasured"] && measured.registry["SweetBatteryVBUSMicrovolts"]==4831136);
    assert(!measured.fLimits.batteryVerified && measured.fLimits.input.valid && !measured.fLimits.parallelVerified && !measured.fController.owned);
    assert(!(measured.fLastReasons&SB_R_INPUT) && (measured.fLastReasons&SB_R_BATTERY_ID) && (measured.fLastReasons&SB_R_PARALLEL));
    auto originalInputTime=measured.fSnapshot.inputStartedNs;
    auto writesBeforeInput=writes.size();
    // Both stages of the delivered/queued IRQ fence must revoke a fresh lease.
    for(unsigned event:{SWEET_BATT_IRQ_USB,SWEET_BATT_IRQ_SOURCE,SWEET_BATT_IRQ_TYPEC,SWEET_BATT_IRQ_INPUT_FAULT,SWEET_MUX_FAILED}){
     measured.connection.pending=event;
     unsigned actual=0;auto prior=writes.size();
     assert(SweetBattery::runtimeUpdate(&measured,SWEET_CHARGER_ENABLE,1,timeNs+1000,&actual)==SWEET_SPMI_NOT_READY);
     assert(writes.size()==prior);SweetBattery::adcChanged(&measured);
     assert(!measured.fLimits.input.valid && !measured.registry["SweetBatteryInputVerified"]);
     measured.connection.pending=0;SweetBattery::adcChanged(&measured);assert(measured.fLimits.input.valid);
     measured.fIrqPending=event;SweetBattery::adcChanged(&measured);assert(!measured.fLimits.input.valid);
     measured.fIrqPending=0;SweetBattery::adcChanged(&measured);assert(measured.fLimits.input.valid);
    }
    assert(writes.size()==writesBeforeInput && !measured.fController.owned);
    timeNs+=3000000001ULL;SweetBattery::adcChanged(&measured);
    assert(!measured.registry["SweetBatteryVBUSMeasured"] && !measured.registry.count("SweetBatteryVBUSMicrovolts"));
    assert(!measured.fLimits.input.valid);
    adc.samples[1].started_ns=adc.samples[1].completed_ns=timeNs;++adc.samples[1].sequence;
    measured.fSnapshot.startedNs=measured.fSnapshot.completedNs=timeNs;
    SweetBattery::adcChanged(&measured);assert(measured.fLimits.input.valid);
    assert(measured.fSnapshot.inputStartedNs==originalInputTime && !sb_apsd_event_fresh(measured.fSnapshot,timeNs));
    adc.invalidate();unsigned rejected=0;
    assert(SweetBattery::runtimeUpdate(&measured,SWEET_CHARGER_ENABLE,1,timeNs+1000,&rejected)==SWEET_SPMI_NOT_READY);
    SweetBattery::adcChanged(&measured);assert(!measured.fLimits.input.valid);
    assert(writes.size()==writesBeforeInput && !measured.fController.owned);
    measured.fPowerStopping=true;SweetBattery::adcChanged(&measured);
    assert(!measured.registry["SweetBatteryBATIDMeasured"] && !measured.registry.count("SweetBatteryBATIDMicrovolts"));
    sleepWakeNote note={nullptr,1};
    assert(!SweetBattery::runtimePowerNotice(&shutdown,nullptr,kIOMessageSystemWillSleep,nullptr,&note,sizeof(note)));
    assert(acknowledged==1 && note.returnValue==0);
    puts("PASS: actual input permission joins source + exact-epoch VBUS, both IRQ queues suspend/revalidate, voltage expiry/loss revokes, missing dependencies never write");
    puts("PASS: actual runtime sampler, units/validity, IRQ-only input acquisition, monitor publication and stop/no rearm");
}
'''
names=['sweet_batt_now_ns','SweetBattery::dependencyCommand','SweetBattery::dependencyFailure','SweetBattery::refreshDependencies','SweetBattery::inputChangePending','SweetBattery::refreshInputPermit','SweetBattery::adcChanged','SweetBattery::publishAdc','SweetBattery::sampleRuntime','SweetBattery::runtimeUpdate',
       'SweetBattery::runRuntimePolicy','SweetBattery::startRuntime',
       'SweetBattery::monitorFired','SweetBattery::stopRuntime','SweetBattery::failIrq','SweetBattery::publishRuntime','SweetBattery::runtimeClock','SweetBattery::publish','SweetBattery::removePowerValue','SweetBattery::runtimePowerNotice']
with tempfile.TemporaryDirectory(prefix='mh2lm-runtime-') as directory:
    cpp=Path(directory)/'test.cpp';binary=Path(directory)/'test'
    cpp.write_text(prelude+charger+strip_includes(header)+spmi+strip_includes(power_header)+adc_header+event_defines+adc_model+shim+strip_includes(backend)+strip_includes(power)+''.join(function(n) for n in names)+checks)
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-O1','-g',
        '-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),check=True,timeout=30)
