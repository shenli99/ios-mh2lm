# 0220：按 Lineage 对齐面板息屏与唤醒

2026-11-14补充：当前统一入口为[64项全流程审计](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)。[上一轮专项](PANEL-LINEAGE-0223-FLOW-AUDIT.md)发现HS时钟/ULPS和LG亮度post-kickoff差异；全面检查另确认HS内部开关顺序、亮度HS/LP、ON表外CM/锐度恢复等。以下记录仅说明已实现的部分，不代表整体Lineage流程一致；a31成功实机事实保持。

## 0221：framebuffer配合逻辑已修复（2026-11-14）

实现提交`5cf36dd`，补丁[0221](../xnu/patches/0221-mh2lm-lineage-framebuffer-lifecycle.patch)，
SHA256=`5c2d8591be684f07885e32a783cc0be6cf95d7d5e771db352255619b783f7ebf`。
用户要求补齐配合逻辑；下述mode2 consumer遗漏已修复，a31普通息屏唤醒实测通过，见下方交付与实测更新。

framebuffer现在将`sweetpanellifecycle=2`同时映射为supply cycle、link cycle和data parking，
与0220的MDSS一致。OFF完成接受本panel票已撤销、链路/DATA停驻、共享config仍持有的状态；
ON完成仍须供电票/链路/资源恢复，以及新像素和触摸恢复证明。现有完成条件、超时和取消规则保留，
仅选择正确的完整验收分支。快速OFF期间到达的ON继续排队，OFF完成后才提交。
接口注释也已更正；模式2不再表示全部资源保持。新增一次软件模式日志，供CI产物核对。

本轮没有变更MDSS底层时序、GPIO/ULPS、命令表、FIFO/物理DMA、固定扫描管线或固件/ADT/启动参数。
完整MDP/INTF/SMMU独立重建及共享核心低功耗仍属原有边界，不因这次配合修复宣称已实现。

本地验证：

- 新[跨层回归](../tools/tests/test-mh2lm-panel-lineage-fb.py)实际运行MDSS mode2，
  经其`panelLifecycleStatus()`导出四profile各三轮的60份pending/OFF/ON状态和capabilities，
  再交给实际framebuffer处理。有效OFF→ON、排队ON、未完成像素/触摸时保持等待均通过；18组损坏凭据被拒绝。
  同一测试在未修改a30源码上因OFF完成被拒而失败，证明覆盖了本次回归。
- 修正原[FB回归](../tools/tests/test-mh2lm-panel-lifecycle-fb.py)：真实mode2/6初始化，
  各32组完成场景、四种缺失capability、旧mode2资源保持结果拒绝；保留mode1/3/4/5、provider退出/超时/取消验证。
- 原Lineage mode2底层回归和framebuffer service回归通过。上述四套使用ASan/UBSan。
- `SweetFramebuffer.cpp`真实ARM64对象编译通过；基线/修改各1398条告警，逐条消息与类别计数相同。
  使用已有Linux clang19兼容参数，非完整macOS CI构建。
- 0185–0221共37补丁严格重放，无offset/fuzz，78源码逐字节一致。
  CI新增4个mode2源码marker，共118项通过；构建环境未调整。
- 打包脚本新增`SWEET FB PANEL: Lineage mode2 supply/link/data completion enabled`门槛，
  真实旧a30被拒绝且无boot/ramdisk/TOML输出；bash语法与CI YAML解析通过。

跨层回归分两进程传递实际typed状态；OS、硬件响应仍是既有fixture，不能替代真机或并发workloop验证。
底稿`scratch/framebuffer-0221/`含`final-review.json`、四份test输出、旧a30失败输出、
`bridge/mdss-status.json`、编译/告警、累计重放和旧产物拒绝证据。prepare-patch会重建verify，不能在测试/编译读取时执行。

2026-11-14交付与实测更新：[0121a31普通息屏唤醒实测通过](PANEL-D1-0121A31.md)。
78份受检源码匹配0221验证树，118项source markers、223项内核markers通过；ABI608四档打包/回读通过，
全部ADT与args保持a30、retained仍605字节。未取得独立CI run ID或完整构建日志。
用户确认息屏后点亮、触摸/按键/亮度调节正常。日志4776 OFF complete后无consumer cancelled，
4816 ON begin，ULPS exit/host soft reset/DDIC reset/17条ON命令（含四个长包DMA）完成，
5482 owned flush、5495 touch恢复、5496 ON complete、5509 FB owned resume verified。
6636唤醒后完整一分钟59.942Hz/dropped0。本轮配合修复收口，保留a31基线，无需重编或重复首次验证。
此次仅一个普通OFF/ON周期；不代表多轮耐久、完整管线独立重建或完整核心低功耗验收。

