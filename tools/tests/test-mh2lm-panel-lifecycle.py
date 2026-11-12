#!/usr/bin/env python3
"""Actual MDSS lifecycle+resources/link/host/IRQ and real C TouchComm worker.
Only OS scheduling, MMIO and independent TCM wire responses are substituted.
Usage: test-mh2lm-panel-lifecycle.py EXTRACTED_XNU
"""
from pathlib import Path
import os, subprocess, sys, tempfile
from mh2lm_touch_panel_fixtures import actual_worker
if len(sys.argv)!=2:sys.exit(__doc__)
root=Path(sys.argv[1]).resolve();here=Path(__file__).resolve().parent
fixture=here/'test-mh2lm-dsi-link.py';ns={'__file__':str(fixture),'__name__':'lifecycle_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
tf=here/'test-mh2lm-panel-touch.py';tns={'__file__':str(tf),'__name__':'touch_fixture'}
exec(compile(tf.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(tf),'exec'),tns)
prelude=ns['prelude'].replace('static int hostMode=1,linkMode=2;', 'static int hostMode=1,linkMode=2,lifecycleMode=1;')
prelude=prelude.replace(' if(!strcmp(k,"sweettouchpanel"))', ' if(!strcmp(k,"sweetpanellifecycle")){*(int*)v=lifecycleMode;return true;}\n if(!strcmp(k,"sweetdsi")){*(int*)v=1;return true;}\n if(!strcmp(k,"sweettouchpanel"))')
# Model W1C DMA_DONE independently of the production polling code. Host reset
# and physical PLL/root status destruction remain the existing real fixtures.
prelude=prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){','''static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(b==bases[6]&&o==0x110&&(v&1))dmaFired=false;
''')
touch=tns['fixture'].split('static void fixtures(){')[0]
touch+=r'''
static void touchSetup(){
 tcFixtureReboot();wire.clear();commands.clear();hidIDs.clear();
 busReads=busWrites=ups=blocks=clears=wakes=0;firmwareFault=0;sleepScale=1;sleepUntil=0;
 dropMask=keepEnabled=keepPending=false;waitHook=busHook=armHook=nullptr;
 regs[TG+0x11c]=TB;regs[TG+0x21c]=TB;regs[TG+0x31c]=0;
 regs[TP+8]=0x71;regs[TP+4]=1;regs[TP+12]=1;
 touchPanelMode=2;panelPinsMode=0;lifecycleMode=1;
}
static IOService*touchNub(){auto*n=new IOService;uint32_t one=1;n->properties["sweet,touch-panel-abi"]=OSData::withBytes(&one,4);return n;}
'''
helpers=ns['helpers']
helpers=helpers.replace('IOService *n,*d;SweetRPMh*r;', 'IOService *n,*d,*tn;SweetTouchPanel*t;SweetRPMh*r;')
helpers=helpers.replace('linkFixture();n=nub();','linkFixture();touchSetup();tn=touchNub();t=new SweetTouchPanel();assert(t->start(tn));n=nub();')
helpers=helpers.replace('m=displayMake(d,r);','m=displayMake(d,r);SweetTouchPanelClient::matched(m->fTouchPanelClient,nullptr,t,nullptr);pump();assert(m->fTouchPanel==t);')
helpers=helpers.replace('physical(m);linkPhysical(m);{','''physical(m);linkPhysical(m);if(t->fStatus.state==SWEET_TOUCH_RESUME_REQUESTED&&wire.empty())identify();tcFixtureStep();
  {std::lock_guard<std::recursive_mutex>l(t->fLoop->gate);SweetTouchPanel::timerAction(t,t->fTimer);}
  {std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);SweetMDSS::lifecycleTimerAction(m,m->fLifecycleTimer);}
  {''')
helpers=helpers.replace('void begin(){assert(m->beginScanout(pool,channel)==0);step();step();assert(m->fScanoutState==SweetScanoutResources&&!pool->pinned);}', '''void begin(){assert(m->beginScanout(pool,channel)==0);}
 void life(){std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);SweetMDSS::lifecycleTimerAction(m,m->fLifecycleTimer);}
 void untilLife(unsigned phase){for(unsigned i=0;i<12000&&m->fLifecycle.phase!=phase&&m->fLifecycle.phase!=SweetPanelFault;++i)step();
  if(m->fLifecycle.phase!=phase)for(auto&msg:messages)fputs(msg.c_str(),stderr);
  assert(m->fLifecycle.phase==phase);}
 void complete(){untilLife(SweetPanelTouchResume);step();assert(m->fLifecycle.phase==SweetPanelComplete);}
 void takeover(){begin();for(unsigned i=0;i<12000&&m->fScanoutState!=SweetScanoutSubmitted&&m->fLifecycle.phase!=SweetPanelFault;++i)step();
  if(m->fScanoutState!=SweetScanoutSubmitted)for(auto&msg:messages)fputs(msg.c_str(),stderr);
  assert(m->fScanoutState==SweetScanoutSubmitted);regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=0;realFrame(m);scan();complete();}
 uint64_t power(unsigned op){uint64_t token=0;assert(m->beginPanelLifecycle(op,&token)==0&&token);return token;}
 void wakePixels(){untilLife(SweetPanelFrame);regs[0x0ae01014]|=0x100;realFrame(m);complete();}''')
helpers=helpers.replace('if(pinned)pool->release(); // simulated reboot', 'if(pinned)pool->release();t->stop(tn);t->release();dropNub(tn); // simulated reboot')
actual=ns['actual']
tests=r'''
static SweetPanelLifecycleStatus state(Harness&h,uint64_t token=0){SweetPanelLifecycleStatus v{};assert(h.m->panelLifecycleStatus(token?token:h.m->fLifecycle.token,&v)==0);return v;}
static void fault(Harness&h){for(unsigned i=0;i<12000&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step();assert(h.m->fLifecycle.phase==SweetPanelFault&&h.m->fLifecycle.uncertain);
 auto before=trace.size();h.life();h.scan();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);assert(trace.size()==before);
 uint64_t rejected=0;assert(h.m->beginPanelLifecycle(SweetPanelDisplayOff,&rejected)!=0&&!rejected);}
