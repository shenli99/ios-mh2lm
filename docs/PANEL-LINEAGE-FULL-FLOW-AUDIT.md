# 0223：目标显示驱动全流程审计

2026-11-14。用户要求整体、全面检查。**当前实现仍是局部对齐，不能称为与 Lineage 全流程一致。** 本文统一记录目标选择、首次接管、普通 OFF/ON、亮度与其他命令、持续刷新、资源、跨层完成和异常退出，以及目标面板已有但 XNU 未实现的功能。旧专项文档保留历史，当前范围和结论以本文为入口。

本审计未修改驱动、构建内核或操作设备。后续[0121a33/0223两次OFF/ON](PANEL-D1-0121A33.md)已实测通过，现为mode7成功基线，a31/mode2保留；矩阵中的剩余源码差异仍存在，不因唤醒通过改判为全流程一致。a32黑屏的单项根因尚未确定。

## 固定对象与判定方法

- Lineage：`ref/lge-lineage-kernel`，`2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`，工作树干净。主目标为 LG G8X 的 R66456A/TIANMAMD PV、DSI0/PHY0、1080×2340/60Hz/四 lane command mode；同时检查 XNU 支持的 PP1、DV1、DV2 表和配置。
- XNU：实现 `6ff3085`，本轮开始时仓库 HEAD `a66348c`；实际最终源码为 workspace `scratch/framebuffer-0223/verify/`。0223 补丁 SHA256：`946daee753f31c81669b5a47b23e4f33330f59b9583bf6126bb177d89853fc83`。本轮核对七份变更源码仍与既有最终证据相同。
- 审计主模式为 `sweetpanellifecycle=7`，兼查 mode2 的差别。其他旧模式不作为当前目标，也不把所有 Qualcomm 机型/视频模式分支当作本机必经流程。
- 下文 `L:` 路径默认从 Lineage `drivers/gpu/drm/msm/` 开始；`X:` 默认从最终 XNU `iokit/Kernel/arm/` 开始。`PV/PP1/DV1/DV2` 指 Lineage `arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-*-tianmamd-fhd-cmd.dtsi`；`common-panel` 指同级上层 `sm8150_common-panel.dtsi`。
- 调用关系以实际 caller、Kconfig、DTS 和状态条件共同判断，不能只看底层 helper 的排列或函数名。Lineage 一列描述固定源码定义的行为；不是同机 Android 的实测轨迹。

判定：**同**仅表示本行限定的操作/条件已核对；**差**表示已实现路径仍有逻辑差异；**适**表示明确的跨 OS 或安全适配；**缺**表示目标功能/完整生命周期尚未实现；**不适**表示当前目标不启用该分支；**待**表示缺少运行时证据。这些类别不能互相替代，“适配”也不等于已经获得硬件等价证明。

## 普通 OFF/ON 的实际链路

以下以成功的完整 OFF/ON、目标 LG 扩展启用为前提。电源属性分支指 `CONNECTOR_PROP_LP` 变更触发的 `dsi_display_set_power`；不能把所有仅改变 CRTC enable 的提交都写成无条件执行该回调。

