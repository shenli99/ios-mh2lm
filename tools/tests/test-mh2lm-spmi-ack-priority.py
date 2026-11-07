#!/usr/bin/env python3
"""Run the real SPMI acquisition helpers with controlled contending threads.

Checks sleeping/recheck/wakeup and ACK-before-next-telemetry admission. Host
mutex scheduling cannot establish a native XNU latency bound or IRQ timing.
Usage: test-mh2lm-spmi-ack-priority.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
source = (Path(sys.argv[1])/'bsd/dev/sweet_spmi.c').read_text()

def function(name):
    match = re.search(r'\n'+name+r'\([^)]*\)\n\{', source)
    assert match, name
    start = source.rfind('\n\n', 0, match.start())+2
    end = source.index('{', match.start())+1
    depth = 1
    while depth:
        depth += (source[end] == '{')-(source[end] == '}')
        end += 1
    return source[start:end]+'\n'

prelude = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
using IOLock=std::mutex;
static IOLock controller;
static IOLock *sp_lock=&controller;
static unsigned sp_ack_waiters;
static std::condition_variable condition;
static std::atomic<unsigned> sleepers{0},ackDone{0},normalDone{0};
static std::atomic<bool> pauseAck{true};
static thread_local bool urgent=false;
static thread_local unsigned depth=0;
#define THREAD_UNINT 0
static void IOLockLock(IOLock *l) {
    if(urgent)while(pauseAck)std::this_thread::yield();
    l->lock();assert(depth++==0);
}
static void IOLockUnlock(IOLock *l) {assert(--depth==0);l->unlock();}
static int IOLockSleep(IOLock *l,void *event,int flags) {
    assert(event==&sp_ack_waiters && flags==THREAD_UNINT && depth==1);
    ++sleepers;--depth;
    std::unique_lock<std::mutex> guard(*l,std::adopt_lock);
    condition.wait(guard);guard.release();++depth;--sleepers;return 0;
}
static void IOLockWakeup(IOLock *,void *event,bool one) {
    assert(event==&sp_ack_waiters && !one && depth==1);condition.notify_all();
}
'''
checks = r'''
int main() {
    for(unsigned round=0;round<100;++round) {
        ackDone=normalDone=0;pauseAck=true;
        sweet_spmi_irq_lock();
        std::vector<std::thread> acknowledgers,readers;
        for(unsigned n=0;n<3;++n)acknowledgers.emplace_back([]{
            urgent=true;sweet_spmi_irq_ack_lock();
            assert(normalDone==0);++ackDone;sweet_spmi_irq_unlock();
        });
        while(__atomic_load_n(&sp_ack_waiters,__ATOMIC_ACQUIRE)!=3)std::this_thread::yield();
        for(unsigned n=0;n<8;++n)readers.emplace_back([]{
            sweet_spmi_irq_lock();assert(ackDone==3);++normalDone;sweet_spmi_irq_unlock();
        });
        sweet_spmi_irq_unlock();
        // Hold urgent lock acquisition back until EVERY normal contender has
        // actually checked the predicate and slept, not just lost a race.
        while(sleepers!=8)std::this_thread::yield();
        // Force a wake without changing the predicate; readers must recheck.
        for(unsigned n=0;n<3;++n){condition.notify_all();std::this_thread::yield();}
        assert(!normalDone);pauseAck=false;
        for(auto &t:acknowledgers)t.join();
        for(auto &t:readers)t.join();
        assert(!sleepers && !sp_ack_waiters && ackDone==3 && normalDone==8);
        sweet_spmi_irq_lock();sweet_spmi_irq_unlock();
    }
    puts("PASS: actual admission helpers, 100 controlled schedules, 1100 threads; no bypass/lost wake or stranded normal reader");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-ack-priority-') as directory:
    cpp=Path(directory)/'test.cpp'
    binary=Path(directory)/'test'
    cpp.write_text(prelude+''.join(function(n) for n in [
        'sweet_spmi_irq_lock','sweet_spmi_irq_ack_lock','sweet_spmi_irq_unlock'])+checks)
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-O1','-g',
        '-pthread','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
