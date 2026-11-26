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
        node('gic', [('reg', struct.pack('<QQ', 0x17a00000-m.SOC_BASE, 0x10000)),
                     ('AAPL,phandle', struct.pack('<I', 0x0d))]),
        node('disp0', [('device_type', b'display-subsystem\0'),
                       ('canvas-width', struct.pack('<I', 1125))])])])
blob = adt.serialize(root)
result = m.derive(blob)
r, end = adt.parse(result)
assert end == len(result)
arm = r.children[1]
mdss = next(n for n in arm.children if n.name == 'mdss')
# The pad controller node rides along; it must not perturb the display one.
tlmm = next(n for n in arm.children if n.name == 'tlmm')
assert tlmm.prop('compatible') == b'qcom,tlmm-sm8150\0'
assert tlmm.prop('qcom,pin-count') == struct.pack('<I', 175)
assert len(tlmm.prop('reg')) == 8 * 8
baseline_regs = mdss.prop('reg')
assert mdss.name == 'mdss' and mdss.prop('device_type') is None
assert mdss.prop('interrupts') is None and mdss.prop('sweet,mdss-intid') == struct.pack('<I', 115)
assert arm.prop('ranges')[:len(ranges)] == ranges
translations = list(struct.iter_unpack('<QQQ', arm.prop('ranges')))
for (addr, size), (expected, length) in zip(struct.iter_unpack('<QQ', mdss.prop('reg')), m.WINDOWS):
    hits = [parent+addr-child for child, parent, n in translations if child <= addr and addr+size <= child+n]
    assert hits == [expected] and size == length
# Reverting only the intended range extension and the two added nodes
# reproduces every byte of the input tree.
arm.props = [(k, f, ranges if k == 'ranges' else v) for k, f, v in arm.props]
arm.children = [n for n in arm.children if n.name not in ('mdss', 'tlmm')]
# The GIC personality metadata is also added when missing; drop it too.
gic = next(n for n in arm.children if n.name == 'gic')
gic.props = [(k, f, v) for k, f, v in gic.props if k != '#interrupt-cells']
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

# NoC extension belongs to MDSS; the apps RPMh nub is byte-identical to v1.
noc = m.derive(blob, display_noc=True)
r, end = adt.parse(noc)
assert end == len(noc)
arm = r.children[1]
mdss, rpmh = arm.children[-2:]
assert mdss.prop('sweet,display-noc-abi') == struct.pack('<I', 1)
assert mdss.prop('sweet,display-noc-irq-abi') == struct.pack('<I', 1)
assert mdss.prop('sweet,display-noc-intid') == struct.pack('<I', 161)
assert len(mdss.prop('reg')) == 16*16
translations = list(struct.iter_unpack('<QQQ', arm.prop('ranges')))
all_windows = expected+list(m.NOC_WINDOWS)
for (address, size), (pa, length) in zip(struct.iter_unpack('<QQ', mdss.prop('reg')), all_windows):
    hits = [parent+address-base for base, parent, n in translations if base <= address and address+size <= base+n]
    assert hits == [pa] and size == length
for i, (pa, size) in enumerate(all_windows):
    assert all(pa+size <= q or q+n <= pa for q, n in all_windows[i+1:])
assert list(struct.iter_unpack('<QQ', rpmh.prop('reg'))) == [
    (pa-m.SOC_BASE if pa >= m.SOC_BASE else m.LOW_ALIAS+pa, length) for pa, length in m.RPMH_WINDOWS]
mdss.props = [(k, f, v[:13*16] if k == 'reg' else v) for k, f, v in mdss.props if k not in ('sweet,display-noc-abi', 'sweet,display-noc-irq-abi', 'sweet,display-noc-intid')]
assert adt.serialize(r) == full
print('PASS: sixteen disjoint MDSS windows, display RSC/TCS/wrapper split, unchanged v1 apps nub and removable NoC extension')

