#!/usr/bin/env python3
"""Execute the reference branch2 leaf path with mocked regmap/time.

Usage: audit-mh2lm-lineage-branch.py LINEAGE_KERNEL [LG_KERNEL]
Requires clang++-19. Does not emulate CCF parents, a bus, or physical clocks.
Reference function bodies are extracted at runtime, not reimplemented here.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile


def reference(root):
    directory = Path(root) / 'drivers/clk/qcom'
    branch = (directory / 'clk-branch.c').read_text()
    regmap = (directory / 'clk-regmap.c').read_text()
    header = (directory / 'clk-branch.h').read_text()
    def function(source, name):
        match = re.search(r'^(?:static )?(?:bool|int|void) ' + name +
                          r'\([^;]*?\n\{.*?^\}', source, re.M | re.S)
        if not match:
            raise ValueError(f'missing reference function: {name}')
        return match.group(0)
    definitions = '\n'.join(line for line in header.splitlines()
                            if line.startswith('#define BRANCH_'))
    definitions += '\n' + branch[branch.index('#define BRANCH_CLK_OFF'):
                                 branch.index('static bool clk_branch2_check_halt')]
    bodies = [function(regmap, name) for name in (
        'clk_is_enabled_regmap', 'clk_enable_regmap', 'clk_disable_regmap')]
    bodies += [function(branch, name) for name in (
        'clk_branch_in_hwcg_mode', 'clk_branch2_check_halt', 'clk_branch_wait',
        'clk_branch_toggle', 'clk_branch2_enable', 'clk_branch2_disable')]
    return definitions + '\n' + '\n'.join(bodies)


if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
actual = reference(sys.argv[1])
if len(sys.argv) == 3:
    if actual != reference(sys.argv[2]):
        sys.exit('LG and Lineage reference functions differ; review before comparing')
    print('LG and Lineage extracted definitions/functions are byte-identical', flush=True)

prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <initializer_list>
using u32 = uint32_t;
#define BIT(n) (1u << (n))
struct clk_branch;
struct clk_hw { clk_branch *branch; void *core; };
struct clk_regmap {
 void *regmap; u32 enable_reg,enable_mask; bool enable_is_inverted; clk_hw hw;
};
struct clk_branch {
 u32 hwcg_reg,halt_reg; unsigned hwcg_bit,halt_bit,halt_check; clk_regmap clkr;
};
static clk_branch *to_clk_branch(clk_hw *hw) { return hw->branch; }
static clk_regmap *to_clk_regmap(clk_hw *hw) { return &hw->branch->clkr; }
static const char *clk_hw_get_name(const clk_hw *) { return "non_gdsc_ahb"; }
static u32 sample,written; static unsigned reads,updates,barriers,delay,warnings;
static unsigned clearOffAt; static int updateError;
static int regmap_read(void *,u32 address,u32 *value) {
 assert(address==0x4004 && barriers==1); ++reads;
 if (clearOffAt && reads==clearOffAt) sample&=~BIT(31);
 *value=sample; return 0;
}
static int regmap_update_bits(void *,u32 address,u32 mask,u32 value) {
 assert(address==0x4004 && mask==BIT(0) && !barriers && !reads);
 ++updates; written=value;
 // Post-write status is controlled independently of the requested enable.
 return updateError;
}
static void mb() { assert(updates==1 && !reads); ++barriers; }
static void udelay(unsigned us) { assert(barriers==1); delay+=us; }
#define WARN_CLK(...) (++warnings)
'''
cases = r'''
static clk_branch reset(unsigned policy,u32 status) {
 sample=status; written=0xffffffffu; reads=updates=barriers=delay=warnings=0;
 clearOffAt=0; updateError=0;
 clk_branch br{}; br.halt_reg=br.clkr.enable_reg=0x4004;
 br.clkr.enable_mask=BIT(0); br.halt_check=policy; return br;
}
int main() {
 unsigned cases=0;
 for (unsigned policy: {unsigned(BRANCH_HALT), unsigned(BRANCH_VOTED)}) {
  for (u32 status: {0u,1u,0x80000000u,0x80000001u,0x20000000u,0xa0000000u}) {
   auto br=reset(policy,status); br.clkr.hw.branch=&br;
   int rc=clk_branch2_enable(&br.clkr.hw); bool off=status&BIT(31);
   assert(rc==(off ? -EBUSY : 0));
   assert(updates==1 && written==1 && barriers==1);
   assert(reads==(off ? 500u : 1u));
   assert(delay==(off ? 500u : 0u) && warnings==unsigned(off));
   // Separate is_enabled() can be false even though enable() returned 0.
   assert(clk_is_enabled_regmap(&br.clkr.hw)==int(bool(status&1u)));
   ++cases;
  }
  { auto br=reset(policy,0x80000001); br.clkr.hw.branch=&br; clearOffAt=4;
    assert(clk_branch2_enable(&br.clkr.hw)==0);
    assert(reads==4 && delay==3 && !warnings); ++cases; }
  { auto br=reset(policy,0); br.clkr.hw.branch=&br; updateError=-EIO;
    assert(clk_branch2_enable(&br.clkr.hw)==-EIO);
    assert(updates==1 && written==1 && !reads && !barriers && !delay && !warnings);
    ++cases; }
 }
 for (unsigned policy: {unsigned(BRANCH_HALT), unsigned(BRANCH_VOTED)})
 for (u32 status: {0u,1u,0x80000000u,0x80000001u}) {
  auto br=reset(policy,status); br.clkr.hw.branch=&br;
  clk_branch2_disable(&br.clkr.hw);
  assert(updates==1 && written==0 && barriers==1);
  if (policy==BRANCH_VOTED) assert(!reads && delay==10 && !warnings);
  else if (status&BIT(31)) assert(reads==1 && !delay && !warnings);
  else assert(reads==500 && delay==500 && warnings==1);
  ++cases;
 }
 printf("PASS: %u leaf-path cases; CBCR0 passes enable, OFF1 times out, "
        "VOTED disable only delays, update error aborts before halt reads\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-lineage-branch-') as temporary:
    cpp = Path(temporary) / 'branch.cpp'
    binary = Path(temporary) / 'branch'
    cpp.write_text(prelude + actual + cases)
    subprocess.run(['clang++-19', '-std=c++17', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, env=dict(
        os.environ, ASAN_OPTIONS='detect_leaks=0',
        UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
