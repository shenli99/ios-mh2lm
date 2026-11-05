#!/usr/bin/env python3
"""Check real Sweet log macros and threshold: ERROR always, WARN/INFO filtered.
Usage: test-mh2lm-log-levels.py EXTRACTED_XNU
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = Path(sys.argv[1])
header = (root/'bsd/dev/sweet_log_level.h').read_text()
source = (root/'bsd/dev/sweet_ufs.c').read_text()
start = source.index('int\nsweet_log_level_enabled(unsigned level)')
end = source.index('\n}\n', start)+3
threshold = source[start:end]
prelude = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
static int requested, supplied;
static int PE_parse_boot_argn(const char *key, void *out, unsigned long size) {
    assert(!strcmp(key,"sweetloglevel") && size==sizeof(int));
    if (supplied) *(int *)out=requested;
    return supplied;
}
'''
main = r'''
int main(int argc, char **argv) {
    assert(argc==2); supplied=strcmp(argv[1],"unset")!=0; requested=atoi(argv[1]);
#ifdef FORCE_FILTER_OFF
    unsigned expected=0;
#else
    unsigned expected=(!supplied || requested<0 || requested>3)?2u:(unsigned)requested;
    for (unsigned level=0;level<=4;++level)
        assert(!!sweet_log_level_enabled(level)==(level<=expected));
#endif
    unsigned evaluated=0;
    SWEET_LOG_ERROR("failure %u\n",++evaluated);
    SWEET_LOG_WARN("warning %u\n",++evaluated);
    SWEET_LOG_INFO("running %u\n",++evaluated);
    SWEET_LOG_DEBUG("detail %u\n",++evaluated);
    assert(evaluated==expected+1);
}
'''
for relative in ['bsd/dev/sweet_spmi.c', 'bsd/dev/sweet_keys.c',
                 'iokit/Kernel/arm/SweetBattery.cpp',
                 'iokit/Kernel/arm/SweetPmicKeys.cpp',
                 'iokit/Kernel/arm/SweetSpmiInterruptMux.cpp',
                 'iokit/Kernel/arm/SweetSpmiMuxClient.cpp']:
    assert not re.search(r'\b(?:printf|IOLog)\s*\(', (root/relative).read_text()), relative
with tempfile.TemporaryDirectory(prefix='mh2lm-log-levels-') as temp:
    for language, compiler, standard in [('c',os.environ.get('CC','cc'),'c11'),
                                         ('cpp',os.environ.get('CXX','c++'),'c++17')]:
        for force_off in [False, True]:
            implementation = 'int sweet_log_level_enabled(unsigned level) { (void)level; return 0; }' if force_off else threshold
            program = Path(temp)/('test.'+language)
            binary = Path(temp)/'test'
            program.write_text(prelude+header+'\n'+implementation+'\n'+main)
            subprocess.run([compiler,'-std='+standard,*( ['-DFORCE_FILTER_OFF'] if force_off else []),str(program),'-o',str(binary)],check=True)
            for value in ['unset','-1','0','1','2','3','4']:
                level = 0 if force_off else (2 if value in ['unset','-1','4'] else int(value))
                expected = ['[ERROR] failure 1','[WARN] warning 2','[INFO] running 3','[DEBUG] detail 4'][:level+1]
                result = subprocess.run([str(binary),value],text=True,capture_output=True,check=True)
                assert result.stdout.splitlines()==expected, (language,force_off,value,result.stdout)
    print('PASS: real C/C++ log macros and threshold; ERROR survives forced filtering; filtered arguments not evaluated; six PMIC modules contain no raw log calls')
