# MH2LM 电池 IRQ：分阶段测试与结果

**当前0119：充电诊断九笔均state7/en0、无负电流；下一步同内核no-usb对照。**
0145 registry consumer、0146统一事务锁、0147按键workloop准备、0148隔离/日志
已进入0119。下面0115/0141/0142段落保留为历史，旧baseline/no-keys流程不再
作为当前前置要求。MSOC已实测，CHGR/SOC_READY变化事件未覆盖，IRQ大目标未结束。

## 0119充电诊断结果与当前下一步

已检查 `sweetlog-0119-battery-charge-diag.bin`，SHA256：
`36f445679818886a45e53eb1b44baa11750f3ec65efc6575d373ea1c44057875`。
头v1/ring4194304/total259899/flush49，未环绕、解码无NUL。实际参数保留
mode4/keys1/真实gauge/ACM2/vbus1，并明确sweetbattdiag1；75行确认诊断开启。
原始解码/逐行分析在工作区 `scratch/artifact-0119/charge-diag-persistent.txt`、
`analyze-charge-diag.py` 与 `charge-diag-analysis.json`。

- **九笔样本均未显示真实充电**：CHGR0x47/state7、CHARGING_ENABLE_CMD的en0、
  pause0、cfg2=0x2c（inhibit bit0未置位）。全部DIAG config-ok1/current-ok1。
  IBATT依次+488/+488/0/+460448/+460448/+539550/+488/0/+466308µA；未见
  Qualcomm FG充电方向的负值。71%降至70%，无充电趋势证据。IRQ稀疏采样，
  不能声称精确覆盖整段每一时刻或以插拔瞬间正电流推断长期供电比例。
- 插线样本INPUT path0x95、use-usb1、valid-input1；path/cmd均未suspend，
  ICL配置1500mA、AICL启用。当前有效输入已建立，直接可见的阻塞是充电使能
  仍关闭；ICL配置不等于测得USB实际输入电流。插入瞬间APSD0、较后MSOC
  样本APSD0x21/dtc-done1/slow-plugin1，不能用早期0就判检测一直失败。
- **112/114行首笔采样已经state7/en0，早于138行board、142行mux服务启动，
  以及1394行USB控制器初始化。** 禁充初态不能归因于之后的电池IRQ arm或
  gadget初始化；这也不排除gadget/override影响稍后的充电恢复行为。
- 156～177行两组接入正常，mode4/eventsf、无两秒轮询。六次USBIN刷新
  3003/3067/3080/3093/3465/3600，external交替0/1；一次独立MSOC命中3425，
  3431行events4刷新，连接期间71→70%。七次通知/采样1→7、全healthy1；
  未见CHGR/READY独立命中。诊断增加读取后通知至发布18.011～18.351ms，
  不是线缆到UI延迟，也不能与无诊断的约7ms直接归因于IRQ性能回退。
- `SWEET BATT SMB`九次均read-ok0；该链中的Type-C/SMB未读零值不可当硬件
  状态。与之区分，DIAG的config-ok1确实证明其0x1648 smben0已读成功；
  但SMB选择配置0x1690是否被读到、Type-C哪个地址失败仍未知。
  UI current-valid0表示现有驱动尚未把诊断电流发布到标准字段，不是IBATT读取失败。
- 本轮记录power3对、volume-up1对，未记录volume-down HID；触摸5对/单槽，顺序
  均配平，末次IRQ136/empty0。一次电源键息屏/亮屏，输入覆盖至最后一次插线前。
  无相关驱动ERROR/mux失败/隔离/panic签名；6条WARN均为既有USB ep0/5cmd6。
  USB只有早期计数/2次ACM配置，不能声称后段枚举全部正常。
  SpringBoard发起关机3624，尾部未证明服务stop/free完整执行。

**现在测已准备的对照镜像** `out-bs/boot-16k-0119-battery-charge-diag-no-usb.img`，
SHA256 `90655d8c533a0e90d09705938b797dd41617a1a35dc55941ae84cde2a761ce5c`
已再次核对。保留同0119内核、mode4/按键/触摸/真实电池和诊断；仅sweetusb0，
去掉sweetvbus/usbstep，不启动gadget，没有USB live日志。这是电池充电对照，
不是重新调试USB日志。无需push/云编译。

使用与主档相同的独立充电器和线，尽量相同操作：进桌面后拔电脑20～30秒，
接充电器3～5分钟，拔20～30秒，可再接一次；导出
`sweetlog-0119-battery-charge-diag-no-usb.bin`。同时记录电源类型/线和UI现象，
不从PMIC的external标志猜测是电脑还是独立充电器。

