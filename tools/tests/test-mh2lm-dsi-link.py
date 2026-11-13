#!/usr/bin/env python3
"""Actual PHY/PLL/root reconstruction on the real managed scanout path.
Usage: test-mh2lm-dsi-link.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
if len(sys.argv) != 2:
    sys.exit(__doc__)
here=Path(__file__).resolve().parent
fixture=here/'test-mh2lm-dsi-host.py'
ns={'__file__':str(fixture),'__name__':'link_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
prelude=ns['prelude'].replace('static int hostMode=1;', '''static int hostMode=1,linkMode=2;
static bool physicalHalt=true,physicalRun=true,physicalUnlock=true,physicalRelock=true,physicalRefgen=true,physicalRCG=true,physicalRootHalt=true,physicalRootRun=true;
static uintptr_t dropLinkWrite=0;
static bool dropPHYResetExit=false;
static unsigned phyResets=0;''')
prelude=prelude.replace(' if(!strcmp(k,"sweetdsihost"))', ' if(!strcmp(k,"sweetdsilink")){*(int*)v=linkMode;return true;}\n if(!strcmp(k,"sweetdsihost"))')
prelude=prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){', '''static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(linkMode){
  if(b+o==dropLinkWrite){trace.push_back({true,b+o,v});return;}
  if(b==bases[1]&&(o==0x2004||o==0x2028||o==0x202c)){
   trace.push_back({true,b+o,v});regs[b+o]=(v&~0x80000000u)|(regs[b+o]&0x80000000u);return;
  }
  if(b==bases[12]&&(o==0x140||(o>=0x6b0&&o<0x6d0)||(o>=0x700&&o<0x710)||(o>=0x718&&o<=0x720)||o<16))assert(!"write to PHY/PLL read-only status/identity");
  if(b==bases[6]&&o==0x12c){
   trace.push_back({true,b+o,v});
   if(!v&&dropPHYResetExit)return;
   regs[b+o]=v;
   if(v){
    ++phyResets;
    // PHY reset destroys configuration; none of the old table survives.
    for(unsigned off=16;off<0x760;off+=4)if(off!=0x6b0&&off!=0x14c)regs[bases[12]+off]=0;
   }
   return;
  }
 }
''')
helpers=ns['helpers']
helpers=helpers.replace('struct Harness {', r'''
static void linkFixture(){
 linkMode=2;physicalHalt=physicalRun=physicalUnlock=physicalRelock=physicalRefgen=physicalRCG=physicalRootHalt=physicalRootRun=true;
 dropLinkWrite=0;dropPHYResetExit=false;phyResets=0;
 auto p=bases[12];regs[p]=0;regs[p+4]=1;regs[p+8]=4;regs[p+12]=0; // raw fixture bytes, not proven device values
 regs[p+0x24]=0x7f;regs[p+0xa0]=0x1f;regs[p+0x3c]=1;regs[p+0x6b0]=1;
 regs[p+0x140]=1;regs[p+0x14c]=0x1f;regs[p+0x34]=0x21;regs[p+0x38]=0x84;regs[p+0x14]=0x20;
 regs[bases[1]+0x8000]=0x154; // unrelated PHY1 wrapper fields must survive
 regs[p+0x10]=0x61; // old dividers intentionally DIFFER from target 62
 for(auto r:{0x20b0,0x212c}){regs[bases[1]+r]=0x80000000;regs[bases[1]+r+4]=0x201;}
}
static void linkPhysical(SweetMDSS*m){
 if(physicalUnlock&&!regs[bases[12]+0x3c])regs[bases[12]+0x6b0]&=~1u;
 if(physicalRelock&&regs[bases[12]+0x3c]&&m->fLinkPhase>=SweetMDSS::LinkLock)regs[bases[12]+0x6b0]|=1u;
 if(physicalRefgen&&m->fLinkPhase>=SweetMDSS::LinkRefgen)regs[bases[12]+0x140]|=1;
 for(auto b:{0x2004,0x202c,0x2028}){
  if(physicalHalt&&!(regs[bases[1]+b]&1))regs[bases[1]+b]|=0x80000000u;
  if(physicalRun&&(regs[bases[1]+b]&1))regs[bases[1]+b]&=~0x80000000u;
 }
 for(auto r:{0x2098,0x2110}){
  if(physicalRCG)regs[bases[1]+r]&=~0xf1u;
  bool enabled=r==0x2098 ? regs[bases[1]+0x2004]&1 : (regs[bases[1]+0x2028]|regs[bases[1]+0x202c])&1;
  if(!enabled&&physicalRootHalt)regs[bases[1]+r]|=0x80000000u;
  if(enabled&&physicalRootRun)regs[bases[1]+r]&=~0x80000000u;
 }
}
struct Harness {''')
helpers=helpers.replace('coreFixture();hostFixture();n=nub();','coreFixture();hostFixture();linkFixture();n=nub();')
helpers=helpers.replace('physical(m);{', 'physical(m);linkPhysical(m);{')
actual=ns['actual']
tests=r'''
static void untilLink(Harness&h,unsigned phase){
 for(unsigned i=0;i<6000&&h.m->fLinkPhase!=phase&&h.m->fManagedState!=SweetMDSS::ManagedFault;++i)h.step();
 if(h.m->fLinkPhase!=phase)for(auto&msg:messages)fputs(msg.c_str(),stderr);
 assert(h.m->fLinkPhase==phase&&!sourceWritten()&&!h.pool->pinned);
}
static void failed(Harness&h){
 for(unsigned i=0;i<6000&&h.m->fScanoutState!=SweetScanoutFailed;++i)h.step();
 assert(h.m->fScanoutState==SweetScanoutFailed&&h.m->fManagedState==SweetMDSS::ManagedFault);
 if(sourceWritten()||h.pool->pinned){fprintf(stderr,"unexpected submit phase=%u drop=%#lx flags=%u%u%u%u%u%u\n",h.m->fLinkPhase,dropLinkWrite,physicalHalt,physicalRun,physicalUnlock,physicalRelock,physicalRefgen,physicalRCG);for(auto&msg:messages)fputs(msg.c_str(),stderr);}
 assert(!sourceWritten()&&!h.pool->pinned&&!h.m->managedAccessReady());
 assert(h.m->fManagedRails==15&&h.m->fManagedARC&&h.m->fManagedBandwidth&&h.m->fManagedClock);
 auto before=trace.size();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.scan();assert(trace.size()==before);
}
int main(){
 // Exact independent fixed-point oracle, boundaries and safe output-on-failure.
 SweetDSILinkPlan p{};assert(sweet_dsi_link_plan(1056619200,&p));
 assert(p.vcoHz==2113238400&&p.actualVcoHz==2113238378&&p.decimal==0x37&&p.fraction==0x2106);
 assert(p.sscPeriod==302&&p.sscStep==714&&p.actualVcoHz/16==132077398&&p.actualVcoHz/12==176103198);
 for(uint64_t rate:{0ULL,499999999ULL,1500000000ULL,~0ULL}){auto before=p;assert(!sweet_dsi_link_plan(rate,&p)&&!memcmp(&before,&p,sizeof(p)));}
 assert(!sweet_dsi_link_plan(1056619200,nullptr));
 // Invalid combinations never touch hardware or vote; clean partial start.
 for(unsigned f=0;f<4;++f){warmFixture();coreFixture();hostFixture();linkFixture();auto*d=displayNub();
  if(f==0)linkMode=1;if(f==1)hostMode=0;if(f==2)coreMode=0;if(f==3)managedMode=0;
  auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());mdssClockWriter=mdssNoCWriter=0;
 }
 // 0190 default retains the running PHY/PLL; no link write without explicit profile.
 {Harness h;h.m->fLinkProfile=0;h.begin();h.finish();assert(!phyResets&&hostResets==1);
  for(auto op:trace)assert(!op.write||op.addr<bases[12]||op.addr>=bases[12]+0x760);}
 {
  Harness h;h.begin();untilLink(h,SweetMDSS::LinkLaneIdle);
  assert(h.m->linkBusy()&&!h.m->localClocksReady()&&!h.m->dsiBusy()&&!h.m->hostBusy());
  uint64_t token=0;uint32_t word=0x80050029;
  assert(h.m->dcsWrite(&word,1)!=0&&h.m->setPanelPower(false)!=0&&h.m->brightnessReady()!=0);
  assert(h.m->retainLocalClocks(SweetClockDSI0,&token)==kIOReturnBusy);
  assert(h.m->releaseLocalClocks(h.m->fManagedClock)==kIOReturnBusy);
  assert(h.m->releaseBandwidth(h.m->fManagedBandwidth)==kIOReturnBusy);
  SweetScanoutSnapshot snap{};auto before=trace.size();assert(h.m->inspectScanout(&snap)==kIOReturnNotReady&&before==trace.size());
  SweetMDSS::probeAction(h.m,h.m->fProbe);assert(before==trace.size());
  // Software off writes alone cannot authorize any PLL stop/configuration.
  physicalHalt=false;for(unsigned n=0;n<8;++n)h.step();
  assert(h.m->fLinkPhase==SweetMDSS::LinkGatesOff&&regs[bases[12]+0x3c]==1&&!phyResets);
  physicalHalt=true;untilLink(h,SweetMDSS::LinkResetHold);
  assert(phyResets==1&&regs[bases[6]+0x12c]==0x1000001&&regs[bases[12]+0x10]==0);
  // Early callback must not shorten the 1ms PHY reset pulse.
  auto now=fakeTicks;fakeTicks=h.m->fLinkWait-1;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
  assert(regs[bases[6]+0x12c]==0x1000001);fakeTicks=now;
  untilLink(h,SweetMDSS::LinkLock);assert(!hostResets&&!sourceWritten()&&!h.pool->pinned);
  // Freeze the independent lock after start. A PLL start write is not lock.
  physicalRelock=false;regs[bases[12]+0x6b0]=0;for(unsigned n=0;n<8;++n)h.step();
  assert(h.m->fLinkPhase==SweetMDSS::LinkLock&&!(regs[bases[12]+0x14]&0x20));physicalRelock=true;
  untilLink(h,SweetMDSS::LinkReady);assert(h.m->localClocksReady()&&h.m->linkConfiguration()&&!hostResets);
  assert((regs[bases[1]+0x2128]&3)==1&&regs[bases[1]+0x8000]==0x3f4);
  assert(regs[bases[12]+0x10]==0x62&&regs[bases[12]+0x14]==0x20&&regs[bases[12]+0x1c]==1);
  assert(regs[bases[12]+0x6a4]==0x40&&regs[bases[12]+0x748]==0x28&&regs[bases[12]+0x734]==0x22&&regs[bases[12]+0x594]==0x22);
  assert(regs[bases[12]+0x650]==2&&regs[bases[12]+0x620]==0xca&&regs[bases[12]+0x624]==2);
  h.finish();assert(hostResets==1&&phyResets==1&&h.m->fHostPhase==SweetMDSS::HostReady);
  assert(h.m->fScanoutStatus.done==0x100&&h.m->fFrameCount>h.m->fScanoutSubmitFrame);
  for(auto op:trace)if(op.write){
   assert(op.addr!=bases[6]+0x90&&op.addr!=bases[6]+0x17c); // no vendor/DCS command
   assert(!(op.addr>=0x0ae96400&&op.addr<0x0ae97000)); // no PHY1/slave
  }
  auto resets=phyResets;assert(h.m->setPanelPower(false)==0&&h.m->setPanelPower(true)==0&&resets==phyResets);
 }
 // Unknown identity, CPHY and live aliases reject; inherited configuration differences log.
 for(unsigned f=0;f<9;++f){Harness h;h.begin();h.until(SweetMDSS::ManagedLink);
  if(f==0)regs[bases[12]+4]=0xffffffff;if(f==1)regs[bases[12]+0x34]=0x10;
  if(f==2)regs[bases[12]+0x18]=0x40;if(f==3)regs[bases[12]+0xa4]=1;
  if(f==4)regs[bases[12]+0x210]=1;if(f==5)regs[bases[12]+0x3c]=0;
  if(f==6){regs[bases[1]+0x2130]=0x101;regs[bases[1]+0x212c]=0;}
  if(f==7){regs[bases[1]+0x20b4]=0x101;regs[bases[1]+0x2008]=1;}
  if(f==8)regs[bases[12]+0x14]=4;
  if(f==1||f==3||f==4||f==8){h.finish();assert(h.m->fLinkPhase==SweetMDSS::LinkReady);continue;}
  h.step();assert(!h.m->fLinkTouched&&!phyResets);
  for(unsigned n=0;n<trace.size();++n)if(trace[n].write)assert(trace[n].addr<bases[12]||trace[n].addr>=bases[12]+0x760);failed(h);
 }
 // ROOT_OFF is diagnostic; a pending RCG UPDATE still blocks reconfiguration.
 for(unsigned f=0;f<2;++f){Harness h;h.begin();untilLink(h,SweetMDSS::LinkLaneIdle);
  if(!f){physicalRootHalt=false;h.finish();assert(h.m->fLinkPhase==SweetMDSS::LinkReady);continue;}
  else {physicalRCG=false;regs[bases[1]+0x2110]|=1u;}
  for(unsigned n=0;n<8;++n)h.step();
  assert(h.m->fLinkPhase==SweetMDSS::LinkGatesOff&&regs[bases[12]+0x3c]==1&&!phyResets);
  failed(h);assert(!phyResets&&regs[bases[12]+0x3c]==1);
 }
 // A successful RCG write and running branches do not substitute for ROOT_ON.
 {Harness h;h.begin();untilLink(h,SweetMDSS::LinkRootWait);physicalRootRun=false;
  failed(h);assert(h.m->fLinkPhase==SweetMDSS::LinkFault&&!hostResets);}
 // Real halt/refgen/relock/update/run deadlines remain; unlock is diagnostic.
 for(unsigned f=0;f<6;++f){Harness h;h.begin();
  if(f==0){untilLink(h,SweetMDSS::LinkLaneIdle);physicalHalt=false;}
  if(f==1){untilLink(h,SweetMDSS::LinkGatesOff);physicalUnlock=false;h.finish();assert(h.m->fLinkPhase==SweetMDSS::LinkReady);continue;}
  if(f==2){untilLink(h,SweetMDSS::LinkResetHold);physicalRefgen=false;}
  if(f==3){untilLink(h,SweetMDSS::LinkPLLConfig);physicalRelock=false;}
  if(f==4){untilLink(h,SweetMDSS::LinkRoots);physicalRCG=false;}
  if(f==5){untilLink(h,SweetMDSS::LinkRootWait);physicalRun=false;}
  failed(h);assert(h.m->fLinkPhase==SweetMDSS::LinkFault&&h.m->fDsiPoisoned&&!hostResets);
 }
 // Configuration non-echo can reach LinkReady, then still needs fresh pixels.
 const uintptr_t drop[]={0x20,0xb8,0x418,0x6a4,0x5e4,0x620,0x10,0x528,0x128};
 for(auto off:drop){Harness h;h.begin();untilLink(h,SweetMDSS::LinkResetHold);dropLinkWrite=bases[12]+off;
  untilLink(h,SweetMDSS::LinkReady);assert(h.m->fScanoutState!=SweetScanoutActive);h.finish();assert(h.m->fScanoutState==SweetScanoutActive);}
 // Losing the reconstructed PLL during host reset closes that next stage too.
 {Harness h;h.begin();untilLink(h,SweetMDSS::LinkReady);
  for(unsigned n=0;n<16&&h.m->fHostPhase!=SweetMDSS::HostResetHold;++n)h.step();
  assert(h.m->fHostPhase==SweetMDSS::HostResetHold);physicalRelock=false;regs[bases[12]+0x6b0]=0;
  failed(h);assert(h.m->fHostPhase==SweetMDSS::HostFault);}
 // Stop, timeout/provider loss and late work at every destructive phase.
 for(unsigned phase:{unsigned(SweetMDSS::LinkGatesOff),unsigned(SweetMDSS::LinkResetHold),unsigned(SweetMDSS::LinkPLLConfig),unsigned(SweetMDSS::LinkLock),unsigned(SweetMDSS::LinkGatesOn)}){
  Harness h;h.begin();untilLink(h,phase);h.m->stop(h.d);auto before=trace.size();
  SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.scan();assert(trace.size()==before&&h.m->fLinkPhase==SweetMDSS::LinkStopped&&!sourceWritten()&&!h.pool->pinned);
 }
 for(unsigned f=0;f<7;++f){Harness h;h.begin();untilLink(h,f==5 ? SweetMDSS::LinkResetHold : SweetMDSS::LinkPLLConfig);
  if(f==0)fakeTicks+=250000000;if(f==1){assert(h.r->willTerminate(h.n,0));emit(gIOTerminatedNotification,h.r);pump();}
  if(f==2)regs[bases[1]+0x2038]=0x80000000;
  if(f==3){regs[bases[1]+0x2130]=0x101;regs[bases[1]+0x212c]=0;}
  if(f==4)regs[bases[1]+0x2118]^=1; // noncontrolled BYTE MND snapshot drift
  if(f==5)dropPHYResetExit=true;if(f==6)regs[bases[1]+0x8000]^=1u<<6;
  if(f>=4){h.finish();assert(h.m->fLinkPhase==SweetMDSS::LinkReady);}
  else failed(h);
 }
 puts("PASS: real managed PHY/PLL/DSI host/scanout; destructive reset, independent branch halt/REFGEN/lock/UPDATE/run with configuration diagnostics, fractional+DOWN-SSC oracle, guarded topology, serialized lease suspension, controlled snapshot updates, new pixel fence, partial-write/drift/alias/resource/timeout/stop/provider loss, no PHY1/vendor and zero objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-link-') as temp:
    cpp=Path(temp)/'test.cpp'
    cpp.write_text(prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+helpers+tests)
    binary=Path(temp)/'test'
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(binary)],check=True)
    env=dict(os.environ);env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_leaks=0';env['UBSAN_OPTIONS']=env.get('UBSAN_OPTIONS','')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)],check=True,env=env,timeout=60)
