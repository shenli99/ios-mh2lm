#!/usr/bin/env python3
"""Actual RPMh ACTIVE executor: module mechanics plus its pre-extraction MMIO trace.

The module is compiled on its own, so nothing it needs can be supplied by the
owner; then the shipped owner is compiled through the display fixture and its
ACTIVE trajectory is compared with the literals captured before the extraction.
Usage: test-mh2lm-rpmh-executor.py EXTRACTED_XNU
"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
arm = root / 'iokit/Kernel/arm'
header = (arm / 'SweetRPMhExecutor.h').read_text()
source = (arm / 'SweetRPMhExecutor.cpp').read_text()
owner = (arm / 'SweetRPMh.cpp').read_text()

# The mechanics are a translation unit of its own: no provider, no map, no
# timer, no lock, no service registry. Anything of that kind would make the
# module an owner instead of a register sequence.
for banned in ('mapDeviceMemory', 'IOService', 'IOTimerEventSource', 'IOLock',
               'IOSimpleLock', 'registerService', 'getProvider', 'OSData',
               'OSArray', 'IOMemoryDescriptor', 'IOSleep', 'thread_call'):
    assert banned not in source, banned
    assert banned not in header, banned
# ... and the owner still reaches every one of the four mechanics by name.
for called in ('SweetRPMhExecutor::arm(', 'SweetRPMhExecutor::step(',
               'SweetRPMhExecutor::verify(', 'SweetRPMhExecutor::stageCleanup('):
    assert called in owner, called
# The one record that says who kept the hardware is emitted by the owner.
assert 'executor=module tcs=%u commands=%u open=%u hardware-owner=SweetRPMh' in owner

UNITS = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "SweetRPMhExecutor.h"

/* A TCS slot is modelled as little as possible: a word per offset, the source
 * status word cleared only by the acknowledge write, and an access log. */
struct Regs {
 uint32_t v[1024];
 unsigned reads, writes, recorded;
 uint32_t order[32];
 bool swallow;  /* a posted write the bus has not landed yet */
};
static uint32_t rdRead(void *c, uint32_t o) {
 Regs *r = (Regs *)c;
 r->reads++;
 if (r->recorded < 32) r->order[r->recorded++] = o;
 return r->v[o];
}
static void rdWrite(void *c, uint32_t o, uint32_t v) {
 Regs *r = (Regs *)c;
 r->writes++;
 if (r->swallow) return;
 if (o == 8) r->v[4] &= ~v; else r->v[o] = v;
}
static void reset(Regs *r) { memset(r, 0, sizeof(*r)); }
static SweetRPMhExecutor::Ports ports(Regs *r) {
 SweetRPMhExecutor::Ports p;
 p.context = r; p.read = rdRead; p.write = rdWrite;
 return p;
}
/* Same geometry the owner publishes: status, source status, enable, clear,
 * then the slot words, then the command block. */
static const SweetRPMhLayout L = {0x18, 0x04, 0x00, 0x08, 0x14, 0x1c, 0x10, 0x3c, 0x14, 0x3};
static const uint32_t SLOT = 0x2a0, AMC = 0x10000;

int main() {
 Regs r; SweetRPMhExecutor::Ports p;
 uint32_t seen = 0xdeadbeef; bool issued = false; unsigned open = 0;
 SweetRPMhWord word; SweetRPMhWord words[SweetRPMhExecutor::kCapacity];

 /* arm(): a clear source means the slot is ours; an already posted bit is a
  * completion that cannot be attributed, and an all-ones answer is no answer. */
 reset(&r); p = ports(&r); issued = false; r.v[4] = 0;
 assert(SweetRPMhExecutor::arm(p, L, 1u, &seen) == SweetRPMhExecutor::ArmReady && seen == 0);
 r.v[4] = 2;
 assert(SweetRPMhExecutor::arm(p, L, 1u, &seen) == SweetRPMhExecutor::ArmReady && seen == 2);
 r.v[4] = 1;
 assert(SweetRPMhExecutor::arm(p, L, 1u, &seen) == SweetRPMhExecutor::ArmStale && seen == 1);
 r.v[4] = ~0u;
 assert(SweetRPMhExecutor::arm(p, L, 1u, &seen) == SweetRPMhExecutor::ArmInvalid);
 r.v[4] = 0;
 assert(SweetRPMhExecutor::arm(p, L, 1u, NULL) == SweetRPMhExecutor::ArmReady);

 /* step(): one posted word per call. The readback decides whether the next
  * word may go out; a word that never settles blocks without being rewritten. */
 word.offset = 0x30; word.value = 0x10108; word.w1c = false;
 reset(&r); p = ports(&r); issued = false; r.v[0x30] = 0x10108;
 assert(SweetRPMhExecutor::step(p, L, word, &issued, 0, 10, 0) == SweetRPMhExecutor::StepAdvanced);
 assert(!issued && r.writes == 1 && r.reads == 1 && r.order[0] == 0x30);
 reset(&r); p = ports(&r); issued = false; r.swallow = true;
 assert(SweetRPMhExecutor::step(p, L, word, &issued, 0, 10, 0) == SweetRPMhExecutor::StepBlocked);
 assert(issued && r.writes == 1 && r.reads == 1);
 /* A w1c word is acknowledged by the write and observed at the source status,
  * never by reading back the acknowledge register. */
 word.offset = 0x08; word.value = 2; word.w1c = true;
 reset(&r); p = ports(&r); issued = false; r.v[4] = 3;
 assert(SweetRPMhExecutor::step(p, L, word, &issued, 0, 10, &seen) == SweetRPMhExecutor::StepAdvanced);
 assert(!issued && seen == 1 && r.order[0] == 0x04 && r.v[4] == 1 && r.writes == 1);
 reset(&r); p = ports(&r); issued = false; r.v[4] = 6; r.swallow = true; issued = false;
 assert(SweetRPMhExecutor::step(p, L, word, &issued, 0, 10, 0) == SweetRPMhExecutor::StepBlocked && issued);
 /* A word that never settles past the deadline expires with the write still
  * outstanding, so the caller cannot mistake it for a queued word. */
 reset(&r); p = ports(&r); issued = false; r.v[4] = 6; r.swallow = true; issued = false;
 assert(SweetRPMhExecutor::step(p, L, word, &issued, 5, 5, 0) == SweetRPMhExecutor::StepExpired);
 assert(issued && r.writes == 1 && r.reads == 1);

 /* verify(): the whole slot is sampled first, then the verdict. A command that
  * answers all ones or misses the finished bits keeps the transaction open. */
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = 1; r.v[SLOT + 0x18] = 1;
 r.v[SLOT + 0x3c] = 0x10103; r.v[SLOT + 0x50] = 0x10103; r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, &open) == SweetRPMhExecutor::VerdictComplete);
 assert(open == 0 && r.recorded == 5);
 assert(r.order[0] == 0x04 && r.order[1] == SLOT + 0x18);
 assert(r.order[2] == SLOT + 0x3c && r.order[3] == SLOT + 0x50 && r.order[4] == SLOT + 0x64);
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = 0; r.v[SLOT + 0x18] = 1;
 r.v[SLOT + 0x3c] = r.v[SLOT + 0x50] = r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, &open) == SweetRPMhExecutor::VerdictIncomplete && open == 0);
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = 1; r.v[SLOT + 0x18] = 0;
 r.v[SLOT + 0x3c] = r.v[SLOT + 0x50] = r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, &open) == SweetRPMhExecutor::VerdictIncomplete && open == 0);
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = 1; r.v[SLOT + 0x18] = 1;
 r.v[SLOT + 0x3c] = 0x10103; r.v[SLOT + 0x50] = 0x10101; r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, &open) == SweetRPMhExecutor::VerdictIncomplete && open == 1);
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = 1; r.v[SLOT + 0x18] = 1;
 r.v[SLOT + 0x3c] = 0x10103; r.v[SLOT + 0x50] = ~0u; r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, &open) == SweetRPMhExecutor::VerdictIncomplete && open == 1);
 /* Unreadable source or slot invalidates the verdict, but the sample is still
  * the same five reads: the shape of the sample may not depend on the answer. */
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = ~0u; r.v[SLOT + 0x18] = 1;
 r.v[SLOT + 0x3c] = r.v[SLOT + 0x50] = r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, 0) == SweetRPMhExecutor::VerdictInvalid && r.recorded == 5);
 reset(&r); p = ports(&r); issued = false;
 r.v[4] = 1; r.v[SLOT + 0x18] = ~0u;
 r.v[SLOT + 0x3c] = r.v[SLOT + 0x50] = r.v[SLOT + 0x64] = 0x10103;
 assert(SweetRPMhExecutor::verify(p, L, SLOT, 1u, 3, 0) == SweetRPMhExecutor::VerdictInvalid && r.recorded == 5);

 /* stageCleanup(): engine first, then the slot words, the acknowledge while the
  * source is still masked, and the mask back to inherited-for-this-slot. */
 reset(&r); p = ports(&r); issued = false; r.v[0] = 0x45;
 unsigned count = 0;
 assert(SweetRPMhExecutor::stageCleanup(p, L, SLOT, words, &count, AMC, 2u, 0x42));
 assert(count == 6);
 assert(words[0].offset == SLOT + 0x14 && words[0].value == AMC && !words[0].w1c);
 assert(words[1].offset == SLOT + 0x14 && words[1].value == 0 && !words[1].w1c);
 assert(words[2].offset == SLOT + 0x10 && words[2].value == 0 && !words[2].w1c);
 assert(words[3].offset == SLOT + 0x1c && words[3].value == 0 && !words[3].w1c);
 assert(words[4].offset == 0x08 && words[4].value == 2u && words[4].w1c);
 assert(words[5].offset == 0x00 && words[5].value == 0x47 && !words[5].w1c);
 assert(r.reads == 1 && r.writes == 0 && r.order[0] == 0x00);
 /* Bounded storage: the tail is staged whole or not at all, and a source that
  * cannot be read back leaves the transaction for the owner to retire. */
 reset(&r); p = ports(&r); issued = false; r.v[0] = 0x41; count = 15;
 assert(!SweetRPMhExecutor::stageCleanup(p, L, SLOT, words, &count, AMC, 1u, 0) && count == 20);
 reset(&r); p = ports(&r); issued = false; count = 16;
 assert(!SweetRPMhExecutor::stageCleanup(p, L, SLOT, words, &count, AMC, 1u, 0) && count == 16);
 assert(r.reads == 0 && r.writes == 0);
 reset(&r); p = ports(&r); issued = false; r.v[0] = ~0u; count = 0;
 assert(!SweetRPMhExecutor::stageCleanup(p, L, SLOT, words, &count, AMC, 1u, 0) && count == 5);
 puts("PASS: actual ACTIVE executor mechanics; arm ready/stale/invalid, posted write advanced/blocked/expired without extra traffic, verdict after one fixed five-read sample, w1c observed at the source status, bounded cleanup tail staged whole or not at all");
 return 0;
}
"""

