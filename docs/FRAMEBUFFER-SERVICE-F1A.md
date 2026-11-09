# F1a：显示资源与背光服务迁移（0175）

2026-11-09。状态：实现提交`a22e1b3`；结合0121a5对照及0121a6日志和用户观察，F1a资源/背光迁移正常运行路径功能验收通过，进入F1b。F1整体未完成；真实服务终止路径仍未上机验证。
用户决定暂缓[BUG-003](BUGS.md)，进入[显示F1](FRAMEBUFFER-PANEL-PLAN.md)。
F0保留未闭环项；本轮不再安排缺时间专项取证。滑条区域半透明颜色异常也保留，
不是“几何重叠”，不认定与缺时间同因。

## 本轮架构与范围

```text
IOPlatformDevice (ADT /arm-io/disp0)
└─ AppleCLCD : IOMobileFramebuffer
   ├─ 现有用户客户端 / CPU裁剪拷贝 / 软件vsync
   └─ AppleARMBacklight : IOService
      ├─ 请求锁 + 自有workloop / 渐变timer
      └─ SweetDisplayClient（每consumer实例）
         ├─ IORegistry first-match / terminated通知
         ├─ software IOInterruptEventSource → 背光workloop
         └─ retain SweetDisplayBackend* ───────────────┐
IOPlatformDevice (ADT /arm-io/mdss)                   │
└─ SweetMDSS : SweetDisplayBackend : IOService ◄──────┘
   ├─ IOWorkLoop + IOCommandGate：串行化所有探针/DCS访问
   ├─ IOTimerEventSource：30秒后探针3次，间隔1秒
   └─ IOMemoryMap[9]：由provider的reg资源取得
```

后端目前只提供亮度就绪查询、`setBrightness()`和`dcsWrite()`；没有全局service指针、
`fromPath()`临时穿透或BSD转发桥。AppleCLCD本轮只按选定路径创建背光consumer，
还没有直接持有硬件后端；其用户客户端分发表/参数校验、状态gate和后端绑定属于F1b。
F1a通过不代表F1整体完成。面板电源F2、硬件vsync F3、自有扫描缓冲F4均未实现于本轮。
0173/0174实验继续独立且默认关闭，不把它们变成新路径的隐含前置。

## 资源与硬件行为

`sweetmdss`默认0，使用旧BSD实现。`sweetmdss=1`时，旧探针不创建线程，旧映射入口直接
拒绝，旧背光ready/set也不能绕过该保护。服务失败不转回BSD写入，避免两套所有者并存。

ADT派生工具为`tools/mk-mh2lm-mdss-adt.py`，只追加mdss和一个低地址翻译窗口，
不重新生成chosen、random-seed、内存、画布或设备身份，不覆盖原ADT。
通用`mkadt.py`尚无MH2LM设备配置，本轮不通过它重建已验证的MH2LM树。

| reg索引 | 窗口 | 物理地址 / 长度 |
|---|---|---|
| 0 | GCC display AHB页 | 0x0010b000 / 0x1000 |
| 1 | DISPCC | 0x0af00000 / 0x3000 |
| 2 | VIG0 SSPP | 0x0ae05000 / 0x1000 |
| 3 | CTL0 | 0x0ae02000 / 0x1000 |
| 4 | INTF1 | 0x0ae6b800 / 0x800 |
| 5 | INTF2 | 0x0ae6c000 / 0x800 |
| 6 | DSI0 | 0x0ae94000 / 0x400 |
| 7 | SMMU GR0/GR1，可选证据 | 0x15000000 / 0x2000 |
| 8 | SMMU CB2，可选证据 | 0x15082000 / 0x1000 |

GCC低于原SOC_BASE=0x01000000，不能直接做无符号相减。原ranges保持，另追加
child=0x100000000→parent=0、长度0x01000000；GCC reg使用0x10010b000。
服务通过`mapDeviceMemoryWithIndex()`映射并检查实际PA和长度，错误地址在读寄存器前失败。
7/8映射缺失不禁用DCS；CB2仍须trace≥2且布局/route匹配才读取。

候选INTID115仅存`sweet,mdss-intid`元数据；F3核对硬件和中断controller连接前，不发布
标准`interrupts`属性，避免触发未完成的IOKit中断解析。这轮不注册/使能MDSS IRQ。

