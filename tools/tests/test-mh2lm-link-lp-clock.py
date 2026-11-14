#!/usr/bin/env python3
"""Actual mode2/7 ESC0 stop/start completion and retained-resource fences.
Usage: test-mh2lm-link-lp-clock.py EXTRACTED_XNU [--pipeline]
"""
from pathlib import Path
import os, subprocess, sys, tempfile

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
pipeline = '--pipeline' in sys.argv
sys.argv = sys.argv[:2]
here = Path(__file__).resolve().parent
fixture = here / ('test-mh2lm-panel-pipeline.py' if pipeline else 'test-mh2lm-panel-lineage-wake.py')
ns = {'__file__': str(fixture), '__name__': 'lp_clock_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
ns['prelude'] = ns['prelude'].replace('static unsigned phyResets=0;', 'static bool escHalt=true,escRun=true;static unsigned phyResets=0;')
ns['helpers'] = ns['helpers'].replace('if(physicalHalt&&!(regs[bases[1]+b]&1))', 'if(physicalHalt&&(b!=0x2038||escHalt)&&!(regs[bases[1]+b]&1))')
ns['helpers'] = ns['helpers'].replace('if(physicalRun&&(regs[bases[1]+b]&1))', 'if(physicalRun&&(b!=0x2038||escRun)&&(regs[bases[1]+b]&1))')
ns['touch'] = ns['touch'].replace('\n tcFixtureReboot();', '\n escHalt=escRun=true;tcFixtureReboot();')
tests = ns['tests'].split('int main(){')[0] + r'''
static void pendingLP(Harness&h,bool wake){
 ready(h);
 if(wake){supplyPark(h);escRun=false;}else escHalt=false;
 supplyWrites.clear();panelPackets.clear();h.power(wake?SweetPanelDisplayOn:SweetPanelDisplayOff);
 untilLink(h,wake?SweetMDSS::LinkLPStart:SweetMDSS::LinkLPStop);
 assert(h.m->linkBusy()&&h.m->fLinkSuspendedMask==0x78&&h.pool->pinned);
 SweetClockLease boot{};assert(!h.m->localClockStatus(h.m->fClockBootToken,&boot));
 assert(boot.state==SweetClockHeld&&boot.hardwareSuspended&&!h.m->localClocksReady());
}
static void noLateWrites(Harness&h){auto before=supplyWrites.size();fakeTicks+=1000000000;quiet(h);assert(supplyWrites.size()==before);}
int main(){
 for(bool wake:{false,true}){
  Harness h;pendingLP(h,wake);auto count=panelPackets.size();auto host=hostResets;
  auto before=supplyWrites.size();fakeTicks+=100000;h.step();
  assert(h.m->fLinkPhase==(wake?SweetMDSS::LinkLPStart:SweetMDSS::LinkLPStop));
  assert(supplyWrites.size()==before&&hostResets==host&&panelPackets.size()==count);
  if(wake)assert(regs[bases[12]+0xa0]==0);else assert(regs[bases[12]+0xa0]==0x1f);
  uint64_t token=0;assert(h.m->retainLocalClocks(SweetClockDSI0,&token)!=0&&!token);
  assert(h.m->setBrightness(0x456)==kIOReturnBusy&&supplyWrites.size()==before);
  // A delayed physical completion, independent of the request, resumes.
  if(wake)escRun=true;else escHalt=true;
  if(wake){h.wakePixels();assert(h.t->fStatus.ready&&h.m->fLinkSuspendedMask==0);
   auto esc=findWrite(bases[1]+0x2038,1,1),hostSetup=findWrite(bases[6]+0xb0,0x8421);
   assert(esc<hostSetup&&supplyWrites[hostSetup].time>=supplyWrites[esc].time+100000);
  }else{h.untilLife(SweetPanelComplete);assert(h.m->linkDormantConfiguration()&&h.m->fLinkSuspendedMask==0x78);}
 }
 // Posted bit0 is not halt/run proof; both directions time out closed.
 for(bool wake:{false,true}){Harness h;pendingLP(h,wake);
  auto count=panelPackets.size();fakeTicks=h.m->fLinkStepDeadline+1;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);
  supplyFailed(h);assert(h.m->fLinkPhase==SweetMDSS::LinkFault&&h.m->fDsiPoisoned&&h.m->fLinkSuspendedMask==0x78);
  assert(panelPackets.size()==count&&!h.m->fLifecycle.pixelsComplete);noLateWrites(h);
 }
 // Lost branch writes also cannot be substituted by requested enable state.
 for(bool wake:{false,true}){Harness h;ready(h);if(wake)supplyPark(h);
  dropLinkWrite=bases[1]+0x2038;h.power(wake?SweetPanelDisplayOn:SweetPanelDisplayOff);
  supplyFailed(h);assert(h.m->fLinkPhase==SweetMDSS::LinkFault&&h.m->fLinkSuspendedMask==0x78);noLateWrites(h);
 }
 // Both new waits must terminate on participant/ownership/idle loss, without
 // completing ON, withdrawing the retained core or issuing subsequent DMA.
 for(bool wake:{false,true})for(unsigned fault=0;fault<10;++fault){Harness h;pendingLP(h,wake);
  auto count=panelPackets.size();
  if(fault==0)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==1)h.m->stop(h.d);
  if(fault==2){SweetPanelPinsClient::terminated(h.m->fPanelPinsClient,nullptr,h.p,nullptr);pump();}
  if(fault==3)fakeTicks=h.m->fLifecycleStepDeadline+1;
  if(fault==4)h.m->fClockFault=true;
  if(fault==5)++h.m->fClockReferences[3];
  if(fault==6)regs[bases[1]+0x214c]=0x101; // ESC parent no longer XO
  if(fault==7)regs[bases[1]+0x2148]|=1; // parent update in progress
  if(fault==8)regs[bases[4]+0x288]|=1u<<20;
  if(fault==9)regs[bases[3]+0x1c]=1;
  SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);supplyFailed(h);
  assert(h.m->fLinkSuspendedMask==0x78&&panelPackets.size()==count&&!h.m->fLifecycle.pixelsComplete);
  noLateWrites(h);
 }
 // A fake running ESC leaf cannot authorize host setup with a stopped root.
 {Harness h;pendingLP(h,true);escRun=true;regs[bases[1]+0x2148]|=0x80000000u;
  supplyFailed(h);assert(h.m->fLinkSuspendedMask==0x78&&panelPackets.empty());noLateWrites(h);}
 // A third ordinary DSI lease is a real other user, including its ESC bit.
 {Harness h;ready(h);uint64_t other=0;assert(!h.m->retainLocalClocks(SweetClockDSI0,&other));
  for(unsigned n=0;n<100;++n){SweetClockLease v{};assert(!h.m->localClockStatus(other,&v));if(v.state==SweetClockHeld)break;h.step();}
  supplyWrites.clear();uint64_t token=0;assert(h.m->beginPanelLifecycle(SweetPanelDisplayOff,&token)!=0&&!token);
  for(auto&w:supplyWrites)assert(w.address!=bases[1]+0x2038);assert(regs[bases[1]+0x2038]&1);}
 assert(dsiPrepares==dsiCompletes);
 puts("PASS: ESC0 stop after ULPS / run before host; real delayed halt/run and lost-write timeouts; 20 wait-state fault cases, stopped-root and extra lease refusal; suspended boot lease never grants access, no early pixels/DMA or late writes; retained core/scanout pins");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-lp-clock-') as temp:
    temp = Path(temp)
    flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    worker = temp / 'worker.c'
    worker.write_text(ns['actual_worker'](ns['root']))
    obj = temp / 'worker.o'
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', *flags, '-c', str(worker), '-o', str(obj)], check=True)
    source = temp / 'test.cpp'
    actual = ns['actual']
    source.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n' + ns['prelude'] + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] + actual['header'] + actual['source'] + actual['backlight'] + ns['touch'] + ns['helpers'] + tests)
    binary = temp / 'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', *flags, '-Wno-format', '-Wno-format-security', '-Wno-inconsistent-missing-override', str(source), str(obj), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'), check=True, timeout=90)
