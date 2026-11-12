#!/usr/bin/env python3
"""Actual unified profile sleep/reset/brightness + pins + TCM + MDSS engines.
Only OS/MMIO, independent GPIO input and TCM wire endpoints are fixtures.
Usage: test-mh2lm-panel-profile-lifecycle.py EXTRACTED_XNU
"""
from pathlib import Path
import json,os,subprocess,sys,tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;existing=here/'test-mh2lm-panel-lifecycle.py'
ns={'__file__':str(existing),'__name__':'profile_lifecycle_fixture'}
exec(compile(existing.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(existing),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];helpers=ns['helpers'];touch=ns['touch'];actual=ns['actual']
prelude=prelude.replace('static void writeReg(uintptr_t b',r'''
struct WirePacket {uint64_t time;unsigned type;std::vector<uint8_t>payload;bool memory;};
static std::vector<WirePacket>panelPackets;
static std::vector<uint8_t>panelFIFO,*commandMemory=nullptr;
static std::vector<std::pair<uint64_t,unsigned>>pinEdges;
static bool followPins=true;static uintptr_t pinDrop=0;static unsigned selectedProfile=4;
static void writeReg(uintptr_t b''')
prelude=prelude.replace('trace.push_back({true,b+o,v});',r'''trace.push_back({true,b+o,v});
 if(b==0x03d06000||b==0x03908000){
  assert(o==0||(b==0x03d06000&&o==4));if(b+o==pinDrop)return;
  if(o==4){assert(!(v&~2u));pinEdges.push_back({fakeTicks,v&2u});regs[b+o]=(regs[b+o]&~2u)|(v&2u);return;}
  regs[b+o]=v;return;
 }
 if(b==bases[6]&&o==0x17c)for(unsigned i=0;i<4;++i)panelFIFO.push_back((v>>(8*i))&255);
 if(b==bases[6]&&o==0x90){
  bool memory=!regs[b+0x15c];auto bytes=memory ? *commandMemory : panelFIFO;
  unsigned length=regs[b+0x4c];assert(length>=4&&bytes.size()>=length);bytes.resize(length);
  unsigned type=bytes[2],n=type==5?1:(type==21?2:bytes[0]+256u*bytes[1]);
  std::vector<uint8_t>payload;
  if(type==57){assert(bytes[3]==0xc0&&n+4<=bytes.size());payload.assign(bytes.begin()+4,bytes.begin()+4+n);}
  else{assert((type==5||type==21)&&bytes[3]==0x80);payload.assign(bytes.begin(),bytes.begin()+n);}
  panelPackets.push_back({fakeTicks,type,payload,memory});panelFIFO.clear();
 }
''')
# Exact fixture memory backing used by the real descriptor DMA, not a driver state model.
actual=dict(actual);actual['DSI_MEMORY_FIXTURE']=actual['DSI_MEMORY_FIXTURE'].replace('p->bytes.resize(4096,0xab);','p->bytes.resize(4096,0xab);commandMemory=&p->bytes;')
touch=touch.replace('touchPanelMode=2;panelPinsMode=0;lifecycleMode=1;',r'''
 touchPanelMode=2;panelPinsMode=2;lifecycleMode=2;panelPinsOwner=0;
 panelPackets.clear();panelFIFO.clear();pinEdges.clear();commandMemory=nullptr;followPins=true;pinDrop=0;
 regs[0x03d06000]=0x40002c0;regs[0x03d06004]=3;regs[0x03d06008]=0x600;
 regs[0x03908000]=0x2000005;regs[0x03908008]=0x200;
''')
helpers=helpers.replace('static IOService*displayNub(){auto*n=nub(false);',r'''static IOService*displayNub(){auto*n=nub(false);
 uint8_t record[96];mh2lm_panel_record(record,MH_PANEL_SELECTED,selectedProfile);
 n->properties[MH2LM_PANEL_PROPERTY]=OSData::withBytes(record,96);''')
helpers=helpers.replace('IOService *n,*d,*tn;', 'IOService *n,*d,*tn,*pn;SweetPanelPins*p;')
helpers=helpers.replace('tn=touchNub();',r'''pn=new IOService;pn->pinsNub=true;uint32_t one=1;pn->properties["sweet,panel-pins-abi"]=OSData::withBytes(&one,4);
  p=new SweetPanelPins();assert(p->start(pn));tn=touchNub();''')
helpers=helpers.replace('assert(m->fTouchPanel==t);',r'''assert(m->fTouchPanel==t);
  SweetPanelPinsClient::matched(m->fPanelPinsClient,nullptr,p,nullptr);pump();assert(m->fPanelPins==p);''')
helpers=helpers.replace('physical(m);linkPhysical(m);',r'''physical(m);linkPhysical(m);
  if(followPins)regs[0x03d06004]=(regs[0x03d06004]&~1u)|((regs[0x03d06004]>>1)&1u);
  {std::lock_guard<std::recursive_mutex>l(p->fLoop->gate);SweetPanelPins::timerAction(p,p->fTimer);}
''')
helpers=helpers.replace('bool pinned=pool->pinned;m->release();', 'bool pinned=pool->pinned;auto*buffer=m->fDsiPinned?m->fDsiBuffer:nullptr;m->release();if(buffer){buffer->complete(kIODirectionOut);buffer->release();}')
helpers=helpers.replace('t->stop(tn);t->release();dropNub(tn);', 't->stop(tn);t->release();dropNub(tn);p->stop(pn);p->release();dropNub(pn);')
# Independent immutable LG fixtures used only as a wire oracle.
expected=[]
for rec in json.loads((here/'fixtures/mh2lm-panel-profiles.json').read_text())['profiles']:
 for key,val in rec['sequences'].items():
  raw=bytes.fromhex(val['hex']);off=0;packets=[]
  while off<len(raw):
   n=int.from_bytes(raw[off+5:off+7],'big');packets.append((raw[off],raw[off+4],raw[off+7:off+7+n]));off+=7+n
  expected.append(f'if(profile=={rec["id"]} && on=={str(key=="on").lower()}) return {{'+','.join('{'+str(t)+','+str(delay)+',{'+','.join(map(str,payload))+'}}' for t,delay,payload in packets)+'};')
tests=r'''
struct Expected{unsigned type,delay;std::vector<uint8_t>payload;};
static std::vector<Expected>expected(unsigned profile,bool on){
'''+ '\n'.join(expected)+r'''
 abort();}
static void checkPackets(unsigned profile,bool on,unsigned brightness=0){
 auto want=expected(profile,on);if(brightness)want.push_back({57,0,{0x51,uint8_t(brightness>>8),uint8_t(brightness)}});
 assert(panelPackets.size()==want.size());
 for(unsigned i=0;i<want.size();++i){auto&p=panelPackets[i];assert(p.type==want[i].type&&p.payload==want[i].payload);
  assert(p.memory==(p.type==57&&p.payload.size()+4>64));
  auto end=i+1<want.size()?panelPackets[i+1].time:fakeTicks;
  assert(end-p.time>=uint64_t(want[i].delay)*1000000ULL);}
}
static void dead(Harness&h){
 for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault&&h.m->fLifecycle.phase!=SweetPanelStopped;++i)h.step();
 assert(h.m->fLifecycle.uncertain&&(h.m->fLifecycle.phase==SweetPanelFault||h.m->fLifecycle.phase==SweetPanelStopped));
 assert(!h.p->fBusy&&!h.p->fTimer->timeout);
 auto before=trace.size();h.life();h.scan();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);SweetPanelPins::timerAction(h.p,h.p->fTimer);
 assert(trace.size()==before&&h.m->fManagedRails==15&&h.pool->pinned);
}
static void ready(Harness&h){h.takeover();SweetDisplayBackend*b=h.m;assert(b->setBrightness(0x345)==0&&h.m->fPanelBrightnessKnown);panelPackets.clear();panelFIFO.clear();}
static void sleep(Harness&h){h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);assert(h.m->fLifecycle.sleeping&&h.m->fLifecycle.touchQuiesced);}
int main(){
 // Bad/missing/mismatching ABI never admits mode2 or writes any hardware.
 for(unsigned bad=0;bad<6;++bad){warmFixture();coreFixture();hostFixture();linkFixture();touchSetup();auto*d=displayNub();
  auto*record=(OSData*)d->properties[MH2LM_PANEL_PROPERTY];
  if(bad==0){record->release();d->properties.erase(MH2LM_PANEL_PROPERTY);}
  if(bad==1)record->bytes[16]=99;if(bad==2)record->bytes[24]^=1;
  if(bad==3)record->bytes[12]=MH_PANEL_ABSENT;if(bad==4)panelPinsMode=1;if(bad==5)lifecycleMode=4;
  auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());mdssClockWriter=mdssNoCWriter=0;}
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;h.takeover();
  assert(h.m->panelLifecycleCapabilities()&SweetPanelRetainedSleepReset);
  uint64_t rejected=99;auto before=trace.size();assert(h.m->beginPanelLifecycle(SweetPanelDisplayOff,&rejected)==kIOReturnNotReady&&!rejected&&trace.size()==before);
  SweetDisplayBackend*b=h.m;assert(b->setBrightness(0x345)==0);panelPackets.clear();panelFIFO.clear();
  for(unsigned round=0;round<3;++round){
   pinEdges.clear();sleep(h);checkPackets(profile,false);assert(pinEdges.empty());
   assert(h.m->fManagedRails==15&&h.m->fManagedARC&&h.m->fManagedBandwidth&&h.m->fManagedClock);
   panelPackets.clear();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPinReset);
   assert(h.m->lifecycleBusy()&&!h.m->fLifecycle.commandToken);
   auto wrong=h.m->fLifecycle.pinToken+1;auto n=trace.size();assert(h.p->cancelRequest(h.m->fPanelPinsConsumer,wrong)==kIOReturnNotFound&&trace.size()==n);
   h.wakePixels();assert(h.m->fLifecycle.resetComplete&&h.m->fLifecycle.brightnessRestored&&!h.m->fLifecycle.sleeping&&h.t->fStatus.ready);
   checkPackets(profile,true,0x345);assert(pinEdges.size()==3&&pinEdges[0].second==2&&pinEdges[1].second==0&&pinEdges[2].second==2);
   assert(pinEdges[1].first-pinEdges[0].first>=10000000&&pinEdges[2].first-pinEdges[1].first>=2000000);
   assert(panelPackets.front().time-pinEdges[2].first>=15000000); // high10 + LG settle5
   panelPackets.clear();panelFIFO.clear();
  }
 }
 selectedProfile=4;
 // Cancellation during active/reset phases leaves no more pin edges, no rail drop.
 for(unsigned point=0;point<9;++point){Harness h;ready(h);sleep(h);h.power(SweetPanelDisplayOn);
  if(point==0)h.untilLife(SweetPanelPinActivate);
  else{h.untilLife(SweetPanelPinReset);
   unsigned phases[]={SweetPanelPins::Configure,SweetPanelPins::HighConfirm,SweetPanelPins::HighHold,SweetPanelPins::LowConfirm,SweetPanelPins::LowHold,SweetPanelPins::FinalHighConfirm,SweetPanelPins::FinalHighHold,SweetPanelPins::Settle};
   unsigned target=phases[point-1];for(unsigned i=0;i<2000&&h.p->fPhase!=target;++i)h.step();assert(h.p->fPhase==target);}
  h.m->cancelPanelLifecycle(h.m->fLifecycle.token);dead(h);}
 // Stop, queued withdrawal, lost input/readback, command DMA and brightness failure.
 for(unsigned bad=0;bad<10;++bad){Harness h;ready(h);sleep(h);panelPackets.clear();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPinReset);
  if(bad==0){h.m->stop(h.d);dead(h);}
  if(bad==1){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);h.life();dead(h);pump();}
  if(bad==2){pinDrop=0x03d06004;dead(h);}
  if(bad==3){followPins=false;dead(h);}
  if(bad==4){regs[0x03908008]^=0x10;dead(h);}
  if(bad==5){fakeTicks+=100000000;h.life();dead(h);}
  if(bad>=6){
   if(bad==6){while(!h.m->fDsiPinned)h.step();dmaDone=false;dead(h);assert(h.m->fDsiPinned);}
   if(bad==7){h.untilLife(SweetPanelBrightness);dmaDone=false;dead(h);}
   if(bad==8){h.untilLife(SweetPanelCommand);firmwareFault=3;h.untilLife(SweetPanelFrame);regs[0x0ae01014]|=0x100;realFrame(h.m);h.life();dead(h);}
   if(bad==9){h.untilLife(SweetPanelCommand);SweetTouchPanelClient::terminated(h.m->fTouchPanelClient,nullptr,h.t,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);dead(h);pump();}
  }
 }
 // Latest target on Busy is preserved; backlight retries any change arriving later.
 {Harness h;ready(h);sleep(h);h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPinReset);
  SweetDisplayBackend*b=h.m;assert(b->setBrightness(0x678)==kIOReturnBusy);panelPackets.clear();h.wakePixels();checkPackets(4,true,0x678);}
 puts("PASS: actual mode2 four LG profile tables over FIFO/prepared DMA; three sleep/reset/wake cycles each, exact wire/delay/reset+settle and brightness recovery, physical touch reinit/pixel completion, live identity/dependency rejection, cancel every pin phase, provider loss/stop/input/ACK/timeout/DMA/brightness/touch faults, retained votes and zero objects/maps/allocations/notifications after simulated reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-profile-lifecycle-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