探针保持GCC enable→DISPCC clock-off→MDSS顺序。DCS保持原0x51包、0→1、最大0xfff、
暂停TE/autorefresh、等待帧/DSI idle、FIFO/DMA提交、W1C/复位、恢复寄存器的顺序与超时。
连续3次实际引擎失败停写；成功清计数。软件vsync、扫描地址、裁剪、copy、关屏策略不变。
新代码日志使用ERROR/WARN/INFO，ERROR始终输出；旧路径日志分级改造不扩入此轮。

## 绑定、并发与停止

- 匹配回调只retain后端、更新consumer指针并发软件事件；无探针/DCS操作。
- terminated只清指针、release并发事件，不调用后端方法或读取其成员。
- `copyBackend()`在consumer锁内retain，随即解锁；实际硬件方法在后端gate中检查
  admission及inactive。不会拿consumer锁等待provider gate。
- 后端不回调consumer，避免“consumer loop→provider gate→consumer loop”的锁环。
- 背光请求早于匹配时保留最新目标，由匹配事件唤起一次，无新增就绪轮询。
- 背光stop先用请求锁关闭入口，再于gate外remove通知、同步移除事件，最后取消/移除timer；
  helper、锁和timer对象留到free，停止后新请求返回NotReady。
- 后端stop经gate等待当前DCS/探针退出，再关闭admission和取消探针；gate/maps留到free，
  让已retain但尚未进入gate的调用能安全返回NotReady，不访问失效映射。

## 本地验证与限制

0175对0121a4实际源码严格应用，六文件逐字节一致。使用真实CI导出头文件和编译参数，
修改后的Framebuffer、BSD探针、KernelConfigTables及新增SweetMDSS均生成arm64对象。
既有文件编译前后无新增告警类别；不是零告警。Framebuffer 1331→1339，增加8条头文件
保留标识符告警；原format告警仍2条。新服务44条unsafe-buffer-usage及SDK/IOKit头文件
告警类别都在已有基线中，没有通过禁用告警掩盖错误。

16项host检查通过（含现有14项），涉及的C/C++ fixture用ASan/UBSan运行。
新测试直接提取实际服务/client/背光代码，比较新旧DCS完整MMIO访问轨迹，覆盖正常、
初始状态错误、line/idle超时、DMA超时和DMA错误及恢复；另测时钟保护、3次失败停写、
PA错误/缺资源清理、匹配/terminated、亮度早于匹配、旧路径保留及写入并发stop。
fixture最终对象/映射计数归零。LSan受现有环境限制，使用detect_leaks=0，不声称LSan通过。
ADT测试核对9个窗口翻译、原树逐字节保留、低GCC窗口、无重复display child和无提前IRQ接线。

打包脚本`bash -n`通过，旧0121a4内核被新service marker检查拒绝。
未修改CircleCI配置；其旧源码gate保留，新marker在打包入口检查，云产物仍需逐文件核对。
完整云编译、真实IORegistry、硬件亮度和设备回归均待验证；host测试不能代替这些结果。
底稿：`scratch/framebuffer-0175/{verification.json,warning-counts.json,tests.json,*.compile.txt,test-*.txt}`。

## 云产物下载后的测试

