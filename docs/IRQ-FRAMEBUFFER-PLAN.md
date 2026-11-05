# MH2LM IRQ、Framebuffer 与日志体系路线图

**当前进度：** 0119 mode4已实测三次独立MSOC中断，拔线期间电量由74%下降至
73/72/71%，与四次USBIN刷新区分，三键/触摸继续工作；之前mode3息屏插拔唤屏
已有证据。CHGR仍state7/charging0、没有独立命中，充电状态IRQ尚未验收。
按用户要求继续电池驱动：先用同0119内核的充电诊断镜像恢复物理充电/CHGR
证据链，再完善采样有效性、真实遥测及容量/profile，见
[`BATTERY-DRIVER-NEXT.md`](BATTERY-DRIVER-NEXT.md)。充电诊断主档九笔、no-usb
对照十一笔均state7/en0、无负IBATT，插线输入有效；禁充初态早于mux/USB初始化。
关闭当前启动的gadget未恢复充电。0149已修复逐字段诊断有效性，通过本地验证，
待用户push/云编译；保留SPMI白名单，不改充电使能或IRQ路径。
电池IRQ子目标仍未完成，framebuffer继续排在IRQ验收之后。证据与镜像见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。0148故障隔离仍只有本地
注入验证，未在手机触发；策略见 [`MUX-GROUP-ISOLATION.md`](MUX-GROUP-ISOLATION.md)。

**当前补充 0145：** 按用户要求，先完成 consumer 匹配通知、公开 C++ attach/
detach 和 terminated 清理，移除全局单例桥接。本地实现/验证完成；0118 原生
构建已核对，baseline 的新 registry 接入、三键/触摸/ACM 已有实机证据；
补充 wake live 已确认单独电源键息屏/唤醒及唤醒后输入，下一步测 no-keys/usbin，
电池 IRQ 子目标仍未验收。当前架构见
[`MUX-CONSUMERS.md`](MUX-CONSUMERS.md)，固定路由表与后续 framebuffer 顺序不变。

**状态：** 0114a11 实机验证按键三路与 DWC3 USB IRQ 并行；0114a13 实机验证
触摸 GPIO122 IRQ 与 USB 并行。0114a13 三路组合测试中音量上键的 PMIC
确认写入失败，mux 回退轮询，所以尚未完成固定 IRQ 验收。0138 增加有界
确认重试；0139 将 MH2LM 三路驱动固定为 IRQ、移除静默轮询回退，仍待
CircleCI 和实机验证。0140 已作为临时方案给当前 IRQ 驱动添加分级日志。
0114a14 构建日志确认 dxb 跳过了 0139 和 0140，该产物不得用于实机验证；
0139 已修正与 0138 的补丁冲突，CI 增加源码标记校验。0114a15 产物已确认包含
0139/0140，已打包普通与 DEBUG 两个临时镜像。0114a15 实机 ACM 日志中三键
PMIC mux 路由共 32 次命中、触摸 689 次、USB 1536 次，含息屏唤醒后的触摸和
并发活动，无驱动停止；固定 IRQ 的正常路径已通过本轮组合测试。0138 的异常
确认重试未被触发，仍未单独验证。日志文件名虽含 DEBUG，实际 boot-args 未含
`sweetloglevel=3`，分级日志的 DEBUG 档仍待核对。
随后 `live-e-0114a15-irq-only-debug1.txt` 明确带有 `sweetloglevel=3`，
记录 279 条 `[DEBUG]` 且 USB/触摸 IRQ 继续工作；正常 INFO 与 DEBUG
两档已分别验证。按键未在这份较短的 DEBUG 日志中操作，仍以前一份
组合测试为依据。
2026-11-05 补充：上述 0114a15 镜像漏掉真实电池启动参数，三路 IRQ
日志中的电池服务是静态占位。`d84ac7f` 已补回参数并用同一内核重打包
`0114a15-batt` 镜像。`live-0114a15-batt-irq-only.txt` 已确认真实电池
采样恢复：63 次 gauge 读数稳定 70%、4012–4013 mV，与 USB IRQ/ACM
及少量触摸事件并行；本次未操作按键、未覆盖插拔转换，所以完整组合回归
仍未覆盖。硬件报告 USB 存在但 state=7 未充电，未启用电流诊断。
2026-11-05 路线调整：按用户要求，在 IRQ 阶段追加子目标 4“电池 IRQ 化”，
在 framebuffer 之前实施。0141 已完成电池 IRQ 首版本地实现、交叉编译和模拟
故障检查；0115 云端产物的最终源码、符号与标记核对通过，六个临时镜像已生成。
0115 mode1 实机 live 日志显示真实采样与三键/USB/触摸正常，随后持久日志
补齐 REVID/FG 信息并明确 preflight=1。随后 mode2 因电池早于 mux 启动，
十秒等待耗尽而停用，电池路由未接入。0142 改为 mux 就绪事件交接，本地
编译与启动顺序模拟检查通过，待新云产物重测 USBIN；子目标仍未完成。
同一内核的分阶段测试入口见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。
2026-11-05 用户调整当前优先级：暂停电池验收，先清理启动归属。
0143 从 keys 移出 mux 启动、从 UFS 移出无存储依赖的外设启动，增加独立
BSD 晚期板级入口；keys/battery 各自 attach 路由。保留 0142 就绪交接，
UFS 只保留存储及分区持久日志。此项本地实现，仍待新 CI 与实机回归。
电池检查点、前后调用链、故障边界及同内核三档回归见
[`DRIVER-STARTUP.md`](DRIVER-STARTUP.md)。
随后按用户要求追加 0144：mux IOService 化，晚期 nub + personality 正常匹配，
实例拥有资源、consumer 与 stop/free；保留原 GIC callback/软件 IES。
filter 与 controller 的职责/选择、退出并发规则见
[`MUX-IOSERVICE.md`](MUX-IOSERVICE.md)。当前仍先验收启动及服务匹配，再继续电池。
Framebuffer 修复尚未启动。
0117 baseline 已确认新 board 入口先于根挂载、mux IOService 成功启动，
三键真实 IRQ/HID、触摸、USB 日志与电源键息屏唤醒回归通过。电池 IRQ=0，
子目标 4 仍未完成；下一步同内核 no-keys 独立 USBIN，再测 keys+USBIN。
**更新日期：** 2026-11-05
**适用范围：** `ref/ios-mh2lm` 的 MH2LM XNU 驱动；本文是实施和验收计划，
不替代 `NOTES.md` 中已经取得的硬件观察、日志结论和回归基线。