| 阶段 | Lineage | 当前 XNU mode2/7 |
|---|---|---|
| OFF 准备 | bridge disable 消费电源属性，LG 禁止后续亮度更新并发送亮度0；pre_disable 取得 DSI clock vote。encoder 等 TX complete，执行资源 PRE_STOP、关闭 tearcheck。 | FB 合并目标状态，触摸 worker/IRQ/HID quiesce；关闭 TE/autorefresh，排空扫描/IRQ，关闭 tearcheck。 |
| 面板 OFF | bridge post_disable → display disable：释放 display engine 引用，再发送 OFF 表；每包有 transfer 引用。 | 释放 display 引用，逐包发送同一 OFF 表并等待真实完成。 |
| reset/host OFF | panel unprepare 执行 LG reset_off：等待、suspend pinctrl、reset low；随后 host disable。 | 同方向 pinctrl/reset-low，取得完成凭据后才关闭 host。 |
| link/resources OFF | HS pre-off 停 PHY clock gating → 释放 HS → LP pre-off 进 ULPS → 释放 LP → CORE pre-off PHY idle/clamp/reset mask → 撤 panel supplies。 | 先 ULPS，再停 PHY clock gating/HS/PLL/PHY；park DATA，保留共享 core/ESC/config/模拟供电，最后撤 panel supplies。 |
| ON 供电/链路 | panel pre_prepare 供电；CORE、controller、LP/PHY idle_on/ULPS重入、HS/resync/ULPS退出；随后 host soft reset、panel reset。 | 恢复 panel votes/DATA；controller 配置、PHY/ULPS重入、PLL/HS/resync/ULPS退出；host soft reset，再 panel reset。细节见 D03–D06。 |
| ON 表/引擎 | bridge pre_enable 内 prepare→panel ON→取得 display engine 引用；encoder enable 连接 TE。 | ON 表→取得 display engine 引用；**先恢复亮度**，才重配/恢复像素流。 |
| ON 表之外 | KMS commit 的 connector pre_kickoff 消费 ON 电源属性；LG mode_set 恢复色彩/锐度，随后像素 kickoff。 | **没有 LG color-manager 恢复步骤**。mode7 重建固定管线，TE→flush→autorefresh1。 |
| 亮度/完成 | bridge enable 的亮度更新先 pending；wait_commit_done→complete_commit→post_kickoff 发亮度并开放更新。 | 等 fresh PP_DONE/flush/RD_PTR 和 generation，恢复触摸，才完成 FB 唤醒。亮度已在 kickoff 前发送。 |

上层依据：`L:msm_atomic.c:214/425/573`、`dsi-staging/dsi_drm.c:179/237/269/298`、`sde/sde_encoder.c:3374/4759`、`sde/sde_connector.c:423/556/603/680/718`；对应 `X:SweetMDSSLifecycle.inc:197/373`、`SweetMDSSPanelSupply.inc:103`、`SweetMDSSResources.inc:289`。

## 审计矩阵

### A. 目标、配置与分支

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| A01 | LG 扩展是否生效 | **同/已确认参考分支**。`L:Kconfig:201` 的 `LGE_DISPLAY_COMMON default y`；目标 `arch/arm64/configs/vendor/lineageos_mh2_defconfig` 未禁用。此前仅引用 vendor/mh2lm_defconfig 不足，本轮补查默认值。不能拿无 LG override 的通用 panel 路径代替本机。 |
| A02 | panel/display/ops 选择 | **同/限定目标**。common-panel 中 DSI0/PHY0、GPIO6 reset/GPIO8 TE；PP1/DV1 为 r66456，DV2/PV 为 r66456a，`L:lge/lge_ddic_ops.c:27` 均绑定 r66456_ops。XNU identity→profile 见 `X:SweetMDSSLifecycle.inc:22`，依赖 ABL 选择，不是读回 DDIC ID。 |
| A03 | DTS 覆盖 | **已核对**。mh2lm common、普通和日版 panel include 文件未覆盖这四份 r66456 配置；按对应 common 节点合并，不能把其他面板节点的属性算入 PV。没有用本次 Android 运行时 DTB 代替源码，实际选择仍属 H01。 |
| A04 | 固定传输拓扑 | **同/限定配置**。四份 DTS 均 VC0/RGB888/四 lane/command；host profile 的 lane-map、TE source、DMA software trigger/MDP none、2c/3c 见 `X:SweetMDSSHost.inc:15`。mode7 固定 VIG0→LM0→PP0→INTF1，见 Pipeline.inc。不是任意 DRM 拓扑实现。 |
| A05 | LP11/reset/power 条件 | **同/限定普通分支**。DTS 启用 lp11-init、use-labibb、reset-after-ddvd、非 incell；`L:lge/lge_dsi_panel.c:180` 普通运行走 full_power_seq。制造测试覆盖条件另算，不作为日常路径。 |
| A06 | DSC/video/双 DSI/动态模式 | **不适**。当前目标无 DSC/split/video 切换或多 timing 动态模式条件；不能因库中存在 PPS、POMS、DMS 函数就认定普通 ON 漏发。XNU 也不支持这些泛化功能。 |
| A07 | 普通 PRE/POST 表 | **同/无额外表**。四个目标均没有普通 pre/post ON/OFF 命令表，不能据函数存在推断漏命令。**这不包括 LG color manager 等独立命令集**，见 D09/G04。 |
| A08 | 全范围功能开关 | **缺/需分别处理**。四个目标均启用 color manager、ambient、ESD 和 partial update；PV 还启用 ACE/IRC/FP-LHBM/TC-perf/VR-LP。不能把这些统一标成“别的面板才有”；具体触发条件见 G 组。 |

