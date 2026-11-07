# Type-C CC/Rp 输入判断（0160～0162）

0163已把本页CC/APSD事件依据接入长期输入会话和同epoch VBUS短许可，并新增
独立USBIN UV/OV路由；0120a12实测取得三次短许可，后被重复source IRQ误删来源。
0164已本地修复，待push/新云产物复测，不启充。UV交付正常、OV无自然命中，
长期许可仍未通过。原3秒观测保留为
诊断；当前授权接口与测试见[输入会话](BATTERY-INPUT-SESSION.md)。以下0120a11
结果是已验证的分类基线，不代替0163长期会话/保护验收。

**当前：0120a11已实机验证0162的sink Rp分类修复和拔线撤销。** 稳定输入读到
Rp3A，CC reasons0/qualified1/kind2，APSD可同时为FLOAT。仍为runtime1观测，
没有真实启充或完整input provider，详见文末实测结果。
40e2e91新增的CI marker检查因未转义`<<`破坏CircleCI模板解析，已在后续修正中
撤回；CI文件恢复为其父提交原样，驱动修复及本地/打包器校验保留，详见文末。
先按角色解释Rp，misc bit4保留为诊断观测；USBIN OV/UV独立拒绝。0120a10的7笔
一致Rp3A快照离线回放可建立CC观测依据，尚无修复版实机结果，不启充。详见文末。

2026-11-07。补丁 `0160-mh2lm-typec-cc-input.patch` 接在0159之后。用户要求把
CC判断加入现有电池输入逻辑；本轮完成事件接入、独立分类、撤销及诊断，仍用
`--battery-evidence` 的 runtime1 观察镜像，尚无本轮设备结果。

## 为什么不能只等DCP/CDP

工作区 `docs/charging-logic.md` 第3.1节已区分BC1.2/APSD、Type-C CC和PD。
Android `smb5-lib.c:update_sw_icl_max` 的次序是：PD接管时退出；QC2/3交给独立
处理；有效Type-C中/高Rp使用Rp能力；其余走BC1.2分支。C口不等于已建立PD，
CC电流广播也不要求APSD先识别为DCP。之前XNU只把CC作为DCP/CDP的附加条件，
范围过窄。

参考均为工作区 `ref/android_kernel_lge_sm8150/`：

- `drivers/power/supply/qcom/smb5-reg.h:TYPE_C_SNK_STATUS_REG/TYPE_C_MISC_STATUS_REG`。
- `drivers/power/supply/qcom/smb5-lib.c:smblib_get_prop_ufp_mode/smblib_get_prop_typec_mode/update_sw_icl_max`。
- `arch/arm64/boot/dts/qcom/pm8150b.dtsi:qcom,typec@1500`。

## 调用链与IRQ

```text
PM8150B SID2/PID15 状态事件
  → 已有GIC INTID513 callback
  → SweetSpmiInterruptMux IOService/workloop 的路由表
  → SweetSpmiMuxClient 通知，仅排队
  → SweetBattery workloop
  → sweet_pmic_power_read_snapshot（typed内部采样接口）
  → sb_cc_status / sb_note_cc / sb_input_capability
  → 策略、IORegistry诊断属性及日志
```

不新增GIC注册、全局mux桥接或MMIO采样；重SPMI工作仍在电池workloop。
SPMI操作继续走现有串行化transport。按键、DWC3和触摸IRQ路径不改。

六条路由共用 `SWEET_BATT_IRQ_TYPEC=0x40` 通知位，具体命中由mux路由日志区分。
边沿来自本机Android DTS，不根据名称猜极性：

| PID15 bit | 路由 | 边沿 |
| --- | --- | --- |
| 0 | battery-typec-rid | 上升 |
| 2 | battery-typec-cc | 上升 |
| 3 | battery-typec-vconn | 双边沿 |
| 4 | battery-typec-vbus | 上升 |
| 5 | battery-typec-attach | 上升 |
| 6 | battery-typec-legacy | 双边沿 |

仅runtime请求时订阅这些路由。沿用mux的唯一EE0 IRQ-owned APID检查、已使能位
拒绝接管、按位回滚、按group隔离和detach同步；不能解析/配置时明确报失败，
不静默退回轮询。APID不硬编码；0160最初的host映射不能作为实机结论，后续
0120a10已实际解析APID271并验证配置及自然IRQ交付，见文末结果。

## 采样与判断

复用已允许的只读状态：`0x1310` USBIN；`0x1506` Rp；`0x1507` debug accessory；
`0x1509` attach；`0x150b` misc；`0x150d` legacy。不扩大寄存器写权限。
同一有界采集前后对比VBUS和全部CC状态，变化/读取错误撤销valid，不能拿旧raw
当新证据。APSD状态和使能也保留前后核对。无sleep、重采循环或补偿轮询；一致
字节仅是软件检查，不能证明硬件原子快照，不能排除ABA或合并期间漏掉物理插拔。

`sb_cc_status()` 返回角色、独立观测标志、Rp、广播电流、方向和拒绝原因。
角色由misc bit6区分（1为本机source，0为本机sink），仍需有效的附着/VBUS等
条件才可使用；source角色不解释残留sink Rp。不是配置角色或建立PD合同。