先核对0175六文件、manifest和内核marker，再打包：

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-service
```

生成`boot-16k-编号-fb-service-{baseline,mdss}.img`。共用派生ADT/内核/参数，
只差sweetmdss=0/1；均preserveoff=1、trace/proof/contract=0、probe=1、loglevel=2，
三键/USB/触摸IRQ和电池runtime3参数保留。不是重测0173/0174视觉实验。

1. 启动后等待至少35秒，覆盖三次探针；核对新档资源归属、旧探针禁用及背光匹配标记。
2. 亮度调低→调高→调低，观察是否及时生效；核对DCS rc，无新ERROR/WARN。
3. 手动息屏/按键唤醒、息屏插拔各2–3次，回归三键、触摸、USB ACM和充电状态。
   缺时间/半透明异常作为基线记录是否变化，不要求其消失，不测自动息屏。
4. 两档分别导出`sweetlog-编号-fb-service-{baseline,mdss}.bin`；USB重连失败可用持久日志。
   若发生BUG-002隔离，标明其后的电池/按键测试无效，不混判为显示迁移失败。

F1a收口后进入F1b，再进入F2；不把本地编译成功记作设备验收。

## 0121a5 产物与镜像记录

预期实现提交`a22e1b3`。43个源码文件逐字节匹配（含0175六文件）、114个原CI源码marker、
新SweetMDSS/背光匹配/旧探针关闭内核marker及kernel manifest核验通过。
kernel SHA256：`9cc2e35f4eb0f7b8c742557a9c2f4c69e5e4c0d7adb4e35787420e955cfbc9c3`。
本地无本轮构建日志或CI run ID，未检查全量编译告警；尚无实机验收证据。

两档均在`/home/shenli/work1/out-bs/`，35,667,968字节：

| 镜像 | SHA256 |
|---|---|
| `boot-16k-0121a5-fb-service-baseline.img` | `0b62ea58d19d86172ac3846e277ce31825bc0c59def06ab1b1af921ef1e6baf5` |
| `boot-16k-0121a5-fb-service-mdss.img` | `216c11874f17febeb1763f447f1c8bcd4dc1d384b9e14924821022985f4856c0` |

从最终boot回读Android v1 header/FAT，kernel blob/ramdisk/XNU/ADT/TOML逐字节核验通过，
参数无重复、两档只差sweetmdss=0/1，公共参数符合上节。派生ADT SHA256：
`590ce846d0c6526165748f40f0e5770a08cec7df74973a75c2da67025e36e014`；原始ADT未覆盖，SHA256仍为
`f2fddbdac483347011deb64ff75e77d23a34a700d264e0505e765df1ffeea2e9`。
底稿在`scratch/artifact-0121a5/{verification.json,source-compare.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}`。

先用baseline，再用mdss，各按上节等待35秒并测亮度/输入/手动息屏唤醒/充电，分别导出
`sweetlog-0121a5-fb-service-baseline.bin`、`sweetlog-0121a5-fb-service-mdss.bin`。
本轮未改内核或CI、未push、未执行设备boot/刷写；无需新云编译即可开始测试。

0121a5两档持久日志已分析：mdss资源/绑定/探针和8次记录的DCS写入成功；baseline确认
旧路径探针/DCS。两档均复现相同UV→USB校验失效→停充/启充链，BUG-004独立跟踪；
新MDSS后端非必要触发条件。本组对照结束，不重复或为此新编译；完整亮度/生命周期验收
仍未收口，显示继续原F1a/F1b计划，不重开缺时间专项。详见[实测诊断](FRAMEBUFFER-SERVICE-0121A5.md)。

## 0121a6 重新构建与F1a补充验收镜像

2026-11-09收到`xnu-kernel-0121a6.gz`、`xnu-buildtree-0121a6.gz`，无本轮完整编译日志或
CI run ID。不能据产物推断零编译告警。43个预期源码文件（含0175六文件）逐字节匹配，
114个CI源码marker及已有内核marker通过，kernel manifest匹配。
kernel SHA256：`91ef77c4b7ce7b1c36f754ea4b26f6b52fc28a5940709f6c877229eed2dbc918`。

额外逐文件比较两个buildtree中的全部`xnu/`普通文件：0121a5与0121a6均5,261个文件，
无新增、删除或内容变化。这是同一源码的重新构建，二进制SHA不同不代表新增行为；没有
新增BUG-004修复或F1b实现，不要求再次做已完成的充电A/B对照。

两档均35,667,968字节，最终boot回读header/FAT、kernel blob、ramdisk、XNU、派生ADT、
TOML均匹配；参数无重复、两档仅sweetmdss=0/1差异，原ADT保持不变。

| 镜像（work1/out-bs） | SHA256 |
|---|---|
| `boot-16k-0121a6-fb-service-baseline.img` | `db5658bad52c40dae92c5d52a712f86c9da036f5ac29570f30cc9d4e69f52cc7` |
| `boot-16k-0121a6-fb-service-mdss.img` | `f14370a039e7a371a4bfef33b69650df47f4558cfa2578507255ea7b5a080d66` |

底稿`scratch/artifact-0121a6/{verification.json,source-compare.json,
full-xnu-source-comparison.json,boot-verification.json,verify-artifacts.py,verify-boots.py,
packaging.txt,mdss.adt}`。IRQ按键/触摸/USB ACM及电池runtime3参数均保留。

本次只需MDSS档补查尚未充分记录的F1a功能：启动等待35秒，亮度低→中→高→低，确认
实际屏幕亮度跟随且无卡顿/闪屏；手动电源键息屏→等待3秒→唤醒，连续3次，确认亮度
能够恢复且触摸/按键可用。不要求自动息屏，也不重开缺时间或充电闪烁专项。
导出`sweetlog-0121a6-fb-service-mdss.bin`并记录肉眼亮度/唤醒表现。
baseline已生成供保留，不安排重复刷测；已知BUG-004仍未修复，不拿图标闪烁单独判显示退化。
本轮仅核对/打包/文档，无驱动或CI改动、未push、未执行设备boot/刷写；F1a仍待上机验收。

## 0121a6 实测：F1a正常运行路径功能验收通过

用户明确反馈“亮度跟随正常，息屏亮屏也正常”。持久日志
`work1/logs/sweetlog-0121a6-fb-service-mdss.bin`，SHA256：
`2b67f0b4facc4067c5ad1756dd74414571612aa662a1f70f543f9dc4a9f41095`。
4,198,400字节；ring4,194,304，有效321,588字节，flush22，未回卷、有效区无NUL，解码4,641行。
第363行boot-args确认sweetmdss=1、probe1、trace/proof/contract0、runtime3、IRQ4。
底稿`scratch/artifact-0121a6/{fb-service-mdss.txt,mdss-log-analysis.json,analyze-mdss.py}`。

| 核对项 | 日志证据（解码行号） | 结果 |
|---|---|---|
| 新服务资源及绑定 | 78–86行9个窗口mapped1，87行启动，92行背光registry match | 新后端实际工作 |
| 旧路径禁用 | 226行legacy probe disabled | 无BSD探针并行写入 |
| 三次探针 | 174/383/447行；GCC0x80000003、DISPCC0x1、VIG0地址0x9d200000/stride4320 | 与已验证配置一致 |
| 亮度请求与DCS | 29条CoreBrightness请求，日志值406～2770；2418及3395–3408行8条DCS rc0 | 用户确认实际亮度跟随正常；无显示WARN/ERROR或已记录DCS失败 |
| 手动息屏/唤醒 | off3576→on3609、off3724→on3758、off3960→on3991；另有启动on2342 | 3轮完成，用户确认正常 |
| 输入并行 | 30条HID键事件覆盖电源/音量增减，61条HID触摸事件 | 事件路径保持工作 |
| USB ACM | 1302/2301行两次配置，2次reset，3条ep0/ep5命令WARN | 保留既有告警，不据此认定主机枚举故障或已修复 |
| 电池/mux | 13份采样、1次启充，无USB VERIFY(-12)、遥测停充、mux隔离/SPMI timeout/panic | 本轮未见BUG-002/004故障链，不宣称修复 |

**验收边界：** F1a的资源归属、服务匹配、DCS背光和正常息屏/唤醒功能迁移可收口。
8条DCS成功日志是打印预算，不是总写次数；用户观察不能替代逐写硬件trace。
本轮亮度日志未覆盖精确0/4095端点，不虚报全寄存器数值范围硬件验收；真实provider终止/
重新匹配、stop/free仍只有本地fixture证据，不用正常息屏冒充IOService生命周期测试。
无需再为这些边界重复当前测试或重编译，后续相关行为修改时有针对性覆盖。
BUG-001/002/003/004状态不变，半透明颜色异常保留；F1整体、F2真面板电源、F3硬件vsync、
F4自有扫描缓冲均未因此验收。sweetmdss默认仍0，后续实验显式打开。

**下一步F1b：** AppleCLCD直接以匹配通知取得typed backend，用户客户端改为
IOExternalMethodDispatch并按本版ABI校验参数，状态统一由workloop/IOCommandGate串行化。
保持当前画面/亮度硬件行为，不把模式语义、真关屏或硬件vsync提前混入；新增行为默认关闭。
本轮仅分析/文档，没有新增内核补丁、CI修改或push。
