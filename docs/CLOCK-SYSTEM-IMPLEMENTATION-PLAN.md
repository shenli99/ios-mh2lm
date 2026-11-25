# SM8150 时钟系统分阶段实施计划

更新：2026-11-22（按审查意见修订：需求清单前移、候选合并规则、debugcc 测频、控制器范围、C6 入口、apps RSC 交接拆分、通用约束集中）。状态：**计划已制定；C1-01～C1-04 已实现为只读候选（补丁 0230／0231／0232），默认档与诊断档均已实机通过（写计数为零），其余小计划未开始**。目标设备为 LG G8X（mh2lm／SM8150）。本轮只编制计划，不修改驱动、CI、固件、设备树或启动参数，不生成测试镜像。

## 1. 最终目标与范围

由 `IOQcomClockProvider : IOService` 为外设提供统一时钟入口，GCC、RPMhCC、DISPCC、VIDEOCC、CAMCC、GPUCC 分别提供后端。外设按资源名取得句柄，通过有完成结果的请求使用时钟；reset、电源域、regulator、带宽保留各自的资源语义。

公共 RPMh 事务模块和 `IOQcomRSCChannel` 供 RPMhCC、RPMhPD、regulator、BCM voter 共用。apps RSC 与 display RSC 分实例；同一寄存器资源／DRV 的 TCS 集合只有一个写入者。SM8150 首版不增加没有实际职责的 MMCC wrapper。

交付按“具体资源及已验证能力”登记，不能因为一个控制器能初始化就称其功能齐全。芯片中尚无消费者、无安全样本或无完整资料的资源保留为未支持；后续按第9阶段逐组扩展。

**需求驱动**：实施顺序由 C0-04 的消费者需求清单决定，不按控制器逐个“补齐”。某阶段的卡若没有清单内消费者需要，标“暂缓（无需求）”，不为凑进度实施；需求出现后再解除。

**控制器范围**：LG 参考树含 GCC、RPMhCC、DISPCC、VIDEOCC、CAMCC、GPUCC、NPUCC、SCC 及 debugcc。NPUCC、SCC 不在 C7 首批内，其适用性由 C0-05 依实际 ADT 判定并记录来源，不默认省略。整个计划不承诺同步完成 GPU、相机、视频编解码器或整机 CPU suspend 驱动。

## 2. 分层与不可混淆的关系

| 层 | 所有权及职责 |
|---|---|
| 统一时钟入口 | 注册／查询后端，绑定设备资源名与 `(controller, local ID)`，管理句柄及跨 CC 依赖协调；不直接映射所有控制器 |
| 本地 CC 后端 | 自己的 MMIO、branch、RCG、PLL、reset；本地 GDSC 操作通过同一寄存器所有者提供 |
| RPMh 资源客户端 | 按资源类型聚合并编码；同一通道／资源／状态集不能建立互相覆盖的聚合账本 |
| RPMh 事务模块 | 状态集、事务、confirmed 缓存、批次和完成语义；初期可与 RSC 共处一个服务的不同模块 |
| RSC channel | 所属 DRV 的 TCS 调度、寄存器、trigger、IRQ、期限和回收；不猜测 corner／带宽含义 |
| 电源资源层 | 电源域、供电票、频率到 corner 的约束和 PM 连接；缺能力时明确拒绝 |

- Service Plane 挂载、clock parent、电源依赖分别保存。一个 CC 可以覆盖多个域；clock 节点不必各建 IOService。
- GCC 的本地 vote 位仍走 GCC MMIO；普通 GCC 操作不因“投票”一词转走 TCS。
- register-access clock／domain 与输出时钟的依赖分别描述，避免域关闭后无法访问其上电控制寄存器。
- 供电／corner 提高完成后才提高频率；降频完成后才降低所需 corner。具体 GDSC／reset 顺序按已核实硬件流程执行。
- 新的 IRQ／TCS 内存、队列容量等资源在实际需要它们的小计划建立；不提前增加 DMA 或通用大缓冲区。

## 3. 小计划执行规则

### 3.1 粒度

1. **本文所有已列明的具体小计划只有一个目标**，低于用户允许的最多三个目标。一个目标可以包含不可分割的步骤，例如一次事务的写入、触发、完成、回收。C9-R 是尚未实例化的扩展模板；未来只有可完全隔离、独立选择并分别验收的同类资源，才允许在其一张实例卡中设置两到三个子目标。
2. 初始化、只读查询、一次门控、引用共享、分频、RCG、PLL、reset、GDSC、IRQ 迁移、缓存、睡眠恢复分别立项。
3. 每个功能样本固定到一个资源、一个算法类型或一个所有权切换。出现第二种 PLL／RCG／GDSC 变体，复制新小计划并赋独立编号，不能偷偷扩展原任务。
4. 提取现有代码和接管硬件分开；接管硬件和改变其算法分开。既有路径缺陷另建修复小计划，不能夹进迁移候选。
5. 小计划必须有可复查的行为或调查交付；不得只写“完善某控制器”“适配电源”“继续调试”。

### 3.2 验收等级与未知目标

- **调查任务**：交付完整表格、证据和逐项结论，可为“可实施／缺少依赖／本设备不适用”。这只完成调查，不代表相应硬件功能通过。
- **本地通过**：相关编译、fixture／故障／边界检查通过；只证明对应软件合同。
- **实机通过**：实际执行本卡指定路径，保存事件和结果。只有普通启动／没有异常，不能代替新功能触发证据。
- **受阻**：缺少安全资源、供电、消费者、独立完成证据或系统低功耗入口；记录具体缺项和解除检验。不得以 mock、跳过或 `return success` 改写为实机通过。
- **不适用**：必须有本设备资源表及来源证明，经记录从支持范围排除；不能只因难做而标不适用。

每卡的“实机检验”是未来实施时的验收要求；现在不要求重测 b32。纯描述／调查任务不为凑实机次数新增镜像。若尚不能确定能安全操作的资源，先完成 C0-03，不猜一个空闲 clock。

