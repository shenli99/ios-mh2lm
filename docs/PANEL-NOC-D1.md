# D1：显示 NoC 带宽事务与 RSC 所有权（0187）

**0205当前修复（2026-11-12）：** 按用户决定将display ACV建模为 `0x8` 开关资源；
保留真实width0/unit1/VCD11，EBI有需求发送X0/Y8，无需求撤票。普通BCM缩放/非零width检查保留。
本地验证通过，待push云编译及新retained镜像实测；不是再次只加诊断，也不代表面板接管已验收。
本地DTS无mask属性；LG apps有mask8/X0/Y8，本次显式应用到display，不搬跨BCM累计副作用。
详见[实现、依据、验证与下一步](PANEL-ACV-ENABLE-MASK.md)。
以下此前“不能套apps mask/继续查投票策略/无编译需求”等为历史判断，由本段取代。

**0121a17来源核对：** 已确认ACV width0来自固件commandDB aux；Lineage直接采用。
DTS qcom,buswidth4是独立节点字段，不能用来覆盖BCM width。
旧fixture/非零width统一假设需按实际资源语义修正，非改变真实元数据。
见[设备树、Lineage与字段布局证据](PANEL-BCM-WIDTH-SOURCES.md)。以下旧阶段原样保留。

**0121a16实测阻塞（2026-11-12）：** MDSS时钟/服务已通过，managed预检在第六项ACV
commandDB元数据失败（前五项通过），未开始本次managed投票。现日志不含失败字段，不能认定
ACV缺失或unit/width为零。LG display逻辑节点ACV_DISPLAY的真实DB key仍是ACV；LG允许aux长度
至少8字节，与当前恰好8字节检查存在差异，但尚无本机值支持放宽。0204只增加首次失败ERROR
及成功INFO的名称/gate/已读字段，不加读取或改变准入；需新编译取证。
详见[实测及LG核对](PANEL-D1-0121A16.md)。以下按原实现保留。

**0198后续增量（本地通过，未实机）：** mode5现已将数据票停驻与host/PHY/PLL关闭、
重建组合：OFF保config→放clock→关link→放DATA；ON取DATA→重建link→取clock→重建host→
原reset/ON/亮度/pixel/touch。普通暂停接口仍拒绝改票，仅协调器指定DATA请求获得有限授权；
不是rail-off或冷NoC/MDP。详见[组合事务和架构](PANEL-COMBINED-SUSPEND-D1.md)。
以下按各原阶段范围保留，当前以0198及NOTES顶部为准。

**0196后续增量（本地通过，未实机）：** 已接入mode3 managed数据票释放与重新取得，
保留独立config floor及所有供电/boot/link/core资源；原mode1/2范围保留。
详见[当前资源事务及边界](PANEL-RESOURCE-PARKING-D1.md)。以下按本文件原阶段范围保留。

0188补充：显式managed模式已有实际扫描接管调用这些事务，见[资源生命周期首段](PANEL-MANAGED-D1.md)。
下文按0187范围保留；单开resource/clock/NoC ABI仍不自动投票，managed1才在接管暂停后请求。

2026-11-11，接0186。新增实际 CN0 和 display BCM 投票状态机，本地编译与模拟 MMIO
验证通过，**未实机投票，D1及完整面板驱动尚未完成**。MDSS 面板启停生命周期尚未调用
这些接口，当前默认显示行为、30Hz CPU复制、自有A单缓冲/B未提交均未改变。

## 所有者与接口

```text
SweetMDSS : IOService / SweetDisplayBackend
 ├─ 唯一 GCC / DISPCC / GDSC / DSI / PHY 映射
 ├─ 原 workloop + IOCommandGate
 │   ├─ 本地 clock lease：Core / DSI0 / RSC
 │   └─ SweetMDSSNoC.inc：typed 带宽 lease / 聚合 / timer / 完成账本
 ├─ 唯一 display RSC / TCS / wrapper 映射和 writer
 │   └─ MM1 / MM0 / SH0 / MC0 / ACV_DISPLAY，ACTIVE2/3
 └─ SweetRPMhClient：真实匹配、持有 provider 引用
     └─ SweetRPMh : IOService / 独立 workloop + gate + timer
         ├─ command DB：VRM / ARC / BCM 分类型元数据
         └─ 唯一 apps-DRV writer，ACTIVE0/1
             ├─ 四路 LDO
             ├─ MMCX / MX / CX
             └─ CN0：显示配置总线带宽
```

display RSC 和 apps RSC 是不同的硬件投票端。display RSC 由实际拥有其时钟/域的
MDSS控制，避免RPMh反向同步调用MDSS并形成gate环。RPMh不保存consumer对象或回调它；
MDSS通过已有实例接口查询元数据、请求CN0和查询token，不使用全局C实例桥或ready布尔授权。