对照若进入state3/负IBATT且有CHGR命中，继续拆分gadget/override与启动残留
影响；若仍state7/en0，则USB gadget并非该条件下的唯一原因，推进逐字段
有效性修复和充电初始化/控制设计。单次对照不能证明所有电源条件或唯一因果。
不为取得CHGR事件直接写充电使能，不重复无诊断all档。

## 0119 mode4结果与当时下一步（充电诊断已测，见上）

`sweetlog-0119-battery-irq-all.bin` SHA256：
`3ede5ea87e447b1b45e54627128b1361906112af0f4dc54b3468b11c567351aa`。
头v1/ring4194304/total288962/flush86，未环绕、解码无NUL。原始记录/计数在
工作区 `scratch/artifact-0119/all-persistent.txt`、`all-analysis.json` 和
`analyze-all.py`；只使用持久日志，不要求补USB live。

- 98行mode4/preflight1；139～154行USBIN269、CHGR266、MSOC/READY283全部接入，
  group1/events0xf，armed且关闭两秒轮询。group0三键也成功接入。bootargs
  保留真实gauge、keys1、ACM2/vbus1、mode4；未开启sweetbattdiag。
- 四次独立USBIN刷新在3413/3431/3474/4123行，external依次0/1/0/1。
  第三次拔线之后、第四次插线之前有三次**独立MSOC硬件事件**：

  | mux命中行 / IRQ | 刷新行 / event | 原始MSOC / 发布电量 | 外接电源 |
  | --- | --- | --- | --- |
  | 3604 / 20 | 3606 / 0x4 | 0xbc / 73% | 0 |
  | 3824 / 21 | 3826 / 0x4 | 0xb9 / 72% | 0 |
  | 4020 / 22 | 4022 / 0x4 | 0xb7 / 71% | 0 |

  中间没有USBIN事件；不是插拔代采样或周期轮询。七次IRQ通知/采样严格1→7，
  全部healthy1，通知至发布7.269～7.399ms。三次FG刷新即完成真实电量发布；
  BATT UI诊断只记录电源连接/充电flag变化，未打印这三次percent变化是日志
  筛选条件，不代表跳过updateStatus。源码publish每次都调用updateStatus。
- 初始+arm-sync+七次IRQ共九笔采样，电量74→73→72→71%，最后接线原始MSOC
  0xb5仍取整71%。CHGR九笔均0x47/state7/charging0；无battery-chgr或
  battery-ready命中，不能把“全事件配置”记为“所有事件已验证”。MSOC固件
  门限仍未知，三次命中足以证明当前配置能自然触发，无需为门限读取SRAM。
- 三键11对（power5/up2/down4），触摸9对/单槽，顺序全部配平；输入在最后
  插线后仍继续。touch末次IRQ158/empty0。一次电源键display-off/on已覆盖，
  本日志未覆盖息屏插线唤醒（以前述mode3为证）。未见相关驱动ERROR、mux
  失败/隔离或panic签名；14条WARN全是既有USB ep0/ep5 cmd6/status1。
  USB只有早期hits128计数及2次ACM配置，不能声称后段枚举/实时日志无问题。
  日志尾部未记录系统完成关机，不能用其验证服务stop/free。

**当前进入实际充电与电池驱动完善。** 先复用0119现有只读诊断，核对已知能
真实充电的电源条件，见 [`BATTERY-DRIVER-NEXT.md`](BATTERY-DRIVER-NEXT.md)。
新打包入口（不编译内核）：

```bash
ref/ios-mh2lm/tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-0119.gz 0119 --battery-charge-check
```

- **先测** `out-bs/boot-16k-0119-battery-charge-diag.img`：保留mode4、三键、
  触摸和USB ACM，只增加sweetbattdiag1。SHA256：
  `c33d8d49eaffdfeb8859f9a211afc2599e791c7599833790bf3f72518e711f41`。
- **仅作后续可选对照** `out-bs/boot-16k-0119-battery-charge-diag-no-usb.img`：
  mode4/输入/真实gauge/诊断相同，但sweetusb0，移除sweetvbus/usbstep，没有ACM。
  SHA256 `90655d8c533a0e90d09705938b797dd41617a1a35dc55941ae84cde2a761ce5c`。
  不预判USB是原因，不要求现在同时测；先看主档与电源类型，再决定是否需要。

两张最终Android v1镜像的嵌入FAT/kernel/ADT/TOML及唯一参数已独立回读核验，
同一native kernel SHA81074d86…6c3c3ae2；完整清单、脚本和验证在工作区
`scratch/artifact-0119/charge-boot-manifest.json`、`verify-charge-boots.py` 和
`charge-boot-verification.txt`。不需要push/云编译，未改CI或驱动写路径。

