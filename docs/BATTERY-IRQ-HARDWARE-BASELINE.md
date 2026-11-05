# MH2LM 电池 IRQ 硬件基线与资料缺口

**日期：** 2026-11-05。已验证的 0114a15 电池仍是两秒只读采样；0141 已完成
本地 IRQ 实现，尚无新内核实机读回，下面的未知项不能视为已经测得。
实施范围和验收见 `IRQ-FRAMEBUFFER-PLAN.md` 子目标 4；新一轮测试入口见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。

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
2. **已有源码依据，0141 已实现、仍待实机验证：** PMIC IRQ `RT_STS +0x10`、`SET_TYPE +0x11`、
   `POLARITY_HIGH/LOW +0x12/+0x13`、`LATCHED_CLR +0x14`、
   `EN_SET/CLR +0x15/+0x16`、`LATCHED_STS +0x18` 的位级行为；
   arb v5 channel `+0x100/+0x104/+0x108` 的 enable/status/clear 顺序。
   这些布局已有 Linux/按键实现可参考，新增源不能复制按键专属触发策略。
3. **实现后实测：** 外设事件 → mux route → 电池 workloop → IOPM 的完整
   链路、事件消退、快速连续变化、MSOC 跨阈值、与三键共享 SPI 时的并发及
   故障隔离。没有原理图或手册能替代这些运行结果。

## 工作区电源资料已补齐的内容

已核对工作区根目录的 `docs/INDEX.md` 及相关章节，并针对本轮关键项回查
`ref/android_kernel_lge_sm8150/`，HEAD 为
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`。该目录是新增资料的主源；
它的 `pm8150b.dtsi` 与此前 `ref/lge-lineage-kernel/` 的该文件逐字节相同。
以下属于**源码事实**，不把它们升级为本机当前寄存器读回：

- `docs/charging-logic.md` §3.3：`qpnpint_irq_ack()` 先清 arb APID pending，
  再写该 peripheral `+0x14` 的事件 BIT；`qpnpint_irq_unmask()` 先开 arb
  access gate，若外设原来未使能，则清 latch 并置 enable。本轮已有准确
  参考分支，不再将“缺少 clear/enable 文档”列作开工阻塞。
- `docs/fuel-gauge-logic.md` §4.1：LG common 的 `fg-delta-soc-thr=3`
  是 0.3%，通用默认 5 是 0.5%。`DELTA_MSOC_THR` 位于十进制 SRAM
  word32:byte1，BSOC 是 word30:byte1；参数值 3/5 分别编码为 raw6/raw10。
  已回查参数宏、DT 覆盖及编码函数；**当前 XNU 继承的实际阈值仍未知**。
- 同章 §4.2：读 SRAM 也需 MEMIF 请求/grant/release，以及按硅版本选择
  workaround；它不是普通 observer 连续读。第一步先验证固件现有阈值能否
  产生 MSOC IRQ；若必须读取或改阈值，要单独设计 SRAM 事务，不为获取
  一个阈值直接照搬整个 FG probe、重载 profile 或 restart 算法。
- `docs/open-questions.md`：write ownership 与 IRQ ownership 分开核对，
  同 PPID 可有多个 APID；observer 可读不代表允许写 IRQ 配置。
  该静态报告没有本项目 XNU 运行上下文；其中“当前只读根因未知”等措辞
  不能覆盖这里已经确认的事实：电池 observer 有意只读，而按键 mux 已有
  经实机验证的 IRQ 写入/确认路径。

因此，本轮真正缺少的是 REVID、FG 当前路由/门限/状态及新增 consumer 的
端到端实测；不需要用户先补一份完整 PMIC 手册才能开始实现。

### 与 IRQ 分开跟踪的差异

`docs/hardware-facts.md` 和 `docs/devicetree-map.md` 记录目标 DTS 还配置
SMB1355 并联充电器与 P9222 无线接收器；这不是两者已在 XNU 驱动或实机
枚举成功的证据，本轮不顺带接管这些路径。

目标 mh2lm DTS 引用 BLT42 Tocad/LGC 两份 **4000mAh 命名的 profile**，
LG supplement `capacity-mah-min=3880`、`capacity-raw-full=247`。
目前 0114a15 `SweetBattery::publish()` 却固定使用 **5020 mAh**，SOC 则按
通用 MSOC 0..255 缩放。已回查两份 profile 和 XNU 最终源码，确有差异。
5020 是当前移植占位基准，不是本机电池容量的探测结果；但容量分子和分母
使用同一基准，不能单凭该差异断言百分比跳变由此造成。
真实电池厂商/ID、实际加载 profile、学习容量及 LG UI 满量程要另行验证，
不在电池 IRQ 补丁中盲改容量、重缩放 SOC 或重载 profile。

## 用户可协助寻找的资料

现有目标 Linux 源码和工作区上述六章资料已足以开展设计，不需要先等外部资料。若能找到以下资料，
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

本轮新增报告的阅读入口是工作区 `docs/INDEX.md`（不在 ios-mh2lm git
仓库内）；六章分别是 `hardware-facts.md`、`architecture.md`、
`charging-logic.md`、`fuel-gauge-logic.md`、`devicetree-map.md` 和
`open-questions.md`。其中的源码路径、commit 和静态/实测边界需要保留。

以下文件可在 `ref/android_kernel_lge_sm8150/` 查阅；此前预检使用的
`ref/lge-lineage-kernel/` 也保留作为历史参考：

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
