# MH2LM 显示/面板驱动重构计划（大目标二）

**状态（2026-11-08）：计划已定，F0 未开始。** 本文是 `IRQ-FRAMEBUFFER-PLAN.md`
“大目标二：修复 framebuffer 渲染”的实施文档。agent 按 F0→F6 顺序做，每步
完成并通过验收后，在本文“进度记录”和 `NOTES.md` 写结果，再进入下一步。
不跳步，不合并两步的行为变更到同一默认路径。

## 1. 现状（以 0120a18 buildtree 源码与 0120a17 日志为准）

### 1.1 硬件事实

| 项 | 事实 | 证据 |
|---|---|---|
| 面板 | Tianma R66456A，1080×2340，60 Hz，RGB888，MIPI DSI **command mode**，4 lane，VC0 | `docs/panel.md` §1（LG PV 面板 DTS） |
| TE | 面板 TE 走 TLMM GPIO8，`te-pin-select=1`、`te-check-enable` | `docs/panel.md` 表 84 行 |
| 上下电 DCS | on：…`35 00`(TE on) → `29` → `11`+150 ms；off：`51 00 00`+90 ms → `28` → `10`+150 ms | `docs/panel.md` 139-156 行 |
| ABL 交接状态 | VIG0 源地址 0x9d200000、stride 0x10e0、尺寸 0x0924_0438（2340×1080）；INTF1 tear-check 使能，**autorefresh=0x80000001（每帧自动刷新）**；CTL0 flush/start=0；DSI0 ctrl=0x1f5（cmd 模式） | 0120a17 日志 `SWEET DISP: pass 0..2`（`scratch/artifact-0120a17/basic-charge1-persistent.txt`） |
| 结论 | MDP 在每个 TE 自动把 0x9d200000 的整帧经 DSI 发给面板，所以 CPU 写这块 RAM 就能上屏；没有任何“提交”动作，也没有帧完成通知 | 同上 |
| MDSS 中断 | `mdss_mdp@ae00000` `interrupts=<0 83 0>` → GIC SPI 83 = **INTID 115**【待核实】；INTF1 tear 中断块 MDP+0x6E800（EN+0/STATUS+4/CLEAR+8），bit0 AUTOREFRESH_DONE、bit2 RD_PTR、bit3 TE_DETECTED；顶层 HW_INTR_STATUS 在 MDP+0x10 | `android_kernel_lge_sm8150` `sm8150-sde.dtsi:44`，`sde_hw_interrupts.c:25,37-41,194-198` |
| 时钟门 | gcc disp_ahb 0x80000003、dispcc+0x2080=0x1：MDSS 有时钟，可访问 | 0120a17 日志 |

### 1.2 软件结构

```
CoreAnimation(backboardd) ──SwapEnd(0x46c 记录)──► AppleCLCD::swap()  仅保存“最新一份”记录
                                                       │ fDirty
 IOTimerEventSource 16.7ms (假 vsync) ──► vsyncFired ──┴─► 每 60/sweetfb_hz 次: present()
                                                            CPU memcpy 图层 → 0x9d200000
 RequestPowerChange(0) ──► 7 次 vsync 把扫描内存逐步变黑；(1) ──► 只重放最新一份 swap
 AppleARMBacklight(独立 workloop) ──► sweet_display_brightness_set() ──► DSI DCS 0x51
 sweet_display_probe.c：ml_io_map 直映射 GCC/DISPCC/MDP/DSI，一个全局 mutex
```

源码：`xnu/iokit/Kernel/arm/SweetFramebuffer.cpp`（763 行）、`SweetSurface.cpp/.h`、
`xnu/bsd/dev/sweet_display_probe.c`（347 行），相关补丁 0046/0056/0061/0068/0079/
0108–0111/0127。

### 1.3 已知问题与原因

