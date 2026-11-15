# 0228：逐命令 DSI 时钟引用

2026-11-14。实现提交 **`072200c`**，前置 `02abb3e`。在[a37/0227](PANEL-D1-0121A37.md)已验证普通息屏/唤醒及ESC0停启的基础上，补齐C08的逐命令本地DSI时钟引用。**本地实现与验证通过；2026-11-15已核对a38下载产物及ABI608镜像，待实机验证。** a37/mode7/PV仍是最新普通唤醒实机基线，完整C08和PD4继续进行中。见[a38镜像、核对证据与实机命令](PANEL-D1-0121A38.md)。

本批解决的是“发命令期间谁在使用时钟、何时可以归还引用”。每条命令不会无条件物理关钟：BOOT/managed外层使用者仍持有引用。完整DSI/MDP外层动态管理，以及相关状态丢失后独立重建，是后续范围。

## 实现与释放条件

mode2/7的同步亮度、FIFO短包和物理DMA长包统一经过：

```text
确认BOOT凭据、分支及父源可用 → 取得命令clock lease
→ 取得engine引用 → 实际传输 → fresh DONE + idle + 无错误
→ 恢复命令寄存器 → 释放engine引用 → 释放clock lease
→ 后续delay或帧流程
```

- 使用现有 `fClockLeases[4]`、`fClockReferences[]` 和 `fClockNextToken`；`fDsiTransferClock`保存当前命令票，没有独立的影子引用计数。
- 仅允许加入已经运行、已有引用的DSI0分支（mask `0x7f`）。取得前检查槽位、token耗尽、引用溢出、BOOT票状态/mask/uncertain/result、实际branch/parent以及转换/故障状态。
- 命令票的取得/归还不写时钟寄存器，不新增等待或timer。归还要求每个分支仍有其他引用；本批不会执行最后使用者的物理关钟。
- 命令票存在期间，普通release不能撤掉命令票、BOOT或managed票，`clockCanGate`也明确拒绝。正常完成后引用余额恢复。
- 超时、ACK错误、stale DONE、取消、stop及运行时失钟/票失效会保留实际引用。不能确认完成时，不继续DSI清理、TE恢复、停钟或撤供电。故障传播仍可mask/ACK已经归属的显示IRQ，不能概括为“故障后全部MMIO零写”。
- `releaseDSITransfer`由void改为bool，释放异常向上传播失败，不伪报完成。既有prepared 4KiB缓冲、独立DMA pin和命令/新帧完成门槛保留。

[生产补丁](../xnu/patches/0228-mh2lm-dsi-command-clock-leases.patch)只修改三份生产源码：`SweetMDSS.cpp`、`SweetMDSSClock.inc`、`SweetMDSSDSI.inc`。补丁SHA256：

```text
e62c0b50f3d32b5f4e080f07ee2284dedb653593ed56b7ee39aa5fd4d59c9a4c
```

公有Backend ABI、ABI608、ADT、605byte pipeline启动参数、固件、面板表和色彩/亮度政策保持。`SweetDisplayBackend.h`、`SweetFramebuffer.cpp`、Lifecycle/Link/Pipeline文件逐字节未变。

## Lineage参考及适配边界

固定 `ref/lge-lineage-kernel` 的 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。参考测试组合实际 `dsi_host_transfer`、`dsi_display_wake_up` 与既有真实clock-manager链；在DSI或MDP外层持票两种条件下，各执行17条命令及四种错误出口，确认engine在clock引用内、共享时钟保持运行。固定版本的 `wake_up` 为空函数。

18个原函数被提取并编入夹具，不代表每个函数及其全部分支都有执行覆盖。wire、command engine、OS和物理clock使用测试桩；SDE autorefresh、connector的MDP client及DSI post_enable/pre_disable仍有静态caller核对，未运行完整DRM/CCF或真实硬件。

Linux原路径在transfer错误后也归还票；XNU在硬件完成不确定时保留引用和pin，是明确的保护性适配。本批仍依赖BOOT/managed外层持票，尚未实现DSI post_enable撤长期票、pre_disable重取、完整MDP外层动态管理，或NoC/ARC/core/SMMU生命周期。普通唤醒已经可用，不等于所有共享状态丢失后均能独立重建；详见[全流程矩阵C08及PD4缺项](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)。

## 已完成的本地验证

证据目录为workspace `scratch/framebuffer-0228/`，入口 `final-review.json`。测试代码为[命令时钟测试](../tools/tests/test-mh2lm-command-clock.py)及[Lineage参考测试](../tools/tests/test-mh2lm-command-clock-reference.py)。