### B. 首次接管、资源与地址空间

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| B01 | continuous splash 接管 | **差**。Lineage splash 跳过 PHY reset、hardware enable、host soft reset、panel ON 和 resync：`L:dsi-staging/dsi_display.c:2949/7286/7791/3475`、`dsi_phy.c:946`。XNU ManagedLink→ManagedHost 仍停链路/PLL、PHY reset/config、resync、host reset；0223 仅取消正常 CTL reset。 |
| B02 | 服务发布/接管时机 | **适**。`X:SweetMDSS.cpp:597` 初始化 PanelOn、准备缓冲/显示引用后发布服务；FB 随后发起 takeover，先等待触摸 worker（启动等待最长30s）。Lineage 通过 splash state/resource adoption；二者不是同一个阶段划分。 |
| B03 | panel 与模拟供电 | **同+适**。common-panel 的 VDDI1.8V/62000µA、VPNL3V/857000µA 与本地 managedRailVotes 对应；OFF reverse、VDDI post20ms 保持。XNU typed ACK/独立票与 regulator 框架不同；共享 host/PHY L3/L5 留票，不能宣称每次全部物理断电。 |
| B04 | core/GDSC/频率 | **差/缺**。XNU core2 走 warm 管理，AHB75MHz/MDP460MHz、ARC Turbo，保留引导/共享资源；`X:SweetMDSSCore.inc:77/110`。Lineage 经 SDE power/performance 聚合管理。存在 cold 函数形态不等于已有可用的无 ABL 冷启动路径。 |
| B05 | NoC/RSC/RSCC | **差/缺**。`X:SweetMDSSNoC.inc:243` 和 Resources.inc 为固定资源模式，bootstrap DATA peak6.4GB/s、CN0/config300MB/s，运行在 CLK/AMC 使用范围，未实现完整 solver sleep/wake。`L:sde/sde_encoder.c:2357` 的 KICKOFF/PRE_STOP/STOP/ENTER_IDLE/EARLY_WAKEUP 周期未整体移植。 |
| B06 | 时钟投票与物理动作 | **差**。Lineage `dsi_clk_manager` 通过计数/prepare/enable 和回调转换；XNU 可保留 lease 同时按 owned mask 停物理分支，共享 ESC/core/boot 不全部撤销。两边“释放引用”都不能无条件等同物理 PLL/rail OFF。 |
| B07 | SMMU、地址与独占 | **适+缺**。用户指定短 FIFO/长物理 DMA；XNU 保留 unsecure CB2/bypass 拓扑及 fault/别名/所有权检查，没有建立 Lineage GEM/IOVA/domain 生命周期。`L:msm_smmu.c:66/220`；`X:SweetMDSSScanout.inc:73`、Pipeline.inc:24。不能为对齐关闭现有归属保护。 |
| B08 | 完整状态丢失后恢复 | **缺**。mode7 只重建固定像素管线及已有 host/link范围；共享 SMMU/core/VBIF/QoS/DSPP、部分 PP/ABL 首帧依赖未全部清除。成功 warm OFF/ON 不等于完整 power-collapse、冷启动或错误恢复完成。 |

