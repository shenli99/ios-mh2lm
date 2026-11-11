# D1：GDSC 与显示核心时钟控制（0189）

2026-11-11，接0188。新增 `SweetMDSSCore.inc`，在实际扫描资源获取中接入显示域与核心
时钟控制；本地验证通过，尚未实机。**DSI 链路和整屏冷启动仍未完成，D1 未整体验收。**
完整驱动优先，之后零拷贝；本段不单独增加云编译/刷机轮次。

## 所有权与接入

```text
SweetMDSS : IOService
 ├─ 原 workloop / command gate / 唯一 DISPCC、GDSC MMIO owner
 ├─ managed 资源协调器（实际 beginScanout 续程）
 │   ├─ SweetRPMh typed 四路 LDO / ARC / CN0
 │   ├─ core 控制器（复用原 clock timer、writer 和账本）
 │   │   ├─ GDSCR 软件供电票
 │   │   ├─ TRION PLL0 / PLL1
 │   │   └─ AHB / MDP RCG、核心分支及完成状态
 │   └─ 原 NoC / local clock 模块
 └─ 原 IRQ / DSI executor / 扫描地址提交与 DMA pin
```

没有新增映射、线程、独立 provider 空壳或全局实例桥。所有硬件访问仍在 MDSS owner 内，
上层不传寄存器地址。内部入口是 `beginCoreResources(SweetCoreWarm / SweetCoreCold)`；
状态通过原资源协调器等待，clock timer 每次只推进一个步骤，不在 gate 中忙等。

显式 `sweetdisplaycore=2` 选择 LG 的 SM8150-v2 配置；要求原 clock1、managed1 及其依赖。
默认0保留0188行为；其他 profile/依赖缺失明确拒绝启动。IORegistry 的
`SweetDisplayCoreSelectedProfile` 表示**所选配置，不是检测出的硬件版本**。
现有解码 DT 声明 `qcom,dispcc-sm8150-v2`，但本段未取得新的实机版本读数，不把它升级成
已验证的芯片身份；v1 的 TEST_CTL_HI=2 不会被套用。暂不新增镜像参数矩阵。

实际温态接管顺序为：暂停 TE/autorefresh、观察 idle、暂停 IRQ → 四 LDO → ARC Turbo
整链完成 → CN0 300MB/s 完成 → GDSC → 两颗核心 PLL → AHB/MDP RCG → 核心分支
物理运行 → 原 NoC → DSI0 lease → 原 fresh PP DONE/flush/RD_PTR 及自有 A 提交。
资源完成不替代像素传输完成；本段仍不改 CPU 拷贝频率、A 单缓冲或 B 未提交的现状。

## GDSC 与访问门槛

GDSCR 在 DISPCC+0x3000。配置通道的外层门槛是 GCC_DISP_AHB 的软件 enable，不能先要求
域内 DISPCC_AHB/MDP 已运行，否则无法开启关闭的显示域。冷路径在实际 ARC 与 CN0 ACK 前，
不读取 GDSC/PLL；整个冷态核心引擎不读取 MDSS pipeline、DSI、PHY 或 display RSC。

只 RMW 清 SW_COLLAPSE bit0，保留其他位；HW_CONTROL bit1 或 SW_OVERRIDE bit2 存在时
拒绝，不把 Linux probe 的强制清位当成可随意接管的依据。写后通过原 dp_wr 屏障，并等待
至少1µs（超过8个 XO 周期）才查询 PWR_ON bit31；软件票清除不等于电源已完成。
PWR_ON 通过后再等待2µs稳定时间。PWR_ON 查询预算100µs，整条 core 链500ms。

当前真实启动入口仍要求 splash/live pipeline；它调用 Warm，意外掉电会失败，绝不暗中
走 Cold 修复。Cold 入口仅供后续完整 link-init 调用，拒绝既有 clock、IRQ、DSI、scanout/DMA
账本；其实际冷态寄存器控制已经在组件测试执行，**没有声称现有内核可从显示全关启动**。

## PLL、频率与分支

| 对象 | 实现 |
|---|---|
| PLL0 | TRION，offset0，L=0x47、alpha=0xe000，1380MHz |
| PLL1 | TRION，offset0x1000，L=0x1f、alpha=0x4000，600MHz |
| AHB | CMD0x22bc，PLL1 main /8，75MHz，HID=15 |
| MDP | CMD0x20c8，PLL0 main /3，460MHz，HID=5、HW_CLK_CTRL_MODE |
| 核心分支 | CBCR0x2080 / 0x200c；实际 enable、CLK_OFF 清除且 root 运行才完成 |

这些是 LG 频率表的最高 AHB/MDP 档，暂保留原 Turbo 资源保护；没有 DVFS、正常低功耗
频率策略或实际频率计数测量。不会为实现核心时钟而改 BYTE/PCLK 或 DSI PHY PLL。

