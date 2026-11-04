# BearSSL source used by XNU

`0020-stage-bearssl.sh` originally fetched BearSSL commit
`7bea48e5e850ab4cafbe68d3765cdaba13a86d6f` from the author's Git
server. The server returned HTTP 502 during the 0114a12 CircleCI build.
`bearssl-vendor/` is the 30-file staged source from an earlier successful
CircleCI build tree. A second local staged tree was byte-identical. The
staged source includes the original MIT `LICENSE.txt` and the existing
XNU-specific `src/inner.h` change that selects `<machine/limits.h>` when
`KERNEL` is defined. No other BearSSL source change is made by the stager.

The source-tree fingerprint (sorted relative paths, each followed by NUL
and its file bytes, SHA-256) is
`acb7defd7dffba7db5516894c58bf7371f3e95d63bb20eddf27c000cd55ceb49`.
The source was copied from
`scratch/ci-mh2lm-9/bt/x/xnu/libkern/crypto/sweet_bearssl/`; the matching
copy was `scratch/sweet-tree/libkern/crypto/sweet_bearssl/`. These paths are
local provenance only; CI needs neither of them. The original upstream Git
object was not available to recheck while preparing this vendor snapshot.
Upstream whitespace is retained byte-for-byte; `.gitattributes` exempts only
this vendor tree from Git's whitespace check.

The locally cloned `julianpas/BearSSL` mirror has only a 2018 commit and
does not contain the pinned commit. The BearSSL 0.6 release has not been
verified byte-identical to the pinned Git commit and must be compared and
tested before using it as a replacement.
