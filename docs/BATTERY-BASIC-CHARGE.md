# runtime3 基础充电（0168）

## 为什么之前一直不充电

runtime1/2 的放行链路在实机上永远无法全部满足，所以 0042 从未被置 1：

- `parallelVerified` 硬编码为 false，外部 SMB1355 隔离永远不算“已证实”；
- watchdog 门要求的配置与实测 0x1651/0x1653（0x62/0x8c）不一致；
- 放行依赖同 epoch ADC5 USBIN/BAT_ID，而 0120a16 实测 ADC 只活了约 1.26 秒
  就因 RESULT VERIFY-12 停掉（见 BATTERY-DEPENDENCY-TESTS.md 0120a16 节）；
- 60 秒所有权租约、一次故障即永久停止。

每轮修一个门，下一个门又挡住，这就是“反复返工”的原因。证据：0120a16 日志
19 次 CONFIG 全部 enable0，无 CONTROL/WD 命令。

## 新设计：按 LG 的分工驱动硬件

LG 的 SMB5 驱动（`android_kernel_lge_sm8150/drivers/power/supply/qcom/`）里，
APSD/AICL/ICL、CC/CV、截止、回充和 BAT_OV 由 PMIC 自己完成；电池温度保护是软件
（protection-battemp 定时 work）。软件只做：

| LG 行为 | 出处 | 0168 对应 |
|---|---|---|
| init_hw 设 FCC/FV 上限 | qpnp-smb5.c | FCC 2000mA / FV 4400mV（低于 board 2800 / profile 4430） |
| SEC_NONE 时 SMB_EN 拉低 | smb5-lib.c | PARALLEL_INHIBIT（0x1648 mask0x18=8） |
| WD bite 停充、plugin 自动开 WD、bark IRQ | qpnp-smb5.c:3151/3161 | WATCHDOG_PREPARE（0x1653=0xf3，0x1651 mask0xc2=0x42） |
| bark IRQ 里喂狗 | smb5-lib.c:7251 `wdog_bark_irq_handler` | 只在 battery-wdog-bark IRQ 后 CHARGING_PET（0x1643） |
| protection-battemp 温度表与周期 | lge/protection-battemp.c:246-405，sm8150_common-pm.dtsi:120 | `scb_health`/`scb_decide`/`scb_period_ms` 逐项一致 |
| CHG_DISABLE 投票 | smb5-lib.c | 任一原因置位即写 0042=0 并回读 |

温度表（0.1°C）：升 cool30/good120/warm450/hot550，降 warm520/good430/cool100/cold0。
COOL：VBAT≥4.0V 时 500mA，否则 1150mA；WARM：1900mA + FV 4000mV；HOT/COLD/未知：停充；
CHILLY（0～15°C）：VBAT≥4.0V 时 1000mA，否则 1900mA，与上面取小（同 LG 第 397 行 min）。
周期：GOOD 60s，CHILLY/COOL/WARM 30s，其他、遥测缺失或退避中 10s。

## 执行路径（无软轮询）

- 触发：USB/APSD/Type-C/输入故障/充电故障/WDOG bark 的 mux IRQ → 电池 workloop
  采样 → `runBasicCharge`；另外只有 LG 温度周期定时器（60/30/10s，每次 IRQ 重置）。
- 不创建 ADC5 client，不依赖 BAT_ID、身份、permit、租约。
- 启充顺序（每步回读）：ENABLE0 → PARALLEL_INHIBIT → WATCHDOG_PREPARE → WATCHDOG_PET →
  FCC → FV → PAUSE0 → INPUT_SUSPEND0 → ENABLE1。
- 已充电时温度档变化只重写 FCC/FV（SCB_LIMITS），不重走整套。
- 停充原因：无输入、USBIN OV、无电池、温度、VBAT>4450mV、0x1007 BAT_OV、0x100d 硬温/AFP、
  遥测缺失、关机/重启、命令失败退避 5s、连续 5 次失败（含外部写者清 enable）后本次开机不再启充。
