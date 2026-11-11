#!/usr/bin/env python3
"""Run the actual panel pin provider/client/MDSS with independent GPIO input.
Usage: test-mh2lm-panel-pins.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
existing = Path(__file__).with_name('test-mh2lm-display-service.py')
ns = {'__file__': str(existing), '__name__': 'panel_pins_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)
# GPIO input bit0 changes only when the independent physical step runs. A
# successful output latch write must never itself satisfy input confirmation.
prelude = ns['prelude'].replace('static void writeReg(uintptr_t b',
    'static uintptr_t pinDrop=0;\nstatic void writeReg(uintptr_t b')
prelude = prelude.replace('trace.push_back({true,b+o,v});', '''trace.push_back({true,b+o,v});
 if(b==0x03d06000||b==0x03908000){
  assert(o==0||(b==0x03d06000&&o==4));
  if(b+o==pinDrop)return;
  if(o==4){assert(!(v&~2u));regs[b+o]=(regs[b+o]&~2u)|(v&2u);return;}
  regs[b+o]=v;return;
 }''')
tests = r'''
static constexpr uintptr_t RESET=0x03d06000,TE=0x03908000;
static unsigned writes(){unsigned n=0;for(auto&o:trace)n+=o.write;return n;}
static void fixtures(){
 assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());queued.clear();panelPinsOwner=0;
 resetRegs();trace.clear();panelPinsMode=2;resourceMode=2;pinDrop=0;
 badMap=missingMap=shortMap=99;failTimerCreate=failEventAdd=0;
 regs[RESET]=0x40002c0;regs[RESET+4]=3;regs[RESET+8]=0x600;
 regs[TE]=0x2000005;regs[TE+8]=0x200;fakeTicks=1000000000;
}
static IOService*nub(bool pins=true){auto*n=new IOService;n->pinsNub=pins;
 uint32_t abi=1;n->properties[pins?"sweet,panel-pins-abi":"sweet,display-resource-abi"]=OSData::withBytes(&abi,4);return n;}
static void dropNub(IOService*n){for(auto&p:n->properties)p.second->release();n->release();}
static SweetPanelPins*make(IOService*n){auto*s=new SweetPanelPins();assert(s->start(n));assert(trace.empty());return s;}
static uint64_t attach(SweetPanelPins*s){uint64_t id=0;auto size=trace.size();assert(s->attachConsumer(&id)==0&&id);assert(size==trace.size());return id;}
static void tick(SweetPanelPins*s,unsigned us=100,bool follow=true){
 fakeTicks+=uint64_t(us)*1000;
 if(follow)regs[RESET+4]=(regs[RESET+4]&~1u)|((regs[RESET+4]>>1)&1u);
 std::lock_guard<std::recursive_mutex> l(s->fLoop->gate);s->fTimer->action(s,s->fTimer);
}
static SweetPanelPinStatus status(SweetPanelPins*s,uint64_t id,uint64_t token){SweetPanelPinStatus p={};assert(s->copyStatus(id,token,&p)==0);return p;}
static uint64_t active(SweetPanelPins*s,uint64_t id){uint64_t t=0;auto size=trace.size();assert(s->activate(id,&t)==0&&t&&trace.size()==size);tick(s);auto p=status(s,id,t);assert(p.state==SweetPinsActive&&!p.uncertain&&p.resetKnown);return t;}
static void destroy(SweetPanelPins*s,IOService*n){s->stop(n);auto size=trace.size();s->fTimer->action(s,s->fTimer);assert(trace.size()==size);s->release();dropNub(n);queued.clear();assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());}
int main(){
 // Default and metadata-only mode never touch GPIO, including explicit denial.
 fixtures();panelPinsMode=0;auto*n=nub();auto*s=new SweetPanelPins();assert(!s->start(n)&&trace.empty());s->release();dropNub(n);
 fixtures();panelPinsMode=1;n=nub();s=make(n);auto id=attach(s);uint64_t token=123,other=123;
 assert(s->activate(id,&token)==kIOReturnNotPermitted&&!token&&trace.empty());
 assert(s->attachConsumer(&other)==kIOReturnBusy&&!other);
 assert(s->detachConsumer(id)==0);other=attach(s);assert(other>id);assert(s->detachConsumer(other)==0);
 destroy(s,n);assert(panelPinsOwner==0);
 // Every mapped-page failure unwinds without MMIO; ABI validated first.
 for(unsigned fault=0;fault<11;++fault){fixtures();n=nub();
  if(fault<2)badMap=fault;else if(fault<4)missingMap=fault-2;else if(fault<6)shortMap=fault-4;
  else if(fault==6){n->properties.begin()->second->release();n->properties.clear();}
  else if(fault==7){n->properties.begin()->second->release();uint32_t abi=2;n->properties.begin()->second=OSData::withBytes(&abi,4);}
  else if(fault==8)failTimerCreate=timerCreates+1;
  else failEventAdd=eventAdds+(fault==9?1:2);
  s=new SweetPanelPins();assert(!s->start(n)&&trace.empty());s->release();dropNub(n);assert(!live&&!liveMaps&&panelPinsOwner==0);
 }
 // Exclusive service and bounded, monotonically increasing client IDs/tokens.
 fixtures();n=nub();s=make(n);auto*second=new SweetPanelPins();assert(!second->start(n));second->release();id=attach(s);
 s->fNextToken=~0ULL;assert(s->activate(id,&token)==kIOReturnNoResources&&!token&&trace.empty());
 assert(s->detachConsumer(id)==0);s->fNextClient=~0ULL;assert(s->attachConsumer(&token)==kIOReturnNoResources&&!token);destroy(s,n);
 // Full reset: independent input confirmation, exact minimum holds, early timers.
 fixtures();n=nub();s=make(n);id=attach(s);auto initial=active(s,id);
 assert((regs[RESET]&0x3ff)==0x2c0&&(regs[RESET]&~0x3ffu)==0x4000000&&regs[RESET+8]==0x600);
 assert((regs[TE]&0x3ff)==5&&(regs[TE]&~0x3ffu)==0x2000000&&regs[TE+8]==0x200);
 assert(s->detachConsumer(id)==kIOReturnBusy);
 assert(s->reset(id,&token)==0&&token>initial);assert(s->park(id,&other)==kIOReturnBusy&&!other);
 tick(s);assert(s->fPhase==SweetPanelPins::HighConfirm);tick(s);assert(s->fPhase==SweetPanelPins::HighHold);
 auto confirmed=fakeTicks;tick(s,9999);assert(s->fPhase==SweetPanelPins::HighHold&&fakeTicks-confirmed==9999000);
 tick(s,1);assert(s->fPhase==SweetPanelPins::LowConfirm&&regs[RESET+4]==1); // latch low, input still high
 tick(s,100,false);assert(s->fPhase==SweetPanelPins::LowConfirm);tick(s);assert(s->fPhase==SweetPanelPins::LowHold);
 tick(s,1999);assert(s->fPhase==SweetPanelPins::LowHold);tick(s,1);assert(s->fPhase==SweetPanelPins::FinalHighConfirm&&regs[RESET+4]==2);
 tick(s,100,false);assert(s->fPhase==SweetPanelPins::FinalHighConfirm);tick(s);assert(s->fPhase==SweetPanelPins::FinalHighHold);
 tick(s,9999);assert(s->fPhase==SweetPanelPins::FinalHighHold);tick(s,1);assert(s->fPhase==SweetPanelPins::Settle);
 tick(s,4999);assert(s->fBusy);tick(s,1);auto p=status(s,id,token);assert(p.state==SweetPinsResetComplete&&p.resetKnown&&p.resetHigh&&!p.uncertain);
 assert(s->copyStatus(id,initial,&p)==kIOReturnNotFound&&!p.token);
 // Park never releases OE, holds low for5ms; reactivation stays low until reset.
 assert(s->park(id,&token)==0);tick(s);assert(regs[RESET+4]==1);tick(s);assert(s->fPhase==SweetPanelPins::ParkHold);
 tick(s,4999);assert(s->fBusy);tick(s,1);p=status(s,id,token);assert(p.state==SweetPinsParked&&p.resetKnown&&!p.resetHigh);
 assert((regs[RESET]&0x3ff)==0x201&&regs[RESET+4]==0&&(regs[TE]&0x3ff)==5);
 token=active(s,id);assert(!status(s,id,token).resetHigh&&regs[RESET+4]==0);
 assert(s->reset(id,&token)==0);for(unsigned i=0;i<400&&s->fBusy;++i)tick(s);assert(status(s,id,token).state==SweetPinsResetComplete);
 destroy(s,n);assert(panelPinsOwner==2);
 n=nub();s=new SweetPanelPins();assert(!s->start(n));s->release();dropNub(n);assert(!live&&!liveMaps);
 // Configuration/ownership mismatches reject before any write and can detach.
 for(unsigned fault=0;fault<9;++fault){fixtures();n=nub();s=make(n);id=attach(s);
  if(fault==0)regs[RESET]|=4;if(fault==1)regs[TE]&=~4u;if(fault==2)regs[TE]|=512;
  if(fault==3)regs[RESET]|=1<<12;if(fault==4)regs[TE]|=1<<12;if(fault==5)regs[RESET+8]|=1;
  if(fault==6)regs[TE+8]|=1;if(fault==7)regs[RESET+4]=2;if(fault==8)regs[RESET]&=~512u;
  assert(s->activate(id,&token)==0);tick(s,100,false);p=status(s,id,token);assert(p.state==SweetPinsFailed&&!p.uncertain&&!writes());
  assert(s->detachConsumer(id)==0);destroy(s,n);assert(panelPinsOwner==0);
 }
 // Dropped config/output writes, independent input timeout, hold-level loss,
 // configuration/IRQ drift and total timeout never retry/rollback/detach.
 for(unsigned fault=0;fault<8;++fault){fixtures();n=nub();s=make(n);id=attach(s);
  if(fault==0){regs[RESET]=(regs[RESET]&~0x3ffu)|0x200;pinDrop=RESET;assert(s->activate(id,&token)==0);tick(s);}
  else if(fault==1){regs[TE]=(regs[TE]&~0x3ffu)|4;pinDrop=TE;assert(s->activate(id,&token)==0);tick(s);}
  else{active(s,id);trace.clear();assert(s->park(id,&token)==0);
   if(fault==2)pinDrop=RESET+4;tick(s);
   if(fault==3)tick(s,1001,false);
   if(fault==4){tick(s);regs[RESET+4]|=1;tick(s,100,false);}
   if(fault==5){regs[RESET]^=1;tick(s);}
   if(fault==6){regs[TE+8]^=0x10;tick(s);}
   if(fault==7)tick(s,100001);
  }
  p=status(s,id,token);assert(p.state==SweetPinsFailed&&p.uncertain&&s->fPoisoned&&!s->fBusy);
  assert(s->detachConsumer(id)==kIOReturnBusy);trace.clear();tick(s);assert(trace.empty());
  assert(s->reset(id,&other)==kIOReturnNotReady&&!other&&trace.empty());destroy(s,n);assert(panelPinsOwner==2);
 }
 // Stop at every reset/park phase, including before the first write. Late
 // callbacks and status queries perform no MMIO; partial pins stay as-is.
 for(unsigned phase=0;phase<11;++phase){fixtures();n=nub();s=make(n);id=attach(s);
  if(phase==0)assert(s->activate(id,&token)==0);
  else{active(s,id);assert((phase>8?s->park(id,&token):s->reset(id,&token))==0);
   for(unsigned count=0;count<400&&s->fPhase!=(phase-1);++count)tick(s);
   assert(s->fBusy);
  }
  trace.clear();s->willTerminate(n,0);assert(trace.empty());p=status(s,id,token);
  assert(p.state==SweetPinsStopped&&p.result==kIOReturnAborted&&p.uncertain==(phase!=0));
  tick(s);assert(trace.empty());destroy(s,n);assert(panelPinsOwner==(phase?2u:0u));
 }
 // Actual workloop gate makes stop wait for an in-flight register operation.
 fixtures();n=nub();s=make(n);id=attach(s);assert(s->activate(id,&token)==0);
 pauseRead=true;pauseAddress=RESET;enteredRead=releaseRead=false;std::atomic<bool>stopped=false;
 std::thread event([&]{tick(s);});{std::unique_lock<std::mutex>l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 std::thread stopping([&]{s->stop(n);stopped=true;});
 {std::lock_guard<std::mutex>l(pauseLock);assert(!stopped);releaseRead=true;}pauseCV.notify_all();
 event.join();stopping.join();pauseRead=false;pauseAddress=0;assert(stopped);destroy(s,n);
 // Real registry binding: notification does not call provider. Workloop attach
 // is metadata-only; termination only drops ref/queues; stop removes notifiers.
 fixtures();n=nub();s=make(n);auto*d=nub(false);auto*m=new SweetMDSS();assert(m->start(d));
 auto count=trace.size();assert(!s->fClient&&!m->fPanelPins);
 assert(SweetPanelPinsClient::matched(m->fPanelPinsClient,nullptr,s,nullptr));assert(!s->fClient&&!m->fPanelPins&&trace.size()==count);
 pump();assert(s->fClient&&m->fPanelPins==s&&m->fPanelPinsConsumer==s->fClient&&trace.size()==count);
 auto client=s->fClient;assert(SweetPanelPinsClient::terminated(m->fPanelPinsClient,nullptr,s,nullptr));
 assert(s->fClient==client&&m->fPanelPins==s&&trace.size()==count);pump();assert(!s->fClient&&!m->fPanelPins&&trace.size()==count);
 SweetPanelPinsClient::matched(m->fPanelPinsClient,nullptr,s,nullptr);pump();assert(m->fPanelPins==s);
 m->stop(d);assert(!s->fClient);m->release();dropNub(d);destroy(s,n);assert(panelPinsOwner==0);
 puts("PASS: actual pin provider+MDSS registry binding; default/metadata no-MMIO, exact SOUTH/NORTH ABI, exclusive IDs/tokens, queued work, independent latch/input,10/2/10/5ms minimum holds, park/reactivate, readback/input/drift/timeouts, partial-failure retention, every-phase stop/late event, concurrent gate stop, ownership retirement and zero objects/maps/notifications");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-panel-pins-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+ns['DSI_MEMORY_FIXTURE']+ns['pool_fixture']+ns['header']+ns['source']+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
