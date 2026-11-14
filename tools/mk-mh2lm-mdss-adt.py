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


def derive(blob, irq_preflight=False, display_resources=False, display_noc=False, panel_pins=False, panel_touch=False, panel_identity=False, rpmh_irq=False, panel_pipeline=False):
    display_noc = display_noc or panel_pipeline
    display_resources = display_resources or display_noc or panel_pins or panel_touch or panel_identity or rpmh_irq
    root, end = adt.parse(blob)
    if end != len(blob) or b'lge,mh2lm\0' not in (root.prop('compatible') or b''):
        raise ValueError('expected a complete MH2LM ADT')
    arm = next(n for n in root.children if n.name == 'arm-io')
    expected = struct.pack('<QQQ', 0, SOC_BASE, 0x100000000 - SOC_BASE)
    if arm.prop('ranges') != expected:
        raise ValueError('unexpected arm-io ranges; refusing to replace translations')
    if any(n.name in ('mdss', 'display-rpmh', 'panel-pins', 'panel-touch') for n in arm.children):
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
    if panel_pipeline:
        # Append-only mode7 ABI: LM0, PP0, VIG0 CSC10 opmode.
        windows.extend(((0x0ae45000, 0x320), (0x0ae71000, 0xd4), (0x0ae06a00, 4)))
    regs = encode(windows)
    mdss = adt.Node()
    mdss.props = [('name', 0, b'mdss\0'), ('compatible', 0, b'sweet,mdss\0'),
                  ('reg', 0, regs),
                  # Metadata only. F3 must validate and wire the interrupt
                  # controller before publishing standard interrupt properties.
                  ('sweet,mdss-intid', 0, struct.pack('<I', 115))]
    arm.children.append(mdss)
    if panel_pipeline:
        mdss.props.append(('sweet,display-pipeline-abi', 0, struct.pack('<I', 1)))
    if panel_identity:
        # ABI placeholder only. No CLI option can preselect a revision: the
        # DarwinBoot adapter replaces this from ABL's live /chosen/bootargs before XNU.
        mdss.props.append(('sweet,panel-identity', 0, bytes(96)))
    if display_noc:
        mdss.props.append(('sweet,display-noc-abi', 0, struct.pack('<I', 1)))
        mdss.props.append(('sweet,display-noc-irq-abi', 0, struct.pack('<I', 1)))
        mdss.props.append(('sweet,display-noc-intid', 0, struct.pack('<I', 161)))
    if display_resources:
        mdss.props.append(('sweet,display-resource-abi', 0, struct.pack('<I', 1)))
        rpmh = adt.Node()
        rpmh.props = [('name', 0, b'display-rpmh\0'), ('compatible', 0, b'sweet,rpmh\0'),
                      ('reg', 0, encode(RPMH_WINDOWS + (IRQ_WINDOWS[-1:] if rpmh_irq else ()))),
                      ('sweet,rpmh-abi', 0, struct.pack('<I', 1))]
        if rpmh_irq:
            # Explicit append-only extension. Mode2 uses GIC callback + native
            # event/workloop; metadata-only and old ABI output stay unchanged.
            rpmh.props.extend([('sweet,rpmh-irq-abi', 0, struct.pack('<I', 1)),
                               ('sweet,rpmh-intid', 0, struct.pack('<I', 37))])
        arm.children.append(rpmh)
    if panel_pins:
        pins = adt.Node()
        pins.props = [('name', 0, b'panel-pins\0'), ('compatible', 0, b'sweet,panel-pins\0'),
                      ('reg', 0, encode(PANEL_PIN_WINDOWS)),
                      ('sweet,panel-pins-abi', 0, struct.pack('<I', 1))]
        arm.children.append(pins)
    if panel_touch:
        touch = adt.Node()
        touch.props = [('name', 0, b'panel-touch\0'), ('compatible', 0, b'sweet,touch-panel\0'),
                       ('sweet,touch-panel-abi', 0, struct.pack('<I', 1))]
        arm.children.append(touch)  # mailbox facade: no new MMIO/IRQ resource
    return adt.serialize(root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--irq-preflight', action='store_true', help='append optional F3 read-only IRQ resources')
    parser.add_argument('--display-resources', action='store_true', help='v1 expanded DISPCC, PHY/PLL and typed RPMh service (implies IRQ resources)')
    parser.add_argument('--display-noc', action='store_true', help='append sole MDSS display RSC/TCS/wrapper windows; implies display resources, requires resource2/clock1 at runtime')
    parser.add_argument('--panel-pins', action='store_true', help='append GPIO6/reset and GPIO8/TE provider; implies display resources, never authorizes reset/vendor commands')
    parser.add_argument('--panel-touch', action='store_true', help='append typed touch worker coordination service; no new GPIO/IRQ mapping, no automatic pause/resume')
    parser.add_argument('--panel-identity', action='store_true', help='reserve zeroed live-ABL panel selection slot; implies resources, never selects a revision')
    parser.add_argument('--rpmh-irq', action='store_true', help='append apps RSC GIC mapping and INTID37 completion ABI; implies display resources')
    parser.add_argument('--panel-pipeline', action='store_true', help='append LM0/PP0/CSC10 windows for mode7 pipeline rebuild; implies display NoC resources')
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    source, destination = args.source, args.destination
    if source.resolve() == destination.resolve():
        sys.exit('output must differ from the baseline ADT')
    destination.write_bytes(derive(source.read_bytes(), args.irq_preflight, args.display_resources, args.display_noc, args.panel_pins, args.panel_touch, args.panel_identity, args.rpmh_irq, args.panel_pipeline))
