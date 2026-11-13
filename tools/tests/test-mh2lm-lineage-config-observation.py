#!/usr/bin/env python3
"""Exercise Lineage plain-write versus synchronous-write completion policy.
Usage: test-mh2lm-lineage-config-observation.py EXTRACTED_XNU
Register and interrupt responses are injected independently.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
tree = Path(sys.argv[1])
here = Path(__file__).resolve().parent
evidence = here / 'test-mh2lm-noc-completion-evidence.py'
context = {'__file__': str(evidence), '__name__': 'qtimer_evidence_fixture'}
exec(compile(evidence.read_text().split('\ndef run(tree, diagnostic):', 1)[0], str(evidence), 'exec'), context)
fixture = here / 'test-mh2lm-display-noc.py'
fixture_context = {'__file__': str(fixture), '__name__': 'qtimer_noc_fixture'}
sys.argv[:] = [sys.argv[0], str(tree)]
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0], str(fixture), 'exec'), fixture_context)
prelude = fixture_context['prelude']
hook = r'''
static const unsigned timerOffsets[]={0x2020,0x2024,0x3020,0x3024,0x202c,0x302c};
static int nonEcho=-1;
static int corruptControl=-1;
static unsigned controlWrites=0;
static uintptr_t corruptInit=0;
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(b==0xaf30000)for(unsigned i=0;i<6;++i)if(o==timerOffsets[i]&&(nonEcho==int(i)||nonEcho==6)){
  trace.push_back({true,b+o,v});regs[b+o]=i==0?0x224cac6u:(i<4?0x12345678u:5u);return;
 }
 if(b==0xaf21c00&&o==2*0x2a0+0x14&&corruptControl>=0&&controlWrites++==unsigned(corruptControl)){trace.push_back({true,b+o,v});regs[b+o]=v^8u;return;}
 if(corruptInit&&b+o==corruptInit){trace.push_back({true,b+o,v});regs[b+o]=v^8u;return;}
'''
assert prelude.count('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){') == 1
prelude = prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){', hook)
actual = fixture_context['ns']
checks = r'''
int main(){unsigned cases=0;
 // Independently corrupt every RSC initialization entry. This covers the
 // actual production table; the separate Lineage audit checks its contents.
 for(const auto&op:mdssRSCV2Init){Harness h;corruptInit=(op.wrapper?DW:DR)+op.offset;
  h.triggered();assert(h.m->fNoCRSCInitialized&&!nstatus(h.m,h.token).acknowledged);
  assert(count("mismatch=1")>0);
  h.hardware(1,0x10100);h.poll();assert(!nstatus(h.m,h.token).acknowledged&&!h.m->fNoCEvidence.polls);
  drive(h.m,h.r);assert(nstatus(h.m,h.token).state==SweetNoCHeld);corruptInit=0;++cases;
 }
 // Payload, WAIT and ENABLE writes are plain in Lineage. An observation
 // never supplies command completion: only the explicit physical IRQ can.
 for(unsigned offset:{0x30u,0x34u,0x38u,0x10u,0x1cu}){Harness h;rpmhDrop=2*0x2a0+offset;
  h.triggered();assert(count("mismatch=1")>0&&!nstatus(h.m,h.token).acknowledged);
  h.hardware(1,0x10100);h.poll();assert(!nstatus(h.m,h.token).acknowledged);
  rpmhDrop=~0u;drive(h.m,h.r);assert(nstatus(h.m,h.token).state==SweetNoCHeld);++cases;
 }
 // Cleanup WAIT/ENABLE non-echo does not add a new hardware-idle predicate.
 for(unsigned offset:{0x10u,0x1cu}){Harness h;h.triggered();rpmhDrop=2*0x2a0+offset;
  drive(h.m,h.r);assert(nstatus(h.m,h.token).state==SweetNoCHeld&&count("mismatch=1")>0);
  rpmhDrop=~0u;++cases;
 }
 // All three pre-trigger CONTROL writes are synchronous in Lineage.
 for(int index=0;index<3;++index){Harness h;corruptControl=index;controlWrites=0;
  for(unsigned i=0;i<256&&h.m->noCBusy();++i)h.poll();
  h.failed(kIOReturnIOError);assert(!h.m->fNoCExecuting&&!nstatus(h.m,h.token).acknowledged);
  assert(count("reason=write-readback")>0);corruptControl=-1;++cases;
 }
 // IRQ enable is configuration readback. A real callback with this slot's
 // status remains required; another simultaneous source does not veto it.
 for(unsigned mode=0;mode<2;++mode){Harness h;h.triggered();h.hardware(1,0x10100,true);
  unsigned bit=1u<<h.m->fNoCTCS;
  if(!mode)regs[DT]&=~bit;else{regs[DT]|=8;regs[DT+4]|=8;}
  assert(sweet_mdss_noc_irq(161));pump();assert(h.m->fNoCIRQObserved&&h.m->fNoCOps);
  if(!mode)assert(count("stage=noc-irq-enable")>0);
  regs[DT+4]&=~8u;drive(h.m,h.r);assert(nstatus(h.m,h.token).state==SweetNoCHeld);++cases;
 }
 // Our cached observation cannot grant access when the upstream lease fails.
 {Harness h;h.m->fClockFault=true;auto before=trace.size();h.poll();
  for(size_t i=before;i<trace.size();++i)assert(!(trace[i].addr>=DW&&trace[i].addr<DW+0x4000));++cases;}
 assert(cases==78);printf("PASS: %u Lineage configuration scenarios; all 65 RSC entries, payload/cleanup non-echo, three CONTROL sync failures, real IRQ required, access loss\n",cases);
}
'''
source = (prelude + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] +
          actual['header'] + actual['source'] + fixture_context['helpers'] +
          fixture_context['tests'].split('int main(){', 1)[0] +
          context['checks'].split('int main(){', 1)[0] + checks)
with tempfile.TemporaryDirectory(prefix='mh2lm-lineage-config-') as temp:
    cpp, binary = Path(temp) / 'test.cpp', Path(temp) / 'test'
    cpp.write_text(source)
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-DDIAG_CHECK=1', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=60,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
