# ADC5 与 watchdog 时序取证（0166）

**0120a16身份档实测：未通过，阻塞点为USBIN RESULT校验失败-12。**
只有3请求/2成功/5EOC，约53.744秒终止；失败这次GIC→notify61.979µs，ACK等锁
0.312µs/持锁12.188µs，callback距deadline仍98.522ms，属于及时交付后的校验错误。
快照status1/channel8均正常，但未保存原校验值和data lo/hi，不能断言具体是哪项。
实测ADC仅覆盖约1.255秒，不能用整档约700秒日志宣布旧超时/长期ADC已修复。
LG拒绝恰好0x8000，其他越界电压码按零处理；本实现拒绝所有raw>=0x8000，存在
具体语义差异，尚无被拒原始码证实因果。下一实现保留已读校验值/失败阶段并核对
零电压语义；本批不再重刷identity/watchdog/组合，无新驱动需push，继续禁充。
[完整时间线、已证实与未证实项](BATTERY-DEPENDENCY-TESTS.md#0120a16身份档实测及时交付后发生校验失败)。
以下旧待测/待编译动作均为历史，以本段为准。


**当前0120a16：真实云源码/内核核对通过，同核五档boot已打包并回读，待测identity1。**
实现357b26f/0167，相关33源码与本地验证树逐字节一致，原106源码gate、新仲裁/
ACK计时及snapshot v2标记通过；五档均runtime1、不主动启充，三键/USB/触摸IRQ
及USB日志保留。先用`out-bs/boot-16k-0120a16-battery-identity.img`：桌面后尽快
拔5秒→同一稳定电源5～10分钟→拔5秒，简测三键/触摸，导出
`sweetlog-0120a16-battery-identity.bin`，持久日志即可。目标核对持续USBIN转换与
ACK等锁/持锁/通知时序，是否消除旧超时尚待实机；之后按结果决定同核watchdog4/
组合7。无需新push/编译，不重测旧0120a15或baseline。
[镜像归档与验证边界](BATTERY-DEPENDENCY-TESTS.md#0120a16云产物与同内核测试镜像)。
以下旧待push/待编译/待测动作为历史记录，以本段为准。


**当前0167：ACK/遥测竞争修复已完成本地验证，待用户push/新云内核实测。**
等待中的mux IRQ写/ACK优先于下一笔普通SPMI事务；每遍先ACK、再合并通知，全部
有限遍次结束后输出日志；独立故障快照逐字段让锁，协议错误的恢复前快照仍保留
原子区间。新增ACK等锁/持锁计时，原期限/owner/poison/group隔离及runtime1禁充
不变。7文件ARM64单编译、并发/故障回归通过，CI未改，尚无实机修复结论。
下一步用户push，下载新产物后核对/打包同核五档，优先identity1；不再测旧0120a15。
[实现、锁顺序、计时边界和测试步骤](SPMI-ACK-ARBITRATION.md)。以下旧待测动作均按历史保留。


**0120a15-watchdog已分析：** 独立看门狗配置/9次自然bark维护/180秒有限停止通过
（含初始化10pet，结束后20bark不再pet），仍未主动启充。ADC59又RESULT超时，
这次mux在deadline前4.003ms已读到EOC pending，却到deadline后9.028ms才notify，
确认存在预截止事件交付过晚；ACK锁等待/抢占/总线耗时仍待拆分。下一实现处理
关键ACK/分发与重遥测竞争，当前不追加组合7或重复测试，无新驱动需push。
[完整实测与验收边界](BATTERY-DEPENDENCY-TESTS.md#0120a15看门狗档实测正常维护通过adc确认deadline前已有eoc)。
下方待watchdog等步骤按历史保留，以本段为当前动作。

**0120a15身份档已分析：** 5次身份转换正常；第6请求（首笔USBIN）RESULT超时。
恢复前读到转换完成/EOC锁存，第二mux scan晚于deadline78.134ms、notify晚109.106ms；
首EOC回调仅23.178µs、advance锁等待0.312µs，timer是真到期。已定位到存在分发
延迟，尚未证明硬件在deadline前完成，精确阻塞来源仍待核对。身份/限值撤销正确，
无有效VBUS或正许可。下一项同核watchdog4：桌面尽快拔5秒→接稳定电源≥60秒→拔5秒，
导出`sweetlog-0120a15-battery-watchdog.bin`。无新push/编译、不主动启充；不重测身份/
baseline/inhibit，暂不要求组合。[实测时间线与边界](BATTERY-DEPENDENCY-TESTS.md#0120a15身份档实测捕获result完成后的分发延迟)。
下方待identity等步骤按历史保留，以本段为当前动作。

**0120a15云产物已核对并打包，待实机：** 0166真实源码33文件与验证树一致，
原106源码检查、kernel manifest/新诊断marker和五档最终boot回读通过。先测
`boot-16k-0120a15-battery-identity.img`：进桌面尽快拔5秒→同一稳定电源约5分钟→
拔5秒，导出`sweetlog-0120a15-battery-identity.bin`，持久日志即可。之后按新证据
安排同核watchdog/组合档；当前不重测baseline/旧四档/独立inhibit，不主动启充。
[镜像、哈希与步骤](BATTERY-DEPENDENCY-TESTS.md#0120a15云产物与同内核测试镜像)。
下方待push/待云构建段落按历史保留，当前动作以本段为准。

2026-11-07。本轮承接0120a13四档实测，新增patch
`0166-mh2lm-battery-irq-timing.patch`。本地实现及验证完成，待用户push/云编译和
新内核实测。尚无新boot或硬件结果，不能宣布ADC故障或首bark问题已修复。

## 为什么需要下一轮

0120a13暴露三种ADC错误：identity238未见首EOC，inhibit68收到首EOC但advance
已过期，watchdog51/组合19第二阶段timer超时而稍后记录mux hit。原hit行在notify
之后才打印，文本顺序不能代替时间；advance又先取得共享SPMI锁再检查deadline。

组合档在软件plugin后23秒失败，首bark采样比plugin晚27.853秒。单独档能自然
维护并正常有限停止，但这不能证明组合首轮起算关系正确，更不能归因于mux冲突。

LG参考 `qpnp-smb5.c:3150–3168` 按DT配置bark/bite/snarl并启用
`WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT`，
`smb5-lib.c:wdog_bark_irq_handler` 只在bark中断里pet；
`smb5-reg.h:WD_CFG_REG/SNARL_BARK_BITE_WD_CFG_REG` 定义位域。
这些源码没有证明“移植驱动采到plugin的时间＝硬件首次计时零点”。本轮保持
`WD62(maskc2=42)/timef3`、100ms ADC阶段期限、23秒首bark期待和180秒诊断窗口。

## 分层实现

路径仍为 GIC callback → SweetSpmiInterruptMux IOService → registry client →
consumer自己的workloop。matched/terminated不做硬件初始化，未增加单例桥接、
IRQ轮询后备、自动重试或额外pet。SweetBattery经
`sweet_pmic_power_capture_watchdog_fault()`读取固定语义的诊断，寄存器布局仍在
SPMI后端。ADC服务直接使用自己的typed固定通道协议，不把地址传给高层。

1. mux为每条路由保存最后一次已完成ACK、即将notify的值快照。共享GIC入口、
   route pending扫描、ACK完成、notify前各有uptime纳秒记录。原ADC/watchdog
   hit行附加时间戳，保留晚到hit；不再额外增加一行每EOC日志。
2. `SweetSpmiMuxClient::copyInterruptTrace(token,out)`只允许单一token，在client
   短锁保护的provider引用下调用有界值拷贝。provider无gate、MMIO或SPMI访问；
   单writer在mux loop上发布，所有字段使用atomic，奇偶sequence最多重读3次。
   读不到/参数错误时返回false且不改out；它不分发事件、不授予权限。
3. ADC记录每次trigger进入/总线命令完成、advance进入/取得SPMI锁、检查时的
   phase/deadline，consumer回调和timer入口。每次request清空旧session诊断并
   记录mux route-hits基线，拒绝的begin不能借用上次时间。
4. timer重新核对**当前阶段**的settle/deadline；未到时只按原deadline重挂定时，
   不读取或接受ADC数据、不延长转换期限。真正到期仍失败撤销。这是防御修正，
   尚无实机证据证明0120a13是旧timer误报；XNU timer本身也有generation检查。
5. ADC底层终止错误在原锁内、恢复前保存一次固定快照；timer超时则先撤销
   provider ready/缓存有效性，再快照、abort恢复、detach通知。底层仍按原合同
   清理lease后返回，测量输出错误时保持不变。恢复本身失败或begin尚未建session
   时，现场可能已经恢复/没有本次转换；日志明确标记时机，不把它当原始现场。
6. watchdog增加prepare/pet命令进入、取得control锁和返回时间；软件plugin观察
   记录input epoch、输入采集起止、observed时间及pet数，明确hardware-start
   unproven。故障先置terminal/撤回诊断属性并尝试主路disable，再读取固定快照，
   最后清pending；不因取证延迟禁充，不因晚到bark恢复pet或启动诊断。

## 日志契约与判读

时间均为内核uptime纳秒，非日志打印/导出墙钟时间。ERROR故障现场始终输出；
启动、命令、plugin及正常hit为INFO，可按现有sweetloglevel过滤。诊断档仍用INFO。
新增格式按所有数值最大宽度及最长相关route名核对，含日志前缀/换行最长222字节，
低于现有255字节限制。

| 记录 | 字段及边界 |
| --- | --- |
| `SWEET ADC5 TIMING: v1` | 新版本启动marker；本地打包器据此拒绝旧内核 |
| `SWEET MUX: hit route=adc5-eoc/battery-wdog-bark` | route-hits/irq/work及gic/scan/ack/notify；已有单行扩展 |
| `SWEET ADC5 FAULT/STAGE` | request id、错误、当前phase/EOC数、timer/callback、最后一次trigger与advance；phase0/时间0可能表示尚未建立会话 |
| `SWEET ADC5 MUX/PATH` | 本次起始baseline、最后trace；hits未超过baseline时不能说是本请求的EOC |
| `SWEET ADC5 SNAPSHOT/FIELD` | 总API error和逐字段error；before-restore/saved明确时机，error不为0时不解释raw |
| `SWEET BATT WD COMMAND` | prepare/pet尝试的op与enter/locked/done；error不为0时actual0是日志占位，无成功意义 |
| `SWEET BATT WD START` | 软件plugin期待起点，不能等同PMIC硬件计时零点 |
| `SWEET BATT WD FAULT/MUX/PATH/SNAPSHOT/FIELD` | terminal时的期待、pending/pets、最后分发和禁充尝试后的固定读回 |

mux的gic是**该次drain入口看到的最近一次父GIC入口**，不是外设完成时间，也不是
ADC专属线。一个drain可处理多个路由/两个EOC；维护drain也可能借用上次GIC时间。
notify是发布trace后、调用通知前的观察值，不是consumer实际开始执行的时间。
无trace/旧hits/通知合并时必须保留这些边界，不能拿差值强行归因硬件。

- trigger→scan长，仅能定位到转换/IRQ可见性/调度范围；没有硬件完成时间不能拆分。
- scan→ack长，调查ACK的SPMI事务与共享锁；ack→callback长，调查通知/consumer排队。
- advance enter→locked长，才是此次SPMI锁等待的直接证据。
- timer真到期且快照EOC/latched已置位，结合晚到hit时间，调查投递路径；不能在
  失败后接受该结果、重发转换或放宽期限。
- watchdog命令、plugin采集、父GIC/notify、consumer采样时间共同核对首轮；真实
  plugin抖动/硬件计时启动语义仍需新日志，不能仅凭晚到27.853秒推定答案。

## 固定只读现场字段

所有地址为SID2 SPMI字节地址，只有后端持有数组；新API不接受任意地址。不读
ADC转换数据0x3150/51，不写trigger/enable/IRQ清除位，不把快照作为测量或充电许可。
顶层rc=0只表示固定检查执行；每字段错误仍须分别核对。invalid/unready/非arb-v5
返回错误时保持out不变。观察器事务沿用原有有限等待、超时poison，无重试。

| ADC field | 地址 | 含义 |
| --- | --- | --- |
| 0/1 | 0x3108 / 0x3109 | ADC status1/status2 |
| 2/3/4 | 0x3144 / 0x3146 / 0x3147 | channel / enable / conversion request |
| 5/6/7/8 | 0x3110 / 0x3111 / 0x3115 / 0x3118 | IRQ RT / type / enable-set读回 / latched |
| 9 | 0x3744 | calibration delay |

| WD field | 地址 | 含义 |
| --- | --- | --- |
| 0/1/2/3 | 0x1610 / 0x1611 / 0x1615 / 0x1618 | MISC IRQ RT / type / enable-set读回 / latched |
| 4/5/6 | 0x1651 / 0x1653 / 0x1648 | watchdog配置 / timeout配置 / SMB输出命令 |

`before-restore=1,saved=1`：底层原错误恢复前保存；`1/0`：service当前active，
在abort前观察；`0/0`：inactive/恢复后现场。observer本身失败不跳过原恢复尝试；已有observer超时poison仍可能使后续读回/恢复校验失败。
这些读回不是跨所有寄存器的硬件原子快照，也不会撤销mux已经完成的ACK。

## 本地验证与下一次上机

基线取真实0120a13云源码33文件；0166严格apply并逐文件比对。原106个源码gate
通过，CircleCI/GitHub Actions配置均未改。7个相关C/C++文件ARM64单文件编译通过，
不代替完整云端链接。ASAN/UBSAN传输131场景、mux/consumer66场景及80附加竞争、
电池运行层全部回归、ADC client19场景及100竞争、真实日志等级检查通过。

新增覆盖早到timer/跨phase timer、真实期限在等待锁期间到达、预恢复快照、读错/
unsupported/unready输出保持、固定读地址/零写入、sequence有界失败和200次IRQ
并发拷贝；诊断读前禁充、故障不重启/不额外pet均有回归。本地底稿
`/home/shenli/work1/scratch/battery-irq-timing/`，不提交scratch/产物。

**下一步用户push后云编译一次。** 新产物下载后先核对实际源码/内核marker，再用
`tools/mk-mh2lm-event-matrix.sh KERNEL LABEL --battery-dependencies`生成同核五档。
本地脚本现已拒绝缺0166 marker的0120a13，不能换标签重打旧内核。原profiles与
runtime1/deps0/1/2/4/7不变，不动CI；USB日志通道继续可用，持久日志足够取证。

优先测新identity1档：桌面后尽快拔5秒→同一稳定电源接至少60秒→拔5秒；三键和
触摸做简短确认并导出持久日志。如未复现ADC错误，保持同电源更长运行（旧身份档
约296秒才失败）；若已失败及时导出即可，不为凑时长重复测试。随后按证据决定
watchdog4/组合7，180秒从驱动准备开始，桌面不要空等。无需重刷0120a13四档、
追加baseline或重复单独inhibit，具体新boot路径待真实产物到达后给出。

验收要区分：本轮取证代码通过、实机错误根因定位、对应修复实机通过，是三件事。
所有profile仍不主动启充；长期有效输入许可、有效期间插拔撤销、外部SMB1355
I²C隔离及最终managed-watchdog/保护验收继续待完成。
