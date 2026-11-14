# 0227：普通息屏/唤醒的 ESC0 低速时钟周期

2026-11-14。在a36/0226四轮普通唤醒基线上，推进C08的**ESC0停启子项**。mode2/7普通OFF在HS停、ULPS进入后关闭ESC0，确认分支真正停止，再关PHY；普通ON先恢复ESC0并确认分支/父源可用，再配置host/PHY。**本地实现与验证完成，待新CI产物及实机。** 这不是完整C08或共享core电源收口；a36仍为最新实机基线，BUG-006/007未修。

## 参考结论与本批范围

Lineage固定 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`，路径相对 `ref/lge-lineage-kernel/`。

| 对象 | 核对结果及其影响 |
|---|---|
| `dsi_clk_manager.c:dsi_update_clk_state` | 普通关钟先HS OFF，再LP OFF；LP的pre-off callback进ULPS，LP helper关ESC，post-off callback再关PHY。不能把全部步骤合成“关HS”。 |
| `dsi_link_lp_clk_start/stop` | 实际LP资源为ESC clock。start在非continuous-splash时设rate，再prepare/enable；stop disable/unprepare。夹具现在执行这两个原始函数，不再用一个“lp-on/off”桩代替。 |
| `dsi_display.c:dsi_post_clkon_cb` | LP恢复后配置host/PHY，ULPS重入，再HS启动、resync和ULPS退出。本批只补此前一直保留的ESC分支周期，沿用已有19.2MHz XO父源，不重写RCG频率。 |
| `drivers/clk/qcom/dispcc-sm8150.c` | ESC0为`BRANCH_HALT`，CBCR/enable均`0x2038`、请求bit0；父源RCG `0x2148`。停止/启动用实际`CLK_OFF`检查，不以bit0回显替代完成。 |
| `dsi_host_transfer`、DSI/MDP clients | 每包ALL_CLKS引用嵌套于prepare或MDP持票，并非每包物理关钟；固定版wake_up为空。完整动态引用尚未移植。 |
| `sde_encoder.c`、`sde_connector.c` | encoder先取得SDE core/DSI票，再IRQ/QoS；connector使用独立MDP clock handle。FRAME_DONE在自动刷新开启时不安排常规延迟idle-off；pending帧多于1也跳过。此部分是原始caller/条件静态核对，未执行完整SDE/DRM栈。 |

上述聚合条件解释了为什么不能直接给每包套一对物理开关，也不能在当前autorefresh期间撤掉全部资源。本批选择可单独验证的普通OFF/ON ESC分支；完整post_enable/pre_disable、DSI/MDP动态票、RSC/QoS及shared core/SMMU仍在[全流程矩阵](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)保留。

## 实现与安全边界

```text
OFF：停扫/真实排空 → 原OFF表 → reset-low/host关
     → HS停止 → ULPS进入成功 → ESC0停止请求 → CLK_OFF确认
     → PHY关/复位传播屏蔽 → 原DATA与panel票停放 → OFF完成
ON ：原panel/DATA恢复 → ESC0启动请求 → CLK_OFF清除且XO父源可用
     → host setup/PHY配置 → ULPS重入 → 原HS/PLL/ULPS退出
     → 原ON表 → a36色彩/首帧/亮度/第二帧/触摸完成