主档测试：进桌面后拔电脑线留20～30秒，接曾确认能充电的独立充电器3～5分钟，
再拔20～30秒，可再接一次；记录充电器/线、亮屏或息屏、UI现象。导出
`sweetlog-0119-battery-charge-diag.bin`；USB live可选。IRQ模式并非每两秒打印
DIAG；数据来自初始/arm-sync和实际USBIN/CHGR/MSOC事件，等待时无新行不等于
日志失效。一次样本可能早于充电状态稳定，后续FG/CHGR证据须一起看。

注意旧DIAG的config-ok与SMB/read-ok是短路读取链的合并结果：失败时其后零值
不可解读为“寄存器为零”，read-ok0不可指认具体失败地址。current-ok单独验证
一致IBATT pair；必须结合state、USB、持续电流方向及SOC/电压趋势判断充电。

## 0119 mode3结果与当时下一步（mode4已测，见上）

已检查 `sweetlog-0119-battery-irq-charger.bin`；本轮未导出USB live，持久日志
足以分析现有事件，不要求补采。SHA256：
`26f7a12e66338e12efde80ed0c834a444bb9256b208ca1bae1f55ce950f40629`。
头v1/ring4194304/total317377/flush47，未环绕，解码无NUL。解码、分析脚本及
逐行证据在工作区 `scratch/artifact-0119/charger-{persistent.txt,analysis.json}`
和 `analyze-charger.py`。

- 89/98行身份预检通过、mode3/preflight1；139/148行分别接入USBIN APID269和
  CHGR APID266，149行group1/events0x3接入，152行armed且关闭两秒轮询。
  按键在自己的workloop准备后，153～160行三路/group0接入；实际bootargs
  保留keys1、真实gauge、ACM2/vbus1、sweetbattirq3。
- 3444～4399行共20次独立battery-usbin命中，各有events0x1/sourceIRQ刷新，
  notifications/samples严格1→20，全部healthy1。external交替0/1共十轮，
  每次均完成updateStatus。通知至发布7.103～7.780ms，不等于线缆到UI延迟。
- **未见battery-chgr运行时命中或含0x2刷新。** 初始、arm-sync加20次IRQ共22笔
  采样，CHGR全部0x47/state7、charging0。因此只能验收CHGR路由配置及共存，
  不能验收其硬件事件，也不能证明真实充电。本日志没有采到充电状态转换，
  暂不能判定是未产生转换还是CHGR投递异常；不因此盲改触发方式/充电控制。
- gauge显示始终74%，21笔原始MSOC0xbe、末笔0xbd，3953～4040mV。原始SOC
  已有变化但取整百分比相同；mode3未订阅FG事件，不能据此评价MSOC IRQ。
  电压随负载/插拔变化未导致百分比跳变。
- 三键HID共32条，power9对、volume-up3对、volume-down4对，顺序配平；触摸
  五槽54对且顺序配平，末次IRQ629/empty0。最后输入均在SpringBoard正常关机
  之前，未见0-client键事件。多次息屏拔插后出现display on及后续输入；例如
  off3968→拔线4035→on4045、off4064→插线4078→on4090。此为display-off
  唤屏，未验收整机深度休眠或修复既有锁屏渲染。
- 未见相关驱动ERROR、mux读回/隔离/停用或panic签名。13条WARN均为既有USB
  ep0/ep5 cmd0x6/status1。USB计数仅早期一笔hits128/empty0、TX0/1 err0，
  ACM配置2次；不足以声称反复插拔后每次USB枚举/实时日志均正常。
  0148隔离与真实服务stop/free仍未触发验收。

**下一步直接使用现有镜像** `out-bs/boot-16k-0119-battery-irq-all.img`，SHA256：
`320ff213e595a3aea0cc80e1eec5f5135f04921e9f4e7f3242d389e54e85c1dd`。
同一0119内核，sweetbattirq4，events0xf；保留三键、USB/触摸固定IRQ、真实gauge
和ACM，新增FG APID283的MSOC_DELTA/SOC_READY。无需push或云编译。

1. 启动后检查三键/触摸及一次拔插、息屏插线唤屏。
2. 拔线正常使用一段时间，使真实电量有机会自然下降；期间避免频繁插拔，
   以免USBIN采样掩盖“是否由FG自身事件刷新”。可先观察10～20分钟再导出，
   该时长不保证跨过固件未知的MSOC门限，不要求人为耗尽电池。
