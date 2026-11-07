# APSD 事件来源与结果时效（0159）

2026-11-07。补丁 `0159-mh2lm-apsd-event-provenance.patch` 接在0158之后；两者
一起推送和编译，不要求先推0158。当前仍是 runtime1 证据镜像，不启充。

## 核对结论与前次解释更正

工作区 `docs/charging-logic.md` 第3.1节描述 USBIN plugin（SID2/PID13/bit4）和
source-change（同外设bit6）两类事件，不能假定两者严格先后。source-change
不只是APSD done，还包括QC等状态变化，handler必须再检查APSD_STATUS。

**0151已添加battery-source路由、SOURCE事件位及runtime consumer订阅。**
0155保留该路径，0120a5也已观察到6次source刷新。此前对话中根据0141初版
断言“当前没有bit6路由”是错误的；不能据此再添加一条重复route。

实际缺口：原irqAction把所有电池事件交给poll，runtime继而统一调用
sampleRuntime(true)，CHGR/MSOC/READY也会读取APSD并更新inputEpoch/时间。
读取到DONE只能说明当时寄存器值，不能说明它来自本次检测，也不能替代使能。

Android依据：`smb5-lib.c:smblib_apsd_enable/smblib_rerun_apsd/usb_source_change_irq_handler`；
`smb5-reg.h:USBIN_OPTIONS_1_CFG_REG/BC1P2_SRC_DETECT_BIT/CMD_APSD_REG`；
本设备USB DTS的dpdm-supply实际指向usb2_phy1。

## 本次修改

- irqAction将USBIN/SOURCE原因传给消费者自己的采样逻辑；初始只读一次输入，
  之后只有plugin或source事件刷新输入寄存器。CHGR/MSOC/READY、SOC有限确认
  和保护monitor不扫描APSD，不更新输入epoch/时间。原有事件、IOPM发布仍执行。
- 通过原typed PMIC采样接口增加精确只读 `SID2:0x1362`，bit3为BC1.2检测使能。
  不扩大写权限，不触碰QC配置；使能状态和每字节错误分别记录。
- 后端按 `USBIN → APSD_STATUS → RESULT → APSD_STATUS → USBIN` 有界读取；
  status/VBUS前后不一致或任何相关读取失败，coherent=false，不能认可这组结果。
  这是软件一致性检查，不是硬件原子快照，不排除ABA变化。
- 新增观测到的usbSession、APSD来源及独立采集时间。启动遗留DONE只能诊断；
  source事件、稳定VBUS、有效DONE/result和已启用检测共同形成事件限定的证据。
  未完成、使能关闭、无VBUS、读错或后续新输入IRQ排队时撤销该证据。
- 已观测拔出/重新插入或未知VBUS会撤销旧会话。source先到、同电平plugin后到
  且相关raw一致时保留来源，**不延长原source时间**；输入类型/角色变化也撤销。
  source/plugin合并通知可以同批处理，不要求固定时序。session计数饱和后拒绝。
- 策略增加来源、使能、观测会话与3秒source期限检查；执行期限同时取source、
  输入快照、遥测和limits的最早期限。没有inputVerified或其他依赖仍不允许充电。

仍沿用GIC callback → mux workloop → SweetSpmiMuxClient → battery workloop。
consumer不直接映射MMIO、不在mux回调里做重SPMI操作，不新增轮询兜底或GIC注册。
0158的pending route日志/按组隔离保持在补丁链中。

## 日志与设备验收

新INFO短行：

```text
SWEET BATT APSD EVENT: v1 id=... events=... session=... source=... coherent=... qualified=... options=.../error enabled=...
SWEET BATT APSD TIME: v1 id=... session=... started-ns=... completed-ns=...
```

`events=0x1` 为plugin，`0x10` 为source，`0x11`为两者合并；初始读取为0。
`source=1`表示当前保留了source事件依据，不表示新的物理电流合同。
`qualified=1`仅表示该事件证据当前有效，不能代替PD/USB current contract、
电池身份、并联隔离或watchdog。3秒后不再满足策略，无timer读取来延长它。
日志值与同id的INPUT/TYPEC/INPUT TIME配套；ERROR仍无条件输出。

新云产物用原 `--battery-evidence` 打包，现增加0159 kernel marker检查，防止
旧kernel冒充。参数保持runtime1/IRQ4/ADCconv0，三键、触摸、USB IRQ和USB日志保留。
同一次启动完成电脑→拔线→普通已知5V电源→拔线→电脑的测试，导出持久日志。
关注source事件与DONE/result/enable的关系、前后读取一致性、旧source时间不被
plugin/CHGR/SOC续期，并继续检查0158的pending route以及三键正常性。

## 本次边界与下一步

本轮**没有主动enable或rerun APSD**，也不声称DCP/CDP缺失原因已确认。
0x1362 bit3原先根本未采样，因此“XNU没写enable”不能推导“硬件一定没使能”。
既有APSD1和结果可能来自固件、自动检测或启动残留，必须区分。