`SweetBCMInfo`仅向MDSS及RPMh内部开放地址/unit/width/VCD；普通consumer拿不到raw命令接口。
RPMh增加`copyBCMInfo / requestRegisterBandwidth / copyRegisterBandwidthStatus`，CN0与原
LDO/ARC共用一次一个事务的apps引擎。consumer间CN0取最高bytes/s，成功ACK后提交软件票；
有票/在途/不确定请求的detach返回Busy。空账本Off不撤销继承投票。

MDSS内部增加`retainBandwidth / releaseBandwidth / bandwidthStatus`，与clock模块一样
沿用自身gate。四个lease槽、单调token，旧token在槽复用后失效；查询只读软件状态。
Held表示本软件事务完成，不表示实测DDR吞吐量、物理供电状态或其他执行环境已经撤票。

## LG参考事实与编码

参考仓库`android_kernel_lge_sm8150`，HEAD
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`：

| 来源 | 核对结果 |
|---|---|
| `sm8150.dtsi:2597`、`:2613`，`qcom-rpmh-mailbox.c:1205` | apps DRV2先ACTIVE2；display DRV0按DTS顺序SLEEP1、WAKE1、ACTIVE2，因此display ACTIVE为槽2/3 |
| `sm8150-bus.dtsi` | CN0走apps；MM1/MM0/SH0/MC0/ACV_DISPLAY走rsc_disp，包含MNOC/LLCC/EBI各段 |
| `msm_bus_fabric_rpmh.c` | BCM aux为u32 unit、u16 width、u8 VCD、保留字节；命令COMMIT30/VALID29/X14..27/Y0..13 |
| `msm_bus_arb_rpmh.c` | 节点AB求和、IB取max，再按节点宽度/aggports/BCM width/unit缩放；每VCD末命令提交并请求完成 |
| `dispcc-sm8150.c:1408`、`:1480`、`:1498` | NON_GDSC_AHB +0x4004、RSCC_AHB +0x400c、RSCC_VSYNC +0x4008；前两者共享AHB root，VSYNC为XO |
| `sde_rsc.c:1122`、`:1835`，`sde_rsc_hw.c:646` | RSC访问前先域/时钟；CLK模式关闭solver，不能把映射成功当作可访问证明 |

BCM按需解析，不要求只做原LDO的固件fixture必须含BCM；显式新请求则必须完整。
要求slave5、地址高16位5且word对齐、名称/地址唯一、aux恰好8bytes且有界对齐、
unit非零、普通BCM width非零（0205对显式mask资源ACV保留width0）、VCD<16及保留字节为零。名称对应的硬件地址不硬编码。

**0205资源模型：** LG原apps/display分支差异仍是源码事实；本项目按用户决定将display ACV
显式改为mask8/X0/Y8，不再按普通带宽公式编码，具体依据及范围见本文顶部链接。

`SweetDisplayBandwidth`输入全部为bytes/s：两个MDP端口各AB/IB、LLCC AB/IB、EBI AB/IB，
另有registerPeak。每个输入AB<=IB；有效票要求有MDP端口峰值、LLCC/EBI峰值及至少
76,800,000 bytes/s配置票（LG配置总线19.2MHz×4bytes的下限）。固定槽聚合时AB求和、IB取max，
registerPeak取max；所有加法/乘法与14bit上限均在写硬件前检查。

| 资源 | 节点宽度/聚合端口 | X（平均） | Y（峰值） |
|---|---|---|---|
| CN0 / apps | 4 / 1 | 0 | registerPeak缩放 |
| MM1 / display | 每端口32 / 1 | 两端口分别缩放后取max | 同左 |
| MM0 / display | 32 / 2 | 两端口AB之和缩放，除聚合端口数 | 两端口IB取max缩放，不除端口数 |
| SH0 / display | 16 / 4 | LLCC AB缩放，除聚合端口数 | LLCC IB缩放，不除端口数 |
| MC0 / display | 4 / 4 | EBI AB缩放，除聚合端口数 | EBI IB缩放，不除端口数 |
| ACV_DISPLAY / display（0205） | 不参与带宽缩放，保留真实width0 | 0 | EBI AB或IB非零为8，否则0 |

普通BCM缩放为`ceil(rate × BCMwidth / (nodeWidth × ports × unit))`。正值向上舍入，超出14bit
显式拒绝，不静默截断或饱和。各段输入代表路径不同位置的带宽，不能把它们再合成一个MM0票。

## 准入、事务和撤回

获取按CN0配置票→真实RSC时钟lease→display数据票；降低CN0延迟到数据票完成后。
最后撤票按display数据Off→RSC时钟lease撤回并确认物理OFF→CN0降低/Off。
RSC clock token在数据撤票完成前禁止单独释放，配置票保留至关钟完成。
首次接管、全部Off后的重新获取，以及任何撤票，均由MDSS核对实际DMA/link空闲；
调用者不能提供布尔idle跳过检查。多个有效lease间增加请求可保留现有链路。

取得真实RSC时钟lease且确认分支运行/父源不漂移、温态GDSC条件满足后，才允许第一次读
display RSC。要求pre-v3主版本1/2、DRV0至少4槽、NCPT1..16；wrapper override+4 bit0
清零（solver关闭）、power+0x24 bit12清零、ctrl bit1清零，sequencer busy+0x404和errorIRQ+0xd0为0。
solver开启、冷域、时钟未知或冲突均显式失败，不编程solver，不走冷开启回退。

仅计划有变化的资源，按VCD排序。每VCD整个组必须装进一个TCS，超出NCPT直接拒绝，
不拆坏COMMIT边界。非末命令仅要求issued；末命令加COMMIT、response、Wait并要求complete。
完成还必须有idle及独立fresh IRQ或观察到执行状态变化，旧DONE不能单独完成新事务。

只使用真正空闲的ACTIVE2/3；不碰sleep0/wake1或覆盖继承payload。选槽后以及屏蔽本槽IRQ、
首payload写入前再次检查继承冲突。清trigger/mode/enable/wait、W1C本槽、恢复本槽IRQ，
保留无关位。每VCD清理后记录applied值和ACK位，只有整链成功才提交lease/聚合票。

写读回1ms、每VCD100ms、整链1.5s，由timer推进；gate内没有长时间忙等。任何失败保留
确认票与时钟，记录Failed/result/uncertain并停止新写入；不自动回滚、reset或静默退回旧路径。
provider丢失时关准入并保留状态；stop取消timer，晚事件不访问MMIO，不隐式撤票/关域。
全局仅原子writer状态字，无实例指针；可能发生过写的实例free后在本boot内retired，防止丢账本重建。

## ADT、运行边界与后续

`mk-mh2lm-mdss-adt.py --display-noc`隐含`--display-resources`，追加
`sweet,display-noc-abi=1`及MDSS索引13..15：

| 索引 | 物理窗口 | 唯一所有者 |
|---|---|---|
| 13 | display RSC `0x0af20000 / 0x1c00` | SweetMDSS |
| 14 | display TCS `0x0af21c00 / 0x3000` | SweetMDSS |
| 15 | display wrapper `0x0af30000 / 0x3fd4` | SweetMDSS |

头部/扩展窗口在TCS前截断，避免直接搬LG原drv长度0x1c44造成重叠。apps nub及原
default/IRQ/v1 ADT结果保持不变。新ABI要求`sweetdisplayresources=2`、`sweetdisplayclocks=1`；
仅映射不读RSC、不投票，开启参数也不自动获取带宽。没有新增boot档，暂不需要push/刷机。

**本段是温态ACTIVE带宽事务。** 未实现冷GDSC、solver/sequencer及sleep/wake初始化、
NoC QoS/SBM/端口冷配置、非XO时钟父源、PHY/PLL/host或面板完整初始化。
0185时钟状态仍如实标记上游继承；新增接口尚未让默认显示路径拥有上游资源。
下一步把LDO/ARC/NoC/本地clock接入实际MDSS资源生命周期，并补其冷域/时钟/链路控制，
再连接已确认profile的reset/ON/OFF。最终独立重建及退出ABL依赖的验收要求不变。

## 本地验证

0187严格应用到0186验证树，无offset/fuzz；6份源码字节匹配，114条原CI marker保留。
真实XNU/KDK头的RPMh与MDSS基线/最终arm64-apple-darwin对象均编译通过。
原生告警RPMh1298→1321、MDSS1324→1391，新增均为固定有界数组访问提示；
已核对元数据、枚举、四lease、五数据资源/排序及TCS操作队列边界，不声称零warning。

七套实际源码ASan/UBSan测试（NoC/clock/display/FB/DSI/RPMh/perf）及Python ADT检查通过。
新测试覆盖真实匹配、无请求不读RSC、缩放/聚合、VCD与非末命令完成、ACTIVE槽/无关IRQ位、
共享票/反向撤回/全部Off后重新获取的idle门槛、四槽/旧token/等价票不重复写、独立RSC
clock ON/OFF与关钟期间保留CN0、继承冲突及准入后新冲突、坏BCM/溢出、NCPT组放不下、
solver/时钟/域/修订/忙/错误IRQ拒绝、坏ABI/缺映射的startup清理、旧DONE不完成、丢写/清理/超时/停止晚事件。
旧clock测试撤票等待次数由写死7改为分支总数，覆盖新增3个RSC分支后的反向扫描。
UBSan halt_on_error，输出无sanitizer报告；LSan受ptrace环境限制关闭，另断言对象/映射/
分配/通知归零。模拟完成不是实机电压或吞吐量证据。

补丁`xnu/patches/0187-mh2lm-display-noc-votes.patch`，SHA256：
`14d2845e6bf429833888de20d3bd99dd14f5b3c4d3eb30485c836c4f3b827937`。
底稿`scratch/framebuffer-0187/`含严格重放、编译、告警与测试记录。
CircleCI blob保持`da0b6c0947a66f03491b3a9023590b49665292df`；未push/触发CI/操作设备，
未改USB、按键、触摸、电池和SPMI mux。
