# MH2LM 硬件按键 IRQ 基线

**状态：** 仅完成资料和运行时观察的汇总。按键仍由 20 ms 轮询驱动；
`SweetSpmiInterruptMux` 尚未注册 GIC SPI 或写入任何 PMIC/PDC 中断寄存器。
**更新日期：** 2026-11-03
**目的：** 记录将 Power、Volume Down、Volume Up 转为 IRQ 模式所需的硬件事实、
未知项和验证顺序，防止把其他设备、Linux irqdomain 或推测的寄存器值带入 XNU。

## 读法和边界

状态标记：

- **已确认**：目标 MH2LM DTS、真实设备日志或当前 XNU 源码直接证明。
- **架构/源码确认**：ARM GIC 架构、目标 Linux 驱动或本项目 `ArmCpu` 实现直接证明；
  仍可能需要在本机上验证配置是否已经由固件接管。
- **待实机预检**：已知应读取什么，但尚不可据此写寄存器或 arm IRQ。
- **无关**：不是硬件按键 IRQ 链路的一部分；可为后续触摸等目标保留资料。

本轮的物理路径是：

```text
Power / Volume Down: PM8150 PON (SID 0, peripheral 0x08)
Volume Up:           PM8150 GPIO6 (SID 0, peripheral 0xc5)
                         -> PMIC-arb v5 / PDC
                         -> GIC SPI 481 (DT 编号) = INTID 513 (XNU 编号)
                         -> 一个 XNU GIC callback
                         -> SweetSpmiInterruptMux 路由表
                         -> 按键 worker / 既有 sweet_hid_key()
```

三个来源共用 PMIC-arb 的上游 SPI；因此它只能有一个
`armcpu_spi_attach(513, ...)` 所有者。按键、充电和未来 PMIC consumer 必须经 mux
分发，不能分别直接绑定该 SPI。

## 一览：按键来源与已知 IRQ 位

| 逻辑按键 | PMIC 来源 | DTS 中断说明符 | APID | 现有实时状态 | 状态 |
|---|---|---|---:|---|---|
| Power | PM8150 PON，SID 0，PPID `0x0008` | `<0x0 0x08 0x0 IRQ_TYPE_NONE>`，bit 0 | 248 | `RT_STS 0x0810` bit 0：松开 0、按下 1 | 已确认 |
| Volume Down | 同一 PON，SID 0，PPID `0x0008` | `<0x0 0x08 0x1 IRQ_TYPE_NONE>`，bit 1 | 248 | `RT_STS 0x0810` bit 1：松开 0、按下 1 | 已确认 |
| Volume Up | PM8150 GPIO6，SID 0，PPID `0x00c5` | `<0x0 0xc5 0x0 IRQ_TYPE_NONE>`，bit 0 | 261 | `RT_STS 0xc510` bit 0：松开 1、按下 0 | 已确认 |

APID 和电平来自 `sweetlog-0113-baseline.bin` 的运行时启动/按键记录；上表 IRQ 位来自
目标 `pm8150.dtsi`。这里的 PMIC bit 是 **外设内的 0--7 位**，不是 GIC INTID。

## SoC / GIC 通用信息

### GIC 版本、映射和关键寄存器

| 项目 | 值 | 状态 | 与按键 IRQ 的关系 |
|---|---|---|---|
| GIC | ARM GICv3 | 已确认 | 直接相关 |
| GICD | `0x17a00000`, `0x10000` | 已确认 | 上游 SPI 513 配置所在 |
| GICR | `0x17a60000`, `0x100000` | 已确认 | CPU 初始化相关；不是 PMIC 源路由本身 |
| GICR stride | `0x20000`，共 8 个 frame | 已确认 | 同上 |
| `GICD_CTLR` | `+0x000` | 架构/源码确认 | `RWP` 等待；`ArmCpu` 重配置 SPI 时使用 |
| `GICD_TYPER` | `+0x004` | 架构/源码确认 | 确认可用 INTID 上界 |
| `GICD_IGROUPR[n]` | `+0x080 + 4*n` | 架构/源码确认 | 将 SPI 置为 Group 1 |
| `GICD_ISENABLER[n]` | `+0x100 + 4*n` | 架构/源码确认 | 使能 SPI |
| `GICD_ICENABLER[n]` | `+0x180 + 4*n` | 架构/源码确认 | 配置前关闭 / 未处理时关闭 |
| `GICD_IPRIORITYR[intid]` | `+0x400 + intid` | 架构/源码确认 | 当前 helper 设为 `0xa0` |
| `GICD_ICFGR[n]` | `+0xc00 + 4*(intid/16)` | 架构/源码确认 | edge/level 配置 |
| `GICD_IROUTER[intid]` | `+0x6000 + 8*intid` | 架构/源码确认 | 路由至 boot CPU |
| `GICD_PIDR2` | `+0xffe8` | 架构/源码确认 | `ArmCpu` 以 architecture field 3 验证 GICv3 |