PRELUDE = """
#pragma once
#include <stdint.h>
"""

DUMP = r"""
static void show(const char *label) {
 printf("## %s\n", label);
 for (auto &o : trace) printf("%s %#x=%#x\n", o.write ? "W" : "R", (unsigned)o.addr, o.value);
 trace.clear();
}
static void flushMessages(size_t *cursor) {
 for (; *cursor < messages.size(); ++(*cursor)) printf("#MSG %s\n", messages[*cursor].c_str());
}
int main() {
 setvbuf(stdout, NULL, _IONBF, 0);
 size_t cursor = 0;
 IOService *n; SweetRPMh *s; uint64_t id;
 // One rail vote through a fresh ACTIVE completion.
 fixtures(); resourceMode = 2; n = nub(); s = make(n); id = attach(s);
 vote(s, id, SweetRailVDDI, {true, 1800, 1880, 62000}); tick(s); ack(s);
 show("rail-complete"); destroy(s, n); reboot();
 // ARC: selection, first target, cleanup, then the later stages of one vote.
 arcFixtures(); resourceMode = 2; n = nub(); s = make(n); id = attach(s);
 displayVote(s, id, SweetDisplayLowSVS); arcStage(s, 0, 6); show("arc-stage-1");
 arcStage(s, 1, 6); show("arc-stage-2");
 arcStage(s, 2, 1); show("arc-stage-3");
 arcStage(s, 1, 3); arcStage(s, 0, 2); show("arc-stage-5");
 destroy(s, n); reboot();
 // Cleanup staged, completion never clears: the bounded posted-write readback.
 fixtures(); n = nub(); s = make(n); id = attach(s);
 rpmhKeepIRQ = true; vote(s, id, SweetRailVDDI, {true, 1800, 1880, 62000});
 regs[TCS + 4] |= 1u << s->fTCSIndex;
 for (unsigned i = 0; i < 3; ++i) { fakeTicks += 100000; s->fTimer->action(s, s->fTimer); }
 show("rail-kept-irq"); destroy(s, n); reboot();
 flushMessages(&cursor);
 puts("DUMP-OK");
 return 0;
}
"""

