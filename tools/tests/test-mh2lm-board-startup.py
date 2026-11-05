#!/usr/bin/env python3
"""Check actual board fan-out and source ownership at an extracted XNU tree.

Usage: test-mh2lm-board-startup.py EXTRACTED_XNU_DIRECTORY
Runs the real board entry with failing/disabled child adapters. Source checks
also enforce the phase and remove leaf-to-parent/leaf-to-sibling startup edges.
Not a replacement for native linking or phone startup/IRQ regression.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
r = Path(sys.argv[1]).resolve()
board = (r/'bsd/dev/sweet_board.c').read_text()
bsd = (r/'bsd/kern/bsd_init.c').read_text()
ufs = (r/'bsd/dev/sweet_ufs.c').read_text()
keys = (r/'bsd/dev/sweet_keys.c').read_text()
mux = (r/'iokit/Kernel/arm/SweetSpmiInterruptMux.cpp').read_text()
files = (r/'bsd/conf/files').read_text()
assert bsd.count('sweet_board_init();') == 1
assert bsd.index('bsd_autoconf();') < bsd.index('inittodr(0);') < bsd.index('sweet_board_init();') < bsd.index('/* Mount the root file system. */')
assert 'bsd/dev/sweet_board.c' in files
for name in ['sweet_keys_init', 'sweet_touch_init', 'sweet_usb_probe',
             'sweet_sample_start', 'sweet_display_probe_init', 'sweet_gpu_init',
             'sweet_spmi_mux_start', 'sweet_board_init']:
    assert name not in ufs, 'UFS owns unrelated startup: ' + name
assert 'sweet_log_start();' in ufs
assert ufs.index('su.ready = true;') < ufs.index('sweet_log_start();')
assert 'sweet_spmi_mux_start' not in keys
registry_clients = (r/'iokit/Kernel/arm/SweetSpmiMuxClient.cpp').exists()
if registry_clients:
    client = (r/'iokit/Kernel/arm/SweetSpmiMuxClient.cpp').read_text()
    key_service = (r/'iokit/Kernel/arm/SweetPmicKeys.cpp').read_text()
    assert 'sweet_spmi_mux' not in keys
    assert 'SweetSpmiMuxClient::withOwner' in key_service
    assert 'mux->attachConsumer(owner, group, events' in client
    assert 'mux->detachConsumer(owner, group, this)' in client
else:
    assert 'sweet_spmi_mux_attach(SWEET_MUX_KEYS' in keys
    assert 'sweet_spmi_mux_detach(SWEET_MUX_KEYS' in keys
assert 'sweet_keys_' not in mux
assert 'keyConsumer' not in mux
if 'class SweetSpmiInterruptMux : public IOService' in mux or registry_clients:
    config = (r/'iokit/KernelConfigTables.cpp').read_text()
    assert "'IOProviderClass' = SweetSpmiMuxDevice;" in config
    assert "'IOClass'         = SweetSpmiInterruptMux;" in config
    assert 'nub->attach(platform)' in mux
    assert 'nub->registerService(kIOServiceSynchronous)' in mux
    assert 'IOInterruptEventSource::interruptEventSource(this,' in mux
    assert 'static MuxBinding bindings' not in mux
    assert 'ml_io_map_unmappable(physical, size, VM_WIMG_IO)' in mux
    assert 'ml_io_map(' not in mux
body = re.sub(r'^#include .*$', '', board, flags=re.M)
prelude = r'''
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdio>
#define ARM64_BOARD_CONFIG_SWEET 1
#define ARM64_BOARD_CONFIG_MH2LM 1
#define SWEET_LOG_INFO(...) ((void)0)
#define SWEET_LOG_ERROR(...) ((void)0)
static std::vector<int> calls;
static bool muxFail, keysDisabled, keyFail;
int sweet_spmi_mux_start() { calls.push_back(1); return muxFail ? -1 : 0; }
void sweet_keys_init() {
    calls.push_back(2);
    if (keysDisabled || keyFail) return;
}
void sweet_touch_init() { calls.push_back(3); }
void sweet_usb_probe() { calls.push_back(4); }
void sweet_sample_start() { calls.push_back(5); }
void sweet_display_probe_init() { calls.push_back(6); }
'''
checks = r'''
int main(int argc, char **argv) {
    if (argc>1) {
        muxFail=!std::strcmp(argv[1],"mux-fail");
        keysDisabled=!std::strcmp(argv[1],"keys-disabled");
        keyFail=!std::strcmp(argv[1],"keys-fail");
    }
    sweet_board_init();
    assert((calls==std::vector<int>{1,2,3,4,5,6}));
    sweet_board_init(); // root mount retries must not duplicate drivers
    assert(calls.size()==6);
    std::printf("PASS: board startup %s; independent fan-out, one-shot\n",argc>1?argv[1]:"normal");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-board-start-') as d:
    src=Path(d)/'board.cpp'
    binary=Path(d)/'board-test'
    src.write_text(prelude+body+checks)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', str(src), '-o', str(binary)], check=True)
    for scenario in ['normal','keys-disabled','keys-fail','mux-fail']:
        subprocess.run([str(binary),scenario],
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True)
print('PASS: BSD phase, UFS storage-only startup, key consumer ownership')