0114a15 当前 USB、按键、触摸、framebuffer、IOSurface 和背光的实际数据流、
与 XNU/IOKit 常规方式的差距，见
[`CURRENT-DRIVER-ARCHITECTURE.md`](CURRENT-DRIVER-ARCHITECTURE.md)。

## 总体顺序

1. 先完成一部分既有驱动的 IRQ 化：按键、DWC3 USB、触摸，再追加电池 IRQ 化。
2. 在 IRQ 目标结束后，再为 framebuffer 修复制定可执行的详细计划并实施。
3. 将日志体系优化列为后续大目标；当前 0140 分级日志先用于 IRQ 验收。

### 0139 固定 IRQ 验收阶段

用户决定以功能显式停止和持久日志暴露 IRQ 故障。MH2LM 运行时按键、DWC3
事件、TouchComm 输入不再使用定时恢复读取，也不接受通过启动参数切回轮询。
按键 mux 失败时释放仍按下的键；触摸失败时释放触点；USB 事件故障时屏蔽
IRQ 并停止 gadget 工作线程和实时 ACM 日志。USB 日志发送线程由日志新增、
配置和发送完成事件唤醒。启动时有界等待、GIC 软件 pending 探针和诊断
计数仍保留；电池、UFS、framebuffer 的既有机制不在 0139 范围内。

0139 的实现与验收仅针对 MH2LM；共享按键和 USB 源文件已去掉旧轮询分支，
GUACAMOLE 与 SWEET 不在本轮支持和验证范围内。
这一步的源码和本地编译检查不等于实机通过。下一轮从 CircleCI 产物用
`tools/mk-mh2lm-event-matrix.sh ... --irq-only` 生成普通临时镜像；
包含 0140 的产物还会生成同内核的 DEBUG 日志镜像，
用 `fastboot boot` 验证三键、触摸、USB 枚举与日志并发、息屏/唤醒及重复
输入。特别检查 0114a13 音量上键确认失败是否被 0138 重试恢复；若三次
重试仍失败，应看到明确失败日志和按键失效。USB 故障时实时日志可能断开，
需提取新的持久 SWEETLOG，并以本轮 boot args 和日志计数确认新鲜度。

每一轮仍应沿用一个已验证的内核产物生成多个临时 `fastboot boot` 测试镜像的
方式。不要为了单一测试组合重复触发云端编译；不要在本路线中持久刷写启动分区。

按键的 GIC、DTS、PMIC 外设和 PMIC-arb 事实、未知项及预检步骤见
[`KEY-IRQ-HARDWARE-BASELINE.md`](KEY-IRQ-HARDWARE-BASELINE.md)。该基线是
`SweetSpmiInterruptMux` 的硬件输入，不以其他设备的 PDC/PMIC 常量替代 MH2LM 实测。

## 大目标一：既有驱动的 IRQ 化

### 架构约束

按键 0114a 系列的失败原因与可复用检查顺序见
[`IRQ-BRINGUP-LESSONS.md`](IRQ-BRINGUP-LESSONS.md)；USB、触摸适配前先核对
其中的产物来源、日志新鲜度、GIC 投递和真实设备事件四项。

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

