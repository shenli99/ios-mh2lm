#!/usr/bin/env python3
"""Execute Lineage's actual v2 timer/init functions and compare all 65 writes.
Usage: audit-mh2lm-lineage-rsc.py EXTRACTED_XNU LINEAGE_KERNEL
The reference kernel is read locally; no reference source is vendored.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 3:
    sys.exit(__doc__)
xnu, lineage = map(Path, sys.argv[1:])
root = lineage / 'drivers/gpu/drm/msm'
hw = (root / 'sde_rsc_hw.c').read_text()
driver = (root / 'sde_rsc.c').read_text()
header = (root / 'sde_rsc_hw.h').read_text()


def function(source, name):
    match = re.search(r'(?:static )?(?:int|u32) ' + name + r'\([^;]+?\)\s*\{', source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


# Keep the actual default/clamp/division logic and probe's v2 latency inputs.
timer = function(driver, 'sde_rsc_timer_calculate')
probe = driver[driver.index('\tif (rsc->version == SDE_RSC_REV_2)', driver.index('platform_set_drvdata')):]
probe = probe[:probe.index('\n\tif (rsc->version == SDE_RSC_REV_3)')]
latencies = re.search(r'rsc->time_slot_0_ns = \(rsc->single_tcs_execution_time \* 2\).*?RSC_MODE_THRESHOLD_OVERHEAD;', driver, re.S)[0]
definitions = '\n'.join(line for line in (header + driver).splitlines()
                        if re.match(r'#define\s+(SDE_RSCC_|SDE_RSC_SOLVER_|SDE_RSC_TIMERS_|SINGLE_TCS_|RSC_MODE_|DEFAULT_PANEL_|TICKS_IN_|MIN_THRESHOLD_)', line))
fields = sorted(set(re.findall(r'timer_config\.(\w+)', timer)))
functions = ['rsc_hw_qtimer_init', 'rsc_hw_wrapper_init', 'rsc_hw_seq_memory_init_v2',
             'rsc_hw_solver_init', 'rsc_hw_pdc_init']
source = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define BIT(n) (1u << (n))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define SDE_RSC_REV_2 2
#define SDE_RSC_REV_3 3
enum sde_rsc_state { SDE_RSC_IDLE_STATE, SDE_RSC_CMD_STATE };
static u64 div_u64(u64 a, u32 b) { return a / b; }
struct sde_rsc_cmd_config { u32 fps, jitter_numer, jitter_denom, vtotal, prefill_lines; };
struct sde_rsc_priv {
 u32 version, debug_mode, wrapper_io, drv_io, single_tcs_execution_time;
 u32 time_slot_0_ns, backoff_time_ns, mode_threshold_time_ns;
 struct sde_rsc_cmd_config cmd_config;
 struct { TIMER_FIELDS } timer_config;
 struct { int (*init)(struct sde_rsc_priv *); int (*timer_update)(struct sde_rsc_priv *); } hw_ops;
};
static void dss_reg_w(u32 *io, u32 offset, u32 value, u32 debug) {
 (void)debug; printf("%u %x %x\n", *io, offset, value);
}
'''.replace('TIMER_FIELDS', ''.join('u32 ' + field + ';' for field in fields))
source += definitions + '\n' + timer + '\n'
source += '\n'.join(function(hw, name) for name in functions)
source += '\nint main(void) { struct sde_rsc_priv data = {0}, *rsc = &data;\n'
source += 'rsc->version = 2; rsc->wrapper_io = 1;\n' + probe + '\n' + latencies
source += '\nsde_rsc_timer_calculate(rsc, NULL, SDE_RSC_IDLE_STATE);\n'
source += '\n'.join(name + '(rsc);' for name in functions) + '\nreturn 0; }\n'
with tempfile.TemporaryDirectory(prefix='lineage-rsc-audit-') as temp:
    cfile, binary = Path(temp) / 'reference.c', Path(temp) / 'reference'
    cfile.write_text(source)
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', '-fsanitize=address,undefined',
                    str(cfile), '-o', str(binary)], check=True)
    emitted = subprocess.check_output([str(binary)], text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
reference = [tuple(int(value, 16) for value in row.split()) for row in emitted.splitlines()]
production = (xnu / 'iokit/Kernel/arm/SweetMDSSNoC.inc').read_text()
table = production.split('mdssRSCV2Init[]={', 1)[1].split('};', 1)[0]
actual = [tuple(int(v.rstrip('u'), 0) for v in row) for row in
          re.findall(r'\{(\d+),(0x[0-9a-f]+),(0x[0-9a-f]+u)\}', table)]
assert len(reference) == 65 and reference == actual, (reference, actual)
print('PASS: actual Lineage v2 probe inputs, timer defaults/clamp/divisions and five init functions emit exactly the same 65 ordered register writes')
