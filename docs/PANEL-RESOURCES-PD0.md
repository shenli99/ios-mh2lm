# PD0：完整显示/面板驱动的资源、接口与实施依赖

**0196当前增量（2026-11-12，本地通过，未实机）：** 协调器增加显式`sweetpanellifecycle=3`：
完整OFF后取得独立config-only NoC票，再释放managed clock/DATA NoC票；ON先重新取得新票，
再执行原reset/完整ON/亮度/fresh像素/触摸恢复。CN0 300MB/s floor、boot/RSC clock、
LDO/ARC/core/host/PHY继续保留；释放本票不等于物理关钟/断电，也不撤其它active票。
原0/1/2语义保留，无mode2回退。资源/clock/NoC各推进点重查同代fence及硬件静止/期限，
FB要求新能力与资源释放/恢复证据。19套本地检查及MDSS/FB真实arm64单文件编译通过，
严格重放/114个原CI marker通过；不代表整核链接或硬件结果。
详见[资源票释放与恢复、架构及边界](PANEL-RESOURCE-PARKING-D1.md)。
PD3物理host/PHY/供电关闭、PD4冷NoC/MDP及独立重建、ABL退出仍缺；共享供电E8未关闭。
启动侧仍须合入0195新DarwinBoot；本轮无新FD/boot、push/CI/设备操作，CircleCI未改。
暂不为此单开云编译/测试，继续完整驱动优先。以下0195及更早段落保留当时范围，当前以0196为准。

**0195当前增量（本地通过，未实机）：** 已把四版LG完整ON/OFF表、Sleep In/Out、typed GPIO reset
和亮度恢复接入原协调器，新增显式`sweetpanellifecycle=2`，保留全部供电/ARC/NoC/clock票。
原0/1语义不变；2要求经实际Mu→DarwinBoot→ADT链路传递的唯一ABL选择及pins2等依赖，
缺失/歧义/未知就拒绝，不猜PV。启动侧另交付DarwinBoot补丁，旧shim没有变更；仅新内核配旧UEFI
不会得到身份。本机live选择及reset恢复仍未实测。FB核对完整模式能力与reset/亮度/像素证据，
pins新增consumer/token取消、停timer保留部分状态。18套检查、arm64对象及Mu消费的DarwinBoot模块
编译链接通过；不代表整核/完整FD/实机。详见[当前PD3架构与边界](PANEL-PROFILE-D1.md)。
完整反向释放、冷NoC/MDP、独立重建与ABL退出仍缺；A单缓冲/30Hz复制不变。暂不要求push/刷机，
CircleCI未改。以下0194及更早段落保留当时范围，当前以0195和NOTES顶部为准。

**0194当前增量（本地通过，未实机）：** 已接通SweetMDSS内部统一生命周期协调器，复用
原唯一gate/timer及资源/扫描/DSI执行层。显式sweetpanellifecycle=1把实际温态接管与28/29
OFF/ON串成typed token事务：触摸暂停fence→显示DMA/IRQ真实静止→原资源/链路/像素完成→
触摸验证恢复；完成前FB不交付Active/不呈现。快速相反电源请求合并，取消/provider撤回/超时/
stop终止本代、保票/保DMA pin，无自动恢复。背光Busy保存最新目标，原生IOKit interest仅排队
自己的事件，事务完成后重试，无空闲轮询。默认关闭，未知profile的prepare/reset/unprepare
在副作用前Unsupported，不调用pins activate/reset/park、不猜vendor表。16套本地检查、
真实XNU/KDK arm64对象与严格重放通过；详见[协调器架构、接口和边界](PANEL-LIFECYCLE-D1.md)。
完整reset/ONOFF/反向释放、冷NoC/MDP、独立重建和ABL退出仍未完成。后续继续完整驱动优先，
之后零拷贝；暂不要求push/刷机，CircleCI未改。以下0193及更早段落均按当时范围保留，
当前以0194及NOTES顶部为准。

