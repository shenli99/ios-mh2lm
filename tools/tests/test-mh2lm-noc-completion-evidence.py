#!/usr/bin/env python3
"""Exercise real NoC completion with independently supplied hardware status.
Usage: test-mh2lm-noc-completion-evidence.py EXTRACTED_XNU [A27_BASELINE_XNU]
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
  assert(!(field(a,"irq-enable")&(1u<<field(e,"tcs"))));
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
int main(){unsigned scenario=0;
 // A source-masked, instantly complete transaction is observationally the
 // same as stale command status: it MUST remain unacknowledged in this patch.
 for(unsigned state=0;state<4;++state){Harness h;h.triggered();
  assert(!(regs[DT]&(1u<<h.m->fNoCTCS)));
  h.hardware(state==1?0:1,state==2?0x100u:(state==3?~0u:0x10100u));
  for(unsigned p=0;p<3;++p)h.poll();assert(!nstatus(h.m,h.token).acknowledged);
  h.expire();h.failed();evidence(h,"group-deadline",3,true,true);
  if(DIAG_CHECK){auto p=message("SWEET MDSS NOC POLL:");
   assert(field(p,"seen-busy")==unsigned(state==1)&&field(p,"seen-incomplete")==unsigned(state>=2));
   assert(field(p,"last-complete")==unsigned(state<2)&&!field(p,"seen-irq"));
   auto cmd=message("SWEET MDSS NOC COMMAND:");assert(field(cmd,"last-status")==regs[DT+h.m->fNoCTCS*0x2a0+0x3c]);}
  h.report(scenario++);
 }
 // A sampled busy/incomplete transition, or an independent raw completion
 // IRQ, still completes every group. No diagnostic state authorizes success.
 for(bool irq:{false,true}){Harness h;for(unsigned ticks=0;ticks<256&&h.m->noCBusy();++ticks){
   if(h.m->fNoCExecuting&&!h.m->fNoCOps){
    if(!irq&&!h.m->fNoCProgress)h.hardware(0,0);else h.hardware(1,0x10100,irq);
   }h.poll();}
  assert(nstatus(h.m,h.token).state==SweetNoCHeld&&nstatus(h.m,h.token).acknowledged==0x3e);
  if(DIAG_CHECK)assert(!count("SWEET MDSS NOC EVIDENCE:"));h.report(scenario++);
 }
 // The overall timeout precedes admission. Losing every access condition
 // must cause zero MMIO here; the last poll remains explicitly historical.
 {Harness h;h.triggered();h.hardware(0,0);h.poll();auto at=fakeTicks;
  h.m->fClockFault=true;regs[bases[1]+0x3000]=0;auto before=trace.size();
  h.expire(true);assert(trace.size()==before);h.failed();evidence(h,"overall-deadline",1,true,false);
  if(DIAG_CHECK)assert(field(message("SWEET MDSS NOC POLL:"),"last-ns")==at);h.report(scenario++);}
 // Group timeout before the first poll: status-valid=0, not hardware zero.
 {Harness h;h.triggered();h.expire();h.failed();evidence(h,"group-deadline",0,true,true);h.report(scenario++);}
 // Admission loss before a group timeout forbids the new register sample.
 for(unsigned gate=0;gate<3;++gate){Harness h;h.triggered();h.poll();
  if(gate==0)h.m->fClockFault=true;if(gate==1)regs[bases[1]+0x3000]=0;if(gate==2)regs[DW+4]=1;
  auto before=trace.size();h.expire();h.failed(kIOReturnNotReady);evidence(h,"provider",1,true,false);
  for(unsigned i=before;i<trace.size();++i)assert(!(trace[i].addr>=DT&&trace[i].addr<DT+0x3000));
  h.report(scenario++);}
 for(unsigned bad=0;bad<2;++bad){Harness h;h.triggered();h.hardware(bad?~0u:1,0x10100);
  if(!bad)regs[DT+4]=~0u;h.poll();h.failed(kIOReturnIOError);evidence(h,"invalid-status",1,true,false);h.report(scenario++);}
 // Failed programming, cleanup, and group deadline during programming.
 for(unsigned failure=0;failure<3;++failure){Harness h;
  if(failure==1){h.triggered();h.hardware(1,0x10100,true);h.poll();rpmhDrop=h.m->fNoCTCS*0x2a0+0x14;h.poll();}
  else{rpmhDrop=2*0x2a0+0x30;for(unsigned i=0;i<256&&!h.m->fNoCWriteIssued;++i)h.poll();}
  assert(h.m->fNoCWriteIssued);
  if(failure==2)h.expire();else{fakeTicks=h.m->fNoCWriteDeadline;SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);}
  h.failed(failure==2?kIOReturnTimeout:kIOReturnIOError);
  evidence(h,failure==2?"group-deadline":"write-readback",failure==1?1:0,failure==1,failure==2);
  if(DIAG_CHECK){auto read=message("SWEET MDSS NOC READBACK:");assert(field(read,"valid")&&field(read,"want")!=field(read,"got")&&field(read,"posted"));}
  h.report(scenario++);}
 {Harness h;h.triggered();h.poll();auto before=trace.size();messages.clear();h.m->stopNoC();
  assert(before==trace.size());h.failed(kIOReturnAborted);if(DIAG_CHECK)assert(!count("SWEET MDSS NOC EVIDENCE:"));h.report(scenario++);}
 // No group yet: one timing record, no inherited/default TCS samples.
 {Harness h;auto before=trace.size();h.expire(true);assert(before==trace.size());
  assert(nstatus(h.m,h.token).result==kIOReturnTimeout);
  if(DIAG_CHECK){assert(!field(message("SWEET MDSS NOC EVIDENCE:"),"group-valid"));assert(!count("SWEET MDSS NOC REGISTERS:"));}
  h.report(scenario++);}
 // Bounds and slot identity: five commands at one VCD; occupied ACTIVE2
 // forces ACTIVE3. Failure does not touch the inherited ACTIVE2 control.
 for(unsigned mode=0;mode<2;++mode){Harness h(mode==0,mode==1);h.triggered();h.hardware(1,0);
  h.poll();h.expire();h.failed();evidence(h,"group-deadline",1,true,true,mode==0?5:1);
  assert(h.m->fNoCTCS==(mode==0?2u:3u));if(mode==1)assert(regs[DT+2*0x2a0+0x14]==1);h.report(scenario++);}
 // A later group's failure must not reuse the first group's poll evidence.
 {Harness h;h.triggered();h.hardware(1,0x10100,true);h.poll();h.poll();assert(h.m->fNoCCursor==1);
  h.triggered();h.poll();h.expire();h.failed();evidence(h,"group-deadline",1,true,true);
  if(DIAG_CHECK)assert(field(message("SWEET MDSS NOC EVIDENCE:"),"group-cursor")==1);
  assert(nstatus(h.m,h.token).acknowledged==1u<<SweetBCMMC0);h.report(scenario++);}
 // A new request that expires before queueing cannot expose the last request's group.
 {Harness h;drive(h.m,h.r);assert(h.m->releaseBandwidth(h.token)==0);messages.clear();h.expire(true);
  if(DIAG_CHECK){assert(!field(message("SWEET MDSS NOC EVIDENCE:"),"group-valid"));assert(!count("SWEET MDSS NOC COMMAND:"));}
  h.report(scenario++);}
 assert(scenario==22);
}
'''


def run(tree, diagnostic):
    sys.argv[:] = [sys.argv[0], str(tree)]
    context = {'__file__': str(fixture), '__name__': 'noc_completion_fixture'}
    exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
                 str(fixture), 'exec'), context)
    actual = context['ns']
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
baseline = Path(sys.argv[2]) if len(sys.argv) == 3 else None
result = run(target, True)
if baseline:
    # This four-read sequence occurs only in the two new sample sites.
    # Normal programming reads IRQ_ENABLE then writes it, never this sequence.
    lines = result.splitlines(keepends=True)
    filtered, samples = [], 0
    i = 0
    while i < len(lines):
        keys = [line.split()[:2] for line in lines[i:i+4]]
        match = any(keys == [[b'R', addr] for addr in addresses] for addresses in (
            [b'af21c00', b'af22154', b'af2215c', b'af22150'],
            [b'af21c00', b'af223f4', b'af223fc', b'af223f0']))
        if match:
            samples += 1
            i += 4
        else:
            filtered.append(lines[i])
            i += 1
    assert samples > 0
    assert b''.join(filtered) == run(baseline, False), 'MMIO, outcome, ledger, timer or time changed'
    print(f'Allowed additional evidence reads: {samples} four-register samples')
print('PASS: 22 actual provider scenarios; masked fast completion, busy/missing/invalid status, '
      'independent completion, overall/group/readback faults, access loss, stop, multi-command/slot/group/request; '
      'ASan/UBSan; ' + ('all other traces match a27' if baseline else 'no baseline supplied'))
