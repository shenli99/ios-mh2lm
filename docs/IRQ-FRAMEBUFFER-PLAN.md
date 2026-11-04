# MH2LM IRQ 与 Framebuffer 路线图

**状态：** 大目标一 / 子目标 1 的三条按键硬件 IRQ 路由已由 0114a10 实机
验证；Mode 3 保留 200 ms 恢复轮询，长期可靠性与完全事件驱动仍需后续评估。
子目标 2（DWC3 USB IRQ）尚未开始。
**更新日期：** 2026-11-04
**适用范围：** `ref/ios-mh2lm` 的 MH2LM XNU 驱动；本文是实施和验收计划，
不替代 `NOTES.md` 中已经取得的硬件观察、日志结论和回归基线。

## 总体顺序

1. 先完成一部分既有驱动的 IRQ 化：按键、DWC3 USB、触摸。
2. 在 IRQ 目标结束后，再为 framebuffer 修复制定可执行的详细计划并实施。

每一轮仍应沿用一个已验证的内核产物生成多个临时 `fastboot boot` 测试镜像的
方式。不要为了单一测试组合重复触发云端编译；不要在本路线中持久刷写启动分区。

按键的 GIC、DTS、PMIC 外设和 PMIC-arb 事实、未知项及预检步骤见
[`KEY-IRQ-HARDWARE-BASELINE.md`](KEY-IRQ-HARDWARE-BASELINE.md)。该基线是
`SweetSpmiInterruptMux` 的硬件输入，不以其他设备的 PDC/PMIC 常量替代 MH2LM 实测。

## 大目标一：既有驱动的 IRQ 化

### 架构约束

本项目采用下列 XNU/IOKit 路径：

```text
PMIC 外设 / 控制器
  -> PMIC-arb / PDC 或控制器 GIC SPI
  -> XNU GIC callback
  -> SweetSpmiInterruptMux（仅共享 PMIC 源）
  -> consumer 的软件事件入口 / IOWorkLoop worker
  -> 既有的 HID、USB 或 TouchComm 工作路径
```

- `SweetSpmiInterruptMux` 是板级 PMIC 源解复用器，不是 Linux
  `irqdomain`，也不取代 XNU 的 GIC 或 IOKit 中断管理。
- 共享 PMIC SPI 的物理注册只能有一个拥有者，因此不能让按键、充电/电池等
  consumer 分别直接绑定同一 SPI。UniXNU 的 `armcpu_spi_attach(...)` 是
  参考实现；0114a7 实际内核使用内建 `SweetCPU`，不含这个符号，须先为当前
  XNU 的 GIC IRQ 分发建立唯一的 SPI 513 注册与回退机制。
- mux 的上游应是很短的 GIC callback：屏蔽对应 GIC SPI 并向软件
  `IOInterruptEventSource` 发信号。PMIC pending 扫描、latch 确认和重新使能
  由 `IOWorkLoop` worker 完成；不得在 GIC callback 执行 SPMI observer 读取、
  HID、DSI 或 UI 工作。
- mux 内部不得写死面向具体功能的 `switch`。采用静态路由表：每一项描述
  SID、PPID、PMIC bit mask、初始化后解析出的 APID、触发/极性/latch/确认策略、
  诊断及风暴保护参数、以及 consumer 回调和 context。第一阶段在启用 IRQ 前
  完成注册，运行期路由表保持不可变。
- 将来若 PMIC 子功能增加，路由表可抽象为注册接口或由 PMIC provider 注册子
  consumer；GIC callback 与上层 consumer handler 的接口不应因此改变。
- PMIC SPI 481 的共享对象是按键和 PMIC 充电/电池事件；它不是 DWC3 数据 IRQ。
  DWC3 使用控制器自己的 GIC IRQ，触摸使用其 TLMM GPIO/TouchComm IRQ 路径。

### 子目标 1：完成并验证 `SweetSpmiInterruptMux`

**目的：** 建立共享 PMIC IRQ 的可扩展分发和下半部交接机制，并以按键作为第一
个真实 consumer 验证。

**历史检查点（0114a7，只读路由预检完成）：** PON 的重复 PPID 映射包含
EE1/EE4 的 APID 1/19 和 EE0 的 APID 248。旧探针读取外部 EE channel 时
回 LG BSP；0132 跳过外部 EE 后，PON、GPIO6 的路由和外设配置单项/组合
预检全部进入 iOS。两条 EE0 channel 的 `ACC_ENABLE`、`IRQ_STATUS` 均为 0，
外设 `EN_SET` 也为 0。下一阶段是核对上游 PDC/GIC、latch/clear 和使能
顺序，再实现可回退的单 source Power IRQ 试验；当前 20 ms 轮询仍在工作。
详见硬件基线文档和 `NOTES.md` 的 0114a7 实测记录。

