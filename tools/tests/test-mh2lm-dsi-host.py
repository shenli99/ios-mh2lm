#!/usr/bin/env python3
"""Actual DSI v2.3 host rebuild through real managed scanout/core/resource owners.
Usage: test-mh2lm-dsi-host.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
fixture = here/'test-mh2lm-display-core.py'
ns = {'__file__': str(fixture), '__name__': 'host_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
prelude = ns['prelude'].replace('static unsigned offPLLs=0;', '''static unsigned offPLLs=0;
static int hostMode=1;
static uintptr_t dropHostWrite=0;
static bool dropResetExit=false,hostResetError=false;
static unsigned hostResets=0;''')
prelude = prelude.replace(' if(!strcmp(k,"sweetdisplaycore"))',
    ' if(!strcmp(k,"sweetdsihost")){*(int*)v=hostMode;return true;}\n if(!strcmp(k,"sweetdisplaycore"))')
prelude = prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){', '''static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(b==bases[6] && hostMode){
  if(b+o==dropHostWrite || (o==0x118&&!v&&dropResetExit)){trace.push_back({true,b+o,v});return;}
  if(o==0x110){trace.push_back({true,b+o,v});
   regs[b+o]=(regs[b+o]&0x51115501u&~v)|(v&0xa222aa02u);return;}
  if(o==0x118 && !v){
   ++hostResets;
   // A real reset destroys old host configuration. Reads cannot stand in for rebuild.
   for(unsigned off=4;off<0x400;off+=4)if(off!=8&&off!=12)regs[b+off]=0;
   if(hostResetError)regs[b+0x110]=1u<<24;
  }
 }
''')
helpers = ns['helpers']
helpers = helpers.replace('struct Harness {', r'''
static void hostFixture(){
 hostMode=1;dropHostWrite=0;dropResetExit=false;hostResetError=false;hostResets=0;
 regs[bases[6]]=0x20030000;regs[bases[6]+4]=0x1f5;
 regs[bases[6]+12]=0x1310; // observed FIFO, not an invented all-lanes-empty predicate
 regs[bases[6]+0x110]=0x102; // stale done + enable; zero must not ACK done
 regs[bases[6]+0x40]=0x100008;regs[bases[6]+0x84]=4;
 regs[bases[6]+0x58]=0x0ca90039;regs[bases[6]+0x5c]=0x09240438;
}
struct Harness {''')
helpers = helpers.replace('warmFixture();coreFixture();n=nub();', 'warmFixture();coreFixture();hostFixture();n=nub();')
helpers = helpers.replace('physical(m);scan();',
    'physical(m);{std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);SweetMDSS::dsiTimerAction(m,m->fDsiTimer);}scan();')
actual = ns['actual']
tests = r'''
static void untilHost(Harness&h,unsigned phase){
 for(unsigned i=0;i<4000&&h.m->fHostPhase!=phase&&h.m->fManagedState!=SweetMDSS::ManagedFault;++i)h.step();
 assert(h.m->fHostPhase==phase);
}
static void fail(Harness&h,unsigned externalAuto=0){
 for(unsigned i=0;i<1000&&h.m->fScanoutState!=SweetScanoutFailed;++i)h.step();
 if(h.m->fScanoutState!=SweetScanoutFailed)for(auto&msg:messages)fputs(msg.c_str(),stderr);
 assert(h.m->fScanoutState==SweetScanoutFailed&&h.m->fManagedState==SweetMDSS::ManagedFault);
 assert(!sourceWritten()&&!h.pool->pinned&&!h.m->managedAccessReady());
 assert(h.m->fManagedRails==15u&&h.m->fManagedARC&&h.m->fManagedBandwidth&&h.m->fManagedClock);
 assert(regs[bases[4]+0x2b4]==externalAuto&&!(regs[bases[4]+0x288]&(1u<<20)));
 auto before=trace.size();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.scan();assert(before==trace.size());
}
int main(){
 // Bad opt-in dependencies close admission before writes/requests, clean partial start.
 for(unsigned fault=0;fault<3;++fault){
  warmFixture();coreFixture();hostFixture();auto*d=displayNub();
  if(fault==0)hostMode=2;if(fault==1)coreMode=0;if(fault==2)managedMode=0;
  auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
  mdssClockWriter=mdssNoCWriter=0;
 }
 // Default path leaves host alone, including unknown revisions: no stealth reset.
 {Harness h;h.m->fHostProfile=0;h.begin();h.finish();assert(!hostResets);
  for(auto op:trace)assert(!op.write||op.addr<bases[6]||op.addr>=bases[6]+0x400);}
 // Wrong live revision/active FIFO/error is refused before quiesce/votes.
 for(unsigned fault=0;fault<5;++fault){Harness h;
  if(fault==0)regs[bases[6]]=0x20020000;if(fault==1)regs[bases[6]+0x15c]=1;
  if(fault==2)regs[bases[6]+0x110]|=1u<<24;if(fault==3)regs[bases[6]+0x68]=1;
  if(fault==4)h.m->fDsiPoisoned=true;
  assert(h.m->beginScanout(h.pool,h.channel)==kIOReturnNotReady&&!writes()&&!h.m->fScanoutUsed);
 }
 {
  Harness h;assert(!h.m->fDsiTimer);h.begin();untilHost(h,SweetMDSS::HostResetHold);
  assert(h.m->hostBusy()&&!sourceWritten()&&!h.pool->pinned&&h.m->fClockTouched);
  assert(!hostResets&&regs[bases[6]+0x110]==0x100); // stale W1C status preserved pre-reset
  // Too-early callbacks cannot shorten the reset pulse or touch reset-domain registers.
  auto before=trace.size();auto now=fakeTicks;fakeTicks=h.m->fHostWait-1;
  SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
  for(unsigned i=before;i<trace.size();++i)assert(trace[i].addr<bases[6]||trace[i].addr>=bases[6]+0x400);
  assert(regs[bases[6]+0x118]==1);fakeTicks=now;
  uint64_t token=0;uint32_t word=0x80050029;uint8_t bytes[]={5,1,0,0,0,0,1,0x29};
  assert(h.m->brightnessReady()!=0&&h.m->setPanelPower(false)!=0&&h.m->dcsWrite(&word,1)!=0);
  h.m->fDsiEnabled=true;assert(h.m->beginDSISequence(bytes,sizeof(bytes),&token)!=0&&!token);
  assert(h.m->retainLocalClocks(SweetClockDSI0,&token)==kIOReturnBusy);
  assert(h.m->releaseLocalClocks(h.m->fManagedClock)==kIOReturnBusy);
  assert(h.m->retainBandwidth(managedBandwidth(),&token)==kIOReturnBusy);
  assert(h.m->releaseBandwidth(h.m->fManagedBandwidth)==kIOReturnBusy);
  SweetScanoutSnapshot snap{};before=trace.size();assert(h.m->inspectScanout(&snap)==kIOReturnNotReady&&trace.size()==before);
  SweetMDSS::probeAction(h.m,h.m->fProbe);assert(trace.size()==before);
  untilHost(h,SweetMDSS::HostConfigure);
  assert(hostResets==1&&!sourceWritten()&&!h.pool->pinned);
  assert(regs[bases[6]+0x58]==0x0ca90039&&regs[bases[6]+0x5c]==0x09240438);
  // Controller readback/idle alone never pins or reports successful scanout.
  h.finish();assert(h.m->fHostPhase==SweetMDSS::HostReady&&h.m->hostConfiguration());
  assert(h.m->fScanoutStatus.done==0x100&&h.m->fFrameCount>h.m->fScanoutSubmitFrame);
  assert(regs[bases[6]+0xb0]==0x8421&&regs[bases[6]+0x84]==0x80000004);
  for(auto op:trace)if(op.write){
   assert(!(op.addr>=bases[12]&&op.addr<bases[12]+0x760)); // no PHY/PLL writes
   assert(op.addr!=bases[6]+0x12c&&op.addr!=bases[6]+0x90&&op.addr!=bases[6]+0x17c);
   if(op.addr==bases[6]+0x110)assert(!op.value); // no status ACK/IRQ enables
  }
  auto count=hostResets;assert(h.m->setPanelPower(false)==0&&h.m->setPanelPower(true)==0);
  assert(hostResets==count); // ordinary DCS lifecycle does not reset host/withdraw votes
 }
 // Plain configuration write observations do not gate Ready; real pixel completion does.
 const unsigned drop[]={0x40,0x44,0x58,0x5c,0x60,0x64,0x84,0xb0,0xbc,0xc4,0xcc,0x1b8};
 for(auto offset:drop){Harness h;h.begin();untilHost(h,SweetMDSS::HostResetHold);
  dropHostWrite=bases[6]+offset;untilHost(h,SweetMDSS::HostReady);
  assert(h.m->fScanoutState!=SweetScanoutActive);h.finish();assert(h.m->fScanoutState==SweetScanoutActive);}
 // Pre-reset checks, resource loss, errors, stuck reset, timeout, and stop in every phase.
 for(unsigned fault=0;fault<10;++fault){Harness h;h.begin();
  if(fault<3){h.until(SweetMDSS::ManagedHost);
   if(fault==0){regs[bases[6]+0x110]=2;dropHostWrite=bases[6]+0x110;}
   if(fault==1)dropHostWrite=bases[6]+4;
   if(fault==2)dropHostWrite=bases[6]+0x11c;
  }else{untilHost(h,SweetMDSS::HostResetHold);
   if(fault==3)dropResetExit=true;if(fault==4)hostResetError=true;
   if(fault==5)fakeTicks+=20000000;if(fault==6)regs[bases[1]+0x2038]=0x80000000;
   if(fault==7)h.m->fNoCLeases[0].status.state=SweetNoCFailed;
   if(fault==8){assert(h.r->willTerminate(h.n,0));emit(gIOTerminatedNotification,h.r);pump();}
   if(fault==9)regs[bases[4]+0x2b4]=1;
  }
  if(fault<4){h.finish();assert(h.m->fHostPhase==SweetMDSS::HostReady);}
  else fail(h,fault==9 ? 1 : 0);
 }
 for(unsigned phase: {unsigned(SweetMDSS::HostResetHold),unsigned(SweetMDSS::HostConfigure)}){
  Harness h;h.begin();untilHost(h,phase);h.m->stop(h.d);
  auto before=trace.size();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.scan();
  assert(before==trace.size()&&h.m->fHostPhase==SweetMDSS::HostStopped&&!sourceWritten()&&!h.pool->pinned);
  assert(h.m->fDsiPoisoned);
 }
 puts("PASS: real host reset/config + managed core/NoC/clock chain, destructive reset fixture, >=1us pulse, mixed W1C/enables, lane map/TE profile, busy admission, independent fresh pixel completion, configuration diagnostics and real resource/error/timeout/provider/stop failures, no PHY/vendor writes, zero objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-host-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+helpers+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
