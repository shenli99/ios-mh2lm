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
static bool irqConfigurationWrite(const Op&o){
 return (o.addr==0x17a00184&&o.value==(1u<<5))||o.addr==0x17a00084||
        o.addr==0x17a00c08||o.addr==0x17a00424||
        ((o.addr==0x17a06128||o.addr==0x17a0612c)&&!o.value);
}
// A failed/invalid fence may mask delivery, but must not touch payload,
// trigger, W1C completion or resource votes. Preserve every unrelated bit.
static void irqQuiesceOnly(size_t from,uint32_t oldEnable){
 for(size_t i=from;i<trace.size();++i){auto&o=trace[i];if(!o.write)continue;
  if(o.addr==TCS){assert(!(o.value&~oldEnable)&&(o.value&~3u)==(oldEnable&~3u));oldEnable=o.value;}
  else assert(o.addr==0x17a00184&&o.value==(1u<<5));
 }
}
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
// CN0 metadata the register-bandwidth path needs: one additional DB header
// describing a single slave-5 entry named CN0, its 8-byte aux table holding the
// unit and the packed width/vcd. The DB is enlarged for it; the rails keep their
// own entries where fixtures() put them.
static void bandwidthFixtures(){
 fixtures();regs[LOC+4]=512;
 regs[DB+24]=5|(112<<16);regs[DB+28]=176|(1<<16); // slave5, entries@256, aux@320, count1
 uint32_t key[2]={0,0};const char*name="CN0";
 for(unsigned k=0;k<3;++k)key[k/4]|=uint32_t(uint8_t(name[k]))<<(8*(k%4));
 regs[DB+256]=key[0];regs[DB+260]=key[1];
 regs[DB+256+16]=0x50000;regs[DB+256+20]=8;regs[DB+256+22]=0;
 regs[DB+320]=20000;regs[DB+324]=4|(1<<16);
}
static IOService*nub(bool rpmh=true){auto*n=new IOService;n->rpmhNub=rpmh;
 uint32_t abi=1;auto*d=OSData::withBytes(&abi,4);n->properties[rpmh?"sweet,rpmh-abi":"sweet,display-resource-abi"]=d;
 if(rpmh){n->properties["sweet,rpmh-irq-abi"]=OSData::withBytes(&abi,4);uint32_t intid=37;n->properties["sweet,rpmh-intid"]=OSData::withBytes(&intid,4);}return n;}
