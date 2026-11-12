# 首次面板资源接管：ldoa14 RPMh超时与0207故障快照

**0208修复已准备（代码`190c558`，本地通过、未实机）：** apps RSC INTID37 经
GIC callback/software IOInterruptEventSource/IOWorkLoop取得fresh完成，再核验命令和清理，
保留原期限及失败保护。hardware ADT新增显式IRQ扩展，不能复用0121a20旧ADT；固件/参数不变。
现在需要用户push云编译；产物核对后重打retained，只先确认启动、真实IRQ和ldoa14 ACK。
详见[中断实现、验证及实机目标](PANEL-RPMH-COMPLETION-IRQ.md)。以下“下一步设计IRQ”按历史保留。

**0121a20已核对打包：** 78源码匹配0207（代码ae78f99）、114 source/164 binary markers与镜像回读通过。
只测新ABI608 retained档，605字节参数/ADT同0121a19；等约90秒导启动日志，取首次RPMh SNAPSHOT。
这是定位ldoa14超时阶段的诊断版，仍可能静帧；接管前不做OFFON/亮度/长测，identity不重测，combined/supply暂停。
见[镜像路径与日志目标](PANEL-D1-0121A20.md)。以下待push/待云编译步骤已完成，实机仍待验证。


2026-11-12，接[0121a19实测](PANEL-D1-0121A19.md#实测结果2026-11-12)。
**已确认触摸启动等待与quiesce成功；新阻塞是第一笔ldoa14事务Timeout。**
0207是取证增量，未宣称修好RPMh或已完成面板接管。代码/测试提交`ae78f99`。

## 0121a20实测：审计警告与完成证据缺失（2026-11-12）

日志`logs/live-0121a20-abi608-panel-retained.txt`，225225bytes / 3119行，SHA256：
`84560e58b3d0f726573be90e2620cb80e6537b95ca06875726f7d9e8476b0109`。底稿`scratch/artifact-0121a20/live-retained-log-analysis.json`。

### 用户指出的SCANOUT警告

607、695、840行均有`live configuration needs review; no handoff authorized`。
本轮对应快照几何/地址/stride/format和既有SMMU route符合audit中的layoutOK/routeOK条件，
但原始`CB2 FSR=0x400`非零，命中`!layoutOK || !routeOK || v.cbFault`的最后一项。

这是`SweetFBScanout.inc::auditScanout()`的只读WARN分支：此分支没有设置Failed、没有中止接管，
仅表示这些只读观察本身不授权新DMA。后续848接受生命周期token，849明确`F4 v2 accepted`，
1651触摸ready/quiesce完成，1657已进入managed LDO链，所以它不是本轮停止推进的分支。
不能反推FSR已证明无害；保留原始状态，不清FSR或删警告来掩盖问题。

### 真正失败阶段已缩小

1679–1684提供了本轮首次故障快照：

| 字段 | 实测值 | 可得结论 |
| --- | --- | --- |
| reason / phase | deadline / completion | 失败在等待完成阶段，不是仍在写payload或收尾 |
| executing / operation / posted | 1 / 0/0 / 0 | 已走过软件trigger写与编程读回队列，没有未完成的posted操作 |
| selected TCS / commands | 0 / 3（mask7） | apps ACTIVE TCS0的三条A14命令 |
| IRQ_ENABLE / saved | 0 / 1 | 请求前bit0使能，被当前实现屏蔽 |
| IRQ_STATUS / TCS_STATUS | 0 / 1 | 没有记录到fresh IRQ，当前TCS空闲 |
| CONTROL / ENABLE / WAIT | 0x1010000 / 7 / 7 | AMC+Trigger读回仍在，三条payload保留，尚未执行清理 |
| command0 | addr42100 / data708 / status10101 | 计划1800mV；状态含ISSUED与COMPLETE |
| command1 | addr42108 / data7 / status10101 | 计划mode7；状态含ISSUED与COMPLETE |
| command2 | addr42104 / data1 / status10101 | 计划enable1；状态含ISSUED与COMPLETE |
| progress | 0 | 软件没有记录到busy或command不完整的执行变化 |
| deadline-late-us | 68 | 失败回调超过原总期限68微秒，不是首timer尚未编程的情况 |

当前`advance()`要求idle、所有command状态完整，且额外满足fresh IRQ或观测到执行变化。
此处status/idle条件符合，`irq&bit || fProgress`不满足，因而不进入清理/提交确认账本，最终Timeout。
**已定位到阻止完成的具体软件条件；尚未证明三条状态属于本轮新ACK，不能直接认定真实供电成功。**

优先怀疑现有mask+timer方案无法可靠保留/观测本轮完成证据：可能完成很快、timer错过busy，
也可能IRQ_STATUS与source使能存在关系；旧CMD_STATUS也不能排除。现有快照不能独立区分这些解释，
不能说已经证明“硬件本轮执行成功、只是软件假超时”。同样不能直接删fresh保护接受遗留状态。
bit0的额外含义未在已读LG完成检查中解码，不新增推断。

1686仍rails0/NoC0/clock0，1687明确未提交新DMA，1696pixels冻结；后段系统活动继续，无panic签名。
本轮11份FG profile及4条USB WARN作为伴随观察保留，不由此转移主任务或称实际充电正常。

### 下一步边界

下一步应核对并改进唯一SweetRPMh provider获取fresh完成证据的方式，对照LG ACTIVE IRQ路径；
若接入apps RSC IRQ，须先核对SPI5/INTID37的已有所有者和GIC接口、事件源/workloop、
source使能/ACK及stop顺序，安装完整接收路径后才能启用source。不能直接开未知IRQ、延长超时、
忙等抢busy窗口或绕过fresh条件。本轮仅日志/源码分析和文档，没有新代码/镜像，不需为本次记录重编。

以下0121a19未知阶段及0207待编译步骤为历史，实测更新以本节为准。

## 已确认与未知

日志1770进入warm managed事务，1773报告token1/ldoa14/0xe00002d6/uncertain1。
真实XNU `iokit/IOKit/IOReturn.h`将0x2d6定义为kIOReturnTimeout。
后续rails=0、NoC=0、clock=0，scanout声明未提交新DMA地址；pixels冻结，系统/USB仍活动。

已通过：ACV metadata、生命周期一次准入/异步等待、触摸worker/IRQ/HID暂停、TE/autorefresh和显示IRQ暂停。
尚未通过：第一笔VRM确认完成、后续三条LDO、ARC/core/NoC/clock、DSI重建及自有扫描地址接管。
确认完成账本为0不等于硬件绝无写入，更不等于电源已经关闭。

旧日志不含选中的TCS编号、编程进度、命令状态、IRQ/idle/control或timer超期量。
所以不能区分命令未发出、硬件未完成、完成证据不足、清理未完成或回调延迟；
也不能直接认定资源地址错、PMIC拒绝电压、或ACV投票失败。此轮尚未走到实际ACV票。

## LG参考核对

参考仓库 `ref/lge-lineage-kernel` HEAD `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。

| 核对项 | LG实现 | 当前实现 |
| --- | --- | --- |
| apps RSC资源与TCS分组 | sm8150.dtsi:2595；0x18220000/0x18220d00，DRV2，ACTIVE2→SLEEP3→WAKE3 | 相同映射，ACTIVE仅0/1，config数量位12、NCPT位27 |
| TCS/command步长及寄存器 | qcom-rpmh-mailbox.c:51–83；0x2a0/0x14，MSGID30/ADDR34/DATA38/STATUS3c | 相同 |
| 写并请求完成 | LEN8、RESP_REQ bit8、WRITE bit16（584–599） | WriteACK=0x10108 |
| VRM资源偏移 | rpmh-regulator.c:95–101，寄存器index0/1/2为mV/enable/mode，按4字节编址 | base+0/base+4/base+8；A14本次计划1800mV、mode7、enable1 |
| trigger顺序 | mailbox.c:611–627，清trigger→清AMC→使能AMC→trigger | 在空TCS/Control0准入后，Control0→AMC读回→AMC|Trigger |
| 完成判定 | mailbox.c:454–476，IRQ驱动后检查ISSUED bit8，需response时另查COMPLETE bit16 | timer读取，额外要求idle及fresh IRQ或已观测执行变化 |
| IRQ路径差别 | ACTIVE IRQ使能，走真实IRQ handler；handler按TCS清IRQ | 现有provider屏蔽所选source，timer取证后清理/恢复原mask |

基础偏移、消息位与启动顺序未发现足以据此确认的错误。当前屏蔽source时IRQ_STATUS是否保留fresh状态、
以及异步timer能否错过短暂busy状态，仍需实机寄存器证据；不能仅凭LG启用IRQ就断言这是本次根因。
不直接启用未接入GIC处理的source，也不删掉fresh条件接受可能遗留的CMD_STATUS。

## 0207实现范围

`xnu/patches/0207-mh2lm-rpmh-failure-snapshot.patch`仅修改`SweetRPMh.cpp::advance()`。
局部lambda在**终止故障**时采集已映射、选中ACTIVE slot的只读状态；不新增成员、provider接口或writer。
触发点只有总/ARC阶段期限、posted-write读回期限、无效IRQ/idle读数。原返回值和状态更新保持。

- `SWEET RPMH SNAPSHOT`全部为ERROR，无条件输出；一次失败最多6条，不每个timer周期打印。
- `phase=program/completion/cleanup`区分软件停在哪段；request=VRM/ARC/CN0，rail仅对VRM有意义，
  arc-stage仅对ARC有意义，written为provider本次启动内曾写入的标志；TCS编号、command count/mask、operation/count、posted、executing、progress、written。
- 总期限/ARC阶段/posted-write超期微秒数，用于识别“定时器到达时已经迟到”。这不是单独的硬件执行耗时。
- IRQ_ENABLE及请求前保存mask、IRQ_STATUS、TCS_STATUS、CONTROL、CMD_ENABLE、WAIT。
- 最多三条当前请求命令的MSGID/ADDR/DATA/CMD_STATUS。无扫其他TCS，无访问command DB以外的新地址。
- posted-write失败另外打印原操作offset/want/got/W1C；W1C读回仍读IRQ_STATUS，不读IRQ_CLEAR。
- 紧接原finish日志提供token和资源名称。寄存器是顺序取样，不保证原子一致。

stop、取消/participant失效入口、未busy/不再accepting入口不执行新快照；失败后晚timer也无新读写。
不增加超时、重试、复位、ACK、触发或其他写入；原100ms、ARC总500ms/阶段100ms、读回1ms均不变。
原IRQ mask/fresh完成、共享电源保护、已确认/不确定票保留、pool保留和失败冻结保持。

这仍按typed consumer→SweetRPMh IOService/gate/timer→唯一apps RSC writer调用，未让面板consumer穿透寄存器。
没有改CircleCI、固件、boot参数、USB、按键、触摸worker、电池或mux。

## 本地验证

- 6套真实源码ASan/UBSan回归通过：RPMh资源、display NoC、managed display、panel lifecycle、
  panel supply fences、panel supply lifecycle；无未释放对象/映射。
- RPMh新增覆盖首timer超过总期限、已确认完成后清理timer迟到，核对phase、每次最多6条快照、
  无新写入、stop无快照、W1C无读、失败后晚到timer无MMIO；原posted/IRQ清理失败和失效读也检查诊断。
- 真实CI头/KDK ARM64单文件修改前后均编译通过；warning1326→1326，无新增。
- 0185–0207严格累计应用无offset/fuzz，78源码字节一致、114现有CI源码marker通过。
  CircleCI blob仍`da0b6c0947a66f03491b3a9023590b49665292df`。
- 底稿`scratch/framebuffer-0207/{verification,tests,cumulative-replay,warning-comparison}.json`及编译日志。
  模型验证不证明真实TCS完成语义，也不等于整核/实机验收。

## 下一轮最小测试

需要用户push本轮本地提交并云编译，下载新kernel/buildtree后核对0207字符串与实际源码，
沿用原ABI608固件、605字节参数/ADT，打新标签retained镜像。旧0121a19和identity无需重测。
启动等约90秒，导一份完整USB或持久启动日志即可；不需手动插拔、调亮度、息屏或长测。
本版没有解除已知超时，仍可能保持静帧；目标是第一次失败前的SNAPSHOT及紧接token/ldoa14结果。
若phase=program，先看operation/posted及读回；completion看IRQ/idle/每条ISSUED和COMPLETE及progress；
cleanup看尚未读回的收尾操作。若完整managed成功再按原计划同镜像验证显示。
combined/supply继续暂停。禁止用延长期限、接受旧状态或ABL回退代替定位。
