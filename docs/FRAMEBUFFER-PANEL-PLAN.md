# MH2LM 显示/面板驱动重构计划（大目标二）

**状态（2026-11-09，第三版顺序调整）：按用户决定暂缓插电后锁屏缺时间（BUG-003），结束本轮F0专项排查，进入F1 IOKit分层重构；F0保留未闭环项，不伪记全部验收。** 本文是 `IRQ-FRAMEBUFFER-PLAN.md`
“大目标二”的实施文档。第二版目标从“修好接手的 ABL framebuffer”提升为：**驱动自己
分配和管理扫描缓冲（simplefb 式），硬件双缓冲翻页，CoreAnimation 渲染的 surface
直接被 MDP 扫描，消除每帧 CPU 拷贝。**

通常按F0→F7顺序做；用户2026-11-09授权的例外是将BUG-003暂缓，使用已取得的硬件/接口
证据进入F1。F0的正常iPhone UI、模式ABI因果及半透明根因未闭环，不能标记修复或全部验收。
后续仍逐步验收、记录进度；每步新行为由boot-arg打开，默认路径在该步验收前不变。

**当前进度补充（0121a6）：** F1a资源/背光迁移的正常运行路径功能验收通过，用户确认亮度
跟随及息屏唤醒正常，日志确认新服务/绑定/DCS/3轮off-on及输入工作；进入F1b，F1整体未完成。
真实服务终止路径及精确亮度端点未上机核验，范围见[F1a实测](FRAMEBUFFER-SERVICE-F1A.md#0121a6-实测f1a正常运行路径功能验收通过)。
BUG-004在0121a5两档同链复现，独立跟踪；0121a6未复现不视为修复，不安排重复充电A/B。

0121 结论与证据见 [F0 实测报告](FRAMEBUFFER-F0-0121.md)：普通档电池/按键 group 先后被 mux
隔离，解释两条唤醒来源失效；该独立问题转入 [BUG-002](BUGS.md)，不自动增加显示步骤或验收前置。
preserveoff 实际保留像素，但未解决全部缺时间现象。
0170 不能核验时钟区域像素或 TE 频率；SMMU stream 配置为 TRANS/CB2，F4 前须补转换核对。
0121a1 的 include 路径错误由0172修复；0121a2两档实测见 [新报告](FRAMEBUFFER-F0-0121A2.md)。
用户确认两档异常相同；时钟区域当前source/扫描同黑，不能仅归因于擦除/漏拷。模式查询、
update/disable/background已由原始框架核对；原版内核的等待参数与状态设计见
[帧完成契约](FRAMEBUFFER-SWAP-CONTRACT.md)。F0仍未验收，不提前进入F1。

## 1. 现状（0120a18 buildtree 源码 + 0120a17 日志）

### 1.1 硬件事实

| 项 | 事实 | 证据 |
|---|---|---|
| 面板 | Tianma R66456A，1080×2340，60 Hz，RGB888，MIPI DSI **command mode**，4 lane，VC0 | `docs/panel.md` §1 |
| TE / 上下电 DCS | TE 走 GPIO8；on …`35 00`→`29`→`11`+150 ms；off `51 00 00`+90 ms→`28`→`10`+150 ms | `docs/panel.md` 84、139-156 行 |
| ABL 交接 | VIG0 SSPP：`SRC_SIZE`(+0x00)=0x0924_0438、`SRC0_ADDR`(+0x14)=0x9d200000、`YSTRIDE0`(+0x24)=0x10e0；INTF1 tear-check 开、**autorefresh=0x80000001**；CTL0 FLUSH/START=0；DSI0 cmd 模式 | 0120a17 日志 `SWEET DISP: pass 0..2` |
| 含义 | MDP 每个 TE 自动从 SSPP 源地址取整帧发给面板。**改 `SRC0_ADDR` 并 flush 即可换扫描缓冲**，这是硬件翻页和零拷贝的基础 | 同上；`sde_hw_sspp.c:25-37` |
| ABL 缓冲 | 当前 0120a17 运行探针看到 VIG0 `SRC0_ADDR=0x9d200000`，计划暂按其后 0x1200000（18 MiB）处理；Mu-Silicium 标 `Display Reserved`、WRITE_THROUGH | `MemoryMapLib.c:25`；0120a17 `SWEET DISP` 日志 |
| SSPP 寄存器 | `SRC_SIZE`0x00、`SRC_XY`0x08（源裁剪起点）、`OUT_SIZE`0x0c、`SRC0_ADDR`0x14、`YSTRIDE0`0x24、`SRC_FORMAT`0x30、`UNPACK`0x34、`OP_MODE`0x38、`ADDR_SW_STATUS`0x70 | `sde_hw_sspp.c:25-82` |
| CTL | `FLUSH`0x018、`START`0x01c，VIG0 的 flush 位 BIT(0) | `sde_hw_ctl.c:29-30,167` |
| MDSS 中断 | `interrupts=<0 83 0>` → INTID 115【待核实】；INTF1 tear 中断块 MDP+0x6E800（EN/STATUS/CLEAR=+0/+4/+8），bit2 RD_PTR、bit0 AUTOREFRESH_DONE；顶层 HW_INTR_STATUS=MDP+0x10 | `sm8150-sde.dtsi:44`；`sde_hw_interrupts.c:25,37-41,194-198` |
| SMMU | MDSS SID=0x800、匹配 mask=0x420。0121a2 SMR3=0x84200800、S2CR=0x2（TRANS/CB2），但 CB2 SCTLR=0xe0/M0、TTBR/TTBCR/MAIR0，本次 stage1 转换未使能；新物理页可达性仍需核实 | `sm8150-sde.dtsi:47`；0121a2 报告 §6 |

**地址范围待核对。** `docs/panel.md` 还记录 Android DTS 的 `splash_region` 为
`0x9c000000/0x02400000`（36 MiB），而运行时 VIG0 使用的是 `0x9d200000`。
这可能是同一预留区内的不同交接子区，也可能是 ABL/ADT 版本差异；在 F0 之前不能
把两者合并、扩大或拿来分配扫描缓冲。F0 必须同时记录 `SRC0_ADDR`、stride、内存
内容校验和以及实际映射边界，确认后再决定 F4 的分配地址。

### 1.2 软件现状与问题

```
CA(backboardd) 画进 IOSurface 2/3（普通可缓存内存，双缓冲）
   └─SwapEnd──► AppleCLCD 只记“最新一份”记录
 60Hz 定时器(假vsync) ──► 每 60/sweetfb_hz 次 CPU memcpy 约 11 MB → 0x9d200000（ABL 缓冲）
 off：7 次 vsync 把 0x9d200000 擦黑；on：重放最新 swap
 AppleARMBacklight ──► sweet_display_probe.c 的 DCS 0x51（BSD 层 ml_io_map 直映射）
```

| # | 问题 | 原因 |
|---|---|---|
| P1 | 插线唤醒/取消关机后缺时间、关机滑条区域半透明效果异常 | off 擦除与无有效 surface 的记录重放是已知缺口；0121 preserveoff 未消除全部现象，具体像素/合成根因待查 |
| P2 | 假 vsync | 60 Hz 定时器；两份日志里 `sel 72` 从未出现 |
| P3 | 撕裂/竞争 | 往正在被 autorefresh 扫描的内存里 memcpy；present 时才查 surface，CA 可能已在画下一帧；`SwapWait` 空操作 |
| P4 | CPU 拷贝开销 | 每次呈现约 11 MB memcpy 进写合并内存，所以限到 30 Hz |
| P5 | 依赖 ABL | 扫描缓冲、格式、SSPP 配置全部继承自 ABL，驱动不拥有 |
| P6 | 显示电源假 | “关屏”=擦像素+最低亮度 |
| P7 | 不符合 IOKit 常规 | BSD 层直映射寄存器；switch 分发不校验参数；IOLock 跨上下文共享状态；无 IOPM |

0120a17 日志：CA 每次 swap 只用 **layer 0**，全屏 1125×2436，surface 2/3 交替，
`bpr 4608`（=1152×4，已 64 字节对齐）。所以单图层直扫是可行目标，内核多图层重组不需要。

## 2. 目标架构

```
ADT /arm-io/disp0 (display-subsystem, canvas 1125x2436)
 └─ AppleCLCD : IOMobileFramebuffer               帧语义 / 用户态 ABI / 电源策略
     ├─ IOWorkLoop + IOCommandGate                 唯一状态所有者
     ├─ IOInterruptEventSource                     接 MDSS vsync/翻页完成
     ├─ SweetScanoutPool                           驱动自有扫描缓冲（≥2 个）
     ├─ IOMobileFramebufferUserClient              IOExternalMethodDispatch 表
     └─ SweetDisplayBackend*  ──────────────┐      纯虚接口：flip/power/dcs/vsync
ADT /arm-io/mdss (reg: MDP/DSI0/DISPCC/GCC 页; interrupts: 115)
 └─ SweetMDSS : IOService  ◄────────────────┘      硬件：SSPP/CTL/INTF/DSI/中断
 AppleARMBacklight : IOService                     亮度经 SweetMDSS::dcsWrite
```

### 2.1 扫描缓冲：自己的 simplefb

- 驱动启动时分配**物理连续、64 字节对齐行距**的扫描缓冲：用
  `IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIOMemoryPhysicallyContiguous |
  kIOMapWriteCombineCache | kIODirectionInOut, size, mask)`（mask 限 32 位以内物理地址，
  仅限制物理位置，不能替代 SMMU 映射）。0121 已读到 stream 为 TRANS/CB2；在 F4 写扫描地址前
  必须核实 CB2 的有效转换配置和页表可访问范围，必要时先完成独立的 F4a 映射子步骤。
- 尺寸按**逻辑画布** 1125×2436 分配（行距 4608，与 CA 的 surface 一致，约 10.7 MiB/个），
  面板只显示中间 1080×2340：通过 SSPP `SRC_XY=(22,48)`、`SRC_SIZE/OUT_SIZE=1080×2340`
  做**硬件裁剪**，不再 CPU 裁剪拷贝。（22=(1125−1080)/2，48=(2436−2340)/2，与现
  `fOffX/fOffY` 相同。）
- 格式固定 BGRA8888（CA 的 `fmt 0x42475241`）；`SRC_FORMAT/UNPACK` 由驱动写入而不是继承。
- 交接：首帧把 ABL 缓冲当前内容拷贝到自有缓冲（一次性），翻页到自有缓冲后不再写 ABL
  区域；ABL 区域保留供 panic/早期控制台使用（见 F7）。
- 文本控制台（`PE_Video`）在翻页后通过 `kPEBaseAddressChange` 指向当前前台缓冲，
  或保持旧地址并在 F7 定义 panic 时的切换规则。

### 2.2 双缓冲与零拷贝的三个层级

| 层级 | 做法 | 每帧 CPU 拷贝 | 步骤 |
|---|---|---|---|
| L1 驱动双缓冲 | 两个自有缓冲 A/B：CA surface → 拷到后台缓冲 → vsync 翻页 | 1 次，但不再撕裂、无需限频 | F4 |
| L2 直接扫描 surface | CA 用于 layer 0 的 surface 本身由驱动分配为扫描兼容内存，swap 时直接把 `SRC0_ADDR` 指向该 surface | **0** | F5 |
| L3 回退 | 遇到不兼容 surface（格式/尺寸/对齐/非连续），退回 L1 拷贝并计数 | 1 | F5 |

L2 的关键：IOSurface 创建时识别“可能被 IOMFB 扫描”的 surface（尺寸=画布、格式 BGRA、
来自 backboardd/CA；或 CA 设置的属性如 `IOSurfaceIsDisplayable`/`kIOSurfaceAllocSize`
等——**具体以 F0 记录的 create 属性字典为准**），从 `SweetScanoutPool` 给它分配物理
连续内存。

### 2.3 缓存策略（减少拷贝之外的代价）

- 当前自建 surface 用 `kIOMapDefaultCache`（可缓存），扫描缓冲用写合并。L2 直扫时，
  CPU 写入的数据可能还在 D-cache，MDP 读到旧数据。两种可选：
  - **方案 A（首选）**：可扫描 surface 保持**可缓存**（CPU 软件渲染快，读改写友好），
    在 swap 提交时、翻页前对该 surface 做 `clean_dcache64(phys, size, TRUE)`（只清理不失效），
    10.7 MiB 的 clean 远小于一次 memcpy。
  - **方案 B**：可扫描 surface 用写合并映射给用户态，免 clean，但 CPU 读/混合会很慢。
  - F5 先实现 A，测量 clean 耗时与 CA 渲染耗时；必要时对比 B，结论写入 §6。
- 只在 surface 的 seed（Unlock 写入计数）变化时 clean，避免重复。

### 2.4 帧生命周期（swap 语义）

```
SwapEnd(rec) ── 校验/retain surface ──► pending
RD_PTR(vsync) ── pending 存在 ──► clean cache ─► 写 SRC0_ADDR(+XY) ─► CTL FLUSH
下一次 RD_PTR ── 读 ADDR_SW_STATUS/确认新地址生效 ──► 旧前台 surface 释放 + swap 完成通知
```

- 一个 surface 在被扫描期间不得交还给 CA 继续绘制：必须核对并实现本版用户态实际使用的
  完成/复用协议；**retain 只保对象存活，不阻止 CA 改写像素**（0121a2 已捕获复用证据）。
  翻页确认后才释放上一帧，按不同options更新查询条件；`SwapWait`的非阻塞请求立即查询，
  阻塞请求用有界`commandSleep`释放gate等待，不能堵住完成事件。
- 队列深度1只允许替换尚未进入reading/硬件提交的queued：记录取消/替换原因，并在确认
  无读者后归还源。不能把reading、displayCurrent和被丢弃的queued统一算“完成”。
  当前显示ID、pending集合、源读取权和配置ready分别维护；ID0的意义由options决定。
  options0/1/2/3/4及副作用边界以[原版内核核对与设计](FRAMEBUFFER-SWAP-CONTRACT.md)为准。

### 2.5 IOKit 原则

1. `AppleCLCD` 不碰寄存器；`SweetMDSS` 不懂 swap/surface；两者间是 `SweetDisplayBackend`
   （`setScanout(phys,stride,xy,size,fmt)`、`flush()`、`scanoutLatched()`、`setPanelPower()`、
   `dcsWrite()`、`setVsync(bool)`），host 测试用假后端。
2. 所有状态只在 `AppleCLCD` workloop 上改（`IOCommandGate::runAction`）。
3. MMIO 由 ADT `reg` 描述、`mapDeviceMemoryWithIndex` 映射；不在 BSD 层直映射。
4. 中断沿用项目验证过的模式：`sleh` 分派 `sweet_mdss_irq(intid)`，上半部只屏蔽源、记时、
   `interruptOccurred()`；清除/再使能在 workloop。
5. 纯逻辑（swap 队列、翻页状态机、电源状态机、扫描兼容性判定）放 `sweet_fb_state.h`，
   host 测试覆盖。
6. IOPM 两态（off/on），`RequestPowerChange` 和关机/重启走同一状态机。

### 2.6 boot-arg

| boot-arg | 步骤 | 含义 |
|---|---|---|
| `sweetfbtrace=1/2` | F0 | v2 显示诊断；2 额外读取已知布局 CB2 配置 |
| `sweetmdss=1` | F1 | 使用 SweetMDSS IOService 后端（行为不变） |
| `sweetfbpower=1` | F2 | DCS 真关屏，不擦像素 |
| `sweetfbvsync=1` | F3 | MDSS RD_PTR 中断做 vsync |
| `sweetfbown=1` | F4 | 自有扫描缓冲 + 硬件裁剪 + 双缓冲翻页（L1） |
| `sweetfbdirect=1` | F5 | surface 直接扫描（L2，含 L3 回退） |
| `sweetfbkickoff=1` | F6 | 关 autorefresh，按帧 kickoff（可选） |

## 3. 分步实施与验收

**每步通用要求**：本地交叉编译通过且告警类别不新增；`tools/tests` 全过（新状态机配
host 测试）；patch 对上一结果树 strict apply；CI 加 `require_marker`；打包脚本加 profile；
只 `fastboot boot`；回归三键/触摸/USB ACM/电池充电不退化；一步一个补丁（从 0170 起）；
日志用 `sweet_log_level.h`，前缀 `SWEET FB`/`SWEET MDSS`。

### F0：取证，不改行为

**做什么**
- 0171 / `sweetfbtrace=2`：selector 0–127 各前 32 次参数，最近 4 次提交按时间排序，
  增加 update/disable 和 +0x10c/+0x110 原始字段；另记实际完成 CPU 拷贝的 surface、swap、裁剪和时间。
- 息屏/唤醒以及亮屏每秒（上限 600 次）读取 BGRA，覆盖上/中/下、时钟中心和底部横条 ROI；
  按相同目的像素对齐源图和 panel，排除 padding、验证映射边界。明确是实时稀疏样本，不能当 CA fence。
- IOSurface create 前 32 次记录最多各 64 个属性键及关键值；保留创建者/尺寸等日志，为 F5 提供依据。
- 只读：SSPP 格式/地址、INTF1 真正 frame(+0xac)、计数使能及实际间隔/行相位；
  不把循环行差当 TE 频率。SMMU 记录 SMR/S2CR，已知 ID/TRANS/CB2 时增加只读配置；
  CB2 PA 经原机 DT 与 LG 驱动核对为 0x15082000。地址转换可用范围仍待实机证据。
- 打包两档：`fb-trace`、`fb-trace-preserveoff`。

**验收**
1. 两档各做：电源键息屏→3 秒→插充电器；接充电器时电源键息屏→3 秒→拔；各 5 次。
   当前未实现自动息屏，不安排 auto-lock；增加关机界面及取消后的显示观察。
2. 区分当前 source 同黑、擦除后未重画和提交/完成/复用语义。source 同黑不直接等于 CA 自身错误；
   需结合本版 ABI 与正常充电过渡 UI 核验，关机滑条区域半透明效果异常独立闭环。
3. 得到选择子使用表、CA surface 创建属性表，写入 §6。
4. 得到 SSPP 格式寄存器值、计数使能/实际间隔和 SMMU route/CB2 有效配置，写入 §6。
   command mode 计数若不能表征 TE，明确保留未知，TE 实测在 F3 的 IRQ 时间戳中完成；
   不用无效行差填充频率结论。F4 前仍必须明确新扫描地址的映射可行性。
5. 原始日志 SHA 记入 §7。

### F1：IOKit 分层重构，行为不变（当前）

0121a4为当前代码/画面基线。BUG-003和半透明颜色异常可保留，只验收是否引入新显示/亮度/
输入/充电回归。停止为缺时间增加取证轮次；模式元数据修复暂不混入F1。

**实施分段**：F1a先迁移硬件资源/探针/DCS与背光consumer（[0175实现与验证](FRAMEBUFFER-SERVICE-F1A.md)，0121a6正常运行路径功能验收通过）；当前下一步F1b迁移
AppleCLCD后端绑定、用户客户端分发表/参数校验和状态gate。两段分别验收，F1a通过不等于整个F1完成。

**做什么**
- ADT 新增 `/arm-io/mdss`（`compatible "sweet,mdss"`，`reg` 列 MDP、DSI0、DISPCC、GCC disp 页，
  `sweet,mdss-intid` 115仅记录候选；标准中断属性在F3核对后接入）。
- 新 `SweetMDSS : IOService`：`mapDeviceMemoryWithIndex`；迁入探针与 DCS 写（寄存器序列逐字保留）；
  实现 `SweetDisplayBackend`。
- `AppleCLCD` 经匹配通知取得后端（无全局指针）；用户客户端改 `IOExternalMethodDispatch`，
  经 `IOCommandGate` 进 workloop；未知选择子保持“记录一次+成功”。
- `AppleARMBacklight` 改调 `SweetMDSS::dcsWrite`；修正 1080×2400 等过时注释。
- `sweetmdss=0` 保留旧实现一个版本。

**验收**
1. `sweetmdss=1` 探针输出与 0120a17 `SWEET DISP` 逐项一致。
2. 亮度 0→最大→最小，DCS rc 全 0。
3. UI/锁屏/息屏表现与 `sweetmdss=0` 相同。
4. 新服务路径无 `ml_io_map`，旧BSD路径仅sweetmdss=0使用；`ioreg` 可见 `SweetMDSS`、`AppleCLCD`。
5. host 测试：分发表参数校验、gate 序列化。

### F2：真显示电源（修 P1/P6）

**做什么**
- 电源状态机 `ON→OFF_PENDING→OFF→ON_PENDING→ON`，失败回退到旧行为。
- off：停呈现→DCS `28`；**不擦扫描内存**。on：`29`→继续显示当前缓冲。第一阶段只用
  `28/29`，通过后再评估 `10/11`（+150 ms）。
- 背光与电源解耦，亮度由 CoreBrightness 恢复。

**验收**
1. 电源键息屏/亮屏、息屏插电、息屏拔电各 5 次，以及关机界面/取消返回，全部完整显示。
   auto-lock/通知唤醒仅在对应功能已实现时加入，不作当前测试前置。
2. 息屏时面板肉眼全黑（非最低亮度），DCS rc 全 0。
3. 唤醒亮度恢复，触摸立即可用；30 分钟息屏充电后唤醒正常。
4. 日志 `SWEET FB POWER: off/on rc=0 elapsed-ms=`。
5. host 测试：重复请求、off 中再 off、DCS 失败回退。

### F3：硬件 vsync（修 P2）

**做什么**
- 只读预检 INTID 115 的 GIC group/enable/config 与 MDSS 中断寄存器现值。
- `sleh` 链加入 `sweet_mdss_irq()`；只开 INTF1 RD_PTR；上半部屏蔽+记时，workloop 清除/计帧/
  发通知/再使能。
- 60 Hz 定时器改为看门狗（N ms 无 RD_PTR 报错并退回定时器 vsync，日志明确）。
- 显示 OFF 时关中断。

**验收**
1. 亮屏 60 秒 RD_PTR 60±1 Hz，记录间隔 p50/p99。
2. 息屏中断计数不增长。
3. 无风暴（>200/s 自动禁用并报错）；看门狗未触发。
4. 三键/触摸/USB/电池 IRQ 正常。
5. host 测试：上下半部记账、看门狗回退、off 禁用。

### F4：自有扫描缓冲 + 硬件裁剪 + 双缓冲翻页（修 P3/P5，L1）

**做什么**
- `SweetScanoutPool` 分配 2 个物理连续缓冲（1125×2436×4，行距 4608，写合并），记录物理地址。
- 交接：一次性复制 ABL 当前画面到缓冲 A；配置 SSPP：`SRC_FORMAT/UNPACK` 显式写 BGRA、
  `YSTRIDE0=4608`、`SRC_XY=(22,48)`、`SRC_SIZE=OUT_SIZE=1080×2340`、`SRC0_ADDR=A`，CTL FLUSH VIG0；
  下一 RD_PTR 读 `ADDR_SW_STATUS` 确认生效。
- 呈现：swap 的 layer 0 surface → 拷到后台缓冲（不裁剪，整画布行拷贝）→ RD_PTR 时翻页
  （写 `SRC0_ADDR`+FLUSH）→ 下一 RD_PTR 确认后释放上一帧 surface、发完成通知。
- 实现 `SwapWait`；废弃 `sweetfb_hz`（保留解析并告警）。
- 失败（分配失败、翻页未生效）→ 回到旧 ABL 缓冲路径并显式记录。

**验收**
1. `ioreg`/日志显示扫描地址不再是 0x9d200000，而是驱动缓冲的物理地址；ABL 区域此后无写入。
2. 画面位置与旧路径一致（状态栏、圆角、底部横条不偏移）——硬件裁剪正确。
3. 滚动/拖动 1 分钟无撕裂；60 Hz 呈现（不再限 30 Hz），记录每帧拷贝耗时 p50/p99。
4. 30 分钟压力无 panic，surface 创建/释放计数平衡。
5. F2 电源循环在新缓冲上重复通过。
6. host 测试：翻页状态机（pending 覆盖、确认后释放、超时回退）、SwapWait。

### F5：surface 直接扫描（修 P4，L2/L3）

**做什么**
- IOSurface `create` 时按 F0 记录的属性识别“可扫描”surface（画布尺寸、BGRA、bpr 64 对齐、
  创建者 backboardd 等），从 `SweetScanoutPool` 分配物理连续内存（可缓存，方案 A）。
  池满或不匹配则普通分配。
- swap 时若 layer 0 surface 可扫描：`clean_dcache64` 该 surface（仅 seed 变化时）→ RD_PTR 时
  `SRC0_ADDR` 直接指向它 → 确认后释放上一帧。不兼容则走 F4 拷贝路径（L3）并计数。
- 池大小：至少 3 个画布缓冲（CA 双缓冲 + 前台），上限可配置；记录内存占用。
- 对比方案 B（写合并映射给用户态）并把结论写入 §6。

**验收**
1. 正常使用中 `direct-flips` 占比 ≥95%，`copy-fallback` 有计数且原因可查。
2. 每帧 CPU 拷贝为 0（直扫帧）；记录 clean 耗时 p50/p99（预期远小于 F4 拷贝）。
3. 无撕裂、无脏数据（快速动画下无旧帧残影/花块）——证明 cache clean 正确。
4. surface 在扫描期间不被 CA 改写（翻页确认前不释放）；压力 30 分钟无 panic、计数平衡。
5. CPU 占用较 F4 明显下降（内核计数或 `top` 对比）。
6. host 测试：可扫描判定、池耗尽回退、clean 仅在 seed 变化时执行、释放顺序。

### F6（可选）：按帧 kickoff，替代 autorefresh

F5 后仍有撕裂或功耗问题时做。参照 Linux `sde_encoder_phys_cmd`/`sde_hw_ctl`：关 INTF1
autorefresh；有新帧时翻页 + CTL FLUSH/START 提交一帧；以 PINGPONG/CTL done 中断确认；
无新帧时不发送。失败恢复 autorefresh 并记录。

**验收**：静止画面时 DSI 传输计数不增长；60 秒动画无撕裂/丢帧异常；任何失败自动恢复
autorefresh 且画面不冻结。

### F7：生命周期、IOPM、panic 与清理

**做什么**
- IOPM 两态；关机/重启前：翻回 ABL 缓冲（或把当前画面复制回去）、恢复 SSPP 原配置、DCS `28/10`，
  让后续固件接手时状态已知；`willTerminate/stop` 释放中断、映射、扫描池。
- panic：panic 时确保控制台能画到正在扫描的缓冲（翻页后 `kPEBaseAddressChange` 或 panic 路径
  强制翻回 ABL 缓冲），并在显示 OFF 时先 `29`。
- 默认切换到新路径；删除旧 BSD 探针、定时器 vsync、擦像素、CPU 裁剪拷贝；更新
  `CURRENT-DRIVER-ARCHITECTURE.md`。

**验收**
1. 重启/关机/进 recovery 各 3 次，面板无残影花屏，下次开机正常。
2. 默认镜像通过 F2–F5 验收回归子集。
3. 诊断档人为 panic，屏幕可见 panic 文本（亮屏与息屏两种情况）。
4. 无旧路径残留；文档与 `NOTES.md` 记录最终基线 SHA。

## 4. 风险与预案

| 风险 | 预案 |
|---|---|
| 新缓冲不可达，MDP 花屏/总线错误 | 同时核对 SMR/S2CR 与 CBAR/SCTLR，不能只按 TRANS route 判断。0121a2 的 CB2.M=0，不据此强加 F4a。F4 首次写新地址前重查 live 配置、内存归属与可达性，用 `ADDR_SW_STATUS` 和肉眼验证。ABL 暂按18 MiB，放不下两个1080×2340缓冲，不能作为双缓冲退路；只有确认需要地址转换时才另设计 F4a（仅扫描池映射），单独验收 |
| 物理连续 10.7 MiB 分配失败 | 启动早期分配；失败回退旧路径并记录；考虑使用 ADT 预留区 |
| 写 SSPP 格式寄存器出错导致花屏 | 先写回与 ABL 相同的值验证读写；再改裁剪；每次改动后读回比对 |
| cache clean 不足导致残影 | F5 验收 3 专门检查；回退方案 B |
| CA 不提供可识别属性，无法提前分配可扫描 surface | 按尺寸+格式+创建者匹配；仍不行则 L1（F4）作为最终方案，记录原因 |

## 5. 边界

- 不移植 UniXNU/HakiOS 整套显示栈；可参考选择子含义与寄存器序列并注明出处。
- 不做 GPU 合成（另立项目，见 `GPU-ADRENO640.md`）；`SweetDisplayBackend` 和可扫描 surface
  池为 GPU 输出预留接口。
- 不在内核重组多图层；layer 0 以外出现时先记录，不臆测处理（直扫只对单全屏图层）。
- 不把电池/充电 UI 问题当显示问题修。
- 所有设备操作 `fastboot boot`；不写盘。

## 6. F0 取证结果（0121a2 已补像素/硬件，ABI 待闭环）

0170 只增加只读取证，不改变显示行为。`sweetfbtrace=1` 记录选择子、最近 swap、
off/on 前后扫描区与 source surface 稀疏校验和；`sweetdispprobe=1` 的显示探针增加
SSPP 格式/裁剪/地址状态、INTF 计数差值和 apps SMMU SID 0x800 匹配信息。
打包脚本 `--framebuffer-f0` 会从同一内核产物生成 `fb-trace` 与
`fb-trace-preserveoff` 两档，后者只额外设置 `sweetfbpreserveoff=1`。
**0121 校正：** frame 差值误读 AUTOREFRESH_CONFIG，不能测 TE 频率；稀疏 hash 未覆盖时钟区域，
不能判定缺时间来自 CA 还是呈现路径。实现不足和实测结果详见 [F0 实测报告](FRAMEBUFFER-F0-0121.md)。

### 0121 产物核对与操作

- 0170 三个改动文件与 0120a18 基线严格应用补丁后的结果逐字节一致；连同现有源码门检查文件
  共核对 27 个文件，114 项 require_marker 通过。内核 manifest 和 F0 marker 检查通过。
- 内核 SHA256：`6e3be4432ea04c331ac9aef8e5783e0980ed2174dc463c44f76eda24bfbb48a0`。
- buildtree 包 SHA256：`4e3a9c10d373c3815e3558180eb64b8fe28083e2c209435195c3927da77c62b8`；
  kernel 包 SHA256：`b1d3de0b788a91c3ed39ad706c9cdde10127b48e9e77afc7dd193136c88a8fbb`。
- 镜像位于 `out-bs/boot-16k-0121-fb-trace.img` 与
  `out-bs/boot-16k-0121-fb-trace-preserveoff.img`；每个 35,667,968 字节。
  header/kernel blob/ramdisk/kernel/ADT/TOML 均已回读核对。SHA256 见 NOTES 顶部。
- 本轮未提供构建日志或 CI run ID，未检查全量编译警告。底稿在 `scratch/artifact-0121/`。

原两档操作（用户已完成手动息屏插拔，不重复要求）：

1. 进桌面后等至少 35 秒，让启动约 30 秒后的三次显示探针完成；确认三键、触摸正常。
2. 未接充电器时按电源键息屏，等 3 秒，插充电器；大充电 logo 消失后观察锁屏时间/中间区域。
   正常亮屏后拔线，重复 5 次。
3. 接着充电器按电源键息屏，等 3 秒后拔线，观察唤醒和画面，重复 5 次。
4. 记录普通档和 preserveoff 档表现是否不同；自动锁屏项已删除，当前设备没有实现自动息屏。
5. 结束后等至少 15 秒再导出持久日志，每档分别命名
   `sweetlog-0121-fb-trace.bin` / `sweetlog-0121-fb-trace-preserveoff.bin`。
   USB ACM 日志可同时保留，但无需依赖实时抓取来完成测试。

0121 已有结果：

- 选择子：可见 3/4/5/6/8/12/15/17/22/27/28/29/33/50/55/57；无 72 调用，完整参数/统计待补。
- CA surface：2/3，1125×2436 BGRA、bpr4608、alloc11239424、cache 属性 0x400；完整键表待补。
- 显示根因：preserveoff 有效保留像素，但不足以修复缺时间/滑条区域半透明效果异常，具体根因未定。
- SSPP 格式和 SMMU stream 已读；TE 频率、CB2 有效转换/映射范围待补，不能假定 bypass。
- mux 故障：普通档 USBIN group1 → power group0 被隔离；由 [BUG-002](BUGS.md) 独立跟踪，
  不将故障后的无唤醒当作显示路径故障证据。
- F5 缓存方案对比：未开始。

## 7. 进度记录

| 步骤 | 补丁 | 产物/日志 | 结论 |
|---|---|---|---|
| F0 | 0170 / d58e7fc | 0121 两档持久日志；SHA 见实测报告 §1 | 已分析；mux 双组隔离独立记 BUG-002，0170 取证不足，未验收 |
| F0 v2 | 0171+0172 / 2bc0251 | 0121a2 两档持久日志；SHA 见新报告 §1 | 两档异常相同；source/扫描时钟同黑，模式/帧语义待核对；CB2.M0，frame计数关闭，未验收 |
| F1a | 0175 / a22e1b3 | 0121a5两档对照、0121a6 mdss日志及用户亮度/唤醒反馈 | 资源/背光迁移正常运行路径功能验收通过；BUG-004独立；下一步F1b，F1整体未完成 |

### 0171 / F0 补充实现（历史记录，0121a2 已完成实测）

像素/实际拷贝/属性/计数补证据，详见 [F0 v2](FRAMEBUFFER-F0-V2.md)。本地编译和 host 检查通过，
实机结果见下节。CircleCI 配置未改；打包门要求新 v2 marker。独立 bug 不在本轮修复。

### 0121a2 / 0171+0172 实测已分析

38文件源码比对、114项门检查、新kernel manifest/marker以及两档boot回读通过。
镜像路径、SHA及原测试顺序见 [F0 v2](FRAMEBUFFER-F0-V2.md)；日志分析见 [0121a2 报告](FRAMEBUFFER-F0-0121A2.md)。

本轮新增事实：

- 用户确认两档肉眼异常相同；时钟ROI多次当前source/扫描RGB同为0、差异0，copy持续推进。
  普通息屏擦黑、保留档留像素均捕获，但不能解释全部异常，不把source同黑直接定为CA错误。
- surface2/3的11键属性表与21种selector集合已得到。selector79/block0x40当前全清0，
  参考实现为当前模式ID，需核对本版ABI；SwapWait/完成通知/复用协议仍有缺口。
- 只见layer0非零surface；update/disable及raw10c=0xff000000形态已捕获。空记录可能是合法
  blank，不能通过过滤空记录或硬恢复旧图绕过语义。关机滑条区域半透明效果异常仍未独立定因。
- CB2 SCTLR.M0，TTBR/TTBCR/MAIR0，本次stage1转换未使能；新地址可达性仍在F4前验证。
  frame-en0，TE实测留给F3 IRQ时间戳，不追加同类一秒差值实验。
- BUG-002本轮未复现，未宣布修复。采样中位约79ms，有诊断开销，不以此评价默认性能。

NEXT：先离线核对模式查询/属性和提交/完成/复用ABI，再定针对性修复。不立即重编译或重复
两档测试，不直接改面板/SMMU，不进入F1；F0仍未验收。

### F0 后续：本版 ABI 与症状校正

见 [离线核对](FRAMEBUFFER-USERLAND-ABI.md)。用户澄清关机滑条是半透明颜色未正常渲染，
不是控件几何重叠；此处不规划几何布局修复。已定位本地iOS16.7的原始框架：+0x10c背景、
bit31、全disable合法blank和SwapWait成功后CA推进页回收均已确认。实现优先顺序调整为：
完成/复用契约（先厘清flags/id0/非阻塞）→持久layer/background和blank→一致的模式属性/查询。
模式输出+4/+8已确认，但不能任意填ID；半透明/blur症状还需局部像素证据，不直接加内核混色。
本轮离线审计无新kernel补丁，不要求重复刷机；F0未验收，F1仍未开始。

### F0 后续：原版内核等待语义与状态设计

从本地同版IPSW解压原生kernelcache，核对legacy gated wait；28项指令/dispatch记录通过。
详见[帧完成契约](FRAMEBUFFER-SWAP-CONTRACT.md)：options0区分当前显示与pending，1检查pending，
2检查整队列；3/4检查配置状态及其反向条件，高位noBlock未就绪返回Busy（3/4各有立即返回规则）。
不能将id0配置查询写成队列排空，也不能让所有options仅比较lastCompleted。
本次修正§2.4笼统的pending覆盖完成策略，写明gate等待、源释放后自有像素重放、取消/停止边界。
尚未实现行为补丁；不改变F0只读取证、不跳过F1/F3/F4。视觉异常的单一根因仍未证实。

### 0173 / CPU源像素所有权行为实验（尚未上机）

F0未验收，另列[自有快照实验](FRAMEBUFFER-OWNED-CONTRACT.md)，不宣称F1/F3/F4完成。
独立开关sweetfbcontract=1，默认旧路径。一次编译两档，仅开关不同，均保留息屏像素并关闭重采样。
实现gate状态/有界wait、提交线程复制到自有内存、源归还后仅读自有像素、持久layer0/背景/blank、
取消/close/stop。没有切换MDP扫描地址，没有真实vsync fence，也不认定半透明异常已定因。
三文件strict apply/arm64编译、14项host回归通过，LSan受ptrace限制未验收。
NEXT：用户push并下载产物→核对打包→对照/实验档手动息屏插拔与关机界面半透明/取消测试。

### 0121a3 / 0173 两档实测已分析（2026-11-09）

见[实验结果](FRAMEBUFFER-CONTRACT-0121A3.md)。新路径启用，options0当前显示Busy、替换后
成功，options1可在仍显示时成功；blank与7轮off/on均有记录，未记录helper错误。
用户确认两档视觉一致、未见新异常，缺时间/滑条区域半透明效果未消除，不验收F0。
保留独立开关、不默认切换，不重复相同A/B；下一步离线核对模式/颜色时序属性及无匹配mode
分支，再设计独立修复。对照末段Type-C电池group隔离补入BUG-002，不扩当前显示范围。

### 0174 / 同一 swap 像素补证（2026-11-09）

用户追问旧证据能否证明原始提交缺时间，因此将[同ID取证](FRAMEBUFFER-SAME-SWAP-PROOF.md)
放在模式元数据核对前。独立sweetfbproof=1，依赖0173；关联源复制到自有layer、组帧、写屏
后的扫描内存回读，完整检查时钟ROI，区分新layer/旧缓存/blank。按亮屏窗口限额，无新timer。
严格应用/arm64编译/14项host回归通过；新产物尚未云编译、未上机，不宣称修复视觉问题。
NEXT：用户push→下载新kernel/buildtree→核对打一个proof档→手动息屏插拔与取消关机后留持久日志。
旧对照无需重复。F0仍未验收，不将CPU回读解释为硬件完成，不提前验收F1/F3/F4。

0121a4产物已核对：41源码逐字节匹配、114源码marker及新内核proof marker通过，单档
`boot-16k-0121a4-fb-contract-proof.img`已生成并回读；SHA与参数见[取证文档](FRAMEBUFFER-SAME-SWAP-PROOF.md)。
等待用户手动测试，启动正常锁屏先停5–10秒作参照，再做息屏插拔及关机界面/取消；尚未验收。

### 0121a4 / 同一swap实测结果（2026-11-09）

详见[实测报告](FRAMEBUFFER-PROOF-0121A4.md)。第一轮Type-C RID/APID271 bit0 pending触发
电池group1隔离，解释插线/UI无反应，按键仍能唤醒；补BUG-002独立跟踪。
第二轮无隔离/ERROR/WARN，61份样本全部panel逐像素diff0；关键插电窗口连续约7.33秒的
同ID/新layer/完整ROI/rc0样本，源快照、组帧、扫描内存均全黑，拔电后ROI有非黑内容。
用户更新：插电（包括原本亮屏时插电）大logo后缺时间、息屏拔电正常、取消关机后锁屏正常。
不能据此识别时间字形或给CA定因；关机组合键晚于最后像素样本，半透明没有本轮独立证据。
NEXT：不重复取证/旧A/B；继续离线核对模式/颜色/时序属性与本版fallback，确定分支后再修复。
F0未验收；不强行重放旧时间、不据CPU回读提前验收F1/F3/F4。0174采样约45ms中位，默认关闭。