# Captured from the tree before the executor was extracted: record count, read
# count, whole-scenario digest, and every write in order. These are literals
# because the point of the extraction is that the trajectory did not move.
PINNED = {
    "rail-complete": (171, 140,
     "ae55381360424e998f1e370d7242f11c891c5143bae83f903c4ecf014097f96c",
     [
      "W 0x17a00184=0x20",
      "W 0x17a00084=0x20",
      "W 0x17a00c08=0",
      "W 0x17a00424=0xa000",
      "W 0x17a06128=0",
      "W 0x17a0612c=0",
      "W 0x18220d00=0x40",
      "W 0x18220d08=0x1",
      "W 0x18220d30=0x10108",
      "W 0x18220d34=0x40000",
      "W 0x18220d38=0x708",
      "W 0x18220d44=0x10108",
      "W 0x18220d48=0x40008",
      "W 0x18220d4c=0x7",
      "W 0x18220d58=0x10108",
      "W 0x18220d5c=0x40004",
      "W 0x18220d60=0x1",
      "W 0x18220d10=0x7",
      "W 0x18220d1c=0x7",
      "W 0x18220d14=0",
      "W 0x18220d14=0x10000",
      "W 0x18220d00=0x41",
      "W 0x18220d14=0x1010000",
      "W 0x17a00104=0x20",
      "W 0x17a00184=0x20",
      "W 0x18220d14=0x10000",
      "W 0x18220d14=0",
      "W 0x18220d1c=0",
      "W 0x18220d10=0",
      "W 0x18220d08=0x1",
      "W 0x18220d00=0x41",
     ]),
    "arc-stage-1": (289, 264,
     "6ce3527934ffe724962499b4501043ea23c49d2410225edb780cafe2ed27c004",
     [
      "W 0x17a00184=0x20",
      "W 0x17a00084=0x20",
      "W 0x17a00c08=0",
      "W 0x17a00424=0xa000",
      "W 0x17a06128=0",
      "W 0x17a0612c=0",
      "W 0x18220d00=0x40",
      "W 0x18220d08=0x1",
      "W 0x18220d30=0x10108",
      "W 0x18220d34=0x30000",
      "W 0x18220d38=0x6",
      "W 0x18220d10=0x1",
      "W 0x18220d1c=0x1",
      "W 0x18220d14=0",
      "W 0x18220d14=0x10000",
      "W 0x18220d00=0x41",
      "W 0x18220d14=0x1010000",
      "W 0x17a00104=0x20",
      "W 0x17a00184=0x20",
      "W 0x18220d14=0x10000",
      "W 0x18220d14=0",
      "W 0x18220d1c=0",
      "W 0x18220d10=0",
      "W 0x18220d08=0x1",
      "W 0x18220d00=0x41",
     ]),
    "arc-stage-2": (75, 56,
     "2801e4e138cc09135968fdf7900fbbbf37595ba3e8ab8c795e2ccab6ae50a512",
     [
      "W 0x18220d00=0x40",
      "W 0x18220d08=0x1",
      "W 0x18220d30=0x10108",
      "W 0x18220d34=0x30004",
      "W 0x18220d38=0x6",
      "W 0x18220d10=0x1",
      "W 0x18220d1c=0x1",
      "W 0x18220d14=0",
      "W 0x18220d14=0x10000",
      "W 0x18220d00=0x41",
      "W 0x18220d14=0x1010000",
      "W 0x17a00104=0x20",
      "W 0x17a00184=0x20",
      "W 0x18220d14=0x10000",
      "W 0x18220d14=0",
      "W 0x18220d1c=0",
      "W 0x18220d10=0",
      "W 0x18220d08=0x1",
      "W 0x18220d00=0x41",
     ]),
    "arc-stage-3": (75, 56,
     "1af9663039e1b019311562c217e911b92ff58a0f9e9524bb8a4011584aebaf43",
     [
      "W 0x18220d00=0x40",
      "W 0x18220d08=0x1",
      "W 0x18220d30=0x10108",
      "W 0x18220d34=0x30008",
      "W 0x18220d38=0x1",
      "W 0x18220d10=0x1",
      "W 0x18220d1c=0x1",
      "W 0x18220d14=0",
      "W 0x18220d14=0x10000",
      "W 0x18220d00=0x41",
      "W 0x18220d14=0x1010000",
      "W 0x17a00104=0x20",
      "W 0x17a00184=0x20",
      "W 0x18220d14=0x10000",
      "W 0x18220d14=0",
      "W 0x18220d1c=0",
      "W 0x18220d10=0",
      "W 0x18220d08=0x1",
      "W 0x18220d00=0x41",
     ]),
    "arc-stage-5": (137, 99,
     "0c3e7378142613073b2fe110af851032650c193d67a8e4ce658c8b3844a3bb21",
     [
      "W 0x18220d00=0x40",
      "W 0x18220d08=0x1",
      "W 0x18220d30=0x10108",
      "W 0x18220d34=0x30004",
      "W 0x18220d38=0x3",
      "W 0x18220d10=0x1",
      "W 0x18220d1c=0x1",
      "W 0x18220d14=0",
      "W 0x18220d14=0x10000",
      "W 0x18220d00=0x41",
      "W 0x18220d14=0x1010000",
      "W 0x17a00104=0x20",
      "W 0x17a00184=0x20",
      "W 0x18220d14=0x10000",
      "W 0x18220d14=0",
      "W 0x18220d1c=0",
      "W 0x18220d10=0",
      "W 0x18220d08=0x1",
      "W 0x18220d00=0x41",
      "W 0x18220d00=0x40",
      "W 0x18220d08=0x1",
      "W 0x18220d30=0x10108",
      "W 0x18220d34=0x30000",
      "W 0x18220d38=0x2",
      "W 0x18220d10=0x1",
      "W 0x18220d1c=0x1",
      "W 0x18220d14=0",
      "W 0x18220d14=0x10000",
      "W 0x18220d00=0x41",
      "W 0x18220d14=0x1010000",
      "W 0x17a00104=0x20",
      "W 0x17a00184=0x20",
      "W 0x18220d14=0x10000",
      "W 0x18220d14=0",
      "W 0x18220d1c=0",
      "W 0x18220d10=0",
      "W 0x18220d08=0x1",
      "W 0x18220d00=0x41",
     ]),
    "rail-kept-irq": (92, 92,
     "6e34d21512192f3322bde46f6fde1c318c6ba626b638cf730bf28afff5fbeefa",
     [
     ]),
}

