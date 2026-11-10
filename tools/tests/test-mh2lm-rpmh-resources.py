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
int main(){
 auto*n=nub();resourceMode=0;auto*s=new SweetRPMh();assert(!s->start(n)&&!writes());s->release();
 // Read-only metadata mode: no hardware writes, no automatic enable/disable.
 fixtures();resourceMode=1;s=make(n);uint64_t id=attach(s),token=0;
 SweetRailInfo info={};assert(s->resourceInfo(SweetRailVPNL,&info)==0&&info.available&&info.minMV==2856);
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
 for(unsigned test=0;test<6;++test){fixtures();s=make(n);id=attach(s);
  token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});
  if(test==0){tick(s);tick(s,100001);}
  if(test==1){rpmhDrop=0x30;tick(s);assert(s->fBusy);tick(s,1001);}
  if(test==2){tick(s);rpmhKeepIRQ=true;ack(s);}
  if(test==3){s->stop(n);}
  if(test==4){tick(s);s->stop(n);}
  if(test==5){tick(s);regs[TCS+4]=~0u;tick(s);}
  if(test==2){assert(s->fBusy);tick(s,1001);}
  assert(!s->fBusy);q=s->fClients[0].status;
  assert(q.state==SweetRPMhFailed&&q.uncertain==(test!=3));
  if(test<3||test==5)assert(s->requestVote(id,SweetRailPHY,{true,880,880,1},&denied)==kIOReturnNotReady);
  auto count=trace.size();s->fTimer->action(s,s->fTimer);assert(trace.size()==count);
  destroy(s,n);assert(rpmhOwner==(test==3?0u:2u));reboot();}
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
 dropNub(n);assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 puts("PASS: actual RPMh/MDSS resources; no implicit writes, bounded IDs/tokens, multi-client aggregate/disable, independent fresh completion, inherited conflicts, timer readback/timeout, stop/late event/retirement,11 malformed resources, actual matching/termination");
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
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