**0193当前增量（本地通过，未实机）：** 已补`SweetTouchPanel : IOService`及MDSS真实
匹配绑定，typed暂停/恢复交给原唯一TouchComm worker；消费者不穿透读写寄存器。
暂停fence要求peripheral/GIC禁用读回、active退出和HID触点释放；parked worker/晚IRQ无硬件I/O。
恢复复用实际识别/app/格式读回再arm；无touch reset/电源/固件写入，无轮询回退。
C后端是无实例指针/回调的原子mailbox，未把整个触摸驱动转换成IOService；stop取消是异步收尾，
不伪造硬件静止证明。默认关闭，MDSS本批仍仅绑定，未实际调用pause/reset/完整ONOFF。
14套本地检查、arm64单文件对象和严格重放通过。详见[触摸协调实现与边界](PANEL-TOUCH-D1.md)。
下一步接统一生命周期事务及已确认panel profile；E1与E7实机共享供电证据仍缺，完整驱动未完成。
以下旧段落按当时范围保留，当前以0193及NOTES顶部为准；暂不要求push/刷机，CircleCI未改。

**0192当前增量（本地通过，未实机）：** 已补独立`SweetPanelPins : IOService`及MDSS真实
registry匹配绑定，管理GPIO6/SOUTH reset与GPIO8/NORTH TE。typed请求在provider workloop/
gate/timer执行，分别确认输出latch与实际输入，reset最短10/2/10/5ms；支持park后低保持重激活。
默认关闭，MDSS本批仅绑定，不调用activate/reset/park；实际启动尚未复位面板。
13套本地检查及arm64单文件对象通过，严格重放和114个原CI marker通过；CircleCI未改。
详见[引脚服务架构、时序与边界](PANEL-PINS-D1.md)。PD3仍需实际panel身份、触摸协调、
完整vendor ON/OFF及资源反向释放，整屏冷初始化/ABL退出未完成。继续完整驱动优先，暂不要求push/刷机。
以下旧段落按当时范围保留，当前以0192及NOTES顶部为准。

**0191后续更新：** 已将 PHY v4.0 / PLL 7nm-v2 与 BYTE/PCLK roots 的真实重建接到
managed 链的 host 之前，沿原 owner/gate/DSI timer；独立确认停稳、失锁/新锁及更新/运行。
本地12套检查和arm64对象编译通过，未实机；面板生命周期/整屏冷启动仍未完成。
默认关闭，详见[link实现与边界](PANEL-LINK-D1.md)，下文原阶段记录保留。

**0184实现更新：** 下文原静态清单/设计按PD0保留。实际已有RPMh provider、唯一TCS入口、
typed投票与MDSS匹配绑定，以及v1资源ABI，详见[资源层实现](PANEL-POWER-D1.md)。
0121a13已实测四路command DB/pre-v3 RSC及MDSS consumer绑定，尚未实测投票；
自有A接管/唤醒通过，用户反馈滑动有所改善、无新增异常，见[本批记录](PANEL-D1-0121A13.md)。
第5节的独立clock/GDSC provider修订为MDSS内部typed资源模块（硬件独占映射不拆出第二writer）；
0185已补MDSS内部温态本地时钟lease与分支控制，见[时钟模块](PANEL-CLOCK-D1.md)；
0186补RPMh内MMCX及MX/CX事务，见[ARC](PANEL-ARC-D1.md)；0187补apps CN0及MDSS独占的
温态display RSC/NoC，见[NoC实现](PANEL-NOC-D1.md)。0188把这些事务接入显式managed模式的
实际扫描资源获取，见[获取/失败/保留边界](PANEL-MANAGED-D1.md)，尚未完整面板ON/OFF或反向释放。
0189已补GDSC与核心PLL/AHB/MDP控制并接实际温态获取，见[核心资源](PANEL-CORE-D1.md)；
冷核心组件测试不等于现有start支持整屏冷启动。NoC冷配置及DSI/PHY/完整ONOFF仍缺。
0190已将host v2.3软复位/公共command配置重建接入实际managed路径，见[host](PANEL-HOST-D1.md)；
实机版本20030000已有证据，目标TRIG外部TE位与ABL不同，尚未实测。PHY/PLL仍继承。

