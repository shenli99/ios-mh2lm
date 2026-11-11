#!/usr/bin/env python3
"""Derive a display ADT; opt-in v1 resources add PHY/PLL and RPMh service."""
from pathlib import Path
import struct
import sys
import argparse
import adt

SOC_BASE = 0x01000000
LOW_ALIAS = 0x100000000
WINDOWS = ((0x0010b000, 0x1000), (0x0af00000, 0x3000),
           (0x0ae05000, 0x1000), (0x0ae02000, 0x1000),
           (0x0ae6b800, 0x800), (0x0ae6c000, 0x800),
           (0x0ae94000, 0x400), (0x15000000, 0x2000), (0x15082000, 0x1000))
IRQ_WINDOWS = ((0x0ae00000, 0x2000), (0x0ae6e800, 0x200), (0x17a00000, 0x10000))
PHY_WINDOW = (0x0ae94400, 0x760)  # includes PLL0 +0x500; one owner/map
RPMH_WINDOWS = ((0x18220000, 0x100), (0x18220d00, 0x3000), (0x0c3f000c, 8))
PANEL_PIN_WINDOWS = ((0x03d06000, 0x1000), (0x03908000, 0x1000))  # GPIO6 SOUTH, GPIO8 NORTH
NOC_WINDOWS = ((0x0af20000, 0x1c00), (0x0af21c00, 0x3000), (0x0af30000, 0x3fd4))


def derive(blob, irq_preflight=False, display_resources=False, display_noc=False, panel_pins=False):
    display_resources = display_resources or display_noc or panel_pins
    root, end = adt.parse(blob)
    if end != len(blob) or b'lge,mh2lm\0' not in (root.prop('compatible') or b''):
        raise ValueError('expected a complete MH2LM ADT')
    arm = next(n for n in root.children if n.name == 'arm-io')
    expected = struct.pack('<QQQ', 0, SOC_BASE, 0x100000000 - SOC_BASE)
    if arm.prop('ranges') != expected:
        raise ValueError('unexpected arm-io ranges; refusing to replace translations')
    if any(n.name in ('mdss', 'display-rpmh', 'panel-pins') for n in arm.children):
        raise ValueError('display resource node already exists')
    # GCC's 0x10b000 page is below SOC_BASE. Add a disjoint low-PA alias;
    # never underflow an unsigned reg address or change the original range.
    arm.props = [(k, f, v + struct.pack('<QQQ', LOW_ALIAS, 0, SOC_BASE)
                  if k == 'ranges' else v) for k, f, v in arm.props]
    def encode(windows):
        return b''.join(struct.pack('<QQ', pa-SOC_BASE if pa >= SOC_BASE else LOW_ALIAS+pa, size)
                        for pa, size in windows)
    windows = list(WINDOWS)
    if display_resources:
        windows[1] = (windows[1][0], 0x20000)  # includes GDSC/clamp, never a second writer
    if irq_preflight or display_resources:
        windows.extend(IRQ_WINDOWS)
    if display_resources:
        windows.append(PHY_WINDOW)
    if display_noc:
        windows.extend(NOC_WINDOWS)
    regs = encode(windows)
    mdss = adt.Node()
    mdss.props = [('name', 0, b'mdss\0'), ('compatible', 0, b'sweet,mdss\0'),
                  ('reg', 0, regs),
                  # Metadata only. F3 must validate and wire the interrupt
                  # controller before publishing standard interrupt properties.
                  ('sweet,mdss-intid', 0, struct.pack('<I', 115))]
    arm.children.append(mdss)
    if display_noc:
        mdss.props.append(('sweet,display-noc-abi', 0, struct.pack('<I', 1)))
    if display_resources:
        mdss.props.append(('sweet,display-resource-abi', 0, struct.pack('<I', 1)))
        rpmh = adt.Node()
        rpmh.props = [('name', 0, b'display-rpmh\0'), ('compatible', 0, b'sweet,rpmh\0'),
                      ('reg', 0, encode(RPMH_WINDOWS)),
                      ('sweet,rpmh-abi', 0, struct.pack('<I', 1))]
        arm.children.append(rpmh)
    if panel_pins:
        pins = adt.Node()
        pins.props = [('name', 0, b'panel-pins\0'), ('compatible', 0, b'sweet,panel-pins\0'),
                      ('reg', 0, encode(PANEL_PIN_WINDOWS)),
                      ('sweet,panel-pins-abi', 0, struct.pack('<I', 1))]
        arm.children.append(pins)
    return adt.serialize(root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--irq-preflight', action='store_true', help='append optional F3 read-only IRQ resources')
    parser.add_argument('--display-resources', action='store_true', help='v1 expanded DISPCC, PHY/PLL and typed RPMh service (implies IRQ resources)')
    parser.add_argument('--display-noc', action='store_true', help='append sole MDSS display RSC/TCS/wrapper windows; implies display resources, requires resource2/clock1 at runtime')
    parser.add_argument('--panel-pins', action='store_true', help='append GPIO6/reset and GPIO8/TE provider; implies display resources, never authorizes reset/vendor commands')
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    source, destination = args.source, args.destination
    if source.resolve() == destination.resolve():
        sys.exit('output must differ from the baseline ADT')
    destination.write_bytes(derive(source.read_bytes(), args.irq_preflight, args.display_resources, args.display_noc, args.panel_pins))
