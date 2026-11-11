#!/usr/bin/env python3
"""Run the actual F1a service/client and compare DCS MMIO traces to legacy C.
Only OS scheduling/MMIO primitives are fixtures, not a model of the driver.
Usage: test-mh2lm-display-service.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
from mh2lm_dsi_fixtures import DSI_MEMORY_FIXTURE

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
arm = root/'iokit/Kernel/arm'
header = (arm/'SweetDisplayBackend.h').read_text()
header = header.replace('#include "SweetDSICommand.h"', (arm/'SweetDSICommand.h').read_text())
source = (arm/'SweetMDSS.cpp').read_text()
legacy = (root/'bsd/dev/sweet_display_probe.c').read_text()
source = source.replace('#include "SweetMDSSIRQ.inc"', (arm/'SweetMDSSIRQ.inc').read_text())
source = source.replace('#include "SweetMDSSScanout.inc"', (arm/'SweetMDSSScanout.inc').read_text())
source = source.replace('#include "SweetMDSSDSI.inc"', (arm/'SweetMDSSDSI.inc').read_text())
if (arm/'SweetMDSSResources.inc').exists():
    source = source.replace('#include "SweetMDSSResources.inc"', (arm/'SweetMDSSResources.inc').read_text())
if (arm/'SweetMDSSNoC.inc').exists():
    source = source.replace('#include "SweetMDSSNoC.inc"', (arm/'SweetMDSSNoC.inc').read_text())
if (arm/'SweetMDSSHost.inc').exists():
    source = source.replace('#include "SweetMDSSHost.inc"', (arm/'SweetMDSSHost.inc').read_text())
if (arm/'SweetMDSSCore.inc').exists():
    source = source.replace('#include "SweetMDSSCore.inc"', (arm/'SweetMDSSCore.inc').read_text())
if (arm/'SweetMDSSClock.inc').exists():
    source = source.replace('#include "SweetMDSSClock.inc"', (arm/'SweetMDSSClock.inc').read_text())
    source = source.replace('#include "SweetDisplayClock.h"', (arm/'SweetDisplayClock.h').read_text())
source = source.replace('__asm__ volatile("dsb oshst" ::: "memory");', '(void)0;')
fb = (arm/'SweetFramebuffer.cpp').read_text()
backlight = fb[fb.index('#define SWEET_BL_MAX'):fb.index('#endif /* ARM64_BOARD_CONFIG_MH2LM */', fb.index('#define SWEET_BL_MAX'))]
backlight = backlight.replace('class AppleARMBacklight :', 'struct AppleARMBacklight :')


def function(text, name):
    m = re.search(r'(?:static )?(?:uint32_t|void|int)\n(?:SweetMDSS::)?'+name+r'\(', text)
    assert m, name
    start, brace = m.start(), text.index('{', m.end())
    end, depth = brace+1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


# CPU barriers and actual MMIO are the only replaced engine functions.
for name, body in [('dp_rd', 'return readReg(base,off);'),
                   ('dp_wr', 'writeReg(base,off,v);'),
                   ('dp_wr64', 'writeReg(base,off,(uint32_t)value);writeReg(base,off+4,(uint32_t)(value>>32));')]:
    original = function(source, name)
    source = source.replace(original, original[:original.index('{')+1]+body+'}')
source = source.replace('class SweetMDSS :', 'struct SweetMDSS :')
rpmh_header = (arm/'SweetRPMh.h').read_text().replace('class SweetRPMh :', 'struct SweetRPMh :').replace('class SweetRPMhClient :', 'struct SweetRPMhClient :') if (arm/'SweetRPMh.h').exists() else ''
rpmh_source = (arm/'SweetRPMh.cpp').read_text() if rpmh_header else ''
# Same actual RPMh code in all display regression fixtures. Only MMIO/OS
# primitives are substituted; its new resource path has dedicated cases too.
rpmh_source = re.sub(r'uint32_t SweetRPMh::read\([^\n]*\)\n\{.*?\n\}', 'uint32_t SweetRPMh::read(vm_offset_t b,uint32_t o) { return readReg(b,o); }', rpmh_source, flags=re.S)
rpmh_source = re.sub(r'void SweetRPMh::write\([^\n]*\)\n\{.*?\n\}', 'void SweetRPMh::write(vm_offset_t b,uint32_t o,uint32_t v) { writeReg(b,o,v); }', rpmh_source, flags=re.S)
source = re.sub(r'^#include.*\n', '', rpmh_header+rpmh_source, flags=re.M)+source
header = header.replace('class SweetDisplayClient :', 'struct SweetDisplayClient :')
header = re.sub(r'^#include.*\n', '', header, flags=re.M)
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
# Genuine old DCS body as an independent trace oracle. C++ only hoists the
# initialized locals across goto, with no operation/read/write reordering.
old = function(legacy, 'dp_dcs_write')
old = old.replace('dp_dcs_write(', 'legacyDCS(', 1)
old = old.replace('dp_rd(', 'readReg(').replace('dp_wr(', 'writeReg(')
old = old.replace('dp_poll(', 'SweetMDSS::dp_poll(')
old = old.replace('int rc = 0;', 'int rc = 0; uint32_t dma=0,length=0,trigger=0,irq=0,status=0,ack=0; const uint32_t statuses=0x51115501u;')
old = old.replace('uint32_t dma = dp_rd', 'dma = dp_rd')
old = old.replace('uint32_t dma = readReg(dsi, 0x03c), length = readReg(dsi, 0x04c), trigger = readReg(dsi, 0x084);',
                  'dma=readReg(dsi,0x03c); length=readReg(dsi,0x04c); trigger=readReg(dsi,0x084);')
old = old.replace('const uint32_t statuses = 0x51115501u;', '')
old = old.replace('uint32_t irq = readReg(dsi, 0x110), status = readReg(dsi, 0x008), ack = readReg(dsi, 0x068);',
                  'irq=readReg(dsi,0x110); status=readReg(dsi,0x008); ack=readReg(dsi,0x068);')
# Failure logging rereads are legacy too; source still executes their args.
prelude = r'''
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <algorithm>
#define ARM64_BOARD_CONFIG_MH2LM 1
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareAbstractStructors(x)
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define OSDefineMetaClassAndAbstractStructors(x,y)
#define OSTypeAlloc(x) new x()
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
#define OSSafeReleaseNULL(x) do {if (x) {(x)->release();(x)=nullptr;}} while(0)
using vm_offset_t=uintptr_t;
using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnBadArgument=2,kIOReturnIOError=3,kIOReturnUnsupported=4,kIOReturnBusy=5,kIOReturnNoMemory=6,kIOReturnAborted=7,kIOReturnNoResources=8,kIOReturnTimeout=9,kIOReturnNotFound=10,kIOReturnNotPermitted=11;
using IOOptionBits=unsigned;using IOInterruptState=unsigned;
constexpr unsigned SWEET_DEV_FB_H=2340,SWEET_DEV_FB_W=1080,SWEET_DEV_FB_PA=0x9d200000,SWEET_DEV_FB_STRIDE=4320;
static unsigned live=0,liveMaps=0,rawBlocks=0;
static void*IOMallocData(size_t n){void*p=malloc(n);if(p)++rawBlocks;return p;}
static void IOFreeData(void*p,size_t){assert(rawBlocks);--rawBlocks;free(p);}
struct OSObject {
 std::atomic<unsigned> refs{1};
 OSObject(){++live;} virtual ~OSObject(){--live;}
 virtual bool init(){return true;} virtual void free(){}
 void retain(){assert(refs.fetch_add(1));}
 void release(){if(refs.fetch_sub(1)==1){free();delete this;}}
};
using IOLock=std::mutex;
static IOLock *IOLockAlloc(){return new IOLock;}
static void IOLockFree(IOLock*p){delete p;}
static void IOLockLock(IOLock*p){p->lock();}
static void IOLockUnlock(IOLock*p){p->unlock();}
struct IOWorkLoop;
struct IOEvent : OSObject {IOWorkLoop *loop=nullptr; bool enabled=true; OSObject *owner=nullptr;
 void enable(){enabled=true;} void disable(){enabled=false;}};
static unsigned eventAdds=0,failEventAdd=0,timerCreates=0,failTimerCreate=0;
struct IOWorkLoop : OSObject {
 std::recursive_mutex gate;
 static IOWorkLoop *workLoop(){return new IOWorkLoop;}
 int addEventSource(IOEvent *e){if(++eventAdds==failEventAdd)return kIOReturnIOError;e->loop=this;e->retain();return 0;}
 void removeEventSource(IOEvent *e){std::lock_guard<std::recursive_mutex> l(gate);e->loop=nullptr;e->release();}
};
struct IOCommandGate : IOEvent {
 using Action=IOReturn(*)(OSObject*,void*,void*,void*,void*);
 static IOCommandGate *commandGate(OSObject *o){auto*p=new IOCommandGate;p->owner=o;return p;}
 IOReturn runAction(Action a,void*x=nullptr,void*y=nullptr){std::lock_guard<std::recursive_mutex> l(loop->gate);return a(owner,x,y,nullptr,nullptr);}
};
static std::vector<struct IOInterruptEventSource*> queued;
struct IOInterruptEventSource : IOEvent {
 using Action=void(*)(OSObject*,IOInterruptEventSource*,int);Action action;
 static IOInterruptEventSource *interruptEventSource(OSObject *o,Action a){auto*p=new IOInterruptEventSource;p->owner=o;p->action=a;return p;}
 void interruptOccurred(void*,void*,int){if(std::find(queued.begin(),queued.end(),this)==queued.end())queued.push_back(this);}
};
static void pump(){auto list=queued;queued.clear();for(auto*e:list)if(e->loop&&e->enabled){std::lock_guard<std::recursive_mutex> l(e->loop->gate);e->action(e->owner,e,1);}}
struct IOTimerEventSource : IOEvent {
 using Action=void(*)(OSObject*,IOTimerEventSource*);Action action;unsigned timeout=0;
 static IOTimerEventSource*timerEventSource(OSObject*o,Action a){if(++timerCreates==failTimerCreate)return nullptr;auto*p=new IOTimerEventSource;p->owner=o;p->action=a;return p;}
 void setTimeoutMS(unsigned t){timeout=t;} void setTimeoutUS(unsigned t){timeout=t;} void cancelTimeout(){timeout=0;}
};
static constexpr uintptr_t bases[16]={0x0010b000,0x0af00000,0x0ae05000,0x0ae02000,0x0ae6b800,0x0ae6c000,0x0ae94000,0x15000000,0x15082000,0x0ae00000,0x0ae6e800,0x17a00000,0x0ae94400,0x0af20000,0x0af21c00,0x0af30000};
static constexpr size_t lengths[16]={0x1000,0x3000,0x1000,0x1000,0x800,0x800,0x400,0x2000,0x1000,0x2000,0x200,0x10000,0x760,0x1c00,0x3000,0x3fd4};
static int resourceMode=0;
static unsigned badMap=99,missingMap=99,shortMap=99;
static std::vector<unsigned> mapCalls;
struct IOMemoryMap : OSObject {
 unsigned index;uintptr_t customPA=0;size_t customSize=0;
 IOMemoryMap(unsigned i):index(i){++liveMaps;}~IOMemoryMap(){--liveMaps;}
 uintptr_t getPhysicalAddress(){return (customPA?customPA:bases[index])+(index==badMap?4:0);}
 uintptr_t getVirtualAddress(){return customPA?customPA:bases[index];}
 size_t getLength(){return (customPA?customSize:(index==1&&resourceMode?0x20000:lengths[index]))-(index==shortMap?1:0);}
};
struct OSNumber : OSObject {
 uint64_t value;static OSNumber*withNumber(unsigned long long n,unsigned){auto*p=new OSNumber;p->value=n;return p;}
 uint64_t unsigned64BitValue(){return value;}
};
struct OSDictionary : OSObject {
 std::map<std::string,OSObject*> values;
 static OSDictionary*withCapacity(unsigned){return new OSDictionary;}
 void setObject(const char*k,OSObject*o){if(values.count(k))values[k]->release();o->retain();values[k]=o;}
 OSObject*getObject(const char*k){return values.count(k)?values[k]:nullptr;}
 unsigned getCount(){return values.size();}
 void free()override{for(auto &v:values)v.second->release();}
};
struct IONotifier;
static std::vector<IONotifier*> subscriptions;
static constexpr const char *gIOTerminatedNotification="term",*gIOFirstMatchNotification="match";
struct IOService : OSObject {
 bool inactive=false,rpmhNub=false;std::map<std::string,OSObject*>properties;
 using Notify=bool(*)(void*,void*,IOService*,IONotifier*);
 virtual bool start(IOService*){return true;}virtual void stop(IOService*){}
 virtual bool willTerminate(IOService*,IOOptionBits){inactive=true;return true;}
 virtual IOReturn setProperties(OSObject*){return 0;}
 virtual IOWorkLoop*getWorkLoop()const{return nullptr;}
 OSObject*getProperty(const char*k){return properties.count(k)?properties[k]:nullptr;}
 bool isInactive(){return inactive;}
 int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){mapCalls.push_back(i);assert(i<16);if(i==missingMap)return nullptr;auto*m=new IOMemoryMap(i);if(rpmhNub){assert(i<3);static const uintptr_t pa[]={0x18220000,0x18220d00,0x0c3f000c};static const size_t n[]={0x100,0x3000,8};m->customPA=pa[i];m->customSize=n[i];}return m;}
 void setProperty(const char*,unsigned long long,unsigned){}void setProperty(const char*,bool){}
 void setProperty(const char*,const char*){} void setProperty(const char*,OSObject*){}
 void registerService(){}
 static OSDictionary*serviceMatching(const char*){return new OSDictionary;}
 static IONotifier*addMatchingNotification(const char*,OSDictionary*,Notify,void*);
};
struct IONotifier : OSObject {
 const char*kind;OSDictionary*dict;IOService::Notify cb;void*target;
 void remove(){subscriptions.erase(std::find(subscriptions.begin(),subscriptions.end(),this));dict->release();release();}
};
IONotifier*IOService::addMatchingNotification(const char*k,OSDictionary*d,Notify cb,void*t){
 auto*n=new IONotifier;n->kind=k;n->dict=d;d->retain();n->cb=cb;n->target=t;subscriptions.push_back(n);return n;
}
static void emit(const char*k,IOService*p){for(auto*n:subscriptions)if(n->kind==k)n->cb(n->target,nullptr,p,n);}
static int enabled=1,probeEnabled=1,powerEnabled=0,irqProbeMode=0,irqMode=0,ownMode=0;
static bool PE_parse_boot_argn(const char*k,void*v,size_t){
 if(!strcmp(k,"sweetdisplayresources")){*(int*)v=resourceMode;return true;}
 if(!strcmp(k,"sweetfbown")){*(int*)v=ownMode;return true;}
 if(!strcmp(k,"sweetmdss")){*(int*)v=enabled;return true;}
 if(!strcmp(k,"sweetdispprobe")){*(int*)v=probeEnabled;return true;}
 if(!strcmp(k,"sweetmdssirq")){*(int*)v=irqMode;return true;}
 if(!strcmp(k,"sweetmdssirqprobe")){*(int*)v=irqProbeMode;return true;}
 if(!strcmp(k,"sweetfbpower")){*(int*)v=powerEnabled;return true;}return false;
}
static std::vector<std::string> messages;
static void quiet(const char*fmt,...){char text[2048];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);messages.push_back(text);}
#define IOLog quiet
#define SWEET_LOG_INFO quiet
#define SWEET_LOG_WARN quiet
#define SWEET_LOG_ERROR quiet
static bool sweet_log_level_enabled(unsigned){return true;}
static uint64_t fakeTicks=42;
static uint64_t mach_absolute_time(){return fakeTicks;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t*n){*n=t;}
static void delay(unsigned){}
static constexpr uint64_t NSEC_PER_MSEC=1000000;
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t*p){*p=n;}
using IOSimpleLock=IOLock;
static IOSimpleLock*IOSimpleLockAlloc(){return IOLockAlloc();}
static void IOSimpleLockFree(IOSimpleLock*p){IOLockFree(p);}
static void IOSimpleLockLock(IOSimpleLock*p){p->lock();}
static void IOSimpleLockUnlock(IOSimpleLock*p){p->unlock();}
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock*p){p->lock();return 0;}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock*p,IOInterruptState){p->unlock();}
static int legacyWrites;
static int sweet_display_brightness_set(uint32_t){++legacyWrites;return 0;}

static std::map<uintptr_t,uint32_t> regs;
struct Op {bool write;uintptr_t addr;uint32_t value;bool operator==(const Op&o)const{return write==o.write&&addr==o.addr&&value==o.value;}};
static std::vector<Op> trace;
static bool dmaFired=false,dmaDone=true,lateError=false,pauseRead=false,enteredRead=false,releaseRead=false;
static uintptr_t pauseAddress=0;
static unsigned failNextDMAs=0;
static bool activeDMAFails=false;
static std::mutex pauseLock;
static std::condition_variable pauseCV;
static uint32_t readReg(uintptr_t b,uint32_t o){
 if(pauseRead&&((b==bases[6]&&o==4)||(pauseAddress&&b+o==pauseAddress))){std::unique_lock<std::mutex> l(pauseLock);enteredRead=true;pauseCV.notify_all();pauseCV.wait(l,[]{return releaseRead;});}
 uint32_t v=regs[b+o];if(b==bases[6]&&o==0x110&&dmaFired)v=(dmaDone&&!activeDMAFails?1:0)|(lateError?(1u<<24):0);
 trace.push_back({false,b+o,v});return v;
}
static bool irqMMIO=false,injectLateFrame=false,dropSourceWrite=false,keepPPDone=false;
static uint32_t rpmhDrop=~0u;static bool rpmhKeepIRQ=false;
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){trace.push_back({true,b+o,v});
 if(b==0x18220d00||b==0x0af21c00){if(o==rpmhDrop)return;if(o==8){if(!rpmhKeepIRQ)regs[b+4]&=~v;return;}}
 if(irqMMIO&&b==0x0ae00000&&o==0x1018){if(!keepPPDone)regs[b+0x1014]&=~v;return;}
 if(dropSourceWrite&&b==bases[2]&&o==0x14)return;
 if(irqMMIO&&b==0x17a00000&&o==0x18c){regs[b+0x10c]&=~v;return;}
 if(irqMMIO&&b==0x17a00000&&o==0x10c){regs[b+o]|=v;return;}
 if(irqMMIO&&b==0x17a00000&&o==0x28c){regs[b+0x20c]&=~v;return;}
 if(irqMMIO&&b==0x17a00000&&o==0x20c){regs[b+o]|=v;return;}
 if(irqMMIO&&b==0x0ae6e800&&o==8){regs[b+4]&=~v;if(injectLateFrame){regs[b+4]|=4;injectLateFrame=false;}return;}
 regs[b+o]=v;if(b==bases[6]&&o==0x90){dmaFired=true;activeDMAFails=failNextDMAs>0;if(failNextDMAs)--failNextDMAs;}}
static void resetRegs(){trace.clear();regs.clear();dmaFired=false;dmaDone=true;lateError=false;
 failNextDMAs=0;activeDMAFails=false;
 regs[bases[0]+0xc]=1;regs[bases[1]+0x2080]=0;regs[bases[6]+4]=5;
 regs[bases[4]+0x288]=0x12345678;regs[bases[4]+0x2b4]=0x3344;
 regs[bases[6]+0x3c]=0x9;regs[bases[6]+0x4c]=0x18;regs[bases[6]+0x84]=0x32;
}
'''
pool_fixture = r'''
struct IOMemoryDescriptor : OSObject {
 uintptr_t pa;size_t length;
 static IOMemoryDescriptor*withPhysicalAddress(uintptr_t p,size_t n,unsigned){auto*d=new IOMemoryDescriptor;d->pa=p;d->length=n;return d;}
 IOMemoryMap*map(){auto*m=new IOMemoryMap(3);m->customPA=pa;m->customSize=length;return m;}
};
constexpr unsigned kIODirectionInOut=3;
struct SweetScanoutPool : OSObject {
 bool pinned=false;uint64_t pa=0x10000000;
 unsigned width(){return 1125;}unsigned height(){return 2436;}unsigned stride(){return 4608;}
 size_t bytes(){return 11225088;}void*address(unsigned){return this;}
 uint64_t physical(unsigned n){return pa+n*0x4000000ULL;}
 bool hardwarePinned(){return pinned;}void pinHardware(){assert(!pinned);pinned=true;retain();}
};
'''
tests = r'''
static unsigned changes=0;
static void changed(OSObject*){++changes;}
static SweetMDSS*make(IOService*p){auto*s=new SweetMDSS();assert(s->start(p));return s;}
static void configure(unsigned mode){resetRegs();
 if(mode==1)regs[bases[6]+4]=0;
 if(mode==2)regs[bases[6]+8]=6;
 if(mode==3)regs[bases[4]+0x2b0]=42;
 if(mode==4)dmaDone=false;
 if(mode==5)lateError=true;
}
int main(){
 auto*nub=new IOService;
 const uint32_t words[2]={0xc0390003,0xff563451};
 auto*s=make(nub);
 // Match old and new complete read/write traces, including every restore path.
 for(unsigned mode=0;mode<6;++mode){
  configure(mode);dp_logged=0;int oldrc=legacyDCS(bases[4],bases[6],words,2);auto expected=trace;
  configure(mode);s->dp_logged=0;int rc=s->dp_dcs_write(bases[4],bases[6],words,2);
  assert(rc==oldrc&&trace==expected);
  assert(regs[bases[4]+0x288]==0x12345678&&regs[bases[4]+0x2b4]==0x3344);
  assert(regs[bases[6]+0x3c]==9&&regs[bases[6]+0x4c]==0x18&&regs[bases[6]+0x84]==0x32);
 }
 resetRegs();assert(s->setBrightness(0)==0);
 assert(std::find(trace.begin(),trace.end(),Op{true,bases[6]+0x17c,0xff010051})!=trace.end());
 resetRegs();assert(s->setBrightness(0xffff)==0);
 assert(std::find(trace.begin(),trace.end(),Op{true,bases[6]+0x17c,0xffff0f51})!=trace.end());
 assert(s->dcsWrite(nullptr,2)==kIOReturnBadArgument&&s->dcsWrite(words,17)==kIOReturnBadArgument);
 // Disabled GCC may be read, but never its gated DISPCC/MDSS descendants.
 resetRegs();regs[bases[0]+0xc]=0;assert(s->brightnessReady()==kIOReturnNotReady);
 assert(trace.size()==1&&trace[0].addr==bases[0]+0xc);
 assert(s->setBrightness(25)==kIOReturnNotReady);assert(trace.size()==2);
 resetRegs();regs[bases[1]+0x2080]=1u<<31;assert(s->setBrightness(25)==kIOReturnNotReady);
 assert(trace.size()==2);
 // Three engine failures disable writing; success clears consecutive failures.
 for(unsigned i=0;i<2;++i){configure(1);assert(s->setBrightness(25)==kIOReturnIOError);}
 resetRegs();assert(s->setBrightness(25)==0&&s->dp_failed==0);
 for(unsigned i=0;i<3;++i){configure(1);assert(s->setBrightness(25)==kIOReturnIOError);}
 resetRegs();assert(s->setBrightness(25)==kIOReturnNotReady&&trace.empty());
 s->stop(nub);s->release();assert(liveMaps==0);
 // Bad translations and required missing windows unwind all prior mappings.
 for(unsigned i=0;i<9;++i){badMap=i;s=new SweetMDSS();assert(!s->start(nub));s->release();assert(!liveMaps);}badMap=99;
 for(unsigned i=0;i<7;++i){missingMap=i;s=new SweetMDSS();assert(!s->start(nub));s->release();assert(!liveMaps);}missingMap=99;
 missingMap=8;s=make(nub);s->stop(nub);s->release();missingMap=99;
 enabled=0;s=new SweetMDSS();assert(!s->start(nub));s->release();assert(!liveMaps);enabled=1;
 // Per-consumer matching, deferred change event, retained in-flight reference.
 s=make(nub);auto*loop=IOWorkLoop::workLoop();
 auto*c=SweetDisplayClient::withOwner(nub,loop,changed);assert(c&&c->watch());pump();changes=0;
 emit(gIOFirstMatchNotification,s);assert(changes==0);pump();assert(changes==1);
 auto*retained=c->copyBackend();assert(retained==s);
 s->inactive=true;emit(gIOTerminatedNotification,s);assert(!c->copyBackend());pump();assert(changes==2);
 resetRegs();assert(retained->setBrightness(2)==kIOReturnNotReady&&trace.empty());
 s->stop(nub);s->release();assert(liveMaps==9);retained->release();assert(!liveMaps);
 c->stop();c->release();loop->release();assert(subscriptions.empty());queued.clear();
 // stop must serialize with an active command before closing admission.
 s=make(nub);resetRegs();pauseRead=true;std::atomic<bool> stopped=false;
 std::thread write([&]{assert(s->setBrightness(10)==0);});
 {std::unique_lock<std::mutex> l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 std::thread stop([&]{s->stop(nub);stopped=true;});
 assert(!stopped.load());
 {std::lock_guard<std::mutex> l(pauseLock);releaseRead=true;}pauseCV.notify_all();
 write.join();stop.join();pauseRead=false;assert(stopped);
 size_t end=trace.size();assert(s->setBrightness(10)==kIOReturnNotReady&&trace.size()==end);
 assert(liveMaps==9);s->release();assert(!liveMaps);

 // Real backlight: a request before match is deferred and awakened by an event.
 s=make(nub);auto*bl=new AppleARMBacklight();assert(bl->start(nub));
 auto*d=OSDictionary::withCapacity(1);auto*n=OSNumber::withNumber(17,32);d->setObject("brightness",n);n->release();
 assert(bl->setProperties(d)==0);d->release();
 resetRegs();AppleARMBacklight::step(bl,bl->fTimer);
 assert(bl->fAwaitingBackend&&trace.empty()&&legacyWrites==0);
 emit(gIOFirstMatchNotification,s);pump();assert(trace.empty());
 AppleARMBacklight::step(bl,bl->fTimer);assert(bl->fLevel==17&&!bl->fAwaitingBackend);
 assert(std::find(trace.begin(),trace.end(),Op{true,bases[6]+0x17c,0xff110051})!=trace.end());
 s->inactive=true;emit(gIOTerminatedNotification,s);pump();assert(!bl->fWasBound);
 bl->stop(nub);assert(bl->setProperties(nub)==kIOReturnBadArgument);
 d=OSDictionary::withCapacity(1);n=OSNumber::withNumber(5,32);d->setObject("brightness",n);n->release();
 assert(bl->setProperties(d)==kIOReturnNotReady);d->release();
 bl->release();s->stop(nub);s->release();assert(subscriptions.empty());queued.clear();
 // Legacy default still uses the C entry point exclusively.
 enabled=0;bl=new AppleARMBacklight();assert(bl->start(nub));
 d=OSDictionary::withCapacity(1);n=OSNumber::withNumber(5,32);d->setObject("brightness",n);n->release();
 assert(bl->setProperties(d)==0);d->release();AppleARMBacklight::step(bl,bl->fTimer);
 assert(legacyWrites==1&&bl->fLevel==5);bl->stop(nub);bl->release();
 enabled=1;s=make(nub);resetRegs();
 assert(s->setPanelPower(false)==kIOReturnUnsupported&&trace.empty());s->stop(nub);s->release();
 // F2 short packets are independently expected from Qualcomm's packet format.
 powerEnabled=1;s=make(nub);resetRegs();
 auto fifo=[](){std::vector<uint32_t> result;for(auto o:trace)if(o.write&&o.addr==bases[6]+0x17c)result.push_back(o.value);return result;};
 assert(s->setPanelPower(true)==0&&trace.empty()); // splash ON; idempotent
 regs[bases[4]+0x2b0]=42; // frozen TE line must not prevent wake/control
 assert(s->setPanelPower(false)==0&&s->fPanelState==SweetMDSS::PanelOff);
 assert(fifo()==(std::vector<uint32_t>{0x80050028,0}));
 assert(std::find(trace.begin(),trace.end(),Op{true,bases[6]+0x4c,4})!=trace.end());
 assert(std::find(trace.begin(),trace.end(),Op{true,bases[6]+0x3c,0x14000009})!=trace.end()); // embedded + LP
 size_t endPower=trace.size();assert(s->setPanelPower(false)==0&&trace.size()==endPower);
 trace.clear();assert(s->setBrightness(31)==0); // OFF brightness uses idle, not missing TE
 trace.clear();assert(s->setPanelPower(true)==0&&s->fPanelState==SweetMDSS::PanelOn);
 assert(fifo()==(std::vector<uint32_t>{0x80050029,0}));
 assert(regs[bases[4]+0x288]==0x12345678&&regs[bases[4]+0x2b4]==0x3344);
 assert(regs[bases[6]+0x3c]==9&&regs[bases[6]+0x4c]==0x18&&regs[bases[6]+0x84]==0x32);
 // Off timeout can have reached DDIC: one ON compensation, then legacy fallback.
 resetRegs();failNextDMAs=1;assert(s->setPanelPower(false)==kIOReturnIOError);
 assert(fifo()==(std::vector<uint32_t>{0x80050028,0,0x80050029,0}));
 assert(s->fPanelFallback&&s->fPanelState==SweetMDSS::PanelOn);
 endPower=trace.size();assert(s->setPanelPower(false)==kIOReturnUnsupported&&trace.size()==endPower);
 assert(s->setPanelPower(true)==0&&trace.size()==endPower);
 s->stop(nub);s->release();
 // Recovery also fails: UNKNOWN, never pretend ON. A later ON still retries
 // even if brightness has exhausted its independent failure budget.
 s=make(nub);resetRegs();failNextDMAs=2;assert(s->setPanelPower(false)==kIOReturnIOError);
 assert(s->fPanelState==SweetMDSS::PanelUnknown&&s->fPanelFallback);
 resetRegs();s->dp_failed=3;assert(s->setPanelPower(true)==0&&s->fPanelState==SweetMDSS::PanelOn);
 assert(fifo()==(std::vector<uint32_t>{0x80050029,0}));s->dp_failed=0;
 s->stop(nub);s->release();
 // ON failure is not an idempotent ON; repeat must reissue the command.
 s=make(nub);resetRegs();assert(s->setPanelPower(false)==0);
 resetRegs();failNextDMAs=1;assert(s->setPanelPower(true)==kIOReturnIOError&&s->fPanelState==SweetMDSS::PanelUnknown);
 resetRegs();assert(s->setPanelPower(true)==0&&fifo()==(std::vector<uint32_t>{0x80050029,0}));
 s->stop(nub);s->release();
 // All new panel MMIO still obeys provider clock and stop admission.
 s=make(nub);resetRegs();regs[bases[0]+0xc]=0;
 assert(s->setPanelPower(false)==kIOReturnNotReady);
 for(auto o:trace)assert(!o.write&&o.addr==bases[0]+0xc);
 s->stop(nub);s->release();
 // A second OFF while the first is OFF_PENDING waits for the same gate;
 // after completion it is idempotent and cannot enqueue a second DMA.
 s=make(nub);resetRegs();pauseRead=true;enteredRead=false;releaseRead=false;
 std::thread firstOff([&]{assert(s->setPanelPower(false)==0);});
 {std::unique_lock<std::mutex> l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 assert(s->fPanelState==SweetMDSS::PanelOffPending);
 std::atomic<bool> secondStarted=false;std::atomic<int> secondResult=-1;
 std::thread secondOff([&]{secondStarted=true;secondResult=s->setPanelPower(false);});
 while(!secondStarted.load())std::this_thread::yield();assert(secondResult.load()==-1);
 {std::lock_guard<std::mutex> l(pauseLock);releaseRead=true;}pauseCV.notify_all();
 firstOff.join();secondOff.join();pauseRead=false;
 assert(secondResult.load()==0&&fifo()==(std::vector<uint32_t>{0x80050028,0}));
 s->stop(nub);s->release();
 s=make(nub);resetRegs();pauseRead=true;enteredRead=false;releaseRead=false;stopped=false;
 std::thread power([&]{assert(s->setPanelPower(false)==0);});
 {std::unique_lock<std::mutex> l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 std::thread stopPower([&]{s->stop(nub);stopped=true;});assert(!stopped.load());
 {std::lock_guard<std::mutex> l(pauseLock);releaseRead=true;}pauseCV.notify_all();
 power.join();stopPower.join();pauseRead=false;endPower=trace.size();
 assert(s->setPanelPower(true)==kIOReturnNotReady&&trace.size()==endPower);s->release();

 // F3 defaults do not map/read any new resource; old ADTs remain valid.
 powerEnabled=0;probeEnabled=0;mapCalls.clear();resetRegs();s=make(nub);
 assert(mapCalls==(std::vector<unsigned>{0,1,2,3,4,5,6,7,8}));
 assert(!s->fIrqProbe&&trace.empty());s->stop(nub);s->release();
 auto fire=[](SweetMDSS*p){std::lock_guard<std::recursive_mutex> l(p->fLoop->gate);
   p->fIrqProbe->timeout=0;SweetMDSS::irqProbeAction(p,p->fIrqProbe);};
 auto hasMessage=[](const char*part){for(auto&m:messages)if(m.find(part)!=std::string::npos)return true;return false;};
 // Only documented read-only GIC registers allowed, never a write.
 const std::vector<uintptr_t> gicReads={0x17a00000,0x17a00004,0x17a00008,0x17a0ffe8,
   0x17a0008c,0x17a00d0c,0x17a0010c,0x17a0020c,0x17a0030c,0x17a00c1c,
   0x17a00470,0x17a06398,0x17a0639c};
 irqProbeMode=1;mapCalls.clear();resetRegs();s=make(nub);
 assert(mapCalls==(std::vector<unsigned>{0,1,2,3,4,5,6,7,8,11}));
 assert(trace.empty()&&s->fIrqProbe->timeout==30000&&s->fProbe->timeout==0);
 // Nontrivial values catch word/lane/INTID mistakes independent of constants.
 regs[0x17a0010c]=1u<<19;regs[0x17a0020c]=1u<<19;regs[0x17a0030c]=1u<<20;
 regs[0x17a00c1c]=2u<<6;regs[0x17a00470]=0xa5123456;regs[0x17a06398]=0x80000001;regs[0x17a0639c]=2;
 messages.clear();fire(s);
 assert(hasMessage("en=1 pending=1 active=0 cfg=2 priority=0xa5 route=0x2:80000001"));
 assert(s->fIrqProbePass==1&&s->fIrqProbe->timeout==1000);
 assert(trace.size()==gicReads.size());
 for(auto o:trace)assert(!o.write&&std::find(gicReads.begin(),gicReads.end(),o.addr)!=gicReads.end());
 fire(s);fire(s);assert(s->fIrqProbePass==3&&s->fIrqProbe->timeout==0);
 end=trace.size();fire(s);assert(trace.size()==end);s->stop(nub);s->release();assert(!liveMaps);
 // Full snapshot includes both TE blocks. Sticky RD_PTR is logged, never ACKed.
 irqProbeMode=2;resetRegs();s=make(nub);assert(trace.empty()&&liveMaps==12);
 regs[0x0ae6e800]=4;regs[0x0ae6e804]=0x14;regs[0x0ae6e900]=8;regs[0x0ae6e904]=0x18;
 messages.clear();auto before=regs;fire(s);
 assert(hasMessage("tear1-en=0x4 tear1-status=0x14 tear2-en=0x8 tear2-status=0x18"));
 assert(hasMessage("rdptr-enabled=1 rdptr-latched=1"));
 for(auto o:trace)assert(!o.write);
 for(auto pair:before)assert(regs[pair.first]==pair.second);
 assert(std::none_of(trace.begin(),trace.end(),[](auto o){return o.addr==0x0ae6e808||o.addr==0x0ae6e908||o.addr==0x0ae01018;}));
 s->stop(nub);assert(s->fIrqProbe->timeout==0);end=trace.size();fire(s);assert(trace.size()==end);
 s->release();assert(!liveMaps);
 // Missing, mistranslated or too-short diagnostic resources only disable the probe.
 for(unsigned kind=0;kind<3;++kind)for(unsigned i=9;i<12;++i){
  if(kind==0)missingMap=i;if(kind==1)badMap=i;if(kind==2)shortMap=i;
  resetRegs();s=make(nub);assert(!s->fIrqProbeMode&&!s->fIrqProbe&&liveMaps==9);
  assert(trace.empty());assert(s->brightnessReady()==0); // display remains usable
  s->stop(nub);s->release();assert(!liveMaps);missingMap=badMap=shortMap=99;
 }

 // Diagnostic event allocation/add failures also retain the display service.
 for(unsigned kind=0;kind<2;++kind){
  if(!kind)failTimerCreate=timerCreates+2;else failEventAdd=eventAdds+3;
  resetRegs();s=make(nub);assert(!s->fIrqProbe&&!s->fIrqProbeMode&&liveMaps==9&&trace.empty());
  assert(s->brightnessReady()==0);s->stop(nub);s->release();assert(!liveMaps);
  failTimerCreate=failEventAdd=0;
 }
 // stop waits for a running preflight before closing admission and removing its timer.
 resetRegs();s=make(nub);pauseRead=true;pauseAddress=0x17a00000;enteredRead=false;releaseRead=false;stopped=false;
 std::thread snapshot([&]{fire(s);});
 {std::unique_lock<std::mutex> l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 std::thread stopSnapshot([&]{s->stop(nub);stopped=true;});assert(!stopped.load());
 {std::lock_guard<std::mutex> l(pauseLock);releaseRead=true;}pauseCV.notify_all();
 snapshot.join();stopSnapshot.join();pauseRead=false;pauseAddress=0;
 end=trace.size();fire(s);assert(trace.size()==end&&s->fIrqProbe->timeout==0&&liveMaps==12);
 s->release();assert(!liveMaps);
 // Never read MDSS/TE with their AHB clock gates shut; GIC snapshot is independent.
 for(unsigned which=0;which<2;++which){resetRegs();s=make(nub);
  if(!which)regs[0x0010b00c]=0;else regs[0x0af02080]=1u<<31;
  messages.clear();fire(s);assert(hasMessage("MDSS clocks unavailable"));
  for(auto o:trace)assert(!o.write&&(o.addr<0x0ae00000||o.addr>=0x0ae70000));
  s->stop(nub);s->release();assert(!liveMaps);
 }
 // Inactive provider and queued post-stop callbacks cannot touch any register.
 resetRegs();s=make(nub);s->inactive=true;fire(s);assert(trace.empty());
 s->stop(nub);fire(s);assert(trace.empty());s->release();
 for(int mode:{-1,3}){irqProbeMode=mode;mapCalls.clear();s=make(nub);
  assert(mapCalls.size()==9&&!s->fIrqProbe);s->stop(nub);s->release();}

 irqProbeMode=0;irqMode=2;powerEnabled=1;probeEnabled=0;irqMMIO=true;
 auto irqRegs=[](){resetRegs();fakeTicks=1000000000;
  regs[0x17a00000]=0x12;regs[0x17a00004]=0x7b041d;regs[0x17a0ffe8]=0x3b;
  regs[0x17a00470]=0x80123456;regs[0x0ae00000]=0x50000001;
  regs[bases[4]+0x284]=1;regs[bases[4]+0x288]=0x180088;
  regs[bases[4]+0x2a4]=0x925;regs[bases[4]+0x2b4]=0x80000001;
  regs[0x0ae6e804]=0xf;
 };
 auto timer=[](SweetMDSS*p){std::lock_guard<std::recursive_mutex> l(p->fLoop->gate);
  p->fIrqTimer->timeout=0;SweetMDSS::irqTimerAction(p,p->fIrqTimer);};
 auto startProbe=[&](SweetMDSS*p){assert(p->fIrqTimer->timeout==35000);timer(p);
  assert(p->fIrqPhase==1&&p->fIrqState==SweetMDSS::IrqProbe);
  assert(regs[0x0ae6e800]==0); // GIC probe cannot enable a peripheral
  assert(sweet_mdss_irq(115));assert(p->fProbeHits==1);fakeTicks+=50000000;timer(p);
 };
 auto frame=[&](SweetMDSS*p,uint64_t delta){fakeTicks+=delta;
  regs[0x0ae6e804]|=4;regs[0x0ae00010]=1;
  auto beforeFrame=p->fFrameCount;assert(sweet_mdss_irq(115));
  assert(p->fFrameCount==beforeFrame&&!(regs[0x17a0010c]&(1u<<19))); // top half never drains or notifies
  pump();
 };
 assert(!sweet_mdss_irq(116));assert(sweet_mdss_irq(115));
 // GIC-only mode proves delivery and leaves TE disabled, with no interval buffer.
 irqMode=1;irqRegs();s=make(nub);startProbe(s);
 assert(s->fIrqPhase==2&&s->fIrqState==SweetMDSS::IrqIdle&&!s->fIntervals);
 assert(regs[0x0ae6e800]==0&&!(regs[0x17a0010c]&(1u<<19)));
 s->stop(nub);assert(regs[0x17a00470]==0x80123456);s->release();assert(!liveMaps);queued.clear();
 // Real hardware path; mailbox publications queue, never call consumer actions inline.
 irqMode=2;irqRegs();s=make(nub);loop=IOWorkLoop::workLoop();
 unsigned received=0;
 auto*event=IOInterruptEventSource::interruptEventSource(nub,[](OSObject*,IOInterruptEventSource*,int){++changes;});
 assert(loop->addEventSource(event)==0);auto*channel=SweetVsyncChannel::withEvent(event);assert(channel);
 assert(s->attachVsync(channel)==0);pump();SweetVsyncSample sample{};assert(channel->copySample(&sample)&&sample.state==SweetVsyncWaiting);
 auto*second=SweetVsyncChannel::withEvent(event);assert(s->attachVsync(second)==kIOReturnBusy);second->release();
 startProbe(s);pump();assert(s->fIrqState==SweetMDSS::IrqArmed&&regs[0x0ae6e800]==4);
 assert(regs[0x0ae6e804]==0xb); // only RD_PTR was cleared, the other ABL raw bits survive
 received=changes;frame(s,16666667);assert(s->fFrameCount==1&&changes==received);
 pump();assert(changes==received+1&&channel->copySample(&sample)&&sample.frame==1&&sample.state==SweetVsyncFrame&&sample.ticks==fakeTicks);
 assert(regs[0x17a0010c]&(1u<<19));
 // A new RD_PTR arriving after ACK is delivered by the next IRQ, not quarantined.
 injectLateFrame=true;frame(s,16666667);assert(s->fFrameCount==2&&s->fIrqState==SweetMDSS::IrqArmed&&(regs[0x0ae6e804]&4));
 frame(s,16666667);assert(s->fFrameCount==3);
 // Subscription while a newer IRQ awaits worker processing reports the last
 // completed frame timestamp, never the unprocessed next top-half timestamp.
 auto processedTicks=s->fFrameTicks;fakeTicks+=16666667;regs[0x0ae6e804]|=4;sweet_mdss_irq(115);
 s->detachVsync(channel);assert(s->attachVsync(channel)==0);
 assert(channel->copySample(&sample)&&sample.frame==3&&sample.ticks==processedTicks);
 pump();assert(s->fFrameCount==4);
 uint32_t mixed[]={50,1,100,2,90,3,80};mdss_interval_sort(mixed,7);
 for(unsigned i=1;i<7;++i)assert(mixed[i-1]<=mixed[i]);
 // 60 seconds worth of events, p50/p99 from actual top-half timestamps.
 for(unsigned i=0;i<3600;++i)frame(s,16666667);
 messages.clear();timer(s);assert(hasMessage("stats partial=0")&&hasMessage("p50-us=16666 p99-us=16666"));
 assert(s->fIrqState==SweetMDSS::IrqArmed);
 // OFF masks parent/local gate; queued frame before OFF cannot publish afterward.
 fakeTicks+=16666667;regs[0x0ae6e804]|=4;sweet_mdss_irq(115);
 assert(s->setPanelPower(false)==0);auto offFrames=s->fFrameCount;pump();
 assert(s->fFrameCount==offFrames&&s->fIrqState==SweetMDSS::IrqIdle&&regs[0x0ae6e800]==0&&s->fIrqTimer->timeout==0);
 assert(channel->copySample(&sample)&&sample.state==SweetVsyncOff);
 fakeTicks+=60000000000ULL;timer(s);assert(s->fIrqState==SweetMDSS::IrqIdle&&s->fFrameCount==offFrames);
 assert(s->setPanelPower(true)==0&&s->fIrqState==SweetMDSS::IrqArmed);
 frame(s,16666667);assert(s->fFrameCount==offFrames+1);
 // Detach synchronizes future publications; close synchronizes queued event ownership.
 s->detachVsync(channel);assert(!s->fVsyncChannel);channel->close();assert(!channel->copySample(&sample));
 loop->removeEventSource(event);event->release();channel->release();loop->release();queued.clear();
 // Missing RD_PTR watchdog is explicit and permanent until a new provider instance.
 fakeTicks+=300000000;messages.clear();timer(s);
 assert(s->fIrqState==SweetMDSS::IrqFailed&&hasMessage("watchdog >250ms"));
 assert(!(regs[0x17a0010c]&(1u<<19))&&regs[0x0ae6e800]==0);
 s->stop(nub);s->release();assert(!liveMaps&&!rawBlocks);queued.clear();
 // Software probe failure, non-target owner, and RWP timeout fail closed.
 for(unsigned kind=0;kind<3;++kind){irqRegs();s=make(nub);
  if(kind==1)regs[0x0ae01010]=1;
  if(kind==2)regs[0x17a00000]|=1u<<31;
  timer(s);if(!kind){fakeTicks+=50000000;timer(s);}
  assert(s->fIrqState==SweetMDSS::IrqFailed&&regs[0x0ae6e800]==0);
  s->stop(nub);s->release();assert(!liveMaps&&!rawBlocks);queued.clear();
 }
 // Storm cannot create unbounded work/notifications; disable only the owned pipeline.
 irqRegs();s=make(nub);startProbe(s);
 for(unsigned i=0;i<201;++i)frame(s,1000000);
 assert(s->fIrqState==SweetMDSS::IrqFailed&&s->fFrameCount==200&&regs[0x0ae6e800]==0);
 s->stop(nub);s->release();queued.clear();
 // willTerminate disconnects the physical callback before maps/events can die.
 irqRegs();s=make(nub);startProbe(s);assert(s->willTerminate(nub,0));
 trace.clear();assert(sweet_mdss_irq(115));assert(trace.empty());s->stop(nub);s->release();queued.clear();
 // A consumer watchdog requests shutdown by queuing, with no inline MMIO/gate call.
 irqRegs();s=make(nub);startProbe(s);trace.clear();s->requestVsyncFallback();assert(trace.empty());pump();assert(s->fIrqState==SweetMDSS::IrqFailed);
 s->stop(nub);s->release();queued.clear();

 // F4 inspection is an actual typed provider method: read-only under its
 // gate, no security-bit or source-address readback masquerading as a fence.
 irqRegs();s=make(nub);
 regs[bases[2]+0]=0x9240438;regs[bases[2]+0x14]=0x9d200000;regs[bases[2]+0x24]=4320;
 regs[bases[2]+0x30]=0x237ff;regs[bases[2]+0x34]=0x03020001;regs[bases[2]+0x70]=5;
 regs[bases[7]+0x20]=0x4c017e5e;regs[bases[7]+0x24]=0x60000038;
 regs[bases[7]+0x800+12]=0x84200800;regs[bases[7]+0xc00+12]=2;
 regs[bases[7]+0x1000+8]=0x1f000;regs[bases[7]+0x1808]=1;
 regs[bases[8]+0]=0;regs[bases[8]+0x58]=0x12;regs[bases[8]+0x60]=0xabcdef00;
 auto readOnly=[&]{for(auto op:trace)assert(!op.write);};
 SweetScanoutSnapshot snap{};trace.clear();assert(s->inspectScanout(&snap)==0);readOnly();
 assert(snap.address==0x9d200000&&snap.stride==4320&&snap.secure==5&&snap.format==0x237ff);
 assert(snap.knownSmmu&&snap.matches==1&&snap.streamIndex==3&&snap.smr==0x84200800&&snap.s2cr==2);
 assert(snap.cb2Observed&&snap.cbar==0x1f000&&snap.sctlr==0&&snap.cbFault==0x12&&snap.farLo==0xabcdef00);
 assert(s->inspectScanout(nullptr)==kIOReturnBadArgument);
 // Unknown IDs must not lead to speculative stream/context access.
 regs[bases[7]+0x20]=0;trace.clear();assert(s->inspectScanout(&snap)==0);readOnly();
 assert(!snap.knownSmmu&&!snap.cb2Observed&&!snap.matches&&!snap.cbFault);
 for(auto op:trace)assert(op.addr<bases[7]+0x800||op.addr>=bases[8]+0x1000);
 regs[bases[7]+0x20]=0x4c017e5e;
 // Ambiguous stream match / route to another CB is never followed.
 regs[bases[7]+0x800+16]=0x80000800;trace.clear();assert(s->inspectScanout(&snap)==0);readOnly();
 assert(snap.matches==2&&!snap.cb2Observed);
 regs[bases[7]+0x800+16]=0;regs[bases[7]+0xc00+12]=3;
 trace.clear();assert(s->inspectScanout(&snap)==0);readOnly();assert(snap.matches==1&&!snap.cb2Observed);
 // Closed admission or gated clocks do not touch MDSS/SMMU registers.
 regs[bases[0]+0xc]=0;trace.clear();assert(s->inspectScanout(&snap)==kIOReturnNotReady);
 assert(trace.size()==1&&trace[0].addr==bases[0]+0xc);
 s->stop(nub);trace.clear();memset(&snap,0xab,sizeof(snap));assert(s->inspectScanout(&snap)==kIOReturnNotReady&&trace.empty());
 SweetScanoutSnapshot zero{};assert(!memcmp(&snap,&zero,sizeof(snap)));s->release();queued.clear();
 // Missing optional CB2 mapping is an explicit failure; no consumer raw map.
 irqRegs();missingMap=8;s=make(nub);assert(s->inspectScanout(&snap)==kIOReturnNotReady);
 s->stop(nub);s->release();missingMap=99;queued.clear();

 // Stage2: actual timer-driven quiesce/submit/status code, independent inputs
 // for in-flight line, source readback, flush, PP done, IRQ progress and faults.
 ownMode=2;
 auto handoffRegs=[&](){
  regs[bases[2]+0]=regs[bases[2]+0xc]=0x9240438;
  regs[bases[2]+0x14]=0x9d200000;regs[bases[2]+0x24]=4320;
  regs[bases[2]+0x30]=0x237ff;regs[bases[2]+0x34]=0x03020001;
  regs[bases[2]+0x38]=0x80000000;regs[bases[3]+0x14]=0x20000;
  regs[bases[7]+0x20]=0x4c017e5e;regs[bases[7]+0x24]=0x60000038;
  regs[bases[7]]=0x2b0406;
  regs[bases[7]+0x80c]=0x84200800;regs[bases[7]+0xc0c]=2;
  regs[bases[7]+0x1008]=0x1f000;regs[bases[7]+0x1808]=1;
  regs[bases[8]]=0xe0;regs[bases[8]+0x58]=0x400;
  regs[0x0ae01014]=0x100; // stale PP0 done MUST be cleared before the new attempt
 };
 auto pollHandoff=[&](){fakeTicks+=1000000;std::lock_guard<std::recursive_mutex> l(s->fLoop->gate);
  SweetMDSS::scanoutTimerAction(s,s->fScanoutTimer);};
 for(unsigned scenario=0;scenario<20;++scenario){
  irqRegs();s=make(nub);startProbe(s);handoffRegs();
  auto*wl=IOWorkLoop::workLoop();auto*e=IOInterruptEventSource::interruptEventSource(nub,[](OSObject*,IOInterruptEventSource*,int){});
  assert(wl->addEventSource(e)==0);auto*ch=SweetVsyncChannel::withEvent(e);
  assert(s->attachVsync(ch)==0);frame(s,16666667);
  auto*pool=new SweetScanoutPool;SweetScanoutStatus st{};
  if(scenario>=6&&scenario<=12){
   if(scenario==6)regs[bases[8]+0x58]=2; // a REAL translation fault
   if(scenario==7)regs[bases[8]+0x58]=0x800; // unclassified new raw bit
   if(scenario==8)regs[bases[8]]=0xe1; // stage1 translation on
   if(scenario==9)regs[bases[7]+0x1008]=0x3f000; // stage2 translation on
   if(scenario==10)regs[bases[7]+0x48]=1;
   if(scenario==11)regs[bases[2]+0x30]^=1;
   if(scenario==12)pool->pa=0x9d210000;
   trace.clear();assert(s->beginScanout(pool,ch)!=0&&!pool->pinned&&!s->fScanoutUsed);
   for(auto op:trace)assert(!op.write);
  }else{
   trace.clear();assert(s->beginScanout(pool,ch)==0&&!pool->pinned);
   assert(s->beginScanout(pool,ch)==kIOReturnBusy); // exactly one attempt
   auto countBefore=trace.size();assert(s->scanoutStatus(pool,&st)==0&&st.state==SweetScanoutQuiescing);
   assert(trace.size()==countBefore); // status is PURE retained state, no MMIO
   assert(s->dcsWrite(words,2)==kIOReturnBusy); // DCS cannot forge completion mid-transfer
   regs[bases[4]+0x2b0]=1000;pollHandoff();pollHandoff();
   assert(!pool->pinned&&regs[bases[2]+0x14]==0x9d200000); // never program while busy
   if(scenario==1){fakeTicks+=50000000;pollHandoff();assert(s->fScanoutState==SweetScanoutFailed&&!pool->pinned);
    assert(regs[bases[4]+0x288]==0x180088&&regs[bases[4]+0x2b4]==0x80000001);
   }else if(scenario==2){s->abandonScanout(pool);assert(!pool->pinned&&s->fScanoutState==SweetScanoutFailed);
   }else if(scenario==13){s->stop(nub);assert(!pool->pinned&&s->fScanoutState==SweetScanoutFailed);
   }else if(scenario==15){s->failIRQ("fixture delivery loss");pollHandoff();assert(!pool->pinned&&s->fScanoutState==SweetScanoutFailed);
   }else if(scenario==18){ /* exercise free without stop while still quiescing */
   }else if(scenario==16){regs[bases[4]+0x2b0]=0;pollHandoff();keepPPDone=true;pollHandoff();keepPPDone=false;
    assert(!pool->pinned&&s->fScanoutState==SweetScanoutFailed&&regs[bases[2]+0x14]==0x9d200000);
   }else{
    regs[bases[4]+0x2b0]=2340;pollHandoff();assert(!pool->pinned);
    if(scenario==5)dropSourceWrite=true;
    pollHandoff();dropSourceWrite=false;assert(pool->pinned&&s->fScanoutTouched);
    if(scenario==5)assert(s->fScanoutState==SweetScanoutFailed);
    else{
     assert(s->fScanoutState==SweetScanoutSubmitted&&regs[bases[2]+0x14]==pool->pa);
     assert(regs[bases[2]+0x24]==4608&&regs[bases[2]+8]==0x300016&&!regs[0x0ae01014]);
     assert(regs[bases[4]+0x288]==0x180088&&regs[bases[4]+0x2b4]==0x80000001);
     pollHandoff();assert(s->fScanoutState==SweetScanoutSubmitted);
     if(scenario==3){fakeTicks+=100000000;pollHandoff();assert(s->fScanoutState==SweetScanoutFailed);}
     else if(scenario==4){s->stop(nub);assert(s->fScanoutState==SweetScanoutFailed);}
     else if(scenario==14){assert(s->setPanelPower(false)==0&&s->fScanoutState==SweetScanoutFailed);}
     else if(scenario==19){ /* exercise free without stop after source submission */ }
     else{
      regs[0x0ae01014]=0x100;pollHandoff();assert(s->fScanoutState==SweetScanoutSubmitted);
      frame(s,16666667);pollHandoff();assert(s->fScanoutState==SweetScanoutSubmitted); // flush still pending
      regs[bases[3]+0x18]=0;pollHandoff();assert(s->fScanoutState==SweetScanoutActive);
      assert(s->scanoutStatus(pool,&st)==0&&st.state==SweetScanoutActive&&st.hardwareReachable&&st.address==pool->pa);
      if(scenario==17){s->failIRQ("fixture active IRQ loss");trace.clear();
       assert(s->scanoutStatus(pool,&st)==0&&st.state==SweetScanoutFailed&&trace.empty());}
     }
    }
    // Every source-register write follows auto disable and the independent pin.
    bool disabled=false;for(auto op:trace)if(op.write){
     if(op.addr==bases[4]+0x2b4&&!op.value)disabled=true;
     if(op.addr==bases[2]+0x14||op.addr==bases[2]+8||op.addr==bases[2]+0x24)assert(disabled&&pool->pinned);
     assert(op.addr!=bases[3]+0x1c); // no software CTL_START under autorefresh
    }
   }
  }
  IOTimerEventSource*retainedTimer=nullptr;
  if(scenario>=18){retainedTimer=s->fScanoutTimer;retainedTimer->retain();}
  else{
   s->stop(nub);trace.clear();pollHandoff();assert(trace.empty());
   if(s->fScanoutUsed){assert(s->scanoutStatus(pool,&st)==0&&trace.empty());}
  }
  s->detachVsync(ch);ch->close();ch->release();wl->removeEventSource(e);e->release();wl->release();
  s->release();queued.clear();assert(pool->refs==(pool->pinned?2u:1u));
  if(retainedTimer){assert(!retainedTimer->loop&&!retainedTimer->timeout);retainedTimer->release();}
  bool pinned=pool->pinned;pool->release();
  if(pinned){assert(pool->refs==1);pool->release();} // simulated reboot: fake hardware can no longer DMA
  assert(!liveMaps&&!rawBlocks);
 }
 puts("PASS: stage2 actual provider, 20 quiesce/busy/stale-done/fresh-done/flush/IRQ/readback/fault/stop/free cases; submitted pools remain pinned through service free until simulated reboot");
 ownMode=0;
 assert(!rawBlocks&&!liveMaps);irqMode=0;irqMMIO=false;
 nub->release();assert(live==0);
 puts("PASS: actual service/client; legacy DCS traces, F2 short packets/LP, idempotence, frozen TE, recovery/unknown state, independent fault budgets, gates/stop, F3 IRQ/probe/ACK/mailbox/storm/watchdog/OFF/termination, F3 read-only snapshots/decoding/bounds/clock admission/default compatibility, F4 typed read-only scanout/security/SMMU inspection, unknown/ambiguous layout rejection and zero live objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-display-service-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+DSI_MEMORY_FIXTURE+pool_fixture+header+source+backlight+'\nstatic unsigned dp_logged;\n'+old+tests)
    compiler = os.environ.get('CXX', 'clang++-19')
    binary = Path(temp)/'test'
    subprocess.run([compiler, '-std=c++17', '-g', '-pthread', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env=dict(os.environ)
    env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_leaks=0'
    subprocess.run([str(binary)], check=True,env=env,timeout=45)