- 睡眠：WillSleep 只确认、不停充（同 LG）；bark 没人喂时 bite 由硬件停充。
  Restart/PowerOff 仍在通知器里同步写 0042=0。
- 服务停止：`stopRuntime` 写 0042=0。

## 硬件 JEITA 与软件温度表的关系

- LG `qpnp-smb5.c:3309` 在 init 调 `smblib_disable_hw_jeita(chg, true)`（smb5-lib.c:2805），
  清 0x1090 bit3..0，即关掉硬件软限 FCC/FV 补偿；全树从不写 bit4 `JEITA_EN_HARDLIMIT`。
- mh2lm 电池档 LGE_BLT42_*.dtsi 没有 `qcom,jeita-*` 阈值，`jeita_update_work` 不写硬件阈值、
  走 FAILURE 分支，所以 `bat-temp` IRQ 处理函数 `batt_temp_changed_irq_handler` 直接返回。
- 实测 0120a6/0120a16 日志 `LIMIT CONFIG ... jeita=0/0`：0x1090=0x00，读成功。
- 0168 与 LG 一致：只读 0x1090，不写；温度降流/停充全由软件 LG 表负责，不会和硬件补偿叠加。
- 0x100d bit3..0（硬限/AFP 状态）置位时软件也停充；两者都是“停”，方向一致。
- 【待核实】bit4=0 时 PMIC 硬温停充是否生效、默认硬阈值是多少。在核实前不能把硬件硬限当作
  已有的保护层；目前唯一确定的温度保护是软件表 + 喂狗失败时的 bite。
- 可选改进（未实现）：FG RRADC `batt-temp-delta` IRQ（pm8150b.dtsi:428，SID2 0x42 irq2，
  LG extension-fg-gen4.c:153 有处理）接到电池 workloop，温度变化即触发评估，定时器降为兜底。

## 不变的部分

ICL 不由软件设置，沿用硬件 APSD/AICL 结果（0120a16 实测 1500mA）。runtime1/2 和
deps 诊断路径代码不动；runtime3 需要 `sweetbattery=1 sweetbatt=1 sweetbattirq=4`。

## 本地验证（未上机）

- `tools/tests/test-mh2lm-charge-basic.py`：真实 `sweet_charge_basic.h`，覆盖顺序、
  只在 bark 喂狗、温度带与滞回、周期、各故障、退避/稳定清零、外部写者、enable 回读失败。
- `tools/tests/test-mh2lm-battery-runtime.py` 新增 `mode3Tests`：真实 SweetBattery 采样器 +
  sweet_charger.c 后端，核对寄存器写序列、无 ADC client、60s 周期、bark 只写 0x1643、
  睡眠不停充、拔插/硬温 IRQ、重启和服务停止写 0042=0。
- 其余 10 个既有 host 测试在新树上全部通过；7 个相关源文件用 CI buildtree 交叉编译 rc0，
  无 error；sweet_charge_basic.h 有 13 条 -Wunsafe-buffer-usage（原代码同类告警很多），
  未逐条与 base 树对比。
- 0168 对 0167 结果树 `git apply --check`、`patch -p1 --dry-run` 通过，应用后逐字节一致。
- CI 新增 4 个 require_marker，0168 被拒时构建失败。

## 【待核实】（只有实机能回答）

- SMB1355 是否真的听 SMB_EN 低电平（LG 依赖同一机制，但本机未证实）。
- bite 后再喂狗是否恢复充电，还是需要重新走启充序列。
- 本移植是否在熄屏时发 WillSleep；若系统真睡眠，bark IRQ 能否唤醒。
- 实际充电电流：以 FG 电流变负、0x1006 stage 1～3 为准。

## 0120a17 实机结果（sweetlog-0120a17-battery-basic-charge.bin）

日志 SHA256 06412841bb307239d16241653442e3a6893d1e5aa6a985eaf4dd37a8295944ab，v1/4MiB，
有效 398827 字节/5042 行，无回卷/NUL；文本底稿 scratch/artifact-0120a17/basic-charge-persistent.txt。

