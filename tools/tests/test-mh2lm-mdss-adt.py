#!/usr/bin/env python3
"""Check F1a resource translation and preservation of the baseline ADT."""
from pathlib import Path
import importlib.util
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import adt
spec = importlib.util.spec_from_file_location('mdss_adt', Path(__file__).resolve().parents[1]/'mk-mh2lm-mdss-adt.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, name.encode()+b'\0')]+[(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


ranges = struct.pack('<QQQ', 0, m.SOC_BASE, 0x100000000-m.SOC_BASE)
root = node('device-tree', [('compatible', b'D22AP\0lge,mh2lm\0')], [
    node('chosen', [('random-seed', bytes(range(64)))]),
    node('arm-io', [('ranges', ranges)], [
        node('gic', [('reg', struct.pack('<QQ', 0x17a00000-m.SOC_BASE, 0x10000))]),
        node('disp0', [('device_type', b'display-subsystem\0'),
                       ('canvas-width', struct.pack('<I', 1125))])])])
blob = adt.serialize(root)
result = m.derive(blob)
r, end = adt.parse(result)
assert end == len(result)
arm = r.children[1]
mdss = arm.children.pop()
baseline_regs = mdss.prop('reg')
assert mdss.name == 'mdss' and mdss.prop('device_type') is None
assert mdss.prop('interrupts') is None and mdss.prop('sweet,mdss-intid') == struct.pack('<I', 115)
assert arm.prop('ranges')[:len(ranges)] == ranges
translations = list(struct.iter_unpack('<QQQ', arm.prop('ranges')))
for (addr, size), (expected, length) in zip(struct.iter_unpack('<QQ', mdss.prop('reg')), m.WINDOWS):
    hits = [parent+addr-child for child, parent, n in translations if child <= addr and addr+size <= child+n]
    assert hits == [expected] and size == length
# Reverting only the intended range extension and node reproduces every byte.
arm.props = [(k, f, ranges if k == 'ranges' else v) for k, f, v in arm.props]
assert adt.serialize(r) == blob
for bad in [result, blob+b'bad', blob.replace(b'lge,mh2lm', b'bad,mh2lm')]:
    try:
        m.derive(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('accepted incompatible or already extended input')
print('PASS: all nine physical windows, disjoint low GCC alias, unchanged existing tree, no premature IRQ/display child, invalid input rejection')

# The F3 extension appends resources without changing the old nine or identity.
extended = m.derive(blob, irq_preflight=True)
r, end = adt.parse(extended)
assert end == len(extended)
arm = r.children[1]
mdss = arm.children[-1]
assert mdss.prop('reg')[:9*16] == baseline_regs
windows = list(struct.iter_unpack('<QQ', mdss.prop('reg')))
assert len(windows) == 12
translations = list(struct.iter_unpack('<QQQ', arm.prop('ranges')))
for (addr, size), (expected, length) in zip(windows, m.WINDOWS+m.IRQ_WINDOWS):
    hits = [parent+addr-child for child, parent, n in translations if child <= addr and addr+size <= child+n]
    assert hits == [expected] and size == length
assert mdss.prop('interrupts') is None and mdss.prop('interrupt-parent') is None
mdss.props = [(k, f, v[:9*16] if k == 'reg' else v) for k, f, v in mdss.props]
assert adt.serialize(r) == result
print('PASS: optional IRQ resources append-only, all twelve translations, unchanged default ADT, no IRQ registration')

# v1 resource ABI preserves all indices; expands the sole DISPCC map and
# appends a single PHY window containing PLL. RPMh is a distinct real nub.
full = m.derive(blob, display_resources=True)
assert full == m.derive(blob, irq_preflight=True, display_resources=True)
r, end = adt.parse(full)
assert end == len(full)
arm = r.children[1]
mdss, rpmh = arm.children[-2:]
assert mdss.prop('sweet,display-resource-abi') == struct.pack('<I', 1)
assert rpmh.name == 'display-rpmh' and rpmh.prop('compatible') == b'sweet,rpmh\0'
assert rpmh.prop('sweet,rpmh-abi') == struct.pack('<I', 1)
expected = list(m.WINDOWS)+list(m.IRQ_WINDOWS)+[m.PHY_WINDOW]
expected[1] = (0x0af00000, 0x20000)
translations = list(struct.iter_unpack('<QQQ', arm.prop('ranges')))
for child, wanted in [(mdss, expected), (rpmh, m.RPMH_WINDOWS)]:
    windows = list(struct.iter_unpack('<QQ', child.prop('reg')))
    assert len(windows) == len(wanted)
    for (address, size), (pa, length) in zip(windows, wanted):
        hits = [parent+address-base for base, parent, n in translations
                if base <= address and address+size <= base+n]
        assert hits == [pa] and size == length
    physical = list(wanted)
    for i, (pa, size) in enumerate(physical):
        assert all(pa+size <= q or q+n <= pa for q, n in physical[i+1:])
    assert child.prop('interrupts') is None
# Regenerate the old IRQ node by undoing only v1's documented changes.
arm.children.pop()
mdss.props = [(k, f, (v[:16]+struct.pack('<QQ', 0x0af00000-m.SOC_BASE, 0x3000)+v[32:12*16])
              if k == 'reg' else v) for k, f, v in mdss.props if k != 'sweet,display-resource-abi']
assert adt.serialize(r) == extended
try:
    m.derive(full, display_resources=True)
except ValueError:
    pass
else:
    raise AssertionError('accepted duplicate full resources')
print('PASS: v1 thirteen MDSS windows, disjoint PHY/PLL ownership, separate RPMh ABI/nub, default and IRQ byte preservation')
