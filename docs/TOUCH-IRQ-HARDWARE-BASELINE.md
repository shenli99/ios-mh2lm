# MH2LM TouchComm 触摸 IRQ 硬件基线

**状态（2026-11-04）：** 0114a11 实机确认 Synaptics TouchComm 触摸驱动工作，
当前用 10 ms ATTN 轮询。0137 已加入可选 IRQ 路径；0114a13 构建产物
包含该路径，测试镜像已打包。**TLMM 汇总 SPI 208／INTID 240 尚未经过
实机投递验证**。

## 已确认的链路

| 环节 | 值与证据 |
| --- | --- |
| 触摸控制器 | 当前 `sweet_touch_tcm.c` 的 TouchComm 驱动，I2C 地址 `0x20`；0114a11 日志有 `TouchComm ready`、触摸报告和零 I2C 错误 |
| ATTN 引脚 | TLMM GPIO122，低电平有效；LG MH2LM 触摸 DTS 用 `interrupt-parent=<&tlmm>`、`interrupts=<122 0x2008>`，Windows ACPI 也指定 GPIO122 低电平 |
| 当前处理 | 驱动每 10 ms 读 GPIO122 电平，低电平时读 TouchComm；每第 10 次还强制读一次。TouchComm 消息读取和 HID 发布已经工作 |
| TLMM 寄存器 | SM8150 GPIO122 位于 South tile：基址 `0x03d00000`、pin offset `0x7a000`；IO `0x03d7a004`，中断配置 `0x03d7a008`，状态 `0x03d7a00c`。当前驱动已能访问该 tile 的 GPIO 配置和电平 |
| TLMM 配置语义 | Linux `pinctrl-sm8150.c`：enable bit 0、polarity bit 1、detection bits 2–3、raw-status bit 4、KPSS target bits 5–7 值 3；level-low 使 polarity/detection 均为 0。`pinctrl-msm.c` 对此 pin 的状态 bit 0 用写 0 清除 |
| 上游候选 | `sm8150-pinctrl.dtsi` 给 TLMM 汇总中断 `<0 208 0>`，即 GIC SPI 208／INTID 240；GPIO122 不在 `sm8150_dir_conn[]` 静态直连列表，当前 SoC data 也没有 GPIO-to-PDC mux input 表 |

因此目标链路是 **TouchComm ATTN → TLMM GPIO122 状态 → TLMM 汇总
SPI 208 → XNU INTID 240 → 触摸线程 → HID**。SPI 208 是 TLMM 汇总源，
不属于按键的 PMIC-arb SPI 481，也不属于 DWC3 SPI 133。未来若有其他
TLMM GPIO 消费者，应由汇总源分发多个 pin；本阶段只使能 GPIO122。

## 0137 分阶段实现与实测判据

- `sweettouchirq=0`（默认）：保留当前 10 ms ATTN 轮询及每 100 ms
  强制读取。
- `sweettouchirq=1`：保持 GPIO122 中断关闭，对 INTID 240 做有界
  GIC 软件 pending 探针。要求日志出现 `hits=1 clear=1 pending=0 active=0`；
  这只证明 GIC 入口，不证明 TLMM 到 GIC 的真实线路。
- `sweettouchirq=2`：探针通过后配置 GPIO122 为 KPSS、level-low，清旧
  状态，再使能 GIC 和引脚中断。GIC 上半部只屏蔽 SPI 并唤醒触摸线程；
  线程读取 TouchComm、清 TLMM 状态、重新使能 SPI。空闲时每 200 ms
  只检查 ATTN 电平，不做周期性 I2C 强制读取。连续漏 IRQ、空 IRQ、
  I2C 错误或 ATTN 低电平无报告进展时，屏蔽中断并恢复 10 ms 轮询。

同一云端内核可用 `tools/mk-mh2lm-event-matrix.sh` 的 `--touch-irq`
生成 GIC 探针、触摸真实 IRQ、触摸＋USB＋按键 IRQ 三张镜像。按顺序
`fastboot boot`，只在前一阶段通过后测下一阶段。真实触摸测试覆盖
单点、多点、拖动、锁屏及息屏唤醒后的第一次触摸；组合镜像还应有
USB 实时日志、三键 IRQ 和触摸 IRQ 同时活动的证据。

## 尚须实机确认

- INTID 240 在 MH2LM 上的软件 pending 和 GPIO122 真实触发能否投递。
- TLMM 状态清除后，低电平消退与 SPI 重使能是否会重复触发或丢报告。
- 触摸连续拖动与多指时，IRQ 频率、I2C 耗时和 200 ms 恢复检查是否足够。
- 共享 TLMM 汇总 SPI 是否有其他目前未启用的 GPIO 源；若出现无关
  中断，不能把所有 SPI 240 命中都计作触摸报告。

源码：`xnu/bsd/dev/sweet_touch_tcm.c`、`xnu/osfmk/arm64/sleh.c`、
`ref/lge-lineage-kernel/drivers/pinctrl/qcom/pinctrl-sm8150.c`、
`pinctrl-msm.c`、`sm8150-pinctrl.dtsi` 和 MH2LM touch DTS。