3. 导出 `sweetlog-0119-battery-irq-all.bin`；live可选。分析必须看到FG接入/
   mode4 armed，并区分battery-msoc/含0x4与USBIN刷新；SOC_READY含0x8按实际
   覆盖记录。CHGR继续保持未验收，不能因mode4启动正常或电量UI变化就勾选。

不重跑同样的mode3拔插来代替CHGR证据，不通过写SRAM/profile或充电控制制造
事件，也不加轮询兜底。电池IRQ目标仍未完成。

## 0119 mode2组合结果与当时下一步（mode3已测，见上）

用户反馈本轮未感到异常。`live-0119-board-startup-usbin.txt` 与
`sweetlog-0119-board-startup-usbin.bin` 已核对；持久头v1/ring4MiB，
total329685、flush37，未环绕。两份均无NUL；live覆盖前段，完整启动/拔插
以持久日志为准，不重复累计live回放。bootargs明确keys1/batteryIRQ2、真实
gauge、ACM2/vbus1，默认INFO。原始日志SHA及逐项计数保存于
`scratch/artifact-0119/usbin-analysis.json`。

- 持久139～160行：电池先接入group1；按键在自己的workloop准备后接入group0，
  两组均matched/attached。armed mode2，三键IRQ-only；未见0118的SET_TYPE
  读回失败、组隔离、停用或相关驱动ERROR。
- 3825、3996、4200、4406行有四个独立battery-usbin命中；notifications和
  samples均单调1→4，external依次0→1→0→1，各自updateStatus且healthy1。
  启动/arm-sync两笔加四笔IRQ，共六次真实采样，无周期轮询记录。
  通知至发布约7.217～7.504ms，不是物理插拔至UI延迟。
- 三键/组合及电源键息屏唤醒正常：最后一次touch前56条HID严格成对，power10对、
  volume-up10对、volume-down8对；其后关机前另有volume-down按下，SpringBoard
  发起正常关机后出现0-client up/down，尾部不完整，未将整段称为全部配平。
  关机尾部不等于IOService terminated/stop/free已经被真实验证。
- touch五槽共40对down/up，末次计数563/empty0；插拔后和唤醒后仍有触摸/键事件。
  显示3次off、4次on，其中一次息屏后replug经电池发布后亮屏。此为display-off，
  不是整机深度睡眠验收。
- USB末次计数1920/empty0，TX1276/1281、err0，ACM配置6次。12条WARN均为既有
  ep0/ep5 cmd0x6/status1；未见相关驱动ERROR、IRQ storm/timeout或panic签名。
  不据err0声称live无丢失；本次live未覆盖后段，持久已补齐。
- gauge始终75%，charging0/CHGR0x47；验证了USB存在变化和事件发布，未证明真实
  充电，也没有在mode2验证充电状态或电量变化事件。0148隔离路径未触发；0118
  故障未复现，不把控制器并发认定为已证明的唯一硬件原因。

同一0119原生内核已用 `--battery-irq` 生成六个分阶段镜像，所有最终Android
镜像内kernel/ADT/TOML、唯一启动参数和INFO/DEBUG选择均独立回读核验；清单见
`scratch/artifact-0119/battery-boot-manifest.json`。无需push/云编译。

**下一档：** `out-bs/boot-16k-0119-battery-irq-charger.img`，SHA256：
`f6a80c553288cb37523d4d4d7fa972aab85c5e62795005c9fafc3ff9e4006ad3`。
keys1、真实gauge、USB/触摸固定IRQ、ACM、`sweetbattirq=3`（events=0x3）。
先确认启动/三键/触摸正常，再两轮拔插并留线观察自然充电状态变化，覆盖一次
息屏插线/电源键唤醒。保存 `live-0119-battery-irq-charger.txt` 和
`sweetlog-0119-battery-irq-charger.bin`。必须区分USBIN事件与battery-chgr
独立命中/含0x2刷新；若CHGR始终state7、没有自然状态转换，记录未覆盖，
不强行写充电控制寄存器制造IRQ。看完此档，再进入已准备的mode4全事件镜像。

## 历史实现与测试记录

**0145 当前实现：** 电池改为实例持有的 registry matching/terminated 订阅，
调用 mux 公开 C++ 接口；旧全局 READY/C 桥接已移除。先等新云端内核并测新
baseline → no-keys → usbin，判据和退出边界见
[`MUX-CONSUMERS.md`](MUX-CONSUMERS.md)。0117 不含此修改，以下为之前的阶段记录。