# Panel pins are a separate restricted provider. The pads come from the TLMM
# controller's exclusive handles, so this node must not carry tile windows: a
# second mapper would be a second writer.
for with_noc in (False, True):
    pins_blob = m.derive(blob, display_noc=with_noc, panel_pins=True)
    r, end = adt.parse(pins_blob)
    assert end == len(pins_blob)
    arm = r.children[1]
    pins = arm.children.pop()
    assert pins.name == 'panel-pins'
    assert pins.prop('compatible') == b'sweet,panel-pins\0'
    assert pins.prop('sweet,panel-pins-abi') == struct.pack('<I', 1)
    assert pins.prop('reg') is None
    assert pins.prop('interrupts') is None and pins.prop('interrupt-parent') is None
    assert pins.prop('sweet,panel-profile') is None
    assert adt.serialize(r) == (noc if with_noc else full)
    # Reject an existing pins nub even when it is the only extension present.
    original, _ = adt.parse(blob)
    original.children[1].children.append(pins)
    try:
        m.derive(adt.serialize(original), panel_pins=True)
    except ValueError:
        pass
    else:
        raise AssertionError('accepted duplicate panel pin provider')
print('PASS: panel pin provider ABI with no tile windows (pads owned through TLMM), separate MDSS/RPMh maps, no identity/IRQ claims, NoC composition and byte-identical legacy ADTs')

# Touch coordination owns no new GPIO/GIC mapping: the sole legacy worker
# remains the hardware writer. Older outputs and pin service stay byte-identical.
for with_pins in (False, True):
    for with_noc in (False, True):
        touch_blob = m.derive(blob, display_noc=with_noc, panel_pins=with_pins, panel_touch=True)
        r, end = adt.parse(touch_blob)
        assert end == len(touch_blob)
        touch = r.children[1].children.pop()
        assert touch.name == 'panel-touch' and touch.prop('compatible') == b'sweet,touch-panel\0'
        assert touch.prop('sweet,touch-panel-abi') == struct.pack('<I', 1)
        assert touch.prop('reg') is None
        # The facade registers SPI 240 through the nub, so the node carries the
        # standard specifier and the GIC as its interrupt parent.
        assert touch.prop('interrupt-parent') == struct.pack('<I', 0x0d)
        assert touch.prop('interrupts') == struct.pack('<III', 240, 0, 4)
        assert adt.serialize(r) == m.derive(blob, display_resources=True,
                                          display_noc=with_noc, panel_pins=with_pins)
        original, _ = adt.parse(blob)
        original.children[1].children.append(touch)
        try:
            m.derive(adt.serialize(original), panel_touch=True)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted duplicate touch coordination service')
print('PASS: optional touch coordination nub with its SPI 240 specifier, no second GPIO writer, pins/NoC composition, old ABI bytes preserved and duplicate rejection')

# Exercise the user-facing CLI, including simultaneous pins/touch/NoC options.
import subprocess
import tempfile
with tempfile.TemporaryDirectory(prefix='mh2lm-panel-adt-') as temp:
    source = Path(temp)/'base.adt'
    output = Path(temp)/'panel.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--panel-touch', '--panel-pins',
                    '--display-noc', str(source), str(output)], check=True)
    assert source.read_bytes() == blob
    assert output.read_bytes() == m.derive(blob, display_noc=True, panel_pins=True, panel_touch=True)
print('PASS: combined panel-touch/panel-pins/display-noc CLI output equals validated derivation; baseline file unchanged')

# The opt-in identity slot has no revision or fabricated source evidence.
for pins in (False, True):
    for touch in (False, True):
        for noc in (False, True):
            result = m.derive(blob, display_noc=noc, panel_pins=pins, panel_touch=touch, panel_identity=True)
            r, end = adt.parse(result)
            assert end == len(result)
            mdss = next(n for n in r.children[1].children if n.name == 'mdss')
            assert mdss.prop('sweet,panel-identity') == bytes(96)
            mdss.props = [(k, f, v) for k, f, v in mdss.props if k != 'sweet,panel-identity']
            assert adt.serialize(r) == m.derive(blob, display_resources=True, display_noc=noc, panel_pins=pins, panel_touch=touch)
