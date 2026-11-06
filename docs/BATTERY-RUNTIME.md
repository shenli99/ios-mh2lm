# 电池遥测、策略与运行期实现（0151）

0154本地更新：runtime控制回调改为固定操作+物理单位+期限；寄存器编码和
每次物理更新前的期限检查在充电后端，新采样经内部PMIC组件。保留现有
IOPMPowerSource/workloop/mux生命周期。0120a3实机通过新增ADC5只读预检及采样回归；0155加入默认空闲的 ADC5 转换 provider，
尚未有实机 EOC/恢复证据，见[硬件接口](BATTERY-HARDWARE-INTERFACES.md)。

2026-11-06，承接0150。按用户要求补第4～6段的可独立实现代码，同时补上
第2～3段的采样/策略前置。本地实现与测试完成；0120原生云产物已核对、
三档boot已打包并初步实测，**六段尚未验收**，见[BATTERY-RUNTIME-0120-TESTING.md](BATTERY-RUNTIME-0120-TESTING.md)。
第4段的“真实充电→受控停止”没有发生，不能宣称整份六段计划已经完成。

0120a2 已实测通过 0153 初始 DMA 清错：WRITE_ERROR 0x2→0，779次温度采样有效，
nominal/learned4027mAh、profile0x13均读出，短日志与完整耗时正常。
见 [FG DMA 恢复与实机结果](BATTERY-FG-DMA-RECOVERY.md)。原始温度未做LG补偿，
电池身份/限值、实际VBUS/输入能力、并联隔离与watchdog/pet仍待补，不开放充电。

## ADC5 provider 模式（0155）

| 参数 | 行为 |
| --- | --- |
| `sweetbattadcconv` 省略或0 | 发布 `SweetPmicAdc5` 服务但不发起转换 |
| `sweetbattadcconv=1` | 仅 BAT_ID (`0x4b`) 一次转换 |
| `sweetbattadcconv=2` | 仅 USBIN (`0x08`) 一次转换，含 GND workaround |
| `sweetbattadcconv=3` | BAT_ID 后 USBIN，各一次；仅诊断，不改变充电放行 |

provider 的 EOC 走 mux group2 和自己的 workloop；matched仅retain/排队，不做SPMI；attach和转换在consumer workloop。
转换超时、错误结果、owner/route异常或恢复失败都会停止 provider，本启动不回退
轮询。当前打包入口为 `tools/mk-mh2lm-event-matrix.sh ... --battery-adc5-conversion`。

## 当前完成度与仍缺的依赖

| 段 | 已实现 | 尚未完成 |
| --- | --- | --- |
| 2 测量 | Gen4温度/nominal/learned/profile固定字段DMA会话；V/I影子配对；配置快照、独立错误与时间 | 实机核对单位、LG温度修正、电池ID、实际VBUS、并联路径；尚无ADC5/I²C供电证据提供者 |
| 3 策略 | 原因位集合、保守上限、温区迟滞、时效、watchdog拒绝、模拟档、故障/会话期限 | 真实输入/物理电池/并联证据接入；保护行为实机核对 |
| 4 首次控制 | 先禁充、约束ICL/FCC/FV、逐笔回读、最后使能；60秒期限、每步检查决策时效、异常禁充 | 当前三个必要证据provider为unknown，设备上不会取得控制；没有控制镜像、真实充电或CHGR正常转换证据 |
| 5 运行期 | USB source-change及CHGR error新增路由、1秒保护timer、事件到策略、停止/重启/系统睡眠禁充提交互锁 | 实机插拔/温区/恢复验证；并未覆盖全部BATIF/温度/WDOG新IRQ；真正系统睡眠后不自动恢复控制 |
| 6 上报 | 实测电流/原始温度、FG容量字段、失效移除、百分比容量退化单位、标准updateStatus；新增三档打包 | 真实iOS/powerd显示、容量/profile物理归属及完整实机回归；循环学习/健康度未实现 |

`SweetBattLimits`暂为空，batteryVerified/parallelVerified/inputVerified均false，
没有boot参数可以将它们改成true。它们需要实测provider，**不是让用户填写几个
“确认=1”参数**。当前只有host测试向策略提供合成有效证据，验证后续执行顺序。
这意味着“控制执行器已有代码”与“手机可以受控充电”明确分开。

## 分层与模式

新增`bsd/dev/sweet_battery_runtime.h`仅含策略/执行器和实例数据，不依赖IOKit。
SweetBattery在自己的IOWorkLoop上拥有snapshot/controller/monitor；mux保持IRQ
基础设施职责，充电写仍经0150 transport。没有新增全局consumer单例。

