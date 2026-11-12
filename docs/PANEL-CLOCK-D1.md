# D1 本地显示时钟引用与 GDSC 准入（0185）

**0121a23实测：** a22的branch-unowned准入拒绝已越过，实际发出branch7 enable；
随后clock token2 Timeout（原result0xe00002d6），尚无Held/mask381。NoC phase2/clock-token2/
register-token7/executing0/clock-cursor7/waiting1，未进入display TCS；后续NotReady/HANDOFF冻结为连锁结果。
当前未记录超时出口/期限/CBCR样本，不能区分enable读回、物理OFF或整体期限/调度问题。
下一步补有界clock timeout证据，不延长期限或删保护。见[实测分析](PANEL-D1-0121A23.md#实测准入已越过branch7-开启完成等待超时)。
本次仅分析/文档，无新编译要求；旧a23不重测，完整接管前其他显示测试暂停。


**0121a23产物/镜像已核对，待实机：** 78源码匹配0210（7d35162），114 source/177 binary markers通过。
ABI608固件、ADT和605字节retained参数保持a22；boot/FAT回读通过。本轮只启动retained约90秒，
验证RSC clock Held/mask0x381，取NoC后续首错或新DMA证据；尚未证明完整接管成功。
镜像路径和取日志命令见[0121a23交付](PANEL-D1-0121A23.md)。不需再次云编译，其他显示测试继续暂停。


**0210已实现（代码`7d35162`，本地通过、待实机）：** RSC分支表区分voted/halt，
NON_GDSC_AHB取得自身enable票，撤回仅确认自身票清除+异步至少10us settle，不强求共享aggregate OFF。
两RSC AHB分支只有实际core2/PLL1 AHB和typed ARC/CN0成功凭据成立才获新开启权限；不普遍放行PLL/PHY。
覆盖worker/最终核验/NoC准入及引用、撤回失败/stop；新专项43组场景+另11套回归、ARM64对象/累计补丁重放通过。
现在需要用户push云编译，新产物核对后打原ABI608 retained；完整接管尚未实机证明。
详见[实现、验证与下一步](PANEL-RSC-CLOCK-VOTES.md)。以下“仅分析/无需编译”为历史。


**0121a22实测已定位首错：** 四LDO/ARC/CN0和core profile2通过，RSC clock在
`branch-unowned branch=7 cbcr=0 source=4 link=0`拒绝；phase2/clock-token0/executing0，尚未进入display TCS。
branch7是LG定义的NON_GDSC_AHB/BRANCH_VOTED，原warm通用策略要求关闭态+XO，未对接core已建立的PLL1 AHB父源。
下一步需区分voted语义，并受coreReady/父配置/上游票约束地取得RSC分支自身票，覆盖worker与撤回；不能简单删保护。
详见[实测和修复边界](PANEL-D1-0121A22.md#实测首错定位到-rsc-non_gdsc_ahb-分支准入)。本次仅分析/文档，无新编译要求。


**0121a17当前执行点：** 已核对0204 ACV metadata诊断：78源码字节一致、114 source/154
binary markers及ABI608镜像回读通过。只启动`boot-16k-0121a17-abi608-panel-retained.img`，
停留约一分钟导日志，目标取ACV具体gate/字段；这不是ACV修复版，仍可能显示静帧。
无需息屏/插拔/长测；identity不重测，combined/supply继续暂停。
见[0121a17镜像与步骤](PANEL-D1-0121A17.md)。以下旧执行点按历史保留，以本段及NOTES为准。

**0121a14 retained实测失败（2026-11-12）：** 可进系统，但亮度滑条不生效，按息屏后画面冻结。
日志第85行MDSS在warm clock lease准入返回NotReady（0xe00002d8），start未完成、未发布服务；
CoreBrightness请求已到（3304–3339行），第3624行FB生命周期准入失败后主动冻结pixels。
冻结后仍有按键/触摸及进程活动，未证实整核死锁；完整OFF/ON、reset及触摸恢复尚未开始。
具体GCC/GDSC/root/branch失败条件旧日志未记录，不能猜寄存器值或放宽保护。
新增0202仅补首次准入的有界分阶段诊断，错误为ERROR、读值为INFO，运行期检查保持安静；
不新增寄存器读取、写入或fallback。需用户push/云编译后打包新retained档取证，尚未修复硬件准入。
**暂停旧retained复测及combined/supply；E7/E8尚未验收。** 详见[retained故障记录](PANEL-D1-0121A14.md#retained启动准入失败)。

**0198后续增量（本地通过，未实机）：** mode5现已将数据票停驻与host/PHY/PLL关闭、
重建组合：OFF保config→放clock→关link→放DATA；ON取DATA→重建link→取clock→重建host→
原reset/ON/亮度/pixel/touch。普通暂停接口仍拒绝改票，仅协调器指定DATA请求获得有限授权；
不是rail-off或冷NoC/MDP。详见[组合事务和架构](PANEL-COMBINED-SUSPEND-D1.md)。
以下按各原阶段范围保留，当前以0198及NOTES顶部为准。

**0197后续增量（本地通过，未实机）：** 新增mode4，保留全部票，实际关闭并重建host/PHY/PLL；
clock状态另报物理暂停，FB核对关闭/重建证据。原0/1/2/3范围保留；未实现rail-off。
详见[硬件关闭及恢复、架构和边界](PANEL-LINK-POWER-D1.md)。以下按原阶段范围保留。

**0196后续增量（本地通过，未实机）：** 已接入mode3 managed数据票释放与重新取得，
保留独立config floor及所有供电/boot/link/core资源；原mode1/2范围保留。
详见[当前资源事务及边界](PANEL-RESOURCE-PARKING-D1.md)。以下按本文件原阶段范围保留。

**0191后续更新：** 已将 PHY v4.0 / PLL 7nm-v2 与 BYTE/PCLK roots 的真实重建接到
managed 链的 host 之前，沿原 owner/gate/DSI timer；独立确认停稳、失锁/新锁及更新/运行。
本地12套检查和arm64对象编译通过，未实机；面板生命周期/整屏冷启动仍未完成。
默认关闭，详见[link实现与边界](PANEL-LINK-D1.md)，下文原阶段记录保留。

**0189当前增量（本地通过，未实机）：** 显式 core profile2 已将 GDSC 软件票、两颗核心
PLL、AHB75MHz/MDP460MHz 的配置与完成检查接到实际 managed 扫描资源链，复用 MDSS
唯一 owner/gate/clock timer；先实际 ARC/CN0 ACK，失败保票并关闭写入。工作 PLL 保留，
关闭 PLL 才初始化；不改 DSI PHY/PCLK/BYTE。冷态核心引擎已做组件测试，但真实 start 仍为
温态入口，尚无整屏冷启动/完整断电恢复或撤票。详见[核心资源控制](PANEL-CORE-D1.md)。
默认关闭，无新刷机轮次，继续补 DSI host/PHY/PLL 和面板完整 ON/OFF；D1 未整体完成。
以下旧段落按当时范围保留，当前增量以0189为准。

0187补充：已有[typed ARC父供电事务](PANEL-ARC-D1.md)及[NoC事务](PANEL-NOC-D1.md)，
0188已在显式managed模式下接实际扫描资源获取，见[生命周期首段](PANEL-MANAGED-D1.md)。
本时钟模块新增RSC use（AHB及NON_GDSC_AHB/RSCC_AHB/RSCC_VSYNC，
mask0x381），由MDSS带宽模块持有真实lease；带宽撤回完成前不能单独释放该token。
新增分支DISPCC+0x4004/0x400c共享AHB root0x22bc，+0x4008共享VSYNC root0x20f8。
继承配置与仅XO/div1可重新使能的限制不变。下文0185按当时范围保留，上游仍标记继承，
不因新API存在而放开冷GDSC或PHY操作。

2026-11-11，接0184资源provider与0121a13实测。本段完成SweetMDSS内部本地分支控制、
引用与有界异步完成；**尚未实现MMCX/NoC投票、GDSC冷开启/关闭或独立链路初始化**。
默认关闭，不为本段单独要求云编译/刷机。完整驱动仍按D1→D2→独立重建→零拷贝推进。

## 现有结构与接口

```text
AppleCLCD / AppleARMBacklight
 └─ SweetDisplayBackend typed 请求
     └─ SweetMDSS : IOService（唯一硬件所有者、workloop / gate）
         ├─ 扫描 / IRQ / DSI executor / 原短DCS
         ├─ 本地时钟模块 SweetMDSSClock.inc
         │   ├─ retainLocalClocks(Core / DSI0) → token
         │   ├─ localClockStatus(token) → 纯软件历史状态
         │   ├─ releaseLocalClocks(token) → 异步撤回
         │   └─ 原workloop上的独立timer，固定分支表与引用账本
         └─ SweetRPMhClient → 实际匹配的SweetRPMh
             └─ 四路面板/host/PHY LDO的typed投票（0184已有，绑定不自动发票）
```

时钟接口是MDSS内部模块接口，调用统一经现有IOCommandGate；上层不传寄存器地址、mask
或“已经idle”的布尔承诺。没有新增重叠映射、clock IOService空壳、全局实例指针或C桥。
全局只有原子writer所有权字：空闲/占用/retired，供销毁后的账本保护。

四个同时有效的lease槽，token递增不复用；释放槽可重用，但旧token失效。Core包含AHB、
MDP、VSYNC；DSI0包含Core、ESC0、BYTE0、BYTE0_INTF、PCLK0。每个分支独立引用计数，
只在完整获取成功后提交；操作串行，Busy不暗中重试。查询不读MMIO，Held表示引用获取完成，
不等于拥有上游供电或保证硬件永远不变；真正使用前另校验完整时钟条件。

## 本地控制和继承边界

| 分支 | DISPCC CBCR | root CMD | 本段权限 |
|---|---:|---:|---|
| AHB | 0x2080 | 0x22bc | 保持已运行配置；config MMIO路径本身必须已通 |
| MDP | 0x200c | 0x20c8 | 保持已运行的合法父源；不启动继承的PLL父源 |
| VSYNC | 0x2024 | 0x20f8 | 已确认XO/div1配置可重新使能；只撤回自己新增的enable位 |
| ESC0 | 0x2038 | 0x2148 | 同上，固定19.2MHz XO |
| BYTE0 | 0x2028 | 0x2110 | 仅保留已运行的PHY0父源，等待后续PHY/PLL控制 |
| BYTE0_INTF | 0x202c | 0x2110 | 同上，另保存0x2128 divider |
| PCLK0 | 0x2004 | 0x2098 | 同上，保存M/N/D与配置 |

仅当非link分支的父源明确为XO、整数旁路或div1且没有update/dirty时，允许恢复原先关闭的分支；
不改RCG父源/频率、PLL或PHY。默认的非XO父源只保留，不据DT就启动它。

GCC AHB是LG的critical/HWCG时钟，本段不写它；按既有config MMIO准入检查SW enable及
DISPCC AHB。GCC HF AXI还须有SW enable并运行；之后才读DISPCC+0x3000 GDSCR。
GDSCR必须PWR_ON=1、SW_COLLAPSE/HW_CONTROL/SW_OVERRIDE全0。与LG一致，HW_CONTROL
不能当作可以任意接管的状态；PWR_ON但SW_COLLAPSE=1也不代表已有HLOS供电票。

**上述条件仅允许温态保留。** 状态中`inheritedUpstream=true`一直保留：没有真实MMCX/
NoC lease，不写GDSCR，不reset/断电/改频，不从睡眠或冷态强开域。HF AXI运行也不是带宽票
证明。后续冷初始化必须补真正的上游provider引用，不能把这些检查改成虚构的布尔ready。

LG资料还区分`mmcx.lvl`与四路LDO；NoC部分同时有apps/display的BCM及`rsc_disp`路由。
下一段要核对command DB类型/辅助数据、consumer聚合与实际RSC目标，不能将所有BCM一律
发往现有apps-DRV或将四路LDO开启当作MMCX完成。本段不新映射display RSC。

## 完成、失败与停止

- 获取前先验证所有目标分支/父源，再开始第一个写；保存CFG、link M/N/D及divider。
  每次推进复核配置，获取结束再整体复核，防止前面分支在等待后面分支时发生变化。
- 分支只RMW enable位，保留其他控制；enable读回不算完成，还要看到CLK_OFF清除。
  关闭同理检查enable清除及CLK_OFF置位；root停转可属于正常关钟结果。
- 每次timer最多推进一个分支/一次写或一次完成查询，100µs timer再调度；单分支等待
  最长10ms，整次请求200ms。没有gate内长忙等、sleep或同步consumer回调。
- 撤回只减少本lease引用，最后引用才可能关自己新增的分支；继承enable始终不关。
  实际关钟还要求IRQ处于Idle、TE/autorefresh关闭、DSI DMA/MDP空闲且没有scanout切换/
  DSI执行或未知在途DMA。重新检查后才写，不相信调用者提供的idle标志。
- timeout、写未生效或配置/供电条件丢失后，记录ERROR/故障与不确定状态，关闭后续写入。
  不盲目反写、不关域；可能已改的分支保留，等待显式恢复设计。
- stop先关admission、取消timer，lease标Stopped，移除event source后释放；停止不是DMA退出，
  所以不自动关时钟。发生过任何时钟写的实例free后writer字变retired，拒绝在同boot创建
  丢失账本的新writer；没有写过的元数据/保留实例可重新创建。

原亮度、28/29电源命令、新DSI序列和扫描接管在开启此模式后都检查本地lease/硬件条件；
Pending或故障返回NotReady，不绕过模块改走旧DCS。没有改已有USB/按键/触摸/电池/mux。

## 参数与测试边界

`sweetdisplayclocks=0`默认完全不创建控制器；`=1`启用温态本地lease，并要求
`sweetdisplayresources=2`及完整v1 ADT。其他值或不匹配明确拒绝启动；没有隐式降档。
开启后MDSS给当前DSI0路径获取一个实际长期lease，运行于自己的timer/gate；生命周期后续
通过同一引用接口使用。即使该lease完成，仍不代表冷启动能力或上游票已归驱动所有。

本段没有修改boot矩阵或要求用户补测；新行为等后续MMCX/NoC/链路代码合并后逐级测试。

## 息屏统计修复

0121a13发现`cpu-interval`跨OFF累计。原清零在`vsyncFired()`，实际OFF会取消timer/暂停IRQ，
可能完全没有该回调。0185在真正的`requestPanelPower()`状态变化处清`lastPresentNS`。
不清first/latest pending，保留息屏期间最新swap及其年龄语义；没有改变复制频率、屏障或呈现。

## 验证与证据

补丁`xnu/patches/0185-mh2lm-local-display-clock-leases.patch`，严格应用至0184验证树，无
偏移/fuzz，6份变化源码逐字节匹配；114条原CI源码marker保留。补丁SHA256：
`84d019a5201587645f8974254788cedd05a38ffcf2f688300863ed1d4276b059`。CircleCI blob仍为
`da0b6c0947a66f03491b3a9023590b49665292df`。

真实XNU/KDK头的arm64-apple-darwin MDSS/FB对象编译成功；对照基线编译也成功。
新编译告警为42条固定有界数组访问提示，已逐项核对索引/状态范围；其余既有告警保留，
不声称零warning。ASan/UBSan实际源码测试覆盖：

- 默认/metadata不进入新路径，16类资源/域/父源/时钟准入拒绝；重复writer和timer分配/
  添加失败的清理；四lease限制、Busy、旧token与多引用。
- 独立硬件ON/OFF完成（写寄存器不会自动伪造完成）、共享引用不提前关钟、DMA/TE忙拒绝
  释放、最后引用关闭时正常root停止、超时/丢写/源漂移/域与接口丢失、最终整体复核。
- stop/晚到timer/销毁后retired、默认无PHY/GDSCR写、实际DCS/电源/DSI接口拒绝不就绪路径。
- 原display/F2/F3/F4、framebuffer、DSI、RPMh、ADT回归及FB计时通过。FB测试走真实OFF/ON
  API，OFF期间没有任何vsync回调，验证interval排除息屏且pending时间仍保留。

LSan受环境ptrace限制关闭，另检查对象、映射、通知和分配计数归零。这些是本地验证，
**不是实机时钟/GDSC或冷初始化验收**。底稿位于工作区`scratch/framebuffer-0185/`，含
`verification.json`、`warning-counts.json`、`reference-audit.json`、编译输出及七套测试日志。