### C. 命令、缓冲与完成

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| C01 | 四 profile 的 ON/OFF | **同**。本轮独立逐字节比较8张表，包含 packet type/last/VC/ACK/长度/payload/wait；均匹配。`X:SweetPanelProfile.h` 对照四份 DTS，统计见下表。 |
| C02 | ON/OFF 的 LP 属性 | **同**。四 profile 的 ON/OFF state 均 dsi_lp_mode，当前 sequencer 也为 LP。不能推及普通亮度和 LG vendor 表，见 D08/D09。 |
| C03 | 支持的 packet 范围 | **同/目标表；缺/通用能力**。目标表全是0x05/0x15/0x39、VC0、last1、noACK，最大payload149；本地支持上限252。Lineage 还支持 read/BTA、非 last 批处理、多控制器等；这些不在当前 ON/OFF 命令内。`X:SweetDSICommand.h`。 |
| C04 | 编码与搬运 | **适**。编码≤64bytes 从栈 words 写 FIFO，较长包拷入低4GiB连续物理缓冲后 DMA。Lineage 普通 host_transfer 使用保留 GEM/IOVA。缓冲存在不表示 FIFO 也经该 descriptor 搬运。`X:SweetMDSSDSI.inc:268`。 |
| C05 | 缓冲准备/寿命 | **适**。0223 mode2/7 发布服务前 prepare4KiB，跨 OFF/ON 保留；Lineage `L:dsi-staging/dsi_display.c:3000/519` 首 transfer 懒分配4KiB。目的为移出活动 gate 首次分配，不是照搬分配时机；也未证明启动分配可被 deadline 打断。 |
| C06 | 每包软件内存 | **适**。Lineage `L:dsi-staging/dsi_ctrl.c:986` 每包另有 copy/pad 临时分配，不能说 Lineage 从无逐包 allocation。XNU 栈编码、保留 DMA descriptor；真实完成前不复用/释放。 |
| C07 | command engine 引用 | **同/限定语义**。OFF 表前释放 display，每 transfer 获取/完成后释放，ON 整表后持有 display；wake reset 恢复旧 control，DDIC reset 时 command bit 关闭。`L:dsi-staging/dsi_display.c:289/333/3000/7791/8031`；`X:SweetMDSSDSI.inc:75`、Lifecycle.inc:544、Host.inc:134。 |
| C08 | 每包 clock/wake 与停扫 | **差**。Lineage host_transfer 为 ALL_CLKS→wake_up→engine→transfer→engine/clock release；XNU 依赖已持资源，整个命令序列/同步亮度停 TE/autorefresh。display post_enable 撤 vote、pre_disable 取 vote 的周期也未整体复制。 |
| C09 | 完成、延时与 IRQ | **同+适**。均要求真实完成再做表内 delay；Lineage `dsi_ctrl.c:1304` DMA_DONE IRQ/completion、200ms超时后读状态补漏 IRQ；XNU timer1ms查询，同步亮度最长200ms轮询，fresh DONE+MDP/DMA idle+ACK/error 检查。不是相同通知/调度模型。 |
| C10 | 失败/取消后的缓冲与引擎 | **适**。XNU uncertain 传输保留引擎引用，DMA另保留独立 pin；poison 后禁止继续或撤资源。Lineage有 FIFO reset、释放引用和部分 OFF 错误继续清理，见 `dsi_ctrl.c:1304`、`dsi_panel.c:4454`。不能通过“超时就当完成”获得表面相同的退出。 |

| profile | ON bytes/包数 | ON 长 DMA 包数 | 最大 payload | OFF bytes/包数 |
|---|---:|---:|---:|---:|
| PP1 | 834 / 20 | 5 | 135 | 35 / 4 |
| DV1 | 538 / 16 | 3 | 135 | 35 / 4 |
| DV2 | 538 / 16 | 3 | 135 | 35 / 4 |
| PV | 694 / 17 | 4 | 149 | 35 / 4 |

DMA 包数按当前≤64bytes编码走 FIFO 的规则统计；不是 Lineage 的搬运方式。所有 OFF 包当前均走 FIFO。

