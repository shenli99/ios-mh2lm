# MH2LM 显示/面板驱动重构计划（大目标二）

**状态（2026-11-08，第二版）：计划已定，F0 未开始。** 本文是 `IRQ-FRAMEBUFFER-PLAN.md`
“大目标二”的实施文档。第二版目标从“修好接手的 ABL framebuffer”提升为：**驱动自己
分配和管理扫描缓冲（simplefb 式），硬件双缓冲翻页，CoreAnimation 渲染的 surface
直接被 MDP 扫描，消除每帧 CPU 拷贝。**

agent 按 F0→F7 顺序做。每步通过验收后，在本文 §7“进度记录”和 `NOTES.md` 写结果，
再进入下一步。不跳步；每步的新行为都由 boot-arg 打开，默认路径在该步验收前不变。

## 1. 现状（0120a18 buildtree 源码 + 0120a17 日志）

### 1.1 硬件事实

| 项 | 事实 | 证据 |
|---|---|---|
| 面板 | Tianma R66456A，1080×2340，60 Hz，RGB888，MIPI DSI **command mode**，4 lane，VC0 | `docs/panel.md` §1 |
| TE / 上下电 DCS | TE 走 GPIO8；on …`35 00`→`29`→`11`+150 ms；off `51 00 00`+90 ms→`28`→`10`+150 ms | `docs/panel.md` 84、139-156 行 |
| ABL 交接 | VIG0 SSPP：`SRC_SIZE`(+0x00)=0x0924_0438、`SRC0_ADDR`(+0x14)=0x9d200000、`YSTRIDE0`(+0x24)=0x10e0；INTF1 tear-check 开、**autorefresh=0x80000001**；CTL0 FLUSH/START=0；DSI0 cmd 模式 | 0120a17 日志 `SWEET DISP: pass 0..2` |
| 含义 | MDP 每个 TE 自动从 SSPP 源地址取整帧发给面板。**改 `SRC0_ADDR` 并 flush 即可换扫描缓冲**，这是硬件翻页和零拷贝的基础 | 同上；`sde_hw_sspp.c:25-37` |
| ABL 缓冲 | 0x9d200000 起 0x1200000（18 MiB），Mu-Silicium 标 `Display Reserved`、WRITE_THROUGH；Android DTS `disp_rdump` 0x9c000000 起 0x1200000 | `MemoryMapLib.c:25`；`sm8150-mh2lm_common.dtsi:80` |
| SSPP 寄存器 | `SRC_SIZE`0x00、`SRC_XY`0x08（源裁剪起点）、`OUT_SIZE`0x0c、`SRC0_ADDR`0x14、`YSTRIDE0`0x24、`SRC_FORMAT`0x30、`UNPACK`0x34、`OP_MODE`0x38、`ADDR_SW_STATUS`0x70 | `sde_hw_sspp.c:25-82` |
| CTL | `FLUSH`0x018、`START`0x01c，VIG0 的 flush 位 BIT(0) | `sde_hw_ctl.c:29-30,167` |
| MDSS 中断 | `interrupts=<0 83 0>` → INTID 115【待核实】；INTF1 tear 中断块 MDP+0x6E800（EN/STATUS/CLEAR=+0/+4/+8），bit2 RD_PTR、bit0 AUTOREFRESH_DONE；顶层 HW_INTR_STATUS=MDP+0x10 | `sm8150-sde.dtsi:44`；`sde_hw_interrupts.c:25,37-41,194-198` |
| SMMU | MDSS 在 apps_smmu 下（SID 0x800/0x420）。ABL 留下的 VIG0 地址就是物理地址 0x9d200000，说明当前上下文是旁路或恒等映射【待核实】 | `sm8150-sde.dtsi:47`；日志 |

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
| P1 | 息屏插拔唤醒中间黑块 | off 擦扫描内存，on 只重放最近帧（源码可证；0127 preserveoff A/B 从未实测） |
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
  覆盖 SMMU 旁路/恒等映射的不确定性）。
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