观测会话不能证明所有物理插拔边沿均未丢失。若拔插在通知合并期间发生、最终
电平和结果完全相同，本轮不能从字节值恢复每次物理会话；现有授权互锁保留。
启动前已完成检测也只作为诊断，尚无可授权的启动会话重建。

后续输入provider要接DPDM PHY供电/所有权、Type-C角色/PD优先级及禁用原因，
再通过typed操作实现APSD enable/条件rerun/超时/撤销；trigger命令不能复用
通用RMW+readback。然后接输入能力与电流限制，才可能解除充电控制互锁。
不把充电enable、APSD enable、IRQ enable混成同一个开关。

## 本地验证

0158+0159在实际0120a6云buildtree上严格apply，结果与本地verify逐文件一致。
SweetBattery、SweetPmicPower、sweet_spmi的ARM64单文件交叉编译通过；这是旧CI头
配合Linux clang的检查，不能代替新的macOS云链接和设备运行。

ASAN/UBSAN host测试覆盖plugin/source两种先后、合并事件、启动遗留DONE、拔插
撤销、使能关闭、过期/执行期限、读取中DONE/VBUS变化和新IRQ排队；真实irqAction
的事件掩码传递、mux consumer生命周期/竞态、transport只读权限、ADC consumer、
SPMI串行化、日志分级和board startup回归通过。mock不能证明真实APSD检测时序。

底稿：工作区 `scratch/battery-apsd-events/`，含base/edited/verify、cloud-apply、
cloud-validation.json、测试输出及三个交叉编译结果。未push，无新设备结果。

## 0120a8云产物与测试镜像

用户已下载0120a8，33文件与最终0159验证树一致，95个源码gate通过，kernel包含
0158/0159日志标记且manifest一致。已使用原battery-evidence档打包：

`/home/shenli/work1/out-bs/boot-16k-0120a8-battery-evidence.img`

大小35,667,968字节，SHA256
`1b44219c37d3d4247582bcc19781109108b331b5f0deaa87082c42ef078b9481`。
内核SHA256 `b5e1546a6d8207003b8a4812a9e17e9fbb01d4ee50130d1cff30f4c6d420de7e`。
runtime1/IRQ4/ADCconv0，三键/触摸/USB IRQ及USB日志保留；未启充。ramdisk
三件套回读与boot header实际载荷均已核验。用户自行fastboot，等待
`sweetlog-0120a8-battery-evidence.bin`，按上面的电脑/5V电源流程测试；本轮无需
baseline、dry或重新测ADC bring-up。尚无设备结果，不能称APSD/充电已修复验收。

底稿工作区`scratch/artifact-0120a8/`；本轮未改驱动/CI、未push/操作设备。

## 0120a8实机结果：APSD已使能且source路径工作，ADC5另有超时

日志 `logs/sweetlog-0120a8-battery-evidence.bin`，SHA256
`f242bb5eb22f8440d40c35e90e8bde52e020a6ee2c94b7210a3d10a23b84b3cd`。
SWEETLOG v1/ring4194304/有效810725字节/flush76，未回卷，有效正文无NUL。
771组遥测完整连续，跨度789.563秒；12组irq/启动/arm采样、759保护tick。
12次profile全match1、integrity13、nominal4027mAh，维持Tocad候选证据。

### APSD事实与事件顺序

10组输入采集：初始1、USBIN6、source-change3。**全部1362=0x18/error0，bit3=1，
BC1.2检测实际已使能。** XNU未主动写enable不等于硬件未开启，本轮数据排除
“APSD没开所以没结果”的解释，仍不能确定最初由哪个固件writer使能。

以下时间相对第一组输入获取；不是物理接头触碰的硬件时间。电源名称仅按测试
顺序理解，日志本身没有记录适配器型号。

| 相对时间/样本 | 原因 | STATUS/RESULT | 结论 |
| --- | --- | --- | --- |
| 0秒/id1 | 初始 | 11/01，SDP | source0、qualified0；启动已有DONE只作诊断 |
| 402.703/id391 | plugin | 00/00，VBUS有 | 尚未完成 |
| 402.843/id392 | plugin | 00/00，VBUS无 | 已观测到短暂掉线，撤销旧会话 |
| 403.709/id394 | plugin | 00/00，VBUS有 | 开始观测会话5，未完成 |
| 405.058/id397 | source | 21/10，FLOAT | source1/coherent1/qualified1 |
| 405.165/id398 | source | 21/10，FLOAT | 再次source变化，仍FLOAT；不是第二次插线 |
| 610.394/id596 | plugin | 00/00，VBUS无 | 撤销会话5 |
| 736.390/id718 | plugin | 00/00，VBUS有 | 会话7，等待结果 |
| 737.141/id720 | source | 01/01，SDP | source1/coherent1/qualified1 |

