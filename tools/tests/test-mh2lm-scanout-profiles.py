#!/usr/bin/env python3
"""Check the real packager's F4/F3 argument branches without creating boots.
Optionally pass an old kernel artifact to exercise F4 marker rejection too.
"""
from pathlib import Path
import shlex
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
script = repo/'tools/mk-mh2lm-event-matrix.sh'
s = script.read_text()
start = s.index('if [ "$KEY_IRQ_PROFILE" = \'--framebuffer-service\' ]; then\n FB_ARGS=')
end = s.index('\nif [ "$KEY_IRQ_PROFILE" = \'--framebuffer-contract\' ]', start)
parse_start = s.index('KEY_IRQ_PROFILE=${3:-}')
parse_end = s.index('\ncase "$LABEL" in', parse_start)

def profiles(option):
    body = '''set -euo pipefail
LABEL=test
KERNEL_SHA256=test
make_profile() { printf '%s\\t%s\\n' "$1" "$3"; }
'''+s[parse_start:parse_end]+'\n'+s[start:end]
    r = subprocess.run(['bash', '-c', body, 'test', 'unused', 'test', option], capture_output=True, text=True, check=True)
    result = {}
    for line in r.stdout.splitlines():
        if '\t' not in line:
            continue
        name, args = line.split('\t', 1)
        pairs = [item.split('=', 1) for item in shlex.split(args)]
        d = dict(pairs)
        assert len(d) == len(pairs), (name, 'duplicate args')
        for k, v in {'sweetmdss':'1', 'sweetfbservice':'1', 'sweetfbpower':'1',
                     'sweetusb':'2', 'sweetkeys':'1', 'sweetbattery':'1',
                     'sweetbattirq':'4', 'sweetbattruntime':'3', 'sweetfbcontract':'0',
                     'sweetfbproof':'0', 'sweetloglevel':'2', 'sweetmdssirqprobe':'0'}.items():
            assert d[k] == v, (name, k)
        result[name] = d
    return result

f4 = profiles('--framebuffer-scanout')
assert set(f4) == {'fb-scanout-baseline', 'fb-scanout-preflight'}
assert f4['fb-scanout-baseline'].pop('sweetfbown') == '0'
assert f4['fb-scanout-preflight'].pop('sweetfbown') == '1'
assert f4['fb-scanout-baseline'] == f4['fb-scanout-preflight']
assert f4['fb-scanout-baseline']['sweetmdssirq'] == '2'
assert f4['fb-scanout-baseline']['sweetfbvsync'] == '1'
handoff = profiles('--framebuffer-handoff')
assert set(handoff) == {'fb-scanout-baseline','fb-scanout-preflight','fb-scanout-single'}
for name, own in [('fb-scanout-baseline','0'),('fb-scanout-preflight','1'),('fb-scanout-single','2')]:
    assert handoff[name].pop('sweetfbown') == own
    assert handoff[name] == f4['fb-scanout-baseline']
f3 = profiles('--framebuffer-vsync')
assert set(f3) == {'fb-vsync-baseline','fb-vsync-gic','fb-vsync-rdptr','fb-vsync-hardware'}
for name, irq, vsync in [('fb-vsync-baseline','0','0'), ('fb-vsync-gic','1','0'),
                         ('fb-vsync-rdptr','2','0'), ('fb-vsync-hardware','2','1')]:
    assert f3[name]['sweetmdssirq'] == irq and f3[name]['sweetfbvsync'] == vsync
assert f3['fb-vsync-hardware'] == f4['fb-scanout-baseline']
subprocess.run(['bash', '-n', str(script)], check=True)
if len(sys.argv) > 1:
    r = subprocess.run(['bash', str(script), sys.argv[1], 'f4-reject-old', '--framebuffer-scanout'],
                       cwd=repo, capture_output=True, text=True)
    assert r.returncode != 0 and 'kernel lacks F4 scanout prerequisite marker' in r.stderr, r
    print('PASS: actual pre-F4 kernel rejected before make_profile; no test boot created')
if len(sys.argv) > 2:
    r = subprocess.run(['bash', str(script), sys.argv[2], 'f4-reject-no-handoff', '--framebuffer-handoff'],
                       cwd=repo, capture_output=True, text=True)
    assert r.returncode != 0 and 'kernel lacks F4 controlled handoff marker' in r.stderr, r
    print('PASS: actual 0181 kernel rejected for own2 before make_profile; no test boot created')
print('PASS: real F4 scanout own0/1 and handoff own0/1/2 branches differ only own; F3 hardware baseline and IRQ/ACM/runtime3/F2 arguments preserved; shell syntax valid')
