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

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
arm = root/'iokit/Kernel/arm'
header = (arm/'SweetDisplayBackend.h').read_text()
source = (arm/'SweetMDSS.cpp').read_text()
legacy = (root/'bsd/dev/sweet_display_probe.c').read_text()
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
                   ('dp_wr', 'writeReg(base,off,v);')]:
    original = function(source, name)
    source = source.replace(original, original[:original.index('{')+1]+body+'}')
source = source.replace('class SweetMDSS :', 'struct SweetMDSS :')
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
constexpr int kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnBadArgument=2,kIOReturnIOError=3,kIOReturnUnsupported=4;
constexpr unsigned SWEET_DEV_FB_H=2340,SWEET_DEV_FB_W=1080,SWEET_DEV_FB_PA=0x9d200000,SWEET_DEV_FB_STRIDE=4320;
static unsigned live=0,liveMaps=0;
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
struct IOWorkLoop : OSObject {
 std::recursive_mutex gate;
 static IOWorkLoop *workLoop(){return new IOWorkLoop;}
 int addEventSource(IOEvent *e){e->loop=this;e->retain();return 0;}
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
 static IOTimerEventSource*timerEventSource(OSObject*o,Action a){auto*p=new IOTimerEventSource;p->owner=o;p->action=a;return p;}
 void setTimeoutMS(unsigned t){timeout=t;} void setTimeoutUS(unsigned t){timeout=t;} void cancelTimeout(){timeout=0;}
};
static constexpr uintptr_t bases[9]={0x0010b000,0x0af00000,0x0ae05000,0x0ae02000,0x0ae6b800,0x0ae6c000,0x0ae94000,0x15000000,0x15082000};
static constexpr size_t lengths[9]={0x1000,0x3000,0x1000,0x1000,0x800,0x800,0x400,0x2000,0x1000};
static unsigned badMap=99,missingMap=99;
struct IOMemoryMap : OSObject {
 unsigned index;
 IOMemoryMap(unsigned i):index(i){++liveMaps;}~IOMemoryMap(){--liveMaps;}
 uintptr_t getPhysicalAddress(){return bases[index]+(index==badMap?4:0);}
 uintptr_t getVirtualAddress(){return bases[index];}
 size_t getLength(){return lengths[index];}
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
 bool inactive=false;
 using Notify=bool(*)(void*,void*,IOService*,IONotifier*);
 virtual bool start(IOService*){return true;}virtual void stop(IOService*){}
 virtual IOReturn setProperties(OSObject*){return 0;}
 bool isInactive(){return inactive;}
 int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){return i==missingMap?nullptr:new IOMemoryMap(i);}
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
static int enabled=1,probeEnabled=1,powerEnabled=0;
static bool PE_parse_boot_argn(const char*k,void*v,size_t){
 if(!strcmp(k,"sweetmdss")){*(int*)v=enabled;return true;}
 if(!strcmp(k,"sweetdispprobe")){*(int*)v=probeEnabled;return true;}
 if(!strcmp(k,"sweetfbpower")){*(int*)v=powerEnabled;return true;}return false;
}
static void quiet(const char*,...){}
#define IOLog quiet
#define SWEET_LOG_INFO quiet
#define SWEET_LOG_WARN quiet
#define SWEET_LOG_ERROR quiet
static bool sweet_log_level_enabled(unsigned){return true;}
static uint64_t mach_absolute_time(){return 42;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t*n){*n=t;}
static void delay(unsigned){}
static constexpr uint64_t NSEC_PER_MSEC=1000000;
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t*p){*p=n;}
using IOSimpleLock=IOLock;
static IOSimpleLock*IOSimpleLockAlloc(){return IOLockAlloc();}
static void IOSimpleLockFree(IOSimpleLock*p){IOLockFree(p);}
static void IOSimpleLockLock(IOSimpleLock*p){p->lock();}
static void IOSimpleLockUnlock(IOSimpleLock*p){p->unlock();}
static int legacyWrites;
static int sweet_display_brightness_set(uint32_t){++legacyWrites;return 0;}

static std::map<uintptr_t,uint32_t> regs;
struct Op {bool write;uintptr_t addr;uint32_t value;bool operator==(const Op&o)const{return write==o.write&&addr==o.addr&&value==o.value;}};
static std::vector<Op> trace;
static bool dmaFired=false,dmaDone=true,lateError=false,pauseRead=false,enteredRead=false,releaseRead=false;
static unsigned failNextDMAs=0;
static bool activeDMAFails=false;
static std::mutex pauseLock;
static std::condition_variable pauseCV;
static uint32_t readReg(uintptr_t b,uint32_t o){
 if(pauseRead&&b==bases[6]&&o==4){std::unique_lock<std::mutex> l(pauseLock);enteredRead=true;pauseCV.notify_all();pauseCV.wait(l,[]{return releaseRead;});}
 uint32_t v=regs[b+o];if(b==bases[6]&&o==0x110&&dmaFired)v=(dmaDone&&!activeDMAFails?1:0)|(lateError?(1u<<24):0);
 trace.push_back({false,b+o,v});return v;
}
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){trace.push_back({true,b+o,v});regs[b+o]=v;if(b==bases[6]&&o==0x90){dmaFired=true;activeDMAFails=failNextDMAs>0;if(failNextDMAs)--failNextDMAs;}}
static void resetRegs(){trace.clear();regs.clear();dmaFired=false;dmaDone=true;lateError=false;
 failNextDMAs=0;activeDMAFails=false;
 regs[bases[0]+0xc]=1;regs[bases[1]+0x2080]=0;regs[bases[6]+4]=5;
 regs[bases[4]+0x288]=0x12345678;regs[bases[4]+0x2b4]=0x3344;
 regs[bases[6]+0x3c]=0x9;regs[bases[6]+0x4c]=0x18;regs[bases[6]+0x84]=0x32;
}
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
 nub->release();assert(live==0);
 puts("PASS: actual service/client; legacy DCS traces, F2 short packets/LP, idempotence, frozen TE, recovery/unknown state, independent fault budgets, gates/stop and zero live objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-display-service-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+header+source+backlight+'\nstatic unsigned dp_logged;\n'+old+tests)
    compiler = os.environ.get('CXX', 'clang++-19')
    binary = Path(temp)/'test'
    subprocess.run([compiler, '-std=c++17', '-g', '-pthread', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