int main(){
 // Invalid opt-in combinations unwind before touch operations or hardware writes.
 for(unsigned f=0;f<4;++f){warmFixture();coreFixture();hostFixture();linkFixture();touchSetup();
  if(f==0)lifecycleMode=2;if(f==1)touchPanelMode=1;if(f==2)ownMode=1;if(f==3)managedMode=0;
  auto*d=displayNub();auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());mdssClockWriter=mdssNoCWriter=0;}
 {Harness h;
  assert(h.m->panelLifecycleCapabilities()==((1u<<SweetPanelTakeover)|(1u<<SweetPanelDisplayOn)|(1u<<SweetPanelDisplayOff)));
  for(unsigned op:{unsigned(SweetPanelPrepare),unsigned(SweetPanelReset),unsigned(SweetPanelUnprepare),99u}){
   auto before=trace.size();uint64_t token=99;assert(h.m->beginPanelLifecycle(op,&token)==kIOReturnUnsupported&&!token&&trace.size()==before&&!(tcFixtureFrame()>>4));}
  h.begin();auto token=h.m->fLifecycle.token;
  assert(state(h).phase==SweetPanelTouchPause&&!writes()&&!sourceWritten());
  auto untouched=tcFixtureFrame();assert(h.t->cancelRequest(h.m->fTouchPanelConsumer+1,h.m->fLifecycle.touchToken)==kIOReturnNotFound);
  assert(h.t->cancelRequest(h.m->fTouchPanelConsumer,h.m->fLifecycle.touchToken+1)==kIOReturnNotFound&&tcFixtureFrame()==untouched);
  SweetScanoutStatus s{};assert(h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutCoordinating);
  uint64_t another=99;assert(h.m->beginPanelLifecycle(SweetPanelDisplayOff,&another)==kIOReturnBusy&&!another);
  assert(h.m->setPanelPower(false)==kIOReturnUnsupported&&h.m->brightnessReady()==kIOReturnBusy);
  uint32_t word=0x80050028;assert(h.m->dcsWrite(&word,1)==kIOReturnBusy);
  const uint8_t cmd[]={5,1,0,0,0,0,1,0x28};assert(h.m->beginDSISequence(cmd,sizeof(cmd),&another)==kIOReturnBusy);
  // Neither old GIC active nor RWP can be treated as an IRQ-drain fence.
  regs[TG+0x30c]=1u<<19;
  for(unsigned i=0;i<30;++i)h.step();
  assert(h.m->fScanoutState==SweetScanoutResources&&!h.m->fManagedStarted&&!sourceWritten());
  regs[TG+0x30c]=0;regs[TG]|=1u<<31;for(unsigned i=0;i<5;++i)h.step();assert(!h.m->fManagedStarted);
  regs[TG]&=~(1u<<31);
  for(unsigned i=0;i<12000&&h.m->fScanoutState!=SweetScanoutSubmitted;++i)h.step();
  assert(h.m->fScanoutState==SweetScanoutSubmitted&&h.pool->pinned);
  // Stale DONE / flush-cleared / RD_PTR alone never starts touch recovery.
  for(unsigned i=0;i<4;++i)h.step();assert(h.m->fLifecycle.phase==SweetPanelTakeoverPending&&commands.empty());
  regs[bases[3]+0x18]=0;realFrame(h.m);h.scan();h.life();assert(commands.empty()&&state(h).phase==SweetPanelTakeoverPending);
  regs[0x0ae01014]|=0x100;h.scan();h.life();assert(state(h).phase==SweetPanelTouchResume);
  assert(h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutCoordinating);
  h.step();assert(state(h).phase==SweetPanelComplete&&state(h).pixelsComplete&&state(h).displayDrained&&state(h).resourcesHeld);
  assert(h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutActive&&commands.size()==5);
  h.m->cancelPanelLifecycle(token-1);assert(state(h).phase==SweetPanelComplete);
  // Complete warm OFF/ON uses the same actual DSI engine, not legacy busy waits.
  auto off=h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);
  assert(state(h,off).touchQuiesced&&h.m->fLifecycleParked&&h.m->fPanelState==SweetMDSS::PanelOff);
  assert(h.m->fManagedRails==15&&h.m->fManagedARC&&h.m->fManagedClock&&h.m->fManagedBandwidth);
  auto accesses=trace.size();h.life();tcFixtureStep();assert(accesses==trace.size());
  auto on=h.power(SweetPanelDisplayOn);assert(on>off);
  SweetPanelLifecycleStatus old{};assert(h.m->panelLifecycleStatus(off,&old)==kIOReturnNotFound);
  h.untilLife(SweetPanelFrame);h.life();assert(state(h,on).phase==SweetPanelFrame);
  regs[0x0ae01014]|=0x100;h.life();assert(state(h,on).phase==SweetPanelFrame); // no RD_PTR yet
  realFrame(h.m);h.complete();assert(!h.m->fLifecycleParked&&!state(h).touchQuiesced&&h.m->fPanelState==SweetMDSS::PanelOn);
 }
 // Reproduce the actual startup ordering: native provider matched but its
 // TouchComm worker is still initializing. The framebuffer calls begin ONCE.
 {Harness h;tcFixtureOnline(0);h.begin();auto token=h.m->fLifecycle.token;
  assert(token&&!h.m->fLifecycle.touchToken&&!h.m->fScanoutUsed&&!writes());
  assert(state(h).phase==SweetPanelTouchPause);
  SweetScanoutStatus pending{};assert(h.m->scanoutStatus(h.pool,&pending)==0&&pending.state==SweetScanoutCoordinating);
  trace.clear();fakeTicks+=8000000000ULL; // beyond the OLD 6.5s pause budget
  for(unsigned i=0;i<4;++i)h.life();
  assert(state(h).phase==SweetPanelTouchPause&&!writes()&&trace.empty());
  assert(!h.m->fLifecycle.touchToken&&!h.m->fManagedStarted&&!h.pool->pinned&&!(tcFixtureFrame()>>4));
  tcFixtureOnline(1);h.life();auto touch=h.m->fLifecycle.touchToken;
  assert(touch&&h.m->fLifecycle.token==token&&h.m->fLifecycleDeadline>fakeTicks+15000000000ULL);
  assert(h.m->fLifecycleStepDeadline>fakeTicks+6000000000ULL);
  h.life();assert(h.m->fLifecycle.touchToken==touch); // no second quiesce request
  for(unsigned i=0;i<12000&&h.m->fScanoutState!=SweetScanoutSubmitted&&h.m->fLifecycle.phase!=SweetPanelFault;++i)h.step();
  assert(h.m->fScanoutState==SweetScanoutSubmitted&&h.pool->pinned&&h.m->fLifecycle.token==token);
  regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=0;realFrame(h.m);h.scan();h.complete();
  assert(state(h).phase==SweetPanelComplete);
 }
 // Bounded wait, cancellation, stop and participant withdrawal cannot start
 // a delayed takeover after the original generation has been invalidated.
 for(unsigned reason=0;reason<5;++reason){Harness h;tcFixtureOnline(0);h.begin();auto token=h.m->fLifecycle.token;
  assert(token&&!h.m->fLifecycle.touchToken&&!h.m->fManagedStarted&&!sourceWritten());
  if(reason==0){fakeTicks+=30000000001ULL;h.life();assert(state(h).result==kIOReturnTimeout);}
  if(reason==1)h.m->cancelPanelLifecycle(token);
  if(reason==2)h.m->stop(h.d);
  if(reason==3)SweetTouchPanelClient::terminated(h.m->fTouchPanelClient,nullptr,h.t,nullptr);
  if(reason==4)SweetRPMhClient::terminated(h.m->fRPMhClient,nullptr,h.r,nullptr);
  h.life();pump();assert(state(h).phase==(reason==2?SweetPanelStopped:SweetPanelFault));
  assert(!h.m->fLifecycle.touchToken&&!h.t->fTouched&&!h.t->fPoisoned&&!h.pool->pinned&&!sourceWritten());
  trace.clear();tcFixtureOnline(1);h.life();h.scan();assert(trace.empty()&&!(tcFixtureFrame()>>4));
  assert(!h.m->fManagedStarted&&h.m->fLifecycle.token==token);
 }
 // Native interest replays newest brightness after lifecycle, no busy-loop retry
 // or consumer gate callback from MDSS. The actual backlight and DCS code run.
 {Harness h;h.takeover();auto*bl=new AppleARMBacklight();assert(bl->start(h.d));
  SweetDisplayClient::matched(bl->fBackendClient,nullptr,h.m,nullptr);pump();
  h.power(SweetPanelDisplayOff);
  for(unsigned level:{17u,23u}){auto*d=OSDictionary::withCapacity(1);auto*n=OSNumber::withNumber(level,32);
   d->setObject("brightness",n);n->release();assert(bl->setProperties(d)==0);d->release();
   auto before=trace.size();AppleARMBacklight::step(bl,bl->fTimer);assert(bl->fAwaitingBackend&&trace.size()==before);}
  h.untilLife(SweetPanelComplete);pump();AppleARMBacklight::step(bl,bl->fTimer);assert(bl->fAwaitingBackend);
  h.power(SweetPanelDisplayOn);h.wakePixels();
  assert(bl->fAwaitingBackend);pump();AppleARMBacklight::step(bl,bl->fTimer);
  assert(!bl->fAwaitingBackend&&bl->fLevel==23);
  bl->stop(h.d);bl->release();}
 // Independent touch verification failure never delivers scanout Active.
 {Harness h;firmwareFault=3;h.begin();for(unsigned i=0;i<12000&&h.m->fScanoutState!=SweetScanoutSubmitted;++i)h.step();
  regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=0;realFrame(h.m);h.scan();fault(h);
  SweetScanoutStatus s{};assert(h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutFailed&&h.pool->pinned);}
 // Pause/active drain and end-to-end deadlines do not fall back or vote early.
 {Harness h;keepEnabled=true;h.begin();h.step();fault(h);assert(!sourceWritten()&&!h.m->fManagedStarted);}
 {Harness h;h.begin();fakeTicks+=16000000000ULL;h.life();fault(h);assert(!sourceWritten());}
 // Cancel during touch, resource acquisition, pixel wait, resume, command and parked OFF.
 for(unsigned point=0;point<7;++point){Harness h;
  if(point>=4){h.takeover();h.power(SweetPanelDisplayOff);if(point==4)h.untilLife(SweetPanelCommand);else h.untilLife(SweetPanelComplete);
   if(point==6){h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelCommand);}}
  else {h.begin();if(point==1)h.until(SweetMDSS::ManagedRails);
   if(point>=2){for(unsigned i=0;i<12000&&h.m->fScanoutState!=SweetScanoutSubmitted;++i)h.step();}
   if(point==3){regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=0;realFrame(h.m);h.scan();h.life();assert(state(h).phase==SweetPanelTouchResume);}}
  auto token=h.m->fLifecycle.token;h.m->cancelPanelLifecycle(token);fault(h);assert(h.t->fPoisoned&&!h.t->fSession);
  tcFixtureStep();assert(tcFixtureOwner()==~0ULL);
 }
 // Withdrawing notification invalidates before queued changed(), no new token adoption.
 for(unsigned which=0;which<2;++which){Harness h;h.begin();
  if(which==0)SweetTouchPanelClient::terminated(h.m->fTouchPanelClient,nullptr,h.t,nullptr);
  else SweetRPMhClient::terminated(h.m->fRPMhClient,nullptr,h.r,nullptr);
  h.life();fault(h);pump();assert(h.m->fLifecycle.phase==SweetPanelFault&&!sourceWritten());}
 // A parked worker losing availability invalidates the fence before a child
 // resource timer can advance. Pins participate in cohort identity even though
 // this warm transaction never activates/resets/parks GPIOs.
 {Harness h;h.begin();h.until(SweetMDSS::ManagedRails);tcFixtureOnline(0);
  SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);fault(h);assert(!sourceWritten());}
 {Harness h;panelPinsMode=1;assert(h.m->setupPanelPins());auto*n=new IOService;n->pinsNub=true;
  uint32_t abi=1;n->properties["sweet,panel-pins-abi"]=OSData::withBytes(&abi,4);
  auto*p=new SweetPanelPins();assert(p->start(n));SweetPanelPinsClient::matched(h.m->fPanelPinsClient,nullptr,p,nullptr);pump();
  h.begin();SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,p,nullptr);h.life();fault(h);pump();
  assert(!p->fTouched&&!sourceWritten());p->stop(n);p->release();dropNub(n);}
 // A provider stopping at each phase leaves timers inert and tokens queryable.
 for(unsigned point=0;point<5;++point){Harness h;h.begin();
  if(point==1)h.until(SweetMDSS::ManagedRails);
  if(point==2)h.until(SweetMDSS::ManagedLink);
  if(point>=3){for(unsigned i=0;i<12000&&h.m->fScanoutState!=SweetScanoutSubmitted;++i)h.step();}
  if(point==4){regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=0;realFrame(h.m);h.scan();h.life();}
  auto token=h.m->fLifecycle.token;h.m->stop(h.d);auto before=trace.size();h.life();h.scan();SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
  assert(before==trace.size()&&state(h,token).phase==SweetPanelStopped);}
 {Harness h;h.m->fLifecycleNextToken=~0ULL;assert(h.m->beginScanout(h.pool,h.channel)==kIOReturnNoResources&&!writes()&&!(tcFixtureFrame()>>4));}
 puts("PASS: real unified lifecycle+late TCM startup single-admission/wait/timeout/cancel/stop/withdrawal+managed core/PHY/host/scanout and asynchronous 28/29; independent GIC active/RWP, pixel fences, touch verification, exclusive admission, unknown-profile no effects, cancellation/provider withdrawal/stop, retained resources/pins and zero objects/maps/allocations/notifications");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-lifecycle-') as temp:
 temp=Path(temp);c=temp/'worker.c';c.write_text(actual_worker(root));obj=temp/'worker.o'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11',*flags,'-c',str(c),'-o',str(obj)],check=True)
 cpp=temp/'test.cpp';cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
 binary=temp/'test';subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-pthread',*flags,str(cpp),str(obj),'-o',str(binary)],check=True)
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 subprocess.run([str(binary)],check=True,env=env,timeout=60)
