#!/usr/bin/env python3
"""Actual clock worker timeout evidence, optionally compared with its baseline.
Usage: test-mh2lm-clock-timeout-diagnostics.py EXTRACTED_XNU [BASELINE_XNU]
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
fixture = Path(__file__).resolve().with_name('test-mh2lm-rsc-clock-votes.py')
checks = r'''
static unsigned count(const char*needle){unsigned n=0;for(auto&s:messages)n+=s.find(needle)!=std::string::npos;return n;}
static std::string message(const char*needle){for(auto&s:messages)if(s.find(needle)!=std::string::npos)return s;assert(false);return {};}
static uint64_t field(const std::string&s,const char*name){
 std::string key=std::string(" ")+name+"=";auto p=s.find(key);assert(p!=std::string::npos);
 return strtoull(s.c_str()+p+key.size(),nullptr,0);
}
static void diagnostics(bool timeout,const char*reason="",bool current=false,
                        bool sample=false,bool written=false,unsigned branch=0,
                        uint32_t cbcr=0,uint32_t before=0,uint32_t want=0,bool releasing=false){
 if(!DIAG_CHECK)return;
 assert(count("SWEET MDSS CLOCK TIMEOUT:")==unsigned(timeout));
 assert(count("SWEET MDSS CLOCK WRITE:")==unsigned(timeout));
 assert(count("SWEET MDSS CLOCK SAMPLE:")==unsigned(timeout));
 if(!timeout)return;
 auto t=message("SWEET MDSS CLOCK TIMEOUT:"),w=message("SWEET MDSS CLOCK WRITE:"),s=message("SWEET MDSS CLOCK SAMPLE:");
 assert(t.find(std::string("reason=")+reason)!=std::string::npos);
 assert(field(t,"release")==unsigned(releasing)&&field(s,"current")==unsigned(current));
 assert(field(s,"valid")==unsigned(sample)&&field(w,"valid")==unsigned(written));
 auto now=field(t,"now-ns"),deadline=field(t,"request-deadline-ns"),wait=field(t,"wait-deadline-ns");
 assert(now==fakeTicks&&field(t,"request-late-ns")== (now>=deadline?now-deadline:0));
 assert(field(t,"wait-late-ns")== (wait&&now>=wait?now-wait:0));
 if(sample){if(field(s,"branch")!=branch||field(s,"cbcr")!=cbcr){fprintf(stderr,"expected branch=%u cbcr=%#x\n%s%s%s",branch,cbcr,t.c_str(),w.c_str(),s.c_str());}
  assert(field(s,"branch")==branch&&field(s,"cbcr")==cbcr);
  assert(field(s,"enable")==unsigned(bool(cbcr&1))&&field(s,"off")==unsigned(bool(cbcr&0x80000000u)));
  assert(field(s,"sample-ns")<=now&&field(s,"age-ns")==now-field(s,"sample-ns"));
  assert(field(s,"parent-valid")==1&&field(s,"source")== (branch==9?0:4));
  if(branch==7||branch==8)assert(field(s,"cfg")==0x40f);
 }
 if(written){assert(field(w,"branch")==branch&&field(w,"before")==before&&field(w,"want")==want);
  assert(field(w,"entry-ns")<=field(w,"issued-ns")&&field(w,"issued-ns")<=now);
 }
}
static void report(unsigned scenario,Harness&h,uint64_t token){
 auto s=clockStatus(h,token);
 printf("case %u state %u result %d uncertain %u owned %u cursor %u waiting %u fault %u timer %u now %llu\n",
        scenario,s.state,s.result,unsigned(s.uncertain),h.m->fClockOwnedMask,h.m->fClockCursor,
        unsigned(h.m->fClockWaiting),unsigned(h.m->fClockFault),h.m->fClockTimer->timeout,(unsigned long long)fakeTicks);
 for(auto&o:trace)printf("%c %llx %x\n",o.write?'W':'R',(unsigned long long)o.addr,o.value);
}
static void lateSilent(Harness&h){auto n=trace.size(),logs=messages.size();
 SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);assert(n==trace.size()&&logs==messages.size());}
static void writeWithoutPhysicalTransition(Harness&h){
 for(unsigned j=0;j<100&&!(h.m->fClockWaiting&&h.m->fClockCursor==7);++j)
  clockTick(h,100,false,false,false);
 assert(h.m->fClockWaiting&&h.m->fClockCursor==7);
}
int main(){
 unsigned scenario=0;
 // Success has no timeout output, including a callback beyond the branch wait
 // deadline whose sample is ready (the original completion ordering).
 for(unsigned late=0;late<2;++late){Harness h;offLeaves();configReady(h);untilWrite(h,7);
  if(late){externalClocks();fakeTicks=h.m->fClockWaitDeadline;clockTick(h,0);}
  heldRSC(h);h.finish();diagnostics(false);report(scenario++,h,h.m->fNoCClockToken);}
 // Lost own vote with aggregate ON, own vote with CLK_OFF, and both missing.
 for(unsigned bad=0;bad<3;++bad){Harness h;offLeaves();configReady(h);
  if(bad!=1)dropClockWrite=bases[1]+0x4004;
  if(bad)regs[bases[1]+0x4004]|=0x80000000u;
  auto before=regs[bases[1]+0x4004];writeWithoutPhysicalTransition(h);
  auto token=h.m->fNoCClockToken;auto n=trace.size();auto expectedSample=regs[bases[1]+0x4004];
  messages.clear();fakeTicks=h.m->fClockWaitDeadline;
  clockTick(h,0,false);auto status=clockStatus(h,token);
  if(status.result!=int(kIOReturnTimeout)){fprintf(stderr,"bad=%u state=%u result=%#x\n",bad,status.state,unsigned(status.result));for(auto&s:messages)fputs(s.c_str(),stderr);}
  assert(status.result==int(kIOReturnTimeout));
  assert(h.m->fClockOwnedMask&0x80);noPayload();
  diagnostics(true,"branch-wait",true,true,true,7,expectedSample,before,before|1);
  for(unsigned j=n;j<trace.size();++j)assert(!trace[j].write);
  lateSilent(h);report(scenario++,h,token);dropClockWrite=0;
 }
 // Overall expiry before the first worker sample: neither cache nor write is
 // valid. No branch read is added to this early timeout exit.
 {Harness h;offLeaves();configReady(h);assert(h.m->clockBusy());
  auto token=h.m->fNoCClockToken;trace.clear();messages.clear();fakeTicks=h.m->fClockDeadline;
  clockTick(h,0);assert(clockStatus(h,token).result==int(kIOReturnTimeout));
  diagnostics(true,"overall");for(auto&o:trace)assert(o.addr!=bases[1]+0x4004);
  lateSilent(h);report(scenario++,h,token);}
 // Overall expiry takes precedence even if hardware became ready. The cached
 // pre-write sample is historical, not an invented current register value.
 for(unsigned ready=0;ready<2;++ready){Harness h;offLeaves();configReady(h);untilWrite(h,7);
  auto token=h.m->fNoCClockToken;messages.clear();trace.clear();
  if(ready)externalClocks();fakeTicks=h.m->fClockDeadline+1234;
  clockTick(h,0,false);assert(clockStatus(h,token).result==int(kIOReturnTimeout));
  diagnostics(true,"overall",false,true,true,7,0x2000,0x2000,0x2001);
  for(auto&o:trace)assert(o.addr!=bases[1]+0x4004);lateSilent(h);report(scenario++,h,token);}
 // A prior waiting sample is preserved with its age at the overall deadline.
 {Harness h;offLeaves();regs[bases[1]+0x4004]|=0x80000000u;configReady(h);writeWithoutPhysicalTransition(h);
  clockTick(h,100,false);auto token=h.m->fNoCClockToken;messages.clear();fakeTicks=h.m->fClockDeadline;
  clockTick(h,0,false);diagnostics(true,"overall",false,true,true,7,0x80002001,0x80002000,0x80002001);
  lateSilent(h);report(scenario++,h,token);}
 // Ordinary HALT release must observe OFF; voted release must read our own
 // enable clear. Both timeout paths preserve ownership and produce evidence.
 for(unsigned voted=0;voted<2;++voted){Harness h;offLeaves();configReady(h);heldRSC(h);
  auto token=h.m->fNoCClockToken;h.m->fNoCTarget.registerPeak=0;h.m->fNoCCursor=h.m->fNoCCount;
  assert(h.m->releaseLocalClocks(token)==0);if(voted)dropClockWrite=bases[1]+0x4004;
  unsigned branch=voted?7:9;
  for(unsigned j=0;j<100&&!(h.m->fClockCursor==branch&&h.m->fClockWaiting);++j)clockTick(h,100,true,voted);
  assert(h.m->fClockCursor==branch&&h.m->fClockWaiting);messages.clear();fakeTicks=h.m->fClockWaitDeadline;
  clockTick(h,0,true,voted);assert(clockStatus(h,token).result==int(kIOReturnTimeout));
  diagnostics(true,"branch-wait",true,true,true,branch,voted?0x2001:0x8000,
              voted?0x2001:0x8001,voted?0x2000:0x8000,true);
  assert((h.m->fClockOwnedMask&0x380)==0x380);lateSilent(h);report(scenario++,h,token);dropClockWrite=0;
 }
 // A new release clears previous acquisition diagnostics; expiry before its
 // first sample cannot present the old acquisition sample or write as valid.
 {Harness h;offLeaves();configReady(h);heldRSC(h);auto token=h.m->fNoCClockToken;
  h.m->fNoCTarget.registerPeak=0;h.m->fNoCCursor=h.m->fNoCCount;
  assert(h.m->releaseLocalClocks(token)==0);messages.clear();fakeTicks=h.m->fClockDeadline;
  clockTick(h,0);diagnostics(true,"overall",false,false,false,0,0,0,0,true);
  lateSilent(h);report(scenario++,h,token);}
 // Parent drift and stop are not Timeout, so no new timeout records appear.
 for(unsigned stop=0;stop<2;++stop){Harness h;offLeaves();configReady(h);untilWrite(h,7);
  auto token=h.m->fNoCClockToken;messages.clear();
  if(stop)h.m->stop(h.d);else {regs[bases[1]+0x22c0]^=0x80000000u;clockTick(h);}
  diagnostics(false);lateSilent(h);report(scenario++,h,token);}
 assert(scenario==14);
}
'''


def run(tree, diagnostic):
    sys.argv[:] = [sys.argv[0], str(tree)]
    context = {'__file__': str(fixture), '__name__': 'clock_timeout_fixture'}
    exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
                 str(fixture), 'exec'), context)
    core, actual = context['ns'], context['actual']
    source = (core['prelude'] + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] +
              actual['header'] + actual['source'] + core['helpers'] +
              context['tests'].split('int main(){')[0] + checks)
    with tempfile.TemporaryDirectory(prefix='mh2lm-clock-timeout-') as temp:
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
    assert result == run(baseline, False), 'diagnostics changed MMIO, result, ownership or timer state'
print('PASS: 14 actual clock worker scenarios; bounded timeout/write/sample records, '
      'own-enable/CLK_OFF evidence, deadline precedence, historical/unavailable samples, '
      'request reset, voted/HALT release, parent drift, stop/late silence; ' +
      ('baseline MMIO/results/timer state identical' if baseline else 'ASan/UBSan'))
