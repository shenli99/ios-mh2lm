#!/usr/bin/env python3
"""Derive an F1a ADT without regenerating chosen/memory/identity properties."""
from pathlib import Path
import struct
import sys
import adt

SOC_BASE = 0x01000000
LOW_ALIAS = 0x100000000
WINDOWS = ((0x0010b000, 0x1000), (0x0af00000, 0x3000),
           (0x0ae05000, 0x1000), (0x0ae02000, 0x1000),
           (0x0ae6b800, 0x800), (0x0ae6c000, 0x800),
           (0x0ae94000, 0x400), (0x15000000, 0x2000), (0x15082000, 0x1000))


def derive(blob):
    root, end = adt.parse(blob)
    if end != len(blob) or b'lge,mh2lm\0' not in (root.prop('compatible') or b''):
        raise ValueError('expected a complete MH2LM ADT')
    arm = next(n for n in root.children if n.name == 'arm-io')
    expected = struct.pack('<QQQ', 0, SOC_BASE, 0x100000000 - SOC_BASE)
    if arm.prop('ranges') != expected:
        raise ValueError('unexpected arm-io ranges; refusing to replace translations')
    if any(n.name == 'mdss' for n in arm.children):
        raise ValueError('mdss already exists')
    # GCC's 0x10b000 page is below SOC_BASE. Add a disjoint low-PA alias;
    # never underflow an unsigned reg address or change the original range.
    arm.props = [(k, f, v + struct.pack('<QQQ', LOW_ALIAS, 0, SOC_BASE)
                  if k == 'ranges' else v) for k, f, v in arm.props]
    regs = b''.join(struct.pack('<QQ', pa-SOC_BASE if pa >= SOC_BASE else LOW_ALIAS+pa, size)
                    for pa, size in WINDOWS)
    mdss = adt.Node()
    mdss.props = [('name', 0, b'mdss\0'), ('compatible', 0, b'sweet,mdss\0'),
                  ('reg', 0, regs),
                  # Metadata only. F3 must validate and wire the interrupt
                  # controller before publishing standard interrupt properties.
                  ('sweet,mdss-intid', 0, struct.pack('<I', 115))]
    arm.children.append(mdss)
    return adt.serialize(root)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(f'usage: {sys.argv[0]} BASE.adt OUT.adt')
    source, destination = map(Path, sys.argv[1:])
    if source.resolve() == destination.resolve():
        sys.exit('output must differ from the baseline ADT')
    destination.write_bytes(derive(source.read_bytes()))
