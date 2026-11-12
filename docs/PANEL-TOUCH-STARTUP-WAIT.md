# 首次面板接管与触摸异步初始化竞态（0206）

**0121a19已准备：** 实际78源码、114 source/160 binary markers及镜像回读通过。
新retained镜像同ABI608/605字节参数，待实机waiting→ready→managed/接管；旧0121a18不复测。
见[0121a19命令与日志目标](PANEL-D1-0121A19.md)。本次仅打包，不为文档重新编译。

2026-11-12，接[0121a18镜像](PANEL-D1-0121A18.md)。用户仍见早期日志静帧。
ACV metadata阻塞已解除；本次修复首次接管过早请求触摸worker、一次拒绝后永久冻结的启动竞态。
本地复现及修复验证通过，尚待新内核实机确认，不能称面板已完成接管。

## 实机证据与推断边界

`logs/live-0121a18-abi608-panel-retained.txt`，237460bytes/3311行，SHA256：
`c7077d1e0d78526591716db9dfe344f4b19e0b99027e3d5ee003fef7c9ee3446`。

- 813–818行六项BCM metadata全部通过；ACV保留unit1/width0/VCD11，enable-mask8。
  0205真实解除元数据准入故障，但尚无本次managed ACV命令已发送/ACK的实机证明。
- 819行 `SWEET FB HANDOFF: FAILED begin rc=0xe00002d8`，即NotReady。
  没有managedPreflight的任何失败ERROR，也没有生命周期begin/managed paused日志。
  按实际源码，BCM后还有带宽/clock/core/host预检；这些失败都有单独ERROR，随后才进入beginLifecycle。
  因此错误已推进到生命周期初始准入，并非之前ACV gate仍失败。
- 1601–1638行才出现TouchComm identify、app info、ready、IRQ probe/arm。
  原worker只有IRQ arm成功后才release-store `ts_panel_live=1`。
  native `SweetTouchPanel::registerService()`可以更早完成匹配，不等于该worker就绪。
- framebuffer在 `SweetFBScanout.inc::scanoutStep()` 设置Started后仅调用一次beginScanout；
  一切非Success均设永久Failed。触摸后来就绪也不会重试，画面保持早期静帧。
- SpringBoard已启动，按键/触摸/USB后段有事件，两个完整RD_PTR窗口59877/59867mHz，
  无kernel panic签名。3265行后续息屏请求被生命周期拒绝，是接管未完成的后续表现。

旧beginLifecycle未逐分支打印拒绝原因，不能用当前日志排除同时存在的participant绑定问题，
也不能把泛化CB2 FSR0x400审计WARN当作本次直接原因。**已直接复现的是：** provider已匹配、
worker尚未online时，0121a18原代码立即拒绝；真实framebuffer不会像旧测试那样再次调用begin。
0206同时为participant和worker读取/运行期准入失败补一次明确ERROR，以区分剩余问题。
不清SMMU fault、不绕过保护、不恢复ABL呈现来掩盖错误。

## 修复及边界

交付 `xnu/patches/0206-mh2lm-panel-touch-startup-wait.patch`，仅修改
`SweetMDSSLifecycle.inc`，不改ACV/USB/电池/按键/触摸worker或CircleCI。

首次Takeover收到typed worker available=false时，仍接受一次生命周期token并持有CPU扫描pool；
停留 `SweetPanelTouchPause`、touchToken=0，向framebuffer返回Coordinating。
已有IOKit workloop/timer每50ms读取provider的软件快照，最多等30秒；没有硬件轮询、忙等或sleep。
等待期不发quiesce、不申请LDO/ARC/NoC票、不改TE/autorefresh/scanout地址、pool不标硬件pin。
worker available后只提交一次typed quiesce，取得touchToken才启动原16秒生命周期/6.5秒暂停期限；
得到真实quiesced证明后，回到原scanout continuation及完整managed事务。

现有已就绪路径不增加等待；普通息屏/唤醒仍要求既有worker与fence，不能靠此机制恢复运行期故障。
provider退出、取消、stop沿用原token失效/资源保留处理；没有touchToken时不取消或poison触摸worker。
30秒未就绪则明确 `touch worker startup deadline` 失败，后来上线不能复活已结束事务。
worker查询错误/取quiesce失败立即失败，不反复提交quiesce；后续硬件准入保持原检查。
新增INFO分别记录waiting和worker-ready两次状态转换，没有每50ms日志。