| Rp原始掩码值（1506 & 0x7f） | rp枚举 | advertised-ma |
| --- | --- | --- |
| 0x08 | 1：default | 0，默认档不能直接授予500mA |
| 0x04 | 2：1.5A | 1500 |
| 0x02 | 3：3A | 3000 |
| 其余/组合/short | 0：unknown | 0 |

正常sink须VBUS存在、attach与CC附着、debounce完成、非source角色，且没有water、
VCONN OC、debug/legacy和USBIN OV/UV标记。misc bit1为方向，不按故障拒绝。
0162起misc bit4只进入`observed`诊断，不再单独否决sink Rp。该改变依据LG SMB5
分类/限流路径，不是证明该状态永远无害，也没有清位、切换角色或重置PMIC。
现阶段legacy仍拒绝，未声称实现Android的完整legacy/FLOAT策略。

`reasons`位表（可组合）：

| 值 | 含义 |
| --- | --- |
| 0x001 | 读取不完整 |
| 0x002 | 前后读取不一致 |
| 0x004 | USBIN或Type-C VBUS缺失 |
| 0x008 | attach/CC附着未成立 |
| 0x010 | debounce未完成 |
| 0x020 | 本机为source角色 |
| 0x040 | water状态 |
| 0x080 | 保留；0160/0161曾用misc bit4拒绝，0162已修正 |
| 0x100 | VCONN OC |
| 0x200 | debug accessory |
| 0x400 | legacy/noncompliant |
| 0x800 | Rp未知/short/组合值 |
| 0x1000 | USBIN实际OV状态，1310 bit3 |
| 0x2000 | USBIN实际UV状态，1310 bit2 |

0120a8～0120a10的`1506=02/misc=39/debug=0/legacy=4`在0160/0161下显示
`advertised-ma=3000/reasons=0x80/qualified0/kind0`，是当时软件拒绝结果；
0162在其他有效性条件满足时应为`role=1/observed=1/reasons=0`，由plugin/Type-C
事件建立CC依据，不能据此宣称PD或真实启充。

## 来源、期限与仲裁

CC依据独立于APSD enable/DONE/result：

- 初始读只诊断。plugin或Type-C事件可以建立稳定普通sink的CC观测依据。
- source事件只能保留同会话、同状态的已有CC依据，不能建立改变后的CC依据。
  相同状态的重复plugin/Type-C事件也不延长原始CC采集时间。
- 沿用观测USB session，另存CC session和开始/结束时间，3秒后不再满足策略。
  monitor不读输入、不延长CC或APSD期限。这里不是已完成的持久输入能力provider。
- 拔线、角色/能力变化、读取错误、不一致、未知VBUS或session饱和撤销旧依据。
  采样中/采样后有新输入IRQ排队时撤销；每次非disable控制命令提交前也检查
  pending输入IRQ。之后由排队的workloop action取得新状态，不隐藏重试。

`sb_input_capability()` 当前输出：

| kind | 依据 | 当前max-ma的含义 |
| --- | --- | --- |
| 0 | 无可认可依据/错误/过期/QC | 0 |
| 1 | source限定的DCP/CDP完成结果 + 正常sink | 500，现有BC试验上限 |
| 2 | 事件限定且有效的CC中/高Rp | 1500/3000，广播值 |

CC路径不要求APSD成功，APSD寄存器读错也不阻断其他完整CC证据。default Rp仍需
BC/USB依据；FLOAT本身不授予电流，有有效CC中/高Rp时可采用CC。已识别QC2/3
暂拒绝，不伪装为普通5V输入。尚无PD协商/合同provider，不实现高电压授权。

策略采用上述typed结果，执行deadline选择实际采用的CC或APSD时间；ICL仍取
provider上限、观测能力和原500mA试验上限的最小值。**本轮不设置inputVerified，
不绕过VBUS测量、电池身份、并联隔离、watchdog和温控互锁，也不启充。**
ADC请求196第二EOC超时是独立待办，0160没有修改ADC实现。

## 日志与属性

每次输入事件增加INFO短行，保持含等级和换行在255字节内：

```text
SWEET BATT CC: v1 id=... events=... coherent=... reasons=... rp=... advertised-ma=... orientation=... qualified=...
SWEET BATT CC ROLE: v1 id=... role=... observed=...; LG-SMB5 sink classification
SWEET BATT CC TIME: v1 id=... session=... started-ns=... completed-ns=...
SWEET BATT INPUT CAP: v1 id=... kind=... max-ma=...; observed-only not-PD
```

`qualified`只指CC观测有效，不是电池充电许可。原始字节仍在同id的TYPEC行，
APSD来源日志保留。错误沿用ERROR无条件输出，INFO可按既有等级过滤。

IORegistry增加`SweetBatteryCCRejectReasons/CCRp/CCAdvertisedMA/CCEvidenceFresh`及
`SweetBatteryObservedInputKind/ObservedInputMaxMA`。前缀均为SweetBattery。
0162增加`SweetBatteryCCRole/CCObservedFlags`，role0未知/1本机sink/2本机source，
observed bit0表示misc的VBUS error状态位。诊断用INFO，不把原始状态当作已证明
故障而伪报ERROR；已确认的IRQ/transport错误仍沿用原等级处理。
属性发布时重新计算时效，过期/停止不保留有效kind；不借此设置UI充电标志。

