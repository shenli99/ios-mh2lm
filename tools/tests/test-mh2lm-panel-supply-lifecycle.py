#!/usr/bin/env python3
"""Actual mode6 full OFF/reset-low + DATA/link parking + panel vote cycle.
Only OS/MMIO and independent physical/wire peers are fixtures.
Usage: test-mh2lm-panel-supply-lifecycle.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-combined-suspend.py'
ns={'__file__':str(fixture),'__name__':'supply_lifecycle_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers']
touch=ns['touch'].replace('lifecycleMode=5;', 'lifecycleMode=6;')
prelude=prelude.replace('static void writeReg(uintptr_t b',
 'struct SupplyWrite{uint64_t time;uintptr_t address;uint32_t value;};static std::vector<SupplyWrite>supplyWrites;\nstatic void writeReg(uintptr_t b')
prelude=prelude.replace('trace.push_back({true,b+o,v});','trace.push_back({true,b+o,v});supplyWrites.push_back({fakeTicks,b+o,v});')
tests=ns['tests'].split('int main(){')[0]+r'''
static void supplyPark(Harness&h){
 panelPackets.clear();h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);
 auto&m=*h.m;auto&v=m.fLifecycle;checkPackets(selectedProfile,false);
 assert(v.panelVotesParked&&!v.panelVotesRestored&&v.resetHeldLow&&v.panelReleasedMask==3u&&v.rails==12u&&!v.retainedPower);
 assert(v.resourcesParked&&v.linkPoweredDown&&!v.resourcesHeld&&v.configHeld&&v.sleeping&&v.touchQuiesced&&v.displayDrained);
 assert(!v.dataToken&&!v.clockToken&&!v.supplyToken&&v.configToken&&v.panelVoteTokens[0]&&v.panelVoteTokens[1]);
 assert(m.fManagedRails==12u&&m.fManagedState==SweetMDSS::ManagedParked&&!m.fManagedToken&&!m.fManagedBandwidth&&!m.fManagedClock);
 assert(m.linkDormantConfiguration()&&m.managedRetainedResources()&&m.managedConfigHeld());
 assert(m.fLinkSuspendedMask==0x70&&!m.lifecycleResourcesHeld()&&!m.fLifecyclePinsActive);
 for(unsigned r=0;r<4;++r)assert(h.r->fClients[0].votes[r].enabled==(r>=2));
 SweetPanelPinStatus p{};assert(h.p->copyLowStatus(m.fPanelPinsConsumer,v.pinToken,&p)==0&&!p.resetHigh);
 assert(!h.r->fPanelFence.pins&&!h.r->fPanelFence.touch);
}
static void quiet(Harness&h){
 auto before=trace.size();h.life();h.scan();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
 SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
 SweetRPMh::timerAction(h.r,h.r->fTimer);SweetPanelPins::timerAction(h.p,h.p->fTimer);
 assert(trace.size()==before);
}
static void supplyFailed(Harness&h){
 for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault&&h.m->fLifecycle.phase!=SweetPanelStopped;++i)h.step();
 assert(h.m->fLifecycle.uncertain&&(h.m->fLifecycle.phase==SweetPanelFault||h.m->fLifecycle.phase==SweetPanelStopped));
 assert(!h.r->fBusy&&!h.r->fPanelFence.pins&&!h.r->fPanelFence.touch&&h.pool->pinned);
 assert((h.m->fManagedRails&12u)==12u&&h.m->fManagedARC&&h.r->fClients[0].votes[2].enabled&&h.r->fClients[0].votes[3].enabled);
 for(unsigned r=0;r<2;++r)assert(bool(h.m->fManagedRails&(1u<<r))==h.r->fClients[0].votes[r].enabled);
 quiet(h);
}
static void reachVote(Harness&h,bool on,unsigned rail,bool triggered=false,bool cleanup=false){
 if(on){supplyPark(h);h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
 for(unsigned i=0;i<12000;++i){
  if(h.m->fLifecycle.supplyRail==rail&&h.m->fLifecycle.supplyToken&&h.r->fBusy&&(!triggered||h.r->fExecuting)&&(!cleanup||h.r->fOperationCount))return;
  assert(h.m->fLifecycle.phase!=SweetPanelFault);h.step();
 }
 assert(false);
}
static unsigned commandIndex(uint32_t value,unsigned start=0){
 for(unsigned i=start;i<supplyWrites.size();++i)if(supplyWrites[i].address==TCS+0x34&&supplyWrites[i].value==value)return i;
 assert(false);return 0;
}
static uint64_t ackTime(unsigned start){
 bool triggered=false;
 for(unsigned i=start;i<supplyWrites.size();++i){auto&w=supplyWrites[i];
  if(w.address==TCS+0x14&&w.value==((1u<<16)|(1u<<24)))triggered=true;
  if(triggered&&w.address==TCS)return w.time;
 }
 assert(false);return 0;
}
int main(){
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;ready(h);
  assert(h.m->panelLifecycleCapabilities()&SweetPanelSupplyCycle);
  auto boot=h.m->fClockBootToken,arc=h.m->fManagedARC,rsc=h.m->fNoCClockToken;uint64_t floor=0;
  for(unsigned round=0;round<3;++round){
   pinEdges.clear();supplyWrites.clear();auto resets=phyResets,host=hostResets;supplyPark(h);
   assert(phyResets==resets&&hostResets==host&&pinEdges.size()==1&&pinEdges[0].second==0);
   assert(pinEdges[0].first>=panelPackets.back().time+155000000ULL);
   unsigned vpnloff=commandIndex(0x40104),vddioff=commandIndex(0x40004);
   assert(vpnloff<vddioff&&fakeTicks>=ackTime(vddioff)+5000000ULL);
   unsigned resetIndex=~0u,hostIndex=~0u;
   for(unsigned i=0;i<supplyWrites.size();++i){auto&w=supplyWrites[i];
    if(w.address==0x03d06004&&w.value==0&&resetIndex==~0u)resetIndex=i;
    if(w.address==bases[6]+4&&(w.value&7u)==1&&hostIndex==~0u)hostIndex=i;
    assert(w.address!=TCS+0x34||(w.value&~15u)!=0x40200u);assert(w.address!=TCS+0x34||(w.value&~15u)!=0x40300u);
   }
   assert(resetIndex<hostIndex&&hostIndex<vpnloff);
   assert(supplyWrites[hostIndex].time>=pinEdges.front().first+5000000ULL);
   supplyWrites.clear();
   if(!floor)floor=h.m->fManagedConfigBandwidth;
   panelPackets.clear();h.power(SweetPanelDisplayOn);assert(h.m->fLifecycle.phase==SweetPanelSupplyActivate);
   h.untilLife(SweetPanelPinReset);assert(h.m->fLifecycle.panelVotesRestored&&!h.m->fLifecycle.panelReleasedMask&&h.m->fManagedRails==15u);
   assert(h.m->fLifecycle.linkRebuilt&&h.m->fLifecycle.resourcesRestored&&h.m->lifecycleResourcesHeld());
   assert(phyResets==resets+1&&hostResets==host+1&&panelPackets.empty());
   unsigned vddion=commandIndex(0x40000),vpnon=commandIndex(0x40100);
   assert(vddion<vpnon&&supplyWrites[vpnon].time>=ackTime(vddion)+20000000ULL);
   uint64_t activeTime=0;for(unsigned i=0;i<vddion;++i)if(supplyWrites[i].address==0x03908000)activeTime=supplyWrites[i].time;
   assert(activeTime&&supplyWrites[vddion].time>=activeTime+5000000ULL);
   h.wakePixels();checkPackets(profile,true,0x345);
   assert(h.m->fLifecycle.panelVotesRestored&&!h.m->fLifecycle.panelVotesParked&&!h.m->fLifecycle.resetHeldLow);
   assert(h.m->fLifecycle.panelAggregateEnabledMask==3u&&h.m->fLifecycle.rails==15u);
   assert(h.m->fClockBootToken==boot&&h.m->fManagedARC==arc&&h.m->fNoCClockToken==rsc&&h.m->fManagedConfigBandwidth==floor);
   assert(pinEdges.size()==4&&pinEdges[1].second==2&&pinEdges[2].second==0&&pinEdges[3].second==2);
   assert(pinEdges[1].first>=ackTime(vpnon)+5000000ULL);
   for(unsigned r=0;r<4;++r)assert(h.r->fClients[0].votes[r].enabled);
  }
 }
 // All added pin preparation phases and supply votes cancel/stop/provider
 // withdrawal with explicit failure and no late writer.
 for(unsigned phase:{unsigned(SweetPanelPinParkActivate),unsigned(SweetPanelPinParkWait),unsigned(SweetPanelPinPark)})
  for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);h.power(SweetPanelDisplayOff);h.untilLife(phase);
   if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
   if(fault==1)h.m->stop(h.d);
   if(fault==2){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);pump();}
   supplyFailed(h);assert(h.m->fManagedRails==15u);
  }
 for(bool on:{false,true})for(unsigned rail=0;rail<2;++rail)for(unsigned stage=0;stage<3;++stage)
  for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);reachVote(h,on,rail,stage>=1,stage>=2);
   auto pending=h.m->fLifecycle.supplyToken;auto expected=h.m->fManagedRails;
   if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
   if(fault==1)h.m->stop(h.d);
   if(fault==2){SweetTouchPanelClient::terminated(h.m->fTouchPanelClient,nullptr,h.t,nullptr);pump();}
   supplyFailed(h);assert(h.m->fLifecycle.supplyToken==pending&&h.m->fManagedRails==expected);
  }
 // Cancellation in the active-low restore preparation and ACK-based waits.
 for(unsigned target=0;target<5;++target){Harness h;ready(h);
  bool on=target>=1;if(on){supplyPark(h);h.power(SweetPanelDisplayOn);}
  else h.power(SweetPanelDisplayOff);
  for(unsigned i=0;i<12000;++i){auto&v=h.m->fLifecycle;
   bool at=target==0 ? (v.phase==SweetPanelSupplyPark&&v.panelReleasedMask==3u) :
    (target==1 ? v.phase==SweetPanelSupplyActivate :
    (target==2 ? (v.phase==SweetPanelSupplyRestore&&v.supplyRail==SweetRailVDDI&&!v.supplyToken) :
    (target==3 ? (v.phase==SweetPanelSupplyRestore&&v.panelReleasedMask==2u&&!v.supplyToken) :
                (v.phase==SweetPanelSupplyRestore&&!v.panelReleasedMask&&!v.supplyToken))));
   if(at)break;assert(v.phase!=SweetPanelFault);h.step();assert(i<11999);
  }
  auto rails=h.m->fManagedRails;h.m->cancelPanelLifecycle(h.m->fLifecycle.token);supplyFailed(h);assert(h.m->fManagedRails==rails);
 }
 // Resource/link child timers also require the new low proof. This loss is
 // injected BEFORE the parent timer can see it, with no new payload writes.
 for(bool restore:{false,true})for(bool link:{false,true}){Harness h;ready(h);
  if(restore){supplyPark(h);h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
  h.untilLife(link ? (restore ? SweetPanelLinkRebuild : SweetPanelLinkPowerDown) :
                    (restore ? SweetPanelResourceRestore : SweetPanelResourcePark));
  regs[0x03d06004]|=1;auto begin=trace.size();
  SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
  for(unsigned i=begin;i<trace.size();++i)if(trace[i].write){auto addr=trace[i].addr;
   assert(!(addr>=DT&&addr<DT+0x3000));assert(!(addr>=TCS&&addr<TCS+0x3000));assert(!(addr>=bases[12]&&addr<bases[12]+0x760));
  }
  h.life();supplyFailed(h);
 }
 // Child commits ACK before parent cancellation: the exact successful vote
 // must be reconciled, even though the coordinator never polled it normally.
 for(bool on:{false,true})for(unsigned rail=0;rail<2;++rail){Harness h;ready(h);reachVote(h,on,rail,true);
  auto expected=h.m->fManagedRails;auto token=h.m->fLifecycle.supplyToken;
  auto*r=h.r;uint32_t b=r->fTCSIndex*0x2a0;regs[TCS+4]|=1u<<r->fTCSIndex;regs[TCS+b+0x18]=1;
  for(unsigned c=0;c<r->fCommandCount;++c)regs[TCS+b+0x3c+c*0x14]=(1<<8)|(1<<16);
  for(unsigned i=0;i<3&&r->fBusy;++i){fakeTicks+=100000;if(!deliverRPMhIRQ()){std::lock_guard<std::recursive_mutex>l(r->fLoop->gate);r->advance();}}
  assert(!r->fBusy&&r->fClients[0].status.token==token&&r->fClients[0].status.state==SweetRPMhComplete);
  assert(h.m->fManagedRails==expected);h.m->cancelPanelLifecycle(h.m->fLifecycle.token);supplyFailed(h);
  assert(h.m->fManagedRails==(on ? (expected|(1u<<rail)) : (expected&~(1u<<rail)))&&!h.m->fLifecycle.supplyToken);
  assert(h.m->fLifecycle.panelVoteTokens[rail]==token);
 }
 // Stop/invalid low-hold before the child timer must prevent ANY new TCS
 // write; the parent is deliberately not scheduled until after the child.
 for(bool on:{false,true})for(unsigned fault=0;fault<8;++fault){Harness h;ready(h);reachVote(h,on,0,false);
  if(fault==0)regs[0x03d06004]|=1;
  if(fault==1)regs[0x03908008]^=0x10;
  if(fault==2)h.p->stop(h.pn);
  if(fault==3)h.t->stop(h.tn);
  if(fault==4)tcFixtureOnline(0);
  if(fault==5)h.p->cancelRequest(h.m->fPanelPinsConsumer,h.m->fLifecycle.pinToken);
  if(fault==6)h.t->cancelRequest(h.m->fTouchPanelConsumer,h.m->fLifecycle.touchToken);
  if(fault==7)fakeTicks+=500000000ULL;
  auto from=trace.size();auto oldEnable=regs[TCS];{std::lock_guard<std::recursive_mutex>l(h.r->fLoop->gate);h.r->advance();}
  assert(!h.r->fBusy);irqQuiesceOnly(from,oldEnable);h.life();supplyFailed(h);
 }
 // Missing ACK, posted-write failure and pin input loss never bypass the
 // dependency. A retry cannot adopt a partly released supply generation.
 for(bool on:{false,true})for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);reachVote(h,on,0,false);
  if(fault==0){for(unsigned i=0;i<1100&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step(false);}
  if(fault==1){rpmhDrop=0x34;supplyFailed(h);rpmhDrop=~0u;}
  if(fault==2){regs[0x03d06004]|=1;h.life();}
  supplyFailed(h);uint64_t token=99;auto before=writes();assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&token)!=0&&!token&&writes()==before);
 }
 // Aggregate enabled is allowed when an actual second consumer holds the
 // panel rails. Never confuse withdrawal of our ticket with voltage off.
 {Harness h;ready(h);auto other=attach(h.r);
  for(unsigned rail=0;rail<2;++rail){auto t=vote(h.r,other,(SweetRail)rail,managedRailVotes[rail]);assert(!h.r->fBusy&&status(h.r,other,t).enabled);}
  supplyPark(h);assert(h.m->fLifecycle.panelAggregateEnabledMask==3u);
  h.power(SweetPanelDisplayOn);h.wakePixels();assert(h.m->fLifecycle.panelVotesRestored);
  for(unsigned rail=0;rail<2;++rail){vote(h.r,other,(SweetRail)rail,{});assert(!h.r->fBusy);}
  assert(h.r->detachConsumer(other)==0);
 }
 // Parked config drift is diagnostic; dependency/ownership loss still rejects ON.
 for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);supplyPark(h);
  if(fault==0)regs[0x03d06004]=1;
  if(fault==1)regs[bases[1]+0x2114]^=1u<<14;
  if(fault==2)h.m->fLifecycle.panelReleasedMask=1;
  if(fault==3)h.m->fLifecycle.panelVotesParked=false;
  uint64_t token=99;auto before=writes();
  if(fault==1){assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&token)==0&&token);h.wakePixels();}
  else assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&token)!=0&&!token&&writes()==before);
 }
 puts("PASS: actual mode6 four profiles x three supply cycles; reset-low before link shutdown, VDDI/VPNL typed votes, shared L3/L5/ARC/config/boot retained, resource/link/host/reset/ON/pixel/TCM recovery, no fallback and zero objects/maps/allocations/notifications after reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-supply-lifecycle-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
