#!/usr/bin/env python3
"""Compile and exercise the actual optional CPU display contract, with host gate/surface fixtures.
Usage: test-mh2lm-framebuffer-contract.py PATCHED_XNU
No hardware-fence or iOS visual correctness is inferred from these tests.
"""
from pathlib import Path
import os,subprocess,sys,tempfile
if len(sys.argv)!=2:sys.exit(__doc__)
source=Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
prelude=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <map>
#include <functional>
#include <chrono>
#include <vector>
using IOReturn=uint32_t;
static constexpr uint32_t kIOReturnSuccess=0,kIOReturnError=0xe00002bcU,kIOReturnBadArgument=0xe00002c2U,
 kIOReturnBusy=0xe00002d5U,kIOReturnAborted=0xe00002ebU,kIOReturnOffline=0xe00002d7U,
 kIOReturnUnsupported=0xe00002c7U,kIOReturnTimeout=0xe00002d6U,kIOReturnCannotLock=0xe00002ccU,
 kIOReturnNoResources=0xe00002beU;
static constexpr int THREAD_UNINT=0,THREAD_AWAKENED=0,THREAD_TIMED_OUT=1,kMillisecondScale=1000000;
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define OSSafeReleaseNULL(x) do {if(x){(x)->release();(x)=nullptr;}} while(0)
static std::atomic<unsigned> errorLogs{0};
#define SWEET_LOG_ERROR(...) do {++errorLogs;}while(0)
#define SWEET_LOG_INFO(...) do {}while(0)
static int sweet_console_muted;
static bool PE_parse_boot_argn(const char *,void *,size_t){return false;}
static uint64_t mach_absolute_time(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void clock_interval_to_deadline(uint32_t n,int scale,uint64_t *d){*d=mach_absolute_time()+uint64_t(n)*scale;}
static int allocationFailure=-1;static unsigned rawBlocks;
static void *IOMallocZeroData(size_t n){if(allocationFailure==0)return nullptr;if(allocationFailure>0)--allocationFailure;void *p=calloc(1,n);if(p)++rawBlocks;return p;}
static void IOFreeData(void *p,size_t){assert(rawBlocks);--rawBlocks;free(p);}
using IOLock=std::mutex;
static void IOLockLock(IOLock *l){l->lock();}static void IOLockUnlock(IOLock *l){l->unlock();}
static std::atomic<unsigned> liveObjects{0};
struct OSObject {
 std::atomic<unsigned> refs{1};
 static void *operator new(size_t n){void *p=calloc(1,n);if(!p)throw std::bad_alloc();return p;}
 static void operator delete(void *p){::free(p);}
 OSObject(){++liveObjects;}virtual ~OSObject(){--liveObjects;}virtual bool init(){return true;}virtual void free(){}
 void retain(){assert(refs.fetch_add(1)>0);}void release(){if(refs.fetch_sub(1)==1){free();delete this;}}
};
struct IOCommandGate;
struct IOWorkLoop:OSObject {
 std::recursive_mutex mutex;std::condition_variable_any changed;
 bool onThread(){return false;}
 IOReturn addEventSource(IOCommandGate *);void removeEventSource(IOCommandGate *);
};
static std::mutex eventMutex;static std::condition_variable eventCV;static unsigned sleeps;
static void waitSleeps(unsigned n){std::unique_lock<std::mutex> l(eventMutex);assert(eventCV.wait_for(l,std::chrono::seconds(3),[&]{return sleeps>=n;}));}
static unsigned sleepCount(){std::lock_guard<std::mutex> l(eventMutex);return sleeps;}
struct IOCommandGate:OSObject {
 OSObject *owner=nullptr;IOWorkLoop *loop=nullptr;
 static IOCommandGate *commandGate(OSObject *o){auto *g=new IOCommandGate;g->owner=o;return g;}
 using Action=IOReturn(*)(OSObject *,void *,void *,void *,void *);
 IOReturn runAction(Action f,void *a){std::lock_guard<std::recursive_mutex> l(loop->mutex);return f(owner,a,nullptr,nullptr,nullptr);}
 IOReturn commandSleep(void *,uint64_t deadline,int){
  {std::lock_guard<std::mutex> l(eventMutex);++sleeps;eventCV.notify_all();}
  std::unique_lock<std::recursive_mutex> lock(loop->mutex,std::adopt_lock);
  auto result=loop->changed.wait_until(lock,std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline)));
  lock.release();return result==std::cv_status::timeout?THREAD_TIMED_OUT:THREAD_AWAKENED;
 }
 void commandWakeup(void *){loop->changed.notify_all();}
};
IOReturn IOWorkLoop::addEventSource(IOCommandGate *g){g->loop=this;return 0;}
void IOWorkLoop::removeEventSource(IOCommandGate *g){assert(g->loop==this);g->loop=nullptr;}
struct Memory {std::vector<unsigned char> bytes;explicit Memory(size_t n):bytes(n){}size_t getLength(){return bytes.size();}};
struct SweetSurface:OSObject {
 uint32_t id=1,width=4,height=3,bytesPerRow=20,offset=8,pixelFormat=0x42475241,allocSize=68;
 uint32_t bytesPerElement=4,elementWidth=1,elementHeight=1,planeCount=0;
 Memory *memory=nullptr;std::function<void()> hook;std::atomic<unsigned> reads{0};
 SweetSurface(){memory=new Memory(68);}~SweetSurface(){delete memory;}
 uintptr_t kernelAddress(){++reads;if(hook)hook();return (uintptr_t)memory->bytes.data();}
};
static std::map<uint32_t,SweetSurface *> surfaces;
static SweetSurface *sweet_surface_copy(uint32_t id){auto i=surfaces.find(id);if(i==surfaces.end())return nullptr;i->second->retain();return i->second;}
struct AppleCLCD {
 uint32_t fWidth=4,fHeight=3,fRowBytes=24;int32_t fOffX=0,fOffY=0;uintptr_t fPixels;
 IOLock lock;IOLock *fLock=&lock;SweetSurface *fSurface=nullptr;bool fPowerOff=false,fPreserveOff=true;
};
struct IOMobileFramebufferUserClient {bool fContractClosed=false;};
struct IOExternalMethodArguments {
 uint32_t scalarInputCount=0,scalarOutputCount=0;uint64_t *scalarInput=nullptr,*scalarOutput=nullptr;
 void *structureInputDescriptor=nullptr;const void *structureInput=nullptr;size_t structureInputSize=0;
};
#define SWEET_FB_SWAP_REC_SIZE 0x46c
#define SWEET_FB_FORMAT_BGRA 0x42475241
#include "SweetFBContract.inc"
struct Barrier {
 std::mutex m;std::condition_variable cv;bool entered=false,go=false;
 void pause(){std::unique_lock<std::mutex> l(m);entered=true;cv.notify_all();assert(cv.wait_for(l,std::chrono::seconds(3),[&]{return go;}));}
 void wait(){std::unique_lock<std::mutex> l(m);assert(cv.wait_for(l,std::chrono::seconds(3),[&]{return entered;}));}
 void release(){std::lock_guard<std::mutex> l(m);go=true;cv.notify_all();}
};
struct Fixture {
 AppleCLCD fb;std::vector<uint32_t> panel;IOWorkLoop *loop;SweetFBContract *driver;
 IOMobileFramebufferUserClient client;SweetSurface *surface;
 Fixture():panel(18,0xdeadbeefU){fb.fPixels=(uintptr_t)panel.data();loop=new IOWorkLoop;driver=new SweetFBContract;surface=new SweetSurface;surfaces[1]=surface;assert(driver->init()&&driver->setup(&fb,loop));fill(0xff334455);}
 ~Fixture(){driver->shutdown();driver->release();surfaces.erase(1);surface->release();loop->release();}
 void fill(uint32_t v){for(unsigned y=0;y<3;++y)for(unsigned x=0;x<4;++x)memcpy(surface->memory->bytes.data()+8+y*20+x*4,&v,4);}
 uint32_t begin(){uint64_t out=0;IOExternalMethodArguments a;a.scalarOutputCount=1;a.scalarOutput=&out;assert(driver->invoke(&client,4,&a)==0);return (uint32_t)out;}
 IOReturn wait(uint32_t id,uint32_t flags,uint64_t ms=0){uint64_t in[]={id,flags,ms};IOExternalMethodArguments a;a.scalarInputCount=3;a.scalarInput=in;return driver->invoke(&client,6,&a);}
 IOReturn scalar(uint32_t sel,std::initializer_list<uint64_t> values){std::vector<uint64_t> in(values);IOExternalMethodArguments a;a.scalarInputCount=(uint32_t)in.size();a.scalarInput=in.data();return driver->invoke(&client,sel,&a);}
 uint64_t time(){uint64_t out=0;IOExternalMethodArguments a;a.scalarOutputCount=1;a.scalarOutput=&out;assert(driver->invoke(&client,77,&a)==0);return out;}
 IOReturn submit(uint32_t id,uint32_t updated=1,uint32_t disabled=0,uint32_t bg=0){
  unsigned char rec[0x46c]={};auto put=[&](unsigned off,uint32_t v){memcpy(rec+off,&v,4);};
  put(0x50,id);put(0x54,1);put(0x104,updated);put(0x108,disabled);put(0x10c,bg);put(0x6c,4);put(0x70,3);put(0xcc,4);put(0xd0,3);
  IOExternalMethodArguments a;a.structureInput=rec;a.structureInputSize=sizeof(rec);return driver->invoke(&client,5,&a);
 }
 void expect(uint32_t pixel){for(unsigned y=0;y<3;++y){for(unsigned x=0;x<4;++x)assert(panel[y*6+x]==pixel);assert(panel[y*6+4]==0xdeadbeef&&panel[y*6+5]==0xdeadbeef);}}
};
int main(){
 {SFCState s={};s.ready=true;auto *e=sfc_begin(&s,1,1);assert(e&&e->id==1);e->phase=SFC_CACHED;s.displayID=1;
  assert(sfc_query(&s,1,1,0x80000000)==SFC_BUSY&&sfc_query(&s,1,1,0x80000001)==0);
  assert(sfc_query(&s,1,0,0x80000003)==0&&sfc_query(&s,1,0,4)==SFC_BAD);
  s.ready=false;assert(sfc_query(&s,1,0,3)==SFC_BAD&&sfc_query(&s,1,0,4)==0);
  s.nextID=UINT32_MAX;auto *next=sfc_begin(&s,1,2);assert(next&&next->id==2);
  for(unsigned i=0;i<SFC_SLOTS-2;++i)assert(sfc_begin(&s,1,3));assert(!sfc_begin(&s,1,4));
  e->phase=SFC_RETIRED;e->waiters=1;s.displayID=0;assert(!sfc_begin(&s,1,5));e->waiters=0;assert(sfc_begin(&s,1,6));}
 {SFCRect r={};assert(sfc_rect(0,0,0,0,4,3,1,1,4,3,3,2,&r));assert(r.sx==1&&r.sy==1&&r.w==3&&r.h==2);
  assert(!sfc_rect(INT32_MAX,0,0,0,4,3,0,0,4,3,4,3,&r));assert(!sfc_bounds(UINTPTR_MAX-8,64,0,4,3,20));}
 {Fixture f;auto id=f.begin();assert(!f.submit(id));f.expect(0xff334455);
  assert(f.wait(id,0x80000000)==kIOReturnBusy&&!f.wait(id,0x80000001));
  assert(f.wait(id,0,2)==kIOReturnTimeout);assert(!f.wait(0,0x80000003));
  auto reads=f.surface->reads.load();f.fill(0xff112233);assert(!f.scalar(12,{0}));assert(!f.scalar(12,{1}));f.expect(0xff334455);assert(f.surface->reads==reads);
  auto next=f.begin();assert(!f.submit(next));assert(!f.wait(id,0));f.expect(0xff112233);
  auto readsBefore=f.surface->reads.load();auto unchanged=f.begin();assert(!f.submit(unchanged,0,0));f.expect(0xff112233);assert(f.surface->reads==readsBefore);
  auto bg=f.begin();assert(!f.submit(bg,0x80000000,0x80000000,0xffabcdef));f.expect(0xff112233);
  auto unsupported=f.begin();assert(f.submit(unsupported,2,0)==kIOReturnUnsupported);f.expect(0xff112233);
  auto blank=f.begin();assert(!f.submit(blank,0x80000007,0x80000007,0xff000000));f.expect(0xff000000);
  auto failed=f.begin();f.surface->pixelFormat=0;assert(f.submit(failed)==kIOReturnUnsupported);assert(f.wait(failed,1)==kIOReturnUnsupported);f.expect(0xff000000);
 }
 {Fixture f;Barrier barrier;f.surface->hook=[&]{barrier.pause();};auto id=f.begin();std::atomic<uint32_t> result{123};
  std::thread producer([&]{result=f.submit(id);});barrier.wait();
  assert(f.wait(id,0x80000001)==kIOReturnBusy&&f.wait(0,0x80000002)==kIOReturnBusy);
  auto sleepsBefore=sleepCount();std::atomic<uint32_t> cancelled{123};std::thread canceller([&]{cancelled=f.scalar(52,{id});});waitSleeps(sleepsBefore+1);
  assert(cancelled==123&&f.wait(id,0x80000001)==kIOReturnBusy);barrier.release();producer.join();canceller.join();
  assert(result==kIOReturnAborted&&!cancelled&&f.wait(id,1)==kIOReturnAborted);
  assert(f.panel[0]==0xdeadbeef&&f.surface->refs==1);
 }
 {Fixture f;Barrier barrier;f.surface->hook=[&]{barrier.pause();};auto id=f.begin();std::atomic<uint32_t> result{123},waitResult{123};
  std::thread producer([&]{result=f.submit(id);});barrier.wait();auto before=sleepCount();
  std::thread waiter([&]{waitResult=f.wait(id,1);});waitSleeps(before+1);barrier.release();producer.join();waiter.join();assert(!result&&!waitResult);
 }
 {Fixture f;Barrier barrier;f.surface->hook=[&]{barrier.pause();};auto first=f.begin(),second=f.begin();
  std::atomic<uint32_t> r1{123},r2{123};std::thread p1([&]{r1=f.submit(first);});barrier.wait();auto before=sleepCount();
  std::thread p2([&]{r2=f.submit(second,0x80000007,0x80000007,0xff000000);});waitSleeps(before+1);
  assert(r2==123);barrier.release();p1.join();p2.join();assert(!r1&&!r2);f.expect(0xff000000);assert(!f.wait(first,0));}
 {Fixture f;auto id=f.begin();assert(!f.submit(id));auto before=sleepCount();std::atomic<uint32_t> waited{123};
  std::thread waiter([&]{waited=f.wait(id,0);});waitSleeps(before+1);f.driver->shutdown();waiter.join();assert(waited==kIOReturnOffline);}
 {Fixture f;auto old=f.begin();auto cutoff=f.time();auto newer=f.begin();assert(!f.scalar(81,{cutoff>>32,cutoff&0xffffffffU}));
  assert(f.submit(old)==kIOReturnAborted);assert(!f.submit(newer));}
 {Fixture f;Barrier barrier;f.surface->hook=[&]{barrier.pause();};auto id=f.begin();std::atomic<uint32_t> result{123};
  std::thread producer([&]{result=f.submit(id);});barrier.wait();f.driver->close(&f.client);barrier.release();producer.join();
  assert(result==kIOReturnAborted&&f.surface->refs==1&&f.panel[0]==0xdeadbeef);
 }
 {Fixture f;Barrier barrier;f.surface->hook=[&]{barrier.pause();};auto id=f.begin();std::atomic<uint32_t> result{123};
  std::thread producer([&]{result=f.submit(id);});barrier.wait();auto before=sleepCount();std::atomic<bool> stopped{false};
  std::thread stopper([&]{f.driver->shutdown();stopped=true;});waitSleeps(before+1);assert(!stopped);barrier.release();producer.join();stopper.join();
  assert(stopped&&result==kIOReturnAborted&&f.surface->refs==1&&f.panel[0]==0xdeadbeef);
 }
 {for(int failure=0;failure<3;++failure){AppleCLCD fb;uint32_t pixels[18]={};fb.fPixels=(uintptr_t)pixels;
   auto *loop=new IOWorkLoop;auto *driver=new SweetFBContract;assert(driver->init());allocationFailure=failure;
   assert(!driver->setup(&fb,loop));driver->release();loop->release();allocationFailure=-1;}}
 assert(liveObjects==0&&rawBlocks==0);
 puts("framebuffer contract state/owned pixels/real implementation concurrency: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='sweet-fb-contract-') as d:
 p=Path(d);(p/'IOKit').mkdir();(p/'IOKit/IOCommandGate.h').write_text('/* provided by host fixture */\n')
 (p/'test.cpp').write_text(prelude)
 command=['clang++-19','-std=c++17','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(p),'-I'+str(source),str(p/'test.cpp'),'-o',str(p/'test')]
 subprocess.run(command,check=True)
 env=dict(os.environ);env.setdefault('ASAN_OPTIONS','detect_leaks=1')
 subprocess.run([str(p/'test')],check=True,env=env,timeout=30)