### 3.3 一次候选与失败归属

- 一个候选内核只改变一个正在验收的小计划的硬件生产行为（可合并的卡类型见 3.4）；基础已验提交可以作为前置。通常一个小计划一个实现提交；必要时允许准备提交，但不把未验行为叠加进下一卡。
- 首次硬件写入、硬件所有权转交及新状态转换，完成本地检查后分别交付实机。文档、只读审查和后续测试设计可以提前。
- 设备自检通过实际公开接口或明确的诊断入口发请求，记录 `planID / resource / consumer / token / generation / phase / result`。诊断开关必须显式选择，不能自动改动正常启动的资源。
- 至少记录请求已接受、实际执行、所需完成、最后结果。门控 OFF、RPMh ACK、GDSC 状态各按自己的语义判断，不互相替代。
- 前置探测未通过记 `blockedBy=<前置卡>`；本卡执行已开始后失败记本卡与具体 phase。日志中途丢失、镜像不匹配或路径未执行记“证据不足”，不猜故障阶段。
- 通用回归只判断是否退化。新目标能否通过，要看本卡的专属事件链；不能用“能进桌面”归因门控、PLL 或 IRQ 成功。

### 3.4 候选镜像合并规则

“一个候选只改变一个正在验收的小计划的生产行为”针对**硬件行为**。为避免几十轮刷机，按卡类型区分：

| 卡类型 | 例子 | 是否需要独立候选 |
|---|---|---|
| 纯调查／文档 | C0-02～05、C9-01、C9-05 | 不出镜像 |
| 纯本地合同（无新 MMIO 写、默认关闭） | C1-05、C4-05 本地部分、C8-01／02 | 可随下一个写卡候选一并打包，在该候选记录中单列，不单独上机 |
| 只读初始化／查询（写计数为零） | C1-01～04、C2-01／02、C3-01／02、C6-01／02 | 依赖链上的多张可合入一个候选，但每张卡有自己的事件与写计数为零证据 |
| 首次硬件写、所有权转交、新状态转换 | C2-03、C2-08、C3-04、C4-02、C5-02… | 必须独立候选，前一候选实机通过后才叠加 |

合并候选中任一卡失败，只记该卡；其他卡的证据按其自身事件链判定，不连带通过或失败。合并不得让未验写路径默认开启。

### 3.5 所有卡共享的默认约束

以下约束对所有卡默认生效，卡内不再逐条重复；卡内只写本卡特有的需求、例外和更严格的边界。

1. 未实现或前置未满足的操作返回 Unsupported／NotReady／明确拒绝，不返回成功；不以 mock、跳过或兼容回退冒充实机通过。
2. 不顺带修改本卡目标外的父源、频率、电源、reset、GDSC 或邻位；不运行 disable-unused；继承资源与 inherited／pinned 资源不因引用为零关闭。
3. 结果不确定时保留资源和所有权账本并报告 Uncertain，不自动重试、回滚或强行复位；confirmed 只随真实完成推进。
4. 只读卡写计数必须为零；“配置解码”不等于“测得频率”，本方票完成不等于远端 aggregate 状态。
5. 诊断入口需显式选择，默认启动不触发；日志字段见 3.3。
6. 每卡失败归属写为 `<卡号>/<资源ID> <phase>`；前置不成立记 blockedBy，证据缺失记证据不足。

### 3.6 证据、回退与现有工作

每卡记录：基线提交、候选提交、模式／样本／依赖、源码与构建树匹配、kernel／镜像 SHA、原始日志路径及新鲜度、检验结果、未覆盖项、下一步。设备日志沿用 [sweet-log 工具](../tools/sweet-log.py) 和 [构建指南](BUILD-GUIDE.md)。原始产物留工作区，不入 git。

公共回归选择已验基线中的启动／根卷、USB 日志、触摸、显示和普通 OFF/ON；只对可能受本卡影响的路径执行必要检查。用户负责 push、下载、sudo 和设备操作，agent 负责实现、本地验证、归档核对、打包及记录；每卡的 fastboot 操作依现有分工执行，不因写入本计划扩大设备操作权限。

继承中的 UFS、USB、显示资源不得因新框架注册、初始引用为零或退出而被关闭；不运行通用 disable-unused。未知执行结果时保留资源并报告 uncertain，不能用自动重试／回滚／旧路径回退掩盖失败。

当前普通实机基线为 b32；C08／PD4 等显示计划未完成的物理转换与状态重建不会因接口迁移自动完成。每项迁移必须保留原有边界，见 [状态](STATUS.md)、[显示完整驱动计划](PANEL-FULL-DRIVER-PLAN.md)及 [C08 范围](PANEL-LINEAGE-DISPLAY-CLIENT-CLOCKS.md)。

## 4. 阶段入口与依赖

阶段是能力分组，不是必须等待全阶段所有可选资源才能继续。**具体依赖以小计划卡为准**：例如无安全 reset 样本不阻塞已具备依赖的 RSC 只读初始化。阶段仍有未验卡时，不能笼统标成整个阶段通过。