| # | 问题 | 原因（源码可证） |
|---|---|---|
| P1 | 息屏后插拔充电器唤醒，状态栏/底部横条在、中间时钟区域黑 | off 时把扫描内存**擦黑**；on 只重放最新 swap，若该帧内容不完整就无法恢复。已知解释与现象吻合，未用帧内容抓取证明（0127 的 `sweetfbpreserveoff=1` A/B 从未实测） |
| P2 | 没有真实 vsync | 60 Hz 定时器；0120a17 两份日志里 `sel 72`（注册通知）**一次都没出现**，即 vsync 通知可能根本没有接收方，CA 的节拍来源未知 |
| P3 | 撕裂/读写竞争 | present 在任意时刻 memcpy 进正在被 autorefresh 扫描的内存；present 时才按 id 查 surface，CA 可能已在往同一 surface 画下一帧（没有 swap 完成/释放语义，`SwapWait` 为空操作） |
| P4 | 显示电源不是真的 | “关屏”=擦像素+CoreBrightness 调最低亮度，面板和 MDP 一直在跑 |
| P5 | 不符合 IOKit 常规 | 硬件寄存器在 BSD 文件里 `ml_io_map` 直映射；外部方法用 switch、不检查参数个数；用户态调用和定时器之间用 IOLock 混合同步；无 IOPM 电源状态；文件头注释仍写 1080×2400 |

0120a17 日志另一事实：CA 每次 swap 只用 **layer 0**，全屏 1125×2436，surface 2/3
交替（双缓冲）。所以“内核按四图层重组”不是当前需求，不要做。

## 2. 目标架构（XNU/IOKit 常规做法）

```
ADT /arm-io/disp0 (display-subsystem, canvas 1125x2436)
 └─ AppleCLCD : IOMobileFramebuffer          帧语义/用户态 ABI/电源策略（与硬件无关）
     ├─ IOWorkLoop + IOCommandGate            所有状态只在本 workloop 上改
     ├─ IOInterruptEventSource(无 provider)   接 SweetMDSS 转来的 RD_PTR/完成事件
     ├─ IOTimerEventSource                    只做超时/看门狗，不再当 vsync
     ├─ IOMobileFramebufferUserClient         IOExternalMethodDispatch 表，gate->runAction
     └─ SweetMDSS* (成员，非全局)              硬件后端接口（纯虚类，见下）
ADT /arm-io/mdss (新增节点，reg: MDP/DSI0/DISPCC/GCC-disp 页，interrupts: 115)
 └─ SweetMDSS : IOService                     硬件：时钟门检查、INTF1 tear/autorefresh、
     ├─ mapDeviceMemoryWithIndex()             CTL、DSI 命令引擎(DCS)、中断使能/清除
     └─ GIC 上半部：屏蔽+记账，交给 AppleCLCD 的 IOInterruptEventSource
 AppleARMBacklight : IOService                保留；亮度改为调用 SweetMDSS::dcsWrite
```

原则：

1. **分层**：`AppleCLCD` 不碰寄存器；`SweetMDSS` 不懂 swap/surface。两者之间是一个
   小的 C++ 接口（`SweetDisplayBackend`：`presentBegin/End`、`setPanelPower`、
   `dcsWrite`、`enableVsync`、`frameCounter`），便于 host 测试用假后端替换，将来 GPU
   后端也只换这一层。
2. **单线程状态**：用户客户端、IRQ 下半部、定时器都在 `AppleCLCD` 的 workloop 上执行
   （`IOCommandGate::runAction`），不再用 `fLock` 在多上下文间共享 `fPending`。
3. **MMIO 归 IOService**：寄存器窗口由 ADT `reg` 描述，`IOService::mapDeviceMemoryWithIndex`
   映射，`stop()` 释放；不在 BSD 层 `ml_io_map`。
4. **中断**：本移植没有 GIC 的 `IOInterruptController`，沿用项目已验证的模式（mux/USB/
   触摸）：`sleh` 分派到 `sweet_mdss_irq(intid)`，上半部只屏蔽 MDSS 源、记时间戳、
   `interruptOccurred()`；清除/重新使能在 workloop。上半部不取锁、不做 SPMI/DSI。
5. **纯状态机放头文件**：swap 队列、电源状态机、vsync 节拍判定写成无 IOKit 依赖的
   `sweet_fb_state.h`（参照 `sweet_charge_basic.h`），用 `tools/tests/` 下 host 测试覆盖。
6. **IOPM**：`AppleCLCD` `PMinit` + `registerPowerDriver`（off/on 两态），`setPowerState`
   在 workloop 执行；`RequestPowerChange` 改为请求该状态机，系统重启/关机走同一路径。