**日期：** 2026-11-05。补丁 `0141-mh2lm-battery-event-irqs.patch` 基于
0114a15 最终 buildtree（0138→0139→0140 已生效）生成。完成本地检查；0115
云端产物的最终源码、内核符号与标记已核对，六个测试镜像已生成并回读验证。
mode1 的 live 与持久日志已核对，采样和既有三路 IRQ 正常，启动记录明确
`preflight=1`，只读预检通过。但 0115 mode2 在 mux 启动之前耗尽十秒等待，
电池服务停用、路由未接入。0142 已改为就绪事件交接并通过本地检查，待新一轮
云编译与实机验证。**暂停测试旧 0115 的 charger/all 档，不代表硬件 IRQ 已失败。**
已验证的真实采样包括 0114a15 和 0115 mode1，尚未验收电池 IRQ。

**当前先做启动链清理：** 用户暂缓电池测试。0143 增加独立板级入口，
mux 不再等待 keys 或 UFS。下一轮先用 `--board-startup` 的三个镜像验收启动
归属与关闭按键时的独立接入，再推进本文 CHGR/MSOC 阶段。电池检查点及测试
顺序见 [`DRIVER-STARTUP.md`](DRIVER-STARTUP.md)。0117 baseline 已验证独立
board/mux IOService 正常启动及三键/触摸/USB 回归；该档 battery IRQ=0，
尚未验证电池 READY 交接或实际 USBIN。下一档是同内核 board-startup-no-keys，
保持亮屏做两次拔线/接回（各等 5 秒），保留 live 与持久日志后再测并行档。
本档按键/组合键/软件按键唤醒被主动关闭，按既有恢复流程导出日志后切回有按键镜像。

0144 又将 mux 变为 IOService，由 board 发布晚期 nub 后正常匹配，资源归
服务实例；电池接口和 IRQ 路由不变。先按 `--board-startup` 验收新匹配/启动链，
不要把本地生命周期模拟通过当作真实 USBIN 已通过。见
[`MUX-IOSERVICE.md`](MUX-IOSERVICE.md)。

## 实现路径与边界

```text
PM8150B USBIN / CHGR / FG
  → PMIC-arb / PDC / 唯一 INTID513
  → 原有 GIC callback（屏蔽 SPI、排队）
  → SweetSpmiInterruptMux 的 IOWorkLoop（路由、确认、通知）
  → SweetBattery 自己的软件 IOInterruptEventSource / IOWorkLoop
  → 合并事件、observer 读真实状态、IOPMPowerSource::updateStatus()
```

mux 用路由表描述事件和触发边沿，绑定接口与物理 GIC callback 分离；没有
按功能写死的 switch，也没有新增第二个共享 SPI 所有者。目前仍是板级静态
route 表与两个 consumer group，不是完整的可枚举 PMIC 子驱动 provider。
按键三个 route 保留；DWC3、触摸和 ACM 日志路径保留。

新增 `sweet_spmi_mux_attach/detach()` 在 mux workloop 下串行执行。回调只向
电池事件源排队，不能等待电池 workloop；detach 返回后才允许释放 consumer
和事件源，避免交叉 workloop 死锁或释放后回调。电池先发布初始采样，再 arm，
arm 后补读一次关闭启动窗口。0141 原有的十秒重试已在实机暴露启动顺序问题；
0142 改用 `sweet_spmi_mux_watch_ready/unwatch_ready()` 单次订阅，battery
启动后立即返回，不阻塞 BSD 初始化。mux 硬件就绪后通知电池自己的 workloop
接入路由；mux 先启动或已失败时立即排队相应结果。

就绪通知与订阅取消由独立 IOLock 同步，锁内回调只能排队，不等待任何 workloop。
退出前先取消订阅、同步回调，再 detach 和释放事件源；就绪事件不计入硬件 IRQ
计数。等待期间不设置重试定时器、不扫描状态，属性
`SweetBatteryIRQWaitingForMux=true`、`SweetBatteryIRQHealthy=false` 明确
表示未就绪。若 mux 永远未启动，就保持显式等待；mux 实际初始化失败则 ERROR
停用，不能伪装健康或改成轮询。0143 由独立 BSD 板级入口
启动唯一 mux，既不依赖 `sweetkeys=1` 也不提前到电池早期启动阶段；按键与
电池分别接入，consumer 先后可互换。

新增路由先解析唯一 EE0 IRQ-owned APID，再访问该 channel。拒绝接管已经
使能的对应外设 IRQ 位；保存并恢复自己的触发位，保留其他位。电池 observer
读取与新增 IRQ 写入共用 transport lock；写入限于 IRQ 配置/确认寄存器
`+0x11..+0x16`，不写充电控制、限流、SRAM、profile 或算法启动寄存器。
RW 事务超时后该 APID 本轮启动禁止继续写入，避免复用可能晚完成的命令。