| 阶段 | 结束时增加的能力 | 小计划入口 |
|---|---|---|
| C0 基线与试验资源 | 明确基线、现有 writer、消费者需求清单、控制器范围、可安全验证的具体样本 | [阶段0](clock-plan/00-baseline.md) |
| C1 统一入口 | 服务能初始化，后端能注册，外设能绑定只读句柄和依赖描述，公共需求账本可本地验证 | [阶段1](clock-plan/01-provider.md) |
| C2 最小 GCC | 从初始化、只读查询、debugcc 测频到一个本地门控／共享引用／voted／reset 样本 | [阶段2](clock-plan/02-gcc.md) |
| C3 apps RSC 公共通道 | 保留现有 IRQ 完成，先以启动时选定 owner 转交执行器，形成有界事务接口；在线交接另卡且非必需 | [阶段3](clock-plan/03-rsc.md) |
| C4 RPMh 资源客户端 | RPMhCC 单资源投票、别名聚合、受约束缓存，逐种迁移 VRM／ARC／BCM | [阶段4](clock-plan/04-rpmh-clients.md) |
| C5 频率与本地电源域 | 分步实现 GDSC、divider、RCG、PLL 和 corner／频率协调 | [阶段5](clock-plan/05-rate-and-power.md) |
| C6 显示资源迁移 | 按一个资源／使用组迁入 DISPCC；单独迁移 display RSC。写交接卡（C6-03 起）须满足第 4 节 C6 入口条件 | [阶段6](clock-plan/06-display.md) |
| C7 其他多媒体 CC | VIDEOCC、CAMCC、GPUCC 各自分步取得最小可验证能力；仅在 C0-04 有对应消费者时实施 | [阶段7](clock-plan/07-multimedia.md) |
| C8 状态集、PM 与恢复 | 分别验证 sleep／wake 缓存、flush、PM 连接及局部状态丢失重建 | [阶段8](clock-plan/08-pm-and-restore.md) |
| C9 覆盖扩展与收口 | 按资源组扩展清单，验证跨 CC 使用及交付边界 | [阶段9](clock-plan/09-coverage.md) |

**C6 入口条件**：显示计划（C08／PD4）仍在修改 `SweetMDSSClock.inc` 等时钟账本。C6-01／02 只读卡可随时进行；C6-03 起的写交接卡开始前须满足其一：(a) PD4 已收口；或 (b) 已记录一次 MDSS 时钟账本接口冻结（冻结提交、冻结的函数／账本字段清单、显示侧后续改动需先修订 C6 卡）。两条线不得在同一候选中同时改动同一时钟账本文件。

可并行的准备工作：C1／C2 本地框架与 C3 的只读／提取工作；C7 各 CC 的资料审查。首次实现不同时开启两条新的实机写路径。C6 的 display RSC 迁移不作为 apps RSC 基础供电投票的前置，避免电源／显示时钟依赖环。

## 5. 当前已有代码与参考

| 入口 | 当前可复用部分／不能提前推导的结论 |
|---|---|
| [SweetRPMh 接口](../dxb/xnu/iokit/Kernel/arm/SweetRPMh.h)及[实现](../dxb/xnu/iokit/Kernel/arm/SweetRPMh.cpp) | apps RSC、现有 IRQ 完成及 VRM／ARC／CN0；当前未等于通用 RPMhCC 或完整低功耗实现 |
| [显示 clock](../dxb/xnu/iokit/Kernel/arm/SweetMDSSClock.inc)及[core](../dxb/xnu/iokit/Kernel/arm/SweetMDSSCore.inc) | 本地租约、voted 分支和部分 GDSC／PLL／RCG 规则；迁移时不扩大冷初始化或最后释放权限 |
| [显示 NoC](../dxb/xnu/iokit/Kernel/arm/SweetMDSSNoC.inc) | display RSC／数据带宽已有独立 owner；不能全部改发 apps RSC |
| [LG GCC 参考](../../android_kernel_lge_sm8150/drivers/clk/qcom/gcc-sm8150.c)及[branch](../../android_kernel_lge_sm8150/drivers/clk/qcom/clk-branch.c) | 本地寄存器、voted 等语义；Linux 已列出的资源不等于 XNU 已接管或可随意关闭 |
| [LG TCS 驱动](../../android_kernel_lge_sm8150/drivers/mailbox/qcom-rpmh-mailbox.c)及[RPMh 事务](../../android_kernel_lge_sm8150/drivers/soc/qcom/rpmh.c) | 本树传输层位于 mailbox，不能套上游文件名和行号；只按已验证版本移植寄存器事实 |
| [SM8150 DTS](../../android_kernel_lge_sm8150/arch/arm64/boot/dts/qcom/sm8150.dtsi)及[GDSC](../../android_kernel_lge_sm8150/arch/arm64/boot/dts/qcom/sm8150-gdsc.dtsi) | apps／display 通道与分散在各 CC 的本地域；最终输入仍要与实际 ADT／硬件核对 |

## 6. 实施状态与下一步

