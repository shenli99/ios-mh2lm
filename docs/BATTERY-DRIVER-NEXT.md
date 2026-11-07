# 0119之后的电池驱动推进

**最新实现：0162按LG SMB5修正sink Rp误拒绝，本地完成，待用户push。**
角色先于Rp解释；misc bit4移到独立诊断，不再作为sink直接否决条件，复用1310
字节独立检查USBIN OV/UV。无新寄存器访问/写权限/IRQ路由，没有改mux/keys/USB/
touch或ADC。真实启充互锁仍在；不表示已实现PD或完整input provider。

33文件严格apply/108源码gate，SweetBattery ARM64单文件编译及runtime/mux/
ADC consumer/transport/日志回归通过；0120a10的34笔输入离线回放，7笔一致
Rp3A可建立CC观测，其余拒绝，不是新设备证据。详见
[0162依据、修改与验证](BATTERY-TYPEC-CC.md#0162按lg-smb5修正sink-rp误拒绝)。
用户push后新云产物核对，用原battery-evidence/runtime1一次测试；观察稳定
电源接入CC role1/observed1/reasons0/qualified1及kind2/max-ma3000、拔线撤销，
APSD为FLOAT也可。观测3秒期限未改，不能靠定时读取续许可。无需重跑baseline。
下一步补长期input/Type-C/DPDM及错误保护事件provider；ADC错误路径取证、identity/
parallel/watchdog仍需后续，禁止用本次Rp分类成功直接代替这些依赖。

### 0120a10及更早结果（历史）

**0120a10最新：0161初始化修复已实机通过。** battery group成功attach，Type-C
路由有实际命中，34次运行期IRQ刷新healthy1，13次UI源状态发布，插拔撤销/恢复
正常出现在日志中。不能把内核发布结果直接当作屏幕视觉验收。ADC273次全部成功，
旧第二EOC超时未复现但未修复；本轮没有触发consumer故障撤销分支。

当前输入卡点：7次一致记录已读到Rp3A，misc39含bit4；当前XNU CC规则以
VBUS_ERROR直接拒绝，capability仍0。LG SMB5的sink Rp分类没有同样的bit4
拒绝规则，需要离线核对该位适用角色/保护语义和provider生命周期；不能将当前
软件拒绝直接解释为充电器损坏，也不能直接删检查或清位。APSD实际开启且source
完成已返回FLOAT/SDP，无DCP/CDP。本轮34配置均enable0、无CONTROL，runtime1
未启充。详见[0120a10完整结果](BATTERY-TYPEC-CC.md#0120a10实测irq修复通过rp已读到但输入授权未成立)。

下一步不重复baseline/本轮镜像：先核对输入角色/错误位语义，明确Type-C/DPDM
provider与FLOAT策略接口，再做必要实现与有目标取证；ADC错误路径typed快照、
identity授权、parallel隔离及watchdog仍需后续闭合。现有GIC→mux IOService→
registry client→consumer workloop保持，不引入轮询fallback或高层直接寄存器写。

以下保留0120a8及更早的诊断过程；以顶部最新结论为准。

0120a8实机已确认APSD检测开启（1362=18）且3次source-change完成事件工作，
plugin先读未完成，随后source给出FLOAT/SDP；无DCP/CDP，10次普通sink校验未过。
新问题是USBIN ADC请求196在ground后缺第二EOC处理，超时后测量撤销；并非本轮
mux全局停止，battery IRQ和touch继续。先复核ADC第二阶段/超时并补错误路径
typed快照；输入需Type-C/DPDM/角色provider及FLOAT限流策略，不是盲写APSD使能。
详见[0120a8结果与后续](BATTERY-APSD-EVENTS.md#0120a8实机结果apsd已使能且source路径工作adc5另有超时)。
无需重复本轮/baseline或ADC正常测试。本轮未改驱动/CI，仍不启充。

0159已修正APSD事件来源与时效：0151早已接入source-change bit6，本轮保留路由，
仅初始/plugin/source采输入，防止CHGR/SOC续期；新增只读1362 bit3实际使能观察，
结果前后状态一致性检查及source会话期限。0158+0159一起待push/云编译，继续
原battery-evidence单镜像。详见[APSD事件修复](BATTERY-APSD-EVENTS.md)。尚未
主动enable/rerun APSD，DPDM/Type-C输入provider和真实启充仍需后续实现。

0157已实现固定profile tuple读取与输入快照/时效；0158增加共享mux pending route
取证与按组隔离，待用户push和新云产物。仍不
启充；下一轮同一镜像采电脑/普通5V电源证据，无需再跑ADC bring-up。接口及步骤见
[profile/输入证据实现](BATTERY-PROFILE-INPUT-EVIDENCE.md)，之后继续身份/输入许可
consumer、并联隔离和watchdog依赖。

0120a5后离线核对：实测BAT_ID换算94～95kΩ与Tocad91kΩ匹配，4027mAh容量也
相符；板级FCC覆盖2800mA。下一实现补固定24字节profile前缀及输入类型/模式/
epoch日志与证据消费者，不能用旧ICL或USB configured bool直接授权。源码依据、
范围和最小测试批次见[身份/输入核对](BATTERY-IDENTITY-INPUT-AUDIT.md)。
本轮无需上机或push，仍缺并联隔离/watchdog，不重复ADC正常路径。

0120a5补充：0156的ADC5测量consumer正常路径已实测通过。约24分半、657次
转换/1266次EOC；655个结果被接受，2个因插拔/识别改变epoch正确丢弃，无ADC
超时/ERROR/panic。电池身份、输入能力、并联隔离和watchdog仍缺，充电仍互锁。
详细计数、范围与验收边界见[ADC5 consumer实机结果](BATTERY-ADC5-CONSUMER.md#0120a5-实机结果持续测量与插拔正常路径通过)。

0156实现（现已由0120a5验证正常路径）：SweetBattery 已通过每consumer的
SweetAdc5Client 订阅 ADC5 IOService，自己的workloop发起请求并验证token、
source epoch与时效；插拔撤销旧VBUS，保护monitor维护电压测量，退出同步清理。
没有设置充电授权，身份/输入能力/并联隔离/watchdog仍缺。
接口、时序和下一轮步骤见[ADC5 consumer](BATTERY-ADC5-CONSUMER.md)。

0120a4补充：0155 ADC5 provider实机已得到BAT_ID 889684uV、USBIN 4831136uV，
三次EOC及配置恢复成功，无ADC错误；下一步接入测量provider并核对充电证据。
充电仍互锁，不能把转换成功等同于真实充电。详细数据和回归边界见
[ADC5实机结果](BATTERY-ADC5-PROVIDER.md#0120a4-实机结果转换链路通过)。

0154硬件接口与ADC5只读预检已在0120a3实机正常运行，13字段全部可读，
user observer/writer/IRQ元数据为APID280；calibration未发现本地IRQ owner符合DTS。
策略不再提交地址/掩码，新runtime采样移入内部PMIC组件；0155已实现独立ADC5
IOService/转换/EOC/恢复，0120a4正常转换已通过，下一步接入测量与充电证据。
三类充电证据provider接入与watchdog/pet仍未完成，无需重复预检或先跑dry，
仍不启充。实机步骤见[ADC5 provider](BATTERY-ADC5-PROVIDER.md)，接口见
[BATTERY-HARDWARE-INTERFACES.md](BATTERY-HARDWARE-INTERFACES.md)。

**2026-11-06当前执行计划：** 用户要求把电池驱动实现分段，已编写
[`BATTERY-IMPLEMENTATION-PLAN.md`](BATTERY-IMPLEMENTATION-PLAN.md)：
共享访问与控制接口 → 真实测量/配置 → 策略与保护 → 首次受控充电 → 运行期
事件管理 → 完整上报与回归。下面保留此前诊断过程，不再把“等待SMB_CFG”
当作全部推进计划；0149可随第一批实现合并编译，无须单独消耗一轮。
0150/0151 已随 0120 实测；0152 observer 修正在 0120a1 生效。
0120a2 已验证 0153：首次 DMA WRITE_ERROR 从0x2清到0，温度28.75～30.5°C、
FG nominal/learned4027mAh与profile0x13有效，短日志完整。详见
[BATTERY-FG-DMA-RECOVERY.md](BATTERY-FG-DMA-RECOVERY.md)。无需补live或先测dry。
下一步转向第2～3段剩余的实际电池身份/限值、VBUS/输入能力、并联隔离provider
与watchdog/pet，不继续把FG错误当作当前阻塞。

第4～6段已有可独立实现的控制/事件/上报代码，电池身份/上限、VBUS实测及
并联隔离 provider 尚缺，真实充电未验收、不打包控制档。0120a1 已取得
watchdog `0x62/0x8c` 与 enable0，pet 尚缺；不通过关 watchdog 绕过保护。
当前状态见 [BATTERY-RUNTIME.md](BATTERY-RUNTIME.md)。

## 历史诊断与计划依据

2026-11-05，用户希望全事件日志确认后继续电池驱动。当前USBIN与MSOC的真实
IRQ刷新已通过；CHGR仅接入成功，没有自然状态转换/独立命中。SOC_READY一直
ready，没有人为复位FG制造边沿。电池IRQ子目标保持未完成，暂不进入framebuffer。

2026-11-06先完成历史复核：m19首次采样已enable1，后续才插线进入真实充电；
0113无USB档首次已enable0，说明禁充初态早于IRQ/mux改造。旧成功/当前镜像
沿用完全相同的Mu固件，实际包含高通充电模块，具体启动writer/分支未证明。
当前m20文件与历史SHA不符且等同0113日志，不能继续把它当作已复核的旧成功
证据。详见 [`BATTERY-CHARGING-HISTORY.md`](BATTERY-CHARGING-HISTORY.md)。

**最新诊断结果：** 0119主档九次config/current均有效，全部state7/en0、没有
负IBATT；no-usb对照十一笔同样state7/en0、无负IBATT，九次USBIN刷新正常。
插线INPUT有效、未suspend、ICL配置1500mA，禁充初态早于board/mux/USB初始化。
关闭当前启动的gadget未解决问题，不能只归因于gadget或IRQ化；电源/线一致性
尚待用户确认。0149诊断有效性修复已完成本地验证，待用户push/云编译；未修改
充电使能。完整证据见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。

## 已确定与仍缺的证据

- 0119 mode4：3次独立MSOC命中，拔线期间真实gauge从74%降至73/72/71%，
  与4次USBIN事件分开；没有轮询兜底，按键/触摸继续运行。
- 0119 mode3/4：所有已采样CHGR都是0x47/state7，未开启电流诊断。不能将
  外接电源标志解释为真实充电，也不能将缺少CHGR命中直接认定为IRQ配置错误。
- 旧0112m19/m20已经有真实充电证据：state3/FULLON、enable1、匹配IBATT约
  -2.2～-2.3A、SOC及电压上升。旧镜像未启用现有USB gadget/VBUS override，
  电源/线与启动残留配置也可能不同；不是已经证明的回归原因。
- 标准IOPMPowerSource发布已存在，现阶段不需要重写通知框架或绕过powerd。
  尚缺完整的真实电流/温度发布、容量/profile确认及充电策略管理。

## 当前先做：同内核恢复充电证据链

### 按工作区六章文档复核：完成的是部分诊断，尚未闭合根因

2026-11-05，用户追问是否已找到原因、是否按文档执行。再次核对工作区
`docs/INDEX.md`、第1/3/5/6章及其源码入口后，明确区分：

| 文档要求 | 当前证据或缺口 |
| --- | --- |
| 芯片/版本与实际运行镜像对应 | 已有0119源码核对、REVID与PM8150B身份；最终原厂DTBO/板级差异仍不能仅凭静态DTS结案 |
| 事件到状态发布 | USBIN、MSOC已实测；CHGR没有自然转换，不能验收全部电池IRQ |
| 读取可信度、SID/地址/返回码 | 0149修正短路/假零；尚待原生构建/实机；Type-C仍受软件白名单限制 |
| USB ICL数值与模式/实际限流 | 已知配置1500mA且未suspend；0x1366/1342/1365模式与0x1108 settled ICL未补齐，不能称输入策略已完全核实 |
| CHG_DISABLE到充电使能控制 | 日志确认0x1042 bit0=0；XNU没有充电策略setter，无法主动恢复禁充初态；谁导致初态为0仍未知 |
| 温度、FCC/FV、主/并联路径 | 真实温度、有效FCC/FV和SMB1355运行状态未闭合；没有完整的充电控制实现 |
| 写权限、保护或其他执行环境重写 | 未执行充电控制写事务/回读；不能宣称固件锁、写权丢失或被覆盖 |

参考源码再次确认：`qpnp-smb5.c:smb5_init_hw` 的“enable the charging path”
通过 `vote(chg_disable_votable, DEFAULT_VOTER, false, 0)` 进入禁充仲裁；
`smb5-lib.c:smblib_chg_disable_vote_callback` 将有效结果反向写入0x1042 bit0。
当前SweetBattery只有状态采样/发布；mux对电池route的写接口限制在IRQ偏移
0x11～0x16，不能代替充电控制器。**缺少控制解释了为何禁充不能被本驱动主动
恢复，但不能据此宣称已经找到此前能充、现在不充的具体回归原因。**

另需澄清：0x1648的SMB_EN命令值0意味着override bit3未置位，不等于已经
测得外部SMB1355被关闭。`smblib_smb_disable_override_vote_callback` 在
disable_smb=false时也会清override；需要0x1690模式及实际并联状态配合判断。
因此不能把“en0、smben0”合并解释成两个已经确证的充电阻塞。

目前只完成文档规定的事件/读值诊断子集，不能声称整份充电控制清单已经执行。
0149是修诊断可信度，不是完整充电修复。后续按第6章矩阵补证据并核对第3章
控制依赖；能够本地完成的源码/历史差异检查无需等待新产物。启用控制前仍需
闭合温度、限流/限压、并联和错误退出，不能把所有缺口都归为“等SMB_CFG”。

0119主档和no-usb对照已完成，不重复旧镜像。先构建0149，再使用
`--battery-charge-check` 的主档（保留ACM、mode4与输入IRQ），开启sweetbattdiag1。
以已知可用独立充电器/线重现插入、持续、拔出、再次接入；
保留原始持久日志和电源类型。流程、镜像SHA见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。

判据：独立CHGR命中/events0x2，实际state变化；若要确认物理充电，另须一致
IBATT pair且current-ok1、持续方向与SOC/电压相互支持。一个瞬时负小电流或
插线UI不足以结案。IRQ采样是事件驱动，没有承诺每两秒取得遥测。

新诊断优先补齐SMB_CFG和逐字段有效性，再沿禁充初态调查继承的充电配置。
Type-C当前被软件白名单拒绝，不将unknown当成零，也不为补日志盲目扩大访问。
如后续需要对照，同一次内核编译可生成多档；no-usb以持久日志取证。
不回刷旧0112内核、不改分区，也不盲写0x1042制造状态转换。

## 下一批源码工作：先提高数据可信度，再增加控制

1. **独立采样有效性：0149本地完成，待原生构建/实机。** 已拆开
   `logChargePolicy()` 与DIAG的短路链，逐字段读取并保留错误/有效性；失败显示
   unknown，不伪造零值。已确认Type-C外设0x15被软件白名单拒绝、后续SMB_CFG
   被短路，此次保留访问边界。实现/掩码/测试见
   [`BATTERY-DIAGNOSTIC-VALIDITY.md`](BATTERY-DIAGNOSTIC-VALIDITY.md)。
2. **真实遥测发布。** 在验证寄存器、单位和一致性后，将IBATT/电池温度作为
   带有效性及采样来源的数据接入标准电源字段。FG负电流表示充电，IOPM发布
   符号需要转换；读取失败不能标记有效或触发错误的充电状态。温度映射与LG
   修正、取样时效及适用事件必须先明确，不能简单删除25°C占位后直接猜值。
   电压/电流/温度的量测时效与SOC事件频率分开设计，不把周期遥测伪装成IRQ。
3. **容量/profile。** 核对BLT42厂商、电池ID及已加载profile，再替换5020mAh
   占位；DTS的4000mAh命名本身不等于本机已测容量。同步校正SOCEstimated等
   语义，不影响已经验证的MSOC缩放与防跳变。
4. **充电控制。** 取得有效温度、电源能力、固件现有FCC/FV/ICL和主/并联路径
   状态后，单独设计禁充仲裁、限流、热保护及错误退出。当前不将继承充电
   改成XNU强制使能，不把“补一位enable”作为完整控制器实现。

每次源码改动仍用增量patch与同一次云编译的多档镜像，保留mode4/三键/USB/
触摸回归。服务匹配、SPMI事务锁及mux路由边界继续沿用，不重新引入全局桥接。
资料入口为工作区 `docs/INDEX.md` 六章与LG目标源码；复杂时序回查实际函数。

### 0120a6 结果：profile 已闭合，mux 仍需定位

0120a6 的 0157 证据镜像已完成设备采集。11 次 profile 读取全部有效，Tocad
前缀、integrity `0x13` 和 4027 mAh 一致；输入只观察到 SDP、UNKNOWN 和 OCP，
没有 DCP/CDP，也没有 `SWEET BATT CONTROL`，所以没有真实启充动作。第二次电脑
USB 枚举失败仍属于已知 gadget 问题。

末尾共享 PMIC mux 在 ADC5 token 373 完成后报告 `handled=1 pending=1` 并停用
所有 consumer。该错误会撤销电池/ADC测量和按键 IRQ，但 profile/ADC转换本身在
故障前成功。现有日志未保存具体 pending route，下一步是给 mux 增加 route、APID、
status、ack 后读回和 group 信息，再决定按 group 隔离还是修正 ADC/按键清除顺序。
在故障 route 未确认前不启用充电控制，也不把 USB 枚举问题并入电池修复。