| 检查 | 结果与覆盖 |
|---|---|
| 19组驱动测试 | command-clock、command-lifecycle、brightness-lifecycle、color-lifecycle、link-lp-clock各mode2/7；panel-lineage-wake、panel-pipeline、display-clocks、host、dsi-sequence、supply-mode6、link-mode4；panel-lineage-fb两模式各120状态。全部通过。 |
| 新命令引用测试 | 两模式四profile各三轮OFF/ON及同步亮度，每次实际MMIO trigger检查真实lease；正常完成余额恢复且无物理关钟。覆盖17种准入拒绝、FIFO/DMA延迟完成、重复/stale token、timeout/ACK/cancel/stop保留引用，以及运行时失钟/票失效不继续DSI/TE清理。ASan/UBSan通过。 |
| Lineage参考链 | 固定源文件、真实host与clock-manager链的外层共享引用和错误出口通过，边界如上。 |
| ARM64单文件交叉编译 | a37基线及最终版本均成功；告警1599→1613，新增14条均为新有界数组访问的 `unsafe buffer access`，未抑制告警。不是完整云端构建结果。 |
| 补丁重放 | 单patch严格apply通过；0185–0228共44patch严格重放，80文件与edited/verify及记录SHA一致，无offset/fuzz。 |
| CircleCI配置 | 仅替换1条旧源码断言、新增3条，总149个source markers。YAML、4处合法参数模板、6段run shell及实际源码断言通过；workflow/executor/参数/资源保持，无非法 `<<`。未执行服务器验证或触发云构建，无新run ID。 |
| 打包门槛 | 增加启动声明及command-clock-held/released三个kernel markers。真实旧a37被新门槛拒绝、无输出生成，原a37镜像SHA保持。 |

测试期间修正了两处夹具断言：MMIO observer只在唯一实际寄存器write处计数；失钟故障只放行精确地址/值的合法显示IRQ mask/ACK，仍禁止DSI/TE/clock/supply写。初次失败记录保存在 `regressions.initial.json`、`test-command-clock-diagnostic.log`；对应两组修正后通过。最终结果见 `regressions.json`，没有改生产完成门槛来规避失败。

其他证据：`verification.json`、`strict-check.txt`、`cumulative-replay.json`、`command-clock-reference.json`、`base-SweetMDSS.compile.txt`、`verify-SweetMDSS.compile.txt`、`ci-check.json`、`packaging-gate.json`、`old-a37-rejected.txt`。检查/编译/重放脚本也保存在同目录。

保留的a37镜像：`out-bs/boot-16k-0121a37-abi608-panel-pipeline.img`，SHA256：

```text
ec7a22ab2f62af9a00b5937e5724725b5f750eed61dac57d1461c803cae89366
```

## 下一步：0121a38构建与实机

1. **产物与镜像已完成核对（2026-11-15）**：a38 kernel/buildtree完整，80源码匹配0228，149source/256kernel markers通过；现有ABI608两档镜像回读通过。证据 `scratch/artifact-0121a38/final-review.json`；尚无0228实机结果。下一步使用[a38 mode7镜像](PANEL-D1-0121A38.md)按下列步骤实测。
2. 新镜像交付后，先开USB采集，再由用户操作设备。确认首次桌面、触摸、按键及低/中/高调光；比较同一画面的亮度、色彩和持续刷新。
3. 两轮息屏约5秒/唤醒，每轮恢复正常后各保持亮屏至少60秒，同时留USB和持久日志。回归已有HS→ULPS→ESC halt→PHY关，以及ESC running→host/PHY→ON表→色彩→首帧→HS亮度→第二帧→触摸完成顺序。
4. 核对新命令引用日志与失败日志，确认命令完成后释放，再进入后续步骤。取得/释放INFO成对限前8次，日志停止输出不代表引用停止管理；普通ON的 `traceWake` 另有held/released事件，OFF不打印该ON专用跟踪。

```text
SWEET MDSS DSI CLOCK: per-transfer leases enabled; shared BOOT/core retained
SWEET MDSS DSI CLOCK: acquired token=... mask=...; running branches shared
SWEET MDSS DSI CLOCK: released token=... after command idle; shared owners retained
... step=command-clock-held ...
... step=command-clock-released ...
```

本批不修首次亮度状态发布BUG-008或音量显示BUG-009；BUG-007在a37未复现但未证明修复，继续暂缓观察。BUG-006日志问题也保留，见[Bug台账](BUGS.md)。a38通过后推进C08剩余外层引用及其余PD4，不将本批认定为完整资源管理或节电验收。