## 本地验证与下一次设备验证

0160严格apply到0120a8实际云源码后，与verify的33文件逐字节一致；102个CI源码
marker通过，旧0120a8缺新CC marker会被拒绝。CI仅增加源码gate，编译命令不变；
打包器原`--battery-evidence`增加CC/路由kernel marker检查。

SweetBattery、SweetPmicPower、SweetSpmiInterruptMux的ARM64单文件交叉编译通过。
host ASAN/UBSAN覆盖CC独立于APSD、Rp/方向/错误解码、初始化与来源、过期/撤销、
读中CC/VBUS变化、pending命令拒绝、registry撤销及既有控制互锁。mux真实源码
测试覆盖六条路由、合并通知、detach/rollback/group隔离、按键并行和生命周期
竞态；ADC consumer、charger transport、日志分级和board startup回归通过。
这些检查不能替代macOS云链接、Type-C真实IRQ权限/交付或实际电源协商。

用户自行push，云产物下载后再核对并打包；本轮无新boot/设备结果。下一次同一
runtime1镜像做电脑→拔线→当前充电器→拔线→电脑即可，记录A-to-C或C-to-C及
充电器型号/输出标识。导出持久日志即可，保留按键/触摸/USB IRQ和USB日志。
重点看Type-C路由attach/hit、CC拒绝原因及CC与APSD各自结果，不重复baseline。
若IRQ ownership或配置失败，保留该错误分析，不盲改owner或恢复轮询。

底稿工作区`scratch/battery-cc/`：base/edited/verify、cloud-apply、严格应用/源码
gate报告、测试记录及交叉编译输出。原始设备日志不入git。

## 0120a9云产物与测试镜像

实现提交`0b1cf56`。0120a9实际buildtree中33文件与本地verify逐字节一致，
102个源码gate通过，kernel manifest与新CC/Type-C路由标记核对通过。
kernel SHA256：`7d99770458eab7dfb03a4441078eb5efd6d510f479642e756f3e384799ef5f01`。

镜像：`/home/shenli/work1/out-bs/boot-16k-0120a9-battery-evidence.img`。
大小35,667,968字节；SHA256：
`8dc5f64f39de24683732b67451265cfaf70787e64048254837832d17447e9c72`。
runtime1/IRQ4/ADCconv0，保留三键/触摸/USB IRQ和USB日志。boot header v1、
firmware及ramdisk实际载荷一致，从最终boot内FAT回读kernel/ADT/TOML均匹配，
没有重复参数。底稿`scratch/artifact-0120a9/`。

设备验证：电脑启动桌面约30秒→拔10秒→当前充电器约30秒→拔10秒→电脑约10秒，
确认三键/触摸并记录线材A-to-C/C-to-C、电源型号/输出标识；持久日志命名
`sweetlog-0120a9-battery-evidence.bin`即可。仍是输入判断观察，不能当作真实
启充或PD验收；ADC超时未修复。本轮只核对、打包及记文档，未push/操作设备，
尚无本轮设备结果或用户提供的CI run ID/构建日志。

## 0120a9实测：IRQ配置读取被旧接口拒绝（0161修复）

日志`sweetlog-0120a9-battery-evidence.bin`，SHA256：
`4e788faad4f31f902caada05a38085c7586986090176806994b052b640370214`。
SWEETLOG v1，4MiB ring，有效273261字节、flush60、3870行，未回卷且正文无NUL。
用户观察：充电线松脱/重新插稳后图标保持，最终拔线图标也不消失。

事实：

- 29组遥测（初始1/保护tick28，约29.219秒），输入只读过初始化1次。
  UI仅开机一次`external=1 capable=1 charging=0`发布，初始化配置`enable=0`，
  无CONTROL，无任何插拔电池IRQ刷新或Type-C hit；不能将首次视觉变化归于
  已收到新电池事件，也不能把图标当真实启充。
- mux正确解析Type-C唯一EE0 IRQ-owned APID为**271**，六路元数据解析通过。
  第一条`battery-typec-rid`配置失败，client attach返回`0xe00002ca`，
  电池组回滚，battery标记IRQ失败并停止刷新。它发生在实际插拔验收之前。
- 其他group没有全局停止：失败后ADC有2请求/2结果/3EOC；三键都有down/up，
  触摸计数到74。旧battery日志“shared PMIC mux stopped; keys may also be
  unavailable”混淆了consumer attach失败与provider整体失败，不能按字面定性。
- 两次ACM configured，后段重接电脑反复reset而未再次configured，保留已知
  gadget问题；ADC第二EOC超时这轮没有覆盖，不能宣称已修复。

根因链（源码可确定，不需要再让用户测极性）：

```text
configureRoute(battery-typec-rid)
  → readPeripheral(offset=0x11)
  → sweet_spmi_read(SID2, 0x1511)
  → 旧通用白名单不含peripheral 0x15，直接返回-1，未发起该SPMI命令
  → 电池consumer整体attach失败/回滚
  → failIrq停止采样，却只改健康标志，保留此前外接电源/充电相关布尔值
  → 用户空间继续看到旧状态
```

