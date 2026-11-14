#!/usr/bin/env python3
"""Replay actual MDSS mode2 lifecycle observations through the actual framebuffer.

Both drivers run unchanged against their existing OS/hardware fixtures in two
separate executables. Only the typed status/capability messages cross processes;
this is a contract regression, not a hardware or concurrent-workloop simulation.
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('xnu', type=Path)
parser.add_argument('--evidence-dir', type=Path)
parser.add_argument('--pipeline', action='store_true', help='exercise mode7 with erased pipeline configuration')
args = parser.parse_args()
root = args.xnu.resolve()
here = Path(__file__).resolve().parent
sys.argv = [str(Path(__file__)), str(root)]


def fixture(name):
    path = here / ('test-mh2lm-' + name + '.py')
    scope = {'__file__': str(path), '__name__': 'lineage_contract_fixture'}
    prefix = path.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0]
    exec(compile(prefix, str(path), 'exec'), scope)
    return scope


# Transfer every field explicitly, never native struct padding or a synthetic
# all-held result. New status fields automatically join the bridge.
header = (root / 'iokit/Kernel/arm/SweetDisplayBackend.h').read_text()
body = header.split('struct SweetPanelLifecycleStatus {', 1)[1].split('};', 1)[0]
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
fields = []
for declaration in body.split(';'):
    declaration = declaration.strip()
    if not declaration:
        continue
    kind, names = declaration.split(None, 1)
    assert kind in ('uint64_t', 'unsigned', 'IOReturn', 'bool'), declaration
    for name in names.split(','):
        name = name.strip()
        array = re.fullmatch(r'(\w+)\[(\d+)\]', name)
        fields.extend([f'{array[1]}[{i}]' for i in range(int(array[2]))] if array else [name])

provider = fixture('panel-pipeline' if args.pipeline else 'panel-lineage-wake')
consumer = fixture('panel-lifecycle-fb')
emit = '\n'.join(f' printf(" %llu",(unsigned long long)v.{field});' for field in fields)
provider_tests = provider['tests'].split('int main(){', 1)[0] + r'''
static void emitStatus(Harness& h,unsigned profile,unsigned round,unsigned stage) {
 SweetPanelLifecycleStatus v{};
 assert(h.m->panelLifecycleStatus(h.m->fLifecycle.token,&v)==kIOReturnSuccess);
 printf("STATUS %u %u %u %u",profile,round,stage,h.m->panelLifecycleCapabilities());
''' + emit + r'''
 puts("");
}
int main() {
 for(unsigned profile=1;profile<=4;++profile) {
  selectedProfile=profile;Harness h;ready(h);
  for(unsigned round=0;round<3;++round) {
   h.power(SweetPanelDisplayOff);emitStatus(h,profile,round,0);
   h.untilLife(SweetPanelComplete);emitStatus(h,profile,round,1);
   h.power(SweetPanelDisplayOn);emitStatus(h,profile,round,2);
   h.untilLife(SweetPanelColorRestore);emitStatus(h,profile,round,3);
   h.untilLife(SweetPanelFrame);emitStatus(h,profile,round,4);
   regs[bases[3]+0x18]=regs[bases[3]+0x1c]=0;regs[0x0ae01014]|=0x100;realFrame(h.m);h.life();
   assert(h.m->fLifecycle.phase==SweetPanelBrightnessDrain);emitStatus(h,profile,round,5);
   h.untilLife(SweetPanelBrightness);emitStatus(h,profile,round,6);
   h.untilLife(SweetPanelFrame);emitStatus(h,profile,round,7);
   regs[bases[3]+0x18]=regs[bases[3]+0x1c]=0;regs[0x0ae01014]|=0x100;realFrame(h.m);
   h.untilLife(SweetPanelTouchResume);emitStatus(h,profile,round,8);
   h.untilLife(SweetPanelComplete);emitStatus(h,profile,round,9);
  }
 }
}
'''
if args.pipeline:
    provider_tests = provider_tests.replace('h.power(SweetPanelDisplayOn);emitStatus', 'erasePipeline();h.power(SweetPanelDisplayOn);emitStatus')
flags = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
runtime = dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
with tempfile.TemporaryDirectory(prefix='mh2lm-lineage-fb-') as temp:
    temp = Path(temp)
    worker = temp / 'worker.c'
    worker.write_text(provider['actual_worker'](root))
    obj = temp / 'worker.o'
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', *flags,
                    '-c', str(worker), '-o', str(obj)], check=True)
    source = temp / 'provider.cpp'
    actual = provider['actual']
    source.write_text('#define SWEET_TOUCH_PANEL_REAL_WORKER 1\n' + provider['prelude'] +
                      actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] + actual['header'] +
                      actual['source'] + actual['backlight'] + provider['touch'] +
                      provider['helpers'] + provider_tests)
    binary = temp / 'provider'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-pthread',
                    *flags, str(source), str(obj), '-o', str(binary)], check=True)
    output = subprocess.check_output([str(binary)], env=runtime, text=True, timeout=60)
    records = []
    for line in output.splitlines():
        if not line.startswith('STATUS '):
            continue
        values = [int(n) for n in line.split()[1:]]
        assert len(values) == len(fields) + 4, line
        records.append(dict(profile=values[0], round=values[1], stage=values[2], cap=values[3],
                            status=dict(zip(fields, values[4:]))))
    assert len(records) == 120, len(records)
    initializers = []
    for i, record in enumerate(records):
        assert (record['profile'], record['round'], record['stage']) == (i // 30 + 1, i // 10 % 3, i % 10)
        assignments = ''.join(f'v.{field}={value}ULL;' for field, value in record['status'].items())
        initializers.append('{'+str(record['cap'])+',[]{SweetPanelLifecycleStatus v{};'+assignments+'return v;}()}')
    data = 'struct Record{unsigned cap;SweetPanelLifecycleStatus status;};\nstatic Record records[]={\n' + ',\n'.join(initializers) + '\n};\n'
    tests = consumer['tests'].split('int main(){', 1)[0] + data + r'''
static void feed(Harness& h,const SweetPanelLifecycleStatus& status) {
 h.b->lifecycle=status;h.tick();
}
static void pending(Harness& h) {
 assert(!h.f->fPanelLifecycleFault&&h.f->fPowerOff&&h.f->fPanelLifecycleBackend==h.b);
 assert(!h.f->fHardwareVsyncActive&&h.b->cancelled.empty());
}
int main() {
 lifecycleArg=2;
 for(unsigned profile=0;profile<4;++profile) {
  Harness h;auto*first=&records[profile*30];h.b->cap=first->cap;
  h.b->next=first->status.token-1;
  for(unsigned round=0;round<3;++round) {
   auto*r=first+round*10;
   assert(h.request(true)==0&&h.f->fPanelLifecycleToken==r[0].status.token);
   feed(h,r[0].status);pending(h);
   // One round queues ON while OFF is still pending; only OFF completion
   // permits the next request. Other rounds exercise a genuinely parked gap.
   if(round==1)assert(h.request(false)==0&&h.b->operations.size()==round*2+1);
   feed(h,r[1].status);assert(!h.f->fPanelLifecycleFault&&h.f->fPowerOff);
   if(round!=1) {
    assert(!h.f->fPanelLifecycleBackend);
    auto count=h.b->operations.size();assert(h.request(true)==0&&h.b->operations.size()==count);
    assert(h.request(false)==0);
   }
   assert(h.f->fPanelLifecycleToken==r[2].status.token);
   feed(h,r[2].status);pending(h);
   feed(h,r[3].status);pending(h); // fresh pixels/touch not complete yet
   for(unsigned stage=4;stage<9;++stage){feed(h,r[stage].status);pending(h);}
   feed(h,r[9].status);
   assert(!h.f->fPanelLifecycleFault&&!h.f->fPowerOff&&!h.f->fPanelLifecycleBackend);
   assert(h.b->cancelled.empty()&&h.b->operations.size()==(round+1)*2);
   assert(h.b->operations[round*2]==SweetPanelDisplayOff&&h.b->operations[round*2+1]==SweetPanelDisplayOn);
   assert(panelRequests.empty()); // no legacy synchronous fallback
  }
 }
 // Reject corrupt common/final proofs derived from the real provider, not
 // merely from a mock's interpretation of which state counts as complete.
 for(bool on:{false,true})for(unsigned fault=0;fault<9;++fault) {
  Harness h;h.b->cap=records[0].cap;h.b->next=records[0].status.token-1;
  assert(h.request(true)==0);
  if(on){feed(h,records[1].status);assert(h.request(false)==0);}
  auto v=records[on?9:1].status;
  if(fault==0)v.uncertain=true;
  if(fault==1)v.result=kIOReturnIOError;
  if(fault==2)v.profile=0;
  if(fault==3)v.profile=5;
  if(fault==4)v.operation=on?SweetPanelDisplayOff:SweetPanelDisplayOn;
  if(fault==5)v.sleeping=on;
  if(fault==6)v.touchQuiesced=on;
  if(fault==7){if(on)v.resetComplete=false;else v.displayDrained=false;}
  if(fault==8){if(on)v.brightnessRestored=false;else v.configHeld=false;}
  feed(h,v);
  assert(h.f->fPanelLifecycleFault&&h.f->fPowerOff&&h.b->cancelled.size()==1&&h.request(false)!=0);
 }
 puts("PASS: 120 actual MDSS mode2 status observations across four profiles/three OFF-ON cycles accepted by real framebuffer; pending color/fresh-pixel/touch gates, coalesced ON, 18 corrupted proofs rejected, no cancellation on success or legacy fallback");
}
'''
    if args.pipeline:
        tests = tests.replace('lifecycleArg=2;', 'lifecycleArg=7;')
        tests = tests.replace(' puts("PASS:', r''' // The valid final state came from the real driver. Remove only its
 // generation/pixel/capability proof and ensure the consumer rejects it.
 for(unsigned fault=0;fault<6;++fault) {
  Harness h;h.b->cap=records[0].cap;h.b->next=records[0].status.token-1;
  if(fault==0){h.b->cap&=~SweetPanelPipelineRebuild;assert(h.request(true)!=0&&h.b->operations.empty());continue;}
  assert(h.request(true)==0);auto off=records[1].status;
  if(fault==1){off.pipelineGeneration=0;feed(h,off);assert(h.f->fPanelLifecycleFault);continue;}
  feed(h,off);assert(h.f->fPanelPipelineGeneration==off.pipelineGeneration&&h.request(false)==0);
  auto on=records[9].status;
  if(fault==2)on.pipelineGeneration=0;
  if(fault==3)on.pipelineGeneration=off.pipelineGeneration;
  if(fault==4)on.pixelsComplete=false;
  if(fault==5)on.touchQuiesced=true;
  feed(h,on);assert(h.f->fPanelLifecycleFault&&h.f->fPowerOff&&h.b->cancelled.size()==1);
 }
 puts("PASS:''')
        tests = tests.replace('MDSS mode2 status', 'MDSS mode7 erased-pipeline status')
        tests = tests.replace('18 corrupted proofs rejected', '18 common plus 6 pipeline/capability proofs rejected')
    source = temp / 'consumer.cpp'
    source.write_text(consumer['prelude'] + consumer['ns']['source'] + consumer['helpers'] + tests)
    binary = temp / 'consumer'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-pthread', *flags,
                    '-I' + str(root / 'bsd/dev'), str(source), '-o', str(binary)], check=True)
    if args.evidence_dir:
        args.evidence_dir.mkdir(parents=True, exist_ok=True)
        (args.evidence_dir / 'mdss-status.json').write_text(json.dumps(records, indent=2) + '\n')
    subprocess.run([str(binary)], env=runtime, check=True, timeout=60)
