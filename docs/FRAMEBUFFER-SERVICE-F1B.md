# F1b：AppleCLCD 后端绑定、分发表与状态 gate（0176）

2026-11-09。接续已通过正常运行路径功能验收的F1a。状态：0121a7正常运行路径实测验收通过，F1a/F1b分层重构正常使用范围收口；真实provider终止/重匹配仍未上机验证。

## 本轮架构

```text
ADT /arm-io/disp0
└─ AppleCLCD : IOMobileFramebuffer
   ├─ 私有 IOWorkLoop + IOCommandGate
   │  ├─ client数组 / swap ID / pending / dirty / power / trace状态
   │  ├─ IOTimerEventSource：原软件vsync和呈现节流
   │  └─ 软件匹配事件：更新后端绑定状态
   ├─ IOMobileFramebufferUserClient（每连接）
   │  ├─ IOExternalMethodDispatch：标准数量/大小检查
   │  ├─ StateCall / Add / Close / Port → AppleCLCD gate
   │  └─ 关闭标记 / 通知函数与端口 / 有界选择子日志
   ├─ SweetDisplayClient：本consumer的typed引用及匹配通知 ──┐
   └─ AppleARMBacklight + 独立SweetDisplayClient（0175） ───┤
ADT /arm-io/mdss                                           │
└─ SweetMDSS : SweetDisplayBackend : IOService ◄────────────┘
   └─ 自有workloop/gate、ADT映射、原探针/DCS引擎
```

AppleCLCD直接拥有registry consumer，无全局service单例或BSD穿透桥。匹配/终止仍由0175
helper retain/撤回typed引用并投递软件事件；AppleCLCD回调只更新绑定属性和日志，不做
硬件操作。绑定不代表硬件就绪；未来F2每次硬件调用须copyBackend并走provider admission。

`IOCommandGate::runAction`由调用线程获取workloop gate后同步执行，不代表所有方法都
搬到后台线程。timer和软件事件共享该gate，客户端调用、端口、关闭和帧状态相互排斥。
`fLock`留给旧路径及独立contract实验；新路径的stateLock/stateUnlock不重复拿它。

前端用真正的IOExternalMethodDispatch检查并调用，handler复用原legacy switch，维持
绘制/裁剪/pending覆盖/软件vsync/保留像素/电源行为。SwapWait仍沿用旧返回语义。
F2真关屏、F3硬件vsync、F4自有缓冲、模式属性及BUG-003不混入本段。

## 参数与兼容性

| selector | scalar输入/输出 | inline结构输入/输出 | 依据/行为 |
|---|---|---|---|
| 3 默认surface | 2 / 1 | 0 / 0 | 已有实机调用 |
| 4 SwapBegin | 0 / 1 | 0 / 0 | 已有实机调用；gate内分配ID |
| 5 SwapEnd | 0 / 0 | 0x46c / 0 | 本版框架及实机大小；拒绝descriptor/空输入 |
| 6 SwapWait | 3 / 0 | 0 / 0 | 本版框架及实机形状 |
| 7 GetID、18 IsMainDisplay | 输入可变 / 1 | 0 / 0 | 未取得严格输入形状证据，不猜测收紧 |
| 8 DisplaySize | 0 / 2 | 0 / 0 | 已有实机调用 |
| 12 PowerChange | 1 / 0 | 0 / 0 | 已有实机调用 |
| 28 DotPitch | 0 / 1 | 0 / 0 | 已有实机调用 |
| 72 通知启停 | 3或4 / 0 | 0 / 0 | 原实现读取前三项；type小于8 |

统一限制scalar不超过XNU的16项上限，非零数量/长度必须有对应指针或descriptor。
已实现接口保留inline/scalar ABI；标准基类检查通过后才进入gate，拒绝参数不会分配ID
或改变通知/电源状态。未知选择子继续成功、清零inline输出；未知descriptor不被读写，
保持旧行为。每连接最多记录32种选择子，每种一次，达到预算后停止打印。

## 停止与引用

1. provider stop先在gate内关闭入口、取消timer、关闭全部client并撤回通知端口。
2. gate外stop匹配helper以同步撤回回调，再移除timer，最后super::stop。
3. gate/workloop/扫描资源留到free。每个新路径client在start retain framebuffer、在free
   release，晚到调用可安全返回Offline。free也覆盖部分start失败。