### D. 普通息屏、唤醒与亮度

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| D01 | 排空、tearcheck、OFF 表 | **同/关键先后**。完成扫描排空后关闭 tearcheck，再释放显示引擎并发 OFF；`L:sde/sde_encoder_phys_cmd.c:1335`、`dsi_display.c:8031`；`X:SweetMDSSLifecycle.inc:425`。RSC PRE_STOP/触摸/TE暂停跨度仍不同，不把整段称为等价。 |
| D02 | reset/pinctrl/供电延时 | **同/限定动作**。OFF 前5ms、suspend pinctrl→reset low及5ms hold；ON reset高10/低2/高10+settle5ms，VDDI post20ms、rails后5ms。`L:lge/lge_dsi_panel.c:689/837/881`；`X:SweetPanelPins.cpp:172`、PanelSupply.inc:103。GPIO输出/ACK凭据不是电压实测。 |
| D03 | OFF 的 HS/ULPS | **差**。Lineage manager先 HS OFF，再 LP OFF；HS pre-off停gating，LP pre-off进ULPS。XNU先ULPS再HS/PLL停。依据 `L:dsi-staging/dsi_clk_manager.c:831/993`、`dsi_display.c:3386`；`X:SweetMDSSLink.inc:165`。callback 中 if 的书写顺序不能替代 manager 调用顺序。 |
| D04 | HS 内部开关顺序 | **差**。Lineage开 byte→pixel→byte_intf，关 byte_intf→pixel→byte；XNU开 byte→byte_intf→pixel，关 pixel→byte_intf→byte。`L:dsi_clk_manager.c:434/469`、DISPCC 分支2028/2004/202c；`X:SweetMDSSLink.inc:14/356`。是逻辑顺序差异，尚无其导致黑屏的因果证据。 |
| D05 | ULPS idle/重入/退出 | **同/局部；差/资源外层**。普通唤醒不做 cold PHY reset；controller setup→PHY idle_on重新配置→ULPS重入→reset unmask→HS→resync→ULPS exit→gating，已具备。`L:dsi_display.c:3475`、`dsi_phy.c:1046`；`X:SweetMDSSLink.inc:265/482`。不重置不等于不配置PHY；完整 LP/CORE vote仍缺。 |
| D06 | clamp/host soft reset | **同/局部**。v2.3 ctrl clamp和v4 PHY clamp未提供硬件回调；不能发明clamp寄存器。软件clamp状态仍参与ULPS重入，reset_config false置mask。host soft reset在ULPS退出后保存/恢复clock/control。`L:dsi_catalog.c`、`dsi_ctrl_hw_cmn.c:172/602`；`X:SweetMDSSHost.inc:25/82`。host enable/disable force clocks在Lineage也存在。 |
| D07 | 亮度恢复时机 | **差**。XNU ON表→亮度→像素；LG电源OFF使panel allow_bl_update=false，bridge enable记录pending，complete_commit/post_kickoff恢复。`L:sde/sde_kms.c:1200`→`dsi_drm.c:1035`→`dsi_display.c:7666`→`lge/brightness/lge_brightness.c:167`；`X:SweetMDSSLifecycle.inc:544`。无bl-update-flag只表示无额外first-frame等待选项，不取消LG pending机制。 |
| D08 | 普通亮度 HS/LP | **差**。目标panel由kzalloc创建，drv_init未设mipi_device.mode_flags，DSI/LG子树无该字段写入；通用mipi transfer仅在MODE_LPM时加USE_LPM。因此该参考路径普通亮度默认 **HS**，当前XNU同步和异步亮度固定 **LP**。`L:dsi_panel.c:3369/3503`、`drivers/gpu/drm/drm_mipi_dsi.c:367/1069`、`L:lge/brightness/lge_brightness.c:345`；`X:SweetMDSS.cpp:1343`。51 high low字节和long packet本身相同。 |
| D09 | ON 表外色彩/锐度恢复 | **差**。四份DTS均有use-color-manager，均未启用ddic-register-backup；正常初值backup=false/complete=false使 `!(backup ^ complete)` 为true。ON电源属性→set_nolp的OFF→ON分支→mode_set→r66456 screen_mode→display_control，发送CM dummy与sharpness两组 **HS** 命令。`L:lge/lge_dsi_panel.c:112/634`、`lge/lge_ddic_ops_r66456_tianma_md.c:316/575/796`、PV:191/227。当前XNU只恢复ON表/亮度，没有此步骤。payload按screen/HDR/RGB状态修改，不能直接硬拷一份默认vendor表就宣称全部支持。 |
| D10 | 亮度数值/0/default | **适/行为不同**。LG UI max365/default158经blmap映射至12位DCS，允许0且OFF另外发送0；XNU Apple backlight使用0..4095，backend将0夹到1，OFF表内才包含51=0。`L:lge/brightness/lge_brightness.c:207`、common-panel；`X:SweetFramebuffer.cpp:330`、SweetMDSS.cpp:35。UI刻度/渐变政策应适配iOS，不能把Android UI值直接用作DCS值。 |
| D11 | 首次亮度未知时准入 | **适/限制**。XNU full lifecycle要求已有成功typed brightness，初始未知时拒绝；Lineage有default brightness状态。`X:SweetMDSSLifecycle.inc:197`、SweetMDSS.cpp:1056；失败或Busy时target保存规则不同于LG pending。 |
| D12 | 唤醒成功定义 | **适**。XNU supplies/link/config generation、亮度、fresh pixels、touch均成功才报告FB ON；Lineage经atomic/commit/fences及notifier表达完成。`X:SweetMDSSLifecycle.inc:550`、SweetFBService.inc:319；不能用单个配置读回、软件post-kickoff或ON表发送成功替代新帧。 |

