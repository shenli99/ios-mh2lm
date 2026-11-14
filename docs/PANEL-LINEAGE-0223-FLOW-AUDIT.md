# 0223 与 Lineage 上层显示流程复核

**后续全面检查已完成本轮源码审计：[64项统一矩阵](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)。** 本文为上一轮四项专项记录，不是剩余差异的完整清单。全面检查补入亮度HS/LP、ON表外色彩/锐度恢复、HS开启内部顺序、持续刷新/资源/功能/退出范围；本轮未修改0223驱动。

2026-11-14，用户要求再次比较。**0223 修正了上一轮确认的局部差异，整体流程仍未完全对齐。** 本轮只审计和更新文档，没有修改驱动、生成新内核或验证实机。

比较基线：XNU 实现 `6ff3085`、交接 `3a0533a`，最终源码为 workspace `scratch/framebuffer-0223/verify/iokit/Kernel/arm/`；补丁 SHA256 `946daee753f31c81669b5a47b23e4f33330f59b9583bf6126bb177d89853fc83`。Lineage 是 `ref/lge-lineage-kernel` 的 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`，工作树干净。下文 Lineage 文件位于 `drivers/gpu/drm/msm/`，另标路径除外；行号对应这两个固定版本。

## 已对齐的具体环节

| 环节 | 当前结论 |
|---|---|
| 正常 CTL reset | mode7 首次/唤醒不再无条件 CTL_SW_RESET。Lineage command encoder 正常 enable 没有这一步；CRTC reset 受错误恢复条件保护。 |
| external TE 与 flush | mode2/7 在 flush/kickoff 前连接 TE。实际依据是 `msm_atomic.c:515` 的 bridge pre-enable → encoder enable → KMS commit → bridge enable，以及 encoder enable helper 的 TE 操作。 |
| tearcheck | OFF 排空扫描后关闭，ON 恢复；与 command encoder disable/enable 的方向一致。此项不代表整个停扫/TE 生命周期一致。 |
| command engine | OFF 表前释放显示引用，每条命令独立获取/完成后释放传输引用，ON 整表完成后再持有显示引用。wake soft reset 恢复保存的控制值，不强开 command bit。 |
| 面板命令/供电大顺序 | 既有 PV ON/OFF 字节及延时、普通 OFF 后 reset/panel 票撤销、ON 先恢复供电/链路再 reset 和发 ON 表保持。底层细分时序仍有下述差异。 |

命令缓冲现在跨 OFF/ON 保留，唤醒不再首次分配。但它提前到服务发布前准备，Lineage 仍是在首个 host transfer 内懒分配，不能把两者称为分配时机完全一致。

## 尚未对齐的正常流程

### 1. OFF 的 HS 时钟与 ULPS 顺序

这是本轮新确认、此前 0220/0223 对齐说明遗漏的差异。实际调用链是：

- Lineage：HS pre-off 关闭 PHY clock gating → 释放 HS clocks → LP pre-off 进入 ULPS → 释放 escape clock → CORE pre-off 执行 PHY idle-off、clamp/reset_config。
- 当前 XNU：进入 ULPS 并检查状态 → 关闭 PHY clock gating → 关闭 pixel/intf/byte 分支 → 停 PLL/bias → PHY idle-off/reset mask。

依据：`dsi_clk_manager.c:993` 先调用 HS OFF，再调用 LP OFF；`:831` 的 wrapper 在对应时钟操作之前执行 pre-off callback。`dsi_display.c:3386` 将 ULPS 放在 **LP** 回调，关闭 PHY clock gating 放在 **HS** 回调。不能按这两个 if 在 callback 文件中的书写位置推断执行先后。

HS 确实是同一组硬件分支：`dsi_clk_manager.c:469` 操作 byte_intf/pixel/byte；`arch/arm64/boot/dts/qcom/sm8150-sde.dtsi:431` 将它们映射至 DSI0，`drivers/clk/qcom/dispcc-sm8150.c:943`、`:1426` 对应 DISPCC `0x202c/0x2004/0x2028`。XNU `SweetMDSSLink.inc:14`、`:165` 使用这三个地址。其内部关闭顺序 pixel→intf→byte 也不同于 Lineage 的 intf→pixel→byte。

Lineage 的 `clk_disable/unprepare` 受时钟引用计数约束，不能由上述调用顺序推断每次物理 PLL 必然断电。可以确定的是 **HS 释放与 ULPS 请求的相对顺序不同**。当前 XNU 还保留共享 core/escape 等资源，未实现完整 LP/CORE 投票生命周期。唤醒的 PHY 配置/ULPS 重入→HS 启动→resync/ULPS 退出的大顺序已具备，不等于整个 clock manager 已移植。

### 2. LG 亮度恢复晚于当前 XNU

XNU `SweetMDSSLifecycle.inc:544` 的普通 ON 是：ON 表完成 → 显示引擎引用 → 恢复亮度命令 → restore stream/配置与 kickoff → fresh pixels → 触摸。

Lineage 普通 DPMS OFF/ON 使用 LG 的亮度状态管理：

1. `sde_connector.c:423` 的 power 回调绑定 `sde_kms.c:1415` 的 `dsi_display_set_power`。`dsi_display.c:1217` 在 OFF 设置 `panel->lge.allow_bl_update=false` 并清空 pending level；ON 不直接把该字段设回 true。
2. `msm_atomic.c:515` 先执行 KMS commit，再执行 bridge enable。`sde_connector.c:718` 的 bridge enable 调用 backlight update；LG override `lge/brightness/lge_brightness.c:292` 在不允许更新时先保存 pending brightness。
3. `msm_atomic.c:588` 之后是 wait_for_commit_done、complete_commit；`sde_kms.c:1200` → `dsi_drm.c:1035` → `dsi_display.c:7666` 的 post_kickoff，在 power ON、pending≥0、allow=false 时调用 `lge_update_backlight`。后者在 `lge_brightness.c:167` 真正发送并开放后续更新。

这是目标 LG 路径，不只是假设 Qualcomm 通用实现；`arch/arm64/configs/vendor/mh2lm_defconfig:896` 启用 `CONFIG_LGE_DISPLAY_COMMON`。**PV 未配置 `qcom,bl-update-flag` 只表示没有额外的 first-frame 等待选项，不代表亮度应在 kickoff 前恢复。** 也不应把软件 post-kickoff 自动等同于任何错误情况下都成功完成首帧。

这项先亮度后像素的 XNU 顺序在 a31 已存在，不能把它单独归为 0223 新回退或 a32 已证实故障原因。

### 3. 首次接管仍不是 Lineage continuous splash 路径

XNU `SweetMDSSResources.inc:168` 仍进入 ManagedLink→ManagedHost；`SweetMDSSLink.inc` 会停链路/PLL、执行 PHY reset、重新配置并 resync，`SweetMDSSHost.inc:82` 仍做 host soft reset。0223 取消的是 **CTL reset**，没有同时取消这些 DSI/PHY 操作。首次接管不发 panel reset/ON 表也不足以证明无扰动继承。

Lineage 的 splash 分支：`dsi_display_phy_sw_reset` (`dsi_display.c:2949`) 提前返回；`dsi_phy.c:946` 跳过 PHY hardware enable；`dsi_display_prepare` (`:7286`) 跳过 panel pre-prepare 和 host soft reset/panel prepare；`dsi_display_enable` (`:7791`) 走 splash 状态接收/资源交接；`dsi_post_clkon_cb` (`:3475`) 在 splash 时还特意跳过 resync FIFO toggle。

因此当前是有停扫和重建的 warm takeover。这个差异早于 0223，且 a31/a32 首次接管已可用；它仍属于与 Lineage 首次接管逻辑未对齐的范围。

### 4. autorefresh 固定策略与 Lineage 默认提交不同

XNU mode7 `SweetMDSSPipeline.inc:117` 固定写 `INTF+0x2b4=0x80000001`，不写 CTL_START。Lineage `sde_connector.c:2372` 的 autorefresh 属性默认是 **0**；`sde_encoder_phys_cmd.c:1677` 仅在属性非零时采用 autorefresh，否则调用 CTL_START。

Lineage 支持当前这类 autorefresh 模式，但不能把固定1称为与其默认提交行为一致。尚无同机 Android 用户态实际属性值，不能断言 Android 实机一定使用0。其已开启 autorefresh 时的 `prepare_commit` (`:1629`) 还执行 TE off→autorefresh off→等待 PP transfer→TE on；当前停扫/命令流程保持 TE 关闭的跨度不同。

## 明确保留的适配与未完成范围

| 项目 | 当前差异及边界 |
|---|---|
| 命令缓冲/地址 | 用户指定短 FIFO、长物理 DMA。4KiB 物理缓冲提前 prepare；FIFO 从栈上编码 words 写入 FIFO，**不通过该 descriptor 搬运字节**，只有长包拷入缓冲。Lineage 普通 host_transfer 使用懒分配 GEM/IOVA。 |
| 每条命令的时钟/停扫 | Lineage `dsi_display.c:3000` 为每次 transfer 获取 ALL_CLKS→wake_up→engine→transfer→释放 engine/clocks；post_enable 撤掉 display clock vote，pre_disable 重新取得。XNU 依赖已持有的资源，在整个异步序列/同步亮度期间暂停 TE/autorefresh，没有对应的逐命令动态投票。释放投票不等于每包后物理关钟。 |
| 完成通知 | Lineage `dsi_ctrl.c:1304` 开 DMA_DONE IRQ 并等待 completion，超时回读 DONE 补偿漏 IRQ；XNU 异步命令用 timer 查询，同步亮度用轮询。两者都需要真实完成，通知机制不同。 |
| 错误清理 | XNU 对不确定 DMA 保持独立 pin/引擎引用并停止推进；Lineage 有 FIFO reset/继续清理和部分 OFF 错误忽略路径。不能为追求表面一致而移除现有 SMMU、IRQ、资源归属与真实完成保护。 |
| 其余范围 | 共享 SMMU/core、VBIF/QoS、DSPP/部分 PP 和 ABL 首帧依赖仍有保留；完整冷启动/错误恢复/PD4/PD5 未完成。XNU 的触摸参与者和 FB 完成凭据也不是 Linux notifier 的逐行移植。 |

## 证据与下一步

本轮底稿为 workspace `scratch/framebuffer-0223/audit-upper-flow.py` 和 `upper-flow-audit.json`：37 段源码/行号/文件 SHA，6 项静态调用点检查，核对 0223 补丁及七份最终变更源码哈希未变化。Link/Resources 与 a32 基线逐字节一致，lifecycle 差异也未改变亮度先于像素的旧顺序。它不执行 DRM 栈，也不是新的驱动回归或实机结果。

上一轮13份通过日志仍有效，但其夹具验证的是当时指定的时序，未覆盖这里补查的 HS/LP clock manager、LG post-kickoff 亮度、splash 分支和 autorefresh 默认策略。14个底层 helper 的输出及既有上层静态检查不能作为这些流程已等价的证明。

后续对齐应优先处理普通 OFF/ON 的 HS/ULPS 顺序、LG 亮度恢复时机，并从实际调用链建立期望；首次 splash 接管与逐帧/autorefresh 策略分别明确范围。0223 已具备待用户编译的本地版本，但应标为局部修正，不能用 a33 一次点亮来宣布全流程对齐。

a31 仍是成功实机基线；a32 持久日志止于 OFF 前，USB 止于 DMA 分配入口且接收时长不确定。本次发现不改变“a32 根因未证实、0223 实机效果未验证”的结论。没有 push、CI 或设备操作。