static void dropNub(IOService*n){for(auto&p:n->properties)p.second->release();n->release();}
// Hardware peer dispatches only an enabled level; old CMD_STATUS alone cannot
// invent an event. The callback and actual software event/workloop are real.
static bool deliverRPMhIRQ(){
 constexpr uintptr_t g=0x17a00000;constexpr unsigned bit=1u<<5;
 if(!(regs[g+0x104]&bit)||!(regs[TCS]&regs[TCS+4]))return false;
 regs[g+0x204]|=bit;regs[g+0x304]|=bit;regs[g+0x204]&=~bit; // IAR
 assert(sweet_rpmh_irq(37));regs[g+0x304]&=~bit; // sleh EOI
 pump();return true;
}
static void tick(SweetRPMh*s,unsigned us=100){fakeTicks+=us*1000;assert(s->fTimerAdded);if(!deliverRPMhIRQ())s->fTimer->action(s,s->fTimer);}
static void ack(SweetRPMh*s){
 // C3-05: retiring a flight can start the next waiting request inside the same
 // window, so "the machine is idle afterwards" only holds for an empty queue.
 unsigned waiting=s->fQueue.depth();
 uint32_t b=s->fTCSIndex*0x2a0;regs[TCS+4]|=1u<<s->fTCSIndex;regs[TCS+b+0x18]=1;
 for(unsigned c=0;c<s->fCommandCount;++c)regs[TCS+b+0x3c+c*0x14]=(1<<8)|(1<<16);
 tick(s);assert(s->fBusy);tick(s);
 // A waiting request is dispatched in the same window, but only a request
 // that is actually legal starts a flight - either way the queue shrinks and
 // nothing is left waiting without an owner.
 assert(!s->fBusy||s->fQueue.empty());assert(!waiting||s->fQueue.depth()<waiting);
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
 assert(s->fBusy&&regs[TCS]==0x41);tick(s);assert(s->fBusy);
 assert(status(s,id,token).state==SweetRPMhPending);
 // C3-05: a different resource is no longer refused while one is in flight -
 // it waits in the queue. What is still refused is the resource already
 // outstanding, and that refusal costs no token, no queue slot and no write.
 uint64_t denied=123,serial=s->fNextToken;auto issued=writes();
 assert(s->requestVote(id,SweetRailVDDI,{true,1800,1880,6000},&denied)==kIOReturnBusy&&denied==0);
 assert(s->fNextToken==serial&&s->fQueue.empty()&&writes()==issued);
 assert(status(s,id,token).state==SweetRPMhPending);
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
 // C3-05, device evidence 0148: this boot's own consumer votes each rail as a
 // point (min == max), and the aggregate takes the maximum of the floors with
 // the minimum of the ceilings, so a point vote pins both ends at once. A
 // second consumer that moves the floor is therefore illegal - even inside the
 // rail's own range - and the screen refuses it before it costs a token, a
 // queue slot or a write, leaving the first consumer's completion readable.
 // The one admissible second vote is one whose aggregate is identical, which is
 // a no-op and cannot demonstrate order. That is why the demonstration had to
 // move to a resource that aggregates by maximum.
 fixtures();resourceMode=2;s=make(n);id=attach(s);other=attach(s);
 token=vote(s,id,SweetRailVDDI,{true,1800,1800,6000});tick(s);ack(s);
 assert(status(s,id,token).millivolts==1800);
 uint64_t held=token,refused=7;auto pointWrites=writes();
 assert(s->requestVote(other,SweetRailVDDI,{true,1808,1808,6000},&refused)==kIOReturnBadArgument);
 assert(refused==0&&writes()==pointWrites&&s->fQueue.empty()&&s->fNextToken==held);
 assert(status(s,id,held).state==SweetRPMhComplete&&status(s,id,held).millivolts==1800);
 assert(s->requestVote(other,SweetRailVDDI,{true,1808,1800,6000},&refused)==kIOReturnBadArgument);
 assert(refused==0&&writes()==pointWrites&&s->fQueue.empty());
 token=vote(s,other,SweetRailVDDI,{true,1800,1880,0});
 assert(token!=held);assert(!s->fBusy);assert(writes()==pointWrites);assert(s->fQueue.empty());
 assert(status(s,other,token).state==SweetRPMhComplete&&status(s,other,token).millivolts==1800);
 assert(s->fAggregate[SweetRailVDDI].minMV==1800&&s->fAggregate[SweetRailVDDI].maxMV==1800);
 // Withdrawing the point vote opens no hardware flight either: the floor and
 // the mode are what a write carries, and neither moved.
 token=vote(s,id,SweetRailVDDI,{false,0,0,0});
 assert(!s->fBusy&&writes()==pointWrites);
 assert(s->fAggregate[SweetRailVDDI].minMV==1800&&s->fAggregate[SweetRailVDDI].maxMV==1880);
 token=vote(s,other,SweetRailVDDI,{false,0,0,0});tick(s);ack(s);
 assert(!s->fAggregate[SweetRailVDDI].enabled);
 assert(s->detachConsumer(id)==0&&s->detachConsumer(other)==0);destroy(s,n);reboot();
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
 // Competing clients run through the same provider gate. Both are admitted -
 // one starts and the other waits its turn - and each keeps its own token, so
 // neither overwrites the slot the machine is working on. The gate still runs
 // exactly one flight at a time, which is why the second needs a second ACK.
 fixtures();s=make(n);id=attach(s);other=attach(s);
 IOReturn rc1=-1,rc2=-1;uint64_t tok1=0,tok2=0;
 std::thread a([&]{rc1=s->requestVote(id,SweetRailVDDI,{true,1800,1880,62000},&tok1);});
 std::thread b([&]{rc2=s->requestVote(other,SweetRailPHY,{true,880,880,36000},&tok2);});
 a.join();b.join();assert(rc1==0&&rc2==0&&tok1&&tok2&&tok1!=tok2);
 assert(s->fCurrentValid&&s->fCurrent.token==(s->fBusy?s->fCurrent.token:0));
 for(unsigned i=0;i<4&&s->fBusy;++i){tick(s);ack(s);}
 assert(!s->fBusy&&s->fQueue.empty());
 assert(status(s,id,tok1).state==SweetRPMhComplete&&status(s,other,tok2).state==SweetRPMhComplete);
 destroy(s,n);reboot();
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
  if(test==7){tick(s);regs[TCS+4]=1;tick(s);tick(s,100001);}
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
   if(test==6)assert(diagnostic.find("executing=0 irq-observed=0 written=0")!=std::string::npos);
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
 // C3-05: what is refused while a chain runs is the resource already in flight,
 // not an unrelated one (that one waits, see test-mh2lm-apps-queue.py).
 for(unsigned c=0;c<2;++c)assert(s->requestDisplayPower(id,SweetDisplayLowSVS,&denied)==
     kIOReturnBusy&&!denied&&!writes());
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
 for(auto&o:trace)if(o.write)assert(o.addr==TCS||irqConfigurationWrite(o));
 destroy(s,n);reboot();
 // Use second ACTIVE slot without stealing unrelated armed payload/IRQ bits.
 arcFixtures();regs[TCS+0x1c]=1;regs[TCS+0x34]=0x51234;
 s=make(n);id=attach(s);token=displayVote(s,id,SweetDisplayLowSVS);
 arcAcquire(s);
 assert(regs[TCS+0x1c]==1&&regs[TCS+0x34]==0x51234&&regs[TCS+4]==0&&regs[TCS]==0x41);
 destroy(s,n);reboot();
 // Independent clients and VRM/ARC requests cannot interleave transactions: a
// request submitted while a display chain owns the TCS waits for the whole
// chain to retire, then runs alone on the slot the chain released.
 arcFixtures();s=make(n);id=attach(s);other=attach(s);tok1=tok2=0;
 tok1=displayVote(s,id,SweetDisplayLowSVS);
 assert(s->requestVote(other,SweetRailPHY,{true,880,880,36000},&tok2)==0&&tok2&&tok2!=tok1);
 assert(s->fBusy&&s->fArcRequest&&s->fQueue.depth()==1);
 arcStage(s,0,6);arcStage(s,1,6);arcStage(s,2,1);arcStage(s,1,3);arcStage(s,0,2);
 assert(s->fBusy&&!s->fArcRequest&&s->fQueue.empty());
 tick(s);ack(s);
 assert(!s->fBusy&&s->fQueue.empty()&&status(s,other,tok2).state==SweetRPMhComplete);
 assert(displayStatus(s,id,tok1).state==SweetRPMhComplete);
 destroy(s,n);reboot();
// C3-05: a request still waiting when the owner loses its footing never crosses
// the isolation state - it is refused with the isolation reason, is not
// programmed onto any TCS, and does not linger in the queue.
 arcFixtures();s=make(n);id=attach(s);other=attach(s);tok1=tok2=0;
 tok1=displayVote(s,id,SweetDisplayLowSVS);
 assert(s->requestVote(other,SweetRailPHY,{true,880,880,36000},&tok2)==0&&tok2&&s->fQueue.depth()==1);
 tick(s);tick(s,100001);
 assert(s->fPoisoned&&s->fQueue.empty()&&!s->fBusy&&!s->fCurrentValid);
 SweetRPMhStatus qst2=status(s,other,tok2);
 assert(qst2.state==SweetRPMhFailed&&qst2.result==kIOReturnNotReady&&!qst2.uncertain&&!s->fOwned[SweetRailPHY]);
 auto settled=trace.size();tick(s);assert(trace.size()==settled);destroy(s,n);reboot();
// A full queue refuses the next submission explicitly: QueueFull, not a drop, a
// merge or a quiet reorder, and the refusal spends no token and no status slot.
 arcFixtures();s=make(n);
 uint64_t q5[8];for(unsigned i=0;i<8;++i)q5[i]=attach(s);
 uint64_t chainToken=displayVote(s,q5[0],SweetDisplayLowSVS);
 const SweetRail q5Rails[4]={SweetRailVDDI,SweetRailVPNL,SweetRailHost,SweetRailPHY};
 uint64_t q5Tokens[8];unsigned filled=0;
 for(unsigned i=1;i<8;++i)q5Tokens[filled++]=vote(s,q5[i],q5Rails[i%4],{});
 q5Tokens[filled++]=vote(s,q5[0],SweetRailPHY,{}); /* client 0, own distinct rail */
 assert(filled==8&&s->fQueue.full()&&s->fQueue.accepted()==8&&s->fArcRequest);
 uint64_t extra=99;auto q5Writes=writes();
 assert(s->requestVote(q5[1],SweetRailHost,{},&extra)==kIOReturnNoResources&&!extra);
 assert(writes()==q5Writes&&s->fQueue.depth()==8&&s->fQueueRefused==1);
 SweetRPMhStatus qst={};assert(s->copyStatus(q5[1],q5Tokens[0],&qst)==0&&qst.state==SweetRPMhPending);
 assert(displayStatus(s,q5[0],chainToken).state==SweetRPMhPending);
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

// C3-06 cancel / detach / lifetime. A request that has not been dispatched can
// be taken back by its owner and can never be dispatched afterwards; a request
// that has already been triggered cannot, and belongs to the provider until it
// finishes. Neither kind of withdrawal is a hardware rollback, and one client's
// withdrawal never touches another client's votes or another client's requests.
 fixtures();resourceMode=2;s=make(n);id=attach(s);other=attach(s);
 token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);assert(s->fBusy);
 uint64_t bTok=vote(s,other,SweetRailPHY,{true,880,880,36000});
 assert(s->fQueue.depth()==1&&s->fQueue.dispatched()==0);
 auto cancelWrites=writes();
 assert(status(s,other,bTok).state==SweetRPMhPending);
 // Nothing is withdrawn by a caller who cannot name it, and an in-flight
 // request is not a candidate: its storage belongs to the provider now.
 assert(s->cancelRequest(other,0)==kIOReturnBadArgument&&s->fQueue.depth()==1);
 assert(s->cancelRequest(other,token)==kIOReturnBusy&&s->fQueue.depth()==1);
 assert(s->cancelRequest(other,0xdeadbeef)==kIOReturnNotFound&&s->fQueue.depth()==1);
 // Only the owner may take its own wait back; a neighbour's wait is not a
 // withdrawal candidate, and the refusal leaves it waiting.
 assert(s->cancelRequest(id,bTok)==kIOReturnNotPermitted&&s->fQueue.depth()==1);
 assert(status(s,other,bTok).state==SweetRPMhPending&&writes()==cancelWrites);
 // The owner withdraws it: the token fails as withdrawn (not as a timeout, and
 // not uncertain), no hardware is written, and the flight in progress is
 // untouched and still completes under its own provider.
 assert(s->cancelRequest(other,bTok)==0);
 assert(s->fQueue.empty()&&s->fQueue.cancelled()==1&&s->fQueue.dispatched()==0);
 SweetRPMhStatus gone=status(s,other,bTok);
 assert(gone.state==SweetRPMhFailed&&gone.result==kIOReturnAborted&&!gone.uncertain);
 assert(writes()==cancelWrites&&s->fBusy&&s->fCurrentValid&&s->fCurrent.token==token);
 // a cancelled token is gone, not "cancelled again", and costs nothing
 assert(s->cancelRequest(other,bTok)==kIOReturnNotFound);
 assert(s->fQueue.cancelled()==1&&writes()==cancelWrites);
 ack(s);assert(!s->fBusy&&status(s,id,token).state==SweetRPMhComplete);
 // A withdrawn wait releases its resource: the same client may ask again and
 // what it gets is a new request with a new token, not the withdrawn one.
 uint64_t again=vote(s,other,SweetRailPHY,{true,880,880,36000});
 assert(again&&again!=bTok);tick(s);ack(s);
 assert(status(s,other,again).state==SweetRPMhComplete&&s->fQueue.cancelled()==1);
 destroy(s,n);reboot();
 // Exiting is the other way a wait is withdrawn, and an exit is refused while
 // anything is retained: the refusal withdraws nothing.
 fixtures();resourceMode=2;s=make(n);id=attach(s);other=attach(s);uint64_t third=attach(s);
 uint64_t keep=vote(s,other,SweetRailVDDI,{true,1800,1880,62000});tick(s);ack(s);
 assert(!s->fBusy&&s->fClients[1].votes[SweetRailVDDI].enabled);
 token=vote(s,id,SweetRailVPNL,{true,3000,3008,857000});tick(s);assert(s->fBusy);
 uint64_t cTok=vote(s,other,SweetRailHost,{true,1200,1200,1000});
 uint64_t dTok=vote(s,third,SweetRailHost,{true,1200,1200,1000});
 assert(s->fQueue.depth()==2);
 assert(s->detachConsumer(other)==kIOReturnBusy&&s->fQueue.depth()==2);
 assert(status(s,other,cTok).state==SweetRPMhPending&&s->fClients[1].votes[SweetRailVDDI].enabled);
 // The request in flight is not its client's to take back either.
 assert(s->detachConsumer(id)==kIOReturnBusy&&s->fQueue.depth()==2);
 // third holds nothing but its wait, so it goes - and the wait goes with it,
 // before the slot can be handed to anyone else.
 messages.clear();
 assert(s->detachConsumer(third)==0&&s->fQueue.depth()==1&&s->fQueue.cancelled()==1);
 assert(s->detachConsumer(third)==kIOReturnNotFound);
 bool sawExit=false,sawDetach=false;
 for(auto &m:messages){
  if(m.find("phase=cancel")!=std::string::npos&&m.find("why=exit")!=std::string::npos)sawExit=true;
  if(m.find("phase=detach")!=std::string::npos&&m.find("cancelled=1")!=std::string::npos)sawDetach=true;
 }
 assert(sawExit&&sawDetach);
 // The wait that was still queued is untouched and still gets dispatched.
 ack(s);assert(s->fBusy&&s->fCurrent.token==cTok&&s->fQueue.empty());
 tick(s);ack(s);
 assert(!s->fBusy&&status(s,other,cTok).state==SweetRPMhComplete);
 // Giving a retained vote back is a flight like any other - a disable, not a
 // withdrawal - and only after it lands can that client leave. Host is a
 // handoff rail, so its entry is refused to a lone client and keeps the client.
 uint64_t off=vote(s,other,SweetRailVDDI,{false,0,0,0});tick(s);ack(s);
 assert(!s->fBusy&&!s->fClients[1].votes[SweetRailVDDI].enabled);
 assert(status(s,other,off).state==SweetRPMhComplete&&s->fClients[1].votes[SweetRailHost].enabled);
 assert(s->detachConsumer(other)==kIOReturnBusy&&s->detachConsumer(id)==kIOReturnBusy);
 off=vote(s,id,SweetRailVPNL,{false,0,0,0});tick(s);ack(s);
 assert(!s->fBusy&&!s->fClients[0].votes[SweetRailVPNL].enabled);
 assert(s->detachConsumer(id)==0&&s->detachConsumer(id)==kIOReturnNotFound);
 destroy(s,n);reboot();
 // Withdrawing a ledger entry is not a vote. It is allowed only when the array
 // is being told what someone else asks for: then it is a pure ledger edit. When
 // the client holds the aggregate, lowering it needs a real vote and the
 // withdrawal is refused instead of quietly rolling the hardware back.
 bandwidthFixtures();s=make(n);id=attach(s);other=attach(s);
 uint64_t prev=9,agg=9,bw1=0,bw2=0;
 assert(s->withdrawRegisterBandwidth(id,&prev,&agg)==kIOReturnNotFound);
 assert(!prev&&!agg);
 assert(s->withdrawRegisterBandwidth(0,&prev,&agg)==kIOReturnNotFound);
 assert(s->withdrawRegisterBandwidth(id,NULL,NULL)==kIOReturnBadArgument);
 assert(s->withdrawRegisterBandwidth(id,NULL,&agg)==kIOReturnBadArgument);
 assert(s->requestRegisterBandwidth(id,20000,&bw1)==0&&bw1);
 tick(s);ack(s);
 assert(s->requestRegisterBandwidth(other,40000,&bw2)==0&&bw2&&bw2!=bw1);
 tick(s);ack(s);
 assert(s->fRegisterAggregate==40000&&s->fRegisterOwned&&s->fClients[0].registerVote==20000);
 trace.clear();prev=agg=0;
 assert(s->withdrawRegisterBandwidth(id,&prev,&agg)==0);
 assert(prev==20000&&agg==40000&&!writes());
 assert(s->fClients[0].registerVote==0&&s->fRegisterAggregate==40000);
 // other holds the aggregate: a withdrawal would have to lower the array.
 prev=agg=0;
 assert(s->withdrawRegisterBandwidth(other,&prev,&agg)==kIOReturnNotPermitted);
 assert(!prev&&!agg&&!writes()&&s->fClients[1].registerVote==40000);
 // It leaves by saying zero as a vote, which is a flight like any other.
 assert(s->detachConsumer(id)==0);
 uint64_t bw3=0;
 assert(s->requestRegisterBandwidth(other,0,&bw3)==0&&bw3);
 tick(s);ack(s);
 assert(!s->fRegisterAggregate&&s->fClients[1].registerVote==0);
 assert(s->detachConsumer(other)==0);
 destroy(s,n);reboot();
