# MH2LM 电池 IRQ 硬件基线与资料缺口

**日期：** 2026-11-05。0115 mode1 持久日志已补齐只读预检：PM8150B 身份、
FG APID/owner 与外设配置现在已有实机读回。该模式仍是两秒采样，未开启电池
IRQ；FG channel 访问、MSOC 门限及真实事件仍待测试，不能视为已经测得。
随后 0115 mode2 在 mux 启动前耗尽十秒等待，电池路由未接入；这是软件
启动依赖错误，不把该轮失败归因于 IRQ 极性或硬件线路。0142 本地修正待重测。
实施范围和验收见 `IRQ-FRAMEBUFFER-PLAN.md` 子目标 4；新一轮测试入口见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。

## PMIC 型号的证据等级

- **目标 DTS 明确：** 电池/充电相关 PMIC 为 Qualcomm **PM8150B**，SPMI
  USID 2。目标 Linux `pm8150b.dtsi` 声明 SMB5 充电器与 FG GEN4 电量计，
  MH2LM 自己的 `common-pm.dtsi` 也直接配置 `&pm8150b_charger`。
  同平台 DTS 还包括 PM8150、PM8150L；现有三键在 PM8150 SID0。
- **实机功能匹配：** SID2 的 MSOC、VBATT、USBIN、CHGR 和既往 IBATT 数据
  可读，且与插拔、真实充电行为吻合；现有 APID 预检与目标寄存器布局匹配。
- **0115 实机直接识别：** `sweetlog-0115-battery-irq-preflight.bin` 中
  REVID revision1..4=`0/0/0/0x2`、type=`0x51`、subtype=`0x20`；
  FG SOC subtype=`0x10`，两项读取均成功，`pm8150b=1`。
  `selected mode=1 preflight=1` 明确通过身份和 FG 配置检查。
  原始 revision 字节保留，不未经对应表就命名硅版本；日志中 PM6150/PM6150B
  的历史字符串仍不能用于识别本机型号。

Linux 的 REVID 定义提供只读识别依据：SID2 `0x0100..0x0103`
为 revision1..4，`0x0104` 为 type，`0x0105` 为 subtype。
`PMIC_TYPE=0x51`，`PM8150B_SUBTYPE=0x20` 的源码预期已由上述本机读回确认。
访问 REVID 前仍须按已有规则解析其 EE/observer APID；不可为识别型号遍历读取
外部 EE channel。revision 对应的 workaround 需按实测版本选择。

## 已知中断来源

| 事件 | SID / peripheral / bit | PPID | 目标 DTS 触发 | 当前证据 |
| --- | --- | --- | --- | --- |
| USBIN_PLUGIN | 2 / 0x13 / 4 | 0x213 | 双边沿 | 0115 复核：EE0 IRQ-owned APID 269；外设 enable、arb access gate、latch/status 均 0；rt=1，edge/high/low 均 1，read-ok=1。 |
| CHG_STATE_CHANGE | 2 / 0x10 / 1 | 0x210 | 上升沿 | 0115 复核：EE0 IRQ-owned APID 266；两级 gate、latch/status 均 0；rt=0，edge/high=1、low=0，read-ok=1。 |
| MSOC_DELTA | 2 / 0x40 / 3 | 0x240 | 上升沿 | 0115 元数据确认 EE0 IRQ-owned APID 283；外设 edge/high/low/enable/latch 原始字节均 0；channel gate/status、SRAM 阈值及真实投递待测。 |
| SOC_READY | 2 / 0x40 / 1 | 0x240 | 双边沿 | 同一 APID 283；外设 RT=0x2 表示 ready，IRQ 配置字节均 0。0141 mode4 会配置双边沿；真实投递待测。 |

上述 APID 是本次实测值；新增驱动仍运行时解析并核对 owner，不将数值
当作所有固件版本的固定映射。已有共享上游是 PMIC-arb v5 → PDC pin1 →
DT SPI481 / GIC INTID513，按键已经证明端到端投递；新增电池源要验证自己的
外设到 mux 路由，不能以按键成功或 GIC 软件 pending 代替。

## 开 gate 前还需要什么

1. **本机待测：** FG APID 283 的实际 channel 访问/gate/status、MSOC delta
   阈值及运行中的 FG 是否在不新增算法配置时产生事件。REVID、FG 元数据
   和当前外设 IRQ 字节已测得，不再列为未知。USBIN/CHGR 本次只读配置也已
   复核，下一步进入 mode2 USBIN，arm 前驱动仍自动核对当次状态。
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

因此，当前缺少的是 FG channel/门限及新增 consumer 的端到端实测；
不需要用户先补一份完整 PMIC 手册才能继续验证。

### 0115 预检记录的读取边界

原始持久日志 SHA256 为
`ff7c67cba070256e558e5272f5e6eb0e1ceced9756c4c58a53e8a18265c5fc92`，
解码后工作区 `scratch/battery-irq-0115/preflight-persistent.txt` 的
78/79 行为 USBIN/CHGR、106 行为 REVID、114/116/117 行为 FG 和预检结果。
ring 未绕回，启动记录完整。FG 使用 `route-parts=0x3`，只读取 META/OWNER；
该行打印的 `acc-enable=0 irq-status=0` 是未读取字段的初始值，**不是 FG
channel 实测为零**。独立 `FG preflight` 行的 edge/high/low/enable/latch
来自 observer，是真实外设字节。不可混淆这两类记录。

本次仍是 mode1，未执行电池 IRQ arm；现有按键的 INTID513 成功也不能替代
USBIN/CHGR/FG 的投递验收。154 次真实采样稳定 70%、4008–4009mV，
external=1、charging=0、CHGR state7，不代表真实充电或电池 IRQ 已成功。

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

原始预检与真实充电证据见 `NOTES.md` 的 0112m17、0112m19/m20；真实采样
恢复见 `live-0114a15-batt-irq-only.txt`。最新预检采用上面的 0115 持久日志；
同次 `live-0115-battery-irq-preflight.txt` 缺少启动段，不能独立提供这些硬件值。