`ArmCpu::dispatch()` 在读取 `ICC_IAR1_EL1` 后、写 `ICC_EOIR1_EL1` 前调用 SPI
callback。因此上半部必须短小，只记录/确认 pending 并调度 worker；不得在这里执行
SPMI observer、HID、UI 或 IOKit 工作。

### INTID 分配规则及本项目映射

| 范围/说明符 | 含义 | 本项目结论 | 状态 |
|---|---|---|---|
| INTID 0--15 | SGI | 不用于按键 | 架构确认，无关 |
| INTID 16--31 | PPI | 不用于按键；已验证 timer 使用 18/19 | 架构确认，无关 |
| INTID 32--1019 | SPI | PMIC-arb 位于此范围 | 架构确认 |
| DTS `<GIC_SPI N flags>` | `N` 是相对 SPI 号，XNU INTID 为 `32 + N` | `481 -> 513` | 已确认 |
| PMIC-arb 输出 | DTS `interrupts = <GIC_SPI 481 IRQ_TYPE_NONE>` | mux 的唯一候选上游是 INTID 513 | 已确认 |
| GIC trigger | `IRQ_TYPE_NONE` 不表达 edge/level | `armcpu_spi_attach(513, false, ...)` 是当前 level 候选，不得在 0128 前视为已验收 | 待实机预检 |

当前 `armcpu_spi_attach()` 仅接受实际 INTID、拒绝重复注册、最多有 16 个自定义 SPI
slot，并将 callback 置于 EOI 前。这些约束是采用单一 PMIC mux 的直接原因。

### 已知 SoC 内置 IP 中断号

| IP / 事件 | 中断号 | 与按键 IRQ 的关系 |
|---|---:|---|
| PMIC-arb peripheral IRQ | DTS SPI 481，XNU INTID 513 | **直接相关** |
| GICT fault / error | DTS SPI 46 / 17 | 无关；`gict@17a20000` 是错误报告块，不是主中断控制器 |
| physical / virtual timer | PPI 对应 INTID 18 / 19 | 无关；仅证明 GIC Group 1 IRQ 工作 |
| DWC3 | 独立控制器 SPI，编号本表未收集 | 无关；属于子目标 2，不可借用 513 |

## DTB / DTS 所给出的板级连接

### 按键引脚和 pinctrl

| 项目 | DTS 事实 | 状态 | 备注 |
|---|---|---|---|
| Power / Volume Down | `qcom,power-on@800` 的 `kpdpwr` / `resin` | 已确认 | 不是 SoC GPIO，也没有 TLMM pinctrl |
| Volume Up GPIO | `gpios = <&pm8150_gpios 6 GPIO_ACTIVE_LOW>` | 已确认 | 是 PM8150 GPIO6，不是 SoC TLMM GPIO6 |
| Volume Up pinctrl | `key_vol_up_default`：`pins = "gpio6"`、`function = "normal"`、`input-enable`、`bias-pull-up`、`power-source = <1>` | 已确认 | 已由 Linux 描述；现有 XNU 轮询输入也正常 |
| debounce / wake | `debounce-interval = <15>`、`gpio-key,wakeup`、`linux,can-disable` | 已确认 | 未来 IRQ worker 需保留等效去抖/唤醒语义 |
| PON 配置 | `qcom,pon-dbc-delay = <15625>`、`kpdpwr-sw-debounce`，并配置 KPDPWR/RESIN | 已确认 | PON 可能由 PMIC 固件和 Android 留下的配置参与，不能盲改 |
| PDC input / mapping | PDC 基址 `0x0b220000`，大小 `0x400` | 已确认其存在 | 哪个 PDC pin 对应按键源、是否需软件写入，均未知 |

### GPIO 控制器寄存器：哪些需要、哪些不需要