接入局部失败时尝试撤回电池路由；清理成功则按键继续工作。若清理失败或发生
共享 mux 确认失败、未知 pending、IRQ 风暴，屏蔽共享 SPI 并明确通知按键和
电池，**这类故障会影响按键**。因此本轮必须回归三键并发。顺带修正原 mux
累计 256 次 IRQ 后停用的问题，改为一秒窗口超过 256 次的风暴限制；本机
正常/异常速率仍待验证。

IRQ 档没有两秒状态扫描或低频遥测兜底。除启动采样、arm 后同步采样外，只有
硬件事件触发刷新；若既有“大幅 SOC 跳变需两次一致”保护拦住一次结果，允许
该事件最多一次 100ms 确认读取，单独标记 `source=bounded-confirm`。
不能把确认读取当作独立硬件 IRQ 的证据。

IRQ 档遇到 gauge/charger 无效或 pack missing，保留上次已发布状态、标记
`SweetBatteryIRQHealthy=false` 和 `SweetBatteryHardwareSampleValid=false`，
停止采样并撤回路由，输出 ERROR；不发布伪造满电，也不切回轮询。
初始预检或初始采样失败可导致服务无法启动，同样应检查明确错误。
电压、温度、电流等完整电池管理和充电策略不是本补丁的交付范围。

## 一次编译的测试档位

所有镜像保留 `sweetbattery=1 sweetbatt=1`、按键/USB/触摸 IRQ 和 USB ACM。
下面的后缀由 `tools/mk-mh2lm-event-matrix.sh ... --battery-irq` 生成。

| 镜像后缀 | sweetbattirq | 用途 | 周期采样 |
| --- | --- | --- | --- |
| battery-irq-baseline | 0 | 新内核的真实电池对照基线，不接电池 IRQ | 两秒 |
| battery-irq-preflight | 1 | 只读 REVID、FG 和既有 USBIN/CHGR 预检 | 两秒 |
| battery-irq-usbin | 2 | 仅接 USBIN_PLUGIN，验证插拔到 IOPM 链路 | 无 |
| battery-irq-charger | 3 | USBIN_PLUGIN + CHG_STATE_CHANGE | 无 |
| battery-irq-all | 4 | 上述事件 + MSOC_DELTA + SOC_READY | 无 |
| battery-irq-all-debug | 4 | 同一全事件配置，sweetloglevel=3 | 无 |

事件 mask：USBIN `0x1`、CHGR `0x2`、MSOC `0x4`、SOC_READY `0x8`。
模式 2/3 故意不接电量事件，空闲电量不刷新符合该测试配置，不能用于完整验收。
旧 `--irq-only` 入口默认仍是 mode0；测试时必须核对实际 boot arguments。

预检读取 SID2 `0100..0105`，要求 type=`0x51`、subtype=`0x20`，FG
`4005` subtype=`0x10`。
0115 持久日志已实测确认上述 type/subtype 和 FG subtype，revision1..4 为
`0/0/0/0x2`，不是推测值。mode≥2 不匹配或配置读取失败就拒绝 arm。
FG 只读预检仅查看全局 APID/owner 元数据和 observer 寄存器，不读取新的
FG channel MMIO；FG channel 的核对和
使能留到 mode4。USBIN/CHGR 两条历史安全路径仍按当次状态重新核对。

MSOC delta 沿用固件现有配置，不访问 MEMIF/SRAM。LG 源码 0.3% 是静态参考，
**当前设备门限未知**。若真实电量已变化却无 FG 事件，保留失败证据，再单独
设计有授权/版本 workaround/释放的 SRAM 检查，不能靠后台轮询补齐验收。
容量 5020mAh 与目标 profile 的差异亦另案处理，本轮不改 SOC 缩放或容量。

## 产物打包与实机顺序

0115 的 `logs/xnu-kernel-0115.gz` 和 `logs/xnu-buildtree-0115.gz` 已核对：
五个 0141 文件与本地实现逐字节一致，按键/USB/触摸/日志等级源码保持既有版本，
最终内核包含电池 IRQ 符号与标记，manifest 校验通过。kernel SHA256 为
`a601cb597d38697832e4324fbb4341c75810034722ed95b139472bdb65dae6f2`。
本次未提供构建日志或 CI run ID，尚未审查完整构建警告。