这里不是APSD、CC极性或充电器协议导致的route失败。Type-C**状态**原本通过
`sweet_spmi_charger_read()`精确表读取，初始化读取成功；漏掉的是新IRQ**配置**
读取走了另一个旧接口。本地0160 mux测试把后者替换为可读模拟，没执行真实
transport白名单，因此漏测此错误。

### 0161的具体修改

1. 新增`sweet_spmi_battery_irq_read(peripheral, offset, out)`，SID固定2；只准
   CHGR/USBIN/TYPEC/FG-SOC（0x10/13/15/40）的0x11/12/13/15配置字节。
   用现有observer/共享锁/timeout poison，失败不改输出。mux用此固定只读接口，
   不扩大旧通用白名单或charger写表，不修改owner，不增加MMIO/轮询路径。
2. IRQ读取失败增加ERROR，含route/SID/peripheral/offset/返回码；consumer故障
   描述不再推断整个mux或按键停止。
3. `failIrq()`停止运行后撤销输入/CC/APSD/测量有效性，通过原publish/updateStatus
   清除modern及legacy的external/capable/charging/full标志。保留上次百分比，
   ExternalValid/ChargeStateValid保持false，表示输入未知，不假造一次“已测到
   拔线”。ADC consumer测量也撤销；已有硬件控制仍先走原disable流程。
   这不是证明硬件已停充，不增加自动重试/轮询/静默fallback。

本地：0161严格apply到0120a9实际云源码，与verify33文件一致；106个源码gate
通过。SweetBattery/sweet_spmi/SweetSpmiInterruptMux ARM64交叉编译通过。
ASAN/UBSAN测试直接连接真实`readPeripheral`和完整transport，覆盖旧API拒绝
0x1511而新API可读、固定表拒绝、错误输出保留/超时poison；mux测试恢复旧API
对Type-C的拒绝，新增Type-C读取失败仅撤销battery而保留keys；实际failIrq及
publish测试确认现代/legacy/UI状态撤销、无新读取/重试；SPMI串行化、ADC
consumer、board和日志回归通过。CI只追加gate，编译命令不变。

打包器新增0161 marker，拒绝旧0120a9冒充修复版。仍用原`--battery-evidence`
runtime1流程；用户push后下载新产物再打包，本轮无新boot或修复版实机结果。
无需重跑0120a9；下一轮先看battery consumer attach成功，再看同一充电器
松线/插稳/拔线是否有plugin/Type-C事件和UI发布。真实充电/PD仍未完成。
分析底稿`scratch/artifact-0120a9/log-analysis.json`；修复/验证底稿
`scratch/battery-cc-irq-fix/`。

## 0120a10：0161云产物与测试boot

实现提交`1f953e2`。实际buildtree的33文件与0161 verify逐字节匹配，106个源码
gate通过；kernel manifest及固定IRQ读取/故障撤销/CC/既有IRQ与ACM标记匹配。
kernel SHA256：`a1db9878a0d2f1e10574975bd6c7c62645670c900b1f78d861658cad1de13fa4`。

镜像：`/home/shenli/work1/out-bs/boot-16k-0120a10-battery-evidence.img`，
35,667,968字节，SHA256：
`55e35f48f094ad85e9c33355dc2283166eadb52fae737c0db1daeb7caf99eb41`。
仍为runtime1/IRQ4/ADCconv0，保留三键/触摸/USB IRQ及USB日志，不启充。
最终boot header v1的firmware/ramdisk切片与输入一致，回读FAT中kernel/ADT/
TOML匹配，参数无重复。底稿`scratch/artifact-0120a10/`。

用户自行fastboot。电脑启动桌面约30秒→拔10秒→当前充电器约30秒→拔10秒→
电脑约10秒，观察图标插拔变化及三键/触摸，记录线材和电源型号；持久日志
`sweetlog-0120a10-battery-evidence.bin`即可。先看consumer attach成功，再验
Type-C/plugin/CC/APSD/UI事件。没有修复版设备结果，本轮不能称已完成实机修复。
未改驱动/CI、未push/操作设备，也没有用户提供的CI run ID或构建日志。

## 0120a10实测：IRQ修复通过，Rp已读到但输入授权未成立

日志`logs/sweetlog-0120a10-battery-evidence.bin`（工作区路径），SHA256：
`8c26b574212b60f1a3dd46011e84449f509c79d1a4b72a2ee22bf890c66783ab`。
SWEETLOG v1，4MiB ring，有效896826字节、flush70、11356行，无回卷或正文NUL。
分析脚本及机器结果：工作区`scratch/artifact-0120a10/{analyze-log.py,log-analysis.json}`。

### 0161修复与事件交付

- Type-C六条路由实际解析到APID271；battery consumer成功attach，group1/
  events0x7f/INTID513，随后`armed mode=4`，没有回到2秒输入轮询。
- 34次运行期battery刷新全部healthy1：Type-C单独18次，Type-C+plugin6次，
  plugin单独4次，source5次，MSOC1次。另有初始/arm-sync，合计36次irq来源遥测。
  mux命中rid19、cc12、vbus10、attach9、legacy5、usbin10、source5、msoc1；
  VCONN路由未见自然命中，不能将无过流事件当失败或宣称已覆盖该路。
