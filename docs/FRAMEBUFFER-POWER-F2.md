# F2：面板 Display Off/On（0177，0121a9基础与长时唤醒已验证）

2026-11-09。接续0121a7已验收的F1b正常使用路径。0177的0121a9基础开关屏及长时电源键唤醒已验证，用户确认显示/亮度/触摸正常；完整插拔唤醒次数覆盖仍保留，不记全矩阵验收通过。新增`sweetfbpower=1`，默认0，要求
`sweetfbservice=1 sweetmdss=1 sweetfbcontract=0`，配置冲突明确ERROR并拒绝启动。

## 首轮范围与参考依据

先只在ABL已初始化、保持供电的面板上发送DCS Display Off `28` / Display On `29`。
这是真正禁止/恢复面板显示，不是完整休眠或电源轨下电；不发送`10/11`、不改reset/GPIO、
regulator、扫描地址、硬件vsync或GPU。实际DDIC是否按预期执行仍须实机视觉验证。

已核对`work1/docs/panel.md` §5及LG原始实现：

| 来源（相对work1/ref/android_kernel_lge_sm8150） | 核对事实 |
|---|---|
| `arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-pv-tianmamd-fhd-cmd.dtsi:409` | ON末尾先29，再11；11后等待150ms |
| 同文件`:414` | 完整OFF为B0/03、51/0000后等90ms、28、10后等150ms；两组均LP |
| `drivers/gpu/drm/drm_mipi_dsi.c:459` | 本树短包header为DCS命令、参数、dtype、控制字节 |
| `drivers/gpu/drm/msm/dsi-staging/dsi_ctrl.c:987`、`:1221` | 短包无long-payload位；last设header字节3 bit7 |
| `drivers/gpu/drm/msm/dsi-staging/dsi_ctrl_hw_cmn.c:660`、`:740` | 嵌入式命令DMA bit28、LP bit26 |
| 同文件`:1576` | command-mode等待DSI_STATUS中的MDP busy清零，不依赖TE行计数前进 |

因此0177发单个短包字`0x80050028/0x80050029`，VC0、last1、ack0、LP；复用既有
FIFO/DMA引擎，奇数word补齐但DMA长度仍为4字节。LG完整ON/OFF序列作为后续完整休眠
参考，不把本轮28/29子集称为照搬完整序列，也不将150ms错误地放到每个28/29之后。
PV是源码候选，尚无DDIC读回确认选中revision；不据此猜vendor寄存器或重发初始化blob。

## 架构与接口

```text
/arm-io/disp0
└─ AppleCLCD : IOMobileFramebuffer
   ├─ UserClient selector12 → dispatch参数检查
   ├─ 私有IOWorkLoop / IOCommandGate
   │  ├─ 逻辑off/on、pending、dirty、timer/CPU呈现
   │  └─ requestPanelPower(off)
   └─ SweetDisplayClient（本consumer独立匹配/retain）
      └─ SweetDisplayBackend::setPanelPower(on) ──────────┐
/arm-io/mdss                                           │
└─ SweetMDSS : SweetDisplayBackend : IOService ◄─────────┘
   ├─ 私有IOWorkLoop / IOCommandGate
   │  ├─ 面板状态、故障降级及电源命令
   │  ├─ 背光DCS写（独立consumer）
   │  └─ 探针、stop admission
   └─ ADT映射 / DSI FIFO与DMA
```

前端保留现有iOS IOMobileFramebuffer selector12入口。它经过标准dispatch验证、进入
AppleCLCD gate，再通过retained typed backend请求；不直接读取/写入寄存器，不调BSD
显示全局函数。后端在自己的gate中检查admission、inactive及clock，再操作MMIO。
这是现有IOKit显示服务的语义电源接口；尚未实现完整IOPM系统睡眠/电源域管理。

锁序为AppleCLCD gate→SweetMDSS gate；后端不在gate内回调consumer，registry通知仍只
撤回引用/投递事件。背光与电源写共用provider gate，硬件写不会交错，亮度目标仍由
CoreBrightness决定，电源路径不覆盖亮度值。

## 正常状态与保留像素

