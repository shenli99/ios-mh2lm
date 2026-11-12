#!/usr/bin/env python3
"""Actual lifecycle mode4: host disable, PHY/PLL stop and full rebuild.
OS/MMIO/status and independent pin/TCM peers only are substituted.
Usage: test-mh2lm-panel-link-power-cycle.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-profile-lifecycle.py'
ns={'__file__':str(fixture),'__name__':'link_cycle_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers']
helpers=helpers.replace('SweetMDSS::lifecycleTimerAction(m,m->fLifecycleTimer);', 'auto beforeLink=m->fLinkPhase;SweetMDSS::lifecycleTimerAction(m,m->fLifecycleTimer);if(beforeLink!=m->fLinkPhase&&m->fLinkPhase==SweetMDSS::LinkLaneIdle)return;')
touch=ns['touch'].replace('lifecycleMode=2;', 'lifecycleMode=4;')
tests=ns['tests'].split('int main(){')[0]+r'''
static void inert(Harness&h){
 auto before=trace.size();h.life();h.scan();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
 SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
 assert(before==trace.size());
}
static void failed(Harness&h){
 for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault&&h.m->fLifecycle.phase!=SweetPanelStopped;++i)h.step();
 assert(h.m->fLifecycle.uncertain&&(h.m->fLifecycle.phase==SweetPanelFault||h.m->fLifecycle.phase==SweetPanelStopped));
 assert(h.m->fManagedRails==15&&h.m->fManagedARC&&h.m->fManagedBandwidth&&h.m->fManagedClock&&h.pool->pinned);
 inert(h);
}
static void until(Harness&h,unsigned link){
 for(unsigned i=0;i<12000&&h.m->fLinkPhase!=link&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step();
 if(h.m->fLinkPhase!=link){fprintf(stderr,"wanted=%u actual=%u life=%u\n",link,h.m->fLinkPhase,h.m->fLifecycle.phase);for(auto&msg:messages)fputs(msg.c_str(),stderr);}
 assert(h.m->fLinkPhase==link);
}
static void checkOff(Harness&h){
 auto&m=*h.m;auto&v=m.fLifecycle;
 assert(v.sleeping&&v.linkPoweredDown&&!v.linkRebuilt&&!v.resourcesHeld&&v.retainedPower&&v.touchQuiesced&&v.displayDrained);
 assert(v.dataToken==m.fManagedBandwidth&&v.clockToken==m.fManagedClock&&m.linkDormantConfiguration());
 assert(m.fHostPhase==SweetMDSS::HostDormant&&m.fLinkPhase==SweetMDSS::LinkDormant);
 assert(m.fLinkSuspendedMask==0x70&&!m.localClocksReady()&&!m.lifecycleResourcesHeld()&&m.lifecycleLinkResourcesHeld());
 SweetClockLease c{};assert(m.localClockStatus(m.fManagedClock,&c)==0&&c.state==SweetClockHeld&&c.hardwareSuspended);
 assert(m.localClockStatus(m.fClockBootToken,&c)==0&&c.state==SweetClockHeld&&c.hardwareSuspended);
 assert(!(regs[bases[6]+4]&7)&&!regs[bases[12]+0x3c]&&!(regs[bases[12]+0x6b0]&1));
 assert(!regs[bases[12]+0x528]&&!regs[bases[12]+0x214]&&!regs[bases[12]+0xa0]&&!regs[bases[12]+0x24]);
 for(auto b:{0x2004,0x202c,0x2028})assert((regs[bases[1]+b]&0x80000001u)==0x80000000u);
 for(auto r:{0x2098,0x2110})assert((regs[bases[1]+r]&0x800000f1u)==0x80000000u);
 auto before=writes();uint64_t t=99;auto bandwidth=managedBandwidth();
 assert(m.retainLocalClocks(SweetClockDSI0,&t)!=0&&!t&&m.releaseLocalClocks(m.fManagedClock)!=0);
 assert(m.retainBandwidth(bandwidth,&t)!=0&&!t&&m.releaseBandwidth(m.fManagedBandwidth)!=0);
 uint32_t command=0x80050029;assert(m.dcsWrite(&command,1)!=0);
 assert(writes()==before);
 auto n=trace.size();for(unsigned i=0;i<8;++i){h.life();tcFixtureStep();}assert(trace.size()==n);
}
static void sleepCycle(Harness&h){h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);checkPackets(selectedProfile,false);checkOff(h);}
int main(){
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;ready(h);
  assert(h.m->panelLifecycleCapabilities()&SweetPanelLinkPowerCycle);
  assert(!(h.m->panelLifecycleCapabilities()&SweetPanelDataParking));
  auto boot=h.m->fClockBootToken,arc=h.m->fManagedARC,data=h.m->fManagedBandwidth,clock=h.m->fManagedClock;
  for(unsigned round=0;round<4;++round){
   panelPackets.clear();pinEdges.clear();auto resets=phyResets,host=hostResets;
   auto traceStart=trace.size();sleepCycle(h);assert(phyResets==resets&&hostResets==host&&pinEdges.empty());
   // Wire OFF and delays finish BEFORE the first actual host disable.
   bool commandStopped=false,hostStopped=false,gated=false,pllStopped=false,biasStopped=false;
   uint64_t lastPacket=panelPackets.back().time;
   for(unsigned i=traceStart;i<trace.size();++i){auto op=trace[i];if(!op.write)continue;
    if(op.addr==bases[6]+4&&(op.value&7u)==1)commandStopped=true;
    if(op.addr==bases[6]+4&&!(op.value&7u)){assert(commandStopped);hostStopped=true;}
    if(op.addr==bases[1]+0x2004&&!(op.value&1u)){assert(hostStopped);gated=true;}
    if(op.addr==bases[12]+0x3c&&!op.value){assert(gated);pllStopped=true;}
    if(op.addr==bases[12]+0x528&&!op.value){assert(pllStopped);biasStopped=true;}
    if(op.addr==bases[12]+0x24&&!op.value)assert(biasStopped);
   }
   assert(commandStopped&&hostStopped&&gated&&pllStopped&&biasStopped&&fakeTicks-lastPacket>=150000000ULL);
   panelPackets.clear();h.power(SweetPanelDisplayOn);assert(h.m->fLifecycle.phase==SweetPanelLinkRebuild&&panelPackets.empty());
   h.untilLife(SweetPanelPinReset);
   assert(h.m->fLifecycle.linkRebuilt&&!h.m->fLifecycle.linkPoweredDown&&h.m->lifecycleResourcesHeld());
   assert(!h.m->fLinkSuspendedMask&&phyResets==resets+1&&hostResets==host+1&&panelPackets.empty());
   h.wakePixels();checkPackets(profile,true,0x345);
   assert(h.t->fStatus.ready&&h.m->fLifecycle.resetComplete&&h.m->fLifecycle.brightnessRestored);
   assert(h.m->fClockBootToken==boot&&h.m->fManagedARC==arc&&h.m->fManagedBandwidth==data&&h.m->fManagedClock==clock);
   SweetClockLease c{};assert(h.m->localClockStatus(clock,&c)==0&&!c.hardwareSuspended);
  }
 }
 selectedProfile=4;
 // Extra software link users and a hardware DSI1 alias prevent suspension.
 for(unsigned other=0;other<2;++other){Harness h;ready(h);uint64_t token=0;
  if(!other){assert(h.m->retainLocalClocks(SweetClockDSI0,&token)==0);for(unsigned i=0;i<100&&h.m->clockBusy();++i)h.step();}
  else{regs[bases[1]+0x212c]=0;regs[bases[1]+0x2130]=0x101;}
  auto before=writes();uint64_t rejected=99;
  assert(h.m->beginPanelLifecycle(SweetPanelDisplayOff,&rejected)==kIOReturnNotReady&&!rejected&&writes()==before);
  assert(!h.m->fLinkSuspendedMask&&!h.m->fLifecycle.uncertain);}
 // Cancellation, stop and withdrawals at both shutdown and restoration stages.
 const unsigned down[]={SweetMDSS::LinkLaneIdle,SweetMDSS::LinkGatesOff,SweetMDSS::LinkPLLStop,SweetMDSS::LinkBiasOff};
 const unsigned up[]={SweetMDSS::LinkResetHold,SweetMDSS::LinkResetSettle,SweetMDSS::LinkRefgen,SweetMDSS::LinkPHYConfig,
  SweetMDSS::LinkPLLConfig,SweetMDSS::LinkBiasOn,SweetMDSS::LinkLock,SweetMDSS::LinkRoots,SweetMDSS::LinkRootWait,SweetMDSS::LinkGatesOn,SweetMDSS::LinkResync};
 for(bool restore:{false,true})for(unsigned stage:restore?std::vector<unsigned>(std::begin(up),std::end(up)):std::vector<unsigned>(std::begin(down),std::end(down)))
  for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);
   if(restore){panelPackets.clear();sleepCycle(h);panelPackets.clear();h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
   until(h,stage);
   if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
   if(fault==1)h.m->stop(h.d);
   if(fault==2){SweetRPMhClient::terminated(h.m->fRPMhClient,nullptr,h.r,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
   if(fault==3){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
   failed(h);if(restore)assert(panelPackets.empty());pump();
  }
 // Actual PHY/clock/PLL response failures must not produce a successful OFF.
 for(unsigned fault=0;fault<10;++fault){Harness h;ready(h);auto resets=phyResets;h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelLinkPowerDown);
  if(fault==0)physicalHalt=false;if(fault==1)physicalRootHalt=false;if(fault==2)physicalUnlock=false;
  if(fault==3)regs[bases[12]+0x14c]=0;
  if(fault==4)dropLinkWrite=bases[12]+0x214;if(fault==5)dropLinkWrite=bases[12]+0x24;
  if(fault==6)dropLinkWrite=bases[12]+0x528;if(fault==7)dropLinkWrite=bases[1]+0x2004;
  if(fault==8){regs[TG+0x30c]=1u<<19;}if(fault==9)fakeTicks+=1000000000ULL;
  failed(h);assert(phyResets==resets&&!h.m->fLifecycle.linkPoweredDown);
 }
 // Restore needs new hardware responses and real host reconstruction. No reset
 // or vendor packet may follow a failed fresh REFGEN/LOCK/root/config response.
 for(unsigned fault=0;fault<9;++fault){Harness h;ready(h);panelPackets.clear();sleepCycle(h);panelPackets.clear();h.power(SweetPanelDisplayOn);
  if(fault==0)physicalRefgen=false;if(fault==1)physicalRelock=false;if(fault==2)physicalRCG=false;
  if(fault==3)physicalRun=false;if(fault==4)physicalRootRun=false;if(fault==5)dropPHYResetExit=true;
  if(fault==6)dropLinkWrite=bases[12]+0xb8;
  if(fault==7)dropResetExit=true;if(fault==8)hostResetError=true;
  failed(h);assert(panelPackets.empty()&&!h.m->fLifecycle.linkRebuilt&&!h.m->fLifecycle.resetComplete);
 }

 // A previously successful shutdown doesn't authorize using changed root state,
 // nor continued reset after a parked cancellation/provider withdrawal.
 for(unsigned fault=0;fault<5;++fault){Harness h;ready(h);panelPackets.clear();sleepCycle(h);panelPackets.clear();
  if(fault<2){
   if(fault==0)regs[bases[1]+0x2114]^=1u<<14;else regs[bases[1]+0x2128]^=1u<<4;
   auto before=writes();uint64_t token=99;assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&token)!=0&&!token&&writes()==before);
   assert(!h.m->fLinkResume&&panelPackets.empty());continue;
  }
  if(fault==2)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==3)h.m->stop(h.d);
  if(fault==4){SweetTouchPanelClient::terminated(h.m->fTouchPanelClient,nullptr,h.t,nullptr);pump();}
  failed(h);assert(h.m->fLinkSuspendedMask==0x70&&panelPackets.empty());
 }
 // Root drift DURING resume is rejected before a controlled update can replay
 // unrelated configuration bits from an old snapshot.
 {Harness h;ready(h);panelPackets.clear();sleepCycle(h);panelPackets.clear();h.power(SweetPanelDisplayOn);
  until(h,SweetMDSS::LinkRoots);regs[bases[1]+0x2114]^=1u<<14;failed(h);assert(panelPackets.empty());}
 // Host disable's exact dynamic-force/readback sequence rejects a dropped write.
 for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelCommand);
  for(unsigned i=0;i<60000&&h.m->dsiBusy();++i){fakeTicks+=100000;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
  assert(!h.m->dsiBusy()&&h.m->fDsiStatus.state==SweetDSIComplete);
  if(fault==0)dropHostWrite=bases[6]+4;if(fault==1)dropHostWrite=bases[6]+0x11c;if(fault==2)regs[bases[6]+0x11c]=0;
  failed(h);assert(!h.m->fLinkSuspendedMask);if(fault<2)assert(h.m->fHostPhase==SweetMDSS::HostFault);}
 // Dependent hardware can drift after the parent timer; the DSI child checks first.
 for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);h.power(SweetPanelDisplayOff);until(h,SweetMDSS::LinkGatesOff);
  if(fault==0)regs[bases[4]+0x288]|=1u<<20;if(fault==1)regs[bases[2]+0x14]=0x9d200000;
  if(fault==2)regs[bases[1]+0x2130]=0x101;if(fault==2)regs[bases[1]+0x212c]=0;
  if(fault==3)tcFixtureOnline(0);
  auto start=trace.size();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
  assert(h.m->fLifecycle.phase==SweetPanelFault);
  for(unsigned i=start;i<trace.size();++i)assert(!trace[i].write||trace[i].addr<bases[12]||trace[i].addr>=bases[12]+0x760);
  inert(h);
 }
 puts("PASS: actual mode4 four profiles x four host/PHY shutdown+reset/config/PLL/root/host reconstruction cycles; wire order, retained tokens, suspended clock readiness, real TCM/pixel fences, extra-user rejection, cancel/stop/withdrawal every link phase, missing physical halt/unlock/refgen/lock/root/readback, host failure, child-before-parent drift; no fallback and zero objects/maps/allocations/notifications after reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-link-cycle-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
