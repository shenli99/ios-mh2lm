#!/usr/bin/env python3
"""Actual C3-01 apps RSC channel against contract-only MMIO fixtures.
Only OS allocation/locking primitives, the device memory backing store and the
command DB descriptor are fixtures: the shipped IOQcomRSCChannel is compiled as
written, with its single register accessor routed to the fixture register file.
The structural checks pin the source to one load and no store at all - the
channel's whole claim is that it cannot write - and the functional cases prove
the ten-load read budget, every start refusal, the observation of the owner's
TCS words without touching them, and a submit that answers NotReady.
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
   by accident on the wrong window. */
static constexpr uintptr_t rscPAs[3]={0x18220000,0x18220d00,0x0c3f000c};
static constexpr size_t rscLens[3]={0x100,0x3000,8};
static bool rscMode=true;
static unsigned rscAbsentIndex=99,rscWrongPA=99,rscShortLen=99,rscNoBase=99;''')
prelude = prelude.replace('int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){',
'''int getResources(){return 0;}IOMemoryMap*mapDeviceMemoryWithIndex(unsigned i){
 if(rscMode){mapCalls.push_back(i);
  /* the channel may only ever ask for the three ADT ranges: index 3 is the GIC
     window of the display tree and a request for it would be a second listener */
  if(i>=3)return nullptr;
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
# One device memory load and no store anywhere: "writes=0" is a property of the
# file, not of a code path, so the absence has to be checkable in the text.
assert raw.count('*(volatile uint32_t *)')==1, raw.count('*(volatile uint32_t *)')
assert raw.count('observed=*(volatile uint32_t *)(w.base+offset);')==1
assert '*(w.base+offset)=' not in raw
assert raw.count('__asm__ volatile("dmb oshld" ::: "memory");')==1
assert 'dmb oshst' not in raw and 'dsb osh' not in raw
assert raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
assert raw.count('#endif /* ARM64_BOARD_CONFIG_MH2LM */')==1
assert header_raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
# The three opt-ins are the whole ADT surface, each read once.
assert raw.count('sweet,rsc-abi')==1
assert raw.count('sweet,rsc-irq-abi')==1
assert raw.count('sweet,rsc-intid')==1
# No second owner of the line: no interrupt source, no registration, no GIC
# access, no lock that a handler could contend on.
for banned in ('registerInterrupt','IOInterruptEventSource','IOSimpleLock',
               'GICEnable','mapDeviceMemoryWithIndex(3','0x17a00000','0x17a0000',
               'writeRegister','kIODirectionInOut'):
    assert banned not in raw, banned
    assert banned not in header_raw, banned
# The DB is mapped with the read-only direction, and only once.
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
# Actual MMIO and the barrier are the only replaced engine statements.
rsc_source = rsc_source.replace('observed=*(volatile uint32_t *)(w.base+offset);',
                                'observed=readReg(w.base,offset);')
rsc_source = rsc_source.replace('__asm__ volatile("dmb oshld" ::: "memory");', '(void)0;')
assert 'readReg(w.base,offset)' in rsc_source and 'volatile uint32_t *)' not in rsc_source
rsc_source = '\n#undef super\n' + rsc_source

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
   block itself starts with version, magic and the flag word at offset 144. */
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
 regs[dbPA]=dbVersion;regs[dbPA+4]=dbMagic;regs[dbPA+144]=dbFlags;
}
/* abi/irqAbi/intid: 0 absent, otherwise (length,value) as given. */
static IOService*rscNub(unsigned abiLen=4,uint32_t abiValue=1,
 unsigned irqAbiLen=4,uint32_t irqAbiValue=1,
 unsigned intidLen=4,uint32_t intidValue=37){
 auto*n=fixtureAlloc<IOService>();
 if(abiLen){uint32_t v=abiValue;setData(n,"sweet,rsc-abi",&v,abiLen);}
 if(irqAbiLen){uint32_t v=irqAbiValue;setData(n,"sweet,rsc-irq-abi",&v,irqAbiLen);}
 if(intidLen){uint32_t v=intidValue;setData(n,"sweet,rsc-intid",&v,intidLen);}
 return n;
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
  assert(logged("phase=start result=0 windows=4 ranges=3 reads=10 writes=0 id=0x21000 tcs=8 ncpt=4 db=0xb0000100 db-size=1024 irq=37 submit=not-ready"));
  /* the owner's TCS words were read and left exactly as they were */
  assert(logged("phase=tcs irq-enable=0x1f irq-status=0x40 detail=observed-not-owned"));
  assert(regs[0x18220d00]==0x1fu&&regs[0x18220d04]==0x40u);
  /* order: identity before the DB, the DB before the TCS observation */
  assert(rowAt("phase=identity")<rowAt("phase=command-db"));
  assert(rowAt("phase=command-db")<rowAt("phase=tcs"));
  assert(rowAt("phase=tcs")<rowAt("phase=start result=0"));
  /* the published surface says the same thing the rows do */
  assert(!p->channelSubmitAllowed());

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
  regs[dbPA]=2u;regs[dbPA+144]=1u;
  auto*p=fixtureAlloc<IOQcomRSCChannel>();
  assert(p->start(n));
  assert(logged("detail=version-flags-differ"));
  assert(p->fIdentity.dbVersion==2u&&p->fIdentity.dbFlags==1u);
  assert(writes()==0);
  p->stop(n);p->release();
  n->release();
 }
 assert(!live&&!liveMaps);
 printf("test-mh2lm-rsc-channel: PASS (%u rows inspected)\n",(unsigned)messages.size());
 return 0;
}
'''


with tempfile.TemporaryDirectory(prefix='mh2lm-rsc-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+extra+rsc_header+rsc_source+tests)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path('/tmp/mh2lm-rsc-kept.cpp').write_text(cpp.read_text())
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=60)