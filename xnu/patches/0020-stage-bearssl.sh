#!/bin/bash
# Pinned, portable constant-time AES for the SWEET legacy CBC/ECB API.
set -euo pipefail
[[ "${MACHINE_CONFIG:-}" == SWEET || "${MACHINE_CONFIG:-}" == GUACAMOLE || "${MACHINE_CONFIG:-}" == MH2LM ]] || exit 0
[[ -f libkern/crypto/corecrypto_aes.c ]] || exit 1
bearssl_sha=7bea48e5e850ab4cafbe68d3765cdaba13a86d6f
bearssl_source="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bearssl-vendor"
[[ -f "$bearssl_source/LICENSE.txt" ]] || {
    echo "Missing vendored BearSSL source: $bearssl_source" >&2
    exit 1
}
python3 - "$bearssl_source" <<'PY'
from pathlib import Path
import shutil
import sys

source = Path(sys.argv[1])
target = Path('libkern/crypto/sweet_bearssl')
if target.exists():
    raise SystemExit(f'refusing to overwrite existing AES source: {target}')
shutil.copytree(source, target)
PY
echo "Staged vendored BearSSL portable AES, MD5, SHA-1 and SHA-384/512 ($bearssl_sha) for SWEET"