**0114a11 结果（2026-11-04）：** 已验证 INTID 165 软件 pending 与真实
DWC3 IRQ 投递、event buffer 处理、CDC-ACM 实时输出，以及与三键 IRQ
并行。USB IRQ 计数至少到 512，无空中断、漏 IRQ 回退或 mux 回退；
三键均有真实 mux 命中和 HID 按下/松开。一次音量上松开时 PON 读取失败，
GPIO 与 HID 松开正常，详见 `NOTES.md`。用户确认多次拔插后偶发的
Windows 未识别设备在旧轮询驱动中已存在，列为独立 gadget 重连缺陷。
当前转入子目标 3。

**目的：** 把当前已验证的 DWC3 CDC-ACM 轮询事件处理改为控制器 IRQ 驱动，同时
保持既有 USB 设备模式和日志能力。

**实施前的源码基线：** MH2LM 使用的 `sm8150-usb.dtsi` 中，主 USB
`dwc3@a600000` 子节点列出 GIC SPI 133（INTID 165）；USB wrapper 另有
`pwr_event_irq` 等中断，不能把它们当作 DWC3 事件缓冲区 IRQ。当前
`sweet_usb.c` 在 `GEVNTSIZ` bit 31 屏蔽事件中断，并由 `ud_thread` 每
1 ms 读取 `GEVNTCOUNT`。以上是当时的源码映射，实机投递结果见本节开头。
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

**0137 本地进度：** 已定位正在使用的 TouchComm 驱动、GPIO122 和 TLMM 汇总
SPI 208（候选 INTID 240），并写好带 GIC 探针、真实 IRQ 与轮询回退的补丁。
本地编译与补丁应用通过，仍待 CircleCI 和手机验证。寄存器与测试门槛见
[`TOUCH-IRQ-HARDWARE-BASELINE.md`](TOUCH-IRQ-HARDWARE-BASELINE.md)。

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

### 子目标 4（临时追加）：电池 IRQ 化

**当前状态：** 0119已验证0145～0148的新启动与consumer架构，mode2/3的USBIN
和三键并行通过；mode4又实测三次独立MSOC命中与73/72/71%刷新，电量事件
正常路径已通过。CHGR/SOC_READY变化事件尚未覆盖，CHGR state7/charging0
不是实际充电证据。下一步同内核充电诊断，不重跑已跳过的no-keys。
完整历史、镜像和观测判据见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。
PMIC 型号证据、事件位置、既有 APID 预检和待测项见
[`BATTERY-IRQ-HARDWARE-BASELINE.md`](BATTERY-IRQ-HARDWARE-BASELINE.md)。
工作区 `docs/INDEX.md` 的电源六章报告已补充 ack/unmask 顺序、LG 0.3%
MSOC delta 配置及 SRAM 事务依据；这些是源码参考，实际门限仍未知，但FG
channel接入与MSOC自然事件已有0119实测，无需为测门限访问SRAM。沿用固件现有
FG 状态验证事件，不为 IRQ 化重载 profile；
需要 SRAM 访问时单独设计授权、版本 workaround 与释放路径。

**目的：** 让插拔电源、充电状态和电量变化经 PMIC IRQ 触发状态刷新，继续通过
`IOPMPowerSource::updateStatus()` 通知 iOS，降低两秒定时采样带来的状态延迟。

**实施边界与顺序：**

1. 复用现有 `SweetSpmiInterruptMux` 与唯一 INTID 513 callback，以路由表新增
   battery consumer。按键 IRQ 保持启用；不单独注册共享 SPI，不写死功能 `switch`。
   明确 consumer 就绪、启动初始采样、路由 arm 及 stop/free 的顺序，防止回调
   访问未启动或已释放的电池实例。
2. 先接 USBIN_PLUGIN（SID2/peripheral 0x13/bit4）和 CHG_STATE_CHANGE
   （SID2/0x10/bit1），再核对并接入 FG MSOC_DELTA（SID2/0x40/bit3）；
   视需要接 SOC_READY。每个新外设先检查 EE0 ownership、APID、触发配置、
   latch/clear 和使能顺序，不重复读取已知危险的外部 EE channel。
3. GIC 上半部保持短小；mux workloop 完成源识别和确认，仅把事件交给电池
   consumer。电池在自己的 IOWorkLoop 上合并事件、读取硬件并发布，避免在
   mux worker 中进行整套电池诊断、长时间读取或 UI 操作。
4. IRQ 模式验收时停止现有两秒状态扫描。电压/电流等连续遥测若需低频定时
   采样，必须明确用途、周期和触发来源；不得用它静默补偿插拔、充电状态或
   MSOC IRQ 丢失。启动采样和有界事件后复查单独记录；IRQ 故障显式记录、
   屏蔽故障源，并说明是否影响共享按键路径，不能伪装为 IRQ 正常运行。
