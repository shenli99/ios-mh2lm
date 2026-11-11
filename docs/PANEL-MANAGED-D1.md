# D1：实际扫描接管中的资源获取（0188）

2026-11-11，接0187。这段把LDO、ARC、NoC和本地clock事务接到真实`beginScanout()`续程，
不再只提供尚未调用的接口。**本地验证通过，未实机；仅温态接管，完整D1及面板驱动未完成。**
默认关闭，暂无新boot档，不单独要求本段云编译/刷机。仍优先完整驱动，之后进入零拷贝。

## 接入位置与结构

```text
AppleCLCD / SweetFBScanout
 └─ typed beginScanout(pool, channel) / scanoutStatus
     └─ SweetMDSS : IOService（唯一硬件owner、原gate与scanout timer）
         ├─ 现有扫描接管与IRQ / TE / autorefresh暂停
         ├─ SweetMDSSResources.inc：温态资源协调器、阶段账本
         │   ├─ retained SweetRPMh / 原consumer ID
         │   │   ├─ 四路LDO：requestVote / copyStatus
         │   │   └─ ARC：requestDisplayPower / copyDisplayPowerStatus
         │   ├─ NoC模块：retainBandwidth / bandwidthStatus
         │   │   ├─ RPMh apps CN0
         │   │   └─ MDSS RSC时钟lease + display数据BCM
         │   └─ clock模块：retainLocalClocks(DSI0) / localClockStatus
         └─ 原自有A提交、硬件生命周期pin及fresh PP DONE / RD_PTR完成
```

没有新增映射、第二个TCS writer、全局实例桥或供电寄存器直写。
协调器沿现有MDSS gate/scanout timer推进，不新增线程/等待队列，不反向同步调用FB gate。
匹配/启动回调不投票；真正收到扫描接管请求后才进入资源获取。

开启`sweetdisplaymanaged=1`要求own2、power1、IRQ2、resource2、clock1及NoC ABI。
不匹配的组合明确拒绝startup；默认0保持原行为。NoC ADT仍用`--display-noc`生成，
没有改变既有default/IRQ/v1生成结果。参数不是冷启动授权，也不允许未知vendor表或reset。

## 实际执行与完成条件

1. 在暂停扫描前，确认provider/consumer、四路LDO约束、Turbo ARC链、六个BCM元数据及
   带宽可表示性、本地boot clock lease。缺失或溢出打印具体preflight错误，不发送资源票。
2. 沿原接管暂停TE/autorefresh，观察两次非有效扫描行，再暂停RD_PTR IRQ。
   增加`SweetScanoutResources`状态，FB继续通过原typed查询等待，不需要新consumer API。
3. 依次获取VDDI、VPNL、host、PHY。每路等待真实provider Complete；VDDI ACK后20ms，
   VPNL ACK后5ms，均由timer等待。记录已确认rail位图，不靠寄存器写读回宣称成功。
4. 请求ARC Turbo并等待整链完成，然后获取NoC带宽lease，最后获取独立DSI0 clock lease。
   NoC内部仍先CN0、实际RSC clock、display BCM，遵守0187的VCD完成和访问准入。
5. 全链完成后再复核继承pipeline、实际idle及本地clock条件；恢复IRQ，重新观察空闲，
   才沿原代码pin自有A、提交源地址并恢复TE/autorefresh。资源ACK不替代新传输的完成：
   仍要求fresh PP DONE、flush清除、RD_PTR前进才报告ScanoutActive。

资源状态为Idle、Rails、ARC、NoC、Clock、Ready、Fault、Stopped。
Idle保留尚未接管的原温态DCS行为；进入获取阶段后，亮度/DCS/DSI序列/面板电源命令
明确返回NotReady，防止在paused期间重入；Ready才恢复这些入口。忙时请求不会被当作成功。
IORegistry发布`SweetDisplayManagedState`及故障标记，日志沿用ERROR/WARN/INFO等级。

## 取值依据与保留范围

四路LDO沿[0184约束](PANEL-POWER-D1.md)：VDDI1800mV/62000µA、VPNL3000mV/857000µA、
host1200mV/21800µA、PHY880mV/36000µA。全部使用typed rail API，不穿透SPMI或TCS。
VDDI等待来自LG panel供电表，5ms来自`dsi_panel_pre_prepare()`；这里保持面板reset/state，
没有借这些等待执行冷启动GPIO或厂商命令。

