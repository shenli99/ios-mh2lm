#!/usr/bin/env python3
"""Actual C2-01/C2-02/C2-08 GCC backend against contract-only MMIO fixtures.
Only OS allocation/locking/thread-call primitives and the device memory backing
store are fixtures: the shipped IOQcomGCC backend is compiled as written, with
its two register accessors routed to the fixture register file. The structural
checks pin the source to one load and one barriered store, and the functional
cases prove that the default (gate absent) image still performs zero writes
while the C2-08 candidate stores, measures, restores and settles its ledger.
Usage: test-mh2lm-gcc-backend.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
# Reuse the display-service primitives and every C1 provider fixture patch by
# executing the clock provider test up to the point where it reads the tree.
existing = Path(__file__).with_name('test-mh2lm-clock-provider.py')
ns = {'__file__': str(existing), '__name__': 'gcc_fixture', '__doc__': ''}
exec(compile(existing.read_text().split('arm = Path(sys.argv[1])')[0], str(existing), 'exec'), ns)

prelude = ns['prelude']
extra = ns['extra']

# The diagnostic entry is a 5 s delayed thread call on the device. The fixture
# queues it so a case can register a backend first and then run the listing,
# which is the ordering the delay exists for.
extra = extra.replace('''/* The diagnostic entry is armed with a delay so a backend that starts later
   can register first; this fixture runs it in place, which is enough here. */
static bool thread_call_enter_delayed(thread_call_t c,uint64_t){c->fn(c->p,nullptr);return false;}''',
'''/* The diagnostic entry is a 5 s delayed thread call on the device. Queue it
   so a case can register a real backend first and then run the listing. */
static bool thread_call_enter_delayed(thread_call_t c,uint64_t){delayedCalls.push_back(c);return false;}
static void fireDelayed(){auto list=delayedCalls;delayedCalls.clear();
 for(auto*c:list)c->fn(c->p,nullptr);}''')
extra = extra.replace('static void thread_call_free(thread_call_t c){delete c;}',
'''/* queued delayed calls: freed entries must not stay reachable */
static std::vector<FixtureThreadCall*> delayedCalls;
static void thread_call_free(thread_call_t c){
 delayedCalls.erase(std::remove(delayedCalls.begin(),delayedCalls.end(),c),delayedCalls.end());
 delete c;}''')
assert 'delayedCalls' in extra

# C2-08 needs a device model for the debug counter: the fixture reads of the
# measurement status register can be computed from the register file, exactly
# as the clock's own edges would be.
prelude = prelude.replace('static uint32_t readReg(uintptr_t b,uint32_t o){',
 'static std::function<uint32_t(uintptr_t,uint32_t)> gccMeasureHook;\n'
 '/* Device model for the two restore subtleties: a branch CBCR reports its\n'
 ' * halt status one read after the disable that cleared it (0127 read back\n'
 ' * 0x0 for a stored 0x80000000), and a register can hold a wrong config bit. */\n'
 'static bool gccHaltLag=false,gccNeighbourShift=false,gccDisableNeighbourShift=false;\n'
 '/* a store withdrew an enable: a never-halting branch can be modelled only\n'
 ' * after the driver itself disabled it, never on the first read of a run */\n'
 'static bool gccDisableStored=false;\n'
 'static std::map<uintptr_t,bool> gccHaltPending;\n'
 'static uintptr_t gccDriftPA=0;\n'
 'static uint32_t gccDriftBits=0;\n'
 'static uint32_t readReg(uintptr_t b,uint32_t o){')
prelude = prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){',
 '''static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
  /* Disabling a branch arms one lagging halt status: the 0127 ring read back
     0x0 from GCC_DEBUG_CBCR although 0x80000000 had been stored. */
  {uintptr_t pa=b+o;
   if(gccHaltLag&&(pa==0x00143008u||pa==0x00162008u)&&!(v&1u))gccHaltPending[pa]=true;
   /* A write that also moves a neighbour: the C2-03 proof must notice. */
   if(gccNeighbourShift&&pa==0x00133008u&&(v&1u))regs[0x00133014u]^=0x10u;
   /* The same trick for the disable store: a way for the C2-04 neighbour proof
      to see a change the cleared enable bit alone would not explain. */
   if(gccDisableNeighbourShift&&pa==0x00133008u&&!(v&1u))regs[0x00133014u]^=0x10u;
   /* remember that an enable was withdrawn, for the never-halts model */
   if(pa==0x00133008u&&!(v&1u))gccDisableStored=true;}''')
prelude = prelude.replace('''uint32_t v=regs[b+o];if(b==bases[6]&&o==0x110&&dmaFired)''',
 '''uint32_t v=regs[b+o];
 if(gccDriftPA&&b+o==gccDriftPA)v^=gccDriftBits;	/* a configuration bit stuck */
 if(gccHaltPending[b+o]){gccHaltPending[b+o]=false;v&=~0x80000000u;}	/* halt status still clearing */
 if(gccMeasureHook)v=gccMeasureHook(b+o,v);
 if(b==bases[6]&&o==0x110&&dmaFired)''')
assert 'gccMeasureHook' in prelude and 'gccHaltPending' in prelude

# The GCC backend maps four consecutive ADT windows; the fixture provides a
# host-backed page per window and knobs for the four start-time refusals.
prelude = prelude.replace('static std::vector<unsigned> mapCalls;',
'''static std::vector<unsigned> mapCalls;
using IOPhysicalAddress=uint64_t;using IOVirtualAddress=uintptr_t;using IOByteCount=uint64_t;
static constexpr uintptr_t gccBases[4]={0x0010b000,0x00133000,0x00143000,0x00162000};
static constexpr size_t gccLengths[4]={0x1000,0x1000,0x1000,0x1000};
static bool gccMode=true;
static unsigned gccMemoryCount=4,gccAbsentIndex=99,gccWrongPA=99,gccShortLen=99,gccNoBase=99;''')
prelude = prelude.replace('int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){',
'''int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){
 if(gccMode){mapCalls.push_back(i);
  if(i>=gccMemoryCount||i==gccAbsentIndex)return nullptr;
  auto*g=new IOMemoryMap(i);
  g->customPA=gccBases[i]+(i==gccWrongPA?4:0);
  g->customSize=gccLengths[i]-(i==gccShortLen?1:0);
  return g;}''')
prelude = prelude.replace('uintptr_t getVirtualAddress(){return customPA?customPA:bases[index];}',
'uintptr_t getVirtualAddress(){if(gccMode&&index==gccNoBase)return 0;return customPA?customPA:bases[index];}')
assert 'gccMemoryCount' in prelude

# The measurement candidate bounds its counter poll with IODelay; the fixture
# records the call so a timeout case can prove the loop is bounded.
prelude += '''
/* The device boots at log level 0, so DEBUG rows never reach the ring: model that. */
#define SWEET_LOG_DEBUG(...) ((void)0)
static unsigned gccDelayCalls;
static void IODelay(unsigned us){(void)us;++gccDelayCalls;}
'''

arm = Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
header = re.sub(r'^#include.*\n', '', (arm/'IOQcomClock.h').read_text(), flags=re.M)
gcc_header = re.sub(r'^#include.*\n', '', (arm/'IOQcomGCC.h').read_text(), flags=re.M)
# The contract classes keep their members implicitly private; the harness seeds
# windows and inspects counters, so it widens access by type, not by macro.
header = header.replace('class IOQcomClockHandle : public OSObject {',
                        'struct IOQcomClockHandle : public OSObject {')
header = header.replace('class IOQcomClockProvider : public IOService {',
                        'struct IOQcomClockProvider : public IOService {')
gcc_header = gcc_header.replace('class IOQcomGCC : public IOQcomClockController {',
                                'struct IOQcomGCC : public IOQcomClockController {')
assert 'struct IOQcomGCC' in gcc_header

provider = re.sub(r'^#include.*\n', '', (arm/'IOQcomClock.cpp').read_text(), flags=re.M)
raw = (arm/'IOQcomGCC.cpp').read_text()
# Structural evidence: exactly one device memory load and exactly one store,
# both inside their accessor, and the store sits between its barriers in the
# order the driver contract requires.
assert raw.count('*(volatile uint32_t *)')==2, raw.count('*(volatile uint32_t *)')
assert raw.count('=*(volatile uint32_t *)(w.base+offset);')==1
assert raw.count('*(volatile uint32_t *)(w.base+offset)=value;')==1
assert '''		__asm__ volatile("dmb oshst" ::: "memory");
		*(volatile uint32_t *)(w.base+offset)=value;
		__asm__ volatile("dsb osh" ::: "memory");''' in raw
assert raw.count('__asm__ volatile("dmb oshld" ::: "memory");')==1
assert raw.count('__asm__ volatile("dmb oshst" ::: "memory");')==1
assert raw.count('__asm__ volatile("dsb osh" ::: "memory");')==1
assert raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
assert raw.count('#endif /* ARM64_BOARD_CONFIG_MH2LM */')==1
# The measurement candidate is opt-in: the gate property is the only way in.
assert raw.count('sweet,clock-measure')==1
assert raw.count('sweet,clock-gate')==1
# C2-04 adds exactly one more opt-in, and it is never read on its own.
assert raw.count('sweet,clock-disable')==1
assert 'fMeasureEnabled' in raw and 'fGateEnabled' in raw and 'gate-enable' in raw
assert 'fDisableEnabled' in raw and 'disableClocks' in raw and 'stage=disable' in raw
# The retraction withdraws the enable bit and nothing else, exactly once: the
# recovery is the C2-03 store, not a second private writer.
assert raw.count('saved&~kGccBranchEnable')==1
assert raw.count('stage=disable')==1
gcc_source = re.sub(r'^#include.*\n', '', raw, flags=re.M)
# CPU barriers and actual MMIO are the only replaced engine statements.
gcc_source = gcc_source.replace('observed=*(volatile uint32_t *)(w.base+offset);',
                                'observed=readReg(w.base,offset);')
gcc_source = gcc_source.replace('*(volatile uint32_t *)(w.base+offset)=value;',
                                'writeReg(w.base,offset,value);')
gcc_source = gcc_source.replace('__asm__ volatile("dmb oshld" ::: "memory");','(void)0;')
gcc_source = gcc_source.replace('__asm__ volatile("dmb oshst" ::: "memory");','(void)0;')
gcc_source = gcc_source.replace('__asm__ volatile("dsb osh" ::: "memory");','(void)0;')
# Two translation units in the kernel, one in this harness: rename the shared
# file-scope enum tag and reset the `super` macro between them.
gcc_source = gcc_source.replace('kIOQcomLogBuf', 'kIOQcomGccLogBuf')
gcc_source = '#undef super\n' + gcc_source
assert 'readReg(w.base,offset)' in gcc_source and 'volatile uint32_t *)' not in gcc_source

tests = r'''
static unsigned writes(){unsigned n=0;for(auto&o:trace)if(o.write)++n;return n;}
static bool logged(const std::string&n){for(auto&m:messages)if(m.find(n)!=std::string::npos)return true;return false;}
static unsigned counted(const std::string&n){unsigned c=0;for(auto&m:messages)if(m.find(n)!=std::string::npos)++c;return c;}
static void setData(IOService*s,const char*k,const void*p,unsigned n){
 OSData*d=OSData::withBytes(p,n);assert(d);s->setProperty(k,d);d->release();
}
static IOService*clockNub(){
 auto*n=fixtureAlloc<IOService>();uint32_t abi=1;setData(n,"sweet,clock-abi",&abi,4);return n;
}
static IOService*gccNub(uint32_t variant=0x8150,bool abi=true,bool haveVariant=true,
 unsigned abiLength=4,unsigned measure=0,unsigned gate=0,unsigned disable=0){
 auto*n=fixtureAlloc<IOService>();
 if(abi){uint32_t a=1;setData(n,"sweet,gcc-abi",&a,abiLength);}
 if(haveVariant)setData(n,"sweet,gcc-variant",&variant,4);
 /* measure: 0 absent, 1 enabled, 2 a value the ADT never writes, 3 a short gate */
 if(measure){uint32_t m=measure==2?2u:1u;setData(n,"sweet,clock-measure",&m,measure==3?3u:4);}
 /* gate: 0 absent, 1 enabled, 2 a value the ADT never writes, 3 a short property */
 if(gate){uint32_t g=gate==2?2u:1u;setData(n,"sweet,clock-gate",&g,gate==3?3u:4);}
 /* disable: 0 absent, 1 enabled, 2 a value the ADT never writes, 3 a short property */
 if(disable){uint32_t d=disable==2?2u:1u;setData(n,"sweet,clock-disable",&d,disable==3?3u:4);}
 return n;
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
static IOService*consumer(const char*name){auto*c=fixtureAlloc<IOService>();c->setName(name);return c;}
static IOReturn acquire(IOQcomClockProvider*p,IOService*c,const char*n,IOQcomClockHandle**h){
 *h=nullptr;return p->acquireClock(c,n,h);
}
static void setReg(uintptr_t pa,uint32_t off,uint32_t v){regs[pa+off]=v;}
/* The C0-03 snapshot values a real device produced, plus a PDM configure that
 * decodes to XO with no division encoded. */
static void resetGccRegs(){
 trace.clear();regs.clear();
 setReg(0x0010b000u,0x00c,0x80000001u);	/* CLK_OFF|ENABLE, inherited on */
 setReg(0x0010b000u,0x038,0x00000001u);
 setReg(0x0010b000u,0x048,0x00000001u);
 setReg(0x00133000u,0x008,0x00000001u);
 setReg(0x00133000u,0x00c,0x00000001u);
 setReg(0x00133000u,0x010,0x00000000u);	/* CMD: nothing pending */
 setReg(0x00133000u,0x014,0x00000000u);	/* CFG: sel 0 (XO), div 0 */
 setReg(0x00143000u,0x008,0x00000001u);
 setReg(0x00162000u,0x000,0x00000000u);
 setReg(0x00162000u,0x004,0x00000000u);
 setReg(0x00162000u,0x008,0x00000001u);
}
/* C2-08/C2-03 device model knobs, declared before the reset that clears them. */
static bool gccRefStuckOff=false,gccCountStuck=false,gccGateStuckOff=false;
/* C2-04: a branch whose halt status never rises after the disable store. */
static bool gccDisableStuckRunning=false;
/* Off by default: the shipped fixture lets a test present any register value,
 * so a synthetic 0x0 is a legitimate input for the preconditions. A case that
 * wants the halt handshake the device performs (enable bit decides CLK_OFF)
 * asks for it, and the two behaviours then cover both refusal arms. */
static bool gccHaltFollowsEnable=false;
/* Device model knobs for the restore check. gccHaltLag makes the halt status
 * of a branch CBCR lag one read behind its disable (the 0127 ring read back
 * 0x0 with 0x80000000 stored), and gccDriftPA/gccDriftBits hold one
 * configuration bit of a register at the wrong level, which must still fail. */
static void clearKnobs(){gccMemoryCount=4;gccAbsentIndex=99;gccWrongPA=99;gccShortLen=99;gccNoBase=99;
 gccHaltLag=false;gccHaltPending.clear();gccDriftPA=0;gccDriftBits=0;
 gccRefStuckOff=false;gccCountStuck=false;gccGateStuckOff=false;gccNeighbourShift=false;
 gccDisableNeighbourShift=false;gccDisableStuckRunning=false;gccHaltFollowsEnable=false;
 gccDisableStored=false;}
/* C2-08 device model. The debug counter sees the selected source: the running
 * clock (19.2 MHz) yields four counts per loaded tick of the XO/4 reference,
 * a stopped source yields the same count on both runs, and a stuck counter
 * never raises the done bit. */
static uint32_t gccCounter(uintptr_t pa,uint32_t v){
 if(pa==0x00143008u||pa==0x00162008u||pa==0x00133008u){
  if(pa==0x00143008u&&gccRefStuckOff)return v&~1u;	/* a branch that will not come up */
  if(pa==0x00133008u&&gccGateStuckOff)return v&~1u;	/* the C2-03 sample refusing to run */
  /* A branch CBCR's halt status follows its own enable bit on the device: a
   * running branch reports CLK_OFF=0, a disabled one reports CLK_OFF=1. The
   * disable store writes the value it read (which never carries the bit the
   * hardware owns), so this model is what makes the halt observable at all. */
  if(pa==0x00133008u){
   if(gccDisableStuckRunning&&gccDisableStored&&!(v&1u))return v&~0x80000000u;	/* never halts */
   if(gccHaltFollowsEnable){
    if(v&1u)return v&~0x80000000u;
    return v|0x80000000u;}}
  if(v&1u)return v&~0x80000000u;		/* a running branch reports CLK_OFF=0 */
  return v;
 }
 if(pa!=0x0016203cu)return v;
 uint32_t ctl=regs[0x00162038u];
 if(!(ctl&(1u<<20)))return 0;			/* idle until the counter is enabled */
 if(gccCountStuck)return 0;			/* done never arrives */
 uint32_t mux=regs[0x00162000u]&0x3ffu;
 uint32_t ticks=ctl&0xffffffu&~((1u<<20));
 /* the C2-03 branch is a plain XO gate, so it counts like the running sample:
  * four per loaded XO/4 tick. 0130 modelled it as the XO/4 clock itself and the
  * device refuted that (19200036 against a declared 4800000). */
 if(mux==(uint32_t)kIOQcomGccGateMux)return (1u<<25)|(uint32_t)((uint64_t)ticks*4u);
 if(mux!=(uint32_t)kIOQcomGccMeasureRunningMux)return (1u<<25)|0x40u;
 return (1u<<25)|(uint32_t)((uint64_t)ticks*4u);
}
static IOQcomGCC*startGcc(IOService*gn){
 auto*g=fixtureAlloc<IOQcomGCC>();assert(g->start(gn));return g;
}
struct Decoded {IOReturn result;IOQcomClockState state;};
static Decoded decodeSample(IOQcomGCC*g,uint32_t cmd,uint32_t cfg){
 setReg(0x00133000u,0x010,cmd);setReg(0x00133000u,0x014,cfg);
 Decoded d;memset(&d,0,sizeof d);
 d.result=g->copyClockState(kIOQcomGccSampleLocalID,&d.state);
 return d;
}

int main(){
 assert(!live&&!liveMaps);
 /* ---- start refusals: identity first, then window, then provider ---- */
 {	/* no provider object at all: nothing to match, nothing to map */
  messages.clear();mapCalls.clear();
  auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(nullptr));
  assert(!g->fMapped&&!g->fLock&&!g->fRegistered&&g->fReads==0);
  g->release();assert(!live&&!liveMaps&&mapCalls.empty());
 }
 {	/* absent property is the ADT default, not an opt-in */
  messages.clear();mapCalls.clear();
  auto*n=fixtureAlloc<IOService>();auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));
  assert(counted("detail=missing-gcc-abi")==1);
  assert(logged("resource=gcc phase=start result="+std::to_string(kIOReturnBadArgument)+
                " detail=missing-gcc-abi"));
  assert(!g->fMapped&&!g->fLock&&mapCalls.empty());
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* the variant is present but the opt-in flag is not */
  messages.clear();
  auto*n=fixtureAlloc<IOService>();uint32_t variant=0x8150;
  setData(n,"sweet,gcc-variant",&variant,4);
  auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));assert(counted("detail=missing-gcc-abi")==1);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* an abi of the wrong length is not an abi */
  messages.clear();
  auto*n=gccNub(0x8150,true,true,3);auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));assert(counted("detail=missing-gcc-abi")==1);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* a zero abi is the ADT default, not an opt-in */
  messages.clear();
  auto*n=fixtureAlloc<IOService>();uint32_t abi=0,variant=0x8150;
  setData(n,"sweet,gcc-abi",&abi,4);setData(n,"sweet,gcc-variant",&variant,4);
  auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));assert(counted("detail=missing-gcc-abi")==1);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* abi two is somebody else's contract version */
  messages.clear();
  auto*n=fixtureAlloc<IOService>();uint32_t abi=2,variant=0x8150;
  setData(n,"sweet,gcc-abi",&abi,4);setData(n,"sweet,gcc-variant",&variant,4);
  auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));assert(counted("detail=missing-gcc-abi")==1);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* the opt-in is there but the SoC identity is not */
  messages.clear();
  auto*n=fixtureAlloc<IOService>();uint32_t abi=1;
  setData(n,"sweet,gcc-abi",&abi,4);
  auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));assert(counted("detail=missing-variant")==1);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* a short variant blob cannot be trusted as an identity */
  messages.clear();
  auto*n=fixtureAlloc<IOService>();uint32_t abi=1;
  setData(n,"sweet,gcc-abi",&abi,4);setData(n,"sweet,gcc-variant","\x50\x81\0",3);
  auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));assert(counted("detail=missing-variant")==1);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* another SoC in the same driver must refuse, not decode */
  messages.clear();
  auto*n=gccNub(0x8250);auto*g=fixtureAlloc<IOQcomGCC>();
  assert(!g->start(n));
  assert(logged("detail=variant-mismatch observed=0x8250 required=0x8150"));
  assert(counted("phase=start")==1);
  assert(!g->fMapped&&!g->fLock&&!g->fRegistered);
  assert(g->fReads==0&&g->fReadFailures==0&&mapCalls.empty());
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* a second start on a live instance is refused, never double-mapped */
  messages.clear();
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub();clearKnobs();resetGccRegs();
  auto*g=startGcc(gn);
  unsigned calls=mapCalls.size();messages.clear();
  assert(!g->start(gn));
  assert(logged("phase=start result="+std::to_string(kIOReturnBusy)+" detail=already-started"));
  assert(mapCalls.size()==calls);	/* nothing mapped twice */
  assert(g->fMapped&&g->fRegistered);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  assert(!live&&!liveMaps);
 }
 /* ---- window failures: each refuses start and leaves nothing mapped ---- */
 {	/* a missing device memory object is a window that does not exist */
  auto*n=gccNub();auto*g=fixtureAlloc<IOQcomGCC>();
  clearKnobs();gccAbsentIndex=3;mapCalls.clear();
  assert(!g->start(n));
  assert(logged("resource=gcc:0x162000 phase=map result=1 detail=window-absent"));
  assert(mapCalls.size()==4);
  assert(!g->fMapped&&!g->fLock&&!g->fRegistered);
  assert(liveMaps==0);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* a translated address that differs from the ADT range is not ours */
  auto*n=gccNub();auto*g=fixtureAlloc<IOQcomGCC>();
  clearKnobs();gccWrongPA=1;mapCalls.clear();
  assert(!g->start(n));
  assert(logged("resource=gcc:0x133000 phase=map result=2 detail=address-mismatch observed=0x133004"));
  assert(mapCalls.size()==2&&liveMaps==0);
  assert(!g->fMapped&&!g->fLock);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* a short window cannot cover the offsets this card reads */
  auto*n=gccNub();auto*g=fixtureAlloc<IOQcomGCC>();
  clearKnobs();gccShortLen=2;mapCalls.clear();
  assert(!g->start(n));
  assert(logged("resource=gcc:0x143000 phase=map result=2 detail=length-short observed=0xfff required=0x1000"));
  assert(mapCalls.size()==3&&liveMaps==0);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 {	/* a window with no kernel virtual base is not usable even if it maps */
  auto*n=gccNub();auto*g=fixtureAlloc<IOQcomGCC>();
  clearKnobs();gccNoBase=0;mapCalls.clear();
  assert(!g->start(n));
  assert(logged("resource=gcc:0x10b000 phase=map result=1 detail=no-virtual-base"));
  assert(mapCalls.size()==1&&liveMaps==0);
  g->release();n->release();assert(!live&&!liveMaps);
 }
 /* ---- entry absent: bounded wait, then one refusal line, no half state ---- */
 {
  auto*n=gccNub();auto*g=fixtureAlloc<IOQcomGCC>();
  clearKnobs();uint64_t before=fakeTicks;
  assert(!g->start(n));
  assert(logged("resource=gcc phase=start result=1 detail=provider-absent"));
  assert(fakeTicks>=before+200ull*5u*1000u);	/* 200 tries at 5 ms */
  assert(!g->fMapped&&!g->fLock&&!g->fRegistered&&liveMaps==0);
  assert(g->fReads==kIOQcomGccSnapshotCount);	/* the snapshot still happened */
  g->release();n->release();assert(!live&&!liveMaps);
 }
 /* ---- happy path: four windows, eleven snapshots, zero writes ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub();clearKnobs();resetGccRegs();
  auto*g=startGcc(gn);
  assert(g->fMapped&&g->fRegistered&&g->fLock);
  assert(g->clockControllerID()==kIOQcomCCGCC);
  assert(g->registerWrites()==0&&writes()==0);
  /* no gate, no measurement: neither opt-in leaves a row of any kind */
  assert(counted("sweetclock-gate")==0&&counted("sweetclock-measure")==0);
  assert(counted("phase=gate-check")==0&&counted("phase=measure")==0);
  assert(g->fReadFailures==0);
  /* 11 snapshot reads, then the decode walk: RCG CMD+CFG and one read per
   * branch (five branches). */
  assert(g->fReads==kIOQcomGccSnapshotCount+2+6);
  assert(g->registerReads()==g->fReads);
  assert(logged("resource=gcc:0x10b000 phase=map result=0 length=0x1000 offset=0xb000"));
  assert(logged("resource=gcc:0x133000 phase=map result=0 length=0x1000 offset=0x33000"));
  assert(logged("resource=gcc:0x143000 phase=map result=0 length=0x1000 offset=0x43000"));
  assert(logged("resource=gcc:0x162000 phase=map result=0 length=0x1000 offset=0x62000"));
  assert(logged("resource=gcc:0x10b00c phase=snapshot result=0 value=0x80000001"));
  assert(logged("resource=gcc:0x133010 phase=snapshot result=0 value=0"));
  /* the summary is emitted after the decode walk, so it counts those reads */
  assert(logged("planID=C2 resource=cc1 phase=start result=0 windows=4 snapshot=11 "
                "reads=19 writes=0 variant=0x8150"));
  assert(g->fSnapshot[kSnapshotDispAhb].valid&&g->fSnapshot[kSnapshotDispAhb].value==0x80000001u);
  assert(g->fSnapshot[kSnapshotDispXo].valid&&g->fSnapshot[kSnapshotDebugCbcr].value==1u);
  /* the ADT window order is the ABI: window 3 is the debug mux page */
  assert(g->fWindow[3].physical==0x00162000u&&g->fWindow[3].map!=nullptr);
  assert(p->fSlots[kIOQcomCCGCC].active);
  assert(p->registeredControllers()==1&&p->registeredWrites()==0);
  /* ---- descriptors are declared facts, not guesses ---- */
  {
   IOQcomClockInfo i;memset(&i,0,sizeof i);
   assert(g->copyClockInfo(1,&i)==kIOReturnSuccess);
   assert(i.controller==kIOQcomCCGCC&&i.localID==1);
   assert(i.kind==kIOQcomClockBranch&&i.halt==kIOQcomHaltDelay);
   assert(i.capabilities==(kIOQcomCapQueryConfig|kIOQcomCapQueryRate));
   assert(i.pinned&&i.possibleCount==0&&i.domain==0);
   assert(i.inheritedOn);		/* ENABLE was observed at start */
   assert(g->copyClockInfo(5,&i)==kIOReturnSuccess);
   assert(i.kind==kIOQcomClockRCG&&i.domain==kIOQcomDomainCX);
   assert(i.possibleCount==3&&!i.pinned);
   assert(i.possible[0].source==kIOQcomSourceXO&&i.possible[0].localID==0);
   assert(i.possible[1].source==kIOQcomSourceGPLL0Main&&i.possible[1].localID==1);
   assert(i.possible[2].source==kIOQcomSourceGPLL0Even&&i.possible[2].localID==6);
   assert(g->copyClockInfo(6,&i)==kIOReturnSuccess);
   assert(i.possibleCount==1&&i.possible[0].source==kIOQcomSourceLocal);
   assert(i.possible[0].controller==kIOQcomCCGCC&&i.possible[0].localID==kIOQcomGccSampleLocalID);
   /* the C2-08 counter reference is its own clock identity, not the C2-03
    * sample: it is the tree's only XO/4 parent, while the sample is a plain XO
    * gate in a different register, so the two identities differ twice over */
   assert(g->copyClockInfo(kIOQcomGccMeasureReferenceLocalID,&i)==kIOReturnSuccess);
   assert(i.localID==7&&i.kind==kIOQcomClockBranch&&!i.pinned);
   assert(i.possibleCount==1&&i.possible[0].source==kIOQcomSourceXODiv4);
   assert((unsigned)g->fGateEnabled==0u);
   assert(g->copyClockInfo(0,&i)==kIOReturnNotFound);
   assert(g->copyClockInfo(8,&i)==kIOReturnNotFound);
   assert(g->copyClockInfo(5,NULL)==kIOReturnBadArgument);
   /* the vendor tree's identifier, so a log and the tree agree */
   char name[kIOQcomNameMax];
   assert(g->copyClockName(5,name,sizeof name)&&!strcmp(name,"gcc_pdm2_clk_src"));
   assert(g->copyClockName(6,name,sizeof name)&&!strcmp(name,"gcc_pdm2_clk"));
   assert(g->copyClockName(1,name,sizeof name)&&!strcmp(name,"gcc_disp_ahb_clk"));
   assert(g->copyClockName(7,name,sizeof name)&&!strcmp(name,"gcc_xo_div4_clk"));
   assert(g->copyClockName(kIOQcomGccGateLocalID,name,sizeof name)&&!strcmp(name,"gcc_pdm_xo4_clk"));
   assert(!g->copyClockName(9,name,sizeof name));
   assert(!g->copyClockName(5,NULL,sizeof name));
   char tiny[4];assert(g->copyClockName(1,tiny,sizeof tiny)&&!strcmp(tiny,"gcc"));
  }
  /* ---- C2-02 decode vectors against clk_rcg2_calc_rate ---- */
  {
   Decoded d=decodeSample(g,0,0);
   assert(d.result==kIOReturnSuccess);
   assert(d.state.raw[0]==0&&d.state.raw[1]==0);
   assert(d.state.selected.source==kIOQcomSourceXO);
   assert(d.state.divX2==0);		/* 0 = no division encoded, not 2/(0+1) */
   assert(d.state.configuredHz==19200000ull&&d.state.rateKnown);
   assert(!d.state.updatePending&&!d.state.rootOff&&!d.state.haltTrusted);
   assert(logged("detail=configured sel=0 div=0 div2=0 hz=19200000 raw=0,0 known=1"));
  }
  {	/* div is a raw pre_div: divX2 = div+1, rate = 2*parent/divX2 */
   Decoded d=decodeSample(g,0,3);
   assert(d.result==kIOReturnSuccess&&d.state.divX2==4);
   assert(d.state.configuredHz==9600000ull&&d.state.rateKnown);
  }
  {
   Decoded d=decodeSample(g,0,31);	/* hid_width 5: the widest encoding */
   assert(d.result==kIOReturnSuccess&&d.state.divX2==32);
   assert(d.state.configuredHz==1200000ull&&d.state.rateKnown);
  }
  {	/* an untrusted parent keeps the raw fields and reports Unknown */
   Decoded d=decodeSample(g,0,(1u<<8));
   assert(d.result==kIOReturnSuccess);
   assert(d.state.selected.source==kIOQcomSourceGPLL0Main);
   assert(!d.state.rateKnown&&d.state.configuredHz==0);
   assert(d.state.divX2==0);
   assert(logged("detail=parent-rate-unknown sel=1 div=0 raw=0,0x100 known=0"));
   d=decodeSample(g,0,(6u<<8)|5);
   assert(d.result==kIOReturnSuccess&&d.state.selected.source==kIOQcomSourceGPLL0Even);
   assert(!d.state.rateKnown&&d.state.configuredHz==0&&d.state.divX2==6);
  }
  {	/* selector 7 is the test clock and 2..5 are unused in gcc_parent_map_0 */
   Decoded d=decodeSample(g,0,(7u<<8));
   assert(d.result==kIOReturnBadArgument);
   assert(d.state.selected.source==kIOQcomSourceUnknown&&d.state.selected.localID==7);
   assert(d.state.raw[0]==0&&d.state.raw[1]==(7u<<8));
   assert(logged("detail=src-invalid sel=7 raw=0,0x700"));
   d=decodeSample(g,0,(2u<<8));
   assert(d.result==kIOReturnBadArgument&&d.state.selected.source==kIOQcomSourceUnknown);
   assert(logged("detail=src-invalid sel=2 raw=0,0x200"));
  }
  {	/* mnd_width is 0 for this sample: any mode field is not our config */
   Decoded d=decodeSample(g,0,(1u<<12));
   assert(d.result==kIOReturnBadArgument);
   assert(logged("detail=mode-unsupported mode=1 raw=0,0x1000"));
   d=decodeSample(g,0,(3u<<12));
   assert(d.result==kIOReturnBadArgument);
   assert(logged("detail=mode-unsupported mode=3 raw=0,0x3000"));
  }
  {	/* a pending update means the configuration is not settled yet */
   Decoded d=decodeSample(g,kGccCmdUpdate,0);
   assert(d.result==kIOReturnSuccess&&d.state.updatePending);
   assert(!d.state.rateKnown&&d.state.configuredHz==0);
   assert(d.state.divX2==0&&d.state.selected.source==kIOQcomSourceXO);
   assert(logged("detail=update-pending sel=0 div=0 raw=0x1,0 known=0"));
  }
  {	/* ROOT_OFF is reported, and the decoded value stays a configuration */
   Decoded d=decodeSample(g,kGccCmdRootOff,0);
   assert(d.result==kIOReturnSuccess);
   assert(d.state.rootOff&&d.state.clockOffBit&&!d.state.enableBit);
   assert(d.state.rateKnown&&d.state.configuredHz==19200000ull);
   assert(!d.state.haltTrusted);	/* clk_rcg2 has no branch halt status */
  }
  {	/* boundary encodings: bits outside the declared masks are ignored */
   Decoded d=decodeSample(g,0,0x2fu);	/* div 15 */
   assert(d.state.divX2==16&&d.state.configuredHz==2400000ull);
   d=decodeSample(g,0,(1u<<13));	/* mode 2 */
   assert(d.result==kIOReturnBadArgument);
   d=decodeSample(g,0xfffffffeu,0);	/* harmless CMD bits, no update */
   assert(d.result==kIOReturnSuccess&&d.state.rateKnown);
  }
  /* ---- branches: halt trust follows the vendor halt class ---- */
  {
   setReg(0x00133000u,0x008,0x80000001u);
   IOQcomClockState s;assert(g->copyClockState(4,&s)==kIOReturnSuccess);
   assert(s.raw[0]==0x80000001u&&s.enableBit&&s.clockOffBit);
   assert(s.haltTrusted);	/* gcc_pdm_xo4_clk is BRANCH_HALT */
   /* a plain XO gate: the header constant and the descriptor agree, and the
    * C2-08 counter is the only /4 in the tree (see the gate measure fixture) */
   assert(s.rateKnown&&s.configuredHz==kIOQcomGccGateHz);
   assert(logged("detail=configured enable=1 off=1 raw=0x80000001 known=1"));
   assert(g->copyClockState(3,&s)==kIOReturnSuccess);
   assert(s.haltTrusted==false);	/* BRANCH_HALT_DELAY: CLK_OFF is not evidence */
   assert(s.rateKnown&&s.configuredHz==19200000ull);	/* XO */
   assert(g->copyClockState(6,&s)==kIOReturnSuccess);
   assert(!s.rateKnown&&s.configuredHz==0);	/* parent is a local RCG */
   assert(s.selected.source==kIOQcomSourceLocal&&s.selected.localID==kIOQcomGccSampleLocalID);
   assert(g->copyClockState(1,&s)==kIOReturnSuccess);	/* no declared parent */
   assert(!s.rateKnown&&s.selected.source==kIOQcomSourceNone);
   assert(g->copyClockState(99,&s)==kIOReturnNotFound);
   assert(g->copyClockState(5,NULL)==kIOReturnBadArgument);
  }
  /* ---- the register accessor is narrow, aligned and bounded ---- */
  {
   uint32_t v=0;
   unsigned failures=g->fReadFailures;
   assert(!g->readRegister(0,0xffe,&v));		/* 4 bytes at 0xffe cross the end */
   assert(g->fReadFailures==failures+1);
   assert(!g->readRegister(0,0x102,&v));		/* unaligned offset */
   assert(g->fReadFailures==failures+2);
   assert(!g->readRegister(kIOQcomGccWindowCount,0,&v));	/* not a window */
   assert(g->fReadFailures==failures+2);
   assert(!g->readRegister(0,0,NULL));		/* no output pointer */
   assert(g->readRegister(0,0xffc,&v));	/* last legal word is inside */
   assert(v==0);
   assert(g->fReadFailures==failures+2);
  }
  /* ---- provider contract: binding, admission, no capability to write ---- */
  {
   auto*c=consumer("gcc-consumer");
   bindings(c,{"gcc_pdm2_clk_src","gcc_pdm2_clk","gcc_disp_ahb_clk","gcc_pdm_xo4_clk"},
    {{kIOQcomCCGCC,5},{kIOQcomCCGCC,6},{kIOQcomCCGCC,1},{kIOQcomCCGCC,4}});
   IOQcomClockHandle*h=NULL;
   assert(acquire(p,c,"gcc_pdm2_clk_src",&h)==kIOReturnSuccess);
   IOQcomClockInfo i;IOQcomClockState s;
   assert(h->copyInfo(&i)==kIOReturnSuccess&&i.domain==kIOQcomDomainCX);
   assert(h->copyState(&s)==kIOReturnSuccess&&s.rateKnown);
   /* admission precedes the capability gate: this clock declares CX and no
    * domain provider exists yet, so the refusal names the dependency */
   assert(h->enable()==kIOReturnNotReady);
   assert(logged("phase=enable result="+std::to_string(kIOReturnNotReady)+" detail=domain-absent"));
   assert(h->disable()==kIOReturnUnsupported);	/* admission-free, no Disable bit */
   uint32_t reason=kIOQcomDepReady;
   assert(p->checkDependencies(h,&reason)==kIOReturnNotReady);
   assert(reason==kIOQcomDepDomain);		/* no domain provider yet */
   h->release();
   assert(acquire(p,c,"gcc_pdm2_clk",&h)==kIOReturnSuccess);
   assert(p->checkDependencies(h,&reason)==kIOReturnSuccess);	/* child of cc1:5 */
   h->release();
   assert(acquire(p,c,"gcc_disp_ahb_clk",&h)==kIOReturnSuccess);
   assert(h->copyInfo(&i)==kIOReturnSuccess&&i.pinned&&i.inheritedOn);
   assert(h->enable()==kIOReturnUnsupported);	/* admitted, but no Enable bit */
   assert(logged("phase=enable result="+std::to_string(kIOReturnUnsupported)+" detail=capability-absent"));
   assert(h->disable()==kIOReturnNotPermitted);	/* inherited: never a disable candidate */
   assert(logged("phase=disable result="+std::to_string(kIOReturnNotPermitted)+" detail=pinned-clock"));
   assert(p->checkDependencies(h,&reason)==kIOReturnSuccess);	/* no parents */
   h->release();
   assert(acquire(p,c,"gcc_pdm_xo4_clk",&h)==kIOReturnSuccess);
   assert(h->copyState(&s)==kIOReturnSuccess&&s.rateKnown&&s.configuredHz==kIOQcomGccGateHz);
   h->release();
   assert(writes()==0);
   c->release();
  }
  /* ---- stop withdraws the registration and leaves no mapping ---- */
  {
   g->stop(gn);
   assert(!g->fMapped&&!g->fLock&&g->fRegistered==false);
   assert(!g->fWindow[0].map&&!g->fWindow[3].map);
   assert(liveMaps==0);
   assert(logged("planID=C2 resource=cc1 phase=stop result=0 reads="));
   assert(!p->fSlots[kIOQcomCCGCC].active);
   IOQcomClockState s;
   assert(g->copyClockState(5,&s)==kIOReturnNotReady);
   IOQcomClockInfo i;
   assert(g->copyClockInfo(5,&i)==kIOReturnSuccess);	/* descriptors are static */
   g->stop(gn);	/* idempotent */
   assert(!g->fMapped&&!g->fLock);
   assert(writes()==0);
  }
  {
   /* the provider disappears before the backend: stop must still unmap and
    * keep a single truthful line instead of touching a withdrawn slot. */
   p->stop(pn);
   g->stop(gn);
   assert(!g->fMapped&&!g->fLock);
   assert(writes()==0);
   g->release();gn->release();
   p->release();pn->release();
   assert(!live&&!liveMaps);
  }
 }
 /* ---- opt-in diagnostics list the real backend, names and all ---- */
 {
  clockMode=1;
  auto*pn=clockNub();auto*p=startProvider(pn);
  assert(delayedCalls.size()==1);	/* armed at start, not run in place */
  clearKnobs();resetGccRegs();
  auto*gn=gccNub();auto*g=startGcc(gn);
  assert(p->registeredControllers()==1);	/* only the real backend so far */
  messages.clear();
  fireDelayed();
  assert(p->registeredControllers()==2);	/* the listing registers cc7 itself */
  if(getenv("MH2LM_GCC_DUMP")){for(auto&m:messages)if(m.find("phase=diag")!=std::string::npos)fprintf(stderr,"%s",m.c_str());}
  assert(logged("detail=backend-registered"));
  assert(logged("consumer=gcc_disp_ahb_clk"));
  assert(logged("consumer=gcc_pdm2_clk_src"));
  assert(logged("resource=cc1:0x5"));
  assert(logged("diag-done result=0 detail=zero-writes writes=0 controllers=2 probes=11"));
  /* every listed resource was bound and queried through the provider */
  assert(counted("phase=diag result=0 info=0 state=0")>=6);	/* every GCC clock */
  assert(writes()==0);
  /* the same log line the device will carry: caps and domain come from the
   * descriptor, deps from the provider, enable is the refusal this card has */
  assert(logged("caps=0x3 parents=3 domain=0x1"));
  assert(logged("caps=0x3 parents=0 domain=0"));
  clockMode=0;
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  assert(!live&&!liveMaps);
  assert(writes()==0);
 }
 /* ---- a fresh backend starts clean: no stale window or counter survives ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub();clearKnobs();resetGccRegs();
  auto*g=startGcc(gn);
  assert(g->fReads==kIOQcomGccSnapshotCount+2+6);
  g->stop(gn);
  auto*gn2=gccNub();resetGccRegs();
  auto*g2=startGcc(gn2);
  assert(g2->fReads==kIOQcomGccSnapshotCount+2+6&&g2->fReadFailures==0);
  assert(g2->fMapped&&g2->fRegistered);
  g2->stop(gn2);
  g->release();g2->release();gn->release();gn2->release();
  p->stop(pn);p->release();pn->release();
  assert(!live&&!liveMaps);
 }
  /* ---- the default image is still a zero-store candidate ---- */
  {
   auto*pn=clockNub();auto*p=startProvider(pn);
   clearKnobs();resetGccRegs();gccMeasureHook=gccCounter;
   auto*gn=gccNub();auto*g=startGcc(gn);
   assert(!g->fMeasureEnabled);		/* absent gate = the ADT default */
   assert(writes()==0&&g->registerWrites()==0);
   assert(counted("phase=measure")==0);	/* not even a DEBUG row: no attempt */
   assert(!logged("sweetclock-measure"));
   for(unsigned i=0;i<kIOQcomLedgerEntries;i++)assert(!p->fLedger[i].active);
   g->stop(gn);assert(writes()==0);
   g->release();gn->release();
   p->stop(pn);p->release();pn->release();
   assert(!live&&!liveMaps);
  }
  /* ---- C2-08: a gated candidate stores, measures, restores, then settles -- */
  {
   auto*pn=clockNub();auto*p=startProvider(pn);
   auto*gn=gccNub(0x8150,true,true,4,1);	/* sweet,clock-measure = 1 */
   resetGccRegs();
   setReg(0x00143000u,0x008,0x80000000u);	/* the XO/4 branch is off */
   gccMeasureHook=gccCounter;
   auto*g=startGcc(gn);
   assert(g->fMeasureEnabled);
   assert(logged("phase=gate result=0 clock-measure=1"));
   /* 4 setup stores, 7 per sample, 5 restores: every one of them traced */
   assert(writes()==23);assert(g->registerWrites()==23);
   assert((unsigned)counted("phase=write-issued")==23);
   assert(g->fWriteFailures==0);
   /* the reference was really taken through the ledger, once */
   assert(logged("phase=transition result=0 token="));
   assert(logged("owner=1"));
   assert(logged("phase=acquire result=0 token="));
   assert(logged("transition=new"));
   assert(logged("phase=running result=0 enable=1 off=0 value=0x1"));
   /* the running sample: 4x XO/4, cross-checked against the C2-02 decoding */
   assert(logged("phase=measure result=0 mux=0x53"));
   assert(logged("hz=19199084"));
   assert(logged("running=1 post-div=1 multiplier=1"));
   assert(logged("phase=measure-compare result=0 decoded-hz=19200000 measured-hz=19199084"));
   assert(logged("tolerance-ppm=10000"));
   /* the known-off clock is not running, and no divide-by-zero invented a rate */
   assert(logged("phase=measure result=0 mux=0x9a"));
   assert(logged("running=0 post-div=1 multiplier=1"));
   /* every saved register came back, in reverse, verified by a read-back */
   assert(counted("phase=restore result=0 expected=")==5);
   assert(g->fRestoreMismatches==0);
   assert(regs[0x00162000u]==0&&regs[0x00162004u]==0&&regs[0x00162008u]==1);
   assert(regs[0x00162038u]==0&&regs[0x0016203cu]==0);
   assert(regs[0x00143008u]==0x80000000u);
   assert(logged("phase=measure-done result=0 writes=23 restores=5 mismatches=0"));
   /* the ledger closed both transitions and kept no holder */
   IOQcomClockLedgerState ls;assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,0,&ls)==kIOReturnSuccess);
   assert(ls.holders==0&&ls.desired==0&&ls.pending==0&&ls.confirmed==0);
   assert(ls.transitions==2&&ls.confirmations==2);
   assert(logged("phase=confirm result=0 token="));
   assert(logged("phase=release result=0 token="));
   assert(logged("desired=0 pending=0 confirmed=0"));
   assert(p->registeredWrites()==writes());
   /* stop is not a writer either */
   g->stop(gn);assert(writes()==23);
   assert(logged("phase=stop result=0 reads="));
   g->release();gn->release();
   p->stop(pn);p->release();pn->release();
   assert(!live&&!liveMaps);
  }
  /* ---- the halt status lags, the configuration does not: the 0127 ring ---- */
  {
   auto*pn=clockNub();auto*p=startProvider(pn);
   auto*gn=gccNub(0x8150,true,true,4,1);	/* sweet,clock-measure = 1 */
   clearKnobs();resetGccRegs();messages.clear();
   /* the device read both gates as 0x80000000 (off, halt status set) */
   setReg(0x00143000u,0x008,0x80000000u);
   setReg(0x00162000u,0x008,0x80000000u);
   gccHaltLag=true;			/* a disable's halt status lags one read, like 0127 */
   gccMeasureHook=gccCounter;
   auto*g=startGcc(gn);
   assert(writes()==23);
   /* the enable clears CLK_OFF and the ledger confirms from that very read */
   assert(logged("phase=running result=0 enable=1 off=0 value=0x1"));
   /* both branch restores read back with the halt status still clear, and the
      configuration bits are what the check actually compares */
   assert(counted("phase=restore result=0 expected=0x80000000 value=0 mask=0xfffffff")==2);	/* %#x of 0 is 0 */
   /* the mux, post-divider and counter are compared whole */
   assert(counted("mask=0xffffffff")==3);
   assert(g->fRestoreMismatches==0);
   assert(!logged("detail=mismatch"));
   assert(logged("phase=measure-done result=0 writes=23 restores=5 mismatches=0"));
   assert(regs[0x00162008u]==0x80000000u);	/* the store was right, the status lags */
   assert(regs[0x00143008u]==0x80000000u);
   g->stop(gn);assert(writes()==23);
   g->release();gn->release();
   p->stop(pn);p->release();pn->release();
   assert(!live&&!liveMaps);
  }
  /* ---- a configuration bit that does not come back is still a failure ---- */
  {
   auto*pn=clockNub();auto*p=startProvider(pn);
   auto*gn=gccNub(0x8150,true,true,4,1);
   clearKnobs();resetGccRegs();messages.clear();
   gccDriftPA=0x00162038u;		/* the counter register holds a wrong bit */
   gccDriftBits=0x10u;
   gccMeasureHook=gccCounter;
   auto*g=startGcc(gn);
   assert(writes()==23);
   assert(counted("phase=restore result=")==5);
   assert(g->fRestoreMismatches==1);
   /* the failure is a WARN with the mask that was compared, not a filtered row */
   assert(counted("expected=0x10 value=0 mask=0xffffffff detail=mismatch")==1);
   assert(!logged("phase=measure-done result=0"));
   assert(logged("mismatches=1"));
   assert(logged("detail=measure-incomplete"));
   g->stop(gn);
   g->release();gn->release();
   p->stop(pn);p->release();pn->release();
   assert(!live&&!liveMaps);
  }
  /* ---- the gate itself is validated: a value or width the ADT never writes -- */
  {
   clockMode=0;unsigned before=writes();
   auto*gn=gccNub(0x8150,true,true,4,2);auto*g=fixtureAlloc<IOQcomGCC>();
   messages.clear();
   assert(!g->start(gn));
   assert(counted("detail=bad-clock-measure-gate value=0x2")==1);
   assert(!g->fMapped&&!g->fLock&&!g->fMeasureEnabled);
   g->release();gn->release();
   auto*gn2=gccNub(0x8150,true,true,4,3);auto*g2=fixtureAlloc<IOQcomGCC>();
   messages.clear();
   assert(!g2->start(gn2));
   assert(counted("detail=bad-clock-measure-gate length=3")==1);
   g2->release();gn2->release();
   assert(writes()==before);		/* a refused gate is not a licence to store */
   assert(!live&&!liveMaps);
  }
  /* ---- a reference that will not come up is reported, not measured ---- */
  {
   auto*pn=clockNub();auto*p=startProvider(pn);
   auto*gn=gccNub(0x8150,true,true,4,1);
   resetGccRegs();setReg(0x00143000u,0x008,0x80000000u);
   gccRefStuckOff=true;gccMeasureHook=gccCounter;
   auto*g=startGcc(gn);
   assert(counted("detail=reference-not-running")==1);
   assert(counted("mux=0x53")==0&&counted("mux=0x9a")==0);
   assert(writes()==2);			/* the enable, and the restore that undoes it */
   assert(logged("phase=measure-done result="+std::to_string(kIOReturnNotReady)));
   IOQcomClockLedgerState ls;
   assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,0,&ls)==kIOReturnSuccess);
   assert(ls.holders==0&&ls.pending==0&&ls.confirmed==0);
   assert(ls.transitions==2&&ls.confirmations==1);	/* the enable never landed */
   g->stop(gn);g->release();gn->release();
   p->stop(pn);p->release();pn->release();
   gccRefStuckOff=false;
   assert(!live&&!liveMaps);
  }
  /* ---- a counter that never finishes is bounded, and the restore still runs ---- */
  {
   auto*pn=clockNub();auto*p=startProvider(pn);
   auto*gn=gccNub(0x8150,true,true,4,1);
   resetGccRegs();setReg(0x00143000u,0x008,0x80000000u);
   gccCountStuck=true;gccMeasureHook=gccCounter;gccDelayCalls=0;
   auto*g=startGcc(gn);
   assert(counted("detail=done-timeout")==2);	/* both samples give up */
   assert(gccDelayCalls==2*kIOQcomGccMeasurePollTries);	/* and they give up on time */
   assert(logged("phase=measure-done result="+std::to_string(kIOReturnTimeout)));
   assert(logged("restores=5 mismatches=0"));
   assert(regs[0x00143008u]==0x80000000u&&regs[0x00162000u]==0);
   g->stop(gn);g->release();gn->release();
   p->stop(pn);p->release();pn->release();
   gccCountStuck=false;gccMeasureHook=nullptr;
   assert(!live&&!liveMaps);
  }
 /* ---- C2-03: the first write candidate, verified by halt status and neighbours ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1);
  messages.clear();resetGccRegs();clearKnobs();
  /* the C0-03 sample starts halted and disabled; the neighbours carry values
   * that a stray write would change */
  setReg(0x00133000u,0x008,0x80000000u);
  setReg(0x00133000u,0x00c,0x00000005u);
  setReg(0x00133000u,0x014,0x00000000u);
  setReg(0x0010b000u,0x048,0x00000001u);
  gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(counted("phase=gate result=0 clock-measure=0 clock-gate=1")==1);
  assert(logged("consumer=sweetclock-gate phase=request result=0 enable=0 off=1 value=0x80000000"));
  assert(logged("phase=request result=0 enable=0 off=1"));
  assert(writes()==1);
  assert(counted("phase=write-issued")==1);
  assert(logged("consumer=sweetclock-gate phase=write-issued result=0 stage=gate-enable write=1 value=0x80000001"));
  assert(gccDelayCalls==0);		/* the branch came up on the first read */
  assert(logged("consumer=sweetclock-gate phase=running result=0 enable=1 off=0 value=0x1 polls=0"));
  assert(logged("phase=gate-check result=0 neighbours=4 changed=0"));
  assert(logged("phase=completed result=0 token="));
  assert(logged("detail=sample-kept-until-reboot"));
  assert(logged("phase=gate-done result=0 writes=1 neighbours=4 changed=0 polls=0 hold=1"));
  /* the gate's read cost is counted, not estimated: the 19-read walk, one read
   * of the register before the store, the poll that came back clean on its
   * first read (one read for polls=0) and the four neighbours captured and
   * re-checked (0130 printed reads=29, which is this sum) */
  assert(g->fReads==kIOQcomGccSnapshotCount+2+6+1+1+8);
  assert(logged("phase=start result=0 windows=4 snapshot=11 reads="+
                std::to_string(kIOQcomGccSnapshotCount+2+6+1+1+8)));
  assert(g->fGateRunning&&g->fGateEnabled&&g->fGateNeighbourChanges==0);
  /* the sample is running and the card says so: the enable bit is kept */
  assert(regs[0x00133008u]==0x80000001u);
  /* neighbours byte-identical */
  assert(regs[0x0013300cu]==0x00000005u&&regs[0x00133014u]==0x00000000u);
  assert(regs[0x0010b048u]==0x00000001u);
  /* the token is held, not released: one holder, no release row */
  IOQcomClockLedgerState ls;
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&ls)==kIOReturnSuccess);
  assert(ls.holders==1&&ls.desired==1&&ls.confirmed==1&&ls.pending==0);
  assert(ls.transitions==1&&ls.confirmations==1);
  assert(counted("phase=release")==0);
  assert(logged("detail=gate-incomplete")==false);
  /* the gate alone stores nothing beyond its own enable */
  assert(counted("phase=measure")==0&&counted("restores=")==0);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
  /* ---- C2-03 with C2-08: the enabled branch becomes the counter's third source ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,1,1);
  messages.clear();resetGccRegs();clearKnobs();
  /* the sample starts halted, and so does the C2-08 XO/4 reference it will be
   * compared against: measure saves that branch, so it must be off to begin */
  setReg(0x00133000u,0x008,0x80000000u);
  setReg(0x00143000u,0x008,0x80000000u);
  gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(logged("phase=gate result=0 clock-measure=1 clock-gate=1"));
  /* the C0-03 S2 branch is measured through the C2-08 machinery at its own mux */
  assert(counted("consumer=sweetclock-measure phase=measure result=0 mux=0x99")==1);
  assert(logged("name=gcc_pdm_xo4_clk consumer=sweetclock-measure phase=measure result=0 mux=0x99"));
  /* the counter and the descriptor now agree on the same clock: with the XO
   * parent the comparison is a real check, not 4.8 MHz against itself */
  assert(logged("resource=cc1:0x4 name=gcc_pdm_xo4_clk consumer=sweetclock-measure "
                "phase=measure-compare result=0 decoded-hz=19200000 measured-hz=19199084"));
  assert(counted("consumer=sweetclock-measure phase=measure result=0 mux=")==3);	/* running, stopped, the gate */
  /* 23 measure stores + 1 gate store + 7 for the third sample */
  assert(writes()==23+1+7);
    assert(logged("phase=measure-done result=0 writes=31"));
  assert(logged("restores=5 mismatches=0"));
  assert(counted("phase=gate-done result=0")==1);
  /* the gate freed nothing: the reference restore must not touch its register */
  assert(regs[0x00133008u]==0x80000001u);
  IOQcomClockLedgerState ls;
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&ls)==kIOReturnSuccess);
  assert(ls.holders==1&&ls.desired==1&&ls.confirmed==1);
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccMeasureReferenceLocalID,0,&ls)==kIOReturnSuccess);
  assert(ls.holders==0&&ls.desired==0&&ls.confirmed==0);
  assert(logged("detail=gate-incomplete")==false);
  /* The ring's auditor can be pointed at this case: with the dump requested the
   * fixture prints the gate+measure rows in the device vocabulary, so the same
   * rows that just passed here are re-read by the offline auditor. */
  /* DUMP_ROWS_HOOK */
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
 /* ---- C2-03 refuses a branch it does not own, and a state it cannot explain ---- */
 {	/* somebody else's enable bit: no store, no ledger demand, no undoing it */
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1);
  messages.clear();resetGccRegs();clearKnobs();
  /* resetGccRegs leaves the PDM row at ENABLE on a real device image */
  setReg(0x00133000u,0x008,0x00000001u);
  gccMeasureHook=gccCounter;
  auto*g=startGcc(gn);
  assert(counted("detail=already-enabled")==1);
  assert(writes()==0&&!g->fGateRunning);
  assert(regs[0x00133008u]==0x00000001u);
  assert(counted("phase=gate-done")==0&&counted("phase=transition")==0);
  /* the refusal comes before the ledger exists: no demand was ever recorded */
  IOQcomClockLedgerState ls;
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&ls)==kIOReturnNotFound);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  assert(!live&&!liveMaps);
 }
 {	/* disabled but not halted: the halt check cannot verify anything */
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1);
  messages.clear();resetGccRegs();clearKnobs();
  setReg(0x00133000u,0x008,0x00000000u);	/* no ENABLE, no CLK_OFF */
  gccMeasureHook=gccCounter;
  auto*g=startGcc(gn);
  assert(counted("detail=halt-not-set")==1);
  assert(writes()==0&&!g->fGateRunning);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  assert(!live&&!liveMaps);
 }
 {	/* the ADT value decides before any window is touched */
  messages.clear();resetGccRegs();
  auto*gn=gccNub(0x8150,true,true,4,0,2);
  auto*g=fixtureAlloc<IOQcomGCC>();assert(!g->start(gn));
  assert(logged("detail=bad-clock-gate value=0x2"));
  assert(g->fReads==0&&writes()==0);
  g->release();gn->release();assert(!live&&!liveMaps);
  messages.clear();
  auto*shortNub=gccNub(0x8150,true,true,4,0,3);
  auto*g2=fixtureAlloc<IOQcomGCC>();assert(!g2->start(shortNub));
  assert(logged("detail=bad-clock-gate length=3"));
  assert(g2->fReads==0&&writes()==0);
  g2->release();shortNub->release();assert(!live&&!liveMaps);
 }
 /* ---- C2-03: a halted branch is bounded, and keeps the state it was given ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1);
  messages.clear();resetGccRegs();clearKnobs();
  setReg(0x00133000u,0x008,0x80000000u);
  gccGateStuckOff=true;gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(counted("detail=halt-timeout")==2);	/* the running row, then the card's verdict */
  assert(logged("polls=2500"));
  assert(gccDelayCalls==(int)kIOQcomGccGatePollTries);	/* bounded, not unbounded */
  assert(!g->fGateRunning);
  assert(writes()==1&&regs[0x00133008u]==0x80000001u);	/* the store is not undone */
  assert(logged("phase=gate-done result="+std::to_string(kIOReturnTimeout)+" writes=1"));
  assert(logged("hold=0 detail=halt-timeout"));
  /* the demand goes back: nothing is held for a clock that never ran */
  IOQcomClockLedgerState ls;
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&ls)==kIOReturnSuccess);
  assert(ls.holders==0&&ls.desired==0&&ls.confirmed==0);
  /* the enable landed but was never proven: the demand closes as owed */
  assert(ls.pending==1&&ls.transitions==2&&ls.confirmations==0);
  /* and start() reports the incomplete card once */
  assert(counted("detail=gate-incomplete")==1);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccGateStuckOff=false;gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
 /* ---- C2-03: a neighbour that moves fails the card, and says which one ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1);
  messages.clear();resetGccRegs();clearKnobs();
  setReg(0x00133000u,0x008,0x80000000u);
  setReg(0x00133000u,0x014,0x00000000u);
  gccNeighbourShift=true;gccMeasureHook=gccCounter;
  auto*g=startGcc(gn);
  assert(counted("detail=changed")==1);
  assert(logged("phase=neighbours result="+std::to_string(kIOReturnIOError)+" detail=changed"));
  assert(logged("phase=gate-check result="+std::to_string(kIOReturnIOError)+" neighbours=4 changed=1"));
  assert(g->fGateNeighbourChanges==1);
  assert(logged("phase=gate-done result="+std::to_string(kIOReturnIOError)+" writes=1"));
  assert(logged("hold=1"));
  assert(counted("detail=gate-incomplete")==1);
  /* the branch still ran and is still on: the card fails the proof, not the write */
  assert(g->fGateRunning&&regs[0x00133008u]==0x80000001u);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccNeighbourShift=false;gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
  assert(!live&&!liveMaps&&rawBlocks==0);
 /* ---- C2-04: the disable retracts the enable, proves the halt, hands the demand back ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1,1);
  messages.clear();resetGccRegs();clearKnobs();
  /* the C0-03 sample starts halted; the neighbours carry values a stray write
   * would change, and the same set is checked around the disable store */
  setReg(0x00133000u,0x008,0x80000000u);
  setReg(0x00133000u,0x00c,0x00000005u);
  setReg(0x00133000u,0x014,0x00000000u);
  setReg(0x0010b000u,0x048,0x00000001u);
  gccHaltFollowsEnable=true;gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(counted("phase=gate result=0 clock-measure=0 clock-gate=1 clock-disable=1")==1);
  /* enable, disable, enable: three stores, each announced once, and the second
   * enable is the C2-03 path verbatim, so its rows appear twice */
  assert(writes()==3);
  assert(counted("phase=write-issued")==3);
  assert(counted("consumer=sweetclock-gate phase=write-issued")==2);
  assert(logged("consumer=sweetclock-gate phase=write-issued result=0 stage=gate-enable write=1 value=0x80000001"));
  /* the store is a read-modify-write of what the branch reported: the halt bit
   * is hardware-owned and was low, so only the enable bit is withdrawn */
  assert(logged("consumer=sweetclock-disable phase=write-issued result=0 stage=disable write=2 value=0"));
  assert(counted("consumer=sweetclock-gate phase=write-issued result=0 stage=gate-enable write=3")==1);
  assert(logged("consumer=sweetclock-disable phase=disable-request result=0 enable=1 off=0 value=0x1"));
  assert(logged("consumer=sweetclock-disable phase=halt result=0 enable=0 off=1 value=0x80000000 polls=0"));
  assert(logged("consumer=sweetclock-disable phase=disable-check result=0 neighbours=4 changed=0"));
  assert(logged("phase=disable-done result=0 writes=2 neighbours=4 changed=0 polls=0 hold=0 last-holder=1 detail=sample-halted"));
  /* the release is the halted verdict, not a bare withdraw: stopped=true shows
   * up as confirmed=0/pending=0 and the confirmation count moves */
  assert(counted("phase=release result=0")==1);
  assert(logged("resource=cc1:0x4 phase=release result=0 token=1 holders=0 desired=0 pending=0 confirmed=0 transitions=2 confirmations=2"));
  /* and the recovery ran: a second gate pass, a new demand, the branch running */
  assert(counted("phase=gate-done result=0")==2);
  assert(logged("phase=gate-done result=0 writes=3 neighbours=4 changed=0 polls=0 hold=1"));
  assert(counted("phase=confirm result=0")==2);
  assert(logged("phase=start result=0 detail=disable-incomplete")==false);
  assert(counted("consumer=sweetclock-disable phase=reenable")==0);
  assert(g->fReads==29+20);	/* the gate's 29 reads plus the same pass twice more */
  assert(regs[0x00133008u]==0x80000001u);
  assert(regs[0x0013300cu]==0x00000005u&&regs[0x00133014u]==0x00000000u);
  assert(g->fGateRunning&&g->fGateTokenHeld&&g->fGateToken==2);
  IOQcomClockLedgerState ls;
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&ls)==kIOReturnSuccess);
  assert(ls.holders==1&&ls.desired==1&&ls.pending==0&&ls.confirmed==1);
  assert(ls.transitions==3&&ls.confirmations==3);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
 /* ---- C2-04 guards: a disable without the demand, or of a branch it did not leave running ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1,0);	/* gate only: no disable opt-in */
  messages.clear();resetGccRegs();clearKnobs();
  setReg(0x00133000u,0x008,0x80000000u);
  gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(logged("phase=gate result=0 clock-measure=0 clock-gate=1 clock-disable=0"));
  assert(writes()==1);
  /* absent opt-in: the card is not armed at all, and says so without a store */
  assert(g->disableClocks()==kIOReturnUnsupported);
  assert(counted("consumer=sweetclock-disable")==0);
  assert(writes()==1);
  /* armed, but the demand is not in hand: nothing may be released that the
   * ledger does not hold for this driver */
  messages.clear();
  g->fDisableEnabled=true;
  g->fGateTokenHeld=false;g->fGateToken=0;
  assert(g->disableClocks()==kIOReturnNotReady);
  assert(logged("detail=no-token hold=0"));
  assert(writes()==1);
  /* handed a branch that never carried the enable this driver wrote: with the
   * fixture's plain register file neither bit is set */
  g->fGateTokenHeld=true;g->fGateToken=1;
  setReg(0x00133000u,0x008,0x00000000u);
  assert(g->disableClocks()==kIOReturnNotReady);
  assert(logged("detail=not-enabled enable=0 off=0 value=0"));
  /* and the same handover on a device whose halt status follows the enable bit:
   * the branch was already halted by somebody else */
  gccHaltFollowsEnable=true;
  assert(g->disableClocks()==kIOReturnNotReady);
  assert(logged("detail=already-halted enable=0 off=1 value=0x80000000"));
  gccHaltFollowsEnable=false;
  assert(writes()==1);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
 /* ---- C2-04: a halt that never arrives keeps the demand and the branch ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1,1);
  messages.clear();resetGccRegs();clearKnobs();
  setReg(0x00133000u,0x008,0x80000000u);
  gccDisableStuckRunning=true;gccHaltFollowsEnable=true;gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(logged("consumer=sweetclock-disable phase=halt result="+std::to_string(kIOReturnTimeout)+
                " enable=0 off=0 value=0 polls="+std::to_string(kIOQcomGccDisablePollTries)+
                " writes=2 hold=1 detail=halt-timeout"));
  assert(counted("phase=disable-check")==0);
  assert(counted("phase=release")==0);
  assert(logged("phase=disable-done")==false);
  assert(logged("detail=disable-incomplete disable-result="+std::to_string(kIOReturnTimeout)));
  assert(writes()==2);		/* enable and the disable store; nothing undone */
  assert(gccDelayCalls==kIOQcomGccDisablePollTries);
  assert(g->fGateRunning&&g->fGateTokenHeld&&g->fGateToken==1);
  assert(regs[0x00133008u]==0x00000000u);	/* as stored: the halt bit never rose */
  IOQcomClockLedgerState ls;
  assert(p->ledgerInspect(kIOQcomCCGCC,kIOQcomGccGateLocalID,0,&ls)==kIOReturnSuccess);
  assert(ls.holders==1&&ls.desired==1&&ls.pending==0&&ls.confirmed==1&&ls.transitions==1);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
 /* ---- C2-04: a neighbour that moved keeps the demand even though the halt happened ---- */
 {
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*gn=gccNub(0x8150,true,true,4,0,1,1);
  messages.clear();resetGccRegs();clearKnobs();
  setReg(0x00133000u,0x008,0x80000000u);
  setReg(0x00133000u,0x00c,0x00000005u);
  setReg(0x00133000u,0x014,0x00000000u);
  gccDisableNeighbourShift=true;gccHaltFollowsEnable=true;gccMeasureHook=gccCounter;gccDelayCalls=0;
  auto*g=startGcc(gn);
  assert(logged("phase=neighbours result="+std::to_string(kIOReturnIOError)+" detail=changed"));
  assert(logged("consumer=sweetclock-disable phase=disable-check result="+std::to_string(kIOReturnIOError)+
                " neighbours=4 changed=1"));
  assert(logged("phase=disable-done result="+std::to_string(kIOReturnIOError)+
                " writes=2 neighbours=4 changed=1 polls=0 hold=1 detail=neighbour-changed"));
  assert(counted("phase=release")==0);
  assert(logged("detail=disable-incomplete disable-result="+std::to_string(kIOReturnIOError)));
  assert(writes()==2);		/* and no re-enable: the card will not pretend */
  assert(g->fGateTokenHeld&&g->fGateToken==1);
  g->stop(gn);g->release();gn->release();
  p->stop(pn);p->release();pn->release();
  gccMeasureHook=nullptr;
  assert(!live&&!liveMaps);
 }
 /* ---- C2-04: the opt-in is validated before any window is touched ---- */
 {
  messages.clear();resetGccRegs();
  auto*gn=gccNub(0x8150,true,true,4,0,1,2);
  auto*g=fixtureAlloc<IOQcomGCC>();assert(!g->start(gn));
  assert(logged("detail=bad-clock-disable value=0x2"));
  assert(g->fReads==0&&writes()==0);
  g->release();gn->release();assert(!live&&!liveMaps);
  messages.clear();
  auto*shortNub=gccNub(0x8150,true,true,4,0,1,3);
  auto*g2=fixtureAlloc<IOQcomGCC>();assert(!g2->start(shortNub));
  assert(logged("detail=bad-clock-disable length=3"));
  assert(g2->fReads==0&&writes()==0);
  g2->release();shortNub->release();assert(!live&&!liveMaps);
  messages.clear();
  /* a disable of a branch this driver never opened is refused at start: the
   * retraction would be aimed at a state no card here can explain */
  auto*loneNub=gccNub(0x8150,true,true,4,0,0,1);
  auto*g3=fixtureAlloc<IOQcomGCC>();assert(!g3->start(loneNub));
  assert(logged("detail=clock-disable-needs-gate clock-gate=0 clock-disable=1"));
  assert(g3->fReads==0&&writes()==0);
  g3->release();loneNub->release();assert(!live&&!liveMaps);
 }
 printf("PASS: actual C2-01/C2-02/C2-08/C2-03 GCC backend; ADT-matched identity (abi/variant) and "
        "window refusals (absent/address/length/no-base) with clean unmapping, bounded "
        "provider wait, four mapped windows and eleven snapshots, narrow aligned "
        "barriered read-only accessor with out-of-bounds refusal, clk_rcg2 decode "
        "vectors (no-division, divX2, widest divider, invalid selector, unsupported "
        "mode, update pending, ROOT_OFF), branch halt classes, provider binding and "
        "domain admission, opt-in listing of the real backend, zero MMIO writes by "
        "default, and the gated C2-08 measurement candidate: 23 ordered stores behind "
        "one 607-byte-safe ADT gate, a ledger-acquired XO/4 reference, short/full counter "
        "runs with a bounded poll, NotRunning on equal counts, a C2-02 cross-check, and a "
        "reverse-ordered read-back-verified restore of all five saved registers "
        "with the branch halt status masked out of the comparison, and the C2-03 branch "
        "gate: one store behind its own ADT opt-in, halt verified with a bounded poll, "
        "neighbours proven unmoved, a halted branch that keeps its state and releases "
        "its demand, refusals for an already-enabled or unhalted sample, and the gated "
        "branch reused as the counter's third source\n");
 return 0;
}
'''


# The C2-03 row dump exists only when a caller asks for it: the shipped fixture
# is one program, and the auditor replay must read the rows this build printed.
if os.environ.get('MH2LM_GCC_DUMP_ROWS'):
    assert tests.count('/* DUMP_ROWS_HOOK */') == 1
    tests = tests.replace('/* DUMP_ROWS_HOOK */',
                          'for(auto&m:messages)printf("%s\\n",m.c_str());')

with tempfile.TemporaryDirectory(prefix='mh2lm-gcc-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+extra+header+gcc_header+provider+gcc_source+tests)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path('/tmp/mh2lm-gcc-kept.cpp').write_text(cpp.read_text())
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=60)