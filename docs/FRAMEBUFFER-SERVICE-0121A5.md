# 0121a5 SweetMDSS 实测：充电状态闪烁

2026-11-09。用户直接测试mdss档，未测试baseline；插入电脑USB后充电状态反复闪烁。
F1a已有部分正向证据，但暂不验收，先用同内核baseline判断关联；无需新云编译。

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
`sweet_charge_basic.h:92–96`将缺SB_USB视为SCB_R_TELEMETRY；`:151–160`停充；
下一次完整有效且无其它禁止原因时`:183`后的启充序列重新执行。
`SweetBattery.cpp:1815–1828`根据相同SB_USB有效性发布external和charging。
这已解释软件为何反复启停，**尚未证明输入UV最初因何产生**。

0121a4→0121a5对比，SweetBattery、SweetPmicPower、sweet_charge_basic、battery_runtime、
sweet_charger、sweet_spmi、SweetSpmiInterruptMux七文件逐字节相同。
但代码没改不能证明负载/时序变化无关；只有mdss档，不能将故障直接归因或排除于SweetMDSS。
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

## 下一步：同内核短对照

已登记[BUG-004](BUGS.md)。先不改驱动、不新编译、不扩查BUG-003。
直接fastboot boot现有`out-bs/boot-16k-0121a5-fb-service-baseline.img`，保持同电脑口/线缆，
亮度固定，启动等35秒；拔线5秒再插，保持60秒，观察闪烁并导出
`sweetlog-0121a5-fb-service-baseline.bin`。无需重复全部输入/显示专项操作。

若baseline也有相同UV→校验失败→启停循环，将其作为独立电池输入稳定性bug排期；
若只有mdss出现，则继续收窄新后端调用/亮度负载/时序，不能跳过这个差异验收F1a。
这次证据足以确认启停循环，baseline是判断迁移是否引入回归所缺的对照。
