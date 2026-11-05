#!/usr/bin/env python3
"""Exercise actual shared SPMI init/lock and key/battery observer methods.

Host memory stands in for already-completed MMIO commands; this checks locking,
initialization races and error unwind, not real PMIC command completion/timing.
Usage: test-mh2lm-spmi-serialization.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1])
spmi = (root/'bsd/dev/sweet_spmi.c').read_text()
keys = (root/'bsd/dev/sweet_keys.c').read_text()

def function(source, name):
    m = re.search(r'\n'+name+r'\([^)]*\)\n\{', source)
    assert m, name
    start = source.rfind('\n\n', 0, m.start())+2
    end = source.index('{', m.start())+1
    depth = 1
    while depth:
        depth += (source[end] == '{')-(source[end] == '}')
        end += 1
    # Only target-specific memory barriers are omitted on the host.
    return re.sub(r'^\s*__asm__.*\n', '\n', source[start:end]+'\n', flags=re.M)

prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
using vm_offset_t=uintptr_t; using vm_size_t=size_t; using IOLock=std::mutex;
static std::atomic<unsigned> lockAllocations{0},lockFrees{0},lockAttempts{0},mapCalls{0};
static bool failAlloc=false;
static unsigned failMap=0;
static IOLock *IOLockAlloc() { if(failAlloc)return nullptr; ++lockAllocations;return new IOLock; }
static void IOLockFree(IOLock *p) {++lockFrees;delete p;}
static void IOLockLock(IOLock *p) {++lockAttempts;p->lock();}
static void IOLockUnlock(IOLock *p) {p->unlock();}
static uint32_t coreMemory[0x2000/4],cfgMemory[0x1000/4],obsMemory[0x10000/4];
#define SP_CORE_PA 0x0c440000u
#define SP_CFG_PA 0x0c40a000u
#define SP_OBS_PA 0x0e600000u
#define SP_NO_APID 0xffffu
#define SP_APID_MAP_OFF 0x900u
#define SP_OWNERSHIP_OFF 0x700u
#define KEYS_NO_APID 0xffffu
#define SWEET_LOG_ERROR(...) ((void)0)
#define SWEET_LOG_INFO(...) ((void)0)
static vm_offset_t ml_io_map(vm_offset_t a,vm_size_t) {
    auto n=++mapCalls;if(failMap && n==failMap)return 0;
    if(a==SP_CORE_PA)return reinterpret_cast<vm_offset_t>(coreMemory);
    if(a==SP_CFG_PA)return reinterpret_cast<vm_offset_t>(cfgMemory);
    assert(a==SP_OBS_PA);return reinterpret_cast<vm_offset_t>(obsMemory);
}
static void delay(unsigned) {}
static vm_offset_t sp_core,sp_obs,sp_cfg,keys_obs;
static IOLock *sp_lock;
static unsigned sp_count;
static bool sp_attempted,sp_ready,sp_poisoned[512];
static uint32_t keys_last_status;
static uint32_t keys_rd(vm_offset_t base,uint32_t off) {return *reinterpret_cast<uint32_t *>(base+off);}
'''
units = ''.join(function(spmi, n) for n in ['sp_rd', 'sp_find_apid', 'sp_get_lock',
    'sp_initialize', 'sweet_spmi_init', 'sweet_spmi_irq_lock', 'sweet_spmi_irq_unlock',
    'sweet_spmi_read'])+function(keys, 'keys_read')
checks = r'''
int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];
    coreMemory[0]=0x50000000;coreMemory[1]=512;
    coreMemory[(0x900+4*269)/4]=0x01000000|(0x213<<8);
    obsMemory[(269*0x80+8)/4]=1;obsMemory[(269*0x80+0x18)/4]=0xa5;
    obsMemory[(261*0x80+8)/4]=1;obsMemory[(261*0x80+0x18)/4]=0x5a;
    keys_obs=reinterpret_cast<vm_offset_t>(obsMemory);
    if(!std::strcmp(mode,"alloc-fail")) {
        failAlloc=true;assert(sweet_spmi_init()==-1 && !mapCalls && !sp_attempted);
        failAlloc=false;
    }
    if(!std::strcmp(mode,"map-fail"))failMap=2;
    std::atomic<unsigned> done{0};std::vector<std::thread> init;
    for(unsigned n=0;n<16;++n)init.emplace_back([&]{
        assert(sweet_spmi_init()==(failMap?-1:0));++done;
    });
    for(auto &thread:init)thread.join();
    assert(done==16 && mapCalls==3 && lockAllocations-lockFrees==1);
    if(!failMap) {
        uint8_t key=0,battery=0;
        // A valid-looking Type-C APID must not bypass the observer whitelist.
        // This is the known software cause of the old diagnostic short circuit;
        // a rejected read must not acquire the lane or touch its command MMIO.
        coreMemory[(0x900+4*270)/4]=0x01000000|(0x215<<8);
        obsMemory[(270*0x80+8)/4]=1;
        unsigned beforeReject=lockAttempts;battery=9;
        for(unsigned address:{0x1506u,0x1509u,0x150bu}) {
            assert(sweet_spmi_read(2,address,&battery)==-1 && battery==9);
            assert(lockAttempts==beforeReject && !obsMemory[270*0x80/4]);
        }
        // Hold the real transport lock: both real observer methods must wait
        // before programming even disjoint command lanes.
        sweet_spmi_irq_lock();unsigned attempts=lockAttempts;
        std::atomic<unsigned> readersDone{0};
        std::thread k([&]{assert(keys_read(261,0xc510,&key));++readersDone;});
        std::thread b([&]{assert(sweet_spmi_read(2,0x1310,&battery)==0);++readersDone;});
        while(lockAttempts<attempts+2)std::this_thread::yield();
        assert(!readersDone && !obsMemory[261*0x80/4] && !obsMemory[269*0x80/4]);
        sweet_spmi_irq_unlock();k.join();b.join();
        assert(key==0x5a && battery==0xa5 && readersDone==2);
        assert(obsMemory[261*0x80/4]==((1u<<27)|(0x10u<<4)));
        assert(obsMemory[269*0x80/4]==((1u<<27)|(0x10u<<4)));
        std::thread kr([&]{for(unsigned i=0;i<100;++i)assert(keys_read(261,0xc510,&key));});
        std::thread br([&]{for(unsigned i=0;i<100;++i)assert(!sweet_spmi_read(2,0x1310,&battery));});
        kr.join();br.join();
        // All terminal status/timeout exits must unlock and preserve output.
        key=7;battery=9;obsMemory[(261*0x80+8)/4]=9;obsMemory[(269*0x80+8)/4]=9;
        assert(!keys_read(261,0xc510,&key) && key==7);
        assert(sweet_spmi_read(2,0x1310,&battery)==-1 && battery==9);
        obsMemory[(261*0x80+8)/4]=0;obsMemory[(269*0x80+8)/4]=0;
        assert(!keys_read(261,0xc510,&key) && key==7);
        assert(sweet_spmi_read(2,0x1310,&battery)==-1 && battery==9 && sp_poisoned[269]);
        obsMemory[269*0x80/4]=0;
        assert(sweet_spmi_read(2,0x1310,&battery)==-1 && !obsMemory[269*0x80/4]);
        sweet_spmi_irq_lock();sweet_spmi_irq_unlock();
    } else assert(!sp_ready && sweet_spmi_init()==-1 && mapCalls==3);
    IOLockFree(sp_lock);sp_lock=nullptr;assert(lockAllocations==lockFrees);
    std::printf("PASS: actual SPMI serialization/init/error paths %s\n",mode);
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-spmi-lock-') as directory:
    source = Path(directory)/'test.cpp'
    binary = Path(directory)/'test'
    source.write_text(prelude+units+checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-pthread',
        '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    for mode in ['normal', 'alloc-fail', 'map-fail']:
        subprocess.run([str(binary), mode], env=env, check=True, timeout=20)
    for _ in range(10):
        subprocess.run([str(binary), 'normal'], env=env, check=True, timeout=20,
            stdout=subprocess.DEVNULL)
    print('PASS: 10 additional actual init/observer concurrency runs')
