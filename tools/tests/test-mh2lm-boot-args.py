#!/usr/bin/env python3
"""Check actual firmware/kernel boot_args layouts and real panel matrix limits.

Usage: test-mh2lm-boot-args.py PATCHED_DARWINBOOT_DRIVER XNU_ARM64_BOOT_HEADER
"""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import tomllib

if len(sys.argv) != 3:
    sys.exit(__doc__)
driver, kernel_header = map(lambda s: Path(s).resolve(), sys.argv[1:])
repo = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bootargs', repo/'tools/mh2lm-boot-args.py')
bootargs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootargs)
source = (driver/'DarwinBoot.c').read_text()
start = source.index('  ZeroMem (CommandLine, sizeof (CommandLine));')
end = source.index('  Root = NULL;', start)
parser = source[start:end]
assert 'CHAR8                           CommandLine[DARWIN_BOOT_LINE_LENGTH];' in source
assert 'CopyMem (Args->CommandLine, CommandLine, sizeof (CommandLine));' in source
code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BootArgs.h"
#include "kernel-boot.h"
_Static_assert(sizeof(DARWIN_BOOT_ARGS)==sizeof(boot_args),"whole ABI");
_Static_assert(BOOT_LINE_LENGTH==DARWIN_BOOT_LINE_LENGTH,"command line ABI");
#define SAME(a,b) _Static_assert(offsetof(DARWIN_BOOT_ARGS,a)==offsetof(boot_args,b),#a)
SAME(VirtualBase,virtBase); SAME(PhysicalBase,physBase); SAME(MemorySize,memSize);
SAME(TopOfKernelData,topOfKernelData); SAME(Video,Video); SAME(MachineType,machineType);
SAME(DeviceTree,deviceTreeP); SAME(DeviceTreeLength,deviceTreeLength);
SAME(CommandLine,CommandLine); SAME(BootFlags,bootFlags); SAME(MemorySizeActual,memSizeActual);
#define ZeroMem(p,n) memset(p,0,n)
#define DarwinPrint(...) ((void)0)
#define EFI_BAD_BUFFER_SIZE 1
static int parse(const uint16_t *text,char *out) {
  struct { const uint16_t *BootArgs; } request={text},*Request=&request;
  size_t Index;
  char CommandLine[DARWIN_BOOT_LINE_LENGTH];
''' + parser + r'''
  memcpy(out,CommandLine,sizeof(CommandLine));return 0;
}
int main(void) {
  uint16_t text[1025];char out[DARWIN_BOOT_LINE_LENGTH];
  for(size_t n=0;n<=1024;++n){
    for(size_t i=0;i<n;++i)text[i]='x';text[n]=0;
    memset(out,0xa5,sizeof(out));
    assert(parse(text,out)==(n>=BOOT_LINE_LENGTH));
    if(n<BOOT_LINE_LENGTH){
      DARWIN_BOOT_ARGS args={0};
      args.BootFlags=0x12345678;args.MemorySizeActual=0xc0000000;
      memcpy(args.CommandLine,out,sizeof(out));
      boot_args view;memcpy(&view,&args,sizeof(view));
      assert(view.bootFlags==0x12345678 && view.memSizeActual==0xc0000000);
      assert(strlen(view.CommandLine)==n);
    }
  }
  text[0]=0x100;text[1]=0;assert(parse(text,out)==EFI_BAD_BUFFER_SIZE);
  assert(parse(NULL,out)==0 && out[0]==0);
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-bootargs-') as temp:
    t = Path(temp)
    (t/'Base.h').write_text('''#include <stddef.h>
#include <stdint.h>
typedef uint16_t UINT16; typedef uint32_t UINT32; typedef uint64_t UINT64;
typedef char CHAR8;
#define STATIC_ASSERT(c,m) _Static_assert(c,m)
#define OFFSET_OF(t,m) offsetof(t,m)
''')
    for name in ['consistent_debug.h', 'protos.h']:
        p = t/'pexpert/arm'/name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('')
    (t/'BootArgs.h').write_bytes((driver/'BootArgs.h').read_bytes())
    (t/'kernel-boot.h').write_bytes(kernel_header.read_bytes())
    (t/'test.c').write_text(code)
    subprocess.run(['clang-19','-std=c11','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-I'+str(t),str(t/'test.c'),
                    '-o',str(t/'test')], check=True)
    subprocess.run([str(t/'test')], check=True,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))

    # Execute the actual shell branch; stub only the final image writer.
    script = (repo/'tools/mk-mh2lm-event-matrix.sh').read_text()
    start = script.index('if [ "$KEY_IRQ_PROFILE" = \'--framebuffer-service\' ]; then\n FB_ARGS=')
    end = script.index('\nif [ "$KEY_IRQ_PROFILE" = \'--framebuffer-contract\' ]', start)
    prefix = '''set -euo pipefail
KEY_IRQ_PROFILE=--framebuffer-service
PANEL_LIFECYCLE=1
LABEL=test
KERNEL_SHA256=test
TMPDIR=/tmp
make_profile() { printf '%s\\t%s\\n' "$1" "$3"; }
'''
    result = subprocess.run(['bash','-c',prefix+script[start:end]],
                            capture_output=True, text=True, check=True)
    names = []
    base = 'debug=0x14e serial=3 -v keepsyms=1 rd=ufsroot0 wdt=-1 panic_screen=1 cpus=8 sweetfb_noconsole=1 sweetfb_hz=30'
    for line in result.stdout.splitlines():
        if '\t' not in line:
            continue
        name, args = line.split('\t', 1)
        # The common base comes from make_profile and the baseline TOML.
        original = base+' '+args
        compact = bootargs.compact_panel(original)
        bootargs.validate(compact)
        old = dict(x.split('=',1) for x in original.split() if '=' in x)
        new = dict(x.split('=',1) for x in compact.split() if '=' in x)
        for key in ['sweetusb','sweetusbstep','sweetvbus','sweetbattery','sweetbatt',
                    'sweetbattirq','sweetbattruntime','sweetmdss','sweetfbservice',
                    'sweetfbpower','sweetmdssirq','sweetfbvsync','sweetfbown','sweetdsi',
                    'sweetdisplayresources','sweetfbperf']:
            assert old[key] == new[key], (name,key)
        assert 'sweetfbpreserveoff=1' in compact if name=='panel-identity' else 'sweetfbpower=1' in compact
        if name!='panel-identity':
            assert new['sweetpanellifecycle'] == {'panel-retained':'2','panel-combined':'5','panel-supply':'6'}[name]
        path = t/(name+'.toml')
        text = '[[entry]]\nid="xnu"\ntype="xnu"\nargs = '+json.dumps(original)+'\n'
        path.write_text(text)
        subprocess.run([sys.executable,str(repo/'tools/mh2lm-boot-args.py'),str(path),'--compact-panel'],check=True)
        assert tomllib.loads(path.read_text())['entry'][0]['args'] == compact
        names.append(name)
    assert names==['panel-identity','panel-retained','panel-combined','panel-supply']
    # Invalid input must fail before rewriting the TOML.
    invalid=t/'too-long.toml'
    for args in ['x'*608,'x'*1024,'sweetusb=2 sweetusb=0','sweetkeys=1 sweetkeys=0']:
        text='[[entry]]\ntype="xnu"\nargs = '+json.dumps(args)+'\n'
        invalid.write_text(text)
        r=subprocess.run([sys.executable,str(repo/'tools/mh2lm-boot-args.py'),str(invalid)],capture_output=True)
        assert r.returncode and invalid.read_text()==text
        r=subprocess.run([sys.executable,str(repo/'tools/mh2lm-boot-args.py'),str(invalid),'--compact-panel'],capture_output=True)
        assert r.returncode and invalid.read_text()==text
print('PASS: actual XNU/loader ABI equal; actual loader rejects 608..1024-byte and non-ASCII input; four real panel profiles fit without changing enabled hardware modes')