- 一个 surface 在被扫描期间不得交还给 CA 继续绘制：翻页确认后才释放上一帧，并发
  swap 完成通知（类型按 F0 结果）；`SwapWait` 在命令门上等到目标 swap 完成（带超时）。
- 队列深度 1：新 swap 覆盖尚未上屏的 pending 时，被覆盖的 swap 立即按完成释放。

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
| `sweetfbtrace=1` | F0 | 显示诊断日志 |
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
- `sweetfbtrace=1`：每个 IOMFB 选择子记录进程名、参数个数与前 4 个标量（各前 32 次后计数）；
  off/on 前后最近 4 次 swap 的 surface id/mask/rect；off/on 时扫描内存与来源 surface 中间
  区域（行 900–1400，稀疏采样）校验和。
- IOSurface `create` 记录属性字典的键与关键值（宽高、格式、bpr、alloc size、cache mode、
  是否有 displayable/scanout 类属性），以及创建者进程——为 F5 识别可扫描 surface 提供依据。
- 只读：SSPP VIG0 `SRC_XY/OUT_SIZE/SRC_FORMAT/UNPACK/OP_MODE/ADDR_SW_STATUS`，INTF1
  帧/行计数 1 秒变化（TE 频率），SMMU 对 MDSS SID 0x800 的 SMR/S2CR 状态（判断旁路）。
- 打包两档：`fb-trace`、`fb-trace-preserveoff`。

**验收**
1. 两档各做：电源键息屏→3 秒→插充电器→亮屏→拔；auto-lock→插拔；各 5 次。
2. 回答黑块根因：来源帧本身黑（CA 侧）还是被擦后没重画（内核侧）。
3. 得到选择子使用表、CA surface 创建属性表，写入 §6。
4. 得到 SSPP 格式寄存器值、TE 频率、SMMU 是否旁路，写入 §6。
5. 原始日志 SHA 记入 §7。

### F1：IOKit 分层重构，行为不变

**做什么**
- ADT 新增 `/arm-io/mdss`（`compatible "sweet,mdss"`，`reg` 列 MDP、DSI0、DISPCC、GCC disp 页，
  `interrupts` 115）。
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
4. 显示代码无 `ml_io_map`；`ioreg` 可见 `SweetMDSS`、`AppleCLCD`。
5. host 测试：分发表参数校验、gate 序列化。

### F2：真显示电源（修 P1/P6）

**做什么**
- 电源状态机 `ON→OFF_PENDING→OFF→ON_PENDING→ON`，失败回退到旧行为。
- off：停呈现→DCS `28`；**不擦扫描内存**。on：`29`→继续显示当前缓冲。第一阶段只用
  `28/29`，通过后再评估 `10/11`（+150 ms）。
- 背光与电源解耦，亮度由 CoreBrightness 恢复。

**验收**
1. 20 次循环（电源键/auto-lock/息屏插拔/通知唤醒各 5）全部完整显示，无黑块。
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
| SMMU 非旁路，MDP 访问不到自有缓冲（花屏/总线错误） | F0 先读 SMR/S2CR 判断。F4 首次翻页只在诊断档做，用 `ADDR_SW_STATUS` 和肉眼确认后再继续。ABL 预留区只有 18 MiB，放不下两个 1080×2340 缓冲（约 20.2 MB），不能作为双缓冲退路；若确认非旁路，在 F4 前插入子步骤 F4a：为 MDSS 上下文建立恒等映射（只映射扫描池），单独验收 |
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

## 6. F0 取证结果（待填）

- 选择子使用表：
- CA surface 创建属性：
- 黑块根因：
- SSPP 格式寄存器 / TE 频率 / SMMU 状态：
- F5 缓存方案对比：

## 7. 进度记录

| 步骤 | 补丁 | 产物/日志 | 结论 |
|---|---|---|---|
| F0 | — | — | 未开始 |