后端状态：`ON(0)→OFF_PENDING(1)→OFF(2)→ON_PENDING(3)→ON`，另有`UNKNOWN(4)`。
初始ON依据本项目ABL continuous splash基线，不重放面板初始化。同状态请求幂等，不重发
成功的28/29；consumer仍转发重复ON，避免掩盖后端UNKNOWN恢复。

off请求先在framebuffer gate内阻止CPU呈现，再发28。无论boot参数preserveoff如何，
F2强制保留扫描像素，timer不会淡出/擦黑；息屏期间SwapEnd仍可更新最新pending。
on发29后恢复原有timer呈现；状态变化将dirty置位，使用原缓冲/最新pending重画。
保留原软件vsync、surface生命周期、裁剪及CPU拷贝，不提前实现F3/F4。

新电源事务在停新kickoff/autorefresh后等待DSI busy清零，不等面板TE/行计数前进，避免
关屏后唤醒命令卡在已停止的TE上。F2开启且面板OFF/UNKNOWN时，背光事务也走该idle条件；
F2关闭时原亮度传输逐次MMIO读写轨迹与旧引擎一致，恢复寄存器的顺序保持。
每次事务结束仍恢复ABL的INTF/DSI临时设置，不关闭硬件扫描或时钟。

## 失败、停止与日志

- 事务失败可能已送达DDIC，不能简单标成旧状态。后端进入UNKNOWN并置fallback。
  off失败后在同一provider gate中只补偿一次29；成功才标ON，补偿失败保持UNKNOWN。
- 一次故障后，本次provider生命周期停止发送新OFF，退回保留像素/原逻辑背光策略，
  明确返回非成功并打印ERROR。后续ON如处于OFF/UNKNOWN仍可重试，成功ON也不重新启用OFF。
  没有周期重试或无限重试；重新启用电源实验需重新启动测试镜像。
- 亮度连续失败预算与电源恢复独立，不能因为亮度写被禁就禁止必要的29。
  这不承诺DSI硬件故障一定能恢复；失败日志与UNKNOWN必须保留。
- provider未匹配时前端记录ERROR并保留逻辑请求；不在匹配通知里自动发DCS。
  之后同状态的明确电源请求仍会转发，通知回调继续只维护绑定信息。
- selector12保留兼容层原有成功返回语义；硬件返回码另行记录/发布。接口调用rc0本身
  不可用来证明DCS成功，验收须同时核对`SWEET FB POWER`和`SWEET MDSS POWER`。
- stop与正在运行的硬件事务串行，关闭admission后无MMIO；gate/maps仍留到free保护
  retained调用者。stop不自动向面板发28/29，真实provider生命周期仍未上机触发。

成功日志为INFO：`SWEET FB POWER: off/on rc=0 elapsed-ms=`；provider同时记录状态/耗时。
失败为ERROR，含原始返回码、是否补偿、补偿返回码、状态及fallback。原DCS引擎ERROR保留。
registry属性有`SweetPanelPowerControl`、`SweetPanelPowerLastResult`（consumer）和
`SweetPanelDisplayState`、`SweetPanelPowerFallback`（provider）。

本轮是同步runAction，调用线程持有两级gate直到事务完成；不声称全部搬到后台线程。
新路径每次DSI idle等待最多200ms、DMA完成等待最多200ms；off及补偿合计最坏约800ms，
另有少量寄存器/日志开销。实际耗时和UI延迟待实测；不在未完成的事务中释放硬件gate
以容许其它写入。是否需异步化由实测决定，不先引入额外worker/超时重试层。

## 本地验证

0177只改四个kernel文件：`SweetDisplayBackend.h`、`SweetMDSS.cpp`、
`SweetFramebuffer.cpp`、`SweetFBService.inc`。USB/按键/电池/SPMI及CI配置未改。

- 对0121a7对应源码strict apply，四文件逐字节核对；114个既有CI源码marker通过。
  新marker放在打包入口，按用户要求不改CircleCI。
- base/edited/verify的Framebuffer与MDSS用已记录CI组件flags/headers在Linux生成arm64
  对象，保留既有工具链兼容定义。base/verify告警类别和数量均相同，含FB的2条既有
  format告警；不声称零告警或等同完整Xcode链接。
