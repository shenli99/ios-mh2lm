#!/usr/bin/env python3
"""Build the actual Mu mh2 firmware with the bounded ABL panel adapter.

Local files only. Copies the platform and loader to an output tree, applies
the existing loader patch, and preserves the baseline BootShim and appended
DTB bytes. Never changes the known kernel-32m.blob or operates a device.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import lzma
import os
import shutil
import struct
import subprocess
import zlib


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(args):
    repo = Path(__file__).resolve().parents[1]
    workspace = repo.parents[1]
    mu = (args.mu or workspace/'ref/Mu-Silicium').resolve()
    unixnu = (args.unixnu or workspace/'ref/UniXNU-src').resolve()
    baseline = (args.baseline or workspace/'out-bs/kernel-32m.blob').resolve()
    out = args.output.resolve()
    if out == baseline.parent or baseline.is_relative_to(out):
        raise ValueError('output must be a separate directory from the baseline')
    if (out/'panel-uefi.json').exists() or (out/'source').exists():
        raise ValueError('output already has build files; choose a fresh output directory')
    out.mkdir(parents=True, exist_ok=True)
    # gzip has an appended original DTB: gzip.decompress would incorrectly
    # treat those bytes as another gzip member. Keep unused_data verbatim.
    original = baseline.read_bytes()
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(original)+decoder.flush()
    if not decoder.eof or len(raw) != 112+0x300000:
        raise ValueError('expected the current 112-byte BootShim + 3MiB FD baseline')
    shim, tail = raw[:112], decoder.unused_data
    if (shim[56:60] != b'ARM\x64' or
        struct.unpack_from('<QQ', shim, 8) != (0xa0000000, 0x300000) ):
        raise ValueError('baseline BootShim/FD geometry is unexpected')
    # The known baseline appends two board DTBs, not a single DTB. Preserve
    # every byte and validate only their bounded concatenation/header lengths.
    offset, dtb_count = 0, 0
    while offset < len(tail):
        if len(tail)-offset < 40 or tail[offset:offset+4] != b'\xd0\x0d\xfe\xed':
            raise ValueError('unexpected appended DTB sequence')
        size = struct.unpack_from('>I', tail, offset+4)[0]
        if size < 40 or size > len(tail)-offset:
            raise ValueError('invalid appended DTB length')
        offset += size
        dtb_count += 1
    if not dtb_count:
        raise ValueError('baseline has no appended DTB')
    source = out/'source'
    ignore = shutil.ignore_patterns('.git', '__pycache__')
    shutil.copytree(unixnu/'package', source/'package', ignore=ignore, dirs_exist_ok=True)
    loader = source/'drivers/DarwinBoot'
    shutil.copytree(unixnu/'drivers/DarwinBoot', loader, ignore=ignore, dirs_exist_ok=True)
    patch = repo/'boot-support/0001-mh2lm-darwinboot-panel-selection.patch'
    for flags in [['--check'], ['--verbose']]:
        result = subprocess.run(['git', 'apply', *flags, '-p3', str(patch)],
                                cwd=loader, text=True, capture_output=True)
        if result.returncode or 'offset' in result.stderr.lower() or 'fuzz' in result.stderr.lower():
            raise RuntimeError('loader patch is not a strict match: '+result.stderr)
    driver = loader/'DarwinBootPkg/Driver'
    for name in ['panel_handoff.c', 'panel_handoff.h', 'panel_identity.h']:
        if (driver/name).read_bytes() != (repo/'boot-support/panel-handoff'/name).read_bytes():
            raise ValueError('loader parser differs from the tested source: '+name)
    platform = out/'platform/mh2Pkg'
    shutil.copytree(mu/'Platforms/LG/mh2Pkg', platform, dirs_exist_ok=True)
    dsc = platform/'mh2.dsc'
    text = dsc.read_text()
    old = 'OUTPUT_DIRECTORY               = Build/mh2Pkg'
    if text.count(old) != 1:
        raise ValueError('unexpected platform OUTPUT_DIRECTORY')
    dsc.write_text(text.replace(old, 'OUTPUT_DIRECTORY               = '+str(out/'Build')))
    bt = mu/'Mu_Basecore/BaseTools'
    env = dict(os.environ, WORKSPACE=str(mu), EDK_TOOLS_PATH=str(bt),
               EDK_TOOLS_BIN=str(bt/'Source/C/bin'), PYTHON_COMMAND='python3',
               CLANG_BIN='/usr/lib/llvm-19/bin/', PYTHONPATH=str(bt/'Source/Python'))
    env['PATH'] = str(bt/'Source/C/bin')+':'+str(bt/'BinWrappers/PosixLike')+':'+env['PATH']
    env['PACKAGES_PATH'] = ':'.join(map(str, [out/'platform', mu, mu/'Mu_Basecore',
        source/'package', loader, mu/'Platforms/LG', mu/'Common/Mu',
        mu/'Common/Mu_OEM_Sample', mu/'Silicon/Qualcomm', mu/'Silicon/Silicium',
        mu/'Silicium-ACPI/Platforms/LG', mu/'Binaries', mu/'Silicon']))
    cmd = ['python3', str(bt/'Source/Python/build/build.py'), '-p', str(dsc),
        '-a', 'AARCH64', '-t', 'CLANGPDB', '-b', 'RELEASE', '-n', '3',
        '-D', 'ENABLE_SECUREBOOT=0', '-D', 'DEVICE_MODEL=mh2',
        '-D', 'FD_BASE=0xA0000000', '-D', 'FD_SIZE=0x300000', '-D', 'FD_BLOCKS=0x300']
    print('Building complete Mu mh2 RELEASE FD; log: '+str(out/'build.txt'), flush=True)
    with (out/'build.txt').open('w') as log:
        result = subprocess.run(cmd, cwd=mu, env=env, text=True, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError('firmware build failed; see '+str(out/'build.txt'))
    fd = out/'Build/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd'
    data = fd.read_bytes()
    if len(data) != 0x300000 or data[40:44] != b'_FVH':
        raise ValueError('unexpected complete FD size/header')
    # GenFds must actually consume this module, rather than an old module
    # cached elsewhere. The produced FFS embeds this exact uncompressed PE.
    guid = '1B64AF87-8415-4B57-9B16-680D70913B9F'
    ffs = out/'Build/RELEASE_CLANGPDB/FV/Ffs'/f'{guid}DarwinBoot'/f'{guid}.ffs'
    module = out/'Build/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
    if module.read_bytes() not in ffs.read_bytes():
        raise ValueError('generated DarwinBoot FFS does not contain the new PE module')
    # Final FDF's compressed FVMAIN consumes the generated FFS. Require it
    # in FVMAIN.Fv too; the outer compression is emitted by this same build.
    main_fv = out/'Build/RELEASE_CLANGPDB/FV/FVMAIN.Fv'
    ffs_in_fv = bytearray(ffs.read_bytes())
    # GenFv complements the FFS State byte for erase-polarity=1 volumes.
    if not struct.unpack_from('<I', main_fv.read_bytes(), 44)[0] & 0x800:
        raise ValueError('expected erase-polarity=1 FVMAIN')
    ffs_in_fv[23] ^= 0xff
    if bytes(ffs_in_fv) not in main_fv.read_bytes():
        raise ValueError('FVMAIN does not contain the newly built DarwinBoot FFS')
    container = out/'Build/RELEASE_CLANGPDB/FV/Ffs/9E21FD93-9C72-4c15-8C4B-E77F1DB2D792FVMAIN_COMPACT'
    guided = (container/'9E21FD93-9C72-4c15-8C4B-E77F1DB2D792SEC1.guided').read_bytes()
    data_offset = struct.unpack_from('<H', guided, 20)[0]
    if (data_offset != 24 or guided not in data or
        main_fv.read_bytes() not in lzma.decompress(guided[data_offset:])):
        raise ValueError('final FD does not contain the newly built compressed FVMAIN')
    blob = out/'kernel-panel.blob'
    blob.write_bytes(gzip.compress(shim+data, mtime=0)+tail)
    verify = zlib.decompressobj(31)
    unpacked = verify.decompress(blob.read_bytes())+verify.flush()
    if unpacked != shim+data or verify.unused_data != tail or baseline.read_bytes() != original:
        raise ValueError('packaged firmware did not preserve shim/DTB/baseline')
    manifest = dict(abi=1, panel_handoff=True, firmware='Mu mh2 RELEASE',
        blob=str(blob), blob_sha256=sha(blob.read_bytes()), fd=str(fd), fd_sha256=sha(data),
        shim_sha256=sha(shim), appended_dtb_sha256=sha(tail), appended_dtb_count=dtb_count, baseline_sha256=sha(original),
        darwinboot_sha256=sha(module.read_bytes()), darwinboot_ffs_sha256=sha(ffs.read_bytes()),
        loader_patch_sha256=sha(patch.read_bytes()),
        loader_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=unixnu/'drivers/DarwinBoot', text=True).strip(),
        mu_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=mu, text=True).strip(),
        build_command=cmd, full_fd_built=True, hardware_tested=False)
    (out/'panel-uefi.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Verified firmware bundle: '+str(out/'panel-uefi.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--mu', type=Path)
    parser.add_argument('--unixnu', type=Path)
    parser.add_argument('--baseline', type=Path)
    build(parser.parse_args())
