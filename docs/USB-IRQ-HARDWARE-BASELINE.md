# MH2LM DWC3 USB IRQ 硬件基线

**状态（2026-11-04）：** 0136 的 INTID 165 软件 pending 探针、真实
DWC3 事件 IRQ 和 USB＋三键 IRQ 并行均通过 0114a11 手机日志验证。
CDC-ACM 实时输出在并行模式下持续工作。反复拔插后的旧有 gadget
重连故障另列待修；证据与镜像 SHA256 见 `NOTES.md`。

## 已知、可直接用于实现

| 项目 | 值或机制 | 证据与范围 |
| --- | --- | --- |
| DWC3 MMIO | `0x0a600000` | MH2LM `sweet_dev.h` 与当前 USB 驱动已实际使用 |
| DWC3 core 事件 IRQ | DTS `dwc3@a600000` 的 GIC SPI 133，即 INTID 165 | `sm8150-usb.dtsi` 子节点；本机投递待测 |
| USB wrapper IRQ | `pwr_event_irq` 等为 wrapper/PHY 的独立源 | 不是本次 event buffer IRQ；不可将 SPI 130 当成 SPI 133 |
| GIC | GICv3 distributor `0x17a00000`；Group 1 投递框架已由按键 INTID 513 验证 | GIC 版本/布局不需重做通用摸底；SPI 165 本身仍待测 |
| event buffer 0 | `GEVNTADRLO/HI` `+0xc400/+0xc404`，`GEVNTSIZ` `+0xc408`，`GEVNTCOUNT` `+0xc40c` | 当前 `sweet_usb.c` 与 Linux DWC3 驱动一致 |
| 当前事件路径 | 4 KiB event buffer；`GEVNTSIZ=0x80001000` 的 bit 31 (`INTMASK`) 屏蔽 IRQ；`ud_thread` 每 1 ms 读取 count、同步 DMA、处理事件并回写 count | 0114a10 USB 日志和当前 buildtree；DMA 地址每次启动可能不同，不应写死 |
| 确认机制 | 消费 event buffer 后向 `GEVNTCOUNT` 写入已处理的字节数 | 当前轮询驱动和 Linux `dwc3`；IRQ 路径仍要验证清除/重新使能顺序 |
| 既有功能 | CDC-ACM 枚举、DTR、持续 TX、断线重连已通过 0112m/0113/0114a10 实机测试 | 它是 IRQ 改造的工作基线，不需重新证明 USB 本身能输出日志 |

目标 DTS 中 DWC3 IRQ 写作 `<0 133 0>`，未在该单元显式指定 edge/level。
DWC3 event buffer 通常在未确认事件时持续请求中断；新驱动应显式配置
GIC SPI 165 并通过真实事件验证，不能只靠 DTS 的 flags 或 IGROUPR 读回
断言触发方式。按键的 SPI 513 Group 位在非安全视图读回 0，但真实投递成功，
所以 SPI 165 也应以投递结果作为判据。

## 下一轮需要实测的内容

1. **GIC 入口：** 在保持 DWC3 `INTMASK=1` 时，对 INTID 165 做有界软件
   pending 测试；检查 XNU IRQ callback 是否恰好接收、pending/active 是否清零。
   这验证 GIC 编号、路由和本机可投递性，不依赖 USB 链路。
2. **DWC3 真实事件：** 仅在 callback、worker 和轮询回退已就绪后，清除
   `GEVNTSIZ.INTMASK`。通过枚举、bus reset、EP 完成等现有事件确认
   INTID 165 真实命中，worker 消费 event buffer、回写 `GEVNTCOUNT`，
   中断随后消退并能再次触发。记录 IRQ 次数、处理字节数、空 IRQ、积压、
   超时和回退原因，以区分线未连通、确认顺序错误及中断风暴。
