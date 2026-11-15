#!/usr/bin/env python3
"""Actual outer DSI/MDP clock clients across takeover, OFF/ON and failures.
Usage: test-mh2lm-display-client-clocks.py EXTRACTED_XNU [--pipeline]
MMIO peers observe ownership at writes; no driver function is substituted.
"""
from pathlib import Path
import os, subprocess, sys, tempfile
if len(sys.argv) not in (2, 3): sys.exit(__doc__)
pipeline = '--pipeline' in sys.argv
sys.argv = sys.argv[:2]
here = Path(__file__).resolve().parent
fixture = here / ('test-mh2lm-panel-pipeline.py' if pipeline else 'test-mh2lm-panel-lineage-wake.py')
ns = {'__file__': str(fixture), '__name__': 'display_clients_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), ns)
ns['prelude'] = ns['prelude'].replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){', '''
static void (*clientObserver)(uintptr_t,uint32_t,uint32_t)=nullptr;
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(clientObserver)clientObserver(b,o,v);
''')
tests = ns['tests'].split('int main(){')[0] + r'''
static SweetMDSS* observed;
static unsigned clientCommands,starts,shutdowns,restores;
static void ledger(SweetMDSS&m){
 for(unsigned i=0;i<SweetClockCount;++i){unsigned expected=0;
  for(auto&v:m.fClockLeases)if((v.state==SweetClockHeld||v.state==SweetClockReleasing)&&(v.mask&(1u<<i)))++expected;
  assert(m.fClockReferences[i]==expected);
 }
}
static void observeWrite(uintptr_t b,uint32_t o,uint32_t v){
 auto&m=*observed;
 if(b==bases[6]&&o==0x90){
  ++clientCommands;ledger(m);assert(m.fDsiTransferClock&&m.fDsiTransferEngine);
  if(m.fLifecycle.operation==SweetPanelDisplayOff&&m.lifecycleBusy())
   assert(m.fPanelDSIClock&&!m.fMdpClock);
  else if(m.lifecycleBusy())assert(m.fPanelDSIClock);
  else assert(m.fMdpClock&&!m.fPanelDSIClock); // synchronous brightness retains MDP
 }
 if((b==bases[2]&&o==0x14)||(b==bases[3]&&(o==0x18||o==0x1c)&&v)||
    (b==bases[4]&&o==0x2b4&&v)){
  ++starts;ledger(m);assert(m.fMdpClock&&m.displayClientHeld(m.fMdpClock,2));
 }
 if(b==bases[1]&&(o==0x202c||o==0x2004||o==0x2028||o==0x2038)&&!(v&1)){
  ++shutdowns;ledger(m);assert(m.fPanelDSIClock&&!m.fMdpClock&&m.fLifecycle.displayDrained);
 }
 if(b==bases[6]&&o==0xb0&&m.fLifecycle.operation==SweetPanelDisplayOn){
  ++restores;ledger(m);assert(m.fPanelDSIClock&&!m.fMdpClock);
 }
}
static void observe(Harness&h){observed=h.m;clientObserver=observeWrite;}
static unsigned slot(SweetMDSS&m,uint64_t token){
 for(unsigned n=0;n<SweetMDSS::ClockLeaseSlots;++n)if(m.fClockLeases[n].token==token)return n;
 assert(false);return 0;
}
static void onlyFaultIRQs(size_t before){
 for(unsigned i=before;i<supplyWrites.size();++i){auto&w=supplyWrites[i];
  assert((w.address==bases[10]&&w.value==0)||(w.address==bases[10]+8&&w.value==4)||
         (w.address==bases[11]+0x18c&&w.value==(1u<<19)));
 }
}
int main(){
 for(unsigned p=1;p<=4;++p){selectedProfile=p;Harness h;observe(h);ready(h);auto&m=*h.m;
  assert(m.fDisplayClientState==SweetMDSS::ClientsActive&&!m.fPanelDSIClock&&m.fMdpClock);ledger(m);
  for(unsigned cycle=0;cycle<3;++cycle){
   auto mdp=m.fMdpClock,token=m.fClockNextToken;
   for(unsigned frame=0;frame<8;++frame)realFrame(&m);
   assert(m.fMdpClock==mdp&&m.fClockNextToken==token); // no per-autorefresh-frame references
   assert(m.setBrightness(0x345+cycle)==0&&m.fMdpClock==mdp);ledger(m);
   auto before=supplyWrites.size();assert(m.releaseLocalClocks(mdp)==kIOReturnBusy);
   assert(m.releaseLocalClocks(m.fManagedClock)==kIOReturnBusy);
   assert(m.releaseLocalClocks(m.fClockBootToken)==kIOReturnBusy&&supplyWrites.size()==before);
   supplyPark(h);assert(!m.fPanelDSIClock&&!m.fMdpClock&&m.fDisplayClientState==SweetMDSS::ClientsOff);ledger(m);
   h.power(SweetPanelDisplayOn);auto dsi=m.fPanelDSIClock;assert(dsi&&!m.fMdpClock);
   SweetClockLease v{};assert(m.localClockStatus(dsi,&v)==0&&v.state==SweetClockHeld&&v.hardwareSuspended);
   before=supplyWrites.size();assert(!m.acquireDSITransfer()&&supplyWrites.size()==before);
   h.untilLife(SweetPanelFrame);assert(m.fPanelDSIClock==dsi&&m.fMdpClock&&m.fMdpClock!=mdp);
   before=supplyWrites.size();assert(!m.postEnableDisplayClients()&&supplyWrites.size()==before);
   // Missing fresh frame cannot hand DSI back even with scan resources held.
   for(unsigned wait=0;wait<3;++wait)h.life();assert(m.fPanelDSIClock==dsi);
   h.wakePixels();assert(!m.fPanelDSIClock&&m.fMdpClock&&m.fDisplayClientState==SweetMDSS::ClientsActive);ledger(m);
  }
  clientObserver=nullptr;
 }
 assert(clientCommands&&starts&&shutdowns&&restores);selectedProfile=4;
 // Real delayed scan/IRQ drain: pre-disable owns both clients. No MDP return
 // until the hardware idle samples arrive, and no reuse of the old MDP token.
 {Harness h;ready(h);auto&m=*h.m;auto mdp=m.fMdpClock;h.power(SweetPanelDisplayOff);
  h.untilLife(SweetPanelDrain);assert(m.fPanelDSIClock&&m.fMdpClock==mdp);
  regs[bases[4]+0x2b0]=120;for(unsigned n=0;n<3;++n){fakeTicks+=1000000;h.life();}
  assert(m.fMdpClock==mdp);auto before=supplyWrites.size();assert(!m.stopMDPClient()&&supplyWrites.size()==before);
  regs[bases[4]+0x2b0]=0;h.untilLife(SweetPanelComplete);assert(!m.fMdpClock&&!m.fPanelDSIClock);
 }
 // An unrelated sixth-slot lease still blocks OFF admission, never swallowed
 // by the two new roles. Fill all slots through the actual clock controller.
 {Harness h;ready(h);auto&m=*h.m;uint64_t extra[3]={};unsigned count=0;
  while(count<3){auto rc=m.retainLocalClocks(SweetClockDSI0,&extra[count]);if(rc==kIOReturnNoResources)break;
   assert(rc==0);for(unsigned n=0;n<1000&&m.clockBusy();++n)h.step();assert(!m.clockBusy());++count;}
  assert(count);ledger(m);uint64_t t=0;auto before=supplyWrites.size();
  assert(m.beginPanelLifecycle(SweetPanelDisplayOff,&t)!=0&&!t&&supplyWrites.size()==before);
 }
 // Missing/stale/aliased/uncertain owner or aggregate mismatch must fail
 // before command/pipeline/clock writes and preserve the remaining refs/pin.
 for(bool wake:{false,true})for(unsigned fault=0;fault<8;++fault){Harness h;ready(h);auto&m=*h.m;
  if(wake){supplyPark(h);h.power(SweetPanelDisplayOn);}
  auto role=wake?m.fPanelDSIClock:m.fMdpClock;auto n=slot(m,role);
  if(fault==0){if(wake)m.fPanelDSIClock=0;else m.fMdpClock=0;}
  if(fault==1){if(wake)m.fPanelDSIClock=~0ULL;else m.fMdpClock=~0ULL;}
  if(fault==2)m.fClockClient[n]=wake?2:1;
  if(fault==3)m.fClockLeases[n].uncertain=true;
  if(fault==4)m.fClockLeases[n].state=SweetClockReleased;
  if(fault==5)m.fClockLeases[n].result=kIOReturnIOError;
  if(fault==6){if(wake)m.fPanelDSIClock=m.fClockBootToken;else m.fMdpClock=m.fClockBootToken;}
  if(fault==7)--m.fClockReferences[3];
  unsigned refs[SweetClockCount];memcpy(refs,m.fClockReferences,sizeof(refs));auto before=supplyWrites.size();
  assert(!m.displayClientsValid()&&!m.acquireDSITransfer());
  if(wake)h.life();else {SweetScanoutStatus s{};assert(m.scanoutStatus(h.pool,&s)==0);}
  assert(m.fLifecycle.phase==SweetPanelFault&&h.pool->pinned);onlyFaultIRQs(before);
  assert(!memcmp(refs,m.fClockReferences,sizeof(refs)));before=supplyWrites.size();quiet(h);assert(supplyWrites.size()==before);
 }
 // Saturation/overflow cannot partially acquire the outer DSI token. No pin
 // activation/rail restore is permitted after this admission has failed.
 for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);supplyPark(h);auto&m=*h.m;
  if(fault==0)m.fClockNextToken=~0ULL;
  if(fault==1)m.fClockReferences[3]=~0u;
  if(fault==2)for(auto&v:m.fClockLeases)if(!v.token||v.state==SweetClockReleased){v={};v.token=++m.fClockNextToken;v.state=SweetClockHeld;}
  auto before=supplyWrites.size();uint64_t token=0;m.beginPanelLifecycle(SweetPanelDisplayOn,&token);
  assert(!m.fPanelDSIClock&&!m.fMdpClock);onlyFaultIRQs(before);
 }
 // Cancel/provider withdrawal/owner stop on both sides of MDP handoff retains
 // whichever real clients existed; late callbacks do not release or restart.
 for(unsigned stage=0;stage<4;++stage)for(unsigned fault=0;fault<3;++fault){Harness h;ready(h);auto&m=*h.m;
  if(stage>=2){supplyPark(h);h.power(SweetPanelDisplayOn);if(stage==3)h.untilLife(SweetPanelFrame);}
  else {h.power(SweetPanelDisplayOff);if(stage==1)h.untilLife(SweetPanelCommand);}
  auto dsi=m.fPanelDSIClock,mdp=m.fMdpClock;assert(dsi);
  unsigned refs[SweetClockCount];memcpy(refs,m.fClockReferences,sizeof(refs));
  if(fault==0)m.cancelPanelLifecycle(m.fLifecycle.token);
  if(fault==1){SweetPanelPinsClient::terminated(m.fPanelPinsClient,nullptr,h.p,nullptr);h.life();}
  if(fault==2)m.stop(h.d);
  assert(m.fPanelDSIClock==dsi&&m.fMdpClock==mdp&&h.pool->pinned&&!memcmp(refs,m.fClockReferences,sizeof(refs)));
  auto before=supplyWrites.size();quiet(h);assert(supplyWrites.size()==before&&m.fPanelDSIClock==dsi&&m.fMdpClock==mdp);
 }
 puts("PASS: actual DSI/MDP ledger at wire/source/flush/link writes; four profiles x three cycles; autorefresh and brightness retain MDP; suspended DSI grants no command access; delayed drain/fresh frame; unrelated leases; 16 lost-owner cases, saturation/overflow and 12 cancel/provider/stop stages retain resources and pins");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-display-clients-') as temp:
    temp = Path(temp)
    flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    worker = temp/'worker.c'; worker.write_text(ns['actual_worker'](ns['root']))
    obj = temp/'worker.o'
    subprocess.run([os.environ.get('CC','clang-19'), '-std=c11', *flags, '-c', str(worker), '-o', str(obj)], check=True)
    a = ns['actual']; cpp = temp/'test.cpp'
    cpp.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n'+ns['prelude']+a['DSI_MEMORY_FIXTURE']+a['pool_fixture']+a['header']+a['source']+a['backlight']+ns['touch']+ns['helpers']+tests)
    binary = temp/'test'
    subprocess.run([os.environ.get('CXX','clang++-19'), '-std=c++17', '-pthread', *flags, str(cpp), str(obj), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'), check=True, timeout=90)