六个临时镜像已生成于工作区 `out-bs/boot-16k-0115-battery-irq-*.img`；
最终 Android boot 镜像的 ramdisk、FAT 中的 kernel/ADT/启动参数均已回读核对，
完整镜像 SHA256 记录在 `NOTES.md` 和
工作区 `scratch/battery-irq-0115/boot-verification.json`。
用户已跳过 baseline 直接测试 preflight：运行段未见新增驱动故障，因此不要求
补测 baseline。持久日志补齐后预检明确通过；随后 mode2 暴露下面的启动等待
错误。**当前需要 push 0142、下载新内核后重测 USBIN，不继续测试旧 0115 IRQ 档。**

`live-0115-battery-irq-preflight.txt` 的 boot-args 确认 mode1；54 次真实采样
稳定 70%、4009mV，三键 12 次路由与成对 HID、触摸及 USB ACM 均继续运行，
未见驱动 ERROR/停用。插线但 CHGR state7 未充电，与前一真实采样基线一致。
该文件从 launchd 附近开始，未包含 REVID、FG 路由和 `selected mode/preflight`
输出。mode1 即使预检失败也会保留轮询，因此不能用成功启动代替预检结果。
随后 `sweetlog-0115-battery-irq-preflight.bin` 已补齐记录：REVID 确认
PM8150B，USBIN APID269、CHGR APID266、FG APID283 均为 EE0 IRQ owner。
FG 外设 edge/high/low/enable/latch 字节均为 0，RT=0x2；
`selected mode=1 preflight=1`。FG route-parts=0x3 没有访问 channel，
打印的 acc-enable/status=0 不能作为它们已测得为零的证据。详细值见硬件基线。

### 0115 USBIN 失败与 0142 修正

`sweetlog-0115-battery-irq-usbin.bin` 解码后第 98 行为 `mode=2 preflight=1`，
111 行初始采样 70%/4009mV/外接电源，116 行出现
`FAILED mux not ready within 10s ... notifications=0 samples=0`；134/135 行
才出现按键 mux 探针和 arm 成功。没有任何电池 attach/arm/事件刷新。
电池在早期 IOKit 启动，mux 在后续 BSD 按键初始化才启动，固定十秒等待
使电池提前放弃。这足以解释插拔状态冻结和缺少电池事件唤屏，尚不能评价
USBIN 的真实投递、极性或 ack。三键/USB/触摸仍在工作。

0142 修正上述就绪交接，尚未进入已验证的实机镜像。新产物下载后使用下方
通用命令打包，再 `fastboot boot out-bs/boot-16k-LABEL-battery-irq-usbin.img`。
不因这次错误重复完整预检；mode2 自身仍会核对身份与外设状态。

插拔约三轮，每次停留 5–10 秒；同时回归三键/组合键和触摸，再覆盖一次
display-off 插拔。mode2 没有两秒电池采样，重点看插拔事件与状态通知；
导出新编号的 `sweetlog-LABEL-battery-irq-usbin.bin`，有 live ACM 日志也一并保留。
拔线中断 ACM 是正常现象，早期 arm/拔线段以持久日志补齐。未接 CHGR/MSOC，
所以本档不能验收充电状态和电量变化事件。

后续需要重打包时，在工作区根目录执行，`LABEL` 替换为实际产物编号：

```bash
ref/ios-mh2lm/tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-LABEL.gz LABEL --battery-irq
```

生成位置为 `out-bs/boot-16k-LABEL-battery-irq-*.img`。脚本检查 kernel manifest、
0141/0142 标记、ramdisk 回读与 payload 一致性；旧 0114a15 和未修复的 0115
内核会被明确拒绝，不产生新镜像。旧 0115 镜像不删除，保留为失败对照。
所有测试仅 `fastboot boot`，不刷启动分区。

按顺序推进，每步保留原始日志和现象；有异常先停在该步分析，使用同一内核
的其他档位定位，无需为了换参数重复云编译：

1. **baseline：** 正常进桌面，真实电量、三键/组合键、触摸与 ACM 均能工作。
   记录启动时线缆状态；确认不是静态 100% 占位。
2. **preflight：** 获取 REVID revision/type/subtype、FG APID/ownership、
   edge/high/low/enable/latch。预检成功只说明允许继续，不等于 IRQ 投递成功。
3. **usbin：** 插拔几次，含 display-off 后插拔；要求 `battery-usbin` route
   命中、`refresh events=0x1 source=irq` 以及真实 external 变化/IOPM 通知。
   同时按三键、使用触摸，确认共享 mux 未影响按键。拔线时实时 ACM 会断开，
   重连后补取持久 SWEETLOG，不重复调试已经可用的日志通道。
4. **charger：** 保留上述操作，查 `battery-chgr` route 与包含 `0x2` 的事件。
   必须看真实 CHGR 状态变化，单看插线或 UI 闪动不算充电状态 IRQ 验证。
   没有自然状态转换就记录未覆盖，不为制造事件写充电控制寄存器。
