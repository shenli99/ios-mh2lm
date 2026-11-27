#!/usr/bin/env python3
"""Actual C3-01/C3-02 apps RSC channel against contract-only MMIO fixtures.
Only OS allocation/locking primitives, the device memory backing store and the
command DB descriptor are fixtures: the shipped IOQcomRSCChannel is compiled as
written, with its single register accessor routed to the fixture register file.
The structural checks pin the source to one load and no store at all - the
channel's whole claim is that it cannot write - and the functional cases prove
the ten-load read budget, every start refusal, the observation of the owner's
TCS words without touching them, a submit that answers NotReady, and - once the
tree opts in - one read-only command DB query whose result is checked field by
field against the words the fixture laid down.
Usage: test-mh2lm-rsc-channel.py EXTRACTED_XNU
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
ns = {'__file__': str(existing), '__name__': 'rsc_fixture', '__doc__': ''}
exec(compile(existing.read_text().split('arm = Path(sys.argv[1])')[0], str(existing), 'exec'), ns)

prelude = ns['prelude']
extra = ns['extra']

# The channel maps four windows: the three ADT ranges through the provider nub
# and the command DB through a descriptor built from the locator. Both paths
# need host backing, and both need a knob per refusal the card declares.
prelude = prelude.replace('static std::vector<unsigned> mapCalls;',
'''static std::vector<unsigned> mapCalls;
using IOPhysicalAddress=uint64_t;using IOVirtualAddress=uintptr_t;using IOByteCount=uint64_t;
/* Apps RSC geometry exactly as the LG node declares it. rscMode keeps the nub
   from answering with the display tree's own bases, so a host case cannot pass
   by accident on the wrong window. C3-04: a delegating tree adds the apps GIC
   window as the fourth range, so the same nub answers that one too - and only
   when the executor opt-in was set, which is what makes "three ranges" the
   default a case can assert. */
static constexpr uintptr_t rscPAs[4]={0x18220000,0x18220d00,0x0c3f000c,0x17a00000};
static constexpr size_t rscLens[4]={0x100,0x3000,8,0x10000};
static bool rscMode=true,rscExecutor=false;
static unsigned rscAbsentIndex=99,rscWrongPA=99,rscShortLen=99,rscNoBase=99;''')
prelude = prelude.replace('int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){',
'''int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){
 if(rscMode){mapCalls.push_back(i);
  /* Three ranges by default; the fourth is the apps GIC window and it is only
     there when the tree opted into the executor. Asking for it either way is a
     different tree, not a lenient one. */
  if(i>(rscExecutor?3u:2u))return nullptr;
  if(i==rscAbsentIndex)return nullptr;
  auto*m=new IOMemoryMap(i);
  m->customPA=rscPAs[i]+(i==rscWrongPA?4:0);
  m->customSize=rscLens[i]-(i==rscShortLen?1:0);
  return m;}''')
prelude = prelude.replace('uintptr_t getVirtualAddress(){return customPA?customPA:bases[index];}',
'uintptr_t getVirtualAddress(){if(rscMode&&index==rscNoBase)return 0;return customPA?customPA:bases[index];}')
assert 'rscAbsentIndex' in prelude

prelude += '''
/* The command DB is reached through a descriptor, never through the ADT reg
   list. The direction is captured so a case can prove the mapping asked for
   read-only and not for the owner's read-write descriptor. */
constexpr unsigned kIODirectionIn=1;
static bool rejectDescriptor=false;
static unsigned descriptorDirection=0;
static std::vector<std::pair<uintptr_t,size_t> > descriptorRequests;
struct IOMemoryDescriptor : OSObject {
 uintptr_t pa;size_t length;
 static IOMemoryDescriptor*withPhysicalAddress(uintptr_t p,size_t n,unsigned d){
  descriptorDirection=d;descriptorRequests.push_back(std::make_pair(p,n));
  if(rejectDescriptor)return nullptr;
  auto*x=new IOMemoryDescriptor;x->pa=p;x->length=n;return x;}
 IOMemoryMap*map(){auto*m=new IOMemoryMap(3);m->customPA=pa;m->customSize=length;return m;}
};
/* The device boots at log level 0, so DEBUG rows never reach the ring: model that. */
#define SWEET_LOG_DEBUG(...) ((void)0)
'''

arm = Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
header_raw = (arm/'IOQcomRSC.h').read_text()
raw = (arm/'IOQcomRSC.cpp').read_text()

# ---- structural evidence, before anything is rewritten for the harness ----
# C3-04: the channel can be the hardware owner, so it now has exactly one store.
# It stays one statement in one helper, and the load stays one statement in
# another, which is what keeps "a tree that did not delegate writes nothing" a
# property a reader can check in this file instead of a promise in a comment.
assert raw.count('*(volatile uint32_t *)')==2, raw.count('*(volatile uint32_t *)')
assert raw.count('observed=*(volatile uint32_t *)(w.base+offset);')==1
assert raw.count('*(volatile uint32_t *)(w.base+offset)=value;')==1
# The store lives in tcsWrite and nowhere before it: the only way to reach a
# store is the function the door table publishes.
assert raw.index('*(volatile uint32_t *)(w.base+offset)=value;') > raw.index('IOQcomRSCChannel::tcsWrite')
# One barrier pair per direction, and no raw read-modify-write of device memory.
assert raw.count('__asm__ volatile("dmb oshld" ::: "memory");')==1
assert raw.count('__asm__ volatile("dmb oshst" ::: "memory");')==1
assert raw.count('__asm__ volatile("dsb osh" ::: "memory");')==1
assert raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
assert raw.count('#endif /* ARM64_BOARD_CONFIG_MH2LM */')==1
assert header_raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
# The opt-ins are the whole ADT surface, each read once.
assert raw.count('sweet,rsc-abi')==1
assert raw.count('sweet,rsc-irq-abi')==1
assert raw.count('sweet,rsc-intid')==1
assert raw.count('sweet,rsc-query')==1
assert raw.count('getProperty("sweet,rsc-executor")')==1
# Still no second way to touch the line: no registration call, no scalar lock
# in this file (the door's router lock is another translation unit), no GICD
# word spelled here at all - masking goes through SweetAppsLine, which is.
for banned in ('registerInterrupt','IOSimpleLock','GICEnable','GICDisable',
               'GICPending','GICActive','GICConfig','GICPriority','GICRoute',
               'writeRegister','kIODirectionInOut'):
    assert banned not in raw, banned
    assert banned not in header_raw, banned
# The only place the GIC physical address may appear is the header's expectation
# for the fourth window: the file never spells an address it could read.
assert raw.count('0x17a00000')==0 and raw.count('0x17a0000')==0
assert header_raw.count('0x17a00000')==1
assert raw.count('kIOQcomRscExecutorGicPhysical')==1
# One interrupt source, created once, and the top half only posts to it.
assert raw.count('IOInterruptEventSource')==2
assert raw.count('interruptOccurred')==1
# The owner is announced through the one door table and retired through it.
assert raw.count('sweet_apps_door_publish')==1
assert raw.count('sweet_apps_door_withdraw')==1
# The DB is mapped with the read-only direction, and only once, in either mode.
assert raw.count('kIODirectionIn')==1
# One published entry, one definition, and it refuses.
assert raw.count('IOQcomRSCChannel::submit(')==1
assert 'return kIOReturnNotReady;' in raw
assert 'class IOQcomRSCChannel : public IOService' in header_raw
assert 'channelSubmitAllowed(void) const { return false; }' in header_raw

rsc_header = re.sub(r'^#include.*\n', '', header_raw, flags=re.M)
rsc_header = rsc_header.replace('class IOQcomRSCChannel : public IOService {',
                                'struct IOQcomRSCChannel : public IOService {')
assert 'struct IOQcomRSCChannel' in rsc_header
rsc_source = re.sub(r'^#include.*\n', '', raw, flags=re.M)
# Actual MMIO and the barriers are the only replaced engine statements. The
# harness records both directions, so a store the channel should not have made
# is a row in the trace rather than a memory corruption in the test process.
rsc_source = rsc_source.replace('observed=*(volatile uint32_t *)(w.base+offset);',
                                'observed=readReg(w.base,offset);')
rsc_source = rsc_source.replace('*(volatile uint32_t *)(w.base+offset)=value;',
                                'writeReg(w.base,offset,value);')
rsc_source = re.sub(r'__asm__ volatile\("(dmb|dsb)[^"]*" ::: "memory"\);', '(void)0;', rsc_source)
assert 'readReg(w.base,offset)' in rsc_source and 'volatile uint32_t *)' not in rsc_source
assert 'writeReg(w.base,offset,value)' in rsc_source
rsc_source = '\n#undef super\n' + rsc_source
# C3-04 adds two sibling translation units to the same kext: the door router and
# the line driver. The harness compiles both as written and routes the line's two
# MMIO statements into the same register file the channel reads, so the GIC the
# channel claims is a row in `trace` and a word in `regs` like every other bus
# access in this fixture - not a host address that would have to be invented.
def load_tu(name):
 text = re.sub(r'^#include.*\n', '', (arm/name).read_text(), flags=re.M)
 return text
apps_door_header = load_tu('SweetAppsDoor.h')
apps_line_header = load_tu('SweetAppsLine.h').replace('class SweetAppsLine {',
                                                      'struct SweetAppsLine {')
apps_line_source = load_tu('SweetAppsLine.cpp')
assert 'struct SweetAppsLine' in apps_line_header
apps_line_source = apps_line_source.replace('uint32_t value=*(volatile uint32_t *)(base+off);',
                                            'uint32_t value=readReg(base,off);')
apps_line_source = apps_line_source.replace('*(volatile uint32_t *)(base+off)=value;',
                                            'writeReg(base,off,value);')
apps_line_source = re.sub(r'__asm__ volatile\("(dmb|dsb)[^"]*" ::: "memory"\);', '(void)0;',
                          apps_line_source)
# The route word is the one aligned 64-bit store in the line driver; the fixture
# register file is 32-bit, so it becomes the pair the hardware sees.
apps_line_source = apps_line_source.replace('*(volatile uint64_t *)(gic+kSweetAppsGICRoute)=0;',
                                            'writeReg(gic,kSweetAppsGICRoute,0);writeReg(gic,kSweetAppsGICRoute+4,0);')
assert 'volatile uint32_t *)' not in apps_line_source
assert 'volatile uint64_t *)' not in apps_line_source
assert apps_line_source.count('writeReg(gic,kSweetAppsGICRoute,0)')==1
assert apps_line_source.count('readReg(base,off)')==1 and apps_line_source.count('writeReg(base,off,value)')==1
apps_door_source = load_tu('SweetAppsDoor.cpp')
# The door's lock is the router's serialisation and must not be a no-op: reuse
# the prelude's IOSimpleLock, which asserts nothing about ordering but does hold.
assert 'IOSimpleLock' in apps_door_source
# The query module is a second file in the same kext: it owns the command DB
# decoding and takes nothing but a reader, so the harness links the two exactly
# as the build does - the header before the channel that includes it, the source
# beside it - and no fixture stands in for either.
db_header = re.sub(r'^#include.*\n', '', (arm/'SweetRSCDatabase.h').read_text(), flags=re.M)
db_source = re.sub(r'^#include.*\n', '', (arm/'SweetRSCDatabase.cpp').read_text(), flags=re.M)
assert 'kSweetRscDbHeader' in db_source and 'ReadWord' in db_header
assert 'IOQcomRSC.cpp' not in db_source

tests = r'''
static unsigned writes(){unsigned n=0;for(auto&o:trace)if(o.write)++n;return n;}
static bool logged(const std::string&n){for(auto&m:messages)if(m.find(n)!=std::string::npos)return true;return false;}
static unsigned counted(const std::string&n){unsigned c=0;for(auto&m:messages)if(m.find(n)!=std::string::npos)++c;return c;}
static long rowAt(const std::string&n){for(size_t i=0;i<messages.size();i++)
 if(messages[i].find(n)!=std::string::npos)return (long)i;return -1;}
static void setData(IOService*s,const char*k,const void*p,unsigned n){
 OSData*d=OSData::withBytes(p,n);assert(d);s->setProperty(k,d);d->release();
}
/* The command DB locator answers with a DRAM address and a byte length; the
   block itself starts with version, magic and - at offset 140, the header's last
   word - the flag word whose bit 0 means "not compatible". */
static uintptr_t dbPA=0xb0000100u;
static uint32_t dbBytes=0x400u,dbMagic=0x0c0330dbu,dbVersion=1,dbFlags=0;
static void seedRSC(){
 resetRegs();
 regs[0x18220000]=(2u<<16)|0x1000u;          /* id: major 2, part 0x1000 */
 regs[0x18220004]=0x00000001u;                /* solver present */
 regs[0x1822000cu]=(4u<<27)|(8u<<12)|0x400u;  /* ncpt 4, eight TCS */
 regs[0x18220d00]=0x0000001fu;                /* owner's TCS0 enable mask */
 regs[0x18220d04]=0x00000040u;                /* a completion already pending */
 regs[0x0c3f000c]=dbPA;regs[0x0c3f0010]=dbBytes;
 regs[dbPA]=dbVersion;regs[dbPA+4]=dbMagic;regs[dbPA+140]=dbFlags;
}
/* The block's contents, laid out the way the format says and not the way the
   query module computes: the header, then a rail chunk and a bandwidth chunk,
   their entries, one auxiliary table, and the zero slave that ends the walk.
   The panel's CN0 is the third name in the file, so a query has to walk past
   the two rail entries before it can find it. */
static void seedDb(){
 regs[dbPA+8]=4u;regs[dbPA+12]=(240u-144u)|(2u<<16);          /* slave 4, entries at 144, aux at 240 */
 regs[dbPA+24]=5u|((192u-144u)<<16);regs[dbPA+28]=(240u-144u)|(2u<<16);
 regs[dbPA+40]=0u;regs[dbPA+44]=0u;                            /* the end of the chunk list */
 regs[dbPA+144]=0x616f646cu;regs[dbPA+148]=0x3431u;            /* "ldoa14" */
 regs[dbPA+160]=0x42100u;regs[dbPA+164]=0u;
 regs[dbPA+168]=0x636f646cu;regs[dbPA+172]=0x33u;              /* "ldoc3" */
 regs[dbPA+184]=0x42300u;regs[dbPA+188]=0u;
 regs[dbPA+192]=0x304e43u;regs[dbPA+196]=0u;                   /* "CN0" */
 regs[dbPA+208]=0x500a8u;regs[dbPA+212]=8u;                    /* eight bytes of aux, delta 0 */
 regs[dbPA+216]=0x564341u;regs[dbPA+220]=0u;                   /* "ACV" */
 regs[dbPA+232]=0x50ac0u;regs[dbPA+236]=8u|(8u<<16);           /* aux at 248 */
 regs[dbPA+240]=20000u;regs[dbPA+244]=4u|(5u<<16);             /* CN0: unit, width 4, vcd 5 */
 regs[dbPA+248]=1u;regs[dbPA+252]=0u;                          /* ACV: unit and no width */
}
/* abi/irqAbi/intid: 0 absent, otherwise (length,value) as given. */
static IOService*rscNub(unsigned abiLen=4,uint32_t abiValue=1,
 unsigned irqAbiLen=4,uint32_t irqAbiValue=1,
 unsigned intidLen=4,uint32_t intidValue=37,
 unsigned queryLen=0,uint32_t queryValue=1,
 bool executor=false,unsigned executorLen=4,uint32_t executorValue=1){
 auto*n=fixtureAlloc<IOService>();
 if(abiLen){uint32_t v=abiValue;setData(n,"sweet,rsc-abi",&v,abiLen);}
 if(irqAbiLen){uint32_t v=irqAbiValue;setData(n,"sweet,rsc-irq-abi",&v,irqAbiLen);}
 if(intidLen){uint32_t v=intidValue;setData(n,"sweet,rsc-intid",&v,intidLen);}
 if(queryLen){uint32_t v=queryValue;setData(n,"sweet,rsc-query",&v,queryLen);}
 if(executor){uint32_t v=executorValue;setData(n,"sweet,rsc-executor",&v,executorLen);}
 return n;
}
/* The apps GIC as the delegating tree describes it: GICv3, an implemented line
   range that covers INTID 37, group 1 on, and the line free. `busy` and
   `pending` are the two ways a previous owner can still be on it. */
static constexpr uintptr_t GIC=0x17a00000;
static void seedLine(bool busy=false,bool pending=false){
 regs[GIC+0x000]=0x12u;
 regs[GIC+0x004]=0x7b041du;
 regs[GIC+0xffe8]=0x30u;
 regs[GIC+0x084]=0;regs[GIC+0x104]=busy?1u<<5:0u;regs[GIC+0x204]=pending?1u<<5:0u;
 regs[GIC+0x304]=0;regs[GIC+0xc08]=0;regs[GIC+0x424]=0;
 regs[GIC+0x6128]=0;regs[GIC+0x612c]=0;
}
/* One refusal: no writes, no successful start row, every map released. */
static void refusal(IOService*n,bool seed,const char*detail){
 messages.clear();mapCalls.clear();descriptorRequests.clear();
 if(seed)seedRSC();
 unsigned maps=liveMaps;
 auto*p=fixtureAlloc<IOQcomRSCChannel>();
 assert(!p->start(n));
 assert(logged(detail));
 assert(!logged("phase=start result=0"));
 assert(!p->fRegistered&&!p->fMapped&&!p->fLock);
 assert(writes()==0);
 p->release();
 assert(liveMaps==maps);
}
int main(){
 unsigned acceptedRows=0;
 assert(!live&&!liveMaps);
 /* ---- start refusals: the ABI, then the line, then the windows ---- */
 {	/* no provider object at all: nothing to match, nothing to map */
  messages.clear();mapCalls.clear();descriptorRequests.clear();
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(nullptr));
  assert(!p->fMapped&&!p->fLock&&!p->fRegistered&&p->fReads==0);
  p->release();assert(!live&&!liveMaps&&mapCalls.empty());
 }
 {	/* absent opt-in is the ADT default, not a silent read-only mode */
  auto*n=rscNub(0);
  refusal(n,true,"detail=bad-rsc-abi length=0");
  n->release();
 }
 {	/* a short property is a wrong tree, not a version we can guess */
  auto*n=rscNub(3);
  refusal(n,true,"detail=bad-rsc-abi length=3");
  n->release();
 }
 {	/* the ABI is versioned: 2 is not this card */
  auto*n=rscNub(4,2);
  refusal(n,true,"detail=bad-rsc-abi value=0x2 expected=0x1");
  n->release();
 }
 {	/* the line has its own ABI, and it is mandatory: a channel that cannot
      say which line completes it is not the apps RSC */
  auto*n=rscNub(4,1,0);
  refusal(n,true,"detail=bad-rsc-irq-abi length=0");
  n->release();
 }
 {	/* an irq ABI this firmware does not speak */
  auto*n=rscNub(4,1,4,2);
  refusal(n,true,"detail=bad-rsc-irq-abi value=0x2");
  n->release();
 }
 {	/* absent INTID */
  auto*n=rscNub(4,1,4,1,0);
  refusal(n,true,"detail=bad-rsc-intid length=0");
  n->release();
 }
 {	/* another RSC's completion line must not be adopted as this one's */
  auto*n=rscNub(4,1,4,1,4,38);
  refusal(n,true,"detail=bad-rsc-intid value=38 expected=37");
  n->release();
 }
 {	/* the executor opt-in is versioned like the other three: a short property
      is a wrong tree, and it is refused before any window is mapped */
  auto*n=rscNub(4,1,4,1,4,37,0,1,true,3);
  refusal(n,true,"detail=bad-rsc-executor length=3");
  n->release();
 }
 {	/* an owner request this firmware does not speak is a refusal, not a
      read-only boot with a note */
  auto*n=rscNub(4,1,4,1,4,37,0,1,true,4,2);
  refusal(n,true,"detail=bad-rsc-executor value=0x2 expected=0x1");
  n->release();
 }
 {	/* the query opt-in is versioned like the rest, and a short property is a
      wrong tree rather than a version to guess at */
  auto*n=rscNub(4,1,4,1,4,37,3);
  refusal(n,true,"detail=bad-rsc-query length=3");
  n->release();
 }
 {	/* an opt-in this firmware does not speak */
  auto*n=rscNub(4,1,4,1,4,37,4,2);
  refusal(n,true,"detail=bad-rsc-query value=0x2 expected=0x1");
  n->release();
 }
 {	/* a display RSC description pointed at this class: the DRV base differs */
  rscWrongPA=0;
  auto*n=rscNub();
  refusal(n,true,"detail=address-mismatch");
  rscWrongPA=99;
  n->release();
 }
 {	/* the TCS array is 0x3000 long; a shorter one cannot hold 18 TCS */
  rscShortLen=1;
  auto*n=rscNub();
  refusal(n,true,"detail=length-short");
  rscShortLen=99;
  n->release();
 }
 {	/* a missing range is refused by index, not read as far as it goes */
  rscAbsentIndex=2;
  auto*n=rscNub();
  refusal(n,true,"detail=window-absent");
  rscAbsentIndex=99;
  n->release();
 }
 {	/* a window with no kernel virtual base cannot be read at all */
  rscNoBase=1;
  auto*n=rscNub();
  refusal(n,true,"detail=no-virtual-base");
  rscNoBase=99;
  n->release();
 }
 /* ---- identity refusals, each after the DRV words were read ---- */
 {	/* a layout this firmware cannot parse */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x18220000]=(3u<<16)|0x1000u;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=identity-version")&&logged("major=3"));
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* a decoder word of all ones answered nothing */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x18220004]=0xffffffffu;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=identity-solver"));
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* and the same for the config word */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x1822000cu]=0xffffffffu;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=identity-config"));
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* one TCS too few for the vendor range */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x1822000cu]=(4u<<27)|(7u<<12);
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=tcs-count tcs=7 min=8 max=18"));
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* and one more than a 0x3000 array can hold */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x1822000cu]=(4u<<27)|(19u<<12);
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=tcs-count tcs=19"));
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* NCPT below the vendor floor */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x1822000cu]=(2u<<27)|(8u<<12);
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=ncpt ncpt=2 min=3 max=16"));
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* and above its ceiling */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x1822000cu]=(17u<<27)|(8u<<12);
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=ncpt ncpt=17"));
  assert(writes()==0);p->release();
  n->release();
 }
 /* ---- command DB refusals: the locator is firmware data, not a hint ---- */
 {	/* an address outside the band the owner accepts */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x0c3f000c]=0x1000u;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=command-db-geometry address=0x1000"));
  assert(descriptorRequests.empty());
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* a length too short to hold the header */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[0x0c3f0010]=140u;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=command-db-geometry address=0xb0000100 bytes=140"));
  assert(descriptorRequests.empty());
  assert(writes()==0);p->release();
  n->release();
 }
 {	/* the descriptor itself cannot be built */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  rejectDescriptor=true;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=descriptor-absent"));
  assert(writes()==0);p->release();
  rejectDescriptor=false;
  n->release();
 }
 {	/* the block is not this platform's command DB */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();
  regs[dbPA+4]=0xdeadbeefu;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("detail=command-db-magic magic=0xdeadbeef"));
  assert(writes()==0);p->release();
  n->release();
 }
 /* ---- the channel itself ---- */
 {	/* the read budget, the rows, and the absence of any store */
  auto*n=rscNub();seedRSC();messages.clear();mapCalls.clear();descriptorRequests.clear();
  unsigned maps=liveMaps;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  /* three ADT ranges, in order, and never the GIC window */
  assert(mapCalls.size()==3);
  assert(mapCalls[0]==0&&mapCalls[1]==1&&mapCalls[2]==2);
  /* the DB came from the locator, read-only */
  assert(descriptorRequests.size()==1);
  assert(descriptorRequests[0].first==dbPA&&descriptorRequests[0].second==dbBytes);
  assert(descriptorDirection==kIODirectionIn);
  /* ten loads: three DRV words, two locator words, three DB words, TCS pair */
  assert(p->channelReads()==10);
  assert(writes()==0);
  assert(p->fIdentity.major==2u&&p->fIdentity.tcsCount==8u&&p->fIdentity.ncpt==4u);
  assert(p->fIdentity.dbVersion==1u&&p->fIdentity.dbFlags==0u);
  assert(p->fIntid==37u);
  assert(logged("phase=identity id=0x21000 major=2 solver=0x1 tcs=8 ncpt=4"));
  assert(logged("phase=map resource=apps-drv result=0"));
  assert(logged("phase=map resource=apps-tcs result=0"));
  assert(logged("phase=map resource=apps-db-locator result=0"));
  assert(logged("phase=command-db address=0xb0000100 bytes=1024 version=0x1 flags=0 detail=read-only-map"));
  assert(logged("phase=irq intid=37 detail=described-not-armed"));
  assert(logged("phase=start result=0 windows=4 ranges=3 reads=10 query-reads=0 writes=0 id=0x21000 tcs=8 ncpt=4 db=0xb0000100 db-size=1024 irq=37 query=absent submit=not-ready executor=absent"));
  /* the owner's TCS words were read and left exactly as they were */
  assert(logged("phase=tcs irq-enable=0x1f irq-status=0x40 detail=observed-not-owned"));
  assert(regs[0x18220d00]==0x1fu&&regs[0x18220d04]==0x40u);
  /* order: identity before the DB, the DB before the TCS observation */
  assert(rowAt("phase=identity")<rowAt("phase=command-db"));
  assert(rowAt("phase=command-db")<rowAt("phase=tcs"));
  assert(rowAt("phase=tcs")<rowAt("phase=start result=0"));
  /* the published surface says the same thing the rows do */
  assert(!p->channelSubmitAllowed());
  /* what the ring from this pass has to explain: one row per phase, and the
     command DB's two when its version/flags are not the compatible pair */
  acceptedRows=messages.size();

  /* ---- submit: NotReady with a row, and still not a single store ---- */
  messages.clear();
  uint64_t token=0xffffffffffffffffULL;
  assert(p->submit(7u,0x1000u,0x2u,&token)==kIOReturnNotReady);
  assert(token==0);
  assert(logged("phase=submit result=1 detail=read-only-channel client=0x7 address=0x1000 value=0x2 writes=0"));
  assert(writes()==0);
  /* a caller that passes no token is still refused, not accepted */
  assert(p->submit(7u,0x1000u,0x2u,nullptr)==kIOReturnNotReady);
  assert(writes()==0);

  /* ---- stop releases everything the start acquired ---- */
  messages.clear();
  p->stop(n);
  assert(logged("phase=stop result=0 reads=10 writes=0 submit-attempts=2"));
  assert(liveMaps==maps);
  assert(!p->fMapped&&!p->fLock&&!p->fRegistered);
  /* after stop the channel is not accepting: the refusal names that reason */
  messages.clear();
  assert(p->submit(7u,0x1000u,0x2u,nullptr)==kIOReturnNotReady);
  assert(logged("detail=channel-not-accepting"));
  assert(writes()==0);
  p->release();
  n->release();
 }
 {	/* C3-02: an opted-in tree gets one resource read out of the block through
      the same single load site, and the row has to agree with the words the
      fixture laid down - the panel's bandwidth resource, its unit, and the
      width and vcd the encoder packs above it */
  auto*n=rscNub(4,1,4,1,4,37,4,1);seedRSC();seedDb();messages.clear();mapCalls.clear();
  unsigned maps=liveMaps;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  /* the identity budget is untouched: the query has its own count */
  assert(p->channelReads()==10);
  assert(p->fQueryReads==18);
  assert(logged("phase=db-query name=CN0 result=0 slave=5 address=0x500a8 entry=0xc0 aux=0xf0/8 unit=20000 width=4 vcd=5 matches=1 reads=18"));
  assert(logged("phase=start result=0 windows=4 ranges=3 reads=10 query-reads=18"));
  assert(logged("query=ok"));
  assert(p->fIdentity.queryDone);
  assert(p->fIdentity.querySlave==5u&&p->fIdentity.queryAddress==0x500a8u);
  assert(p->fIdentity.queryUnit==20000u&&p->fIdentity.queryWidth==4u&&p->fIdentity.queryVcd==5u);
  assert(p->fIdentity.queryAuxBytes==8u);
  /* the query sits after the block is mapped and before the owner's TCS words */
  assert(rowAt("phase=command-db")<rowAt("phase=db-query"));
  assert(rowAt("phase=db-query")<rowAt("phase=tcs"));
  assert(counted("phase=")==9);
  assert(writes()==0);
  p->stop(n);
  assert(liveMaps==maps);
  p->release();n->release();
 }
 {	/* without the opt-in the channel is the C3-01 one exactly: no query row, no
      query loads, and a summary that says absent instead of pretending ok */
  auto*n=rscNub();seedRSC();seedDb();messages.clear();
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(!logged("phase=db-query"));
  assert(p->channelReads()==10);
  assert(p->fQueryReads==0);
  assert(logged("query-reads=0"));
  assert(logged("query=absent"));
  assert(counted("phase=")==8);
  assert(writes()==0);
  p->stop(n);p->release();n->release();
 }
 {	/* an opted-in tree whose name is not in this block is a warning with a
      reason, not a failed start: the display owner is the voter, and the
      channel's evidence never depended on that one resource */
  auto*n=rscNub(4,1,4,1,4,37,4,1);seedRSC();seedDb();messages.clear();
  regs[dbPA+192]=0u;                    /* the panel's name is gone */
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(logged("phase=db-query name=CN0 result="));
  assert(logged("stage=matches"));
  assert(logged("detail=query-refused"));
  assert(logged("query=missing"));
  assert(!p->fIdentity.queryDone);
  assert(writes()==0);
  p->stop(n);p->release();n->release();
 }
 {	/* a second start after stop is a real first start, not a refused one */
  auto*n=rscNub();seedRSC();messages.clear();
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(p->channelReads()==10);
  p->stop(n);
  assert(p->start(n));
  assert(p->channelReads()==10);
  assert(writes()==0);
  p->stop(n);
  p->release();
  n->release();
 }
 {	/* a command DB whose version/flags differ from the compatible pair is a
      warning, not a refusal: the display owner reads the same words and only
      warns, so a second reader must not reject a boot its own voter accepts */
  auto*n=rscNub();seedRSC();messages.clear();
  regs[dbPA]=2u;regs[dbPA+140]=1u;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(logged("detail=version-flags-differ"));
  assert(p->fIdentity.dbVersion==2u&&p->fIdentity.dbFlags==1u);
  assert(writes()==0);
  p->stop(n);p->release();
  n->release();
 }
 {	/* the flag word is the header's last word: a DB whose version is the
      compatible one but whose flag bit 0 is set must still warn, and a word at
      any other offset must not be mistaken for flags - the device boot that
      produced the 0140 ring reported 0x6c2e7863 here because the read was one
      header-size past the start instead of one word before its end */
  auto*n=rscNub();seedRSC();messages.clear();
  regs[dbPA]=1u;regs[dbPA+140]=1u;regs[dbPA+144]=0x6c2e7863u;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(logged("detail=version-flags-differ version=0x1 flags=0x1"));
  assert(p->fIdentity.dbVersion==1u&&p->fIdentity.dbFlags==1u);
  assert(writes()==0);
  p->stop(n);p->release();
  n->release();
  /* and with the flag word clear the same DB is silent, even though the word
     past the header is noise */
  auto*n2=rscNub();seedRSC();messages.clear();
  regs[dbPA]=1u;regs[dbPA+140]=0u;regs[dbPA+144]=0x6c2e7863u;
  p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n2));
  assert(!logged("version-flags-differ"));
  assert(logged("version=0x1 flags=0 detail=read-only-map"));
  assert(p->fIdentity.dbFlags==0u);
  assert(writes()==0);
  p->stop(n2);p->release();
  n2->release();
 }
 /* =====================================================================
  * C3-04: the delegating tree. The channel is this boot's apps executor, so
  * the same object now owns the line, the store and the one door. What is
  * proved below is the card's local criterion: one owner can write, the line
  * is published only after it is safely programmed, the old entry point has
  * no second write path, and a non-delegating tree still ends with zero
  * stores.
  * ===================================================================== */
 {	/* start on the delegating tree: four ranges, four windows, no store yet */
  rscExecutor=true;
  auto*n=rscNub(4,1,4,1,4,37,0,1,true);seedRSC();seedLine();messages.clear();mapCalls.clear();
  unsigned maps=liveMaps;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(p->channelIsExecutor()&&p->fExecutor&&p->fDoorPublished);
  /* the GIC window is the fourth range and the fifth window */
  assert(mapCalls.size()==4&&mapCalls[0]==0&&mapCalls[1]==1&&mapCalls[2]==2&&mapCalls[3]==3);
  assert(p->fWindow[kIOQcomRscWindowGIC].base==GIC);
  /* the executor row names the owner, and the start row says the same */
  assert(logged("phase=executor result=0 owner=IOQcomRSCChannel intid=37 windows=5 ranges=4 stores=0 writes-at-start=0 workloop=own"));
  assert(logged("executor=channel"));
  assert(logged("phase=start result=0 windows=5 ranges=4 reads=10"));
  /* the two rows that describe the line and the TCS words name the role they
     describe for: this boot arms the line four steps later, so a row claiming
     "not armed" would be true only of the other mode */
  assert(logged("phase=irq intid=37 detail=described-for-executor"));
  assert(logged("phase=tcs irq-enable=0x1f irq-status=0x40 detail=observed-before-owner"));
  assert(!logged("detail=described-not-armed"));
  assert(!logged("detail=observed-not-owned"));
  /* identity is read once, here, and the identity budget is untouched */
  assert(p->channelReads()==10);
  /* nothing has been stored: the door exists, the line is still the tree's */
  assert(writes()==0);
  assert(p->channelStores()==0);
  /* the door is published and it is the one the old entry point finds */
  const SweetAppsDoor *d=sweet_apps_door();
  assert(d&&d->context==(void*)p&&!strcmp(d->name,"IOQcomRSCChannel"));
  assert(d->stores(d->context)==p->channelStores()&&d->irqs(d->context)==0);
  /* the old entry point answers for this line and only this line */
  assert(!sweet_rpmh_irq(38u));
  /* ---- the identity the owner reports is the one it read ---- */
  uint32_t id=0,solver=0,config=0;
  assert(d->identity(d->context,&id,&solver,&config));
  assert(id==0x21000u&&solver==1u&&config==regs[0x1822000c]);
  assert(p->channelReads()==10);

  /* ---- the door's two accesses are the channel's, not a second path ---- */
  unsigned doorReads=p->fDoorReads;
  assert(d->read(d->context,0)==0x1fu);
  assert(d->read(d->context,4)==0x40u);
  assert(p->fDoorReads==doorReads+2);
  /* a door read is not charged to the identity budget */
  assert(p->channelReads()==10);
  assert(p->channelStores()==0);
  /* an acknowledge through the door is one store, in the one place */
  d->write(d->context,4u,0u);
  assert(regs[0x18220d04]==0u);
  assert(p->channelStores()==1&&d->stores(d->context)==1);
  /* an out-of-window store is refused loudly and stores nothing */
  messages.clear();
  d->write(d->context,0x4000u,0u);
  assert(logged("phase=store result=2 detail=out-of-window"));
  assert(p->channelStores()==1);

  /* ---- the line is programmed once, then admitted ---- */
  assert(!p->fIRQConfigured&&!p->fIRQTouched);
  assert(d->triggerReady(d->context)==kIOReturnSuccess);
  assert(p->fIRQTouched&&p->fIRQConfigured);
  assert(logged("phase=line result=0 intid=37 owner=channel"));
  /* group 1, level, priority 0xa0, routed nowhere yet, and still masked */
  assert(regs[GIC+0x084]&(1u<<5));
  assert((regs[GIC+0xc08]&0xc00u)==0);
  assert((regs[GIC+0x424]&0xff00u)==0xa000u);
  assert(regs[GIC+0x6128]==0&&regs[GIC+0x612c]==0);
  assert(!(regs[GIC+0x104]&(1u<<5)));
  /* a second trigger only verifies; it does not re-program the line */
  unsigned programWrites=writes();
  assert(d->triggerReady(d->context)==kIOReturnSuccess);
  assert(writes()==programWrites);
  /* arm then unmask is the service's order: gate first, line second */
  d->arm(d->context);
  d->unmask(d->context);
  assert(regs[GIC+0x104]&(1u<<5));

  /* ---- the top half masks and posts, nothing else ---- */
  unsigned entries=p->fIRQEntries;
  assert(p->fIRQPosted==0);
  assert(d->irq(d->context));                   /* armed: mask, count, post */
  assert(p->fIRQEntries==entries+1);
  assert(!(regs[GIC+0x104]&(1u<<5)));           /* masked before the workloop */
  assert(p->fIRQPosted==1);
  /* an unarmed line still masks and counts, but posts nothing */
  assert(d->irq(d->context));
  assert(p->fIRQEntries==entries+2);
  assert(p->fIRQPosted==1);
  /* the workloop thread with no client says so and clears the post */
  messages.clear();
  pump();
  assert(logged("detail=no-completion-client"));
  assert(p->fIRQPosted==0);
  /* ---- the installed client runs on the workloop, never in the top half ---- */
  {static unsigned calls=0;static bool accept=true;
   static bool (*cb)(void*)=[](void*)->bool{++calls;return accept;};
   assert(!d->install(d->context,nullptr,cb));
   messages.clear();
   assert(d->install(d->context,(void*)p,cb));
   assert(logged("phase=install result=0 detail=completion-client"));
   d->arm(d->context);
   assert(d->irq(d->context));
   assert(p->fIRQPosted==1&&calls==0);         /* posted, client not yet run */
   pump();
   assert(p->fIRQPosted==0&&calls==1);
   /* a client that refuses is a WARN, and the post is still cleared */
   accept=false;
   messages.clear();
   d->arm(d->context);
   assert(d->irq(d->context));
   pump();
   assert(logged("detail=client-refused"));
   assert(p->fIRQPosted==0&&calls==2);
  }
  /* ---- quiet drops our own source bit and never the owner's ---- */
  messages.clear();
  p->fIRQArmed=1;
  d->quiet(d->context,0x2u);
  assert(p->fIRQArmed==0);
  assert(regs[0x18220d00]==0x1du);
  assert(!(regs[GIC+0x104]&(1u<<5)));

  /* ---- withdraw: the door goes before anything is dismantled ---- */
  messages.clear();
  p->stop(n);
  assert(!sweet_apps_door());
  assert(logged("phase=executor-stop result=0 stores="));
  assert(!p->fDoorPublished&&!p->fIRQEventAdded&&!p->fCompletion);
  p->release();
  assert(liveMaps==maps);
  n->release();
 }
 {	/* a tree that delegates to a line somebody else still drives is refused
      before the door exists: no store, no event source, no publish */
  rscExecutor=true;
  auto*n=rscNub(4,1,4,1,4,37,0,1,true);seedRSC();seedLine(true);messages.clear();
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(!p->start(n));
  assert(logged("phase=executor result=5 detail=line-already-live"));
  assert(writes()==0&&!sweet_apps_door()&&!p->fDoorPublished&&!p->fIRQEventAdded);
  p->release();n->release();
 }
 {	/* a second executor on the same boot is refused by the door, and the
      refusal leaves no store behind and the first owner's door standing */
  rscExecutor=true;
  auto*n=rscNub(4,1,4,1,4,37,0,1,true);seedRSC();seedLine();messages.clear();
  auto*a=fixtureAlloc<IOQcomRSCChannel>();assert(a->start(n));
  auto*n2=rscNub(4,1,4,1,4,37,0,1,true);
  auto*b=fixtureAlloc<IOQcomRSCChannel>();
  assert(!b->start(n2));
  assert(logged("phase=executor result=5 detail=door-refused"));
  assert(sweet_apps_door()&&sweet_apps_door()->context==(void*)a);
  assert(b->channelStores()==0&&!b->fDoorPublished&&!b->fIRQEventAdded);
  b->release();n2->release();
  a->stop(n);a->release();n->release();
  assert(!sweet_apps_door());
 }
 assert(!live&&!liveMaps);
 printf("test-mh2lm-rsc-channel: PASS (%u rows from the accepted pass)\n",(unsigned)acceptedRows);
 return 0;
}
'''


with tempfile.TemporaryDirectory(prefix='mh2lm-rsc-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+extra+apps_door_header+apps_line_header+db_header+
                   rsc_header+apps_door_source+apps_line_source+rsc_source+
                   db_source+tests)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path('/tmp/mh2lm-rsc-kept.cpp').write_text(cpp.read_text())
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=60)