#!/usr/bin/env python3
"""Actual mode2 Lineage PV/PP1/DV1/DV2 suspend ULPS and owned scanout.
Independent elapsed-time electrical/status peers, wire tables, real C TCM.
Usage: test-mh2lm-panel-lineage-wake.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-supply-lifecycle.py'
ns={'__file__':str(fixture),'__name__':'lineage_wake_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers']
touch=ns['touch'].replace('lifecycleMode=6;', 'lifecycleMode=2;')
# Independently enforce the hardware command engine prerequisite at every
# actual wire trigger, including synchronous brightness and FIFO commands.
prelude=prelude.replace('if(b==bases[6]&&o==0x90){',
 'if(b==bases[6]&&o==0x90){assert((regs[b+4]&7u)==5u);assert(commandMemory);')
tests=ns['tests'].split('int main(){')[0]+r'''
static unsigned findWrite(uintptr_t address,uint32_t value,uint32_t mask=~0u,unsigned from=0){
 for(unsigned i=from;i<supplyWrites.size();++i){auto&w=supplyWrites[i];if(w.address==address&&(w.value&mask)==value)return i;}
 assert(!"expected physical write missing");return 0;
}
static void untilLink(Harness&h,unsigned phase){
 for(unsigned i=0;i<12000&&h.m->fLinkPhase!=phase&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step();
 if(h.m->fLinkPhase!=phase)for(auto&msg:messages)fputs(msg.c_str(),stderr);
 assert(h.m->fLinkPhase==phase);
}
// Expectations are also exercised against the pinned, unmodified LG clock
// manager/callback call chain by test-mh2lm-lineage-clock-reference.py.
static void checkHSOff(){
 auto host=findWrite(bases[6]+4,0,7),gating=findWrite(bases[1]+0x8000,0,0x2a0);
 auto intf=findWrite(bases[1]+0x202c,0,1),pixel=findWrite(bases[1]+0x2004,0,1),byte=findWrite(bases[1]+0x2028,0,1);
 auto global=findWrite(bases[12]+0x14,0,0x20),pll=findWrite(bases[12]+0x3c,0);
 auto fifo=findWrite(bases[12]+0x1c,0),mux=findWrite(bases[12]+0x528,0),bias=findWrite(bases[12]+0x24,0,0x20);
 auto ulps=findWrite(bases[12]+0xa4,0x1f),lprx=findWrite(bases[12]+0x214,0,~0u,ulps);
 auto mask=findWrite(bases[1]+0x8000,1,1),rail=commandIndex(0x40104);
 assert(host<gating&&gating<intf&&intf<pixel&&pixel<byte&&byte<global&&global<pll&&pll<fifo&&fifo<mux&&mux<bias&&bias<ulps&&ulps<lprx&&lprx<mask&&mask<rail);
 assert(supplyWrites[ulps].time>=supplyWrites[bias].time+250);
 assert(supplyWrites[lprx].time>=supplyWrites[ulps].time+100000);
}
static void checkHSOn(){
 auto byte=findWrite(bases[1]+0x2028,1,1),pixel=findWrite(bases[1]+0x2004,1,1),intf=findWrite(bases[1]+0x202c,1,1);
 auto fifo=findWrite(bases[12]+0x1c,1),exit=findWrite(bases[12]+0xa8,0x1f);
 auto gating=findWrite(bases[1]+0x8000,0x2a0,0x2a0);
 assert(byte<pixel&&pixel<intf&&intf<fifo&&fifo<exit&&exit<gating);
}
static void clockULPSFaults(){
 // Hold each data/clock lane non-idle AFTER HS has stopped. No ULPS request,
 // PHY power-off or rail withdrawal is allowed; a fresh idle sample recovers.
 for(unsigned lane=0;lane<5;++lane)for(bool timeout:{false,true}){
  Harness h;ready(h);supplyWrites.clear();h.power(SweetPanelDisplayOff);untilLink(h,SweetMDSS::LinkULPSIdle);
  assert(h.m->fLinkSuspendedMask==0x70&&!regs[bases[12]+0x3c]);
  regs[bases[12]+0x14c]&=~(1u<<lane);auto before=supplyWrites.size();
  auto poll=[&](){fakeTicks+=10000;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);};
  for(unsigned n=0;n<3;++n)poll();
  assert(h.m->fLinkPhase==SweetMDSS::LinkULPSIdle&&supplyWrites.size()==before);
  assert(!ulpsRequestAt&&regs[bases[12]+0xa0]==0x1f&&h.m->fManagedRails==15&&h.pool->pinned);
  assert(!h.m->localClocksReady()&&h.m->setBrightness(0x346)!=kIOReturnSuccess);
  uint64_t token=0;const uint8_t command[]={0x05,1,0,0,0,0,1,0x28};
  assert(sweet_dsi_sequence_valid(command,sizeof(command),nullptr));
  assert(h.m->retainLocalClocks(SweetClockDSI0,&token)==kIOReturnNotReady&&!token);
  assert(h.m->beginDSISequence(command,sizeof(command),&token)==kIOReturnBusy&&!token);
  assert(h.m->releaseLocalClocks(h.m->fClockBootToken)!=kIOReturnSuccess&&supplyWrites.size()==before);
  if(timeout){fakeTicks+=100001;poll();supplyFailed(h);assert(!ulpsRequestAt&&h.m->fManagedRails==15);}
  else {regs[bases[12]+0x14c]|=1u<<lane;poll();assert(h.m->fLinkPhase==SweetMDSS::LinkULPSEntry);h.untilLife(SweetPanelComplete);checkHSOff();}
 }
 // A branch that never halts must prevent PLL/ULPS/PHY/rail shutdown.
 {Harness h;ready(h);supplyWrites.clear();h.power(SweetPanelDisplayOff);untilLink(h,SweetMDSS::LinkLaneIdle);
  physicalHalt=false;supplyFailed(h);assert(!ulpsRequestAt&&regs[bases[12]+0x3c]==1&&h.m->fManagedRails==15);}
 const unsigned down[]={SweetMDSS::LinkGatesOff,SweetMDSS::LinkPLLStop,SweetMDSS::LinkBiasOff,SweetMDSS::LinkULPSIdle,SweetMDSS::LinkULPSEntry,SweetMDSS::LinkPHYOff};
 for(unsigned phase:down)for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);h.power(SweetPanelDisplayOff);untilLink(h,phase);
  if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==1)h.m->stop(h.d);
  if(fault==2){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);pump();}
  supplyFailed(h);assert(h.m->fManagedRails==15);
 }
}
int main(){
 clockULPSFaults();
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;ready(h);
  assert(h.m->fLifecycleLineage&&h.m->fLifecycleSupplyCycle&&h.m->fLifecycleDataParking&&h.m->fLifecycleLinkCycle);
  auto boot=h.m->fClockBootToken,arc=h.m->fManagedARC;auto*buffer=h.m->fDsiBuffer;auto allocations=dsiAllocations;assert(buffer&&h.m->fDsiPrepared);
  for(unsigned round=0;round<3;++round){
   supplyWrites.clear();pinEdges.clear();auto phy=phyResets,host=hostResets;
   supplyPark(h);assert(phyResets==phy&&hostResets==host);
   assert(regs[bases[4]+0x284]==0&&!h.m->fDsiDisplayEngine&&!h.m->fDsiTransferEngine);
   unsigned teOff=findWrite(bases[4]+0x284,0),engineOff=findWrite(bases[6]+4,1,7),firstCommand=findWrite(bases[6]+0x90,1);
   assert(teOff<engineOff&&engineOff<firstCommand);
   unsigned suspend=findWrite(0x03d06000,0x201,0x3ff);
   unsigned resetLow=findWrite(0x03d06004,0);
   unsigned hostOff=findWrite(bases[6]+4,0,7);
   unsigned ulps=findWrite(bases[12]+0xa4,0x1f);
   unsigned lprx=findWrite(bases[12]+0x214,0,~0u,ulps);
   unsigned gates=findWrite(bases[1]+0x2004,0,1);
   unsigned mask=findWrite(bases[1]+0x8000,1,1);
   unsigned rail=commandIndex(0x40104);
   assert(suspend<resetLow&&resetLow<hostOff&&hostOff<gates&&gates<ulps&&ulps<lprx&&lprx<mask&&mask<rail);
   checkHSOff();
   assert(supplyWrites[hostOff].time>=supplyWrites[resetLow].time+5000000);
   assert(supplyWrites[lprx].time>=supplyWrites[ulps].time+100000);
   assert(supplyWrites[suspend].time>=panelPackets.back().time+155000000);
   assert(regs[bases[1]+0x8000]&1); // v2.3 false => reset masked
   panelPackets.clear();supplyWrites.clear();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPinReset);
   assert(phyResets==phy&&hostResets==host+1&&panelPackets.empty());
   assert(regs[bases[6]+0x58]==0x0ca90039&&regs[bases[6]+0x5c]==0x09240438&&regs[bases[6]+0xb0]==0x8421);
   assert(regs[bases[6]+4]==0x1f1&&regs[bases[6]+0x11c]==0x23f);
   unsigned hostSetup=findWrite(bases[6]+0xb0,0x8421);
   unsigned phyOn=findWrite(bases[12]+0x24,0x60);
   unsigned reentry=findWrite(bases[12]+0xa4,0x1f);
   unsigned unmask=findWrite(bases[1]+0x8000,0,1);
   unsigned pllStart=findWrite(bases[12]+0x3c,1);
   unsigned gateOn=findWrite(bases[1]+0x2028,1,1);
   unsigned fifo=findWrite(bases[12]+0x1c,1);
   unsigned exit=findWrite(bases[12]+0xa8,0x1f);
   unsigned exitClear=findWrite(bases[12]+0xa8,0,~0u,exit+1);
   unsigned stop=findWrite(bases[12]+0xac,0x1f);
   unsigned soft=findWrite(bases[6]+0x118,1);
   checkHSOn();
   assert(hostSetup<phyOn&&phyOn<reentry&&reentry<unmask&&unmask<pllStart&&pllStart<gateOn&&gateOn<fifo&&fifo<exit&&exit<exitClear&&exitClear<stop&&stop<soft);
   assert(supplyWrites[unmask].time>=supplyWrites[reentry].time+100000);
   assert(supplyWrites[exitClear].time>=supplyWrites[exit].time+1000000);
   assert(supplyWrites[soft].time>=supplyWrites[stop].time+100000);
   for(auto&w:supplyWrites)assert(w.address!=bases[6]+0x12c); // no cold PHY reset
   h.untilLife(SweetPanelFrame);checkPackets(profile,true,0x345);
   unsigned source=findWrite(bases[2]+0x14,uint32_t(h.pool->physical(0)));
   unsigned flush=findWrite(bases[3]+0x18,1);
   unsigned te=findWrite(bases[4]+0x288,h.m->fLifecycleTE);
   unsigned autoRefresh=findWrite(bases[4]+0x2b4,h.m->fLifecycleAuto);
   assert(source<te&&te<flush&&flush<autoRefresh);
   for(auto&w:supplyWrites)assert(w.address!=bases[3]+0x1c); // autorefresh kickoff
   assert(regs[bases[3]+0x18]==1&&h.m->fLifecycle.touchQuiesced&&h.pool->pinned);
   // New PP done + advancing RD_PTR cannot bypass a still-pending flush.
   regs[0x0ae01014]|=0x100;realFrame(h.m);h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
   regs[bases[3]+0x18]=0;h.complete();assert(h.m->fLifecycle.pixelsComplete&&h.t->fStatus.ready);
   assert(h.m->fClockBootToken==boot&&h.m->fManagedARC==arc&&h.m->fManagedRails==15);
   assert(h.m->fDsiBuffer==buffer&&dsiAllocations==allocations&&h.m->fDsiDisplayEngine&&!h.m->fDsiTransferEngine&&regs[bases[4]+0x284]==1);
  }
 }
 selectedProfile=4;
 // Frame count, not merely the enable bit, selects autorefresh vs CTL_START.
 for(uint32_t refresh:{0u,0x80000000u}){Harness h;ready(h);supplyPark(h);panelPackets.clear();supplyWrites.clear();
  h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPinReset);h.m->fLifecycleAuto=refresh;
  h.untilLife(SweetPanelFrame);assert(h.m->fLifecycleAuto==0&&regs[bases[3]+0x1c]==1);
  findWrite(bases[3]+0x1c,1);regs[bases[3]+0x1c]=0;regs[bases[3]+0x18]=0;
  regs[0x0ae01014]|=0x100;realFrame(h.m);h.complete();assert(h.m->fLifecycle.pixelsComplete);
 }
 // Real ULPS entry/reentry/exit, REFGEN/PLL/UPDATE/branch responses still gate.
 for(unsigned fault=0;fault<8;++fault){Harness h;ready(h);panelPackets.clear();
  if(fault==0){ulpsEnterOK=false;h.power(SweetPanelDisplayOff);supplyFailed(h);assert(h.m->fManagedRails==15);continue;}
  supplyPark(h);panelPackets.clear();h.power(SweetPanelDisplayOn);
  if(fault==1)ulpsEnterOK=false;if(fault==2)ulpsExitOK=false;
  if(fault==3){physicalRefgen=false;regs[bases[12]+0x140]=0;}
  if(fault==4)physicalRelock=false;if(fault==5)physicalRCG=false;
  if(fault==6)physicalRun=false;if(fault==7)hostResetError=true;
  supplyFailed(h);assert(panelPackets.empty()&&!h.m->fLifecycle.resetComplete);
 }
 // Every added async wait must stop without subsequent writes after cancellation,
 // owner stop or leaf withdrawal; no automatic retry or late reset release.
 const unsigned down[]={SweetMDSS::LinkULPSEntry};
 const unsigned up[]={SweetMDSS::LinkULPSReentry,SweetMDSS::LinkULPSExit,SweetMDSS::LinkULPSStop,SweetMDSS::LinkResumeComplete};
 for(bool restore:{false,true})for(unsigned phase:restore?std::vector<unsigned>(std::begin(up),std::end(up)):std::vector<unsigned>(std::begin(down),std::end(down)))
  for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);if(restore)supplyPark(h);
   panelPackets.clear();h.power(restore?SweetPanelDisplayOn:SweetPanelDisplayOff);untilLink(h,phase);
   if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
   if(fault==1)h.m->stop(h.d);
   if(fault==2){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);pump();}
   supplyFailed(h);if(restore)assert(panelPackets.empty());
  }
 // Input/latch/ordinary IRQ snapshot differences remain observable at the live
 // supply fence and never masquerade as eGPIO/IRQ ownership failures.
 {Harness h;ready(h);supplyPark(h);regs[0x03d06004]=3;regs[0x03908008]^=0x10;followPins=false;
  SweetPanelPinStatus p{};assert(h.p->copyLowStatus(h.m->fPanelPinsConsumer,h.m->fLifecycle.pinToken,&p)==0);
  assert(!p.resetHigh&&p.resetKnown&&regs[0x03d06004]==3&&h.p->fDiagnosticCount);
  h.power(SweetPanelDisplayOn);h.wakePixels();assert(h.m->fLifecycle.resetComplete&&h.m->fLifecycle.pixelsComplete);
 }
 puts("PASS: mode2 four Lineage profiles x three cycles; HS-before-ULPS and branch order, post-HS five-lane delay/timeout, rejected DSI/clock requests, branch halt failure and shutdown waits cancel/stop/provider loss; independent ULPS timing/status, reset-mask polarity, host setup/soft reset, no cold PHY pulse, panel-only supply requests, exact unchanged FIFO/DMA wire tables, owned source/flush/TE/autorefresh and fresh pixels, real completion faults, diagnostic GPIO echoes and zero objects after reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-lineage-wake-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