**2026-11-25：0131 门控＋测频档——第三源站在描述这边，C2-03 全卡收口（补丁 `0238` 结账）**（见[阶段2](clock-plan/02-gcc.md#实机验收0131-门控测频档第三源站在描述这边pass)与[样本](clock-plan/C0-03-SAMPLES.md#s2普通-branch-gatec2-030405)）。`boot-16k-0131-gpioirq-panel-pipeline-clockgatemeasure.img`（构建期 `853c9dd5…`；载荷 `kernel.macho` `6a861360…`、`kernel.macho.adt` `c0ba7498…`，与 0130 的 ADT 逐字节相同）实机进入桌面；`logs/sweetlog-0131-0.bin`（`53543ac0…`，`total=1113159`）审计 `--measure --c2-03 --gate` **rc=0、`verdict=pass`**，且不需要 `--allow-baseline-drift` 或 `--gate-declared-xo4`：第三源 `cc1:0x4` 测得 `19199963` Hz，对 `decoded-hz=19200000` 差 −1 ppm，`gate_declaration_refutation=null`；门控行与 0130／0130-1 逐字相同（单条 store、`polls=0`、四邻位零变化、样本留到重启），账本 `token=2 holders=1`、`restores=5 mismatches=0`。至此描述与计数器互相验证，C2-03 两半（行为＋父源身份）都有实机证据，下一张卡是 C2-04（关钟）与 C2-05（共享）。

**2026-11-24：0130-1 门控＋测频档——门控成立，第三源的“名字父源”被计数器推翻（补丁 `0238`）**（见[阶段2](clock-plan/02-gcc.md#实机验收0130-1-门控测频档门控成立父源被计数器反驳)与[样本](clock-plan/C0-03-SAMPLES.md#s2普通-branch-gatec2-030405)）。镜像 `…-clockgatemeasure.img`（`fce71e03…`），环 `logs/sweetlog-0130-1.bin`（`80ebba44…`，`total=4194304`，环已写满）：门控侧与 0130-0 逐条相同（一条 `stage=gate-enable`、首读即 `enable=1 off=0 polls=0`、四个邻位零变化、单 token 无 `release`、屏幕正常），所以 **C2-03 的门控行为定案**；但同一轮把被门控打开的那一支接成第三测量源后读得 `cc1:0x4 mux=0x99 hz=19200036`，对描述表声明的 4800000 是 ×4.0000075（`BadArgument`，3000007 ppm）。同轮两个对照（`0x53` 差 −1 ppm、`0x9a` `hz=0`）证明偏差只能来自声明：厂商 `MUX_SRC_LIST` 把 `0x99` 映射到 `gcc_pdm_xo4_clk`，而 `0x33008` 的 branch 无 `.parent_names`、无分频字段，名字里的 `xo4` 不是电路；全系统唯一的 /4 是计数器时基 `0x43008`，公式里的 `4800000` 只是它的常量。`0238` 把 localID 4 的父源改为 XO、`kIOQcomGccGateHz=19200000`，宿主测试与环审计器（`MH2LM_GCC_DUMP_ROWS=1` 输出真实行、`selftest-gate-auditor.py` 同时跑 `pass` 与 `gate-parent-refuted` 两态）同步；0131 复验期望 `decoded-hz=19200000`、`|delta-ppm|<=10000`。

**2026-11-24：C2-03 分支门控 0130 实机验收通过（首条“为开钟而写钟”的路径闭合）**（见[阶段2](clock-plan/02-gcc.md#实机验收0130-门控档通过)）。`boot-16k-0130-gpioirq-panel-pipeline-clockgate.img`（`01d2edc0…`）实机进入桌面，`logs/sweetlog-0130-0.bin`（`e8f6870a…`，`total=1279649`，`flush_sequence=11`，未回绕）门控档审计 rc=0：36 条时钟行、零 `[WARN]`／`[ERROR]`。链为 `phase=gate clock-measure=0 clock-gate=1` → `request enable=0 off=1 value=0x80000000`（与 0126 快照同值，说明没有别的路径动过它）→ 账本 `acquire`（`cc1:0x4`，`transition=new`，`holders=1`）→ `transition owner=1` → `write-issued stage=gate-enable write=1 value=0x80000001` → `running enable=1 off=0 value=0x1 polls=0`（**首次读回即运行，未用任何延时**）→ `confirm confirmed=1` → `gate-check neighbours=4 changed=0` → `completed detail=sample-kept-until-reboot` → `gate-done writes=1 changed=0 hold=1`，无 `release`；收尾 `resource=cc1 phase=start … reads=29 writes=1 variant=0x8150`。快照 11 值与 0126 逐字节相同；第 7 个描述符实测 `cc1:0x7 detail=configured known=1 raw=0x80000000`（`raw` 即 `0x143008` 快照值）。刷写前用 `scratch/artifact-0128/read-adt-claims.py` 从镜像 ramdisk 里读出 ADT（`e2996bda…`，与打包钉死的门控树逐字节相同）并确认 `sweet,clock-gate=1`、无 `clock-measure`／`clock-diag`，把“刷进去的到底是什么”变成开机前可回答的问题。基线：`SWEET FB SCANOUT` ×3 与 b32 相同，`SWEET USB` ×13 vs 2 属会话漂移（记入 `baseline_drift`，用 `--allow-baseline-drift` 记录而非放行），面板标记齐全、`touch_armed=2`（b32 为 4）。审计器同期修正一处自造算术：`polls` 是轮询循环跳出下标，首读成功时为 0 但读过一次，故读次数恒等式为 `reads = 29 + polls`（原写 `28 + polls`，被设备实测 `reads=29 polls=0` 当场抓出）；宿主 fixture 改为 `polls=1 → reads=30` 并 8/8 矩阵重跑通过。下一步：门控＋测频档（第三源 `mux=0x99`）与默认档回归两次刷写，然后进入 **C2-04（关钟）**。

**2026-11-24：C2-03 分支门控实现（补丁 0237），随即在 0130 门控档验收通过（见上条）**（见[阶段2](clock-plan/02-gcc.md#本候选实现c2-03-分支门控身份修正补丁-0237)与[样本](clock-plan/C0-03-SAMPLES.md#s2普通-branch-gatec2-030405)）。这是首次“为了开钟而写钟”：C2-08 的 23 次写只为测量服务、结束即全部还原，C2-03 要把 `gcc_pdm_xo4_clk`（localID 4，CBCR `0x133008`）真的打开并留着。顺带修身份：`0x143008` 不是 PDM XO/4，而是 `gcc_xo_div4_clk`（localID 7）——0127／0128 的测量参考一直是它（写 enable 后读回 `0x1`），PDM 的 CBCR 在 `0x33008`。实现走公共路径而非另开捷径：`gateClocks()` 读原状态（`phase=request enable=0 off=1 value=0x80000000`）→ 经 C1-05 账本取得**自己的** token（consumer `sweetclock-gate`，`cc1:0x4`，`transition=new`）→ 只在“自己持有且未使能”时用同一个 `writeRegister` 写一次 `value|ENABLE`（`value=0x80000001`，`dmb oshst`→store→`dsb osh`）→ **判据是硬件 halt 位**（`CLK_OFF` 清 0，`2500×10 µs` 有界，`polls` 可对账），不是“写进去了就当成功” → 把同页三个 PDM 邻位与 `0x10b048`（显示 CC 的 `gcc_disp_xo_clk`）跟开启前的捕获逐条比对（`neighbours=4 changed=0`；有邻位动了就 `neighbour-changed` 并带 `before/after/mask`）→ `ledgerComplete(token,true)` → `phase=completed detail=sample-kept-until-reboot`，**不写回**、凭据保持。拒绝路径都在写之前收手且零写入：`already-enabled`、`halt-not-set`（未使能却已“运行”＝状态不可信）、`ledger-refused`；halt 超时则保留已写状态并 `ledgerComplete(token,false)`，`WARN detail=gate-incomplete` 但后端照常 Ready。ADT 侧新增 `--clock-gate`（隐含 `--gcc`），只追加一条 4 字节 `sweet,clock-gate=1`：门控树 `e2996bda…`（23608 字节）、门控＋测频树 `c0ba7498…`（23648 字节），默认／测量／诊断三棵树逐字节不变；两条属性各自可选，合成树去掉门即逐字节等于测量树。改参数表行不通（605/607 字节），所以走设备树属性这条路是唯一选择。主机证据：GCC fixture 门控档 1 次 store／0 restore／`hold=1`／单 token，两次拒绝后 `ledgerInspect` 为 `NotFound`（拒绝发生在账本建立之前），halt 超时保留状态且 `pending=1`，门控样本成为计数器**第三源**（`mux=0x99`，`decoded-hz=4800000`，模型 `4799853`），门控＋测频 31 次 store／5 次恢复／`mismatches=0`，默认档零写；ADT 测试两条属性各自可选且可还原；补丁 `0237` 在基线严格重放逐字节一致（含生成器与两份主机测试，`git apply -p1`）。`out-bs/mk16k-0130.sh` 已备四档（默认／测频／门控／门控＋测频，同一内核，只差 ADT 属性），审计脚本已支持 `--c2-03 [--gate]` 且对 0127／0128／0129 旧环保持逐字节相同的判定。0130 的验收点：门控档出现完整 `request→write-issued→running→gate-check→completed→gate-done`（`writes=1 neighbours=4 changed=0 hold=1`，账本无 `release`），门控＋测频档多一条 `mux=0x99` 的第三源且两条 `confirm` 只有一条 `release`，默认档比 0129 多一行 `cc1:0x7` 的 `decode` 且仍旧零写。

**2026-11-24：0128 测量档复验掩码判据，C2-08 以“真机写—测—恢复—归还”四件事全部成立收口**（见[阶段2](clock-plan/02-gcc.md#0128-实机结果掩码判据成立c2-08-收口2026-11-24)）。`boot-16k-0128-gpioirq-panel-pipeline-clockmeasure.img`（`b5cabf20…`，内核 `21f99b5e…`）进桌面，`logs/sweetlog-0128-0.bin`（`00570b16…`，`total=1008528`，`flush_sequence=8`）：64 条 `SWEET CLOCK`、**0 条时钟 WARN**、0 条 ERROR。0127 那两条 `[WARN]` 都消失了：`measure-done result=0 writes=23 restores=5 mismatches=0 hold=0`，环里没有 `detail=measure-incomplete`，`start` 回到单条 INFO（`reads=1507 writes=23`）。关键证据是同一份环里两条恢复行落在掩码判据的两个方向：`gcc:0x162008 expected=0x80000000 value=0 mask=0xfffffff result=0`（读回**仍是 0**，halt 位滞后的现象与 0127 一模一样）与 `gcc:0x143008 expected=0x80000000 value=0x80000000 mask=0xfffffff result=0`（halt 已清），另三条 `0x162038`／`0x162004`／`0x162000` 仍按 `0xffffffff` 整寄存器比对——放行的是状态位，不是把核对放宽。测量链路复现且更好：`short=16398 full=262157 hz=19200036`（与解码 `19200000` 差 `+1 ppm`，0127 是 `-1 ppm`，两次符号相反、都在 1 ppm 内，印证“不钉计数、只要同量级／容差内／符号正确”）；关闭样本 `short=full=262157 hz=0 running=0` 无 `measure-compare`；账本 `transitions=2 confirmations=2 holders=0`、`release_value=0x1` 与前次一致。非时钟清单仍是面板 ×3 与 USB `ep0 cmd 0x6 status 1`×5（b32 为 2，属主机侧枚举重试，审计以 `--allow-baseline-drift` 记录次数漂移、ERROR 为零）。同一内核的默认档（`7f3a96bd…`，只把 ADT 换回不含门的 `081f63dc…`）复验门关闭零写入：`logs/sweetlog-0129-0.bin`（`860d1346…`、`total=1200369`、`flush_sequence=10`）25 条 `SWEET CLOCK`、0 WARN／0 ERROR，相位只有只读那六种，`writes=0`、非时钟清单与 b32 逐项相等（无次数漂移）。门关闭时“写路径存在但不写”与“测量正确”是两件事，两条互补，C2-08 整卡收口。


**2026-11-24：0127 测量档实机跑通 C2-08 测量链路（19.2 MHz／−1 ppm），恢复核验判据修正为“只比配置位”（补丁 0236），待 0128 复验**（见[阶段2](clock-plan/02-gcc.md#0127-实机结果测量链路通过恢复核验判据有缺陷2026-11-24)与[阶段1](clock-plan/01-provider.md#c1-05-建立公共多消费者需求账本)）。`boot-16k-0127-gpioirq-panel-pipeline-clockmeasure.img`（`3db09549…`）实机进入桌面，`logs/sweetlog-0127-0.bin`（`c4142395…`，`total=1098038`，`flush_sequence=9`）是本次会话：65 条 `SWEET CLOCK`、2 条 WARN、0 条 ERROR。运行样本 `mux=0x53`（`gcc_disp_xo_clk`）`short=16398 full=262156 polls=9/671 hz=19199963 running=1`，与 C2-02 解码的 `19200000` 差 `-1 ppm`（容差 10000）——主机模型给的 `19199084`／`-47 ppm` 只是另一组计数，本卡不钉具体计数；关闭样本 `mux=0x9a`（`gcc_pdm2_clk`）短测＝长测（`262156`）⇒ `hz=0 running=0` 且无 `measure-compare`，正是厂商注释的“未运行时计数器不归零”；23 次有序写、5 次恢复、`reads=1515`、`windows=4 snapshot=11 variant=0x8150`。C1-05 第一次有真实执行者：`acquire token=1 transition=new` → `transition owner=1` → `running enable=1 off=0 value=0x1` → `confirm confirmations=2` → `release holders=0 pending=0 confirmed=0 transitions=2 confirmations=2`，`release_value=0x1` 即释放判决时参考源仍在跑、随后被逆序恢复写回 `0x80000000`。**缺陷**：`measureRestore` 按整寄存器比对，而分支 CBCR 的 bit31..28 是 FSM 的 OFF/NOC halt 状态（厂商 `clk-branch.c` 只轮询 `clk_branch_wait`，从不写回），门开后要几拍才更新，于是 `gcc:0x162008` 写 `0x80000000` 读回 `0x0` 被记成 mismatch（`expected=0x80000000 value=0`），连带 `phase=measure-done result=-536870198 writes=23 restores=5 mismatches=1 hold=0` 与 `WARN phase=start … detail=measure-incomplete measure-result=-536870198`；store 本身成功（`write=10 debug-gate value=0x80000001`），`0x143008` 同刻读回 `0x1` 是反方向例证。`0236` 把核对收窄为配置位：`0x0fffffff` 用于两个分支 CBCR，整寄存器用于 mux／post-div／计数控制，真实漂移仍被拒；写路径、顺序、屏障不变。主机测试补了两个设备模型用例（滞后 halt 位被掩码放行、真配置漂移被拒），审计脚本加 `--legacy-restore` 模式复现修复前日志并要求那条假 mismatch 必须存在。`0235`＋`0236` 在基线 `d177551` 严格重放逐字节一致。下一步：0128 两档（默认档仍应 `writes=0`，测量档验 `mask=0xfffffff` 恢复行、`measure-done result=0 mismatches=0`、无 `measure-incomplete`）。

**2026-11-24：首个硬件写候选 C2-08 测频＋C1-05 需求账本实现完成（补丁 0235），待 0127 实机**（见[阶段2](clock-plan/02-gcc.md#本候选实现c2-08-测频c1-05-需求账本补丁-0235)与[阶段1](clock-plan/01-provider.md#c1-05-建立公共多消费者需求账本)）。`IOQcomGCC` 增加了第一条写路径（`dmb oshst`→单次 store→`dsb osh`，对齐与越界拒绝，`registerWrites()` 不再是常量 0），并按 LG `clk-debug.c` 实现 debugcc 测频：保存 5 个寄存器 → 经 C1-05 账本取得 XO/4 参考源（`cc1:0x4`，只有打开需求的那个持有者写 enable）→ 按厂商顺序选 mux／写 post-div／开 debug 分支 → 短测 `0x1000` 与长测 `0x10000`（两次计数各三次写、等待 bit25 均有界）→ 逆序恢复并逐条读回核对 → 以“XO/4 的 `ENABLE` 已回 0”判决 `ledgerRelease(token,stopped)`。运行样本 `mux=0x53`（`gcc_disp_xo_clk`）预期 `hz=19199084`，与 C2-02 解出的 `19200000` 差 `-47 ppm`（容差 10000 ppm），这正是“配置解码”与“实测”的分界证据；关闭样本 `mux=0x9A`（`gcc_pdm2_clk`）短测＝长测即 `hz=0 running=0`（NotRunning，不是 0 Hz 的测量值）。C1-05 在 `IOQcomClockProvider` 内落地固定账本（8 条 × 4 持有者，键 `(controller,localID)`，`desired／pending／confirmed` ＋ `generation／transitions／confirmations`）：首个取得才推进 `transitions`、第二个消费者只加持有者、`ledgerComplete` 是 verdict 而非猜、最后释放时硬件没停则保留 `pending` 并 `ERROR detail=hardware-still-on`、generation 变化清 token 但保留计数、撤回使条目失活。测量入口由设备树 `sweet,clock-measure=<u32 1>` 门控（生成器 `--clock-measure`，隐含 `--gcc`；默认树仍 `081f63dc…` 且仍 `writes=0`，加门为 `0fc24851…`）：缺失即 `Unsupported`＋DEBUG（默认镜像行数与 0126 一致）；坏长度／坏值记 `ERROR detail=bad-clock-measure-gate`；测量失败时后端照常 Ready，只补一条 `WARN detail=measure-incomplete`，不让测频失败拖垮只读后端。主机测试三份通过（账本多消费者／欠账／过期／上限，GCC 默认门缺失 0 次 store 与门在 23 次有序 store／`hz=19199084`／`delta-ppm=-47`／5 次恢复 0 mismatch／`reference-not-running`／计数卡死 2×`done-timeout`，ADT 门可选且逐字节可还原），补丁 `0235` 在基线严格重放逐字节一致。下一步：打包 0127 两档（默认档应保持 25 行零写入，测量档待设备验证上述行），用户烧录后回导日志审计。

**2026-11-22：0126 实机进入桌面，C2-01／C2-02 收口（0234 的地址模型在真机成立）**（见[阶段2](clock-plan/02-gcc.md#实机验收0126-默认档通过)）。`boot-16k-0126-gpioirq-panel-pipeline.img`（`fae0eed6…`）实机启动进入桌面，`logs/sweetlog-0126-0.bin`（`3d1cd662…`，`total=1366678`，`flush_sequence=12`，未回绕）默认档审计通过：四条 `phase=map result=0 length=0x1000` 且 `resource-offset=0x100000` 自洽（`0x10b000/0x133000/0x143000/0x162000`，偏移 `0xb000/0x33000/0x43000/0x62000`）、11 条 `phase=snapshot result=0 value=…`（实测 `0x80000003`／`0x4221`／`0x1`／`0x80000000`×3／`0x100000`／`0x80000000`／`0x14b`／`0`／`0x80000000`）、`resource=cc1 phase=start … windows=4 snapshot=11 reads=18 writes=0 variant=0x8150`、六条 `phase=decode`（`cc1:0x1`…`cc1:0x6`）与一条 `decode-sample`。C2-02 实测 `cc1:0x5`＝`gcc_pdm2_clk_src` 解出 `detail=configured sel=0 div=0 div2=0 hz=19200000 known=1`（CFG `0x100000`：`SRC_SEL=0` 选 XO、`hid_div=0` 未编码分频），这只是配置解码，不是物理频率实测。零 MMIO 写入、零时钟 `[WARN]`／`[ERROR]`，非时钟 WARN／ERROR 清单与面板标记不超 b32 基线。已知限制：`cc1:0x6`（`gcc_pdm2_clk`）父源是本 CC 的 `cc1:0x5`，单遍解码只认固定源，如实 `parent-rate-unknown known=0`，跨行速率解析留待后续卡。审计脚本同期修正两处与后端实际不符的期望（快照成功也逐行打印、`phase=decode` 用设备资源名 `cc1:0xN` 而非物理地址），并把每条 decode 的 `raw` 与对应快照值逐字节比对。下一步按[候选镜像合并规则](#34-候选镜像合并规则)进入**首个硬件写候选 C2-08（debugcc 测频）**，它在 C2-03 之前，使门控／频率卡先取得实测证据；C1-05 仍并入第一个写候选。

**2026-11-22：0125 实机失败定位为 GCC 窗口地址缺陷（补丁 0234）**（见[阶段2](clock-plan/02-gcc.md)）。0125 内核（`d4465d32…`）的默认档与 gpioirq 档各刷一次都进 `bsp-kernel`（厂家 watchdog），导出的 `sweetlog-0125-0.bin` 与 0124 的环**SHA256 相同**（挂在首次 UFS 落盘之前，环里留的是上一次会话的内容），因此那两份日志不能用来判断 C2 走到哪一步；屏上回滚只有约 40 行，C1 行在环第 145 行附近，看不到它也不构成定位依据。根因在地址模型：`sm8150.dtsi` 把 GCC 声明成 `reg = <0x100000 0x1f0000>`，LG 表里的寄存器号（`0x33008`、`0x62000`…）是这个块内的偏移，物理地址 = `0x00100000 + 偏移`；生成器把三个窗口写成了裸偏移 `0x0033000`／`0x0043000`／`0x0062000`，于是后端映射并采样与时钟表无关的低地址页，最可能是 NOC／XPU 静默复位（没有 panic，屏上直接落到 `bsp-kernel`）。`0234` 把窗口改成 `0x00133000`／`0x00143000`／`0x00162000`，并让每条 `phase=map` 同时打出 `offset=%#llx`，使设备日志可与厂商表直接对账；主机测试把四个 `offset=` 钉进断言，审计器的期望表也从偏移迁到 PA 并新增 `resource - offset == 0x100000` 检查。`0230`→`0234` 在基线 `4148ed3` 上严格重放逐字节一致（6/6）。对照镜像 `boot-16k-0125-kernel-c1only-adt.img`（`22999be4…`，0125 内核 + 无 `gcc` 节点的 C1-only ADT `6a50db55…`）用来把内核回归与 C2 缺陷分开；`out-bs/mk16k-0126.sh` 已备（ADT 门 `081f63dc…`／诊断 `8d6f2adb…`，`reg` 校验读生成器常量因而自动跟随修正）。0126 默认档的实机结果见本节最新一段（已通过）；首个硬件写仍是 C2-08。

**2026-11-22：C2-01／C2-02 只读 GCC 后端实现完成（补丁 0233）**（见[阶段2](clock-plan/02-gcc.md)）。`IOQcomGCC : IOQcomClockController` 匹配 `sweet,gcc`（ADT 由 `--gcc` 追加独立节点），按 `reg` 映射四页 GCC 窗口（`0x10b000`／`0x33000`／`0x43000`／`0x62000`），首次读取前核对解析出的地址与长度，身份只认数值 `sweet,gcc-abi=1` 与 `sweet,gcc-variant=0x8150`；寄存器访问是带 `dmb oshld` 与对齐／越界检查的窄只读助手，文件内没有写路径，`registerWrites()` 是常量 0。C2-02 只解码 `gcc_pdm2_clk_src` 一种固定父源／分频配置：选择子按 `gcc_parent_map_0` 解码，`div=0` 按 `clk_rcg2_calc_rate` 的分支当作“未编码分频”而不是除数 1，无可信父频、`CMD_UPDATE` 置位、非法选择子或非零 mode 分别报 `parent-rate-unknown`／`update-pending`／`src-invalid`／`mode-unsupported`，绝不以请求值代替硬件配置。本候选顺带修正两处既有缺陷：C1 绑定表上限 8→16（诊断段原本会把每个探测都判成畸形，把后端藏起来）与 GCC `stop` 未释放映射／锁（重启永远 `already-started`）。`test-mh2lm-gcc-backend.py`、`test-mh2lm-clock-provider.py`、`test-mh2lm-mdss-adt.py` 通过，两份 ARM64 单 TU 编译 rc=0，补丁 `0233`（SHA256 `b92d455222eb80bbe12f67b1940a7565f41123815b1cada8f8a1f77acb487649`）在基线 `4148ed3` 上按 `0230`→`0233` 严格重放逐字节一致。C1-05 按 [§3.4](#34-候选镜像合并规则) 并入下一个写候选；下一个实机点是 0125 内核（只读，零写入），首个硬件写仍是 C2-08。

**2026-11-22：C1-01～C1-04 只读候选完成**（见[阶段1](clock-plan/01-provider.md)）。统一入口 `IOQcomClockProvider : IOService` 匹配 `sweet,clock-provider`，ADT 节点由 `mk-mh2lm-mdss-adt.py --clock-provider` 可选追加（无窗口、无子节点，默认派生字节不变）。本候选建立了控制器注册／撤回、generation、设备资源名到 `(controller, local ID)` 的只读绑定、父源／电源依赖描述与明确诊断入口；所有硬件操作返回 Unsupported，admission 先于能力判定。`tools/tests/test-mh2lm-clock-provider.py` 与 `test-mh2lm-mdss-adt.py` 通过，ARM64 单 TU 编译 rc=0，补丁 `0230`（SHA256 `3fc2536659099605f98366ea05e4eab092c7c5a2decf5a90eaa88a32102e428f`）在基线严格应用后逐字节一致。C1-05 需求账本与任何 MMIO 写入都不在本候选；实机只验匹配／Ready／绑定／写计数零，CI 与刷机步骤属于用户。

**2026-11-22：0232 实机复验通过，C1 收口**。0124 内核（`4dd10add94b2cf1e861c23b41d4e79f74ee31f7f581c842897d66cd05016aad8`）的诊断镜像实机一次：时钟行 18→16，`phase=admit` 计数归零，两个被拒句柄各只剩一条 `phase=enable result=-536870184 detail=parent-controller-absent`／`domain-absent`，四行账本与 0123 逐字段一致，收尾仍是 `detail=zero-writes`，零 ERROR，非时钟基线与 b32 一致（`logs/sweetlog-0124-0.bin`，`total=999833` 未绕环）。C1-01～C1-04 至此完成本地、编译、补丁重放与实机四层验收，写计数为零；下一个实施点是只读候选 C2-01／C2-02（不新开实机写路径）；C1-05 按 §3.4 并入下一个写候选，首个硬件写仍是 C2-08。

**2026-11-22：C1 两档实机通过，补丁 0232 收敛拒绝日志**（见[阶段1](clock-plan/01-provider.md#实机证据0123-内核两档各一次)）。0123 内核（`4a054ef0efc9a5bd8d3380a53848961fddbf6cc682c8c9b5cffbff714a25bace`）三份镜像各刷一次：默认档只剩入口行且 `bindings=0`（0231 的如实计数在真机兑现，0122 同位置打的是容量常量 8），无任何绑定／使能／诊断行，时钟 WARN／ERROR 为零；诊断档（`sweet,clock-diag` 属性）18 条时钟行完整走完注册、四个只读句柄绑定、四行账本与 `detail=zero-writes` 收尾，`diag-core`／`diag-child` 查询可用而 `enable=-536870201`（Unsupported），`diag-orphan`／`diag-domain` 分别报 `deps=-536870184 depReason=parent-controller-absent`／`domain-absent` 且 `enable` 等于 `deps`——依赖未满足没有被误报成后端不支持，四个句柄 `info=0 state=0`，无一次写。两档的非时钟 WARN／ERROR 清单与 b32 一致。唯一缺陷是同一失败被 `checkDependencies` 与 `enable` 各记一条**逐字节相同**的 `phase=admit` WARN（每个失败句柄两条），补丁 `0232`（SHA256 `0d3fdf8814312413db556e714048169842e70c1daf88f644d090ab20e20c2db9`）把记账权交给实际报告失败的那一方：遍历加静音开关，句柄 `enable` 打出唯一一条 `phase=enable result=... detail=...`，诊断探测只把原因写进 `depReason` 字段。在基线 `4148ed3` 上 `0230`→`0231`→`0232` 严格应用后四份文件仍逐字节一致，主机测试新增“静音遍历不丢账、显式查询才记账”的断言并通过。

**2026-11-22：C1 实机默认档通过，补丁 0231 收尾**（见[阶段1](clock-plan/01-provider.md)）。0122 内核（`5139febeb5cbfd4891df1ce499111a6ffe8ca4a3f56b6678dd8ebc7c8828a6ac`）的 gpioirq 镜像实机启动成功：`planID=C1 phase=start result=0 detail=entry-ready`，除该行外无任何时钟日志（无 bind／enable／disable／admit／注册／撤回），时钟 WARN／ERROR 均为 0；因未注册控制器、节点无窗口，本候选不存在 MMIO 写路径；面板侧 WARN／ERROR 清单与 b32 完全一致（五条既有 WARN、零 ERROR、handoff／scanout 各 1）。同一轮查出两处缺陷：入口行 `bindings=` 打印容量常量（实机 `bindings=8`，而节点不声明绑定，计划期望 `0`）；`sweetclock=1` 诊断镜像因参数 618 字节被固件以 `boot args rejected at 607` 拒绝、内核未进入。0231 如实统计节点 `sweet,clock-names` 条数，并把诊断开关移到 `sweet,clock-diag=<u32 1>` 设备树属性（参数保持 605 字节），`mk16k-0123.sh` 对每份镜像硬性校验参数 ≤607 字节。诊断段（`consumer=sweetclock-diag`、四条 `phase=diag`、`detail=zero-writes` 收尾）待 0123 内核实机复验。

**2026-11-22：C0 五张调查卡完成**（见[阶段0](clock-plan/00-baseline.md)）。C0-04 将 C2-06／07、C4-01～06、C7、C8-01～06 标为暂缓（无需求）。下一候选为只读合并候选 C1-01～04 + C2-01／02（写计数为零，含 C0-03 所需快照），之后是首个硬件写候选 C2-08。以下为 C0 时的原始说明（C0 完成时所有卡状态均为“未开始”，本文不把上一轮架构讨论当成 C0 验收）：第一个实施点是 **C0-01 基线锁定**，之后 C0-02 资源所有权表、C0-04 消费者需求清单、C0-05 控制器范围判定，再做 C0-03 安全样本调查（样本优先从 C0-04 清单内选）。完成调查前不开始新硬件写入。建议首个硬件写候选为 C2-08 debugcc 测频，使后续门控／频率卡能取得实测证据。

后续每次更新在相应卡下追加该卡的结果链接和证据等级，并同步本节；不要把全体卡预填为完成。若依赖或样本变化，先修订卡片，再实现，保持编号稳定。
