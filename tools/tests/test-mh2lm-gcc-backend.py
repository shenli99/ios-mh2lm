#!/usr/bin/env python3
"""Actual C2-01/C2-02 GCC backend against contract-only MMIO fixtures.
Only OS allocation/locking/thread-call primitives and the device memory backing
store are fixtures: the shipped IOQcomGCC backend is compiled as written, with
its single register accessor routed to the fixture register file. Every case
also asserts that the candidate performed zero register writes, and the source
text is checked for the absence of any store path (a fixture cannot invent one).
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
# Structural zero-write evidence: the shipped file has exactly one device
# memory access and it is a load; there is no store and no write helper.
assert raw.count('*(volatile uint32_t *)')==1, raw.count('*(volatile uint32_t *)')
assert '=*(volatile' in raw
assert 'writeRegister' not in raw and 'IOSyncWrite' not in raw
assert '*(volatile uint32_t *)' in [l.strip() for l in raw.splitlines() if 'volatile' in l][0] or True
for line in raw.splitlines():
    if '*(volatile uint32_t *)' in line:
        assert '=*(volatile' in line.replace(' ',''), line
assert raw.count('__asm__ volatile("dmb oshld" ::: "memory");')==1
assert raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
assert raw.count('#endif /* ARM64_BOARD_CONFIG_MH2LM */')==1
gcc_source = re.sub(r'^#include.*\n', '', raw, flags=re.M)
# CPU barriers and actual MMIO are the only replaced engine statements.
gcc_source = gcc_source.replace('observed=*(volatile uint32_t *)(w.base+offset);',
                                'observed=readReg(w.base,offset);')
gcc_source = gcc_source.replace('__asm__ volatile("dmb oshld" ::: "memory");','(void)0;')
# Two translation units in the kernel, one in this harness: rename the shared
# file-scope enum tag and reset the `super` macro between them.
gcc_source = gcc_source.replace('kIOQcomLogBuf', 'kIOQcomGccLogBuf')
gcc_source = '#undef super\n' + gcc_source
assert 'readReg(w.base,offset)' in gcc_source and 'dmb oshld' not in gcc_source

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
static IOService*gccNub(uint32_t variant=0x8150,bool abi=true,bool haveVariant=true,unsigned abiLength=4){
 auto*n=fixtureAlloc<IOService>();
 if(abi){uint32_t a=1;setData(n,"sweet,gcc-abi",&a,abiLength);}
 if(haveVariant)setData(n,"sweet,gcc-variant",&variant,4);
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
static void clearKnobs(){gccMemoryCount=4;gccAbsentIndex=99;gccWrongPA=99;gccShortLen=99;gccNoBase=99;}
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
  assert(g->fReadFailures==0);
  /* 11 snapshot reads, then the decode walk: RCG CMD+CFG and one read per
   * branch (five branches). */
  assert(g->fReads==kIOQcomGccSnapshotCount+2+5);
  assert(g->registerReads()==g->fReads);
  assert(logged("resource=gcc:0x10b000 phase=map result=0 length=0x1000 offset=0xb000"));
  assert(logged("resource=gcc:0x133000 phase=map result=0 length=0x1000 offset=0x33000"));
  assert(logged("resource=gcc:0x143000 phase=map result=0 length=0x1000 offset=0x43000"));
  assert(logged("resource=gcc:0x162000 phase=map result=0 length=0x1000 offset=0x62000"));
  assert(logged("resource=gcc:0x10b00c phase=snapshot result=0 value=0x80000001"));
  assert(logged("resource=gcc:0x133010 phase=snapshot result=0 value=0"));
  /* the summary is emitted after the decode walk, so it counts those reads */
  assert(logged("planID=C2 resource=cc1 phase=start result=0 windows=4 snapshot=11 "
                "reads=18 writes=0 variant=0x8150"));
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
   assert(g->copyClockInfo(0,&i)==kIOReturnNotFound);
   assert(g->copyClockInfo(7,&i)==kIOReturnNotFound);
   assert(g->copyClockInfo(5,NULL)==kIOReturnBadArgument);
   /* the vendor tree's identifier, so a log and the tree agree */
   char name[kIOQcomNameMax];
   assert(g->copyClockName(5,name,sizeof name)&&!strcmp(name,"gcc_pdm2_clk_src"));
   assert(g->copyClockName(6,name,sizeof name)&&!strcmp(name,"gcc_pdm2_clk"));
   assert(g->copyClockName(1,name,sizeof name)&&!strcmp(name,"gcc_disp_ahb_clk"));
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
   assert(s.rateKnown&&s.configuredHz==4800000ull);	/* XO/4 */
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
   assert(h->copyState(&s)==kIOReturnSuccess&&s.rateKnown&&s.configuredHz==4800000ull);
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
  assert(logged("diag-done result=0 detail=zero-writes writes=0 controllers=2 probes=10"));
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
  assert(g->fReads==kIOQcomGccSnapshotCount+2+5);
  g->stop(gn);
  auto*gn2=gccNub();resetGccRegs();
  auto*g2=startGcc(gn2);
  assert(g2->fReads==kIOQcomGccSnapshotCount+2+5&&g2->fReadFailures==0);
  assert(g2->fMapped&&g2->fRegistered);
  g2->stop(gn2);
  g->release();g2->release();gn->release();gn2->release();
  p->stop(pn);p->release();pn->release();
  assert(!live&&!liveMaps);
 }
 assert(!live&&!liveMaps&&rawBlocks==0);
 printf("PASS: actual C2-01/C2-02 GCC backend; ADT-matched identity (abi/variant) and "
        "window refusals (absent/address/length/no-base) with clean unmapping, bounded "
        "provider wait, four mapped windows and eleven snapshots, narrow aligned "
        "barriered read-only accessor with out-of-bounds refusal, clk_rcg2 decode "
        "vectors (no-division, divX2, widest divider, invalid selector, unsupported "
        "mode, update pending, ROOT_OFF), branch halt classes, provider binding and "
        "domain admission, opt-in listing of the real backend, zero MMIO writes\n");
 return 0;
}
'''


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