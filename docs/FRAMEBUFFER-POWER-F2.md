# F2：面板 Display Off/On（0177，待云编译及实测）

2026-11-09。接续0121a7已验收的F1b正常使用路径。0177本地实现/验证完成，尚无本补丁
云产物或boot，不记F2验收通过。新增`sweetfbpower=1`，默认0，要求
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
回读最终boot后再提供确切`fastboot boot`命令；目前没有0177新boot。

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
