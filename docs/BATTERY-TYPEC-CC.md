# Type-C CC/Rp 输入判断（0160）

**当前：0120a9实测在Type-C IRQ配置读取处失败，未完成CC运行验收。** 0161已本地
修正IRQ读取接口和失败发布，待新云编译/实机；实测与修复见文末。

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
不静默退回轮询。本轮没有预先硬编码Type-C的APID，也没有实机验证其IRQ写权限。
host测试中的APID271是模拟映射，**不是实机结论**。

## 采样与判断

复用已允许的只读状态：`0x1310` USBIN；`0x1506` Rp；`0x1507` debug accessory；
`0x1509` attach；`0x150b` misc；`0x150d` legacy。不扩大寄存器写权限。
同一有界采集前后对比VBUS和全部CC状态，变化/读取错误撤销valid，不能拿旧raw
当新证据。APSD状态和使能也保留前后核对。无sleep、重采循环或补偿轮询；一致
字节仅是软件检查，不能证明硬件原子快照，不能排除ABA或合并期间漏掉物理插拔。

`sb_cc_status()` 返回明确的Rp、广播电流、方向和拒绝原因：

| Rp原始掩码值（1506 & 0x7f） | rp枚举 | advertised-ma |
| --- | --- | --- |
| 0x08 | 1：default | 0，默认档不能直接授予500mA |
| 0x04 | 2：1.5A | 1500 |
| 0x02 | 3：3A | 3000 |
| 其余/组合/short | 0：unknown | 0 |

正常sink须VBUS存在、attach与CC附着、debounce完成、非source角色，且没有water、
VBUS error、VCONN OC、debug/legacy标记。misc bit1为方向，不按故障拒绝。
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
| 0x080 | VBUS error |
| 0x100 | VCONN OC |
| 0x200 | debug accessory |
| 0x400 | legacy/noncompliant |
| 0x800 | Rp未知/short/组合值 |

0120a8的`1506=02/misc=39/debug=0/legacy=4`会显示`advertised-ma=3000`，同时
`reasons=0x80`、qualified0、kind0，明确说明“看见广播，但错误状态阻止认可”。
不能把这条日志写成充电器已授权3A或已建立PD合同。

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
SWEET BATT CC TIME: v1 id=... session=... started-ns=... completed-ns=...
SWEET BATT INPUT CAP: v1 id=... kind=... max-ma=...; observed-only not-PD
```

`qualified`只指CC观测有效，不是电池充电许可。原始字节仍在同id的TYPEC行，
APSD来源日志保留。错误沿用ERROR无条件输出，INFO可按既有等级过滤。

IORegistry增加`SweetBatteryCCRejectReasons/CCRp/CCAdvertisedMA/CCEvidenceFresh`及
`SweetBatteryObservedInputKind/ObservedInputMaxMA`。前缀均为SweetBattery。
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
