#!/usr/bin/env python3
"""Actual freestanding FDT/ADT handoff + kernel profile ABI under ASan/UBSan.
Usage: test-mh2lm-panel-handoff.py EXTRACTED_XNU
"""
from pathlib import Path
import hashlib, json, os, random, re, struct, subprocess, sys, tempfile
if len(sys.argv)!=2:sys.exit(__doc__)
repo=Path(__file__).resolve().parents[2];arm=Path(sys.argv[1])/'iokit/Kernel/arm'
sys.path.insert(0,str(repo/'tools'));import adt
assert (repo/'boot-support/panel-handoff/panel_identity.h').read_bytes()==(arm/'SweetPanelIdentity.h').read_bytes()
records=json.loads((repo/'tools/tests/fixtures/mh2lm-panel-profiles.json').read_text())['profiles']
source=(arm/'SweetPanelProfile.h').read_text()
checks=[]
for rec in records:
 for key,value in rec['sequences'].items():
  raw=bytes.fromhex(value['hex']);assert len(raw)==value['size'] and hashlib.sha256(raw).hexdigest()==value['sha256']
  name=f'sweetPanel_{rec["revision"]}_{key}'
  actual=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',re.search(r'\b'+name+r'\[\]=\{(.*?)\};',source,re.S)[1]))
  assert actual==raw
  checks.append(f'assert(sweet_dsi_sequence_valid({name},sizeof({name}),nullptr));')
  # Independent packet decoder verifies the actual LG headers and delays.
  packets=[];off=0
  while off<len(raw):
   typ,last,vc,ack,delay,hi,lo=raw[off:off+7];n=hi*256+lo
   assert typ in (5,21,57) and last==1 and vc==ack==0 and n and off+7+n<=len(raw)
   packets.append((raw[off+7:off+7+n],delay));off+=7+n
  if key=='off':assert packets==[(b'\xb0\x03',0),(b'\x51\0\0',90),(b'\x28',0),(b'\x10',150)]
  else:assert packets[-2:]==[(b'\x29',0),(b'\x11',150)]

def node(name,props=(),children=()):
 n=adt.Node();n.props=[('name',0,name.encode()+b'\0')]+list(props);n.children=list(children);return n

def adtblob(slot=True,duplicate=False,flags=0,prefill=False):
 props=[('sweet,panel-identity',flags,(b'x' if prefill else b'\0')*96)] if slot else []
 m=node('mdss',props);children=[m]+([node('mdss',props)] if duplicate else [])
 return adt.serialize(node('device-tree',children=[node('chosen'),node('arm-io',children=children)]))

def fdt(args,duplicate=False,chosen_duplicate=False):
 strings=b'bootargs\0';u=lambda x:struct.pack('>I',x)
 begin=lambda name:u(1)+name+b'\0'+bytes((-len(name)-1)%4)
 prop=lambda value:u(3)+u(len(value))+u(0)+value+bytes(-len(value)%4)
 chosen=begin(b'chosen')+prop(args)+(prop(args) if duplicate else b'')+u(2)
 st=begin(b'')+chosen+(chosen if chosen_duplicate else b'')+u(2)+u(9)
 so=56;no=so+len(st);total=no+len(strings)
 return struct.pack('>10I',0xd00dfeed,total,so,no,40,17,16,0,len(strings),len(st))+bytes(16)+st+strings

def array(b):return '{'+','.join(str(x) for x in b)+'}'
cases=[]
for i,rev in enumerate(['pp1','dv1','dv2','pv'],1):
 label=f'dsi_r66456_tianmamd_{rev}_1080_2340_cmd_display'.encode()
 for suffix in [b':',b':config0',b':config0:timing0']:
  cases.append((fdt(b'private=never-copy msm_drm.dsi_display0='+label+suffix+b' unrelated=private\0'),2,i))
for args,status in [(b'\0',1),(b'foo=bar\0',1),(b'msm_drm.dsi_display0=alien:config0\0',3),
 (b'msm_drm.dsi_display0=dsi_r66456_tianmamd_pv_1080_2340_cmd_display:config1\0',3),
 (b'msm_drm.dsi_display0=dsi_r66456_tianmamd_pv_1080_2340_cmd_display\0',3),
 (b'msm_drm.dsi_display0=alien: msm_drm.dsi_display0=alien:\0',4),
 (b'bad\0tail\0',5),(b'no-nul',5),(b'\xff\0',5),(b'x'*8193+b'\0',5)]:cases.append((fdt(args),status,0))
