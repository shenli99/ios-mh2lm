#!/usr/bin/env python3
"""Actual RPMh GIC callback/event/workloop, independent IRQ/EOI/MMIO peers.
Usage: test-mh2lm-rpmh-completion-irq.py EXTRACTED_XNU
"""
from pathlib import Path
import os,subprocess,sys,tempfile
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent
parent=here/'test-mh2lm-rpmh-resources.py'
ns={'__file__':str(parent),'__name__':'rpmh_irq_fixture'}
exec(compile(parent.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(parent),'exec'),ns)
common=ns['ns'];helpers=ns['tests'].split('int main(){')[0]
prelude=common['prelude'].replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){',r'''
static bool staleUnmask=false,pauseIRQWrite=false,fastAtTrigger=false;
static thread_local bool inIRQTop=false;
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(pauseIRQWrite&&inIRQTop&&b==0x17a00000&&o==0x184){std::unique_lock<std::mutex>l(pauseLock);enteredRead=true;pauseCV.notify_all();pauseCV.wait(l,[]{return releaseRead;});}
 if(fastAtTrigger&&b==0x18220d00&&o==0x14&&v==0x1010000)regs[b+4]|=1;
 if(staleUnmask&&b==0x18220d00&&o==0&&(v&1)&&regs[b+0x1c]==7)regs[b+4]|=1;
''')
tests=r'''
static constexpr uintptr_t G=0x17a00000;
static constexpr unsigned GB=1u<<5;
static unsigned perWrites(){unsigned n=0;for(auto&o:trace)n+=o.write&&o.addr>=TCS&&o.addr<TCS+0x3000;return n;}
static unsigned acknowledgements(){unsigned n=0;for(auto&o:trace)n+=o.write&&o.addr==TCS+8;return n;}
static void dispose(SweetRPMh*s,IOService*n){destroy(s,n);dropNub(n);assert(!live&&!liveMaps&&subscriptions.empty());queued.clear();reboot();}
int main(){
 // Metadata remains hardware read-only; missing extension blocks mode2.
 for(unsigned fault=0;fault<5;++fault){fixtures();resourceMode=2;auto*n=nub();
  if(fault==0){n->properties["sweet,rpmh-irq-abi"]->release();n->properties.erase("sweet,rpmh-irq-abi");}
  if(fault==1){n->properties["sweet,rpmh-intid"]->release();uint32_t wrong=38;n->properties["sweet,rpmh-intid"]=OSData::withBytes(&wrong,4);}
  if(fault==2)missingMap=3;if(fault==3)badMap=3;if(fault==4)shortMap=3;
  auto*s=new SweetRPMh();assert(!s->start(n)&&!writes());s->release();dropNub(n);assert(!live&&!liveMaps&&!rpmhOwner&&!rpmhIRQService);
 }
 fixtures();resourceMode=1;auto*n=nub();n->properties["sweet,rpmh-irq-abi"]->release();n->properties.erase("sweet,rpmh-irq-abi");
 auto*s=make(n);assert(!s->fIRQMap&&!s->fIRQEvent&&!rpmhIRQService);dispose(s,n);
 // Refuse another GIC owner, foreign pending or unsupported layout read-only.
 for(unsigned fault=0;fault<5;++fault){fixtures();resourceMode=2;n=nub();s=make(n);
  if(fault==0)regs[G+0x104]=GB;if(fault==1)regs[G+0x204]=GB;
  if(fault==2)regs[G+0x304]=GB;if(fault==3)regs[G+0xffe8]=0x40;if(fault==4)regs[TCS+4]=0x40;
  auto id=attach(s),token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);
  assert(status(s,id,token).state==SweetRPMhFailed&&!writes());
  assert(!s->fExecuting&&!s->fOwned[0]);if(fault==4)assert(regs[TCS+4]==0x40);
  dispose(s,n);
 }
 // Preserve neighbouring GIC fields and unrelated source bits. Fast reply has
 // the SAME command status as the inherited snapshot and no sampled busy.
 fixtures();resourceMode=2;regs[G+0x84]=0x12340000;regs[G+0xc08]=0xa5a50fff;regs[G+0x424]=0x12345678;
 n=nub();s=make(n);auto id=attach(s),token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);
 assert(s->fExecuting&&!s->fIRQObserved&&s->fIRQArmed&&regs[TCS]==0x41);
 assert((regs[G+0x84]&~GB)==0x12340000&&(regs[G+0xc08]&~0xc00u)==(0xa5a50fff&~0xc00u));
 assert((regs[G+0x424]&~0xff00u)==(0x12345678&~0xff00u));
 size_t clear=0,unmask=0,trigger=0;for(size_t i=0;i<trace.size();++i)if(trace[i].write){auto&o=trace[i];
  if(o.addr==TCS+8)clear=i;if(o.addr==TCS&&(o.value&1))unmask=i;
  if(o.addr==TCS+0x14&&o.value==0x1010000)trigger=i;}
 assert(clear<unmask&&unmask<trigger);assert(acknowledgements()==1);
 trace.clear();for(unsigned i=0;i<5;++i){fakeTicks+=100000;s->fTimer->action(s,s->fTimer);}
 assert(trace.empty()&&!s->fIRQObserved&&status(s,id,token).state==SweetRPMhPending); // watchdog does not poll
 regs[TCS+4]=1;regs[G+0x304]=GB;
 assert(sweet_rpmh_irq(37));assert(!s->fIRQArmed&&s->fIRQPosted&&!s->fIRQObserved);
 assert(!perWrites()&&queued.size()==1&&status(s,id,token).state==SweetRPMhPending);
 assert(sweet_rpmh_irq(37));assert(queued.size()==1); // duplicate top half cannot duplicate completion
 regs[G+0x304]=0;pump();assert(s->fIRQObserved&&s->fBusy&&s->fOperationCount);
 assert(!s->fOwned[0]&&!acknowledgements()); // workloop checks before cleanup/ledger
 tick(s);assert(status(s,id,token).state==SweetRPMhComplete&&s->fOwned[0]&&acknowledgements()==1);
 assert(!(regs[G+0x104]&GB)&&regs[TCS]==0x41);dispose(s,n);
 // Reply can arrive inside the trigger write, before GIC enable. Level
 // pending is retained; the provider must not erase it after triggering.
 fixtures();n=nub();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});
 fastAtTrigger=true;tick(s);fastAtTrigger=false;
 assert(s->fExecuting&&regs[TCS+4]==1&&!s->fIRQObserved);assert(deliverRPMhIRQ());tick(s);
 assert(status(s,id,token).state==SweetRPMhComplete);dispose(s,n);
 // Masked old status revealed when source enables must never reach trigger.
 fixtures();resourceMode=2;n=nub();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});
 staleUnmask=true;tick(s);staleUnmask=false;assert(!s->fExecuting&&status(s,id,token).state==SweetRPMhFailed);
 for(auto&o:trace)assert(!o.write||o.addr!=TCS+0x14||o.value!=0x1010000);
 assert(!s->fOwned[0]&&regs[TCS+4]==1);dispose(s,n);
 // No callback: even raw IRQ + complete commands cannot fake a fresh event.
 fixtures();n=nub();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);
 regs[TCS+4]=1;auto before=acknowledgements();fakeTicks+=100000;s->fTimer->action(s,s->fTimer);
 assert(s->fBusy&&!s->fIRQObserved&&acknowledgements()==before);
 fakeTicks+=100000001;s->fTimer->action(s,s->fTimer);assert(status(s,id,token).state==SweetRPMhFailed&&s->fPoisoned);
 assert(acknowledgements()==before&&regs[TCS+4]==1&&regs[TCS+0x1c]==7&&!(regs[TCS]&1)&&!(regs[G+0x104]&GB));dispose(s,n);
 // An IRQ without all command ACKs (or a foreign source) fails explicitly;
 // it must not acknowledge foreign status or publish the vote as successful.
 for(bool foreign:{false,true}){fixtures();n=nub();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);
  before=acknowledgements();regs[TCS+4]=foreign?0x40:1;if(!foreign)regs[TCS+0x50]=1<<8;
  assert(deliverRPMhIRQ());assert(status(s,id,token).state==SweetRPMhFailed&&!s->fOwned[0]);
  assert(acknowledgements()==before&&regs[TCS+4]==(foreign?0x40u:1u)&&!(regs[G+0x104]&GB));dispose(s,n);
 }
 // RWP is timer-driven; no payload or trigger before it drains, and bounded
 // failure never spins or invents a lease. No delay() calls exist in IRQ path.
 fixtures();regs[G]|=1u<<31;n=nub();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);
 assert(s->fBusy&&!s->fExecuting&&!perWrites());before=writes();tick(s);assert(writes()==before);
 tick(s,1001);assert(status(s,id,token).state==SweetRPMhFailed&&!perWrites());dispose(s,n);
 // Stop waits on the physical dispatch lock before removing callback/event.
 // A top half already in progress cannot outlive mapping or enqueue after free.
 fixtures();n=nub();s=make(n);id=attach(s);token=vote(s,id,SweetRailVDDI,{true,1800,1880,62000});tick(s);
 pauseIRQWrite=true;enteredRead=releaseRead=false;
 std::thread irqThread([&]{inIRQTop=true;assert(sweet_rpmh_irq(37));inIRQTop=false;});
 {std::unique_lock<std::mutex>l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
 std::atomic<bool>stopEntered{false},stopDone{false};std::thread stopper([&]{stopEntered=true;s->stop(n);stopDone=true;});
 while(!stopEntered)std::this_thread::yield();assert(!stopDone);
 while(__atomic_load_n(&s->fIRQOnline,__ATOMIC_ACQUIRE))std::this_thread::yield();
 {std::lock_guard<std::mutex>l(pauseLock);releaseRead=true;pauseCV.notify_all();}
 irqThread.join();stopper.join();pauseIRQWrite=false;assert(!rpmhIRQService&&!s->fIRQEventAdded);
 auto size=trace.size();s->fIRQPosted=1;SweetRPMh::irqAction(s,s->fIRQEvent,1);s->fTimer->action(s,s->fTimer);
 assert(sweet_rpmh_irq(37)&&!sweet_rpmh_irq(38)&&trace.size()==size);
 s->release();dropNub(n);reboot();assert(!live&&!liveMaps&&queued.empty());
 puts("PASS: real RPMh INTID37 top half/event/workloop; fast completion with unchanged stale command bits, pre-trigger stale rejection, no polling/raw-status fallback, no lost/foreign/partial IRQ ACK, bounded RWP, neighbours preserved, ABI/map/owner rejection, stop vs physical callback, late delivery and no leaked objects/maps");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-rpmh-irq-') as temp:
    cpp=Path(temp)/'test.cpp';binary=Path(temp)/'test'
    cpp.write_text(prelude+common['DSI_MEMORY_FIXTURE']+common['pool_fixture']+common['header']+common['source']+helpers+tests)
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(binary)],check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(binary)],check=True,env=env,timeout=45)
