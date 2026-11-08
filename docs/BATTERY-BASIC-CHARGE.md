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