| 控制器 | 已知映射/状态 | 与按键 IRQ 的关系 |
|---|---|---|
| PM8150 SPMI GPIO controller | base `0xc000`，范围 `0xa00`；GPIO6 外设 `0xc500` | 直接相关。当前日志证明其 mode `0`、enable `0x80`、subtype `0x11`、input usable。完整 GPIO pad 寄存器表暂未抽取，因为输入已正常，IRQ 首先需要验证的是通用 PMIC interrupt block。 |
| SoC TLMM GPIO controller | 未在本表展开 | **无关**：三个硬件按键都不走 TLMM。它会在触摸 IRQ 子目标中变为直接相关。 |

## 外设寄存器与 PMIC-arb 路由

### PMIC 通用中断寄存器（PON `0x0800`、GPIO6 `0xc500`）

这些偏移来自目标 Linux `spmi-pmic-arb` 驱动的 QPNP interrupt 实现；单个位的含义
由上面的 DTS tuple 决定。

| 外设基址偏移 | 名称 | 含义 | 按键 IRQ 的状态 |
|---:|---|---|---|
| `+0x10` | `RT_STS` | 实时输入电平；不等于 latched pending | 已确认可读，且三个按键电平行为已由实机日志验证 |
| `+0x11` | `SET_TYPE` | bit=1 为 edge，0 为 level | 待 0128 采样；不得假定 |
| `+0x12` | `POLARITY_HIGH` | 高极性选择 | 待 0128 采样 |
| `+0x13` | `POLARITY_LOW` | 低极性选择 | 待 0128 采样 |
| `+0x14` | `LATCHED_CLR` | 向各 IRQ bit 写 1 清该外设 latch | 清除机制的外设一半已源码确认；在本机的完整 ack 顺序仍待验证 |
| `+0x15` | `EN_SET` | 向各 IRQ bit 写 1 使能 | 位号已由 DTS 确认，现有 gate 状态待 0128 读取 |
| `+0x16` | `EN_CLR` | 向各 IRQ bit 写 1 关闭 | 架构/源码确认；当前不应写入 |
| `+0x18` | `LATCHED_STS` | latched pending 状态 | 待 0128 读取并在按下/释放时对照 |

0128 的日志字段名 `latched=` 读取的是 `RT_STS + 0x08 = +0x18`，即
`LATCHED_STS`；这是一项只读状态采样，不是对 `LATCHED_CLR (+0x14)` 的读取或写入。
文档故意区分二者，避免把状态寄存器误作清除地址。

### 按键位和状态位含义

| 外设 | IRQ bit mask | RT_STS mask | 已观察到的值 | 结论 |
|---|---:|---:|---|---|
| PON KPDPWR / Power | `0x01` | `0x01` | idle `0x04`，按下 `0x05` | bit 0 为 active-high 按键电平 |
| PON RESIN / Volume Down | `0x02` | `0x02` | idle `0x04`，按下 `0x06` | bit 1 为 active-high 按键电平 |
| GPIO6 / Volume Up | `0x01` | `0x01` | idle `0x01`，按下 `0x00` | bit 0 为 active-low 按键电平 |

`RT_STS` 的其余置位 bit 不是该按键的事件 bit，不能用作 mux 路由或清除掩码。

### PMIC-arb v5 相关映射

| 区域 | 物理地址 / 步长 | 已知寄存器 | 状态 |
|---|---|---|---|
| core | `0x0c440000` | `APID_MAP = +0x900 + 4*apid` | 已确认；APID map bit 24 表示 IRQ owner |
| cnfg | `0x0c40a000` | owner = `+0x700 + 4*apid` | 已确认；读取 write owner / EE |
| intr | `0x0e700000` | `4*(apid >> 5)` 的 owner/pending bitmap | 已确认其读取用途；bit 值需按各 source 日志确认 |
| chnls | `0x0c600000`，`0x10000*apid` | `ACC_ENABLE +0x100`、`IRQ_STATUS +0x104` | 已确认可做只读预检 |
| observer | `0x0e600000`，`0x80*apid` | 既有单字节 SPMI observer read | 已确认；不可作为 primary IRQ context 操作 |
| channel IRQ clear | 候选 `+0x108` | 需要写 bit 清 PMIC-arb pending 的精确 v5 证据 | **未知，禁止写** |

当前运行时已确认 `PMIC-arb version=0x50000000`、APID count=512；PON 为 APID 248，
GPIO6 为 APID 261。Linux 通用流程会先清 arbiter pending、再向外设
`LATCHED_CLR` 写对应 bit；但目标 v5 channel clear 的偏移、写入宽度和 PDC/上游
重新触发顺序尚未以 MH2LM 实机验证，不能直接搬用。