7. **新行为全部 opt-in**：每步新增 boot-arg（下表），默认仍走旧路径，直到该步验收通过
   再改默认值；旧路径删除放在 F6。

| boot-arg | 引入步骤 | 含义 |
|---|---|---|
| `sweetfbtrace=1` | F0 | 显示诊断日志（选择子、swap、off/on、帧校验） |
| `sweetmdss=1` | F1 | 使用 SweetMDSS IOService 后端（行为与旧路径相同） |
| `sweetfbpower=1` | F2 | DCS 真关屏，不擦像素 |
| `sweetfbvsync=1` | F3 | MDSS RD_PTR 中断做 vsync |
| `sweetfbpresent=1` | F4 | vsync 驱动的一致呈现与 swap 完成语义 |
| `sweetfbkickoff=1` | F5 | 关 autorefresh，按帧 kickoff（可选） |

## 3. 分步实施与验收

每步通用要求：本地交叉编译（`scratch/*/compile.py` 方式）通过、告警不新增类别；
`tools/tests` 全部通过（新增状态机需有对应 host 测试）；patch 对上一结果树 strict
apply；CI 加 `require_marker`；打包脚本加对应 profile；**只 `fastboot boot`**；
回归三键/触摸/USB ACM/电池充电不退化。每步一个补丁编号，日志前缀 `SWEET FB`/
`SWEET MDSS`，用 `sweet_log_level.h` 宏。

### F0：取证，不改行为

**做什么**
- `sweetfbtrace=1` 时：记录每个 IOMFB 选择子的进程名、标量/结构体大小和**前 4 个标量值**
  （每个选择子前 32 次，之后计数）；记录 `RequestPowerChange` 前后最近 4 次 swap 的
  surface id、mask、rect；off/on 时对扫描内存和来源 surface 的中间区域（行 900–1400）
  算轻量校验和（每 8 行 1 行、每行 64 点采样）并打印。
- 在打包脚本生成同一内核的两档：`fb-trace`、`fb-trace-preserveoff`（加 `sweetfbpreserveoff=1`，
  即完成 0127 一直没做的 A/B）。
- 只读记录 INTF1 帧计数/行计数（+0x2b0）在 1 秒内的变化，得到实际 TE 频率。

**验收**
1. 两档各复现：电源键息屏→等 3 秒→插充电器→亮屏→拔；auto-lock 息屏→插拔；各 5 次。
2. 记录每次中间区域是否黑、对应扫描内存与来源 surface 的校验和，能回答：黑是来源帧本身
   就黑（CA 侧）还是内核擦黑后没重画（内核侧）。
3. 得到完整的选择子使用表（尤其 72/79/68/57/6/50 的调用者、频率和参数），写入本文 §5。
4. 测得 TE/autorefresh 实际频率（预期约 60 Hz）。
5. 结论与原始日志 SHA 写入本文进度记录；若 preserveoff 已消除 P1，记录为 F2 的佐证，
   但仍按计划做 F2（擦像素+最低亮度不是真关屏）。

### F1：分层重构为 IOService，行为不变

**做什么**
- ADT（`tools/mkadt.py`）新增 `/arm-io/mdss`：`reg` 列 MDP(0xae00000,0x84208 内用到的页)、
  DSI0(0xae94000)、DISPCC(0xaf00000)、GCC disp 页(0x10b000)；`interrupts` 记 115；
  `compatible "sweet,mdss"`。
- 新 `SweetMDSS : IOService`（`xnu/iokit/Kernel/arm/SweetMDSS.cpp`），匹配该 nub，
  `mapDeviceMemoryWithIndex` 映射；把 `sweet_display_probe.c` 的探针与 DCS 写迁入
  （寄存器序列逐字保留：tear/autorefresh 暂停、FIFO、恢复）；公开 `SweetDisplayBackend`。
- `AppleCLCD` 通过 IOKit 匹配通知拿到 `SweetMDSS`（不用全局指针）；用户客户端改为
  `IOExternalMethodDispatch` 表（检查标量/结构体个数），经 `IOCommandGate` 进入 workloop；
  未知选择子保持“记录一次+返回成功”的兼容行为。