5. 本子目标只增加事件驱动和必要 IRQ 配置，不接管充电使能、限流、温控、
   电量计学习或完整 Apple manager ABI。IRQ 生效与真实充电分开取证。
   同一内核支持单源、组合及诊断参数测试镜像，减少云端编译次数；USB ACM
   继续作为正常日志通道，拔线或链路故障期间用持久 SWEETLOG 补充取证。

**验收条件：**

- 真实插入/拔出电源可见对应 route 命中、battery worker 处理、硬件状态读取
  和 IOPM 发布；充电状态变化也有独立证据，不能只看到 DWC3 IRQ 或图标变化。
- 电量确实跨过硬件配置的 MSOC delta 阈值时，由 FG IRQ 触发真实 gauge
  更新；记录阈值和事件行为，不用电压曲线或定时扫描冒充。短时间电量不变
  只能说明未覆盖此项，不能作为通过证据。
- 空闲期间没有两秒 PMIC 全量扫描；若保留低频遥测，日志能区分其来源。
- 与三键、USB ACM、触摸并发，含息屏/亮屏、重复插拔、启动时已插线与未插线，
  无确认失败、IRQ 风暴、键漏释放、死锁或功能停止。息屏测试指当前 display-off，
  不把它宣称为整机深度休眠唤醒已实现；既有 framebuffer 局部黑屏单独记录。
- 保存路由/worker/合并/错误计数、事件至发布耗时、boot args、产物 SHA256
  和原始日志；验收确认关键状态更新由硬件事件触发。

### 大目标一完成判定（追加电池后）

只有四个子目标均满足各自验收条件，且完成一次组合回归后，才视为此大目标完成。
组合回归至少覆盖：正常桌面启动、按键/组合键、触摸交互、USB CDC-ACM 日志、
插拔 USB 后的恢复、电池插电/充电状态与 MSOC 事件、display-off/唤醒，
以及持久日志中的 IRQ 计数和错误检查。
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

## 大目标三：优化日志结构与管理

**目的：** 建立统一的 Sweet 驱动日志结构、等级和管理方式。最终可以选择开启
哪些驱动或事件类别的日志（日志目标），并按等级过滤；在正常运行时控制输出量，
需要定位问题时再打开指定目标的详细记录。日志管理不能掩盖驱动故障。

**已有临时基础（0140，等待 CircleCI 与实机验证）：** 按键、DWC3 USB、
TouchComm、`SweetSpmiInterruptMux` 已改用 ERROR/WARN/INFO/DEBUG 前缀，
`sweetloglevel=0..3` 控制这四个驱动的全局输出上限，默认 INFO。高频细节在
DEBUG；节流后的硬件 IRQ 命中仍为 INFO。普通镜像和 DEBUG 镜像从同一内核
产物生成，持久 SWEETLOG 与 USB ACM 的字节流格式未改。
`sweetlogdump=1` 已识别新的 ERROR/WARN 前缀。具体范围和限制见
[`LOGGING-LEVELS.md`](LOGGING-LEVELS.md)。这只是过渡方案：其他 Sweet 驱动、
XNU/IOKit 与用户态输出尚未统一，当前也不能按驱动选择开关。

**后续设计方向（进入本目标时再细化子目标）：**

- 清点 Sweet 驱动及日志路径，为等级、目标/模块名、事件时间和必要上下文
  规定统一字段与调用接口；明确启动早期、IRQ 上半部及故障路径可用的接口。
- 为各日志目标配置独立的启用状态和等级阈值，并提供明确的正常、诊断等
  管理模式；配置方式须适合当前的 boot argument/临时镜像调试流程。
- 统一持久 SWEETLOG、USB 实时输出与控制台的过滤/保留规则。USB 本身异常时，
  ERROR 仍须能由持久日志取证；过滤策略不得静默丢弃所需的故障记录。
- 逐步迁移现有驱动，避免高频路径无条件格式化或输出；每次迁移核对日志
  量、启动/功能回归和提取工具兼容性。

**完成判定：** 可从一份内核产物通过配置分别开启指定目标及等级，默认模式
保留必要状态与故障而不过量输出；诊断模式能恢复目标的细节。各输出通道
对同一记录的等级和目标含义一致，故障场景可通过持久日志可靠追踪，
相关驱动和日志提取工具通过实机回归。

## 文档维护规则

- 每次阶段性实现、CI 结果、设备测试和回归结论都追加到 `NOTES.md`，明确区分观察
  与推测，并保留日志与 SHA256。
- 本文件只维护路线、边界和验收条件；已验证的硬件事实以 `NOTES.md` 为准。
- 不推送代码或文档；需要同步远端时明确告知由用户执行 push。