## 启用与确认：已知流程和仍缺的证据

目标 Linux 驱动显示的通用外设流程是：

1. 取得 APID 和 IRQ owner；设置 PMIC-arb channel 的 access enable。
2. 若 PMIC 外设 bit 当前未开启，先写 `LATCHED_CLR(bit)`，再写 `EN_SET(bit)`，避免
   历史 latch 在 enable 后立即触发。
3. IRQ 到达时，清 PMIC-arb 的该 APID pending，再写外设 `LATCHED_CLR(bit)`。
4. 由 `RT_STS` 或 worker 读出的当前电平决定按下/释放；不在 GIC 上半部发布 HID。

这只是 **候选语义**，不是本项目现在允许执行的写序列。实现前必须补齐：

| 待测项目 | 为什么缺它 | 满足条件 |
|---|---|---|
| PON/GPIO6 的 `SET_TYPE`、极性、`EN_SET`、`LATCHED_STS` | 证实 Android/固件留在硬件中的实际配置与 DTS 位号一致 | 0128 在 idle、单键按下、松开三组采样均一致 |
| APID owner、IRQ owner bit、intr owner bitmap、`ACC_ENABLE`、`IRQ_STATUS` | 证实 XNU 所在 EE 有资格接收两条 source | 0128 对 PON 和 GPIO6 均显示可路由，且无误写 |
| PMIC-arb v5 clear | 不清 pending 将造成上游 level 重入/风暴 | 先定位 v5 定义或只读证据，再加入有计数器保护的单 source 写测试 |
| PDC pin/预配置 | 某些 QCOM 路线需 PDC 才能从 PMIC-arb 送达 GIC | 证明现有固件已配置，或确认目标 pin/寄存器后才写 |
| GIC 上游 trigger | DTS `IRQ_TYPE_NONE` 不指定 edge/level | GIC callback 完成受控单按键试验且无重复 / 未处理 disable |

## 下一次实机测试：0130 PON route 微阶段只读预检

0114a2 已经验证：`sweetkeysirqdiag=1`（helper init）可以进入 iOS，而
`sweetkeysirqdiag=2`（PON 完整 route）直接回到 LG BSP。成功日志中的按键轮询已
找到 PON `APID 248`，所以问题不在普通按键访问或 helper 初始化。0130 继续把 PON
route 拆成单独的只读部分；它仍然不写 PDC、PMIC 或 GIC：

```text
mask 32   = PON route metadata：APID map + write owner
mask 64   = PON intr owner bitmap
mask 128  = PON channel ACC_ENABLE
mask 256  = PON channel IRQ_STATUS
```

每个镜像均使用 `sweetkeys=1 sweetusb=0`，继续保留 20 ms 轮询。依次测试 32、64、
128、256；某一步回到 LG BSP 时立即停止。各成功阶段导出持久日志并提取：

```text
SWEET SPMI IRQ: keys-pon route-parts=...
```

0130 的四项全部通过后，才回到 0129 的 `mask 2` 完整 PON route，再继续 GPIO6、
PON/GPIO6 外设中断字节和最终 `mask 31`。只有这些证据齐全后，才实施 mux 的第一
个可写、单 source Power IRQ 测试，并始终保留可选的轮询回退。

## 证据索引

- 目标 SoC / PMIC DTS：`ref/lge-lineage-kernel/arch/arm64/boot/dts/qcom/sm8150.dtsi`、
  `qcom/pm8150.dtsi`、`qcom/sm8150-pmic-overlay.dtsi`。
- MH2LM 板级 DTS：`arch/arm64/boot/dts/lge/sm8150-mh2lm/.../sm8150-mh2lm_common-misc.dtsi`
  与 `sm8150_common/sm8150_common-pm.dtsi`。
- Linux QPNP/PMIC-arb 语义：`drivers/spmi/spmi-pmic-arb.c`。
- XNU GIC helper：`ref/UniXNU-src/drivers/ArmCpu/ArmCpu.cpp`。
- 已验证的按键日志：`logs/sweetlog-0113-baseline.bin`，以及同轮 battery/USB 日志。
- 只读 route-inspection 基础接口：`xnu/patches/0128-mh2lm-spmi-mux-key-preflight.patch`；
  当前分阶段预检：`xnu/patches/0129-mh2lm-key-irq-diag-stages.patch`；PON 微阶段：
  `xnu/patches/0130-mh2lm-pon-route-microdiag.patch`。
