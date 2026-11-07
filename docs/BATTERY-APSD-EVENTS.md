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
