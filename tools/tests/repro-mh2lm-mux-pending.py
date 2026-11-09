#!/usr/bin/env python3
"""Characterize BUG-002 using the actual mux implementation and host fixtures.

Usage: repro-mh2lm-mux-pending.py EXTRACTED_XNU
This intentionally expects the known defective isolation policy. REPRODUCED
means a defect was observed, not that a repaired driver passed regression.
Only host MMIO/event arrivals are instrumented; no kernel source is changed.
After a fix, replace these defect expectations with healthy/recovery assertions.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
if len(sys.argv)!=2:
    sys.exit(__doc__)
repo=Path(__file__).resolve().parents[2]
script=(repo/'tools/tests/test-mh2lm-mux-consumers.py').read_text()
script=script.replace('from sweet_mux_test_support import prelude, startup_adapter',f"sys.path.insert(0, {str(repo/'tools/tests')!r})\nfrom sweet_mux_test_support import prelude, startup_adapter")
script=script.replace("Path(__file__).with_name", f"Path({str(repo/'tools/tests/test-mh2lm-mux-consumers.py')!r}).with_name")
anchor="startup_adapter=startup_adapter.replace('class SweetBattery : public OSObject'"
pos=script.index(anchor)
hooks=r"""
# Only the fake read primitive gets an arrival hook. It returns the old
# snapshot, then raises CC after the last route read of the empty second pass.
prelude=prelude.replace('static uint32_t hwRead(uintptr_t b,unsigned off) {', '''
static bool lateArrivalArmed;
static unsigned ccReads, newEdges;
static uint32_t hwRead(uintptr_t b,unsigned off) {''')
prelude=prelude.replace('std::lock_guard<std::recursive_mutex> lock(hwLock); return mmio[b+off];', '''
    std::lock_guard<std::recursive_mutex> lock(hwLock);
    uint32_t value=mmio[b+off];
    if(lateArrivalArmed && b==SWEET_DEV_SPMI_CHNLS_PA+271*0x10000 && off==0x104 && ++ccReads==12) {
        assert(value==0);mmio[b+off]|=4;pmic[(0x215<<8)|0x18]|=4;
        ++newEdges;lateArrivalArmed=false;
    }
    return value;''')
prelude=prelude.replace('#define SWEET_LOG_ERROR(...) ((void)0)', '#define SWEET_LOG_ERROR(...) std::printf(__VA_ARGS__)')
"""
script=script[:pos]+hooks+'\n'+script[pos:]
checks=r'''
static unsigned reproCallbacks, reproFailures, reproRefills;
static bool refillArmed;
static void reproNotify(void *,unsigned events) {
    if(events&SWEET_MUX_FAILED) {++reproFailures;return;}
    ++reproCallbacks;
    if(refillArmed && reproRefills<4) {
        ++reproRefills;++newEdges;
        mmio[SWEET_DEV_SPMI_CHNLS_PA+271*0x10000+0x104]|=4;
        pmic[(0x215<<8)|0x18]|=4;
    }
}
static void reproCheck(const char *mode) {
    assert(startMux()==0);assert(!attachKeys());int context=1;
    unsigned events=SWEET_BATT_IRQ_TYPEC|SWEET_BATT_IRQ_USB|SWEET_BATT_IRQ_CHG;
    assert(!testAttach(1,events,reproNotify,&context));pump();
    reproCallbacks=reproFailures=reproRefills=0;
    unsigned before=testInstance->routeCount;
    if(!std::strcmp(mode,"bug002-control")) {
        for(unsigned i=0;i<5;++i) {inject(271,4);pump();}
        assert(testInstance->routes[10].hits==5 && !testInstance->isolatedGroups && !reproFailures);
    } else if(!std::strcmp(mode,"bug002-late")) {
        lateArrivalArmed=true;ccReads=newEdges=0;
        inject(269,0x10);inject(266,2);pump();
        assert(newEdges==1 && ccReads==12 && !lateArrivalArmed);
        assert(testInstance->routeCount-before==2 && testInstance->routes[10].hits==0);
        assert(testInstance->isolatedGroups==2 && reproFailures==1);
    } else {
        assert(!std::strcmp(mode,"bug002-budget"));
        refillArmed=true;inject(271,4);pump();refillArmed=false;
        assert(newEdges==4 && reproRefills==4);
        assert(testInstance->routes[10].hits==4 && testInstance->isolatedGroups==2 && reproFailures==1);
    }
    // No timeouts or poisoned lanes, only one IRQ dispatch in either defect case.
    for(unsigned i=0;i<512;++i)assert(!rwLanePoison[i]);
    for(auto &route:testInstance->routes)assert(!route.writePoisoned);
    unsigned dispatches=testInstance->irqCount;
    if(std::strcmp(mode,"bug002-control"))assert(dispatches==1);
    unsigned beforeKeys=keyEvents;inject(248,1);pump();assert(keyEvents==beforeKeys+1 && testInstance->state==1);
    std::printf("%s: irq-dispatches=%u CC-acks=%u new-edges=%u failed=%u isolated=%#x keys-alive=1\n",
        mode,dispatches,testInstance->routes[10].hits,newEdges,reproFailures,testInstance->isolatedGroups);
    testDetach(1,&context);testDetach(0,&keyContext);endProvider();assertNoLeaks();
}
'''
script=script.replace('int main(int argc,char **argv) {',checks+'\nint main(int argc,char **argv) {')
script=script.replace('if (!std::strcmp(mode,"ack-batch")) batchCheck();','if (!std::strncmp(mode,"bug002-",7)) reproCheck(mode);\n    else if (!std::strcmp(mode,"ack-batch")) batchCheck();')
a=script.index("    scenarios=['ack-batch'");b=script.index('    # LSan',a)
script=script[:a]+"    scenarios=['bug002-control','bug002-late','bug002-budget']\n"+script[b:]
a=script.index('    for _ in range(20):')
script=script[:a]
script=script.replace('PASS: registry consumers %s', 'OBSERVED: BUG-002 characterization %s')
with tempfile.TemporaryDirectory(prefix='mh2lm-bug002-') as temp:
    out=Path(temp)/'instrumented-harness.py';out.write_text(script)
    subprocess.run([sys.executable,str(out),str(Path(sys.argv[1]).resolve())],check=True)
print('REPRODUCED BUG-002: late unacknowledged arrival and finite batch exhaustion isolate group 1')