with tempfile.TemporaryDirectory(prefix='mh2lm-identity-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'panel.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--panel-touch', '--panel-pins',
                    '--display-noc', '--panel-identity', str(source), str(output)], check=True)
    assert output.read_bytes() == m.derive(blob, display_noc=True, panel_pins=True, panel_touch=True, panel_identity=True)
    assert source.read_bytes() == blob
print('PASS: zeroed 96-byte live-ABL identity slot, all pins/touch/NoC compositions, unchanged old ADTs and CLI baseline')

# 0208 opts in to a separate append-only apps completion IRQ extension.
# Existing ADTs/metadata-only identity stay byte-identical; SPI ownership is
# per INTID, not exclusive ownership of the whole shared GIC map.
for kwargs in ({}, dict(display_noc=True, panel_pins=True, panel_touch=True, panel_identity=True)):
    old = m.derive(blob, **kwargs)
    extended_irq = m.derive(blob, rpmh_irq=True, **kwargs)
    root_irq, end = adt.parse(extended_irq)
    assert end == len(extended_irq)
    rpmh = next(n for n in root_irq.children[1].children if n.name == 'display-rpmh')
    assert rpmh.prop('sweet,rpmh-abi') == struct.pack('<I', 1)
    assert rpmh.prop('sweet,rpmh-irq-abi') == struct.pack('<I', 1)
    assert rpmh.prop('sweet,rpmh-intid') == struct.pack('<I', 37)
    assert rpmh.prop('interrupts') is None and rpmh.prop('interrupt-parent') is None
    assert len(rpmh.prop('reg')) == 4*16
    assert struct.unpack('<QQ', rpmh.prop('reg')[-16:]) == (0x17a00000-m.SOC_BASE, 0x10000)
    rpmh.props = [(k, f, v[:3*16] if k == 'reg' else v) for k, f, v in rpmh.props
                  if k not in ('sweet,rpmh-irq-abi', 'sweet,rpmh-intid')]
    assert adt.serialize(root_irq) == m.derive(blob, display_resources=True, **kwargs)
print('PASS: opt-in apps RSC INTID37 IRQ ABI, append-only GIC resource, exact legacy ADT restoration, no standard interrupt registration')
with tempfile.TemporaryDirectory(prefix='mh2lm-rpmh-irq-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'panel.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--display-noc', '--panel-pins',
                    '--panel-touch', '--panel-identity', '--rpmh-irq', str(source), str(output)], check=True)
    assert output.read_bytes() == m.derive(blob, display_noc=True, panel_pins=True,
                                          panel_touch=True, panel_identity=True, rpmh_irq=True)
    assert source.read_bytes() == blob
print('PASS: lifecycle hardware CLI declares completion IRQ; identity/default ADT bytes unchanged')

# Pipeline ABI appends exactly three narrow windows; removing it restores the
# complete old ADT including pins, touch, live identity and apps IRQ metadata.
for pins in (False, True):
    for touch in (False, True):
        for irq in (False, True):
            opts = dict(display_noc=True, panel_pins=pins, panel_touch=touch,
                        panel_identity=True, rpmh_irq=irq)
            extended = m.derive(blob, panel_pipeline=True, **opts)
            tree, end = adt.parse(extended)
            assert end == len(extended)
            arm = tree.children[1]
            mdss = next(n for n in arm.children if n.name == 'mdss')
            assert mdss.prop('sweet,display-pipeline-abi') == struct.pack('<I', 1)
            windows = list(struct.iter_unpack('<QQ', mdss.prop('reg')))
            assert len(windows) == 19
            assert windows[16:] == [(pa-m.SOC_BASE, size) for pa, size in
                                    ((0x0ae45000, 0x320), (0x0ae71000, 0xd4), (0x0ae06a00, 4))]
            for a, n in windows[16:]:
                for b, count in windows:
                    if a != b:
                        assert a+n <= b or b+count <= a
            mdss.props = [(k, f, v[:16*16] if k == 'reg' else v) for k, f, v in mdss.props
                          if k != 'sweet,display-pipeline-abi']
            assert adt.serialize(tree) == m.derive(blob, **opts)
