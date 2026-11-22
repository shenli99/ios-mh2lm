#!/usr/bin/env python3
"""Actual C1 clock entry/controller contract, contract-only MMIO fixtures.
Only OS allocation/locking/thread-call primitives are fixtures: the provider,
binding parser, admission walk and diagnostic backend are the shipped code.
Every case also asserts that the candidate performed zero register writes.
Usage: test-mh2lm-clock-provider.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
existing = Path(__file__).with_name('test-mh2lm-display-service.py')
ns = {'__file__': str(existing), '__name__': 'clock_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)

prelude = ns['prelude']
# The kernel allocates OSObject subclasses from zero-filled memory; a plain
# `new` in the fixture would leave provider slots uninitialised and turn a
# zero-write check into undefined behaviour. Zero the storage instead.
# OSObject allocations are zero-filled in the kernel (zalloc Z_ZERO); the
# fixture has to match or a slot field would start with host garbage.
prelude = prelude.replace('#include <algorithm>',
    '#include <algorithm>\n#include <cstdlib>\n#include <new>', 1)
prelude = prelude.replace('virtual ~OSObject(){--live;}',
    'virtual ~OSObject(){--live;}\n'
    ' static void*operator new(size_t n){void*p=std::calloc(1,n);'
    'if(!p)throw std::bad_alloc();return p;}\n'
    ' static void operator delete(void*p){std::free(p);}\n'
    ' static void operator delete(void*p,size_t){std::free(p);}')
prelude = prelude.replace('#define OSTypeAlloc(x) new x()', '#define OSTypeAlloc(x) fixtureAlloc<x>()')
prelude = prelude.replace('using IOLock=std::mutex;', 'template<class T> static T*fixtureAlloc(){return new T();}\nusing IOLock=std::mutex;')
# IOLock is not recursive in XNU. A backend or handle that re-entered a gate
# while the provider still held it must fail here deterministically instead of
# deadlocking or silently nesting.
prelude = prelude.replace('using IOLock=std::mutex;\nstatic IOLock *IOLockAlloc(){return new IOLock;}', '''
struct IOLock {
 std::mutex m;
 std::atomic<std::thread::id> holder{};
 void lock(){assert(holder.load()!=std::this_thread::get_id());m.lock();holder.store(std::this_thread::get_id());}
 void unlock(){assert(holder.load()==std::this_thread::get_id());holder.store(std::thread::id());m.unlock();}
};
static IOLock *IOLockAlloc(){return new IOLock;}''')
# Properties and names are the consumer side of the binding: the fixture must
# store them for the life of the service, exactly as IOService does.
prelude = prelude.replace('std::map<std::string,OSObject*>properties;',
    'std::map<std::string,OSObject*>properties;std::string name;')
prelude = prelude.replace('void setProperty(const char*,OSObject*){}',
    'void setProperty(const char*k,OSObject*o){if(!o)return;o->retain();'
    'auto it=properties.find(k);if(it!=properties.end())it->second->release();properties[k]=o;}')
prelude = prelude.replace('OSObject*getProperty(const char*k){return properties.count(k)?properties[k]:nullptr;}',
    'OSObject*getProperty(const char*k){return properties.count(k)?properties[k]:nullptr;}\n'
    ' const char*getName(){return name.empty()?"IOService":name.c_str();}\n'
    ' void setName(const char*n){name=n?n:"";}\n'
    ' void free()override{for(auto&p:properties)p.second->release();properties.clear();OSObject::free();}')
prelude = prelude.replace('static bool PE_parse_boot_argn(const char*k,void*v,size_t){',
    'static int clockMode=0;\nstatic bool PE_parse_boot_argn(const char*k,void*v,size_t){\n'
    ' if(!strcmp(k,"sweetclock")){*(int*)v=clockMode;return true;}')

extra = r'''
struct OSData : OSObject {
 std::vector<uint8_t> bytes;
 static OSData*withBytes(const void*p,unsigned n){auto*d=new OSData;d->bytes.assign((const uint8_t*)p,(const uint8_t*)p+n);return d;}
 const void*getBytesNoCopy(){return bytes.data();}
 unsigned getLength(){return (unsigned)bytes.size();}
};
using thread_call_param_t=void*;
using thread_call_func_t=void(*)(thread_call_param_t,thread_call_param_t);
struct FixtureThreadCall;using thread_call_t=FixtureThreadCall*;
struct FixtureThreadCall {thread_call_func_t fn;thread_call_param_t p;};
/* The diagnostic entry is an opt-in thread call; here it runs in place. */
static thread_call_t thread_call_allocate(thread_call_func_t fn,thread_call_param_t p){
 auto*c=new FixtureThreadCall;c->fn=fn;c->p=p;return c;}
static bool thread_call_cancel_wait(thread_call_t){return true;}
static void thread_call_free(thread_call_t c){delete c;}
static bool thread_call_enter(thread_call_t c){c->fn(c->p,nullptr);return false;}
'''

arm = Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
header = (arm/'IOQcomClock.h').read_text()
source = (arm/'IOQcomClock.cpp').read_text()
header = re.sub(r'^#include.*\n', '', header, flags=re.M)
# The contract classes keep their members implicitly private; the harness has to
# seed slots and observe counters, so it widens access by type, not by macro.
header = header.replace('class IOQcomClockHandle : public OSObject {',
                        'struct IOQcomClockHandle : public OSObject {')
header = header.replace('class IOQcomClockProvider : public IOService {',
                        'struct IOQcomClockProvider : public IOService {')
source = re.sub(r'^#include.*\n', '', source, flags=re.M)

tests = r'''
static unsigned writes(){unsigned n=0;for(auto&o:trace)if(o.write)++n;return n;}
static bool logged(const char*n){for(auto&m:messages)if(m.find(n)!=std::string::npos)return true;return false;}
static void setData(IOService*s,const char*k,const void*p,unsigned n){
 OSData*d=OSData::withBytes(p,n);assert(d);s->setProperty(k,d);d->release();
}
static IOService*nub(){
 auto*n=fixtureAlloc<IOService>();uint32_t abi=1;setData(n,"sweet,clock-abi",&abi,4);return n;
}
static IOQcomClockProvider*startProvider(IOService*n){
 auto*p=fixtureAlloc<IOQcomClockProvider>();assert(p->start(n));return p;
}
/* sweet,clock-names is NUL-joined UTF-8; sweet,clocks is LE u32 pairs. */
static void bindings(IOService*c,std::initializer_list<const char*>names,
 std::initializer_list<std::pair<unsigned,unsigned> >pairs){
 std::vector<unsigned char> nb;std::vector<unsigned char> pb;
 for(auto n:names){for(;*n;n++)nb.push_back((unsigned char)*n);nb.push_back(0);}
 for(auto&q:pairs){
  for(unsigned i=0;i<4;i++)pb.push_back((unsigned char)(q.first>>(8*i)));
  for(unsigned i=0;i<4;i++)pb.push_back((unsigned char)(q.second>>(8*i)));
 }
 setData(c,"sweet,clock-names",nb.data(),(unsigned)nb.size());
 setData(c,"sweet,clocks",pb.data(),(unsigned)pb.size());
}
static IOService*consumer(const char*name,bool named=true){
 auto*c=fixtureAlloc<IOService>();if(named)c->setName(name);return c;
}
static IOReturn acquire(IOQcomClockProvider*p,IOService*c,const char*n,IOQcomClockHandle**h){
 *h=nullptr;return p->acquireClock(c,n,h);
}

/* A controller owns its resource table. Tests inject descriptors, a refusal
 * result and, where the invariant is re-entrancy, an in-call hook. */
struct TestController : IOQcomClockController {
 uint32_t id=0;
 std::map<uint32_t,IOQcomClockInfo> clocks;
 int enables=0,disables=0;
 IOReturn enableResult=kIOReturnUnsupported,disableResult=kIOReturnUnsupported;
 std::function<void(uint32_t)> beforeInfo;
 std::function<void()> insideEnable;
 uint32_t clockControllerID(void)const APPLE_KEXT_OVERRIDE{return id;}
 IOReturn copyClockInfo(uint32_t localID,IOQcomClockInfo*info)APPLE_KEXT_OVERRIDE{
  if(beforeInfo)beforeInfo(localID);
  if(!info)return kIOReturnBadArgument;
  auto it=clocks.find(localID);if(it==clocks.end())return kIOReturnNotFound;
  *info=it->second;return kIOReturnSuccess;
 }
 IOReturn copyClockState(uint32_t localID,IOQcomClockState*state)APPLE_KEXT_OVERRIDE{
  if(!state)return kIOReturnBadArgument;
  auto it=clocks.find(localID);if(it==clocks.end())return kIOReturnNotFound;
  memset(state,0,sizeof *state);return kIOReturnSuccess;
 }
 IOReturn enableClock(uint32_t,uint64_t)APPLE_KEXT_OVERRIDE{
  ++enables;if(insideEnable)insideEnable();return enableResult;}
 IOReturn disableClock(uint32_t,uint64_t)APPLE_KEXT_OVERRIDE{
  ++disables;return disableResult;}
};
static TestController*addController(IOQcomClockProvider*p,uint32_t id){
 auto*c=fixtureAlloc<TestController>();c->id=id;
 assert(p->registerController(c)==kIOReturnSuccess);
 c->release();	/* the provider owns the registration reference */
 return c;
}
static void addClock(TestController*c,uint32_t lid,
 uint32_t caps=kIOQcomCapQueryConfig|kIOQcomCapQueryRate|kIOQcomCapEnable|kIOQcomCapDisable){
 IOQcomClockInfo i;
 memset(&i,0,sizeof i);
 i.controller=c->id;i.localID=lid;i.kind=kIOQcomClockRCG;i.halt=kIOQcomHaltDelay;
 i.capabilities=caps;c->clocks[lid]=i;
}
static void addParent(TestController*c,uint32_t lid,uint32_t source,uint32_t cc=0,uint32_t plid=0){
 IOQcomClockInfo &i=c->clocks[lid];
 assert(i.possibleCount<kIOQcomMaxParents);
 i.possible[i.possibleCount].source=source;
 i.possible[i.possibleCount].controller=cc;
 i.possible[i.possibleCount].localID=plid;
 ++i.possibleCount;
}
static void dropController(IOQcomClockProvider*p,TestController*c){
 assert(p->withdrawController(c)==kIOReturnSuccess);
}

int main(){
 assert(!live);
 /* ---- start/stop contract and zero writes ---- */
 {
  auto*n=fixtureAlloc<IOService>();
  auto*p=fixtureAlloc<IOQcomClockProvider>();
  assert(!p->start(nullptr));
  assert(!p->start(n));		/* no sweet,clock-abi */
  assert(!p->fReady&&!p->fLock);
  p->release();n->release();assert(!live);
 }
 {
  auto*n=nub();auto*p=startProvider(n);
  assert(p->fReady&&!p->fStopping&&p->fLock&&p->fDiagCall);
  assert(!writes()&&trace.empty());
  assert(!p->start(n));		/* second start is refused, not re-entered */
  assert(!p->fSlots[kIOQcomCCDiagnostic].active);
  assert(!logged("phase=diag"));	/* diagnostics are opt-in only */
  p->stop(n);p->stop(n);	/* idempotent */
  assert(!p->fReady&&p->fStopping&&p->fLock);
  p->release();n->release();assert(!live);
  assert(!writes());
 }
 /* ---- the entry reports what the node declares, never a capacity constant ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  assert(logged("detail=entry-ready bindings=0"));
  p->stop(n);p->release();n->release();assert(!live);
  auto*n2=nub();setData(n2,"sweet,clock-names","a\0b\0",4);
  auto*p2=startProvider(n2);
  assert(logged("detail=entry-ready bindings=2"));
  p2->stop(n2);p2->release();n2->release();assert(!live);
  assert(!writes()&&trace.empty());
 }
 /* ---- the device tree opts diagnostics in: the argument list has no room ---- */
 {
  auto*n=nub();uint32_t one=1;setData(n,"sweet,clock-diag",&one,4);
  auto*p=startProvider(n);
  assert(p->fSlots[kIOQcomCCDiagnostic].active);
  assert(logged("phase=diag-done result=0 detail=zero-writes"));
  p->stop(n);p->release();n->release();assert(!live);
  assert(!writes()&&trace.empty());
 }
 {
  /* A property that is present but unusable is a configuration error: the
   * entry warns and stays without a diagnostic backend. */
  auto*n=nub();uint16_t bad=1;setData(n,"sweet,clock-diag",&bad,2);
  auto*p=startProvider(n);
  assert(!p->fSlots[kIOQcomCCDiagnostic].active);
  assert(logged("detail=bad-diag-property"));
  p->stop(n);p->release();n->release();assert(!live);
  assert(!writes()&&trace.empty());
 }
/* ---- registration identity, collision and withdrawal ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  assert(p->registerController(nullptr)==kIOReturnBadArgument);
  auto*bad=fixtureAlloc<TestController>();bad->id=0;
  assert(p->registerController(bad)==kIOReturnBadArgument);bad->release();
  bad=fixtureAlloc<TestController>();bad->id=kIOQcomCCMax;
  assert(p->registerController(bad)==kIOReturnBadArgument);bad->release();
  auto*g=addController(p,kIOQcomCCGCC);
  auto*dup=fixtureAlloc<TestController>();dup->id=kIOQcomCCGCC;
  assert(p->registerController(dup)==kIOReturnBusy);dup->release();
  addClock(g,5);
  assert(p->withdrawController(dup)==kIOReturnNotFound);
  auto*unknown=fixtureAlloc<TestController>();unknown->id=kIOQcomCCRPMh;
  assert(p->withdrawController(unknown)==kIOReturnNotFound);unknown->release();
  assert(p->withdrawController(nullptr)==kIOReturnBadArgument);
  dropController(p,g);		/* controller is gone; never dereference g */
  auto*fresh=addController(p,kIOQcomCCGCC);
  assert(fresh!=g);
  addClock(fresh,5);
  assert(p->fSlots[kIOQcomCCGCC].generation>1);
  p->stop(n);p->release();n->release();assert(!live);
  assert(!writes());
 }
 /* ---- binding parser: declared names are the only source of truth ---- */
 {
  auto*n=nub();
  auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);addClock(g,5);
  auto*c=consumer("bind-consumer");
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnNotFound&&!h);	/* no properties */
  assert(logged("declared-bindings-unusable"));
  uint32_t pair[2]={kIOQcomCCGCC,5};
  setData(c,"sweet,clocks",pair,8);
  auto num=OSNumber::withNumber(5,32);
  c->setProperty("sweet,clock-names",num);num->release();
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument&&!h);	/* names not OSData */
  bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  num=OSNumber::withNumber(5,32);
  c->setProperty("sweet,clocks",num);num->release();
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument&&!h);	/* pairs not OSData */
  bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  assert(acquire(p,c,"",&h)==kIOReturnBadArgument);
  assert(acquire(p,c,nullptr,&h)==kIOReturnBadArgument);
  assert(acquire(p,c,"gcc6",&h)==kIOReturnNotFound&&!h);
  assert(acquire(p,nullptr,"gcc5",&h)==kIOReturnBadArgument);
  assert(p->acquireClock(c,"gcc5",nullptr)==kIOReturnBadArgument);
  /* duplicate names would alias two clocks; the whole table is refused. */
  bindings(c,{"gcc5","gcc5"},{{kIOQcomCCGCC,5},{kIOQcomCCGCC,5}});
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument&&!h);
  /* count mismatch, missing final NUL, out-of-vocabulary identity */
  bindings(c,{"gcc5","gcc5x"},{{kIOQcomCCGCC,5}});
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument);
  setData(c,"sweet,clock-names","gcc5",4);	/* no terminator */
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument);
  bindings(c,{"gcc5"},{{0,5}});
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument);
  bindings(c,{"gcc5"},{{kIOQcomCCGCC,0}});
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument);
  bindings(c,{"gcc5"},{{kIOQcomCCMax,5}});
  assert(acquire(p,c,"gcc5",&h)==kIOReturnBadArgument);
  /* 31 bytes is a name, 32 is not; nine entries exceed the table bound. */
  std::string a31(31,'a'),a32(32,'a');
  bindings(c,{a31.c_str()},{{kIOQcomCCGCC,5}});
  assert(acquire(p,c,a31.c_str(),&h)==kIOReturnSuccess&&h);h->release();
  bindings(c,{a32.c_str()},{{kIOQcomCCGCC,5}});
  assert(acquire(p,c,a32.c_str(),&h)==kIOReturnBadArgument&&!h);
  {
   std::vector<std::pair<unsigned,unsigned> > nine;
   std::initializer_list<const char*> names9={"n1","n2","n3","n4","n5","n6","n7","n8","n9"};
   bindings(c,{"n1","n2","n3","n4","n5","n6","n7","n8","n9"},
    {{kIOQcomCCGCC,5},{kIOQcomCCGCC,5},{kIOQcomCCGCC,5},{kIOQcomCCGCC,5},
     {kIOQcomCCGCC,5},{kIOQcomCCGCC,5},{kIOQcomCCGCC,5},{kIOQcomCCGCC,5},{kIOQcomCCGCC,5}});
   (void)names9;
   assert(acquire(p,c,"n9",&h)==kIOReturnBadArgument);
  }
  p->stop(n);
  p->release();
  n->release();
  c->release();
  assert(!live);
  assert(!writes());
 }
 /* ---- resolution: identity, generation, token and shutdown ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);addClock(g,5);
  auto*d=addController(p,kIOQcomCCDispCC);addClock(d,5);	/* same local ID */
  auto*c=consumer("resolve");
  bindings(c,{"gcc5","dispcc5"},{{kIOQcomCCGCC,5},{kIOQcomCCDispCC,5}});
  IOQcomClockHandle*g5=NULL,*d5=NULL;
  assert(acquire(p,c,"gcc5",&g5)==kIOReturnSuccess&&g5);
  assert(acquire(p,c,"dispcc5",&d5)==kIOReturnSuccess&&d5);
  assert(g5->token()&&d5->token()&&g5->token()!=d5->token());
  assert(g5->controller()==kIOQcomCCGCC&&d5->controller()==kIOQcomCCDispCC);
  assert(g5->localID()==5&&d5->localID()==5);
  assert(g5->generation()==p->fSlots[kIOQcomCCGCC].generation);
  IOQcomClockInfo gi,di;
  assert(g5->copyInfo(&gi)==kIOReturnSuccess&&gi.controller==kIOQcomCCGCC);
  assert(d5->copyInfo(&di)==kIOReturnSuccess&&di.controller==kIOQcomCCDispCC);
  assert(g5->copyInfo(nullptr)==kIOReturnBadArgument);
  IOQcomClockState st;
  assert(g5->copyState(&st)==kIOReturnSuccess);
  assert(g5->copyState(nullptr)==kIOReturnBadArgument);
  /* token allocation is bounded and monotone across many resolutions. */
  uint64_t last=g5->token();
  for(unsigned i=0;i<64;i++){
   IOQcomClockHandle*h=NULL;assert(acquire(p,c,"gcc5",&h)==kIOReturnSuccess);
   assert(h->token()>last);last=h->token();h->release();
  }
  assert(p->fHandles==2);
  /* withdrawal invalidates the generation, not the object. */
  dropController(p,g);
  assert(g5->copyInfo(&gi)==kIOReturnNotReady);
  assert(g5->copyState(&st)==kIOReturnNotReady);
  assert(g5->enable()==kIOReturnNotReady);
  assert(g5->disable()==kIOReturnNotReady);
  uint32_t reason=kIOQcomDepReady;
  assert(p->checkDependencies(g5,&reason)==kIOReturnNotReady);
  assert(d5->copyInfo(&di)==kIOReturnSuccess);	/* other controller unaffected */
  auto*g2=addController(p,kIOQcomCCGCC);addClock(g2,5);
  IOQcomClockHandle*n5=NULL;
  assert(acquire(p,c,"gcc5",&n5)==kIOReturnSuccess);
  assert(n5->generation()>g5->generation());
  assert(g5->copyInfo(&gi)==kIOReturnNotReady);	/* old generation stays dead */
  assert(n5->copyInfo(&gi)==kIOReturnSuccess);
  g5->release();d5->release();n5->release();
  p->stop(n);p->release();n->release();c->release();assert(!live);
  assert(!writes());
 }
 /* ---- provider lifecycle: resolve before start, after stop, after free ---- */
 {
  auto*n=nub();auto*c=consumer("early");
  auto*p=fixtureAlloc<IOQcomClockProvider>();
  IOQcomClockHandle*h=NULL;
  bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  assert(acquire(p,c,"gcc5",&h)==kIOReturnNotReady&&!h);
  assert(p->registerController(nullptr)==kIOReturnBadArgument);
  auto*early=fixtureAlloc<TestController>();early->id=kIOQcomCCGCC;
  assert(p->registerController(early)==kIOReturnNotReady);
  assert(p->withdrawController(early)==kIOReturnNotReady);	/* no lock yet */
  early->release();
  p->release();n->release();c->release();assert(!live);
 }
 {
  auto*n=nub();auto*p=fixtureAlloc<IOQcomClockProvider>();
  auto*c=consumer("stale");bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  auto*g=fixtureAlloc<TestController>();g->id=kIOQcomCCGCC;addClock(g,5);
  assert(!p->start(nullptr));
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnNotReady);
  g->release();
  p->release();n->release();c->release();assert(!live);
 }
 /* ---- admission: reasons, shape and ordering, all without a write ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);
  auto*c=consumer("admit");
  /* malformed descriptor: unknown kind / wrong identity / no query capability */
  g->clocks[10]={};auto&bad=g->clocks[10];
  bad.controller=g->id;bad.localID=10;bad.kind=9;bad.halt=kIOQcomHaltDelay;
  bad.capabilities=kIOQcomCapQueryConfig;
  g->clocks[11]={};auto&wrong=g->clocks[11];
  wrong.controller=kIOQcomCCDispCC;wrong.localID=11;wrong.kind=kIOQcomClockRCG;
  wrong.halt=kIOQcomHaltDelay;wrong.capabilities=kIOQcomCapQueryConfig;
  g->clocks[12]={};auto&nq=g->clocks[12];
  nq.controller=g->id;nq.localID=12;nq.kind=kIOQcomClockRCG;
  nq.halt=kIOQcomHaltDelay;nq.capabilities=kIOQcomCapEnable;	/* no query */
  addClock(g,13,kIOQcomCapQueryConfig);	/* queryable, but never enableable */
  bindings(c,{"c10","c11","c12","c13"},
   {{kIOQcomCCGCC,10},{kIOQcomCCGCC,11},{kIOQcomCCGCC,12},{kIOQcomCCGCC,13}});
  IOQcomClockHandle*h=NULL;uint32_t reason=kIOQcomDepReady;
  assert(acquire(p,c,"c10",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnBadArgument&&reason==kIOQcomDepReady);
  assert(p->checkDependencies(h,&reason)==kIOReturnBadArgument);
  assert(reason==kIOQcomDepMalformed);
  assert(g->enables==0);
  h->release();
  assert(acquire(p,c,"c11",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnBadArgument);
  assert(p->checkDependencies(h,&reason)==kIOReturnBadArgument&&reason==kIOQcomDepMalformed);
  h->release();
  assert(acquire(p,c,"c12",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnBadArgument);	/* descriptor needs query */
  h->release();
  assert(acquire(p,c,"c13",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnSuccess&&reason==kIOQcomDepReady);
  assert(h->enable()==kIOReturnUnsupported&&g->enables==0);	/* capability gate */
  h->release();
  /* register-access dependency names a controller that is absent */
  addClock(g,20);g->clocks[20].accessDeps=1u<<kIOQcomCCDispCC;
  bindings(c,{"c20"},{{kIOQcomCCGCC,20}});
  assert(acquire(p,c,"c20",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnNotReady);
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepAccess);
  h->release();
  /* required supply domain and unknown parent source */
  addClock(g,21);g->clocks[21].domain=kIOQcomDomainCX;
  addClock(g,22);addParent(g,22,kIOQcomSourceUnknown);
  bindings(c,{"c21","c22"},{{kIOQcomCCGCC,21},{kIOQcomCCGCC,22}});
  assert(acquire(p,c,"c21",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepDomain);
  assert(h->enable()==kIOReturnNotReady);
  h->release();
  assert(acquire(p,c,"c22",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepUnknownParent);
  h->release();
  /* a local parent's controller is missing, then its clock is missing */
  addClock(g,23);addParent(g,23,kIOQcomSourceLocal,kIOQcomCCRPMh,7);
  addClock(g,24);addParent(g,24,kIOQcomSourceLocal,kIOQcomCCGCC,0x99);
  bindings(c,{"c23","c24"},{{kIOQcomCCGCC,23},{kIOQcomCCGCC,24}});
  assert(acquire(p,c,"c23",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepParentController);
  h->release();
  assert(acquire(p,c,"c24",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepParentClock);
  h->release();
  /* cycles are refused, including a self-parent */
  addClock(g,30);addParent(g,30,kIOQcomSourceLocal,kIOQcomCCGCC,30);
  addClock(g,31);addClock(g,32);
  addParent(g,31,kIOQcomSourceLocal,kIOQcomCCGCC,32);
  addParent(g,32,kIOQcomSourceLocal,kIOQcomCCGCC,31);
  bindings(c,{"c30","c31"},{{kIOQcomCCGCC,30},{kIOQcomCCGCC,31}});
  assert(acquire(p,c,"c30",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnBadArgument&&reason==kIOQcomDepCycle);
  h->release();
  assert(acquire(p,c,"c31",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnBadArgument&&reason==kIOQcomDepCycle);
  h->release();
  /* a chain longer than the walk bound is Depth, not a hang */
  for(unsigned i=40;i<52;i++){addClock(g,(uint32_t)i);
   if(i<51)addParent(g,(uint32_t)i,kIOQcomSourceLocal,kIOQcomCCGCC,(uint32_t)i+1);}
  bindings(c,{"c40"},{{kIOQcomCCGCC,40}});
  assert(acquire(p,c,"c40",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnBadArgument&&reason==kIOQcomDepDepth);
  h->release();
  /* admitted clock reaches the backend, whose refusal is reported verbatim */
  addClock(g,50);g->enableResult=kIOReturnIOError;
  bindings(c,{"c50"},{{kIOQcomCCGCC,50}});
  assert(acquire(p,c,"c50",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnIOError&&g->enables==1);
  assert(logged("phase=enable result="));
  /* pinned clocks never turn off; a pinned disable is a refusal, not a vote */
  g->clocks[50].pinned=true;g->disableResult=kIOReturnSuccess;
  assert(h->disable()==kIOReturnNotPermitted);
  assert(g->disables==0);
  /* disable is a requirement removal: `admit` called from `disable` uses
   * requirements=false, so a satisfied deps clock reaches the backend. */
  g->clocks[50].pinned=false;g->clocks[50].domain=kIOQcomDomainCX;
  assert(h->disable()==kIOReturnSuccess&&g->disables==1);
  assert(logged("phase=disable result=0"));
  h->release();
  /* admission precedes the capability gate: a declared dependency that is
   * missing reports NotReady even when the descriptor claims Enable. */
  addClock(g,51,kIOQcomCapQueryConfig|kIOQcomCapEnable);
  g->clocks[51].domain=kIOQcomDomainCX;
  bindings(c,{"c51"},{{kIOQcomCCGCC,51}});
  assert(acquire(p,c,"c51",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnNotReady&&g->enables==1);
  h->release();
  p->stop(n);p->release();n->release();c->release();assert(!live);
  assert(!writes());
 }
 /* ---- callbacks run without the provider gate held ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);addClock(g,5);
  auto*c=consumer("reenter");
  bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnSuccess);
  /* copyClockInfo is called by admit while resolving; re-resolving there must
   * not find the non-recursive gate already held. */
  TestController *gp=g;IOQcomClockProvider *pp=p;IOService *cc=c;
  int hookAcquires=0;
  g->beforeInfo=[&](uint32_t){
   if(hookAcquires)return;
   hookAcquires=1;
   IOQcomClockHandle*inner=NULL;
   assert(acquire(pp,cc,"gcc5",&inner)==kIOReturnSuccess);
   inner->release();
  };
  assert(h->enable()==kIOReturnUnsupported&&hookAcquires==1);
  g->beforeInfo=nullptr;
  /* Withdrawing the controller from inside its own backend must not free the
   * object under the running call: enable holds a retain across the call. */
  int selfWithdraw=0;
  g->insideEnable=[&]{++selfWithdraw;assert(pp->withdrawController(gp)==kIOReturnSuccess);};
  g->enableResult=kIOReturnSuccess;
  assert(h->enable()==kIOReturnSuccess&&selfWithdraw==1);
  assert(h->copyInfo(nullptr)==kIOReturnBadArgument);
  {IOQcomClockInfo i;assert(h->copyInfo(&i)==kIOReturnNotReady);}
  h->release();
  auto*g2=addController(p,kIOQcomCCGCC);addClock(g2,5);
  IOQcomClockHandle*next=NULL;
  assert(acquire(p,c,"gcc5",&next)==kIOReturnSuccess);
  next->release();
  p->stop(n);p->release();n->release();c->release();assert(!live);
  assert(!writes());
 }
 /* ---- capability vocabulary is bounded ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);
  addClock(g,5,kIOQcomCapQueryConfig|(1u<<20));
  auto*c=consumer("caps");bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnSuccess);
  assert(h->enable()==kIOReturnBadArgument);
  h->release();
  p->stop(n);p->release();n->release();c->release();assert(!live);
 }
 /* ---- opt-in diagnostics: registered, queryable, still zero writes ---- */
 {
  auto*n=nub();auto*p=fixtureAlloc<IOQcomClockProvider>();
  clockMode=1;
  assert(p->start(n));
  assert(p->fSlots[kIOQcomCCDiagnostic].active);
  assert(logged("detail=backend-registered"));
  assert(logged("phase=diag-done result=0 detail=zero-writes"));
  auto*c=consumer("diag-reader");
  bindings(c,{"diag-core","diag-child","diag-orphan","diag-domain"},
   {{kIOQcomCCDiagnostic,1},{kIOQcomCCDiagnostic,2},
    {kIOQcomCCDiagnostic,3},{kIOQcomCCDiagnostic,4}});
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"diag-core",&h)==kIOReturnSuccess);
  IOQcomClockInfo i;IOQcomClockState s;
  assert(h->copyInfo(&i)==kIOReturnSuccess);
  assert(i.controller==kIOQcomCCDiagnostic&&i.localID==1);
  assert(i.capabilities==(kIOQcomCapQueryConfig|kIOQcomCapQueryRate));
  assert(h->copyState(&s)==kIOReturnSuccess&&s.selected.source==kIOQcomSourceXO);
  assert(h->enable()==kIOReturnUnsupported);	/* no Enable bit: no backend */
  h->release();
  assert(acquire(p,c,"diag-child",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,NULL)==kIOReturnSuccess);	/* parent is diag-core */
  assert(h->enable()==kIOReturnUnsupported);	/* admitted, backend has no write */
  h->release();
  assert(acquire(p,c,"diag-orphan",&h)==kIOReturnSuccess);
  uint32_t reason=kIOQcomDepReady;
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepParentController);
  h->release();
  assert(acquire(p,c,"diag-domain",&h)==kIOReturnSuccess);
  assert(p->checkDependencies(h,&reason)==kIOReturnNotReady&&reason==kIOQcomDepDomain);
  assert(h->enable()==kIOReturnNotReady);
  h->release();
  clockMode=0;
  p->stop(n);p->release();n->release();c->release();assert(!live);
  assert(!writes()&&trace.empty());
 }
 /* ---- teardown: live handles, provider free without stop, consumer cycle ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);addClock(g,5);
  auto*c=consumer("teardown");bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnSuccess);
  assert(p->fHandles==1);
  /* A surviving handle keeps the provider alive across a bare detach; the
   * consumer is released by the handle and by the provider's own reference. */
  p->release();		/* provider still retained by the handle */
  p->stop(n);
  assert(h->copyInfo(nullptr)==kIOReturnBadArgument);
  h->release();
  n->release();c->release();
  assert(!live);	/* provider, controller, consumer and data all released */
  assert(!writes());
 }
 /* ---- stop drops registrations; a fresh provider starts from generation 1 ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);addClock(g,5);
  auto*c=consumer("stop");bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  p->stop(n);		/* stop drops the registration, so g is gone */
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnNotReady&&!h);
  auto*fresh=fixtureAlloc<TestController>();fresh->id=kIOQcomCCGCC;
  assert(p->registerController(fresh)==kIOReturnNotReady);
  fresh->release();
  p->release();n->release();c->release();assert(!live);
  auto*n2=nub();auto*p2=startProvider(n2);
  assert(p2->fSlots[kIOQcomCCGCC].generation==0);
  auto*g2=addController(p2,kIOQcomCCGCC);
  assert(p2->fSlots[kIOQcomCCGCC].generation==1);
  assert(g2->clockControllerID()==kIOQcomCCGCC);
  p2->stop(n2);p2->release();n2->release();assert(!live);
  assert(!writes());
 }
 /* ---- token wrap keeps tokens non-zero and unique per handle ---- */
 {
  auto*n=nub();auto*p=startProvider(n);
  auto*g=addController(p,kIOQcomCCGCC);addClock(g,5);
  auto*c=consumer("tokens");bindings(c,{"gcc5"},{{kIOQcomCCGCC,5}});
  p->fNextToken=~0ULL;
  IOQcomClockHandle*h=NULL;
  assert(acquire(p,c,"gcc5",&h)==kIOReturnSuccess);
  assert(h->token()==1&&p->fNextToken==1);
  h->release();
  p->stop(n);p->release();n->release();c->release();assert(!live);
 }
 assert(!live&&rawBlocks==0);
 printf("PASS: actual C1 clock entry; opt-in ADT-matched lifecycle, registration/collision/generation, "
        "read-only name-to-handle binding with 13 malformed-binding refusals, admission reasons "
        "(absent controller/clock, domain, access, unknown parent, cycle, self-parent, depth), "
        "admission-before-capability ordering, pinned-disable refusal, backend propagation, "
        "gate-free re-entrancy and self-withdraw, bounded token wrap, idempotent stop, "
        "generation restart, opt-in diagnostics from the argument list or the device tree, "
        "truthful declared-binding count, zero MMIO writes\n");
 return 0;
}
'''


with tempfile.TemporaryDirectory(prefix='mh2lm-clock-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+extra+header+source+tests)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path(temp, 'kept.cpp').write_text(cpp.read_text())
        Path('/tmp/mh2lm-clock-kept.cpp').write_text(cpp.read_text())
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)