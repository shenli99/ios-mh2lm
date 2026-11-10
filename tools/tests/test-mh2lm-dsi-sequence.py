#!/usr/bin/env python3
"""Exercise the real MDSS DSI executor with independent MMIO/time inputs.
Usage: test-mh2lm-dsi-sequence.py EXTRACTED_XNU
Only OS objects/MMIO primitives are fixtures. No alternate executor model.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
here = Path(__file__).resolve().parent
# Reuse the existing OS fixtures/source extraction, without running its main.
existing = here/'test-mh2lm-display-service.py'
ns = {'__file__': str(existing), '__name__': 'dsi_fixture'}
exec(compile(existing.read_text().split('with tempfile.TemporaryDirectory(')[0], str(existing), 'exec'), ns)
prelude = ns['prelude']
prelude = prelude.replace('static bool dmaFired=false,', 'static bool keepDSIDone=false;\nstatic bool dmaFired=false,')
# Completion and busy status must be independent inputs, not auto-generated
# simply because the driver wrote the trigger or read a status register.
prelude = re.sub(r'if\(b==bases\[6\]&&o==0x110&&dmaFired\)v=.*?;', '', prelude)
prelude = prelude.replace(' regs[b+o]=v;if(b==bases[6]&&o==0x90)', '''
 if(b==bases[6]&&o==0x110){
  constexpr uint32_t mask=0x51115501u;
  regs[b+o]=(v&~mask)|(regs[b+o]&mask&~(keepDSIDone?(v&~1u):v));return;
 }
 regs[b+o]=v;if(b==bases[6]&&o==0x90)''')
fixture = json.loads((here/'fixtures/mh2lm-pv-panel-commands.json').read_text())
arrays = '\n'.join('static const uint8_t pv_'+name+'[]={'+','.join('0x'+x for x in fixture[name].split())+'};'
                   for name in ['on', 'off'])
tests = r'''
static SweetMDSS*make(IOService*nub){
 resetRegs();auto*s=new SweetMDSS();assert(s->start(nub));s->fDsiEnabled=true;
 assert(s->mapIrqResources(nub,true));
 regs[bases[6]]=0x20030000;regs[bases[6]+0x110]=0;regs[bases[6]+8]=0;
 regs[bases[7]]=0x2b0406;regs[bases[7]+0x20]=0x4c017e5e;regs[bases[7]+0x24]=0x60000038;
 regs[bases[7]+0x80c]=0x84200800;regs[bases[7]+0xc0c]=2;
 regs[bases[7]+0x1008]=0x1f000;regs[bases[7]+0x1808]=1;
 regs[bases[8]]=0xe0;regs[bases[8]+0x58]=0x400;
 return s;
}
static void tick(SweetMDSS*s,uint64_t ns=1000000){
 fakeTicks+=ns;std::lock_guard<std::recursive_mutex> l(s->fLoop->gate);
 SweetMDSS::dsiTimerAction(s,s->fDsiTimer);
}
static void freshDone(){regs[bases[6]+0x110]|=1;regs[bases[6]+8]=0;}
static unsigned triggers(){unsigned n=0;for(auto op:trace)if(op.write&&op.addr==bases[6]+0x90)++n;return n;}
static void destroy(SweetMDSS*s,IOService*nub){
 s->stop(nub);trace.clear();tick(s);assert(trace.empty());s->release();queued.clear();assert(!liveMaps&&!rawBlocks);
}
int main(){
 auto*nub=new IOService;
 // Every prefix/length/type/VC/ACK/last error is rejected before MMIO writes.
 unsigned count=0;assert(sweet_dsi_sequence_valid(pv_on,sizeof(pv_on),&count)&&count==17);
 assert(sweet_dsi_sequence_valid(pv_off,sizeof(pv_off),&count)&&count==4);
 uint8_t data[253];for(unsigned i=0;i<253;++i)data[i]=(uint8_t)i;
 uint32_t encoded[64];
 for(unsigned n=0;n<=253;++n){
  SweetDSIMessage m{0x39,0,0,data,n};size_t bytes=sweet_dsi_encode(m,(uint8_t*)encoded,sizeof(encoded));
  assert(bool(bytes)==(n>=2&&n<=252));
  if(bytes){auto*p=(uint8_t*)encoded;assert(bytes==((n+7u)&~3u)&&p[0]==n&&p[1]==0&&p[2]==0x39&&p[3]==0xc0);
   assert(!memcmp(p+4,data,n));for(unsigned j=4+n;j<bytes;++j)assert(p[j]==0xff);}
 }
 auto*s=make(nub);uint64_t token=0;std::vector<uint8_t> bad(pv_on,pv_on+sizeof(pv_on));
 for(unsigned field:{0u,1u,2u,3u,5u,6u}){
  auto b=bad;b[field]=0xff;trace.clear();assert(s->beginDSISequence(b.data(),b.size(),&token)==kIOReturnBadArgument&&token==0&&trace.empty());
 }
 for(unsigned n=1;n<7;++n){trace.clear();assert(s->beginDSISequence(pv_on,n,&token)==kIOReturnBadArgument&&trace.empty());}
 auto truncated=bad;truncated.pop_back();trace.clear();assert(s->beginDSISequence(truncated.data(),truncated.size(),&token)==kIOReturnBadArgument&&trace.empty());
 destroy(s,nub);
 // Opt-in, version, command busy/error, clocks, SMMU ambiguity/translation,
 // bad physical buffers, and resource failures all reject before TE/auto writes.
 for(unsigned scenario=0;scenario<20;++scenario){
  s=make(nub);
  if(scenario==0)s->fDsiEnabled=false;
  if(scenario==1)regs[bases[6]]=0x20020000;
  if(scenario==2)regs[bases[6]+0x15c]=1;
  if(scenario==3)regs[bases[0]+0xc]=0;
  if(scenario==4)regs[bases[6]+0x110]=1u<<24;
  if(scenario==5)regs[bases[8]]=0xe1;
  if(scenario==6)regs[bases[7]+0x20]=0;
  if(scenario==7)regs[bases[7]+0x810]=0x84200800;
  if(scenario==8)regs[bases[8]+0x58]=2;
  if(scenario>=9&&scenario<=15)dsiMemoryFault=scenario-8;
  if(scenario==16)failTimerCreate=timerCreates+1;
  if(scenario==17)failEventAdd=eventAdds+1;
  if(scenario==18)s->fDsiNextToken=~(uint64_t)0;
  if(scenario==19){s->fIrqMode=2;s->fIrqState=SweetMDSS::IrqArmed;}
  trace.clear();assert(s->beginDSISequence(pv_on,sizeof(pv_on),&token)!=0&&token==0);
  for(auto op:trace)assert(!op.write);
  s->fIrqMode=0;s->fIrqState=SweetMDSS::IrqIdle;
  dsiMemoryFault=0;failTimerCreate=failEventAdd=0;
  destroy(s,nub);assert(dsiPrepares==dsiCompletes);
 }
 // The entire real PV table executes unsplit, including D3=149 bytes, LP,
 // canonical header/padding, exact waits/order and immutable request storage.
 for(bool off:{false,true}){
  s=make(nub);auto*source=off?pv_off:pv_on;size_t length=off?sizeof(pv_off):sizeof(pv_on);
  std::vector<uint8_t> request(source,source+length);trace.clear();
  assert(s->beginDSISequence(request.data(),request.size(),&token)==0&&token);
  std::fill(request.begin(),request.end(),0); // caller storage no longer needed
  uint32_t te=s->fDsiTE,aut=s->fDsiAuto;size_t offset=0;unsigned commands=0;
  assert(s->beginDSISequence(source,length,&token)==kIOReturnBusy&&token==0);token=s->fDsiStatus.token;
  while(offset<length){
   SweetDSIMessage m{};assert(sweet_dsi_next(source,length,&offset,&m));
   unsigned before=triggers();bool already=s->fDsiStatus.state==SweetDSITransferring;
   if(!already)tick(s);
   assert(s->fDsiStatus.state==SweetDSITransferring&&triggers()==before+(!already));
   uint32_t expected[64];size_t bytes=sweet_dsi_encode(m,(uint8_t*)expected,sizeof(expected));
   assert(regs[bases[6]+0x4c]==bytes&&(regs[bases[6]+0x3c]&(1u<<28))&&(regs[bases[6]+0x3c]&(1u<<26)));
   if(bytes>64){assert(!regs[bases[6]+0x15c]&&s->fDsiPinned&&s->fDsiBuffer->refs==2);
    assert(regs[bases[6]+0x48]==s->fDsiPhysical&&!memcmp(s->fDsiBuffer->getBytesNoCopy(),expected,bytes));}
   else{std::vector<uint32_t> writes;for(auto op:trace)if(op.write&&op.addr==bases[6]+0x17c)writes.push_back(op.value);
    unsigned padded=(unsigned)(bytes/4+((bytes/4)&1u));assert(writes.size()>=padded);
    assert(!memcmp(writes.data()+writes.size()-padded,expected,bytes));}
   assert(s->setBrightness(5)==kIOReturnBusy&&s->setPanelPower(false)==kIOReturnBusy);
   SweetDSIStatus status{};size_t prior=trace.size();assert(s->dsiSequenceStatus(token,&status)==0&&trace.size()==prior);
   tick(s);assert(s->fDsiStatus.state==SweetDSITransferring); // trigger is not done
   freshDone();regs[bases[6]+8]=2;tick(s);assert(s->fDsiStatus.state==SweetDSITransferring); // done is not idle
   regs[bases[6]+8]=0;tick(s);++commands;
   assert(s->fDsiStatus.state==SweetDSIDelay&&s->fDsiStatus.completed==commands&&!s->fDsiPinned);
   unsigned sent=triggers();
   if(m.waitMS){tick(s,(uint64_t)m.waitMS*1000000-1);assert(triggers()==sent&&s->fDsiStatus.state==SweetDSIDelay);}
   // Frozen line inside visible range must not prevent sleep/ON commands.
   regs[bases[4]+0x2b0]=1000;
   if(offset<length){tick(s,1);assert(s->fDsiStatus.state==SweetDSITransferring);}
   else tick(s,1);
  }
  assert(s->fDsiStatus.state==SweetDSIComplete&&s->fDsiStatus.completed==(off?4u:17u));
  assert(regs[bases[4]+0x288]==te&&regs[bases[4]+0x2b4]==aut&&!s->fDsiPoisoned);
  assert(regs[bases[6]+0x3c]==9&&regs[bases[6]+0x4c]==0x18&&regs[bases[6]+0x84]==0x32);
  uint64_t oldToken=token;assert(s->beginDSISequence(pv_off,sizeof(pv_off),&token)==0&&token>oldToken);
  size_t noWrites=trace.size();s->cancelDSISequence(oldToken);assert(trace.size()==noWrites&&s->dsiBusy());
  s->cancelDSISequence(token);assert(!s->dsiBusy()&&!s->fDsiPoisoned);
  destroy(s,nub);assert(dsiPrepares==dsiCompletes);
 }
 // Cancellation/timeout/stop/free during DMA never releases the prepared
 // descriptor pin; only simulated reboot permits fixture cleanup.
 for(unsigned scenario=0;scenario<9;++scenario){
  s=make(nub);assert(s->beginDSISequence(pv_on,sizeof(pv_on),&token)==0);
  if(scenario==0){s->cancelDSISequence(token);assert(s->fDsiStatus.state==SweetDSICancelled&&!s->fDsiPoisoned);}
  else if(scenario==1){regs[bases[6]+8]=6;tick(s,200000001);assert(s->fDsiStatus.state==SweetDSIFailed&&!s->fDsiPoisoned);}
  else{
   // First two packets fit FIFO; third (CE67) uses actual command memory.
   for(unsigned n=0;n<2;++n){if(s->fDsiStatus.state!=SweetDSITransferring)tick(s);freshDone();tick(s);tick(s);}
   assert(s->fDsiStatus.state==SweetDSITransferring&&s->fDsiPinned);
   auto*buffer=s->fDsiBuffer;buffer->retain();
   if(scenario==2)s->cancelDSISequence(token);
   if(scenario==3)tick(s,200000001);
   if(scenario==4){regs[bases[6]+0x110]|=1u<<24;tick(s);}
   if(scenario==5){regs[bases[6]+0x68]=1;tick(s);}
   if(scenario==6){regs[bases[0]+0xc]=0;tick(s);}
   if(scenario==7)s->stop(nub);
   if(scenario==8){s->release();s=nullptr;}
   if(s){assert(s->fDsiPoisoned&&s->fDsiStatus.hardwareUncertain&&s->fDsiPinned);
    size_t prior=trace.size();assert(s->setBrightness(4)==kIOReturnNotReady&&trace.size()==prior);
    destroy(s,nub);}
   assert(buffer->refs==2&&buffer->prepared);buffer->release();
   buffer->complete(kIODirectionOut);buffer->release(); // simulated reboot ONLY
   assert(dsiPrepares==dsiCompletes);continue;
  }
  destroy(s,nub);assert(dsiPrepares==dsiCompletes);
 }
 // Cancellation during a post-command wait has no outstanding DMA, but
 // the partially applied panel table must not be labelled safe/ON again.
 s=make(nub);assert(s->beginDSISequence(pv_off,sizeof(pv_off),&token)==0);
 tick(s);freshDone();tick(s);s->cancelDSISequence(token);
 assert(s->fDsiStatus.completed==1&&s->fDsiStatus.hardwareUncertain&&s->fDsiPoisoned&&!s->fDsiPinned);
 destroy(s,nub);
 // A stale DONE that fails W1C must never be accepted as this command's done.
 s=make(nub);regs[bases[6]+0x110]=1;keepDSIDone=true;
 assert(s->beginDSISequence(pv_off,sizeof(pv_off),&token)==0);trace.clear();tick(s);
 assert(!triggers()&&s->fDsiStatus.state==SweetDSIFailed);keepDSIDone=false;destroy(s,nub);
 nub->release();assert(!live&&!liveMaps&&!rawBlocks&&dsiPrepares==dsiCompletes);
 puts("PASS: real DSI encoder/executor; complete PV17/4 packets, intact149-byte payload, delays/frozen TE, immutable requests, independent freshDONE+idle, staleDONE,20 admission/resource failures, token isolation,9 cancel/timeout/error/stop/free cases, prepared DMA lifetime");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-dsi-sequence-') as temp:
    cpp = Path(temp)/'test.cpp'
    cpp.write_text(prelude+ns['DSI_MEMORY_FIXTURE']+ns['pool_fixture']+ns['header']+ns['source']+arrays+tests)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-pthread',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_leaks=0'
    subprocess.run([str(binary)], check=True, env=env, timeout=45)
