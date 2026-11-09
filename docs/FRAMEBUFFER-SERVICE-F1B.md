# F1b：AppleCLCD 后端绑定、分发表与状态 gate（0176）

2026-11-09。接续已通过正常运行路径功能验收的F1a。状态：实现及本地验证完成，待用户
push、云编译及实机验证；尚无本补丁的云产物或boot，F1整体未验收。

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
本轮未push，未执行设备boot/刷写。真实服务终止和本段设备运行尚未验收。