- 46～85s：开机时接电脑 USB（APSD SDP），启充 9 条命令全部回读成功，但 0x1006=0x47
  （stage 7 DISABLE）、FG 0mA；85s 拔出后写 0042=0。用户说明这是电脑口，不作为失败。
- 162.5～163.5s：插充电器时接触抖动（usb 0x10/0x26/0x12 交替），两次部分快照
  （valid 缺位、INPUT error -12）按遥测缺失不启充，无误启。
- 163.5s 启充（enables=3，failures=0）→ 163.6s 0x1006=0x03 FULLON，APSD 后 FLOAT（type5）。
- 163.6～1047s：FG +1940～1994mA（FCC 2000mA 封顶），间歇约 1420～1464mA；
  VBAT 3769→4335mV，SOC 56%→71%，温度 21.75→33.25°C 一直 GOOD，未触发温度档变化。
- bark 喂狗 54 次、pet-errors 0，约每 16.4s 一次；全程未掉充，未见 bite。
- 1054s 拔出：usb=0x26 → 同一次 IRQ 写 0042=0，回读 0。
- 无 MUX 读失败/隔离、无 ADC 请求；采样 168 次 IRQ、1 次 protection-timer（145s，未充电时）。

仍未验证：温度档切换（WARM/COOL 实机）、SMB1355 是否参与、熄屏睡眠、bite 恢复。ICL settled 0x13 与约 2A 电池电流的输入功率关系未核对【待核实】。

## 0120a17 充满（sweetlog-0120a17-battery-basic-charge1.bin）

日志 SHA256 6ff39821a4c3fdc783859f69d4f67b31561955f255085c2b98841b4124163d48，983088 字节/
12104 行，无回卷/NUL；底稿 scratch/artifact-0120a17/basic-charge1-persistent.txt、charge1-table.txt。

- 106～107.6s 插入抖动：一次部分快照（INPUT error -12）在已启充时触发遥测缺失停充，
  0.9s 后输入稳定重新启充（enables=2，failures=0），符合设计。
- CC：107.7s 0x1006=0x03 FULLON，约 1940～1980mA，VBAT 升到 4392mV。
- CV：404s（75%）起 0x1006=0x04 TAPER，VBAT 稳在 4400～4401mV（=FV 4400），电流逐步降到约 190mA。
- FG 在 3748s 报 100%，此时仍在 TAPER（约 300mA）。
- 截止：4878s 0x1006=0x45 TERMINATE，FG 约 0mA，软件保持 enable=1 不干预（同 LG）。
  之后 5 分钟 VBAT 4401→4363mV，未到回充点，回充未观察到。
- 5189s 拔出 → 写 0042=0。全程温度最高 31.75°C，一直 GOOD；喂狗 305 次、pet-errors 0；
  无 MUX 读失败/隔离；未发生 protection-timer 采样（bark 每约 16.4s 先到）。
- 熄屏：`SWEET FB: display off/on` 显示 205s 熄屏，此后到 5189s 只有 7 次瞬间亮屏（不到 1 秒），
  充电约 98% 时间在熄屏。熄屏期间 FG 采样连续（相邻采样最大间隔 36.7s，bark 约每 16.4s 一次），
  说明 CPU 一直在跑、系统没有进入真睡眠；0168 的 WillSleep 分支不打日志，无法从日志直接看到
  是否收到过 WillSleep。真睡眠下 bark 能否唤醒仍【待核实】。

## 0169：FG 温度变化中断（本地完成，待云编译/实机）

- 新路由 `battery-temp-delta`：SID2 外设 0x42（FG RRADC）bit2，双边沿，事件 `SWEET_BATT_IRQ_TEMP`
  0x200。依据 LG pm8150b.dtsi:428-435 `fg-rradc@4200` 与 qpnp-fg-gen4.c:3940
  `fg_delta_batt_temp_irq_handler`（重读温度后通知电池）。mux 只做 IRQ 配置/ACK，不写 FG SRAM，
  沿用固件留下的 delta 阈值。路由表 19→20 路，电池组 mask 0x1ff→0x3ff。
