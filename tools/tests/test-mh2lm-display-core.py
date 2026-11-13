#!/usr/bin/env python3
"""Actual MDSS core controller + managed scanout; independent hardware completion.
Usage: test-mh2lm-display-core.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
fixture = here/'test-mh2lm-managed-display.py'
ns = {'__file__': str(fixture), '__name__': 'core_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
prelude = ns['prelude'].replace('static int clockMode=1,managedMode=1;',
    'static int clockMode=1,managedMode=1,coreMode=2;\nstatic unsigned offPLLs=0;\nstatic bool physicalDomain=true,physicalLock=true,physicalUpdate=true,physicalBranches=true;\nstatic uintptr_t dropCoreWrite=0;')
prelude = prelude.replace(' if(!strcmp(k,"sweetdisplayclocks"))',
    ' if(!strcmp(k,"sweetdisplaycore")){*(int*)v=coreMode;return true;}\n if(!strcmp(k,"sweetdisplayclocks"))')
# Writes cannot manufacture read-only PWR_ON/LOCK/CLK_OFF or consume CMD_UPDATE.
prelude = prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){',
'''static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(b==bases[1]){
  if(b+o==dropCoreWrite){trace.push_back({true,b+o,v});return;}
  if(o==0x3000||o==0||o==0x1000){
   trace.push_back({true,b+o,v});regs[b+o]=(v&~0x80000000u)|(regs[b+o]&0x80000000u);
   if(o!=0x3000)regs[b+o]|=1u<<26; // autonomous PCAL status may change during MODE writes
   return;
  }
 }
''')
core_helpers = r'''
static void coreFixture(){
 coreMode=2;physicalDomain=physicalLock=physicalUpdate=physicalBranches=true;dropCoreWrite=0;
 for(unsigned pll=0;pll<2;++pll){uintptr_t b=bases[1]+pll*0x1000;
  regs[b]=offPLLs&(1u<<pll) ? 0 : 0x80000005;
  regs[b+0x38]=offPLLs&(1u<<pll) ? 0 : 1;regs[b+0xc]=1;
  regs[b+4]=pll ? 0x1f : 0x47;regs[b+0x40]=pll ? 0x4000 : 0xe000;
 }
 // Existing MDP/AHB start on PLL1. A disabled PLL1 fixture starts on XO.
 if(offPLLs&2){regs[bases[1]+0x22c0]=regs[bases[1]+0x20cc]=1;}
}
static void physical(SweetMDSS*m){
 if(physicalDomain && !(regs[bases[1]+0x3000]&1))regs[bases[1]+0x3000]|=0x80000000;
 for(unsigned pll=0;pll<2;++pll){uintptr_t b=bases[1]+pll*0x1000;
  if(physicalLock && regs[b+0x38]==1)regs[b]|=0x80000000;
 }
 for(unsigned r=0;r<2;++r){uintptr_t root=bases[1]+(r ? 0x20c8 : 0x22bc);
  if(physicalUpdate)regs[root]&=~0xf1u;
  uintptr_t branch=bases[1]+(r ? 0x200c : 0x2080);
  if(physicalBranches && (regs[branch]&1) && (m->fCorePhase>=CoreBranches)){
   regs[branch]&=~0x80000000u;regs[root]&=~0x80000000u;
  }
 }
}
'''
warm_helpers = ns['tests'].split('int main(){')[0]
warm_helpers = warm_helpers.replace('static void irqTimer', core_helpers+'\nstatic void irqTimer', 1)
warm_helpers = warm_helpers.replace('warmFixture();n=nub();', 'warmFixture();coreFixture();n=nub();')
warm_helpers = warm_helpers.replace('::step(m,r,apps,data);scan();', '::step(m,r,apps,data);physical(m);scan();')
warm_helpers = warm_helpers.replace('assert(m->fScanoutState==SweetScanoutSubmitted&&pool->pinned);', 'if(m->fScanoutState!=SweetScanoutSubmitted)for(auto &msg:messages)fputs(msg.c_str(),stderr);assert(m->fScanoutState==SweetScanoutSubmitted&&pool->pinned);')
helpers = ns['helpers']+warm_helpers
actual = ns['actual']
tests = r'''
static void untilCore(Harness&h,unsigned phase){
 for(unsigned i=0;i<4000&&h.m->fCorePhase!=phase&&h.m->fManagedState!=SweetMDSS::ManagedFault;++i)h.step();
 assert(h.m->fCorePhase==phase);
}
static bool coreWrite(uintptr_t start,uintptr_t end){for(auto op:trace)if(op.write&&op.addr>=start&&op.addr<end)return true;return false;}
// Isolated cold controller on its real owner workloop, before any IRQ/clock/
// DMA ledger exists. No production cold boot entry or DSI is being simulated.
struct Cold {
 IOService*n;SweetRPMh*r;SweetMDSS*m;
 Cold(){
  warmFixture();coreFixture();n=nub();r=make(n);m=new SweetMDSS();
  m->fLoop=IOWorkLoop::workLoop();m->fGate=IOCommandGate::commandGate(m);
  assert(m->fLoop->addEventSource(m->fGate)==0);m->fGateAdded=true;
  m->fClockTimer=IOTimerEventSource::timerEventSource(m,SweetMDSS::clockTimerAction);
  assert(m->fLoop->addEventSource(m->fClockTimer)==0);m->fClockTimerAdded=true;
  m->dp_gcc=bases[0];m->dp_dispcc=bases[1];m->fClockEnabled=true;m->fCoreProfile=2;m->fAccepting=true;
  m->fRPMh=r;r->retain();assert(r->attachConsumer(&m->fRPMhConsumer)==0);
  assert(r->requestDisplayPower(m->fRPMhConsumer,SweetDisplayTurbo,&m->fManagedARC)==0);
  for(unsigned i=0;i<1500&&r->fBusy;++i)::step(m,r);
  assert(!r->fBusy);
  regs[bases[1]+0x3000]=1;
  for(unsigned pll=0;pll<2;++pll){regs[bases[1]+pll*0x1000]=0;regs[bases[1]+pll*0x1000+0x38]=0;}
  const unsigned roots[]={0x22bc,0x20c8,0x22a4,0x20e0};
  for(auto root:roots){regs[bases[1]+root]=0x80000000;regs[bases[1]+root+4]=1;}
  regs[bases[1]+0x2080]=regs[bases[1]+0x200c]=0x80000000;
  trace.clear();
 }
 void begin(){std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);assert(m->beginCoreResources(SweetCoreCold)==0);}
 void step(){::step(m,r);physical(m);}
 void finish(){for(unsigned i=0;i<4000&&m->coreBusy();++i)step();assert(m->fCorePhase==CoreReady&&m->coreReady());}
 ~Cold(){m->stop(n);auto before=trace.size();SweetMDSS::clockTimerAction(m,m->fClockTimer);assert(before==trace.size());
  m->release();queued.clear();destroy(r,n);dropNub(n);assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());rpmhOwner=0;}
};
int main(){
 // Explicit mode/profile failures never vote or change a core register.
 for(unsigned fault=0;fault<4;++fault){
  warmFixture();coreFixture();if(fault==0)coreMode=1;if(fault==1)managedMode=0;
  if(fault==2)clockMode=0;if(fault==3)resourceMode=1;
  auto*d=displayNub();auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&!mdssClockWriter);
 }
 {
  Harness h;h.begin();h.until(SweetMDSS::ManagedCore);
  assert(!sourceWritten()&&!h.pool->pinned);h.finish();assert(h.m->coreReady());
  assert(regs[bases[1]+0x22c0]==0x40f&&regs[bases[1]+0x20cc]==0x100105);
  assert(h.m->fCoreInheritedPLL==3&&!h.m->fCoreOwnedBranches);
  // Running PLLs are retained; no configuration/output/reset writes to either.
  assert(!coreWrite(bases[1],bases[1]+0x1044));
  assert(coreWrite(bases[1]+0x3000,bases[1]+0x3004));
  assert(h.m->localClocksReady()&&h.m->fClockParents[0][0]==0x40f&&h.m->fClockParents[1][0]==0x100105);
  // A canceled watchdog queued during the resource pause cannot invent an
  // RD_PTR timeout after a long acquisition while the IRQ is deliberately Idle.
  h.m->pauseIRQ();fakeTicks+=300000000ULL;irqTimer(h.m);assert(h.m->fIrqState==SweetMDSS::IrqIdle);
  assert(h.m->armIRQ());
  trace.clear();assert(h.m->setPanelPower(false)==0&&h.m->setPanelPower(true)==0);
  assert(!coreWrite(bases[1],bases[1]+0x8000)); // DCS does not gate/collapse the core.
 }
 for(unsigned mask=1;mask<=3;mask+=2){
  offPLLs=mask;Harness h;h.begin();h.finish();assert(h.m->coreReady());
  assert(h.m->fCoreInheritedPLL==(3u&~mask));
  assert(regs[bases[1]+8]==0x44);assert(!coreWrite(bases[1]+0x24,bases[1]+0x30)); // v2 never writes v1 TEST
  if(mask==3)assert(regs[bases[1]+0x1008]==0x44);
 }offPLLs=0;
 // Reject mismatched live rates/FSM/orphan consumers before any resource vote.
 for(unsigned fault=0;fault<6;++fault){Harness h;
  if(fault==0)regs[bases[1]+4]=0x46;
  if(fault==1)regs[bases[1]]|=1u<<20;
  if(fault==2){regs[bases[1]+0x1000]=0;regs[bases[1]+0x1038]=0;}
  if(fault==3)regs[bases[1]+0x22bc]|=1;
  if(fault==4)regs[bases[1]+0x3000]|=2;
  if(fault==5)regs[bases[1]]|=1u<<22; // live PLL rate update is not stable
  trace.clear();assert(h.m->beginScanout(h.pool,h.channel)!=0&&!writes());
 }
 {
  Harness h;h.begin();h.finish();regs[bases[1]+0x3000]=0;physicalDomain=false;trace.clear();
  assert(h.m->brightnessReady()==kIOReturnNotReady);SweetScanoutSnapshot snapshot{};
  assert(h.m->inspectScanout(&snapshot)==kIOReturnNotReady);h.m->pauseIRQ();
  for(auto op:trace)assert(!(op.addr>=0x0ae00000&&op.addr<0x0af00000));
 }
 // Independent completion: shadow CFG write is insufficient while UPDATE
 // stays pending; new pixels remain unpinned and no fallback resumes TE.
 {
  Harness h;h.begin();untilCore(h,CoreRootUpdate);physicalUpdate=false;
  for(unsigned i=0;i<20&&h.m->fScanoutState!=SweetScanoutFailed;++i)h.step();
  assert(h.m->fCorePhase==CoreFault&&h.m->fManagedState==SweetMDSS::ManagedFault&&!sourceWritten()&&!h.pool->pinned);
 }
 {
  offPLLs=1;Harness h;h.begin();untilCore(h,CorePLLRun);physicalLock=false;
  for(unsigned i=0;i<30&&h.m->fScanoutState!=SweetScanoutFailed;++i)h.step();
  assert(h.m->fCorePhase==CoreFault&&!sourceWritten()&&!h.pool->pinned);offPLLs=0;
 }
 for(unsigned fault=0;fault<5;++fault){Harness h;h.begin();untilCore(h,CoreDomain);
  if(fault==0)regs[bases[0]+0xc]=0;
  if(fault==1)regs[bases[1]+0x3000]|=4;
  if(fault==2){dropCoreWrite=bases[1]+0x22c0;h.finish();assert(h.m->coreReady());continue;}
  if(fault==3){h.m->stop(h.d);auto before=trace.size();SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);assert(trace.size()==before);}
  if(fault==4){h.r->willTerminate(h.n,0);emit(gIOTerminatedNotification,h.r);pump();}
  for(unsigned i=0;i<2000&&h.m->fScanoutState!=SweetScanoutFailed;++i)h.step();
  assert(h.m->fScanoutState==SweetScanoutFailed&&!sourceWritten()&&!h.pool->pinned);
  assert(h.m->fManagedRails==15);
 }
 {
  Cold c;c.begin();assert(trace.empty());
  while(c.m->fCorePhase==CoreConfigVote){c.step();
   if(c.m->fCorePhase==CoreConfigVote)
    for(auto op:trace)assert(!(op.addr>=bases[1]&&op.addr<bases[1]+0x20000));
  }
  c.finish();assert(c.m->fCoreOwnedBranches==3&&!c.m->fCoreInheritedPLL);
  for(auto op:trace)assert(!(op.addr>=0x0ae00000&&op.addr<0x0af00000)&&!(op.addr>=DR&&op.addr<DW+0x4000));
  // This controller does not read powered-off pipeline/DSI/PHY/display RSC.
 }
 for(unsigned fault=0;fault<6;++fault){Cold c;
  if(fault==0 || fault>=3){
   if(fault==0)c.m->fScanoutUsed=true;if(fault==3)c.m->fIrqClaimed=true;
   if(fault==4)c.m->fClockTouched=true;if(fault==5)c.m->fDsiPoisoned=true;
   assert(c.m->beginCoreResources(SweetCoreCold)==kIOReturnBusy&&!writes());
   c.m->fIrqClaimed=false;
  }
  else {c.begin();if(fault==1)physicalDomain=false;else physicalBranches=false;
   for(unsigned i=0;i<4000&&c.m->coreBusy();++i)c.step();
   assert(c.m->fCorePhase==CoreFault&&c.m->fClockFault);
  }
 }
 puts("PASS: real core/managed controllers: explicit v2 opt-in, ARC/CN0 before GDSC, independent PWR/LOCK/UPDATE/CLK_OFF, cold domain+two PLLs+AHB/MDP branches without domain reads, live PLL preservation, controlled snapshots, fresh scanout, faults/stop/provider loss and zero objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-core-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+helpers+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