- UI源状态发布13次，外接电源标志多次撤销/恢复，每次都有updateStatus完成。
  例如遥测id357 external1 → id568 external0 → id647 external1，已不是0120a9
  只保留开机一次发布的冻结状态。这里只证明内核发布链，不代替用户视觉反馈。
  日志route-to-publish为89.117～118.138ms，不是物理插拔到屏幕动画的完整延迟。
- 本轮没有consumer故障，0161的故障撤销分支没有实机触发；该分支仍只有本地
  测试覆盖，不能把初始化修复成功扩张为所有故障恢复均已验收。

关键窗口（相对首次遥测开始时间，采样时刻，不是物理插拔时间）：

| 遥测id | 相对秒 | 状态 |
| --- | --- | --- |
| 1 | 0 | 开机电脑段，APSD SDP；开机结果仅诊断，不作新会话完成证据 |
| 221/222 | 217.323/217.435 | 第一次稳定Rp3A，misc39，CC reasons80；UI外接状态恢复 |
| 225/226 | 218.677/218.788 | source IRQ给出FLOAT，APSD qualified1，CC仍拒绝 |
| 233 | 224.976 | USBIN拔线，external/capable撤销 |
| 357 | 344.498 | 再次稳定Rp3A，misc39，CC reasons80，external1 |
| 359/360 | 345.757/345.868 | source IRQ再次给出FLOAT，CC仍拒绝 |
| 568 | 560.727 | 拔线发布external0 |
| 647/649 | 638.333/639.005 | 重接电脑先发布external1，随后source完成为SDP |

这些是日志状态窗口。用户尚未提供线材/电源型号，不能仅凭日志确定A-to-C或
C-to-C，也不能精确对应每次手部松线动作。

### CC/APSD已工作的部分与当前拒绝点

34次输入采集均`options=0x18/0 enabled=1`；5次source完成得到4次FLOAT及1次
SDP，均qualified1，无DCP/CDP/QC结果。两次稳定电源接入后，首次FLOAT分别比
同会话plugin晚1.242/1.259秒；重接电脑SDP晚0.672秒。证明APSD使能和完成事件
路径在工作，不需要把本问题解释为“APSD一直没开”或“没等完成就只读一次”。

7次一致的中高Rp记录（id221/222/225/226/357/359/360）全部为：

```text
sink=0x02 attached=0x23 misc=0x39 debug=0 legacy=0x04
rp=3 advertised-ma=3000 orientation=1 coherent=1
reasons=0x80 qualified=0; INPUT CAP kind=0 max-ma=0
```

`misc39`含VBUS(bit5)、VBUS_ERROR(bit4)、debounce(bit3)、CC attached(bit0)，
本机非source(bit6=0)。当前`sb_cc_status()`把bit4直接作为拒绝原因，因此单凭
Rp3A不会建立许可。广播不是PD合同，亦不等于本机已经能吸收3A。

**需要核对的是拒绝规则本身及角色生命周期，不能先下硬件故障结论。** LG主源
`smb5-reg.h:TYPEC_VBUS_ERROR_STATUS_BIT`确实把150b bit4命名为VBUS error；
但`smb5-lib.c:smblib_get_prop_typec_mode/smblib_get_prop_ufp_mode`按角色和Rp
分类，没有以bit4直接否决sink Rp；`update_sw_icl_max`另行处理PD/QC/Rp/BC1.2。
这说明当前XNU规则比该分类路径严格，**不证明bit4可忽略，也不证明必须清位或
复位PMIC**。更旧`smb-lib.c`使用另一套地址/bit定义，只能作逻辑参考，不能将其
寄存器或处理方式搬到本机SMB5。下一步离线核对sink/source时bit4适用范围、
输入保护与Type-C/DPDM provider职责；若静态证据不足，新增明确的typed只读取证，
而不是直接放宽规则、改角色、重跑APSD或反复刷同一镜像。

电脑段misc28/debug1/legacy6、Rp未知，被拒为普通sink；需要完整角色/配件处理，
不能把电脑USB configured当作充电电流许可。34次输入均sink-ok0、CC qualified0、
capability kind0。UI的capable布尔值沿用上报含义，不是inputVerified或电流授权。

6次CC不一致采集id212/217/350/356/646/647，相关字节为-12（VERIFY）；发生在
切换段，撤销fresh/CC依据，后续IRQ有一致的新结果。本日志没有SPMI timeout或
总线错误证据，不把一致性拒绝称为总线损坏；也不新增轮询/自动重试。

### ADC、遥测与回归边界

约729.588秒，737笔连续遥测（36 irq、701 protection-timer）；电压/电流有效，
3835～3928mV、IOPM符号电流-560～+8mA。保护timer仍用于保护/测量维护，不是
恢复USBIN/Type-C状态轮询。36次profile均match1/integrity13/nominal4027mAh。

ADC273请求/273有效结果/522EOC，全部restored1；BATID24次，USBIN249次。
266结果接受，7次USBIN因源epoch变化正确丢弃，error均0。有效BATID
888581～889684uV，USBIN4430448～5106224uV；插拔附近低电压样本不等于稳定
适配器电压。旧0120a8第二EOC超时本轮未复现；本轮请求196也有两次EOC并成功，
但请求编号不是根因，不能宣称修复。ADC总耗时7.492～116.525ms，仅凭总耗时
不能推断每阶段deadline行为。

