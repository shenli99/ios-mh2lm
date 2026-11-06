#!/usr/bin/env python3
"""Execute actual MH2LM restricted transport + mux RW method against a host model.

Usage: test-mh2lm-charger-transport.py EXTRACTED_XNU
Models MMIO completion/ownership, not physical charger behavior or native IOKit.
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
source = (root/'bsd/dev/sweet_spmi.c').read_text()
header = (root/'bsd/dev/sweet_spmi.h').read_text()
mux = (root/'iokit/Kernel/arm/SweetSpmiInterruptMux.cpp').read_text()
battery = (root/'iokit/Kernel/arm/SweetBattery.cpp').read_text()

def function(text, name):
    match = re.search(r'\n'+re.escape(name)+r'\([^)]*\)\n\{', text)
    assert match, name
    start = text.rfind('\n\n', 0, match.start())+2
    end = text.index('{', match.start())+1
    depth = 1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[start:end]

# Only MMIO/barriers and platform headers are replaced; execute the complete C
# transport, its table/validation and the actual mux write method unmodified.
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
source = source.replace(function(source, 'sp_rd'),
    'static uint32_t sp_rd(vm_offset_t b,unsigned o) { return modelRead(b,o); }')
source = source.replace(function(source, 'sp_wr'),
    'static void sp_wr(vm_offset_t b,unsigned o,uint32_t v) { modelWrite(b,o,v); }')
source = re.sub(r'^\s*__asm__.*\n', '\n', source, flags=re.M)
prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#define ARM64_BOARD_CONFIG_SWEET 1
#define ARM64_BOARD_CONFIG_MH2LM 1
#define SWEET_DEV_SPMI_CORE_PA 0x0c440000u
#define SWEET_DEV_SPMI_CHNLS_PA 0x0c500000u
#define SWEET_DEV_SPMI_OBS_PA 0x0e600000u
#define SWEET_DEV_SPMI_CFG_PA 0x0c40a000u
#define SWEET_DEV_SPMI_INTR_PA 0x0c4c0000u
#define VM_WIMG_IO 0
#define PAGE_MASK 0x3fffULL
#define trunc_page(a) ((a)&~PAGE_MASK)
#define round_page(a) (((a)+PAGE_MASK)&~PAGE_MASK)
static std::string logs;
static void recordLog(const char *format,...) __attribute__((format(printf,1,2)));
static void recordLog(const char *format,...) {
    char buf[1024];va_list args;va_start(args,format);
    int n=vsnprintf(buf,sizeof(buf),format,args);va_end(args);
    assert(n>=0 && static_cast<size_t>(n)<sizeof(buf));logs+=buf;
}
#define SWEET_LOG_ERROR(...) recordLog(__VA_ARGS__)
#define SWEET_LOG_WARN(...) recordLog(__VA_ARGS__)
#define SWEET_LOG_INFO(...) recordLog(__VA_ARGS__)
using vm_offset_t=uintptr_t; using vm_size_t=size_t; using IOLock=std::mutex;
static thread_local unsigned depth,sessionDepth;
static IOLock *controllerLock;
static bool failAlloc,failMap,failPermanent,dropWrite,failFgRelease;
static unsigned failFgAddress;
static std::vector<unsigned> rwAddresses;
static std::vector<unsigned> observerApids;
static unsigned maps,unmaps,commands,writes,readCommands,ioReads,failReadAt;
static unsigned observerStatus=1,rwStatus=1;
static uint32_t version=0x50000000;
static std::map<unsigned,unsigned> routing,ownership,pmic,status,data;
static IOLock *IOLockAlloc(){if(failAlloc)return nullptr;auto *p=new IOLock;
    if(!controllerLock)controllerLock=p;return p;}
static void IOLockFree(IOLock *l){delete l;}
static void IOLockLock(IOLock *l){l->lock();if(l==controllerLock)assert(depth++==0);else assert(sessionDepth++==0);}
static void IOLockUnlock(IOLock *l){if(l==controllerLock)assert(--depth==0);else assert(--sessionDepth==0);l->unlock();}
static void delay(unsigned n) {assert(depth==1 || (sessionDepth==1 && n==40));}
static void IOSleep(unsigned) {
    assert(depth==0 && sessionDepth==1);
    // A different controller user must remain runnable while grant/ALG waits.
    std::thread worker([]{IOLockLock(controllerLock);IOLockUnlock(controllerLock);});worker.join();
}
extern "C" vm_offset_t ml_io_map(vm_offset_t a,vm_size_t){assert(depth==1);return failPermanent?0:a;}
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t a,vm_size_t n,unsigned){
    assert(depth==1 && n==0x1000 && a>=SWEET_DEV_SPMI_CHNLS_PA);
    if(failMap)return 0;++maps;return a;
}
extern "C" void ml_io_unmap(vm_offset_t a,vm_size_t n){assert(depth==1 && !(a&PAGE_MASK) && n==0x4000);++unmaps;}
static uint32_t modelRead(vm_offset_t b,unsigned o){
    assert(depth==1);++ioReads;
    if(b==SWEET_DEV_SPMI_CORE_PA){
        if(o==0)return version;if(o==4)return 512;
        assert(o>=0x900 && o<0x1100);return routing[(o-0x900)/4];
    }
    if(b==SWEET_DEV_SPMI_CFG_PA){assert(o>=0x700 && o<0xf00);return ownership[(o-0x700)/4];}
    if(o==8)return status[b];if(o==0x18 || o==0x10)return data[b];
    assert(false);return 0;
}
static void modelWrite(vm_offset_t b,unsigned o,uint32_t v){
    assert(depth==1);
    if(o==0x10){data[b]=v;return;}
    assert(o==0);++commands;
    unsigned apid,reg=(v>>4)&255;
    if(b>=SWEET_DEV_SPMI_OBS_PA){
        assert((v>>27)==1 && !(v&15));++readCommands;
        apid=(b-SWEET_DEV_SPMI_OBS_PA)/0x80;
        observerApids.push_back(apid);
        unsigned address=(((routing[apid]>>8)&0xfff)<<8)|reg;
        if(address>=0x24400 && address<0x24a00)assert((pmic[0x24340]&1) && (pmic[0x24350]&0xa0)==0x80);
        status[b]=(failReadAt==readCommands || address==failFgAddress)?3:observerStatus;
        data[b]=pmic[(((routing[apid]>>8)&0xfff)<<8)|reg];
    }else{
        assert(b>=SWEET_DEV_SPMI_CHNLS_PA && (v>>27)==0);++writes;
        apid=(b-SWEET_DEV_SPMI_CHNLS_PA)/0x10000;
        assert(ownership[apid]==0); // foreign writer permission is never borrowed
        unsigned address=(((routing[apid]>>8)&0xfff)<<8)|reg;rwAddresses.push_back(address);
        if(failFgRelease && address==0x24350 && !(data[b]&0x80)){status[b]=3;return;}
        status[b]=rwStatus;
        if(!dropWrite && (rwStatus==1 || rwStatus==0))
            pmic[(((routing[apid]>>8)&0xfff)<<8)|reg]=data[b]&255;
    }
}
'''
mux_prelude = r'''
#define MUX_KEYS 0
struct MuxRoute {vm_offset_t channel; bool writePoisoned; unsigned apid,group;const char *name;};
struct SweetSpmiInterruptMux {
    MuxRoute routes[2];
    bool writePeripheral(MuxRoute &,unsigned,uint16_t,unsigned);
};
static void write32(vm_offset_t b,unsigned o,uint32_t v){modelWrite(b,o,v);}
static uint32_t read32(vm_offset_t b,unsigned o){return modelRead(b,o);}
'''
preflight_prelude = r'''
using IOReturn=int;
#define kIOReturnNotReady -1
#define kIOReturnSuccess 0
#define SWEET_BATT_SID 2
#define OSDynamicCast(type,object) dynamic_cast<type *>(object)
struct OSObject {virtual ~OSObject()=default;};
struct SweetBattery : OSObject {
    bool fRunning=false,fSid2=false;
    static IOReturn transportPreflightAction(OSObject *,void *,void *,void *,void *);
};
static void clock_get_uptime(uint64_t *t){assert(depth==0);*t=123456;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *n){*n=t;}
'''
checks = r'''
static void own(unsigned apid,unsigned per,unsigned ee=0,bool irq=false){
    routing[apid]=((0x200|per)<<8)|(irq?0x1000000:0);ownership[apid]=ee;
}
static void balanced(){assert(depth==0 && maps==unmaps);}
int main(int argc,char **argv){
    assert(argc==2);const char *mode=argv[1];uint8_t out=0xa5;
    assert(sweet_spmi_charger_read(2,0x1042,&out)==SWEET_SPMI_NOT_READY && out==0xa5);
    if(!strcmp(mode,"alloc-fail")){
        failAlloc=true;assert(sweet_spmi_init()==-1 && !sp_attempted);failAlloc=false;
    }
    if(!strcmp(mode,"map-fail")){
        failPermanent=true;assert(sweet_spmi_init()==-1);
        failPermanent=false;assert(sweet_spmi_init()==-1 && !sp_ready);
        assert(sweet_spmi_charger_update(2,0x1042,1,1,&out)==SWEET_SPMI_NOT_READY);
        IOLockFree(sp_lock);balanced();puts("PASS: permanent mapping failure rejects charger access");return 0;
    }
    assert(sweet_spmi_init()==0);
    own(10,0x10);own(11,0x13);own(12,0x15);own(13,0x16);
    if(!strncmp(mode,"fg-",3)) {
        for(unsigned n=0;n<7;++n)own(30+n,n==0?1:n==1?0x41:n==2?0x43:0x46+n-3);
        pmic[0x20105]=0x20;pmic[0x20103]=strncmp(mode,"fg-v2",5)?1:2;
        pmic[0x24305]=0x0d;pmic[0x24310]=8;pmic[0x241ee]=0x31;
        pmic[0x24858]=100;pmic[0x24859]=0;
        pmic[0x246ea]=0xa0;pmic[0x246eb]=0xf;
        pmic[0x24750]=1;
        pmic[0x24734]=0x3c;pmic[0x24735]=0xf;
        pmic[0x24738]=0x72;pmic[0x24739]=0xe;
        int wanted=0;
        // Real 0120 regression: REVID can be readable through EE0's observer
        // without an EE0 writer. DMA apertures also need reads, never writes.
        if(!strcmp(mode,"fg-foreign-identity")){ownership[30]=3;}
        if(!strcmp(mode,"fg-readonly-pages")){
            for(unsigned apid=33;apid<=36;++apid)ownership[apid]=3;
        }
        if(!strcmp(mode,"fg-v2-foreign-battinfo")){ownership[31]=3;}
        if(!strcmp(mode,"fg-local-preferred")){
            own(29,1,5);own(70,1,3);
        }
        if(!strcmp(mode,"fg-first-foreign")){
            ownership[30]=3;own(29,1,5);own(70,1,3);
        }
        if(!strcmp(mode,"fg-no-memif-writer")){ownership[32]=3;wanted=SWEET_SPMI_NO_OWNER;}
        if(!strcmp(mode,"fg-no-peek-writer")){ownership[31]=3;wanted=SWEET_SPMI_NO_OWNER;}
        if(!strcmp(mode,"fg-ambiguous-writer")){own(70,0x43);wanted=SWEET_SPMI_AMBIGUOUS;}
        if(!strcmp(mode,"fg-poisoned-writer")){sp_rw_poisoned[32]=true;wanted=SWEET_SPMI_POISONED;}
        if(!strcmp(mode,"fg-missing-identity")){routing.erase(30);wanted=SWEET_SPMI_NO_OWNER;}
        if(!strcmp(mode,"fg-missing-data")){routing.erase(35);wanted=SWEET_SPMI_NO_OWNER;}
        if(!strcmp(mode,"fg-observer-denied")){observerStatus=5;wanted=SWEET_SPMI_DENIED;}
        if(!strcmp(mode,"fg-observer-timeout")){observerStatus=0;wanted=SWEET_SPMI_TIMEOUT;}
        if(!strcmp(mode,"fg-recovery")){failFgAddress=0x24859;wanted=SWEET_SPMI_IO;}
        if(!strcmp(mode,"fg-busy")){pmic[0x24350]=0x80;wanted=SWEET_SPMI_BUSY;}
        if(!strcmp(mode,"fg-unknown")){pmic[0x20103]=3;wanted=SWEET_SPMI_UNSUPPORTED;}
        if(!strcmp(mode,"fg-grant")){pmic[0x24310]=0;wanted=SWEET_SPMI_TIMEOUT;}
        if(!strcmp(mode,"fg-active")){pmic[0x241ef]=8;wanted=SWEET_SPMI_TIMEOUT;}
        if(!strcmp(mode,"fg-read-fail")){failFgAddress=0x24859;wanted=SWEET_SPMI_IO;}
        if(!strcmp(mode,"fg-cleanup-fail")){failFgRelease=true;wanted=SWEET_SPMI_IO;}
        if(!strcmp(mode,"fg-dma-error")){pmic[0x24370]=4;wanted=SWEET_SPMI_IO;}
        uint16_t raw=0xdead;assert(sweet_spmi_fg_read(SWEET_FG_TEMPERATURE,&raw)==wanted);
        if(wanted)assert(raw==0xdead);else assert(raw==100);
        if(wanted)assert(logs.find("SWEET FG ACCESS: v2 field=0 op=")!=std::string::npos);
        if(!strcmp(mode,"fg-no-memif-writer")){
            assert(writes==0 && logs.find("op=writer-preflight sid=2 addr=0x4340 ppid=0x243 error=-4")!=std::string::npos);
            auto old=logs;assert(sweet_spmi_fg_read(SWEET_FG_TEMPERATURE,&raw)==wanted && logs==old && writes==0);
        }
        if(!strcmp(mode,"fg-no-peek-writer"))assert(writes==0 && logs.find("op=writer-preflight sid=2 addr=0x41ee")!=std::string::npos);
        if(!strcmp(mode,"fg-poisoned-writer") || !strcmp(mode,"fg-ambiguous-writer") ||
           !strcmp(mode,"fg-missing-identity") || !strcmp(mode,"fg-observer-denied") ||
           !strcmp(mode,"fg-observer-timeout"))assert(writes==0);
        if(!strcmp(mode,"fg-missing-identity"))assert(logs.find("op=observer-read sid=2 addr=0x105")!=std::string::npos);
        if(!strcmp(mode,"fg-missing-data"))assert(logs.find("op=observer-read sid=2 addr=0x4858")!=std::string::npos);
        if(!strcmp(mode,"fg-read-fail") || !strcmp(mode,"fg-recovery"))assert(logs.find("op=observer-read sid=2 addr=0x4859")!=std::string::npos);
        if(!strcmp(mode,"fg-local-preferred"))assert(observerApids.front()==30);
        if(!strcmp(mode,"fg-first-foreign"))assert(observerApids.front()==29);
        if(!strcmp(mode,"fg-foreign-identity"))assert(observerApids.front()==30);
        if(!strcmp(mode,"fg-observer-timeout")){
            auto prior=commands;observerStatus=1;
            assert(sweet_spmi_fg_read(SWEET_FG_TEMPERATURE,&raw)==SWEET_SPMI_POISONED && commands==prior);
        }
        if(wanted==SWEET_SPMI_BUSY || wanted==SWEET_SPMI_UNSUPPORTED || !strcmp(mode,"fg-dma-error"))assert(writes==0);
        if(writes) {
            assert(pmic[0x241ee]==0x31 && (pmic[0x24371]&2)==0);
            assert(!(pmic[0x24340]&1));
            if(!failFgRelease)assert((pmic[0x24350]&0xa0)==0);
        }
        if(!strcmp(mode,"fg-recovery")){
            failFgAddress=0;assert(!sweet_spmi_fg_read(SWEET_FG_TEMPERATURE,&raw) && raw==100);
            assert(logs.find("recovered=1")!=std::string::npos);
        }
        if(!wanted){
            assert(!sweet_spmi_fg_read(SWEET_FG_NOMINAL_CAPACITY,&raw) && raw==4000);
            assert(!sweet_spmi_fg_read(SWEET_FG_PROFILE_INTEGRITY,&raw) && raw==1);
            assert(!sweet_spmi_fg_read(SWEET_FG_LEARNED_CAPACITY,&raw) && raw==(pmic[0x20103]==1?3900:3698));
        }
        if(failFgRelease){
            unsigned prior=commands;assert(sp_fg_failed);
            assert(sweet_spmi_fg_read(SWEET_FG_TEMPERATURE,&raw)==SWEET_SPMI_POISONED && commands==prior);
        }
        for(auto a:rwAddresses)assert(a==0x24340 || a==0x24350 || a==0x24371 || a==0x241ee);
        auto count=commands;assert(sweet_spmi_fg_read(4,&raw)==SWEET_SPMI_INVALID && commands==count);
        balanced();assert(!sessionDepth);IOLockFree(sp_lock);if(sp_fg_lock)IOLockFree(sp_fg_lock);
        printf("PASS: actual SRAM protocol, bounds/unwind/no SRAM writes %s\n",mode);return 0;
    }
    struct sweet_spmi_charger_route route={0xaaaa,5,5};
    // A unique EE0 writer is usable without the IRQ-owner bit. Foreign duplicates
    // do not grant write access; a second EE0 writer is ambiguous even with IRQ.
    own(20,0x10,3,true);
    assert(sweet_spmi_charger_get_route(2,0x1042,&route)==0 && route.apid==10 && !route.irq_owner && !route.write_ee);
    own(21,0x10,0,true);route.apid=0xaaaa;
    assert(sweet_spmi_charger_get_route(2,0x1042,&route)==SWEET_SPMI_AMBIGUOUS && route.apid==0xaaaa);
    routing.erase(21);ownership[10]=3;
    assert(sweet_spmi_charger_get_route(2,0x1042,&route)==SWEET_SPMI_NO_OWNER && route.apid==0xaaaa);
    assert(sweet_spmi_charger_update(2,0x1042,1,1,&out)==SWEET_SPMI_NO_OWNER && !commands);
    ownership[10]=0;version=0x60000000;
    assert(sweet_spmi_charger_read(2,0x1042,&out)==SWEET_SPMI_UNSUPPORTED && !commands);version=0x50000000;
    unsigned before=ioReads;
    for(unsigned addr:{0x1014,0x1016,0x1344,0x1643,0x1644,0x1648,0x1690,0x1506,0x4300,0x104200})
        assert(sweet_spmi_charger_update(2,addr,1,1,&out)==SWEET_SPMI_INVALID);
    assert(sweet_spmi_charger_update(0,0x1042,1,1,&out)==SWEET_SPMI_INVALID);
    assert(sweet_spmi_charger_update(2,0x1042,2,2,&out)==SWEET_SPMI_INVALID);
    assert(sweet_spmi_charger_update(2,0x1042,0,0,&out)==SWEET_SPMI_INVALID);
    assert(sweet_spmi_charger_update(2,0x1042,1,2,&out)==SWEET_SPMI_INVALID);
    assert(sweet_spmi_charger_update(2,0x1042,1,1,nullptr)==SWEET_SPMI_INVALID);
    assert(sweet_spmi_charger_read(2,0x1507,&out)==SWEET_SPMI_INVALID);
    assert(ioReads==before && !commands && out==0xa5);
    // Legacy Type-C policy remains closed; only exact new status table opens.
    assert(sweet_spmi_read(2,0x1506,&out)==-1 && ioReads==before);
    pmic[0x21506]=0x21;assert(!sweet_spmi_charger_read(2,0x1506,&out) && out==0x21 && !maps);
    pmic[0x21042]=0xa0;
    assert(!sweet_spmi_charger_update(2,0x1042,1,1,&out) && out==0xa1 && pmic[0x21042]==0xa1 && writes==1);
    unsigned oldWrites=writes,oldMaps=maps;
    assert(!sweet_spmi_charger_update(2,0x1042,1,1,&out) && writes==oldWrites && maps==oldMaps);
    // Local RMW serialization must preserve independent concurrently changed bits.
    pmic[0x21366]=0x80;
    std::vector<std::thread> threads;
    for(unsigned bit=0;bit<3;++bit)threads.emplace_back([bit]{uint8_t v;
        for(unsigned i=0;i<100;++i)assert(!sweet_spmi_charger_update(2,0x1366,1u<<bit,1u<<bit,&v));});
    for(auto &t:threads)t.join();assert(pmic[0x21366]==0x87);balanced();
    SweetSpmiInterruptMux parallel{};
    parallel.routes[0]={SWEET_DEV_SPMI_CHNLS_PA+11*0x10000u,false,11,1,"usbin"};
    threads.clear();
    threads.emplace_back([&]{for(unsigned i=0;i<100;++i)
        assert(parallel.writePeripheral(parallel.routes[0],0x14,16,1));});
    threads.emplace_back([&]{for(unsigned i=0;i<100;++i){uint8_t v;
        assert(!sweet_spmi_charger_update(2,0x1370,255,i&31,&v));}});
    for(auto &t:threads)t.join();balanced();
    // Actual one-shot battery preflight: not-ready no access, then valid and
    // failed fields; every field continues independently and no write occurs.
    SweetBattery batt;before=commands;
    assert(SweetBattery::transportPreflightAction(&batt,0,0,0,0)==kIOReturnNotReady && commands==before);
    batt.fRunning=batt.fSid2=true;oldWrites=writes;oldMaps=maps;logs.clear();
    assert(!SweetBattery::transportPreflightAction(&batt,0,0,0,0));
    assert(writes==oldWrites && maps==oldMaps && logs.find("no control acquired")!=std::string::npos);
    ownership[11]=3;observerStatus=5;logs.clear();
    assert(!SweetBattery::transportPreflightAction(&batt,0,0,0,0));
    assert(writes==oldWrites && logs.find("route-error=-4 value=unknown")!=std::string::npos);
    assert(logs.find("read-error=-7 value=unknown")!=std::string::npos && logs.find("complete;")!=std::string::npos);
    ownership[11]=0;observerStatus=1;
    // Completed status errors preserve output and release temporary mappings.
    for(auto entry:std::map<unsigned,int>{{3,SWEET_SPMI_IO},{5,SWEET_SPMI_DENIED},{9,SWEET_SPMI_DROPPED}}){
        out=0xa5;observerStatus=entry.first;
        assert(sweet_spmi_charger_read(2,0x1042,&out)==entry.second && out==0xa5);
        observerStatus=1;rwStatus=entry.first;
        assert(sweet_spmi_charger_update(2,0x1042,1,0,&out)==entry.second && out==0xa5);balanced();
    }
    rwStatus=1;failMap=true;
    assert(sweet_spmi_charger_update(2,0x1042,1,0,&out)==SWEET_SPMI_MAP_FAILED && out==0xa5);
    failMap=false;dropWrite=true;
    assert(sweet_spmi_charger_update(2,0x1042,1,0,&out)==SWEET_SPMI_VERIFY && out==0xa5);
    dropWrite=false;failReadAt=readCommands+2;
    assert(sweet_spmi_charger_update(2,0x1042,1,0,&out)==SWEET_SPMI_IO && out==0xa5 && pmic[0x21042]==0xa0);
    failReadAt=0;balanced();
    // RW timeout may already have changed hardware. The actual mux method must
    // refuse reuse, not silently operate its independently cached route flag.
    rwStatus=0;
    assert(sweet_spmi_charger_update(2,0x1042,1,1,&out)==SWEET_SPMI_TIMEOUT && out==0xa5 && sp_rw_poisoned[10]);
    SweetSpmiInterruptMux mux{};
    mux.routes[0]={SWEET_DEV_SPMI_CHNLS_PA+10*0x10000u,false,10,1,"charger"};
    mux.routes[1]={SWEET_DEV_SPMI_CHNLS_PA+11*0x10000u,false,11,1,"usbin"};
    oldWrites=writes;
    assert(!mux.writePeripheral(mux.routes[0],0x14,2,1) && writes==oldWrites && mux.routes[0].writePoisoned);
    // Conversely a mux timeout prevents the charger transport's next command.
    assert(!mux.writePeripheral(mux.routes[1],0x14,16,1) && sp_rw_poisoned[11]);
    before=commands;
    assert(sweet_spmi_charger_update(2,0x1370,255,20,&out)==SWEET_SPMI_POISONED && commands==before);
    rwStatus=1;
    // Read-only telemetry can still use the separate observer lane.
    assert(!sweet_spmi_charger_read(2,0x1042,&out));
    out=0xa5;observerStatus=0;
    assert(sweet_spmi_charger_read(2,0x1506,&out)==SWEET_SPMI_TIMEOUT && out==0xa5);
    before=commands;observerStatus=1;
    assert(sweet_spmi_charger_read(2,0x1506,&out)==SWEET_SPMI_POISONED && commands==before);
    sweet_spmi_irq_lock();assert(sweet_spmi_rw_poisoned_locked(512));sweet_spmi_irq_unlock();
    balanced();IOLockFree(sp_lock);
    printf("PASS: restricted transport/atomic RMW/errors/shared mux poison %s\n",mode);
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-charge-io-') as directory:
    cpp = Path(directory)/'test.cpp'
    binary = Path(directory)/'test'
    cpp.write_text(prelude+header+source+mux_prelude+function(mux,'SweetSpmiInterruptMux::writePeripheral')+preflight_prelude+re.sub(r'^#if[^\n]*\n', '', function(battery,'SweetBattery::transportPreflightAction'), flags=re.M)+checks)
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-O1','-g','-pthread',
        '-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    for mode in ['normal','alloc-fail','map-fail','fg-v1','fg-v2','fg-busy','fg-unknown','fg-grant','fg-active','fg-read-fail','fg-cleanup-fail','fg-dma-error',
                 'fg-foreign-identity','fg-readonly-pages','fg-local-preferred','fg-first-foreign',
                 'fg-no-memif-writer','fg-no-peek-writer','fg-ambiguous-writer','fg-poisoned-writer',
                 'fg-missing-identity','fg-missing-data','fg-observer-denied','fg-observer-timeout',
                 'fg-v2-foreign-battinfo','fg-recovery']:
        subprocess.run([str(binary),mode],env=env,check=True,timeout=20)
