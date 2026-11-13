# 0220：按 Lineage 对齐面板息屏与唤醒

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
