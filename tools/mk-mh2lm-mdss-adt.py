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
# The panel reset/TE pads are owned through TLMMGPIOController's AGPIO
# handles, so this node no longer carries their tile windows: the controller
# is the only mapper of TLMM.
# C2: the four GCC pages this port may need. The first page is also the
# first window of arm-io/mdss, which maps it read-only today: both nodes name
# the same physical page on purpose, and only the clock backend may ever own
# a write to it. Every entry is a resolved physical address: the SM8150 GCC
# block sits at 0x00100000 (sm8150.dtsi reg = <0x100000 0x1f0000>), so the
# vendor driver offsets 0x33008/0x43008/0x62000 live on the pages below, and
# the bare offsets are never ranges of their own.
GCC_WINDOWS = ((0x0010b000, 0x1000), (0x00133000, 0x1000),
               (0x00143000, 0x1000), (0x00162000, 0x1000))
NOC_WINDOWS = ((0x0af20000, 0x1c00), (0x0af21c00, 0x3000), (0x0af30000, 0x3fd4))
# SM8150 TLMM: four 1 MiB tile windows (west, east, north, south). The pad
# slot inside a tile is 0x1000 * global pin number, so a full 1 MiB window
# covers every pad the 175-pad table can reach.
TLMM_WINDOWS = ((0x03100000, 0x100000), (0x03500000, 0x100000),
                (0x03900000, 0x100000), (0x03d00000, 0x100000))