- `AppleARMBacklight` 改调 `SweetMDSS::dcsWrite`；修正文件头 1080×2400 等过时注释。
- `sweetmdss=0` 保留旧 BSD 实现一个版本，便于 A/B。

**验收**
1. `sweetmdss=1` 下探针打印与 0120a17 的 `SWEET DISP` 寄存器值逐项一致。
2. 亮度：CoreBrightness 调节 0→最大→最小，DCS rc 全 0，与旧路径肉眼一致。
3. UI、锁屏、息屏/亮屏表现与 `sweetmdss=0` 相同（P1 仍可能存在，属预期）。
4. 显示代码中不再有 `ml_io_map`；`ioreg` 可见 `SweetMDSS` 与 `AppleCLCD` 各自的属性。
5. 新增 host 测试：用户客户端分发表参数校验、gate 序列化（假后端）。

### F2：真正的显示电源，修复 P1

**做什么**
- `sweet_fb_state.h` 电源状态机：`ON → OFF_PENDING → OFF → ON_PENDING → ON`，含失败回退。
- off：停止呈现 → DCS `28`（Display Off）→（可选，第二阶段）`10` Sleep In + 150 ms；
  **扫描内存保持不动**，不再逐步擦黑。on：（若睡眠）`11` + 150 ms → `29` → 立即用当前
  扫描内存继续显示，同时接受新 swap。DCS 顺序与等待取自面板 DTS（§1.1）。
- 背光与显示电源解耦：on 时由 CoreBrightness 自己恢复亮度，驱动不额外写 0x51。
- DCS 失败：记录 rc，状态机回到 ON 并保持旧行为（最低亮度），不卡死用户态调用。
- 第一阶段只用 `28/29`（面板仍在 sleep-out），通过后再评估 `10/11`。

**验收**
1. 20 次循环（电源键、auto-lock、息屏插拔充电器、息屏来通知唤醒各 5 次）全部完整显示，
   中间区域无黑块；F0 的校验和显示 on 后扫描内存与 off 前一致。
2. 息屏时面板肉眼全黑（不是最低亮度），DCS rc 全 0。
3. 唤醒后亮度恢复到息屏前水平；触摸立即可用。
4. 持久日志有每次 `SWEET FB POWER: off/on rc=0 elapsed-ms=`；30 分钟息屏充电后唤醒正常。
5. host 测试覆盖状态机：重复请求、off 中再 off、DCS 失败回退。

### F3：硬件 vsync（MDSS RD_PTR 中断），修复 P2

**做什么**
- 只读预检：GIC INTID 115 的 group/enable/config、MDSS 顶层 HW_INTR_STATUS、
  INTF1 tear 中断 EN/STATUS 当前值；确认无其他 EE 依赖（参照触摸 IRQ 预检）。
- `sleh` 分派链加入 `sweet_mdss_irq()`；上半部：读顶层状态，屏蔽 INTF1 tear EN，
  记 `mach_absolute_time`，`interruptOccurred()`；workloop：清 STATUS（W1C）、
  计帧、发 vsync 通知、重新使能。只开 RD_PTR（bit2）。
- 取消 60 Hz 定时器作为 vsync 来源；定时器只做“N ms 没有 RD_PTR”看门狗：报错并退回
  定时器 vsync（显式日志，不静默）。
- 显示 OFF 时关中断；ON 时开。
- vsync 通知内容按 F0 查到的实际选择子/格式发送（若 CA 不用 72 注册，记录其实际节拍
  机制，再决定是否需要别的通知类型）。

**验收**
1. 亮屏 60 秒内 RD_PTR 计数 60±1 Hz，间隔抖动记录（p50/p99）。
2. 息屏期间 MDSS 中断计数不增长；亮屏后恢复。
3. 无中断风暴（单秒 >200 次即判失败并自动禁用该中断）；看门狗未触发。
4. 三键/触摸/USB/电池 IRQ 计数与 F2 基线同量级，功能正常。
5. host 测试：上半部/下半部记账、看门狗回退、off 时禁用。

### F4：一致、无撕裂的呈现与 swap 完成语义，修复 P3

