# MH2LM 电池 IRQ 硬件基线与资料缺口

**日期：** 2026-11-05。当前电池仍是两秒只读采样，本文件不代表 IRQ 已实现。
实施范围和验收见 `IRQ-FRAMEBUFFER-PLAN.md` 子目标 4。

## PMIC 型号的证据等级

- **目标 DTS 明确：** 电池/充电相关 PMIC 为 Qualcomm **PM8150B**，SPMI
  USID 2。目标 Linux `pm8150b.dtsi` 声明 SMB5 充电器与 FG GEN4 电量计，
  MH2LM 自己的 `common-pm.dtsi` 也直接配置 `&pm8150b_charger`。
  同平台 DTS 还包括 PM8150、PM8150L；现有三键在 PM8150 SID0。
- **实机功能匹配：** SID2 的 MSOC、VBATT、USBIN、CHGR 和既往 IBATT 数据
  可读，且与插拔、真实充电行为吻合；现有 APID 预检与目标寄存器布局匹配。
- **尚未直接识别：** 当前 XNU 日志没有 REVID type/subtype/revision 的原始
  读数。因此可以按 PM8150B 准备驱动，不能声称已通过芯片 ID 探测确认具体
  硅版本，也不能把日志中的 PM6150/PM6150B 历史字符串当作本机型号。

Linux 的 REVID 定义提供了下一次只读识别依据：SID2 `0x0100..0x0103`
为 revision1..4，`0x0104` 为 type，`0x0105` 为 subtype。
`PMIC_TYPE=0x51`，`PM8150B_SUBTYPE=0x20` 是源码预期值，**不是实测值**。
访问 REVID 前仍须按已有规则解析其 EE/observer APID；不可为识别型号遍历读取
外部 EE channel。revision 对应的 workaround 需按实测版本选择。

## 已知中断来源

| 事件 | SID / peripheral / bit | PPID | 目标 DTS 触发 | 当前证据 |
| --- | --- | --- | --- | --- |
| USBIN_PLUGIN | 2 / 0x13 / 4 | 0x213 | 双边沿 | 0112m17 预检：EE0 IRQ-owned APID 269；外设 enable 与 arb access gate 均为 0；配置为 edge/high/low。 |
| CHG_STATE_CHANGE | 2 / 0x10 / 1 | 0x210 | 上升沿 | 同轮预检：EE0 IRQ-owned APID 266；两级 gate 均为 0；配置为 edge/high。 |
| MSOC_DELTA | 2 / 0x40 / 3 | 0x240 | 上升沿 | 目标 DTS 与 FG GEN4 Linux handler 已定位；本机 APID、ownership、gate、阈值及真实事件尚未核对。 |
| SOC_READY（候选） | 2 / 0x40 / 1 | 0x240 | 双边沿 | DTS 与现有 SOC_READY 读取已知；IRQ 配置与事件尚未验证。 |

前两行的 APID 是历史实测值；新增驱动仍运行时解析并核对 owner，不将数值
当作所有固件版本的固定映射。已有共享上游是 PMIC-arb v5 → PDC pin1 →
DT SPI481 / GIC INTID513，按键已经证明端到端投递；新增电池源要验证自己的
外设到 mux 路由，不能以按键成功或 GIC 软件 pending 代替。

## 开 gate 前还需要什么

1. **本机待测：** REVID；新 FG peripheral 的 APID/EE0/IRQ-owner、可访问
   channel；当前 type/polarity/enable/latch；MSOC delta 阈值及运行中的 FG
   是否在不新增算法配置时产生事件。前两条充电源的旧预检已有，不必重做
   无关的 GIC/USB 功能试验，但 arm 前仍应核对当前配置。
2. **源码语义需落实：** PMIC IRQ `RT_STS +0x10`、`SET_TYPE +0x11`、
   `POLARITY_HIGH/LOW +0x12/+0x13`、`LATCHED_CLR +0x14`、
   `EN_SET/CLR +0x15/+0x16`、`LATCHED_STS +0x18` 的位级行为；
   arb v5 channel `+0x100/+0x104/+0x108` 的 enable/status/clear 顺序。
   这些布局已有 Linux/按键实现可参考，新增源不能复制按键专属触发策略。
3. **实现后实测：** 外设事件 → mux route → 电池 workloop → IOPM 的完整
   链路、事件消退、快速连续变化、MSOC 跨阈值、与三键共享 SPI 时的并发及
   故障隔离。没有原理图或手册能替代这些运行结果。

## 用户可协助寻找的资料

现有目标 Linux 源码足以开展设计，不需要先等外部资料。若能找到以下资料，
可帮助核对硬件语义和 revision 差异：

- **PM8150B register reference / programming guide**，优先找 SMB5 CHGR
  `0x1000`、USBIN `0x1300` 和 FG GEN4 BATT_SOC `0x4000` 的 IRQ、
  latch clear、enable、MSOC delta 阈值与 revision errata；完整寄存器说明
  比只有封装/引脚信息的简要 datasheet 更有用。
- **SM8150 PMIC-arb v5 中断说明**：EE0 ownership、ACC_ENABLE、外设 latch
  与 arb channel clear/rearm 的关系，可与现有 Linux 实现交叉核对。
- **既有的本机 Android/UEFI PMIC revision 日志**，若已有备份，包含
  PM8150B 型号与 revision 的行即可帮助直接识别；无需为此另开 Android。

## 本地资料定位

均位于工作区 `ref/lge-lineage-kernel/`：

- `arch/arm64/boot/dts/qcom/pm8150b.dtsi`：USID/REVID、SMB5、FG GEN4 IRQ。
- `arch/arm64/boot/dts/qcom/sm8150-pmic-overlay.dtsi`：PMIC 组合与平台配置。
- `arch/arm64/boot/dts/lge/sm8150-mh2lm/sm8150-mh2lm_common/sm8150-mh2lm_common-pm.dtsi`：本机充电器覆盖配置。
- `drivers/platform/msm/qpnp-revid.c`、`include/linux/qpnp/qpnp-revid.h`：芯片识别与版本定义。
- `drivers/power/supply/qcom/qpnp-smb5.c`、`smb5-lib.c`、`smb5-reg.h`：充电事件及寄存器。
- `drivers/power/supply/qcom/qpnp-fg-gen4.c`、`fg-reg.h`：MSOC 事件、阈值与状态寄存器。
- `drivers/spmi/spmi-pmic-arb.c`：共享 IRQ、owner、ack 和 mask/unmask 语义。

原始预检与真实充电证据见 `NOTES.md` 的 0112m17、0112m19/m20；最新真实
采样恢复见 `live-0114a15-batt-irq-only.txt`。最新这份日志未启用 IRQ 预检，
不能把旧 gate=0 记录冒充为本次启动的读回。
