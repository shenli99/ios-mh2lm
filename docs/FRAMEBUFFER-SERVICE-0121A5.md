# 0121a5 SweetMDSS 实测：充电状态闪烁

2026-11-09。用户先测mdss档，插入电脑USB后充电状态反复闪烁；随后同核baseline也复现
同一启停故障链。新SweetMDSS后端不是触发故障的必要条件，充电输入稳定性独立跟踪。
F1a仍只有部分正向证据，不据本次对照宣布完整验收；本组A/B结束，无需新云编译。

## 日志与证据

持久日志`work1/logs/sweetlog-0121a5-fb-service-mdss.bin`，SHA256：
`770a8164c2d66ad75053919c4191da80344aff7df8735c23d4fd0bdf18e80b16`。
4,198,400字节；ring 4,194,304，有效1,259,276字节，flush32，未回卷，有效区无NUL。
解码14,095行；349次runtime snapshot，从首至末约335.73秒，不把硬件单调时间当启动秒数。
底稿`scratch/artifact-0121a5/{fb-service-mdss.txt,fb-service-mdss.timeline.txt,
mdss-log-analysis.json,analyze-mdss.py,battery-source-comparison.json}`。

日志boot-args确认sweetmdss=1、runtime3、IRQ4、probe1、trace/proof/contract0，符合预期镜像。

## 已确认故障链

这不是仅有肉眼图标闪烁。整份日志累计67次启充，61次因遥测无效停充，另有6次无输入停充；
62份输入快照SB_USB报VERIFY(-12)，其中61份valid=0x3ffffefff，只缺SB_USB一位，另1份
同时缺一个额外输入字段。命令日志670条均error0，failures始终0；遥测失效不是命令失败，
未触发scb_fail的5秒退避，于是下一次有效采样会立即重新执行整套启充。

| 解码行 / 样本 | 证据 |
|---|---|
| 3609–3618 / id17 | usb=0x10/-12，valid缺SB_USB，ENABLE原为1；reasons0x1，写ENABLE0并成功 |
| 4238–4247 / id28 | usb=0x15/-12，原ENABLE1；同样停充，enables仍2 |
| 4267–4284 / id29 | 约103ms后开始的新快照usb=0x10/0，ENABLE读0；9步启充完成，enables=3 |
| 4205、4253、4299 | 原始CHGR从0x03→0x47→0x03，raw SID2 charging相应1→0→1 |
| 14039–14048 / id349 | 最后输入usb0x26/plugin0，因无输入停充；累计enables67 |

两次“因遥测失效停充”之间的间隔中位约0.717秒（含不同连接阶段，最大49.7秒），
存在持续重复切换。原始SID2状态变化计142次，**不是精确的IOKit publish通知次数**。
实际IOPMPowerSource发布由publishRuntime根据有效USB、PATH、ENABLE和CHGR状态计算，
所以全字节校验撤销SB_USB时，external/capable/charging也会撤回；不能靠隐藏图标解决控制切换。

硬件事件方面记录771次battery-usbin-uv和843次battery-chgr route命中。
0x1310 bit4=plugin、bit3=OV、bit2=UV、bit0=collapse；0x15包含plugin+UV+collapse。
温度36.25～37.25°C、GOOD，策略无温度/高压停充原因；未见mux隔离、SPMI timeout或panic，
不是BUG-002那条consumer退出故障链。UV/CHGR命中数不能替代电压波形或真实插拔次数。

## 源码解释及边界

`SweetPmicPower.cpp:119–138`前后读取完整USB/Type-C/APSD状态字节。USB字节变化时
清SB_USB valid并写VERIFY(-12)；后端charger read本身不做该字节一致性判断。
`sweet_charge_basic.h:85–89`将缺SB_USB视为SCB_R_TELEMETRY；`:157–164`停充；
下一次完整有效且无其它禁止原因时`:189–205`的启充序列重新执行。
`SweetBattery.cpp:1815–1828`根据相同SB_USB有效性发布external和charging。
这已解释软件为何反复启停，**尚未证明输入UV最初因何产生**。

0121a4→0121a5对比，SweetBattery、SweetPmicPower、sweet_charge_basic、battery_runtime、
sweet_charger、sweet_spmi、SweetSpmiInterruptMux七文件逐字节相同。
当时只有mdss档，代码没改不能证明负载/时序变化无关；后续baseline结果见下节。
电脑口能力、线缆/接触、继承的AICL/ICL模式、全字节校验与runtime3耦合都是待区分项。