env = dict(os.environ)
env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '') + ':halt_on_error=1:print_stacktrace=1'

with tempfile.TemporaryDirectory(prefix='mh2lm-executor-') as temp:
    temp = Path(temp)
    include = temp / 'include' / 'IOKit'
    include.mkdir(parents=True)
    (include / 'IOTypes.h').write_text(PRELUDE)
    compiler = os.environ.get('CXX', 'clang++-19')
    flags = ['-std=c++17', '-g', '-pthread', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer']

    # The module compiles against nothing but the system headers: if it ever
    # needs the owner, this unit stops building.
    unit = temp / 'units.cpp'
    unit.write_text('#define ARM64_BOARD_CONFIG_MH2LM 1\n' + UNITS)
    binary = temp / 'units'
    subprocess.run([compiler] + flags + ['-DARM64_BOARD_CONFIG_MH2LM=1',
                    '-I', str(temp / 'include'), '-I', str(arm), str(unit),
                    str(arm / 'SweetRPMhExecutor.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, env=env, timeout=120)

    # Four entry points and no storage: a data symbol here would be a
    # transaction the module remembers between calls.
    obj = temp / 'executor.o'
    subprocess.run([compiler, '-std=c++17', '-O2', '-DARM64_BOARD_CONFIG_MH2LM=1',
                    '-I', str(temp / 'include'), '-I', str(arm), '-c',
                    str(arm / 'SweetRPMhExecutor.cpp'), '-o', str(obj)], check=True)
    nm = shutil.which('llvm-nm-19') or shutil.which('llvm-nm') or 'nm'
    listed = subprocess.run([nm, '-S', '--defined-only', str(obj)],
                            check=True, capture_output=True, text=True).stdout
    defined = {}
    for line in listed.splitlines():
        fields = line.split()
        if len(fields) >= 4:
            defined[fields[-1]] = int(fields[1], 16)
    assert set(defined) == {
        '_ZN17SweetRPMhExecutor3armERNS_5PortsERK15SweetRPMhLayoutjPj',
        '_ZN17SweetRPMhExecutor4stepERNS_5PortsERK15SweetRPMhLayoutRK13SweetRPMhWordPbmmPj',
        '_ZN17SweetRPMhExecutor6verifyERNS_5PortsERK15SweetRPMhLayoutjjjPj',
        '_ZN17SweetRPMhExecutor12stageCleanupERNS_5PortsERK15SweetRPMhLayoutjP13SweetRPMhWordPjjjj',
    }, sorted(defined)

    # Now the shipped owner, through the display fixture it already ships with.
    parent = Path(__file__).with_name('test-mh2lm-rpmh-resources.py')
    ns = {'__file__': str(parent), '__name__': 'rpmh_executor_fixture'}
    exec(compile(parent.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
                 str(parent), 'exec'), ns)
    common = ns['ns']
    helpers = ns['tests'].split('int main(){')[0]
    cpp = temp / 'dump.cpp'
    cpp.write_text(common['prelude'] + common['DSI_MEMORY_FIXTURE'] +
                   common['pool_fixture'] + common['header'] + common['source'] +
                   helpers + DUMP)
    binary = temp / 'dump'
    subprocess.run([compiler] + flags + [str(cpp), '-o', str(binary)], check=True)
    capture = subprocess.run([str(binary)], check=True, env=env, timeout=300,
                             capture_output=True, text=True).stdout
    again = subprocess.run([str(binary)], check=True, env=env, timeout=300,
                           capture_output=True, text=True).stdout
    assert capture == again, 'the recorded trajectory is not reproducible in one binary'

seen = {}
label = None
messages = []
for line in capture.splitlines():
    if line.startswith('## '):
        label = line[3:].strip()
        seen[label] = []
    elif line.startswith('#MSG '):
        messages.append(line[5:])
    elif line[:1] in ('W', 'R') and label:
        seen[label].append(line)
    elif line:
        assert line == 'DUMP-OK', line
assert set(seen) == set(PINNED), sorted(seen)

for name, (records, reads, digest, writes) in PINNED.items():
    body = seen[name]
    assert len(body) == records, (name, len(body), records)
    assert len(body) - sum(1 for line in body if line[0] == 'W') == reads, name
    assert [line for line in body if line[0] == 'W'] == writes, name
    assert hashlib.sha256('\n'.join(body).encode()).hexdigest() == digest, name

# The old service still owns the hardware and says so, once, on a line that the
# boot log level keeps; the module never announces a completion of its own.
owner_lines = [m for m in messages if 'executor=module' in m]
assert owner_lines, messages[-4:]
for line in owner_lines:
    assert re.fullmatch(r'SWEET RPMH: executor=module tcs=\d+ commands=\d+ open=0 '
                        r'hardware-owner=SweetRPMh\n?', line), line
assert [m for m in messages if 'ACTIVE ACK' in m], 'the owner must still announce its own ACKs'

print('PASS: actual executor module compiled alone; four entry points and no storage, '
      'mechanics unit cases, and the shipped owner reproducing the pre-extraction '
      'ACTIVE trajectory record for record (%d records over %d scenarios)' %
      (sum(pin[0] for pin in PINNED.values()), len(PINNED)))