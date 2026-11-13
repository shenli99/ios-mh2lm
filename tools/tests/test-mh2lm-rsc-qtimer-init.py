#!/usr/bin/env python3
"""Run the real RSC initializer with non-echo CNTP observations.
Usage: test-mh2lm-rsc-qtimer-init.py EXTRACTED_XNU [--expect-a28-rejection]
The injected observations model a28's failure, not the timer hardware's cause.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != '--expect-a28-rejection'):
    sys.exit(__doc__)
tree = Path(sys.argv[1])
baseline = len(sys.argv) == 3
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
static uintptr_t corruptInit=0;
static void writeReg(uintptr_t b,uint32_t o,uint32_t v){
 if(b==0xaf30000)for(unsigned i=0;i<6;++i)if(o==timerOffsets[i]&&(nonEcho==int(i)||nonEcho==6)){
  trace.push_back({true,b+o,v});regs[b+o]=i==0?0x224cac6u:(i<4?0x12345678u:5u);return;
 }
 if(corruptInit&&b+o==corruptInit){trace.push_back({true,b+o,v});regs[b+o]=v^8u;return;}
'''
assert prelude.count('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){') == 1
prelude = prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){', hook)
actual = fixture_context['ns']
checks = r'''
int main(){unsigned cases=0;
 // Six individual non-echo observations, then all six together. The first
 // reproduces a28 CVAL_LO. Old code fails before any ACTIVE payload.
 for(int i=0;i<7;++i){Harness h;nonEcho=i;
  if(BASELINE){for(unsigned n=0;n<256&&h.m->noCBusy();++n)h.poll();
   h.failed(kIOReturnIOError);assert(!h.m->fNoCRSCInitialized&&!tcsWrites());
   assert(count("init readback index=")==1&&!nstatus(h.m,h.token).acknowledged);
  }else{
   h.triggered();assert(h.m->fNoCRSCInitialized&&!nstatus(h.m,h.token).acknowledged);
   assert(count("SWEET MDSS RSC: qtimer write issued")==6);
   std::vector<unsigned> offsets;
   for(auto&o:trace)if(o.write&&o.addr>=DW+0x2000&&o.addr<DW+0x3030)offsets.push_back(o.addr-DW);
   assert((offsets==std::vector<unsigned>{0x2020,0x2024,0x3020,0x3024,0x202c,0x302c}));
   for(unsigned j=0;j<6;++j){unsigned reads=0;for(auto&o:trace)reads+=!o.write&&o.addr==DW+timerOffsets[j];assert(reads==1);}
   // Diagnostic observations never complete a bandwidth request. Even
   // pre-existing command status needs this request's real IRQ callback.
   h.hardware(1,0x10100);h.poll();assert(!nstatus(h.m,h.token).acknowledged&&!h.m->fNoCEvidence.polls);
   if(i==6){h.expire();h.failed();assert(!nstatus(h.m,h.token).acknowledged);}
   else{drive(h.m,h.r);assert(nstatus(h.m,h.token).state==SweetNoCHeld&&nstatus(h.m,h.token).acknowledged==0x3e);}
  }
  nonEcho=-1;++cases;
 }
 if(BASELINE){printf("PASS: %u a28 non-echo initialization failures reproduced before TCS submission\n",cases);return 0;}
 // The other initialization blocks also use plain writes in Lineage.
 // Corruption is logged, then fresh IRQ/completion is still required.
 const uintptr_t stable[]={DW+0x1fe0,DW+0x1ff0,DW+0x1040,DW+0x1044,DW+8,DR+0x600,DR+0x1004,DR+0x20};
 for(auto address:stable){Harness h;corruptInit=address;
  h.triggered();assert(h.m->fNoCRSCInitialized&&!nstatus(h.m,h.token).acknowledged);
  assert(count("mismatch=1")>0);drive(h.m,h.r);assert(nstatus(h.m,h.token).state==SweetNoCHeld);
  corruptInit=0;++cases;
 }
 // Loss of the clock lease before initialization must never reach CNTP.
 {Harness h;h.m->fClockFault=true;auto before=trace.size();h.poll();
  assert(!h.m->fNoCRSCInitialized&&!tcsWrites());
  for(size_t i=before;i<trace.size();++i)assert(!(trace[i].addr>=DW&&trace[i].addr<DW+0x4000));++cases;}
 printf("PASS: %u real-provider qtimer scenarios; non-echo writes, configuration diagnostics, IRQ-only ACK and access loss\n",cases);
}
'''
source = (prelude + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] +
          actual['header'] + actual['source'] + fixture_context['helpers'] +
          fixture_context['tests'].split('int main(){', 1)[0] +
          context['checks'].split('int main(){', 1)[0] + checks)
with tempfile.TemporaryDirectory(prefix='mh2lm-qtimer-init-') as temp:
    cpp, binary = Path(temp) / 'test.cpp', Path(temp) / 'test'
    cpp.write_text(source)
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-DDIAG_CHECK=1', f'-DBASELINE={int(baseline)}', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=60,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
