#!/usr/bin/env python3
"""Exercise real NoC completion with independently supplied hardware status.
Usage: test-mh2lm-noc-completion-evidence.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
fixture = Path(__file__).resolve().with_name('test-mh2lm-display-noc.py')
checks = r'''
static unsigned count(const char*needle){unsigned n=0;for(auto&s:messages)n+=s.find(needle)!=std::string::npos;return n;}
static std::string message(const char*needle){for(auto&s:messages)if(s.find(needle)!=std::string::npos)return s;assert(false);return {};}
static uint64_t field(const std::string&s,const char*name){
 std::string key=std::string(" ")+name+"=";auto p=s.find(key);assert(p!=std::string::npos);
 return strtoull(s.c_str()+p+key.size(),nullptr,0);
}
struct Harness{
 IOService*n,*d;SweetRPMh*r;SweetMDSS*m;uint64_t token;
 Harness(bool together=false,bool useThird=false){
  nocFixtures();if(together)for(unsigned b=1;b<6;++b)regs[DB+564+8*b]&=0xffff;
  if(useThird)regs[DT+2*0x2a0+0x14]=1; // occupied but no conflicting payload
  n=nub();r=make(n);d=displayNub();m=displayMake(d,r);messages.clear();trace.clear();token=retain(m);
 }
 ~Harness(){cleanup(m,d,r,n);}
 void triggered(){for(unsigned i=0;i<256&&!(m->fNoCPhase==NData&&m->fNoCExecuting&&!m->fNoCOps);++i)step(m,r,true,false);
  assert(m->fNoCPhase==NData&&m->fNoCExecuting&&!m->fNoCOps);}
 void poll(){step(m,r,true,false);}
 void expire(bool overall=false){fakeTicks=overall?m->fNoCDeadline:m->fNoCStepDeadline;SweetMDSS::noCTimerAction(m,m->fNoCTimer);}
 void hardware(unsigned idle,uint32_t cmd,bool irq=false){
  auto b=DT+m->fNoCTCS*0x2a0;regs[b+0x18]=idle;
  for(unsigned c=0;c<m->fNoCGroup;++c)regs[b+0x3c+c*0x14]=cmd;
  if(irq)regs[DT+4]|=1u<<m->fNoCTCS;
 }
 void failed(IOReturn result=kIOReturnTimeout){auto s=nstatus(m,token);assert(s.state==SweetNoCFailed&&s.result==result&&s.uncertain);
  assert(m->fNoCFault&&m->fNoCClockToken&&m->fNoCRegisterToken);
  auto n=trace.size(),logs=messages.size();SweetMDSS::noCTimerAction(m,m->fNoCTimer);assert(trace.size()==n&&messages.size()==logs);}
 void report(unsigned id){auto s=nstatus(m,token);
  printf("case %u state %u result %d uncertain %u ack %u owned %u clocks %u cursor %u progress %u timer %u now %llu\n",
   id,s.state,s.result,unsigned(s.uncertain),s.acknowledged,m->fNoCOwned,m->fClockOwnedMask,m->fNoCCursor,
   unsigned(m->fNoCProgress),m->fNoCTimer->timeout,(unsigned long long)fakeTicks);
  for(auto&o:trace)printf("%c %llx %x\n",o.write?'W':'R',(unsigned long long)o.addr,o.value);
 }
};
static void evidence(Harness&h,const char*reason,unsigned polls,bool triggered,bool timeout,unsigned commands=1){
 if(!DIAG_CHECK)return;
 assert(count("SWEET MDSS NOC EVIDENCE:")==1);
 auto e=message("SWEET MDSS NOC EVIDENCE:");assert(e.find(std::string("reason=")+reason)!=std::string::npos);
 assert(field(e,"now-ns")==fakeTicks&&field(e,"group-valid")==1&&field(e,"group-count")==commands);
 assert(field(e,"group-deadline-ns")-field(e,"group-ns")==100000000);
 assert(field(e,"deadline-ns")-field(e,"request-ns")==1500000000);
 assert(bool(field(e,"trigger-issued-ns"))==triggered);
 auto a=message("stage=trigger"),b=message("stage=group-timeout");
 assert(field(a,"valid")==triggered&&field(b,"valid")==timeout);
 if(triggered){assert(field(a,"sampled-ns")>=field(e,"trigger-issued-ns"));
  assert(field(a,"irq-enable")&(1u<<field(e,"tcs")));
  assert(field(a,"cmd-enable")==((1u<<commands)-1)&&field(a,"wait")==1u<<(commands-1));}
 if(timeout)assert(field(b,"sampled-ns")==fakeTicks);
 auto p=message("SWEET MDSS NOC POLL:");assert(field(p,"samples")==polls);
 if(polls)assert(field(p,"first-ns")<=field(p,"last-ns")&&field(p,"last-ns")<=fakeTicks);
 else assert(!field(p,"first-ns")&&!field(p,"last-ns"));
 assert(count("SWEET MDSS NOC COMMAND:")==commands&&count("SWEET MDSS NOC REGISTERS:")==2);
 unsigned c=0;for(auto&s:messages)if(s.find("SWEET MDSS NOC COMMAND:")!=std::string::npos){
  unsigned r=unsigned(field(s,"resource"));assert(field(s,"index")==c&&r>0&&r<6);
  assert(field(s,"address")==0x50000+4*r&&field(s,"vcd")==((regs[DB+564+8*r]>>16)&15u));
  assert(field(s,"planned-msg")== (0x10008u|(c+1==commands?0x100u:0u)));
  assert(field(s,"planned-data")== (h.m->fNoCValues[r]|(c+1==commands?1u<<30:0u)));
  assert(field(s,"required")== (0x100u|(c+1==commands?0x10000u:0u)));
  assert(field(s,"status-valid")==unsigned(polls!=0));++c;
 }
}
static unsigned peripheralWrites(){unsigned n=0;for(auto&o:trace)n+=o.write&&o.addr>=DR&&o.addr<DW+0x4000;return n;}
static unsigned tcsWrites(){unsigned n=0;for(auto&o:trace)n+=o.write&&o.addr>=DT&&o.addr<DT+0x3000;return n;}
static unsigned clears(){unsigned n=0;for(auto&o:trace)n+=o.write&&o.addr==DT+8;return n;}
static void onlyGICSince(size_t start){for(size_t i=start;i<trace.size();++i)assert(trace[i].write&&trace[i].addr==0x17a00194);}
int main(){unsigned cases=0;
 // The explicit IRQ extension cannot silently fall back to polling.
 for(unsigned fault=0;fault<7;++fault){nocFixtures();auto*d=displayNub();
  if(fault==0){d->properties["sweet,display-noc-irq-abi"]->release();d->properties.erase("sweet,display-noc-irq-abi");}
  if(fault==1){uint32_t bad=160;d->properties["sweet,display-noc-intid"]->release();d->properties["sweet,display-noc-intid"]=OSData::withBytes(&bad,4);}
  if(fault==2)missingMap=11;if(fault==3)badMap=11;if(fault==4)shortMap=11;
  if(fault==5)regs[0x17a00114]=2;if(fault==6)regs[0x17a00214]=2;
  auto*m=new SweetMDSS();assert(!m->start(d)&&!writes());m->release();queued.clear();dropNub(d);
  assert(!live&&!liveMaps&&!rawBlocks&&!mdssNoCIRQService);missingMap=badMap=shortMap=~0u;++cases;
 }
 // GIC configuration preserves neighbours; SW owner only reuses its AMC.
 {Harness h;regs[0x17a00094]=0xa5a50000;regs[0x17a00c28]=0x5a5a00ff;regs[0x17a004a0]=0x12345678;
  h.triggered();assert((regs[0x17a00094]&~2u)==0xa5a50000);
  assert((regs[0x17a00c28]&~0xcu)==(0x5a5a00ffu&~0xcu));assert((regs[0x17a004a0]&~0xff00u)==(0x12345678u&~0xff00u));
  drive(h.m,h.r);
  std::vector<uint32_t> controls;for(auto&o:trace)if(o.write&&o.addr==DT+2*0x2a0+0x14)controls.push_back(o.value);
  assert(controls.size()==16);assert((std::vector<uint32_t>(controls.begin(),controls.begin()+4)==std::vector<uint32_t>{0,0,0x10000,0x1010000}));
  assert((std::vector<uint32_t>(controls.begin()+4,controls.begin()+8)==std::vector<uint32_t>{0x10000,0,0x10000,0x1010000}));
  assert(regs[DT+2*0x2a0+0x14]==0x1010000&&!(regs[0x17a00114]&2));
  assert(h.m->releaseBandwidth(h.token)==0);drive(h.m,h.r);++cases;}

 // A timer cannot turn stale/changed command status into this request's ACK.
 for(unsigned state=0;state<4;++state){Harness h;h.triggered();
  assert(regs[DT]&(1u<<h.m->fNoCTCS));h.hardware(state==1?0:1,state==2?0x100u:(state==3?~0u:0x10100u));
  for(unsigned p=0;p<3;++p)h.poll();assert(!nstatus(h.m,h.token).acknowledged&&!h.m->fNoCEvidence.polls);
  h.expire();h.failed();evidence(h,"group-deadline",0,true,true);++cases;
 }
 // Raw status without a physical callback is also insufficient.
 {Harness h;h.triggered();h.hardware(1,0x10100,true);trace.clear();
  SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);assert(!h.m->fNoCIRQObserved&&!clears());
  for(auto&o:trace)assert(o.addr!=DT+4&&o.addr!=DT+h.m->fNoCTCS*0x2a0+0x3c);
  h.expire();h.failed();assert(!clears()&&regs[DT+4]&4);++cases;}
 // Unchanged inherited command bits + IRQ latched inside trigger, before
 // GIC enable. Source unmask precedes trigger; no clear after trigger.
 {Harness h;fastNoC=true;h.triggered();fastNoC=false;assert(!h.m->fNoCIRQObserved&&(regs[DT+4]&4));
  size_t unmask=0,trigger=0;for(size_t i=0;i<trace.size();++i)if(trace[i].write){auto&o=trace[i];
   if(o.addr==DT&&(o.value&4))unmask=i;if(o.addr==DT+2*0x2a0+0x14&&o.value==0x1010000)trigger=i;}
  assert(unmask<trigger&&trigger);for(size_t i=trigger;i<trace.size();++i)assert(!trace[i].write||(trace[i].addr!=DT+8&&trace[i].addr!=0x17a00294));
  auto before=peripheralWrites();regs[0x17a00314]=2;assert(sweet_mdss_noc_irq(161));
  assert(h.m->fNoCIRQPosted&&!h.m->fNoCIRQObserved&&peripheralWrites()==before);assert(sweet_mdss_noc_irq(161));
  regs[0x17a00314]=0;pump();assert(h.m->fNoCIRQObserved&&h.m->fNoCOps&&!nstatus(h.m,h.token).acknowledged);
  drive(h.m,h.r);assert(nstatus(h.m,h.token).acknowledged==0x3e);++cases;}
 // Enabling source reveals an old masked IRQ: stop before trigger, preserve it.
 {Harness h;staleNoC=true;for(unsigned i=0;i<256&&h.m->noCBusy();++i)h.poll();staleNoC=false;
  h.failed(kIOReturnIOError);assert(!h.m->fNoCExecuting&&regs[DT+4]&4&&!clears());++cases;}
 // Partial response, non-idle, invalid status and foreign source cannot ACK.
 for(unsigned fault=0;fault<4;++fault){Harness h;h.triggered();h.hardware(fault==1?0:1,fault==0?0:0x10100,true);
  if(fault==2)regs[DT+h.m->fNoCTCS*0x2a0+0x18]=~0u;
  if(fault==3){regs[DT]|=8;regs[DT+4]=8;}
  h.poll();h.failed(kIOReturnIOError);assert(!nstatus(h.m,h.token).acknowledged&&!clears());++cases;}
 // Lost access: no RSC accesses at the overall deadline or in stop.
 {Harness h;h.triggered();h.m->fClockFault=true;regs[bases[1]+0x3000]=0;
  auto before=trace.size();h.expire(true);onlyGICSince(before);h.failed();evidence(h,"overall-deadline",0,true,false);++cases;}
 for(unsigned gate=0;gate<3;++gate){Harness h;h.triggered();
  if(gate==0)h.m->fClockFault=true;if(gate==1)regs[bases[1]+0x3000]=0;if(gate==2)regs[DW+4]=1;
  auto before=trace.size();h.expire();h.failed(kIOReturnNotReady);evidence(h,"provider",0,true,false);
  for(size_t i=before;i<trace.size();++i)assert(!(trace[i].addr>=DT&&trace[i].addr<DT+0x3000));++cases;}
 for(unsigned failure=0;failure<3;++failure){Harness h;
  if(failure==1){h.triggered();rpmhDrop=h.m->fNoCTCS*0x2a0+0x1c;h.hardware(1,0x10100,true);h.poll();}
  else{rpmhDrop=2*0x2a0+0x30;for(unsigned i=0;i<256&&!h.m->fNoCWriteIssued;++i)h.poll();}
  assert(h.m->fNoCWriteIssued);if(failure==2)h.expire();else{fakeTicks=h.m->fNoCWriteDeadline;SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);}
  h.failed(failure==2?kIOReturnTimeout:kIOReturnIOError);evidence(h,failure==2?"group-deadline":"write-readback",failure==1?1:0,failure==1,failure==2);++cases;}
 {Harness h;h.triggered();auto before=trace.size();messages.clear();h.m->stopNoC();onlyGICSince(before);
  h.failed(kIOReturnAborted);assert(!count("SWEET MDSS NOC EVIDENCE:"));++cases;}
 for(unsigned mode=0;mode<2;++mode){Harness h(mode==0,mode==1);h.triggered();h.expire();h.failed();evidence(h,"group-deadline",0,true,true,mode==0?5:1);
  assert(h.m->fNoCTCS==(mode==0?2u:3u));if(mode==1)assert(regs[DT+2*0x2a0+0x14]==1);++cases;}
 {Harness h;h.triggered();h.hardware(1,0x10100,true);h.poll();assert(h.m->fNoCCursor==1);
  h.triggered();h.expire();h.failed();evidence(h,"group-deadline",0,true,true);
  assert(field(message("SWEET MDSS NOC EVIDENCE:"),"group-cursor")==1);
  assert(nstatus(h.m,h.token).acknowledged==1u<<SweetBCMMC0);++cases;}
 {Harness h;drive(h.m,h.r);assert(h.m->releaseBandwidth(h.token)==0);messages.clear();h.expire(true);
  assert(!field(message("SWEET MDSS NOC EVIDENCE:"),"group-valid"));assert(!count("SWEET MDSS NOC COMMAND:"));++cases;}
 // CLK transition plus TCS busy grant/retry precede every initialization write.
 for(bool stuck:{false,true}){Harness h;regs[DW+4]=0x101;regs[DW]=2;regs[DR+0x404]=1;regs[DR+0xc2c]=1;
  while(h.m->fNoCRSCStage!=2&&h.m->noCBusy())h.poll();assert(h.m->fNoCRSCStage==2&&!h.m->fNoCRSCInitialized&&!tcsWrites());
  auto retry=h.m->fNoCRSCRetryAt;assert(!(regs[DW+4]&1)&&regs[DW]==3);
  fakeTicks=retry-1;SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);assert(regs[DW]==3);
  fakeTicks=retry;SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);assert(regs[DW]==2&&!h.m->fNoCRSCInitialized);
  if(stuck){fakeTicks=h.m->fNoCRSCDeadline;SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);h.failed();assert(!tcsWrites()&&regs[DW]==3);}
  else{regs[DW]=0;regs[DR+0x404]=0;drive(h.m,h.r);assert(h.m->fNoCRSCInitialized&&regs[DW]==1&&regs[DW+4]==256);
   assert(regs[DR+0xc2c]==1&&regs[DR+0xc20]==7); // last selected mode is not an idle bit
   unsigned init=0;for(auto&o:trace)if(o.write&&o.addr==DR+0x600)++init;assert(init==1);
   assert(h.m->releaseBandwidth(h.token)==0);drive(h.m,h.r);
   init=0;for(auto&o:trace)if(o.write&&o.addr==DR+0x600)++init;assert(init==1);}
  ++cases;}
 // A dropped initialization write cannot proceed to payload/trigger.
 {Harness h;dropNoCInit=true;for(unsigned i=0;i<256&&h.m->noCBusy();++i)h.poll();dropNoCInit=false;
  h.failed(kIOReturnIOError);assert(!h.m->fNoCRSCInitialized&&!tcsWrites());++cases;}
 // Prior EOI and GIC RWP must drain asynchronously before a new generation.
 {Harness h;while(!h.m->fNoCClockToken&&h.m->noCBusy())h.poll();regs[0x17a00000]|=1u<<31;for(unsigned i=0;i<256&&!h.m->fNoCIRQTouched;++i)h.poll();
  assert(h.m->fNoCIRQTouched&&!tcsWrites());fakeTicks=h.m->fNoCIRQDeadline;SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
  h.failed(kIOReturnIOError);assert(!tcsWrites());++cases;}
 {Harness h;h.triggered();h.hardware(1,0x10100,true);regs[0x17a00314]=2;assert(sweet_mdss_noc_irq(161));pump();
  h.poll();assert(h.m->fNoCCursor==1);auto before=tcsWrites();h.poll();assert(tcsWrites()==before);
  regs[0x17a00314]=0;drive(h.m,h.r);++cases;}
 // Stopped late callback and stop vs top-half map/event lifetime.
 {Harness h;h.triggered();pauseNoCIRQ=true;enteredRead=releaseRead=false;
  std::thread irq([&]{inNoCIRQ=true;assert(sweet_mdss_noc_irq(161));inNoCIRQ=false;});
  {std::unique_lock<std::mutex>l(pauseLock);pauseCV.wait(l,[]{return enteredRead;});}
  std::atomic<bool>done{false};std::thread stop([&]{h.m->stop(h.d);done=true;});
  while(__atomic_load_n(&h.m->fNoCIRQOnline,__ATOMIC_ACQUIRE))std::this_thread::yield();assert(!done);
  {std::lock_guard<std::mutex>l(pauseLock);releaseRead=true;pauseCV.notify_all();}
  irq.join();stop.join();pauseNoCIRQ=false;
  auto before=trace.size();pump();assert(sweet_mdss_noc_irq(161)&&!sweet_mdss_noc_irq(162));assert(trace.size()==before&&!mdssNoCIRQService);
  ++cases;}
 printf("PASS: %u real provider scenarios: v2 initialization, CLK handoff, TCS retry, source/GIC/IRQ ordering, fast/foreign/partial/stale completion, watchdog/evidence, EOI/RWP, stop callback lifetime\n",cases);
}

'''


def run(tree, diagnostic):
    sys.argv[:] = [sys.argv[0], str(tree)]
    context = {'__file__': str(fixture), '__name__': 'noc_completion_fixture'}
    exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
                 str(fixture), 'exec'), context)
    actual = context['ns']
    context['prelude'] = context['prelude'].replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){', r'''
static bool fastNoC=false,staleNoC=false,dropNoCInit=false,pauseNoCIRQ=false;
static thread_local bool inNoCIRQ=false;
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(pauseNoCIRQ&&inNoCIRQ&&b==0x17a00000&&o==0x194){std::unique_lock<std::mutex>l(pauseLock);enteredRead=true;pauseCV.notify_all();pauseCV.wait(l,[]{return releaseRead;});}
 if(dropNoCInit&&b==0xaf20000&&o==0x600)return;
 if(fastNoC&&b==0xaf21c00&&o==2*0x2a0+0x14&&v==0x1010000)regs[b+4]|=4;
 if(staleNoC&&b==0xaf21c00&&o==0&&(v&4)&&regs[b+2*0x2a0+0x1c])regs[b+4]|=4;
''')
    source = (context['prelude'] + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] +
              actual['header'] + actual['source'] + context['helpers'] +
              context['tests'].split('int main(){')[0] + checks)
    with tempfile.TemporaryDirectory(prefix='mh2lm-noc-evidence-') as temp:
        cpp, binary = Path(temp)/'test.cpp', Path(temp)/'test'
        cpp.write_text(source)
        subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        f'-DDIAG_CHECK={int(diagnostic)}', str(cpp), '-o', str(binary)], check=True)
        return subprocess.check_output([str(binary)], timeout=60,
                                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                                                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))


target = Path(sys.argv[1])
if len(sys.argv) != 2:
    sys.exit('0216 changes completion semantics; the historical a27 trace-equivalence check no longer applies')
print(run(target, True).decode(), end='')
