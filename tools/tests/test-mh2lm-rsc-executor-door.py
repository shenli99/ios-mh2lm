#!/usr/bin/env python3
"""C3-04 service side: SweetRPMh as a client of the published executor table.
Usage: test-mh2lm-rsc-executor-door.py EXTRACTED_XNU

The card's local criterion has two halves. The channel half is proved by
test-mh2lm-rsc-channel.py: it owns the line, publishes one table, stores.
This half proves the old entry point became a client of that table:

  * the delegated tree maps the locator and the command DB only - never the
    DRV, TCS or GIC windows - and registers no interrupt handler;
  * the command DB is mapped read-only, because this side no longer writes it;
  * the identity arrives through the owner's table, not from the DRV window;
  * a store in this mode reaches the array through the door, and if the door is
    absent the store is refused with a row and this service poisons itself
    instead of falling back to a second MMIO path;
  * the flight's TCS trajectory is identical to the module-mode flight the
    0139-0143 rings already carry: same offsets, same values, same order.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
if len(sys.argv)!=2:sys.exit(__doc__)
here=Path(__file__).resolve().parent
parent=here/'test-mh2lm-rpmh-completion-irq.py'
ns={'__file__':str(parent),'__name__':'rsc_executor_door_fixture'}
exec(compile(parent.read_text().rsplit("\ntests=r'''",1)[0],str(parent),'exec'),ns)
common=ns['common'];helpers=ns['helpers']

# The fixture drops the descriptor direction. Record it: in this mode the
# command DB must be mapped read-only, and that is the only place to see it.
_struct='struct IOMemoryDescriptor : OSObject {'
_sig='withPhysicalAddress(uintptr_t p,size_t n,unsigned){'
assert _struct in common['pool_fixture'] and common['pool_fixture'].count(_sig)==1, \
    'DB descriptor fixture moved; the direction assert below would go blind'
pool=common['pool_fixture'].replace(_struct,'static unsigned descriptorDirection=0;\n'+_struct,1)
pool=pool.replace(_sig,'withPhysicalAddress(uintptr_t p,size_t n,unsigned dir_){descriptorDirection=dir_;',1)

tests=r'''
static constexpr unsigned GW=0x17a00000;
static bool logged(const std::string &n){for(auto &m:messages)if(m.find(n)!=std::string::npos)return true;return false;}

/* ---- the fake channel: the table contract, backed by the same register file.
   It is not the channel driver - that is the other fixture's job - it is the
   exact ABI the channel publishes, so a mismatch on either side fails here. */
struct Store {uint32_t offset,value;};
static std::vector<Store> doorStores;
static unsigned doorReads,doorTriggers,doorQuiets,doorArms,doorUnmasks;
static bool doorOutOfWindow=false,doorRefused=false;
static bool (*doorCompletion)(void*)=nullptr;
static void *doorCompletionTarget=nullptr;
static uint32_t doorId=0,doorSolver=0,doorConfig=0;
static SweetAppsDoor fakeDoor;

static uint32_t fakeRead(void *,uint32_t offset){
 if(offset>=0x3000){doorRefused=true;return ~0u;}
 doorReads++;return regs[TCS+offset];
}
static void fakeWrite(void *,uint32_t offset,uint32_t value){
 if(offset>=0x3000||(offset&3)){doorOutOfWindow=true;return;}
 doorStores.push_back({offset,value});
 if(offset==8)regs[TCS+4]&=~value;else regs[TCS+offset]=value;
}
static bool fakeIdentity(void *,uint32_t *id,uint32_t *solver,uint32_t *config){
 if(!doorId)return false;
 if(id)*id=doorId;if(solver)*solver=doorSolver;if(config)*config=doorConfig;return true;
}
static IOReturn fakeTriggerReady(void *){doorTriggers++;return kIOReturnSuccess;}
static void fakeArm(void *){doorArms++;}
static void fakeUnmask(void *){doorUnmasks++;}
static void fakeQuiet(void *,uint32_t){doorQuiets++;}
static void fakeStop(void *){}
static bool fakeIrq(void *){return true;}
static bool fakeInstall(void *,void *target,bool (*callback)(void *)){
 if(doorCompletion)return doorCompletion==callback&&doorCompletionTarget==target;
 doorCompletion=callback;doorCompletionTarget=target;return true;
}
static unsigned fakeStores(void *){return (unsigned)doorStores.size();}
static unsigned fakeIrqs(void *){return 0u;}
static void installFakeDoor(){
 memset(&fakeDoor,0,sizeof(fakeDoor));
 fakeDoor.name="IOQcomRSCChannel";fakeDoor.context=&fakeDoor;
 fakeDoor.read=fakeRead;fakeDoor.write=fakeWrite;fakeDoor.identity=fakeIdentity;
 fakeDoor.triggerReady=fakeTriggerReady;fakeDoor.arm=fakeArm;fakeDoor.unmask=fakeUnmask;
 fakeDoor.quiet=fakeQuiet;fakeDoor.stop=fakeStop;fakeDoor.irq=fakeIrq;
 fakeDoor.install=fakeInstall;fakeDoor.stores=fakeStores;fakeDoor.irqs=fakeIrqs;
 assert(sweet_apps_door_publish(&fakeDoor));
}
static IOService *delegatedNub(bool delegated=true){
 IOService *n=nub();
 if(delegated){uint32_t one=1;n->properties["sweet,rpmh-executor-delegated"]=OSData::withBytes(&one,4);}
 return n;
}
static void dispose(SweetRPMh *s,IOService *n){
 destroy(s,n);dropNub(n);
 assert(!live&&!liveMaps&&subscriptions.empty());
 queued.clear();reboot();
}
static std::vector<Store> tcsStores(){
 std::vector<Store> out;
 for(auto &o:trace)if(o.write&&o.addr>=TCS&&o.addr<TCS+0x3000)
  out.push_back({(uint32_t)(o.addr-TCS),o.value});
 return out;
}
static std::string storesText(const std::vector<Store> &v){
 std::string s;
 for(auto &x:v){char b[32];snprintf(b,sizeof(b),"%03x=%08x ",x.offset,x.value);s+=b;}
 return s;
}
int main(){
 /* ================= A: the module-mode flight, recorded ================= */
 fixtures();resourceMode=2;
 auto *n=nub();auto *s=make(n);
 assert(!s->fDelegated&&s->fTablePublished&&s->fRSC&&s->fTCS);
 /* the owning side maps the command DB read-write, as it always has */
 assert(descriptorDirection==3);
 const uint32_t identId=s->fIdentityId,identSolver=s->fIdentitySolver,identConfig=s->fIdentityConfig;
 const unsigned identNCPT=s->fNCPT;
 assert(identId&&s->fMode==2);
 /* the identity the module path parsed off the DRV window is the identity the
    table must hand the delegated path; captured here, never re-derived */
 auto cid=attach(s),token=vote(s,cid,SweetRailVDDI,{true,1800,1880,62000});
 tick(s);
 assert(s->fExecuting&&s->fIRQArmed);
 regs[TCS+4]=1;regs[GW+0x304]=1u<<5;
 assert(sweet_rpmh_irq(37));regs[GW+0x304]=0;pump();
 tick(s);
 assert(status(s,cid,token).state==SweetRPMhComplete&&s->fOwned[0]);
 std::vector<Store> module=tcsStores();
 assert(module.size()>=4);                 /* trigger, clear, unmask, ack */
 assert(s->fSelfStores==module.size());    /* every one of them was ours */

 dispose(s,n);

 /* ================= B: the same flight through the owner ================= */
 fixtures();resourceMode=2;
 messages.clear();
 doorStores.clear();doorReads=doorTriggers=doorQuiets=doorArms=doorUnmasks=0;
 doorOutOfWindow=doorRefused=false;doorCompletion=nullptr;doorCompletionTarget=nullptr;
 doorId=identId;doorSolver=identSolver;doorConfig=identConfig;
 n=delegatedNub();installFakeDoor();
 assert(sweet_apps_door()==&fakeDoor);
 mapCalls.clear();unsigned mapsBefore=liveMaps;
 s=make(n);
 assert(s->fDelegated&&!s->fTablePublished&&sweet_apps_door()==&fakeDoor);
 /* the two write windows this side must never map in this mode; the read-only
    command DB it still needs, because resource lookup is resolution, not MMIO */
 assert(!s->fRSC&&!s->fTCS&&s->fDB);
 assert(mapCalls.size()==1&&mapCalls[0]==2);
 assert(liveMaps==mapsBefore+2);
 /* and it maps the command DB read-only: this side no longer writes it */
 assert(descriptorDirection==1);
 /* no interrupt handler: the owner registers the only one */
 assert(!s->fIRQMap&&!s->fIRQEvent&&!s->fIRQArmed&&!s->fIRQClaimed);
 assert(logged("owner=IOQcomRSCChannel maps=locator+db irq-handler=none stores-here=0"));
 assert(logged("executor=delegated"));
 /* nothing was read or written through the owner at start: the identity and
    the array are adopted on the first request, not at start */
 assert(doorReads==0&&doorStores.empty()&&doorTriggers==0);
 assert(!writes());                        /* start left the array alone */
 cid=attach(s);token=vote(s,cid,SweetRailVDDI,{true,1800,1880,62000});
 /* the identity arrived through the table and produced the same mode */
 assert(s->fIdentityReady&&s->fIdentityId==identId&&s->fIdentityConfig==identConfig);
 assert(s->fNCPT==identNCPT&&s->fMode==2);
 assert(logged("identity adopted from hardware owner"));
 assert(s->fInstalled);
 assert(logged("phase=delegate result=0 owner=IOQcomRSCChannel stores-here=0 irq-handlers-here=0"));
 tick(s);
 assert(s->fExecuting&&doorTriggers==1&&doorArms==1&&doorUnmasks==1);
 /* every store in this mode went through the table; this side counted none */
 assert(s->fSelfStores==0);
 /* the array is reached only through the owner's read entry, never a map */
 assert(doorReads>0);
 regs[TCS+4]=1;
 assert(doorCompletion);                   /* the service bound its entry */
 assert(doorCompletion(doorCompletionTarget));
 tick(s);
 assert(status(s,cid,token).state==SweetRPMhComplete&&s->fOwned[0]);
 /* the decisive row: the same words in the same order as the module path */
 assert(storesText(doorStores)==storesText(module));
 /* and nothing reached outside the window the owner described */
 assert(!doorOutOfWindow&&!doorRefused);

 /* ---- the door is the only write path, so its absence refuses the request ---- */
 /* The owner leaves while this service still believes it has one. A delegated
    service cannot even read the array now, so the refusal belongs at the entry:
    no token, no flight, no store, and a row that says which owner is missing.
    (The mid-flight withdrawal is the same refusal one layer in, in tcsRead and
    tcsWrite; a host fixture cannot open that race deterministically.) */
 size_t storesBefore=doorStores.size();
 sweet_apps_door_withdraw(&fakeDoor);
 assert(!sweet_apps_door());
 auto before=trace.size();
 uint64_t none=0;
 assert(s->requestVote(cid,SweetRailVDDI,{true,1856,1880,62000},&none)!=kIOReturnSuccess);
 assert(none==0);
 tick(s);
 assert(logged("no executor door; request refused, no writable path here"));
 assert(doorStores.size()==storesBefore);
 for(size_t i=before;i<trace.size();++i)
  assert(!trace[i].write||trace[i].addr<TCS||trace[i].addr>=TCS+0x3000);
 assert(!s->fBusy&&!s->fPoisoned&&s->fSelfStores==0);
 /* and stop withdraws nothing, because this side never published a table */
 s->stop(n);assert(!s->fTablePublished);
 s->release();dropNub(n);
 assert(!live&&!liveMaps);queued.clear();reboot();
 doorCompletion=nullptr;
 puts("PASS: delegated trajectory equals the module-mode trajectory; locator+DB only, read-only DB, adopted identity through the owner, zero self stores, withdrawn door refuses the request with no store and no map");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-rsc-door-') as temp:
    cpp=Path(temp)/'test.cpp';binary=Path(temp)/'test'
    # ---- pristine compile -------------------------------------------------
    # The functional cases below compile copies whose `class` was rewritten to
    # `struct` so the driver can look at members; that rewrite deletes access
    # control, which is how the 0145 CI build was the first compiler to ever see
    # a protected member reached from a file-scope helper. This pass keeps the
    # shipped text and only fakes IOKit. SweetRPMh.cpp is the unit C3-04 changed
    # most, so it is the one that has to build with its own access specifiers.
    arm=Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
    def tu(name):
        # A missing final newline would glue one header's #endif to the next
        # header's #if, which is a preprocessor error and not a driver defect.
        return re.sub(r'^#include.*\n','',(arm/name).read_text(),flags=re.M)+'\n'
    pristine=Path(temp)/'pristine.cpp'
    pristine.write_text(ns['prelude']+common['DSI_MEMORY_FIXTURE']+pool+
                        tu('SweetAppsDoor.h')+tu('SweetAppsLine.h')+
                        tu('SweetAppsQueue.h')+
                        tu('SweetRPMhExecutor.h')+tu('SweetRPMh.h')+
                        tu('SweetPanelPins.h')+tu('SweetTouchPanel.h')+
                        tu('SweetAppsDoor.cpp')+tu('SweetAppsLine.cpp')+
                        tu('SweetAppsQueue.cpp')+
                        tu('SweetRPMhExecutor.cpp')+tu('SweetRPMh.cpp'))
    r=subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-fsyntax-only',
                      str(pristine)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if r.returncode:
        print(r.stdout[:4000])
        raise SystemExit('pristine compile failed: SweetRPMh.cpp no longer builds '
                         'with the access specifiers it ships with')
    control=Path(temp)/'control.cpp'
    control.write_text(pristine.read_text()+
                       '\nuint32_t leaked(SweetRPMh *s){return s->fRSC;}\n')
    r=subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-fsyntax-only',
                      str(control)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert r.returncode and 'error:' in r.stdout and 'member of' in r.stdout, \
        'the pristine gate no longer sees access control'
    cpp.write_text(ns['prelude']+common['DSI_MEMORY_FIXTURE']+pool+common['header']+common['source']+helpers+tests)
    if os.environ.get('MH2LM_KEEP_CPP'):
        Path('/tmp/mh2lm-rsc-door-kept.cpp').write_text(cpp.read_text())
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(binary)],check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(binary)],check=True,env=env,timeout=45)