### E. 像素管线、持续刷新与中断

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| E01 | 正常 CTL reset | **同/0223修正**。mode7首次/ON不再无条件CTL_SW_RESET。Lineage command enable正常链无此操作，`L:sde/sde_crtc.c:4434` 的reset_req为错误分支。已有busy/外部reset/资源冲突仍应拒绝，不能改成静默reset。 |
| E02 | external TE 与 flush | **同/0223修正**。encoder enable先连接TE，随后KMS commit；XNU mode2/7先TE后flush/kickoff。`L:msm_atomic.c:515`、`sde/sde_encoder.c:3183`；`X:SweetMDSSPipeline.inc:117`、Lifecycle.inc:325。 |
| E03 | 固定管线重配 | **同/限定几何；缺/通用管线**。mode7重建VIG0/LM0/CTL0/PP0/INTF1、固定BGRA源/stride4608/crop22,48，tearcheck按19.2MHz/(2360×60)=135。mode2仅恢复source/flush/tearcheck。既有14个helper验证局部寄存器，不证明完整DRM状态重建。 |
| E04 | autorefresh 策略 | **差+待**。Lineage connector属性默认0，非零走auto、否则CTL_START；XNU mode7固定1。`L:sde/sde_connector.c:2372`、phys_cmd.c:1677；`X:SweetMDSSPipeline.inc:117`。未知同机Android用户态实际值，不能说Android实机必用0。 |
| E05 | 已启用auto时的提交准备 | **差**。Lineage `sde_encoder_phys_cmd_prepare_commit:1629` TE off→auto off→排空PP transfer→TE on；XNU整段命令/OFF保持停扫，暂停与恢复跨度不同，不能仅凭都有TE开关判定一致。 |
| E06 | 每帧buffer/同步 | **差/适配范围**。XNU owned A单缓冲，CPU30Hz原地拷贝，池B未用于flip；Lineage atomic plane/acquire fence→flush/start→PP/retire/release。`X:SweetFBScanout.inc:114/130`；`L:sde/sde_plane.c`、sde_crtc.c。60Hz面板扫描不等于60Hz应用提交，也无每帧flip fence等价性。 |
| E07 | QoS/DSPP/性能 | **缺**。Lineage plane setup设置OT/QoS remap、CREQ/danger/safe，见 `L:sde/sde_plane.c:4254`；XNU Pipeline.inc明确保留VBIF/QoS/DSPP/部分PP状态。基础点亮不等于色彩、HDR、欠载控制和动态性能完整实现。 |
| E08 | IRQ/掉帧与故障传播 | **适+缺**。XNU拥有RD_PTR IRQ、250ms watchdog/风暴界限，PP_DONE及DSI完成主要轮询；Lineage分工为RD_PTR/PP_DONE/DSI IRQ等和资源控制。`X:SweetMDSSIRQ.inc:233/246`、Scanout.inc:301：owned路径IrqFailed会cancel/freeze，不能依据旧“software fallback”日志声称当前owned失败会继续正常刷新。 |