以下为0221修复前的审计与历史实现记录。

## 四项对齐复核（2026-11-14）

用户要求检查此前“除命令传输外，其他四项与Lineage对齐”是否真正完成。本轮以
Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`与a30实际CI源码为准，命令传输排除在检查目标之外。
**结论：四项都有实际修改，但整体未完成对齐。mode2的framebuffer consumer漏改，导致已完成的OFF被取消；
画面恢复只补齐固定管线的重新提交，没有完整的MDP/INTF/SMMU状态重建。**

| 用户要求 | a30/0220实际实现 | 判定与实机边界 |
|---|---|---|
| 息屏后reset低、休眠引脚、撤面板供电请求 | OFF表完成→5ms→suspend pinctrl→reset低5ms→host/link关闭→VPNL/VDDI撤票→5ms；ON先active pinctrl保持低，按VDDI ACK+20ms、VPNL ACK+5ms恢复 | 下层顺序已对齐所选PV普通OFF/ON路径。a30日志已有OFF/reset-low/ULPS/撤票完成；上层验收漏改，整体在此中断 |
| host/PHY的ULPS、时钟恢复与soft reset | 已加入ULPS entry/reentry/exit、PHY idle-off/on、reset mask/unmask、PLL/时钟恢复、resync、host soft reset，之后才DDIC reset | 所选底层路径已实现，模拟通过；实机仅走过关闭侧。v2.3/v4对应路径没有有效硬件clamp回调，不写额外clamp寄存器是符合本机Lineage的；共享核心供电及RSC完整低功耗状态机仍保留原范围 |
| 恢复管线并提交新帧 | 重新写已pin自有buffer的source/stride/crop→CTL flush→TE/autorefresh；frame_count为0才CTL_START；等待flush0+fresh PP_DONE+RD_PTR后恢复touch | 部分对齐。已超过旧的仅恢复TE/autorefresh，但仍依赖既有固定MDP/INTF/SMMU布局；未重新建立整条管线，也未获得a30唤醒实测 |
| 去掉额外GPIO回显强制检查 | 普通CTL/mux/pull/drive/latch/input差异改为有界诊断；删除1ms输入跟随门槛，按高10/低2/高10/settle5ms推进 | 表中要求的检查规则已修改。仍保留owner/ABI/IRQ竞争及100ms总调度期限，不能说“所有检查都删除”；resetKnown/resetHigh是命令状态，不是独立电压证明 |

源码路径以`scratch/artifact-0121a30/actual/xnu/iokit/Kernel/arm/`为根：
`SweetMDSSLifecycle.inc:44,319,469,497`、`SweetMDSSPanelSupply.inc:109`、
`SweetMDSSLink.inc:167,182,278,378`、`SweetMDSSHost.inc:25,81,138`、`SweetPanelPins.cpp:72,94,172`。
这六份审计文件（含`SweetFBService.inc`）与0220验证树逐字节一致，记录在
`scratch/audit-lineage-0220/source-check.json`。
Lineage直接核对了`lge_dsi_panel.c:689,778,837,891,1047`、
`dsi_display.c:3388,3466,7286,8092`、`dsi_phy.c:764,801`、
`dsi_phy_hw_v4_0.c:352,377,431,487`、`dsi_ctrl_hw_cmn.c:172`、
`dsi_ctrl_hw_2_2.c:32`、`dsi_catalog.c:113,252`及`sde_encoder_phys_cmd.c:1629,1677`。

### 明确遗漏：framebuffer仍按旧mode2验收

MDSS的mode2把supply/link/data parking全部打开；`SweetFBService.inc:13–14`却只给旧mode3–6
设置这些标志。mode2于是走`:346`的`resourcesHeld`要求，拒绝下层OFF后正常的false。
随后`:350`取消token并进入永久fault，之后的ON请求在准入处被拒绝。
这与[a30日志3570–3577](PANEL-D1-0121A30.md)中的OFF complete→consumer cancelled→FB IOError一致。
因此不能通过“下层OFF/ON测试通过”宣称整个驱动完成对齐。

### 本轮验证及原测试缺口

使用未修改的a30源码，本轮运行ASan/UBSan验证：

- 原`test-mh2lm-panel-lineage-wake.py`通过：四profile各三轮、ULPS/host时序、固定扫描提交及故障场景。
  它直接驱动MDSS，没有通过AppleCLCD consumer发起并验收电源请求。
- 原`test-mh2lm-panel-lifecycle-fb.py`也通过，但其旧mode2用例手动设置`fPanelLifecycleFull`，
  未以`lifecycleArg=2`验证完整模式映射；`complete()`始终给`resourcesHeld=true`，未模拟0220停驻结果。
- 新审计复现程序用真实mode2初始化和有效停驻OFF结果，确定会取消token并拒绝ON。
  同一结果作为mode6控制组可以通过；mode2反而仍接受缺少新供电/link凭据的旧resources-held结果。

底稿和完整输出：`scratch/audit-lineage-0220/reproduce-consumer.py`、`consumer.txt`、
`lineage.txt`、`original-fb.txt`。审计复现使用实际FB源码和受控backend状态，不是同机硬件仿真或修复测试。
下一步先同步mode2 consumer合同，并覆盖真实模式选择、有效OFF完成→ON及缺失新凭据的拒绝场景。
本轮没有修改生产补丁、构建新内核、操作设备或宣称唤醒修复；ON后续硬件效果仍【待核实】。

## 原实现记录（2026-11-13，以下验收状态为当时快照）

2026-11-13。用户授权对齐息屏后的状态、host/PHY、恢复画面和复位检查，保留命令传输。
补丁：[0220](../xnu/patches/0220-mh2lm-lineage-panel-suspend-resume.patch)。
参考 Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`；此前的
[a29 流程审计](PANEL-LINEAGE-PATH-COMPARISON.md)是修改前的比较。