// C3-06 on hardware, 0150: the driver's first step needs a window of its own.
// Both demonstrations hang off the same retirement, so at the moment the trio
// would have been admitted the pair's first member is in flight and its second
// is waiting: the trio parks behind them, all three raises really reach the
// array and nothing is demonstrated - the 0150 ring said `depth=4` and
// `trio-did-not-serialize`, and three votes stayed raised for the rest of the
// boot. A window that starts with work in it must therefore be refused in
// silence and retried later, not spent.
 bandwidthFixtures();s=make(n);id=attach(s);other=attach(s);
 uint64_t bw0=0;
 s->fQueueCancel=true;s->fQueueTestRun=true;s->fCancelStep=0;
 token=vote(s,id,SweetRailVDDI,{true,1800,1880,6000});
 assert(s->fBusy);
 assert(s->requestRegisterBandwidth(other,20000,&bw0)==0&&bw0);
 assert(s->fQueue.depth()==1);
 uint64_t nobody=s->fNextClient;
 messages.clear();
 s->retireFlight();
 assert(s->fCancelStep==0&&s->fNextClient==nobody);
 assert(!s->fQueue.cancelled());
 for(auto &m:messages)assert(m.find("phase=cancel-selftest")==std::string::npos);
 // That window's own wait started on the way out; it is a real flight and it
 // ends like one, which leaves the driver the empty window it was waiting for.
 // (Disarmed for the drain: otherwise the driver runs inside the completion,
 // and this window would be answering a question the test did not ask yet.)
 s->fQueueCancel=false;
 tick(s);ack(s);
 assert(!s->fBusy&&s->fQueue.empty()&&s->fCancelStep==0);
 // The pair demonstration would have left two ledger entries behind by now.
 // This fixture makes the same two through the public calls - C3-05 drives the
 // demonstration itself - because step 2's subject is handing a pair back, not
 // creating one: both values are real, both landed, and neither can be
 // withdrawn until somebody else holds the array up.
 uint64_t pair=0;
 assert(s->requestRegisterBandwidth(id,40000,&pair)==0&&pair);
 tick(s);ack(s);
 assert(s->fRegisterAggregate==40000&&!s->fBusy&&s->fQueue.empty());
 assert(s->fClients[1].registerVote==20000&&s->fClients[0].registerVote==40000);
 s->fSelfClients[0]=1;s->fSelfClients[1]=0;
 s->fQueueCancel=true;
 messages.clear();
 s->retireFlight();
 assert(s->fCancelStep==1&&s->fBusy&&s->fQueue.depth()==2);
 bool trio=false;
 for(auto &m:messages)if(m.find("phase=cancel-selftest")!=std::string::npos&&
   m.find("depth=2 high-water=2")!=std::string::npos)trio=true;
 assert(trio);
 // Steps 2 and 3 are each reached by the transaction before them actually
 // completing, so driving them here is the only way the host sees the rows the
 // device would print - including the one that outgrew the console line buffer
 // on the first round.
 messages.clear();
 tick(s);ack(s);
 assert(s->fCancelStep==2);
 std::string step2,step3;
 for(auto &m:messages)if(m.find("phase=cancel-selftest step=2 result=ok")!=std::string::npos)step2=m;
 assert(!step2.empty()&&step2.find("b1-state=3")!=std::string::npos);
 // The console keeps 255 bytes of a row, the newline among them, and drops the
 // rest without replacing it: a row that does not fit is a row the ring cannot
 // carry, and the audit refuses a ring whose row arrived cut off.
 assert(step2.size()<=255);
 tick(s);ack(s);
 assert(s->fCancelStep==3);
 for(auto &m:messages)if(m.find("phase=cancel-selftest step=3 result=ok")!=std::string::npos)step3=m;
 assert(!step3.empty());
 assert(step3.size()<=255);
 // The budget is the whole point of the row's shape, so the measured sizes are
 // printed rather than left to be guessed from the ring.
 printf("log rows: step2=%u step3=%u of 255 bytes\n",
     (unsigned)step2.size(),(unsigned)step3.size());
 // The trio stays where the demonstration put it; stop() is what takes it back.
 s->fQueueCancel=false;s->fCancelStep=3;
 destroy(s,n);reboot();
 // C3-07: one answer per transaction, keyed by the identity the caller holds -
 // while it flies, after it retires, on a generation it never took, for a token
 // nobody was given, and after the client that owned it has left; plus the two
 // ways a completion can arrive with nothing in flight, and what each may change.
 {
  fixtures();resourceMode=2;s=make(n);messages.clear();
  uint64_t cid=attach(s),owner=0,foreign=0;
  owner=vote(s,cid,SweetRailVDDI,{true,1800,1880,62000});
  SweetTransactionStatus st={};
  // In flight: Pending, naming the generation the array is being told about.
  assert(s->copyTransactionStatus(cid,owner,0,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionPending&&st.executed&&st.generation);
  assert(st.kind==SweetAppsRequestRailVote&&!st.retained&&!st.uncertain&&!st.quarantined);
  const uint64_t gen=st.generation;
  // A client that does not hold the token is refused, not told its outcome.
  foreign=attach(s);
  SweetTransactionStatus other={};
  assert(s->copyTransactionStatus(foreign,owner,gen,&other)==kIOReturnNotPermitted);
  assert(other.verdict==SweetTransactionUnknown);
  // A token nobody was handed is NotFound; no answer at all is BadArgument.
  assert(s->copyTransactionStatus(cid,~owner,0,&other)==kIOReturnNotFound);
  assert(s->copyTransactionStatus(cid,owner,0,nullptr)==kIOReturnBadArgument);
  tick(s);ack(s);assert(!s->fBusy);
  // Retired: the attempt still answers, from this service's own record.
  assert(s->copyTransactionStatus(cid,owner,gen,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionComplete&&st.retained&&st.executed);
  // The same token with a generation the attempt did not take is refused, and
  // no other attempt's outcome is offered in its place.
  assert(s->copyTransactionStatus(cid,owner,gen+1,&other)==kIOReturnNotFound);
  assert(other.verdict==SweetTransactionUnknown&&!other.executed);
  // A later request of the same client must not overwrite the first record.
  uint64_t later=vote(s,cid,SweetRailVDDI,{true,1880,1880,1});
  SweetTransactionStatus pending={};
  assert(s->copyTransactionStatus(cid,later,0,&pending)==kIOReturnSuccess);
  assert(pending.verdict==SweetTransactionPending&&pending.executed&&pending.generation);
  const uint64_t laterGen=pending.generation;
  tick(s);ack(s);assert(!s->fBusy);
  // The row path only ever describes the client's latest request - once the
  // same client has submitted again it no longer names the first attempt,
  // which is exactly why a record has to exist.
  SweetRPMhStatus row={};
  assert(s->copyStatus(cid,later,&row)==kIOReturnSuccess&&row.token==later);
  assert(s->copyStatus(cid,owner,&row)==kIOReturnNotFound);
  SweetTransactionStatus second={};
  assert(s->copyTransactionStatus(cid,later,laterGen,&second)==kIOReturnSuccess);
  assert(second.verdict==SweetTransactionComplete&&laterGen>gen);
  assert(s->copyTransactionStatus(cid,owner,gen,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionComplete&&st.retained&&st.generation==gen);
  // By token alone an executed attempt is not answered: the caller has to name
  // the generation it was given while the attempt was in flight, or it is
  // asking about an attempt nobody can name.
  assert(s->copyTransactionStatus(cid,later,0,&second)==kIOReturnNotFound);
  assert(second.verdict==SweetTransactionUnknown);
  destroy(s,n);reboot();
 }
 // A completion for an attempt that already answered: dropped and named as that
 // attempt's, no ledger moved, and no further work accepted on its account.
 {
  fixtures();resourceMode=2;s=make(n);messages.clear();
  uint64_t cid=attach(s);
  uint64_t tok=vote(s,cid,SweetRailVDDI,{true,1800,1880,62000});
  SweetTransactionStatus st={};
  assert(s->copyTransactionStatus(cid,tok,0,&st)==kIOReturnSuccess);
  const uint64_t gen=st.generation;
  tick(s);ack(s);assert(!s->fBusy);
  const unsigned tcs=s->fTCSIndex;
  assert(s->fRetiredToken==tok&&s->fRetiredGeneration==gen&&!s->fRetiredUncertain);
  // A completion raised after the line was disarmed never reaches the ledger:
  // the top half is what latches one, and it latches nothing without the arm.
  messages.clear();
  regs[TCS]|=1u<<tcs;regs[TCS+4]|=1u<<tcs;regs[TCS+tcs*0x2a0+0x18]=1;
  regs[0x17a00104]|=1u<<5;
  assert(!s->fIRQArmed);
  assert(deliverRPMhIRQ());
  assert(!s->fIRQPosted&&!s->fLateCompletions&&!s->fQuarantine&&!s->fPoisoned&&messages.empty());
  // The window that does reach it: the top half latched a completion while the
  // flight was live and the work loop drains it afterwards. Nothing else can
  // set that latch, so it is set here exactly as the top half leaves it.
  s->fIRQPosted=1;
  s->irqAction(s,nullptr,0);
  assert(s->fLateCompletions==1&&!s->fQuarantine&&s->fPoisoned&&!s->fBusy);
  std::string stray;for(auto &m:messages)if(m.find("stray completion")!=std::string::npos&&
      m.find("token="+std::to_string(tok))!=std::string::npos&&
      m.find("generation="+std::to_string(gen))!=std::string::npos)stray=m;
  assert(!stray.empty());
  // The record is untouched: a completion nobody waited for cannot rewrite it.
  assert(s->copyTransactionStatus(cid,tok,gen,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionComplete&&st.retained&&!st.uncertain);
  unsigned tcsOut=99,refused=99;IOReturn cause=kIOReturnNotReady;
  s->quarantineInfo(&tcsOut,&cause,&refused);
  assert(!tcsOut&&cause==kIOReturnSuccess&&refused==1);
  // New work is refused with a named row instead of being accepted silently.
  messages.clear();
  uint64_t denied2=0;
  assert(s->requestVote(cid,SweetRailPHY,{true,880,880,1},&denied2)==kIOReturnNotReady&&!denied2);
  std::string named;for(auto &m:messages)if(m.find("phase=refuse")!=std::string::npos&&
      m.find("detail=stopped")!=std::string::npos)named=m;
  assert(!named.empty());
  // A row that does not fit the console line buffer arrives cut off, newline
  // included, and the audit refuses a ring carrying one.
  assert(stray.size()<=255&&named.size()<=255);
  printf("failure rows: stray=%u refused=%u\n",(unsigned)stray.size(),(unsigned)named.size());
  destroy(s,n);reboot();
 }
 // Deadline first, completion after: the attempt was given up on, so its late
 // completion is that attempt's own - dropped, and its slot quarantined.
 {
  fixtures();resourceMode=2;s=make(n);messages.clear();
  uint64_t cid=attach(s);
  uint64_t tok=vote(s,cid,SweetRailVDDI,{true,1800,1880,62000});
  tick(s);assert(s->fBusy&&s->fIRQArmed);
  const unsigned tcs=s->fTCSIndex;
  // The completion is raised and taken by the top half while the flight is
  // still live; the deadline expires before the work loop drains it.
  regs[TCS]|=1u<<tcs;regs[TCS+4]|=1u<<tcs;regs[TCS+tcs*0x2a0+0x18]=1;
  regs[0x17a00104]|=1u<<5;
  assert(sweet_rpmh_irq(37));
  queued.clear();
  assert(s->fIRQPosted);
  fakeTicks+=200000*1000;s->advance();
  assert(!s->fBusy&&s->fRetiredUncertain);
  SweetTransactionStatus st={};
  assert(s->fQuarantine&&s->fQuarantineTcs==tcs&&s->fQuarantineCause==kIOReturnTimeout);
  assert(s->copyTransactionStatus(cid,tok,s->fRetiredGeneration,&st)==kIOReturnSuccess);
  // The trigger did reach the array - what is unknown is whether the write
  // landed - so the attempt has a generation and says so.
  assert(st.verdict==SweetTransactionUncertain&&st.uncertain&&st.executed&&st.generation);
  // The slot whose completion has no owner is never offered again.
  assert(!s->emptyTCS(tcs));
  // The refusal is a named, counted row, not a silent NotReady.
  messages.clear();
  uint64_t denied2=0;
  assert(s->requestVote(cid,SweetRailPHY,{true,880,880,1},&denied2)==kIOReturnNotReady&&!denied2);
  // The fixture's IOReturn values are the small integers it defines, so the
  // expected spelling is built from the constant instead of the kernel number.
  char causeText[16]={};snprintf(causeText,sizeof(causeText),"cause=%#x",(unsigned)kIOReturnTimeout);
  std::string quarantineRow;for(auto &m:messages)if(m.find("detail=quarantined")!=std::string::npos&&
      m.find(causeText)!=std::string::npos)quarantineRow=m;
  assert(!quarantineRow.empty()&&s->fQuarantined==1);
  unsigned tcsOut=99,refused=0;IOReturn cause=kIOReturnSuccess;
  s->quarantineInfo(&tcsOut,&cause,&refused);
  assert(tcsOut==tcs&&cause==kIOReturnTimeout&&refused==1);
  messages.clear();
  s->irqAction(s,nullptr,0); // the drain finally runs, on an attempt that is over
  std::string late;for(auto &m:messages)if(m.find("late completion")!=std::string::npos&&
      m.find("quarantined")!=std::string::npos)late=m;
  assert(!late.empty()&&s->fLateCompletions==1);
  // The record still says what was actually known, and nothing more.
  assert(s->copyTransactionStatus(cid,tok,s->fRetiredGeneration,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionUncertain&&st.quarantined);
  unsigned tcsAgain=0;refused=0;s->quarantineInfo(&tcsAgain,&cause,&refused);
  assert(tcsAgain==tcs&&cause==kIOReturnTimeout&&refused==2);
  assert(quarantineRow.size()<=255&&late.size()<=255);
  printf("failure rows: quarantined=%u late=%u\n",
      (unsigned)quarantineRow.size(),(unsigned)late.size());
  destroy(s,n);reboot();
 }
 // A request withdrawn before it ran never reached the array: it answers as
 // aborted with no generation at all, and no question can name another attempt.
 {
  fixtures();resourceMode=2;s=make(n);messages.clear();
  uint64_t a=attach(s),b=attach(s);
  vote(s,a,SweetRailVDDI,{true,1800,1880,62000});
  uint64_t tok=0;assert(s->requestVote(b,SweetRailPHY,{true,880,880,1},&tok)==kIOReturnSuccess&&tok);
  SweetTransactionStatus st={};
  assert(s->copyTransactionStatus(b,tok,0,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionPending&&!st.executed&&!st.generation);
  assert(s->cancelRequest(b,tok)==kIOReturnSuccess);
  assert(s->copyTransactionStatus(b,tok,0,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionFailed&&st.result==kIOReturnAborted);
  assert(!st.executed&&st.retained);
  assert(s->copyTransactionStatus(b,tok,1,&st)==kIOReturnNotFound);
  destroy(s,n);reboot();
 }
 // The record is bounded: the oldest attempt is evicted, not answered with a
 // neighbour's outcome, and the newest ten are all still readable.
 {
  fixtures();resourceMode=2;s=make(n);messages.clear();
  uint64_t cid=attach(s),first=0,last=0,firstGen=0,lastGen=0;
  for(unsigned k=0;k<10;++k){
   uint64_t tok=vote(s,cid,SweetRailVDDI,{true,(k&1)?1880u:1800u,1880,62000});
   SweetTransactionStatus pending={};
   assert(s->copyTransactionStatus(cid,tok,0,&pending)==kIOReturnSuccess);
   assert(pending.verdict==SweetTransactionPending&&pending.generation);
   if(!k){first=tok;firstGen=pending.generation;}
   last=tok;lastGen=pending.generation;
   tick(s);ack(s);
  }
  assert(s->fRetainedCount==8);
  SweetTransactionStatus st={};
  assert(s->copyTransactionStatus(cid,first,firstGen,&st)==kIOReturnNotFound);
  assert(s->copyTransactionStatus(cid,last,lastGen,&st)==kIOReturnSuccess);
  assert(st.verdict==SweetTransactionComplete&&st.retained);
  destroy(s,n);reboot();
 }
 // The demonstration itself, driven by hand at the device's boot level: its own
 // raise really runs, the rest of the answers need no flight, and every row it
 // writes must fit the console.
 {
  bandwidthFixtures();resourceMode=2;s=make(n);messages.clear();
  s->fQuerySelfTest=true;
  uint64_t cid=attach(s);
  vote(s,cid,SweetRailVDDI,{true,1800,1880,62000});
  // The client's vote retires and the demonstration's own raise takes the
  // array; that raise retires in turn, and every step after it needs no flight
  // of its own, so the second retirement carries the rest.
  tick(s);ack(s);assert(s->fQueryPhase==1&&s->fBusy);
  // The demonstration's own raise completes like any other flight: the payload
  // is written first, then the completion it waits for is raised.
  tick(s);assert(s->fBusy);
  const unsigned dt=s->fTCSIndex;const uint32_t db=dt*0x2a0;
  regs[TCS]|=1u<<dt;regs[TCS+4]|=1u<<dt;regs[TCS+db+0x18]=1;
  for(unsigned c=0;c<s->fCommandCount&&c<3;++c)regs[TCS+db+0x3c+c*0x14]=(1<<8)|(1<<16);
  regs[0x17a00104]|=1u<<5;
  for(unsigned n=0;n<8&&s->fBusy;++n)tick(s);
  assert(!s->fBusy&&s->fQueryPhase==4);
  unsigned rows=0,longest=0;
  for(auto &m:messages)if(m.find("phase=transaction")!=std::string::npos){
   ++rows;longest=longest>(unsigned)m.size()?longest:(unsigned)m.size();
  }
  assert(rows==4&&longest<=255);
  printf("transaction rows: count=%u longest=%u of 255 bytes\n",rows,longest);
  assert(s->fRetainedCount>=2);
  destroy(s,n);reboot();
 }

  dropNub(n);assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 puts("PASS: actual display ARC chain: typed corners, rounded parents, staged fresh ACK, client aggregate, reverse withdrawal, shared VRM gate, inherited conflicts, optional malformed metadata, partial-failure ledger, no rollback, stop/retirement and exhaustion");
 puts("PASS: actual RPMh/MDSS resources; no implicit writes, bounded IDs/tokens, multi-client aggregate/disable, UFS/USB shared last-disable veto with unchanged token/status and no writes, independent fresh completion, inherited conflicts, timer readback/timeout, stop/late event/retirement, one TCS owner across queued arrivals, explicit queue-full refusal, waiters refused at the isolation state, owner-only cancellation of a waiting request with no hardware write and no reverse vote, in-flight and neighbour cancels refused, exit withdrawal newest-first after its retain checks, ledger-only register withdrawal refused when it would move hardware, 11 malformed resources, owner-scoped transaction records with generation/cross-client/unissued refusal and bounded eviction, stray and late completions dropped without moving the ledger, quarantine veto and named refusal, cancelled request answered aborted without a generation, actual matching/termination");
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
