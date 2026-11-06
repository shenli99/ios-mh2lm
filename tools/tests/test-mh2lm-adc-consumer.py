#!/usr/bin/env python3
"""Exercise the actual ADC registry client with a typed provider/scheduler model.
ASAN/UBSAN covers reference lifetimes; this is not a native IOKit lock proof.
Usage: test-mh2lm-adc-consumer.py EXTRACTED_XNU
"""
from pathlib import Path
import os, re, subprocess, sys, tempfile
root=Path(sys.argv[1])
def strip(s): return re.sub(r'^#include .*$', '', s, flags=re.M)
header=strip((root/'bsd/dev/sweet_adc5_client.h').read_text())
source=strip((root/'iokit/Kernel/arm/SweetAdc5Client.cpp').read_text())
adc=strip((root/'bsd/dev/sweet_adc5.h').read_text())
fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <algorithm>
#define ARM64_BOARD_CONFIG_MH2LM 1
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define APPLE_KEXT_OVERRIDE override
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
#define OSSafeReleaseNULL(p) do { if(p){(p)->release();(p)=nullptr;} } while(0)
#define SWEET_LOG_INFO(...) ((void)0)
#define SWEET_LOG_WARN(...) ((void)0)
#define SWEET_LOG_ERROR(...) ((void)0)
using IOReturn=int;using UInt32=unsigned;using vm_size_t=size_t;
constexpr int kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnIOError=2,kIOReturnBusy=3;
static std::atomic<int> liveObjects{0};
struct OSObject {
 std::atomic<int> refs{1};OSObject(){++liveObjects;}virtual ~OSObject(){--liveObjects;}
 virtual bool init(){return true;}virtual void free(){}
 void retain(){assert(refs.fetch_add(1)>0);}void release(){if(refs.fetch_sub(1)==1){free();delete this;}}
};
using IOLock=std::mutex;
static IOLock *IOLockAlloc(){return new IOLock;}static void IOLockFree(IOLock *p){delete p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
struct IOInterruptEventSource;
struct IOWorkLoop:OSObject {
 std::recursive_mutex gate;
 using Action=IOReturn(*)(OSObject *,void *,void *,void *,void *);
 int addEventSource(IOInterruptEventSource *);void removeEventSource(IOInterruptEventSource *);
 int runAction(Action a,OSObject *o){std::lock_guard<std::recursive_mutex> g(gate);return a(o,nullptr,nullptr,nullptr,nullptr);}
};
static std::vector<IOInterruptEventSource *> queue;
static std::mutex queueLock;
static unsigned failAdd;
struct IOInterruptEventSource:OSObject {
 using Action=void(*)(OSObject *,IOInterruptEventSource *,int);
 OSObject *owner;Action action;IOWorkLoop *loop=nullptr;bool enabled=true;
 IOInterruptEventSource(OSObject *o,Action a):owner(o),action(a){}
 static IOInterruptEventSource *interruptEventSource(OSObject *o,Action a,void *,int){return new IOInterruptEventSource(o,a);}
 void disable(){enabled=false;}void enable(){enabled=true;}
 void interruptOccurred(void *,void *,int){std::lock_guard<std::mutex> g(queueLock);queue.push_back(this);}
};
int IOWorkLoop::addEventSource(IOInterruptEventSource *e){if(failAdd && !--failAdd)return 1;e->loop=this;return 0;}
void IOWorkLoop::removeEventSource(IOInterruptEventSource *e){std::lock_guard<std::mutex> g(queueLock);e->loop=nullptr;queue.erase(std::remove(queue.begin(),queue.end(),e),queue.end());}
static void pump(){
 for(unsigned count=0;;++count){
  assert(count<100);IOInterruptEventSource *e;
  {std::lock_guard<std::mutex> g(queueLock);if(queue.empty())return;e=queue.front();queue.erase(queue.begin());e->retain();}
  if(e->loop){std::lock_guard<std::recursive_mutex> g(e->loop->gate);if(e->enabled)e->action(e->owner,e,1);}
  e->release();
 }
}
static uint64_t fakeNs=1000000000ULL;
static void clock_get_uptime(uint64_t *p){*p=fakeNs;}static void absolutetime_to_nanoseconds(uint64_t a,uint64_t *p){*p=a;}
struct IOService;struct IONotifier;
using MatchHandler=bool(*)(void *,void *,IOService *,IONotifier *);
using InterestHandler=IOReturn(*)(void *,void *,UInt32,IOService *,void *,vm_size_t);
static const char *gIOFirstMatchNotification="match",*gIOTerminatedNotification="term",*gIOGeneralInterest="interest";
constexpr unsigned kIOMessageServicePropertyChange=1;
static std::recursive_mutex registryLock;
static std::vector<IONotifier *> notices;
static std::vector<IOService *> services;
static unsigned failNotice;
struct OSDictionary:OSObject{};
struct IONotifier:OSObject{
 const char *kind;MatchHandler match=nullptr;InterestHandler interest=nullptr;void *target;IOService *service=nullptr;
 void remove(){std::lock_guard<std::recursive_mutex> g(registryLock);notices.erase(std::remove(notices.begin(),notices.end(),this),notices.end());release();}
};
struct IOService:OSObject {
 std::atomic<bool> inactive{false};bool isInactive(){return inactive;}
 static OSDictionary *serviceMatching(const char *){return new OSDictionary;}
 static IONotifier *addMatchingNotification(const char *k,OSDictionary *,MatchHandler fn,void *target){
  std::lock_guard<std::recursive_mutex> g(registryLock);
  if(failNotice && !--failNotice)return nullptr;
  auto *n=new IONotifier;n->kind=k;n->match=fn;n->target=target;notices.push_back(n);
  for(auto *s:services)if(k==gIOFirstMatchNotification)fn(target,nullptr,s,n);
  return n;
 }
 IONotifier *registerInterest(const char *k,InterestHandler fn,void *target){
  std::lock_guard<std::recursive_mutex> g(registryLock);if(failNotice && !--failNotice)return nullptr;
  auto *n=new IONotifier;n->kind=k;n->interest=fn;n->target=target;n->service=this;notices.push_back(n);return n;
 }
 void messageClients(unsigned type){std::lock_guard<std::recursive_mutex> g(registryLock);for(auto *n:notices)if(n->interest && n->service==this)n->interest(n->target,nullptr,type,this,nullptr,0);}
 void publish(){std::lock_guard<std::recursive_mutex> g(registryLock);services.push_back(this);for(auto *n:notices)if(n->kind==gIOFirstMatchNotification)n->match(n->target,nullptr,this,n);}
 void terminate(){std::lock_guard<std::recursive_mutex> g(registryLock);inactive=true;services.erase(std::remove(services.begin(),services.end(),this),services.end());for(auto *n:notices)if(n->kind==gIOTerminatedNotification)n->match(n->target,nullptr,this,n);}
};
'''
provider=r'''
struct SweetPmicAdc5:IOService {
 bool ready=true,failed=false,active=false,busyExternal=false;
 unsigned sequence=0,begins=0;uint64_t start=0;unsigned channel=0;bool valid[2]={};sweet_adc5_measurement samples[2]={};
 IOReturn status(){return failed?kIOReturnIOError:ready?kIOReturnSuccess:kIOReturnNotReady;}
 IOReturn requestSample(sweet_adc5_channel c,unsigned *token){
  assert(ready && !failed);if(active || busyExternal)return kIOReturnBusy;
  active=true;channel=c;start=fakeNs;valid[c-1]=false;++begins;*token=++sequence;return 0;
 }
 IOReturn copySample(sweet_adc5_channel c,sweet_adc5_measurement *out){if(!valid[c-1])return kIOReturnNotReady;*out=samples[c-1];return 0;}
 void complete(){assert(active);active=false;auto &s=samples[channel-1];s={};s.channel=channel;s.sequence=sequence;s.started_ns=start;s.completed_ns=fakeNs+1000000ULL;fakeNs=s.completed_ns;s.microvolts=channel==1?889000:4900000;valid[channel-1]=true;messageClients(1);}
 void fail(){failed=true;active=false;valid[0]=valid[1]=false;messageClients(1);}
};
static unsigned callbacks;
static void changed(void *){++callbacks;}
'''
checks=r'''
static void check(unsigned mode){
 assert(!liveObjects && notices.empty() && services.empty() && queue.empty());
 callbacks=0;fakeNs=1000000000ULL;
 auto *loop=new IOWorkLoop;auto *p=new SweetPmicAdc5;
 if(mode!=1)p->publish();
 if(mode==9)failAdd=1;
 if(mode==10)failNotice=1;
 if(mode==11)failNotice=2;
 auto *c=SweetAdc5Client::withOwner(loop,changed,loop);
 if(mode>=9 && mode<=11){assert(!c);goto cleanup;}
 assert(c);
 if(mode==1){pump();assert(!p->begins);p->publish();}
 if(mode==2)p->ready=false;
 if(mode==3)p->busyExternal=true;
 if(mode==12)failNotice=1;
 c->update(true,true,false,fakeNs);pump();
 if(mode==12){assert(!p->begins);c->stop();c->release();goto cleanup;}
 if(mode==2 || mode==3){
  assert(!p->begins);for(unsigned n=0;n<10;++n){c->update(true,false,false,fakeNs);pump();}assert(!p->begins);
  p->ready=true;p->busyExternal=false;p->messageClients(1);pump();
 }
 if(mode==13){c->stop();assert(p->active);p->complete();pump();assert(!callbacks);c->release();goto cleanup;}
 assert(p->begins==1 && p->channel==1);p->complete();pump();
 assert(p->begins==2 && p->channel==2);
 {sweet_adc5_measurement s{};assert(c->copyFresh(SWEET_ADC5_BAT_ID,fakeNs,&s) && s.microvolts==889000);}
 if(mode==4 || mode==5){
  // A source epoch changes while the old EOC is still in flight.
  c->update(false,true,false,fakeNs);
  if(mode==5)c->update(true,true,false,fakeNs);
 }
 p->complete();pump();
 if(mode==5){assert(p->begins==3 && p->active);p->complete();pump();}
 {sweet_adc5_measurement s{};assert(c->copyFresh(SWEET_ADC5_USBIN,fakeNs,&s)==(mode!=4));}
 if(mode==6){
  fakeNs+=3000000001ULL;sweet_adc5_measurement s{};s.raw=0xcafe;
  assert(!c->copyFresh(SWEET_ADC5_USBIN,fakeNs,&s) && s.raw==0xcafe);
  assert(!c->copyFresh(SWEET_ADC5_BAT_ID,1,&s));
  c->update(true,false,false,fakeNs);pump();assert(p->active);p->complete();pump();assert(c->copyFresh(SWEET_ADC5_USBIN,fakeNs,&s));
  fakeNs+=60000000001ULL;assert(!c->copyFresh(SWEET_ADC5_BAT_ID,fakeNs,&s));
  c->update(true,false,false,fakeNs);pump();assert(p->channel==1);
 }
 if(mode==7){p->fail();pump();sweet_adc5_measurement s{};assert(!c->copyFresh(SWEET_ADC5_BAT_ID,fakeNs,&s));unsigned n=p->begins;c->update(true,true,true,fakeNs);pump();assert(p->begins==n);}
 if(mode==8){p->terminate();sweet_adc5_measurement s{};assert(!c->copyFresh(SWEET_ADC5_USBIN,fakeNs,&s));pump();}
 if(mode==16){p->ready=false;p->active=false;p->messageClients(1);pump();sweet_adc5_measurement s{};assert(!c->copyFresh(SWEET_ADC5_BAT_ID,fakeNs,&s));assert(!c->copyFresh(SWEET_ADC5_USBIN,fakeNs,&s));}
 if(mode==14){std::thread t([&]{for(unsigned n=0;n<100;++n)p->messageClients(1);});c->stop();t.join();}
 if(mode==15){std::thread t([&]{p->terminate();});c->stop();t.join();}
 c->stop();c->stop();c->release();pump();
 cleanup:
 if(!p->isInactive())p->terminate();p->release();loop->release();
 assert(!liveObjects && notices.empty() && services.empty() && queue.empty());
}
int main(){for(unsigned i=0;i<17;++i)check(i);for(unsigned i=0;i<50;++i){check(14);check(15);}puts("PASS: ADC consumer 17 scenarios and 100 notification/termination stop races");}
'''
with tempfile.TemporaryDirectory(prefix='adc-consumer-') as d:
 src=Path(d)/'test.cpp';binary=Path(d)/'test'
 src.write_text(fixture+adc+provider+header+source+checks)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-pthread','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