| 参数 | 行为 |
| --- | --- |
| `sweetbattruntime=0`或省略 | 保持现有电池行为；新timer/新路由/新SRAM读取均不启用 |
| `sweetbattruntime=1` | 真实遥测/新上报与策略观察；不提交充电策略写入 |
| `sweetbattruntime=2` | 同上，运行完整模拟决策；不取得充电控制权、不提交配置写入 |
| `sweetbattruntime=3` | 受条件约束的执行器入口；目前真实证据provider缺失，allow为false，不能启充；不打包此档 |

新模式要求` sweetbattery=1 sweetbatt=1 sweetbattirq=4 `，默认关闭。1/2档也
会写MEMIF请求/地址模式及v1 peek选择，随后恢复；**只读的是SRAM数据，不是
“总线完全无写”**。它们不会写充电策略、profile内容或学习数据。

## SRAM协议与快照

固定USID2/PM8150B，核对PMIC subtype0x20、revision1或2及MEMIF subtype0x0d。
只公开四个字段：温度word328、nominal容量word271、profile integrity word299、
learned容量word285(v1)/287(v2)，不开放任意SRAM字地址。

过程：独立会话锁 → 身份/空闲及写权限预检 → 0153初次DMA状态检查/有界清错
→ v1临时选择ALG_ACTIVE
peek并有界等待 → request/grant → 分区寻址读取 → DMA错误检查 → 恢复地址模式、
释放两个request位、恢复peek。release的两步都会尝试，清理失败禁用本启动后续
会话且不返回数据。已有claim返回BUSY，不替其他owner强行释放。未知revision
拒绝；不复位FG、不清学习数据，也不自动执行全量Linux FG初始化。0153只在
首次准备时允许一次清错；准备完成后的新pre/post错误禁用SRAM直到重启。

ALG等待最多35×10ms，grant最多50×10ms，v1另有必要1ms等待。等待用IOSleep，
**不持共享SPMI事务锁**；会话锁只串行本接口。单笔总线事务仍按0150有界，所有
写通道超时与mux共享poison。一次会话主要等待上限约0.85秒，加有限事务时间；
初始/事件快照可能包含多个会话，因此同时检查整个快照的开始和完成时刻。
没有在匹配通知里执行重SPMI操作。

电压用Gen4固定122070缩放及0x41a6 copy；电流用0x41a8 copy、488281缩放，
IOPM正电流表示充电。温度取10-bit有符号码×25 centi°C；raw=-4表示-1°C。
当前发布的是未经LG补偿的原始温度，属性`SweetBatteryTemperatureCompensated=false`。
容量报告是FG返回值，不等于物理电池身份已经证明；nominal要求integrity bit0
及1000..6000mAh，learned另要求在nominal的50%..120%内，不触发profile重载。

配置快照保留每个寄存器的raw/error/valid位与整体started/completed-ns。事件
刷新输入及APSD/Type-C，保护timer读取V/I/温度和关键配置/故障；timer不读
0x1310/1307/1308/1506/1509/150b，不能以定时探测代替源事件。SOC显示继续保留
原来的事件更新、防跳变和一次有界确认。配置/测量不能混称实际输入电流。

## 策略、执行器与停止

原因位可组合，日志`SWEET BATT POLICY: v1`输出十六进制位集：

| 位 | 名称 | 条件 |
| --- | --- | --- |
| 0x1 / 0x2 | TELEMETRY / STALE | 关键字段无效 / 开始至当前超过3秒或时钟异常 |
| 0x4 / 0x8 | NO_BATTERY / NO_INPUT | 未确认电池存在 / 无有效VBUS插入状态 |
| 0x10 / 0x20 | TEMPERATURE / HARDWARE | 温区限制 / 电压、电流、硬件故障、控制位或限值异常 |
| 0x40 / 0x80 / 0x100 | BATTERY_ID / PARALLEL / INPUT | 电池上限与身份 / 并联隔离 / 输入能力及实测4.5..5.5V证据不足 |
| 0x200 / 0x400 | WATCHDOG / LIMITS | watchdog未确认关闭 / 有效物理上限不足 |
| 0x800 / 0x1000 / 0x2000 | EXPIRED / TRANSPORT / STOPPED | 会话到期 / 总线故障锁存 / 停止 |

允许窗口10..40°C，恢复窗口12..38°C；温度缺失保持限制。初版FCC/ICL最多500mA、
FV最多4.2V，并受有效电池/输入上限约束，量化向下。此为保守测试策略，不是已
验证的LG完整温控。QC来源或未知VBUS拒绝，不主动发起PD/QC。watchdog启动配置
不明或有使能条件就拒绝控制；尚未实现pet接口，不能把“定时器存在”当作已喂狗。