3. **并发与回退：** CDC-ACM 持续输出、DTR、断线重连和按键
   `sweetkeysirq=3` 并行时无卡死或丢事件。USB IRQ 出错时切回当前轮询
   路径；USB 尚未枚举或自身失效时用持久 SWEETLOG 取证。

以上可放进**一次云端内核编译**，用启动参数选择现有轮询、GIC 投递探针、
真实 USB IRQ 与 USB+按键 IRQ 配置。软件 pending 与真实事件的先后 gate
由同一驱动控制，无需先刷只读寄存器镜像。

0136 的启动参数为 `sweetusbirq=0`（默认，原 1 ms 轮询）、`1`（仅 GIC
软件 pending 探针，DWC3 仍屏蔽并轮询）、`2`（探针通过后开放真实 IRQ）。
IRQ 上半部只屏蔽 SPI 165 并唤醒事件线程；线程读取 event buffer、确认
`GEVNTCOUNT`、重新使能 SPI。每 200 ms 做一次恢复检查；连续 8 次只由
恢复检查发现事件，或 16 次空 IRQ 唤醒，则屏蔽 DWC3 IRQ 并恢复原
1 ms 轮询。探针失败也保持原轮询。日志中的 `software-pending probe hits=1`
只证明 GIC 软件路径；`SWEET USB IRQ: hit` 且有真实事件才证明设备路径。

CircleCI 产物下载后运行
`tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-<编号>.gz <编号> --usb-irq`。
同一内核会生成 `usb-irq-gic`、`usb-irq`、`usb-irq-keys` 三张额外镜像。
按这个顺序用 `fastboot boot` 测，若前一阶段无法进桌面、探针失败、
日志无真实命中或 CDC-ACM 中断/按键异常，则先停止后续阶段并导出
本轮持久 SWEETLOG。只在需要定位问题时再刷该内核的 `usb` 轮询镜像。

0114a11 实测已证明软件 pending 和真实 DWC3 IRQ 都能到达、处理事件并
重新使能，计数 `empty=0 missed=0`，没有回退。反复拔插后 Windows
偶发报告未识别设备，用户确认旧轮询驱动也有相同问题；将其记作独立的
USB gadget 重连缺陷。并行 IRQ 测试保持线缆连接，观察 USB IRQ 与
`SWEET MUX: hit route=` 同时持续出现，已在 0114a11 日志验证：
USB IRQ 至少 512 次、实时 CDC-ACM TX 超过 214 次、三键各有真实
IRQ 与 HID 按下/松开，均无回退。一次音量上松开时 PMIC PON 读取
失败，但 GPIO 状态与 HID 松开正常，见 `NOTES.md`。后续单独处理
重连状态清理。

## 当前无需单独测试

- DWC3 是 SoC 内置 IP，不经过外部 GPIO 或 pinctrl；不需要寻找“USB IRQ 引脚”。
- PMIC APID、PDC pin 1、SweetSpmiInterruptMux 路由表属于按键/PMIC 共享源，
  不用于 DWC3 event buffer IRQ。
- GICv3 寄存器布局、通用 INTID 分配规则和 CDC-ACM 基本可用性已有证据；
  本阶段只验证 **SPI 165 这一路**的投递和 DWC3 清除顺序。

## 源码索引

- 目标 SoC：`ref/lge-lineage-kernel/arch/arm64/boot/dts/qcom/sm8150-usb.dtsi`。
- MH2LM USB 覆盖：`arch/arm64/boot/dts/lge/sm8150-mh2lm/sm8150-mh2lm_common/sm8150-mh2lm_common-usb.dtsi`。
- Linux DWC3：`drivers/usb/dwc3/core.c`、`gadget.c`、`core.h`。
- 当前 XNU：0114a10 buildtree 中的 `xnu/bsd/dev/sweet_usb.c`，相关补丁
  `xnu/patches/0112-mh2lm-usb-cdc-acm.patch`、`0113-mh2lm-usb-cdc-acm-tx.patch`。
