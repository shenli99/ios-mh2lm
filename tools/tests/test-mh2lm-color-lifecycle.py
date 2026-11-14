#!/usr/bin/env python3
"""Actual mode2/7 SDR color restore: ordered wire traffic and failure fences.
Usage: test-mh2lm-color-lifecycle.py EXTRACTED_XNU [--pipeline]
"""
from pathlib import Path
import os, subprocess, sys, tempfile
if len(sys.argv) not in (2,3): sys.exit(__doc__)
here=Path(__file__).resolve().parent
fixture=here/'test-mh2lm-command-lifecycle.py'
ns={'__file__':str(fixture),'__name__':'brightness_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
tests=ns['tests'].split('int main(){')[0]+r"""
static void colorBegin(Harness&h){
 ready(h);assert(!h.m->fLifecycleColorRestored);supplyPark(h);panelPackets.clear();
 h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelCommand);
 for(unsigned n=0;n<1000&&h.m->dsiBusy();++n){fakeTicks+=1000000;physical(h.m);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
 assert(!h.m->dsiBusy());h.life();assert(h.m->fLifecycle.phase==SweetPanelColorRestore);
 assert(h.m->fDsiDisplayEngine&&!h.m->fLifecycleColorRestored&&!h.m->fLifecycle.pixelsComplete);
 assert(h.m->fLifecycle.touchQuiesced&&h.m->fLifecycle.resetComplete&&h.m->fIrqState==SweetMDSS::IrqIdle);
 assert(panelPackets.size()==expected(selectedProfile,true).size());
}
static void colorPoint(Harness&h,unsigned point){
 colorBegin(h);
 if(!point)return;
 for(unsigned n=0;n<4000;++n){
  auto state=h.m->fDsiStatus.state;
  if(point==1&&state==SweetDSITransferring&&!h.m->fDsiPinned)break;
  if(point==2&&state==SweetDSITransferring&&h.m->fDsiPinned)break;
  if(point==3&&state==SweetDSIDelay&&h.m->fDsiStatus.completed==2)break;
  h.step();assert(h.m->fLifecycle.phase==SweetPanelColorRestore);
 }
 assert(point==3 ? h.m->fDsiStatus.state==SweetDSIDelay : h.m->fDsiStatus.state==SweetDSITransferring);
}
int main(){
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;colorBegin(h);
  auto generation=h.m->fPipelineGeneration;auto*buffer=h.m->fDsiBuffer;auto allocations=dsiAllocations;
  assert(h.m->setBrightness(0x567)==kIOReturnBusy);
  while(h.m->fLifecycle.phase==SweetPanelColorRestore){
   assert(!h.m->fLifecycleColorRestored&&!h.m->fLifecycle.pixelsComplete&&!h.t->fStatus.ready);
   assert(h.m->fPipelineGeneration==generation);h.step();
  }
  h.untilLife(SweetPanelFrame);checkPackets(profile,true);
  assert(h.m->fLifecycleColorRestored&&!h.m->fLifecycle.brightnessRestored&&!h.m->fLifecycle.pixelsComplete);
  assert(h.m->fDsiBuffer==buffer&&dsiAllocations==allocations&&!h.m->fDsiPinned&&h.m->fDsiDisplayEngine);
  h.wakePixels();checkPackets(profile,true,0x567);assert(h.t->fStatus.ready);
 }
 selectedProfile=4; // PV has the real 20ms post-CE delay.
 // Before any packet, FIFO in flight, memory DMA in flight, and DDIC delay:
 // cancellation/stop/provider/deadline/resource loss cannot escape to pixels.
 for(unsigned point=0;point<4;++point)for(unsigned fault=0;fault<5;++fault){Harness h;colorPoint(h,point);
  auto count=panelPackets.size();bool pinned=h.m->fDsiPinned;
  if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==1)h.m->stop(h.d);
  if(fault==2){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);pump();}
  if(fault==3){fakeTicks=h.m->fLifecycleStepDeadline+1;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.life();}
  if(fault==4){h.m->fClockFault=true;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.life();}
  supplyFailed(h);assert(panelPackets.size()==count&&!h.m->fLifecycleColorRestored&&!h.m->fLifecycle.pixelsComplete);
  assert(h.m->fDsiDisplayEngine&&h.m->fDsiPinned==pinned&&h.pool->pinned);
  auto writes=supplyWrites.size();fakeTicks+=1000000000;quiet(h);assert(supplyWrites.size()==writes);
 }
 // Independent completion faults on both FIFO and physical DMA paths.
 for(unsigned point=1;point<=2;++point)for(unsigned fault=0;fault<4;++fault){Harness h;colorPoint(h,point);
  if(fault==0){dmaDone=false;regs[bases[6]+0x110]&=~1u;}
  if(fault==1)regs[bases[6]+8]=6;
  if(fault==2)regs[bases[6]+0x68]=4;
  if(fault==3)lateError=true;
  auto count=panelPackets.size();supplyFailed(h);
  assert(panelPackets.size()==count&&!h.m->fLifecycleColorRestored&&!h.m->fLifecycle.pixelsComplete&&h.m->fDsiTransferEngine);
  assert(h.m->fDsiPinned==(point==2));
 }
 // No stale-DONE admission; every child advance checks the new phase fences.
 {Harness h;colorBegin(h);commandFault=4;auto count=panelPackets.size();supplyFailed(h);
  assert(panelPackets.size()==count&&!h.m->fLifecycleColorRestored);}
 for(unsigned fault=0;fault<6;++fault){Harness h;colorBegin(h);auto count=panelPackets.size();
  if(fault==0)regs[bases[11]]|=1u<<31;
  if(fault==1)regs[bases[11]+0x30c]|=1u<<19;
  if(fault==2)regs[bases[4]+0x288]|=1u<<20;
  if(fault==3)regs[bases[4]+0x2b4]=0x80000001;
  if(fault==4)regs[bases[3]+0x18]=1;
  if(fault==5)regs[bases[3]+0x1c]=1;
  SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);supplyFailed(h);
  assert(panelPackets.size()==count&&!h.m->fLifecycleColorRestored&&!h.m->fLifecycle.pixelsComplete);
 }
 assert(dsiPrepares==dsiCompletes);
 puts("PASS: SDR Auto color actual driver four profiles; ON LP then six exact HS packets before first pixels, PV-only delay, latest brightness retained; 20 lifecycle faults, 8 FIFO/DMA completion faults, stale DONE and 6 child fences; no premature color/pixel/touch completion or late writes, uncertain DMA pin retained");
}
"""

with tempfile.TemporaryDirectory(prefix='mh2lm-color-') as temp:
 temp=Path(temp);flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 worker=temp/'worker.c';worker.write_text(ns['ns']['actual_worker'](ns['ns']['root']));obj=temp/'worker.o'
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(worker),'-o',str(obj)],check=True)
 fixture=ns['ns'];actual=fixture['actual'];cpp=temp/'test.cpp'
 cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+fixture['prelude']+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+fixture['touch']+fixture['helpers']+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
