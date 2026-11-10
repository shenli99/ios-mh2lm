#!/usr/bin/env python3
"""Exercise actual F4 allocation/seeding code with guarded OS memory fixtures.
Usage: test-mh2lm-scanout-pool.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
from mh2lm_scanout_fixtures import BUFFER_FIXTURE

if len(sys.argv) != 2:
    sys.exit(__doc__)
arm = Path(sys.argv[1]).resolve()/'iokit/Kernel/arm'
source = '\n'.join((arm/name).read_text() for name in ['SweetScanoutPool.h', 'SweetScanoutPool.inc'])
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <new>
#include <cstdio>
#define SWEET_LOG_ERROR(...) do{}while(0)
#define ARM64_BOARD_CONFIG_MH2LM 1
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define OSTypeAlloc(t) new t()
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();p=nullptr;}}while(0)
using IOReturn=uint32_t;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnNoMemory=0xe00002bd;
static unsigned live=0;
struct OSObject {
 unsigned refs=1;
 static void*operator new(size_t n){auto*p=calloc(1,n);if(!p)throw std::bad_alloc();return p;}
 static void operator delete(void*p){::free(p);}
 OSObject(){++live;}virtual ~OSObject(){--live;}virtual bool init(){return true;}virtual void free(){}
 void release(){assert(refs);if(!--refs){free();delete this;}}
};
struct IOMemoryDescriptor : OSObject {uint64_t length=0;uint64_t getLength(){return length;}};
'''
tests = r'''
int main(){
 for(unsigned mode=1;mode<=12;++mode){scanoutReset(mode);
  auto*p=SweetScanoutPool::withGeometry(1125,2436,4608);assert(!p);
  assert(live==0&&scanoutBuffers.empty()&&scanoutPrepares==scanoutCompletes);
 }
 scanoutReset();
 for(auto g: {std::vector<unsigned>{0,7,64},{8,0,64},{8,7,63},{17,7,64},
     {4097,7,16448},{8,4097,64},{8,7,65600},{4096,4096,65536}})
  assert(!SweetScanoutPool::withGeometry(g[0],g[1],g[2]));
 assert(!scanoutAllocCalls);
 auto*p=SweetScanoutPool::withGeometry(8,7,64);assert(p&&p->bytes()==448);
 assert(p->physical(0)==0x10000000&&p->physical(1)==0x14000000&&p->physical(2)==0);
 for(auto*buffer:scanoutBuffers)for(auto byte:buffer->storage)assert(byte==0xab); // caller exclusion checks precede any pixel writes
 uint8_t src[60];for(unsigned i=0;i<60;++i)src[i]=i+1;
 uint8_t original[60];memcpy(original,src,sizeof(src));
 assert(p->seedABL(src,sizeof(src),4,3,20,2,2));assert(!memcmp(original,src,sizeof(src)));
 auto*a=(uint8_t*)scanoutBuffers[0]->getBytesNoCopy();
 auto*b=(uint8_t*)scanoutBuffers[1]->getBytesNoCopy();
 std::vector<uint8_t> expected(448);
 for(unsigned y=0;y<3;++y)memcpy(expected.data()+(y+2)*64+8,src+y*20,16);
 assert(!memcmp(a,expected.data(),448));for(unsigned i=0;i<448;++i)assert(b[i]==0);
 auto rejected=[&](const void*s,size_t len,unsigned w,unsigned h,unsigned stride,unsigned x,unsigned y){
  assert(!p->seedABL(s,len,w,h,stride,x,y));assert(!memcmp(a,expected.data(),448));};
 rejected(nullptr,60,4,3,20,2,2);rejected(src,55,4,3,20,2,2);rejected(src,60,0,3,20,2,2);
 rejected(src,60,4,0,20,2,2);rejected(src,60,4,3,15,2,2);rejected(src,60,4,3,20,5,2);
 rejected(src,60,4,3,20,2,5);rejected(src,60,4,3,20,UINT32_MAX,2);rejected(src,60,4,3,20,2,UINT32_MAX);
 rejected(src,60,UINT32_MAX,3,20,0,0);rejected(src,60,4,UINT32_MAX,20,0,0);
 p->release();assert(live==0);scanoutReset();
 // Full real geometry: exactly the visible panel bytes are read into the
 // logical crop; row padding, margins, and buffer B stay zero.
 p=SweetScanoutPool::withGeometry(1125,2436,4608);assert(p&&p->bytes()==11225088);
 std::vector<uint8_t> panel(4320*2340,0x67);
 assert(p->seedABL(panel.data(),panel.size(),1080,2340,4320,22,48));
 a=(uint8_t*)scanoutBuffers[0]->getBytesNoCopy();b=(uint8_t*)scanoutBuffers[1]->getBytesNoCopy();
 for(unsigned y=0;y<2436;++y)for(unsigned x=0;x<4608;++x){
  bool visible=y>=48&&y<2388&&x>=88&&x<4408;
  assert(a[y*4608+x]==(visible?0x67:0));assert(b[y*4608+x]==0);
 }
 p->release();assert(live==0&&scanoutBuffers.empty()&&scanoutPrepares==scanoutCompletes);
 puts("PASS: actual F4 pool, 12 allocation/prepare/physical-range failure paths, balanced cleanup, bounded ABL seeding, real geometry/crop, zero padding and guarded source/destination memory");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-scanout-pool-') as tmp:
    path=Path(tmp)/'test.cpp'
    path.write_text(prelude+BUFFER_FIXTURE+source+tests)
    binary=Path(tmp)/'test'
    subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-g',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer',str(path),'-o',str(binary)],check=True)
    env=dict(os.environ)
    env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_leaks=0'
    subprocess.run([str(binary)],check=True,env=env,timeout=45)
