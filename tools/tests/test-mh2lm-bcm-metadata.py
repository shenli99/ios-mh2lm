#!/usr/bin/env python3
"""Actual BCM lookup diagnostics, rejection boundaries and one-shot caching.
Usage: test-mh2lm-bcm-metadata.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
fixture = Path(__file__).with_name('test-mh2lm-display-noc.py')
ns = {'__file__': str(fixture), '__name__': 'bcm_fixture'}
exec(compile(fixture.read_text().rsplit('\nwith tempfile.TemporaryDirectory(', 1)[0],
             str(fixture), 'exec'), ns)
helpers = ns['helpers'] + ns['tests'].split('int main(){')[0]
tests = r'''
int main(){
 const uintptr_t entry=DB+400+24*5,table=DB+600;
 const char*gate[]={nullptr,"matches","aux","aux","values","values","values","values",
                    "address","address","aux","aux","aux","matches","alias",
                    "header","header","header","header","matches",nullptr};
 for(unsigned test=0;test<21;++test){
  nocFixtures();auto*n=nub();auto*r=make(n);SweetBCMInfo info;
  if(test==1)regs[entry]=0;
  if(test==2)regs[entry+20]=7|(40<<16);
  if(test==3)regs[entry+20]=12|(40<<16); // extended aux remains rejected
  if(test==4)regs[table]=0;
  if(test==5)regs[table+4]=0;
  if(test==6)regs[table+4]=4|(16<<16);
  if(test==7)regs[table+4]=4|(1<<24);
  if(test==8)regs[entry+16]=0x40000;
  if(test==9)regs[entry+16]=0x50015;
  if(test==10)regs[entry+20]=8|(41<<16);
  if(test==11)regs[entry+20]=8|(0xffffu<<16);
  if(test==12)regs[entry+20]=8|(460<<16); // only four bytes left in DB
  if(test==13)regs[entry-24]=regs[entry];
  if(test==14){assert(r->copyBCMInfo(SweetBCMMC0,&info)==0);regs[entry+16]=0x50010;}
  if(test==15)regs[DB+40]=2|(256<<16);
  if(test==16)regs[DB+40]=5|(257<<16);
  if(test==17)regs[DB+40]=5|(900<<16);
  if(test==18)regs[DB+44]=0xffffu|(6<<16);
  if(test==19)regs[entry+4]=1; // exact eight-byte name required
  if(test==20){regs[entry+20]=8|(456<<16);regs[DB+1016]=1000000;regs[DB+1020]=4;}
  trace.clear();messages.clear();auto rc=r->copyBCMInfo(SweetBCMACV,&info);
  assert(rc==(gate[test]?kIOReturnNotFound:kIOReturnSuccess));
  assert(!writes());
  for(const auto&op:trace)assert(op.addr>=DB&&op.addr+4<=DB+1024);
  // Bad address/aux must not read rejected auxiliary payload, even for logging.
  if(test==2||test==3||(test>=8&&test<=12))
   for(const auto&op:trace)assert(op.addr!=table&&op.addr!=table+4);
  assert(messages.size()==1);
  auto msg=messages.front();assert(msg.find("name=ACV")!=std::string::npos);
  if(gate[test])assert(msg.find(std::string("gate=")+gate[test])!=std::string::npos);
  else assert(msg.find("metadata ready address=0x50014 unit=1000000 width=4 vcd=0")!=std::string::npos);
  if(test==4)assert(msg.find("unit=0 packed=0x4 width=4 vcd=0 reserved=0")!=std::string::npos);
  if(test==7)assert(msg.find("reserved=1")!=std::string::npos);
  if(test==12)assert(msg.find("table=0x3fc bytes=8 db-size=0x400")!=std::string::npos);
  if(test==14)assert(msg.find("other=MC0")!=std::string::npos);
  auto before=trace.size();
  assert(r->copyBCMInfo(SweetBCMACV,&info)==rc&&trace.size()==before&&messages.size()==1);
  destroy(r,n);dropNub(n);reboot();
  assert(!live&&!liveMaps&&!rawBlocks&&subscriptions.empty());
 }
 puts("PASS: actual BCM diagnostics: 21 success/failure cases, bounded reads, no votes, exact gates/values, cached lookup silent");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-bcm-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(ns['prelude']+ns['ns']['DSI_MEMORY_FIXTURE']+ns['ns']['pool_fixture']+
                   ns['ns']['header']+ns['ns']['source']+helpers+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '')+':halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