无`[ERROR]`、kernel panic或mux隔离记录。按键日志只有power/volume-up完整
down/up，不能宣称volume-down也已本轮测试；触摸计数至少327。USB两次ACM
configured，后段重接电脑继续出现reset/ep0 warning，无新configured，保留
已知gadget重复枚举问题，不能把它当电池IRQ已停。

34笔充电配置均enable0，没有CONTROL，UI charging始终0；本镜像是runtime1
观察，未执行启充，不构成真实充电验收。本轮仅分析/记录，未改驱动或CI，未push、
未操作设备。无需重跑baseline或当前镜像；下一实现先解决上述输入策略语义，
ADC故障诊断及identity/parallel/watchdog互锁仍为独立待办。

## 0162：按LG SMB5修正sink Rp误拒绝

补丁`0162-mh2lm-lg-typec-sink-classification.patch`接0161。用户要求阅读LG
参考实现并修复。核对基准仍为LG树HEAD
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`，未套用旧SMB2的寄存器定义。

### 依据和边界

| LG参考入口（drivers/power/supply/下） | 与本次修正的关系 |
| --- | --- |
| qcom/smb5-lib.c:smblib_get_prop_typec_mode | misc bit6先区分UFP/DFP，不能把source时残留的sink Rp当输入能力 |
| qcom/smb5-lib.c:smblib_get_prop_ufp_mode | debug accessory另行分类，普通Rp按1506掩码解码，没有misc bit4直接否决sink的规则 |
| qcom/smb5-lib.c:typec_attach_detach_irq_handler/typec_state_change_irq_handler | 附着和CC事件驱动状态更新；保留现有XNU IRQ与撤销流程 |
| qcom/smb5-lib.c:update_sw_icl_max/typec_rp_med_high | PD/QC另有所有者，中高Rp可独立于普通BC1.2；legacy还受额外条件约束 |
| qcom/smb5-reg.h:TYPE_C_MISC_STATUS_REG/USBIN_OV_RT_STS_BIT/USBIN_UV_RT_STS_BIT | 150b bit4与1310 bit3(OV)/bit2(UV)是不同状态，不可混用 |
| lge/extension-smb5.c:usbin_ov_check | LG检查独立USBIN OV；不能因Rp可解码就认定输入电压无异常 |

修复将角色、诊断观测和拒绝原因分开：

- `SweetBattCcRole`表示本次读取的方向，sink才解释Rp；source始终拒绝输入且
  advertisedMa为0。失效读取时role未知，不发布伪造的有效角色。
- misc bit4保留在`SB_CC_OBS_VBUS_ERROR`及原始TYPEC日志，移出sink拒绝原因；
  旧reasons bit7保留不用，避免重用旧日志含义。**没有宣称bit4只适用于source，
  或确定其物理成因**；修正的是原先没有参考实现依据的直接否决。
- 新增独立USBIN OV/UV拒绝原因0x1000/0x2000，复用已经采集、前后核对的1310，
  不增加寄存器读取或写权限。这是当前窄范围策略的保守拒绝，不等于完整移植LG
  的OV/UV迟滞、storm处理或输入恢复状态机。本次不新增OV/UV IRQ路由，只在已有
  输入事件采集中检查；不能宣称已实现独立OV/UV事件保护，完整provider仍待补齐。
- water/VCONN、debug/legacy、附着/debounce、未知Rp、采集一致性、事件来源、
  session及3秒期限继续约束观测。原始misc变化也参与一致性/同状态检查；不因
  bit4改为诊断就忽略采集中变化。source-only事件不能建立变化后的CC依据。

新增INFO `SWEET BATT CC ROLE: v1`及role/observed registry属性；原CC日志格式
保留。mux/keys/USB/touch、SPMI transport、PMIC写表、ADC实现和UI状态计算未改。
沿用GIC→mux IOService→registry client→battery workloop→typed采样/策略，
没有轮询fallback、补偿重试或上层寄存器写。没有主动enable/rerun APSD、DPDM
切换、Type-C角色管理或PD协商；FLOAT单独仍不授权，只有独立有效中高Rp可给出
CC观测能力。`inputVerified`等真实启充互锁仍未建立。

### 本地验证与后续设备验收

严格应用到实际0120a10源码，33文件与verify逐字节一致，108个CI源码gate通过。
SweetBattery ARM64单文件交叉编译通过；不是完整macOS构建/链接验收。host
ASAN/UBSAN runtime6、mux/IRQ66、charger transport122、ADC consumer1、日志1条
PASS记录。新增覆盖真实Rp3A/misc39组合的采样→策略→属性发布、独立OV/UV拒绝、
source残留Rp、原始状态变化/时效撤销和真实授权缺失时无控制写。测试中的完整
授权仅为host合成数据，不进入设备驱动。控制提交pending检查及故障撤销仍通过。

离线回放0120a10全部34次实际输入快照：旧规则能力全0；新规则7次稳定sink
（221/222/225/226/357/359/360）为CC/3000mA观测，其余27次仍拒绝。该回放
执行实际新旧策略代码，但不能重建未记录的pending IRQ时刻，也不是新设备结果。
底稿`scratch/battery-cc-lg-sink/`，含base/edited/verify、cloud-apply、测试输出、
`integration.json`与`log-replay.json`；原始日志不入git。

CircleCI仅加2个源码gate，编译命令不变；打包器要求新CC ROLE marker，已验证旧
0120a10 kernel被拒绝。用户push后下载新kernel/buildtree，核对再用同一
`--battery-evidence`打包：runtime1/IRQ4/ADCconv0，保留USB日志与三种IRQ驱动。
电脑启动→拔线→当前电源稳定接入约30秒→拔线约10秒→电脑，持久日志即可；
无需重复baseline。记录电源/线材，观察插拔图标并顺手确认按键/触摸。

验收重点是稳定电源接入后的CC `role1/observed1/reasons0/qualified1`、
INPUT CAP `kind2/max-ma3000`及拔线撤销，APSD仍可为FLOAT，不强求DCP。
qualified是短期观测，超过3秒没有新的有效证据会过期，不应靠定时读刷新许可；
长期input provider仍待后续。真实charging保持0，本次不能作为充电验收。
本地完成，未push，无新boot/设备结果或新CI run ID。

### 0162后续：撤回新增CircleCI检查

用户反馈配置损坏后，核对40e2e91的CI差异，只有新增两条require_marker。
其中`SB_CC_USB_OV=1u<<12, SB_CC_USB_UV=1u<<13`含未转义`<<`，CircleCI会在
执行shell前解析参数模板；即使字符串在shell单引号中，也不能避开该层解析。
之前108个源码marker通过，只证明本地字符串存在，不是CircleCI配置有效性的
验证，不能据此承诺云端可运行。

本次删除这两条新增CI检查，逐字节恢复a5ee894的circleci.yml（106个原有gate），
没有撤销0162驱动修复或打包器CC ROLE检查。恢复后检查所有模板仅为原有合法
pipeline.parameters引用，并提取各run命令验证bash语法；未在线重跑CircleCI，
无新产物。后续相同类型的marker检查在本地/打包阶段完成，不再修改既有CI。

## 0120a11：0162云产物核对与测试boot

实际buildtree的33个文件与`scratch/battery-cc-lg-sink/verify/xnu`逐字节一致，
106个原有源码gate通过。kernel manifest有效，新CC ROLE/LG-SMB5标记、既有
APSD/CC/IRQ/USB ACM标记均存在。本轮未修改任何CI配置或驱动源码。

- buildtree gz SHA256：`f13aa3b0b6da1e687f8e4e8ff79b9855dde62121e07a3ee3b5310396310c5da2`。
- kernel gz SHA256：`3e2c7d106788b5322b9d5042bb560c5364eb448cc28c78b3f66664d83ec024fe`。
- kernel SHA256：`0f9f352da3ffed32066605451ec1f063d1c2add6cc0f7d94fb6f3e58237e2ecc`。

镜像：`/home/shenli/work1/out-bs/boot-16k-0120a11-battery-evidence.img`，
35,667,968字节，SHA256：
`41b0ceab10ab97f6767e1fd87556b0238387652a15e46bd5a4504aed15b055c5`。
原`--battery-evidence`参数，runtime1/IRQ4/ADCconv0；三键、触摸、USB IRQ及
USB日志保留，不启充。验证boot header v1与firmware/ramdisk实际切片一致，
从boot内FAT回读kernel/ADT/TOML匹配，参数无重复。
底稿`scratch/artifact-0120a11/`，无新设备结果或提供的CI run ID/构建日志。

用户自行fastboot boot。电脑桌面约30秒→拔10秒→当前电源约30秒→拔10秒→
电脑约10秒，记录线材/电源，确认插拔图标和按键/触摸。持久日志
`sweetlog-0120a11-battery-evidence.bin`足够。先看consumer/路由正常，再看稳定
sink CC分类、短期能力依据及拔线撤销。若沿用电源仍报Rp3A/misc39，预期事件
有效期内role1/observed1/reasons0/qualified1，kind2/max3000；如果misc bit4
清除，observed0同样正常，不要求它必须保持错误观测。APSD可FLOAT，3秒观测
到期与真实charging0仍为现有设计边界，不重复baseline或旧镜像。

## 0120a11实测：CC分类修复通过，拔线撤销正常

日志为工作区`logs/sweetlog-0120a11-battery-evidence.bin`，SHA256：
`4ec5fbc4c666bbe8fb5cb179e1e0ef7b4fde04c58c97b6d6fb7f00351d2db231`。
SWEETLOG v1，4MiB ring，有效779516字节、flush58、10086行，未回卷、正文无NUL。
底稿`scratch/artifact-0120a11/`的`analyze-log.py`、`log-analysis.json`、
`cc-verification.json`及解码文本；原始设备日志不入git。

### 本轮验证的主要结论

19笔输入记录（初始1、运行期18），19笔APSD options18/enabled1。稳定电源
接入后，id359先由plugin+Type-C事件取得CC依据；其后id361/362的source事件
得到FLOAT，继续采用原CC依据，**不再因misc bit4把普通sink Rp3A误拒绝**：

```text
id359: events=0x41 coherent=1 reasons=0 rp=3 advertised-ma=3000 orientation=2 qualified=1
       role=1 observed=0x1; INPUT CAP kind=2 max-ma=3000
       usb=0x10 sink=0x02 attached=0x23 misc=0x3b debug=0 legacy=0x04
       APSD=0/result=0，尚未完成