当前 `sweetpanellifecycle=2` 改为下面这条路径；镜像名 `panel-retained` 和参数保持兼容，
**名称不再表示所有资源常开**。PP1/DV1/DV2/PV 四个 DTS 都启用了 LP11 init、suspend ULPS 和 PHY idle poweroff。
原 mode1/3/4/5/6 的选择保持；引脚回读观察规则和 suspend pinctrl→reset-low 顺序由公共 Pins provider 采用。

| 阶段 | 0220 行为与 Lineage 依据 |
|---|---|
| OFF 命令完成后 | 等5ms→suspend pinctrl→reset低→等5ms；随后关闭host与link；撤销本panel的VPNL、VDDI请求，再等5ms。LG `lge_dsi_panel.c:778,837` |
| host/PHY 关闭 | host硬件关闭→等待lane idle→PHY `LANE_CTRL1=1f`→至少100us→关LPRX/CDRX→确认ULPS→关动态gating与link branches/PLL→PHY idle-off；v2.3 reset_config(false)置DISPCC+8000.bit0 |
| 恢复面板供电 | active pinctrl保持命令reset低→前5ms→VDDI请求完成后20ms→VPNL→后5ms；两项请求完成后才恢复DATA/link |
| PHY 唤醒 | 先恢复host配置，再REFGEN/PHY配置；保持软件clamp配对，重新请求ULPS、100us后校验；reset_config(true)清bit0；恢复PLL/roots/branches并等待真实完成 |
| 退出 ULPS | resync FIFO→开LPRX/CDRX→`LANE_CTRL2=1f`→至少1000us→清entry/exit请求→lane stop pulse→至少100us→核对Lineage的退出状态条件→恢复动态gating |
| DDIC reset 与 ON | ULPS退出后控制器soft reset≥1us，恢复原clock/control；然后高10/低2/高10/settle5ms→原ON表→原亮度目标 |
| 画面恢复 | 在现有MDP/SMMU布局内重新写自有且已pin的source/stride/crop→CTL flush→TE/autorefresh kickoff。frame_count非0不写CTL_START；为0才写START；等flush清零、fresh PP_DONE与新RD_PTR后恢复touch |
| 复位检查 | CTL/IRQ普通快照、mux/pull/drive、输出latch和输入跟随差异打印；取消额外1ms输入跟随门槛，不以电气回显阻断命令序列。resetKnown/resetHigh明确表示已命令的状态，不能称为物理电平证明 |

这里没有额外的冷启动 PHY reset（DSI+12c）。Lineage 的 `dsi_display_prepare()` 在 suspend-ULPS
已建立时跳过该脉冲，但 clock callback 中的 `phy_idle_on()` **仍会调用 v4 PHY enable/config**；
不能把“跳过PHY reset”理解为“不配置PHY”。v4 PHY 和 v2.3 controller 的硬件clamp回调均为NULL，
因此没有新增猜测的clamp寄存器写；仍配对执行ULPS重新进入、reset屏蔽和退出流程。

