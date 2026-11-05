# 0119之后的电池驱动推进

2026-11-05，用户希望全事件日志确认后继续电池驱动。当前USBIN与MSOC的真实
IRQ刷新已通过；CHGR仅接入成功，没有自然状态转换/独立命中。SOC_READY一直
ready，没有人为复位FG制造边沿。电池IRQ子目标保持未完成，暂不进入framebuffer。

**最新诊断结果：** 0119主档九次config/current均有效，全部state7/en0、没有
负IBATT；插线INPUT已有效、未suspend、ICL1500mA。禁充初态在board/mux/USB
控制器启动前已存在，不能直接归因于IRQ化。下一步已备no-usb镜像做同电源
对照，未修改充电使能或内核；完整证据和操作见
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

使用 `--battery-charge-check` 的主档（保留ACM、mode4与输入IRQ），只开启
现有sweetbattdiag1。以已知可用独立充电器/线重现插入、持续、拔出、再次接入；
保留原始持久日志和电源类型。流程、镜像SHA见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。

判据：独立CHGR命中/events0x2，实际state变化；若要确认物理充电，另须一致
IBATT pair且current-ok1、持续方向与SOC/电压相互支持。一个瞬时负小电流或
插线UI不足以结案。IRQ采样是事件驱动，没有承诺每两秒取得遥测。

主档仍state7时，先从有效的INPUT/CHGR/IBATT记录区分电源输入和充电使能；
必要时才用已备的no-usb对照，隔离gadget/VBUS override这组软件条件。
相同充电器/线/操作比较，单次差异仍不能排除启动固件残留状态。no-usb没有ACM，
以持久日志取证。不回刷旧0112内核、不改分区，也不盲写0x1042制造状态转换。

## 下一批源码工作：先提高数据可信度，再增加控制

1. **独立采样有效性。** 拆开现有 `logChargePolicy()` 与DIAG的布尔短路读取链，
   每个输入/Type-C/SMB/CHGR字段保留自己的read-ok及错误信息。失败之后不能
   把未读取的零值打印成已知状态。首次新patch应优先完成此项，而不是以合并
   read-ok0推断SMB_EN。依据：0112m19已记录此缺口，0119源码仍存在。
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
