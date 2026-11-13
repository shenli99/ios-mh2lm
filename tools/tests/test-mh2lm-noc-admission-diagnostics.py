#!/usr/bin/env python3
"""Run actual NoC/clock diagnostics; optional baseline proves identical MMIO traces.
Usage: test-mh2lm-noc-admission-diagnostics.py EXTRACTED_XNU [BASELINE_XNU]
"""
from pathlib import Path
import os,subprocess,sys,tempfile
if len(sys.argv) not in (2,3):sys.exit(__doc__)
here=Path(__file__).resolve().parent
fixture=here/'test-mh2lm-display-noc.py'
checks=r'''
static bool has(const char*needle){for(auto&s:messages)if(s.find(needle)!=std::string::npos)return true;return false;}
static void report(unsigned id,bool result){
 printf("CASE %u result=%u\n",id,unsigned(result));
 for(auto&o:trace)printf("%u %llx %x\n",unsigned(o.write),(unsigned long long)o.addr,o.value);
}
int main(){
 const char*gates[]={"", "clock-service","clock-service","clock-service","clock-service",
  "link-state","core-state","core-ready","gcc-ahb","dispcc-ahb","gcc-hf","gdsc",
  "clock-lease","clock-lease","clock-lease","root-state","root-source","clock-parent-drift",
  "clock-branch","rsc-layout","rsc-layout","rsc-layout","rsc-layout","wrapper-override",
  "wrapper-override","wrapper-power","wrapper-control","sequencer-busy","error-irq-status"};
 for(unsigned fault=0;fault<29;++fault){
  nocFixtures();auto*n=nub();auto*r=make(n);auto*d=displayNub();auto*m=displayMake(d,r);
  auto token=retain(m);drive(m,r);auto clockToken=m->fNoCClockToken;
  unsigned slot=4;for(unsigned i=0;i<4;++i)if(m->fClockLeases[i].token==clockToken)slot=i;assert(slot<4);
  if(fault==1)m->fAccepting=false;if(fault==2)m->inactive=true;
  if(fault==3)m->fClockFault=true;if(fault==4)m->fNoCClockToken=0;
  if(fault==5)m->fLinkPhase=SweetMDSS::LinkFault;if(fault==6)m->fCorePhase=CoreConfigVote;
  if(fault==7)m->fCorePhase=CoreReady;
  if(fault==8)regs[bases[0]+0xc]=0;
  if(fault==9)regs[bases[1]+0x2080]=0x80000001;
  if(fault==10)regs[bases[0]+0x38]=0;if(fault==11)regs[bases[1]+0x3000]=0;
  if(fault==12)m->fClockLeases[slot].mask=1;
  if(fault==13)m->fClockLeases[slot].state=SweetClockFailed;
  if(fault==14)m->fNoCClockToken=~0ULL;
  if(fault==15)regs[bases[1]+0x22bc]=1;
  if(fault==16)regs[bases[1]+0x22c0]=0x701;
  if(fault==17)regs[bases[1]+0x22c0]=0x501;
  if(fault==18)regs[bases[1]+0x400c]=0x80000000;
  if(fault==19)regs[DR]=0x30000;
  if(fault==20)regs[DR+12]=(16u<<27)|3;
  if(fault==21)regs[DR+12]=4;if(fault==22)regs[DR+12]=(17u<<27)|4;
  if(fault==23)regs[DW+4]=1;if(fault==24)regs[DW+4]=~0u;
  if(fault==25)regs[DW+0x24]=1u<<12;if(fault==26)regs[DW]=2;
  if(fault==27)regs[DR+0x404]=1;if(fault==28)regs[DR+0xd0]=1;
  trace.clear();messages.clear();bool ok=m->noCAdmission();assert(ok==(fault==0));
  if(DIAG_CHECK){if(fault)assert(has(gates[fault]));else assert(messages.empty());assert(messages.size()<=3);}
  for(auto&o:trace)assert(!o.write);
  if(fault&&fault<=18)for(auto&o:trace)assert(!(o.addr>=DR&&o.addr<DW+0x4000));
  // Samples consumed by checks only. No read past a failed short circuit.
  if(fault>=23&&fault<=26)for(auto&o:trace)assert(o.addr!=DR+0x404&&o.addr!=DR+0xd0);
  if(fault==27)for(auto&o:trace)assert(o.addr!=DR+0xd0);
  report(fault,ok);m->fAccepting=true;m->inactive=false;cleanup(m,d,r,n);
 }
 // Real state-machine failure context and no repeated/later diagnostics.
 for(unsigned fault=0;fault<3;++fault){
  nocFixtures();auto*n=nub();auto*r=make(n);auto*d=displayNub();auto*m=displayMake(d,r);
  if(fault==0)regs[bases[1]+0x400c]=0x80000000;
  if(fault==1)regs[DW+4]=~0u;if(fault==2)regs[DR+0xd0]=1;
  messages.clear();trace.clear();auto token=retain(m);
  for(unsigned i=0;i<256&&m->noCBusy();++i)step(m,r);
  assert(nstatus(m,token).state==SweetNoCFailed&&!nstatus(m,token).acknowledged);
  if(DIAG_CHECK){assert(has("SWEET MDSS NOC CONTEXT: phase=")&&has("register-acked=1"));
   assert(has(fault==0?"gate=clock-retain":(fault==1?"gate=wrapper-override":"gate=error-irq-status")));
   unsigned diagnostics=0;for(auto&msg:messages)
    diagnostics+=msg.find("SWEET MDSS NOC ADMIT:")!=std::string::npos||
                 msg.find("SWEET MDSS NOC CONTEXT:")!=std::string::npos||
                 msg.find("SWEET MDSS CLOCK: admit gate=")!=std::string::npos;
   assert(diagnostics>=2&&diagnostics<=4);}
  for(auto&o:trace)if(o.write)assert(!(o.addr>=DT&&o.addr<DT+0x3000));
  report(29+fault,false);auto size=trace.size(),logs=messages.size();
  SweetMDSS::noCTimerAction(m,m->fNoCTimer);assert(trace.size()==size&&messages.size()==logs);
  cleanup(m,d,r,n);
 }
}
'''

def run(tree,diagnostic):
    # The existing fixture loads actual code from argv[1].
    sys.argv[:]=[sys.argv[0],str(tree)]
    ns={'__file__':str(fixture),'__name__':'noc_diagnostic_fixture'}
    exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(',1)[0],str(fixture),'exec'),ns)
    common=ns['ns']
    source=ns['prelude']+common['DSI_MEMORY_FIXTURE']+common['pool_fixture']+common['header']+common['source']+ns['helpers']+ns['tests'].split('int main(){')[0]+checks
    with tempfile.TemporaryDirectory(prefix='mh2lm-noc-admit-') as temp:
        cpp,binary=Path(temp)/'test.cpp',Path(temp)/'test';cpp.write_text(source)
        subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',f'-DDIAG_CHECK={int(diagnostic)}',str(cpp),'-o',str(binary)],check=True)
        return subprocess.check_output([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),timeout=60)

target=Path(sys.argv[1]);baseline=Path(sys.argv[2]) if len(sys.argv)==3 else None
actual=run(target,True)
if baseline:
    old=run(baseline,False)
    assert actual==old,'diagnostics changed MMIO/read order or admission result'
print('PASS: 29 admission cases + 3 actual failure paths; precise clock/RSC/wrapper/solver gates, software context, bounded logs, no post-failure RSC/solver reads or payload writes, late timer silence;'+(' identical baseline/diagnostic MMIO traces and results' if baseline else ' actual provider code'))