## 验证与下一步

新增真实TCM worker/provider/MDSS联合回归：单次begin、worker晚8秒online（超过旧6.5秒暂停预算），
同一生命周期token继续到扫描提交/fresh像素/触摸恢复；等待阶段MMIO trace为空。
检查只发一次quiesce、正常期限重新起算，以及永不online超时、取消、stop、touch/RPMh退出后的
迟到online无新操作/无DMA提交。旧0121a18源码在新增case的首次begin成功断言处失败；修复后通过。
此为host模型复现，不声称已在设备上读取到worker准入分支的专门诊断。

15套ASan/UBSan相关回归全部通过（含真实framebuffer、TCM、profile、DATA/link/supply生命周期）；
retained mode2四个面板profile也各以迟8秒worker、单次接管完成后继续三轮正常OFF/ON。
MDSS修改前后真实XNU/KDK ARM64编译成功，warning1538→1538，无新增。
0185–0206累计严格应用无offset/fuzz、78源码字节一致、114现有CI源码marker通过；
CircleCI blob仍 `da0b6c0947a66f03491b3a9023590b49665292df`。
底稿：`scratch/framebuffer-0206/`，实机日志分析底稿 `scratch/artifact-0121a18/live-retained-log-analysis.json`。
需要用户push本地0206提交并云编译，下载新kernel/buildtree后同ABI608/605参数打新retained档。
下一轮先取启动日志，看waiting→ready→pause→managed及首次失败；不重测0121a18，不测combined/supply。

## 伴随观察

本轮10份FG profile记录全部match0/integrity0/nominal3510，prefix与先前Tocad不符；
FG capacity有效性也为0。与本次显示修改没有已证实因果，不据此修改电池或声称真实充电正常。
另有4条USB endpoint WARN，ACM仍传日志；不据此关闭或新增BUG-001枚举故障结论。
保留这些观察，不为当前面板修复增加电池/USB专项测试。

## 为什么面板接管会依赖触摸（用户追问核对）

这次已复现的是**软件初始化顺序与生命周期依赖**。MDSS协调器要求先取得触摸quiesced fence，
再改变显示链路；但IOService匹配成功与C TouchComm worker初始化/IRQ ready是两个时刻。
把服务可用误当成后台线程就绪，且首次begin拒绝后不再继续，才形成永久静帧。
不是已证实同一硬件寄存器被两个驱动同时写坏。

| 项目 | 显示/面板 | 触摸 |
|---|---|---|
| 数据链路 | MIPI DSI，经MDSS host/PHY | TouchComm，经GPIO55/56软件I²C |
| reset | GPIO6，SweetPanelPins所有者 | GPIO54，原触摸worker所有者 |
| 同步/输入IRQ | MDSS INTID115；面板TE GPIO8 | ATTN GPIO122，经现有TLMM汇聚INTID240 |
| 生命周期关系 | 协调器等待触摸暂停/恢复fence | 关闭IRQ、收尾消息、释放HID触点后才确认暂停 |

TLMM触摸驱动映射整tile，PanelPins只映射两页，地址窗口存在包含关系；当前引脚写入所有权不重叠。
两套reset引脚独立不能证明电源或模块电气影响完全独立；共享供电/面板reset对触摸影响仍缺实机证据。
首次Takeover前暂停触摸是当前统一协调器的保守策略，**不是已证明面板硬件上电必须等触摸初始化**。
它让首次接管与后续reset/OFFON遵守相同事务入口，不在切换中让旧触点/在途报文越过状态边界。

LG/Lineage也有显示到触摸的通知链：`touch_core.c:964–981`将panel BLANK转换为LCD_EVENT_LCD_MODE、
RESET转NOTIFY_TOUCH_RESET；本机`synaptics/s3618/touch_s3618.c:3829`的RESET分支为空，
`:3831`的LCD_MODE分支实际进入s3618_lcd_mode/check_mode及异步fb_notify_work。
这支持显示/触摸应协调状态，不证明它们共用reset线，也不能把别的触摸型号动作套到本机。
0206保留typed provider/原worker分工，仅让首次事务有界等待就绪，没有让MDSS直接操作触摸寄存器。
