#!/usr/bin/env python3
"""Exercise actual F1b framebuffer, dispatch and lifecycle with OS-only fixtures.
Usage: test-mh2lm-framebuffer-service.py EXTRACTED_XNU
The unchanged DCS/registry helper has its own test-mh2lm-display-service.py.
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
source = (arm/'SweetFramebuffer.cpp').read_text()
service = (arm/'SweetFBService.inc').read_text()
# Keep the whole real framebuffer and user-client implementation. Only the
# unrelated backlight and optional contract experiment have separate fixtures.
start = source.index('#if defined(ARM64_BOARD_CONFIG_MH2LM)\n/*\n * Panel brightness')
end = source.index('\nbool\nAppleCLCD::start', start)
source = source[:start]+'class AppleARMBacklight : public IOService {};\n'+source[end:]
source = source.replace('class AppleCLCD :', 'struct AppleCLCD :')
source = source.replace('class SweetFBContract;', 'struct SweetFBContract;')
source = source.replace('class SweetDisplayClient;', 'struct SweetDisplayClient;')
source = source.replace('#include "SweetFBContract.inc"', '''
struct SweetFBContract : OSObject {
 bool setup(AppleCLCD *,IOWorkLoop *){return false;}
 void shutdown(){} void close(IOMobileFramebufferUserClient *){}
 IOReturn invoke(IOMobileFramebufferUserClient *,uint32_t,IOExternalMethodArguments *){return kIOReturnUnsupported;}
};''')
source = source.replace('#include "SweetFBService.inc"', service)
source = re.sub(r'^#include.*\n', '', source, flags=re.M)

prelude = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include "sweet_fb_trace.h"
#define SWEET 1
#define ARM64_BOARD_CONFIG_MH2LM 1
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define OSTypeAlloc(t) new t()
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
#define OSSafeReleaseNULL(p) do {if(p){(p)->release();(p)=nullptr;}}while(0)
#define __unused __attribute__((unused))
using IOReturn=uint32_t;using UInt32=uint32_t;using SInt32=int32_t;
using task_t=uintptr_t;using mach_port_t=uint32_t;using mach_vm_address_t=uint64_t;
using io_user_reference_t=uint64_t;using OSAsyncReference64=uint64_t[8];
constexpr IOReturn kIOReturnSuccess=0,kIOReturnBadArgument=0xe00002c2,kIOReturnOffline=0xe00002d7,
 kIOReturnUnsupported=0xe00002c7,kIOReturnNoMemory=0xe00002bd;
constexpr uint32_t kIOUCVariableStructureSize=0xffffffffU,MACH_PORT_NULL=0;
constexpr unsigned kIOExternalMethodScalarInputCountMax=16,kIOExternalMethodScalarOutputCountMax=16;
constexpr unsigned kIOUserNotifyOptionCanDrop=1;
static constexpr const char *kIOUserClientClassKey="class",*kIOUserClientDefaultLockingKey="lock",
 *kIOUserClientDefaultLockingSetPropertiesKey="set",*kIOUserClientDefaultLockingSingleThreadExternalMethodKey="single";
static constexpr bool kOSBooleanTrue=true,kOSBooleanFalse=false;
static std::atomic<unsigned> live{0},rawBlocks{0},lockCalls{0},dispatchCalls{0},errors{0},notified{0};
struct OSObject {
 std::atomic<unsigned> refs{1};
 static void *operator new(size_t n){void*p=calloc(1,n);if(!p)throw std::bad_alloc();return p;}
 static void operator delete(void*p){::free(p);}
 OSObject(){++live;}virtual ~OSObject(){--live;}virtual bool init(){return true;}virtual void free(){}
 void retain(){assert(refs.fetch_add(1)>0);}void release(){if(refs.fetch_sub(1)==1){free();delete this;}}
};
using IOLock=std::mutex;
static IOLock *IOLockAlloc(){return new IOLock;}
static void IOLockFree(IOLock*p){delete p;}
static void IOLockLock(IOLock*p){++lockCalls;p->lock();}static void IOLockUnlock(IOLock*p){p->unlock();}
static void *IOMallocData(size_t n){void*p=malloc(n);if(p)++rawBlocks;return p;}
static void IOFreeData(void*p,size_t){assert(rawBlocks);--rawBlocks;free(p);}
static std::map<const void*,unsigned> additions;
struct OSArray : OSObject {
 std::vector<OSObject*> a;
 static OSArray*withCapacity(unsigned){return new OSArray;}
 bool setObject(OSObject*p){p->retain();a.push_back(p);return true;}
 unsigned getCount(){return a.size();}OSObject*getObject(unsigned i){return a.at(i);}
 unsigned getNextIndexOfObject(OSObject*p,unsigned start){for(unsigned i=start;i<a.size();++i)if(a[i]==p)return i;return unsigned(-1);}
 void removeObject(unsigned i){auto*p=a.at(i);a.erase(a.begin()+i);p->release();}
 void free()override{while(!a.empty())removeObject(0);}
};
struct IOService : OSObject {
 bool inactive=false;
 virtual bool start(IOService*){return true;}virtual void stop(IOService*){}
 bool isInactive(){return inactive;}void terminate(){inactive=true;}
 template<class T>void setProperty(const char*,T){}void setProperty(const char*,unsigned long long,unsigned){}
 void registerService(){}bool attach(IOService*){return true;}void detach(IOService*){}
};
struct IOWorkLoop;
struct Event : OSObject {IOWorkLoop*loop=nullptr;OSObject*owner=nullptr;};
static int failEvent=-1;
struct IOWorkLoop : OSObject {
 std::recursive_mutex gate;
 static IOWorkLoop*workLoop(){return new IOWorkLoop;}
 int addEventSource(Event*e){if(failEvent==0)return -1;if(failEvent>0)--failEvent;e->loop=this;e->retain();return 0;}
 void removeEventSource(Event*e){std::lock_guard<std::recursive_mutex> l(gate);assert(e->loop==this);e->loop=nullptr;e->release();}
};
struct IOCommandGate : Event {
 using Action=IOReturn(*)(OSObject*,void*,void*,void*,void*);
 static IOCommandGate*commandGate(OSObject*o){auto*p=new IOCommandGate;p->owner=o;return p;}
 IOReturn runAction(Action a,void*x){std::lock_guard<std::recursive_mutex> l(loop->gate);return a(owner,x,nullptr,nullptr,nullptr);}
};
struct IOTimerEventSource : Event {
 using Action=void(*)(OSObject*,IOTimerEventSource*);Action action;unsigned timeout=0;
 static IOTimerEventSource*timerEventSource(OSObject*o,Action a){auto*p=new IOTimerEventSource;p->owner=o;p->action=a;return p;}
 void setTimeoutUS(unsigned n){timeout=n;}void cancelTimeout(){timeout=0;}
 void fire(){std::lock_guard<std::recursive_mutex> l(loop->gate);if(timeout)action(owner,this);}
};
struct IOMemoryDescriptor : OSObject {size_t length=0;size_t getLength(){return length;}};
struct IOMemoryMap : OSObject {};
struct IOExternalMethodArguments {
 const uint64_t*scalarInput=nullptr;uint32_t scalarInputCount=0;uint64_t*scalarOutput=nullptr;uint32_t scalarOutputCount=0;
 const void*structureInput=nullptr;uint32_t structureInputSize=0;void*structureOutput=nullptr;uint32_t structureOutputSize=0;
 IOMemoryDescriptor*structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
};
using IOExternalMethodAction=IOReturn(*)(OSObject*,void*,IOExternalMethodArguments*);
struct IOExternalMethodDispatch {IOExternalMethodAction function;uint32_t checkScalarInputCount,checkStructureInputSize,checkScalarOutputCount,checkStructureOutputSize;};
static std::mutex portMutex;static std::map<mach_port_t,unsigned> portReleases;
struct IOUserClient : IOService {
 virtual bool initWithTask(task_t,void*,UInt32){return true;}
 virtual IOReturn clientClose(){return 0;}
 virtual IOReturn registerNotificationPort(mach_port_t,UInt32,io_user_reference_t){return 0;}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*a,IOExternalMethodDispatch*d,OSObject*t,void*r){
  ++dispatchCalls;
  // XNU-8796 IOUserClient::externalMethod dispatch branch, including descriptor lengths.
  uint32_t count=d->checkScalarInputCount;if(count!=kIOUCVariableStructureSize&&count!=a->scalarInputCount)return kIOReturnBadArgument;
  count=d->checkStructureInputSize;if(count!=kIOUCVariableStructureSize&&count!=(a->structureInputDescriptor?a->structureInputDescriptor->getLength():a->structureInputSize))return kIOReturnBadArgument;
  count=d->checkScalarOutputCount;if(count!=kIOUCVariableStructureSize&&count!=a->scalarOutputCount)return kIOReturnBadArgument;
  count=d->checkStructureOutputSize;if(count!=kIOUCVariableStructureSize&&count!=(a->structureOutputDescriptor?a->structureOutputDescriptor->getLength():a->structureOutputSize))return kIOReturnBadArgument;
  return d->function(t,r,a);
 }
 static void releaseNotificationPort(mach_port_t p){std::lock_guard<std::mutex> l(portMutex);++portReleases[p];}
 static void setAsyncReference64(uint64_t*,mach_port_t,mach_vm_address_t,io_user_reference_t,task_t){}
 static void sendAsyncResult64WithOptions(uint64_t*,IOReturn,io_user_reference_t*,uint32_t,unsigned){++notified;}
};
struct SweetSurface : OSObject {
 uint32_t id=1,width=4,height=4,bytesPerRow=16,pixelFormat=0x42475241,bytesPerElement=4,elementWidth=1,elementHeight=1,planeCount=0;
 uint64_t offset=0,allocSize=64;IOMemoryDescriptor*memory=nullptr;uint32_t pixels[16]{};
 SweetSurface(){memory=new IOMemoryDescriptor;memory->length=64;}
 uintptr_t kernelAddress(){return (uintptr_t)pixels;}void free()override{memory->release();}
};
static SweetSurface *testSource=nullptr;
static SweetSurface*sweet_surface_create_physical(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t){return new SweetSurface;}
static SweetSurface*sweet_surface_copy(uint32_t id){if(testSource&&id==testSource->id){testSource->retain();return testSource;}return nullptr;}
static unsigned sweet_surface_port_in_task(SweetSurface*,task_t){return 77;}
struct SweetDisplayBackend : IOService {};
static SweetDisplayBackend *published=nullptr;
static bool failWatch=false;
struct SweetDisplayClient : OSObject {
 OSObject*owner=nullptr;IOWorkLoop*loop=nullptr;void(*callback)(OSObject*)=nullptr;bool stopped=false;
 static SweetDisplayClient*withOwner(OSObject*o,IOWorkLoop*w,void(*cb)(OSObject*)){auto*p=new SweetDisplayClient;p->owner=o;p->loop=w;w->retain();p->callback=cb;return p;}
 bool watch(){return !failWatch;}void stop(){stopped=true;}
 SweetDisplayBackend*copyBackend(){if(stopped||!published)return nullptr;published->retain();return published;}
 void fire(){std::lock_guard<std::recursive_mutex> l(loop->gate);if(!stopped)callback(owner);}
 void free()override{loop->release();}
};
struct PE_Video {uint64_t v_baseAddr;uint32_t v_width,v_height,v_rowBytes;};
static void PE_current_console(PE_Video*v){*v={0x9d200000,4,4,16};}
static int serviceArg=1,mdssArg=1,contractArg=0;
static bool PE_parse_boot_argn(const char*k,void*p,size_t){
 int v=0;
 if(!strcmp(k,"sweetfbservice"))v=serviceArg;
 else if(!strcmp(k,"sweetmdss"))v=mdssArg;
 else if(!strcmp(k,"sweetfbcontract"))v=contractArg;
 else if(!strcmp(k,"sweetfb_hz"))v=30;
 else if(!strcmp(k,"sweetfb_native")||!strcmp(k,"sweetfbpreserveoff"))v=1;
 else return false;
 memcpy(p,&v,sizeof(v));return true;
}
static uint64_t mach_absolute_time(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void absolutetime_to_nanoseconds(uint64_t a,uint64_t*b){*b=a;}
static SInt32 OSIncrementAtomic(volatile SInt32*p){return __atomic_add_fetch(p,1,__ATOMIC_RELAXED);}
static void proc_selfname(char*p,size_t n){snprintf(p,n,"fixture");}
static int sweet_display_brightness_ready(){return 0;}
extern "C" {int sweet_console_muted=0;}
static void quiet(const char*,...){}
#define IOLog quiet
#define SWEET_LOG_INFO quiet
#define SWEET_LOG_WARN quiet
#define SWEET_LOG_ERROR(...) do{++errors;}while(0)
static bool sweet_log_level_enabled(unsigned){return false;}
'''

tests = r'''
static IOReturn call(IOMobileFramebufferUserClient*c,uint32_t s,IOExternalMethodArguments&a){return c->externalMethod(s,&a,nullptr,nullptr,nullptr);}
static IOMobileFramebufferUserClient*client(AppleCLCD*f){auto*c=new IOMobileFramebufferUserClient;assert(c->initWithTask(1,nullptr,0)&&c->start(f));return c;}
static IOExternalMethodArguments scalar(const uint64_t*in,unsigned ni,uint64_t*out,unsigned no){IOExternalMethodArguments a;a.scalarInput=in;a.scalarInputCount=ni;a.scalarOutput=out;a.scalarOutputCount=no;return a;}
int main(){
 auto*provider=new IOService;
 // Partial starts: every gate/timer/helper resource is reclaimed on failure.
 for(int fail: {0,1}){failEvent=fail;auto*f=new AppleCLCD;assert(!f->start(provider));f->release();assert(live==1);}
 failEvent=-1;failWatch=true;{auto*f=new AppleCLCD;assert(!f->start(provider));f->release();assert(live==1);}failWatch=false;
 mdssArg=0;{auto*f=new AppleCLCD;assert(!f->start(provider));f->release();assert(live==1);}mdssArg=1;
 contractArg=1;{auto*f=new AppleCLCD;assert(!f->start(provider));f->release();assert(live==1);}contractArg=0;
 auto*f=new AppleCLCD;assert(f->start(provider));assert(f->fServiceMode&&f->fStateGateAdded);
 assert(!f->fBackendBound);published=new SweetDisplayBackend;f->fDisplayClient->fire();assert(f->fBackendBound);
 published->release();published=nullptr;f->fDisplayClient->fire();assert(!f->fBackendBound);
 auto*c=client(f);auto*d=client(f);assert(f->fClients->getCount()==2);
 // Actual legacy handlers behind the dispatch table: outputs and pixel policy.
 uint64_t out[2]={99,99},in[4]={0,0,0,0};auto a=scalar(nullptr,0,out,2);
 assert(call(c,8,a)==0&&out[0]==4&&out[1]==4);
 a=scalar(nullptr,0,out,1);assert(call(c,28,a)==0&&out[0]==458);
 a=scalar(in,2,out,1);assert(call(c,3,a)==0&&out[0]==77);
 a=scalar(nullptr,0,out,1);assert(call(c,4,a)==0&&out[0]==1);
 uint32_t before=f->fSwapID;
 a=scalar(nullptr,0,nullptr,1);assert(call(c,4,a)==kIOReturnBadArgument&&f->fSwapID==before);
 a=scalar(in,1,out,1);assert(call(c,4,a)==kIOReturnBadArgument&&f->fSwapID==before);
 a={};assert(call(c,4,a)==kIOReturnBadArgument);
 a=scalar(in,17,out,1);assert(call(c,7,a)==kIOReturnBadArgument);
 assert(c->externalMethod(4,nullptr,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 uint8_t rec[SWEET_FB_SWAP_REC_SIZE]{};a={};a.structureInput=rec;a.structureInputSize=sizeof(rec)-1;
 assert(call(c,5,a)==kIOReturnBadArgument);a.structureInputSize=sizeof(rec);a.structureInput=nullptr;assert(call(c,5,a)==kIOReturnBadArgument);
 auto*descriptor=new IOMemoryDescriptor;descriptor->length=sizeof(rec);a.structureInputDescriptor=descriptor;assert(call(c,5,a)==kIOReturnBadArgument);a.structureInputDescriptor=nullptr;
 testSource=new SweetSurface;testSource->id=2;std::fill(std::begin(testSource->pixels),std::end(testSource->pixels),0xff112233);
 auto word=[&](unsigned off,uint32_t v){memcpy(rec+off,&v,4);};word(0x50,1);word(0x54,2);word(0x104,1);word(0xcc,4);word(0xd0,4);
 a.structureInput=rec;assert(call(c,5,a)==0);assert(f->fDirty);f->fVsync->fire();f->fVsync->fire();for(auto px:f->fSurface->pixels)assert(px==0xff112233);
 in[0]=0;a=scalar(in,1,nullptr,0);assert(call(c,12,a)==0&&f->fPowerOff);f->fVsync->fire();for(auto px:f->fSurface->pixels)assert(px==0xff112233);
 in[0]=1;assert(call(c,12,a)==0&&!f->fPowerOff);
 in[0]=123;in[1]=456;in[2]=5;in[3]=0;a=scalar(in,4,nullptr,0);assert(call(c,72,a)==0);
 assert(c->registerNotificationPort(10,0,0)==0);assert(c->registerNotificationPort(11,0,0)==0);assert(portReleases[10]==1);
 f->fVsync->fire();assert(notified==1);
 in[2]=8;assert(call(c,72,a)==kIOReturnBadArgument&&c->fFunc[5]==123);in[2]=5;a.scalarInputCount=2;assert(call(c,72,a)==kIOReturnBadArgument);
 in[0]=1;in[1]=0x80000000;in[2]=0;a=scalar(in,3,nullptr,0);assert(call(c,6,a)==0); // No new wait semantics in F1b.
 unsigned char zeros[32];memset(zeros,0xab,sizeof(zeros));out[0]=99;a=scalar(nullptr,0,out,1);a.structureOutput=zeros;a.structureOutputSize=sizeof(zeros);
 assert(call(c,79,a)==0&&out[0]==0);for(auto v:zeros)assert(v==0);
 a={};a.structureInputDescriptor=descriptor;assert(call(c,999,a)==0); // Unknown OOL input is never dereferenced.
 for(unsigned i=0;i<100;++i){a={};assert(call(c,1000+i,a)==0);}assert(c->fServiceSelectorCount==32);
 // Independent submitting threads allocate distinct IDs; timer/client state is serialized.
 std::mutex idsMutex;std::vector<uint64_t> ids;
 auto submit=[&](IOMobileFramebufferUserClient*u){for(unsigned i=0;i<400;++i){uint64_t id;auto x=scalar(nullptr,0,&id,1);assert(call(u,4,x)==0);std::lock_guard<std::mutex> l(idsMutex);ids.push_back(id);}};
 std::thread t1(submit,c),t2(submit,d),timer([&]{for(unsigned i=0;i<400;++i)f->fVsync->fire();});t1.join();t2.join();timer.join();
 std::sort(ids.begin(),ids.end());assert(ids.size()==800&&std::adjacent_find(ids.begin(),ids.end())==ids.end());assert(lockCalls==0);
 // Closing one client cannot unregister another; close/stop/port teardown is idempotent.
 assert(c->clientClose()==0&&c->fServiceClosed&&portReleases[11]==1);assert(f->fClients->getCount()==1);
 a={};assert(call(c,79,a)==kIOReturnOffline);assert(c->registerNotificationPort(12,0,0)==kIOReturnOffline&&portReleases[12]==1);
 assert(d->registerNotificationPort(20,0,0)==0);c->stop(f);c->release();
 std::atomic<bool> started{false};std::thread racing([&]{started=true;for(unsigned i=0;i<2000;++i){uint64_t id;auto x=scalar(nullptr,0,&id,1);auto rc=call(d,4,x);assert(rc==0||rc==kIOReturnOffline);}});
 std::thread ports([&]{for(unsigned i=21;i<=70;++i){auto rc=d->registerNotificationPort(i,0,0);assert(rc==0||rc==kIOReturnOffline);}});
 while(!started.load())std::this_thread::yield();f->stop(provider);racing.join();ports.join();
 for(unsigned i=21;i<=70;++i)assert(portReleases[i]==1);
 assert(f->fServiceStopped&&f->fClients->getCount()==0&&d->fServiceClosed&&portReleases[20]==1);
 assert(!f->fVsyncAdded&&f->fStateGateAdded); // Gate remains valid for retained clients after stop.
 auto*late=new IOMobileFramebufferUserClient;assert(late->initWithTask(1,nullptr,0)&&!late->start(f));late->release();assert(f->fClients->getCount()==0);
 a=scalar(nullptr,0,out,1);assert(call(d,4,a)==kIOReturnOffline);assert(d->clientClose()==0);d->stop(f);
 f->stop(provider);f->release(); // Client still retains the stopped provider.
 d->release();descriptor->release();testSource->release();testSource=nullptr;provider->release();
 assert(live==0&&rawBlocks==0&&dispatchCalls>0);
 puts("PASS: real F1b start failures, dispatch bounds, unchanged pixel/power handlers, unknown ABI, gate concurrency, ports, provider stop/close and resource cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-fb-service-') as tmp:
    path=Path(tmp)/'test.cpp';binary=Path(tmp)/'test'
    path.write_text(prelude+source+tests)
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',
        '-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(root/'bsd/dev'),str(path),'-o',str(binary)],check=True)
    env=dict(os.environ)
    env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_leaks=0'
    subprocess.run([str(binary)],check=True,env=env,timeout=45)
