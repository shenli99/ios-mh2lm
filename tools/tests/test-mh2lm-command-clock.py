#!/usr/bin/env python3
"""Exercise actual per-command clock ownership at wire triggers and faults.
Usage: test-mh2lm-command-clock.py EXTRACTED_XNU [--pipeline]
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
# Observe the real MMIO trigger, including calls made by synchronous brightness.
ns['prelude'] = ns['prelude'].replace('static unsigned commandFault=0;', 'static void (*commandClockObserver)()=nullptr;static unsigned commandFault=0;')
assert ns['prelude'].count('regs[b+o]=v;if(b==bases[6]&&o==0x90){') == 1
ns['prelude'] = ns['prelude'].replace('regs[b+o]=v;if(b==bases[6]&&o==0x90){', 'regs[b+o]=v;if(b==bases[6]&&o==0x90){if(commandClockObserver)commandClockObserver();')
tests = ns['tests'].split('int main(){')[0] + r'''

static SweetMDSS* observed=nullptr;
static unsigned triggers=0,triggerFault=0;
static uint64_t lastCommandToken=0;
static void ledger(SweetMDSS&m){
 for(unsigned i=0;i<SweetClockCount;++i){unsigned count=0;
  for(auto&v:m.fClockLeases)if(v.state==SweetClockHeld&&(v.mask&(1u<<i)))++count;
  assert(count==m.fClockReferences[i]);
 }
}
static void atTrigger(){
 auto&m=*observed;++triggers;
 assert(m.fDsiTransferEngine&&m.fDsiTransferClock&&m.dsiTransferClockReady());
 assert(m.fDsiTransferClock>lastCommandToken);lastCommandToken=m.fDsiTransferClock;
 ledger(m);
 auto before=supplyWrites.size();
 // Even an idle status before the physical trigger cannot revoke the command
 // or its upstream owner. No physical stop is permitted under its reference.
 assert(!m.clockCanGate());
 assert(m.releaseLocalClocks(m.fDsiTransferClock)==kIOReturnBusy);
 assert(m.releaseLocalClocks(m.fClockBootToken)==kIOReturnBusy);
 assert(m.releaseLocalClocks(m.fManagedClock)==kIOReturnBusy);
 assert(!m.setDSIDisplayEngine(false)&&supplyWrites.size()==before);
 if(triggerFault==1)regs[bases[1]+0x2038]|=0x80000000u;
 if(triggerFault==2)for(auto&v:m.fClockLeases)if(v.token==m.fDsiTransferClock)v.uncertain=true;
}
static void observe(Harness&h){observed=h.m;triggers=triggerFault=0;lastCommandToken=0;commandClockObserver=atTrigger;}
static void done(Harness&h){assert(!h.m->fDsiTransferClock&&!h.m->fDsiTransferEngine);ledger(*h.m);}
int main(){
 // All four tables, FIFO+memory DMA, synchronous brightness and both display
 // modes use actual ledger leases at every hardware trigger. Frame restarts
 // and ordinary OFF/ON retain their existing expected wire tables.
 for(unsigned p=1;p<=4;++p){selectedProfile=p;Harness h;observe(h);ready(h);done(h);
  unsigned original[SweetClockCount];memcpy(original,h.m->fClockReferences,sizeof(original));
  for(unsigned n=0;n<3;++n){
   assert(h.m->setBrightness(0x345+n)==kIOReturnSuccess);done(h);
   panelPackets.clear();supplyPark(h);checkPackets(p,false);done(h);
   panelPackets.clear();h.power(SweetPanelDisplayOn);h.wakePixels();checkPackets(p,true,0x345+n);done(h);
   assert(!memcmp(original,h.m->fClockReferences,sizeof(original)));
  }
  assert(triggers>=3*(1+2+17+6+1));commandClockObserver=nullptr;
 }
 selectedProfile=4;
 // A borrowed reference must use real bounded slots, and must never consume
 // another active slot or enable a stopped root/branch just to send a command.
 for(unsigned fault=0;fault<17;++fault){Harness h;ready(h);auto&m=*h.m;
  if(fault==0)m.fClockFault=true;
  if(fault==1)m.fNoCFault=true;
  if(fault==2)m.fLinkSuspendedMask=8;
  if(fault==3)m.fHostPhase=SweetMDSS::HostFault;
  if(fault==4)m.fLinkPhase=SweetMDSS::LinkLPStop;
  if(fault==5)m.fAccepting=false;
  if(fault==6)m.fClockNextToken=~uint64_t(0);
  if(fault==7)m.fClockReferences[3]=~0u;
  if(fault==8)m.fClockReferences[3]=0;
  if(fault==9)m.fClockKnownMask&=~8u;
  if(fault==10)regs[bases[1]+0x2038]|=0x80000000u;
  if(fault==11)regs[bases[1]+0x2148]|=1u;
  if(fault>=12&&fault<=15)for(auto&v:m.fClockLeases)if(v.token==m.fClockBootToken){
   if(fault==12)v.uncertain=true;if(fault==13)v.result=kIOReturnIOError;
   if(fault==14)v.mask=7;if(fault==15)v.state=SweetClockStopped;
  }
  if(fault==16){for(auto&v:m.fClockLeases)if(v.state==SweetClockReleased||!v.token){v.token=++m.fClockNextToken;v.state=SweetClockHeld;v.mask=0;}}
  auto before=supplyWrites.size();auto token=m.fClockNextToken;unsigned refs[SweetClockCount];memcpy(refs,m.fClockReferences,sizeof(refs));
  assert(!m.acquireDSITransfer()&&!m.fDsiTransferClock&&m.fClockNextToken==token);
  assert(!memcmp(refs,m.fClockReferences,sizeof(refs))&&supplyWrites.size()==before);
 }
 // Delayed physical completion retains the lease for FIFO and physical DMA;
 // successful completion releases exactly once before the inter-packet delay.
 for(bool memory:{false,true}){Harness h;ready(h);observe(h);
  if(memory){supplyPark(h);h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
  for(unsigned n=0;n<6000&&!(h.m->fDsiStatus.state==SweetDSITransferring&&h.m->fDsiPinned==memory);++n)h.step();
  auto&m=*h.m;assert(m.fDsiTransferClock&&m.fDsiTransferEngine);auto token=m.fDsiTransferClock;
  dmaDone=false;for(unsigned n=0;n<10;++n)h.step();assert(m.fDsiTransferClock==token);
  dmaDone=true;
  for(unsigned n=0;n<10&&m.fDsiTransferClock;++n)h.step();
  assert(!m.fDsiTransferClock&&!m.fDsiTransferEngine&&m.fDsiStatus.state==SweetDSIDelay);
  SweetClockLease v={};assert(m.localClockStatus(token,&v)==0&&v.state==SweetClockReleased);
  auto before=supplyWrites.size();assert(!m.releaseDSITransferClock()&&supplyWrites.size()==before);ledger(m);
  commandClockObserver=nullptr;
 }
 // No completion, ACK error, cancellation, and termination keep the actual
 // token and aggregate references; late callbacks never release or restart.
 for(bool memory:{false,true})for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);
  if(memory){supplyPark(h);h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
  for(unsigned n=0;n<6000&&!(h.m->fDsiStatus.state==SweetDSITransferring&&h.m->fDsiPinned==memory);++n)h.step();
  auto&m=*h.m;auto token=m.fDsiTransferClock;assert(token);unsigned refs[SweetClockCount];memcpy(refs,m.fClockReferences,sizeof(refs));
  if(fault==0){dmaDone=false;fakeTicks+=200000001;SweetMDSS::dsiTimerAction(&m,m.fDsiTimer);h.life();}
  if(fault==1){lateError=true;SweetMDSS::dsiTimerAction(&m,m.fDsiTimer);h.life();}
  if(fault==2)m.cancelPanelLifecycle(m.fLifecycle.token);
  if(fault==3)m.stop(h.d);
  assert(m.fDsiPoisoned&&m.fDsiTransferClock==token&&m.fDsiTransferEngine&&m.fDsiPinned==memory);
  assert(!memcmp(refs,m.fClockReferences,sizeof(refs)));
  auto before=supplyWrites.size();fakeTicks+=10000000;quiet(h);assert(supplyWrites.size()==before);
 }
 // A completed status cannot authorize cleanup after a lost command lease
 // or stopped clock; exercise both FIFO and memory DMA via real callbacks.
 for(bool memory:{false,true})for(unsigned fault=1;fault<=2;++fault){Harness h;ready(h);
  if(memory){supplyPark(h);h.power(SweetPanelDisplayOn);}else h.power(SweetPanelDisplayOff);
  for(unsigned n=0;n<6000&&!(h.m->fDsiStatus.state==SweetDSITransferring&&h.m->fDsiPinned==memory);++n)h.step();
  auto&m=*h.m;auto token=m.fDsiTransferClock;assert(token);
  if(fault==1)regs[bases[1]+0x2038]|=0x80000000u;
  if(fault==2)for(auto&v:m.fClockLeases)if(v.token==token)v.uncertain=true;
  auto before=supplyWrites.size();SweetMDSS::dsiTimerAction(&m,m.fDsiTimer);h.life();
  assert(m.fDsiPoisoned&&m.fDsiTransferClock==token&&m.fDsiTransferEngine&&m.fDsiPinned==memory);
  // Fault propagation may mask/ack the already owned RD_PTR IRQ. It must
  // not restore DSI registers/TE, issue clocks, or withdraw power resources.
  for(unsigned i=before;i<supplyWrites.size();++i){auto&v=supplyWrites[i];
   assert((v.address==bases[10]&&v.value==0)||
          (v.address==bases[10]+8&&v.value==4)||
          (v.address==bases[11]+0x18c&&v.value==(1u<<19)));
  }
 }
 // Synchronous brightness must also retain clocks and leave TE/auto paused
 // on stale DONE, missing completion or loss of a clock/lease after trigger.
 for(unsigned fault=0;fault<4;++fault){Harness h;ready(h);observe(h);
  if(fault==0)dmaDone=false;if(fault==1)commandFault=4;
  if(fault>=2)triggerFault=fault-1;
  assert(h.m->setBrightness(0x346)==kIOReturnIOError);
  assert(h.m->fDsiPoisoned&&h.m->fDsiTransferClock&&h.m->fDsiTransferEngine);
  assert(!(regs[bases[4]+0x288]&(1u<<20))&&!regs[bases[4]+0x2b4]);
  if(fault>=2)assert(supplyWrites.back().address==bases[6]+0x90); // no cleanup after clock loss
  commandClockObserver=nullptr;triggerFault=0;
 }
 assert(dsiPrepares==dsiCompletes);
 puts("PASS: four profiles x three cycles; every FIFO/DMA/synchronous trigger holds real shared clock lease; balanced completion without physical gating; 17 admission faults; delayed completion and stale tokens; uncertain FIFO/DMA cancel/stop/ACK/timeout retains references; synchronous lost clock/lease blocks cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-command-clock-') as temp:
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