def derive(blob, irq_preflight=False, display_resources=False, display_noc=False, panel_pins=False, panel_touch=False, panel_identity=False, rpmh_irq=False, panel_pipeline=False, clock_provider=False, clock_diag=False, gcc=False):
    display_noc = display_noc or panel_pipeline
    display_resources = display_resources or display_noc or panel_pins or panel_touch or panel_identity or rpmh_irq
    root, end = adt.parse(blob)
    if end != len(blob) or b'lge,mh2lm\0' not in (root.prop('compatible') or b''):
        raise ValueError('expected a complete MH2LM ADT')
    arm = next(n for n in root.children if n.name == 'arm-io')
    expected = struct.pack('<QQQ', 0, SOC_BASE, 0x100000000 - SOC_BASE)
    if arm.prop('ranges') != expected:
        raise ValueError('unexpected arm-io ranges; refusing to replace translations')
    # The GIC node must describe the driver personality and the redistributor
    # independently of the reg cell order. Older base ADTs predate both; make
    # every derived ADT carry them so the controller can match and start.
    gic = next((n for n in arm.children if n.name == 'gic'), None)
    if gic is not None:
        props = dict((k, v) for k, f, v in gic.props)
        if b'sweet,gicv3\0' not in (props.get('compatible') or b''):
            gic.props = [(k, f,
                          b'sweet,gicv3\0arm,gic-v3\0' if k == 'compatible' else v)
                         for k, f, v in gic.props]
            props = dict((k, v) for k, f, v in gic.props)
        if '#interrupt-cells' not in props:
            gic.props.append(('#interrupt-cells', 0, struct.pack('<I', 3)))
        if 'redistributor' not in props:
            # Only a 4-cell reg (GICD + GICR windows) can supply the
            # redistributor; a minimal audit tree may carry just GICD.
            reg = struct.unpack('<%dQ' % (len(props['reg']) // 8), props['reg'])
            if len(reg) >= 4:
                gic.props.append(('redistributor', 0,
                                  struct.pack('<QQ', reg[2], reg[3])))
    if any(n.name in ('mdss', 'display-rpmh', 'panel-pins', 'panel-touch') for n in arm.children):
        raise ValueError('display resource node already exists')
    # GCC's 0x10b000 page is below SOC_BASE. Add a disjoint low-PA alias;
    # never underflow an unsigned reg address or change the original range.
    arm.props = [(k, f, v + struct.pack('<QQQ', LOW_ALIAS, 0, SOC_BASE)
                  if k == 'ranges' else v) for k, f, v in arm.props]
    def encode(windows):
        return b''.join(struct.pack('<QQ', pa-SOC_BASE if pa >= SOC_BASE else LOW_ALIAS+pa, size)
                        for pa, size in windows)
    # TLMMGPIOController matches this node (IONameMatch qcom,tlmm-sm8150) and
    # maps the four windows as its tile table. Idempotent: a base ADT that
    # already carries the node is left alone.
    def gic_phandle():
        gic = next((n for n in arm.children if n.name == 'gic'), None)
        if gic is None:
            return 0
        ph = dict((k, v) for k, f, v in gic.props).get('AAPL,phandle')
        return struct.unpack('<I', ph)[0] if ph is not None and len(ph) == 4 else 0

    if not any(n.name == 'tlmm' for n in arm.children):
        tlmm = adt.Node()
        tlmm.props = [('name', 0, b'tlmm\0'),
                      ('compatible', 0, b'qcom,tlmm-sm8150\0'),
                      ('gpio-controller', 0, b''),
                      ('#gpio-cells', 0, struct.pack('<I', 2)),
                      ('qcom,pin-count', 0, struct.pack('<I', 175)),
                      ('reg', 0, encode(TLMM_WINDOWS))]
        # The summary line: one GIC SPI aggregates every pad. Until the
        # controller claims it, the client that asks for a pad interrupt owns
        # it; sweetgpioirq=1 moves that ownership here.
        ph = gic_phandle()
        if ph:
            tlmm.props.extend([
                ('interrupt-parent', 0, struct.pack('<I', ph)),
                ('interrupts', 0, struct.pack('<III', 240, 0, 4)),
            ])
        arm.children.append(tlmm)
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
                      ('sweet,panel-pins-abi', 0, struct.pack('<I', 1))]
        arm.children.append(pins)
    if panel_touch:
        touch = adt.Node()
        touch.props = [('name', 0, b'panel-touch\0'), ('compatible', 0, b'sweet,touch-panel\0'),
                       ('sweet,touch-panel-abi', 0, struct.pack('<I', 1))]
        # The facade registers this GIC line through the provider nub; the
        # specifier is <INTID TYPE FLAGS> and the GIC is the interrupt parent.
        # With sweetgpioirq=1 the TLMM controller owns the same line and this
        # property is the fallback path only.
        gic_ph = gic_phandle()
        if not gic_ph:
            raise ValueError('panel-touch needs the gic node and its phandle')
        touch.props.extend([
            ('interrupt-parent', 0, struct.pack('<I', gic_ph)),
            ('interrupts', 0, struct.pack('<III', 240, 0, 4)),
        ])
        arm.children.append(touch)
    if clock_diag or gcc:
        clock_provider = True
    if clock_provider:
        # C1: one clock entry personality. The node carries no reg and no
        # window: every controller window stays with its own CC backend, so
        # this opt-in cannot introduce a second mapper or a write path.
        clock = adt.Node()
        clock.props = [('name', 0, b'clock\0'),
                       ('compatible', 0, b'sweet,clock-provider\0'),
                       ('sweet,clock-abi', 0, struct.pack('<I', 1))]
        if clock_diag:
            # The firmware rejects a boot-argument list past 607 bytes, so the
            # diagnostic backend has to be requested from the device tree.
            clock.props.append(('sweet,clock-diag', 0, struct.pack('<I', 1)))
        arm.children.append(clock)
    if gcc and not any(n.name == 'gcc' for n in arm.children):
        # C2: the read-only GCC backend windows. No window overlaps a writer:
        # the 0x10b000 page is mapped read-only by the display backend and
        # stays that way, the other three have no owner in this tree.
        gcc_node = adt.Node()
        gcc_node.props = [('name', 0, b'gcc\0'),
                          ('compatible', 0, b'sweet,gcc\0'),
                          ('sweet,gcc-abi', 0, struct.pack('<I', 1)),
                          ('sweet,gcc-variant', 0, struct.pack('<I', 0x8150)),
                          ('reg', 0, encode(GCC_WINDOWS))]
        arm.children.append(gcc_node)
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
    parser.add_argument('--clock-provider', action='store_true', help='append the C1 clock entry; no window, no controller registration at boot')
    parser.add_argument('--clock-diag', action='store_true', help='append the C1 clock entry with the diagnostic opt-in property (implies --clock-provider)')
    parser.add_argument('--gcc', action='store_true', help='append the read-only C2 GCC backend windows and variant ABI (implies --clock-provider)')
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    source, destination = args.source, args.destination
    if source.resolve() == destination.resolve():
        sys.exit('output must differ from the baseline ADT')
    destination.write_bytes(derive(source.read_bytes(), args.irq_preflight, args.display_resources, args.display_noc, args.panel_pins, args.panel_touch, args.panel_identity, args.rpmh_irq, args.panel_pipeline, args.clock_provider, args.clock_diag, args.gcc))