**执行顺序更新：** 用户要求完整驱动优先，拷贝双缓冲不再是PD2/PD3前置；E1/E4–E8
仍只阻塞对应硬件写入，不阻塞先实现typed接口、资源所有者和离线可验证的命令执行器。
单项只读取证尽量并入真实驱动批次，不单独占云编译；见[交付批次](PANEL-FULL-DRIVER-PLAN.md#7-加速后的交付批次与性能回归)。

2026-11-10。基线为项目 `1872425`，驱动实现为 `42047d3` / 0181；LG 主源 HEAD 为
`d4f335e13d73c4aa2af85581a6ec11b670ee5b94`。

**本轮完成静态资源核对和接口/恢复设计；实际面板版本、硬件资源状态与写入权限仍待证实。**
这是 PD0 的设计交付，不是 PD1 接管成功，也不是已完成冷启动。当前可以继续 F4 预检；
后文按阶段列出未关闭证据项，不要求先解决所有冷启动资料才能测试扫描池。

证据分三类：**源码**为 LG DTS/实现明确描述；**已有实测**仅指先前 XNU 日志已确认；
**待证实**为本机版本、资源状态或新增行为。源码值不直接等于本机测量结果。
工作区 `docs/panel.md` 是资料入口，以下补充不能仅按面板节点名称推断的依赖。

## 1. 影响实现方式的核对结果

1. **DSI controller v2.3、PHY v4.0、DSI PLL 7nm。** `sm8150-sde.dtsi` 的
   `mdss_dsi0`/`mdss_dsi_phy0` 和 `dsi_catalog.c` 决定真正的 ops；不是直接选旧
   `dsi_phy_hw_v3_0.c`。`sm8150-sde-pll.dtsi` 为 7nm，`sm8150-v2.dtsi` 改为
   `7nm_v2`；不能因头文件名 `mdss-10nm-pll-clk.h` 而选用 10nm PLL 初始化。
   已有 `scratch/mh2lm-dt/android-mh2lm.dts` 也描述 v2.3/v4.0/7nm_v2，但本轮未重新
   核验该旧解码文件的提取来源，不能据此代替当前硬件版本确认。
2. **完整 ON 表不能由现有短 DCS 接口直接发送。** `SweetMDSS::dcsWrite()` 限制
   `count<=16`，即最多 64 字节的已编码数据。PV ON 表包含 149 字节的单条 payload；
   不应只放大上限，也不能把一条厂商长命令拆成多条短命令。
3. **面板主供电走 RPMh，而非任意 SPMI 写。** `vddi/vpnl` 对应 `ldoa14/ldoa17`；
   PHY/host 另需 `ldoa5/ldoc3`。需补资源投票 provider；现有 mux 的充电寄存器白名单
   不授权这些电源写入，不得为面板而扩充充电 API 的任意写权限。
4. **`use-labibb` 是 LG 流程分支，不是某 PMIC peripheral 的地址证明。** 对此候选面板，
   该分支调用 `dsi_pwr_enable_regulator(&panel->power_info, ...)`，所选供电表只有
   `vddi/vpnl`。LG common-pm 将 `pm8150l_lcdb` 置为 disabled；PM8150L 基础 AMOLED
   regulator 节点也是 disabled。PV 文件尾部提到 PMI8998/LABIBB 的块在注释内。
   这些都不能成为主动配置某 LAB/IBB 电压的依据；如本机另有外部偏压控制，仍须补证据。
5. **现有 ADT 只足以支持交接显示。** 未覆盖完整 PHY/PLL、DISPCC GDSC/clamp、
   mixer/pingpong/VBIF 等冷初始化资源。补地址前要明确 provider 所有权及重叠窗口，
   不能把现有 `dp_clocks_on()` 的两个状态位检查误写成“已使能显示时钟”。

## 2. 面板身份、模式与命令表

| 项目 | 源码候选配置 | 当前证据/限制 |
|---|---|---|
| 面板版本 | Tianma MD，R66456 系列；PP1/DV1/DV2/PV 均有独立节点 | PV 候选为 R66456A；分辨率相同不能区分 revision |
| 模式 | 1080×2340@60，RGB888，command mode，VC0，4 data lanes | 现有显示正常，F3 实测约59.9Hz；完整模式/厂商表仍需绑定实际版本 |
| 逻辑画布 | XNU 1125×2436，stride4608；面板矩形偏移22,48 | 来自本项目实测；不能用逻辑尺寸替代 DSI panel timing |
| lane/PHY | `lane_map_0123`；PV 14-byte timing `00 21 08 08 25 22 09 08 06 03 04 00 1b 18` | v4 timing parser 明确要求14项；lane swap、PN swap按实际解析规则和版本核验 |
| 时钟 | PV `panel-clockrate=1056619200` | 是配置目标，非本轮实测；byte/pixel/escape及PLL分频须按LG公式/版本计算，不能直接把它当pixel clock |
| reset | GPIO6，高10ms→低2ms→高10ms | 源码；本轮未切换GPIO |
| 亮度 | DCS51，大端12bit；panel1..4095、framework max365 | 当前DCS亮度已有实测，PD恢复须在完整初始化后重放期望亮度 |

PV 表严格按每包7-byte头解析：`type,last,vc,ack,wait_ms,len_hi,len_lo`，之后为payload。
本轮检查了所有包的长度边界，结果：

| 表 | 包数 | 编码总字节 | 最大单包payload | 已核对的尾部/等待 |
|---|---:|---:|---:|---|
| ON | 17 | 694 | 149（D3） | `35 00`、2A、2B、29、11，11后150ms |
| OFF | 4 | 35 | 3 | B0 03、51 00 00后90ms、28、10后150ms |

其他超64字节payload有 CE67、E2135、DF92。以上长度含 command byte，不含7-byte头。
这是 PV 源表核对，不是允许把该表直接发给尚未确认版本的设备。
编码表 SHA256：ON `902b3fd5b869e6304df3c9459a55b6c6da0afdc8162126cc2d4e6086b6c24a5f`，
OFF `8b0def2d5a08f9534d8c9efdbbdbbba584387d1cfaecf9bdff50a3c0d71a5b95`。

## 3. 电源资源与传输边界

| 用途 | 源码资源/consumer要求 | 访问与依赖设计 | 现有实现 |
|---|---|---|---|
| 面板 VDDI | PM8150 L14 / `ldoa14`，1.8V、enable load62000µA、post-on20ms | RPMh regulator lease；共享资源按consumer合并投票 | 0184 typed LDO票已实现，面板生命周期未调用 |
| 面板 VPNL | PM8150 L17 / `ldoa17`，3.0V、enable load857000µA | 与VDDI按供电表顺序启用，反序释放；provider校验可表示电压 | 0184 typed LDO票已实现，面板生命周期未调用 |
| DSI host analog | PM8150L L3 / `ldoc3`，1.2V、21800µA | host/core资源阶段申请，不与面板vddi混为同一状态 | 0184 typed LDO票已实现，host/PHY生命周期未调用 |
| PHY analog | PM8150 L5 / `ldoa5`，0.88V、36000µA | 先有供电/接口时钟，才可复位/配置PHY | 0184 typed LDO票已实现，host/PHY生命周期未调用 |
| 核心电压级别 | `mmcx.lvl` / `VDD_MMCX_LEVEL`及相关父依赖 | 通过command DB取得等级表；选择与实际频率相容的corner，并保留其他consumer投票 | 0186 typed聚合/父依赖/分阶段ACK已本地实现；频率约束及MDSS实际调用未接入 |
| MDSS电源域 | `mdss_core_gdsc`，0x0af03000 | 时钟/电源域provider拥有MMIO；按ready/超时执行，不由AppleCLCD写 | 0185只作温态准入，不写GDSCR |
| AHB/AXI/MDP/DSI clocks | GCC DISP AHB/HF/SF AXI；DISPCC AHB/MDP/VSYNC/DSI0 byte、byte-intf、pixel、escape等 | provider显式设置父源、频率、分支和引用；实际需要的LUT/其他分支按pipeline确定 | 0185温态分支lease已实现；非XO父源/改频仍未控制 |
| 互连/带宽 | LG `sde_power_handle.c` 的reg/data bus vote及RSC路径 | 不将“时钟开”当作DDR/NoC就绪；PD2列明最小带宽/请求与撤销方法 | 0187 typed CN0+display BCM温态ACTIVE，0188接managed扫描获取；冷QoS/solver及完整撤回尚缺 |

电源数值来自 consumer table；例如 L17 provider 的初始化电压为2.856V，不等于面板
consumer申请的3.0V，不能照搬provider默认值替代面板要求。load是投票参数，不是实测电流。
有其他consumer或固件投票时，释放本驱动lease不等于电源物理关闭；PD4必须另有状态证据。

当前 `sweet_rpmh.c` 已有command DB、ACTIVE TCS、完成检查和poison处理，可参考/迁移，
不是完全没有RPMh。核对0121a11真实云源码：SHA256
`be347e987f70ec41831f61e4012a83980d254a45390a3e1c4280c9cab352c6e8`。
但其vreg helper仅接受板级touch资源和2950..3400mV，固定HPM/enable；ARC helper直接写
corner，没有多consumer聚合、lease/disable或sleep/wake管理。不能把参数换成ldoa14就复用。
新增provider必须接管/协调原TCS writer，不能另建一套互不知情的锁争用同一apps-DRV。
RPMh不经过SPMI mux；只有真正走SPMI的新增功能才使用现有共享事务锁与限定接口。

## 4. MMIO、引脚与中断所有权

| 资源 | 源码物理窗口/用途 | 当前ADT/迁移要求 |
|---|---|---|
| MDP整体 | 0x0ae00000/0x84208；包含mixer/pingpong等子块 | 目前仅局部SSPP/CTL/INTF/TE/MDP窗口；PD2按catalog核对最小完整pipeline，勿盲目映射整块 |
| VBIF / regdma | 0x0aeb0000/0x2008，0x0aeac000/0x214 | 未描述；确认是否使用，再由MDSS拥有，不能默认为沿用ABL即可 |
| DSI0 host | 0x0ae94000/0x400 | 当前MDSS资源6已映射；保留单一所有者 |
| PHY0 | 0x0ae94400/0x760，PLL参考另描述此处/0x800 | 当前缺失；PHY/PLL的重叠映射由同一link模块管理，不能两个独立writer |
| PLL0 | 0x0ae94900/0x260 | 在PHY范围内，属于7nm/v2布局；实际版本确认后配置 |
| DISPCC | 0x0af00000/0x20000 | 当前仅0x3000；GDSC位于+0x3000、DSI wrapper在+0x8000，均不能从现有映射越界读写 |
| GDSC / DSI wrapper | 0x0af03000/4，0x0af08000/4；PLL还描述GDSC/8 | 统一由相应资源provider拥有；clamp/clock操作通过typed方法，不能重复无协调映射 |
| TLMM reset | GPIO6；common active组同时列GPIO6/7，8mA无pull；suspend2mA/pulldown | 只申请已确认需要的引脚；不因通用组就接管GPIO7。tile/功能选择由TLMM provider核对 |
| TLMM TE | GPIO8，active/suspend均mdp_vsync、2mA、pulldown | 使用已验证MDSS RD_PTR链，不额外把GPIO8注册成另一个vsync中断来源 |
| MDSS父中断 | DTS SPI83 → GIC INTID115 | F3已有实际投递；需在现有dispatcher内扩充完成/错误路由，不重复注册父线 |
| DSI0子中断 | `interrupt-parent=mdss_mdp; interrupts=<4 0>` | 是MDSS域内子编号，**不是GIC INTID4或36**；PD2按硬件status扩充分发 |
| SMMU | SID/mask/CB2、DMA范围与缓存 | 使用0181只读证据；buffer物理连续不替代live可达性和传输完成 |

`mdss_core_gdsc`基础DTS为disabled，旧本机解码DTS为ok。此处说明需要解析生效配置，
不能仅复制基础DTS的status；同样不能把旧解码文件当本次实时硬件状态。

触摸当前用GPIO54/59/152、IRQ122及I2C55/56；与panel reset6/TE8没有直接引脚重合。
但这不证明供电/内部状态完全独立：LG touch有panel RESET/BLANK notifier，现有XNU
触摸没有完整的面板断电协调接口。PD3 reset/断电前需明确quiesce、恢复和触摸有效性验证，
不顺带改变触摸IRQ策略，也不从Android其他触摸型号的reset脚替换现有实测配置。

## 5. 冻结的分层与接口方向（尚未实现）

面板序列先作为 **SweetMDSS 内部独立模块**，由同一workloop/gate驱动。避免“MDSS等待
Panel IOService，而Panel又反向同步等待MDSS DSI”的循环依赖。硬件资源另由匹配到的
RPMh、clock/power-domain、TLMM provider管理；后续需要独立面板服务时仍沿typed接口拆分。

```text
AppleCLCD : IOMobileFramebuffer
  ├─ 自有扫描池 / surface与完成状态 / 用户接口
  └─ SweetDisplayBackend typed接口
       └─ SweetMDSS : IOService（显示状态唯一协调者）
            ├─ MDP pipeline / 提交与完成 / 现有父IRQ分发
            ├─ DSI0 link模块（host + PHY0/PLL0）
            ├─ 面板模块（已确认的profile、命令表、生命周期）
            └─ 持有的资源lease
                 ├─ RPMh provider（command DB / 唯一TCS所有者 / 投票聚合）
                 ├─ clock和power-domain provider（GCC/DISPCC）
                 └─ TLMM provider（GPIO6/8及pinctrl）
AppleARMBacklight ── typed亮度请求 ── SweetMDSS
```

后续接口按以下语义实现，名称和C++签名随相应补丁审查；不先加入无人使用的空壳类：

- **资源lease**：consumer申请具体resource和目标约束，返回持有对象/完成结果；释放仅撤回
  该consumer的投票；provider处理冲突/生效值/失败，不接受任意寄存器地址。
- **模式/电源请求**：AppleCLCD表达期望模式/状态；MDSS区分handoff、ready、transition、
  off、fault。现有`setPanelPower(bool)`保持F2语义，完整上下电另建明确接口/档位，不偷偷
  让当前28/29调用突然执行断电。
- **DSI message**：typed的data-type/VC/LP/payload/长度/完成结果；新传输支持至少本表的
  149-byte单包及编码开销。按v2.3实现选择受支持FIFO/DMA路径；DMA内存由descriptor准备、
  核对可达性并持有至真正完成，不能只扩大`count`或任意拆分payload。
- **提交/完成**：token关联的buffer/address请求与硬件传输完成通知，分清已提交、已生效、
  旧读者退出；RD_PTR只提供vsync。扫描buffer与DCS命令DMA的生命周期独立。
- **通知**：匹配回调只retain/撤回和排队；IRQ上半部只屏蔽、记账、排队。provider通知
  consumer仅发布mailbox/software IES，不同步进入consumer gate。

锁顺序：consumer不持AppleCLCD gate等待长电源转换；MDSS在自己的loop推进状态，资源
provider不得回调获取MDSS gate。150ms等阶段等待通过timer/完成继续，不在gate内忙等，
也不持SPMI/TCS事务锁跨等待。每次排队操作携带generation；stop先关闭admission/失效旧
generation，撤销源并等待正在执行的操作退出，再释放资源，避免旧完成误推进新状态。

## 6. 冷初始化与停止的依赖顺序

以已确认的非splash普通command-mode为目标；以下提炼LG主路径，具体硬件写序列在PD2
各补丁内实现，不当作可直接运行的寄存器脚本：

1. 持有provider/lease，准备自有buffer，确认SMMU及DMA条件；建立中断/完成接收入口。
2. LG `dsi_panel_pre_prepare()`：选active pinctrl；条件GPIO序列；固定5ms；此候选
   `reset_after_ddvd`分支按vddi→vpnl申请regulator（含vddi20ms），再等5ms。
3. core clock/host及PHY供电依赖就绪；PHY reset/配置、PLL源/频率、controller配置、
   host enable及link clocks按`dsi_display_prepare()`及clock manager依赖完成。
4. PV的`lp11-init`使panel reset推迟到host/link准备后的`dsi_panel_prepare()`；GPIO6
   reset表后LG另等5ms。不能提前把panel reset塞进matched回调或照搬无LP11分支。
5. panel enable发送选定版本完整ON表；再按LGcommand-engine和MDP配置流程准备自有首帧。
   通过真实传输完成/TE证据进入ready，随后恢复期望亮度和上报可用状态。

正常停止先关闭新提交，保留完成事件与所需时钟，等待已发MDP/DSI/DMA退出；DCS OFF表
仍需host/link可用。LG disable关command engine并发OFF，unprepare执行post-off及相应
reset-off，再关闭host/link/PHY/core，最后panel post-unprepare释放电源。LG支持ULPS保留
PHY的普通suspend；PD4的全断电诊断必须显式选择完整关闭，不能从普通suspend推断状态丢失。
不用简单“关时钟→发10/28”或假定所有函数严格反序。

失败回滚记录本次实际获得的lease/已完成阶段；只释放本consumer资源。命令或DMA超时
先认定硬件状态不确定，保留可能被读取的buffer；有界停止/恢复失败则进入fault，不能把
timeout当完成，也不能关闭其他consumer电源或翻回ABL来伪装成功。

## 7. 未关闭证据与下一步

| 编号 | 尚缺证据 | 影响阶段 | 获取方式/当前决定 |
|---|---|---|---|
| E1 | 实际panel/revision及对应vendor表 | PD3完整命令写入 | 优先从已有ABL/UEFI传递的选择标识或保存的原机日志取证；当前限定搜索95份logs/evidence文本未找到`msm_drm.dsi_display0`。不要求重启Android，不主动试发未知DDIC读命令 |
| E2 | A新DMA传输、画面及唤醒已留证；B传输仍缺 | F4/PD1 | 0121a12已取得A接管/新传输/ON地址证据；0121a13再验证，用户反馈滑动改善、常用操作无新增异常。B未提交，CB2.M0/raw FSR0x400保留，不泛化为任意DMA范围可达；不依赖E1确认 |
| E3 | autorefresh下提交、锁存与旧读者退出的对应事件 | F4后续翻页 | 0182有界暂停TE/auto，两次空闲采样，清旧PP0 DONE后提交；0121a12 frame130→131已观察新DONE/flush0/RD_PTR推进。不授予旧buffer释放权，提交池保留到重启；通用完成/撤销与A/B复用协议仍缺 |
| E4 | 本机PHY/PLL版本、lane及频率/ready状态（DSI host版本已知） | PD2写link配置 | 0121a12已有DSI0 HW_VERSION=20030000；0183发送前复核。0184 v1 ABI已加入PHY/PLL单一受保护窗口，尚未取得clock，不读取版本或越界访问 |
| E5 | RPMh写权限、实际投票/撤销及物理结果仍缺 | PD2/PD3 | 0184统一provider已实现；0121a13实测ldoa14/17/c3/a5地址0x42100/0x41300/0x41e00/0x40b00，pre-v3 RSC id0x10700/config0x800c8104/ncpt16/ACTIVE2、consumer1绑定通过。mode1无投票；继承请求不覆盖，ACK不当实际掉电证明 |
| E6 | 冷态clock/GDSC/MMCX/NoC依赖及恢复状态 | PD2/PD4 | 0185温态clock、0186 ARC、0187 NoC及0188 managed扫描获取已本地验证；未实机投票，冷GDSC/父源/NoC配置仍缺，不能用运行位替代实际票 |
| E7 | 面板/触摸共享供电与reset后的触摸需求 | PD3 reset/PD4 | 校对生效DTS和已有板级资料，设计quiesce/resume并用分档验证；引脚不重合不等于无共享 |
| E8 | 是否存在额外偏压电源控制、完整off后的物理状态 | PD3/PD4 | 先沿实际选定panel供电表/生效DT和板级证据核对；不得仅因use-labibb而扫描/写PMIC偏压寄存器 |

PD0静态设计已具备继续条件，E1/E4–E8保留为对应写入阶段的门槛；不标记完整硬件资料
已全部实证。**0181已实测取得E2的池/live配置；0182受控接管本地验证通过，0121a12接管/唤醒日志通过，用户已报告滑动卡顿加重，其它视觉项未单独确认；不能据此验收回收/双缓冲。**
0121a11产物/boot已核对，preflight运行通过，无需补刷baseline；接管前仍须明确新DMA范围、
缓存/格式及提交与完成关联，不能用本轮原始状态值代替。原F3频率矩阵、半小时息屏和BUG-002
不额外重测。实测与FSR边界见[记录](FRAMEBUFFER-SCANOUT-F4.md#9-0121a11扫描池预检实测首段运行通过新dma尚未验证)。
0182另设own2，不改变own1；完整实现、pin至重启的实验边界和测试步骤见
[接管记录](FRAMEBUFFER-HANDOFF-F4.md)。未改CI，无新设备操作。

## 8. 可追溯资料

LG根目录为工作区`ref/android_kernel_lge_sm8150`。本轮直接核对：

- `arch/arm64/boot/dts/qcom/{sm8150-sde,sm8150-sde-pll,sm8150-v2,sm8150-regulator,sm8150-gdsc,sm8150-pinctrl,pm8150l}.dtsi`。
- `arch/arm64/boot/dts/lge/sm8150_common/{sm8150_common-panel,sm8150_common-pm}.dtsi`及
  `settings_panel/dsi-panel-r66456-pv-tianmamd-fhd-cmd.dtsi`。
- `drivers/gpu/drm/msm/dsi-staging/{dsi_catalog,dsi_display,dsi_pwr,dsi_phy,dsi_phy_hw_v4_0}.c`，
  `drivers/gpu/drm/msm/lge/lge_dsi_panel.c`和`drivers/gpu/drm/msm/sde_power_handle.c`。
- `drivers/regulator/rpmh-regulator.c`、`drivers/clk/qcom/{dispcc-sm8150,mdss/mdss-dsi-pll-7nm}.c`、
  `drivers/input/touchscreen/lge/touch_core.c`。

本地证据保存在`work1/scratch/panel-pd0/reference-audit.json`：19份主要参考源码SHA256、
ON/OFF逐包长度/type/等待、表SHA；另有从0121a11真实buildtree提取的`sweet_rpmh.c`。
核对了0181验证树的`SweetDisplayBackend.h`、`SweetMDSS.cpp`、`sweet_spmi.h`、
`sweet_spmi_mux.h`、`sweet_touch_tcm.c`及当前`tools/mk-mh2lm-mdss-adt.py`。
源码分析不构成新实机测试；未读取或输出设备唯一标识，未修改参考仓库及设备备份。

0183已实现D1的[命令基础/可撤销执行/性能计时](PANEL-COMMAND-D1.md)并本地验证；
0184已实现资源ABI、RPMh统一所有者/typed投票及绑定；0121a13元数据/绑定实测通过。
0185补温态clock、0186补ARC、0187补NoC，0188接实际managed扫描获取，完整撤回仍待后续；
冷态clock-GDSC/NoC配置及PHY/PLL初始化仍缺，
不能将窗口映射当就绪或将本轮当实机投票验证。