温态接管不重配继承频率，暂用已有typed Turbo上限保护核心供电，保留至将来明确的rate/domain
owner可以安全降档。它不是频率测量、动态调压策略或正常运行的最低功耗配置。
带宽取LG `sm8150-sde.dtsi` enabled RSC usecase：两个MNOC端口、LLCC和EBI分别
IB=6,400,000,000 bytes/s，AB=0；配置总线取其最高300,000,000 bytes/s usecase。
这不是由屏幕像素率猜的票。若真实command DB不能在14bit中表示，则在首次资源写之前拒绝，
不套Linux的饱和截断，也不暗中选一个更小票冒充满足请求。

参考LG `android_kernel_lge_sm8150` HEAD
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`，`sm8150-sde.dtsi:267–345`、
`dispcc-sm8150.c:793–833`、`dsi_panel.c`及`dsi_pwr.c`。普通cold enable顺序不由本温态
续程替代：当前显示域和非XO父源仍继承，尚未实际cold配置solver/QoS/PHY/PLL/host。
clock状态保留`inheritedUpstream=true`表示这些依赖未完整接管；日志/ownership属性不再把
已经获得的LDO/ARC/NoC票一概称作继承。

## 失败、停止与释放

整链4s上限，各资源仍有自己的更短超时。初始quiesce和最终新传输继续用原50/100ms预算，
资源获取不挤占原等待预算。没有gate内sleep或长忙等。

中途失败不提交新的扫描地址，不恢复旧TE/autorefresh，不假设“未改源地址就完全没改硬件”。
可能已有LDO/ARC/CN0效果，故保留确认票/在途状态，停止本MDSS的NoC/clock在途推进，
关闭显示写入口并记录Fault及具体阶段错误。已经送到RPMh的事务可能继续完成，其票保留；
不在不确定时自动反向撤电。成功接管后的provider消失也会关闭显示写与扫描状态，保留DMA pin。
无自动重试、旧路径回退或丢账本后重新绑定并假装Ready。

正常stop记Stopped/Aborted，不将正常停止打印为本协调器硬件ERROR；撤销事件后晚回调不访问
MMIO。终止保留票，detach Busy沿原规则显式报告。已提交扫描池继续pin到重启，未提交池仍可释放。

**普通DCS Display Off不撤供电/带宽。** 它不是Sleep In、域塌陷或PHY关闭证据。
本段尚未完成完整unprepare/资源反向释放；须先补实际冷GDSC、父源、host/PHY/PLL停止与重建，
才能撤掉最后的依赖票。该临时高档保留只存在显式managed实验模式，默认模式不新增票。

## 验证和下一步

0188严格应用到0187树，无offset/fuzz，7份变化源码逐字节一致，114条原CI marker保留。
真实XNU/KDK头的MDSS与FB基线/最终arm64对象通过；MDSS新增7条固定有界数组提示，
FB告警不增。核对rail枚举0..3及元数据0..5边界，不声称零warning。

新增实际源码ASan/UBSan测试从真实`beginScanout()`进入，使用真实IRQ暂停/恢复、RPMh/
NoC/clock引擎，只替换OS调度/MMIO完成输入；覆盖未ACK不得提交、20ms等待、投票顺序、
Ready后源pin/fresh完成、DCS OFF/ON不撤票、5类错误startup组合、metadata预检失败、
各资源阶段失败/4s期限/中途stop/provider丢失、保留已确认票且不隐式恢复，以及对象清零。
连同NoC/clock/display/FB/DSI/RPMh/perf及ADT共9套检查通过，UBSan halt_on_error、
无sanitizer报告；LSan ptrace限制仍关，另断言对象/映射/通知/分配归零。

下一段补冷GDSC和时钟父源/频率控制，再接PHY/PLL/host、已确认profile/reset/完整ONOFF，
最终完成独立重建和ABL退出。此时不把资源获取首段当成完整面板驱动或零拷贝。
底稿`scratch/framebuffer-0188/`，补丁`0188-mh2lm-managed-display-resources.patch`，SHA256：
`9c8552e22ed68a0a6944fee7c6d7ea5a02f219c96587b195d6407df3800b7ea8`。
没有新实机/云构建证据；未push/触发CI/操作设备，CircleCI未修改。
