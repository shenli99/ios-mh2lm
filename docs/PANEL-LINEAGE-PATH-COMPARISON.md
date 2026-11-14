# LG G8X：Lineage 与当前 XNU 的显示流程和硬件控制对照

2026-11-13。比较对象是 Lineage `android_kernel_lge_sm8150` 的
`2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。顶部新增专项使用实际a29源码/retained参数；
后半保留原a27/0215（`2ffc1a4`）首次接管审计作为历史。
Lineage 一侧是源码所定义的路径，未取得本次同机 Android 寄存器轨迹；不能把它写成实机测量结果。

## 0221配合逻辑修复（2026-11-14，当前）

用户要求补齐配合后，已在`5cf36dd`修复framebuffer mode2的supply/link/data parking映射。
实际MDSS四profile各三轮60份typed状态经实际FB消费通过；原a30同测试失败，确认修复覆盖已见缺陷。
完成保护和命令传输保持；四套本地回归/ARM64对象/累计补丁重放通过，待新云端内核及实机。
实现、118项CI门及下一步见[0221记录](PANEL-LINEAGE-SUSPEND-RESUME.md)。固定管线与完整低功耗的范围限制仍成立。

## 0220复核结论（2026-11-14，修复前）

用户要求复核除命令传输外的四项逻辑。已直接对照Lineage与a30实际源码并运行本地验证：
下层已补OFF供电/reset、ULPS/host恢复及GPIO诊断规则；扫描只做固定管线source/flush/kickoff重新提交。
**整体仍未对齐完成：framebuffer漏改mode2语义，拒绝合法停驻OFF，随后取消token并拒绝ON。**
真实mode2初始化的consumer复现已通过；原FB测试使用旧resources-held完成状态，漏掉这项配合问题。
四项判定、源文件/行号、三个验证输出及边界见[0220四项对齐复核](PANEL-LINEAGE-SUSPEND-RESUME.md#四项对齐复核2026-11-14)。
本轮为审计，没有新修复补丁。下方a29表格是用户引用表的修改前背景，不代表a30现状。

## 0220 实现进展（原记录）

用户随后授权对齐息屏后状态、host/PHY、画面恢复和复位检查；已实现
[0220 Lineage suspend/resume](PANEL-LINEAGE-SUSPEND-RESUME.md)。mode2采用panel票周期、
PHY v4 suspend ULPS/idle-off/on和host soft reset，重新提交自有source/flush，普通GPIO回显改为打印。
命令传输与表保持原样；共享核心电源和固定MDP/SMMU布局仍保留，尚非完整DRM/CORE collapse移植。
本地验证通过，待云编译与实机；以下a29表格是修改前差异审计，不能当成0220当前实现。

## a29 息屏/唤醒专项对照（修改前审计）

用户要求先比较流程，**暂缓0219云编译/下一轮诊断实测**。本节比较实际0121a29/0218源码与上述Lineage提交，
聚焦已选定的 PV / R66456A 普通 OFF→ON。0219只加打印，下面的硬件流程与它相同。
不把 continuous splash 首次接管、AOD 的 LP1/LP2→NOLP、ESD recovery、普通 OFF→ON 混作一条路径。
以下均为源码比较；没有本次同机Lineage实测寄存器轨迹，差异本身不是黑屏根因证明。
a29正常桌面和HANDOFF已实机确认，故下文a27“尚未走到PHY/新DMA”等为历史。

### 先核对具体面板与命令

PV DTS 和 common panel override 定义：四lane RGB888 command mode、1080×2340、60Hz、DSI0/PHY0；
`lp11-init`、`ulps-enabled`、`suspend-ulps-enabled`、`panel-allow-phy-poweroff`、
`lge,use-labibb`、`lge,reset-after-ddvd` 均启用。LG解析器还会按 lane0-state 设置lp11_init，此处仍为true。
本PV节点未配置`lge,incell-panel`、额外panel-pins、压缩/DSC、pre/post ON/OFF命令、`qcom,bl-update-flag`。
因此不能套用其他LG OLED/DSC面板的PPS、AOD退出表或“亮度必须等首帧”的规则。

直接从Lineage DTS抽取字节，与a29 `SweetPanelProfile.h` 的PV数组比较：

| 项目 | 两边实际相同的内容 |
|---|---|
| ON表 | 694 bytes / 17条，LP模式，整表逐字节相等；末尾`0x29 → 0x11 → 等150ms` |
| OFF表 | 35 bytes / 4条，LP模式，`B0 03 → 51 00 00/等90ms → 28 → 10/等150ms`，逐字节相等 |
| reset脉冲 | 高10ms→低2ms→高10ms；LG额外settle5ms，XNU也有5ms |
| 命令格式 | 此ON/OFF每条last=1、VC0、不请求ACK，均在XNU已有编码支持范围；没有为适配FIFO拆包 |
| DSC/PPS | PV没有压缩配置，Lineage的条件PPS分支不适用；不能把XNU没发PPS算成当前缺项 |
| PRE/POST命令 | 普通框架有这些阶段，但此PV未定义相应命令；不能说漏发了不存在的表 |

ON的第3/6/7/10条编码长度超过64-byte FIFO容量，最大payload149 bytes。
对照脚本输出在workspace `scratch/wake-lineage-a29/command-comparison.json`，
ON SHA256=`902b3fd5b869e6304df3c9459a55b6c6da0afdc8162126cc2d4e6086b6c24a5f`，
OFF SHA256=`8b0def2d5a08f9534d8c9efdbbdbbba584387d1cfaecf9bdff50a3c0d71a5b95`。

### 外层OFF/ON状态机没有对齐

| 环节 | Lineage普通OFF/ON | a29 retained（lifecycle2） | 差异的含义 |
|---|---|---|---|
| OFF后引脚/面板供电 | OFF表之后，LG unprepare按lp11_init执行reset_off；普通full-power分支将reset拉低、切suspend pinctrl；power_off撤销panel regulator请求 | OFF表后完成，reset保持高，面板VDDI/VPNL票一直保留 | 我们执行了DDIC sleep-in，却没有建立Lineage的OFF引脚/供电状态；有reset脉冲不等于两边整个reset生命周期相同 |
| host与链路关闭 | disable关闭command timing engine；unprepare关闭host、撤LINK/CORE请求、deinit控制器并配置ISR；PHY的实际保留/恢复受ULPS-suspend和时钟回调控制 | 只停MDP的TE/autorefresh/RD_PTR，host command engine/PHY/PLL/时钟保持；HostReady/LinkReady延续 | retained是一条保留host的恢复路径，不能称为Lineage普通OFF/ON |
| 唤醒到LP11与reset | pre_prepare激活pinctrl，取得panel regulator请求；CORE上电/控制器和PHY恢复、HOST enable、LINK打开并处理ULPS退出；display soft reset后，panel prepare才执行DDIC reset（LP11分支） | 票与host/PHY已经常开；检查资源/MDP与DSI空闲后直接DDIC reset，再准备ON表 | XNU没有对应的ULPS/clamp/LP11恢复状态机；DSI DMA/MDP idle也不等于显式确认lane的LP11/ULPS状态 |
| 面板电源次序 | reset-after-ddvd使panel供电请求在DDIC reset前完成；供电表VDDI有20ms post-on，LG前后各有适用5ms等待 | retained没有撤票，因此唤醒不重新投票/不执行供电恢复延时 | 不能孤立称为“漏20ms”：票没撤与撤了再恢复是两条不同路径；也不能把regulator撤票等同实测物理断电 |
| ON后host | dsi_panel_enable发送完整ON；display_enable建立command timing engine状态；通用host_transfer还会按每次传输引用使能command engine | ON之前command engine一直保持使能；ON后没有相应host重新enable阶段 | 与前面的host保留策略相连，是流程差异；不能只照抄一个reset调用解决 |
| RSC/带宽/时钟 | DRM、clock manager和RSC管理动态请求及sleep/wake/工作模式 | retained保留ARC Turbo、全部clock/NoC/rails；现有RSC只建立当前CLK/ACTIVE路径 | 当前策略更接近常开资源；既有初始化/ACK成功不等于完整低功耗状态机已实现 |

这里的Lineage不是“每次必定把PHY全部reset再开”：PV启用了suspend-ULPS。
`dsi_display_prepare()` 在 `ulps_suspend_enabled && ulps_enabled` 时跳过普通PHY reset/enable；
`dsi_display_ctrl_init()` 也会避免重复配置已经由clock callback恢复的控制器。
实际clkon callback负责必要的controller setup、PHY idle_on、clamp解除、resync FIFO、ULPS退出。
7nm v4 PHY在`dsi_catalog.c:265`提供ulps_request，因此host_disable不走“无PHY ULPS ops时只更新软件状态”的特殊分支。
同样，`dsi_display_wake_up()` 在此版本是空函数返回0，不能把调用这个函数本身说成补齐了唤醒。

### 命令传输、恢复扫描与额外检查

| 项目 | Lineage | 当前XNU | 判断 |
|---|---|---|---|
| 命令缓冲区 | 普通host_transfer使用FETCH_MEMORY，短包也走非安全GEM/IOVA；首次需要时分配4KiB，后续复用 | ≤64 bytes用FIFO，其余用已prepare的物理连续缓冲；固定继承SMMU CB2 bypass；ON准入先准备整表所需DMA | 路径确有差异。正常Lineage此前的亮度/OFF也可已使用DMA；XNU成功OFF全是FIFO，首次像素DMA来自MDP，不验证DSI命令DMA |
| DSI命令完成 | CMD_DMA_DONE中断/completion，200ms；IRQ未到但真实done status存在时有读状态补偿 | timer读取fresh DMA_DONE并同时要求DMA/MDP idle，200ms，命令延时异步执行 | 调度/完成通道不同；两边都依赖真实done，不能用配置回显当完成。没有证据说明当前需删除DMA完成检查 |
| 恢复像素 | DRM重新提交管线/flush/kickoff，配置tearcheck；autorefresh路径本来就不发CTL_START，其他路径发start | 沿用固定MDP配置与原物理源，重开IRQ，恢复保存的autorefresh/TE，再等flush0+fresh PP_DONE+RD_PTR | XNU恢复的是已有扫描管线，尚不是DRM重新配置提交。不能仅因没写CTL_START就断言遗漏，Lineage自身有autorefresh分支 |
| 亮度 | 普通bridge enable解除blank并更新背光；只有指定bl-update-flag的面板才先等TX_COMPLETE。本PV无该flag | 完整ON结束后先恢复最新0x51，再恢复扫描 | 时机受不同框架安排；**不能说Lineage对本PV强制先出一帧再开亮度**。大端12bit亮度格式相同 |
| touch协作 | LG reset/blank通知touch，触摸驱动自行排队resume/init | reset前worker/IRQ/HID必须quiesced；新像素完成后才TCM app/format校验并恢复 | XNU有更强的跨provider完成依赖，Lineage不是用同样的token/PP_DONE门槛 |
| GPIO复位验证 | gpio_direction_output返回值检查，然后gpio_set_value+usleep；未逐边沿要求独立input回读 | latch/pinctrl/owner与reset input均强制检查，input需在1ms内跟随，reset操作整体100ms | **0218没有把SweetPanelPins的这些额外强制检查改成打印**。不过a29已经打印reset complete，它们本次已通过，不是已知报错点 |
| 地址空间/资源检查 | 由GEM/IOMMU、clock/regulator/DRM的软件对象管理 | 每轮验证touch/provider票据，以及固定SMMU/geometry等继承合同 | 不应把这些所有权/地址路由合同与普通寄存器配置回显等同，也不能据“Lineage没做同样memcmp”直接删除 |
| 错误处理 | OFF发送失败会警告但继续关闭，源码理由是后续enable会完整reset控制器 | 任一生命周期失败会poison、保票、禁止自动恢复 | 依赖的恢复能力不同。当前OFF成功，不能用它解释本次黑屏；尚无完整恢复能力时直接复制忽略错误不成立 |

### 后续按流程对齐的顺序

优先补齐的是OFF/ON的**状态配对**：选择明确的host/PHY/ULPS策略，确定OFF时reset低与panel票的状态，
让ON的供电/LP11/host恢复→DDIC reset→同源ON表→扫描提交彼此对应。
不能只把当前模式从2切到已有4/5/6就称为Lineage：这些档分别增加link/data/panel票周期，
仍没有完整的ULPS/clamp/RSC/DRM状态机，且尚未做本轮实机验证。
也不应照搬其他DSC/AOD面板的PPS/NOLP，或重排已与PV逐字节相等的ON/OFF表。

0219诊断提交d4ffb3d保留，本轮不新增生产补丁、不要求立即push/云编译、不操作设备。
目前尚不能从源码差异判定a29复位之后无日志的具体根因；特别是host/扫描差异不自动解释“为何没有ON admitted”。
本轮完成的是流程审计与记录，后续以本节为实现对照依据。

具体来源（Lineage目录相对路径，行号对应2e897dd）：

- `arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-pv-tianmamd-fhd-cmd.dtsi:34,40,393,414`：reset、LP11/ULPS和同源ON/OFF。
- `arch/arm64/boot/dts/lge/sm8150_common/sm8150_common-panel.dtsi:37,533`：VDDI/VPNL供电、PV override。
- `drivers/gpu/drm/msm/lge/lge_dsi_panel.c:180,689,778,837,881,981,1014,1047,1104,1134,1675`：完整电源条件、reset、LP11与touch通知。
- `drivers/gpu/drm/msm/dsi-staging/dsi_display.c:519,2589,2884,2985,3388,3466,3610,7286,7791,8031,8092`：DMA、空wake_up、ULPS/clamp/clock与正常OFF/ON。
- `drivers/gpu/drm/msm/dsi-staging/dsi_panel.c:525,2342,4383,4454`：发送与延时、亮度flag、ON/OFF错误处理。
- `drivers/gpu/drm/msm/dsi-staging/dsi_ctrl.c:41,1088,1156,1305`：FETCH_MEMORY/FIFO选择与200ms中断/状态完成。
- `drivers/gpu/drm/msm/sde/sde_encoder_phys_cmd.c:1107,1629,1677`：TE、autorefresh/CTL_START分支。
- `drivers/gpu/drm/msm/sde/sde_connector.c:719`；`drivers/input/touchscreen/lge/touch_core.c:940`：亮度和触摸通知。
- XNU：`scratch/artifact-0121a29/actual/xnu/iokit/Kernel/arm/` 的 `SweetMDSSLifecycle.inc:298,368,436,449,504`、
  `SweetMDSSDSI.inc:12,56,214`、`SweetMDSSHost.inc:111`、`SweetPanelPins.cpp:162`；参数 `sweetpanellifecycle=2`。

## 0218 配置回读规则（同日后续，以此为准）

0217 先取消六项 CNTP 回显门槛；0218 继续核对 RSC→NoC→clock/core→PHY/PLL→host→
scanout 和休眠/恢复，将已审计的额外配置相等/快照检查改为 `SWEET MDSS CONFIG:` 打印。
Lineage 本身的三个 CONTROL 同步写、busy/UPDATE/CLK_OFF/REFGEN/LOCK、真实 IRQ 和命令/像素完成保留。
详细源码依据、仍保留的继承合同和本地验证见 [0218 配置观察](PANEL-LINEAGE-CONFIG-OBSERVATION.md)。
下方 a27/0215 表格为历史；不能继续把其中“无 RSC 初始化”等描述当作当前源码状态。
a29已实机完成首次显示接管；仍未实现整套 Lineage 的休眠/唤醒状态机及continuous splash路径。

## 0216 实施进展（同日后续）

以下原对照表记录 a27/0215 的状态。用户授权补缺后，0216 已实现当前 CLK/ACTIVE 路径的
RSC v2 初始化、CLK 状态交接、TCS_USE_OK 握手、INTID161 真实完成事件，以及 Lineage
ACTIVE 的控制寄存器保留/槽位复用流程；`+0xd0` 生产日志已改正为 error-irq-status。
65 项初始化写入与执行 Lineage 实际函数所得结果逐序一致，相关本地回归通过，尚待实机。

这不等于补齐了完整 mode2 进出、CMD solver/VSYNC/sleep-wake、continuous-splash PHY/host
路径或独立 MDP/SMMU 配置。当前先验证资源接管能否越过 NoC，再继续对齐后续阶段。
详见 [0216 实现与验证](PANEL-RSC-TAKEOVER.md)。

## 首次资源接管的缺项复核（同日补充）

再次逐项核对到 TCS 提交/完成，明确存在下面的缺项。需要区分“XNU 没有实现该能力”
与“硬件此刻必然缺少该配置”：温接管的固件遗留状态可能已经满足部分条件。

| 缺项 | 当前实际实现 | 对当前问题的意义 |
|---|---|---|
| 显示 RSC 完成中断及本次完成通道 | 只有 NoC timer，未接 INTID161；编程时清本槽 IRQ_ENABLE，执行等待时继续屏蔽 | 与当前完成等待直接相关；需要可靠的本次完成证据，不能单靠曾观测 busy。现有模拟风险成立，真机是否触发仍未知 |
| TCS 使用权交接 | 读 wrapper 的 SEQ_BUSY 和 DRV 的 SEQ_BUSY，但不写 TCS_USE_OK 协调硬件执行器 | 相比 Lineage 带宽更新流程，缺少“请求停止使用→等待空闲→修改→交还”的步骤；单次读空闲不等同交接完成 |
| RSC 工作状态的完整确认/转换 | 有 wrapper override bit0、power bit12、busy 与错误检查；无完整模式切换/退出低功耗流程 | 只能接受有限的继承状态，不能证明从任意固件状态建立了 Lineage 的软件触发条件 |
| RSC 首次/失电后完整初始化 | 未建立 Lineage 的计时、wrapper、序列内存、solver、PDC 配置 | 完整独立启动确实缺；其中哪些为当前 warm ACTIVE 所必需仍需判断，不能全当作本次超时原因 |
| 自动调度和资源完整撤回 | 未实现 display sleep/wake 表与对应模式管理，保留若干共享资源 | 属于后续低功耗/冷恢复能力；当前常开 ACTIVE 路径不必先实现全部内容 |

**新增确认：现有 `gate=solver-status` 的寄存器名称错误。**

- XNU `noCAdmission()` 读 `fNoCRSC + 0xd0`，变量叫 solver，失败日志叫 solver-status。
- Lineage `sde_rsc_hw.h:31` 明确把 `+0xd0` 定义为 `ERROR_IRQ_STATUS_DRV0`；
  `sde_rsc_debug_show()` 也按 error irq status 输出它。LG 原厂参考头文件定义相同。
- 真正的 `SOLVER_STATUS0/1/2` 在 `+0xc24/+0xc28/+0xc2c`，solver override 在 `+0xc14`。
  当前 NoC 模块没有读取这些状态寄存器。

因此，`+0xd0 == 0` 只说明这次读到的错误中断状态为零，不能用它证明 solver 空闲。
原 wrapper override bit0 检查仍然存在，不能反过来说完全没有 solver 模式相关保护。
本次纠正的是审计结论和文档；生产代码的旧标签及真实状态采样尚未修改。
0215 仍只采集 TCS 完成证据，也没有补齐这组 solver 状态。

没有发现漏掉本次 payload、WAIT、CMD_ENABLE、AMC、TRIGGER 这些基本提交步骤。
成功后的 AMC 撤回与 Lineage 不同，属于已有动作的差异；它尚未执行，不能解释 a27 首组等待超时。
branch7 旧 enable 门槛已修正，四 LDO/ARC/CN0/core/RSC clocks 也已有 a27 成功证据。
修复优先级应聚焦已知 RSC 状态、TCS 交接和可靠完成通道；现有 0215 尚未补上这些流程能力。

## 硬件对象一致，接管范围不同

本机已知 ABL 选择是 TIANMAMD PV / R66456A，1080×2340、60Hz、RGB888、四 lane command mode。
Lineage 的 PV display 指向 controller0/PHY0；XNU 也使用 DSI0/PHY0。
面板 reset 为 GPIO6，TE 为 GPIO8。DDIC 型号依据 ABL/DTS，仍不是物理 ID 读回。

两边使用 SM8150 的同一套 DISPCC、MDSS GDSC、display RSC/RSCC、NoC、MDP、DSI/PHY 硬件。
apps RSC 与 display RSC 是两个不同请求端，这一区分 XNU 已保留；CN0 属于配置通路，
MM1/MM0/SH0/MC0/ACV 属于显示数据通路。不能把 apps 的 ACK 当成 display 数据带宽完成。

| 阶段 | Lineage 的流程/硬件控制 | 当前 XNU 的流程/硬件控制 | 当前实机边界 |
|---|---|---|---|
| 启动画面交接 | 支持 continuous splash：接管资源和引擎状态，跳过面板 ON、PHY reset/重复硬件 enable；也有正常 prepare/enable 路径 | 入口要求既有 splash、固定扫描/SMMU 状态；在接管过程中逐层建立自己的控制 | 仍依赖 ABL/Mu 留下的显示状态 |
| 电源与频率 | regulator/时钟/性能路径按消费者聚合、负载和模式调整，执行对应时序 | 固定取得 VDDI 1.8V、VPNL 3.0V、host 1.2V、PHY 0.88V；ARC Turbo、AHB75MHz、MDP460MHz 和引导带宽 | a27 四路供电请求、ARC、CN0、core profile2 已完成；这些不是电压/频率实测 |
| RSC 自身初始化 | probe 阶段取得 GDSC 和三路时钟，初始化计时、wrapper、sequence memory、solver、PDC 接口 | 没有对应的 RSC 初始化/状态切换序列；检查继承的 wrapper、sequencer busy、错误状态与 clock 后才使用 TCS | 只通过局部准入，未读取真正的 solver status，不能证明具备 Lineage 的全部初始化条件 |
| RSC 工作模式 | CLK/AMC、CMD、VID、IDLE 分支；模式切换与客户端和必要的 VSYNC 完成协调 | 仅允许检查结果符合软件触发条件的继承状态，不主动从任意状态切换到 CLK/AMC | 不能把“空闲样本”视为已完成模式交接 |
| 带宽提交 | 软件触发 ACTIVE，以及硬件自动执行的 sleep/wake 两条路径；更新前协调 sequencer 的 TCS 使用权，更新后恢复 | 只发送 ACTIVE2/3，按 VCD 分组、尾命令 COMMIT；不编程 sleep/wake，不做 TCS_USE_OK 写握手 | 第一个 ACTIVE2 请求正在这里超时 |
| ACTIVE 完成 | 注册 display IRQ，再启用 ACTIVE source mask；IRQ 到达后检查命令 ISSUED/COMPLETE | 整个执行期间屏蔽自己选中的槽 IRQ，timer 轮询 idle/命令位，并要求本槽 IRQ 或曾观测 busy/不完整 | 快速完成漏判是可复现候选条件，尚未证明为真机根因 |
| PHY/DSI host | splash 路径保留；正常初始化路径按 PHY/PLL/clock/host 顺序建立链路 | retained 参数启用 link2/host1，资源完成后会停止并重建 PHY/PLL、输出时钟和 host，同时保留面板已亮状态 | a27 尚未走到这一步，不能归因为当前首错 |
| 面板 reset/ON/OFF | LG override 处理供电、引脚、reset、完整命令及通知；splash 首次接管可跳过重发 ON | 有同源 PV 等命令表和受控 reset/休眠恢复；首次 takeover 保留面板状态，不重发整套 ON | 初始化表匹配不等于已完成实机 reset/恢复验证 |
| MDP/SMMU/帧提交 | DRM 配置显示管线、地址空间、平面、提交与帧完成；另有 splash 接管和释放资源的逻辑 | 保留固定 VIG0/CTL/INTF 路径及既有 SMMU CB2 bypass，改物理缓冲地址/步长/裁剪，要求 fresh PP_DONE/flush/RD_PTR | 还不是任意模式和缓冲映射的完整重建；a27 无新 DMA 提交 |
| 休眠与资源撤回 | 与显示工作模式、共享电源/时钟引用、LPWG 等协作 | 当前 retained 档保留资源；扩展档已有 DATA/link/面板两票周期，跨设备共享 L3/L5 的最后撤票仍受保护 | 扩展实机测试暂停，完整断电/冷重建未验收 |

## 两条启动流程

Lineage 并非每次开机都把屏幕全部复位重建。必须分开看两种情况：

- 有 splash：建立 Linux 自己的供电/时钟/总线请求与 RSC 状态 → 接管现有 DSI/面板/扫描状态 →
  进入正常 DRM 提交并退出 splash 资源持有；连续显示路径跳过面板 ON 和 PHY reset。
- 正常准备显示：按资源依赖建立总线/电源/时钟/RSC → panel pre-prepare 供电与适用的 reset →
  PHY/PLL/host/link → panel prepare/enable 命令 → MDP 提交。
  reset 的具体位置受 LP11、LG profile、ULPS 等条件影响，不能拿一个通用直线顺序覆盖所有分支。

当前 XNU retained 的实际顺序：

```text
确认继承的 splash/时钟/扫描与 SMMU 状态
→ 暂停触摸事务、TE/autorefresh，确认旧传输停止
→ 四 LDO 请求与等待
→ ARC Turbo
→ CN0 / GDSC / core PLL 与 AHB/MDP 配置
→ CN0 与 RSC 时钟租约
→ display ACTIVE TCS 数据投票          ← a27 首错
→ DSI0 时钟租约
→ PHY/PLL/输出时钟重建 → host 重建
→ 替换扫描源 → 新传输完成 → 恢复触摸
```

这是一条逐层接管现有显示的流程。它同时包含状态继承与主动链路重建，
并不等同于 Lineage 的连续 splash 分支，也不等同于从全冷状态启动。

## 与当前 TCS2 故障最直接的差异

### 1. RSC 的可用状态需要建立，不能只依赖几项空闲读数

Lineage 的 RSC v2 初始化会写计时器、wrapper 配置、sequencer 指令、solver 表和 PDC 接口地址。
随后由状态机控制 CLK/CMD/VID/IDLE。XNU 对 RSC/wrapper 的访问目前仅有准入读取，
没有这些初始化和显式模式交接操作。

此外，Lineage 更新显示带宽会先通过 wrapper 的 TCS_USE_OK/SEQ_BUSY 协调硬件 sequencer，
再 invalidate/更新/flush 请求，之后恢复许可；XNU 只检查当时的 busy 状态。
“检查空闲”与“禁止硬件在本次修改期间使用 TCS”是不同的硬件动作。
当前 XNU 拒绝 wrapper override bit0 置位的状态，并检查 busy/错误位，
但没有读取真正的 solver status，也没有证明所有遗留状态和后续变化都已被接管。

这是真实的功能缺口，但并非“缺少任意一个 solver/PDC 初始化写，ACTIVE 就必定失败”的证明。
其中一些机制用于自动低功耗，不应未经分析整批搬入首次 ACTIVE 诊断。

### 2. Lineage 的软件触发与自动调度路径不能混为一谈

SM8150 bus DTS 的 display `req_state=2` 对应 ACTIVE_ONLY/AMC。
在非 solver 状态，active batch 通过 ACTIVE TCS 触发并等待完成。
进入 solver 状态时，`check_ctrlr_state()` 明确拒绝 ACTIVE/awake 软件提交；bus 层对 display
的这类 `-EBUSY` 有专门处理，仍维护 wake/sleep 请求，由 RSC 更新/flush 与硬件模式配合执行。
`rpmh_mode_solver_set()` 上方注释与这个实际检查相矛盾，本审计按可执行分支判断。

所以不能说“Lineage 也是不管状态，向 TCS2 写同一组命令”。
当前 XNU 是 ACTIVE-only 常开接管策略，要与 Lineage 的对应非 solver 分支比较。
后续自动低功耗模式是另一组功能，尚未对接。

### 3. 完成通知的硬件设置确实不同

Lineage mailbox probe 先注册 IRQ handler，再启用 ACTIVE mask；display DTS 的 SPI129 为 INTID161。
XNU 已接入的 apps INTID37 与 display 不是同一中断。
XNU display NoC 选槽后清该槽 IRQ_ENABLE，等待期间没有恢复；直到成功清理才恢复原位。

若 source mask 也影响 IRQ_STATUS，且请求在首次 timer 轮询前完成，
XNU 可能既没看到 busy 过程，也拿不到本槽 IRQ，于是完成位齐全仍超时。
0215 的模拟已覆盖这个条件，实机仍需寄存器证据。不能把 mask 清除与 raw status 是否锁存的未知语义混淆。

Lineage 的 ACTIVE 成功清理还会保留 AMC 设置，只清命令 enable/wait 和 IRQ；
XNU 每组清理会撤 trigger/AMC。它可能影响连续组的行为，但 a27 首组尚未成功，
这个后续清理差异不是本轮第一个等待超时的直接原因。

### 4. 分支时钟的旧差异已实机越过，启动顺序仍不完全相同

Lineage 的 RSCC clock 列表为 VSYNC → NON_GDSC_AHB → RSCC_AHB；
XNU RSC mask 按表顺序处理共同 AHB → NON_GDSC_AHB → RSCC_AHB → VSYNC。
但既有 a26 取证显示 VSYNC 和 RSCC_AHB 已由固件打开，a27 完整 RSC lease 已 Held，
不能仅凭列表顺序差异把它重新定为当前根因。

0214 已修正自身 voted 请求的额外 enable 读回门槛：a27 出现 halt complete 和 mask381/Held。
这证明旧软件阻塞被越过，不解释 bit0 的读零原因，也不证明后续 RSC 电路配置完整。

## 面板电源方面容易误判的地方

所选 profile 的供电表就是 VDDI/VPNL 两路，另有 DSI host/PHY 的模拟供电。
`lge,use-labibb` 和 LG 的“Turn on LABIBB”文本不代表这里必定还缺一颗需要直接编程的 LAB/IBB 外设：
该分支调用的是 panel 的 `power_info`，这份 DTS 的表是 VDDI/VPNL；尾部 PMI8998 LABIBB 片段在注释内。
板级外部偏压来源仍需硬件资料，不能单凭名字认定 XNU 漏供电。

L3/L5 与 UFS/USB 等设备共享。Lineage 由其资源框架管理引用；XNU 还未汇集所有消费者的投票，
因此保留共享供电。这影响完整断电/恢复能力，目前这两路开启请求已通过。

## 排查顺序与证据范围

优先核对 RSC 是否处于明确的非 solver/软件提交状态、TCS 使用权交接，以及本槽完成通知。
0215 已准备的取证仍有效；它能分开期限、配置读回和完成状态，但不是 RSC 初始化完整性的全量证明。
若新证据指向完成条件未满足，应补查上述硬件前置状态，而非直接放宽 ACK 或只拉长时间。
后续才验证尚未到达的 PHY/host、面板恢复和新 DMA。

本次仅源码/流程审计与文档更新，没有修改驱动、参数、固件或另加云编译轮次。
原 0215 编译交接保持；没有重复既有回归，也没有操作设备。

## 来源索引

以下为 `ref/lge-lineage-kernel/` 内本次读取的源码位置：

- `arch/arm64/boot/dts/lge/sm8150-mh2lm/sm8150-mh2lm_common/sm8150-mh2lm_common-panel.dtsi`：接入公共 panel 定义。
- `arch/arm64/boot/dts/lge/sm8150_common/sm8150_common-panel.dtsi:37,197,533`：电源、DSI0/PHY0、PV/DDIC/GPIO。
- `arch/arm64/boot/dts/qcom/sm8150-sde.dtsi:292`、`sm8150.dtsi:2613`、`sm8150-bus.dtsi:48`：RSC v2、时钟、TCS、中断及 req_state。
- `drivers/gpu/drm/msm/sde_power_handle.c:925`：电源/总线/RSC/时钟依赖。
- `drivers/gpu/drm/msm/sde_rsc.c:1123,1170,1329,1799`：初始化、状态切换、带宽更新、probe。
- `drivers/gpu/drm/msm/sde_rsc_hw.c:674,866,911`：完整初始化与 TCS 使用权握手。
- `drivers/soc/qcom/rpmh.c:299,629,730`、`msm_bus/msm_bus_fabric_rpmh.c:627`：模式准入、active/sleep/wake 提交。
- `drivers/mailbox/qcom-rpmh-mailbox.c:442,1292`：完成处理和先注册后启用 IRQ。
- `drivers/gpu/drm/msm/dsi-staging/dsi_display.c:2949,4904,7286,7791`、`dsi_phy.c:946`：splash/普通路径。
- `drivers/gpu/drm/msm/lge/lge_dsi_panel.c:981`：LG pre-prepare/reset；`sde/sde_kms.c:2568,3109`：splash 与地址空间。

XNU 依据实际 a27 的 SweetMDSSResources/NoC/Clock/Link/Host/Lifecycle/Scanout，
以及 `out-bs/unixnu-16k-0121a27-abi608-panel-retained.toml`；实测见 [a27 记录](PANEL-D1-0121A27.md)。