运行中的 PLL 要求 RUN、RESET_N、OUTCTRL、LOCK、main output 与 L/alpha 符合目标，
符合则保留，记录 inherited mask，不重写模拟配置；频率不同或 FSM/部分运行状态明确失败。
关闭的 PLL 先检查全部可选择核心 PLL 的 RCG：AHB、EDP_GTC、MDP、ROT；若有运行中的
孤立使用者，不强行重启其父源。关闭 PLL 才逐项写 LG v2 的 L/CAL/alpha/CONFIG/USER_HI，
设置 HW_UPDATE_LOGIC_BYPASS、output off、STANDBY、RESET_N，再 RUN → 等 LOCK →
打开 USER 输出 → OUTCTRL。每个配置写都有读回；MODE 只比较本次控制位，避免硬件自主更新 LOCK/PCAL 导致误报。
LOCK 单独由后续 timer 查询，预算1ms。
不会写 v1 TEST 字段，也不 reset 工作中的父源。

RCG 保留无关位，写目标 CFG 后单独触发 CMD_UPDATE，等待 UPDATE/dirty 清除，预算500µs；
对温态还要求 root 没有停止。只把这次明确控制的 AHB/MDP CFG 与 source 更新进原 lease
快照，不重新采样所有父源来掩盖 link 漂移。冷路径另启用两个核心分支，等待真实 CLK_OFF
清除与 root 运行，分支预算10ms。Ready 再整体复核两 PLL、两 root、两分支和 GDSC。

## 并发、故障与停止

Core 过渡期拒绝普通 DCS/亮度/面板电源和本地时钟操作；managed timer 不读暂时变化中的
pipeline/旧父源快照，由 core timer 独占推进。扫描快照和诊断同样避开过渡/故障状态。
IRQ 原先已经暂停；补充防止取消后晚到的 watchdog 在 IrqIdle 状态误报超时。
GDSC 状态失效时，pauseIRQ 只操作安全的 GIC，不再穿透读取掉电的 MDSS。

CN0 acquisition 期间保存自己的完成 token；core Ready 后原 NoC 模块以同 consumer 的
同值300MB/s请求接过 CN0 账本，此后使用 NoC 的新 token。没有第二个 apps-TCS writer。
ARC token 保留，core 每步检查其 Complete/Turbo；失去 provider 或票据失效立即失败。

超时、丢写、非法控制/频率、供电丢失和 provider 丢失会关闭写入，保留已确认票与部分硬件
效果，不自动撤电、reset、重试或恢复旧 TE。ERROR 记录故障阶段、PLL/root 与返回码；正常
停止记录 Aborted/Stopped，晚 timer 不访问寄存器。复用 clock writer 的 touched/retired
规则，防止写过硬件后重新创建无账本的 owner。

**DCS Display Off 与 stop 均不关闭 GDSC/PLL、不撤资源票。** 完整 host/PHY 停止、GDSC
collapse 与反向释放必须在后续完整 unprepare 中实现，不能仅凭当前核心 Ready 就撤父票。

## 来源、验证与剩余工作

LG 源 HEAD `d4f335e13d73c4aa2af85581a6ec11b670ee5b94`：
`sm8150-gdsc.dtsi:190`、`sm8150.dtsi:4097`（最终域节点有 GCC AHB、MMCX 和配置总线路径），
`gdsc-regulator.c:270–420`、`dispcc-sm8150.c:227–343 / 793–833 / 1636–1769`、
`clk-alpha-pll.c:827–983`、`clk-rcg2.c:114–140 / 450–485`。
不向当前 GDSC 添加其 DT 未声明的 reset/clamp/mailbox。

补丁0189严格应用到0188，无 offset/fuzz，7份变化源码字节一致；114条原 CI marker 保留，
CircleCI blob仍为 `da0b6c0947a66f03491b3a9023590b49665292df`。
真实 XNU/KDK arm64 MDSS 基线/最终对象通过；告警1398→1413，新增15条为固定有界数组
访问提示，已核对4/2项 root 表、12项 PLL 配置和0..3 branch cursor，不宣称零 warning。

10套最终检查通过：9套实际源码 ASan/UBSan（core、managed、NoC、clock、display、FB、
DSI、RPMh、perf）及 Python ADT。新测试覆盖实际扫描链、显式参数拒绝、运行 PLL 保留、
单/双 PLL 初始化、独立 PWR_ON/LOCK/UPDATE/CLK_OFF、无冷域读取、失败/停止/provider
退出、受控快照更新、DCS 保票、晚 watchdog 与对象归零。UBSan halt_on_error，无 sanitizer
报告；LSan 仍受 ptrace 限制关闭，另检查对象/映射/通知/分配计数。冷组件测试单独构造尚无
IRQ/DMA账本的 owner，不拿这个测试冒充现有 start() 的冷启动。

底稿 `scratch/framebuffer-0189/`；补丁 SHA256：
`ae2980e54f3bc97fb6421c8eda1e151c4348eff4b4c4de310fe2df6a3bd27043`。
没有新的云构建、boot、实机投票或画面证据。下一段继续 DSI host/PHY/PLL 的真实配置、
已确认 panel profile/reset/完整 ON/OFF，再补反向释放和独立重建。NoC 的冷态
solver/sleepwake/QoS/SBM/端口初始化仍缺，ABL/console/default surface 也尚未完全退出。