SM8150 PDC 源码已确认 pin 1 对应 PMIC EE0 IRQ / GIC INTID 513；Linux
PMIC-arb v5 确认 channel clear 在 `+0x108`。0114a8 实机读回 PDC pin 1
`enable-bank=0 config=0x4`，已知该 pin 当前关闭、配置为高电平有效。
GIC SPI 513 的现有空闲值无需再单独测试；当前 XNU 没有为它做专门配置，
新驱动会明确设置并读回 Group 1、level、priority、route 与 enable。
首轮可写试验应先具备当前内核的 SPI callback、mux 路由表、中断风暴保护
及轮询回退，然后才按单 Power source 开 gate。0133 的 GIC/组合只读镜像
保留作诊断备选，不作为进入实现的前置测试。

0134 首版已包含上述可按启动参数选择的 gate：`sweetkeysirq=1` 只验证
IOInterruptEventSource/IOWorkLoop 软件交接，`=2` 仅启用 Power IRQ，`=3`
启用三键 IRQ。默认参数不改硬件。其构建产物需依次临时启动 baseline、
软件交接、Power、三键镜像，每步提取独立日志；只有前一步正常才进入下一步。
0134 的语法与补丁应用已在本地验证；实机 IRQ 结果仍未知。
0114a9 已提供含 mux 符号的完整内核产物，四个测试镜像已从同一内核生成；
实机尚未报告 IRQ 命中，镜像路径与 SHA256 见 `NOTES.md`。

**实测更新：** 0114a9 Power 测试因非安全侧 GIC Group 位读回 0 而回退；
0135 先软件 pending SPI 513 并要求真实投递，再开 PMIC/PDC gate。
0114a10 的 Power 与三键模式均通过此探针，三条路由均有硬件 IRQ 命中，
按键状态配对正常，USB 实时日志同时工作。具体次数、日志 SHA256、
200 ms 恢复轮询的证据边界见 `NOTES.md`。

**实施边界：**

- 实现 `SweetSpmiInterruptMux`、静态路由表、pending 状态扫描、确认顺序、
  计数器和可诊断的异常/风暴防护。
- 在启用任何 PMIC IRQ gate 前，完成 MH2LM DTB/DSDT、PMIC-arb v5、APID、PDC
  与确认寄存器的只读核对；不得复用 UniXNU 其他设备的 PPID、PDC pin 或寄存器
  常量。
- 先仅将按键接入 mux：GIC 上半部调度 mux 的软件事件，mux worker 唤醒既有按键
  worker；按键 worker 继续读取电平并
  经既有 `sweet_hid_key()` 发布 HID 事件。IRQ 成功 arm 后，替换当前 20 ms
  轮询等待；保留安全的回退/诊断模式，直到硬件稳定。
- 暂不把电池/充电 PMIC gate 作为本子目标的硬件 consumer。它们将来可以复用 mux，
  但必须与各自的 latch 确认和 `IOWorkLoop` handoff 一同单独启用。

**验收条件：**

- `SweetSpmiInterruptMux` 的路由、ack、下半部调度和诊断在真实设备上运行，且不
  再依赖硬编码功能分支。
- Power、Volume Up、Volume Down 按键通过 IRQ 成功产生正确的按下/释放 HID 事件。
- 重复按键、组合键、display-off/唤醒及已建立的 USB/电池回归不出现中断风暴、
  漏释放、重复事件、GIC 注册冲突、kernel trap 或 panic。
- 日志保存 IRQ 路由命中、ack、worker 消费及异常计数，足以判定一次按键从物理源
  到 HID 的完整链路。

### 子目标 2：将 DWC3 USB 改为 IRQ 驱动

**目的：** 把当前已验证的 DWC3 CDC-ACM 轮询事件处理改为控制器 IRQ 驱动，同时
保持既有 USB 设备模式和日志能力。

