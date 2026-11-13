#!/usr/bin/env python3
"""Actual bounded warm/pre-write local context and timeout reset evidence; compare with 0212.
Usage: test-mh2lm-rsc-local-reset-evidence.py EXTRACTED_XNU [BASELINE_XNU]
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
 assert(count("SWEET MDSS CLOCK READBACK:")==unsigned(timeout));
 assert(count("SWEET MDSS CLOCK OBSERVED:")==unsigned(timeout));
 assert(count("SWEET MDSS CLOCK RSC PREFLIGHT:")==unsigned(timeout));
 assert(count("SWEET MDSS CLOCK LOCAL:")==2u*unsigned(timeout));
 assert(count("SWEET MDSS CLOCK RESET:")==unsigned(timeout));
 if(!timeout)return;
 auto post=message("SWEET MDSS CLOCK READBACK:"),history=message("SWEET MDSS CLOCK OBSERVED:");
 assert(field(post,"valid")==unsigned(written&&branch==7));
 if(!written){assert(field(post,"pa")==0&&field(history,"wait-samples")==0);}
 auto warm=message("SWEET MDSS CLOCK LOCAL: stage=warm-entry"),pre=message("SWEET MDSS CLOCK LOCAL: stage=pre-write"),reset=message("SWEET MDSS CLOCK RESET:");
 assert(field(warm,"valid")==unsigned(!releasing));
 assert(field(pre,"valid")==unsigned(written&&branch==7&&!releasing));
 assert(field(reset,"valid")==unsigned(written&&branch==7&&!releasing));
 if(written&&branch==7){assert(field(post,"branch")==7&&field(post,"pa")==bases[1]+0x4004);}
 auto admission=message("SWEET MDSS CLOCK RSC PREFLIGHT:");
 assert(field(admission,"valid")==unsigned(!releasing));
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
 // CLK_OFF stuck with enable absent/present, including FSM=2 with OFF=1.
 // Enable=0/OFF=0 completion is covered by the voted-halt regression.
 for(unsigned bad=0;bad<3;++bad){Harness h;offLeaves();configReady(h);
  if(bad!=1)dropClockWrite=bases[1]+0x4004;
  regs[bases[1]+0x4004]|=0x80000000u;
  if(bad==2)regs[bases[1]+0x4004]|=0x20000000u;
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
 // Separate never observed, immediate readback then lost, and a late latch
 // during CLK_OFF followed by loss. Final CLK_OFF=1 keeps all cases pending.
 for(unsigned mode=0;mode<3;++mode){Harness h;offLeaves();regs[bases[1]+0x4004]=0;
  configReady(h);if(mode!=1)dropClockWrite=bases[1]+0x4004;
  untilWrite(h,7);auto token=h.m->fNoCClockToken;
  if(mode==2){regs[bases[1]+0x4004]=0x80000001u;clockTick(h,100,false);assert(h.m->fClockWaiting);}
  regs[bases[1]+0x4004]=0x80000000u;clockTick(h,100,false,false,false);messages.clear();fakeTicks=h.m->fClockWaitDeadline;
  clockTick(h,0,false,false,false);assert(clockStatus(h,token).result==int(kIOReturnTimeout));
  if(DIAG_CHECK){auto p=message("SWEET MDSS CLOCK READBACK:"),o=message("SWEET MDSS CLOCK OBSERVED:"),a=message("SWEET MDSS CLOCK RSC PREFLIGHT:");
   assert(field(p,"valid")==1&&field(p,"cbcr")==unsigned(mode==1)&&field(p,"pa")==bases[1]+0x4004);
   assert(field(o,"wait-samples")== (mode==2?3u:2u));
   assert(field(o,"seen-enable")==unsigned(mode==2)&&field(o,"seen-off")==1u);
   assert(field(o,"first")== (mode==2?0x80000001u:0x80000000u));
   assert((field(o,"last-enabled-ns")!=0)==(mode==2));
   assert(field(a,"valid")==1&&field(a,"non-gdsc")==0&&field(a,"rscc-ahb")==0x80004000u&&field(a,"rscc-vsync")==0x80008000u);
  }
  noPayload();lateSilent(h);report(scenario++,h,token);dropClockWrite=0;
 }
 // Translation is diagnostic, including an unavailable or unexpected PA;
 // it never creates a successful lease when CLK_OFF remains asserted.
 for(vm_offset_t pa:{vm_offset_t(0),vm_offset_t(0xaf08004)}){Harness h;offLeaves();configReady(h);
  translatedAddress=pa;dropClockWrite=bases[1]+0x4004;untilWrite(h,7);regs[bases[1]+0x4004]|=0x80000000u;
  auto token=h.m->fNoCClockToken;messages.clear();fakeTicks=h.m->fClockWaitDeadline;clockTick(h,0,false);
  assert(clockStatus(h,token).result==int(kIOReturnTimeout));
  if(DIAG_CHECK){auto p=message("SWEET MDSS CLOCK READBACK:");assert(field(p,"valid")==1&&field(p,"pa")==pa);}
  noPayload();lateSilent(h);report(scenario++,h,token);dropClockWrite=0;translatedAddress=~vm_offset_t(0);
 }
 assert(scenario==19);

 // Distinct stage values expose inherited state, cleared reset and reset
 // appearing after the write. These are diagnostic inputs, not a model that
 // pretends to prove the physical effect of asserting BCR.
 for(unsigned mode=0;mode<5;++mode){Harness h;offLeaves();
  unsigned warm=mode==1||mode==2,pre=mode==1,failed=mode==1||mode==3;
  regs[bases[1]+0x4000]=0x40u|warm;
  if(mode==4)regs[bases[1]+0x4004]=0x2001;
  auto initial=regs[bases[1]+0x4004],initialCFG=regs[bases[1]+0x22c0];
  configReady(h);
  regs[bases[1]+0x4000]=0x80u|pre;
  regs[bases[1]+0x4004]=0x2000;
  regs[bases[1]+0x400c]|=0x200;regs[bases[1]+0x4008]|=0x400;
  dropClockWrite=bases[1]+0x4004;untilWrite(h,7);auto token=h.m->fNoCClockToken;
  regs[bases[1]+0x4000]=0x100u|failed;regs[bases[1]+0x4004]|=0x80000000u;
  messages.clear();fakeTicks=h.m->fClockWaitDeadline;clockTick(h,0,false);
  assert(clockStatus(h,token).result==int(kIOReturnTimeout));
  if(DIAG_CHECK){
   assert(count("SWEET MDSS CLOCK LOCAL:")==2&&count("SWEET MDSS CLOCK RESET:")==1);
   auto w=message("SWEET MDSS CLOCK LOCAL: stage=warm-entry"),p=message("SWEET MDSS CLOCK LOCAL: stage=pre-write"),r=message("SWEET MDSS CLOCK RESET:");
   assert(field(w,"valid")==1&&field(p,"valid")==1&&field(r,"valid")==1);
   assert(field(w,"bcr")== (0x40u|warm)&&field(p,"bcr")== (0x80u|pre)&&field(r,"bcr")== (0x100u|failed));
   assert(field(w,"non-gdsc")==initial&&field(p,"non-gdsc")==0x2000);
   assert(field(w,"rscc-ahb")==0x80004000u&&field(w,"rscc-vsync")==0x80008000u);
   assert(field(p,"rscc-ahb")==0x80004200u&&field(p,"rscc-vsync")==0x80008400u);
   assert(field(w,"ahb-cfg")==initialCFG&&field(p,"ahb-cfg")==0x40f);
   assert(field(w,"sampled-ns")<=field(p,"sampled-ns")&&field(p,"sampled-ns")<=field(r,"sampled-ns"));
   unsigned bcrReads=0;for(auto&o:trace)if(o.addr==bases[1]+0x4000){assert(!o.write);++bcrReads;}assert(bcrReads==3);
  }
  noPayload();lateSilent(h);report(scenario++,h,token);dropClockWrite=0;
 }
 // Loss of register access after the write must fail at the existing gate,
 // without a diagnostic read through the now unavailable DISPCC window.
 {Harness h;offLeaves();configReady(h);untilWrite(h,7);auto token=h.m->fNoCClockToken;
  regs[bases[0]+GCC_DISP_AHB]=0;trace.clear();messages.clear();clockTick(h);
  assert(clockStatus(h,token).result==int(kIOReturnNotReady));
  for(auto&o:trace)assert(!(o.addr>=bases[1]&&o.addr<bases[1]+0x20000));
  diagnostics(false);lateSilent(h);report(scenario++,h,token);}
 // The logger itself only consumes cached fields; calling it again cannot
 // read another register or alter the timestamp/timer/control state.
 {Harness h;offLeaves();configReady(h);untilWrite(h,7);auto token=h.m->fNoCClockToken;
  auto n=trace.size(),now=fakeTicks;auto timer=h.m->fClockTimer->timeout;
  h.m->logClockTimeout("manual-cache-check",now,false);
  assert(trace.size()==n&&fakeTicks==now&&h.m->fClockTimer->timeout==timer);
  report(scenario++,h,token);}
 // Cold-domain admission must not perform the warm-only context sampling.
 {Cold c;c.begin();c.finish();for(auto&o:trace)assert(o.addr!=bases[1]+0x4000);
#if DIAG_CHECK
  assert(!c.m->fClockWarmLocal.valid);
#endif
  printf("cold %u core %u now %llu\n",scenario++,c.m->fCorePhase,(unsigned long long)fakeTicks);
  for(auto&o:trace)printf("%c %llx %x\n",o.write?'W':'R',(unsigned long long)o.addr,o.value);
 }
 assert(scenario==27);

}
'''


def run(tree, diagnostic):
    sys.argv[:] = [sys.argv[0], str(tree)]
    context = {'__file__': str(fixture), '__name__': 'clock_timeout_fixture'}
    exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
                 str(fixture), 'exec'), context)
    core, actual = context['ns'], context['actual']
    core['prelude'] = core['prelude'].replace(
        'static vm_offset_t ml_vtophys(vm_offset_t address){return address;}',
        'static vm_offset_t translatedAddress=~vm_offset_t(0);\n'
        'static vm_offset_t ml_vtophys(vm_offset_t address){return translatedAddress==~vm_offset_t(0)?address:translatedAddress;}')
    source = (core['prelude'] + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] +
              actual['header'] + actual['source'] + core['helpers'] +
              core['tests'].split('int main(){')[0] + context['tests'].split('int main(){')[0] + checks)
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
    # BCR is otherwise untouched. Each warm/pre-write context is exactly six
    # ordered reads, and the timeout adds only one BCR read (no new CBCR).
    lines = result.splitlines(keepends=True)
    filtered = []
    full = single = 0
    i = 0
    expected = [b'af04000', b'af04004', b'af0400c', b'af04008', b'af022bc', b'af022c0']
    while i < len(lines):
        fields = lines[i].split()
        assert fields[:2] != [b'W', b'af04000'], 'diagnostics must not write reset'
        if fields[:2] != [b'R', b'af04000']:
            filtered.append(lines[i]); i += 1; continue
        if i+1 < len(lines) and lines[i+1].split()[:2] == [b'R', b'af04004']:
            assert [line.split()[:2] for line in lines[i:i+6]] == [[b'R', addr] for addr in expected]
            full += 1; i += 6
        else:
            single += 1; i += 1
    assert full > 0 and single > 0
    assert b''.join(filtered) == run(baseline, False), 'unexpected MMIO, result, ownership, timer or simulated time change'
    print(f'Allowed additional evidence reads: {full} full contexts and {single} timeout BCR samples')
print('PASS: 27 actual provider scenarios; inherited/changed/reset-stage context, branch/overall timeout, '
      'no early/cold/release/failed-access reads, cache-only logger, stop/late silence; ASan/UBSan; '
      + ('all other MMIO/results/ownership/timers/time match 0212' if baseline else 'no baseline supplied'))
