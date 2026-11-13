#!/usr/bin/env python3
"""Actual Pins + RPMh + TouchComm: per-write reset-low/touch fences.
This suite tests provider APIs independently: mode5 creates a stopped link,
then tests explicitly drive the leaf/provider API. Only OS/MMIO/wire peers
are fixtures. Usage: test-mh2lm-panel-supply-fences.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-combined-suspend.py'
ns={'__file__':str(fixture),'__name__':'panel_supply_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers'];touch=ns['touch']
# Independent ownership loss immediately after a selected TCS write. This
# exercises two writes within ONE provider timer, not only inter-timer loss.
prelude=prelude.replace('static void writeReg(uintptr_t b',
 'static unsigned fenceDropOffset=~0u,fenceDropOccurrence=0;\nstatic void writeReg(uintptr_t b')
prelude=prelude.replace('trace.push_back({true,b+o,v});',
 'trace.push_back({true,b+o,v});if(b==0x18220d00&&o==fenceDropOffset&&!--fenceDropOccurrence){regs[0x03d06000]|=1u<<12;fenceDropOffset=~0u;}')
tests=ns['tests'].split('int main(){')[0]+r'''
static void pinTick(Harness&h){
 fakeTicks+=100000;regs[0x03d06004]=(regs[0x03d06004]&~1u)|((regs[0x03d06004]>>1)&1u);
 std::lock_guard<std::recursive_mutex>l(h.p->fLoop->gate);SweetPanelPins::timerAction(h.p,h.p->fTimer);
}
static SweetPanelSupplyFence prepare(Harness&h,bool activeLow=false){
 ready(h);parked(h);uint64_t token=0;
 assert(h.p->activate(h.m->fPanelPinsConsumer,&token)==0);pinTick(h);
 assert(h.p->park(h.m->fPanelPinsConsumer,&token)==0);
 for(unsigned i=0;i<100&&h.p->fBusy;++i)pinTick(h);assert(h.p->fStatus.state==SweetPinsParked);
 if(activeLow){assert(h.p->activate(h.m->fPanelPinsConsumer,&token)==0);pinTick(h);assert(h.p->fStatus.state==SweetPinsActive&&!h.p->fStatus.resetHigh);}
 return {h.p,h.t,h.m->fPanelPinsConsumer,token,h.m->fTouchPanelConsumer,h.m->fLifecycle.touchToken};
}
static SweetRailVote supplyVote(SweetRail rail,bool enable){
 if(!enable)return {false,0,0,0};
 return rail==SweetRailVDDI ? SweetRailVote{true,1800,1880,62000} : SweetRailVote{true,3000,3008,857000};
}
static uint64_t panelVote(Harness&h,SweetRail rail,bool enable,const SweetPanelSupplyFence&f){
 uint64_t token=0;auto before=writes();
 assert(h.r->requestPanelVote(h.m->fRPMhConsumer,rail,supplyVote(rail,enable),f,&token)==0&&token);
 assert(writes()==before);return token;
}
static void child(Harness&h){
 fakeTicks+=100000;if(!deliverRPMhIRQ()){std::lock_guard<std::recursive_mutex>l(h.r->fLoop->gate);SweetRPMh::timerAction(h.r,h.r->fTimer);}
}
static void freshACK(Harness&h){
 auto*r=h.r;uint32_t b=r->fTCSIndex*0x2a0;
 regs[TCS+4]|=1u<<r->fTCSIndex;regs[TCS+b+0x18]=1;
 for(unsigned c=0;c<r->fCommandCount;++c)regs[TCS+b+0x3c+c*0x14]=(1<<8)|(1<<16);
}
static SweetRPMhStatus panelStatus(Harness&h,uint64_t token){SweetRPMhStatus s{};assert(h.r->copyStatus(h.m->fRPMhConsumer,token,&s)==0);return s;}
static void finishVote(Harness&h,uint64_t token,bool enabled){
 child(h);assert(h.r->fBusy);child(h);assert(h.r->fBusy); // stale command bits do not complete
 freshACK(h);child(h);assert(h.r->fBusy);child(h);assert(!h.r->fBusy&&!h.r->fPanelFence.pins&&!h.r->fPanelFence.touch);
 auto s=panelStatus(h,token);assert(s.state==SweetRPMhComplete&&s.result==0&&!s.uncertain&&s.enabled==enabled);
}
int main(){
 // Both successful low states, all real aggregation/no-op/ACK semantics and
 // all dependency refs. Caller may reuse its stack struct after admission.
 for(bool activeLow:{false,true}){Harness h;auto f=prepare(h,activeLow);
  auto pr=h.p->refs.load(),tr=h.t->refs.load();uint64_t token=99;auto before=writes();
  for(auto rail:{SweetRailHost,SweetRailPHY})assert(h.r->requestPanelVote(h.m->fRPMhConsumer,rail,{},f,&token)==kIOReturnNotPermitted&&!token);
  assert(h.r->requestPanelVote(h.m->fRPMhConsumer,SweetRailVDDI,{},f,nullptr)==kIOReturnBadArgument&&writes()==before);
  for(auto rail:{SweetRailVPNL,SweetRailVDDI}){
   token=panelVote(h,rail,false,f);assert(h.p->refs==pr+1&&h.t->refs==tr+1);
   auto saved=f;f={};finishVote(h,token,false);f=saved;
   assert(h.p->refs==pr&&h.t->refs==tr&&!h.r->fClients[0].votes[rail].enabled);
   before=writes();token=panelVote(h,rail,false,f); // exact no-op still checks live leaves
   assert(!h.r->fBusy&&writes()==before&&h.p->refs==pr&&h.t->refs==tr);
   assert(panelStatus(h,token).state==SweetRPMhComplete);
  }
  for(auto rail:{SweetRailVDDI,SweetRailVPNL}){token=panelVote(h,rail,true,f);finishVote(h,token,true);}
  assert(h.r->fClients[0].votes[SweetRailHost].enabled&&h.r->fClients[0].votes[SweetRailPHY].enabled);
  assert(h.m->fManagedARC&&h.m->fManagedConfigBandwidth&&!h.m->fManagedClock&&!h.m->fManagedBandwidth);
  assert(h.p->refs==pr&&h.t->refs==tr);
 }
 // Admission failure has no token, TCS write, ref or old vote/status effect.
 for(unsigned fault=0;fault<15;++fault){Harness h;auto f=prepare(h);
  auto pr=h.p->refs.load(),tr=h.t->refs.load();auto old=h.r->fClients[0].status;auto serial=h.r->fNextToken;
  if(fault==0)f.pins=nullptr;if(fault==1)f.touch=nullptr;if(fault==2)++f.pinsConsumer;if(fault==3)++f.pinsToken;
  if(fault==4)++f.touchConsumer;if(fault==5)++f.touchToken;
  if(fault==6)h.p->inactive=true;if(fault==7)h.t->inactive=true;
  if(fault==8)h.p->cancelRequest(f.pinsConsumer,f.pinsToken);
  if(fault==9)h.t->cancelRequest(f.touchConsumer,f.touchToken);
  if(fault==10)regs[0x03d06000]|=1u<<12;
  if(fault==11){uint64_t t;assert(h.p->activate(f.pinsConsumer,&t)==0);pinTick(h);}
  if(fault==12){uint64_t t;assert(h.t->resume(f.touchConsumer,&t)==0);}
  if(fault==13)h.r->fMode=1;
  if(fault==14)h.r->fPoisoned=true;
  uint64_t token=99;auto before=writes();assert(h.r->requestPanelVote(h.m->fRPMhConsumer,SweetRailVPNL,{},f,&token)!=0&&!token);
  assert(writes()==before&&h.r->fNextToken==serial&&h.r->fClients[0].status.token==old.token);
  assert(h.r->fClients[0].votes[SweetRailVPNL].enabled&&!h.r->fBusy&&h.p->refs==pr&&h.t->refs==tr);
 }
 // Child-before-parent loss of either leaf at admission/programmed/cleanup/
 // outstanding posted-write phases. No parent timer runs after the loss.
 for(unsigned phase=0;phase<4;++phase)for(unsigned fault=0;fault<12;++fault){Harness h;auto f=prepare(h);
  auto pr=h.p->refs.load(),tr=h.t->refs.load();auto token=panelVote(h,SweetRailVPNL,false,f);
  if(phase==1){child(h);assert(h.r->fExecuting);}
  if(phase==2){child(h);freshACK(h);child(h);assert(h.r->fExecuting&&h.r->fOperationCount);}
  if(phase==3){rpmhDrop=0x34;child(h);assert(h.r->fWriteIssued&&!h.r->fExecuting);}
  if(fault==0)regs[0x03d06000]|=1u<<12;
  if(fault==1)regs[0x03908000]|=1u<<12;
  if(fault==2)regs[0x03d06008]|=1;
  if(fault==3)regs[0x03908008]|=1;
  if(fault==4)h.p->cancelRequest(f.pinsConsumer,f.pinsToken);
  if(fault==5)h.p->stop(h.pn);
  if(fault==6)h.t->cancelRequest(f.touchConsumer,f.touchToken);
  if(fault==7)h.t->stop(h.tn);
  if(fault==8)tcFixtureOnline(0);
  if(fault==9){uint64_t newer;assert(h.p->activate(f.pinsConsumer,&newer)==0);}
  if(fault==10){uint64_t newer;assert(h.t->resume(f.touchConsumer,&newer)==0);}
  if(fault==11)h.r->inactive=true;
  auto from=trace.size();auto oldEnable=regs[TCS];child(h);assert(!h.r->fBusy);irqQuiesceOnly(from,oldEnable);
  auto s=h.r->fClients[0].status;assert(s.token==token&&s.state==SweetRPMhFailed&&s.result==kIOReturnNotReady&&s.uncertain==(phase!=0));
  assert(h.r->fClients[0].votes[SweetRailVPNL].enabled&&h.r->fAggregate[SweetRailVPNL].enabled);
  assert(h.p->refs==pr&&h.t->refs==tr&&!h.r->fPanelFence.pins&&!h.r->fPanelFence.touch);
  auto n=trace.size();child(h);assert(trace.size()==n);rpmhDrop=~0u;
 }
 // Recheck BETWEEN individual writes within one child callback, including
 // immediately before trigger and after the final completion cleanup write.
 for(unsigned off:{0u,0x30u,0x34u,0x38u,0x10u,0x1cu,0x14u})for(bool cleanup:{false,true}){
  Harness h;auto f=prepare(h);auto token=panelVote(h,SweetRailVPNL,false,f);
  if(cleanup){child(h);freshACK(h);child(h);if(off==0x30||off==0x34||off==0x38)continue;}
  fenceDropOffset=off;fenceDropOccurrence=off==0x14 ? 2 : 1;
  auto before=writes();child(h);
  assert(!h.r->fBusy&&fenceDropOffset==~0u&&writes()>before);
  auto s=panelStatus(h,token);assert(s.state==SweetRPMhFailed&&s.uncertain&&h.r->fClients[0].votes[SweetRailVPNL].enabled);
  assert(cleanup||!h.r->fExecuting); // no trigger after low-hold proof is lost
  auto n=trace.size();child(h);assert(trace.size()==n);
 }
 // Failures inside begin() release both retained leaf references, preserving
 // the previous client vote and completion; no admission token escapes.
 for(unsigned fault=0;fault<4;++fault){Harness h;auto f=prepare(h);
  auto v=supplyVote(SweetRailVPNL,false);auto pr=h.p->refs.load(),tr=h.t->refs.load();auto old=h.r->fClients[0].status;
  if(fault==0)v={true,1,2,1};
  if(fault==1)h.r->fNextToken=~0ULL;
  if(fault==2)for(unsigned t=0;t<2;++t)regs[TCS+t*0x2a0+0x18]=0;
  if(fault==3)regs[TCS+0x1c]=0x80000000;
  uint64_t token=99;auto before=writes();assert(h.r->requestPanelVote(h.m->fRPMhConsumer,SweetRailVPNL,v,f,&token)!=0&&!token);
  assert(writes()==before&&h.p->refs==pr&&h.t->refs==tr&&!h.r->fPanelFence.pins&&!h.r->fBusy);
  assert(h.r->fClients[0].status.token==old.token&&h.r->fClients[0].votes[SweetRailVPNL].enabled);
 }
 // Scoped cancellation, missing ACK/deadline, provider shutdown. No cleanup
 // TCS writes and no leaked dependency refs, including after actual trigger.
 for(unsigned phase=0;phase<3;++phase)for(unsigned fault=0;fault<3;++fault){Harness h;auto f=prepare(h);
  auto pr=h.p->refs.load(),tr=h.t->refs.load();auto token=panelVote(h,SweetRailVPNL,false,f);
  if(phase>=1)child(h);if(phase==2){freshACK(h);child(h);}
  assert(h.r->cancelPanelVote(h.m->fRPMhConsumer+1,token)==kIOReturnNotFound);
  assert(h.r->cancelPanelVote(h.m->fRPMhConsumer,token+1)==kIOReturnNotFound&&h.r->fBusy);
  auto from=trace.size();auto oldEnable=regs[TCS];
  if(fault==0)assert(h.r->cancelPanelVote(h.m->fRPMhConsumer,token)==0);
  if(fault==1){fakeTicks+=100000000ULL;child(h);}
  if(fault==2)h.r->stop(h.n);
  assert(!h.r->fBusy&&!h.r->fPanelFence.pins&&h.p->refs==pr&&h.t->refs==tr);irqQuiesceOnly(from,oldEnable);
  assert(h.r->fClients[0].votes[SweetRailVPNL].enabled);
  auto n=trace.size();child(h);assert(trace.size()==n);
 }
 // One TCS owner: competing ordinary/fenced requests cannot replace a pending
 // transaction or its fence. Cancellation never aborts an ordinary LDO vote.
 {Harness h;auto f=prepare(h);auto token=panelVote(h,SweetRailVPNL,false,f);uint64_t denied=99;
  auto other=attach(h.r);auto pr=h.p->refs.load(),tr=h.t->refs.load();
  assert(h.r->requestPanelVote(other,SweetRailVDDI,{},f,&denied)==kIOReturnBusy&&!denied);
  assert(h.r->requestVote(other,SweetRailVDDI,{},&denied)==kIOReturnBusy&&!denied);
  assert(h.p->refs==pr&&h.t->refs==tr);finishVote(h,token,false);
  token=vote(h.r,h.m->fRPMhConsumer,SweetRailVPNL,supplyVote(SweetRailVPNL,true));
  assert(h.r->cancelPanelVote(h.m->fRPMhConsumer,token)==0&&h.r->fBusy);
  finishVote(h,token,true);assert(h.r->detachConsumer(other)==0);
 }
 // A different real consumer may keep aggregate enabled: successful removal
 // of this panel vote is not proof of rail-off, or a reason to reject the ACK.
 {Harness h;auto f=prepare(h);auto other=attach(h.r);
  auto token=vote(h.r,other,SweetRailVPNL,supplyVote(SweetRailVPNL,true));
  assert(!h.r->fBusy&&status(h.r,other,token).enabled);
  token=panelVote(h,SweetRailVPNL,false,f);
  assert(!h.r->fBusy&&panelStatus(h,token).enabled&&!h.r->fClients[0].votes[SweetRailVPNL].enabled);
  token=vote(h.r,other,SweetRailVPNL,{});child(h);freshACK(h);child(h);child(h);
  assert(!h.r->fBusy&&!status(h.r,other,token).enabled&&h.r->detachConsumer(other)==0);
 }
 puts("PASS: actual Pins/RPMh/TCM reset-low fences; Parked/Active-low, live provider/eGPIO/IRQ ownership, diagnostic CTL/latch/input, exact leaf tokens, retain/release, no-op/aggregation/fresh ACK, invalid admission, four child-before-parent phases x twelve faults, cancel/timeout/stop, competing consumers, shared L3/L5 untouched, zero objects/maps/notifications/allocations after reboot; provider APIs tested independently");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-supply-fences-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
