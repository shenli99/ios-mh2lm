#!/usr/bin/env python3
"""Run actual SweetBattery diagnostics with per-register failures on the host.

Usage: test-mh2lm-battery-diagnostics.py EXTRACTED_XNU
Checks log validity and independent reads, not PMIC access/timing or charging.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    sys.exit(__doc__)
source = (Path(sys.argv[1]) / 'iokit/Kernel/arm/SweetBattery.cpp').read_text()
start = source.index('struct SweetBattDiagByte {')
end = source.index('\nunsigned\nSweetBattery::estimatePercent', start)
diagnostics = source[start:end]
constants = '\n'.join(re.findall(r'^#define SWEET_BATT_\w+[^\n]*', source, re.M))
prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <map>
#include <set>
#include <string>
#include <vector>
static std::string logs;
static std::map<unsigned,uint8_t> values;
static std::vector<unsigned> reads;
static std::set<unsigned> failures;
static bool pairOK=true;
static uint16_t current=0xed5f;
static unsigned pairReads=0;
static void log(const char *fmt,...) {
    char buffer[2048];va_list args;va_start(args,fmt);
    int n=vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);
    assert(n>=0 && static_cast<size_t>(n)<sizeof(buffer));logs+=buffer;
}
#define SWEET_LOG_INFO(...) log(__VA_ARGS__)
#define SWEET_LOG_WARN(...) log(__VA_ARGS__)
static int sweet_spmi_read(unsigned sid,unsigned address,uint8_t *value) {
    assert(sid==2 && value);reads.push_back(address);assert(values.count(address));
    if(failures.count(address)) {*value=0xee;return -17;}
    *value=values.at(address);return 0;
}
class SweetBattery {
public:
    static void logChargePolicy();
    static void logChargeDiagnostics(unsigned state);
    static bool readWordPair(unsigned sid,unsigned address,unsigned copy,uint16_t *value) {
        assert(sid==2 && address==0x41a2 && copy==0x41a8);
        ++pairReads;*value=pairOK?current:0xdead;return pairOK;
    }
};
'''
checks = r'''
static std::string line(const char *prefix) {
    size_t pos=logs.find(prefix);assert(pos!=std::string::npos);
    return logs.substr(pos,logs.find('\n',pos)-pos);
}
static void contains(const std::string &text,const char *part) {
    if(text.find(part)==std::string::npos) {
        fprintf(stderr,"missing: %s\nin: %s\n",part,text.c_str());abort();
    }
}
static void sample() {
    logs.clear();reads.clear();pairReads=0;SweetBattery::logChargeDiagnostics(3);
    const std::vector<unsigned> expected={0x1007,0x100b,0x100d,0x1042,0x1043,
        0x1051,0x1648,0x110b,0x1307,0x1340,0x1370,0x1380,0x1506,0x1509,
        0x150b,0x1648,0x1690};
    assert(reads==expected && pairReads==1);
}
int main() {
    values={{0x1007,8},{0x100b,0x80},{0x100d,0},{0x1042,1},{0x1043,0},
        {0x1051,0x2c},{0x1648,0x18},{0x110b,0x95},{0x1307,0x21},{0x1340,0},
        {0x1370,30},{0x1380,0xd4},{0x1506,2},{0x1509,0x20},{0x150b,0x21},
        {0x1690,0x10}};
    sample();
    contains(line("SWEET BATT DIAG:"),"en=0x1");
    contains(line("SWEET BATT DIAG:"),"signed=-4769 ua=-2328612");
    contains(line("SWEET BATT DIAG:"),"config-ok=1 current-ok=1 config-valid=0x7f/0x7f");
    contains(line("SWEET BATT INPUT:"),"icl=1500mA");
    contains(line("SWEET BATT INPUT:"),"dtc-done=1 enum=0 slow-plugin=1 read-ok=0xf/0xf");
    contains(line("SWEET BATT TYPEC:"),"cc-attached=1 vbus=1 read-ok=0xf/0xf");
    contains(line("SWEET BATT SMB:"),"cfg=0x10 smb-sel=1 read-ok=0x3/0x3");
    assert(logs.find("unknown")==std::string::npos);
    // Each individual failure must leave all later fields and IBATT sampled.
    for(const auto &entry:values) {
        failures={entry.first};sample();char warning[80];
        snprintf(warning,sizeof(warning),"addr=%#x error=-17; value=unknown",entry.first);
        contains(logs,warning);contains(line("SWEET BATT DIAG:"),"current-ok=1");
    }
    // Reproduce the current transport whitelist: all three Type-C reads fail,
    // while AICL and the downstream SMB configuration are still independently read.
    failures={0x1506,0x1509,0x150b};sample();
    contains(line("SWEET BATT INPUT:"),"read-ok=0xf/0xf");
    contains(line("SWEET BATT TYPEC:"),"sink=unknown typec=unknown state-attached=-1 misc=unknown");
    contains(line("SWEET BATT TYPEC:"),"cc-attached=-1 vbus=-1 read-ok=0x1/0xf");
    contains(line("SWEET BATT SMB:"),"cfg=0x10 smb-sel=1 read-ok=0x3/0x3");
    failures={0x1042,0x1648};sample();
    contains(line("SWEET BATT DIAG:"),"en=unknown pause=0 cfg2=0x2c smben=unknown");
    contains(line("SWEET BATT DIAG:"),"config-ok=0 current-ok=1 config-valid=0x37/0x7f");
    contains(line("SWEET BATT SMB:"),"en=unknown override=-1 override-val=-1 cfg=0x10 smb-sel=1 read-ok=0x2/0x3");
    for(const auto &entry:values)failures.insert(entry.first);
    pairOK=false;sample();
    contains(line("SWEET BATT DIAG:"),"ibatt=unknown signed=unknown ua=unknown config-ok=0 current-ok=0 config-valid=0/0x7f");
    contains(line("SWEET BATT INPUT:"),"icl=-1mA");
    failures.clear();pairOK=true;current=0;sample();
    contains(line("SWEET BATT DIAG:"),"ibatt=0 signed=0 ua=0 config-ok=1 current-ok=1");
    current=943;sample();contains(line("SWEET BATT DIAG:"),"signed=943 ua=460448");
    puts("PASS: actual diagnostics; all independent register reads, failure masks/unknowns, Type-C rejection with later SMB success, invalid IBATT and signed current; no write/publication API in harness");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-battery-diag-') as temp:
    path = Path(temp)
    cpp = path / 'diagnostics.cpp'
    binary = path / 'diagnostics'
    cpp.write_text(prelude + constants + '\n' + diagnostics + checks)
    subprocess.run([os.environ.get('CXX', 'clang++-19'), '-std=c++17', '-g', '-O1',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    # LeakSanitizer cannot run under the workspace tracer. These diagnostics
    # add only stack storage; retain ASAN/UBSAN for actual code execution.
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    subprocess.run([str(binary)], env=env, check=True)
