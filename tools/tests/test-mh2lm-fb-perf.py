#!/usr/bin/env python3
"""Check actual opt-in FB timing accounting without changing presentation policy.
Usage: test-mh2lm-fb-perf.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
existing = here/'test-mh2lm-framebuffer-service.py'
ns = {'__file__': str(existing), '__name__': 'perf_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)
prelude = ns['prelude'].replace('else if(!strcmp(k,"sweetfbpreserveoff"))',
                               'else if(!strcmp(k,"sweetfbperf"))v=1;\n else if(!strcmp(k,"sweetfbpreserveoff"))')
# Each clock read advances by1us, independently from copy/flush counters.
prelude = prelude.replace('if(fakeTicks)return fakeTicks;', 'if(fakeTicks)return ++fakeTicks*1000;')
tests = r'''
int main(){
 SweetFBTiming timing{};
 assert(timing.percentile(50)==0);
 for(unsigned n=1;n<=100;++n)timing.add(n*1000,33000);
 assert(timing.count==100&&timing.overBudget==67&&timing.maxNS==100000);
 assert(timing.percentile(50)==64&&timing.percentile(99)==128);
 SweetFBTiming huge{};huge.add(4000000000000ULL,1);assert(huge.percentile(99)==4000000000ULL);
 fakeTicks=1000000;
 auto*provider=new IOService;auto*f=new AppleCLCD;assert(f->start(provider)&&f->fPerf.enabled);
 testSource=new SweetSurface;testSource->id=2;
 std::fill(std::begin(testSource->pixels),std::end(testSource->pixels),0xff112233);
 uint8_t rec[SWEET_FB_SWAP_REC_SIZE]{};
 auto put=[&](unsigned o,uint32_t n){memcpy(rec+o,&n,4);};
 put(0x104,1);put(0x54,2);put(0xcc,4);put(0xd0,4);
 f->fDirty=false;
 f->swap(rec);auto first=f->fPerf.firstPendingNS;f->swap(rec);
 assert(f->fPerf.swaps==2&&f->fPerf.coalesced==1&&f->fPerf.firstPendingNS==first&&f->fPerf.latestPendingNS>first);
 f->fFlushTicks=1;AppleCLCD::vsyncFired(f,f->fVsync);
 assert(f->fPerf.copy.count==1&&f->fPerf.present.count==1&&f->fPerf.presents==1&&f->fPerf.bytes==64);
 assert(f->fPerf.pending.count==1&&f->fPerf.latest.count==1&&!f->fPerf.firstPendingNS&&!f->fPerf.latestPendingNS);
 assert(f->fPerf.pending.maxNS>=f->fPerf.latest.maxNS&&f->fPerf.copy.totalNS>0);
 f->swap(rec);AppleCLCD::vsyncFired(f,f->fVsync);
 assert(f->fPerf.interval.count==1&&f->fPerf.copy.count==2&&f->fPerf.bytes==128);
 // Real API power transition, with NO vsync callback while OFF. Pending
 // swap age deliberately survives; present interval must not include OFF.
 auto bytes=f->fPerf.bytes;f->swap(rec);
 auto pendingFirst=f->fPerf.firstPendingNS,pendingLast=f->fPerf.latestPendingNS;
 assert(f->fPerf.lastPresentNS&&pendingFirst&&pendingLast);
 f->fHardwareVsyncRequested=true;
 assert(f->requestPanelPower(true)==kIOReturnSuccess);
 assert(!f->fPerf.lastPresentNS&&f->fPerf.bytes==bytes);
 assert(f->fPerf.firstPendingNS==pendingFirst&&f->fPerf.latestPendingNS==pendingLast);
 auto intervalCount=f->fPerf.interval.count;
 f->fPerf.nextReportNS=fakeTicks*1000+200000000000ULL;
 fakeTicks+=90000000; // OFF: no timer or IRQ callback
 assert(f->requestPanelPower(false)==kIOReturnSuccess&&!f->fPerf.lastPresentNS);
 f->fHardwareVsyncRequested=false;
 AppleCLCD::vsyncFired(f,f->fVsync);
 assert(f->fPerf.interval.count==intervalCount&&f->fPerf.lastPresentNS);
 assert(f->fPerf.pending.maxNS>=90000000000ULL); // retained pending includes OFF
 assert(!f->fPerf.firstPendingNS&&!f->fPerf.latestPendingNS);
 f->reportPerf(f->fPerf.nextReportNS);assert(!f->fPerf.copy.count&&!f->fPerf.bytes&&!f->fPerf.swaps);
 // Disabled mode must avoid timestamps/accounting entirely.
 f->fPerf.enabled=false;auto ticks=fakeTicks;f->swap(rec);f->present(rec);
 assert(fakeTicks==ticks&&!f->fPerf.swaps&&!f->fPerf.bytes);
 testSource->release();testSource=nullptr;f->stop(provider);f->release();provider->release();
 assert(!live&&!rawBlocks);
 puts("PASS: real FB perf opt-in; copy bytes/time, CPU intervals, first/latest pending age, coalescing, OFF gap exclusion, histogram bounds/reset and disabled-path neutrality");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-fb-perf-') as temp:
    cpp = Path(temp)/'test.cpp'
    binary = Path(temp)/'test'
    cpp.write_text(prelude+ns['source']+tests)
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I'+str(ns['root']/'bsd/dev'), str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
