#!/usr/bin/env python3
"""Actual C6-01 DISPCC backend against contract-only MMIO fixtures.
Only OS allocation/locking/primitive and the device memory backing store are
fixtures: the shipped IOQcomDispCC backend is compiled as written, on top of the
shipped C1 provider. The structural checks pin the card's "初始化写计数为零" to
the absence of an access path rather than to a counter, and the functional cases
prove the identity/window refusals, the bounded entry wait, the two published
descriptors, and - through the provider's own admission - that the access
prerequisite is enforced and that the domain requirement is refused instead of
reported ready.
Usage: test-mh2lm-dispcc-backend.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
# Reuse the C1 provider fixture and its display-service primitives.
existing = Path(__file__).with_name('test-mh2lm-clock-provider.py')
ns = {'__file__': str(existing), '__name__': 'dispcc_fixture', '__doc__': ''}
exec(compile(existing.read_text().split('arm = Path(sys.argv[1])')[0], str(existing), 'exec'), ns)

prelude = ns['prelude']
extra = ns['extra']
# IOQcomDispCC.h declares its window with IOByteCount, exactly as IOQcomGCC.h
# does; the kernel scalar typedefs are the only thing the fixture supplies.
prelude += ('\nusing IOPhysicalAddress=uint64_t;using IOVirtualAddress=uintptr_t;'
            'using IOByteCount=uint64_t;\n')

# The diagnostic entry is a 5 s delayed thread call on the device. Queue it so a
# case can register the real backend first and then run the listing, which is
# the ordering the delay exists for.
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
assert 'delayedCalls' in extra and 'delayedCalls.push_back' in extra and 'fireDelayed()' in extra

# The DISPCC node's reg 0 resolves to the controller's own base, not to the GCC
# page the positional fixture table happens to put at index 0: the backends are
# matched by node name, so the address has to come from the node.
prelude = prelude.replace('static unsigned badMap=99',
 'static bool dispccNub=false;\n'
 '/* the ADT resource tree maps the whole declared window; the minimum tree maps\n'
 ' * one page, and the card requires the declared length, so the default here is\n'
 ' * the audited resource-mode length. */\n'
 'static size_t dispccLength=0x20000;\n'
 'static unsigned badMap=99', 1)
prelude = prelude.replace('if(rpmhNub){assert(i<4);',
 'if(dispccNub){assert(i==0);m->customPA=0x0af00000;m->customSize=dispccLength;}\n'
 'if(rpmhNub){assert(i<4);', 1)
assert 'dispccNub' in prelude

arm = Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
header = re.sub(r'^#include.*\n', '', (arm/'IOQcomClock.h').read_text(), flags=re.M)
header = header.replace('class IOQcomClockHandle : public OSObject {',
                        'struct IOQcomClockHandle : public OSObject {')
header = header.replace('class IOQcomClockProvider : public IOService {',
                        'struct IOQcomClockProvider : public IOService {')
provider = re.sub(r'^#include.*\n', '', (arm/'IOQcomClock.cpp').read_text(), flags=re.M)

disp_header = re.sub(r'^#include.*\n', '', (arm/'IOQcomDispCC.h').read_text(), flags=re.M)
disp_header = disp_header.replace('class IOQcomDispCC : public IOQcomClockController {',
                                  'struct IOQcomDispCC : public IOQcomClockController {')
# The harness reads the mapping bookkeeping to prove the window is released, so
# the section labels go; no member changes meaning.
disp_header = re.sub(r'^\s*(private|protected):\s*$', 'public:', disp_header, flags=re.M)
assert 'struct IOQcomDispCC' in disp_header

raw = (arm/'IOQcomDispCC.cpp').read_text()

# The harness compiles this file on top of a *struct* provider, which turns the
# entry's default-private members public and hides access-control mistakes that
# the kernel build catches (0155 did not link: registeredWrites was not
# reachable through an entry pointer).  The shipped header is therefore read
# with its own access labels, and every call this backend makes through the
# entry is required to land in the entry's public section.
def access_sections(text, cls):
    body = text.split('class '+cls+' ', 1)[1].split('\n};', 1)[0]
    body = body.split('{', 1)[1]
    section, out = 'private', {}
    for line in body.splitlines():
        label = re.match(r'\s*(public|protected|private)\s*:\s*$', line)
        if label:
            section = label.group(1)
            continue
        for name in re.findall(r'(\w+)\s*\(', line):
            out.setdefault(name, set()).add(section)
    return out

# The raw file: `header` above is already the struct the harness compiles.
provider_access = access_sections((arm/'IOQcomClock.h').read_text(), 'IOQcomClockProvider')
entry_calls = sorted(set(re.findall(r'\bentry\s*->\s*(\w+)\s*\(', raw)))
assert entry_calls == ['registerController', 'registeredWrites', 'withdrawController'], entry_calls
for name in entry_calls:
    assert 'public' in provider_access.get(name, set()), (
        "IOQcomDispCC calls the entry's %s(), which the shipped header does not "
        "make public: the harness struct would hide that and the kernel build "
        "would refuse it" % name)
# And the cross-controller access prerequisite is the one thing this backend
# declares but cannot verify alone: it must stay a declaration, not a probe.
assert 'registerWrites' not in [n for n in entry_calls]
# The card's zero-write claim is structural: the mapping is held for the
# lifecycle and never resolved, so no load or store can exist at all.
assert raw.count('getVirtualAddress')==0, raw.count('getVirtualAddress')
assert raw.count('*(volatile')==0
assert raw.count('readReg')==0 and raw.count('writeReg')==0
assert raw.count('mapDeviceMemoryWithIndex(0)')==1
assert raw.count('IOLockAlloc')==0          # nothing mutable after start
# One declared window, one address, one length: the C0-02 numbers, not defaults.
# The address is the header's constant and nowhere re-typed in the driver.
assert raw.count('kIOQcomDispWindowPhysical')==4   # stored, compared, and both rows
assert '0xaf00000' not in raw and '0x20000' not in raw
assert 'kIOQcomDispWindowPhysical=0x0af00000ULL' in (arm/'IOQcomDispCC.h').read_text()
assert 'kIOQcomDispWindowLength=0x20000ULL' in (arm/'IOQcomDispCC.h').read_text()
assert 'fWindow.map=provider?provider->mapDeviceMemoryWithIndex(0):NULL;' in raw
# Capability honesty: QueryRate exists in the contract and is deliberately not
# advertised while copyClockState answers Unsupported.
assert raw.count('kIOQcomCapQueryRate')==0
assert raw.count('kIOQcomCapQueryConfig')==2   # one per descriptor, both identical
assert 'return kIOReturnUnsupported;' in raw
# The two opt-outs the ADT owns are read from the node, never assumed.
assert raw.count('sweet,dispcc-abi')==1
assert raw.count('sweet,dispcc-variant')==1
assert raw.count('#if defined(ARM64_BOARD_CONFIG_MH2LM)')==1
assert raw.count('#endif /* ARM64_BOARD_CONFIG_MH2LM */')==1
disp_source = re.sub(r'^#include.*\n', '', raw, flags=re.M)
# One translation unit each in the kernel, two in this harness: rename the
# file-scope enum tag and reset the `super` macro between them.
disp_source = disp_source.replace('kIOQcomLogBuf','kIOQcomDispLogBuf')
disp_source = '#undef super\n' + disp_source

tests = r'''
static unsigned writes(){unsigned n=0;for(auto&o:trace)if(o.write)++n;return n;}
static unsigned reads(){unsigned n=0;for(auto&o:trace)if(!o.write)++n;return n;}
static bool logged(const std::string&n){for(auto&m:messages)if(m.find(n)!=std::string::npos)return true;return false;}
static unsigned counted(const std::string&n){unsigned c=0;for(auto&m:messages)if(m.find(n)!=std::string::npos)++c;return c;}
static void setData(IOService*s,const char*k,const void*p,unsigned n){
 OSData*d=OSData::withBytes(p,n);assert(d);s->setProperty(k,d);d->release();
}
static IOService*bareNub(){
 auto*n=fixtureAlloc<IOService>();uint32_t abi=1;setData(n,"sweet,clock-abi",&abi,4);return n;
}
/* The node the entry is matched against, optionally with the listing opt-in
 * the C6 image arms. */
static IOService*clockNub(bool diag=false){
 auto*n=bareNub();if(diag){uint32_t one=1;setData(n,"sweet,clock-diag",&one,4);}return n;
}
static IOQcomClockProvider*startProvider(IOService*n){
 auto*p=fixtureAlloc<IOQcomClockProvider>();assert(p->start(n));return p;
}
/* The ADT node this backend is matched against: sweet,dispcc with the ABI and
 * the variant the generator emits. Everything is a knob so a refusal can be
 * aimed at exactly one property. */
/* The minimum a controller has to be for the access walk: an identity in
 * range and the contract's three virtuals. */
struct GccStandIn : IOQcomClockController {
 uint32_t id=0;
 uint32_t clockControllerID(void)const APPLE_KEXT_OVERRIDE{return id;}
 IOReturn copyClockInfo(uint32_t,IOQcomClockInfo*)APPLE_KEXT_OVERRIDE{return kIOReturnNotFound;}
 IOReturn copyClockState(uint32_t,IOQcomClockState*)APPLE_KEXT_OVERRIDE{return kIOReturnUnsupported;}
};
static IOService*dispNub(uint32_t variant=0x8150,bool abi=true,bool haveVariant=true,
 unsigned abiLength=4,uint32_t abiValue=1){
 dispccNub=true;
 auto*n=fixtureAlloc<IOService>();
 if(abi){setData(n,"sweet,dispcc-abi",&abiValue,abiLength);}
 if(haveVariant)setData(n,"sweet,dispcc-variant",&variant,4);
 return n;
}
/* A registered controller for the block DISPCC's registers depend on. The
 * admission walk only needs it to exist; nothing here describes a clock or
 * touches hardware, and no case enables anything through it. */
static IOQcomClockController*gccStandIn(IOQcomClockProvider*p){
 auto*c=fixtureAlloc<GccStandIn>();c->id=kIOQcomCCGCC;
 assert(p->registerController(c)==kIOReturnSuccess);return c;
}
static IOQcomDispCC*startDisp(IOService*n){auto*d=fixtureAlloc<IOQcomDispCC>();assert(d->start(n));return d;}

int main(){
 assert(writes()==0&&reads()==0&&regs.empty());

 /* -- identity refusals come before the window is even asked for ------ */
 struct {const char*detail;uint32_t variant;bool abi;bool haveVariant;unsigned abiLength;uint32_t abiValue;} bad[]={
  {"detail=missing-dispcc-abi",0x8150,false,true,4,1},
  {"detail=missing-dispcc-abi",0x8150,true,true,3,1},
  {"detail=missing-dispcc-abi",0x8150,true,true,4,2},
  {"detail=missing-variant",   0x8150,true,false,4,1},
  {"detail=variant-mismatch observed=0x8151 required=0x8150",0x8151,true,true,4,1},
 };
 for(auto&c:bad){
  messages.clear();mapCalls.clear();
  auto*n=dispNub(c.variant,c.abi,c.haveVariant,c.abiLength,c.abiValue);
  auto*d=fixtureAlloc<IOQcomDispCC>();
  assert(!d->start(n));
  assert(logged(c.detail));
  assert(counted("phase=register result=")==1);
  assert(logged("planID=C6 resource=dispcc phase=register result="));
  /* an identity this driver does not know is not a reason to map first */
  assert(mapCalls.empty());
  assert(!logged("phase=map"));
  assert(!logged("phase=access")&&!logged("phase=clock"));
  assert(!d->fMapped&&d->fWindow.map==nullptr);
  d->release();n->release();assert(!live&&!liveMaps);
 }

 /* -- window refusals: absent, translated elsewhere, or short ---------- */
 {
  messages.clear();mapCalls.clear();
  missingMap=0;
  auto*n=dispNub();auto*d=fixtureAlloc<IOQcomDispCC>();
  assert(!d->start(n));
  assert(logged("phase=map result="+std::to_string(kIOReturnNotReady)+" detail=window-absent"));
  assert(mapCalls.size()==1&&mapCalls[0]==0);
  assert(!logged("phase=access")&&!logged("phase=register result=0"));
  missingMap=99;
  assert(!d->fMapped&&d->fWindow.map==nullptr);
  d->release();n->release();assert(!live&&!liveMaps);
 }
 {
  messages.clear();mapCalls.clear();
  badMap=0;
  auto*n=dispNub();auto*d=fixtureAlloc<IOQcomDispCC>();
  assert(!d->start(n));
  assert(logged("detail=address-mismatch observed=0xaf00004 required=0xaf00000"));
  assert(!logged("phase=register result=0"));
  badMap=99;
  assert(!d->fMapped&&d->fWindow.map==nullptr);
  d->release();n->release();assert(!live&&!liveMaps);
 }
 {
  messages.clear();mapCalls.clear();
  /* the minimum tree maps one page of the RSCC window; the card's resource is
   * the whole declared 0x20000, so a short mapping is refused, not rounded. */
  dispccLength=0x3000;
  auto*n=dispNub();auto*d=fixtureAlloc<IOQcomDispCC>();
  assert(!d->start(n));
  assert(logged("detail=length-short observed=0x3000 required=0x20000"));
  assert(!logged("phase=register result=0"));
  dispccLength=0x20000;
  assert(!d->fMapped&&d->fWindow.map==nullptr);
  d->release();n->release();assert(!live&&!liveMaps);
 }

 /* -- the entry is a separate IOService: wait bounded, then say so ---- */
 {
  messages.clear();mapCalls.clear();
  uint64_t before=fakeTicks;
  auto*n=dispNub();auto*d=fixtureAlloc<IOQcomDispCC>();
  assert(!d->start(n));
  assert(logged("phase=register result="+std::to_string(kIOReturnNotReady)+" detail=provider-absent"));
  /* 200 tries of 5 ms, and not one millisecond more: the wait is a budget, not
   * a policy of retrying until something appears */
  assert(fakeTicks-before==200u*5u*1000u);
  assert(!logged("phase=register result=0")&&!logged("phase=clock"));
  assert(!d->fMapped&&d->fWindow.map==nullptr);
  d->release();n->release();assert(!live&&!liveMaps);
 }

 /* -- accepted: two descriptors, no write, no read, and a released map - */
 {
  messages.clear();mapCalls.clear();
  auto*pn=clockNub();auto*p=startProvider(pn);
  auto*n=dispNub();auto*d=startDisp(n);
  assert(mapCalls.size()==1&&mapCalls[0]==0);
  assert(logged("planID=C6 resource=dispcc phase=map result=0 physical=0xaf00000 length=0x20000 vbase=0"));
  assert(logged("planID=C6 resource=dispcc phase=access result=0 declared=cc1:0x0 bit=0x2 gate=admission"));
  /* domain=0, not domain=0x0: the driver prints %#x, and C prints a plain 0 for
   * zero, so the ring cannot contain a zero with a 0x prefix. */
  assert(logged("planID=C6 resource=cc2:0x1 consumer=dispcc phase=clock kind=1 halt=1 caps=0x1 "
                "parents=1 domain=0 access=0x2 offset=0x200c pinned=1"));
  assert(logged("planID=C6 resource=cc2:0x2 consumer=dispcc phase=clock kind=2 halt=2 caps=0x1 "
                "parents=4 domain=0x1 access=0x2 offset=0x20c8 pinned=0"));
  assert(logged("planID=C6 resource=dispcc phase=register result=0 controller=cc2:0x0 clocks=2 writes=0"));
  /* the completion row is the last C6 row of the pass */
  long last=-1;for(size_t i=0;i<messages.size();i++)if(messages[i].find("planID=C6")!=std::string::npos)last=(long)i;
  assert(messages[last].find("phase=register result=0")!=std::string::npos);
  /* and the whole pass touched no register at all, on either side */
  assert(writes()==0&&reads()==0&&regs.empty());
  assert(!logged("result=failed"));
  assert(d->fMapped&&d->fWindow.map!=nullptr&&d->fRegistered);
  IOMemoryMap*m=d->fWindow.map;
  d->stop(n);
  assert(logged("planID=C6 resource=dispcc phase=stop result=0 released=1 withdrawn=1"));
  assert(d->fWindow.map==nullptr&&!d->fMapped&&!d->fRegistered);
  /* the mapping object itself is gone, not merely forgotten */
  assert(liveMaps==0);
  (void)m;
  /* a second stop is a no-op, and free() leaves nothing behind */
  d->stop(n);
  assert(logged("phase=stop result=0 released=0 withdrawn=0"));
  d->release();n->release();
  p->stop(pn);p->release();pn->release();
  dispccNub=false;
  assert(!live&&!liveMaps&&!logged("result=failed"));
 }

 /* -- the descriptors are what admission sees, and it is not lenient -- */
 {
  messages.clear();
  auto*pn=clockNub(true);auto*p=startProvider(pn);
  auto*n=dispNub();auto*d=startDisp(n);
  IOQcomClockInfo info;uint32_t reason=kIOQcomDepReady;
  /* without the block controller the resource is refused for its access
   * prerequisite, and the row is the provider's, not this backend's */
  IOReturn r=p->admit(d,kIOQcomDispMdp,"c6-test",&info,&reason,false);
  assert(r==kIOReturnNotReady&&reason==kIOQcomDepAccess);
  assert(logged("phase=admit result="+std::to_string(kIOReturnNotReady)+" detail=access-absent"));
  /* with it registered the access half passes, and the walk stops exactly
   * where this candidate cannot go yet: no domain owner exists for C6-01 */
  auto*g=gccStandIn(p);
  messages.clear();reason=kIOQcomDepReady;
  assert(p->admit(d,kIOQcomDispMdp,"c6-test",&info,&reason,false)==kIOReturnSuccess);
  assert(reason==kIOQcomDepReady);
  assert(info.kind==kIOQcomClockBranch&&info.capabilities==kIOQcomCapQueryConfig);
  assert(info.pinned&&info.possibleCount==1&&info.possible[0].source==kIOQcomSourceLocal);
  assert(info.possible[0].controller==kIOQcomCCDispCC&&info.possible[0].localID==kIOQcomDispMdpSrc);
  /* requirements=true admits the branch: it is structurally complete and its
   * GCC dependency is present. The walk is structural - it proves each Local
   * parent exists, is well formed and is not a cycle - and it does not re-judge
   * the parent's domain, so the branch's admission is not the place the domain
   * gate shows up. Asking for the RCG itself is, and that is the verdict C6-02
   * and C6-03 have to respect instead of assuming the pair is ready. */
  reason=kIOQcomDepReady;
  assert(p->admit(d,kIOQcomDispMdp,"c6-test",&info,&reason,true)==kIOReturnSuccess);
  assert(reason==kIOQcomDepReady);
  reason=kIOQcomDepReady;
  assert(p->admit(d,kIOQcomDispMdpSrc,"c6-test",&info,&reason,true)==kIOReturnNotReady);
  assert(reason==kIOQcomDepDomain);
  assert(logged("detail=domain-absent domain=0x1"));
  /* the RCG declares the ceiling corners the vendor table names, and the rate
   * it cannot decode is not offered */
  IOQcomClockInfo src;reason=kIOQcomDepReady;
  assert(p->admit(d,kIOQcomDispMdpSrc,"c6-test",&src,&reason,false)==kIOReturnSuccess);
  assert(src.kind==kIOQcomClockRCG&&src.domain==kIOQcomDomainCX);
  assert(src.cornerMaxHz[0]==19200000ull&&src.cornerMaxHz[1]==171428571ull&&
         src.cornerMaxHz[2]==300000000ull);
  assert((src.capabilities&kIOQcomCapQueryRate)==0);
  assert(src.possibleCount==4);
  assert(src.possible[0].source==kIOQcomSourceXO&&src.possible[0].localID==0);
  /* the three PLL selectors carry no controller: they are RCG parent-map
   * entries, and claiming one as a clock identity would invent an edge */
  for(unsigned i=1;i<4;i++){
   assert(src.possible[i].source==kIOQcomSourceUnknown);
   assert(src.possible[i].controller==0);
  }
  /* unknown local IDs and states are refused, never defaulted */
  IOQcomClockState state;
  assert(d->copyClockInfo(3,&src)==kIOReturnNotFound);
  assert(d->copyClockState(kIOQcomDispMdp,&state)==kIOReturnUnsupported);
  assert(d->copyClockState(3,&state)==kIOReturnNotFound);
  char name[48];assert(d->copyClockName(kIOQcomDispMdpSrc,name,sizeof name));
  assert(std::string(name)=="disp_cc_mdss_mdp_clk_src");
  assert(!d->copyClockName(9,name,sizeof name));

  /* the opt-in listing prints this backend once the entry is up, and the row
   * is the device's C6 evidence that the RCG cannot be reached without a
   * domain owner */
  messages.clear();
  fireDelayed();
  /* The listing names each clock the way copyClockName spells it, and its
   * deps column is the runtime verdict on the pair: the branch is structurally
   * ready, the RCG is not. Those two rows are what the C6-01 boot has to match
   * on the device. */
  assert(logged("planID=C1 resource=cc2:0x1 consumer=disp_cc_mdss_mdp_clk"));
  assert(logged("planID=C1 resource=cc2:0x2 consumer=disp_cc_mdss_mdp_clk_src"));
  assert(logged("phase=diag-done result=0 detail=zero-writes writes=0"));
  assert(!logged("result=failed"));

  d->stop(n);
  p->withdrawController(g);
  g->release();			/* the fixture's own reference to the stand-in */
  d->release();n->release();
  p->stop(pn);p->release();pn->release();
  dispccNub=false;
  assert(!live&&!liveMaps);
 }
 printf("PASS: actual C6-01 DISPCC backend; ADT-matched identity (abi/variant) refused "
        "before the window is asked for, absent/address/short window refusals with clean "
        "unmapping, the bounded entry wait, two published descriptors, one declaration row "
        "for the GCC access prerequisite enforced by the provider's own admission walk, "
        "the domain requirement refused rather than reported ready, and zero register "
        "access by construction\n");
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='mh2lm-dispcc-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+extra+header+disp_header+provider+disp_source+tests)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path('/tmp/mh2lm-dispcc-kept.cpp').write_text(cpp.read_text())
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=60)