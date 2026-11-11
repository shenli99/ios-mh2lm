# MH2LM panel selection handoff

The current phone boots through Mu Silicium and DarwinBoot. The old `shim/`
loader is not the active path and is unchanged by this work.

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
mode1 to inspect the record; mode2 is the explicit retained-supply full command /
reset experiment once the selection has been checked. No default is changed.

The actual Mu consumer module compiled and linked locally. This did not rebuild
a complete firmware FD, change the existing `out-bs/kernel-32m.blob`, or validate
booting the phone. The parser and the actual adapter also run under ASan/UBSan
with HOB, memory service and allocation failures; every FDT/ADT truncation and
bit mutation is checked. Source copies in this patch and `SweetPanelIdentity.h`
are checked for byte equality by `test-mh2lm-panel-handoff.py`.

中文记录、架构与后续边界见
[PD3完整命令和保留供电的恢复](../docs/PANEL-PROFILE-D1.md)。本机仍未读取到live label；
不猜PV，不启动Android，不试发未知DDIC读命令，不撤共享供电。
