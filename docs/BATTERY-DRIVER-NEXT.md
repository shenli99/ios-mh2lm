# 0119之后的电池驱动推进

**2026-11-06当前执行计划：** 用户要求把电池驱动实现分段，已编写
[`BATTERY-IMPLEMENTATION-PLAN.md`](BATTERY-IMPLEMENTATION-PLAN.md)：
共享访问与控制接口 → 真实测量/配置 → 策略与保护 → 首次受控充电 → 运行期
事件管理 → 完整上报与回归。下面保留此前诊断过程，不再把“等待SMB_CFG”
当作全部推进计划；0149可随第一批实现合并编译，无须单独消耗一轮。
第1段0150受限SPMI接口与一次只读预检已完成本地验证，尚未云编译/实机；
没有运行期充电写入。实现边界见[BATTERY-SPMI-TRANSPORT.md](BATTERY-SPMI-TRANSPORT.md)，
下一步按[BATTERY-TELEMETRY-CHECKLIST.md](BATTERY-TELEMETRY-CHECKLIST.md)补真实温度、
配置、输入/电池约束与并联证据。0149/0150随首批合并，不要求现在单独编译。

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