执行器先禁充并回读，暂suspend输入，设ICL及对应模式、FCC/FV、清pause、恢复
输入，最后enable。每步调用共享transport并核对请求位；时钟回调保证编程途中
决策过期也不会继续使能。取得控制的会话最多60秒，到期不自动新建会话。
写失败可能已生效，尝试禁充并锁存故障，不恢复旧enable或高限值；总线坏时
会明确记录“禁充未确认”，没有软件必定能够关断硬件的承诺。

CHGR fault事件锁存故障。瞬时输入/温区恢复只有在所有证据重新有效、会话尚未
到期时才可继续；故障/到期/主动停止不自动重试。系统睡眠/重启/关机通知通过
提交锁与最后一次enable互斥：通知后仅允许禁充，避免“先禁充、迟到enable”。
电源通知不等consumer loop，不改loop拥有的controller对象；睡眠按标准通知
回执，真正睡眠后需显式新会话，本轮不声称已有深睡充电恢复。当前普通息屏不
会停保护timer，不等于系统深睡。

服务清理先在loop外remove电源notifier，同步等回调，再在loop中停止控制/取消
timer并detach；移除event source后释放timer/loop/提交锁。匹配终止、IRQ错误
仍走已有清理路径，不复活polling。PM root未就绪时拒绝启动新模式，避免在空
root-domain上注册通知。

## 上报与日志

保留IOPMPowerSource及updateStatus通知链。新档有效时发布真实Amperage、
Temperature、nominal/learned容量；失效移除旧温度/电流属性及兼容字典对应项。
没有容量时MaxCapacity=100/CurrentCapacity=百分比，显式
`SweetBatteryCapacityUsesPercent=true`，不伪造5020mAh。CycleCount未知不再发布0。

同时发布电流/温度/容量/电压/电池存在/充电阶段有效性、遥测fresh及sample-age-ms，
校正gauge SOC的Estimated标志。软件策略“允许”与硬件状态“实际充电”分开；
状态栏由硬件enable/pause/stage/path/保护状态判断，不因模拟策略allow就显示充电。
完整物理充电仍需负FG电流及SOC/电压趋势支持。

正常sample/policy/control为INFO，失败/未确认禁充为ERROR，继续使用现有等级
过滤及ACM/SWEETLOG，无新日志通道。bootargs仍由用户选择，内核不会自己push/
fastboot。

## 同一次构建与验收

```sh
tools/mk-mh2lm-event-matrix.sh KERNEL_ARTIFACT LABEL --battery-runtime
```

只生成三个同核档，均保留三键/USB ACM/触摸及mode4：

1. `battery-runtime-observe`：新运行层关闭，已有diag，作为必要时可用的对照。
2. `battery-telemetry`：真实测量/新上报，优先实测MEMIF、温度和容量；可导出live
   及持久日志，记录电源/电池背景、插拔/息屏操作时间。
3. `battery-policy-dry`：与2相同再运行模拟档，检查缺失provider原因明确且无
   `SWEET BATT CONTROL`配置提交。输入来源改变应有source事件，CHGR fault
   无自然事件可标未覆盖，不在手机制造过热/故障。

0120已包含0149～0151，原生源码/符号核对和三档打包已完成；具体路径、校验值与
测试顺序见[0120测试记录](BATTERY-RUNTIME-0120-TESTING.md)。
**不打包控制档，不重复旧0119 noUSB测试。**

本地验证：实际SRAM协议v1/v2、BUSY/未知版本、grant/ALG超时、读取/释放失败、
不写SRAM数据、等待期间其他controller用户可运行；实际策略的缺失/过期/迟滞、
每步失败、编程中到期；实际采样/发布/monitor停止及power通知互锁（额外100次并发提交/通知）；mux新source/
fault路由投递/拆除、旧完整套件与80次生命周期竞争。均使用ASAN/UBSAN模型，
不能替代实机时序/完整IOKit验证。四个修改编译单元ARM64单文件编译、严格补丁
应用及24文件比对通过；旧内核含新增调用所需IOSleep/PM通知符号，不等于新内核
完整链接已验证。CircleCI仅增加0151 source gate，实际构建命令未改。

底稿在工作区`scratch/battery-runtime-0119/`。后续必须先核实设备返回值并补三个
provider（ADC5/身份、VBUS/输入能力、主/并联隔离），再完成受控档与第4～6段
实机验收。不能把host测试里的合成证据复制到设备配置作为放行依据。
