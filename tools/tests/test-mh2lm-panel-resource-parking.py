#!/usr/bin/env python3
"""Actual mode3 coordinator + lease engines + pins + C TCM worker.
Only OS/MMIO/input/wire peers are substituted; no lifecycle state model.
Usage: test-mh2lm-panel-resource-parking.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-profile-lifecycle.py'
ns={'__file__':str(fixture),'__name__':'resource_parking_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers']
touch=ns['touch'].replace('lifecycleMode=2;', 'lifecycleMode=3;')
tests=ns['tests'].split('int main(){')[0]+r'''
static void noMoreWrites(Harness&h){
 auto n=writes();h.life();h.scan();SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
 SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
 assert(writes()==n);
}
static void failed(Harness&h){
 for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault&&h.m->fLifecycle.phase!=SweetPanelStopped;++i)h.step();
 assert(h.m->fLifecycle.uncertain&&(h.m->fLifecycle.phase==SweetPanelFault||h.m->fLifecycle.phase==SweetPanelStopped));
 assert(h.m->fManagedRails==15&&h.m->fManagedARC&&h.pool->pinned);noMoreWrites(h);
}
static void phase(Harness&h,unsigned want){
 for(unsigned i=0;i<12000&&h.m->fManagedState!=want&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step();
 if(h.m->fManagedState!=want)for(auto&msg:messages)fputs(msg.c_str(),stderr);
 assert(h.m->fManagedState==want);
}
static void startRelease(Harness&h){
 ready(h);panelPackets.clear();h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelResourcePark);
 assert(h.m->fLifecycle.sleeping&&h.m->fLifecycle.touchQuiesced);
 checkPackets(selectedProfile,false);assert(pinEdges.empty());
}
static void parked(Harness&h){
 auto oldData=h.m->fManagedBandwidth,oldClock=h.m->fManagedClock;
 sleep(h);checkPackets(selectedProfile,false);
 auto v=h.m->fLifecycle;assert(v.resourcesParked&&!v.resourcesHeld&&v.retainedPower&&v.configHeld);
 assert(v.configToken&&v.configToken==h.m->fManagedConfigBandwidth&&!v.dataToken&&!v.clockToken);
 assert(h.m->fManagedState==SweetMDSS::ManagedParked&&h.m->managedConfigHeld());
 assert(!h.m->fManagedBandwidth&&!h.m->fManagedClock&&h.m->fManagedRails==15&&h.m->fManagedARC);
 SweetNoCStatus n{};SweetClockLease c{};
 assert(h.m->bandwidthStatus(oldData,&n)==0&&n.state==SweetNoCReleased&&!n.uncertain);
 assert(h.m->localClockStatus(oldClock,&c)==0&&c.state==SweetClockReleased&&!c.uncertain);
 // Actual RSC commands lower all five DATA BCMs, while CN0 is retained.
 for(unsigned r=1;r<SweetDisplayBCMCount;++r)assert(!h.m->fNoCApplied[r]&&(h.m->fNoCOwned&(1u<<r)));
 assert(h.m->fNoCAggregate.registerPeak==300000000&&h.m->fNoCClockToken);
 assert(h.m->localClockStatus(h.m->fClockBootToken,&c)==0&&c.state==SweetClockHeld);
 assert(h.m->managedRetainedResources()&&!h.m->lifecycleResourcesHeld());
 auto ntrace=trace.size();for(unsigned i=0;i<10;++i){h.life();tcFixtureStep();}assert(trace.size()==ntrace);
}
int main(){
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;ready(h);
  assert((h.m->panelLifecycleCapabilities()&(SweetPanelRetainedSleepReset|SweetPanelDataParking))==(SweetPanelRetainedSleepReset|SweetPanelDataParking));
  auto boot=h.m->fClockBootToken,arc=h.m->fManagedARC;
  uint64_t config=0;
  for(unsigned round=0;round<4;++round){
   pinEdges.clear();auto oldData=h.m->fManagedBandwidth,oldClock=h.m->fManagedClock;
   panelPackets.clear();parked(h);
   if(!config)config=h.m->fManagedConfigBandwidth;assert(config==h.m->fManagedConfigBandwidth);
   panelPackets.clear();h.power(SweetPanelDisplayOn);
   assert(h.m->fLifecycle.phase==SweetPanelResourceRestore&&panelPackets.empty()&&pinEdges.empty());
   h.untilLife(SweetPanelPinReset);
   assert(h.m->fManagedBandwidth>oldData&&h.m->fManagedClock>oldClock&&h.m->lifecycleResourcesHeld());
   assert(h.m->fLifecycle.resourcesRestored&&!h.m->fLifecycle.resourcesParked&&panelPackets.empty());
   h.wakePixels();checkPackets(profile,true,0x345);
   assert(h.m->fManagedARC==arc&&h.m->fClockBootToken==boot&&h.m->fManagedConfigBandwidth==config);
   assert(h.m->fLifecycle.resetComplete&&h.m->fLifecycle.brightnessRestored&&h.t->fStatus.ready);
   assert(h.m->fLifecycle.resourcesHeld&&h.m->fLifecycle.resourcesRestored);
  }
 }
 selectedProfile=4;
 // Existing FULL lease rejection must not permit a config-only mixed path.
 {Harness h;ready(h);auto before=writes();uint64_t token=99;SweetDisplayBandwidth bad{};bad.registerPeak=300000000;
  bad.port[0].average=1;assert(h.m->retainBandwidth(bad,&token)==kIOReturnBadArgument&&!token&&writes()==before);
  bad={};bad.registerPeak=300000000;bad.llcc.peak=1;token=99;
  assert(h.m->retainBandwidth(bad,&token)==kIOReturnBadArgument&&!token&&writes()==before);}
 // Cancel/stop/provider withdrawal at EVERY resource phase. Late children cannot advance.
 const unsigned stages[]={SweetMDSS::ManagedParkConfig,SweetMDSS::ManagedParkClock,SweetMDSS::ManagedParkNoC,
  SweetMDSS::ManagedRestoreNoC,SweetMDSS::ManagedRestoreClock};
 for(unsigned stage:stages)for(unsigned fault=0;fault<5;++fault){Harness h;ready(h);
  if(stage==SweetMDSS::ManagedRestoreNoC||stage==SweetMDSS::ManagedRestoreClock){panelPackets.clear();parked(h);h.power(SweetPanelDisplayOn);}
  else h.power(SweetPanelDisplayOff);
  phase(h,stage);auto boot=h.m->fClockBootToken;
  if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==1)h.m->stop(h.d);
  if(fault==2){SweetRPMhClient::terminated(h.m->fRPMhClient,nullptr,h.r,nullptr);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);}
  if(fault==3){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);}
  if(fault==4){tcFixtureOnline(0);SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);}
  failed(h);assert(h.m->fClockBootToken==boot);pump();
 }
 // A competing/poisoned physical path is detected by a child timer BEFORE writes.
 for(unsigned drift=0;drift<8;++drift){Harness h;startRelease(h);phase(h,SweetMDSS::ManagedParkNoC);
  h.life();assert(h.m->noCBusy());
  if(drift==0)regs[bases[4]+0x288]|=1u<<20;
  if(drift==1)regs[TG+0x30c]|=1u<<19;
  if(drift==2)regs[bases[6]+8]|=2;
  if(drift==3)regs[bases[3]+0x18]=1;
  if(drift==4)regs[bases[4]+0x2b0]=100;
  if(drift==5)regs[bases[1]+0x2028]=0x80000000u;
  if(drift==6)regs[bases[12]+0x6b0]=0;
  if(drift==7)regs[bases[2]+0x14]=0x9d200000u;
  auto count=writes();auto start=trace.size();SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
  assert(h.m->fLifecycle.phase==SweetPanelFault);
  // Failure may mask the owned IRQ, but never issues another data TCS command.
  for(unsigned i=start;i<trace.size();++i)assert(!trace[i].write||trace[i].addr<DT||trace[i].addr>=DT+0x3000);
  noMoreWrites(h);assert(writes()>=count);
 }

 // Preserve independent active leases and their aggregate demand. Releasing
 // our ticket is NOT evidence that every hardware vote/gate became zero.
 {Harness h;ready(h);uint64_t otherData=0,otherClock=0;
  auto v=managedBandwidth();v.port[0].peak=v.port[1].peak=v.llcc.peak=v.ebi.peak=128000000;v.registerPeak=500000000;
  assert(h.m->retainBandwidth(v,&otherData)==0);for(unsigned i=0;i<2000&&h.m->noCBusy();++i)h.step();
  assert(h.m->retainLocalClocks(SweetClockDSI0,&otherClock)==0);for(unsigned i=0;i<2000&&h.m->clockBusy();++i)h.step();
  panelPackets.clear();sleep(h);assert(h.m->fLifecycle.resourcesParked&&!h.m->fManagedClock&&!h.m->fManagedBandwidth);
  SweetNoCStatus n{};SweetClockLease c{};
  assert(h.m->bandwidthStatus(otherData,&n)==0&&n.state==SweetNoCHeld);
  assert(h.m->localClockStatus(otherClock,&c)==0&&c.state==SweetClockHeld);
  assert(h.m->fNoCAggregate.registerPeak==500000000&&h.m->fNoCAggregate.port[0].peak==128000000);
  for(unsigned r=1;r<SweetDisplayBCMCount;++r)assert(h.m->fNoCApplied[r]);
  h.power(SweetPanelDisplayOn);h.wakePixels();
  assert(h.m->bandwidthStatus(otherData,&n)==0&&n.state==SweetNoCHeld);
  assert(h.m->localClockStatus(otherClock,&c)==0&&c.state==SweetClockHeld);
 }
 // A child timer cannot advance even one data write past the resource budget.
 {Harness h;startRelease(h);phase(h,SweetMDSS::ManagedParkNoC);h.life();assert(h.m->noCBusy());
  fakeTicks+=4000000000ULL;auto start=trace.size();SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
  assert(h.m->fLifecycle.phase==SweetPanelFault);
  for(unsigned i=start;i<trace.size();++i)assert(!trace[i].write||trace[i].addr<DT||trace[i].addr>=DT+0x3000);
  noMoreWrites(h);
 }
 // A stuck DATA ACK never reports Parked and never releases power/config.
 {Harness h;startRelease(h);phase(h,SweetMDSS::ManagedParkNoC);
  for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step(true,false);
  failed(h);assert(h.m->fManagedBandwidth&&h.m->fManagedConfigBandwidth&&!h.m->fLifecycle.resourcesParked);}

 {Harness h;ready(h);panelPackets.clear();parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);
  for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step(true,false);
  failed(h);assert(panelPackets.empty()&&pinEdges.empty()&&!h.m->fLifecycle.resetComplete&&!h.m->fLifecycle.resourcesRestored);}
 // Allocation/token exhaustion or a lost lease is explicit, no mode2 fallback.
 {Harness h;startRelease(h);h.m->fNoCNextToken=~0ULL;failed(h);assert(h.m->fManagedClock&&h.m->fManagedBandwidth);}
 {Harness h;ready(h);panelPackets.clear();parked(h);panelPackets.clear();h.power(SweetPanelDisplayOn);h.m->fClockNextToken=~0ULL;
  failed(h);assert(panelPackets.empty()&&pinEdges.empty()&&!h.m->fLifecycle.resetComplete);}
 // Invalid live identity/dependencies still fail at startup without writes.
 for(unsigned missing=0;missing<2;++missing){warmFixture();coreFixture();hostFixture();linkFixture();touchSetup();auto*d=displayNub();
  if(missing==0){auto*r=(OSData*)d->properties[MH2LM_PANEL_PROPERTY];r->bytes[24]^=1;}
  else panelPinsMode=1;
  auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());mdssClockWriter=mdssNoCWriter=0;}
 puts("PASS: actual mode3 four profiles x four DATA release/reacquire/reset/wake cycles; exact wire tables, real data BCM zero ACK and new tokens, stable config/boot/rails/ARC, actual TCM resume/pixel proof, cancel/stop/withdrawal at every phase, child-before-parent physical drift, missing ACK and exhaustion; no fallback; zero objects/maps/allocations/notifications after simulated reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-resource-parking-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