cases.extend([(fdt(b'\0',duplicate=True),4,0),(fdt(b'\0',chosen_duplicate=True),4,0)])
valid=fdt(b'msm_drm.dsi_display0=dsi_r66456_tianmamd_pv_1080_2340_cmd_display:config0\0')
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "panel_handoff.h"
}
#include "SweetDSICommand.h"
#include "SweetPanelProfile.h"
static void check(std::vector<uint8_t> f,unsigned source,unsigned profile){
 uint8_t out[MH2LM_PANEL_IDENTITY_SIZE];mh2lm_panel_from_fdt(f.data(),f.size(),out);
 assert(mh2lm_panel_source_status(out,sizeof(out))==source);
 assert(mh2lm_panel_profile(out,sizeof(out))==profile);
 assert(out[16]==profile);for(unsigned i=24;i<sizeof(out);++i) assert(!out[i]||(profile&&out[i]<127));
}
int main(){
'''
code+='\n'.join(f'check({array(b)},{status},{profile});' for b,status,profile in cases)
code+='\n'+ '\n'.join(checks)
code+=f'\nstd::vector<uint8_t> f={array(valid)},a={array(adtblob())};\n'
code+=r'''
 uint8_t out[96];mh2lm_panel_from_fdt(f.data(),f.size(),out);assert(mh2lm_panel_profile(out,96)==4);
 assert(mh2lm_panel_fill_adt(a.data(),a.size(),out));
 auto filled=a;assert(mh2lm_panel_fill_adt(a.data(),a.size(),out)==-1&&a==filled);
 for(size_t n=0;n<f.size();++n){mh2lm_panel_from_fdt(f.data(),n,out);assert(!mh2lm_panel_profile(out,96));}
 mh2lm_panel_from_fdt(f.data(),f.size(),out);
 for(unsigned i=0;i<96;++i){out[i]^=0x80;assert(!mh2lm_panel_profile(out,96));out[i]^=0x80;}
 // Every byte/bit mutation and every truncated extent, never an unchecked read.
 for(size_t i=0;i<f.size();++i)for(unsigned bit=0;bit<8;++bit){auto copy=f;copy[i]^=1u<<bit;mh2lm_panel_from_fdt(copy.data(),copy.size(),out);}
'''
for b,success in [(adtblob(),True),(adtblob(False),False),(adtblob(duplicate=True),False),(adtblob(flags=1),False),(adtblob(prefill=True),False),(adtblob()+b'x',False)]:
 code+=f'\n{{std::vector<uint8_t> b={array(b)};auto before=b;uint8_t record[96];mh2lm_panel_record(record,2,4);assert((mh2lm_panel_fill_adt(b.data(),b.size(),record)==1)=={str(success).lower()});'
 if not success:code+='assert(b==before);'
 code+='}\n'
code+=f'\na={array(adtblob())};\n'
code+=r'''
 for(size_t n=0;n<a.size();++n){auto before=a;assert(mh2lm_panel_fill_adt(a.data(),n,out)==-1&&a==before);}
 for(size_t i=0;i<a.size();++i)for(unsigned bit=0;bit<8;++bit){auto copy=a;copy[i]^=1u<<bit;auto before=copy;int ok=mh2lm_panel_fill_adt(copy.data(),copy.size(),out);if(ok!=1)assert(copy==before);}
 puts("PASS: actual bounded FDT/ADT handoff, four exact ABL labels, duplicate/unknown/malformed rejection, no partial writes, every truncation/bit mutation; all eight LG command tables byte-identical and valid with independent delay/order checks");
}
'''
# Exercise the actual DarwinBoot adapter; only HOB/memory services are stubs.
code=code.replace('int main(){', 'static void adapterChecks();\nint main(){')
code=code.replace(' puts("PASS: actual bounded', ' adapterChecks();\n puts("PASS: actual bounded')
code+=r'''
#define STATIC static
#define CONST const
#define VOID void
using UINT8=uint8_t;using UINT32=uint32_t;using UINT64=uint64_t;using UINTN=size_t;
using BOOLEAN=bool;using EFI_STATUS=unsigned;
#define TRUE true
#define FALSE false
#define EFI_SUCCESS 0u
#define EFI_BUFFER_TOO_SMALL 5u
#define EFI_COMPROMISED_DATA 33u
#define EFI_ERROR(s) ((s)!=0)
#define SIZE_1MB (1024u*1024u)
#define SIZE_2MB (2u*SIZE_1MB)
#define MAX_UINT64 UINT64_MAX
#define EFI_PAGE_SHIFT 12
#define DEBUG(x) ((void)0)
struct EFI_GUID{uint32_t a;uint16_t b,c;uint8_t d[8];};
struct EFI_HOB_GUID_TYPE{size_t size;uint64_t address;};
struct EFI_MEMORY_DESCRIPTOR{unsigned Type;uint64_t PhysicalStart,NumberOfPages;};
enum{EfiReservedMemoryType,EfiLoaderCode,EfiLoaderData,EfiBootServicesCode,EfiBootServicesData,EfiRuntimeServicesCode,EfiRuntimeServicesData,EfiConventionalMemory,EfiUnusableMemory,EfiACPIReclaimMemory,EfiACPIMemoryNVS,EfiMemoryMappedIO};
static unsigned allocs=0,hobCalls=0,mapCalls=0,adapterFault=0;
static EFI_HOB_GUID_TYPE hob;static bool hasHob;
static EFI_MEMORY_DESCRIPTOR descriptor;
#define GET_GUID_HOB_DATA_SIZE(p) ((p)->size)
#define GET_GUID_HOB_DATA(p) (&(p)->address)
static EFI_HOB_GUID_TYPE*GetFirstGuidHob(const EFI_GUID*g){assert(g->a==0xDAEE82E6);++hobCalls;return hasHob?&hob:nullptr;}
static uint64_t ReadUnaligned64(const uint64_t*p){uint64_t v;memcpy(&v,p,8);return v;}
static void*AllocatePool(size_t n){if(adapterFault==3)return nullptr;++allocs;return malloc(n);}
static void FreePool(void*p){assert(allocs);--allocs;free(p);}
static EFI_STATUS GetMemoryMap(size_t*size,EFI_MEMORY_DESCRIPTOR*map,size_t*,size_t*desc,uint32_t*){
 ++mapCalls;if(adapterFault==1)return 99;*desc=adapterFault==2?0:sizeof(descriptor);
 if(!map){*size=sizeof(descriptor);return EFI_BUFFER_TOO_SMALL;}
 if(adapterFault==4)return 99;assert(*size>=sizeof(descriptor));*size=sizeof(descriptor);*map=descriptor;return 0;
}
struct BS{decltype(&GetMemoryMap) GetMemoryMap;};static BS services{::GetMemoryMap};static BS*gBS=&services;
'''
code+=(repo/'boot-support/darwinboot/PanelSelection.h').read_text()
code+='\nstatic void adapterChecks(){\n'
code+=f'std::vector<uint8_t> live={array(valid)},base={array(adtblob())},legacy={array(adtblob(False))};\n'
code+=r'''
 descriptor={EfiReservedMemoryType,(uint64_t)live.data(),(live.size()+4095)/4096};
 hasHob=true;hob={8,(uint64_t)live.data()};
 for(unsigned f=0;f<10;++f){auto tree=base;adapterFault=f;hobCalls=mapCalls=0;
  hasHob=f!=5;hob.size=f==6?4:8;hob.address=f==7?1:(uint64_t)live.data();
  descriptor.Type=f==8?EfiMemoryMappedIO:EfiReservedMemoryType;
  descriptor.NumberOfPages=f==9?0:(live.size()+4095)/4096;
  assert(PopulatePanelSelection(tree.data(),tree.size())==EFI_SUCCESS&&!allocs);
  assert(recorded(tree)==(f==0?MH_PANEL_SELECTED:(f==5?MH_PANEL_ABSENT:MH_PANEL_MALFORMED)));
  auto filled=tree;assert(PopulatePanelSelection(tree.data(),tree.size())==EFI_COMPROMISED_DATA&&filled==tree);
 }
 hobCalls=mapCalls=0;assert(PopulatePanelSelection(legacy.data(),legacy.size())==0&&!hobCalls&&!mapCalls);
 auto before=base;assert(PopulatePanelSelection(base.data(),base.size()-1)==EFI_COMPROMISED_DATA&&base==before&&!hobCalls&&!mapCalls);
}
'''
# Define this before adapterChecks without replacing any production code.
helper='static unsigned recorded(const std::vector<uint8_t>&a){for(size_t i=0;i+96<=a.size();++i){auto s=mh2lm_panel_source_status(a.data()+i,96);if(s)return s;}return 0;}\n'
code=code.replace('\nstatic void adapterChecks(){', '\n'+helper+'static void adapterChecks(){')
with tempfile.TemporaryDirectory(prefix='mh2lm-panel-handoff-') as t:
 t=Path(t);cpp=t/'test.cpp';cpp.write_text(code);obj=t/'handoff.o';binary=t/'test'
 flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror']
 subprocess.run(['clang-19','-std=c11',*flags,'-I'+str(repo/'boot-support/panel-handoff'),'-c',str(repo/'boot-support/panel-handoff/panel_handoff.c'),'-o',str(obj)],check=True)
 subprocess.run(['clang++-19','-std=c++17',*flags,'-I'+str(repo/'boot-support/panel-handoff'),'-I'+str(arm),str(cpp),str(obj),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),check=True)
