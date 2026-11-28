#!/usr/bin/env python3
"""Every driver translation unit under iokit/Kernel/arm must be in the build list.

A new .cpp that nobody registered still compiles - its headers are included by
its neighbours, so the compiler is happy - and then the kernel fails to link
with every symbol the file was supposed to define.  That is a whole build round
lost for a missing line in iokit/conf/files.arm64, so the list is checked here.

The scanner is also driven against a fabricated listing so that a check which
can never fail is itself caught.
"""
from pathlib import Path
import sys

tree = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else \
    Path(__file__).resolve().parents[2]


def listed(listing):
    """The relative paths in a files.* listing, in file order."""
    names = []
    for line in listing.read_text().splitlines():
        line = line.split('#')[0].strip()
        if line:
            names.append(line.split()[0])
    return names


def unlisted(root, names, tree):
    """Units under root that the listing does not name, sorted."""
    known = set(names)
    found = []
    for source in sorted((root).rglob('*.cpp')):
        rel = source.relative_to(tree).as_posix()
        if rel not in known:
            found.append(rel)
    return found


build_list = tree / 'iokit/conf/files.arm64'
assert build_list.is_file(), 'iokit/conf/files.arm64 is missing'
arm = tree / 'iokit/Kernel/arm'

names = listed(build_list)
missing = unlisted(arm, names, tree)
assert not missing, ('these translation units are in the tree but not in the '
                     'build list, so the kernel links without them: ' +
                     ', '.join(missing))

# The scanner has to be able to fail: drop one name and it must name that file,
# and only that file - a control that names everything proves nothing either.
victim = next(name for name in names if name.startswith('iokit/Kernel/arm/'))
pruned = unlisted(arm, [name for name in names if name != victim], tree)
assert pruned == [victim], 'the scanner does not isolate a dropped unit: %r' % (pruned,)

# A duplicate would build the unit twice, which the link would report as a
# second definition - a different failure with the same cause, a bad edit here.
seen = set()
for name in names:
    assert name not in seen, 'listed twice: ' + name
    seen.add(name)

# The units this card added are the reason the check exists; keep them pinned so
# that a future weight-loss pass cannot quietly drop one of them again.
for unit in ('iokit/Kernel/arm/SweetAppsDoor.cpp',
             'iokit/Kernel/arm/SweetAppsLine.cpp'):
    assert unit in seen, 'not in the build list: ' + unit

print('test-mh2lm-build-file-list: %d units listed, %d sources under arm, '
      'none missing' % (len(names), len(list(arm.rglob('*.cpp')))))