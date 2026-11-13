#!/usr/bin/env python3
"""Actual warm resource chain through real scanout, IRQ and resource owners.
Usage: test-mh2lm-managed-display.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
existing = here/'test-mh2lm-display-noc.py'
ns = {'__file__': str(existing), '__name__': 'managed_fixture'}
exec(compile(existing.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(existing), 'exec'), ns)
prelude = ns['prelude'].replace('static int clockMode=1;', 'static int clockMode=1,managedMode=1;')
prelude = prelude.replace(' if(!strcmp(k,"sweetdisplayclocks"))',
    ' if(!strcmp(k,"sweetdisplaymanaged")){*(int*)v=managedMode;return true;}\n if(!strcmp(k,"sweetdisplayclocks"))')
helpers = ns['helpers']+ns['tests'].split('int main(){')[0]
actual = ns['ns']
tests = r'''
static void warmFixture(){
 nocFixtures();managedMode=1;ownMode=2;irqMode=2;powerEnabled=1;probeEnabled=0;irqMMIO=true;
 regs[0x17a00000]=0x12;regs[0x17a00004]=0x7b041d;regs[0x17a0ffe8]=0x3b;
 regs[0x17a00470]=0x80123456;regs[0x0ae00000]=0x50000001;
 regs[bases[4]+0x284]=1;regs[bases[4]+0x288]=0x180088;
 regs[bases[4]+0x2a4]=0x925;regs[bases[4]+0x2b4]=0x80000001;regs[0x0ae6e804]=0xf;
 regs[bases[2]]=regs[bases[2]+0xc]=0x9240438;
 regs[bases[2]+0x14]=0x9d200000;regs[bases[2]+0x24]=4320;
 regs[bases[2]+0x30]=0x237ff;regs[bases[2]+0x34]=0x03020001;regs[bases[2]+0x38]=0x80000000;
 regs[bases[3]+0x14]=0x20000;
 regs[bases[7]+0x20]=0x4c017e5e;regs[bases[7]+0x24]=0x60000038;
 regs[bases[7]]=0x2b0406;regs[bases[7]+0x80c]=0x84200800;regs[bases[7]+0xc0c]=2;
 regs[bases[7]+0x1008]=0x1f000;regs[bases[7]+0x1808]=1;
 regs[bases[8]]=0xe0;regs[bases[8]+0x58]=0x400;regs[0x0ae01014]=0x100;
}
static void irqTimer(SweetMDSS*m){std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);SweetMDSS::irqTimerAction(m,m->fIrqTimer);}
static void realFrame(SweetMDSS*m){fakeTicks+=16666667;regs[0x0ae6e804]|=4;regs[0x0ae00010]=1;assert(sweet_mdss_irq(115));pump();}
struct Harness {
 IOService *n,*d;SweetRPMh*r;SweetMDSS*m;SweetScanoutPool*pool;
 IOWorkLoop*wl;IOInterruptEventSource*event;SweetVsyncChannel*channel;
 Harness(){
  warmFixture();n=nub();r=make(n);d=displayNub();m=displayMake(d,r);
  assert(m->fManagedEnabled&&m->fManagedState==SweetMDSS::ManagedIdle&&!writes());
  irqTimer(m);assert(m->fIrqState==SweetMDSS::IrqProbe);assert(sweet_mdss_irq(115));fakeTicks+=50000000;irqTimer(m);
  assert(m->fIrqState==SweetMDSS::IrqArmed);
  wl=IOWorkLoop::workLoop();event=IOInterruptEventSource::interruptEventSource(d,[](OSObject*,IOInterruptEventSource*,int){});
  assert(wl->addEventSource(event)==0);channel=SweetVsyncChannel::withEvent(event);assert(m->attachVsync(channel)==0);
  realFrame(m);pool=new SweetScanoutPool;trace.clear();
 }
 void scan(){std::lock_guard<std::recursive_mutex>l(m->fLoop->gate);SweetMDSS::scanoutTimerAction(m,m->fScanoutTimer);}
 void step(bool apps=true,bool data=true){::step(m,r,apps,data);scan();}
 void begin(){assert(m->beginScanout(pool,channel)==0);step();step();assert(m->fScanoutState==SweetScanoutResources&&!pool->pinned);}
 void until(unsigned phase){for(unsigned j=0;j<2000&&m->fManagedState!=phase;++j)step();assert(m->fManagedState==phase);}
 void finish(){for(unsigned j=0;j<2000&&m->fScanoutState!=SweetScanoutSubmitted&&m->fScanoutState!=SweetScanoutFailed;++j)step();
  assert(m->fScanoutState==SweetScanoutSubmitted&&pool->pinned);
  regs[0x0ae01014]|=0x100;regs[bases[3]+0x18]=0;realFrame(m);scan();assert(m->fScanoutState==SweetScanoutActive);
 }
 ~Harness(){
  m->stop(d);auto before=trace.size();scan();assert(trace.size()==before);
  m->detachVsync(channel);channel->close();channel->release();wl->removeEventSource(event);event->release();wl->release();
  bool pinned=pool->pinned;m->release();queued.clear();pool->release();if(pinned)pool->release(); // simulated reboot
  dropNub(d);destroy(r,n);dropNub(n);
  assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());rpmhOwner=mdssClockWriter=mdssNoCWriter=0;
 }
};
static bool sourceWritten(){for(auto op:trace)if(op.write&&(op.addr==bases[2]+0x14||op.addr==bases[2]+8||op.addr==bases[2]+0x24))return true;return false;}
int main(){
 // Explicit bad combinations fail before a resource request, without objects/maps left.
 for(unsigned fault=0;fault<5;++fault){warmFixture();
  if(fault==0)managedMode=2;if(fault==1)ownMode=1;if(fault==2)irqMode=1;if(fault==3)powerEnabled=0;
  auto*d=displayNub();if(fault==4){d->properties["sweet,display-noc-abi"]->release();d->properties.erase("sweet,display-noc-abi");}
  auto*m=new SweetMDSS();assert(!m->start(d));m->release();queued.clear();dropNub(d);
  assert(!writes()&&!live&&!liveMaps&&!rawBlocks&&subscriptions.empty()&&!mdssClockWriter&&!mdssNoCWriter);
 }
 {
  Harness h;assert(h.m->managedAccessReady());h.begin();assert(!h.m->managedAccessReady()&&h.m->fIrqState==SweetMDSS::IrqIdle);
  assert(h.m->brightnessReady()==kIOReturnNotReady&&h.m->setPanelPower(false)==kIOReturnNotReady);
  uint32_t word=0x80050029;assert(h.m->dcsWrite(&word,1)==kIOReturnNotReady);
  // No ACK: repeated scheduling must never submit source or claim Ready.
  for(unsigned j=0;j<16;++j)h.step(false,false);
  assert(!sourceWritten()&&!h.pool->pinned&&h.m->fManagedState==SweetMDSS::ManagedRails);
  uint64_t vddiAck=0,secondRequest=0;
  while(h.m->fManagedState==SweetMDSS::ManagedRails){h.step();
   if(h.m->fManagedRails==1&&!vddiAck)vddiAck=fakeTicks;
   if(h.m->fManagedRail==1&&h.m->fManagedToken&&!secondRequest)secondRequest=fakeTicks;
   assert(!sourceWritten());}
  assert(secondRequest>=vddiAck+20000000ULL);
  h.finish();assert(h.m->fManagedState==SweetMDSS::ManagedReady&&h.m->fManagedRails==15);
  assert(h.r->fClients[0].displayVote==SweetDisplayTurbo&&h.r->fClients[0].registerVote==300000000);
  for(unsigned i=0;i<4;++i)assert(h.r->fClients[0].votes[i].enabled);
  auto ns=nstatus(h.m,h.m->fManagedBandwidth);assert(ns.state==SweetNoCHeld);
  SweetClockLease cs{};assert(h.m->localClockStatus(h.m->fManagedClock,&cs)==0&&cs.state==SweetClockHeld);
  assert(h.m->brightnessReady()==0);
  // Actual rail address order precedes ARC, config and display-data commands.
  unsigned first[7]={};for(unsigned i=0;i<trace.size();++i){auto op=trace[i];if(!op.write)continue;
   for(unsigned r=0;r<4;++r)if(op.addr==TCS+0x34&&op.value==0x40000+0x100*r&&!first[r])first[r]=i;
   if(op.addr==TCS+0x34&&op.value==0x30000&&!first[4])first[4]=i;
   if(op.addr==TCS+0x34&&op.value==0x50000&&!first[5])first[5]=i;
   if(op.addr==DT+2*0x2a0+0x34&&!first[6])first[6]=i;
  }
  for(unsigned i=1;i<7;++i)assert(first[i]>first[i-1]);
  trace.clear();assert(h.m->setPanelPower(false)==0);assert(h.m->setPanelPower(true)==0);
  for(auto op:trace)assert(!op.write||!((op.addr>=TCS&&op.addr<TCS+0x3000)||(op.addr>=DT&&op.addr<DT+0x3000)));
  assert(h.m->fManagedState==SweetMDSS::ManagedReady); // DCS Off does not collapse an inherited domain
 }
 // Metadata rejected before even pausing inherited scanning.
 {
  Harness h;regs[DB+400+16]=0;assert(h.m->beginScanout(h.pool,h.channel)!=0&&!writes()&&!h.m->fScanoutUsed&&!h.pool->pinned);
 }
 for(unsigned fault=0;fault<8;++fault){
  Harness h;h.begin();
  if(fault==0){for(unsigned j=0;j<1200&&h.m->fManagedState!=SweetMDSS::ManagedFault;++j)h.step(false);}
  if(fault==1){while(h.m->fManagedRail<1)h.step();rpmhDrop=0x34;}
  if(fault==2){h.until(SweetMDSS::ManagedARC);rpmhDrop=0x38;}
  if(fault==3){h.until(SweetMDSS::ManagedNoC);regs[DW+4]=~0u;}
  if(fault==4){h.until(SweetMDSS::ManagedClock);regs[bases[1]+0x209c]^=1;h.finish();
   assert(h.m->fScanoutState==SweetScanoutActive);continue;}
  if(fault==5){while(h.m->fManagedRail<1)h.step();regs[bases[6]+8]=2;}
  if(fault==6){h.until(SweetMDSS::ManagedNoC);h.step();h.m->stop(h.d);}
  if(fault==7){fakeTicks+=4000000000ULL;h.scan();}
  for(unsigned j=0;j<2000&&h.m->fScanoutState!=SweetScanoutFailed;++j)h.step();
  assert(h.m->fScanoutState==SweetScanoutFailed&&h.m->fManagedState==(fault==6 ? SweetMDSS::ManagedStopped : SweetMDSS::ManagedFault)&&!sourceWritten()&&!h.pool->pinned);
  assert(!h.m->managedAccessReady()&&regs[bases[4]+0x2b4]==0&&!(regs[bases[4]+0x288]&(1u<<20)));
  if(fault>=2&&fault<=4){assert(h.m->fManagedRails==15);for(unsigned i=0;i<4;++i)assert(h.r->fClients[0].votes[i].enabled);}
  auto before=writes();h.scan();assert(writes()==before); // no repeat/rollback/implicit resume
 }
 // Provider withdrawal after successful acquisition closes the live path, keeps
 // the hardware pin/votes, and never hands its ledger to a new instance.
 {
  Harness h;h.begin();h.finish();assert(h.r->willTerminate(h.n,0));emit(gIOTerminatedNotification,h.r);pump();
  SweetScanoutStatus st{};assert(h.m->scanoutStatus(h.pool,&st)==0&&st.state==SweetScanoutFailed&&st.hardwareReachable);
  assert(h.pool->pinned&&!h.m->fRPMh&&!h.m->managedAccessReady());
 }
 puts("PASS: real managed scanout/resource chain, paused IRQ/DMA admission, ordered typed LDO/ARC/NoC/clock ACKs, timer rail delays, source pin/fresh completion, DCS retention, malformed setup/metadata, stage faults/deadline/stop/provider loss and zero objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-managed-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+actual['DSI_MEMORY_FIXTURE']+actual['pool_fixture']+actual['header']+actual['source']+helpers+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