4. client close/stop通过同一gate幂等清空通知并移出数组，provider stop也主动关闭数组中
   的client。替换端口释放旧引用，关闭后拒绝注册也释放传入引用。

terminated通知上下文不调用provider方法。真实provider终止/重匹配仍需有对应触发条件时
专测，正常息屏不是IOService stop。CPU整帧拷贝仍占用workloop gate，并发方法需等待；
实际延迟需实机观察，拷贝开销留给后续扫描缓冲阶段。

## 开关及测试入口

新增`sweetfbservice=1`，默认0；要求`sweetmdss=1 sweetfbcontract=0`。配置冲突或初始化
失败会明确ERROR并拒绝启动，不静默回退。`circleci.yml`保持不变。

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-state
```

生成`fb-state-baseline`（F1a、service0）和`fb-state-gated`（F1b、service1），均mdss1，
共用内核/派生ADT/其它参数，只差sweetfbservice。保留按键/触摸/USB IRQ、ACM、电池runtime3，
preserveoff1、trace/proof/contract0、probe1/loglevel2。新marker检查正确拒绝旧0121a6内核。

云产物下载后先核对0176两文件、manifest和内核marker，再打包并提供fastboot boot命令。
优先测试gated档：启动桌面、连续触摸滑动、三键/关机界面及取消、亮度调节、手动息屏唤醒、
USB ACM；出现新增异常再用同核baseline定位，不默认要求完整重测两档。
BUG-004充电A/B已结束，不重新专项；BUG-001/002/003/004及半透明基线保留。

## 本地证据及边界

- 0176严格应用到0121a6源码，两文件逐字节匹配；原114项CI源码marker仍通过。
  F1b新marker在打包入口检查，云产物仍须源码比对。
- base/edited/verify的Framebuffer使用已记录的真实CI组件参数和导出头生成arm64对象。
  Linux工具链保留既有兼容参数，不声称完全等同Xcode；没有增加整个源码根目录搜索路径。
- 告警类别无新增，format保持2条旧告警；unsafe-buffer-usage由87到92，新增项来自有界
  selector日志数组、dispatch表及参数索引；既有SDK告警保留，不声称零告警。
- 17项host检查最终均通过。初次3项受LSan/ptrace限制失败，detect_leaks=0重跑通过；
  ASan/UBSan仍启用，不声称LSan通过。
- 新测试执行完整实际Framebuffer/UserClient及新增inc。仅OS原语、独立contract实验和
  已有单独验证的背光/registry helper为fixture。覆盖部分start失败、参数拒绝无副作用、
  未知ABI、真实共享像素/电源handler、800次双线程ID分配与timer并行、端口/关闭/provider
  stop并发、晚到请求/启动拒绝；fixture对象引用及内存块计数归零。

底稿`work1/scratch/framebuffer-0176/{base,edited,verify,implement.py,make-patch.py,compile.py,
verification.json,warning-counts.json,tests.json,test-*.txt,old-kernel-rejection.txt}`。
本地验证时未push或执行设备boot/刷写；后续设备运行证据见下方0121a7实测。真实服务终止未上机验收。


## 0121a7 云产物及测试 boot（实测结果见下节）

预期实现提交`b1c7969`。44个已跟踪源码文件逐字节匹配，其中0176两文件匹配；
114个既有CI源码marker及新增F1b内核marker通过，kernel归档manifest核验通过。
未收到本轮全量构建日志或CI run ID，未检查全量编译告警。

- kernel SHA256：`92cb3d7df535f692ba5d5fc220068da8f50d4e3a1ca9ce974fc084dadac55c8b`，与0121a6不同。
- `out-bs/boot-16k-0121a7-fb-state-baseline.img`：35,667,968字节，SHA256
  `ac15b4a0dedfb247167c6df1bf4bb19c391c7ece4eacf6c9a882be53170ee080`。
- `out-bs/boot-16k-0121a7-fb-state-gated.img`：35,667,968字节，SHA256
  `3cf98432131b221478c8031e28b80ae8d2678df0f90e3887ae8c2bea96e1fbc2`。

从最终Android boot回读header/FAT，kernel blob、ramdisk、XNU、派生ADT和TOML均匹配；
参数无重复，两档仅sweetfbservice0/1不同，均sweetmdss1，原ADT字节未改。
IRQ/ACM/runtime3及preserveoff1保留，trace/proof/contract0、probe1/loglevel2。
底稿`work1/scratch/artifact-0121a7/{verification.json,source-compare.json,boot-verification.json,
verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}`。

优先只测gated：启动等35秒，桌面连续触摸/滑动、三个实体键、关机界面/取消、亮度
低→高→低、手动息屏/唤醒3轮，确认USB ACM可用；记录新增冻结、明显迟缓或亮度/唤醒失败。
导出`sweetlog-0121a7-fb-state-gated.bin`。仅有新增异常时再用baseline定位。
本轮未改内核/CI、未push、未操作设备，无需为打包重新编译；不重开充电A/B或BUG-003专项。


## 0121a7 实测：F1b正常运行路径验收通过

用户完成测试并反馈“没注意到新问题”。持久日志
`work1/logs/sweetlog-0121a7-fb-state-gated.bin`，SHA256
`6cb1c9f504fb4c67543e565dbcaec0d079e7add1b168941873b388ff83b3a449`。
文件4,198,400字节，ring容量4,194,304字节，有效713,446字节，flush14，未回卷，
解码8,607行。底稿`work1/scratch/artifact-0121a7/{fb-state-gated.txt,
gated-log-analysis.json,analyze-gated.py,gated-selected.txt}`。

| 核对项 | 日志证据（解码行号） | 结论及边界 |
|---|---|---|
| 新路径启用 | 75 F1b dispatch/state gate启用；93 AppleCLCD后端match；365参数service1/mdss1/contract0 | 确认实际运行新路径 |
| 资源和后端 | 79–87九资源；88 SweetMDSS启动；94背光match；227旧探针关闭；176/385/587三次探针 | 硬件资源仍由服务管理 |
| 接口调用 | 2352 sel12、2355 sel6、2394 sel4、2395 sel5（inline1132字节=0x46c）、2396 sel3均rc0；8/28也成功 | 已记录调用兼容本版ABI；有界去重日志不代表记录了所有调用，未知selector的rc0不代表其功能完整 |
| 亮度 | 30条CoreBrightness请求973～2632；2425及3481～3494共8条DCS rc0 | 成功日志预算8，非总写次数；不声称覆盖硬件0/4095端点 |
| 息屏/亮屏 | off3754→on3781、off4817→on4955、off6527→on6555、off6733→on6762 | 4组完整记录；用户未察觉新增问题；这仍是旧像素/背光策略，不是真DCS关屏 |
| 输入/USB | 32条三键HID记录，三键down/up配对；73条触摸；1320/2312两次ACM配置 | 正常路径持续活动；6条既有USB endpoint WARN/6次reset不单独证明枚举失败 |
| 异常筛查 | 无分级ERROR、显示失败、mux隔离、SPMI timeout或kernel panic | 启动/用户态仍有既有非分级错误消息，不宣称整份日志零错误 |

**独立BUG-004再次出现：** 146份电池样本，30份USB字段VERIFY(-12)，30次
step2/reasons0x1遥测失效停充、32次step1启充，另有1次step2/reasons0x2；UV路由命中352次。
4000行id10 usb0x14/-12→4009行停充→4051行恢复启充，符合已登记故障链。
用户未察觉新问题不等于没有充电状态波动；不能把本轮充电记为完全无异常，也不能仅凭
本轮日志断定F1b负载对其完全无影响。此前legacy baseline已同链复现，继续独立跟踪。

验收限定：F1b正常使用路径，以及结合0121a6的F1a/F1b分层重构正常使用范围通过。
未上机触发provider真实终止/重匹配/stop-free，相关并发及引用清理目前只有host证据；
也未测量延迟分布或证明gate不影响性能。无需没有新增显示异常时再重复baseline。
新开关保持默认0；F0遗留BUG-003/半透明颜色、F2真显示电源、F3硬件vsync、F4扫描
缓冲及F5零拷贝均未完成。下一步按计划进入F2，先核对LG面板DCS28/29时序及provider
电源接口，由AppleCLCD通过typed backend请求，避免consumer直接写寄存器。
