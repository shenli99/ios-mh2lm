#!/usr/bin/env python3
"""Actual mode2/7 post-frame typed brightness, transport and failure fences.
Usage: test-mh2lm-brightness-lifecycle.py EXTRACTED_XNU [--pipeline]
"""
from pathlib import Path
import os, subprocess, sys, tempfile
if len(sys.argv) not in (2,3): sys.exit(__doc__)
here=Path(__file__).resolve().parent
fixture=here/'test-mh2lm-command-lifecycle.py'
ns={'__file__':str(fixture),'__name__':'brightness_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
tests=ns['tests'].split('int main(){')[0]+r'''
static void firstPixels(Harness&h){
 regs[bases[3]+0x18]=regs[bases[3]+0x1c]=0;regs[0x0ae01014]|=0x100;realFrame(h.m);h.life();
 assert(h.m->fLifecycle.phase==SweetPanelBrightnessDrain&&h.m->fLifecycle.pixelsComplete&&!h.m->fLifecycle.brightnessRestored);
 assert(h.m->fIrqState==SweetMDSS::IrqIdle&&h.m->fLifecycle.touchQuiesced&&!h.t->fStatus.ready);
}
static void brightnessAdmission(Harness&h){
 assert(h.m->fLifecycle.phase==SweetPanelBrightnessDrain);h.life();h.life();
 assert(h.m->fLifecycle.phase==SweetPanelBrightness&&h.m->fDsiStatus.state==SweetDSIQuiescing);
}
static void secondPixels(Harness&h){
 assert(h.m->fLifecycle.phase==SweetPanelFrame&&h.m->fLifecycle.brightnessRestored&&!h.m->fLifecycle.pixelsComplete);
 auto packets=panelPackets.size();h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
 regs[bases[3]+0x18]=regs[bases[3]+0x1c]=0;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
 // First frame's latched PP_DONE was cleared before rearm. Neither a fresh
 // RD_PTR by itself, nor PP_DONE with no new RD_PTR, constitutes completion.
 regs[0x0ae01014]|=0x100;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
 regs[0x0ae01014]&=~0x100;realFrame(h.m);h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
 regs[0x0ae01014]|=0x100;h.life();assert(h.m->fLifecycle.phase==SweetPanelTouchResume);
 h.untilLife(SweetPanelComplete);assert(h.t->fStatus.ready&&h.m->fLifecycle.pixelsComplete&&panelPackets.size()==packets);
}
static void startWake(Harness&h){ready(h);supplyPark(h);panelPackets.clear();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelFrame);}
int main(){
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;startWake(h);
  checkPackets(profile,true);auto tableCount=panelPackets.size(),generation=h.m->fPipelineGeneration;
  assert(!h.m->fLifecycle.brightnessRestored&&!h.m->fLifecycle.pixelsComplete);
  // Target updates before pixels are retained, without any wire operation.
  assert(h.m->setBrightness(0x456)==kIOReturnBusy);h.life();assert(panelPackets.size()==tableCount);
  firstPixels(h);assert(h.m->setBrightness(0x567)==kIOReturnBusy);h.untilLife(SweetPanelBrightness);
  assert(h.m->fLifecycleBrightnessTarget==0x567&&h.m->fDsiHS);
  auto deadline=h.m->fLifecycleStepDeadline;
  while(h.m->fDsiStatus.state!=SweetDSITransferring)h.step();
  assert(panelPackets.size()==tableCount+1&&!panelPackets.back().lowPower&&!h.m->fLifecycle.brightnessRestored);
  assert(h.m->setBrightness(0x678)==kIOReturnBusy);
  while(h.m->fLifecycleBrightnessTarget!=0x678)h.step();
  assert(h.m->fLifecycleStepDeadline==deadline&&!h.m->fLifecycle.brightnessRestored);
  h.untilLife(SweetPanelFrame);assert(h.m->fLifecycleBrightnessTarget==0x678&&h.m->fPipelineGeneration==generation);
  assert(panelPackets.size()==tableCount+2&&panelPackets.back().payload==std::vector<uint8_t>({0x51,6,0x78})&&!panelPackets.back().lowPower);
  secondPixels(h);
  // Explicit transport selection, not a payload heuristic. The exact same
  // 51 bytes sent via the generic DCS API must retain its LP contract.
  auto saved=regs[bases[6]+0x3c];
  assert(h.m->setBrightness(0x789)==0&&!panelPackets.back().lowPower&&regs[bases[6]+0x3c]==saved);
  const uint32_t words[]={0xc0390003,0xff890751};
  assert(h.m->dcsWrite(words,2)==0&&panelPackets.back().lowPower&&regs[bases[6]+0x3c]==saved);
  assert(h.m->fDsiDisplayEngine&&!h.m->fDsiTransferEngine&&!h.m->fDsiPinned);
 }
 // A target received after a brightness completion still needs a real frame
 // before another command. It must reuse the same pipeline generation.
 {Harness h;startWake(h);firstPixels(h);h.untilLife(SweetPanelFrame);auto generation=h.m->fPipelineGeneration;
  assert(h.m->setBrightness(0x987)==kIOReturnBusy);firstPixels(h);h.untilLife(SweetPanelFrame);
  assert(h.m->fLifecycleBrightnessTarget==0x987&&h.m->fPipelineGeneration==generation);secondPixels(h);}
 // Execute the actual Apple backlight deferred update and property-interest
 // replay across OFF/ON, including an update during touch resume.
 {Harness h;ready(h);auto*bl=new AppleARMBacklight();assert(bl->start(h.d));
  SweetDisplayClient::matched(bl->fBackendClient,nullptr,h.m,nullptr);pump();
  auto target=[&](unsigned level){auto*d=OSDictionary::withCapacity(1);auto*n=OSNumber::withNumber(level,32);
   d->setObject("brightness",n);n->release();assert(bl->setProperties(d)==0);d->release();
   AppleARMBacklight::step(bl,bl->fTimer);assert(bl->fAwaitingBackend);};
  h.power(SweetPanelDisplayOff);target(17);target(23);h.untilLife(SweetPanelComplete);
  h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelFrame);firstPixels(h);h.untilLife(SweetPanelFrame);
  assert(h.m->fLifecycleBrightnessTarget==23);
  regs[bases[3]+0x18]=regs[bases[3]+0x1c]=0;regs[0x0ae01014]|=0x100;realFrame(h.m);h.life();
  assert(h.m->fLifecycle.phase==SweetPanelTouchResume);target(37);h.untilLife(SweetPanelComplete);
  pump();AppleARMBacklight::step(bl,bl->fTimer);assert(!bl->fAwaitingBackend&&bl->fLevel==37&&!panelPackets.back().lowPower);
  bl->stop(h.d);bl->release();}
 // Fresh pixels must precede the first restore, including when PP_DONE was
 // already set at ON completion. First-frame timeout emits no typed packet.
 {Harness h;startWake(h);auto count=panelPackets.size();regs[0x0ae01014]|=0x100;
  regs[bases[3]+0x18]=0;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
  fakeTicks+=100000001;h.life();supplyFailed(h);assert(panelPackets.size()==count&&!h.m->fLifecycle.brightnessRestored);}
 // Drain requires physical GIC completion, DSI idle, empty CTL and two idle
 // line samples. No brightness transfer can escape any of these held fences.
 for(unsigned fence=0;fence<6;++fence){Harness h;startWake(h);firstPixels(h);auto count=panelPackets.size();
  if(fence==0)regs[bases[11]]|=1u<<31;
  if(fence==1)regs[bases[11]+0x30c]|=1u<<19;
  if(fence==2)regs[bases[6]+8]|=6;
  if(fence==3)regs[bases[3]+0x18]=1;
  if(fence==4)regs[bases[4]+0x2b0]=100;
  if(fence==5)regs[bases[3]+0x1c]=1;
  for(unsigned n=0;n<3;++n)h.life();assert(panelPackets.size()==count);
  assert(h.m->fLifecycle.phase==SweetPanelBrightnessDrain || (fence==5&&h.m->fLifecycle.phase==SweetPanelFault));
  fakeTicks+=50000001;h.life();supplyFailed(h);assert(panelPackets.size()==count&&!h.m->fLifecycle.brightnessRestored);
 }
 // First frame, drain, quiescing command, in-flight FIFO, and second frame:
 // cancel/stop/provider/deadline/resource loss all close the same generation.
 for(unsigned point=0;point<5;++point)for(unsigned fault=0;fault<5;++fault){Harness h;startWake(h);
  if(point){firstPixels(h);if(point>=2)brightnessAdmission(h);}
  if(point>=3)while(h.m->fDsiStatus.state!=SweetDSITransferring)h.step();
  if(point==4)h.untilLife(SweetPanelFrame);
  bool inFlight=h.m->fDsiStatus.state==SweetDSITransferring;
  auto count=panelPackets.size();
  if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==1)h.m->stop(h.d);
  if(fault==2){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);pump();}
  if(fault==3){fakeTicks=h.m->fLifecycleStepDeadline+1;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.life();}
  if(fault==4){h.m->fClockFault=true;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.life();}
  supplyFailed(h);assert(panelPackets.size()==count&&h.m->fDsiPoisoned&&h.pool->pinned&&!h.m->fDsiPinned);
  if(inFlight)assert(h.m->fDsiTransferEngine);
  auto writes=supplyWrites.size();fakeTicks+=1000000000;quiet(h);assert(supplyWrites.size()==writes);
 }
 // Missing DONE, busy-after-DONE, ACK, IRQ error and stale DONE before trigger
 // never mark brightness restored or rearm the stream after an uncertain send.
 for(unsigned fault=0;fault<5;++fault){Harness h;startWake(h);firstPixels(h);brightnessAdmission(h);
  if(fault==4)commandFault=4;
  else {while(h.m->fDsiStatus.state!=SweetDSITransferring)h.step();
   if(fault==0){dmaDone=false;regs[bases[6]+0x110]&=~1u;}
   if(fault==1)regs[bases[6]+8]=6;
   if(fault==2)regs[bases[6]+0x68]=4;
   if(fault==3)lateError=true;
  }
  supplyFailed(h);assert(!h.m->fLifecycle.brightnessRestored&&h.m->fIrqState!=SweetMDSS::IrqArmed&&h.m->fDsiTransferEngine);
 }
 puts("PASS: post-frame brightness actual driver; four profiles, latest target before/during DMA, fixed deadline, typed HS/generic and table LP, second fresh frame before touch, six drain fences, 25 cancellation/stop/provider/deadline/resource faults, five command-completion faults; pins/references held and no late writes");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-brightness-') as temp:
 temp=Path(temp);flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 worker=temp/'worker.c';worker.write_text(ns['ns']['actual_worker'](ns['ns']['root']));obj=temp/'worker.o'
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(worker),'-o',str(obj)],check=True)
 fixture=ns['ns'];actual=fixture['actual'];cpp=temp/'test.cpp'
 cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+fixture['prelude']+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+fixture['touch']+fixture['helpers']+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