中段稳定plugin到首次完成读取相差约1.350秒；最后重新接入相差约0.751秒。
这直接说明只在plugin时采一次不能及时得到最终识别结果。3次source均由mux
battery-source命中，10次电池IRQ全healthy1（plugin6/source3/MSOC1），发布延迟
88.863～112.859ms。MSOC id390没有重采输入；759保护tick没有新增输入采集。
10次输入全coherent1，没有实际覆盖“读中状态变化/错误”的拒绝分支。

**qualified1只表示事件限定的检测证据有效，不代表允许充电。** 本轮只有
UNKNOWN、SDP、FLOAT，没有DCP/CDP；10次sink-ok均0。中段Type-C：1506=02、
1509=23、150b=39、debug0、legacy4；按LG smb5-reg.h，misc39含VBUS_ERROR bit4。
回电脑后1506=40、1509=2c、misc28、debug1、legacy6，包含debug/legacy状态。
这些字节不满足现有普通sink校验，不能仅凭Rp或实测5V授予输入电流。
VBUS_ERROR究竟是瞬态、遗留配置还是实际输入问题，本日志不能独立证明。

FLOAT不是“APSD没有完成”，也不等于物理适配器必坏。LG
`override_usb_source_change_irq_handler`对FLOAT有SW_ICL_MAX=500mA投票路径；
当前XNU未实现完整Type-C/DPDM/输入仲裁，窄策略只允许已证明的DCP/CDP，不能
把本轮结果强改DCP，或把换到DCP当作实现input provider的替代方案。

10次充电配置均enable0/pause0/watchdog62/time8c，没有CONTROL，runtime1没有
获得控制权。IOPM电流-548～+20mA不足以证明充电；真实充电仍未恢复。

### 新异常：ADC5请求196缺少第二次完成回调

- 请求1～195均得到有效、已恢复的结果：BAT_ID16、USBIN179，合计374次EOC。
  consumer接受193（BAT_ID16/USBIN177），另2次因epoch变化正确丢弃。
- 请求196是USBIN：先经过ground阶段，收到第一次EOC，advance返回
  `SWEET_ADC5_MORE=1`并进入RESULT phase3。日志的`error=1`在这里是继续标志，
  不是SPMI错误。之后没有第二次EOC处理记录，timer报告EXPIRED(-101)。
- `restore-error=0 cleanup=0`，provider按当前设计停止并撤销BAT_ID/VBUS测量，
  后续没有再提交/接受ADC测量；不能拿停止前旧值继续授权。
- 这不是本轮的“mux pending后全局停机”：无mux ERROR/pending隔离/IRQ storm，
  ADC失败后仍有3次电池IRQ、后续触摸计数到229。三键完整down/up在故障前有
  记录，**没有故障后的按键操作证据**，不能宣称已外部验证故障后三键。
- 全程375次ADC EOC=成功374+失败请求首次1。没有硬件status/request/IRQ pending
  的超时快照及实际deadline时间，不能区分转换未完成、完成但IRQ丢失/合并、
  或事件交付/超时竞态，不能直接定性GIC配置错误或把超时阈值调大了事。
  IOTimerEventSource有calloutGeneration取消保护，仅凭假想旧timer无法定根因。

有效BAT_ID888516～889684uV；USBIN4550864～5103104uV，范围来自被接受的测量。
与电源能力许可分别解释。ADC失败发生于中段source完成后约80秒，日志未见
同时插拔；不将其直接归于重插竞态。

USB首次及启动阶段另一次reset后共2次ACM configured；最后重接电脑反复reset/
connected、无新的configured，符合已知gadget重复枚举问题，独立于APSD。
没有kernel panic记录。原先0120a6的mux pending故障本轮未复现，因此0158隔离
故障分支仍未得到实机覆盖，不能把“没报错”写成已经根治。

### 下一步

1. 不重复baseline、ADC正常bring-up或同镜像APSD开关测试。本轮已足够确认APSD
   开启、source路径正常以及识别实际结果；不盲目重写1362或无限rerun。
2. 先复核ADC RESULT阶段的trigger/ack/交付与deadline。下一代码批在**错误路径**
   通过typed接口保存有界只读status/request/IRQ和时间快照，并核查实际deadline；
   只诊断，不把timeout查询到的完成位当作轮询样本，也不静默重试。
3. 输入侧接Type-C/DPDM/角色与撤销的provider，按LG参考处理FLOAT受限输入及
   debug/legacy/VBUS-error，之后才设计APSD enable/条件rerun和电流合同。
   battery identity/parallel/watchdog仍需闭合，不直接解除充电互锁。

本轮仅分析与文档，没有改驱动/CI、没有新boot或push。复现分析脚本、逐事件时间表
及计数在工作区 `scratch/artifact-0120a8/{analyze-log.py,log-analysis.json,
battery-evidence-persistent.txt}`，原始日志不入git。
