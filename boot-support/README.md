# MH2LM panel selection handoff

The current phone boots through Mu Silicium and DarwinBoot. The old `shim/`
loader is not the active path and is unchanged by this work.

**0121a14 ABI correction:** `0002-mh2lm-darwinboot-xnu8796-bootargs.patch`
matches the actual XNU 8796 ARM64 `boot_args`: CommandLine is 608 bytes,
BootFlags is at 0x2d0 and MemorySizeActual at 0x2d8. The previous loader's
1024-byte layout is incompatible: long arguments become the kernel's memory
size. Both patches are applied in order to an isolated loader copy. The new
manifest records `xnu_boot_args_abi=xnu8796-arm64-608`; the panel packager rejects
older manifests and arguments above 607 bytes, without truncating them.
See [evidence and validation](../docs/BOOTARGS-ABI-0121A14.md).

`0001-mh2lm-darwinboot-panel-selection.patch` adds the board adapter and the exact
bounded C parser in `panel-handoff/` to the existing DarwinBoot component. It
runs after ADT validation and before the kernel/window copies. A missing ADT slot
leaves the old path alone; an empty slot is filled only from Mu's original ABL
x0 HOB. Unknown or ambiguous labels never authorize a vendor table. A prefilled
or malformed slot is a load error. This is bootloader selection evidence, not a
physical DDIC read or an authenticated identity.

The tested source is the local `UniXNU-src` parent
`f86227c39bc4742dccb3c7d5f2eca6c4854740d5`, DarwinBoot component
`4ad0c423361fa7added7dd8a5d6589c1c51af3b1`, with the existing MH2LM loader changes.
This is a separate loader patch, **not an XNU patch**; keep it out of
`xnu/patches/`. In the matching component checkout it can be checked and applied
with `git apply --check -p3 PATCH` / `git apply -p3 PATCH`.
Rebuild DarwinBoot through the consuming Mu mh2 platform, incorporate it into a
new FD, then package that FD into a new temporary boot image. Updating only the
kernel while reusing the old UEFI blob cannot produce the record.

Reserve the empty slot using `tools/mk-mh2lm-mdss-adt.py --panel-identity`; the
option accepts no manual revision. Compose with `--panel-pins --panel-touch
--display-noc` as required by the actual driver stage. Boot first with lifecycle
mode0 (the metadata-only `panel-identity` profile) to inspect the record; mode2 is the explicit retained-supply full command /
reset experiment once the selection has been checked. No default is changed.

As of 0201, `tools/build-mh2lm-panel-uefi.py` has built the **complete Mu mh2
RELEASE FD** locally, including this adapter. It copies platform/loader sources
to an isolated output directory, strictly applies the loader patch, and verifies
the actual PE -> FFS -> FVMAIN -> compressed guided FV -> final FD chain. It
preserves the baseline BootShim and both appended DTBs and leaves the existing
`out-bs/kernel-32m.blob` unchanged. This is not a device boot result.

The current local manifest is
`/home/shenli/work1/out-bs/panel-uefi-0121a14-bootargs608/panel-uefi.json`;
the blob SHA256 is
`27e7a1d1a3f9421af18d02f216e4e7d651137d42f2955be775f9142f118ea480`.
Source HEADs, build command and FD/module/blob hashes are in the manifest.
Reproduce with local Mu/UniXNU sources and toolchain already installed:

```sh
python3 tools/build-mh2lm-panel-uefi.py --output /home/shenli/work1/out-bs/panel-uefi-NEW
```

Use a fresh output directory. The kernel matrix `--panel-lifecycle` requires
`SWEET_PANEL_UEFI_MANIFEST` and checks its ABI, full-FD result and blob hash;
it refuses to silently reuse the old firmware. Firmware binaries and board DTBs
remain outside git. The earlier 0195 result built only the consumer module;
that limitation is superseded by this complete-FD result, not by a phone test.
The earlier 0201-final bundle is preserved for diagnosis and must not be used
for new panel boots: it predates the boot_args ABI correction.

The parser and the actual adapter also run under ASan/UBSan
with HOB, memory service and allocation failures; every FDT/ADT truncation and
bit mutation is checked. Source copies in this patch and `SweetPanelIdentity.h`
are checked for byte equality by `test-mh2lm-panel-handoff.py`.

中文记录、架构与后续边界见
[PD3供电生命周期与四档验收](../docs/PANEL-SUPPLY-LIFECYCLE-D1.md)。0121a14的identity-slot-control
已实测取得source2/profile4，对应ABL选中的PV label，详见
[实测日志与启动对照](../docs/PANEL-D1-0121A14.md)；不是物理DDIC读回，完整reset/供电周期仍待验收。
