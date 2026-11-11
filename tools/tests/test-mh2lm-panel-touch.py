#!/usr/bin/env python3
"""Run actual TouchComm C worker + native service + MDSS coordination binding.
Usage: test-mh2lm-panel-touch.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from mh2lm_touch_panel_fixtures import actual_worker

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
existing = Path(__file__).with_name('test-mh2lm-display-service.py')
ns = {'__file__': str(existing), '__name__': 'panel_touch_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)
fixture = r'''
#include <deque>
#include <functional>
extern "C" {
void tcFixtureReboot();void tcFixtureOnline(unsigned);unsigned tcFixtureStep();
void tcFixtureWait();void tcFixtureIRQWait();unsigned tcFixtureIRQState();
uint64_t tcFixtureFrame();uint64_t tcFixtureOwner();void tcFixtureContact(unsigned);
void tcFixtureTokenLimit();void tcFixtureSessionLimit();bool sweet_touch_irq(unsigned);
}
static constexpr uintptr_t TG=0x17a00000,TP=0x03d7a000;
static constexpr uint32_t TB=1u<<16;
static unsigned busReads,busWrites,ups,blocks,clears,wakes;
static bool dropMask,keepEnabled,keepPending;
static unsigned firmwareFault,sleepScale;
static std::deque<std::vector<uint8_t>> wire;
static std::vector<unsigned> commands;
static std::function<void()>waitHook,busHook,armHook;
static uint64_t sleepUntil;
static std::vector<unsigned> hidIDs;
extern "C" uint64_t touchTime(){return fakeTicks;}
extern "C" void touchSleep(uint64_t d){sleepUntil=fakeTicks+(d-fakeTicks)*sleepScale;}
extern "C" void touchAssertWait(void*){if(waitHook){auto f=waitHook;waitHook=nullptr;f();}}
extern "C" void touchClearWait(){++clears;}
extern "C" void touchBlock(){++blocks;if(sleepUntil>fakeTicks)fakeTicks=sleepUntil;sleepUntil=0;}
extern "C" void touchWake(void*){++wakes;}
extern "C" void touchLog(const char*,...){}
extern "C" void touchHID(int finger,int down,int x,int y){assert(!down&&x==12&&y==34);++ups;hidIDs.push_back(finger);}
extern "C" uint32_t touchRead(uintptr_t b,uint32_t o){return readReg(b,o);}
extern "C" void touchWrite(uintptr_t b,uint32_t o,uint32_t v){
 trace.push_back({true,b+o,v});
 if(b==TG&&o==0x19c){if(!keepEnabled)regs[TG+0x11c]&=~v;return;}
 if(b==TG&&o==0x11c){if(armHook){auto f=armHook;armHook=nullptr;f();}regs[b+o]|=v;return;}
 if(b==TG&&o==0x29c){if(!keepPending)regs[TG+0x21c]&=~v;return;}
 if(b==0x03d00000&&o==0x7a008&&dropMask&&!(v&1))return;
 regs[b+o]=v;
}
static void response(unsigned code,std::vector<uint8_t>data={}){
 wire.push_back({0xa5,(uint8_t)code,(uint8_t)data.size(),(uint8_t)(data.size()>>8)});
 if(!data.empty()){std::vector<uint8_t>body={0xa5,3};body.insert(body.end(),data.begin(),data.end());body.insert(body.end(),5,0x5a);wire.push_back(body);}
}
static void identify(){std::vector<uint8_t>p(24);p[0]=1;p[1]=firmwareFault==1?2:1;
 const char*part="TCM-FIXTURE-ONLY";memcpy(p.data()+2,part,16);p[22]=0;p[23]=2;response(0x10,p);}
static const uint8_t format[]={0x18,8,1,6,8,7,8,8,16,9,16,3,0};
extern "C" int touchBusRead(uint8_t*p,size_t n){
 ++busReads;if(busHook){auto f=busHook;busHook=nullptr;f();}
 if(firmwareFault==5)return EIO;
 assert(!wire.empty()&&wire.front().size()==n);memcpy(p,wire.front().data(),n);wire.pop_front();return 0;
}
extern "C" int touchBusWrite(const uint8_t*p,size_t n){
 ++busWrites;assert(n>=3);commands.push_back(p[0]);
 if(firmwareFault==6)return EIO;
 switch(p[0]){
 case 0x20:{std::vector<uint8_t>a(38);a[2]=firmwareFault==2?1:0;a[12]=16;
  unsigned x=firmwareFault==4?1000:1080,y=2340;a[32]=x&255;a[33]=x>>8;a[34]=y&255;a[35]=y>>8;a[36]=10;response(1,a);break;}
 case 6:assert(n==4&&p[3]==0x11);response(1);break;
 case 0x26:assert(n==19&&!memcmp(p+3,format,sizeof(format)));response(1);break;
 case 0x25:{std::vector<uint8_t>f(format,format+sizeof(format));if(firmwareFault==3)f[0]^=1;response(1,f);break;}
 case 5:assert(n==4&&p[3]==0x11);response(1);break;
 default:assert(false);
 }return 0;
}
static void fixtures(){
 assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());queued.clear();resetRegs();
 tcFixtureReboot();fakeTicks=1000000000;wire.clear();commands.clear();hidIDs.clear();
 busReads=busWrites=ups=blocks=clears=wakes=0;firmwareFault=0;sleepScale=1;sleepUntil=0;
 dropMask=keepEnabled=keepPending=false;waitHook=busHook=armHook=nullptr;
 regs[TG]=2;regs[TG+0x11c]=TB;regs[TG+0x21c]=TB;regs[TG+0x31c]=0;
 regs[TP+8]=0x71;regs[TP+4]=1;regs[TP+12]=1;
 touchPanelMode=2;resourceMode=2;panelPinsMode=0;trace.clear();
 failTimerCreate=failEventAdd=0;badMap=shortMap=missingMap=99;
}
static IOService*nub(bool touch=true){auto*n=new IOService;uint32_t abi=1;
 n->properties[touch?"sweet,touch-panel-abi":"sweet,display-resource-abi"]=OSData::withBytes(&abi,4);return n;}
static void dropNub(IOService*n){for(auto&p:n->properties)p.second->release();n->release();}
static SweetTouchPanel*make(IOService*n){auto*s=new SweetTouchPanel();assert(s->start(n));assert(trace.empty()&&!busReads&&!busWrites);return s;}
static uint64_t attach(SweetTouchPanel*s){uint64_t id=0;assert(s->attachConsumer(&id)==0&&id);return id;}
static void timer(SweetTouchPanel*s,unsigned us=1000){fakeTicks+=uint64_t(us)*1000;std::lock_guard<std::recursive_mutex>l(s->fLoop->gate);s->fTimer->action(s,s->fTimer);}
static SweetTouchPanelStatus status(SweetTouchPanel*s,uint64_t id,uint64_t token){SweetTouchPanelStatus p={};assert(s->copyStatus(id,token,&p)==0);return p;}
static uint64_t pause(SweetTouchPanel*s,uint64_t id){uint64_t t=0;assert(s->quiesce(id,&t)==0&&t);assert(tcFixtureStep()==1);timer(s);auto p=status(s,id,t);assert(p.quiesced&&!p.ready&&!p.uncertain&&p.result==0&&!s->fBusy);return t;}
static void destroy(SweetTouchPanel*s,IOService*n){s->stop(n);auto count=trace.size();s->fTimer->action(s,s->fTimer);assert(count==trace.size());s->release();dropNub(n);queued.clear();assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());}
static void boundedWrites(){for(auto&o:trace)if(o.write)assert(o.addr==TG+0x19c||o.addr==TG+0x11c||o.addr==TG+0x29c||o.addr==TP+8||o.addr==TP+12);}
'''
tests = r'''
int main(){
 // Default service absent; metadata mode never changes worker or maps hardware.
 fixtures();assert(!sweet_touch_irq(122));assert(sweet_touch_irq(240));
 assert(tcFixtureIRQState()==2&&!(regs[TG+0x11c]&TB));
 fixtures();touchPanelMode=0;auto*n=nub();auto*s=new SweetTouchPanel();assert(!s->start(n));s->release();dropNub(n);assert(!tcFixtureOwner()&&trace.empty());
 fixtures();touchPanelMode=1;n=nub();s=make(n);auto id=attach(s);uint64_t token=123,other=0;bool available=false;
 assert(s->copyWorkerAvailable(&available)==0&&available);
 assert(s->quiesce(id,&token)==kIOReturnNotPermitted&&!token);
 assert(s->attachConsumer(&other)==kIOReturnBusy&&!other);
 assert(s->detachConsumer(id)==0);assert(attach(s)>id);destroy(s,n);assert(!tcFixtureOwner()&&tcFixtureIRQState()==1&&trace.empty());
 // Invalid ABI/timer/gate errors unwind a metadata-only owner claim.
 for(unsigned fault=0;fault<5;++fault){fixtures();n=nub();
  if(fault==0){n->properties.begin()->second->release();n->properties.clear();}
  if(fault==1){uint32_t abi=2;n->properties.begin()->second->release();n->properties.begin()->second=OSData::withBytes(&abi,4);}
  if(fault==2)failTimerCreate=timerCreates+1;if(fault==3)failEventAdd=eventAdds+1;if(fault==4)failEventAdd=eventAdds+2;
  s=new SweetTouchPanel();assert(!s->start(n));s->release();dropNub(n);assert(!live&&!tcFixtureOwner()&&trace.empty());
 }
 // Service can bind before worker startup, but cannot invent readiness/tokens.
 fixtures();tcFixtureOnline(0);n=nub();s=make(n);id=attach(s);available=true;
 assert(s->copyWorkerAvailable(&available)==0&&!available);
 assert(s->quiesce(id,&token)==kIOReturnNotReady&&!token&&trace.empty());destroy(s,n);
 // Worker boundary is the only source of quiescence. Request and timer alone
 // never access MMIO, release a contact, or manufacture a successful fence.
 fixtures();n=nub();s=make(n);id=attach(s);tcFixtureContact(1);tcFixtureContact(7);
 assert(s->quiesce(id,&token)==0&&token);assert(trace.empty()&&!ups&&tcFixtureIRQState()==1);
 timer(s);assert(trace.empty()&&!ups&&s->fBusy&&!status(s,id,token).quiesced);
 assert(s->detachConsumer(id)==kIOReturnBusy);assert(tcFixtureStep()==1);timer(s);
 auto p=status(s,id,token);assert(p.quiesced&&p.result==0&&ups==2&&hidIDs==std::vector<unsigned>({1,7}));
 assert(tcFixtureIRQState()==5&&!(regs[TP+8]&1)&&!(regs[TG+0x11c]&TB));
 assert(s->detachConsumer(id)==kIOReturnBusy);boundedWrites();trace.clear();
 for(unsigned i=0;i<10;++i){assert(tcFixtureStep()==1);assert(sweet_touch_irq(240));}assert(trace.empty()&&!busReads&&!busWrites&&ups==2);
 // Resume uses the actual TCM identify/app/format/readback/enable protocol.
 auto pauseToken=token;identify();assert(s->resume(id,&token)==0&&token>pauseToken&&trace.empty());
 assert(tcFixtureStep()==0);timer(s);p=status(s,id,token);assert(p.ready&&!p.quiesced&&!p.uncertain&&p.result==0);
 assert(commands==std::vector<unsigned>({0x20,6,0x26,0x25,5})&&wire.empty()&&tcFixtureIRQState()==1);
 assert((regs[TP+8]&0xff)==0x71&&(regs[TG+0x11c]&TB)&&!(regs[TG+0x21c]&TB));boundedWrites();
 assert(s->copyStatus(id,pauseToken,&p)==kIOReturnNotFound&&!p.token);
 auto previousToken=token;assert(s->detachConsumer(id)==0);auto nextID=attach(s);assert(nextID>id);
 assert(s->copyStatus(nextID,previousToken,&p)==kIOReturnNotFound&&!p.token);
 pause(s,nextID);destroy(s,n);
 assert(tcFixtureOwner()==~0ULL);n=nub();s=new SweetTouchPanel();assert(!s->start(n));s->release();dropNub(n);
 // RWP/active line must actually drain. A delayed active clear permits a fence,
 // an uncleared active bit/enable readback/drop-mask never reports QUIESCED.
 for(unsigned fault=0;fault<5;++fault){fixtures();n=nub();s=make(n);id=attach(s);tcFixtureContact(3);
  if(fault<2)regs[TG+0x31c]=TB;if(fault==2)regs[TG]|=1u<<31;if(fault==3)keepEnabled=true;if(fault==4)dropMask=true;
  if(fault==0)waitHook=[](){regs[TG+0x31c]=0;};
  assert(s->quiesce(id,&token)==0);auto result=tcFixtureStep();timer(s);p=status(s,id,token);
  if(fault==0)assert(result==1&&p.quiesced&&p.result==0);
  else assert(result==2&&!p.quiesced&&p.uncertain&&p.result!=0);
  assert(ups==1&&!busReads&&!busWrites);boundedWrites();destroy(s,n);
 }
 // Re-identification/app/format/geometry/I2C/whole-request deadline failures
 // keep IRQ held. No reset, rail, HID-down, silent fallback or automatic retry.
 for(unsigned fault=1;fault<=8;++fault){fixtures();n=nub();s=make(n);id=attach(s);pause(s,id);trace.clear();
  firmwareFault=fault;if(fault==7)keepPending=true;if(fault==8)sleepScale=150;
  identify();assert(s->resume(id,&token)==0);assert(tcFixtureStep()==2);timer(s);p=status(s,id,token);
  assert(p.result!=0&&!p.ready&&!p.quiesced&&p.uncertain&&s->fPoisoned&&tcFixtureIRQState()==5);
  assert(!(regs[TP+8]&1)&&!(regs[TG+0x11c]&TB));boundedWrites();auto io=trace.size();auto bus=busWrites;
  assert(s->resume(id,&other)==kIOReturnNotReady&&!other);timer(s);assert(trace.size()==io&&busWrites==bus);destroy(s,n);
 }
 // Config drift is rejected before transport or rearming.
 fixtures();n=nub();s=make(n);id=attach(s);pause(s,id);regs[TP+8]^=0x20;
 assert(s->resume(id,&token)==0&&tcFixtureStep()==2);timer(s);assert(status(s,id,token).result!=0&&!busWrites);destroy(s,n);
 // No wakeup loss if submit/cancel races the assert-wait boundary.
 fixtures();n=nub();s=make(n);id=attach(s);
 waitHook=[&](){assert(s->quiesce(id,&token)==0);};tcFixtureIRQWait();assert(clears==1);assert(tcFixtureStep()==1);timer(s);
 identify();waitHook=[&](){assert(s->resume(id,&token)==0);};tcFixtureWait();assert(clears==2);assert(tcFixtureStep()==0);timer(s);destroy(s,n);
 // Cancellation at queued pause, parked state, queued resume, during protocol,
 // and just before rearming never produces a usable completion or callback.
 for(unsigned point=0;point<6;++point){fixtures();n=nub();s=make(n);id=attach(s);
  if(point==0)assert(s->quiesce(id,&token)==0);
  else{pause(s,id);if(point>1){identify();assert(s->resume(id,&token)==0);}}
  if(point==3)busHook=[&](){s->stop(n);};
  else if(point==4)waitHook=[&](){s->stop(n);};
  else if(point==5)armHook=[&](){s->stop(n);};
  else s->stop(n);
  assert(tcFixtureStep()==2);p=status(s,id,token);assert(p.state==SWEET_TOUCH_STOPPED&&!p.ready&&!p.quiesced);
  assert(tcFixtureIRQState()==5&&!(regs[TP+8]&1)&&!(regs[TG+0x11c]&TB));boundedWrites();destroy(s,n);
 }
 // Provider request watchdog cancels without claiming hardware quiescence;
 // worker can safely observe cancellation after the service object is freed.
 fixtures();n=nub();s=make(n);id=attach(s);assert(s->quiesce(id,&token)==0);timer(s,6000001);
 p=status(s,id,token);assert(p.result==kIOReturnTimeout&&p.uncertain&&!p.quiesced&&trace.empty());
 destroy(s,n);assert(tcFixtureStep()==2&&tcFixtureIRQState()==5);boundedWrites();
 // Bounded session IDs, worker tickets and consumer IDs never wrap.
 fixtures();tcFixtureSessionLimit();n=nub();s=new SweetTouchPanel();assert(!s->start(n));s->release();dropNub(n);assert(trace.empty());
 fixtures();n=nub();s=make(n);id=attach(s);tcFixtureTokenLimit();assert(s->quiesce(id,&token)==kIOReturnNoResources&&!token);destroy(s,n);
 fixtures();n=nub();s=make(n);s->fNextClient=~0ULL;assert(s->attachConsumer(&id)==kIOReturnNoResources&&!id);destroy(s,n);
 // Independent callers cannot publish two worker requests through the gate.
 fixtures();n=nub();s=make(n);id=attach(s);IOReturn rc1=0,rc2=0;uint64_t t1=0,t2=0;
 std::thread first([&](){rc1=s->quiesce(id,&t1);});
 std::thread second([&](){rc2=s->quiesce(id,&t2);});first.join();second.join();
 assert((rc1==0&&rc2==kIOReturnBusy&&t1&&!t2)||(rc2==0&&rc1==kIOReturnBusy&&t2&&!t1));
 assert(trace.empty()&&tcFixtureStep()==1);timer(s);destroy(s,n);
 // Output parameters and stale IDs/tokens never expose another lease.
 fixtures();n=nub();s=make(n);id=attach(s);other=123;
 assert(s->quiesce(id+1,&other)==kIOReturnNotFound&&!other);
 assert(s->quiesce(id,nullptr)==kIOReturnBadArgument);
 assert(s->copyWorkerAvailable(nullptr)==kIOReturnBadArgument);
 SweetTouchPanelStatus zero={123};assert(s->copyStatus(id,999,&zero)==kIOReturnNotFound&&!zero.token);
 assert(s->copyStatus(id,1,nullptr)==kIOReturnBadArgument);
 assert(s->resume(id,&other)==kIOReturnBusy&&!other&&trace.empty());
 auto*competing=new SweetTouchPanel();assert(!competing->start(n));competing->release();
 destroy(s,n);assert(!tcFixtureOwner());
 // Real registry -> consumer workloop binding; notifications do no heavy work.
 fixtures();n=nub();s=make(n);auto*d=nub(false);auto*m=new SweetMDSS();assert(m->start(d));
 auto count=trace.size();assert(SweetTouchPanelClient::matched(m->fTouchPanelClient,nullptr,s,nullptr));assert(!s->fClient&&!m->fTouchPanel&&trace.size()==count);
 pump();assert(m->fTouchPanel==s&&s->fClient&&trace.size()==count);
 auto retained=s->fClient;SweetTouchPanelClient::terminated(m->fTouchPanelClient,nullptr,s,nullptr);
 assert(s->fClient==retained&&m->fTouchPanel==s);pump();assert(!m->fTouchPanel&&!s->fClient&&trace.size()==count);
 SweetTouchPanelClient::matched(m->fTouchPanelClient,nullptr,s,nullptr);pump();m->stop(d);m->release();dropNub(d);destroy(s,n);assert(!tcFixtureOwner());
 assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 puts("PASS: actual C TouchComm worker+IRQ+protocol and native service/MDSS binding; queued quiesce fence, independent peripheral/GIC active drain, HID release, zero-I/O parked loop, verified application/format resume, state/transport/deadline failures, wakeup races, cancellation after provider free, bounded IDs/tickets, no reset/rail writes and zero objects/maps/notifications");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-panel-touch-') as temp:
    temp = Path(temp)
    worker = temp/'worker.c'
    worker.write_text(actual_worker(root))
    obj = temp/'worker.o'
    flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', *flags, '-c', str(worker), '-o', str(obj)], check=True)
    cpp = temp/'test.cpp'
    cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+ns['prelude']+ns['DSI_MEMORY_FIXTURE']+ns['pool_fixture']+ns['header']+ns['source']+fixture+tests)
    binary = temp/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-pthread', *flags, str(cpp), str(obj), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
