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
# C2-06 appends one more range, the NPU block, and only when it asks for it. The
# four above are the ABI every GCC tree carries; the fifth is the voted page the
# probe reads, so a tree without the opt-in keeps a four-range reg list.
GCC_PROBE_WINDOW = ((0x0014d000, 0x1000),)
NOC_WINDOWS = ((0x0af20000, 0x1c00), (0x0af21c00, 0x3000), (0x0af30000, 0x3fd4))
# SM8150 TLMM: four 1 MiB tile windows (west, east, north, south). The pad
# slot inside a tile is 0x1000 * global pin number, so a full 1 MiB window
# covers every pad the 175-pad table can reach.
TLMM_WINDOWS = ((0x03100000, 0x100000), (0x03500000, 0x100000),
                (0x03900000, 0x100000), (0x03d00000, 0x100000))


def derive(blob, irq_preflight=False, display_resources=False, display_noc=False, panel_pins=False, panel_touch=False, panel_identity=False, rpmh_irq=False, panel_pipeline=False, clock_provider=False, clock_diag=False, gcc=False, clock_measure=False,
            clock_gate=False, clock_disable=False, clock_share=False,
            clock_vote_probe=False, clock_vote=False, clock_reset=False,
            rsc_channel=False, rsc_query=False, rsc_executor=False,
            rpmh_queue_selftest=False, rpmh_queue_cancel=False,
            rpmh_transaction_query=False, rpmh_command_group=False):
    # C2-05 stands on both cards below it, so the two switch it needs are implied
    # here rather than trusted to every caller: a share node without them would be
    # refused at start and the image would carry a property that can do nothing.
    clock_disable = clock_disable or clock_share
    clock_gate = clock_gate or clock_disable
    # C2-06's write half votes in the page its read-only half maps, so the vote
    # implies the probe: a node that asked for the vote alone would be refused at
    # start, and the generator will not emit one.
    clock_vote_probe = clock_vote_probe or clock_vote
    # C3-02's query lives on the C3-01 channel: a tree that asked for the query
    # without the channel would carry a property nothing reads.
    if rsc_query and not rsc_channel:
        raise ValueError('--rsc-query needs --rsc-channel')
    # C3-04 has two owners or none: the channel becomes the sole writer and the
    # display node has to say so in the same tree. Asking for the delegating
    # half alone, or delegating to a channel that does not exist, would produce
    # a boot with no writer at all.
    if rsc_executor and not rsc_channel:
        raise ValueError('--rsc-executor needs --rsc-channel')
    if rsc_executor and not rpmh_irq:
        raise ValueError('--rsc-executor needs --rpmh-irq (the old owner must be told to stand down)')
    # C3-05's demonstration runs inside the display owner, which is where the
    # queue lives; without that node the property would be read by nobody.
    if rpmh_queue_selftest and not rpmh_irq:
        raise ValueError('--rpmh-queue-selftest needs --rpmh-irq (the property is read by the display owner node)')
    # C3-06's demonstration withdraws requests that the C3-05 one queued, so a
    # tree that asks for it asks for both: on its own it would have nothing to
    # take back and the boot would print a demonstration that proves nothing.
    if rpmh_queue_cancel and not rpmh_queue_selftest:
        raise ValueError('--rpmh-queue-cancel needs --rpmh-queue-selftest (it withdraws requests that demonstration queued)')
    # C3-07's demonstration asks about requests it submits itself, so it needs
    # neither of the two above: it is the first card in the chain whose tree
    # asks for one thing only.
    if rpmh_transaction_query and not rpmh_irq:
        raise ValueError('--rpmh-transaction-query needs --rpmh-irq (the property is read by the display owner node)')
    # C3-08's demonstration asks the group gate three questions and writes no
    # register, so it submits nothing and needs no flight of its own. It still
    # needs the same owner node to read the property.
    if rpmh_command_group and not rpmh_irq:
        raise ValueError('--rpmh-command-group needs --rpmh-irq (the property is read by the display owner node)')
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
        if rsc_executor:
            # C3-04: this node keeps its ranges and its INTID, but no longer
            # writes through them - the channel owns the array and the
            # completion line for this boot. The range list is left exactly as
            # shipped so a rollback only has to drop this property.
            rpmh.props.append(('sweet,rpmh-executor-delegated', 0, struct.pack('<I', 1)))
        if rpmh_queue_selftest:
            # C3-05: one boot-time demonstration of the bounded submission
            # queue. Absent by default, and exactly 1 when present - the driver
            # treats every other length or value as "do not run".
            rpmh.props.append(('sweet,rpmh-queue-selftest', 0, struct.pack('<I', 1)))
        if rpmh_queue_cancel:
            # C3-06: the same boot also takes two waiting requests back, which
            # only exists because the property above made the queue run.
            rpmh.props.append(('sweet,rpmh-queue-cancel', 0, struct.pack('<I', 1)))
        if rpmh_transaction_query:
            # C3-07: one boot-time demonstration of the per-transaction record.
            # It submits its own requests, so it neither needs nor implies the
            # queue demonstration above - and a tree that asks only for this one
            # reads as one demonstration, not as two.
            rpmh.props.append(('sweet,rpmh-transaction-query', 0, struct.pack('<I', 1)))
        if rpmh_command_group:
            # C3-08: the boundary of one ordered command group, asked rather
            # than performed - it names the limit and refuses one more without
            # touching a register, so it can share a boot with any other
            # demonstration and cannot be mistaken for one of them.
            rpmh.props.append(('sweet,rpmh-command-group', 0, struct.pack('<I', 1)))
        arm.children.append(rpmh)
    if rsc_channel:
        # C3-01: a second, read-only reader of the apps RSC. It carries the same
        # three ranges as the display owner and no GIC range at all, so this node
        # cannot describe a second interrupt listener even by accident; the
        # completion ABI is metadata the channel reports and never arms.
        rsc = adt.Node()
        # C3-04 adds the GIC range the channel owns once it executes; every
        # earlier mode keeps the three ranges, so nothing here can listen.
        rsc_ranges = RPMH_WINDOWS + IRQ_WINDOWS[-1:] if rsc_executor else RPMH_WINDOWS
        rsc.props = [('name', 0, b'rsc-channel\0'),
                     ('compatible', 0, b'sweet,rsc-channel\0'),
                     ('reg', 0, encode(rsc_ranges)),
                     ('sweet,rsc-abi', 0, struct.pack('<I', 1)),
                     ('sweet,rsc-irq-abi', 0, struct.pack('<I', 1)),
                     ('sweet,rsc-intid', 0, struct.pack('<I', 37))]
        if rsc_executor:
            # C3-04: the range list above now names the completion window, so the
            # node states who it is for this boot. The property is the switch the
            # driver reads; the ranges only say what it may map once it is on.
            rsc.props.append(('sweet,rsc-executor', 0, struct.pack('<I', 1)))
        if rsc_query:
            # C3-02: permission to ask the command DB about one known resource.
            # Its own versioned opt-in, so a tree that keeps the C3-01 read
            # budget keeps exactly that budget and emits no query row.
            rsc.props.append(('sweet,rsc-query', 0, struct.pack('<I', 1)))
        arm.children.append(rsc)
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
    # C2-04 retracts the enable C2-03 stores, so it cannot be requested without
    # that gate: a tree that asks for a disable alone is a tree the backend
    # refuses, and the generator will not emit one.
    if clock_disable:
        clock_gate = True
    if (clock_diag or gcc or clock_measure or clock_gate or clock_vote_probe
            or clock_reset):
        clock_provider = True
    gcc = gcc or clock_measure or clock_gate or clock_vote_probe or clock_reset
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
                          ('reg', 0, encode(GCC_WINDOWS + GCC_PROBE_WINDOW
                                            if clock_vote_probe or clock_reset
                                            else GCC_WINDOWS))]
        if clock_measure:
            # C2-08 is the first candidate that writes hardware, so it is opt-in
            # from the device tree: the default image keeps the zero-write ring.
            gcc_node.props.append(('sweet,clock-measure', 0, struct.pack('<I', 1)))
        if clock_gate:
            # C2-03 gates a branch. It is a separate opt-in from the measurement:
            # either can be requested alone, and both can ride together.
            gcc_node.props.append(('sweet,clock-gate', 0, struct.pack('<I', 1)))
        if clock_disable:
            # C2-04 withdraws that enable again and hands the demand back. The
            # property therefore never travels alone: the gate stays beside it,
            # and the backend cross-checks the pair before any window is read.
            gcc_node.props.append(('sweet,clock-disable', 0, struct.pack('<I', 1)))
        if clock_share:
            # C2-05 shares the enable the gate opened between two consumers and
            # lets the last of them do the withdraw C2-04 verified. It replaces
            # the single-consumer pass, so one boot makes three stores either way.
            gcc_node.props.append(('sweet,clock-share', 0, struct.pack('<I', 1)))
        if clock_vote_probe:
            # C2-06 maps the voted page and reads it; it stores nothing, so this
            # opt-in can ride on any image that carries the GCC node. The window
            # it needs is the fifth range appended above.
            gcc_node.props.append(('sweet,clock-vote-probe', 0, struct.pack('<I', 1)))
        if clock_vote:
            # C2-06's write half is a separate opt-in so the read-only probe image
            # keeps its zero-write ring: this property is the only thing that
            # makes the fifth page reachable by the store path.
            gcc_node.props.append(('sweet,clock-vote', 0, struct.pack('<I', 1)))
        if clock_reset:
            # C2-07 pulses the NPU reset block, which lives on the same fifth
            # range and on a word no other card touches. It maps the page for
            # itself, so it implies neither the probe nor the vote - a node that
            # carried both would be refused at start.
            gcc_node.props.append(('sweet,clock-reset', 0, struct.pack('<I', 1)))
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
    parser.add_argument('--clock-measure', action='store_true', help='append the C2-08 measurement opt-in gate (implies --gcc); the default image stays zero-write')
    parser.add_argument('--clock-gate', action='store_true', help='append the C2-03 branch gate opt-in (implies --gcc); stores one enable bit and keeps it on until reboot')
    parser.add_argument('--clock-disable', action='store_true', help='append the C2-04 verified disable opt-in (implies --clock-gate); withdraws that enable, proves the halt and releases the demand')
    parser.add_argument('--clock-share', action='store_true', help='append the C2-05 sharing opt-in (implies --clock-gate and --clock-disable); two consumers share the gate demand and the last release retracts it')
    parser.add_argument('--clock-vote-probe', action='store_true', help='append the C2-06 read-only vote probe (implies --gcc); maps the NPU page as a fifth range and reads four CBCRs, zero stores')
    parser.add_argument('--clock-vote', action='store_true', help='append the C2-06 vote opt-in (implies --clock-vote-probe); stores one vote bit, waits for the aggregate, and withdraws it again')
    parser.add_argument('--clock-reset', action='store_true', help='append the C2-07 reset opt-in (implies --gcc); maps the NPC page for itself and pulses the NPU reset bit once, 1 us wide, only after proving the block stopped')
    parser.add_argument('--rsc-channel', action='store_true', help='append the C3-01 read-only apps RSC channel; same three ranges as the display owner, no GIC range, zero stores')
    parser.add_argument('--rsc-query', action='store_true', help='C3-02: let the channel query the read-only command DB for one known resource; requires --rsc-channel')
    parser.add_argument('--rsc-executor', action='store_true', help='C3-04: the channel becomes the sole apps executor (fourth GIC range, sweet,rsc-executor) and the display node stands down; requires --rsc-channel and --rpmh-irq')
    parser.add_argument('--rpmh-queue-selftest', action='store_true', help='C3-05: the display owner demonstrates the bounded submission queue once on this boot (sweet,rpmh-queue-selftest=1); requires --rpmh-irq')
    parser.add_argument('--rpmh-queue-cancel', action='store_true', help='C3-06: the same demonstration also takes two waiting requests back (sweet,rpmh-queue-cancel=1); requires --rpmh-queue-selftest')
    parser.add_argument('--rpmh-transaction-query', action='store_true', help='C3-07: the display owner demonstrates the per-transaction record once on this boot (sweet,rpmh-transaction-query=1); requires --rpmh-irq and implies neither queue demonstration')
    parser.add_argument('--rpmh-command-group', action='store_true', help='C3-08: the display owner asks the command-group gate for its boundary once on this boot (sweet,rpmh-command-group=1); requires --rpmh-irq, writes no register and implies nothing else')
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    source, destination = args.source, args.destination
    if source.resolve() == destination.resolve():
        sys.exit('output must differ from the baseline ADT')
    destination.write_bytes(derive(source.read_bytes(), args.irq_preflight, args.display_resources, args.display_noc, args.panel_pins, args.panel_touch, args.panel_identity, args.rpmh_irq, args.panel_pipeline, args.clock_provider, args.clock_diag, args.gcc, args.clock_measure, args.clock_gate,
                      args.clock_disable, args.clock_share, args.clock_vote_probe,
                      args.clock_vote, args.clock_reset, args.rsc_channel,
                      args.rsc_query, args.rsc_executor,
                      args.rpmh_queue_selftest, args.rpmh_queue_cancel,
                      args.rpmh_transaction_query, args.rpmh_command_group))
