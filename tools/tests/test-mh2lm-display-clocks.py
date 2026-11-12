#!/usr/bin/env python3
"""Actual MDSS warm-clock code; OS/MMIO scheduling are independent fixtures.
Usage: test-mh2lm-display-clocks.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
existing = here/'test-mh2lm-display-service.py'
ns = {'__file__': str(existing), '__name__': 'clock_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)
prelude = ns['prelude'].replace('static int enabled=1,', 'static int clockMode=1;\nstatic int enabled=1,')
prelude = prelude.replace(' if(!strcmp(k,"sweetdisplayresources"))',
    ' if(!strcmp(k,"sweetdisplayclocks")){*(int*)v=clockMode;return true;}\n if(!strcmp(k,"sweetdisplayresources"))')
# CBCR_OFF is read-only. A write is NOT a fabricated clock-on completion:
# only the test's explicit physical transition below changes its status.
prelude = prelude.replace('static void writeReg(uintptr_t b,uint32_t o,uint32_t v){',
    'static uintptr_t dropClockWrite=0;\nstatic void writeReg(uintptr_t b,uint32_t o,uint32_t v){')
prelude = prelude.replace(' regs[b+o]=v;if(b==bases[6]&&o==0x90)', '''
 if(b==bases[1]&&(o==0x2080||o==0x200c||o==0x2024||o==0x2038||o==0x2028||o==0x202c||o==0x2004)){
  if(b+o!=dropClockWrite)regs[b+o]=(v&~0x80000000u)|(regs[b+o]&0x80000000u);return;
 }
 regs[b+o]=v;if(b==bases[6]&&o==0x90)''')
tests = r'''
static unsigned writes(){unsigned n=0;for(auto op:trace)if(op.write)++n;return n;}
static IOService*nub(){auto*n=new IOService;uint32_t one=1;
 n->properties["sweet,display-resource-abi"]=OSData::withBytes(&one,4);return n;}
static void fixtures(){
 resetRegs();resourceMode=2;clockMode=1;dropClockWrite=0;fakeTicks=1000000;
 regs[bases[0]+0x38]=1;regs[bases[1]+0x3000]=0x80000000;
 const unsigned branches[]={0x2080,0x200c,0x2024,0x2038,0x2028,0x202c,0x2004};
 for(auto off:branches)regs[bases[1]+off]=1;
 regs[bases[1]+0x22c0]=0x401; // inherited PLL1 AHB
 regs[bases[1]+0x20cc]=0x401; // inherited PLL1 MDP
 regs[bases[1]+0x20fc]=1;regs[bases[1]+0x214c]=1; // exact XO roots
 regs[bases[1]+0x2114]=0x101;regs[bases[1]+0x209c]=0x2101;
 regs[bases[1]+0x20a0]=1;regs[bases[1]+0x20a4]=0xfe;regs[bases[1]+0x20a8]=0xfd;
 regs[bases[4]+0x288]=0;regs[bases[4]+0x2b4]=0;regs[bases[6]+8]=0;
}
static void tick(SweetMDSS*s,unsigned us=100){
 fakeTicks+=uint64_t(us)*1000;std::lock_guard<std::recursive_mutex> l(s->fLoop->gate);
 SweetMDSS::clockTimerAction(s,s->fClockTimer);
}
static void complete(SweetMDSS*s){for(unsigned n=0;n<32&&s->clockBusy();++n)tick(s);assert(!s->clockBusy());}
static SweetClockLease status(SweetMDSS*s,uint64_t token){
 SweetClockLease q{};auto before=trace.size();assert(s->localClockStatus(token,&q)==0);
 assert(trace.size()==before);return q;
}
static SweetMDSS*make(IOService*n){auto*s=new SweetMDSS();assert(s->start(n));
 assert(status(s,s->fClockBootToken).state==SweetClockPending);complete(s);
 assert(status(s,s->fClockBootToken).state==SweetClockHeld&&s->localClocksReady());return s;}
static void destroy(SweetMDSS*s,IOService*n,bool touched=false){
 trace.clear();s->stop(n);auto count=trace.size();tick(s);assert(trace.size()==count);
 for(auto op:trace)assert(!op.write); // stop/free must never power down
 if(s->fClockBootToken){SweetClockLease boot{};auto rc=s->localClockStatus(s->fClockBootToken,&boot);
  assert(rc==kIOReturnBadArgument || (rc==0&&(boot.state==SweetClockStopped||boot.state==SweetClockReleased)));}
 s->release();queued.clear();assert(mdssClockWriter==(touched?2u:0u));
 mdssClockWriter=0; // simulated reboot, only after all objects destroyed
 assert(!liveMaps&&!rawBlocks);
}
static void releaseBoot(SweetMDSS*s){assert(s->releaseLocalClocks(s->fClockBootToken)==0);complete(s);
 assert(status(s,s->fClockBootToken).state==SweetClockReleased);}
static uint64_t acquireCore(SweetMDSS*s){uint64_t t=0;assert(s->retainLocalClocks(SweetClockCore,&t)==0&&t);return t;}
int main(){auto*n=nub();fixtures();auto*s=make(n);assert(!writes());
 // Real default path and metadata-only mode never create the controller.
 destroy(s,n);
 for(unsigned mode=0;mode<2;++mode){fixtures();clockMode=0;resourceMode=mode;
  s=new SweetMDSS();assert(s->start(n)&&!s->fClockTimer&&!s->fClockClaimed&&!writes());destroy(s,n);}
 // Warm mode rejects missing writable ABI, invalid modes, hardware control,
 // TZ-only vote, dead domain/bus/interface, updating or unknown roots.
 for(unsigned test=0;test<19;++test){fixtures();messages.clear();
  if(test==0)resourceMode=1;
  if(test==1)clockMode=2;
  if(test==2)regs[bases[1]+0x3000]|=2;
  if(test==3)regs[bases[1]+0x3000]|=1;
  if(test==4)regs[bases[1]+0x3000]|=4;
  if(test==5)regs[bases[1]+0x3000]=0;
  if(test==6)regs[bases[0]+0x38]=0x80000001;
  if(test==7)regs[bases[0]+0xc]=0;
  if(test==8)regs[bases[1]+0x2080]=0x80000001;
  if(test==9)regs[bases[1]+0x2110]=1;
  if(test==10)regs[bases[1]+0x2110]=0x80000000;
  if(test==11)regs[bases[1]+0x2114]=0x201; // DSI1 parent refused
  if(test==12)regs[bases[1]+0x214c]=2; // HID=2 is a real divide-by-1.5, not XO bypass
  if(test==13)regs[bases[1]+0x2028]=0x80000000; // no PHY-parent acquisition yet
  if(test==14)regs[bases[1]+0x200c]=0x80000000; // cannot enable inherited PLL root
  if(test==15)regs[bases[1]+0x2148]=0x10; // dirty source
  if(test==16)regs[bases[1]+0x2024]=0x80000001; // branch SW vote but OFF
  if(test==17)regs[bases[1]+0x2024]=0; // off without a known halted state
  if(test==18){regs[bases[1]+0x2024]=0x80000000;regs[bases[6]+8]=2;} // unsafe to enable XO
  s=new SweetMDSS();assert(!s->start(n));auto before=writes();assert(!before);
  if(test>=2){
   const char*gate[]={"gdsc","gdsc","gdsc","gdsc","gcc-hf","gcc-ahb","dispcc-ahb",
    "root-state","root-state","root-source","xo-config","branch-unowned","branch-unowned",
    "root-state","branch-off","branch-unowned","branch-busy"};
   unsigned faults=0;std::string expected=std::string("admit gate=")+gate[test-2];
   for(const auto&m:messages)if(m.find("admit gate=")!=std::string::npos){++faults;assert(m.find(expected)!=std::string::npos);}
   assert(faults==1); // first failed gate must be diagnosed without speculative reads
   for(auto op:trace){
    if(test==7)assert(op.addr!=bases[1]+0x2080); // no DISPCC without GCC vote
    if(test==7||test==8)assert(op.addr!=bases[0]+0x38);
    if(test==6||test==7||test==8)assert(op.addr!=bases[1]+0x3000);
    if(test<=8)assert(op.addr!=bases[1]+0x22bc); // no RCG before prerequisites
    if(test==9||test==10)assert(op.addr!=bases[1]+0x2114); // bad CMD: do not read CFG
    if(test==15)assert(op.addr!=bases[1]+0x214c);
   }
  }
  s->release();queued.clear();assert(!liveMaps&&!rawBlocks&&!mdssClockWriter);
 }
 // Actual 0121a15 prefix through ESC0, with unobserved BYTE/PCLK supplied by
 // the fixture. HID=0 is bypass, not a stopped/unknown XO source. No repair write.
 fixtures();regs[bases[0]+0xc]=0x80000003;regs[bases[0]+0x38]=0x4221;
 regs[bases[1]+0x3000]=0xf822f000;regs[bases[1]+0x200c]=0x221;
 regs[bases[1]+0x22c0]=1;regs[bases[1]+0x20cc]=0x100107;regs[bases[1]+0x214c]=0;
 s=make(n);assert(s->fClockParents[3][0]==0&&s->localClocksReady()&&!writes());
 // Equivalent frequency must not weaken the raw-parent drift guard.
 regs[bases[1]+0x214c]=1;assert(!s->localClocksReady());uint64_t driftToken=999;
 assert(s->retainLocalClocks(SweetClockDSI0,&driftToken)==kIOReturnNotReady&&!driftToken&&!writes());
 destroy(s,n);
 // All combinations of bypass/div1 on AHB/MDP/VSYNC/ESC0 are warm-retainable.
 for(unsigned config=0;config<16;++config){fixtures();
  const uint32_t cfg[]={0x22c0,0x20cc,0x20fc,0x214c};
  for(unsigned i=0;i<4;++i)regs[bases[1]+cfg[i]]=(config>>i)&1u;
  s=make(n);assert(!writes());destroy(s,n);
 }
 // Independent rate oracle from LG clk_rcg2_calc_rate(): HID=0 bypasses;
 // nonzero HID scales parent by 2/(HID+1). Fractional modes stay disallowed.
 fixtures();s=make(n);
 for(unsigned i=0;i<SweetClockCount;++i){const auto&branch=mdssClockBranches[i];
  for(unsigned hid=0;hid<32;++hid)for(unsigned mode=0;mode<4;++mode){
   regs[bases[1]+branch.root+4]=(mode<<12)|hid;uint32_t parent[6]={};
   const uint64_t xo=19200000,rate=hid ? (xo*2)/(hid+1) : xo;
   bool expected=(branch.sources&1u)&&mode==0&&rate==xo;
   assert(s->clockParent(i,parent,true)==expected);
  }
 }
 assert(!writes());destroy(s,n);
 // An owned bypass-configured ESC0 still needs real branch/root ON/OFF:
 // a posted software vote alone never finishes the lease.
 fixtures();regs[bases[1]+0x214c]=0;s=make(n);releaseBoot(s);
 regs[bases[1]+0x2038]=0x80000000;regs[bases[1]+0x2148]=0x80000000;
 uint64_t bypass=0;assert(s->retainLocalClocks(SweetClockDSI0,&bypass)==0);
 for(unsigned j=0;j<SweetClockCount&&!s->fClockWaiting;++j)tick(s);
 assert(s->fClockWaiting&&writes()==1);tick(s);assert(status(s,bypass).state==SweetClockPending);
 regs[bases[1]+0x2038]&=~0x80000000u;regs[bases[1]+0x2148]=0;complete(s);
 assert(status(s,bypass).state==SweetClockHeld&&s->fClockOwnedMask==8&&regs[bases[1]+0x214c]==0);
 assert(s->releaseLocalClocks(bypass)==0);
 for(unsigned j=0;j<SweetClockCount&&!s->fClockWaiting;++j)tick(s);
 assert(s->fClockWaiting&&writes()==2);tick(s);assert(status(s,bypass).state==SweetClockReleasing);
 regs[bases[1]+0x2038]|=0x80000000u;regs[bases[1]+0x2148]=0x80000000;complete(s);
 assert(status(s,bypass).state==SweetClockReleased&&!s->fClockOwnedMask&&regs[bases[1]+0x214c]==0);
 destroy(s,n,true);
 // Duplicate writers and timer-allocation/add failures do not steal ownership.
 fixtures();s=make(n);auto*other=new SweetMDSS();assert(!other->start(n));other->release();
 assert(mdssClockWriter==1);destroy(s,n);
 for(unsigned test=0;test<2;++test){fixtures();
  if(test==0)failTimerCreate=timerCreates+2; // probe, then clock timer
  else failEventAdd=eventAdds+4; // gate,probe,RPMh IES,clock timer
  s=new SweetMDSS();assert(!s->start(n));s->release();queued.clear();
  failTimerCreate=failEventAdd=0;assert(!mdssClockWriter&&!liveMaps&&!rawBlocks&&!writes());
 }
 // Four bounded leases, overlapping references, stale tokens, no MMIO query.
 fixtures();s=make(n);uint64_t old=s->fClockBootToken;
 uint64_t a=acquireCore(s),denied=999;
 assert(s->retainLocalClocks(SweetClockCore,&denied)==kIOReturnBusy&&!denied);complete(s);
 uint64_t b=acquireCore(s);complete(s);uint64_t c=acquireCore(s);complete(s);
 assert(s->retainLocalClocks(SweetClockCore,&denied)==kIOReturnNoResources&&!denied);
 for(unsigned i=0;i<3;++i)assert(s->fClockReferences[i]==4);
 assert(s->releaseLocalClocks(a)==0);complete(s);
 uint64_t fresh=acquireCore(s);complete(s);assert(fresh!=a);
 SweetClockLease q{};assert(s->localClockStatus(a,&q)==kIOReturnBadArgument);
 assert(s->releaseLocalClocks(old)==0);complete(s);assert(!writes());destroy(s,n);
 // Even a purely inherited boot lease cannot be revoked while transfers
 // depend on its admission proof; Busy must leave it Held and unchanged.
 fixtures();s=make(n);regs[bases[6]+8]=2;auto busyWrites=writes();
 assert(s->releaseLocalClocks(s->fClockBootToken)==kIOReturnBusy);
 assert(status(s,s->fClockBootToken).state==SweetClockHeld&&writes()==busyWrites);
 regs[bases[6]+8]=0;destroy(s,n);
 // An inherited gate is never switched off, even after the last release.
 fixtures();s=make(n);releaseBoot(s);assert(!writes()&&!s->fClockOwnedMask);destroy(s,n);
 // Own XO branch: explicit physical ON/OFF, shared ref prevents early disable.
 fixtures();s=make(n);releaseBoot(s);regs[bases[1]+0x2024]=0x80000000;regs[bases[1]+0x20f8]=0x80000000;
 a=acquireCore(s);tick(s);tick(s);tick(s);
 assert(s->fClockWaiting&&writes()==1&&status(s,a).state==SweetClockPending);
 tick(s);assert(status(s,a).state==SweetClockPending); // enable readback isn't completion
 regs[bases[1]+0x2024]&=~0x80000000u;regs[bases[1]+0x20f8]=0;complete(s);assert(status(s,a).state==SweetClockHeld);
 assert(s->fClockOwnedMask==4&&s->fClockReferences[2]==1);
 b=acquireCore(s);complete(s);auto count=writes();
 assert(s->releaseLocalClocks(a)==0);complete(s);assert(writes()==count&&s->fClockReferences[2]==1);
 regs[bases[4]+0x2b4]=1;assert(s->releaseLocalClocks(b)==kIOReturnBusy&&writes()==count);
 regs[bases[4]+0x2b4]=0;regs[bases[6]+8]=2;assert(s->releaseLocalClocks(b)==kIOReturnBusy);
 regs[bases[6]+8]=0;
 assert(s->releaseLocalClocks(b)==0);
 for(unsigned j=0;j<SweetClockCount&&!s->fClockWaiting;++j)tick(s);
 assert(s->fClockWaiting&&writes()==count+1);
 tick(s);assert(status(s,b).state==SweetClockReleasing);
 regs[bases[1]+0x2024]|=0x80000000u;regs[bases[1]+0x20f8]=0x80000000;complete(s);
 assert(status(s,b).state==SweetClockReleased&&!s->fClockOwnedMask&&!s->fClockReferences[2]);
 destroy(s,n,true);
 // Hardware quiescence changes AFTER admission: no later branch write allowed.
 fixtures();s=make(n);releaseBoot(s);regs[bases[1]+0x2024]=0x80000000;
 a=acquireCore(s);regs[bases[4]+0x288]=1u<<20;complete(s);
 assert(status(s,a).state==SweetClockFailed&&!writes());destroy(s,n);
 // Independent loss/drop/timeout/source drift; preserve changed gates, refuse
 // new writes, no rollback based on a consumer-supplied 'idle' flag.
 for(unsigned test=0;test<6;++test){fixtures();s=make(n);releaseBoot(s);
  regs[bases[1]+0x2024]=0x80000000;a=acquireCore(s);
  if(test==0)dropClockWrite=bases[1]+0x2024;
  tick(s);tick(s);tick(s);assert(s->fClockTouched&&s->fClockWaiting);
  if(test==1)regs[bases[1]+0x20fc]=3;
  if(test==2)regs[bases[1]+0x3000]=0;
  if(test==3)regs[bases[1]+0x2080]=0x80000001;
  if(test==4)fakeTicks+=300000000;
  if(test==5){regs[bases[1]+0x2024]&=~0x80000000u;tick(s);
   // completed VSYNC isn't enough: final validation must catch earlier MDP loss.
   regs[bases[1]+0x200c]=0x80000001;}
  if(test==0)fakeTicks+=11000000;
  complete(s);q=status(s,a);
  assert(q.state==SweetClockFailed&&q.uncertain&&s->fClockFault);
  count=writes();assert(s->retainLocalClocks(SweetClockCore,&denied)==kIOReturnNotReady);
  assert(s->releaseLocalClocks(a)==kIOReturnNotReady&&writes()==count);
  destroy(s,n,true);
 }
 // Stop waiting for ON, late timer, destroyed writer's ledger cannot be reused.
 fixtures();s=make(n);releaseBoot(s);regs[bases[1]+0x2024]=0x80000000;a=acquireCore(s);
 tick(s);tick(s);tick(s);auto*timer=s->fClockTimer;timer->retain();
 s->stop(n);assert(status(s,a).state==SweetClockStopped);trace.clear();timer->action(s,timer);assert(trace.empty());
 s->release();queued.clear();assert(!timer->loop&&!timer->timeout&&mdssClockWriter==2);timer->release();
 s=new SweetMDSS();assert(!s->start(n));s->release();queued.clear();mdssClockWriter=0;
 // Fresh/no-write service can rematch; IRQ mode and resources stay independent.
 fixtures();s=make(n);trace.clear();assert(s->localClocksReady());
 for(auto op:trace)assert(!op.write&&!(op.addr>=bases[12]&&op.addr<bases[12]+0x760));
 assert(s->brightnessReady()==kIOReturnSuccess);
 messages.clear();regs[bases[1]+0x209c]^=1;assert(!s->localClocksReady());
 trace.clear();assert(s->brightnessReady()==kIOReturnNotReady);
 uint32_t word=0x80050029;assert(s->dcsWrite(&word,1)==kIOReturnNotReady);
 s->fPanelPowerEnabled=true;assert(s->setPanelPower(false)==kIOReturnNotReady);
 s->fDsiEnabled=true;uint64_t rejected=0;
 const uint8_t command[]={5,1,0,0,0,0,1,0x29};
 assert(s->beginDSISequence(command,sizeof(command),&rejected)==kIOReturnNotReady&&!rejected);
 assert(!writes());
 for(const auto&m:messages)assert(m.find("admit ")==std::string::npos); // runtime checks never repeat boot diagnostics
 destroy(s,n);
 for(auto&kv:n->properties)kv.second->release();n->properties.clear();n->release();
 assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 puts("PASS: actual warm clock leases; XO bypass/div1 warm admission/16 combinations/1280 rate-oracle cases/raw drift/independent bypass ON-OFF, defaults/no writes,19 admission faults/exact failure gate/short-circuit read safety/quiet runtime, bounded tokens/shared refs, independent ON/OFF, busy release, parent drift/deadline/posted-write faults, stop/late event/retirement, no PHY/domain writes and zero live objects");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-display-clocks-') as temp:
    cpp = Path(temp)/'test.cpp'
    binary = Path(temp)/'test'
    cpp.write_text(prelude+ns['DSI_MEMORY_FIXTURE']+ns['pool_fixture']+ns['header']+ns['source']+tests)
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
