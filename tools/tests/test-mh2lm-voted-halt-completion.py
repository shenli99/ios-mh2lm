#!/usr/bin/env python3
"""Real voted acquisition/consumers/references with a26 enable0/OFF0 readback.
Usage: test-mh2lm-voted-halt-completion.py EXTRACTED_XNU [A26_BASELINE_XNU]
MMIO status is simulated; this does not prove why a real write reads zero.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
fixture = Path(__file__).with_name('test-mh2lm-rsc-clock-votes.py')
checks = r'''
static unsigned branch7Writes(){unsigned n=0;for(auto&o:trace)n+=o.write&&o.addr==bases[1]+0x4004;return n;}
static void readZero(Harness&h,bool inherited=false){
 messages.clear();offLeaves();regs[bases[1]+0x4004]=0;
 if(inherited)regs[bases[1]+0x400c]=regs[bases[1]+0x4008]=1;
 configReady(h);dropClockWrite=bases[1]+0x4004;
 // The fixture suppresses the bit0 latch, while still tracing the request.
 // Linux halt semantics cannot distinguish this from a readable-zero vote.
}
static void dataReady(Harness&h){
 for(unsigned j=0;j<4000&&h.m->fNoCPhase!=NData&&h.m->fManagedState!=SweetMDSS::ManagedFault;++j)stepRSC(h);
 if(h.m->fNoCPhase!=NData)for(auto&s:messages)fputs(s.c_str(),stderr);
 assert(h.m->fNoCPhase==NData);
 auto s=clockStatus(h,h.m->fNoCClockToken);
 assert(s.state==SweetClockHeld&&!s.uncertain&&s.result==0&&s.mask==0x381);
 assert(regs[bases[1]+0x4004]==0&&branch7Writes()==1);
}
int main(){unsigned cases=0;
 // Exact a26 sibling state and the independently disabled-sibling case;
 // OFF already clear or clearing after one waiting sample.
 for(unsigned inherited=0;inherited<2;++inherited)for(unsigned delayed=0;delayed<2;++delayed){
  Harness h;readZero(h,inherited);assert(!branch7Writes());noPayload();
#if EXPECT_HALT_COMPLETION
  assert(!h.m->clockBranchRunning(7,0)&&!h.m->fClockIssuedMask);
  h.m->fClockOwnedMask|=0x80;assert(!h.m->clockBranchRunning(7,0));h.m->fClockOwnedMask&=~0x80u;
#endif
  untilWrite(h,7);auto token=h.m->fNoCClockToken;
  assert(clockStatus(h,token).state==SweetClockPending&&branch7Writes()==1);noPayload();
  if(delayed){regs[bases[1]+0x4004]=0x80000000u;clockTick(h,100,false,false,false);
   assert(h.m->fClockWaiting&&clockStatus(h,token).state==SweetClockPending);regs[bases[1]+0x4004]=0;}
#if EXPECT_HALT_COMPLETION
  assert(h.m->fClockIssuedMask==0x80);
  dataReady(h);assert(h.m->fClockOwnedMask==(inherited?0x80u:0x380u));
  assert(h.m->noCClocksReady());h.finish();
  assert(h.m->fScanoutState==SweetScanoutActive&&sourceWritten());
  unsigned messagesSeen=0;for(auto&s:messages)messagesSeen+=s.find("voted halt complete branch=7 cbcr=0 request-issued=1")!=std::string::npos;
  assert(messagesSeen==1&&branch7Writes()==1);
#else
  for(unsigned j=0;j<150&&h.m->clockBusy();++j)clockTick(h,100,false);
  assert(clockStatus(h,token).result==int(kIOReturnTimeout));noPayload();
#endif
  dropClockWrite=0;++cases;
 }
#if EXPECT_HALT_COMPLETION
 // OFF remains a hard gate, including the FSM=2/OFF=1 combination.
 for(unsigned fsm=0;fsm<2;++fsm){Harness h;readZero(h);untilWrite(h,7);
  regs[bases[1]+0x4004]=fsm?0xa0000000u:0x80000000u;
  fakeTicks=h.m->fClockWaitDeadline;clockTick(h,0,false,false,false);
  assert(clockStatus(h,h.m->fNoCClockToken).result==int(kIOReturnTimeout));
  assert(h.m->fClockFault&&h.m->fClockIssuedMask==0x80);noPayload();dropClockWrite=0;++cases;
 }
 // Earlier branch may stop while later siblings complete: final check fails.
 {Harness h;readZero(h);untilWrite(h,9,true);externalClocks();
  regs[bases[1]+0x4004]=0x80000000u;clockTick(h);
  assert(clockStatus(h,h.m->fNoCClockToken).state==SweetClockFailed);noPayload();dropClockWrite=0;++cases;}
 // Held is needed for RSC access; parent/config/domain and upstream status
 // remain prerequisites even with an issued software request.
 for(unsigned bad=0;bad<8;++bad){Harness h;readZero(h);dataReady(h);trace.clear();
  if(bad==0)regs[bases[1]+0x4004]=0x80000000u;
  if(bad==1)regs[bases[1]+0x22bc]=0x80000000u;
  if(bad==2)regs[bases[1]+0x22bc]|=1u;
  if(bad==3)h.r->fClients[0].registerStatus.uncertain=true;
  if(bad==4)h.r->fClients[0].displayStatus.enabled=false;
  if(bad==5)h.m->fClockIssuedMask=0;
  if(bad==6)h.m->fClockOwnedMask&=~0x80u;
  if(bad==7)h.m->fClockLeases[h.m->fNoCClockToken-1].state=SweetClockPending;
  assert(!h.m->noCAdmission());for(auto&o:trace)assert(o.addr<DR||o.addr>=DR+0x1c00);
  noPayload();dropClockWrite=0;++cases;
 }
 // Inherited enable disappearing is not our own issued vote.
 {Harness h;offLeaves();regs[bases[1]+0x4004]=1;configReady(h);
  for(unsigned j=0;j<4000&&h.m->fNoCPhase!=NData;++j)stepRSC(h);
  assert(h.m->fNoCPhase==NData&&!(h.m->fClockIssuedMask&0x80));
  regs[bases[1]+0x4004]=0;assert(!h.m->noCClocksReady());++cases;}
 // Shared retain/release has no duplicate request; last-user withdrawal
 // still writes zero and waits for readback/settle. Reacquisition writes anew.
 {Harness h;readZero(h);dataReady(h);auto token=h.m->fNoCClockToken;h.m->fNoCPhase=NIdle;
  uint64_t extra=0;assert(h.m->retainLocalClocks(SweetClockRSC,&extra)==0);
  while(h.m->clockBusy())clockTick(h);assert(clockStatus(h,extra).state==SweetClockHeld&&branch7Writes()==1);
  assert(h.m->fClockReferences[7]==2&&h.m->releaseLocalClocks(extra)==0);
  while(h.m->clockBusy())clockTick(h);assert(h.m->fClockReferences[7]==1&&branch7Writes()==1&&h.m->fClockIssuedMask==0x380);
  h.m->fNoCPhase=NData;h.m->fNoCTarget.registerPeak=0;h.m->fNoCCursor=h.m->fNoCCount;
  assert(h.m->releaseLocalClocks(token)==0);
  while(!(h.m->fClockCursor==7&&h.m->fClockWaiting))clockTick(h);
  assert(branch7Writes()==2&&h.m->fClockIssuedMask==0x80);
  clockTick(h,0);auto settled=h.m->fClockVoteSettleAt;assert(settled>fakeTicks);
  clockTick(h,9);assert(h.m->fClockWaiting);clockTick(h,1);assert(!h.m->fClockWaiting&&!h.m->fClockIssuedMask);
  while(h.m->clockBusy())clockTick(h);assert(clockStatus(h,token).state==SweetClockReleased);
  h.m->fNoCPhase=NIdle;uint64_t next=0;assert(h.m->retainLocalClocks(SweetClockRSC,&next)==0);
  while(h.m->clockBusy())clockTick(h);assert(clockStatus(h,next).state==SweetClockHeld&&branch7Writes()==3);
  dropClockWrite=0;++cases;
 }
 // Stop cannot consume a pending request or access MMIO on a late callback.
 {Harness h;readZero(h);untilWrite(h,7);auto token=h.m->fNoCClockToken;h.m->stop(h.d);auto n=trace.size();
  SweetMDSS::clockTimerAction(h.m,h.m->fClockTimer);SweetMDSS::noCTimerAction(h.m,h.m->fNoCTimer);
  assert(trace.size()==n&&clockStatus(h,token).state==SweetClockStopped);dropClockWrite=0;++cases;}
#endif
 printf("PASS: %u voted halt cases; policy=%u; a26 zero-readback %s\n",cases,
        EXPECT_HALT_COMPLETION,EXPECT_HALT_COMPLETION?"reaches managed scanout":"reproduces Timeout");
}
'''


def run(tree, updated):
    sys.argv[:] = [sys.argv[0], str(tree)]
    ns = {'__file__': str(fixture), '__name__': 'voted_halt_fixture'}
    exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
                 str(fixture), 'exec'), ns)
    core, actual = ns['ns'], ns['actual']
    source = (core['prelude'] + actual['DSI_MEMORY_FIXTURE'] + actual['pool_fixture'] +
              actual['header'] + actual['source'] + core['helpers'] +
              ns['tests'].split('int main(){')[0] + checks)
    with tempfile.TemporaryDirectory(prefix='mh2lm-voted-halt-') as temporary:
        cpp, binary = Path(temporary)/'test.cpp', Path(temporary)/'test'
        cpp.write_text(source)
        subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        f'-DEXPECT_HALT_COMPLETION={int(updated)}', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=60, env=dict(
            os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))


target = Path(sys.argv[1])
baseline = Path(sys.argv[2]) if len(sys.argv) == 3 else None
if baseline:
    run(baseline, False)
run(target, True)