5. **all：** 验证 FG 接入、MSOC route 与包含 `0x4` 的刷新；让真实 gauge
   电量发生变化并保存足够长的日志，不能规定短短几秒不变就判失败或成功。
   SOC_READY `0x8` 可能保持稳定而不触发，记录实际覆盖情况。回归启动已插线/
   未插线、息屏/亮屏、三键/组合键、触摸、ACM 和重复插拔。
6. **all-debug：** 仅需要细节时再测；保留 mode4 的事件配置。全程使用既有
   USB 日志工具；断连或启动异常使用持久 SWEETLOG 补充。

日志至少保留：`selected mode`、`REVID`、`attach route`、
`consumer attached`、`armed mode`、`hit route=battery-*`、
`refresh events`、`SWEET BATTERY SID2`、`SWEET BATT UI`、ERROR/WARN。
重测还须看到启动交接：`waiting for mux readiness event` → mux armed →
`mux readiness event received` → consumer attached → 电池 armed → arm-sync。
如果 mux 先就绪，先后顺序可不同；必须最终接上，不能只有等待或初始采样。
`route-to-publish-us` 从 mux 通知时刻近似计时，不包括硬件到 GIC/mux 的延迟，
事件合并时也不能解读成每个边沿的精确耗时。记录 IRQ 通知/采样次数、产物和
镜像 SHA256、boot arguments、线缆状态、操作顺序与原始日志名到 `NOTES.md`。
空闲 IRQ 模式不应继续出现每两秒采样；允许前述明确标记的启动与有界确认。

display-off 只指现有逻辑息屏，不代表深度休眠；已知锁屏局部黑屏仍归 framebuffer
后续目标。电池事件成功亦不等于真实充电控制已接管；旧 USB 多次插拔偶发枚举
失败要与本轮 IRQ 路由错误区分，不以历史现象替代本轮日志分析。

## 本地验证与剩余限制

- 0141 对精确 0114a15 基线 `git apply --check --whitespace=error-all` 通过；
  新目录应用后五个文件与编译源逐字节一致。
- 三个翻译单元生成 ARM64 Mach-O 对象，C/C++ 导出符号名称一致。
  Linux clang19 缺少 Apple XNU 类型内建函数，本地 C++ 检查使用仅存在于
  scratch 编译脚本的兼容宏；不能替代 Apple 工具链原生完整构建和链接。
- `tools/tests/test-mh2lm-battery-irq.py EXTRACTED_XNU_DIRECTORY` 对实际
  mux 源码替换 MMIO/IOKit 调度后，在 ASAN/UBSAN 下检查触发掩码、无关位
  保留、外部 EE 拒绝、部分接入失败回滚、带 queued IRQ detach、累计 600 次
  正常 IRQ 不停用、密集风暴显式停止、超时 APID 禁止复用及通知两个 consumer。
  模拟器不能证明真实 PMIC 时序、硬件投递或完整电池服务生命周期。
- 0142 对精确 0115 基线应用后，三个文件与编译源逐字节一致，两个 C++ 单元
  ARM64 对象检查通过，C 链接符号匹配；原生 CircleCI 完整构建和实机仍待验证。
  测试新增提取实际 battery 的七个就绪/IRQ/退出函数，仅替代采样、属性发布
  和调度环境；验证 mux 晚 30 秒启动时无定时重试/状态读取、mux 先启动、
  订阅前后初始化失败、取消待就绪与已排队通知、正常 IRQ 刷新和退出同步，
  以及 200 组注册/发布与取消并发竞争。使用 ASAN/UBSAN；不等同原生 workloop
  并发或完整服务生命周期测试。
- 打包脚本语法、Python 语法、CircleCI YAML 解析通过；旧内核缺少标记时
  打包被拒绝且未生成镜像。CircleCI 实际使用的
  `.github/workflows/circleci.yml` 增加 0141/0142 源码门禁，原编译命令未改。

源码工作目录为工作区 `scratch/battery-irq-0114a15/xnu/`；精确基线在
`base/xnu/`，补丁应用核对目录为 `verify/`，编译脚本/日志/对象在同级 scratch。
这些本地产物不进 git；仓库提交补丁、打包入口、测试脚本与文档。
0115 产物源码核对目录为工作区 `scratch/battery-irq-0115/xnu/`。
0142 的精确 0115 基线、修改源、应用核对与编译记录在工作区
`scratch/battery-irq-startup-0115/`。下一步由用户 push，下载新产物后重测 USBIN。