**下一步的源码基线：** MH2LM 使用的 `sm8150-usb.dtsi` 中，主 USB
`dwc3@a600000` 子节点列出 GIC SPI 133（INTID 165）；USB wrapper 另有
`pwr_event_irq` 等中断，不能把它们当作 DWC3 事件缓冲区 IRQ。当前
`sweet_usb.c` 在 `GEVNTSIZ` bit 31 屏蔽事件中断，并由 `ud_thread` 每
1 ms 读取 `GEVNTCOUNT`。以上是源码映射和现有实现，尚非新 IRQ 的实机投递证据。
先核对目标 DTS 与 GIC 路由，再在现有 CDC-ACM 驱动上加入可由启动参数选择的
软件投递检查、DWC3 IRQ 上半部和既有事件处理 worker 的唤醒/回退。
同一内核应能选择当前轮询路径、USB IRQ 路径，以及同时开启
`sweetkeysirq=3` 的并发路径；USB 日志继续作为正常观测通道，早期或 USB
中断故障仍从持久 SWEETLOG 取证。
已知值、必须实测的 SPI 165 投递/确认步骤及无需重复测试的项目见
[`USB-IRQ-HARDWARE-BASELINE.md`](USB-IRQ-HARDWARE-BASELINE.md)。

**实施边界：**

- DWC3 不进入 `SweetSpmiInterruptMux`。它使用独立的 DWC3 GIC IRQ 与自己的短
  上半部/worker。
- 上半部仅标记并调度；worker 负责 drain event buffer、DMA cache 操作、
  `GEVNTCOUNT` 确认和 rearm 顺序。不得在 IRQ 上半部执行完整 gadget 状态机。
- 保留可选择的诊断/回退路径，直到 IRQ 模式在 CDC-ACM、DTR、TX/RX、拔插、bus
  reset 下稳定。测试镜像可通过 boot arguments 选择单项或组合测试。

**验收条件：**

- USB IRQ 模式下 CDC-ACM 可枚举、host DTR 可用、日志持续输出，TX/RX、重连和
  bus reset 无丢失或卡死。
- DWC3 IRQ 与按键 IRQ 可以并行工作，不共享/争用错误的物理 SPI，也没有 GIC
  注册、锁、DMA 或事件确认顺序冲突。
- 在 USB、按键以及现有电池轮询共同运行的回归中，无 kernel trap、panic、明显
  的事件风暴或功能退化。

### 子目标 3：将触摸改为 IRQ 驱动

**目的：** 用触摸的 TLMM GPIO/TouchComm 中断通知替代当前 10 ms ATTN 采样和周期性
强制传输的功能性回退。

**实施边界：**

- 触摸不进入 `SweetSpmiInterruptMux`；沿用其 TLMM GPIO/TouchComm 路径，并以短
  上半部唤醒/调度既有触摸读取与 HID 发布 worker。
- 逐步替换轮询，只在必要的恢复、去抖或硬件异常处理中保留受控回退，不把轮询
  当作常态数据通路。
- 验证单点、多点、拖动、锁屏、屏幕开关、长时间空闲后的首次触摸，以及与按键和
  USB 并发时的行为。

**验收条件：**

- 触摸 IRQ 模式在真实设备稳定工作，触摸报告和 UI 交互正常。
- 无持续轮询造成的固定空闲读取负担；没有漏触、重复触、卡死、I2C 错误回归或
  中断风暴。
- 与已完成的按键 IRQ、DWC3 USB IRQ 以及既有电池路径并行时无冲突。

### 大目标一完成判定

只有三个子目标均满足各自验收条件，且完成一次组合回归后，才视为此大目标完成。
组合回归至少覆盖：正常桌面启动、按键/组合键、触摸交互、USB CDC-ACM 日志、
插拔 USB 后的恢复、display-off/唤醒，以及持久日志中的 IRQ 计数和错误检查。
每个结果要记录 kernel/image SHA256、boot arguments、线缆状态、测试时间与原始
日志文件名到 `NOTES.md`。

## 大目标二：修复 framebuffer 渲染

**目的：** 解决已记录的局部重绘/锁屏唤醒渲染问题，特别是 display-off 后插拔电源
会唤醒、状态栏和 home indicator 可见但中间时钟/锁屏区域保持黑色的现象。

**当前边界：** 此阶段暂不定义子目标、实现方案或验收细节。先完成大目标一，再基于
当时的 IRQ 回归基线与 framebuffer 实验结果撰写详细计划。

在此之前，保持当前保守方向：先验证 `sweetfbpreserveoff=1` 的非破坏性 display-off
实验；若仍不足，再评估受限的全屏 shadow buffer。不要把 UniXNU、Hakios 或其他项目
的整个 framebuffer 栈直接移植进来，也不要将电池事件或充电 UI 伪装成 framebuffer
问题的修复。

## 文档维护规则

- 每次阶段性实现、CI 结果、设备测试和回归结论都追加到 `NOTES.md`，明确区分观察
  与推测，并保留日志与 SHA256。
- 本文件只维护路线、边界和验收条件；已验证的硬件事实以 `NOTES.md` 为准。
- 不推送代码或文档；需要同步远端时明确告知由用户执行 push。
