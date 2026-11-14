# 0223：Lineage 命令生命周期与缓冲准备

2026-11-14。用户授权将驱动逻辑及命令缓冲准备一起对齐。0223 已实现并完成本地验证，待新 CircleCI 内核和实机。a31/mode2 保留为最后成功实机基线；a32/mode7 首次接管成功、息屏后无法唤醒，不能把本次逻辑修正先记为实机修复。

补丁：`xnu/patches/0223-mh2lm-lineage-command-lifecycle.patch`；SHA256：`946daee753f31c81669b5a47b23e4f33330f59b9583bf6126bb177d89853fc83`。
在 0222 后修改七份 MDSS 源文件，无新 ADT 映射、boot arg、固件或 framebuffer ABI。
参考仍是 LG Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`，不是本机 Android 寄存器实测。

## 本次对齐

| 环节 | 0223 行为与依据 |
|---|---|
| 正常 CTL 路径 | mode7 首次接管及每次唤醒重建固定配置，不再写 CTL_SW_RESET。Lineage `sde_encoder_phys_cmd_enable` 正常调用链没有该步骤，`_sde_crtc_reset_hw` 的调用受 `reset_req` 保护。已有 reset/busy/flush、其他 CTL 占用、资源或域异常仍拒绝；不自动 reset 恢复。 |
| TE/flush | mode2/7 的 external TE 在 flush 前连接；mode7 顺序为 tearcheck 配置、清旧 PP_DONE、IRQ rearm、TE、INTF 子 flush、主 flush、autorefresh。依据是 `msm_atomic_helper_commit_modeset_enables` 的 bridge pre-enable → encoder enable → KMS commit → bridge enable，以及 encoder enable helper 中的 `sde_encoder_control_te(true)`。 |
| tearcheck | OFF 排空扫描后写 INTF1+0x284=0，再释放显示引擎引用、发送 OFF。ON 在恢复扫描前重新启用；mode7 重配完整参数。mode2 不再要求停驻时该位恒为1。依据是 command encoder disable 的 `enable_tearcheck(false)`。 |
| 命令引擎 | 活动 continuous splash 对应一个显示引用；OFF 表前释放。每条 FIFO/物理 DMA 命令获取单次传输引用，fresh DMA_DONE 且 MDP/DMA idle 后释放，再执行命令间延时。ON 整表完成才重新持有显示引用。两类使用者均由原 MDSS gate 串行化，没有额外 owner。同步亮度也经过同一引用管理。 |
| host reset | wake 的 `cmn_soft_reset` 恢复原控制值，不再强置 command bit；DDIC reset 时 controller 开、command engine 关，ON 命令临时启用。 |
| 命令缓冲 | 一个 prepared、低4GiB、物理连续的4KiB缓冲服务 FIFO 和长包命令，跨 OFF/ON 保留，普通唤醒不再分配。独立 DMA 引用在真实完成前保持，service free 不能替代完成。 |

Lineage 的 `dsi_host_transfer` 对每次普通命令（包括短命令）都会确保4KiB缓冲存在：启用/获取 command engine → 首次懒分配 GEM → transfer → 释放 engine。它使用 unsecure IOVA。
**本实现明确保留两个适配差异：短 FIFO/长物理 DMA 是用户指定方案；mode2/7 的物理缓冲在服务发布前准备，比 Lineage 首次 transfer 懒分配更早。**
这样避免在活动 lifecycle workloop/gate 内首次调用可能阻塞的物理连续分配器；分配仍可能在启动阶段等待，不宣称有可打断期限。准备失败则服务不发布。其他旧模式的异步 sequencer 仍在首次命令准入时准备，但短命令也会准备。

## 保留的完成与退出条件

配置代数、flush 清零、fresh PP_DONE 和 RD_PTR 前进仍分别验收，之后才能恢复触摸及完成 framebuffer 唤醒。没有用配置读回替代这些条件。SMMU route/fault、资源票、IRQ drain、生命周期参与者、扫描池 pin 及触摸 fence 保留。

每条异步命令完成前的 timeout/error/cancel/stop 都保留未决传输引用；长包另外保留 prepared buffer pin。部分命令表失败仍视为 DDIC 状态不确定，不重试、不撤资源。同步 FIFO 亮度增加 fresh DONE、idle、ACK/error 校验；异常时不 reset FIFO 或恢复 TE 来假定成功。

共享 SMMU/core、VBIF/QoS、DSPP/部分 PP 状态和 ABL 首帧依赖仍存在。完整错误恢复、无 ABL 冷启动、整显示域 collapse 和完整 PD4/PD5 不在本补丁的完成声明中。

## 本地验证

- mode2、mode7 各四 profile × 三轮 OFF/ON，保持原命令字节、FIFO/长包边界和延时。mode7 注入固定配置丢失后重建，普通路径零 CTL reset 写；两处配置 continuation 覆盖 deadline、取消、stop、provider/domain/DSI/resource/IRQ/CTL 冲突及外部 reset 正在进行。配置等待期间拒绝撤回 boot clock。
- 新 `test-mh2lm-command-lifecycle.py` 在 mode2/7 验证启动分配/prepare/物理范围七类失败的清理、FIFO 首命令、四 profile × 三轮缓冲复用、逐命令引擎引用、reset 时关闭及 ON 整表后恢复显示引用。启动后注入分配失败不影响 OFF/ON，分配/prepare 计数不增长；覆盖 FIFO/DMA timeout/error/cancel/stop、同步亮度 missing DONE/BUSY/ACK/error/stale DONE。
- mode2/7 各60份真实 MDSS typed 状态经真实 framebuffer 消费，含排队 ON、pending pixels/touch、通用和 pipeline 凭据损坏拒绝。两进程状态回放，不等于实机并发 workloop 验证。
- DSI sequencer、host、PHY/link、profile lifecycle、旧 mode6 supply lifecycle、通用 display service 回归通过。实际驱动配合 OS/MMIO/电气状态夹具，ASan/UBSan；LSan 禁用，模拟 reboot 后对象/映射/prepare-complete 计数检查清零。
- 新 `mh2lm_lineage_upper_flow.py` 静态检查真实上层调用点、顺序、reset 条件与引用语义。另执行14个未改写的 Lineage 底层函数比对寄存器配置；CTL reset helper 的自测轨迹与普通 enable 轨迹分开。前者不是 DRM 栈执行，后者的 main 仍为手工组合，两者边界明确。
- MDSS 修改前/后实际 ARM64 对象编译通过，clang 汇总1596→1596，逐条 warning 文本/类别/计数相同（含额外编译器提示的日志匹配数均1598）。没有完整 macOS 链接结果。
- 0185–0223 共39补丁严格无 offset/fuzz 重放，79文件逐字节一致，132项 CI source markers。新的打包门实际拒绝 a32 内核，未生成 boot 输出。ADT/参数/ABI608固件不变，本轮无需重新编固件。

底稿：workspace `scratch/framebuffer-0223/`，含 base/edited/verify/replay、对象与告警比较、回归日志、两档 bridge 状态、补丁与打包拒绝证据。

## 下一步

用户 push 最新 main 后由 CircleCI 编译，下载新 `xnu-kernel-0121a33.gz` 和 `xnu-buildtree-0121a33.gz` 到 workspace logs。Agent 核对0223及源码/内核 markers，再沿原 ABI608 manifest 打 `--panel-pipeline`、mode7。当前尚无 a33 内核/镜像，不重打旧 a32，也不重测 a31。

实机先确认启动时 `command buffer ready bytes=4096`、generation1 和桌面更新，再做一次 OFF/ON。检查显示引用0、OFF complete、host/DDIC reset、ON逐包完成、显示引用1、generation2/fresh pixels、触摸及 FB owned resume；保存 USB 和持久日志。

a32 持久日志最后仍早于 OFF；USB 末行只是 `dma-allocate-enter`，打印卡住且持续采集时长不确定。该事实不能证明分配死锁，也不能证明本次修正已经消除黑屏，详见 [a32 审计](PANEL-D1-0121A32.md)。