**做什么**
- SwapEnd 时校验记录并**立即 retain** 涉及的 SweetSurface，生成 `SwapRecord`；
  队列深度 1，新 swap 替换未呈现的旧 swap 时，旧 swap 立即按“已完成”释放。
- 呈现在 RD_PTR 下半部触发：只有在有新 swap 时复制；复制完成后释放上一帧 surface，
  发 swap 完成通知（类型与格式按 F0 结果）；实现 `SwapWait`（在 gate 上等待指定 swap
  完成，带超时）。
- 复制时机：RD_PTR 后立即开始，保证 1125→1080 裁剪后的复制在下一次读指针前完成；
  测量复制耗时（预计 ~11 MB），超过帧时间则记录并降为隔帧呈现（明确日志，不静默）。
- 若 rect 信息表明只有局部更新，可只复制脏区（先记录 CA 是否提供，F0 数据决定）。
- `sweetfb_hz` 废弃（保留解析一版并告警）。

**验收**
1. 滚动列表/拖动图标 1 分钟，肉眼无撕裂线；记录每帧复制耗时 p50/p99。
2. 30 分钟压力（反复开关应用、旋转锁屏、通知）无 panic、surface 引用计数回零
   （`ioreg`/日志统计创建与释放数一致）。
3. CPU 占用不高于旧路径 `sweetfb_hz=30`（用 `top` 或内核计数估算）。
4. 若 CA 调 `SwapWait`，其返回时间与 RD_PTR 对齐；无死锁（超时计数为 0）。
5. host 测试：队列替换释放、完成通知顺序、SwapWait 超时、surface 生命周期。

### F5（可选）：按帧 kickoff，替代 autorefresh

仅在 F4 后仍有撕裂或功耗问题时做。参照 Linux `sde_encoder_phys_cmd`/`sde_hw_ctl`：
关 INTF1 autorefresh，复制完成后写 CTL0 FLUSH/START 提交一帧，用 PINGPONG/CTL done
中断确认完成，再发 swap 完成。失败时恢复 autorefresh 并记录。

**验收**：无新内容时 MDP 不再每帧发送（DSI 活动计数下降）；60 秒动画无撕裂、无丢帧
统计异常；任意失败可自动恢复 autorefresh 且画面不冻结。

### F6：生命周期、IOPM 与清理

**做什么**
- `AppleCLCD` 接入 IOPM（两态），系统重启/关机前显示 off（DCS 28/10），让下一阶段
  固件接手的面板处于已知状态；`willTerminate/stop` 释放中断、映射、surface。
- panic/控制台路径：panic 时必须仍能在扫描内存上画字——确认显示 OFF 时 panic 会先
  `29`/恢复亮度，或至少保留旧行为；写明边界。
- 默认值切换为新路径，删除旧 BSD 探针/定时器 vsync/擦像素代码；更新
  `CURRENT-DRIVER-ARCHITECTURE.md` 画面一节。

**验收**
1. 重启、关机、从 iOS 进 recovery 各 3 次，面板无残影/花屏，下次开机正常。
2. 默认镜像（不带任何 `sweetfb*` 新参数）通过 F2–F4 全部验收项的回归子集。
3. 一次人为触发的测试 panic（仅诊断档）能在屏幕上看到 panic 文本。
4. 代码中无旧路径残留；文档更新；`NOTES.md` 记录最终基线 SHA。

## 4. 边界

- 不移植 UniXNU/HakiOS 的整套显示栈；可以参考它们的选择子含义和寄存器序列，并注明出处。
- 不做 GPU 合成/零拷贝扫描（需要 SMMU、连续内存与 GPU fence 设计，另立项目，见
  `GPU-ADRENO640.md`）；本计划的 `SweetDisplayBackend` 接口为其留位置。
- 不在内核里重组多图层；layer 0 以外出现时先记录（F0），不臆测处理。
- 不把电池/充电 UI 问题当显示问题修。
- 所有设备操作 `fastboot boot`；写盘、刷分区不在本计划内。

## 5. F0 取证结果（待填）

- 选择子使用表：
- 黑块根因：
- TE/autorefresh 实测频率：

## 6. 进度记录

| 步骤 | 补丁 | 产物/日志 | 结论 |
|---|---|---|---|
| F0 | — | — | 未开始 |
