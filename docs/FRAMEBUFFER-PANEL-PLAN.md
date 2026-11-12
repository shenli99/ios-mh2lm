# MH2LM 显示/面板驱动重构计划（大目标二）

**0205当前修复（2026-11-12）：** 按用户决定将display ACV建模为 `0x8` 开关资源；
保留真实width0/unit1/VCD11，EBI有需求发送X0/Y8，无需求撤票。普通BCM缩放/非零width检查保留。
本地验证通过，待push云编译及新retained镜像实测；不是再次只加诊断，也不代表面板接管已验收。
本地DTS无mask属性；LG apps有mask8/X0/Y8，本次显式应用到display，不搬跨BCM累计副作用。
详见[实现、依据、验证与下一步](PANEL-ACV-ENABLE-MASK.md)。
以下此前“不能套apps mask/继续查投票策略/无编译需求”等为历史判断，由本段取代。

**0121a17诊断已取得：** USB日志867行ACV实际unit1/width0/VCD11/reserved0，当前统一
非零width检查导致拒绝，随后managed接管失败。无需补持久日志/重测；下一步核对LG display
ACV构票语义并修正模型，不能直接替换width或套apps mask。当前无新编译需求。
见[实测字段与修复边界](PANEL-D1-0121A17.md#usb实测诊断目标已取得拒绝条件为acv-width0)。
以下镜像步骤为已完成历史。


**0121a17当前执行点：** 已核对0204 ACV metadata诊断：78源码字节一致、114 source/154
binary markers及ABI608镜像回读通过。只启动`boot-16k-0121a17-abi608-panel-retained.img`，
停留约一分钟导日志，目标取ACV具体gate/字段；这不是ACV修复版，仍可能显示静帧。
无需息屏/插拔/长测；identity不重测，combined/supply继续暂停。
见[0121a17镜像与步骤](PANEL-D1-0121A17.md)。以下旧执行点按历史保留，以本段及NOTES为准。


**0121a16当前执行点：** 新产物已核对0203 ESC0旁路修复；78源码/114 marker及ABI608
镜像回读通过。只启动`boot-16k-0121a16-abi608-panel-retained.img`，停留约一分钟导日志，
先不按息屏；确认后续BYTE/PCLK准入、MDSS发布与接管后再安排亮度/OFFON。
combined/supply继续暂停；旧0121a15不重测。见[0121a16镜像与步骤](PANEL-D1-0121A16.md)。
以下0121a14/15记录为历史，不表示新修复已实机通过。

**0121a14 retained实测失败（2026-11-12）：** 可进系统，但亮度滑条不生效，按息屏后画面冻结。
日志第85行MDSS在warm clock lease准入返回NotReady（0xe00002d8），start未完成、未发布服务；
CoreBrightness请求已到（3304–3339行），第3624行FB生命周期准入失败后主动冻结pixels。
冻结后仍有按键/触摸及进程活动，未证实整核死锁；完整OFF/ON、reset及触摸恢复尚未开始。
具体GCC/GDSC/root/branch失败条件旧日志未记录，不能猜寄存器值或放宽保护。
新增0202仅补首次准入的有界分阶段诊断，错误为ERROR、读值为INFO，运行期检查保持安静；
不新增寄存器读取、写入或fallback。需用户push/云编译后打包新retained档取证，尚未修复硬件准入。
**暂停旧retained复测及combined/supply；E7/E8尚未验收。** 详见[retained故障记录](PANEL-D1-0121A14.md#retained启动准入失败)。

**以下0201及更早内容按各阶段保留，当前执行点以上段为准。**

**0201当前增量（2026-11-12，本地通过，未实机）：** 统一协调器新增显式
`sweetpanellifecycle=6`：完整OFF→reset实际低保持→原mode5 DATA/link停驻→VPNL/VDDI
逆序撤本panel票；ON先active-low→VDDI ACK+20ms→VPNL ACK+5ms，再恢复DATA/link/clock/
host/reset/完整ON/亮度/fresh像素/触摸。共享L3/L5、ARC、config floor和boot/RSC/core通路保留。
每ACK更新部分完成账本，失败先同步取消RPMh子事务再撤Pins/Touch token；FB核对联合证据。
原0–5保留，模式6默认关闭。撤本票不是物理rail-off证明，E7/E8仍待实机。
23套检查、8个真实XNU/KDK arm64单文件对象通过；**完整Mu mh2 RELEASE FD现已构建并核验**，
包含原0195身份adapter，旧blob未改。新`--panel-lifecycle`支持同核identity/retained/combined/
supply四档，先核对live身份，再逐级验收；缺新固件manifest或内核marker就拒绝打包。
现在需要用户push并云编译，下载后再打实际boot；CircleCI未改，未push/触发CI/操作设备。
详见[供电生命周期、架构、四档与证据](PANEL-SUPPLY-LIFECYCLE-D1.md)。
PD4冷NoC/MDP、ABL首帧/console/panic退出、故障后受控释放及完整IOPM仍缺；完整驱动未完成。
**以下0200及更早段落是各阶段历史记录，当前状态以本段及NOTES顶部为准。**

**0200供电依赖校验增量（2026-11-12，本地通过，未实机）：** Pins 新增实时 reset-low
查询，RPMh 新增仅 VDDI/VPNL 的 typed panel vote/取消接口，逐写核对同代 Pins/Touch token，
保留依赖引用，异常停止并保留部分事务；不撤共享 L3/L5。22套检查及八个真实 arm64
单文件对象通过。**尚未接入协调器供电释放/恢复，未新增档位，mode0–5正常路径不变。**
下一步接 LG 顺序、部分撤票账本及联合完成证据；E7/E8仍未实机。详见
[接口、锁顺序、失败边界及验证](PANEL-SUPPLY-FENCE-D1.md)。无新 FD/boot、push/CI/设备操作，
CircleCI 未改，暂不为接口单开云编译。以下旧阶段按各自范围保留，当前以此段及NOTES为准。

**0199供电归属增量（2026-11-12，本地通过，未实机）：** 已确认DSI host L3/PHY L5
与MH2LM UFS及USB PHY共用；这些驱动尚未在SweetRPMh登记依赖，不能按显示最后一票关电。
provider新增typed `lastDisableRequiresHandoff`及mask0xc，在任何写入/token/账本修改前拒绝
这两路最后aggregate disable，保留原票和完成状态；非最后撤票及panel两路原接口保留。
21套检查与RPMh/MDSS/FB六个arm64单文件对象通过。没有新增档位或实际撤电，0198 mode5仍为
当前组合休眠路径。详见[供电图、源码依据、保护与剩余项](PANEL-SUPPLY-OWNERSHIP-D1.md)。
下一步保留L3/L5，补LG顺序的reset-low及panel VDDI/VPNL受控撤票/恢复；E7触摸与E8物理
共享/偏压尚未验收。无新FD/boot、push/CI/设备操作，CircleCI未改，暂不单开云编译。
以下0198及更早描述按当时范围保留；完整驱动仍未完成。

**0198当前增量（2026-11-12，本地通过，未实机）：** 新增显式`sweetpanellifecycle=5`，
把数据票停驻与host/PHY下电接入同一事务。OFF：完整OFF→config floor Held→managed clock
Released→host/PHY/PLL真实关闭→managed DATA NoC Released；ON：DATA Held→PHY/PLL/root
重建→managed clock Held→host重建→面板reset/完整ON/亮度/fresh像素/触摸恢复。
boot/RSC clock、config floor和LDO/ARC/core配置通路保留，暂停mask仍区分票与物理就绪。
仅协调器当前指定的DATA请求可在Dormant沿保留配置通路改票，普通客户端仍拒绝；FB必须同时
核验数据与链路完成证据。原0–4语义保留，无降档。21套检查和MDSS/FB真实arm64单文件编译
通过，严格重放/114原CI marker通过；详见[组合事务、架构和边界](PANEL-COMBINED-SUSPEND-D1.md)。
完整驱动仍缺正常rail-off/故障释放、冷NoC/MDP、ABL退出与完整IOPM；下一步先厘清E8共享电/
偏压所有权并推进受控供电释放。未实机live profile/E7/E8；无新FD/boot、push/CI/设备操作，
CircleCI未改，暂不要求单开云编译/刷机。以下0197及更早段落保留当时范围，当前以0198为准。

**0197当前增量（2026-11-12，本地通过，未实机）：** 协调器新增显式`sweetpanellifecycle=4`：
完整OFF后实际关闭host、BYTE/PCLK分支、PHY PLL/bias和PHY blocks，确认停稳；ON复用原执行层
重新reset/配置PHY、PLL、root和host，再执行面板reset/ON/亮度/fresh像素/触摸恢复。
全部资源票保留，clock查询另报hardwareSuspended，暂停期间普通接口拒绝操作；不是rail-off。
原0/1/2/3语义保留，无降档；额外link使用者/DSI1 alias在DDI副作用前拒绝，子timer重查fence和期限。
20套本地检查及MDSS/FB真实arm64单文件编译通过，严格重放/114原CI marker通过。
详见[硬件下电、重建与架构](PANEL-LINK-POWER-D1.md)。完整供电释放、冷NoC/MDP及ABL退出仍缺，
live profile/E7/E8未实机；无新FD/boot、push/CI/设备操作，CircleCI未改。暂不单开云编译/刷机。
下一步组合资源票释放与硬件下电，继续完整驱动。以下0196及更早段落保留当时范围，当前以0197为准。

**0196当前增量（2026-11-12，本地通过，未实机）：** 协调器增加显式`sweetpanellifecycle=3`：
完整OFF后取得独立config-only NoC票，再释放managed clock/DATA NoC票；ON先重新取得新票，
再执行原reset/完整ON/亮度/fresh像素/触摸恢复。CN0 300MB/s floor、boot/RSC clock、
LDO/ARC/core/host/PHY继续保留；释放本票不等于物理关钟/断电，也不撤其它active票。
原0/1/2语义保留，无mode2回退。资源/clock/NoC各推进点重查同代fence及硬件静止/期限，
FB要求新能力与资源释放/恢复证据。19套本地检查及MDSS/FB真实arm64单文件编译通过，
严格重放/114个原CI marker通过；不代表整核链接或硬件结果。
详见[资源票释放与恢复、架构及边界](PANEL-RESOURCE-PARKING-D1.md)。
PD3物理host/PHY/供电关闭、PD4冷NoC/MDP及独立重建、ABL退出仍缺；共享供电E8未关闭。
启动侧仍须合入0195新DarwinBoot；本轮无新FD/boot、push/CI/设备操作，CircleCI未改。
暂不为此单开云编译/测试，继续完整驱动优先。以下0195及更早段落保留当时范围，当前以0196为准。

**0195当前增量（本地通过，未实机）：** 已把四版LG完整ON/OFF表、Sleep In/Out、typed GPIO reset
和亮度恢复接入原协调器，新增显式`sweetpanellifecycle=2`，保留全部供电/ARC/NoC/clock票。
原0/1语义不变；2要求经实际Mu→DarwinBoot→ADT链路传递的唯一ABL选择及pins2等依赖，
缺失/歧义/未知就拒绝，不猜PV。启动侧另交付DarwinBoot补丁，旧shim没有变更；仅新内核配旧UEFI
不会得到身份。本机live选择及reset恢复仍未实测。FB核对完整模式能力与reset/亮度/像素证据，
pins新增consumer/token取消、停timer保留部分状态。18套检查、arm64对象及Mu消费的DarwinBoot模块
编译链接通过；不代表整核/完整FD/实机。详见[当前PD3架构与边界](PANEL-PROFILE-D1.md)。
完整反向释放、冷NoC/MDP、独立重建与ABL退出仍缺；A单缓冲/30Hz复制不变。暂不要求push/刷机，
CircleCI未改。以下0194及更早段落保留当时范围，当前以0195和NOTES顶部为准。

**0194当前增量（本地通过，未实机）：** 已接通SweetMDSS内部统一生命周期协调器，复用
原唯一gate/timer及资源/扫描/DSI执行层。显式sweetpanellifecycle=1把实际温态接管与28/29
OFF/ON串成typed token事务：触摸暂停fence→显示DMA/IRQ真实静止→原资源/链路/像素完成→
触摸验证恢复；完成前FB不交付Active/不呈现。快速相反电源请求合并，取消/provider撤回/超时/
stop终止本代、保票/保DMA pin，无自动恢复。背光Busy保存最新目标，原生IOKit interest仅排队
自己的事件，事务完成后重试，无空闲轮询。默认关闭，未知profile的prepare/reset/unprepare
在副作用前Unsupported，不调用pins activate/reset/park、不猜vendor表。16套本地检查、
真实XNU/KDK arm64对象与严格重放通过；详见[协调器架构、接口和边界](PANEL-LIFECYCLE-D1.md)。
完整reset/ONOFF/反向释放、冷NoC/MDP、独立重建和ABL退出仍未完成。后续继续完整驱动优先，
之后零拷贝；暂不要求push/刷机，CircleCI未改。以下0193及更早段落均按当时范围保留，
当前以0194及NOTES顶部为准。

**0193当前增量（本地通过，未实机）：** 已补`SweetTouchPanel : IOService`及MDSS真实
匹配绑定，typed暂停/恢复交给原唯一TouchComm worker；消费者不穿透读写寄存器。
暂停fence要求peripheral/GIC禁用读回、active退出和HID触点释放；parked worker/晚IRQ无硬件I/O。
恢复复用实际识别/app/格式读回再arm；无touch reset/电源/固件写入，无轮询回退。
C后端是无实例指针/回调的原子mailbox，未把整个触摸驱动转换成IOService；stop取消是异步收尾，
不伪造硬件静止证明。默认关闭，MDSS本批仍仅绑定，未实际调用pause/reset/完整ONOFF。
14套本地检查、arm64单文件对象和严格重放通过。详见[触摸协调实现与边界](PANEL-TOUCH-D1.md)。
下一步接统一生命周期事务及已确认panel profile；E1与E7实机共享供电证据仍缺，完整驱动未完成。
以下旧段落按当时范围保留，当前以0193及NOTES顶部为准；暂不要求push/刷机，CircleCI未改。

**0192当前增量（本地通过，未实机）：** 已补独立`SweetPanelPins : IOService`及MDSS真实
registry匹配绑定，管理GPIO6/SOUTH reset与GPIO8/NORTH TE。typed请求在provider workloop/
gate/timer执行，分别确认输出latch与实际输入，reset最短10/2/10/5ms；支持park后低保持重激活。
默认关闭，MDSS本批仅绑定，不调用activate/reset/park；实际启动尚未复位面板。
13套本地检查及arm64单文件对象通过，严格重放和114个原CI marker通过；CircleCI未改。
详见[引脚服务架构、时序与边界](PANEL-PINS-D1.md)。PD3仍需实际panel身份、触摸协调、
完整vendor ON/OFF及资源反向释放，整屏冷初始化/ABL退出未完成。继续完整驱动优先，暂不要求push/刷机。
以下旧段落按当时范围保留，当前以0192及NOTES顶部为准。

**0191当前增量（本地通过，未实机）：** 实际 managed 接管已在资源完成后、host 重建前，
加入 PHY v4.0 / PLL 7nm-v2 复位重配和 BYTE/PCLK 时钟重建。沿原 MDSS owner/gate/DSI timer，
显式 `sweetdsilink=2` 要求 host1/core2/managed1；默认0保留0190行为。
先分别确认分支/root 停稳、旧 lock 清除，再配 PHY/分数频率/SSC/分频；新 lock、RCG 更新和
实际运行分别确认，最终仍需 host 重建及 fresh 像素完成。12套本地检查和arm64对象编译通过。
profile 为 LG 板级选择，原始 revision byte 记录不当作自动识别；尚无新实机写入证据。
面板 reset/vendor ON/OFF、反向释放、整屏冷启动及 ABL 退出仍缺，完整驱动未完成。
详见[PHY/PLL/输出时钟实现与验证](PANEL-LINK-D1.md)。下一步面板生命周期/独立重建，
完整驱动优先、之后零拷贝；暂不单独要求 push/刷机，A单缓冲/30Hz拷贝限制不变。
以下0190及更早段落按当时范围保留，当前以0191及NOTES顶部为准。

**0190当前增量（本地通过，未实机）：** 实际 managed 扫描接管在 NoC/DSI0 lease 完成后，
接入 host v2.3 软复位和明确的 RGB888 command-mode 配置重建，再继续原自有 A/fresh 像素完成。
复用 MDSS 唯一 owner/gate/DSI timer；默认关闭，显式 `sweetdsihost=1` 要求 managed1/core2
及原依赖。失败保留资源/部分状态、关闭后续写入，无自动恢复旧配置。PHY/PLL 仍继承，
未发送 vendor 表或操作面板 reset；没有整屏冷启动/独立重建证据，完整驱动仍未完成。
详见[host实现与验证](PANEL-HOST-D1.md)。下一步补 PHY/PLL 和完整面板生命周期；
完整驱动优先、之后零拷贝，暂不单独要求 push/刷机，旧性能限制不变。
以下旧段落按当时范围保留，当前增量以0190为准。

**0189当前增量（本地通过，未实机）：** 显式 core profile2 已将 GDSC 软件票、两颗核心
PLL、AHB75MHz/MDP460MHz 的配置与完成检查接到实际 managed 扫描资源链，复用 MDSS
唯一 owner/gate/clock timer；先实际 ARC/CN0 ACK，失败保票并关闭写入。工作 PLL 保留，
关闭 PLL 才初始化；不改 DSI PHY/PCLK/BYTE。冷态核心引擎已做组件测试，但真实 start 仍为
温态入口，尚无整屏冷启动/完整断电恢复或撤票。详见[核心资源控制](PANEL-CORE-D1.md)。
默认关闭，无新刷机轮次，继续补 DSI host/PHY/PLL 和面板完整 ON/OFF；D1 未整体完成。
以下旧段落按当时范围保留，当前增量以0189为准。

**0188当前显示进度（本地验证通过，未实机投票）：** 已把四路LDO、ARC、CN0/display NoC与
本地clock事务接到实际自有扫描接管，显式managed模式在暂停/idle后顺序获取资源，全部完成
才pin/提交A，并继续要求fresh传输完成。部分失败保留票并关闭写入，不自动恢复旧流。
默认关闭，DCS OFF暂不撤票；冷GDSC/时钟父源、PHY/PLL/host及完整面板ON/OFF仍缺，D1未完成。
见[资源获取与失败边界](PANEL-MANAGED-D1.md)、[NoC](PANEL-NOC-D1.md)、[ARC](PANEL-ARC-D1.md)。
下一段补冷域/时钟/链路，再完成资源反向释放及独立重建；暂不单独要求push或刷机。
下列0185/0184段落保留各自历史范围，当前增量以上述0188为准。


**0185本地时钟增量（本地验证通过，未实机启用）：** SweetMDSS内部已加入typed本地分支lease、
共享引用、token、timer完成/超时和停止保护；温态校验继承GDSC，默认关闭。
仅对已确认XO父源的关闭分支支持恢复/撤回，既有enable不关；没有MMCX/NoC票、GDSC冷上下电、
PLL/PHY/host初始化，不能验收完整D1。另修复OFF跨呈现间隔统计，pending年龄保留。
实现与本地验证见[时钟记录](PANEL-CLOCK-D1.md)；继续合并上游依赖/链路，不单独要求push或刷机。


**0184资源层增量（本地通过，未上机）：** 已新增真实匹配的SweetRPMh IOService、唯一
MH2LM apps-DRV writer、有界timer完成与typed consumer投票；MDSS自己的workloop绑定/查询，
没有匹配回调初始化、全局C实例桥或自动电源写入。v1 ADT扩完整DISPCC并追加单一PHY/PLL窗口，
所有权归MDSS；实际clock/GDSC获取、MMCX/NoC、PHY/PLL/host与面板完整初始化仍缺。
见[资源层实现、释放规则与验证](PANEL-POWER-D1.md)。D1未整体完成，继续合并下一驱动段，暂不要求push/刷机。

**2026-11-10优先级调整：用户要求完整面板驱动优先，随后零拷贝。** 不再以拷贝双缓冲、
60Hz CPU拷贝或完整F4性能收口作为PD2/PD3前置。共用DMA停止/完成与生命周期保护仍保留，
随资源、DSI/PHY/PLL和面板初始化实现一起推进；F4拷贝双缓冲改为兼容路径按需。
0121a12已被用户明确反馈滑动卡顿明显加重，只有接管/唤醒日志通过，不作体验验收。
具体D1–D4批次和合并计时项见[更新后的驱动交付顺序](PANEL-FULL-DRIVER-PLAN.md#7-加速后的交付批次与性能回归)。
0183已实现D1的typed命令编码、完整长包传输、可撤销序列及可选计时，
见[代码架构与验证](PANEL-COMMAND-D1.md)；没有自动发送vendor表，完整资源/链路初始化未完成。

**2026-11-10目标修订：必须提供完整面板驱动，不以接手 ABL 扫描配置作为最终形态。**
新增[完整面板驱动子计划 PD0–PD5](PANEL-FULL-DRIVER-PLAN.md)：利用现有自有扫描证据，
优先接管控制器、DSI/PHY/PLL、供电/复位、DDIC初始化，证明断电后的独立重建。
当前顺序为已有PD0/0182 → PD2/PD3 → PD4 → PD1剩余项/PD5 → F5 → F7；最终扫描、恢复和
控制台/panic 都不能依赖 ABL 缓冲。原 F0–F3 证据保留；0181最新实测状态见下段。

**PD0静态设计已交付：** [资源与接口清单](PANEL-RESOURCES-PD0.md)明确DSI/PHY/PLL版本、
RPMh电源与现有长命令传输缺口、资源所有者、初始化/回滚和E1–E8证据门槛。
本机panel版本等仍待确认，不据静态分析或0181预检宣称完整接管。

**F4首段实测（0181，0121a11）：** 两缓冲分配/一次seed/四快照及输入、OFF/ON回归通过，
用户未见新增问题。CB2.M0；FSR原始0x400不命中LG的FSR_FAULT位，保留复核，不当新DMA故障
或可达性证明。0182受控单缓冲接管已完成本地实现及验证，0121a12 single接管/唤醒日志通过，用户已报告滑动卡顿加重，其它视觉项未单独确认；
新`own2`经typed后端有界暂停/提交/观察，成功后CPU改写A，30Hz仍在，提交池保留到重启。
详见[接管记录与本轮测试](FRAMEBUFFER-HANDOFF-F4.md)及[F4第9节](FRAMEBUFFER-SCANOUT-F4.md#9-0121a11扫描池预检实测首段运行通过新dma尚未验证)。
首段新增`SweetScanoutPool`、有界ABL画面
复制和typed只读扫描快照，默认`sweetfbown=0`；own1申请两个缓冲并预检，仍由ABL扫描。
核对LG DRM/SDE后纠正原计划：`SSPP_SRC_ADDR_SW_STATUS(+0x70)`是安全属性，不是生效地址；
单独下一次RD_PTR也不能充当旧缓冲释放fence。首段不写扫描地址，不提前实现或验收翻页。
实施顺序/源码依据/同核两档与后续完成事件设计见[F4实施记录](FRAMEBUFFER-SCANOUT-F4.md)。

**F3验收基线（2026-11-10）：0121a11完整硬件vsync档通过，F3正常使用路径验收通过；已开始上段F4首段。**
FB首真实帧及唤醒后均进入hardware active，无显式回退；十个完整窗59.874–59.915Hz，
其中唤醒后三窗稳定，OFF帧/IRQ计数不增长。用户未发现异常，见[F3第12节](FRAMEBUFFER-VSYNC-F3.md#12-0121a11完整硬件档f3正常使用路径验收通过)。
默认参数仍关闭，CPU拷贝30Hz仍保留。F4先核对新扫描内存的live SMMU/缓存/可达性，
再推进自有扫描缓冲、硬件裁剪和翻页。生命周期特殊路径未全部上机覆盖，BUG-003/F0未闭环项仍保留。

以下为历史阶段记录，当前结果与下一步以上段及F3第12节为准。

**0180实现时进度（2026-11-09）：** 实际MDSS GIC115/RD_PTR IRQ及typed硬件vsync已实现、
通过本地arm64对象编译与ASan/UBSan验证，待云编译及实机验收。新行为默认关闭；完整档首次
真实帧后切硬件vsync、OFF停通知、失败明确回退软件。一次编译可分GIC/RD_PTR/完整档测试，
见[F3实现与测试](FRAMEBUFFER-VSYNC-F3.md#8-0180真实-irq-和硬件-vsync本地验证待实机)。
以下0179及更早描述保留对应阶段边界；不表示0180尚未实现，也不提前认定F3验收通过。

**状态（2026-11-09）：F1a/F1b正常使用路径已验收，0121a9基础28/29及长时电源键唤醒已验证；F3只读预检已由0121a10实测通过，0180真实MDSS IRQ/vsync已实现待实测；完整插拔唤醒次数未补齐，隔离后的充电唤醒证据无效。BUG-003继续暂缓，F0保留未闭环项。** 本文是 `IRQ-FRAMEBUFFER-PLAN.md`
“大目标二”的实施文档。第二版目标从“修好接手的 ABL framebuffer”提升为：**驱动自己
分配和管理扫描缓冲（simplefb 式），硬件双缓冲翻页，CoreAnimation 渲染的 surface
直接被 MDP 扫描，消除每帧 CPU 拷贝。** 2026-11-10进一步要求完整硬件与面板初始化，
不依赖 ABL 遗留的缓冲和有效寄存器配置；新增 PD 子计划为本目标必要验收项。

原顺序为F0→F7，现按顶部插入PD子计划；用户2026-11-09授权的例外是将BUG-003暂缓，使用已取得的硬件/接口
证据进入F1。F0的正常iPhone UI、模式ABI因果及半透明根因未闭环，不能标记修复或全部验收。
后续仍逐步验收、记录进度；每步新行为由boot-arg打开，默认路径在该步验收前不变。

**F2当前实现（0177）：** 新增默认关闭的sweetfbpower，AppleCLCD通过typed后端请求，
SweetMDSS gate串行DCS28/29及故障恢复；不擦像素，保留独立亮度路径。详细状态/失败
边界、LG依据、本地17项检查及上机步骤见[F2实施文档](FRAMEBUFFER-POWER-F2.md)。
0121a9实际3组off/on均成功（0～11ms，毫秒取整），用户确认显示正常；旧BUG-002电池组隔离/BUG-004另记。同镜像长测已完成，显示/亮度/触摸正常，见[F2长测](FRAMEBUFFER-POWER-F2.md#0121a9-长测手动息屏后正常唤醒)。完整插拔矩阵仍保留边界；BUG-002由0178代码修复，用户决定不作专项复测，后续正常使用观察。用户决定继续F3；0179只读预检已由0121a10实测通过，真实硬件vsync尚未接入，CI配置未改。

**历史进度补充（0121a6）：** F1a资源/背光迁移的正常运行路径功能验收通过，用户确认亮度
跟随及息屏唤醒正常，日志确认新服务/绑定/DCS/3轮off-on及输入工作；进入F1b，F1整体未完成。
真实服务终止路径及精确亮度端点未上机核验，范围见[F1a实测](FRAMEBUFFER-SERVICE-F1A.md#0121a6-实测f1a正常运行路径功能验收通过)。
BUG-004在0121a5两档同链复现，独立跟踪；0121a6未复现不视为修复，不安排重复充电A/B。

**F1b实现补充（0176）：** AppleCLCD registry consumer、标准用户接口dispatch校验、状态
command gate及停止清理已实现；0121a7 gated日志及用户反馈确认正常使用路径验收通过，sweetfbservice默认仍0。
详见[F1b架构与验证](FRAMEBUFFER-SERVICE-F1B.md)。F1a/F1b正常使用范围收口，真实provider终止/重匹配仍未上机核验；下一步F2。CI配置未改。

0121 结论与证据见 [F0 实测报告](FRAMEBUFFER-F0-0121.md)：普通档电池/按键 group 先后被 mux
隔离，解释两条唤醒来源失效；该独立问题转入 [BUG-002](BUGS.md)，不自动增加显示步骤或验收前置。
preserveoff 实际保留像素，但未解决全部缺时间现象。
0170 不能核验时钟区域像素或 TE 频率；SMMU stream 配置为 TRANS/CB2，F4 前须补转换核对。
0121a1 的 include 路径错误由0172修复；0121a2两档实测见 [新报告](FRAMEBUFFER-F0-0121A2.md)。
用户确认两档异常相同；时钟区域当前source/扫描同黑，不能仅归因于擦除/漏拷。模式查询、
update/disable/background已由原始框架核对；原版内核的等待参数与状态设计见
[帧完成契约](FRAMEBUFFER-SWAP-CONTRACT.md)。F0仍未验收，不提前进入F1。

## 1. 现状（0120a18 buildtree 源码 + 0120a17 日志）

### 1.1 硬件事实

| 项 | 事实 | 证据 |
|---|---|---|
| 面板 | Tianma R66456A，1080×2340，60 Hz，RGB888，MIPI DSI **command mode**，4 lane，VC0 | `docs/panel.md` §1 |
| TE / 上下电 DCS | TE 走 GPIO8；on …`35 00`→`29`→`11`+150 ms；off `51 00 00`+90 ms→`28`→`10`+150 ms | `docs/panel.md` 84、139-156 行 |
| ABL 交接 | VIG0 SSPP：`SRC_SIZE`(+0x00)=0x0924_0438、`SRC0_ADDR`(+0x14)=0x9d200000、`YSTRIDE0`(+0x24)=0x10e0；INTF1 tear-check 开、**autorefresh=0x80000001**；CTL0 FLUSH/START=0；DSI0 cmd 模式 | 0120a17 日志 `SWEET DISP: pass 0..2` |
| 含义 | MDP 每个 TE 自动从 SSPP 源地址取整帧发给面板。**改 `SRC0_ADDR` 并 flush 即可换扫描缓冲**，这是硬件翻页和零拷贝的基础 | 同上；`sde_hw_sspp.c:25-37` |
| ABL 缓冲 | 当前 0120a17 运行探针看到 VIG0 `SRC0_ADDR=0x9d200000`，计划暂按其后 0x1200000（18 MiB）处理；Mu-Silicium 标 `Display Reserved`、WRITE_THROUGH | `MemoryMapLib.c:25`；0120a17 `SWEET DISP` 日志 |
| SSPP 寄存器 | `SRC_SIZE`0x00、`SRC_XY`0x08（源裁剪起点）、`OUT_SIZE`0x0c、`SRC0_ADDR`0x14、`YSTRIDE0`0x24、`SRC_FORMAT`0x30、`UNPACK`0x34、`OP_MODE`0x38、`ADDR_SW_STATUS`0x70（安全属性，非生效地址） | `sde_hw_sspp.c:25-82,431-463` |
| CTL | `FLUSH`0x018、`START`0x01c，VIG0 的 flush 位 BIT(0) | `sde_hw_ctl.c:29-30,167` |
| MDSS 中断 | `interrupts=<0 83 0>` → INTID 115【待核实】；INTF1 tear 中断块 MDP+0x6E800（EN/STATUS/CLEAR=+0/+4/+8），bit2 RD_PTR、bit0 AUTOREFRESH_DONE；顶层 HW_INTR_STATUS=MDP+0x10 | `sm8150-sde.dtsi:44`；`sde_hw_interrupts.c:25,37-41,194-198` |
| SMMU | MDSS SID=0x800、匹配 mask=0x420。0121a2 SMR3=0x84200800、S2CR=0x2（TRANS/CB2），但 CB2 SCTLR=0xe0/M0、TTBR/TTBCR/MAIR0，本次 stage1 转换未使能；新物理页可达性仍需核实 | `sm8150-sde.dtsi:47`；0121a2 报告 §6 |

**地址范围待核对。** `docs/panel.md` 还记录 Android DTS 的 `splash_region` 为
`0x9c000000/0x02400000`（36 MiB），而运行时 VIG0 使用的是 `0x9d200000`。
这可能是同一预留区内的不同交接子区，也可能是 ABL/ADT 版本差异；在 F0 之前不能
把两者合并、扩大或拿来分配扫描缓冲。F0 必须同时记录 `SRC0_ADDR`、stride、内存
内容校验和以及实际映射边界，确认后再决定 F4 的分配地址。

### 1.2 软件现状与问题

```
CA(backboardd) 画进 IOSurface 2/3（普通可缓存内存，双缓冲）
   └─SwapEnd──► AppleCLCD 只记“最新一份”记录
 60Hz 定时器(假vsync) ──► 每 60/sweetfb_hz 次 CPU memcpy 约 11 MB → 0x9d200000（ABL 缓冲）
 off：7 次 vsync 把 0x9d200000 擦黑；on：重放最新 swap
 AppleARMBacklight ──► sweet_display_probe.c 的 DCS 0x51（BSD 层 ml_io_map 直映射）
```

| # | 问题 | 原因 |
|---|---|---|
| P1 | 插线唤醒/取消关机后缺时间、关机滑条区域半透明效果异常 | off 擦除与无有效 surface 的记录重放是已知缺口；0121 preserveoff 未消除全部现象，具体像素/合成根因待查 |
| P2 | 假 vsync | 60 Hz 定时器；两份日志里 `sel 72` 从未出现 |
| P3 | 撕裂/竞争 | 往正在被 autorefresh 扫描的内存里 memcpy；present 时才查 surface，CA 可能已在画下一帧；`SwapWait` 空操作 |
| P4 | CPU 拷贝开销 | 每次呈现约 11 MB memcpy 进写合并内存，所以限到 30 Hz |
| P5 | 依赖 ABL | 扫描缓冲、格式、SSPP 配置全部继承自 ABL，驱动不拥有 |
| P6 | 显示电源假 | “关屏”=擦像素+最低亮度 |
| P7 | 不符合 IOKit 常规 | BSD 层直映射寄存器；switch 分发不校验参数；IOLock 跨上下文共享状态；无 IOPM |

0120a17 日志：CA 每次 swap 只用 **layer 0**，全屏 1125×2436，surface 2/3 交替，
`bpr 4608`（=1152×4，已 64 字节对齐）。所以单图层直扫是可行目标，内核多图层重组不需要。

## 2. 目标架构

```
ADT /arm-io/disp0 (display-subsystem, canvas 1125x2436)
 └─ AppleCLCD : IOMobileFramebuffer               帧语义 / 用户态 ABI / 电源策略
     ├─ IOWorkLoop + IOCommandGate                 唯一状态所有者
     ├─ IOInterruptEventSource                     接 MDSS vsync/翻页完成
     ├─ SweetScanoutPool                           驱动自有扫描缓冲（≥2 个）
     ├─ IOMobileFramebufferUserClient              IOExternalMethodDispatch 表
     └─ SweetDisplayBackend*  ──────────────┐      纯虚接口：flip/power/dcs/vsync
ADT /arm-io/mdss (reg: MDP/DSI0/DISPCC/GCC 页; interrupts: 115)
 └─ SweetMDSS : IOService  ◄────────────────┘      硬件：SSPP/CTL/INTF/DSI/中断
 AppleARMBacklight : IOService                     亮度经 SweetMDSS::dcsWrite
```

### 2.1 扫描缓冲：自己的 simplefb

- 驱动启动时分配**物理连续、64 字节对齐行距**的扫描缓冲：用
  `IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIOMemoryPhysicallyContiguous |
  kIOMapWriteCombineCache | kIODirectionInOut, size, mask)`（mask 限 32 位以内物理地址，
  仅限制物理位置，不能替代 SMMU 映射）。0121 已读到 stream 为 TRANS/CB2；在 F4 写扫描地址前
  必须核实 CB2 的有效转换配置和页表可访问范围，必要时先完成独立的 F4a 映射子步骤。
- 尺寸按**逻辑画布** 1125×2436 分配（行距 4608，与 CA 的 surface 一致，约 10.7 MiB/个），
  面板只显示中间 1080×2340：通过 SSPP `SRC_XY=(22,48)`、`SRC_SIZE/OUT_SIZE=1080×2340`
  做**硬件裁剪**，不再 CPU 裁剪拷贝。（22=(1125−1080)/2，48=(2436−2340)/2，与现
  `fOffX/fOffY` 相同。）
- 格式固定 BGRA8888（CA 的 `fmt 0x42475241`）；`SRC_FORMAT/UNPACK` 由驱动写入而不是继承。
- 过渡交接可一次性复制 ABL 当前画面；PD1另须提供完全不读 ABL 像素的自有首帧路径。
  接管后不再读写 ABL 缓冲；不回收仍由固件保留的内存，但不能要求它用于运行或故障显示。
- 文本控制台（`PE_Video`）和 panic 使用驱动拥有且生命周期明确的扫描/应急缓冲；
  结合 `kPEBaseAddressChange` 和翻页同步设计，不能只更新一次指针后释放其所指旧缓冲。

### 2.2 双缓冲与零拷贝的三个层级

| 层级 | 做法 | 每帧 CPU 拷贝 | 步骤 |
|---|---|---|---|
| L1 驱动双缓冲 | 两个自有缓冲 A/B：CA surface → 拷到后台缓冲 → vsync 翻页 | 1 次，但不再撕裂、无需限频 | F4 |
| L2 直接扫描 surface | CA 用于 layer 0 的 surface 本身由驱动分配为扫描兼容内存，swap 时直接把 `SRC0_ADDR` 指向该 surface | **0** | F5 |
| L3 回退 | 遇到不兼容 surface（格式/尺寸/对齐/非连续），退回 L1 拷贝并计数 | 1 | F5 |

L2 的关键：IOSurface 创建时识别“可能被 IOMFB 扫描”的 surface（尺寸=画布、格式 BGRA、
来自 backboardd/CA；或 CA 设置的属性如 `IOSurfaceIsDisplayable`/`kIOSurfaceAllocSize`
等——**具体以 F0 记录的 create 属性字典为准**），从 `SweetScanoutPool` 给它分配物理
连续内存。

### 2.3 缓存策略（减少拷贝之外的代价）

- 当前自建 surface 用 `kIOMapDefaultCache`（可缓存），扫描缓冲用写合并。L2 直扫时，
  CPU 写入的数据可能还在 D-cache，MDP 读到旧数据。两种可选：
  - **方案 A（首选）**：可扫描 surface 保持**可缓存**（CPU 软件渲染快，读改写友好），
    在 swap 提交时、翻页前对该 surface 做 `clean_dcache64(phys, size, TRUE)`（只清理不失效），
    10.7 MiB 的 clean 远小于一次 memcpy。
  - **方案 B**：可扫描 surface 用写合并映射给用户态，免 clean，但 CPU 读/混合会很慢。
  - F5 先实现 A，测量 clean 耗时与 CA 渲染耗时；必要时对比 B，结论写入 §6。
- 只在 surface 的 seed（Unlock 写入计数）变化时 clean，避免重复。

### 2.4 帧生命周期（swap 语义）

```
SwapEnd(rec) ── 校验/retain surface ──► pending
准备后台缓冲（F4拷贝，F5可直扫时clean） ──► typed提交 SRC0_ADDR(+XY) / CTL FLUSH
与提交对应的生效/传输完成确认 ──► 旧扫描缓冲退役 + 更新display/pending + 唤醒等待者
```

- 一个 surface 在被扫描期间不得交还给 CA 继续绘制：必须核对并实现本版用户态实际使用的
  完成/复用协议；**retain 只保对象存活，不阻止 CA 改写像素**（0121a2 已捕获复用证据）。
  翻页确认后才释放上一帧，按不同options更新查询条件；`SwapWait`的非阻塞请求立即查询，
  阻塞请求用有界`commandSleep`释放gate等待，不能堵住完成事件。
- 队列深度1只允许替换尚未进入reading/硬件提交的queued：记录取消/替换原因，并在确认
  无读者后归还源。不能把reading、displayCurrent和被丢弃的queued统一算“完成”。
  当前显示ID、pending集合、源读取权和配置ready分别维护；ID0的意义由options决定。
  options0/1/2/3/4及副作用边界以[原版内核核对与设计](FRAMEBUFFER-SWAP-CONTRACT.md)为准。
- 2026-11-10纠正：`ADDR_SW_STATUS`由LG的`setup_secure()`修改，不能拿来确认翻页；
  `SRC0_ADDR`读回仅证明配置寄存器值。LG分别处理CTL_START、RD_PTR和PP_TX_DONE的fence，
  F4接线前必须明确本机autorefresh下的提交对应关系。F4源surface在CPU读完后可归还，
  驱动自己的扫描缓冲仍须等硬件读者退出；两个节点不能合并为一个“完成”。

### 2.5 IOKit 原则

1. `AppleCLCD` 不碰寄存器；`SweetMDSS` 不懂 swap/surface；两者间是 `SweetDisplayBackend`
   （`setScanout(phys,stride,xy,size,fmt)`、`flush()`、`scanoutLatched()`、`setPanelPower()`、
   `dcsWrite()`、`setVsync(bool)`），host 测试用假后端。
2. 所有状态只在 `AppleCLCD` workloop 上改（`IOCommandGate::runAction`）。
3. MMIO 由 ADT `reg` 描述、`mapDeviceMemoryWithIndex` 映射；不在 BSD 层直映射。
4. 中断沿用项目验证过的模式：`sleh` 分派 `sweet_mdss_irq(intid)`，上半部只屏蔽源、记时、
   `interruptOccurred()`；清除/再使能在 workloop。
5. 纯逻辑（swap 队列、翻页状态机、电源状态机、扫描兼容性判定）放 `sweet_fb_state.h`，
   host 测试覆盖。
6. IOPM 两态（off/on），`RequestPowerChange` 和关机/重启走同一状态机。

### 2.6 boot-arg

| boot-arg | 步骤 | 含义 |
|---|---|---|
| `sweetfbtrace=1/2` | F0 | v2 显示诊断；2 额外读取已知布局 CB2 配置 |
| `sweetmdss=1` | F1 | 使用 SweetMDSS IOService 后端（行为不变） |
| `sweetfbservice=1` | F1b | AppleCLCD后端绑定、用户接口dispatch和状态gate；要求mdss1/contract0，默认0 |
| `sweetfbpower=1` | F2 | 0177 DCS28/29，不擦像素；要求service1/mdss1/contract0，默认0 |
| `sweetfbvsync=1` | F3 | MDSS RD_PTR 中断做 vsync |
| `sweetfbown=1` | F4首段（0181） | 申请扫描池、ABL画面复制准备、typed只读预检；尚不切扫描地址 |
| `sweetfbown=2` | F4受控单缓冲（0182，待实机） | 独立实验档；有界暂停/提交/新传输观察后CPU目标切A；30Hz、原地更新，提交池保留到重启 |
| `sweetfbdirect=1` | F5 | surface 直接扫描（L2，含 L3 回退） |
| `sweetfbkickoff=1` | F6 | 关 autorefresh，按帧 kickoff（可选） |

## 3. 分步实施与验收

**每步通用要求**：本地交叉编译通过且告警类别不新增；`tools/tests` 全过（新状态机配
host 测试）；patch 对上一结果树 strict apply；CI 加 `require_marker`；打包脚本加 profile；
只 `fastboot boot`；回归三键/触摸/USB ACM/电池充电不退化；一步一个补丁（从 0170 起）；
日志用 `sweet_log_level.h`，前缀 `SWEET FB`/`SWEET MDSS`。

### F0：取证，不改行为

**做什么**
- 0171 / `sweetfbtrace=2`：selector 0–127 各前 32 次参数，最近 4 次提交按时间排序，
  增加 update/disable 和 +0x10c/+0x110 原始字段；另记实际完成 CPU 拷贝的 surface、swap、裁剪和时间。
- 息屏/唤醒以及亮屏每秒（上限 600 次）读取 BGRA，覆盖上/中/下、时钟中心和底部横条 ROI；
  按相同目的像素对齐源图和 panel，排除 padding、验证映射边界。明确是实时稀疏样本，不能当 CA fence。
- IOSurface create 前 32 次记录最多各 64 个属性键及关键值；保留创建者/尺寸等日志，为 F5 提供依据。
- 只读：SSPP 格式/地址、INTF1 真正 frame(+0xac)、计数使能及实际间隔/行相位；
  不把循环行差当 TE 频率。SMMU 记录 SMR/S2CR，已知 ID/TRANS/CB2 时增加只读配置；
  CB2 PA 经原机 DT 与 LG 驱动核对为 0x15082000。地址转换可用范围仍待实机证据。
- 打包两档：`fb-trace`、`fb-trace-preserveoff`。

**验收**
1. 两档各做：电源键息屏→3 秒→插充电器；接充电器时电源键息屏→3 秒→拔；各 5 次。
   当前未实现自动息屏，不安排 auto-lock；增加关机界面及取消后的显示观察。
2. 区分当前 source 同黑、擦除后未重画和提交/完成/复用语义。source 同黑不直接等于 CA 自身错误；
   需结合本版 ABI 与正常充电过渡 UI 核验，关机滑条区域半透明效果异常独立闭环。
3. 得到选择子使用表、CA surface 创建属性表，写入 §6。
4. 得到 SSPP 格式寄存器值、计数使能/实际间隔和 SMMU route/CB2 有效配置，写入 §6。
   command mode 计数若不能表征 TE，明确保留未知，TE 实测在 F3 的 IRQ 时间戳中完成；
   不用无效行差填充频率结论。F4 前仍必须明确新扫描地址的映射可行性。
5. 原始日志 SHA 记入 §7。

### F1：IOKit 分层重构，行为不变（正常运行路径验收通过）

0121a4为当前代码/画面基线。BUG-003和半透明颜色异常可保留，只验收是否引入新显示/亮度/
输入/充电回归。停止为缺时间增加取证轮次；模式元数据修复暂不混入F1。

**实施分段**：F1a先迁移硬件资源/探针/DCS与背光consumer（[0175实现与验证](FRAMEBUFFER-SERVICE-F1A.md)，0121a6正常运行路径功能验收通过）；F1b迁移
AppleCLCD后端绑定、用户客户端分发表/参数校验和状态gate，0121a7正常运行路径通过。
两段正常使用范围已验收；生命周期真实终止保留未上机边界，详见[F1b实测](FRAMEBUFFER-SERVICE-F1B.md)。

**做什么**
- ADT 新增 `/arm-io/mdss`（`compatible "sweet,mdss"`，`reg` 列 MDP、DSI0、DISPCC、GCC disp 页，
  `sweet,mdss-intid` 115仅记录候选；标准中断属性在F3核对后接入）。
- 新 `SweetMDSS : IOService`：`mapDeviceMemoryWithIndex`；迁入探针与 DCS 写（寄存器序列逐字保留）；
  实现 `SweetDisplayBackend`。
- `AppleCLCD` 经匹配通知取得后端（无全局指针）；用户客户端改 `IOExternalMethodDispatch`，
  经 `IOCommandGate` 进 workloop；未知选择子保持“记录一次+成功”。
- `AppleARMBacklight` 改调 `SweetMDSS::dcsWrite`；修正 1080×2400 等过时注释。
- `sweetmdss=0` 保留旧实现一个版本。

**验收**
1. `sweetmdss=1` 探针输出与 0120a17 `SWEET DISP` 逐项一致。
2. 亮度 0→最大→最小，DCS rc 全 0。
3. UI/锁屏/息屏表现与 `sweetmdss=0` 相同。
4. 新服务路径无 `ml_io_map`，旧BSD路径仅sweetmdss=0使用；`ioreg` 可见 `SweetMDSS`、`AppleCLCD`。
5. host 测试：分发表参数校验、gate 序列化。

### F2：真显示电源（P1/P6相关，0121a9基础与长时唤醒已验证）

**实现/验证/测试细节**见[0177 F2实施文档](FRAMEBUFFER-POWER-F2.md)。首轮只做28/29显示开关，
不等同完整面板下电或系统睡眠。

**做什么**
- 电源状态机 `ON→OFF_PENDING→OFF→ON_PENDING→ON`，失败标UNKNOWN；off失败补偿一次29，
  之后关闭新的off实验，保留on恢复。明确记录降级，不把超时当作硬件未执行。
- off：停呈现→DCS `28`；**不擦扫描内存**。on：`29`→继续显示当前缓冲。第一阶段只用
  `28/29`，通过后再评估 `10/11`（+150 ms）。
- 背光与电源解耦，亮度由 CoreBrightness 恢复。

**验收**
1. 电源键息屏/亮屏、息屏插电、息屏拔电各 5 次，以及关机界面/取消返回，无新增显示异常。
   BUG-003缺时间/半透明颜色与F1基线比较，保持独立跟踪，不要求在F2自动修复。
   auto-lock/通知唤醒仅在对应功能已实现时加入，不作当前测试前置。
2. 息屏时面板肉眼全黑（非最低亮度），DCS rc 全 0。
3. 唤醒亮度恢复，触摸立即可用；30 分钟手动息屏后唤醒正常。充电能力另按电池任务记录。
4. 日志 `SWEET FB POWER: off/on rc=0 elapsed-ms=`。
5. host 测试：重复请求、off 中再 off、DCS 失败回退。

### F3：硬件 vsync（修 P2，正常使用路径验收通过）

**0121a11验收：** 0179预检、0180 GIC/RD_PTR/完整硬件档均已实测；完整档十个完整窗
59.874–59.915Hz，OFF停止计数，ON恢复hardware active及三窗完整统计，无风暴/watchdog回退。
用户未发现异常，输入/亮度/采样事件继续。证据和限制见[F3第12节](FRAMEBUFFER-VSYNC-F3.md#12-0121a11完整硬件档f3正常使用路径验收通过)。
真实provider终止/重匹配与完整F2插拔次数仍属覆盖边界，不据此声称全部生命周期实测完毕。
下一步F4；不追加BUG-002专项测试，整机低功耗仍在大目标四。默认开关收口留给F7。

**做什么**
- 只读预检 INTID 115 的 GIC group/enable/config 与 MDSS 中断寄存器现值。
- `sleh` 链加入 `sweet_mdss_irq()`；只开 INTF1 RD_PTR；上半部屏蔽+记时，workloop 清除/计帧/
  发通知/再使能。
- 60 Hz 定时器改为看门狗（N ms 无 RD_PTR 报错并退回定时器 vsync，日志明确）。
- 显示 OFF 时关中断。

**验收**
1. 亮屏 60 秒 RD_PTR 60±1 Hz，记录间隔 p50/p99。
2. 息屏中断计数不增长。
3. 无风暴（>200/s 自动禁用并报错）；看门狗未触发。
4. 三键/触摸/USB/电池 IRQ 正常。
5. host 测试：上下半部记账、看门狗回退、off 禁用。

### F4：自有扫描缓冲 + 硬件裁剪（拷贝双缓冲按需，非PD2前置）

**当前首段0181：** 0121a11实测分配/四快照及基本回归通过；CB2原始FSR保留解释，不将“分配成功/寄存器读回”
当作DMA可达性或翻页成功。后续分为单缓冲接管、真实完成事件关联及双缓冲、帧等待与性能
收口，详见[F4实施记录](FRAMEBUFFER-SCANOUT-F4.md)。此阶段`own1`固定为只读预检，后续切换
扫描行为须另设明确档位，不能让同一参数在未核对新镜像时悄悄改变含义。
0182新增`own2`仅实现一次受控单缓冲接管，0121a12接管/唤醒日志通过，用户已报告滑动卡顿加重，其它视觉项未单独确认；安全回收、A/B翻页及
SwapWait尚未完成，不能提前计入以下整体验收。详见[第二段实现](FRAMEBUFFER-HANDOFF-F4.md)。

**原始F4完整方案（拷贝双缓冲与60Hz拷贝优化已延期，不作PD2前置）**
- `SweetScanoutPool` 分配 2 个物理连续缓冲（1125×2436×4，行距 4608，写合并），记录物理地址。
- 交接：一次性复制 ABL 当前画面到缓冲 A；配置 SSPP：`SRC_FORMAT/UNPACK` 显式写 BGRA、
  `YSTRIDE0=4608`、`SRC_XY=(22,48)`、`SRC_SIZE=OUT_SIZE=1080×2340`、`SRC0_ADDR=A`，CTL FLUSH VIG0；
  核对目标硬件的提交生效与传输完成事件后确认切换；`ADDR_SW_STATUS`仅保留安全属性语义。
- 呈现：swap 的 layer 0 surface → 拷到后台缓冲（不裁剪，整画布行拷贝）→ RD_PTR 时翻页
  （写 `SRC0_ADDR`+FLUSH）→ 通过与提交对应的完成事件确认后，才复用旧扫描缓冲并更新帧状态。
  CPU读完源surface与硬件停止读取扫描池是两个独立的所有权节点。
- 实现 `SwapWait`；废弃 `sweetfb_hz`（保留解析并告警）。
- 过渡阶段失败可显式回到旧 ABL 对照路径，但此结果不计为 F4/PD1 验收通过；
  最终独立驱动报告故障并按自身资源恢复，不能依赖 ABL 回退。未确认 DMA 退出不释放缓冲。

**验收**
1. `ioreg`/日志显示扫描地址不再是 0x9d200000，而是驱动缓冲的物理地址；ABL 区域此后无写入。
2. 画面位置与旧路径一致（状态栏、圆角、底部横条不偏移）——硬件裁剪正确。
3. 滚动/拖动 1 分钟无撕裂；60 Hz 呈现（不再限 30 Hz），记录每帧拷贝耗时 p50/p99。
4. 30 分钟压力无 panic，surface 创建/释放计数平衡。
5. F2 电源循环在新缓冲上重复通过。
6. host 测试：翻页状态机（pending 覆盖、确认后释放、超时回退）、SwapWait。

### PD0–PD5：完整面板驱动（新增必要子计划）

详见[分段内容、接口及结束条件](PANEL-FULL-DRIVER-PLAN.md)。PD0先核对实际面板和资源；
PD1复用F4并补无ABL首帧/控制台/故障显示；PD2接管控制器与DSI/PHY/PLL；PD3实现完整
面板供电/复位/命令生命周期；PD4实测显示链路断电及状态失效后的独立重建；PD5完成
IOPM和异常/终止收口。PD3有实现不能代替PD4断电恢复证明；F2的28/29也不等于完整驱动。
显示自身的电源资源和恢复不能推迟到整机电源管理大目标。先完成硬件驱动与必要生命周期，再进入F5；不等拷贝双缓冲整体验收。

### F5：surface 直接扫描（修 P4，L2/L3；硬件驱动与必要生命周期之后）

**做什么**
- IOSurface `create` 时按 F0 记录的属性识别“可扫描”surface（画布尺寸、BGRA、bpr 64 对齐、
  创建者 backboardd 等），从 `SweetScanoutPool` 分配物理连续内存（可缓存，方案 A）。
  池满或不匹配则普通分配。
- swap 时若 layer 0 surface 可扫描：`clean_dcache64` 该 surface（仅 seed 变化时）→ RD_PTR 时
  `SRC0_ADDR` 直接指向它 → 确认后释放上一帧。不兼容则走 F4 拷贝路径（L3）并计数。
- 池大小：至少 3 个画布缓冲（CA 双缓冲 + 前台），上限可配置；记录内存占用。
- 对比方案 B（写合并映射给用户态）并把结论写入 §6。

**验收**
1. 正常使用中 `direct-flips` 占比 ≥95%，`copy-fallback` 有计数且原因可查。
2. 每帧 CPU 拷贝为 0（直扫帧）；记录 clean 耗时 p50/p99（预期远小于 F4 拷贝）。
3. 无撕裂、无脏数据（快速动画下无旧帧残影/花块）——证明 cache clean 正确。
4. surface 在扫描期间不被 CA 改写（翻页确认前不释放）；压力 30 分钟无 panic、计数平衡。
5. CPU 占用较 F4 明显下降（内核计数或 `top` 对比）。
6. host 测试：可扫描判定、池耗尽回退、clean 仅在 seed 变化时执行、释放顺序。

### F6（可选）：按帧 kickoff，替代 autorefresh

F5 后仍有撕裂或功耗问题时做。PD2/PD4已经须显式配置可工作的刷新触发，不能继承未知
ABL状态；若独立初始化必须用到kickoff，则相应基础实现前移，不等待此可选优化。
参照 Linux `sde_encoder_phys_cmd`/`sde_hw_ctl`：关 INTF1
autorefresh；有新帧时翻页 + CTL FLUSH/START 提交一帧；以 PINGPONG/CTL done 中断确认；
无新帧时不发送。失败恢复 autorefresh 并记录。

**验收**：静止画面时 DSI 传输计数不增长；60 秒动画无撕裂/丢帧异常；任何失败自动恢复
autorefresh 且画面不冻结。

### F7：生命周期、IOPM、panic 与清理

**做什么**
- 基于PD5完整显示电源状态机统一IOPM、关机/重启和终止；停止提交、确认DMA退出后，
  按已验证的面板/链路流程关闭并交还资源，不翻回ABL或恢复它的快照。固件若有明确交接
  协议另行满足；`willTerminate/stop`按依赖顺序释放中断、映射、扫描池。
- panic使用PD1/PD5验证的自有扫描或独立应急缓冲；亮屏及断电状态的恢复能力分别验证，
  不能假定`29`可从已断电状态恢复，也不能在panic中等待普通workloop。
- 默认切换到新路径；删除旧 BSD 探针、定时器 vsync、擦像素、CPU 裁剪拷贝；更新
  `CURRENT-DRIVER-ARCHITECTURE.md`。

**验收**
1. 重启/关机/进 recovery 各 3 次，面板无残影花屏，下次开机正常。
2. 默认镜像通过 F2–F5 及 PD1–PD5 验收回归子集，实际不依赖 ABL 缓冲/遗留硬件配置。
3. 诊断档人为 panic，屏幕可见 panic 文本（亮屏与息屏两种情况）。
4. 无旧路径残留；文档与 `NOTES.md` 记录最终基线 SHA。

## 4. 风险与预案

| 风险 | 预案 |
|---|---|
| 新缓冲不可达，MDP 花屏/总线错误 | 同时核对 SMR/S2CR 与 CBAR/SCTLR，不能只按 TRANS route 判断。0121a2 的 CB2.M=0，不据此强加 F4a。写新地址前重查live配置、内存归属与可达性，后续结合真实提交/传输完成及画面核验，不能用安全寄存器`ADDR_SW_STATUS`确认地址。ABL暂按18MiB，不够双缓冲；只有确认需要转换时才另设计扫描池映射子步骤 |
| 物理连续 10.7 MiB 分配失败 | 启动早期分配；考虑驱动专用ADT预留区；过渡对照可显式保留旧路径但不算验收通过，最终不依赖ABL |
| 写 SSPP 格式寄存器出错导致花屏 | 先写回与 ABL 相同的值验证读写；再改裁剪；每次改动后读回比对 |
| cache clean 不足导致残影 | F5 验收 3 专门检查；回退方案 B |
| CA 不提供可识别属性，无法提前分配可扫描 surface | 按尺寸+格式+创建者匹配；仍不行则 L1（F4）作为最终方案，记录原因 |

## 5. 边界

- 不移植 UniXNU/HakiOS 整套显示栈；可参考选择子含义与寄存器序列并注明出处。
- 不做 GPU 合成（另立项目，见 `GPU-ADRENO640.md`）；`SweetDisplayBackend` 和可扫描 surface
  池为 GPU 输出预留接口。
- 不在内核重组多图层；layer 0 以外出现时先记录，不臆测处理（直扫只对单全屏图层）。
- 不把电池/充电 UI 问题当显示问题修。
- 所有设备操作 `fastboot boot`；不写盘。

## 6. F0 取证结果（0121a2 已补像素/硬件，ABI 待闭环）

0170 只增加只读取证，不改变显示行为。`sweetfbtrace=1` 记录选择子、最近 swap、
off/on 前后扫描区与 source surface 稀疏校验和；`sweetdispprobe=1` 的显示探针增加
SSPP 格式/裁剪/地址状态、INTF 计数差值和 apps SMMU SID 0x800 匹配信息。
打包脚本 `--framebuffer-f0` 会从同一内核产物生成 `fb-trace` 与
`fb-trace-preserveoff` 两档，后者只额外设置 `sweetfbpreserveoff=1`。
**0121 校正：** frame 差值误读 AUTOREFRESH_CONFIG，不能测 TE 频率；稀疏 hash 未覆盖时钟区域，
不能判定缺时间来自 CA 还是呈现路径。实现不足和实测结果详见 [F0 实测报告](FRAMEBUFFER-F0-0121.md)。

### 0121 产物核对与操作

- 0170 三个改动文件与 0120a18 基线严格应用补丁后的结果逐字节一致；连同现有源码门检查文件
  共核对 27 个文件，114 项 require_marker 通过。内核 manifest 和 F0 marker 检查通过。
- 内核 SHA256：`6e3be4432ea04c331ac9aef8e5783e0980ed2174dc463c44f76eda24bfbb48a0`。
- buildtree 包 SHA256：`4e3a9c10d373c3815e3558180eb64b8fe28083e2c209435195c3927da77c62b8`；
  kernel 包 SHA256：`b1d3de0b788a91c3ed39ad706c9cdde10127b48e9e77afc7dd193136c88a8fbb`。
- 镜像位于 `out-bs/boot-16k-0121-fb-trace.img` 与
  `out-bs/boot-16k-0121-fb-trace-preserveoff.img`；每个 35,667,968 字节。
  header/kernel blob/ramdisk/kernel/ADT/TOML 均已回读核对。SHA256 见 NOTES 顶部。
- 本轮未提供构建日志或 CI run ID，未检查全量编译警告。底稿在 `scratch/artifact-0121/`。

原两档操作（用户已完成手动息屏插拔，不重复要求）：

1. 进桌面后等至少 35 秒，让启动约 30 秒后的三次显示探针完成；确认三键、触摸正常。
2. 未接充电器时按电源键息屏，等 3 秒，插充电器；大充电 logo 消失后观察锁屏时间/中间区域。
   正常亮屏后拔线，重复 5 次。
3. 接着充电器按电源键息屏，等 3 秒后拔线，观察唤醒和画面，重复 5 次。
4. 记录普通档和 preserveoff 档表现是否不同；自动锁屏项已删除，当前设备没有实现自动息屏。
5. 结束后等至少 15 秒再导出持久日志，每档分别命名
   `sweetlog-0121-fb-trace.bin` / `sweetlog-0121-fb-trace-preserveoff.bin`。
   USB ACM 日志可同时保留，但无需依赖实时抓取来完成测试。

0121 已有结果：

- 选择子：可见 3/4/5/6/8/12/15/17/22/27/28/29/33/50/55/57；无 72 调用，完整参数/统计待补。
- CA surface：2/3，1125×2436 BGRA、bpr4608、alloc11239424、cache 属性 0x400；完整键表待补。
- 显示根因：preserveoff 有效保留像素，但不足以修复缺时间/滑条区域半透明效果异常，具体根因未定。
- SSPP 格式和 SMMU stream 已读；TE 频率、CB2 有效转换/映射范围待补，不能假定 bypass。
- mux 故障：普通档 USBIN group1 → power group0 被隔离；由 [BUG-002](BUGS.md) 独立跟踪，
  不将故障后的无唤醒当作显示路径故障证据。
- F5 缓存方案对比：未开始。

## 7. 进度记录

| 步骤 | 补丁 | 产物/日志 | 结论 |
|---|---|---|---|
| F0 | 0170 / d58e7fc | 0121 两档持久日志；SHA 见实测报告 §1 | 已分析；mux 双组隔离独立记 BUG-002，0170 取证不足，未验收 |
| F0 v2 | 0171+0172 / 2bc0251 | 0121a2 两档持久日志；SHA 见新报告 §1 | 两档异常相同；source/扫描时钟同黑，模式/帧语义待核对；CB2.M0，frame计数关闭，未验收 |
| F1a | 0175 / a22e1b3 | 0121a5两档对照、0121a6 mdss日志及用户亮度/唤醒反馈 | 资源/背光迁移正常运行路径功能验收通过；BUG-004独立；下一步F1b，F1整体未完成 |
| F1b | 0176 / b1c7969 | 本地/云产物/boot核对；0121a7 gated持久日志及用户未见新问题 | 正常运行路径验收通过；真实provider终止未上机；BUG-004复现独立跟踪，下一步F2 |
| F2 | 0177 / decebe3 | 0121a9 DCS日志及用户确认显示正常；3组实际off/on均rc0 | 基础开关屏及长时电源键唤醒通过；长测日志与视觉确认见F2报告；完整插拔次数未补齐，旧bug独立跟踪 |
| F3 | 0179+0180 / 7329197 | 0121a10预检、0121a11 GIC/RD_PTR/完整档；SHA见F3报告 | 正常使用路径验收通过；完整档59.874–59.915Hz、OFF停计数/ON恢复硬件消费、无回退；下一步F4 |
| F4首段 | 0181 / 42047d3 | 替换后0121a11的scanout-preflight日志；SHA见F4第9节 | 双池/seed/四快照及基本回归通过；FSR原始0x400保留，新DMA/接管/翻页未测 |
| F4受控单缓冲 | 0182，本地验证通过 | 0121a12 single接管及首次ON自有配置确认；6组OFF/ON成功 | 用户已报告滑动卡顿加重；own2提交后保留池至重启，双缓冲/回收未完成 |

### 0171 / F0 补充实现（历史记录，0121a2 已完成实测）

像素/实际拷贝/属性/计数补证据，详见 [F0 v2](FRAMEBUFFER-F0-V2.md)。本地编译和 host 检查通过，
实机结果见下节。CircleCI 配置未改；打包门要求新 v2 marker。独立 bug 不在本轮修复。

### 0121a2 / 0171+0172 实测已分析

38文件源码比对、114项门检查、新kernel manifest/marker以及两档boot回读通过。
镜像路径、SHA及原测试顺序见 [F0 v2](FRAMEBUFFER-F0-V2.md)；日志分析见 [0121a2 报告](FRAMEBUFFER-F0-0121A2.md)。

本轮新增事实：

- 用户确认两档肉眼异常相同；时钟ROI多次当前source/扫描RGB同为0、差异0，copy持续推进。
  普通息屏擦黑、保留档留像素均捕获，但不能解释全部异常，不把source同黑直接定为CA错误。
- surface2/3的11键属性表与21种selector集合已得到。selector79/block0x40当前全清0，
  参考实现为当前模式ID，需核对本版ABI；SwapWait/完成通知/复用协议仍有缺口。
- 只见layer0非零surface；update/disable及raw10c=0xff000000形态已捕获。空记录可能是合法
  blank，不能通过过滤空记录或硬恢复旧图绕过语义。关机滑条区域半透明效果异常仍未独立定因。
- CB2 SCTLR.M0，TTBR/TTBCR/MAIR0，本次stage1转换未使能；新地址可达性仍在F4前验证。
  frame-en0，TE实测留给F3 IRQ时间戳，不追加同类一秒差值实验。
- BUG-002本轮未复现，未宣布修复。采样中位约79ms，有诊断开销，不以此评价默认性能。

NEXT：先离线核对模式查询/属性和提交/完成/复用ABI，再定针对性修复。不立即重编译或重复
两档测试，不直接改面板/SMMU，不进入F1；F0仍未验收。

### F0 后续：本版 ABI 与症状校正

见 [离线核对](FRAMEBUFFER-USERLAND-ABI.md)。用户澄清关机滑条是半透明颜色未正常渲染，
不是控件几何重叠；此处不规划几何布局修复。已定位本地iOS16.7的原始框架：+0x10c背景、
bit31、全disable合法blank和SwapWait成功后CA推进页回收均已确认。实现优先顺序调整为：
完成/复用契约（先厘清flags/id0/非阻塞）→持久layer/background和blank→一致的模式属性/查询。
模式输出+4/+8已确认，但不能任意填ID；半透明/blur症状还需局部像素证据，不直接加内核混色。
本轮离线审计无新kernel补丁，不要求重复刷机；F0未验收，F1仍未开始。

### F0 后续：原版内核等待语义与状态设计

从本地同版IPSW解压原生kernelcache，核对legacy gated wait；28项指令/dispatch记录通过。
详见[帧完成契约](FRAMEBUFFER-SWAP-CONTRACT.md)：options0区分当前显示与pending，1检查pending，
2检查整队列；3/4检查配置状态及其反向条件，高位noBlock未就绪返回Busy（3/4各有立即返回规则）。
不能将id0配置查询写成队列排空，也不能让所有options仅比较lastCompleted。
本次修正§2.4笼统的pending覆盖完成策略，写明gate等待、源释放后自有像素重放、取消/停止边界。
尚未实现行为补丁；不改变F0只读取证、不跳过F1/F3/F4。视觉异常的单一根因仍未证实。

### 0173 / CPU源像素所有权行为实验（尚未上机）

F0未验收，另列[自有快照实验](FRAMEBUFFER-OWNED-CONTRACT.md)，不宣称F1/F3/F4完成。
独立开关sweetfbcontract=1，默认旧路径。一次编译两档，仅开关不同，均保留息屏像素并关闭重采样。
实现gate状态/有界wait、提交线程复制到自有内存、源归还后仅读自有像素、持久layer0/背景/blank、
取消/close/stop。没有切换MDP扫描地址，没有真实vsync fence，也不认定半透明异常已定因。
三文件strict apply/arm64编译、14项host回归通过，LSan受ptrace限制未验收。
NEXT：用户push并下载产物→核对打包→对照/实验档手动息屏插拔与关机界面半透明/取消测试。

### 0121a3 / 0173 两档实测已分析（2026-11-09）

见[实验结果](FRAMEBUFFER-CONTRACT-0121A3.md)。新路径启用，options0当前显示Busy、替换后
成功，options1可在仍显示时成功；blank与7轮off/on均有记录，未记录helper错误。
用户确认两档视觉一致、未见新异常，缺时间/滑条区域半透明效果未消除，不验收F0。
保留独立开关、不默认切换，不重复相同A/B；下一步离线核对模式/颜色时序属性及无匹配mode
分支，再设计独立修复。对照末段Type-C电池group隔离补入BUG-002，不扩当前显示范围。

### 0174 / 同一 swap 像素补证（2026-11-09）

用户追问旧证据能否证明原始提交缺时间，因此将[同ID取证](FRAMEBUFFER-SAME-SWAP-PROOF.md)
放在模式元数据核对前。独立sweetfbproof=1，依赖0173；关联源复制到自有layer、组帧、写屏
后的扫描内存回读，完整检查时钟ROI，区分新layer/旧缓存/blank。按亮屏窗口限额，无新timer。
严格应用/arm64编译/14项host回归通过；新产物尚未云编译、未上机，不宣称修复视觉问题。
NEXT：用户push→下载新kernel/buildtree→核对打一个proof档→手动息屏插拔与取消关机后留持久日志。
旧对照无需重复。F0仍未验收，不将CPU回读解释为硬件完成，不提前验收F1/F3/F4。

0121a4产物已核对：41源码逐字节匹配、114源码marker及新内核proof marker通过，单档
`boot-16k-0121a4-fb-contract-proof.img`已生成并回读；SHA与参数见[取证文档](FRAMEBUFFER-SAME-SWAP-PROOF.md)。
等待用户手动测试，启动正常锁屏先停5–10秒作参照，再做息屏插拔及关机界面/取消；尚未验收。

### 0121a4 / 同一swap实测结果（2026-11-09）

详见[实测报告](FRAMEBUFFER-PROOF-0121A4.md)。第一轮Type-C RID/APID271 bit0 pending触发
电池group1隔离，解释插线/UI无反应，按键仍能唤醒；补BUG-002独立跟踪。
第二轮无隔离/ERROR/WARN，61份样本全部panel逐像素diff0；关键插电窗口连续约7.33秒的
同ID/新layer/完整ROI/rc0样本，源快照、组帧、扫描内存均全黑，拔电后ROI有非黑内容。
用户更新：插电（包括原本亮屏时插电）大logo后缺时间、息屏拔电正常、取消关机后锁屏正常。
不能据此识别时间字形或给CA定因；关机组合键晚于最后像素样本，半透明没有本轮独立证据。
NEXT：不重复取证/旧A/B；继续离线核对模式/颜色/时序属性与本版fallback，确定分支后再修复。
F0未验收；不强行重放旧时间、不据CPU回读提前验收F1/F3/F4。0174采样约45ms中位，默认关闭。
