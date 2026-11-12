# D1 资源层：RPMh provider、显示投票与资源 ABI（0184）

**0199供电归属增量（2026-11-12，本地通过，未实机）：** 已确认DSI host L3/PHY L5
与MH2LM UFS及USB PHY共用；这些驱动尚未在SweetRPMh登记依赖，不能按显示最后一票关电。
provider新增typed `lastDisableRequiresHandoff`及mask0xc，在任何写入/token/账本修改前拒绝
这两路最后aggregate disable，保留原票和完成状态；非最后撤票及panel两路原接口保留。
21套检查与RPMh/MDSS/FB六个arm64单文件对象通过。没有新增档位或实际撤电，0198 mode5仍为
当前组合休眠路径。详见[供电图、源码依据、保护与剩余项](PANEL-SUPPLY-OWNERSHIP-D1.md)。
下一步保留L3/L5，补LG顺序的reset-low及panel VDDI/VPNL受控撤票/恢复；E7触摸与E8物理
共享/偏压尚未验收。无新FD/boot、push/CI/设备操作，CircleCI未改，暂不单开云编译。
以下0198及更早描述按当时范围保留；完整驱动仍未完成。

0187补充：原provider已有[MMCX/MX/CX事务](PANEL-ARC-D1.md)及apps CN0带宽；MDSS拥有
独立display RSC和[数据NoC事务](PANEL-NOC-D1.md)，实际获取RSC clock lease后才访问。
0188已在显式managed模式下接入实际扫描资源获取，见[生命周期首段](PANEL-MANAGED-D1.md)；
默认关闭，普通DCS OFF不撤票。下文0184的实机证据仍仅为四路LDO元数据绑定。


2026-11-10。本段接在0183命令执行器后，实现电源资源接口和实际TCS状态机；
**本地验证通过；0121a13实机资源元数据/匹配通过，尚无实机投票证据，D1整体尚未完成。**
没有改变30Hz复制、没有修复0121a12滑动卡顿，也没有开始面板reset/厂商表初始化。
不单独安排这段元数据绑定的刷机轮次，继续合并时钟/链路实现。

## 已实现的结构

```text
SweetMDSS : SweetDisplayBackend / IOService
 ├─ 原MDP / IRQ / DSI命令模块
 ├─ 独占GCC页、完整DISPCC、DSI0、PHY0/PLL0映射
 └─ SweetRPMhClient（真实IORegistry匹配、终止通知、自己的软件IES）
     └─ retained SweetRPMh : IOService（sweet,rpmh平台nub）
         ├─ attachConsumer / detachConsumer：有界不复用ID
         ├─ resourceInfo / requestVote / copyStatus：typed rail、约束、token
         ├─ 多consumer投票账本 + 一次一个在途请求
         └─ 唯一apps-DRV writer / command DB / workloop+gate+timer
```

匹配回调只retain/撤回provider并排事件；显示自己的workloop执行attach与元数据查询。
终止通知不调provider方法；正常stop在owner gate外同步撤回通知和事件，再detach/release。
provider不保存或回调consumer指针，不持有consumer gate；consumer持有provider引用。
`SweetDisplayRPMhBound`只表示绑定/元数据可用，**不是电源、时钟或面板已经就绪**。
当前绑定不发任何电源投票；后续面板生命周期通过此实例请求，不加入全局C访问桥。

## 唯一TCS写入入口

0121a12实际云源码的`sweet_rpmh.c`已有command DB、pre-v3 TCS布局、继承请求保护及poison。
0184迁移这些机制到实例；原C实现仅在MH2LM排除，其他板保持原样。
核对真实云源码：MH2LM TouchComm没有调用该helper，`SWEET_DEV_TOUCH_VREG_NAME="none"`；
旧`sweet_touch.c`和`sweet_touch_sec.c`在MH2LM不启用，GPU调用只在GUACAMOLE编译。
因此这不是让显示与触摸分别持锁写同一DRV，也没有修改MH2LM触摸供电。

provider由ADT的RSC/TCS/locator三段资源映射，严格验证物理地址/长度、ABI、RSC主版本1/2、
DRV2几何和ncpt3..16；仅用两个ACTIVE TCS，不借用sleep/wake/control槽。
command DB地址仅来自固件locator，有界低32位、对齐、magic、slave/entry/aux范围和唯一
VRM地址检查；四个rail地址不能重叠。不扫RAM，不提供任意地址写接口。0184只实现VRM；0186增加独立typed ARC链，不开放raw selector。

选槽前检查所有ACTIVE中同resource的继承payload；idle但仍有enable/wait/control也不覆盖。
只屏蔽本槽IRQ位，payload按voltage→mode→enable写入。完成要求issued+complete、idle，
以及fresh IRQ或已观察执行转换；旧command status不作独立完成证据。
完成后清trigger→mode→enable/wait，W1C仅本槽IRQ，恢复时重新读取IRQ enable以保留其他位。

寄存器写回最长1ms、整个请求最长100ms，等待由100µs timer事件推进；gate内没有delay、
sleep或100ms忙等。固定有界写队列最多14项，完成清理6项。Busy直接返回，不隐式重试。
不确定写/超时关闭写入，不reset、不伪造完成。stop先关admission、撤销timer，再移除源；
晚到事件不再访问映射。TCS只读硬件内的命令寄存器，不持有主机DMA descriptor。