- 17项host检查通过，ASan/UBSan用于原有instrumented fixture；LSan仍关闭
  `ASAN_OPTIONS=detect_leaks=0`，不声称LSan通过。
- display-service fixture执行真实header/provider/helper，模拟MMIO与OS原语；独立比较
  旧亮度全部成功/失败轨迹，验证短包/LP/长度、重复off/on、TE冻结、off补偿成功/失败、
  ON失败重试、亮度预算独立、clock guard、与stop并行及引用/映射归零。
- framebuffer fixture执行实际FB/UserClient，后端硬件用typed stub：验证ABI拒绝无命令、
  未匹配/硬件失败的逻辑降级、强制保留像素、息屏pending、重复ON转发和stop拒绝。
- 打包脚本`bash -n`通过；0121a7旧内核因缺F2 marker正确拒绝，未生成冒充F2的镜像。

底稿`work1/scratch/framebuffer-0177/{base,edited,verify,implement.py,compile.py,
verification.json,warning-counts.json,tests.json,test-*.txt,old-kernel-rejection.txt}`。

## 云编译后测试

用户push后下载新kernel/buildtree/log；先核对四源码/内核marker/manifest，再执行：

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-power
```

生成`fb-power-baseline`和`fb-power-dcs`；均mdss1/service1，只差sweetfbpower0/1。
保留IRQ/ACM/runtime3及preserveoff1，trace/proof/contract0，probe1/loglevel2。
0121a9两档boot已回读通过，确切镜像及校验值见下节。

优先测DCS档，启动等35秒：

1. 亮度低→高→低、三键/连续触摸滑动；手动电源键息屏等3秒再唤醒，5轮。
2. 息屏插电/息屏拔电各5轮，关机界面/取消；对比已有画面基线，特别记录新增局部黑屏、
   不亮、冻结及明显迟缓。BUG-003缺时间/半透明颜色不要求在F2自动修复。
3. 导出`sweetlog-编号-fb-power-dcs.bin`，日志需实际28/29成功、无fallback/UNKNOWN。
   若出现错误，即使肉眼无异常也不验收；有新增问题再测同核baseline。
4. 短测通过后，同镜像做30分钟手动息屏，再按键唤醒并取长测日志；不要求重新编译。
   若借此息屏充电，BUG-004仍可能使充电不稳定，单独记录，不把30分钟等同有效充电时长。

没有自动息屏，auto-lock/未实现通知不是测试前置。不重开缺时间专项或充电A/B。
BUG-001/002/003/004未关闭；发生mux隔离后的按键/充电段不能误归为显示电源故障。


## 0121a9 云产物与boot核对（实测结果见下节）

预期实现提交`decebe3`。44个已跟踪源码文件逐字节匹配，包含0177四文件；114个既有
CI源码marker及F2内核marker/状态属性名通过，kernel归档manifest核验通过。
未收到本轮完整build log/CI run ID，未核对全量编译告警。

- kernel SHA256：`50d9fc40ebb98ca23d424ba743513a8cb00f873b8a330032f95583769acd7e55`，与0121a7不同。
- `out-bs/boot-16k-0121a9-fb-power-baseline.img`：35,667,968字节，SHA256
  `5277895a1708cec2a2943bdfcd442f7db2be6144db3bd0b4bcc73f8212720826`。
- `out-bs/boot-16k-0121a9-fb-power-dcs.img`：35,667,968字节，SHA256
  `afc416c3e3b5c8a60b0290ff843cde9fff6c09f3d310266cd11221997e70c44c`。

最终Android boot回读header/FAT，kernel blob/ramdisk/XNU/派生ADT/TOML逐字节匹配；
参数无重复，两档仅sweetfbpower0/1不同，均mdss1/service1，原ADT字节未改。
IRQ/ACM/runtime3、preserveoff1保留，trace/proof/contract0、probe1/loglevel2。
底稿`work1/scratch/artifact-0121a9/{verification.json,source-compare.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}`。

优先`fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a9-fb-power-dcs.img`；启动等35秒，
按上节短测步骤做亮度/输入、手动off-on、息屏插/拔各5轮及关机界面/取消，导出
`sweetlog-0121a9-fb-power-dcs.bin`。有新增问题才测baseline；日志确认无电源错误/
fallback/UNKNOWN后再用同镜像做30分钟手动息屏测试，不必重编译。
本轮只核对/打包/文档，未改内核或CI、未push或操作设备，F2尚未验收。


## 0121a9 实测：基础DCS开关屏通过，长期及充电唤醒保留边界

用户反馈：“息屏和唤醒正常，未见新增显示问题”，另报告旧充电bug复现。
日志`sweetlog-0121a9-fb-power-dcs.bin`，SHA256
`f02beab6ad0b0d9fbf2882ba0d73ae3a8205fbd49c22c1910ba120a7df4810ca`。
文件4,198,400字节，ring4,194,304字节，有效372,499字节，flush51，未回卷，4,993行。
底稿`work1/scratch/artifact-0121a9/{fb-power-dcs.txt,power-log-analysis.json,
analyze-power.py,power-and-battery.timeline.txt}`。

| 核对项 | 解码行号及观察 | 边界 |
|---|---|---|
| 实际路径 | 76 F2启用；357参数power1/mdss1/service1；94 FB后端match，95背光match | 新电源路径确实启用 |
| 第1组实际事务 | 4738 off rc0/state2/9ms →4774 on rc0/state0/11ms | 前端4739/4775同样成功 |
| 第2组实际事务 | 4896 off rc0/state2/4ms →4908 on rc0/state0/3ms | 前端4897/4909同样成功 |
| 第3组实际事务 | 4923 off rc0/state2/9ms →4937 on rc0/state0/0ms | 0是毫秒取整，不代表零开销 |
| 启动ON | 2270前端on rc0/0ms，provider无事务 | splash初始ON幂等处理，不能额外算一次29 |
| 电源异常 | 无FB/MDSS POWER错误、fallback或UNKNOWN，无显示DCS失败、SPMI timeout/panic | 整份日志有电池/mux ERROR，不宣称零错误 |
| 回归观察 | 35条亮度请求768～2374、8条预算内DCS成功；24条键记录（power18、volume-down6）、21触摸、1次ACM配置 | 未捕获volume-up，不声称日志覆盖三键全验收；5次USB reset/5条endpoint WARN不单独定BUG-001 |

6次实际电源事务耗时取整为0～11ms；这不是端到端唤醒延迟测量，也不包括调用进入
framebuffer gate前的排队时间。结合用户视觉确认，可记录基础28/29开关屏成功，无新
显示异常。没有5×3轮完整覆盖或30分钟时间证据，不补造次数或标F2整体验收。

**充电旧bug与证据有效范围：** 4690行Type-C CC/APID271/bit2/status0x4持续pending，
4692隔离电池group1；4722–4727电池consumer退出、disable0、撤回输入，保留93%。
在途id43完成后没有新refresh；按键group0未隔离。该事件早于第一条实际28，之后
三组28/29成功；属于BUG-002复现，不是面板电源报错。4份USB VERIFY(-12)/3次遥测
停充/恢复链另补BUG-004；本轮源端类型未知，不假设仍电脑USB。

电池隔离后的插/拔电唤醒不能用于F2验收：上游已不发布有效电池事件；此时无反应不能
判为29失败，继续重复插拔也不会补足证据。电源键直接触发的panel off/on仍有效。
旧bug独立排期，不为F2改mux/充电、不新增完整充电对照或重做整套短测。

### 四类操作分别验证什么

| 操作 | 对F2的目的 |
|---|---|
| 调亮度、按键与连续触摸 | 小范围回归：电源调用共享硬件gate，检查亮度和输入响应未退化；不是新增外设适配任务 |
| 电源键息屏/唤醒 | 核心验证：selector12请求→typed backend→28/29→panel黑屏/恢复 |
| 息屏插电/拔电 | 验证电池事件这一不同唤醒来源也能触发开屏；不在此重测真实充电能力，group1隔离后此项证据无效 |
| 关机界面与取消 | 检查UI切换/返回锁屏和部分重绘无新异常；属于回归，不宣称每次取消都会发送28/29，也不重开BUG-003专项 |

**短测后安排（已完成，结果见下一节）：** 同一个DCS镜像，保持USB连接状态不变，手动
息屏30分钟，再用电源键唤醒，确认显示/亮度/触摸恢复，导出
`sweetlog-0121a9-fb-power-dcs-long.bin`。不需要重编译或重复整套短测；只把它计为
长期显示检查，不能称有效充电30分钟。充电唤醒未覆盖项保留，不由旧bug自动扩大显示
工作范围。当时F2整体尚未验收；最新进度以下节为准，默认开关保持0。


## 0121a9 长测：手动息屏后正常唤醒

用户完成原DCS镜像的长测，并明确确认“都正常，未见新增问题”（画面、亮度和触摸）。
日志`work1/logs/sweetlog-0121a9-fb-power-dcs-long.bin`，SHA256：
`0ae824787b04471ec67b721c78e3ea5434ed924319ad3aff8bd333a9b6228335`。
文件4198400字节，ring4194304，有效547519，flush550，未回卷，8573行。
底稿`work1/scratch/artifact-0121a9-long/{analyze.py,analysis.json,fb-power-dcs-long.txt,timeline.txt}`。

| 核对项 | 解码行及结果 |
|---|---|
| 参数/实现 | 76启用F2，boot args仍power1/mdss1/service1；原0121a9 / 0177镜像 |
| 关屏 | 3472 MDSS off rc0/state2/12ms；3473前端成功，3474 display off |
| 长时区间 | 3493至7922行87个电池采样时间戳，首末started-ns相差4805.156秒（约80分钟） |
| 电源键唤醒 | 7952按键down；7958 MDSS on rc0/state0/4ms，7959前端成功，7960 display on |
| 亮度恢复 | 3460请求0；7972恢复2838，与2333启动亮度相同；对应DCS rc0 |
| 输入 | 10条HID key（power8、volume-up2）；19条touch，其中18条在唤醒后；用户确认触摸正常 |
| 异常 | 无分级ERROR/WARN、mux隔离/ACK失败、显示电源fallback/UNKNOWN或panic；10条DCS日志均rc0 |

用户按30分钟要求操作，内核采样时间证据覆盖了更长区间；上述4805秒是息屏日志段内的
采样时间戳跨度，不是精确off-on或外部秒表测量。12/4ms是命令调用耗时的毫秒取整，
不包含所有排队和UI恢复时间。

本轮100个有效电池样本、17次IRQ refresh，无USB VERIFY(-12)，无ACM配置/USB reset。
不能据此补造USB/插拔测试，也不能把显示息屏视为CPU/系统suspend、完整面板断电或
有效充电时长。后台仍有保护计时采样和用户态活动，符合本阶段仅做28/29的范围。

**结论：基础28/29与长时电源键息屏/唤醒正常使用路径通过。** 完整5轮×三类唤醒矩阵
没有补齐，旧短测group1隔离后的充电唤醒仍不计有效证据；保留这项覆盖边界，不安排
BUG-002专项重测。原镜像不含0178，长测未复现隔离不是BUG-002修复有效的证据。
BUG-002已由`41a0efa`代码修复，后续随正常使用观察，详见[台账](BUGS.md)。
本轮仅分析/文档，无新镜像/CI变动；未开始F3实现，后续回到显示计划推进。


### 长测后续：耗电不属于本阶段已验收项

用户随后报告明显掉电。复核息屏前91%→末次78%，息屏段87个样本平均净放电约311mA，
离散积分估算约415mAh/1.70Wh，平均约1.28W。已有面板显示关闭/正常唤醒证据仍成立，
但完整系统/设备低功耗没有实现，不能把本次长测记作正常待机功耗。CPU suspend接口
尚未实现、OFF仍60Hz软件vsync、USB/触摸/UFS低功耗及后台重复启动等均已核对，见
[电源管理审计](POWER-MANAGEMENT-AUDIT-0121A9.md)。不据总电流直接分配各驱动功耗，
不把缺失的低功耗适配误记为已完成后回归，也不为BUG-002追加专项测试。