### F. FB、触摸、并发与退出

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| F01 | 快速 OFF/ON 请求 | **适**。`X:SweetFBService.inc:288` 合并latest desired，当前token完成后再执行反向请求；不是每次切换立即取消。Lineageatomic锁/提交序列为另一套并发模型；本地状态回放不能证明实机调度等价。 |
| F02 | 触摸停/恢复 | **适**。XNU明确quiesce worker/IRQ/HID，fresh pixels后重新app_info/报告格式/IRQ；Lineage LG blank/reset/LCD_MODE notifier及异步触摸work。`L:drivers/input/touchscreen/lge/touch_core.c:964`、synaptics/s3618/touch_s3618.c:3829；`X:SweetTouchPanel.cpp`、`bsd/dev/sweet_touch_tcm.c`。s3618 reset通知本身有no-op，不能说每次panel reset通知都直接reset touch。 |
| F03 | deadline 与等待 | **适**。XNU独立参与者、步骤期限、MDSS16s/FB普通完成17s，失败保持不确定状态；Linux mutex/completion/workqueue/atomic waits语义不同。同步阻塞调用不能仅靠同loop timer保证期限。 |
| F04 | provider丢失/取消 | **适**。XNU generation/token、独立pin和归属账本保护；部分写入失败不自动倒序撤资源。Linux各error label/unbind清理不能机械替换XNU的生命周期保护。`X:SweetMDSSDSI.inc:222`、Lifecycle.inc:292、Resources.inc。 |
| F05 | stop/free/重新绑定 | **缺+适**。`X:SweetMDSS.cpp:1118/1163`、SweetFBService.inc:376停止受理、取消timer/子操作，保留硬件可能仍可访问的pin/ownership；没有完整graceful hardware OFF或poison后原地恢复。保pin是保护，不是完成了退出/恢复。 |
| F06 | 显示 OFF 与系统 suspend | **缺/独立范围**。Lineage `L:sde/sde_kms.c:2835/2987` duplicate atomic state，LP1→LP2或disable CRTC，resume恢复state；XNU普通面板OFF/ON没有实现这整套系统PM和状态丢失恢复。 |

### G. 目标面板的额外功能

| ID | 项目 | 判定、依据与边界 |
|---|---|---|
| G01 | ESD检测/恢复 | **缺**。PV:49启用esd-check、te_signal_check；Lineage `dsi_display.c:432/811/862` TE GPIO检查及panel-dead后续恢复。XNU RD_PTR watchdog并不覆盖TE检测、panel-dead恢复和重试链。不能因r66456专用err_detect ops为空就认定没有通用ESD。 |
| G02 | ROI/partial update | **缺**。DTS启用single_roi及alignment约束，Lineage提交路径可变ROI；XNU固定全帧。代码存在不代表Android每帧都用ROI，但功能确实适用于目标。 |
| G03 | AOD/LP1/LP2/NOLP | **缺**。目标有use-ambient、LP2/NOLP命令和AOD ops；XNU只做完整OFF/ON，无这些显示状态和触摸LPWG配合。普通ON-from-OFF在LG set_nolp中跳过NOLP命令表，但仍进入mode_set，不能把两者一起跳过。 |
| G04 | screen mode/HDR/RGB/锐度 | **缺，且影响D09正常恢复**。r66456_ops实现动态payload映射和CM/锐度发送；PV明确use-color-manager。XNU没有状态保存/用户接口/恢复执行。此处是DDIC色彩命令，另有E07的MDP DSPP状态，两者不能混为一项。 |
| G05 | ACE/IRC/FP-LHBM/TC-perf/VR-LP | **缺/按条件触发**。PV有对应开关/表/ops；forced_lhbm、need_fp_lhbm_set、HDR/IRC状态、非零tc_perf或显式VR请求才进入相应路径。不能据此认定每次普通ON无条件漏发所有vendor表。PP1无VR开关，其余三份有；具体表也随revision不同。 |
| G06 | DDIC register backup | **不适/当前配置**。四profile/common对应节点均未启用ddic-register-backup；备份work不是本目标正常必经步骤。**这不会禁用color manager**：backup/complete都false时mode_set条件反而成立。 |
| G07 | panel专用bc_dim/错误线/BIST等 | **不适/按ops**。`L:lge/lge_ddic_ops_r66456_tianma_md.c:796` 的bc_dim、部分检测/BIST/动态分辨率ops为空。不能从LG通用mode_set有调用点就断言本机还必须发送bc_dim命令。通用ESD仍见G01。 |

### H. 实机、回归与未证实项

