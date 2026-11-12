#!/usr/bin/env python3
"""Real core/NoC/clock providers with independent RSC vote/status transitions.
Usage: test-mh2lm-rsc-clock-votes.py EXTRACTED_XNU
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
ns = {'__file__': str(fixture), '__name__': 'rsc_vote_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
actual = ns['actual']
tests = r'''
static constexpr unsigned leaves[]={0x4004,0x400c,0x4008};
static void offLeaves(bool votedRunning=true){
 // CBCR values observed in 0121a22: no local vote, aggregate may be ON.
 regs[bases[1]+leaves[0]]=votedRunning ? 0x2000u : 0x80002000u;
 regs[bases[1]+leaves[1]]=0x80004000u;
 regs[bases[1]+leaves[2]]=0x80008000u;
}
static void externalClocks(bool enable=true,bool halt=true,bool foreign=true){
 for(unsigned j=0;j<3;++j){auto &v=regs[bases[1]+leaves[j]];
  if(v&1u){if(enable)v&=~0x80000000u;}
  else if(j==0&&foreign)v&=~0x80000000u; // unrelated vote survives ours
  else if(halt)v|=0x80000000u;
 }
}
static void clockTick(Harness&h,unsigned us=100,bool enable=true,bool halt=true,bool foreign=true){
 fakeTicks+=uint64_t(us)*1000;SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);
 externalClocks(enable,halt,foreign);
}
static SweetClockLease clockStatus(Harness&h,uint64_t token){
 SweetClockLease s{};assert(h.m->localClockStatus(token,&s)==0);return s;
}
static void stepRSC(Harness&h,bool enable=true,bool halt=true,bool foreign=true){
 h.step();externalClocks(enable,halt,foreign);
}
static void configReady(Harness&h){
 h.begin();h.until(SweetMDSS::ManagedCore);
 for(unsigned j=0;j<4000&&!h.m->coreReady();++j)h.step();
 assert(h.m->coreReady());
 // Advance through the actual NoC CN0 ACK, but stop before the clock worker.
 for(unsigned j=0;j<4000&&h.m->fNoCPhase!=NClock;++j)h.step();
 assert(h.m->fNoCPhase==NClock&&h.m->fNoCRegisterToken);
}
static void heldRSC(Harness&h){
 for(unsigned j=0;j<4000&&h.m->fNoCPhase!=NData&&h.m->fManagedState!=SweetMDSS::ManagedFault;++j)stepRSC(h);
 if(h.m->fNoCPhase!=NData)for(auto &msg:messages)fputs(msg.c_str(),stderr);
 assert(h.m->fNoCPhase==NData);
 auto q=clockStatus(h,h.m->fNoCClockToken);
 assert(q.state==SweetClockHeld&&q.mask==0x381&&!q.uncertain);
 assert((h.m->fClockOwnedMask&0x380)==0x380);
}
static unsigned leafWrites(){unsigned count=0;for(auto&o:trace)if(o.write)
 for(auto leaf:leaves)count+=o.addr==bases[1]+leaf;return count;}
static void noPayload(){for(auto&o:trace)if(o.write)assert(!(o.addr>=DT&&o.addr<DT+0x3000));}
static void fault(Harness&h){
 for(unsigned j=0;j<4000&&h.m->fManagedState!=SweetMDSS::ManagedFault;++j)stepRSC(h);
 if(h.m->fManagedState!=SweetMDSS::ManagedFault)for(auto&msg:messages)fputs(msg.c_str(),stderr);
 if(h.m->fManagedState!=SweetMDSS::ManagedFault||sourceWritten()||h.pool->pinned)
  fprintf(stderr,"fault result managed=%u source=%u pinned=%u clock=%u NoC=%u\n",h.m->fManagedState,unsigned(sourceWritten()),unsigned(h.pool->pinned),h.m->fClockOperation,h.m->fNoCPhase);
 assert(h.m->fManagedState==SweetMDSS::ManagedFault&&!sourceWritten()&&!h.pool->pinned);
 noPayload();auto n=trace.size(),logs=messages.size();
 SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);
 SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
 assert(n==trace.size()&&logs==messages.size());
}
static void untilWrite(Harness&h,unsigned leaf,bool enable=false){
 for(unsigned j=0;j<4000&&!(h.m->fClockWaiting&&h.m->fClockCursor==leaf);++j)stepRSC(h,enable);
 assert(h.m->fClockWaiting&&h.m->fClockCursor==leaf);
}
int main(){
 // Reproduce the real failure with aggregate-on and aggregate-off voted input.
 // Neither a missing SW vote nor a physical ON status is a Held lease.
 for(unsigned sample=0;sample<4;++sample){Harness h;offLeaves(sample&1);
  if(sample<2)regs[bases[1]+0x4004]&=~0x2000u; // exact cbcr=0 as well as unrelated bits
  auto unrelated=regs[bases[1]+0x4004]&0x2000u;configReady(h);
  assert(!h.m->fNoCClockToken||clockStatus(h,h.m->fNoCClockToken).state!=SweetClockHeld);
  noPayload();heldRSC(h);assert(leafWrites()==3);
  assert(regs[bases[1]+0x4004]==(unrelated|1u)&&regs[bases[1]+0x400c]==0x4001&&regs[bases[1]+0x4008]==0x8001);
  assert(regs[bases[1]+0x22c0]==0x40f&&h.m->coreReady());
  h.finish();assert(h.m->fScanoutState==SweetScanoutActive&&sourceWritten());
 }
 // Request/worker must not start unrelated PLL or PHY leaves. Inherited
 // non-XO alone never grants the new RSC parent authority.
 for(unsigned leaf: {1u,4u,5u,6u,7u,8u}){
  Harness h;h.m->pauseIRQ();regs[bases[4]+0x288]=regs[bases[4]+0x2b4]=0;
  regs[bases[1]+mdssClockBranches[leaf].cbcr]=0x80000000;
  uint64_t token=99;trace.clear();
  auto use=leaf>=7 ? SweetClockRSC : SweetClockDSI0;
  assert(h.m->retainLocalClocks(use,&token)==kIOReturnNotReady&&!token&&!writes());
 }
 // All prerequisite failures occur before any new leaf write or display TCS
 // payload, even when the remaining registers appear ready.
 for(unsigned bad=0;bad<16;++bad){if(bad==14)continue;Harness h;offLeaves();configReady(h);trace.clear();
  if(bad==0)h.m->fCorePhase=CoreIdle;
  if(bad==1)h.m->fCoreProfile=0;
  if(bad==2)h.m->fManagedARC=0;
  if(bad==3)h.m->fNoCRegisterToken=0;
  if(bad==4)regs[bases[1]+0x22c0]=0x50f;
  if(bad==5)regs[bases[1]+0x1000]&=~0x80000000u;
  if(bad==6)h.r->fClients[0].displayStatus.uncertain=true;
  if(bad==7)h.r->fClients[0].displayStatus.state=SweetRPMhFailed;
  if(bad==8)h.r->fClients[0].displayStatus.acknowledged=3;
  if(bad==9)h.r->fClients[0].registerStatus.bytesPerSecond=76800000;
  if(bad==10)h.r->fClients[0].registerStatus.uncertain=true;
  if(bad==11)h.r->fClients[0].registerStatus.state=SweetRPMhPending;
  if(bad==12)h.m->fNoCRegisterToken=~0ULL;
  if(bad==13)regs[bases[4]+0x2b4]=1;
  if(bad==15)h.m->fManagedARC=~0ULL;
  fault(h);assert(!leafWrites());
 }
 {Harness h;offLeaves();regs[bases[1]+0x400c]=0x4000;h.begin();trace.clear();
  for(unsigned j=0;j<4000&&h.m->fManagedState!=SweetMDSS::ManagedFault;++j)h.step();
  assert(h.m->fManagedState==SweetMDSS::ManagedFault&&!leafWrites()&&!sourceWritten());noPayload();}
 {Harness h;offLeaves();configReady(h);
  while(h.m->fClockCursor!=8)stepRSC(h);
  assert(!h.m->fClockWaiting);regs[bases[1]+0x400c]&=~0x80000000u;
  auto count=leafWrites();clockTick(h);
  assert(clockStatus(h,h.m->fNoCClockToken).state==SweetClockFailed&&leafWrites()==count);
  fault(h);}
 // Register readback and independent physical completion both matter.
 for(unsigned bad=0;bad<4;++bad){Harness h;offLeaves();configReady(h);trace.clear();
  if(bad==0)dropClockWrite=bases[1]+0x4004; // aggregate ON cannot mask lost own vote
  if(bad==1){regs[bases[1]+0x4004]|=0x80000000u;} // own vote is not physical ON
  untilWrite(h,7);
  auto token=h.m->fNoCClockToken;assert(clockStatus(h,token).state==SweetClockPending);
  if(bad==2)regs[bases[1]+0x22c0]^=0x80000000u; // raw drift, same masked rate
  if(bad==3)h.r->fClients[0].registerStatus.uncertain=true;
  for(unsigned j=0;j<150&&h.m->clockBusy();++j)clockTick(h,100,false);
  assert(clockStatus(h,token).state==SweetClockFailed&&h.m->fClockFault);
  assert(h.m->fClockOwnedMask&0x80); // failed posted write retains uncertain owner
  auto writesBefore=leafWrites();fault(h);assert(leafWrites()==writesBefore);
  dropClockWrite=0;
 }
 // Every branch must still be valid at final acquisition and while using RSC.
 for(unsigned bad=0;bad<3;++bad){Harness h;offLeaves();configReady(h);heldRSC(h);trace.clear();
  if(bad==0)regs[bases[1]+0x4004]&=~1u;
  if(bad==1)h.r->fClients[0].registerStatus.uncertain=true;
  if(bad==2)h.r->fClients[0].displayStatus.uncertain=true;
  assert(!h.m->noCAdmission());noPayload();
 }
 {Harness h;offLeaves();configReady(h);untilWrite(h,9,true);externalClocks();
  regs[bases[1]+0x4004]&=~1u;clockTick(h);
  assert(clockStatus(h,h.m->fNoCClockToken).state==SweetClockFailed);fault(h);}
 // Complete DATA Off -> RSC release -> CN0 Off, then reacquire using the new
 // actual CN0 status token. The former core CN0 token was replaced long ago.
 {Harness h;offLeaves();configReady(h);heldRSC(h);h.finish();
  auto oldConfig=h.m->fNoCRegisterToken;auto dataToken=h.m->fManagedBandwidth;
  h.m->pauseIRQ();regs[bases[4]+0x288]=regs[bases[4]+0x2b4]=0;
  assert(h.m->releaseBandwidth(dataToken)==0);
  for(unsigned j=0;j<4000&&h.m->noCBusy();++j)stepRSC(h);
  assert(!h.m->noCBusy()&&!h.m->fNoCFault&&!h.m->fNoCClockToken);
  assert(nstatus(h.m,dataToken).state==SweetNoCReleased&&h.r->fClients[0].registerVote==0);
  assert(!(h.m->fClockOwnedMask&0x380)&&!(regs[bases[1]+0x4004]&0x80000000u));
  auto v=demand();v.registerPeak=300000000;uint64_t next=0;
  assert(h.m->retainBandwidth(v,&next)==0);
  for(unsigned j=0;j<4000&&h.m->noCBusy();++j)stepRSC(h);
  assert(!h.m->noCBusy()&&!h.m->fNoCFault&&nstatus(h.m,next).state==SweetNoCHeld);
  assert(h.m->fNoCRegisterToken>oldConfig&&h.m->fClockOwnedMask&0x380);
 }
 // Separate RSC leases share only our own vote. No last-user write until the
 // final reference, and no attempt to force an unrelated aggregate vote OFF.
 {Harness h;offLeaves();configReady(h);heldRSC(h);
  auto token=h.m->fNoCClockToken;h.m->fNoCPhase=NIdle;
  uint64_t extra=0;assert(h.m->retainLocalClocks(SweetClockRSC,&extra)==0);
  while(h.m->clockBusy())clockTick(h);assert(clockStatus(h,extra).state==SweetClockHeld);
  trace.clear();assert(h.m->releaseLocalClocks(extra)==0);
  while(h.m->clockBusy())clockTick(h);assert(!leafWrites()&&h.m->fClockReferences[7]==1);
  h.m->fNoCPhase=NData;h.m->fNoCTarget.registerPeak=0;h.m->fNoCCursor=h.m->fNoCCount;
  assert(h.m->releaseLocalClocks(token)==0);
  while(!(h.m->fClockCursor==7&&h.m->fClockWaiting))clockTick(h);
  assert(!(regs[bases[1]+0x4004]&1)&&!(regs[bases[1]+0x4004]&0x80000000u));
  // Enforce asynchronous settle from observed clear, even on an early callback.
  clockTick(h,0);auto readyAt=h.m->fClockVoteSettleAt;assert(readyAt>fakeTicks);
  clockTick(h,0);assert(h.m->fClockWaiting&&h.m->fClockVoteSettleAt==readyAt);
  clockTick(h,9);assert(h.m->fClockWaiting);
  clockTick(h,1);assert(!h.m->fClockWaiting);
  while(h.m->clockBusy())clockTick(h);
  assert(clockStatus(h,token).state==SweetClockReleased&&!(h.m->fClockOwnedMask&0x380));
  assert(regs[bases[1]+0x4004]==0x2000&&regs[bases[1]+0x400c]==0x80004000u&&regs[bases[1]+0x4008]==0x80008000u);
  assert(leafWrites()==3);
 }
 // Ordinary HALT branches must still actually stop. Lost disable readback,
 // reasserted own vote, raw parent drift, or stop never fabricate Released.
 for(unsigned bad=0;bad<5;++bad){Harness h;offLeaves();configReady(h);heldRSC(h);
  auto token=h.m->fNoCClockToken;h.m->fNoCTarget.registerPeak=0;h.m->fNoCCursor=h.m->fNoCCount;
  assert(h.m->releaseLocalClocks(token)==0);
  if(bad==0)dropClockWrite=bases[1]+0x4004;
  bool injected=false;
  for(unsigned j=0;j<160&&h.m->clockBusy();++j){
   clockTick(h,100,true,bad!=1);
   if(!injected&&h.m->fClockCursor==7&&h.m->fClockWaiting){
    if(bad==2)regs[bases[1]+0x22c0]^=0x80000000u;
    if(bad==3){h.m->stop(h.d);injected=true;break;}
    injected=true;
   }
   if(bad==4&&h.m->fClockCursor<7)regs[bases[1]+0x4004]|=1;
  }
  auto q=clockStatus(h,token);assert(q.state==(bad==3?SweetClockStopped:SweetClockFailed));
  if(bad!=3)assert(h.m->fClockFault&&(h.m->fClockOwnedMask&0x380)==0x380);
  auto size=trace.size();SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);assert(size==trace.size());
  dropClockWrite=0;
 }
 // A late callback after acquisition stop cannot touch released MMIO or votes.
 {Harness h;offLeaves();configReady(h);untilWrite(h,7);h.m->stop(h.d);
  auto n=trace.size();SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);
  SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);assert(n==trace.size());}
 puts("PASS: actual RSC voted/HALT acquisition + managed scanout, real core/ARC/CN0 authority, no inherited PLL/PHY permission, readback/physical completion faults, parent/upstream drift, shared refs, voted release/10us settle with foreign ON, HALT OFF, final validation, stop/late timer, preserved bits and zero leaked objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-rsc-clock-votes-') as temp:
    cpp, binary = Path(temp)/'test.cpp', Path(temp)/'test'
    cpp.write_text(ns['prelude']+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+
                   actual['header']+actual['source']+ns['helpers']+tests)
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=60,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