低层依据：`dsi_display.c:3388,3466,7286,8092`、`dsi_phy.c:764,801,1025,1048`、
`dsi_phy_hw_v4_0.c:308,352,377,431,487`、`dsi_ctrl_hw_2_2.c:32,135`、
`dsi_ctrl_hw_cmn.c:172` 和 `dsi_catalog.c:113,252`。
扫描依据 `sde_encoder_phys_cmd.c:1629,1677`、`sde_hw_intf.c:510`。

## 保留的边界

这是在现有 XNU provider/票据和固定扫描管线内对齐所选流程，尚不是完整移植 DRM、RSC sleep/wake 或
CORE collapse。共享 L3/L5、ARC Turbo、boot/config floor和相关核心资源仍保留，避免影响 USB/UFS；
仅撤销本panel的VDDI/VPNL票，聚合状态可以被其他消费者保持，不能据此宣称物理断电。
MDP/SMMU主体布局保留，不采用独立的任意模式重建。

短包FIFO、长包prepared物理DMA、200ms fresh DMA_DONE/idle判据、SMMU路由、ON/OFF表和面板命令延时均未修改。
PV依旧17条/694bytes ON、4条/35bytes OFF，29在11之前；没有新增PPS或首帧前强制延迟亮度。
控制器的command-engine启用继续服从现有传输策略；没有照搬Linux每次transfer的引用计数。
触摸quiesce/token/provider、排他owner/ABI、eGPIO或GPIO IRQ竞争、真实ULPS/REFGEN/PLL/UPDATE/CLK_OFF、
命令和像素完成、100ms引脚操作调度期限等仍是有效检查。失败保留部分状态，禁止自动重试/回滚。

新增 `SWEET PANEL LINEAGE:` 打印ULPS entry/reentry/exit状态与恢复步骤，
`SWEET PANEL PINS CONFIG:` 给出GPIO、offset、want/got/mask（每token最多64条）。
后者的采样不等于ACK；输出显示不跟随也会继续，后续可按打印检查实际寄存器值。
0219 `SWEET PANEL WAKE:` 准入/命令进度证据保留。

## 本地验证与下一步

12套ASan/UBSan回归通过：新增Lineage唤醒、profile lifecycle、Pins、supply fences，
以及DSI host/link、基础lifecycle/FB、DATA parking、link cycle、combined、supply lifecycle。
新增场景覆盖四profile各三轮、寄存器与延时次序、mask位极性、无冷PHY reset、非autorefresh分支、
flush未完成不得恢复touch、ULPS/REFGEN/PLL/RCG/branch失败、每个新增等待阶段cancel/stop/provider loss，
以及GPIO普通回显不一致继续、实际owner失去停止。
现有故障用例把“输入/普通回显不符必须失败”改为观察成功，另保留eGPIO/IRQ owner失去失败的覆盖。

独立抽取并实际执行Lineage六个底层函数：LPRX、ULPS entry/exit、resync、reset_config、clock gating，
验证实际寄存器/100us、1000us、100us轨迹，与新增回归所用时序常量相符。
ARM64 SweetMDSS、SweetPanelPins对象编译通过；MDSS告警1585→1587，净增两条为固定边界host/gates数组索引；
Pins告警1171→1171。Lineage软复位按其保留setup、恢复clock/control合同建模，低层host另测破坏性上下文重建。严格重放0185–0220共36patch，78文件逐字节一致，
114项原CI source markers通过，CircleCI配置未改。完整底稿 `scratch/framebuffer-0220/`。
补丁SHA256：`2d6c31f88189915ee0e0a068ad3f95d32f92fa8503ff99278de25014f55d5e71`。

打包门要求 `SWEET PANEL LINEAGE:`、`SweetPanelLineageSuspendULPS`、`SWEET PANEL PINS CONFIG:` 和
`owned source recommitted/flush issued`；真实旧a29被拒绝，未产生boot输出。

**下一步：用户push最新main并云编译，下载新kernel/buildtree（建议0121a30）；agent核对含0219+0220后，
沿ABI608、原ADT/retained参数打包。** 进入桌面后先做一次OFF/ON，保留USB完整日志；
先看ULPS/供电/soft reset/ON表，再看flush与fresh像素/触摸完成。不要求重测a29。
2026-11-14补充：[0121a30内核和ABI608镜像](PANEL-D1-0121A30.md)已核对就绪，下一步直接实测；
a29仍是最后实测，根因尚未确认。
