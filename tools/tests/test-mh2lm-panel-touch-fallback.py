#!/usr/bin/env python3
"""Run complete display-only first adoption with the actual MDSS/TCM code.
Usage: test-mh2lm-panel-touch-fallback.py EXTRACTED_XNU
"""
from pathlib import Path
import os, subprocess, sys, tempfile
from mh2lm_touch_panel_fixtures import actual_worker

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
fixture = here/'test-mh2lm-panel-pipeline.py'
ns = {'__file__': str(fixture), '__name__': 'touch_fallback_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
root = ns['root']; prelude = ns['prelude']; actual = ns['actual']; helpers = ns['helpers']
touch = 'static unsigned fallbackMode=7;\n' + ns['touch'].replace('lifecycleMode=7;', 'lifecycleMode=fallbackMode;')
# Instrument only the OS gate boundary, before acquisition, for deterministic
# stop/query serialization and elapsed time while awaiting a touch snapshot.
prelude = prelude.replace('struct IOCommandGate : IOEvent {', 'static std::function<void(OSObject*)> gateEntryHook;\nstruct IOCommandGate : IOEvent {')
prelude = prelude.replace('std::lock_guard<std::recursive_mutex> l(loop->gate);return a(owner,x,y,z,w);', 'if(gateEntryHook)gateEntryHook(owner);std::lock_guard<std::recursive_mutex> l(loop->gate);return a(owner,x,y,z,w);')
tests = r'''
int main(){
 // The production profile and pipeline paths both finish the entire lifecycle.
 // MMIO/physical completions remain the independent existing fixtures.
 for(unsigned mode:{2u,7u})for(unsigned profile=1;profile<=4;++profile){
  fallbackMode=mode;selectedProfile=profile;Harness h;tcFixtureFail();h.begin();
  auto token=h.m->fLifecycle.token,frame=tcFixtureFrame();
  for(unsigned n=0;n<12000&&h.m->fScanoutState!=SweetScanoutSubmitted&&h.m->fLifecycle.phase!=SweetPanelFault;++n)h.step();
  assert(h.m->fScanoutState==SweetScanoutSubmitted&&h.m->fLifecycle.touchQuiesced&&!h.m->fLifecycle.touchToken);
  SweetScanoutStatus s{};assert(h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutCoordinating);
  h.life();assert(h.m->fLifecycle.phase==SweetPanelTakeoverPending);
  regs[bases[3]+0x18]=0;realFrame(h.m);h.scan();h.life();
  assert(h.m->fLifecycle.phase==SweetPanelTakeoverPending);
  regs[0x0ae01014]|=0x100;h.scan();h.life();
  SweetPanelLifecycleStatus l{};assert(h.m->panelLifecycleStatus(token,&l)==0);
  assert(l.phase==SweetPanelComplete&&l.result==0&&!l.uncertain&&l.pixelsComplete&&l.resourcesHeld);
  assert(l.touchQuiesced&&!l.touchToken&&h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutActive);
  assert(h.pool->pinned&&!h.m->fPanelDSIClock&&h.m->fMdpClock&&h.m->fClockBootToken);
  assert(tcFixtureFrame()==frame&&!h.t->fTouched&&!h.t->fStatus.token&&commands.empty()&&!busReads&&!busWrites);
  assert(panelPackets.empty()&&pinEdges.empty()&&!h.p->fTouched); // no panel reset/table/supply cycle
  bool named=false;for(auto&msg:messages)if(msg.find("takeover complete without touch")!=std::string::npos)named=true;
  assert(named);
  if(mode==7)assert(h.m->fPipelineConfigured&&h.m->fPipelineGeneration==1);
  assert(h.m->setBrightness(0x345)==0&&h.m->fPanelBrightnessKnown);
  for(unsigned op:{unsigned(SweetPanelDisplayOff),unsigned(SweetPanelDisplayOn)}){
   uint64_t rejected=99;auto before=supplyWrites.size();
   assert(h.m->beginPanelLifecycle(op,&rejected)==kIOReturnNotReady&&!rejected&&supplyWrites.size()==before);}
 }
 // Failure after a real pause token, or during touch resume, is still fatal.
 fallbackMode=7;selectedProfile=4;
 for(unsigned point=0;point<2;++point){Harness h;h.begin();
  if(!point)h.until(SweetMDSS::ManagedRails);
  else {
   for(unsigned n=0;n<12000&&h.m->fScanoutState!=SweetScanoutSubmitted;++n)h.step();
   regs[bases[3]+0x18]=0;regs[0x0ae01014]|=0x100;realFrame(h.m);h.scan();h.life();
   assert(h.m->fLifecycle.phase==SweetPanelTouchResume);
  }
  assert(h.m->fLifecycle.touchToken);tcFixtureFail();h.life();
  assert(h.m->fLifecycle.phase==SweetPanelFault&&h.m->fLifecycle.uncertain);
  SweetScanoutStatus s{};assert(h.m->scanoutStatus(h.pool,&s)==0&&s.state==SweetScanoutFailed&&(point ? h.pool->pinned : !sourceWritten()));
 }
 // A legitimately parked ON cannot reuse a fence from a failed worker.
 {Harness h;h.takeover();assert(h.m->setBrightness(0x345)==0&&h.m->fPanelBrightnessKnown);
  h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelComplete);tcFixtureFail();
  uint64_t rejected=99;auto before=supplyWrites.size();
  assert(h.m->beginPanelLifecycle(SweetPanelDisplayOn,&rejected)==kIOReturnNotReady&&!rejected&&supplyWrites.size()==before);
 }
 // A touch gate query can consume the remaining startup time. Only a fresh
 // timestamp AFTER that query can keep the 30s cutoff hard.
 {Harness h;tcFixtureOnline(0);h.begin();fakeTicks+=29999000000ULL;tcFixtureFail();
  bool delayed=false;gateEntryHook=[&](OSObject*owner){if(owner==h.t&&!delayed){delayed=true;fakeTicks+=2000000ULL;}};
  h.life();gateEntryHook=nullptr;
  assert(delayed&&h.m->fLifecycle.phase==SweetPanelFault&&h.m->fLifecycle.result==kIOReturnTimeout);
  assert(!h.m->fManagedStarted&&!sourceWritten()&&!h.m->fLifecycle.touchToken);
 }
 // Terminal snapshots serialize with close/stop on the touch service gate.
 {Harness h;tcFixtureFail();bool failed=false;unsigned entered=0;
  gateEntryHook=[&](OSObject*owner){if(owner==h.t)++entered;};
  assert(h.t->copyWorkerFailed(&failed)==0&&failed&&entered==1);gateEntryHook=nullptr;
  assert(h.t->copyWorkerFailed(nullptr)==kIOReturnBadArgument);
  std::unique_lock<std::recursive_mutex>held(h.t->fLoop->gate);
  std::mutex signal;std::condition_variable cv;bool waiting=false;std::atomic<bool>done{false};IOReturn rc=0;
  gateEntryHook=[&](OSObject*owner){if(owner==h.t){std::lock_guard<std::mutex>l(signal);waiting=true;cv.notify_all();}};
  std::thread query([&]{rc=h.t->copyWorkerFailed(&failed);done.store(true);});
  {std::unique_lock<std::mutex>l(signal);cv.wait(l,[&]{return waiting;});}
  assert(!done.load());h.t->stop(h.tn);held.unlock();query.join();gateEntryHook=nullptr;
  assert(rc==kIOReturnNotReady&&!failed&&done.load());
 }
 puts("PASS: display-only first takeover complete in mode2/mode7 x four profiles; fresh pixel/resource/post-enable gates, retained MDP/BOOT refs, no touch/reset/table requests, OFF/ON refused, real pause/resume faults remain fatal");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-touch-fallback-') as temp:
    temp = Path(temp); c = temp/'worker.c'; c.write_text(actual_worker(root)); obj = temp/'worker.o'
    flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', *flags, '-c', str(c), '-o', str(obj)], check=True)
    cpp = temp/'test.cpp'
    cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+actual['backlight']+touch+helpers+tests)
    binary = temp/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-pthread', *flags, str(cpp), str(obj), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=90, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
