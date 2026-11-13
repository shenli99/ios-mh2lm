#!/usr/bin/env python3
"""Actual mode5: DATA lease parking composed with physical host/PHY shutdown.
Usage: test-mh2lm-panel-combined-suspend.py EXTRACTED_XNU
Only OS scheduling, MMIO responses and independent wire peers are replaced.
"""
from pathlib import Path
import os,subprocess,sys,tempfile
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-link-power-cycle.py'
ns={'__file__':str(fixture),'__name__':'combined_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers']
helpers=helpers.replace('auto beforeLink=m->fLinkPhase;', 'auto beforeManaged=m->fManagedState;auto beforeLink=m->fLinkPhase;').replace('if(beforeLink!=m->fLinkPhase', 'if(beforeManaged!=m->fManagedState&&(m->fManagedState==SweetMDSS::ManagedParkLink||m->fManagedState==SweetMDSS::ManagedRestoreLink))return;if(beforeLink!=m->fLinkPhase')
touch=ns['touch'].replace('lifecycleMode=4;', 'lifecycleMode=5;')
tests=ns['tests'].split('static void inert(')[0]+r'''
static void inert(Harness&h){
 auto n=trace.size();h.life();h.scan();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
 SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
 assert(trace.size()==n);
}
static void failed(Harness&h){
 for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault&&h.m->fLifecycle.phase!=SweetPanelStopped;++i)h.step();
 assert(h.m->fLifecycle.uncertain&&(h.m->fLifecycle.phase==SweetPanelFault||h.m->fLifecycle.phase==SweetPanelStopped));
 assert(h.m->fManagedRails==15&&h.m->fManagedARC&&h.pool->pinned);inert(h);
}
static void stage(Harness&h,unsigned managed,unsigned link=0){
 for(unsigned i=0;i<12000&&(h.m->fManagedState!=managed||(link&&h.m->fLinkPhase!=link))&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step();
 if(h.m->fManagedState!=managed||(link&&h.m->fLinkPhase!=link)){
  fprintf(stderr,"want managed=%u link=%u; got managed=%u link=%u life=%u\n",managed,link,h.m->fManagedState,h.m->fLinkPhase,h.m->fLifecycle.phase);
  for(auto&msg:messages)fputs(msg.c_str(),stderr);
 }
 assert(h.m->fManagedState==managed&&(!link||h.m->fLinkPhase==link));
}
static void parked(Harness&h){
 panelPackets.clear();h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);
 auto&m=*h.m;auto&v=m.fLifecycle;checkPackets(selectedProfile,false);
 assert(v.resourcesParked&&v.linkPoweredDown&&!v.resourcesHeld&&v.retainedPower&&v.configHeld&&v.sleeping&&v.touchQuiesced&&v.displayDrained);
 assert(!v.dataToken&&!v.clockToken&&v.configToken==m.fManagedConfigBandwidth&&v.configToken);
 assert(m.fManagedState==SweetMDSS::ManagedParked&&!m.fManagedBandwidth&&!m.fManagedClock);
 assert(m.linkDormantConfiguration()&&m.managedRetainedResources()&&m.managedConfigHeld());
 assert(m.fLinkSuspendedMask==0x70&&!m.localClocksReady()&&!m.lifecycleResourcesHeld());
 SweetClockLease c{};assert(m.localClockStatus(m.fClockBootToken,&c)==0&&c.state==SweetClockHeld&&c.hardwareSuspended);
 for(unsigned i=4;i<=6;++i)assert(m.fClockReferences[i]==1);
 for(unsigned r=1;r<SweetDisplayBCMCount;++r)assert(!m.fNoCApplied[r]);
 uint64_t token=99;auto vote=managedBandwidth();auto n=writes();
 assert(m.retainBandwidth(vote,&token)!=0&&!token&&m.releaseBandwidth(m.fManagedConfigBandwidth)!=0);
 assert(m.retainLocalClocks(SweetClockDSI0,&token)!=0&&!token&&m.releaseLocalClocks(m.fClockBootToken)!=0);
 assert(m.lifecycleDataBandwidth(false)!=0); // private entry also needs the exact active phase
 assert(n==writes());
}
int main(){
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;ready(h);
  assert((h.m->panelLifecycleCapabilities()&(SweetPanelDataParking|SweetPanelLinkPowerCycle))==(SweetPanelDataParking|SweetPanelLinkPowerCycle));
  auto boot=h.m->fClockBootToken,arc=h.m->fManagedARC,rsc=h.m->fNoCClockToken;uint64_t floor=0;
  for(unsigned round=0;round<4;++round){
   auto data=h.m->fManagedBandwidth,clock=h.m->fManagedClock;auto resets=phyResets,host=hostResets;
   pinEdges.clear();parked(h);assert(phyResets==resets&&hostResets==host&&pinEdges.empty());
   if(!floor)floor=h.m->fManagedConfigBandwidth;assert(floor==h.m->fManagedConfigBandwidth);
   SweetClockLease c{};SweetNoCStatus n{};
   assert(h.m->localClockStatus(clock,&c)==0&&c.state==SweetClockReleased);
   assert(h.m->bandwidthStatus(data,&n)==0&&n.state==SweetNoCReleased);
   panelPackets.clear();h.power(SweetPanelDisplayOn);
   assert(h.m->fLifecycle.phase==SweetPanelResourceRestore);
   stage(h,SweetMDSS::ManagedRestoreLink);
   assert(h.m->fManagedBandwidth>data&&!h.m->fManagedClock&&h.m->fLinkSuspendedMask&&phyResets==resets&&hostResets==host);
   stage(h,SweetMDSS::ManagedRestoreClock);
   assert(h.m->fLinkPhase==SweetMDSS::LinkReady&&!h.m->fLinkSuspendedMask&&phyResets==resets+1&&hostResets==host);
   h.untilLife(SweetPanelPinReset);
   assert(h.m->fLifecycle.linkRebuilt&&h.m->fLifecycle.resourcesRestored&&h.m->lifecycleResourcesHeld());
   assert(h.m->fManagedClock>clock&&hostResets==host+1&&panelPackets.empty());
   h.wakePixels();checkPackets(profile,true,0x345);
   assert(h.m->fClockBootToken==boot&&h.m->fManagedARC==arc&&h.m->fManagedConfigBandwidth==floor&&h.m->fNoCClockToken==rsc);
   assert(h.t->fStatus.ready&&h.m->fLifecycle.brightnessRestored);
  }
 }

 selectedProfile=4;
 // Explicit handoff proofs; OFF data release cannot precede physical shutdown.
 {Harness h;ready(h);panelPackets.clear();h.power(SweetPanelDisplayOff);
  stage(h,SweetMDSS::ManagedParkLink);checkPackets(selectedProfile,false);
  assert(!h.m->fManagedClock&&h.m->fManagedBandwidth&&h.m->managedConfigHeld());
  assert(h.m->fLinkPhase==SweetMDSS::LinkLaneIdle&&!h.m->fLinkSuspendedMask);
  stage(h,SweetMDSS::ManagedParkNoC);
  SweetNoCStatus n{};assert(h.m->bandwidthStatus(h.m->fManagedBandwidth,&n)==0&&n.state==SweetNoCHeld);
  assert(h.m->linkDormantConfiguration()&&!h.m->noCBusy());
  h.untilLife(SweetPanelComplete);
 }
 // Every resource and link handoff stops on cancellation/stop/peer loss.
 const unsigned resources[]={SweetMDSS::ManagedParkConfig,SweetMDSS::ManagedParkClock,SweetMDSS::ManagedParkNoC,
  SweetMDSS::ManagedRestoreNoC,SweetMDSS::ManagedRestoreClock};
 for(unsigned state:resources)for(unsigned fault=0;fault<5;++fault){Harness h;ready(h);
  bool up=state==SweetMDSS::ManagedRestoreNoC||state==SweetMDSS::ManagedRestoreClock;
  if(up){parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
  stage(h,state);
  if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==1)h.m->stop(h.d);
  if(fault==2){SweetRPMhClient::terminated(h.m->fRPMhClient,nullptr,h.r,nullptr);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);}
  if(fault==3){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);}
  if(fault==4){tcFixtureOnline(0);SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);}
  failed(h);if(up)assert(panelPackets.empty());pump();
 }
 const unsigned down[]={SweetMDSS::LinkLaneIdle,SweetMDSS::LinkGatesOff,SweetMDSS::LinkPLLStop,SweetMDSS::LinkBiasOff};
 const unsigned up[]={SweetMDSS::LinkResetHold,SweetMDSS::LinkResetSettle,SweetMDSS::LinkRefgen,SweetMDSS::LinkPHYConfig,
  SweetMDSS::LinkPLLConfig,SweetMDSS::LinkBiasOn,SweetMDSS::LinkLock,SweetMDSS::LinkRoots,SweetMDSS::LinkRootWait,SweetMDSS::LinkGatesOn,SweetMDSS::LinkResync};
 for(bool restore:{false,true})for(unsigned link:restore?std::vector<unsigned>(std::begin(up),std::end(up)):std::vector<unsigned>(std::begin(down),std::end(down)))
  for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);
   if(restore){parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
   stage(h,restore?SweetMDSS::ManagedRestoreLink:SweetMDSS::ManagedParkLink,link);
   if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
   if(fault==1)h.m->stop(h.d);
   if(fault==2){SweetRPMhClient::terminated(h.m->fRPMhClient,nullptr,h.r,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
   if(fault==3){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
   failed(h);if(restore)assert(panelPackets.empty());pump();
  }
 // Missing DATA ACK leaves the original/new token and physical state intact.
 for(bool restore:{false,true}){Harness h;ready(h);
  if(restore){parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);}
  else{h.power(SweetPanelDisplayOff);stage(h,SweetMDSS::ManagedParkNoC);}
  auto resets=phyResets,host=hostResets;
  for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step(true,false);
  failed(h);assert(h.m->fManagedBandwidth&&h.m->fManagedConfigBandwidth&&h.m->fLinkSuspendedMask);
  assert(resets==phyResets&&host==hostResets);if(restore)assert(panelPackets.empty());
 }
 // No hardware failure is repaired by declaring a bridge complete.
 for(bool restore:{false,true})for(unsigned fault=0;fault<6;++fault){Harness h;ready(h);
  if(restore){parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);
   if(fault==0)physicalRefgen=false;if(fault==1)physicalRelock=false;if(fault==2)physicalRCG=false;
   if(fault==3)physicalRootRun=false;if(fault==4)dropResetExit=true;if(fault==5)hostResetError=true;
  }else{h.power(SweetPanelDisplayOff);stage(h,SweetMDSS::ManagedParkLink);
   if(fault==0)physicalHalt=false;if(fault==1)physicalRootHalt=false;if(fault==2)physicalUnlock=false;
   if(fault==3)dropLinkWrite=bases[12]+0x214;if(fault==4)dropLinkWrite=bases[12]+0x24;if(fault==5)dropLinkWrite=bases[12]+0x528;
  }
  if((restore&&fault==4)||(!restore&&fault>=1)){
   if(restore){h.wakePixels();assert(h.m->fLifecycle.resetComplete);}
   else{h.untilLife(SweetPanelComplete);assert(h.m->fLifecycle.resourcesParked);}continue;
  }
  failed(h);if(restore)assert(panelPackets.empty()&&!h.m->fLifecycle.linkRebuilt&&!h.m->fLifecycle.resetComplete);
  else assert(h.m->fManagedBandwidth&&!h.m->fLifecycle.resourcesParked);
 }
 // Suspended NoC children recheck physical/config/touch fences BEFORE writes.
 for(bool restore:{false,true})for(unsigned drift=0;drift<7;++drift){Harness h;ready(h);
  if(restore){parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);}
  else{h.power(SweetPanelDisplayOff);stage(h,SweetMDSS::ManagedParkNoC);}
  h.life();assert(h.m->noCBusy());
  if(drift==0)regs[bases[4]+0x288]|=1u<<20;
  if(drift==1)regs[TG+0x30c]|=1u<<19;
  if(drift==2)regs[bases[2]+0x14]=0x9d200000;
  if(drift==3)regs[bases[1]+0x2114]^=1u<<14;
  if(drift==4)tcFixtureOnline(0);
  if(drift==5)fakeTicks+=4000000000ULL;
  if(drift==6){regs[bases[1]+0x212c]=0;regs[bases[1]+0x2130]=0x101;} // another hardware link user
  auto before=trace.size();SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
  if(drift==2||drift==3){assert(h.m->fLifecycle.phase!=SweetPanelFault);
   if(restore)h.wakePixels();else h.untilLife(SweetPanelComplete);continue;}
  if(h.m->fLifecycle.phase!=SweetPanelFault)fprintf(stderr,"drift restore=%u drift=%u failed to fault; life=%u noc=%u\n",restore,drift,h.m->fLifecycle.phase,h.m->fNoCPhase);
  assert(h.m->fLifecycle.phase==SweetPanelFault);
  for(unsigned i=before;i<trace.size();++i)assert(!trace[i].write||trace[i].addr<DT||trace[i].addr>=DT+0x3000);
  inert(h);
 }
 // Extra link users refuse OFF before touch/DDI side effects.
 for(unsigned other=0;other<2;++other){Harness h;ready(h);uint64_t token=0;
  if(!other){assert(h.m->retainLocalClocks(SweetClockDSI0,&token)==0);for(unsigned i=0;i<100&&h.m->clockBusy();++i)h.step();}
  else{regs[bases[1]+0x212c]=0;regs[bases[1]+0x2130]=0x101;}
  auto before=writes();token=99;
  assert(h.m->beginPanelLifecycle(SweetPanelDisplayOff,&token)==kIOReturnNotReady&&!token&&writes()==before);
 }
 // DATA consumers retain their own lease/aggregate; our release need not be zero.
 {Harness h;ready(h);uint64_t other=0;auto vote=managedBandwidth();
  vote.port[0].peak=vote.port[1].peak=vote.llcc.peak=vote.ebi.peak=128000000;vote.registerPeak=500000000;
  assert(h.m->retainBandwidth(vote,&other)==0);for(unsigned i=0;i<2000&&h.m->noCBusy();++i)h.step();
  h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);
  SweetNoCStatus n{};assert(h.m->bandwidthStatus(other,&n)==0&&n.state==SweetNoCHeld);
  assert(h.m->fNoCAggregate.registerPeak==500000000&&h.m->fNoCAggregate.port[0].peak==128000000);
  for(unsigned r=1;r<SweetDisplayBCMCount;++r)assert(h.m->fNoCApplied[r]);
  h.power(SweetPanelDisplayOn);h.wakePixels();assert(h.m->bandwidthStatus(other,&n)==0&&n.state==SweetNoCHeld);
 }
 // Exhaustion at new DATA/clock tokens stops before panel reset/ON.
 for(bool clock:{false,true}){Harness h;ready(h);parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);
  if(clock)h.m->fClockNextToken=~0ULL;else h.m->fNoCNextToken=~0ULL;
  failed(h);assert(panelPackets.empty()&&!h.m->fLifecycle.resetComplete);
 }
 // Re-admission logs dormant snapshots; parked cancellation
 // must not acquire DATA or issue reset packets in a later timer.
 for(unsigned fault=0;fault<5;++fault){Harness h;ready(h);parked(h);panelPackets.clear();
  if(fault<2){
   if(fault==0)regs[bases[1]+0x2114]^=1u<<14;else regs[bases[1]+0x2128]^=1u<<4;
   auto before=writes();uint64_t token=99;
   assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&token)==0&&token);h.wakePixels();continue;
  }else{
   if(fault==2)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
   if(fault==3)h.m->stop(h.d);
   if(fault==4){SweetTouchPanelClient::terminated(h.m->fTouchPanelClient,nullptr,h.t,nullptr);pump();}
   failed(h);
  }
  assert(!h.m->fManagedBandwidth&&!h.m->fManagedClock&&h.m->fLinkSuspendedMask==0x70&&panelPackets.empty());
 }
 puts("PASS: actual mode5 four profiles x four cycles; release/shutdown and DATA/PHY/clock/host restore order; stable config/boot/RSC/ARC, new DATA/clock tokens; actual TCM/pixel; every resource/link phase cancel/stop/withdrawal; missing ACK/physical responses, child-before-parent drift/deadline, other consumers, exhausted tokens, parked drift/cancel; no fallback and zero objects/maps/allocations/notifications after reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-combined-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(ns['actual_worker'](root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