已查LG参考：smb5-reg.h:244–253有上述RT位；qpnp-smb5.c:367–385的PM8150B分支
只加CHG_TERMINATION_WA，WEAK_ADAPTER_WA在PMI632分支。
因此不能看到usbin_uv_irq_handler中的弱适配器workaround就直接搬给PM8150B。
未来如修改，应通过现有charger provider/控制接口处理输入状态与限流，不在显示或UI层写PMIC。
不得简单接受全部-12、长期缓存“正在充电”或删掉OV/拔线/温度保护。

## 显示迁移已观察到的部分

第78–86行9资源映射，87行新服务启动，92行背光registry匹配，224行旧探针禁用。
第174、383、584行三次探针均完成：GCC0x80000003、DISPCC0x1；VIG0仍
0x9d200000、1080×2340、stride4320，SMMU配置与原基线一致。
第2429、3800–3815行记录8次DCS rc0；成功日志预算仅8次，不推断后续没写。
全日志无显示ERROR/WARN或DCS失败日志；16个HID键事件、65个触摸事件，4次off/5次on。
这些不能代替用户确认亮度全范围、所有视觉效果或完整F1a验收。
USB仅两次ACM配置、10次reset及10条endpoint命令WARN；本轮用户未报告主机识别结果，
不能单靠WARN将BUG-001认定为本轮已复现或把它当充电闪烁的根因。

## 同内核baseline对照：同一故障链已复现

用户导出`sweetlog-0121a5-fb-service-baseline.bin`并确认成功复现。SHA256：
`05b5ae79eaa5e41b32e18e3a8b9b23bc65158d89dbe84d7666a0589191296e09`。
4,198,400字节；有效1,079,001字节，flush15，未回卷且有效区无NUL；解码11,885行。
第332行boot-args确认sweetmdss=0、runtime3、IRQ4、probe1；没有SweetMDSS资源/启动日志，
第2127/2158/2180行旧BSD探针三遍完成，第2406行旧DCS路径rc0。
底稿`scratch/artifact-0121a5/{fb-service-baseline.txt,fb-service-baseline.timeline.txt,
baseline-log-analysis.json,baseline-summary.txt,analyze-baseline.py}`。

| 指标 | baseline / sweetmdss=0 | mdss / sweetmdss=1 |
|---|---:|---:|
| runtime样本数 | 301 | 349 |
| 首末样本跨度（秒） | 179.87 | 335.73 |
| USB VERIFY(-12)样本 | 61 | 62 |
| 因遥测无效停充 | 59 | 61 |
| 累计启充 | 64 | 67 |
| 另因无输入停充 | 4 | 6 |
| USBIN UV route命中 | 698 | 771 |
| CHGR route命中 | 772 | 843 |
| BASIC命令日志 / error非0 | 639 / 0 | 670 / 0 |
| mux隔离 / SPMI timeout / panic | 0 / 0 / 无 | 0 / 0 / 无 |

两轮时长和操作不同，不按累计次数判断哪个档更严重。baseline的61份USB失效中，60份仅
缺SB_USB，另1份多缺一个输入字段；停充间隔中位0.716657秒。温度36.50～37.25°C，
策略没有温度或高压停充原因。输入UV源头仍未确定，不能因此断言电脑口供电不足。

直接证据：baseline样本22，第3585行usb=0x15/-12、3591行ENABLE1且缺SB_USB；
第3593–3594行成功写ENABLE0、reasons0x1。样本23约103ms后开始，第3614行usb=0x10/0、
3620行ENABLE0且valid完整；3622–3631行完整九步重新启充，enables3→4。
第3646行原始CHGR=0x03、charging1，随后继续发生同类循环。不是仅有相似的肉眼现象。

**结论边界：** 关闭新后端仍复现同一故障链，不能将其定为SweetMDSS专属回归。
此对照不证明旧0121a4镜像已复现，不排除两档共用配置或显示负载参与触发，也不能仅凭它
宣布F1a完整验收。BUG-001主机识别问题仍独立；baseline有18条USB WARN/reset、2次ACM
配置，不能单凭计数认定枚举失败。

**后续：** 本组A/B结束，不要求重复或重新编译。BUG-004独立排期，核对输入限流/AICL及
USB动态状态与协议一致性校验的边界，修复走charger provider接口，保留拔线/OV/温度保护。
显示继续原F1a收口/F1b计划，不恢复BUG-003专项；亮度全范围与完整生命周期尚无充分验收证据。
