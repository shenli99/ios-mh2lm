#!/usr/bin/env python3
"""Run the actual C key worker/start/stop with host thread/MMIO adapters.
Checks pressed-key release, IRQ wake, stop during an observer read, start/map
failure unwind, and 16K mapping balance. Not native scheduler/hardware proof.
Usage: test-mh2lm-key-lifecycle.py EXTRACTED_XNU
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if len(sys.argv)!=2:
    sys.exit(__doc__)
s=(Path(sys.argv[1])/'bsd/dev/sweet_keys.c').read_text()
s=re.sub(r'^#include .*$', '',s,flags=re.M)
for name,body in [('keys_rd','return fakeRead(base, off);'),
                  ('keys_read','return fakeObserver(apid, addr, out);')]:
    m=re.search(r'\n'+name+r'\([^)]*\)\n\{',s);assert m,name
    a=s.index('{',m.start());b=a+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    s=s[:a]+'{ '+body+' }'+s[b:]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#define ARM64_BOARD_CONFIG_SWEET 1
#define SWEET_DEV_KEYS_VOLUME 1
#define SWEET_DEV_VOL_DOWN_PON 1
#define SWEET_DEV_VOL_GPIO_SID 0
#define SWEET_DEV_SPMI_CORE_PA 0x0c440000u
#define SWEET_DEV_SPMI_CFG_PA 0x0c40a000u
#define SWEET_DEV_SPMI_OBS_PA 0x0e600000u
#define SWEET_DEV_PON_RT 0x810u
#define SWEET_DEV_VOL_GPIO_RT 0xc510u
#define SWEET_DEV_VOL_GPIO2_RT 0xc610u
#define SWEET_LOG_INFO(...) ((void)0)
#define SWEET_LOG_DEBUG(...) ((void)0)
#define SWEET_LOG_WARN(...) ((void)0)
#define SWEET_LOG_ERROR(...) ((void)0)
#define SWEET_SPMI_IRQ_DIAG_ROUTE_META 1
#define SWEET_SPMI_IRQ_DIAG_ROUTE_OWNER 2
#define SWEET_SPMI_IRQ_DIAG_ROUTE_ENABLE 4
#define SWEET_SPMI_IRQ_DIAG_ROUTE_STATUS 8
#define SWEET_SPMI_IRQ_DIAG_ROUTE_CHANNEL 16
#define PAGE_MASK 0x3fffu
#define trunc_page(x) ((x)&~uintptr_t(PAGE_MASK))
#define round_page(x) (((x)+PAGE_MASK)&~uintptr_t(PAGE_MASK))
#define VM_WIMG_IO 7u
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL nullptr
#define KERN_SUCCESS 0
using vm_offset_t=uintptr_t;using vm_size_t=size_t;
using kern_return_t=int;using wait_result_t=int;using thread_t=std::thread *;
static std::mutex sleepLock,hwLock,hidLock;
static std::condition_variable sleepCV,readCV,hidCV;
static std::map<const volatile void *,unsigned> generations;
static thread_local const volatile void *waitChannel;
static thread_local unsigned waitGeneration;
static std::map<uintptr_t,size_t> mappings;
static std::vector<thread_t> workers;
static std::vector<std::pair<int,int>> hid;
static unsigned failMap;
static bool failThread=false,holdRead=false,readEntered=false,releaseRead=false,diag=false;
static std::atomic<bool> upPressed{false};
static int assert_wait(const volatile void *c,int) {
    std::lock_guard<std::mutex> g(sleepLock);waitChannel=c;waitGeneration=generations[c];return 0;
}
static void thread_wakeup(const volatile void *c) {
    {std::lock_guard<std::mutex> g(sleepLock);++generations[c];}sleepCV.notify_all();
}
static void thread_block(void *) {
    std::unique_lock<std::mutex> g(sleepLock);
    sleepCV.wait(g,[]{return generations[waitChannel]!=waitGeneration;});
}
struct ThreadExit {};
static thread_t current_thread() { return nullptr; }
kern_return_t thread_terminate(thread_t) { throw ThreadExit{}; }
static kern_return_t kernel_thread_start(void (*fn)(void *,wait_result_t),void *arg,thread_t *out) {
    if (failThread) return -1;
    *out=new std::thread([=]{try {fn(arg,0);}catch(ThreadExit &) {}});workers.push_back(*out);return 0;
}
static void thread_deallocate(thread_t) {}
static void joinWorkers() {for(auto *p:workers) {p->join();delete p;}workers.clear();}
vm_offset_t ml_io_map_unmappable(vm_offset_t a,vm_size_t n,unsigned flags) {
    assert(flags==7);std::lock_guard<std::mutex> g(hwLock);
    if (failMap && --failMap==0) return 0;
    auto base=trunc_page(a);assert(!mappings.count(base));mappings[base]=round_page(n+(a&PAGE_MASK));return a;
}
void ml_io_unmap(vm_offset_t a,vm_size_t n) {
    std::lock_guard<std::mutex> g(hwLock);assert(mappings.at(a)==n);mappings.erase(a);
}
static uint32_t fakeRead(vm_offset_t base,uint32_t off) {
    std::lock_guard<std::mutex> g(hwLock);assert(mappings.count(trunc_page(base)));
    if (base==SWEET_DEV_SPMI_CORE_PA) {
        if(off==0) return 0x50000000;
        if(off==4) return 512;
        if(off==0x900+4*248) return 0x01000000|(0x008<<8);
        if(off==0x900+4*261) return 0x01000000|(0x0c5<<8);
    }
    return 0;
}
static bool fakeObserver(uint16_t apid,uint16_t addr,uint8_t *out) {
    std::unique_lock<std::mutex> g(hwLock);assert(mappings.count(SWEET_DEV_SPMI_OBS_PA));
    if (holdRead) {readEntered=true;readCV.notify_all();readCV.wait(g,[]{return releaseRead;});}
    if (apid==248) *out=1; // power held
    else if (apid==261) {
        if (addr==SWEET_DEV_VOL_GPIO_RT) *out=upPressed?0:1;
        else if(addr==SWEET_DEV_VOL_GPIO_RT+0x36) *out=0x80;
        else if(addr==SWEET_DEV_VOL_GPIO_RT-0x0b) *out=0x10;
        else *out=0;
    } else return false;
    return true;
}
void sweet_hid_key(int k,int down) {
    {std::lock_guard<std::mutex> g(hidLock);hid.emplace_back(k,down);}hidCV.notify_all();
}
static bool PE_parse_boot_argn(const char *key,void *p,size_t) {
    if (diag && !std::strcmp(key,"sweetkeysirqdiag")) *static_cast<int *>(p)=0xc00;
    return false;
}
static int sweet_spmi_init() {return 0;}
static void sweet_spmi_irq_diag_source(unsigned,unsigned,const char *) {}
static void sweet_spmi_irq_diag_source_parts(unsigned,unsigned,const char *,unsigned) {}
void sweet_keys_stop(void);
'''
checks=r'''
int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];
    if (!std::strncmp(mode,"map-fail-",9)) failMap=unsigned(mode[9]-'0');
    diag=!std::strcmp(mode,"diagnostics");
    bool prepared=sweet_keys_prepare();
    if (!std::strncmp(mode,"map-fail-",9)) {
        assert(!prepared && mappings.empty());return 0;
    }
    assert(prepared && mappings.size()==3);
    if (!std::strcmp(mode,"thread-fail")) {
        failThread=true;assert(!sweet_keys_start());sweet_keys_stop();assert(mappings.empty());return 0;
    }
    if (!std::strcmp(mode,"inflight-read")) holdRead=true;
    assert(sweet_keys_start());
    if (holdRead) {
        {std::unique_lock<std::mutex> g(hwLock);readCV.wait(g,[]{return readEntered;});}
        std::atomic<bool> done{false};std::thread stopper([&]{sweet_keys_stop();done=true;});
        while(!__atomic_load_n(&keys_irq_failed,__ATOMIC_ACQUIRE)) std::this_thread::yield();
        assert(!done); // joining must not return while observer read is paused
        {std::lock_guard<std::mutex> g(hwLock);assert(mappings.size()==3);releaseRead=true;}
        readCV.notify_all();stopper.join();
    } else {
        {std::unique_lock<std::mutex> g(hidLock);hidCV.wait(g,[]{return !hid.empty();});}
        upPressed=true;sweet_keys_event(2);
        {std::unique_lock<std::mutex> g(hidLock);hidCV.wait(g,[]{for(auto h:hid)if(h.first==1&&h.second) return true;return false;});}
        sweet_keys_stop();
    }
    joinWorkers();assert(mappings.empty() && !keys_worker_running);
    unsigned pressed[3]={};
    for(auto e:hid) {if(e.second) {assert(!pressed[e.first]);++pressed[e.first];}
                    else {assert(pressed[e.first]);--pressed[e.first];}}
    for(auto n:pressed) assert(!n);
    sweet_keys_stop();assert(mappings.empty()); // idempotent cleanup
    std::printf("PASS: actual key backend %s, worker joined / HID released / mappings balanced\n",mode);
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-key-lifetime-') as d:
    src=Path(d)/'test.cpp';exe=Path(d)/'test';src.write_text(pre+s+checks)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-pthread',
                    '-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    for mode in ['normal','diagnostics','inflight-read','thread-fail','map-fail-1','map-fail-2','map-fail-3']:
        subprocess.run([str(exe),mode],env=env,check=True,timeout=15)
    for _ in range(20):
        subprocess.run([str(exe),'inflight-read'],env=env,check=True,timeout=15,stdout=subprocess.DEVNULL)
    print('PASS: 20 additional key worker stop/read races')
