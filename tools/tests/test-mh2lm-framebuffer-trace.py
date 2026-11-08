#!/usr/bin/env python3
"""Exercise actual F0 pixel reader in C and C++ with ASan/UBSan.
Usage: test-mh2lm-framebuffer-trace.py EXTRACTED_XNU
These are memory/geometry checks, not evidence about the physical display.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
include = Path(sys.argv[1]).resolve()/'bsd/dev'
assert (include/'sweet_fb_trace.h').is_file()
program = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sweet_fb_trace.h"
int main(void)
{
    unsigned char a[248], b[45];
    memset(a, 0xa5, sizeof(a)); memset(b, 0x5a, sizeof(b));
    struct SweetFBTraceView src={a,sizeof(a),8,8,6,40};
    struct SweetFBTraceView dst={b,sizeof(b),5,3,2,20};
    /* Crop (3,2) into (0,0), different offsets/row padding on each side. */
    for (unsigned y=0;y<2;++y) for (unsigned x=0;x<3;++x) {
        unsigned char pixel[4]={(unsigned char)(1+x),(unsigned char)(10+y),0,255};
        memcpy(a+8+(y+2)*40+(x+3)*4,pixel,4);
        memcpy(b+5+y*20+x*4,pixel,4);
    }
    struct SweetFBTracePixels p,q;
    assert(sweet_fb_trace_pixels(&src,3,2,&dst,0,0,3,2,1,&p));
    assert(p.samples==6 && p.rgb==6 && p.alpha==6 && !p.differentRGB && !p.differentAlpha);
    assert(sweet_fb_trace_pixels(&dst,0,0,NULL,0,0,3,2,1,&q) && q.hash==p.hash);
    /* Changes outside visible pixels, including row padding, do not affect hash. */
    a[8+2*40+8]=17; a[8+2*40+39]=32; b[5+12]=11;
    assert(sweet_fb_trace_pixels(&src,3,2,&dst,0,0,3,2,1,&q) && q.hash==p.hash && !q.differentRGB);
    /* One red, one green, one alpha-only change: all three are observable. */
    b[5+2]=9;
    assert(sweet_fb_trace_pixels(&src,3,2,&dst,0,0,3,2,1,&q) && q.differentRGB==1 && q.differentAlpha==0);
    b[5+4+1]=77; b[5+20+3]=0;
    assert(sweet_fb_trace_pixels(&src,3,2,&dst,0,0,3,2,1,&q) && q.differentRGB==2 && q.differentAlpha==1);
    assert(sweet_fb_trace_pixels(&dst,0,0,NULL,0,0,3,2,1,&q) && q.hash!=p.hash && q.alpha==5);
    /* Alpha-only black is distinct from RGB content. */
    unsigned char black[16]={0,0,0,255, 0,0,0,0, 0,0,0,128, 0,0,0,0};
    struct SweetFBTraceView v={black,sizeof(black),0,2,2,8};
    assert(sweet_fb_trace_pixels(&v,0,0,NULL,0,0,2,2,1,&q) && q.samples==4 && q.rgb==0 && q.alpha==2);
    /* Exactly the last pixel is legal; one missing alpha byte is rejected. */
    assert(sweet_fb_trace_pixels(&v,1,1,NULL,0,0,1,1,1,&q) && q.samples==1);
    v.length=15;
    assert(!sweet_fb_trace_pixels(&v,1,1,NULL,0,0,1,1,1,&q) && q.samples==0);
    v.length=16;
    assert(!sweet_fb_trace_pixels(&src,3,2,&v,0,0,3,2,1,&q) && q.samples==0);
    /* Invalid rectangles and overflow attempts must fail before any read. */
    assert(!sweet_fb_trace_pixels(&v,2,0,NULL,0,0,1,1,1,&q));
    assert(!sweet_fb_trace_pixels(&v,0,2,NULL,0,0,1,1,1,&q));
    assert(!sweet_fb_trace_pixels(&v,1,0,NULL,0,0,2,1,1,&q));
    assert(!sweet_fb_trace_pixels(&v,0,1,NULL,0,0,1,2,1,&q));
    assert(!sweet_fb_trace_pixels(&v,UINT32_MAX,0,NULL,0,0,2,1,1,&q));
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,UINT32_MAX,1,1,&q));
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,0,1,&q));
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,0,&q));
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,UINT32_MAX,&q));
    v.stride=7;
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,1,&q));
    v.stride=8; v.offset=UINT64_MAX;
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,1,&q));
    v.offset=0; v.width=UINT32_MAX;
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,1,&q));
    v.width=2; v.height=UINT32_MAX;
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,1,&q));
    v.height=2; v.base=(const volatile uint8_t *)(UINTPTR_MAX-8);
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,1,&q));
    v.base=NULL;
    assert(!sweet_fb_trace_pixels(&v,0,0,NULL,0,0,1,1,1,&q));
    /* Step=8 is explicitly sparse: changing an unsampled pixel need not show. */
    v.base=black;
    assert(sweet_fb_trace_pixels(&v,0,0,NULL,0,0,2,2,8,&p) && p.samples==1);
    black[4]=255;
    assert(sweet_fb_trace_pixels(&v,0,0,NULL,0,0,2,2,8,&q) && q.hash==p.hash);
    puts("PASS: BGRA channels, alpha, offsets/crop/padding, descriptor and overflow bounds, sparse sampling limits");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-fb-trace-') as temp:
    for ext, compiler, std in [('c',os.environ.get('CC','clang-19'),'c11'),
                                ('cpp',os.environ.get('CXX','clang++-19'),'c++17')]:
        path = Path(temp)/('test.'+ext)
        binary = Path(temp)/'test'
        path.write_text(program)
        subprocess.run([compiler,'-std='+std,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer','-I'+str(include),str(path),'-o',str(binary)],check=True)
        # LeakSanitizer cannot inspect threads in the workspace's ptraced runner.
        # The test allocates no heap memory; ASan/UBSan access checks remain active.
        env = dict(os.environ)
        env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS','')+':detect_leaks=0'
        subprocess.run([str(binary)],check=True,env=env)
