#!/usr/bin/env python3
"""Run real mode2/mode7 command buffer and engine lifetime/failure paths.
Usage: test-mh2lm-command-lifecycle.py EXTRACTED_XNU [--pipeline]
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
pipeline = '--pipeline' in sys.argv
sys.argv = sys.argv[:2]
here = Path(__file__).resolve().parent
fixture = here / ('test-mh2lm-panel-pipeline.py' if pipeline else 'test-mh2lm-panel-lineage-wake.py')
ns = {'__file__': str(fixture), '__name__': 'command_lifecycle_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
# Faults belong to the register peer: command completion can coexist with
# BUSY/ACK, and a W1C write may fail to clear stale DONE.
ns['prelude'] = ns['prelude'].replace('static bool dmaFired=false,', 'static unsigned commandFault=0;static bool dmaFired=false,')
ns['prelude'] = ns['prelude'].replace('uint32_t v=regs[b+o];', 'uint32_t v=regs[b+o];if(commandFault==4&&b==bases[6]&&o==0x110&&!dmaFired)v|=1;')
ns['prelude'] = ns['prelude'].replace('if(b==bases[6]&&o==0x90){', 'if(b==bases[6]&&o==0x90){if(commandFault==2)regs[b+8]=6;if(commandFault==3)regs[b+0x68]=4;')
ns['touch'] = ns['touch'].replace('\n tcFixtureReboot();', '\n commandFault=0;tcFixtureReboot();')
tests = ns['tests'].split('int main(){')[0] + r'''
static void engineStep(Harness&h){
 h.step();
 if(h.m->fLifecycle.phase!=SweetPanelCommand)return;
 auto&m=*h.m;
 assert(!m.fDsiDisplayEngine);
 if(m.fDsiStatus.state==SweetDSITransferring){assert(m.fDsiTransferEngine&&(regs[bases[6]+4]&4));}
 if(m.fDsiStatus.state==SweetDSIDelay){assert(!m.fDsiTransferEngine&&!(regs[bases[6]+4]&4));}
}
int main(){
 // First ordinary command may be FIFO: startup prepares exactly once before
 // consumer publication, not at first ON, and unwind balances prepare/complete.
 for(unsigned fault=1;fault<=7;++fault){
  warmFixture();coreFixture();hostFixture();linkFixture();touchSetup();auto*d=displayNub();
  dsiMemoryFault=fault;auto allocations=dsiAllocations;auto*m=new SweetMDSS();
  assert(!m->start(d)&&dsiAllocations==allocations+1&&!m->fDsiPrepared&&!m->fDsiBuffer&&!writes());
  dsiMemoryFault=0;m->release();queued.clear();dropNub(d);mdssClockWriter=mdssNoCWriter=0;
  assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty()&&dsiPrepares==dsiCompletes);
 }
 for(unsigned profile=1;profile<=4;++profile){selectedProfile=profile;Harness h;
  auto*buffer=h.m->fDsiBuffer;auto allocations=dsiAllocations,prepares=dsiPrepares;
  assert(buffer&&h.m->fDsiPrepared&&!h.m->fDsiPinned&&h.m->fDsiDisplayEngine&&!h.m->fDsiTransferEngine);
  ready(h); // synchronous FIFO brightness also acquires/releases a transfer ref
  assert(h.m->fDsiDisplayEngine&&!h.m->fDsiTransferEngine);
  // Allocation failure introduced AFTER start must not affect FIFO/OFF or long ON.
  dsiMemoryFault=1;
  for(unsigned round=0;round<3;++round){
   panelPackets.clear();h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelCommand);
   for(unsigned n=0;n<3000&&h.m->fLifecycle.phase==SweetPanelCommand;++n)engineStep(h);
   h.untilLife(SweetPanelComplete);checkPackets(profile,false);
   assert(!h.m->fDsiDisplayEngine&&!h.m->fDsiTransferEngine&&!h.m->fDsiPinned&&regs[bases[4]+0x284]==0);
   panelPackets.clear();h.power(SweetPanelDisplayOn);h.untilLife(SweetPanelPinReset);
   assert(!(regs[bases[6]+4]&4)&&!h.m->fDsiDisplayEngine&&panelPackets.empty());
   h.untilLife(SweetPanelCommand);
   for(unsigned n=0;n<4000&&h.m->fLifecycle.phase==SweetPanelCommand;++n)engineStep(h);
   assert((h.m->fLifecycle.phase==SweetPanelColorRestore||h.m->fLifecycle.phase==SweetPanelFrame||h.m->fLifecycle.phase==SweetPanelPipelineRestore)&&h.m->fDsiDisplayEngine&&!h.m->fLifecycle.brightnessRestored);
   h.wakePixels();checkPackets(profile,true,0x345);
   assert(h.m->fDsiBuffer==buffer&&dsiAllocations==allocations&&dsiPrepares==prepares&&!h.m->fDsiPinned);
  }
  dsiMemoryFault=0;
 }
 // Synchronous brightness shares engine ownership and cannot reset/restore
 // the stream after a missing completion or a reported hardware error.
 for(unsigned fault=0;fault<5;++fault){Harness h;ready(h);auto packets=panelPackets.size();
  if(fault==0)dmaDone=false;else if(fault==1)lateError=true;else commandFault=fault;
  assert(h.m->setBrightness(0x346)==kIOReturnIOError);
  assert(h.m->fDsiPoisoned&&h.m->fDsiDisplayEngine&&h.m->fDsiTransferEngine&&!h.m->fDsiPinned);
  if(fault==4)assert(panelPackets.size()==packets); // stale DONE rejected before trigger
  assert(!(regs[bases[4]+0x288]&(1u<<20))&&!regs[bases[4]+0x2b4]);
  auto before=supplyWrites.size();assert(h.m->setBrightness(0x347)!=kIOReturnSuccess&&supplyWrites.size()==before);
 }
 // Cancelling before any command releases no nonexistent transfer reference.
 {Harness h;ready(h);h.power(SweetPanelDisplayOff);h.untilLife(SweetPanelDrain);
  for(unsigned n=0;n<5&&h.m->fLifecycle.phase==SweetPanelDrain;++n)h.life();
  assert(h.m->fLifecycle.phase==SweetPanelCommand&&!h.m->fDsiTransferEngine);h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  supplyFailed(h);assert(!h.m->fDsiTransferEngine&&!h.m->fDsiPinned);}
 // Once triggered, FIFO and physical DMA both keep their engine reference on
 // timeout/error/cancel/stop; only memory DMA needs the independent buffer pin.
 for(bool memory:{false,true})for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);
  if(memory){supplyPark(h);h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
  for(unsigned n=0;n<5000;++n){
   if(h.m->fDsiStatus.state==SweetDSITransferring&&h.m->fDsiPinned==memory)break;
   h.step();
  }
  assert(h.m->fDsiStatus.state==SweetDSITransferring&&h.m->fDsiTransferEngine&&h.m->fDsiPinned==memory);
  if(fault==0){regs[bases[6]+0x110]&=~1u;regs[bases[6]+8]|=6;fakeTicks+=200000001;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.life();}
  if(fault==1){lateError=true;SweetMDSS::dsiTimerAction(h.m,h.m->fDsiTimer);h.life();}
  if(fault==2)h.m->cancelPanelLifecycle(h.m->fLifecycle.token);
  if(fault==3)h.m->stop(h.d);
  assert(h.m->fDsiPoisoned&&h.m->fDsiTransferEngine&&h.m->fDsiPinned==memory&&(regs[bases[6]+4]&4));
  assert(!h.m->fDsiDisplayEngine&&h.m->fDsiPrepared);
  auto before=supplyWrites.size();fakeTicks+=10000000;quiet(h);assert(supplyWrites.size()==before);
 }
 assert(dsiPrepares==dsiCompletes);
 puts("PASS: startup preparation/unwind, FIFO-first and all profiles x three buffer-reusing cycles, per-packet engine references, OFF/reset/ON display references, no runtime allocation, synchronous fresh-DONE/idle/ACK/stale-status fences, FIFO/DMA timeout/error/cancel/stop keeps uncertain engine/pin, zero objects after simulated reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-command-lifecycle-') as temp:
    temp = Path(temp)
    flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    worker = temp / 'worker.c'
    worker.write_text(ns['actual_worker'](ns['root']))
    obj = temp / 'worker.o'
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', *flags, '-c', str(worker), '-o', str(obj)], check=True)
    actual = ns['actual']
    cpp = temp / 'test.cpp'
    cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n' + ns['prelude'] + actual['DSI_MEMORY_FIXTURE'] +
                   actual['pool_fixture'] + actual['header'] + actual['source'] + actual['backlight'] +
                   ns['touch'] + ns['helpers'] + tests)
    binary = temp / 'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-pthread', *flags,
                    str(cpp), str(obj), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'), check=True, timeout=90)
