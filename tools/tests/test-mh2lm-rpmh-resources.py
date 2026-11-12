#!/usr/bin/env python3
"""Actual RPMh/MDSS resource code, independent MMIO completion/lifecycle inputs.
Usage: test-mh2lm-rpmh-resources.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
existing = Path(__file__).with_name('test-mh2lm-display-service.py')
ns = {'__file__': str(existing), '__name__': 'rpmh_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)
tests = r'''
static constexpr uintptr_t RSC=0x18220000,TCS=0x18220d00,DB=0x90000000,LOC=0x0c3f000c;
static unsigned writes(){unsigned n=0;for(auto&o:trace)n+=o.write;return n;}
static void fixtures(){
 resetRegs();rpmhDrop=~0u;rpmhKeepIRQ=false;badMap=missingMap=shortMap=99;
 regs[RSC]=0x20000;regs[RSC+4]=0;regs[RSC+12]=(3u<<27)|(8<<12);
 regs[LOC]=DB;regs[LOC+4]=256;regs[DB]=1;regs[DB+4]=0x0c0330db;
 regs[DB+8]=4;regs[DB+12]=96|(4<<16);
 const char*names[]={"ldoa14","ldoa17","ldoc3","ldoa5"};
 for(unsigned i=0;i<4;++i){uint32_t key[2]={};for(unsigned k=0;names[i][k];++k)key[k/4]|=uint32_t(uint8_t(names[i][k]))<<(8*(k%4));
  uintptr_t e=DB+144+i*24;regs[e]=key[0];regs[e+4]=key[1];regs[e+16]=0x40000+0x100*i;}
 for(unsigned t=0;t<2;++t){regs[TCS+t*0x2a0+0x18]=1;
  for(unsigned c=0;c<3;++c)regs[TCS+t*0x2a0+0x3c+c*0x14]=(1<<8)|(1<<16);}
 regs[TCS]=0x41;
}
static IOService*nub(bool rpmh=true){auto*n=new IOService;n->rpmhNub=rpmh;
 uint32_t abi=1;auto*d=OSData::withBytes(&abi,4);n->properties[rpmh?"sweet,rpmh-abi":"sweet,display-resource-abi"]=d;return n;}
static void dropNub(IOService*n){for(auto&p:n->properties)p.second->release();n->release();}
static void tick(SweetRPMh*s,unsigned us=100){fakeTicks+=us*1000;assert(s->fTimerAdded);s->fTimer->action(s,s->fTimer);}
static void ack(SweetRPMh*s){
 uint32_t b=s->fTCSIndex*0x2a0;regs[TCS+4]|=1u<<s->fTCSIndex;regs[TCS+b+0x18]=1;
 for(unsigned c=0;c<s->fCommandCount;++c)regs[TCS+b+0x3c+c*0x14]=(1<<8)|(1<<16);
 tick(s);assert(s->fBusy);tick(s);assert(!s->fBusy);
}
static SweetRPMh*make(IOService*n){auto*s=new SweetRPMh();assert(s->start(n));assert(!writes());return s;}
static void destroy(SweetRPMh*s,IOService*n){s->stop(n);s->release();}
static uint64_t attach(SweetRPMh*s){uint64_t id=0;assert(s->attachConsumer(&id)==0&&id);return id;}
static SweetRPMhStatus status(SweetRPMh*s,uint64_t id,uint64_t token){SweetRPMhStatus q={};assert(s->copyStatus(id,token,&q)==0);return q;}
static uint64_t vote(SweetRPMh*s,uint64_t id,SweetRail rail,SweetRailVote v){uint64_t token=0;assert(s->requestVote(id,rail,v,&token)==0&&token);return token;}
static void reboot(){assert(!liveMaps);rpmhOwner=0;}
// ARC command DB is independent of production lookup/selection. Every table
// has distinct selectors; this catches accidentally sending raw level/mV data.
static void arcFixtures(){
 fixtures();regs[LOC+4]=512;
 regs[DB+24]=3|(96<<16);regs[DB+28]=168|(3<<16);regs[DB+32]=0x100;
 const char*names[]={"cx.lvl","mx.lvl","mmcx.lvl"};
 const unsigned levels[3][8]={{0,16,48,64,128,256,384,0},
                            {0,16,48,64,128,256,384,0},
                            {0,64,128,192,256,384,0,0}};
 for(unsigned r=0;r<3;++r){
  uint32_t key[2]={};for(unsigned k=0;names[r][k];++k)key[k/4]|=uint32_t(uint8_t(names[r][k]))<<(8*(k%4));
  uintptr_t e=DB+240+r*24;regs[e]=key[0];regs[e+4]=key[1];regs[e+16]=0x30000+4*r;
  regs[e+20]=16|((16*r)<<16);
  for(unsigned k=0;k<8;k+=2)regs[DB+312+16*r+2*k]=levels[r][k]|(levels[r][k+1]<<16);
 }
}
static uint64_t displayVote(SweetRPMh*s,uint64_t id,SweetDisplayCorner corner){
 uint64_t token=0;assert(s->requestDisplayPower(id,corner,&token)==0&&token);return token;
}
static SweetDisplayPowerStatus displayStatus(SweetRPMh*s,uint64_t id,uint64_t token){
 SweetDisplayPowerStatus q={};assert(s->copyDisplayPowerStatus(id,token,&q)==0);return q;
}
static void arcStage(SweetRPMh*s,unsigned supply,unsigned selector){
 assert(s->fBusy&&s->fArcRequest);tick(s);
 uint32_t b=s->fTCSIndex*0x2a0;
 assert(s->fExecuting&&s->fCommandCount==1);
 assert(regs[TCS+b+0x34]==0x30000+4*supply&&regs[TCS+b+0x38]==selector);
 // Old issued/complete bits alone cannot advance this or a subsequent stage.
 tick(s);assert(s->fBusy&&s->fArcStages[s->fArcStage].supply==supply);
 regs[TCS+4]|=1u<<s->fTCSIndex;regs[TCS+b+0x18]=1;
 regs[TCS+b+0x3c]=(1<<8)|(1<<16);tick(s);tick(s);
}
static void arcAcquire(SweetRPMh*s){
 arcStage(s,0,6);arcStage(s,1,6);arcStage(s,2,1);arcStage(s,1,3);arcStage(s,0,2);
 assert(!s->fBusy);
}
int main(){
 auto*n=nub();resourceMode=0;auto*s=new SweetRPMh();assert(!s->start(n)&&!writes());s->release();
 // Read-only metadata mode: no hardware writes, no automatic enable/disable.
 fixtures();resourceMode=1;s=make(n);uint64_t id=attach(s),token=0;
 SweetRailInfo info={};assert(s->resourceInfo(SweetRailVPNL,&info)==0&&info.available&&info.minMV==2856);
 assert(!info.lastDisableRequiresHandoff);
 assert(s->resourceInfo(SweetRailVDDI,&info)==0&&!info.lastDisableRequiresHandoff);
 for(auto rail:{SweetRailHost,SweetRailPHY}){
  assert(s->resourceInfo(rail,&info)==0&&info.available&&info.lastDisableRequiresHandoff);
 }
 assert(s->requestVote(id,SweetRailVDDI,{true,1800,1880,62000},&token)==kIOReturnNotPermitted&&token==0);
 assert(s->detachConsumer(id)==0);assert(!writes());destroy(s,n);assert(rpmhOwner==0);
 // All attach slots bounded, no zero/stale handle aliasing.
 fixtures();s=make(n);uint64_t ids[8];for(auto &i:ids)i=attach(s);
 assert(s->attachConsumer(&token)==kIOReturnNoResources&&token==0);
 assert(s->detachConsumer(0)==kIOReturnNotFound);
 for(auto i:ids)assert(s->detachConsumer(i)==0);
 id=attach(s);assert(id>ids[7]&&s->detachConsumer(ids[0])==kIOReturnNotFound);
 assert(s->detachConsumer(id)==0);destroy(s,n);
 // Independent completion: stale command-status alone must not finish.
 fixtures();resourceMode=2;s=make(n);id=attach(s);uint64_t other=attach(s);
 token=vote(s,id,SweetRailVDDI,{true,1800,1880,6000});tick(s);
 assert(s->fBusy&&regs[TCS]==0x40);tick(s);assert(s->fBusy);
 assert(status(s,id,token).state==SweetRPMhPending);
 uint64_t denied=123;assert(s->requestVote(other,SweetRailPHY,{true,880,880,36000},&denied)==kIOReturnBusy&&denied==0);
 // An unrelated IRQ-enable bit changes during completion; preserve it.
 regs[TCS]|=0x80;ack(s);auto q=status(s,id,token);assert(q.state==SweetRPMhComplete&&q.mode==4&&!q.uncertain);
 assert(regs[TCS]==0xc1&&!(regs[TCS+4]&1));
 assert(s->detachConsumer(id)==kIOReturnBusy);
 uint64_t first=token;token=vote(s,other,SweetRailVDDI,{true,1800,1880,6000});tick(s);ack(s);
 assert(status(s,other,token).mode==7);
 token=vote(s,id,SweetRailVDDI,{false,0,0,0});tick(s);ack(s);
 assert(status(s,id,token).enabled&&status(s,id,token).mode==4);
 assert(s->copyStatus(id,first,&q)==kIOReturnNotFound);
 assert(s->detachConsumer(id)==0);
 token=vote(s,other,SweetRailVDDI,{false,0,0,0});tick(s);ack(s);
 assert(!status(s,other,token).enabled&&regs[TCS+0x34]==0x40004&&regs[TCS+0x38]==0);
 assert(s->detachConsumer(other)==0);destroy(s,n);assert(rpmhOwner==2);
 s=new SweetRPMh();assert(!s->start(n));s->release();reboot();
 // Every known shared rail vetoes a last aggregate disable; all client
 // slots participate. A refusal must preserve the previously completed status and ledger.
 // Non-last removal and unowned no-op remain usable; VPNL's original disable
 // path still requires an independent fresh ACK and is not silently pinned.
 for(unsigned rail=0;rail<SweetRailCount;++rail){
  fixtures();s=make(n);uint64_t clients[8];for(auto&i:clients)i=attach(s);
  assert(s->resourceInfo((SweetRail)rail,&info)==0&&info.available);
  SweetRailVote enabled={true,info.minMV,info.maxMV,62000};
  token=vote(s,clients[7],(SweetRail)rail,{false,0,0,0});
  assert(!writes()&&!s->fOwned[rail]);
  for(unsigned client=0;client<8;++client){
   token=vote(s,clients[client],(SweetRail)rail,enabled);
   if(s->fBusy){tick(s);ack(s);}
  }
  for(unsigned client=0;client<7;++client){
   token=vote(s,clients[client],(SweetRail)rail,{false,0,0,0});
   if(s->fBusy){tick(s);ack(s);}
   assert(status(s,clients[client],token).enabled&&!s->fClients[client].votes[rail].enabled);
   assert(s->detachConsumer(clients[client])==0);
  }
  auto oldStatus=s->fClients[7].status;auto oldAggregate=s->fAggregate[rail];
  auto serial=s->fNextToken;auto traceBefore=trace.size();
  if(info.lastDisableRequiresHandoff){
   for(unsigned retry=0;retry<3;++retry){denied=123;
    assert(s->requestVote(clients[7],(SweetRail)rail,{false,0,0,0},&denied)==kIOReturnNotPermitted&&!denied);
    assert(s->fNextToken==serial&&trace.size()==traceBefore&&!s->fBusy);
    auto current=status(s,clients[7],oldStatus.token);
    assert(current.token==oldStatus.token&&current.result==oldStatus.result&&current.state==oldStatus.state&&current.enabled&&!current.uncertain);
    assert(s->fClients[7].votes[rail].enabled&&s->fAggregate[rail].enabled&&s->fAggregate[rail].loadUA==oldAggregate.loadUA);
   }
   assert(s->detachConsumer(clients[7])==kIOReturnBusy);
  }else{
   token=vote(s,clients[7],(SweetRail)rail,{false,0,0,0});tick(s);
   assert(s->fBusy&&status(s,clients[7],token).state==SweetRPMhPending);
   assert(regs[TCS+s->fTCSIndex*0x2a0+0x34]==0x40004+rail*0x100&&regs[TCS+s->fTCSIndex*0x2a0+0x38]==0);
   ack(s);assert(!status(s,clients[7],token).enabled&&s->detachConsumer(clients[7])==0);
  }
  destroy(s,n);reboot();
 }
 // Competing clients run through the same provider gate. Exactly one request
 // is admitted; the other gets Busy rather than overwriting the active slot.
 fixtures();s=make(n);id=attach(s);other=attach(s);
 IOReturn rc1=-1,rc2=-1;uint64_t tok1=0,tok2=0;
 std::thread a([&]{rc1=s->requestVote(id,SweetRailVDDI,{true,1800,1880,62000},&tok1);});
 std::thread b([&]{rc2=s->requestVote(other,SweetRailPHY,{true,880,880,36000},&tok2);});
 a.join();b.join();assert((rc1==0&&rc2==kIOReturnBusy)||(rc2==0&&rc1==kIOReturnBusy));
 tick(s);ack(s);destroy(s,n);reboot();
 // A real workloop-gated MMIO action and stop synchronize before unmapping.
 fixtures();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});
 pauseRead=true;pauseAddress=TCS;enteredRead=false;releaseRead=false;
 std::thread worker([&]{std::lock_guard<std::recursive_mutex>l(s->fLoop->gate);tick(s);});
 {std::unique_lock<std::mutex>l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 std::atomic<bool>stopEntered{false},stopDone{false};
 std::thread stopper([&]{stopEntered=true;s->stop(n);stopDone=true;});
 while(!stopEntered)std::this_thread::yield();assert(!stopDone);
 {std::lock_guard<std::mutex>l(pauseLock);releaseRead=true;pauseCV.notify_all();}
 worker.join();stopper.join();pauseRead=false;pauseAddress=0;
 auto postStop=trace.size();s->fTimer->action(s,s->fTimer);assert(trace.size()==postStop);
 s->release();reboot();
 // Voltage aggregation/conflicts and load overflow rejected before writes.
 fixtures();s=make(n);id=attach(s);other=attach(s);
 token=vote(s,id,SweetRailVPNL,{true,3000,3008,857000});tick(s);ack(s);trace.clear();
 assert(s->requestVote(other,SweetRailVPNL,{true,2856,2900,1},&denied)==kIOReturnBadArgument&&!writes());
 assert(s->requestVote(other,SweetRailVPNL,{true,3001,3007,1},&denied)==kIOReturnBadArgument&&!writes());
 assert(s->requestVote(other,SweetRailVPNL,{true,3000,3008,~0u},&denied)==kIOReturnBadArgument&&!writes());
 destroy(s,n);reboot();
 // Repeated/no-op requests and an empty ledger never disable inherited rails.
 fixtures();s=make(n);id=attach(s);token=vote(s,id,SweetRailPHY,{false,0,0,0});assert(!writes()&&!s->fOwned[SweetRailPHY]);
 token=vote(s,id,SweetRailPHY,{true,880,880,36000});tick(s);ack(s);trace.clear();
 token=vote(s,id,SweetRailPHY,{true,880,880,36000});assert(!writes()&&status(s,id,token).state==SweetRPMhComplete);
 destroy(s,n);reboot();
 // Inherited requests are never overwritten, including idle-but-armed TCS.
 for(unsigned test=0;test<3;++test){fixtures();
  if(test==0){regs[TCS+0x1c]=1;regs[TCS+0x34]=0x40000;}
  if(test==1){regs[TCS+0x1c]=1;regs[TCS+0x2a0+0x1c]=1;}
  if(test==2)regs[TCS+0x1c]=1u<<20;
  s=make(n);id=attach(s);assert(s->requestVote(id,SweetRailVDDI,{true,1800,1880,1},&denied)==(test==2?kIOReturnIOError:kIOReturnBusy)&&!writes());
  destroy(s,n);reboot();}
 // Per-register posted-write wait is timer-driven. Completion timeout, failed
 // programming, uncleared IRQ, stop before/after first write and late event.
 for(unsigned test=0;test<8;++test){fixtures();s=make(n);id=attach(s);messages.clear();
  token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});
  if(test==0){tick(s);tick(s,100001);}
  if(test==1){rpmhDrop=0x30;tick(s);assert(s->fBusy);tick(s,1001);}
  if(test==2){tick(s);rpmhKeepIRQ=true;ack(s);}
  if(test==3){s->stop(n);}
  if(test==4){tick(s);s->stop(n);}
  if(test==5){tick(s);regs[TCS+4]=~0u;tick(s);}
  // A late first callback must say program (not falsely report a PMIC ACK
  // timeout); a late cleanup callback must retain the confirmed payload.
  if(test==6){tick(s,100001);assert(!writes());}
  if(test==7){tick(s);regs[TCS+4]=1;tick(s);auto before=writes();tick(s,100001);assert(writes()==before);}
  if(test==2){assert(s->fBusy);tick(s,1001);}
  assert(!s->fBusy);q=s->fClients[0].status;
  assert(q.state==SweetRPMhFailed&&q.uncertain==(test!=3));
  unsigned snapshots=0;std::string diagnostic;
  for(auto &m:messages)if(m.find("SWEET RPMH SNAPSHOT:")==0){++snapshots;diagnostic+=m;}
  if(test==3||test==4)assert(!snapshots); // stop/cancel must not inspect hardware
  else{
   assert(snapshots>=5&&snapshots<=6);
   const char*phase=(test==1||test==6)?"phase=program":((test==2||test==7)?"phase=cleanup":"phase=completion");
   assert(diagnostic.find(phase)!=std::string::npos);
   const char*reason=test==5?"reason=invalid-completion":((test==1||test==2)?"reason=posted-write":"reason=deadline");
   assert(diagnostic.find(reason)!=std::string::npos);
   assert(diagnostic.find("request=VRM rail=0 arc-stage=0 tcs=0 commands=3 mask=0x7")!=std::string::npos);
   if(test==6)assert(diagnostic.find("executing=0 progress=0 written=0")!=std::string::npos);
   for(auto &op:trace)assert(op.write||op.addr!=TCS+8); // no W1C register reads
  }
  if(test<3||test>=5)assert(s->requestVote(id,SweetRailPHY,{true,880,880,1},&denied)==kIOReturnNotReady);
  auto count=trace.size();s->fTimer->action(s,s->fTimer);assert(trace.size()==count);
  destroy(s,n);assert(rpmhOwner==((test==3||test==6)?0u:2u));reboot();}
 // Malformed ABI/DB/revision/map failures never write and release all maps.
 for(unsigned test=0;test<11;++test){fixtures();
  if(test==0)regs[RSC]=0x30000;
  if(test==1)regs[RSC+12]=0;
  if(test==2)regs[LOC]=0x1000;
  if(test==3)regs[LOC+4]=1480*1024;
  if(test==4)regs[DB+4]=0;
  if(test==5)regs[DB+12]=0xffff0001;
  if(test==6)regs[DB+144+16]=0x30000;
  if(test==7)regs[DB+144+24+16]=0x40000;
  if(test==8)badMap=1;
  if(test==9)missingMap=2;
  if(test==10)shortMap=0;
  s=new SweetRPMh();assert(!s->start(n)&&!writes());s->release();assert(!liveMaps&&rpmhOwner==0);
 }
 fixtures();
 // Real registry client + actual MDSS resource-mode attachment/termination.
 s=make(n);auto*d=nub(false);auto*m=new SweetMDSS();assert(m->start(d));
 auto before=writes();assert(SweetRPMhClient::matched(m->fRPMhClient,nullptr,s,nullptr));assert(!m->fRPMh);
 pump();assert(m->fRPMh==s&&m->fRPMhConsumer&&writes()==before);
 assert(SweetRPMhClient::terminated(m->fRPMhClient,nullptr,s,nullptr));pump();assert(!m->fRPMh);
 m->stop(d);m->release();dropNub(d);destroy(s,n);
 // Wrong/absent display ABI refuses new mode without reading PHY/GDSC.
 for(unsigned test=0;test<2;++test){fixtures();d=nub(false);
  if(test==0){d->properties.begin()->second->release();d->properties.clear();}
  else badMap=12;
  m=new SweetMDSS();assert(!m->start(d));m->release();dropNub(d);assert(!liveMaps&&!writes());}
 // Optional ARC metadata never breaks the four-rail binding or grants writes.
 fixtures();s=make(n);id=attach(s);SweetDisplayPowerInfo pi={};
 assert(s->displayPowerInfo(&pi)==0&&!pi.available&&!writes());
 assert(s->requestDisplayPower(id,SweetDisplayLowSVS,&token)==kIOReturnNotReady&&!token&&!writes());
 assert(s->detachConsumer(id)==0);destroy(s,n);reboot();
 arcFixtures();resourceMode=1;s=make(n);id=attach(s);
 assert(s->displayPowerInfo(&pi)==0&&pi.available&&pi.supported[SweetDisplayTurbo]&&!writes());
 assert(s->requestDisplayPower(id,SweetDisplayLowSVS,&token)==kIOReturnNotPermitted&&!token&&!writes());
 destroy(s,n);reboot();resourceMode=2;
 // Owned dependency sequence, per-stage fresh ACK and aggregate/no-op rules.
 arcFixtures();s=make(n);id=attach(s);other=attach(s);
 token=displayVote(s,id,SweetDisplayOff);assert(!writes()&&!s->fBusy&&!s->fArcOwned[0]);
 token=displayVote(s,id,SweetDisplayLowSVS);first=token;
 assert(s->detachConsumer(id)==kIOReturnBusy);
 assert(s->requestVote(other,SweetRailPHY,{true,880,880,36000},&denied)==kIOReturnBusy&&!denied);
 arcStage(s,0,6);assert(s->fBusy&&s->fClients[0].displayVote==SweetDisplayOff);
 assert(displayStatus(s,id,token).acknowledged==1);
 arcStage(s,1,6);assert(s->fBusy&&s->fClients[0].displayVote==SweetDisplayOff);
 arcStage(s,2,1);assert(s->fBusy&&s->fClients[0].displayVote==SweetDisplayOff);
 assert(displayStatus(s,id,token).appliedLevels[0]==384&&displayStatus(s,id,token).appliedLevels[1]==384);
 arcStage(s,1,3);arcStage(s,0,2);assert(!s->fBusy&&s->fClients[0].displayVote==SweetDisplayLowSVS);
 auto pq=displayStatus(s,id,token);assert(pq.state==SweetRPMhComplete&&pq.acknowledged==7&&!pq.uncertain);
 assert(pq.known==7&&pq.appliedLevels[0]==48&&pq.appliedLevels[1]==64&&pq.appliedLevels[2]==64);
 assert(pq.levels[0]==48&&pq.levels[1]==64&&pq.levels[2]==64&&regs[TCS]==0x41);
 trace.clear();token=displayVote(s,other,SweetDisplayLowSVS);assert(!writes()&&!s->fBusy);
 token=displayVote(s,other,SweetDisplayNominal);
 arcStage(s,1,5);assert(s->fBusy);arcStage(s,2,4);assert(!s->fBusy);
 trace.clear();token=displayVote(s,id,SweetDisplayOff);assert(!writes()&&displayStatus(s,id,token).enabled);
 assert(s->copyDisplayPowerStatus(id,first,&pq)==kIOReturnNotFound);
 assert(s->detachConsumer(id)==0);
 token=displayVote(s,other,SweetDisplaySVS);arcStage(s,2,2);arcStage(s,1,4);
 assert(!s->fBusy&&displayStatus(s,other,token).acknowledged==6);
 token=displayVote(s,other,SweetDisplayOff);arcStage(s,2,0);arcStage(s,1,0);arcStage(s,0,0);
 assert(!s->fBusy&&!displayStatus(s,other,token).enabled&&s->detachConsumer(other)==0);
 // The same engine remains usable for VRM after ARC and vice versa.
 id=attach(s);token=vote(s,id,SweetRailPHY,{true,880,880,36000});tick(s);ack(s);
 token=displayVote(s,id,SweetDisplayLowSVS);arcStage(s,0,2);arcStage(s,1,3);arcStage(s,2,1);
 destroy(s,n);assert(rpmhOwner==2);reboot();
 // Select parents from the actual rounded MMCX DB level, not caller's floor.
 arcFixtures();regs[DB+344]=80u<<16; // mmcx index1 becomes80, index2 remains128.
 s=make(n);id=attach(s);token=displayVote(s,id,SweetDisplayLowSVS);
 arcStage(s,0,6);arcStage(s,1,6);arcStage(s,2,1);arcStage(s,1,4);arcStage(s,0,2);
 pq=displayStatus(s,id,token);assert(pq.levels[1]==128&&pq.levels[2]==80);
 destroy(s,n);reboot();
 // Unsupported corner has no token/ledger/MMIO writes; lower corners work.
 arcFixtures();regs[DB+352]=256|(320u<<16);s=make(n);id=attach(s);
 assert(s->displayPowerInfo(&pi)==0&&pi.available&&!pi.supported[SweetDisplayTurbo]&&pi.supported[SweetDisplayNominal]);
 assert(s->requestDisplayPower(id,SweetDisplayTurbo,&denied)==kIOReturnBadArgument&&!denied&&!writes());
 denied=123;assert(s->requestDisplayPower(id,(SweetDisplayCorner)99,&denied)==kIOReturnBadArgument&&!denied&&!writes());
 assert(s->requestDisplayPower(id,SweetDisplayLowSVS,nullptr)==kIOReturnBadArgument&&!writes());
 assert(s->displayPowerInfo(nullptr)==kIOReturnBadArgument);
 assert(s->copyDisplayPowerStatus(id,0,nullptr)==kIOReturnBadArgument);
 assert(s->requestDisplayPower(999,SweetDisplayLowSVS,&denied)==kIOReturnNotFound&&!denied);
 assert(s->copyDisplayPowerStatus(id,0,&pq)==kIOReturnNotFound);
 destroy(s,n);reboot();
 // Match every rail in the entire chain against inherited active requests.
 for(unsigned r=0;r<3;++r){arcFixtures();regs[TCS+0x2a0+0x1c]=1;regs[TCS+0x2a0+0x34]=0x30000+4*r;
  s=make(n);id=attach(s);assert(s->requestDisplayPower(id,SweetDisplayLowSVS,&token)==kIOReturnBusy&&!token&&!writes());
  destroy(s,n);reboot();}
 // Recheck the whole chain when the timer resumes, before payload writes.
 arcFixtures();s=make(n);id=attach(s);token=displayVote(s,id,SweetDisplayLowSVS);
 regs[TCS+0x2a0+0x1c]=1;regs[TCS+0x2a0+0x34]=0x30008;tick(s);
 pq=displayStatus(s,id,token);assert(pq.state==SweetRPMhFailed&&pq.result==kIOReturnBusy&&pq.uncertain&&!pq.acknowledged);
 for(auto&o:trace)if(o.write)assert(o.addr==TCS);
 destroy(s,n);reboot();
 // Use second ACTIVE slot without stealing unrelated armed payload/IRQ bits.
 arcFixtures();regs[TCS+0x1c]=1;regs[TCS+0x34]=0x51234;regs[TCS+4]=0x40;
 s=make(n);id=attach(s);token=displayVote(s,id,SweetDisplayLowSVS);
 arcAcquire(s);
 assert(regs[TCS+0x1c]==1&&regs[TCS+0x34]==0x51234&&regs[TCS+4]==0x40&&regs[TCS]==0x41);
 destroy(s,n);reboot();
 // Independent clients and VRM/ARC requests cannot interleave transactions.
 arcFixtures();s=make(n);id=attach(s);other=attach(s);tok1=tok2=0;
 std::thread c([&]{rc1=s->requestDisplayPower(id,SweetDisplayLowSVS,&tok1);});
 std::thread e([&]{rc2=s->requestVote(other,SweetRailPHY,{true,880,880,36000},&tok2);});
 c.join();e.join();assert((rc1==0&&rc2==kIOReturnBusy)||(rc2==0&&rc1==kIOReturnBusy));
 if(rc1==0){arcAcquire(s);}else{tick(s);ack(s);}
 destroy(s,n);reboot();
 // A failure at any stage retains confirmed parents and stops all later writes.
 for(unsigned stage=0;stage<5;++stage){arcFixtures();s=make(n);id=attach(s);
  token=displayVote(s,id,SweetDisplayLowSVS);
  if(stage>0)arcStage(s,0,6);if(stage>1)arcStage(s,1,6);
  if(stage>2)arcStage(s,2,1);if(stage>3)arcStage(s,1,3);
  tick(s);tick(s,100001);pq=displayStatus(s,id,token);
  const unsigned masks[]={0,1,3,7,7};
  assert(pq.state==SweetRPMhFailed&&pq.uncertain&&pq.acknowledged==masks[stage]&&pq.known==masks[stage]);
  assert(s->fPoisoned&&s->fClients[0].displayVote==SweetDisplayOff);
  for(unsigned r=0;r<3;++r)if(masks[stage]&(1u<<r))assert(s->fArcOwned[r]&&s->fArcApplied[r]!=0);
  if(stage>2)assert(pq.appliedLevels[0]==384&&pq.appliedLevels[1]==(stage==4?64u:384u)&&pq.appliedLevels[2]==64);
  assert(s->detachConsumer(id)==kIOReturnBusy);
  trace.clear();assert(s->requestDisplayPower(id,SweetDisplayOff,&denied)==kIOReturnNotReady&&!writes());
  assert(s->requestVote(id,SweetRailPHY,{false,0,0,0},&denied)==kIOReturnNotReady&&!writes());
  tick(s);assert(!writes());destroy(s,n);reboot();}
 // Posted-write timeout and cleanup failure cannot count as a confirmed stage.
 for(unsigned fault=0;fault<2;++fault){arcFixtures();s=make(n);id=attach(s);
  token=displayVote(s,id,SweetDisplayLowSVS);
  if(!fault){rpmhDrop=0x30;tick(s);tick(s,1001);}
  else{tick(s);regs[TCS+4]=1;tick(s);rpmhKeepIRQ=true;tick(s);tick(s,1001);}
  pq=displayStatus(s,id,token);assert(pq.state==SweetRPMhFailed&&pq.uncertain&&!pq.acknowledged&&s->fPoisoned);
  destroy(s,n);reboot();}
 // Admission is rechecked after each cleanup; a newly inherited later-rail
 // request halts the chain rather than overwriting it or rolling parents back.
 arcFixtures();s=make(n);id=attach(s);token=displayVote(s,id,SweetDisplayLowSVS);tick(s);
 regs[TCS+0x2a0+0x1c]=1;regs[TCS+0x2a0+0x34]=0x30008;regs[TCS+4]=1;tick(s);tick(s);
 pq=displayStatus(s,id,token);assert(pq.state==SweetRPMhFailed&&pq.result==kIOReturnBusy&&pq.acknowledged==1&&pq.uncertain);
 assert(s->fArcOwned[0]&&!s->fArcOwned[1]&&!s->fArcOwned[2]);destroy(s,n);reboot();
 // Stop before/after writes and between stages; late timer performs no MMIO.
 for(unsigned stage=0;stage<3;++stage){arcFixtures();s=make(n);id=attach(s);token=displayVote(s,id,SweetDisplayLowSVS);
  if(stage==1)tick(s);if(stage==2)arcStage(s,0,6);
  s->stop(n);pq=s->fClients[0].displayStatus;
  assert(pq.state==SweetRPMhFailed&&pq.result==kIOReturnAborted&&pq.uncertain==(stage!=0));
  trace.clear();s->fTimer->action(s,s->fTimer);assert(trace.empty());
  destroy(s,n);assert(rpmhOwner==(stage?2u:0u));reboot();}
 // Malformed optional ARC data is rejected, while legacy metadata still binds.
 for(unsigned fault=0;fault<11;++fault){arcFixtures();
  if(fault==0)regs[DB+240+16]=0x40000; // wrong accelerator address
  if(fault==1)regs[DB+240+16]=0x30001; // unaligned address
  if(fault==2)regs[DB+240+20]=3;       // odd aux length
  if(fault==3)regs[DB+240+20]=34;      // table exceeds16 entries
  if(fault==4)regs[DB+240+20]=16|(0xffffu<<16); // aux out of range
  if(fault==5)regs[DB+312]=1|(16<<16); // index0 is not OFF
  if(fault==6)regs[DB+316]=48|(48<<16);// repeated/nonascending level
  if(fault==7)regs[DB+320]=256u<<16; // nonzero level after interior zero padding
  if(fault==8)regs[DB+264+16]=0x30000;// aliased MX/CX address
  if(fault==9){regs[DB+264]=regs[DB+240];regs[DB+268]=regs[DB+244];} // duplicate name
  if(fault==10)regs[DB+240+20]=16|(1<<16); // unaligned halfword table
  s=make(n);id=attach(s);assert(s->displayPowerInfo(&pi)==0&&!pi.available&&!writes());
  assert(s->resourceInfo(SweetRailPHY,&info)==0&&info.available);
  assert(s->requestDisplayPower(id,SweetDisplayLowSVS,&denied)==kIOReturnNotReady&&!denied&&!writes());
  destroy(s,n);reboot();}
 // Exhaustion fails explicitly, never wraps to a zero/reused client or token.
 arcFixtures();s=make(n);id=attach(s);s->fNextToken=~0ULL;
 assert(s->requestDisplayPower(id,SweetDisplayLowSVS,&token)==kIOReturnNoResources&&!token&&!writes());
 assert(s->requestVote(id,SweetRailPHY,{true,880,880,36000},&token)==kIOReturnNoResources&&!token&&!writes());
 s->fNextClient=~0ULL;assert(s->attachConsumer(&token)==kIOReturnNoResources&&!token);
 destroy(s,n);reboot();
 dropNub(n);assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 puts("PASS: actual display ARC chain: typed corners, rounded parents, staged fresh ACK, client aggregate, reverse withdrawal, shared VRM gate, inherited conflicts, optional malformed metadata, partial-failure ledger, no rollback, stop/retirement and exhaustion");
 puts("PASS: actual RPMh/MDSS resources; no implicit writes, bounded IDs/tokens, multi-client aggregate/disable, UFS/USB shared last-disable veto with unchanged token/status and no writes, independent fresh completion, inherited conflicts, timer readback/timeout, stop/late event/retirement,11 malformed resources, actual matching/termination");
}
'''
# ack helper normally asserts successful cleanup; uncleared IRQ deliberately
# stays pending until the bounded posted-write timeout.
tests = tests.replace('rpmhKeepIRQ=true;ack(s);', '''rpmhKeepIRQ=true;regs[TCS+4]|=1u<<s->fTCSIndex;tick(s);tick(s);''')
with tempfile.TemporaryDirectory(prefix='mh2lm-rpmh-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(ns['prelude']+ns['DSI_MEMORY_FIXTURE']+ns['pool_fixture']+ns['header']+ns['source']+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