- SweetBattery：`sweetbatttempirq=1`（仅 runtime3）时订阅；IRQ 到来走已有路径：新快照 →
  `runBasicCharge` 温度表投票 → 重置 60/30/10s 定时器。定时器保留为兜底。计数发布为
  `SweetBatteryTemperatureIRQs`，并出现在 `BASIC STATE ... temp-irqs=`。
- 关机/重启：runtime3 在优先级电源通知里停充成功后打 `SWEET BATT BASIC POWER: v1 transition=...
  disabled readback=0`；WillSleep 打 `will-sleep acknowledged; charging kept`。行为不变，只补证据。
- 默认镜像不带 `sweetbatttempirq`，与 0168 行为完全一致；打包脚本在 0169 内核上额外生成
  `battery-basic-charge-tempirq`。
- 本地验证：12 个 host 测试全过，新增 mux 场景 battery-temp / -read-fail / -enabled（成功挂接、
  只改 bit2、命中一次触发一次采样、拆除恢复原配置；读失败或固件已使能时整组可见失败）；
  7 文件 CI buildtree 交叉编译 rc0，告警与 0168 树相比只多 1 条同类 missing-field（新路由表项）。
  patch 对 0168 结果树 strict apply、逐字节一致；CI 新增 4 个 require_marker，本地 114 条全部命中。
- 【待核实】0x242 在本机是否唯一 EE0 IRQ-owned APID（0120a17 路由诊断未列 0x242，0169 已加入
  诊断列表）；若不是，tempirq 镜像会在挂接时整组失败、不启充，退回普通镜像即可。
  FG 默认 delta 阈值和实际触发频率未知。

## 0120a18 tempirq 档实测（sweetlog-0120a18-battery-basic-charge-tempirq.bin）

日志 SHA256 6b676f2e65bd0a872608cdcba81c22b745eec7ef2479cb3d937e45e14c624f19，553773 字节/6907 行，
无回卷/NUL；底稿 scratch/artifact-0120a18/（文本与 table.txt）。启动参数含 `sweetbatttempirq=1`。

- 温度中断路由：`SWEET FG ROUTE ppid=0x242 apid=285 write-ee=0 irq-owner=1`，唯一 EE0 归属；
  `attach route=battery-temp-delta apid=285`，电池组 events=0x3ff 挂接成功，无错误，充电正常。
  0169 的【待核实】（APID 归属）关闭。
- 本次 temp-irqs=0：全程温度 27.50～28.75°C，只变 1.25°C，低于 FG 默认 delta 阈值
  （LG 绑定文档：0～3°C 可配，本机未配置，默认值未知）。不是失败，但 IRQ 实际触发仍未观察到。
- 充电：开机已 100%。2795s 插入（抖动期间正确不启充）→ FULLON → 立即 TAPER，VBAT 4401mV，
  电流 431→190mA → 3433s 硬件 TERMINATE（0x45），UI event7 `full=1`。
- 回充：截止后观察约 25 分钟（3433～4924s），VBAT 4401→4361mV 后稳定，电流约 0，未回充。
  0x1051 CFG2=0x2c，bit2:1=10 即 **VBAT 回充模式**，阈值寄存器 0x107e/0x107f 未采样；
  LG 配置是 `qcom,auto-recharge-soc = <98>`（SOC 回充，`sm8150_common-pm.dtsi:385`），
  本移植沿用固件的 VBAT 模式。回充仍未验证【待核实阈值】。
- 关机：约 4941s SpringBoard 发起关机，插着充电器。日志在 shutdown 开始后结束，
  **没有 `SWEET BATT BASIC POWER` 行**：持久日志每 10s 刷一次（sweet_ufs.c `SWEET_LOG_SECS`），
  关机通知在系统断电前来不及落盘。关机停充是否执行无法从本日志判断。
- bark 喂狗路由命中 131 次，无 ERROR 行；三键/触摸正常。