```

新增`LinkLPStop`、`LinkLPStart`在内部enum尾部，旧值不移动；`linkBusy()`包含新等待态。每次等待受已有link总250ms、生命周期总16s约束，ESC每方向另限10ms。旧模式4/5/6和首次接管的流程保持，旧resume阶段初始1ms期限也保持；只mode2/7普通息屏链增加ESC停启。

`fLinkSuspendedMask`先由0到HS的`0x70`，ULPS完成发ESC停止请求时到`0x78`；wake ESC真完成后回`0x70`，原HS/新配置完成后归0。持有的BOOT租约仍是软件归属，不是运行证明；`hardwareSuspended`覆盖ESC，普通clock/DSI请求仍拒绝过渡状态。资源/供电停放只接受对应模式完整mask，ESC引用也必须只有本轮BOOT/managed合法持有者；额外使用者会阻止停钟。

新等待复用已有participant、reset-low、触摸quiesce、IRQ/CTL/line排空、资源与期限检查。状态未完成时不配置后续host/PHY、不发像素；失败保留资源和扫描池pin、不自动重试/恢复。ESC是PIO时钟操作，不新分配DMA，也不改变prepared4KiB缓冲、FIFO/物理DMA、命令真完成或独立pin规则。

生产补丁 `xnu/patches/0227-mh2lm-lineage-esc-clock-cycle.patch`，SHA256 `d9646f81366355e8929b852bcb860f6e7d512682d1a08680a402f80a81d7271e`：修改MDSS.cpp、Link.inc、Lifecycle.inc、Resources.inc、PanelSupply.inc，以及DisplayClock.h的一处说明。公有Backend状态布局、ABI608、ADT、605byte pipeline参数、固件保持。没有实施core/GDSC collapse、共享SMMU失效恢复，也没有以停止ESC推断物理panel rail-off或节电幅度。

## 验证

证据目录 `scratch/framebuffer-0227/`，入口 `final-review.json`。

- 16组驱动回归：mode2/mode7四profile各三轮正常流程，两模式ESC专用、命令生命周期、亮度、色彩、实际MDSS→FB桥接，以及旧mode6供电、mode4 link、host、DSI序列。
- ESC专用测试用独立物理peer延迟/拒绝CLK_OFF变化；覆盖两方向完成延迟与超时、请求写丢失、两等待态各10种cancel/stop/provider/deadline/引用/父源/TE/CTL故障、父root未运行、第三份真实DSI租约拒绝。失败不越过下一步或晚写，不通过删除完成门槛使测试通过。
- Lineage16个原始函数执行：原HS/clock manager/callback，加真实LP start/stop；正常/continuous-splash rate条件、嵌套DSI和独立MDP票通过。OS/CCF、core/PHY叶子仍为桩，不能称完整DRM或硬件执行。
- 两模式各120条MDSS状态交给实际FB逻辑，完成/色彩/像素/触摸保护通过；18坏凭据、mode7额外6种拒绝保持。真实设备仍待验证。
- ARM64 MDSS基线与最终修改编译成功：1596→1599条告警，新增3条均是`fClockParents[3]`固定下标访问的`unsafe-buffer-usage`；数组长度10，未改编译器或关闭告警。其余告警类别/数量不变。
- 0227单补丁检查及0185–0227共43patch严格重放，80份文件逐字节一致。公有状态头与启动参数保持。
- CircleCI仅替换1条源码检查、增加2条，合计146 markers；YAML、4处参数模板、6段run shell通过，工作流/executor/资源/参数保持。未调用服务器验证或触发云构建。
- 打包相应更新3个内核标记（净增2），真实旧a36被新门槛拒绝且没有输出镜像。a36 pipeline SHA仍为`9ea8bb9763a946310b6cd1714b4542f1bb397d64ea292edec1822b83ffed4e6d`。

## 下一步

用户push后下载新CircleCI kernel/buildtree，建议编号**0121a37**；agent核对0227、沿原ABI608打包mode7，再交付启动命令。当前没有新kernel/boot或实机结果。

实机先验证首次接管、触摸、按键与调光，再两轮息屏约5秒/唤醒，每轮亮屏至少60秒并留USB/持久日志。新增完成日志应依次包含：

```text
ULPS entered after HS stop; ESC0 halt pending
ESC0 halted after ULPS; PHY shutdown next
... OFF complete ...
ESC0 running before ULPS host setup
... 原ULPS退出、ON表、六包色彩、亮度、第二帧、触摸/ON complete ...
```

BUG-007只记录，不要求本批消失。ESC子项实机通过后，再推进C08剩余动态资源/共享core工作；完整显示驱动仍在PD4。