| ID | 项目 | 判定、证据与边界 |
|---|---|---|
| H01 | Android实际选择/属性 | **待**。没有本轮同机Android寄存器/提交轨迹、autorefresh值及screen/HDR/RGB/FP运行时状态。源码默认值和条件能确定，实机各请求值不能猜。 |
| H02 | 时钟/rail聚合后的物理结果 | **待**。共享consumer与固件投票影响真实关钟/断电，typed ACK、clk_disable与GPIO输出记录都不是示波器测量。 |
| H03 | a32 USB与持久日志 | **待/根因未证实；已有复测**。首次持久日志早于OFF、接收时长不确定。最新pipeline1用户确认唤醒后等待超过1分钟；USB4315仍为dma-allocate-enter，末12行与首次相同，无return。新持久日志flush19覆盖OFF完成，末25行对齐USB3770–3794，早于3817 ON。优先定位分配调用附近，未证明allocator死锁；不再重复同条件a32。详见[PANEL-D1-0121A32](PANEL-D1-0121A32.md)与scratch/artifact-0121a32/pipeline1-wake-log-review.json。 |
| H04 | 0223原有回归 | **局部有效**。既有13份通过日志、ARM64对象、39patch/79文件/132markers证据见final-review.json。本轮未重跑驱动回归；其夹具未验证本次确认的clock manager、LG亮度/色彩、splash、持续提交全链。 |
| H05 | 0223实机效果 | **待**。无新内核/镜像/启动结果，不能写成a32已修好或全流程已对齐。单次新帧通过也不验证AOD/ESD/系统suspend/并发故障恢复。 |

## 本轮检查结果与证据保存

workspace `scratch/framebuffer-0223/full-flow-audit.py` 已运行，输出 `full-flow-audit.json`：**63份源文件指纹、150段完整函数/行号、72项静态检查、8张ON/OFF表字节匹配**，另调用既有 upper-flow 静态检查。检查内容包括实际atomic/bridge顺序、engine引用、clock manager分支、splash guard、LG亮度传输flags与pending、color-manager适用条件、DTS覆盖和四profile命令。

这些数量是证据索引和静态检查数量，**不是完整DRM栈执行或所有状态组合的覆盖率**。本轮沿已实现路径和目标功能分支审阅；底层PLL数值/SSC/host寄存器及固定管线helper继续复用既有独立回归证据，没有重新验证所有寄存器、所有硬件版本或完整Linux驱动栈。底稿中的源文件hash使后续可以辨认参考变更，不能只凭文档标题延用结论。

与上一轮四项专项相比，本次收口新增/展开：普通亮度HS/LP、ON表外LG色彩/锐度恢复、HS开启内部顺序、亮度0/default/blmap政策、持续帧与RSC资源周期、目标ESD/AOD/ROI/其他DDIC功能、stop/free和系统PM范围。它们不是本轮代码新引入的回退；本轮没有驱动修改。0222新增CTL reset/TE顺序问题与0223修正的历史保持，不能把所有后来发现的旧差异都记为0223回退。

## 后续对齐的明确范围

1. 普通OFF/ON优先：D03/D04的clock manager先后、D07/D08的亮度时机与HS、D09的色彩/锐度恢复；同时处理C08的逐命令资源与停扫语义。先从真实caller、DTS、默认/非默认状态建立期望，再改实现。
2. 首次接管与持续刷新：B01的splash、E04–E06的autorefresh/提交/帧同步，不能靠更改一个寄存器值宣布整段一致。
3. 完整显示驱动：B04–B08、E07、F05/F06、G组中的缺项继续作为独立实现范围；不能用“普通OFF/ON通过”销项。
4. 用户指定FIFO/物理DMA、prepared buffer与真实完成/独立pin、SMMU/IRQ/资源归属保护继续保留；跨OS亮度政策等适配要显式记录，不机械复制Linux错误清理。

此前反复补出差异，主要是检查范围与结论范围不一致：用底层helper/局部表验证推到整个流程，没有同时穷尽实际caller、LG override、目标DTS和错误分支；有些参考行为也未带上条件。本文用固定ID保留每项状态和证据，后续修改只能更新相应项及其上下游，不能再以“已对齐”概括未检查范围。
