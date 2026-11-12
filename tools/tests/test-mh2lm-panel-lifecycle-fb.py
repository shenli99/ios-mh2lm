#!/usr/bin/env python3
"""Actual framebuffer asynchronous lifecycle consumer; only backend/OS are fixtures.
Usage: test-mh2lm-panel-lifecycle-fb.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent;fixture=here/'test-mh2lm-framebuffer-service.py'
ns={'__file__':str(fixture),'__name__':'lifecycle_fb_fixture'}
exec(compile(fixture.read_text().split('with tempfile.TemporaryDirectory(')[0],str(fixture),'exec'),ns)
prelude=ns['prelude'].replace('unsigned panelLifecycleCapabilities(){return 0;}', '''
 unsigned cap=(1u<<SweetPanelDisplayOff)|(1u<<SweetPanelDisplayOn);
 uint64_t next=0;SweetPanelLifecycleStatus lifecycle{};IOReturn beginLife=0,queryLife=0;
 std::vector<unsigned>operations;std::vector<uint64_t>cancelled;
 unsigned panelLifecycleCapabilities(){return cap;}''')
prelude=prelude.replace('IOReturn beginPanelLifecycle(unsigned,uint64_t*){return kIOReturnUnsupported;}', '''IOReturn beginPanelLifecycle(unsigned op,uint64_t*token){
  *token=0;if(beginLife)return beginLife;lifecycle={};lifecycle.token=++next;
  lifecycle.operation=op;lifecycle.phase=SweetPanelTouchPause;lifecycle.result=kIOReturnNotReady;
  operations.push_back(op);*token=next;return 0;}''')
prelude=prelude.replace('IOReturn panelLifecycleStatus(uint64_t,SweetPanelLifecycleStatus*){return kIOReturnUnsupported;}', '''IOReturn panelLifecycleStatus(uint64_t token,SweetPanelLifecycleStatus*v){assert(token==lifecycle.token);*v=lifecycle;return queryLife;}''')
prelude=prelude.replace('void cancelPanelLifecycle(uint64_t){}','void cancelPanelLifecycle(uint64_t token){cancelled.push_back(token);}')
prelude=prelude.replace('static int serviceArg=1,', 'static int lifecycleArg=1,serviceArg=1,')
prelude=prelude.replace('else if(!strcmp(k,"sweetfbpreserveoff"))','else if(!strcmp(k,"sweetpanellifecycle"))v=lifecycleArg;\n else if(!strcmp(k,"sweetfbpreserveoff"))')
helpers=ns['tests'].split('int main(){')[0]
tests=r'''
struct Harness{
 IOService*p;AppleCLCD*f;IOMobileFramebufferUserClient*c;SweetDisplayBackend*b;
 Harness(){scanoutReset();failEvent=-1;realGeometry=true;ownArg=2;vsyncArg=1;irqArg=2;powerArg=1;fakeTicks=1000000000;
  p=new IOService;published=b=new SweetDisplayBackend;f=new AppleCLCD;assert(f->start(p)&&f->fPanelLifecycleRequested);
  f->fDisplayClient->fire();c=client(f);panelRequests.clear();}
 IOReturn request(bool off){uint64_t in[]={off?0ULL:1ULL};auto a=scalar(in,1,nullptr,0);return call(c,12,a);}
 void tick(){fakeTicks+=1000000;AppleCLCD::vsyncFired(f,f->fVsync);}
 void complete(){b->lifecycle.phase=SweetPanelComplete;b->lifecycle.result=0;
  b->lifecycle.resourcesHeld=b->lifecycle.displayDrained=b->lifecycle.pixelsComplete=true;
  b->lifecycle.touchQuiesced=b->lifecycle.operation==SweetPanelDisplayOff;tick();}
 ~Harness(){f->stop(p);c->stop(f);c->release();f->release();b->release();published=nullptr;p->release();
  assert(live==0&&rawBlocks==0&&scanoutBuffers.empty());}
};
int main(){
 {Harness h;assert(h.request(false)==0&&h.b->operations.empty());
  assert(h.request(true)==0&&h.f->fPanelLifecycleBackend==h.b&&h.f->fPowerOff);
  auto token=h.f->fPanelLifecycleToken;auto count=h.f->fVsyncCount;h.f->fDirty=true;
  h.tick();assert(h.f->fVsyncCount==count&&h.f->fDirty&&h.f->fPowerOff);
  h.b->sample(fakeTicks);h.f->fHardwareEvent->pump();assert(h.f->fVsyncCount==count&&!h.f->fHardwareVsyncActive);
  assert(h.request(true)==0&&h.request(false)==0&&h.b->operations.size()==1);
  h.complete();assert(h.b->operations==std::vector<unsigned>({SweetPanelDisplayOff,SweetPanelDisplayOn}));
  assert(h.f->fPowerOff&&h.f->fPanelLifecycleToken>token);
  h.tick();assert(h.f->fPowerOff);h.complete();assert(!h.f->fPowerOff&&!h.f->fPanelLifecycleBackend&&!h.f->fPanelLifecycleFault);
  assert(h.b->cancelled.empty()&&panelRequests.empty());
  assert(h.request(true)==0);h.complete();assert(h.f->fPowerOff&&!h.f->fPanelLifecycleBackend);
  assert(h.request(true)==0&&h.b->operations.size()==3); // already off
  assert(h.request(false)==0);h.complete();assert(!h.f->fPowerOff);
 }
 for(unsigned failure=0;failure<8;++failure){Harness h;
  if(failure==0)h.b->cap=0;if(failure==1)h.b->beginLife=kIOReturnBusy;
  auto rc=h.request(true);if(failure<2)assert(rc!=0);else{
   if(failure==2){h.b->lifecycle.phase=SweetPanelFault;h.b->lifecycle.result=kIOReturnIOError;}
   if(failure==3)h.b->queryLife=kIOReturnNotReady;
   if(failure==4){auto*other=new SweetDisplayBackend;published=other;h.f->fDisplayClient->fire();other->release();published=h.b;}
   if(failure==5)fakeTicks+=17000000000ULL;
   if(failure==6){h.b->lifecycle.phase=SweetPanelComplete;h.b->lifecycle.result=0;h.b->lifecycle.operation=SweetPanelDisplayOn;}
   if(failure==7){h.b->lifecycle.phase=SweetPanelComplete;h.b->lifecycle.result=0;} // missing actual fences
   h.tick();}
  assert(h.f->fPowerOff&&h.f->fPanelLifecycleFault&&!h.f->fPanelLifecycleBackend&&panelRequests.empty());
  auto frames=h.f->fVsyncCount;h.tick();assert(h.f->fVsyncCount==frames&&h.request(false)!=0);
  if(failure>=2)assert(h.b->cancelled.size()==1);
 }
 // Stop cancels exactly the retained token; a late timer never queries/advances.
 {Harness h;assert(h.request(true)==0);auto token=h.f->fPanelLifecycleToken;
  h.f->stop(h.p);auto frames=h.f->fVsyncCount;h.tick();assert(h.f->fVsyncCount==frames&&h.b->cancelled==std::vector<uint64_t>{token});}
 // Mode2 never accepts a warm-only backend, or generic Complete without the
 // matching profile/sleep/reset/brightness proofs. The actual FB checks run.
 {Harness h;h.f->fPanelLifecycleFull=true;
  assert(h.request(true)!=0&&h.b->operations.empty()&&h.f->fPanelLifecycleFault);}
 {Harness h;h.f->fPanelLifecycleFull=true;h.b->cap|=SweetPanelRetainedSleepReset;
  assert(h.request(true)==0);h.b->lifecycle.profile=4;h.b->lifecycle.sleeping=true;h.complete();
  assert(!h.f->fPanelLifecycleFault&&h.f->fPowerOff);
  assert(h.request(false)==0);h.b->lifecycle.profile=4;h.b->lifecycle.resetComplete=h.b->lifecycle.brightnessRestored=true;h.complete();
  assert(!h.f->fPanelLifecycleFault&&!h.f->fPowerOff);}
 for(unsigned missing=0;missing<4;++missing){Harness h;h.f->fPanelLifecycleFull=true;h.b->cap|=SweetPanelRetainedSleepReset;
  assert(h.request(true)==0);h.b->lifecycle.profile=4;h.b->lifecycle.sleeping=true;h.complete();
  assert(h.request(false)==0);h.b->lifecycle.profile=4;h.b->lifecycle.resetComplete=h.b->lifecycle.brightnessRestored=true;
  if(missing==0)h.b->lifecycle.profile=0;if(missing==1)h.b->lifecycle.resetComplete=false;
  if(missing==2)h.b->lifecycle.brightnessRestored=false;if(missing==3)h.b->lifecycle.sleeping=true;
  h.complete();assert(h.f->fPanelLifecycleFault&&h.f->fPowerOff);}

 // Boot mode3 selects the actual consumer path. Require explicit capability
 // and both retained/config ownership and data release/reacquire proofs.
 lifecycleArg=3;
 {Harness h;assert(h.f->fPanelLifecycleFull&&h.f->fPanelLifecycleDataParking);
  h.b->cap|=SweetPanelRetainedSleepReset;assert(h.request(true)!=0&&h.b->operations.empty());}
 {Harness h;h.b->cap|=SweetPanelRetainedSleepReset|SweetPanelDataParking;
  assert(h.request(true)==0);auto&v=h.b->lifecycle;v.profile=4;v.sleeping=true;
  v.retainedPower=v.configHeld=v.resourcesParked=true;v.configToken=6;
  v.phase=SweetPanelComplete;v.result=0;v.touchQuiesced=v.displayDrained=true;h.tick();
  assert(!h.f->fPanelLifecycleFault&&h.f->fPowerOff&&!h.f->fPanelLifecycleBackend);
  assert(h.request(false)==0);v.profile=4;v.retainedPower=v.configHeld=v.resourcesRestored=true;
  v.configToken=6;v.dataToken=7;v.clockToken=8;v.resetComplete=v.brightnessRestored=true;h.complete();
  assert(!h.f->fPanelLifecycleFault&&!h.f->fPowerOff);
 }
 for(unsigned missing=0;missing<9;++missing){Harness h;h.b->cap|=SweetPanelRetainedSleepReset|SweetPanelDataParking;
  assert(h.request(true)==0);auto&v=h.b->lifecycle;v.profile=4;v.sleeping=true;
  v.retainedPower=v.configHeld=v.resourcesParked=true;v.configToken=6;
  v.phase=SweetPanelComplete;v.result=0;v.touchQuiesced=v.displayDrained=true;
  if(missing<6){
   if(missing==0)v.retainedPower=false;if(missing==1)v.configHeld=false;
   if(missing==2)v.configToken=0;if(missing==3)v.resourcesParked=false;
   if(missing==4)v.resourcesHeld=true;if(missing==5)v.clockToken=1;
   h.tick();assert(h.f->fPanelLifecycleFault&&h.f->fPowerOff);continue;
  }
  h.tick();assert(h.request(false)==0);v.profile=4;v.retainedPower=v.configHeld=v.resourcesRestored=true;
  v.configToken=6;v.dataToken=7;v.clockToken=8;v.resetComplete=v.brightnessRestored=true;
  if(missing==6)v.resourcesRestored=false;if(missing==7)v.dataToken=0;if(missing==8)v.resourcesParked=true;
  h.complete();assert(h.f->fPanelLifecycleFault&&h.f->fPowerOff);
 }
 lifecycleArg=1;
 puts("PASS: actual FB asynchronous lifecycle requests/status, pending presentation freeze, rapid OFF/ON coalescing, no completion from frame mailbox, retained provider/token, explicit fault/withdrawal/stop, no legacy fallback and zero objects/allocations/buffers");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-lifecycle-fb-') as temp:
 cpp=Path(temp)/'test.cpp';cpp.write_text(prelude+ns['source']+helpers+tests);binary=Path(temp)/'test'
 subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ns['root']/'bsd/dev'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),timeout=60)
