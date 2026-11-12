#!/usr/bin/env python3
"""Actual MDSS display-RSC + RPMh apps-CN0 code; independent MMIO completion.
Usage: test-mh2lm-display-noc.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
clock = here/'test-mh2lm-display-clocks.py'
clockns = {'__file__': str(clock), '__name__': 'noc_clock_fixture'}
exec(compile(clock.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(clock), 'exec'), clockns)
rpmh = here/'test-mh2lm-rpmh-resources.py'
rpns = {'__file__': str(rpmh), '__name__': 'noc_rpmh_fixture'}
exec(compile(rpmh.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(rpmh), 'exec'), rpns)
ns = clockns['ns']
prelude = clockns['prelude'].replace('o==0x2004)', 'o==0x2004||o==0x4004||o==0x4008||o==0x400c)')
helpers = rpns['tests'].split('int main(){')[0]
tests = r'''
static constexpr uintptr_t DR=0x0af20000,DT=0x0af21c00,DW=0x0af30000;
static void nocFixtures(){
 arcFixtures();resourceMode=2;clockMode=1;fakeTicks=1000000;
 regs[LOC+4]=1024;regs[DB+40]=5|(256<<16);regs[DB+44]=416|(6<<16);regs[DB+48]=0x100;
 const char*names[]={"CN0","MM1","MM0","SH0","MC0","ACV"};
 const unsigned widths[]={4,32,32,16,4,0},vcds[]={0,2,1,2,0,11};
 for(unsigned r=0;r<6;++r){
  uint32_t key=0;for(unsigned k=0;names[r][k];++k)key|=uint32_t(uint8_t(names[r][k]))<<(8*k);
  uintptr_t e=DB+400+24*r;regs[e]=key;regs[e+4]=0;regs[e+16]=0x50000+4*r;regs[e+20]=8|((8*r)<<16);
  regs[DB+560+8*r]=r==5 ? 1 : 1000000;regs[DB+564+8*r]=widths[r]|(vcds[r]<<16);
 }
 regs[bases[0]+0x38]=1;regs[bases[1]+0x3000]=0x80000000;
 const unsigned branches[]={0x2080,0x200c,0x2024,0x2038,0x2028,0x202c,0x2004,0x4004,0x400c,0x4008};
 for(auto o:branches)regs[bases[1]+o]=1;
 regs[bases[1]+0x22c0]=0x401;regs[bases[1]+0x20cc]=0x401;
 regs[bases[1]+0x20fc]=regs[bases[1]+0x214c]=1;
 regs[bases[1]+0x2114]=0x101;regs[bases[1]+0x209c]=0x2101;
 regs[bases[1]+0x20a0]=1;regs[bases[1]+0x20a4]=0xfe;regs[bases[1]+0x20a8]=0xfd;
 regs[bases[4]+0x288]=regs[bases[4]+0x2b4]=regs[bases[6]+8]=0;
 regs[DR]=0x20000;regs[DR+12]=(16u<<27)|4;regs[DR+0x404]=regs[DR+0xd0]=0;
 regs[DW]=regs[DW+4]=regs[DW+0x24]=0;
 for(unsigned t=0;t<4;++t){regs[DT+t*0x2a0+0x18]=1;
  for(unsigned c=0;c<16;++c)regs[DT+t*0x2a0+0x3c+c*0x14]=(1<<8)|(1<<16);}
 regs[DT]=0x85;regs[DT+4]=0x80;
}
static void clocks(SweetMDSS*m){for(unsigned n=0;n<32&&m->clockBusy();++n){fakeTicks+=100000;SweetMDSS::clockTimerAction(m,m->fClockTimer);}assert(!m->clockBusy());}
static IOService*displayNub(){auto*n=nub(false);uint32_t one=1;n->properties["sweet,display-noc-abi"]=OSData::withBytes(&one,4);return n;}
static SweetMDSS*displayMake(IOService*n,SweetRPMh*r){
 auto*m=new SweetMDSS();assert(m->start(n));clocks(m);assert(m->fNoCEnabled&&!writes());
 for(auto op:trace)assert(!(op.addr>=DR&&op.addr<DW+0x4000)); // maps/boot lease never read RSC
 SweetRPMhClient::matched(m->fRPMhClient,nullptr,r,nullptr);pump();assert(m->fRPMh==r&&m->fRPMhConsumer);
 return m;
}
static SweetDisplayBandwidth demand(){return {{{64000000,128000000},{32000000,96000000}},{128000000,256000000},{64000000,128000000},76800000};}
static void step(SweetMDSS*m,SweetRPMh*r,bool apps=true,bool data=true){
 fakeTicks+=100000;
 if(apps&&r->fBusy&&r->fExecuting&&!r->fOperationCount){
  regs[TCS+4]|=1u<<r->fTCSIndex;regs[TCS+r->fTCSIndex*0x2a0+0x18]=1;
  for(unsigned c=0;c<r->fCommandCount;++c)regs[TCS+r->fTCSIndex*0x2a0+0x3c+c*0x14]=(1<<8)|(1<<16);
 }
 if(data&&m->fNoCPhase==NData&&m->fNoCExecuting&&!m->fNoCOps){
  regs[DT+4]|=1u<<m->fNoCTCS;regs[DT+m->fNoCTCS*0x2a0+0x18]=1;
  for(unsigned c=0;c<m->fNoCGroup;++c)
   regs[DT+m->fNoCTCS*0x2a0+0x3c+c*0x14]=(1<<8)|(c+1==m->fNoCGroup ? 1<<16 : 0);
 }
 {std::lock_guard<std::recursive_mutex>l(r->fLoop->gate);r->advance();}
 {std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);SweetMDSS::clockTimerAction(m,m->fClockTimer);SweetMDSS::noCTimerAction(m,m->fNoCTimer);}
}
static void drive(SweetMDSS*m,SweetRPMh*r){for(unsigned n=0;n<256&&m->noCBusy();++n)step(m,r);assert(!m->noCBusy()&&!m->fNoCFault);}
static uint64_t retain(SweetMDSS*m){auto v=demand();uint64_t token=0;assert(m->retainBandwidth(v,&token)==0&&token);return token;}
static SweetNoCStatus nstatus(SweetMDSS*m,uint64_t token){SweetNoCStatus s{};auto before=trace.size();assert(m->bandwidthStatus(token,&s)==0&&trace.size()==before);return s;}
static void cleanup(SweetMDSS*m,IOService*d,SweetRPMh*r,IOService*n){
 m->stop(d);auto before=trace.size();SweetMDSS::noCTimerAction(m,m->fNoCTimer);assert(trace.size()==before);
 m->release();queued.clear();dropNub(d);destroy(r,n);dropNub(n);
 assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());rpmhOwner=mdssClockWriter=mdssNoCWriter=0;
}
// Decode only emitted command writes, independently of the owner's final ledger.
static std::vector<uint32_t> acvPayloads(){
 std::map<uintptr_t,uint32_t> seen;std::vector<uint32_t> payloads;
 for(auto op:trace)if(op.write){
  if(op.addr>=TCS&&op.addr<TCS+0x540){
   unsigned off=unsigned(op.addr-TCS)%0x2a0;
   if(off>=0x34&&(off-0x34)%0x14==0)assert(op.value!=0x50014); // no apps ACV writer
  }
  if(op.addr>=DT&&op.addr<DT+0xa80){
   unsigned off=unsigned(op.addr-DT)%0x2a0;
   if(off>=0x38&&(off-0x38)%0x14==0&&seen[op.addr-4]==0x50014)payloads.push_back(op.value);
   seen[op.addr]=op.value;
  }
 }
 return payloads;
}

int main(){
 // Metadata identity must omit the NoC extension when resource1/clock0.
 // The original lifecycle matrix declared NoC and could not reach identity
 // publication. Check actual start/free with both declarations and no writes.
 for(unsigned declareNoC=0;declareNoC<2;++declareNoC){
  nocFixtures();resourceMode=1;clockMode=0;
  auto*metadata=nub(false);
  uint8_t identity[96]={};metadata->properties["sweet,panel-identity"]=OSData::withBytes(identity,sizeof(identity));
  if(declareNoC){uint32_t one=1;metadata->properties["sweet,display-noc-abi"]=OSData::withBytes(&one,4);}
  auto*service=new SweetMDSS();assert(service->start(metadata)==!declareNoC);
  assert(!service->fNoCEnabled&&!writes());
  if(!declareNoC)service->stop(metadata);
  service->release();queued.clear();dropNub(metadata);
  assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty()&&!mdssClockWriter&&!mdssNoCWriter);
 }
 // An explicit extension must never silently downgrade on malformed ABI or
 // missing maps. Failed startup frees all previously acquired clock resources.
 for(unsigned fault=0;fault<7;++fault){
  nocFixtures();auto*bad=displayNub();
  if(fault<3){bad->properties["sweet,display-noc-abi"]->release();uint32_t value=fault==2 ? 2 : 1;
   bad->properties["sweet,display-noc-abi"]=fault==0 ? (OSObject*)OSNumber::withNumber(1,32) : OSData::withBytes(&value,fault==1 ? 3 : 4);}
  if(fault==3)clockMode=0;
  if(fault>=4)missingMap=13+fault-4;
  auto*failed=new SweetMDSS();assert(!failed->start(bad));failed->release();queued.clear();
  assert(!writes()&&!mdssClockWriter&&!mdssNoCWriter);dropNub(bad);missingMap=~0u;
  assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 }
 nocFixtures();auto*n=nub();auto*r=make(n);auto*d=displayNub();auto*m=displayMake(d,r);
 // Config/metadata entry points never expose arbitrary writes or use display RSC.
 SweetBCMInfo info;assert(r->copyBCMInfo(SweetBCMMM1,&info)==0&&!writes());
 assert(r->copyBCMInfo((SweetDisplayBCM)99,&info)==kIOReturnBadArgument);
 uint64_t token=retain(m),denied=123;
 assert(m->retainBandwidth(demand(),&denied)==kIOReturnBusy&&!denied);
 assert(!m->localClocksReady());drive(m,r);auto q=nstatus(m,token);
 assert(q.state==SweetNoCHeld&&q.acknowledged==0x3e&&q.registerAcknowledged&&!q.uncertain);
 assert(m->fNoCValues[1]==(0x20000000u|(64<<14)|128));
 assert(m->fNoCValues[2]==(0x20000000u|(48<<14)|128));
 assert(m->fNoCValues[3]==(0x20000000u|(32<<14)|256));
 assert(m->fNoCValues[4]==(0x20000000u|(16<<14)|128));
 assert(m->fNoCValues[5]==0x20000008u); // ACV width=0, VCD11: X=0/Y=8
 assert(r->fClients[0].registerVote==76800000&&m->localClocksReady());
 assert(m->releaseLocalClocks(m->fNoCClockToken)==kIOReturnBusy);
 assert(regs[DT]==0x85&&regs[DT+4]==0x80);
 // Only ACTIVE2/3; each VCD ends in COMMIT+response, no ACK on other entries.
 std::vector<unsigned> groups;
 for(unsigned i=0;i<trace.size();++i)if(trace[i].write&&trace[i].addr>=DT&&trace[i].addr<DT+0x3000){
  unsigned off=unsigned(trace[i].addr-DT);if(off<0x10)continue;
  assert(off/0x2a0==2||off/0x2a0==3);
  if(off%0x2a0==0x1c&&trace[i].value)groups.push_back(trace[i].value);
 }
 assert(groups==(std::vector<unsigned>{1,1,3,1})); // VCD0 MC0,1 MM0,2 MM1+SH0,11 ACV
 // Shared average sums/max peak, and withdrawing one lease retains others.
 uint64_t second=retain(m);drive(m,r);assert(m->fNoCValues[2]==(0x20000000u|(96<<14)|128));
 assert(m->releaseBandwidth(token)==0);drive(m,r);assert(nstatus(m,token).state==SweetNoCReleased);
 assert(m->fNoCValues[2]==(0x20000000u|(48<<14)|128));
 // An active link prevents withdrawal before any write; old lease is held.
 trace.clear();regs[bases[4]+0x288]=1u<<20;
 assert(m->releaseBandwidth(second)==kIOReturnBusy&&!writes()&&nstatus(m,second).state==SweetNoCHeld);
 regs[bases[4]+0x288]=0;trace.clear();assert(m->releaseBandwidth(second)==0);drive(m,r);
 assert(nstatus(m,second).state==SweetNoCReleased&&!m->fNoCClockToken&&r->fClients[0].registerVote==0);

 unsigned lastData=0,zeroCN0=0;
 for(unsigned i=0;i<trace.size();++i){auto op=trace[i];if(!op.write)continue;
  if(op.addr>=DT&&op.addr<DT+0x3000)lastData=i;
  if(op.addr==TCS+0x38&&op.value==0x40000000)zeroCN0=i;
 }
 assert(zeroCN0>lastData);
 // Historical owned bits survive Off; re-acquiring still requires idle.
 trace.clear();regs[bases[4]+0x288]=1u<<20;denied=123;
 assert(m->retainBandwidth(demand(),&denied)==kIOReturnBusy&&!denied&&!writes());
 regs[bases[4]+0x288]=0;token=retain(m);drive(m,r);
 assert(nstatus(m,token).state==SweetNoCHeld);
 assert(m->releaseBandwidth(token)==0);drive(m,r);
 assert(r->detachConsumer(m->fRPMhConsumer)==0);m->fRPMhConsumer=0;
 cleanup(m,d,r,n);
 // Four leases, stale token after reuse, and aggregate-equivalent requests
 // never emit duplicate pixel commands. The RSC clock pin remains protected.
 nocFixtures();n=nub();r=make(n);d=displayNub();m=displayMake(d,r);
 auto zeroAverage=demand();zeroAverage.port[0].average=zeroAverage.port[1].average=zeroAverage.llcc.average=zeroAverage.ebi.average=0;
 uint64_t ids[4];
 for(unsigned i=0;i<4;++i){trace.clear();assert(m->retainBandwidth(zeroAverage,&ids[i])==0);drive(m,r);
  if(i)assert(!writes());}
 denied=123;assert(m->retainBandwidth(zeroAverage,&denied)==kIOReturnNoResources&&!denied);
 assert(m->releaseBandwidth(ids[1])==0);drive(m,r);uint64_t fresh=0;
 assert(m->retainBandwidth(zeroAverage,&fresh)==0&&fresh>ids[3]);drive(m,r);
 SweetNoCStatus stale{};assert(m->bandwidthStatus(ids[1],&stale)==kIOReturnNotFound);
 assert(m->releaseBandwidth(ids[1])==kIOReturnNotFound);cleanup(m,d,r,n);
 // Actual ACV width0/unit1/VCD11, plus a nonzero-width ACV: resource
 // identity selects mask semantics. Config floor, multiple DATA clients,
 // magnitude changes, last DATA withdrawal and re-acquire exercise the writer.
 for(unsigned acvWidth: {0u,4u}){
  nocFixtures();regs[DB+604]=acvWidth|(11u<<16);
  n=nub();r=make(n);d=displayNub();m=displayMake(d,r);
  SweetDisplayBandwidth config={};config.registerPeak=76800000;uint64_t floor=0;
  trace.clear();assert(m->retainBandwidth(config,&floor)==0);drive(m,r);
  assert(acvPayloads().empty()&&!m->fNoCValues[SweetBCMACV]);
  trace.clear();token=retain(m);drive(m,r);
  assert(acvPayloads()==std::vector<uint32_t>{0x60000008u}); // standalone VCD11 COMMIT
  auto other=demand();other.ebi.average*=2;other.ebi.peak*=2;second=0;
  trace.clear();assert(m->retainBandwidth(other,&second)==0);drive(m,r);
  assert(acvPayloads().empty()&&m->fNoCValues[SweetBCMACV]==0x20000008u);
  trace.clear();assert(m->releaseBandwidth(token)==0);drive(m,r);
  assert(acvPayloads().empty()&&m->fNoCValues[SweetBCMACV]==0x20000008u);
  trace.clear();assert(m->releaseBandwidth(second)==0);drive(m,r);
  assert(acvPayloads()==std::vector<uint32_t>{0x40000000u});
  assert(!m->fNoCValues[SweetBCMACV]&&r->fClients[0].registerVote==76800000);
  trace.clear();token=retain(m);drive(m,r);
  assert(acvPayloads()==std::vector<uint32_t>{0x60000008u});
  trace.clear();assert(m->releaseBandwidth(token)==0);drive(m,r);
  assert(acvPayloads()==std::vector<uint32_t>{0x40000000u});
  trace.clear();assert(m->releaseBandwidth(floor)==0);drive(m,r);
  assert(acvPayloads().empty());cleanup(m,d,r,n);
 }
 // RSC CBCR enable readback is not clock-on proof. No RSC access until an
 // independent CLK_OFF transition; reverse release also awaits physical OFF.
 nocFixtures();regs[bases[1]+0x4008]=0x80000000;n=nub();r=make(n);d=displayNub();m=displayMake(d,r);
 token=retain(m);unsigned turns=0;
 while(!m->fClockWaiting&&turns++<64)step(m,r);assert(m->fClockWaiting);
 for(unsigned k=0;k<3;++k)step(m,r);
 assert(m->fNoCPhase==NClock&&nstatus(m,token).state==SweetNoCPending);
 for(auto op:trace)assert(!(op.addr>=DR&&op.addr<DW+0x4000));
 regs[bases[1]+0x4008]&=~0x80000000u;drive(m,r);assert(m->fClockOwnedMask&(1u<<9));
 assert(m->releaseBandwidth(token)==0);turns=0;
 while(!(m->fNoCPhase==NClockRelease&&m->fClockWaiting)&&turns++<128)step(m,r);
 assert(m->fClockWaiting&&nstatus(m,token).state==SweetNoCReleasing);
 assert(r->fClients[0].registerVote==76800000); // config bus retained through clock OFF
 regs[bases[1]+0x4008]|=0x80000000u;drive(m,r);
 assert(!m->fNoCClockToken&&!(m->fClockOwnedMask&(1u<<9))&&r->fClients[0].registerVote==0);cleanup(m,d,r,n);
 // CN0's provider ledger aggregates independent clients and protects an
 // inherited request that appears after admission, before its payload write.
 nocFixtures();n=nub();r=make(n);uint64_t a=attach(r),b=attach(r),rt=0;
 assert(r->requestRegisterBandwidth(a,76800000,&rt)==0);tick(r);ack(r);
 assert(r->requestRegisterBandwidth(b,150000000,&rt)==0);tick(r);ack(r);
 assert(r->requestRegisterBandwidth(a,0,&rt)==0&&!r->fBusy&&r->fRegisterAggregate==150000000);
 assert(r->detachConsumer(a)==0&&r->detachConsumer(b)==kIOReturnBusy);
 assert(r->requestRegisterBandwidth(b,0,&rt)==0);tick(r);ack(r);assert(r->detachConsumer(b)==0);
 destroy(r,n);dropNub(n);reboot();
 nocFixtures();n=nub();r=make(n);a=attach(r);assert(r->requestRegisterBandwidth(a,76800000,&rt)==0);
 regs[TCS+0x2a0+0x1c]=1;regs[TCS+0x2a0+0x34]=0x50000;tick(r);
 SweetRegisterBandwidthStatus rs{};assert(r->copyRegisterBandwidthStatus(a,rt,&rs)==0&&rs.state==SweetRPMhFailed&&rs.uncertain);
 for(auto op:trace)if(op.write)assert(op.addr==TCS);
 destroy(r,n);dropNub(n);reboot();
 // Inputs, missing metadata, overflow, and live inherited pipeline: no writes.
 for(unsigned fault=0;fault<7;++fault){nocFixtures();n=nub();r=make(n);d=displayNub();m=displayMake(d,r);auto v=demand();
  if(fault==0)v.registerPeak=0;if(fault==1)v.port[0].average=v.port[0].peak+1;
  if(fault==2)v.port[0].peak=~0ULL;if(fault==3)regs[DB+560+8]=0;
  if(fault==4)regs[DB+400+24+16]=0x50000; // duplicate BCM address
  if(fault==5)regs[bases[4]+0x288]=1u<<20;if(fault==6)regs[bases[1]+0x3000]=0;
  trace.clear();denied=123;assert(m->retainBandwidth(v,&denied)!=0&&!denied&&!writes());cleanup(m,d,r,n);}
 // Metadata malformed per-entry, including VCD/type/aux bounds and reserved.
 for(unsigned fault=0;fault<6;++fault){nocFixtures();
  if(fault==0)regs[DB+564+8]=32|(16<<16);
  if(fault==1)regs[DB+400+24+16]=0x40000;
  if(fault==2)regs[DB+400+24+20]=7;
  if(fault==3)regs[DB+400+24+20]=8|(0xffffu<<16);
  if(fault==4)regs[DB+564+8]=32|0x1000000;
  if(fault==5)regs[DB+564+8]=0;
  n=nub();r=make(n);assert(r->copyBCMInfo(SweetBCMMM1,&info)==kIOReturnNotFound&&!writes());destroy(r,n);dropNub(n);reboot();}
 // No RSC accesses when its clocks failed; no solver/cold-domain fallback.
 for(unsigned fault=0;fault<8;++fault){nocFixtures();n=nub();r=make(n);d=displayNub();m=displayMake(d,r);
  if(fault==0)regs[bases[1]+0x400c]=0x80000000; // non-XO parent cannot be enabled
  if(fault==1)regs[DW+4]=1;if(fault==2)regs[DW+0x24]=1u<<12;
  if(fault==3)regs[DR+0x404]=1;if(fault==4)regs[DR]=0x30000;
  if(fault==5)regs[DR+12]=(2u<<27)|4; // VCD group fits2, this is valid
  if(fault==6)regs[DT+2*0x2a0+0x1c]=1,regs[DT+2*0x2a0+0x34]=0x50004;
  if(fault==7)regs[DR+0xd0]=1;
  token=retain(m);
  for(unsigned k=0;k<256&&m->noCBusy();++k)step(m,r);
  if(fault==5)assert(nstatus(m,token).state==SweetNoCHeld);
  else{assert(nstatus(m,token).state==SweetNoCFailed&&m->fNoCFault);
   for(auto op:trace)if(op.write)assert(!(op.addr>=DT&&op.addr<DT+0x3000));
   if(fault==0)for(auto op:trace)assert(!(op.addr>=DR&&op.addr<DW+0x4000));
  }
  cleanup(m,d,r,n);
 }
 // A group exceeding NCPT is never split: preserve admitted CN0/clock ledger.
 nocFixtures();for(unsigned b=1;b<6;++b)regs[DB+564+8*b]&=0xffff;
 regs[DR+12]=(4u<<27)|4;n=nub();r=make(n);d=displayNub();m=displayMake(d,r);token=retain(m);
 for(unsigned k=0;k<256&&m->noCBusy();++k)step(m,r);
 q=nstatus(m,token);assert(q.state==SweetNoCFailed&&q.result==kIOReturnNoResources&&!q.acknowledged);
 cleanup(m,d,r,n);
 // Fresh completion per VCD, no completion from old command status alone.
 nocFixtures();n=nub();r=make(n);d=displayNub();m=displayMake(d,r);token=retain(m);
 while(!(m->fNoCPhase==NData&&m->fNoCExecuting))step(m,r,true,false);
 for(unsigned k=0;k<4;++k)step(m,r,true,false);
 assert(nstatus(m,token).state==SweetNoCPending&&!nstatus(m,token).acknowledged);
 drive(m,r);assert(nstatus(m,token).state==SweetNoCHeld);cleanup(m,d,r,n);
 // Mid-group timeout, posted-write failure, uncleared IRQ, stop, loss of clocks.
 for(unsigned fault=0;fault<5;++fault){nocFixtures();n=nub();r=make(n);d=displayNub();m=displayMake(d,r);token=retain(m);
  while(!(m->fNoCPhase==NData&&m->fNoCExecuting))step(m,r,true,false);
  if(fault==0)fakeTicks+=100001000;
  if(fault==1){regs[DT+4]|=1u<<m->fNoCTCS;step(m,r,true,false);rpmhDrop=m->fNoCTCS*0x2a0+0x14;regs[DT+rpmhDrop]=0x1010000;}
  if(fault==2)rpmhKeepIRQ=true;
  if(fault==3)m->stop(d);
  if(fault==4)regs[bases[1]+0x4008]=0x80000001;
  for(unsigned k=0;k<32&&m->noCBusy();++k)step(m,r);
  q=nstatus(m,token);assert(q.state==SweetNoCFailed&&q.uncertain&&m->fNoCFault);
  auto count=trace.size();SweetMDSS::noCTimerAction(m,m->fNoCTimer);assert(count==trace.size());
  cleanup(m,d,r,n);
 }
 puts("PASS: actual NoC and CN0: separate owners, warm-clock admission, typed aggregate/scaling + ACV mask8 width0/VCD11 on/off/reacquire, per-VCD COMMIT/fresh ACK, ACTIVE2/3 preservation, reverse release, explicit faults/stop and zero objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-noc-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+ns['DSI_MEMORY_FIXTURE']+ns['pool_fixture']+ns['header']+ns['source']+helpers+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
