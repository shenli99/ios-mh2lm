#!/usr/bin/env python3
"""Execute actual mode7 MDSS/TouchComm with independent CTL/MMIO completion.
Usage: test-mh2lm-panel-pipeline.py EXTRACTED_XNU
"""
from pathlib import Path
import json, os, subprocess, sys, tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-panel-lineage-wake.py'
ns={'__file__':str(fixture),'__name__':'pipeline_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
root=ns['root'];prelude=ns['prelude'];actual=ns['actual'];helpers=ns['helpers']
touch=ns['touch'].replace('lifecycleMode=2;', 'lifecycleMode=7;ctlResetCount=0;')
# Config loss is a test fault, not production writes or a claimed real reset
# footprint. Only programmed configuration is lost; shared SMMU/QoS/colour,
# DMA status and independent IRQ counters are not emulated by configuration.
prelude=prelude.replace('static void writeReg(uintptr_t b',r'''
static unsigned ctlResetCount=0;
static void erasePipeline(){
 for(unsigned o:{0u,8u,0xcu,0x10u,0x14u,0x18u,0x1cu,0x20u,0x24u,0x28u,0x30u,0x34u,0x38u,0x70u,0xb4u,0x138u,0x170u,0xa04u})regs[bases[2]+o]=0;
 for(unsigned n=0;n<3;++n)for(unsigned o:{0x100u,0x104u,0x108u})regs[bases[2]+o+n*0x10]=0;
 for(unsigned n=0;n<6;++n){regs[bases[3]+(n==5?0x24:n*4)]=0;for(unsigned o:{0x40u,0x70u,0xa0u})regs[bases[3]+o+n*4]=0;}
 for(unsigned o:{0x14u,0xe4u,0xe8u,0xecu,0xf0u,0xf4u,0xf8u,0x134u})regs[bases[3]+o]=0;
 for(unsigned o:{0x25cu,0x280u,0x284u,0x288u,0x28cu,0x290u,0x294u,0x29cu,0x2a0u,0x2a4u})regs[bases[4]+o]=0;
 regs[bases[16]]=regs[bases[16]+4]=regs[bases[17]+0xa0]=regs[bases[17]+0xc8]=regs[bases[18]]=0;
 for(unsigned n=0;n<11;++n)regs[bases[16]+0x20+n*0x18]=0;
}
static void writeReg(uintptr_t b''',1)
prelude=prelude.replace('supplyWrites.push_back({fakeTicks,b+o,v});','''supplyWrites.push_back({fakeTicks,b+o,v});
 if(b==bases[3]&&o==0x30&&v==1){++ctlResetCount;}''')
helpers=helpers.replace('static IOService*displayNub(){auto*n=nub(false);','''static IOService*displayNub(){auto*n=nub(false);
 uint32_t pipelineABI=1;n->properties["sweet,display-pipeline-abi"]=OSData::withBytes(&pipelineABI,4);''')
tests=ns['tests'].split('int main(){')[0]+r'''
static void configured(Harness&h){
 assert(regs[bases[2]]==0x09240438&&regs[bases[2]+0xc]==0x09240438);
 assert(regs[bases[2]+8]==0x00300016&&regs[bases[2]+0x24]==4608&&regs[bases[2]+0x14]==h.pool->physical(0));
 assert(regs[bases[2]+0x30]==0x237ff&&regs[bases[2]+0x34]==0x03020001&&regs[bases[2]+0x38]==0x80000000);
 assert(regs[bases[2]+0x108]==0x09240438&&!regs[bases[2]+0xa04]&&!(regs[bases[18]]&3));
 assert(regs[bases[16]+4]==0x09240438&&!regs[bases[16]]);
 assert(regs[bases[3]]==1&&regs[bases[3]+0x14]==0x20000&&regs[bases[3]+0xf4]==2);
 assert(!regs[bases[17]+0xa0]&&!(regs[bases[17]+0xc8]&(1u<<18))&&!(regs[bases[4]+0x25c]&15));
 assert(regs[bases[4]+0x280]==0&&regs[bases[4]+0x284]==1&&regs[bases[4]+0x288]==0x180087);
 assert(regs[bases[4]+0x28c]==0xfff0&&regs[bases[4]+0x294]==2340&&regs[bases[4]+0x2a4]==2341);
 assert(regs[bases[4]+0x2a0]==2340&&regs[bases[4]+0x29c]==0x40004&&regs[bases[4]+0x290]==2345);
 assert(h.m->fPipelineConfigured&&h.m->fLifecycle.pipelineGeneration==h.m->fPipelineGeneration);
}
static void untilConfiguration(Harness&h,bool wake){
 if(wake){supplyPark(h);erasePipeline();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPipelineRestore);}
 else {h.begin();for(unsigned n=0;n<12000&&!h.m->fPipelinePending&&h.m->fLifecycle.phase!=SweetPanelFault;++n)h.step();}
 assert(h.m->fPipelinePending&&!h.m->fPipelineConfigured&&h.pool->pinned);
 assert(!h.m->clockCanGate());auto writesBefore=supplyWrites.size();
 assert(h.m->releaseLocalClocks(h.m->fClockBootToken)==kIOReturnBusy&&supplyWrites.size()==writesBefore);
}
int main(){
 clockULPSFaults();
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;ready(h);configured(h);
  assert(h.m->fPipelineGeneration==1&&ctlResetCount==0&&(h.m->panelLifecycleCapabilities()&SweetPanelPipelineRebuild));
  auto boot=h.m->fClockBootToken,arc=h.m->fManagedARC;
  for(unsigned round=0;round<3;++round){
   supplyWrites.clear();supplyPark(h);checkHSOff();erasePipeline();h.m->fLifecycleTE=0xdeadbeef;h.m->fLifecycleAuto=0x81231234;
   panelPackets.clear();supplyWrites.clear();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPipelineRestore);
   assert(regs[bases[3]+0x30]==0&&!h.m->fPipelineConfigured&&h.m->fLifecycle.pipelineGeneration==0);
   assert(ctlResetCount==0&&!h.m->fLifecycle.brightnessRestored);
   h.untilLife(SweetPanelFrame);configured(h);checkPackets(profile,true);
   checkHSOn();
   auto source=findWrite(bases[2]+0x14,uint32_t(h.pool->physical(0)));
   auto sub=findWrite(bases[3]+0x110,2),flush=findWrite(bases[3]+0x18,0x80020041);
   auto autoRefresh=findWrite(bases[4]+0x2b4,0x80000001),te=findWrite(bases[4]+0x288,0x180087);
   assert(source<te&&te<sub&&sub<flush&&flush<autoRefresh);
   assert(h.m->fPipelineGeneration==round+2&&ctlResetCount==0&&h.m->fLifecycle.touchQuiesced);
   // A generation or register echoes do not prove a new transfer. Each of
   // fresh PP_DONE, empty flush and advancing RD_PTR is independently needed.
   h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
   regs[0x0ae01014]|=0x100;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
   regs[bases[3]+0x18]=0;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
   realFrame(h.m);regs[0x0ae01014]&=~0x100;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
   regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=1;h.life();assert(h.m->fLifecycle.phase==SweetPanelFrame);
   regs[bases[3]+0x18]=0;h.complete();checkPackets(profile,true,0x345);assert(h.t->fStatus.ready&&h.m->fLifecycle.pixelsComplete);
   assert(h.m->fPipelineGeneration==round+2);
   assert(h.m->fClockBootToken==boot&&h.m->fManagedARC==arc);
  }
 }
 // Both configuration continuations: deadline, cancelled transaction, service stop,
 // detached participant, SMMU/DSI/resource/IRQ failure. No late configuration and no automatic CTL reset recovery.
 for(bool wake:{false,true})for(unsigned fault=0;fault<10;++fault){Harness h;if(wake)ready(h);untilConfiguration(h,wake);
  auto generation=h.m->fPipelineGeneration;
  if(fault==0){fakeTicks+=100000001;}
  if(fault==1)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==2)h.m->stop(h.d);
  if(fault==3){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);pump();}
  if(fault==4)regs[bases[8]]=0;
  if(fault==5)regs[bases[6]+8]|=6;
  if(fault==6)h.m->fManagedRails&=~1u;
  if(fault==7)regs[bases[11]+0x30c]|=1u<<19;
  if(fault==9)regs[bases[3]+0x30]=1; // external reset already active, no programming
  if(fault==8)regs[bases[3]+0x200+0xf4]=2; // competing controller, no reset of it

  if(wake)h.life();else {h.scan();h.life();}
  assert(h.m->fLifecycle.phase==SweetPanelFault||h.m->fLifecycle.phase==SweetPanelStopped);
  assert(h.m->fPipelineGeneration==generation&&!h.m->fLifecycle.pixelsComplete);
  auto writesBefore=supplyWrites.size();fakeTicks+=10000000;quiet(h);
  assert(supplyWrites.size()==writesBefore&&h.pool->pinned);
 }
 // Parked configuration loss must not hide domain loss or permit a competing
 // owner. Configuration readbacks themselves remain diagnostics, not gates.
 {Harness h;ready(h);supplyPark(h);erasePipeline();regs[bases[7]+0xc0c]=3;
  uint64_t token=0;auto before=supplyWrites.size();assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&token)!=0&&!token&&supplyWrites.size()==before);}
 for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);supplyPark(h);h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelCommand);
  // Finish the real ON table before testing pipeline admission.
  for(unsigned n=0;n<1000&&h.m->dsiBusy();++n){fakeTicks+=1000000;physical(h.m);SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);}
  assert(!h.m->dsiBusy());auto before=ctlResetCount;
  if(fault==0)regs[bases[6]+8]|=6;if(fault==1)regs[bases[11]+0x30c]|=1u<<19;
  if(fault==2)regs[bases[3]+0x200+0xf4]=2;if(fault==3)regs[bases[3]+0x18]=1;
  h.life();assert(h.m->fLifecycle.phase==SweetPanelFault&&ctlResetCount==before);
 }
 // Explicit ABI and every narrow map are mandatory only for the new mode.
 for(unsigned fault=0;fault<10;++fault){warmFixture();coreFixture();hostFixture();linkFixture();touchSetup();auto*d=displayNub();
  if(fault==0){d->properties["sweet,display-pipeline-abi"]->release();d->properties.erase("sweet,display-pipeline-abi");}
  if(fault>=1&&fault<=3)missingMap=15+fault;if(fault>=4&&fault<=6)badMap=12+fault;if(fault>=7)shortMap=9+fault;
  auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  missingMap=badMap=shortMap=99;mdssClockWriter=mdssNoCWriter=0;
  assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 }
 puts("PASS: mode7 actual driver, four profiles x three erased-configuration wakes; no normal CTL reset, software geometry/TE, encoder TE-before-flush-before-kickoff, fresh pixel fences, both configuration continuations deadline/cancel/stop/provider/domain/DMA/resource/IRQ/conflict failures, ABI/maps unwind; shared SMMU/core retained");
}
'''
# Immutable oracle was generated by executing the selected, unmodified LG
# helpers. Compare all steady configuration results in addition to completion
# and sequencing assertions; no volatile flush/reset value is a steady config.
oracle=json.loads((here/'fixtures/mh2lm-pipeline-lineage.json').read_text())
reference_checks=''.join(f'assert(regs[{address}ULL]=={value}u);' for address,value in oracle['configured_registers'].items() if int(address) not in (0x0ae02018,0x0ae02110))
tests=tests.replace('static void configured(Harness&h){', 'static void configured(Harness&h){'+reference_checks)
with tempfile.TemporaryDirectory(prefix='mh2lm-pipeline-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True,timeout=90)