id361/362: events=0x10，APSD=0x21/result=0x10 (FLOAT)，CC仍qualified1/kind2
```

本轮misc3b与此前misc39只差方向bit1，orientation从1变2，亦说明方向位没有
误作拒绝条件；不据此猜测用户实际翻转插头的动作。observed1保留了misc bit4
诊断，既没有清位也没有把它隐藏。USBIN10没有OV/UV位。

三笔CC的session7、started-ns652523091875、completed-ns652550295000完全一致，
source事件没有刷新CC原始时间。首次FLOAT在同会话plugin后1.259秒，第二次
在1.371秒后。CC早于APSD完成即成立，满足独立来源设计；没有依赖伪造DCP。
初始化及电脑段仍拒绝普通sink许可，未把USB枚举成功当作电流授权。

以首次遥测开始为零点，关键采样窗口如下；它们不是物理插拔的精确时间：

| id | 相对秒 | 内核观测 |
| --- | --- | --- |
| 240/241 | 247.261/247.363 | 拔线，CC依据清零，external0 |
| 351～358 | 360.781～361.796 | 接入切换，VBUS/附着先后变化，有两次一致性拒绝 |
| 359 | 361.898 | 一致sink Rp3A，CC依据成立，external1 |
| 361/362 | 363.157/363.268 | source完成为FLOAT，CC依据保持原始时间 |
| 535/536 | 541.882/541.986 | 拔线，CC session/time清零、qualified0/kind0、external/capable0 |
| 584～587 | 590.197～590.973 | 重接电脑，external恢复；后续source返回SDP，普通CC许可仍拒绝 |

8次UI源状态发布均有updateStatus完成，包含完整撤销/恢复。该证据证明内核
发布链工作，用户没有另述屏幕现象，不能替代视觉验收。拔线晚于3秒观测期限，
本轮证明状态撤销和会话清理，不扩张为“在有效期内突发拔线竞态已实机覆盖”。
CC依据仍仅3秒有效，长期持续input provider尚缺；本日志没有独立过期日志，
不能将source保留时间验证直接说成已覆盖所有期限边界。

id351/358的USB字节前后变化报VERIFY/-12，撤销fresh/CC，后续IRQ取得一致
结果，没有timeout/总线损坏证据。拔线USB26含UV bit2，因此新的0x281c或
0x280c含欠压拒绝是断线状态的诊断组合，不是接电后新发生的稳定欠压故障。
本轮未覆盖VBUS仍存在时的OV/UV保护，也未新增独立OV/UV IRQ或验证source角色。

### 运行与回归

- 约609.832秒，606笔连续遥测（初始/arm-sync/事件共20 irq，保护586），全部
  电压/电流有效，3831～3923mV、IOPM符号电流-686～+5mA。20次profile全
  match1/integrity13/nominal4027mAh。保护monitor不是输入状态轮询fallback。
- 电池group1/events7f成功attach，mode4 armed；18次运行期刷新healthy1，
  路由至发布100.654～117.577ms，不是屏幕端到端延迟。mux命中rid11/cc8/
  vbus6/attach5/legacy3/usbin6/source3。VCONN/MSOC/CHGR自然事件未覆盖；
  没有据此认定这些路径失败，也不宣称本轮完成全部电池IRQ验收。
- ADC226请求/226有效且restored结果/432EOC，BATID20、USBIN206；223结果
  接受，3次USBIN因源epoch变化丢弃(error0)。有效BATID888646～889684uV，
  USBIN4490656～5104144uV；不能把插拔附近的低值当作稳定电源电压。无ADC
  超时，旧第二EOC问题未复现，尚未修复。总请求耗时7.463～160.536ms，不足以
  单独判断分阶段deadline或恢复耗时，仍需原计划的错误快照取证。
- 无`[ERROR]`、kernel panic或mux隔离。三键均有down/up（volume-down1组、
  volume-up2组、power3组），触摸计数至少192。USB两次ACM configured，
  后段重接电脑reset/ep0 warning后无新configured，仍保留已知gadget枚举问题。

19次充电配置全enable0，无CONTROL，UI charging0；稳定输入时策略allow0/
owned0、reasons7c0（identity/parallel/input/watchdog/limits等依赖未闭合）。
CC观测成功没有设置inputVerified，3A广播不是实际吸收电流或PD合同，亦不等于
可以启充。本轮达到的是**sink CC分类、来源保持和正常拔线撤销**的目标。

下一步进入正式输入能力管理：将已验证的CC/APSD与同会话有效ADC电压结合，
设计长期会话、变化/失效撤销、保护事件及typed接口；不靠轮询读取续短期依据，
不把CC kind2直接变成启充开关。继续补身份授权、并联隔离和watchdog依赖，
ADC故障取证仍为独立待办。无需重复本轮/baseline。本次只分析/更新文档，
未改驱动/CI、未push或操作设备，无新boot或CI run ID。
