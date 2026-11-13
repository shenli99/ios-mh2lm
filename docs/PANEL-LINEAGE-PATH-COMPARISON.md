# LG G8X：Lineage 与当前 XNU 的显示流程和硬件控制对照

2026-11-13。比较对象是 Lineage `android_kernel_lge_sm8150` 的
`2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`，以及实际 a27 源码/retained 参数。
0215（`2ffc1a4`）只增加取证，不改变下述工作路径。
Lineage 一侧是源码所定义的路径，未取得本次同机 Android 寄存器轨迹；不能把它写成实机测量结果。

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
| RSC 自身初始化 | probe 阶段取得 GDSC 和三路时钟，初始化计时、wrapper、sequence memory、solver、PDC 接口 | 没有对应的 RSC 初始化/状态切换序列；检查继承的 wrapper、sequencer/solver、clock 状态后才使用 TCS | 只通过局部准入，不能证明具备 Lineage 的全部初始化条件 |
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
当前 XNU 拒绝已观察到 solver 活跃的状态，但没有证明所有遗留状态和后续变化都已被接管。

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