with tempfile.TemporaryDirectory(prefix='mh2lm-pipeline-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'pipeline.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--panel-pipeline',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == m.derive(blob, display_noc=True, panel_pipeline=True)
    assert source.read_bytes() == blob
print('PASS: mode7 LM0/PP0/CSC10 narrow maps, append-only ABI, all pins/touch/IRQ compositions, exact old ADT restoration and CLI')

# C1 clock entry: one opt-in node, no window and no child, and removing it
# reproduces every byte of the same ADT built without the option.
for opts in (dict(), dict(display_noc=True, panel_pins=True, panel_touch=True,
                          panel_identity=True, rpmh_irq=True, panel_pipeline=True)):
    plain = m.derive(blob, **opts)
    clocked = m.derive(blob, clock_provider=True, **opts)
    tree, end = adt.parse(clocked)
    assert end == len(clocked)
    arm = tree.children[1]
    clock = next(n for n in arm.children if n.name == 'clock')
    assert clock.prop('compatible') == b'sweet,clock-provider\0'
    assert clock.prop('sweet,clock-abi') == struct.pack('<I', 1)
    assert clock.prop('sweet,clock-diag') is None
    assert clock.prop('reg') is None and clock.children == []
    arm.children = [n for n in arm.children if n.name != 'clock']
    assert adt.serialize(tree) == plain
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'clock.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-provider',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == m.derive(blob, clock_provider=True)
    assert source.read_bytes() == blob
# The diagnostic opt-in lives on the node: the firmware rejects a boot-argument
# list past 607 bytes, so the tree is the only request channel. Dropping the
# property must reproduce the plain clock entry byte for byte.
diagged = m.derive(blob, clock_diag=True)
tree, end = adt.parse(diagged)
assert end == len(diagged)
arm = tree.children[1]
clock = next(n for n in arm.children if n.name == 'clock')
assert clock.prop('sweet,clock-diag') == struct.pack('<I', 1)
assert clock.prop('reg') is None and clock.children == []
clock.props = [(k, f, v) for k, f, v in clock.props if k != 'sweet,clock-diag']
assert adt.serialize(tree) == m.derive(blob, clock_provider=True)
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-diag-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'diag.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-diag',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == diagged
    assert source.read_bytes() == blob
print('PASS: C1 clock entry is opt-in, windowless, byte-restorable and CLI-selectable, '
      'diagnostic request on the node')

# C2 asks for the four GCC windows on a dedicated node. The 0x10b000 page sits
# below SOC_BASE, so its cell must use the low alias the display node already
# relies on; dropping the node has to restore the C1 bytes exactly.
for opts in (dict(), dict(display_noc=True, panel_pins=True, panel_touch=True,
                          panel_identity=True, rpmh_irq=True, panel_pipeline=True),
             dict(clock_diag=True)):
    clocked = m.derive(blob, clock_provider=True, **opts)
    gcced = m.derive(blob, gcc=True, **opts)
    tree, end = adt.parse(gcced)
    assert end == len(gcced)
    arm = tree.children[1]
    gcc = next(n for n in arm.children if n.name == 'gcc')
    assert gcc.prop('compatible') == b'sweet,gcc\0'
    assert gcc.prop('sweet,gcc-abi') == struct.pack('<I', 1)
    assert gcc.prop('sweet,gcc-variant') == struct.pack('<I', 0x8150)
    assert gcc.children == []
    reg = struct.unpack('<%dQ' % (len(gcc.prop('reg')) // 8), gcc.prop('reg'))
    assert len(reg) == 2 * len(m.GCC_WINDOWS)
    windows = []
    for i in range(0, len(reg), 2):
        addr, size = reg[i], reg[i + 1]
        windows.append((addr + m.SOC_BASE if addr < m.LOW_ALIAS else addr - m.LOW_ALIAS,
                        size))
    assert tuple(windows) == m.GCC_WINDOWS
    assert arm.prop('ranges') == (struct.pack('<QQQ', 0, m.SOC_BASE, 0x100000000 - m.SOC_BASE) +
                                  struct.pack('<QQQ', m.LOW_ALIAS, 0, m.SOC_BASE))
    order = [n.name for n in arm.children]
    assert order.index('clock') + 1 == order.index('gcc')
    arm.children = [n for n in arm.children if n.name != 'gcc']
    assert adt.serialize(tree) == clocked
    if not opts.get('clock_diag'):	# the clock entry is part of the plain tree
        arm.children = [n for n in arm.children if n.name != 'clock']
        assert adt.serialize(tree) == m.derive(blob, **opts)
# The window request alone is enough: asking for GCC asks for the entry too.
assert m.derive(blob, gcc=True) == m.derive(blob, clock_provider=True, gcc=True)
assert m.derive(blob, gcc=True) != m.derive(blob)
assert not any(n.name == 'gcc' for n in adt.parse(m.derive(blob))[0].children[1].children)
assert not any(n.name == 'gcc' for n in adt.parse(m.derive(blob, clock_provider=True))[0].children[1].children)
with tempfile.TemporaryDirectory(prefix='mh2lm-gcc-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'gcc.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--gcc',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == m.derive(blob, gcc=True)
    assert source.read_bytes() == blob
    both, diagged = Path(temp)/'both.adt', m.derive(blob, clock_diag=True)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--gcc', '--clock-diag',
                    str(source), str(both)], check=True)
    assert both.read_bytes() == m.derive(blob, gcc=True, clock_diag=True)
    tree, end = adt.parse(both.read_bytes())
    assert end == len(both.read_bytes())
    arm = tree.children[1]
    clock = next(n for n in arm.children if n.name == 'clock')
    assert clock.prop('sweet,clock-diag') == struct.pack('<I', 1)
    assert diagged != both.read_bytes()	# the diag bit and the node are separable
# C2-08 writes hardware for the first time, so the measurement request is a
# property on the GCC node: absent from every earlier image, removable byte for
# byte, and never implied by the window request alone.
measured = m.derive(blob, clock_measure=True)
assert measured == m.derive(blob, gcc=True, clock_measure=True)
assert measured != m.derive(blob, gcc=True)
tree, end = adt.parse(measured)
assert end == len(measured)
arm = tree.children[1]
gcc = next(n for n in arm.children if n.name == 'gcc')
assert gcc.prop('sweet,clock-measure') == struct.pack('<I', 1)
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-measure']
assert adt.serialize(tree) == m.derive(blob, gcc=True)
assert b'sweet,clock-measure' not in m.derive(blob, gcc=True)
assert b'sweet,clock-measure' not in m.derive(blob, clock_provider=True)
assert b'sweet,clock-measure' not in blob
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-measure-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'measure.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-measure',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == measured
    assert source.read_bytes() == blob
# C2-03 turns a branch on, so it gets its own property beside the measurement's:
# each is removable alone, the combined tree is the measurement tree plus the
# gate, and neither is reachable from a window request.
gated = m.derive(blob, clock_gate=True)
assert gated == m.derive(blob, gcc=True, clock_gate=True)
assert gated != m.derive(blob, gcc=True)
assert gated != measured
tree, end = adt.parse(gated)
assert end == len(gated)
arm = tree.children[1]
gcc = next(n for n in arm.children if n.name == 'gcc')
assert gcc.prop('sweet,clock-gate') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-measure') is None
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-gate']
assert adt.serialize(tree) == m.derive(blob, gcc=True)
assert b'sweet,clock-gate' not in m.derive(blob, gcc=True)
assert b'sweet,clock-gate' not in m.derive(blob, clock_measure=True)
assert b'sweet,clock-gate' not in blob
both_gates = m.derive(blob, clock_measure=True, clock_gate=True)
tree, end = adt.parse(both_gates)
assert end == len(both_gates)
gcc = next(n for n in tree.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-gate') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-measure') == struct.pack('<I', 1)
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-gate']
assert adt.serialize(tree) == measured, 'the combined tree must be the measurement tree plus the gate'
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-gate-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'gate.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-gate',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == gated
    assert source.read_bytes() == blob
# C2-04 retracts the C2-03 enable, so its property is the gate's dependent: the
# generator adds the gate when it is asked for a disable, and a tree with the
# disable alone is unreachable from every entry point.
disabled = m.derive(blob, clock_disable=True)
assert disabled == m.derive(blob, clock_gate=True, clock_disable=True)
assert disabled == m.derive(blob, gcc=True, clock_disable=True)
assert disabled != gated
tree, end = adt.parse(disabled)
assert end == len(disabled)
gcc = next(n for n in tree.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-disable') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-gate') == struct.pack('<I', 1)
withdrawn = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-disable']
gcc.props = withdrawn
assert adt.serialize(tree) == gated, 'the disable is the gate tree plus one property'
assert b'sweet,clock-disable' not in m.derive(blob, clock_gate=True)
assert b'sweet,clock-disable' not in m.derive(blob, clock_measure=True, clock_gate=True)
assert b'sweet,clock-disable' not in blob
assert b'sweet,clock-gate' in m.derive(blob, clock_disable=True), 'a disable alone is refused, so it implies the gate'
all_three = m.derive(blob, clock_measure=True, clock_gate=True, clock_disable=True)
tree, end = adt.parse(all_three)
assert end == len(all_three)
gcc = next(n for n in tree.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-measure') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-gate') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-disable') == struct.pack('<I', 1)
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-disable']
assert adt.serialize(tree) == both_gates, 'the three opt-ins are additive'
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-disable-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'disable.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-disable',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == disabled
    assert source.read_bytes() == blob
# C2-05 shares the demand the gate opened and ends it through the disable, so it is
# the dependent of both: every entry point that can build a share tree also builds
# the two cards below it, and the share is the top of this chain.
shared = m.derive(blob, clock_share=True)
assert shared == m.derive(blob, clock_gate=True, clock_disable=True, clock_share=True)
assert shared == m.derive(blob, clock_disable=True, clock_share=True)
assert shared == m.derive(blob, gcc=True, clock_share=True)
assert shared != disabled
nested, end = adt.parse(shared)
assert end == len(shared)
gcc = next(n for n in nested.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-share') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-disable') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-gate') == struct.pack('<I', 1)
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-share']
assert adt.serialize(nested) == disabled, 'the share is the disable tree plus one property'
assert b'sweet,clock-share' not in m.derive(blob, clock_gate=True, clock_disable=True)
assert b'sweet,clock-share' not in m.derive(blob, clock_measure=True, clock_disable=True)
assert b'sweet,clock-share' not in blob
all_four = m.derive(blob, clock_measure=True, clock_share=True)
nested, end = adt.parse(all_four)
assert end == len(all_four)
gcc = next(n for n in nested.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-share') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-disable') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-gate') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-measure') == struct.pack('<I', 1)
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-share']
assert adt.serialize(nested) == all_three, 'the share rides on top of the three opt-ins'
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-share-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'share.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-share',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == shared
    assert source.read_bytes() == blob
# C2-06 reads one more page than the ABI carries, so its opt-in owns both the
# property and the fifth range: the generator emits them as one step, and a tree
# with the property but the four-range reg list is unreachable.
def encode_windows(windows):
    # the generator's rule, restated here so the tree can be rebuilt by hand
    return b''.join(struct.pack('<QQ', pa - m.SOC_BASE if pa >= m.SOC_BASE
                                else m.LOW_ALIAS + pa, size) for pa, size in windows)


probed = m.derive(blob, clock_vote_probe=True)
assert probed == m.derive(blob, gcc=True, clock_vote_probe=True)
assert probed != m.derive(blob, gcc=True)
assert b'sweet,clock-vote-probe' not in m.derive(blob, gcc=True)
assert b'sweet,clock-vote-probe' not in blob
ptree, end = adt.parse(probed)
assert end == len(probed)
gcc = next(n for n in ptree.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-vote-probe') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-gate') is None	# the probe writes nothing
assert gcc.prop('sweet,clock-measure') is None
assert gcc.prop('sweet,clock-disable') is None
preg = struct.unpack('<%dQ' % (len(gcc.prop('reg')) // 8), gcc.prop('reg'))
assert len(preg) == 2 * (len(m.GCC_WINDOWS) + len(m.GCC_PROBE_WINDOW))
pwindows = []
for i in range(0, len(preg), 2):
    addr, size = preg[i], preg[i + 1]
    pwindows.append((addr + m.SOC_BASE if addr < m.LOW_ALIAS else addr - m.LOW_ALIAS,
                     size))
assert tuple(pwindows) == m.GCC_WINDOWS + m.GCC_PROBE_WINDOW
# dropping the page and the property together is the plain GCC tree again
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-vote-probe']
gcc.props = [(k, f, encode_windows(m.GCC_WINDOWS) if k == 'reg' else v)
             for k, f, v in gcc.props]
assert adt.serialize(ptree) == m.derive(blob, gcc=True)
# the probe composes with the write cards: it is one more property on top of
# whatever the gate/share chain already emitted, and the ranges stay five.
combo = m.derive(blob, clock_vote_probe=True, clock_share=True)
ctree, end = adt.parse(combo)
assert end == len(combo)
gcc = next(n for n in ctree.children[1].children if n.name == 'gcc')
for name in ('sweet,clock-vote-probe', 'sweet,clock-share', 'sweet,clock-disable',
             'sweet,clock-gate'):
    assert gcc.prop(name) == struct.pack('<I', 1), name
assert len(gcc.prop('reg')) == 8 * 2 * (len(m.GCC_WINDOWS) + len(m.GCC_PROBE_WINDOW))
# the write half is its own property and implies the probe: a vote alone would be
# a tree the backend refuses, so the generator will not emit one.
voted = m.derive(blob, clock_vote=True)
assert voted == m.derive(blob, clock_vote_probe=True, clock_vote=True)
assert voted != probed
assert b'sweet,clock-vote\0' not in m.derive(blob, clock_vote_probe=True)
assert b'sweet,clock-vote\0' not in blob
vtree, end = adt.parse(voted)
assert end == len(voted)
gcc = next(n for n in vtree.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-vote') == struct.pack('<I', 1)
assert gcc.prop('sweet,clock-vote-probe') == struct.pack('<I', 1), 'the vote implies the probe'
# the vote buys no write authorisation of its own: the gate card stays absent, so
# the page is reachable only by the vote register the driver checks by offset.
assert gcc.prop('sweet,clock-gate') is None
vreg = struct.unpack('<%dQ' % (len(gcc.prop('reg')) // 8), gcc.prop('reg'))
assert len(vreg) == 2 * (len(m.GCC_WINDOWS) + len(m.GCC_PROBE_WINDOW))
# dropping the vote property alone leaves the read-only probe tree
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-vote']
assert adt.serialize(vtree) == probed
combo = m.derive(blob, clock_vote=True, clock_share=True)
ctree, end = adt.parse(combo)
assert end == len(combo)
gcc = next(n for n in ctree.children[1].children if n.name == 'gcc')
for name in ('sweet,clock-vote', 'sweet,clock-vote-probe', 'sweet,clock-share',
             'sweet,clock-disable', 'sweet,clock-gate'):
    assert gcc.prop(name) == struct.pack('<I', 1), name
assert len(gcc.prop('reg')) == 8 * 2 * (len(m.GCC_WINDOWS) + len(m.GCC_PROBE_WINDOW))
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-vote-probe-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'probe.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-vote-probe',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == probed
    assert source.read_bytes() == blob
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-vote-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'vote.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-vote',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == voted
    assert source.read_bytes() == blob
# C2-07 needs the same fifth range for a different reason: the NPU reset block
# lives in that page, so the reset opt-in owns the range as well - but it maps the
# page for itself, which is why it implies neither the probe nor the vote.
reset = m.derive(blob, clock_reset=True)
assert reset == m.derive(blob, gcc=True, clock_reset=True)
assert reset != probed and reset != voted
assert b'sweet,clock-reset' not in m.derive(blob, gcc=True)
assert b'sweet,clock-reset' not in blob
rtree, end = adt.parse(reset)
assert end == len(reset)
gcc = next(n for n in rtree.children[1].children if n.name == 'gcc')
assert gcc.prop('sweet,clock-reset') == struct.pack('<I', 1)
for absent in ('sweet,clock-vote-probe', 'sweet,clock-vote', 'sweet,clock-gate',
               'sweet,clock-measure', 'sweet,clock-disable', 'sweet,clock-share'):
    assert gcc.prop(absent) is None, absent
rreg = struct.unpack('<%dQ' % (len(gcc.prop('reg')) // 8), gcc.prop('reg'))
assert len(rreg) == 2 * (len(m.GCC_WINDOWS) + len(m.GCC_PROBE_WINDOW))
rwindows = []
for i in range(0, len(rreg), 2):
    addr, size = rreg[i], rreg[i + 1]
    rwindows.append((addr + m.SOC_BASE if addr < m.LOW_ALIAS else addr - m.LOW_ALIAS,
                     size))
assert tuple(rwindows) == m.GCC_WINDOWS + m.GCC_PROBE_WINDOW
# same range as C2-06 and no property of C2-06's: the two cards can be read in
# one tree only by accident, and the driver refuses that pair at start.
assert gcc.prop('reg') == next(n for n in
                               adt.parse(probed)[0].children[1].children
                               if n.name == 'gcc').prop('reg')
# dropping the reset property alone leaves the plain GCC tree: the generator
# keeps the range and the property in one step, like the probe's.
gcc.props = [(k, f, v) for k, f, v in gcc.props if k != 'sweet,clock-reset']
gcc.props = [(k, f, encode_windows(m.GCC_WINDOWS) if k == 'reg' else v)
             for k, f, v in gcc.props]
assert adt.serialize(rtree) == m.derive(blob, gcc=True)
combo = m.derive(blob, clock_reset=True, clock_share=True)
ctree, end = adt.parse(combo)
assert end == len(combo)
gcc = next(n for n in ctree.children[1].children if n.name == 'gcc')
for name in ('sweet,clock-reset', 'sweet,clock-share', 'sweet,clock-disable',
             'sweet,clock-gate'):
    assert gcc.prop(name) == struct.pack('<I', 1), name
assert len(gcc.prop('reg')) == 8 * 2 * (len(m.GCC_WINDOWS) + len(m.GCC_PROBE_WINDOW))
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-reset-adt-') as temp:
    source, output = Path(temp)/'base.adt', Path(temp)/'reset.adt'
    source.write_bytes(blob)
    subprocess.run([sys.executable, str(Path(m.__file__)), '--clock-reset',
                    str(source), str(output)], check=True)
    assert output.read_bytes() == reset
    assert source.read_bytes() == blob
print('PASS: C2 GCC windows are a separate opt-in node, low-alias encoded, order-stable, '
      'exactly removable to the C1 and original bytes, and selectable from the CLI, with the '
      'C2-08 measurement, C2-03 branch gate and dependent C2-04 disable as opt-in properties '
      'that no earlier image carries, and the C2-05 share on top of all three, plus the '
      'C2-06 vote probe, which owns both its property and the fifth range, and the vote on '
      'top of it as a property of its own, and the C2-07 reset, which shares the '
      'fifth range with the probe but implies neither it nor the vote')
