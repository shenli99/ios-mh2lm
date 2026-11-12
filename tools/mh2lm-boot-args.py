#!/usr/bin/env python3
"""Validate XNU 8796's 608-byte command line; optionally omit panel defaults.

Never truncate arguments: bytes after CommandLine overlap XNU's bootFlags and
memSizeActual when an incompatible loader uses a 1024-byte command line.
"""
from pathlib import Path
import argparse
import json
import re
import tomllib

BOOT_LINE_LENGTH = 608


def compact_panel(args):
    # Defaults verified against the 0121a14 XNU source. This is deliberately
    # limited to the panel matrix; explicit non-default switches stay intact.
    defaults = {
        'sweetkeys=1', 'sweetloglevel=2', 'sweetdispprobe=1',
        'sweetfbtrace=0', 'sweetfbcontract=0', 'sweetfbproof=0',
        'sweetmdssirqprobe=0', 'sweetdisplayclocks=0', 'sweetdisplaycore=0',
        'sweetdisplaymanaged=0', 'sweetdsihost=0', 'sweetdsilink=0',
        'sweetpanelpins=0', 'sweettouchpanel=0', 'sweetpanellifecycle=0',
    }
    tokens = args.split()
    # AppleCLCD computes preserveOff != 0 || fPanelPowerMode. Power mode1
    # already implies preservation; mode0 must keep its explicit setting.
    if 'sweetfbpower=1' in tokens:
        defaults.add('sweetfbpreserveoff=1')
    return ' '.join(token for token in tokens if token not in defaults)


def validate(args):
    raw = args.encode('ascii')
    if '\0' in args or '\n' in args or '\r' in args:
        raise ValueError('boot arguments must be one NUL-free ASCII line')
    if len(raw) >= BOOT_LINE_LENGTH:
        raise ValueError(f'XNU 8796 boot arguments are {len(raw)} bytes; maximum is '
                         f'{BOOT_LINE_LENGTH - 1}; refusing to truncate or package')
    keys = [t.split('=', 1)[0] for t in args.split()]
    if len(set(keys)) != len(keys):
        raise ValueError('duplicate boot arguments are ambiguous; refusing to package')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('toml', type=Path)
    parser.add_argument('--compact-panel', action='store_true')
    options = parser.parse_args()
    original = options.toml.read_text()
    entries = [e for e in tomllib.loads(original)['entry'] if e.get('type') == 'xnu']
    if len(entries) != 1:
        raise ValueError('expected exactly one XNU entry')
    args = entries[0]['args']
    keys = [token.split('=', 1)[0] for token in args.split()]
    if len(set(keys)) != len(keys):
        raise ValueError('duplicate boot arguments; input left unchanged')
    result = compact_panel(args) if options.compact_panel else args
    validate(result)
    if result != args:
        updated, count = re.subn(r'^args = ".*"$', lambda _: 'args = ' + json.dumps(result),
                                 original, flags=re.MULTILINE)
        if count != 1:
            raise ValueError('expected exactly one args line; input left unchanged')
        options.toml.write_text(updated)
    print(f'XNU 8796 boot arguments: {len(result.encode())}/{BOOT_LINE_LENGTH - 1} bytes')


if __name__ == '__main__':
    main()