全局仅保留一个原子**所有权状态字**，没有实例指针/consumer查找。发生任何硬件写入后，
实例销毁将该boot标记retired，拒绝创建一个丢失投票账本的新实例；纯元数据实例可以重匹配。
这是故障恢复尚未实现时的显式限制，不宣称热重启provider或sleep/wake投票已实现。

## 投票与释放语义

| typed rail | command DB | provider范围mV | HPM阈值µA | panel/host候选申请 |
|---|---|---:|---:|---|
| VDDI | ldoa14 | 1800..1880 | 10000 | 1800mV / 62000µA；后续生命周期另等20ms |
| VPNL | ldoa17 | 2856..3008 | 1 | 3000mV / 857000µA |
| Host | ldoc3 | 1200 | 1 | 1200mV / 21800µA |
| PHY | ldoa5 | 880 | 1 | 880mV / 36000µA |

依据LG基线`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`的`sm8150-regulator.dtsi`、
`sm8150_common-panel.dtsi`与`rpmh-regulator.c`。没有通过充电SPMI接口写面板LDO。

最多8个consumer，ID/token单调递增；每个consumer每rail有独立投票。合并电压下限取max、
上限取min、load用64位求和检查32位溢出，按8mV格点检查可表示性；冲突在硬件写前拒绝。
累计load决定LPM4/HPM7，enable为OR；完成后才提交软件账本。
撤一个投票仍保留其他consumer约束；无软件历史的空账本不下发disable，不关继承ABL电源。
后续显式获得过的最后一个投票可下发enable0，调用者必须先停止相关DMA/link。

`requestVote`成功仅表示接收，token状态为Pending/Complete/Failed；查询不读MMIO。
状态中的enabled/millivolts/mode表示合并目标，撤回自己的票后仍可能因其他consumer而enabled。
Complete仅为ACTIVE ACK，不是物理电压或真正断电的测量。无变化请求可直接完成且不写硬件。
`detachConsumer`遇到在途请求或仍启用的投票返回Busy；不能在析构中擅自撤电。
provider退出保留票与硬件状态，对未detach者WARN；没有反向调用已销毁consumer的路径。
当前所有权模型只管本XNU ACTIVE请求，不能覆盖固件/AOP/其他执行环境的投票。

## ADT与开启边界

`mk-mh2lm-mdss-adt.py --display-resources`生成v1：原索引不变，DISPCC资源1从0x3000
扩至0x20000，包含GDSC(+0x3000)/wrapper(+0x8000)；附带原IRQ9..11，追加资源12
PHY0 `0x0ae94400/0x760`。PLL0位于同一窗口+0x500，不再重复映射/写入。
另加`display-rpmh` nub和`sweet,rpmh-abi=1`；MDSS有`sweet,display-resource-abi=1`。
默认和`--irq-preflight`生成结果逐字节保持旧行为。

`sweetdisplayresources=0`默认关闭新增路径；`=1`映射/绑定/元数据，拒绝电源写；
`=2`允许上述typed内核请求，但**开启参数本身仍不会发送任何投票**。
新模式资源不齐直接失败，不静默改用原路径；不读未获clock的PHY/GDSC寄存器。
用户主动提供0121a13后，已复用该构建生成资源绑定+性能采样及可选对照boot，
见[本批步骤与实测](PANEL-D1-0121A13.md)。主档已验证四路command DB、pre-v3 RSC与consumer绑定，
用户反馈滑动改善且无新增异常；无需补刷control。未测试电源投票，不追加元数据专项云构建。

PD0原图的独立clock/GDSC provider在此明确调整为**MDSS内部typed资源模块的唯一所有者**：
现有MDSS已经拥有GCC/DISPCC，不再另起一个重叠映射的空壳服务。0184先落实资源所有权，
0185已补typed本地分支lease、timer获取/撤回与温态GDSC准入，见[时钟模块](PANEL-CLOCK-D1.md)。
0186补MMCX父供电、0187补CN0/display NoC，0188在显式managed模式下接扫描资源获取；
时钟父源/频率、GDSC冷上下电、NoC冷配置及完整资源反向释放仍待实现；
原`dp_clocks_on()`两个状态位检查仍不是acquire或ready证明。

## 本地验证与下一段

0184严格重放到verified0183，无offset/fuzz，6份变化源码与编辑树字节一致；114条原CI源码
marker保留。RPMh/MDSS/FB/排除后的旧C均以真实XNU/KDK头编译arm64-apple-darwin对象成功。
实际代码ASan/UBSan覆盖：元数据不写、8个ID/stale ID/token、双consumer聚合/撤票、并发
请求的gate串行、posted-write/独立fresh完成、继承冲突、6种故障/stop与晚到事件、在途MMIO
与stop同步、retired后拒绝重建、11类坏资源、真实matched→owner attach→terminated撤回。
原显示/F2/F3/F4、framebuffer、DSI序列和计时回归通过；ADT旧结果字节保留/新窗口翻译通过。
LSan仍受ptrace环境限制关闭，另断言对象/映射/通知归零；不把模拟MMIO当硬件验证。

补丁`xnu/patches/0184-mh2lm-display-resource-leases.patch`；底稿
`scratch/framebuffer-0184/{verification.json,tests.json,test-*.txt,*compile.txt}`。
下一段补冷态时钟/GDSC/NoC配置及PHY/PLL/host控制，再接已确认profile/reset/完整ON/OFF及资源撤回。
E1仍只阻塞未知厂商表写入；这段没有选定PV或把映射成功写成完整驱动完成。
未push、触发CI或操作设备；未改CircleCI、USB/充电/按键/触摸/mux策略。
