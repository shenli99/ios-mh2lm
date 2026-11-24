# C1：统一时钟入口与只读资源接口

[返回总计划](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)。各卡默认遵守[总计划 3.5 共享约束](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)，卡内只写特有边界。状态：C1-01～C1-04 已实现为只读候选（补丁 `0230`／`0231`／`0232`，写计数为零），默认档与诊断档均已实机通过；**C1-05 已实现（补丁 `0235`，与 C2-08 合并为第一个写候选），并已在 0127、0128 两档测量镜像里各走完一次真实的“取得—确认—释放”（两次都是 `transitions=2 confirmations=2 holders=0`，无欠账）**；其硬件共享实例仍待 C2-05／C4-04。

## 阶段目标与退出条件

能初始化服务、注册后端并绑定句柄；所有尚未支持的硬件操作明确拒绝。注册和查询不会触发时钟、电源或 reset 写入。本阶段不要求任一控制器功能齐全。

### C1-01 建立统一 provider 的服务生命周期

- **目标**：使 IOQcomClockProvider 能匹配、初始化并有序退出。
- **前置**：C0-01。
- **需求与边界**：只建立入口服务、必要锁／事件资源和 capability 查询；不等待所有 CC 就绪，不映射各控制器窗口。
- **本地检验**：编译；初始化失败逐点释放；重复 start／stop、通知撤销及未初始化调用均有确定结果。
- **实机检验**：记录一次匹配、Ready 与无写操作计数；正常启动基线无退化。真实热卸载若无支持，只保留 host 退出证据。
- **预期结果**：统一入口存在、能力表为空或全未支持；缺少后端不会造成启动等待环。
- **失败归属**：C1-01 match／allocate／publish／stop；不得归因尚不存在的 GCC 功能。

### C1-02 建立 CC 后端注册合同

- **目标**：让一个无硬件行为的后端通过公共控制器接口注册／撤回。
- **前置**：C1-01。
- **需求与边界**：定义 IOQcomClockController 的身份、资源表和 capability 注册；先使用零资源／诊断后端。注册关系与实际 nub 资源来源分别保存；不创建每 clock 一个 IOService。
- **本地检验**：重复 controller ID 拒绝；撤回后的查询拒绝；保留对象直到在用查询结束。确认入口未持自身 gate 阻塞调用后端。
- **实机检验**：诊断后端按固定 ID 注册且可查询；该阶段所有硬件操作仍拒绝。
- **预期结果**：得到可复用后端边界；后续增加 GCC 不需要改外设侧接口。
- **失败归属**：C1-02 register／collision／withdraw；硬件身份验证属于各 CC 初始化卡。

### C1-03 实现设备资源名到时钟句柄的只读绑定

- **目标**：把一个设备资源名稳定解析到 `(controller, local ID)` 句柄。
- **前置**：C1-02；资源描述有明确来源。
- **需求与边界**：定义 acquire／release、consumer 身份和后端有效期；先允许查询与 capability，不开启时钟。DT／ADT 编码若需新增，只增加这一绑定所需字段并单独记录 ABI。
- **本地检验**：合法名、缺失名、无效 ID、后端尚未就绪、后端撤回和句柄重用；不同 CC 相同 local ID 不能串线。
- **实机检验**：一个诊断消费者取得并释放已声明的句柄；记录解析三元组，硬件写计数仍为零。
- **预期结果**：外设不传物理地址；绑定失败可区分资源不存在与 provider 未就绪。
- **失败归属**：C1-03 parse／resolve／lifetime；本卡不验收频率或门控。

### C1-04 发布父源和电源依赖的只读描述

- **目标**：使一个 clock 的依赖可以查询，并能明确表达尚未满足的依赖。
- **前置**：C1-03、C0-02。
- **需求与边界**：分别记录 possible／selected clock parent、register-access 依赖、required domains、rate-to-corner 及继承状态。只声明，不注册 IOPM 控制策略，不自动供电。
- **本地检验**：缺父源、缺电源 provider、环形描述及同 CC 多域用例；依赖不满足时执行接口拒绝。
- **实机检验**：查询首批样本，依赖与 C0 样本卡一致；对不存在的依赖作无写入拒绝验证。
- **预期结果**：能区分服务挂载、时钟父源、电源要求；后续只实现指定依赖操作即可扩展。
- **失败归属**：C1-04 descriptor／cycle／admission；真实上电留给 C5。

### C1-05 建立公共多消费者需求账本

- **目标**：对同一逻辑资源独立保存两个消费者的需求及有效聚合边界。
- **前置**：C1-03；此卡不依赖 GCC 或 RPMh 的实机写样本。
- **需求与边界**：只实现身份、引用及 desired／pending／confirmed 的公共账本合同；不引入各资源专属聚合公式，不声称逻辑取得等于硬件开启。生产硬件操作在后端未验时仍拒绝。
- **本地检验**：A/B 取得、A 释放、B 最后释放、重复／过期句柄、确定性交错及失败不推进 confirmed；通过隔离 fixture 检查转换意图，不能返回伪硬件完成。
- **实机检验**：需要时用无 MMIO 的明确诊断账本查询两个消费者的逻辑状态；无新生产硬件行为时本地验收即可，不强制新增镜像，也不登记硬件共享已验。
- **预期结果**：公共账本合同本地通过，C2-05 和 C4-04 分别负责 GCC／RPMh 的真实硬件共享实例，互不作为对方前置。
- **失败归属**：C1-05 identity／ledger／transition-intent；实际 gate 或投票执行归对应后端卡。

- **本候选实施记录（补丁 0235，与 C2-08 合并）**：0233 决定按[总计划 §3.4](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#34-候选镜像合并规则)把它并入下一个**写入**候选——C2-08 是首个真实写，而它要开的 XO/4 参考源正好需要一个能“取得—确认—释放”的公共需求账本，于是账本在这个候选里有了第一台真实执行者，而不是一台没有执行者的状态机。C1-05 的合同本体与 GCC 硬件无关，GCC 的真实硬件共享仍由 C2-05 单独验收，两者互不作为对方前置。
- **C1-05 条目与身份**：`IOQcomClockProvider` 内固定表 `fLedger[kIOQcomLedgerEntries=8]`，键 `(controller,localID)`；每条最多 `kIOQcomLedgerHolders=4` 个 token，字段为 `holders／desired／pending／confirmed` 与 `generation／transitions／confirmations`，`active` 表示条目曾建立。`token` 全局递增、从不签发 `0`（回绕时跳过在用值）。`controller` 必须落在 `1..kIOQcomCCMax`，空 consumer 与空 token 指针一律 `BadArgument`；provider 未挂载／未 Ready／正在 stop／后端未注册 → `NotReady`；表满或 token 位满 → `NoResources`。
- **C1-05 转换边界**：只有“首个取得”推进 `transitions`——`desired=1,pending=1,transitions++`，返回 `openedTransition=true`，日志 `transition=new`；同一资源的第二个消费者只增 `holders`，日志 `transition=shared`，**物理转换不发生**。`ledgerComplete(token,true)` 才置 `pending=0,confirmed=1,confirmations++`；`ledgerComplete(token,false)` 同样清 `pending`，但记 `WARN detail=not-confirmed` 并返回 `NotReady`——确认是判决，不是猜。
- **C1-05 撤回与欠账**：最后一个持有者 `ledgerRelease(token,stopped)` 在 `stopped=true` 时清 `pending／confirmed`、`transitions++`／`confirmations++`；`stopped=false` 时**保留** `pending=1` 并记 `ERROR detail=hardware-still-on`（`IOError`），把“硬件其实还开着”留下来而不是抹平。非最后持有者只减 `holders`。
- **C1-05 过期与清理**：后端 `start` 每次生成新 `generation`；取得时发现 `entry-generation != generation` 就清 token／holders／desired／pending／confirmed（**保留** `transitions／confirmations`）并记 `WARN detail=stale-generation entry-generation=%llu generation=%llu`。未知 token（含已过期、已撤回、来自别的条目）→ `WARN phase=confirm|release result=10 detail=unknown-token`（`kIOReturnNotFound`）。`withdrawController` 调 `ledgerForget` 使条目失活，此后按 key 或旧 token 查询都是 `NotFound`。
- **C1-05 边界**：不引入任何资源专属聚合公式（GCC 的门控聚合仍归 C2-05，RPMh 投票归 C4-04），不声称逻辑取得等于硬件开启；账本本身不写任何寄存器。C2-08 只把它当“参考源需求”的持有者与停钟判决的依据。
- **C1-05 实机记录（0127 测量档，`sweetlog-0127-0.bin`）**：C2-08 每次测量都向账本取得 `cc1:0x4`（`gcc_pdm_xo4_clk`），因此这一份环第一次出现了完整的账本四行：`consumer=sweetclock-measure phase=acquire result=0 token=1 holders=1 desired=1 pending=1 confirmed=0 transitions=1 transition=new` → `phase=transition result=0 token=1 owner=1` → `phase=confirm result=0 token=1 holders=1 pending=0 confirmed=1 confirmations=2` → `phase=release result=0 token=1 holders=0 desired=0 pending=0 confirmed=0 transitions=2 confirmations=2`。要点有三个：`transition=new`（只有一个持有者，物理转换真的发生了一次）、`confirmations=2`（取得之后按“XO/4 的 `enable=1` 且 halt 位为 0”确认了一次，释放之后按“`ENABLE` 已回 0”又确认了一次）、释放行 `pending=0 confirmed=0` 且没有 `detail=hardware-still-on`（硬件确实停了，账本没有留欠账）。`consumer=` 只出现在 `acquire` 行，`confirm`／`release` 行不带它，审计按此区分。


## 本候选实现（C1-01～C1-04，补丁 0230）

实现文件：[IOQcomClock.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.h)、[IOQcomClock.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.cpp)；接入 [files.arm64](../../dxb/xnu/iokit/conf/files.arm64)（`optional iokitcpp`）与 [KernelConfigTables.cpp](../../dxb/xnu/iokit/KernelConfigTables.cpp)（MH2LM 段，`compatible` 匹配 `sweet,clock-provider`）；ADT 节点由 [mk-mh2lm-mdss-adt.py](../../tools/mk-mh2lm-mdss-adt.py) 的 `--clock-provider` 追加，节点无 `reg`、无子节点，只有 `name='clock'`、`compatible` 与 `sweet,clock-abi=<u32 1>`。默认派生字节不变，只有显式开关才出现该节点。

- **C1-01**：`start` 先校验自身 `sweet,clock-abi`（4 字节 `1`），再 `IOLockAlloc`、清空槽位、分配诊断 `thread_call`，最后置 `fReady`；重复 start 直接返回已启动结果，`stop` 幂等（第二次不重复 `super::stop`），失败路径不调用 `super::stop`，释放全部持有控制器后才交回父类。不映射任何控制器窗口，不等待任何 CC。
- **C1-02**：`registerController`／`withdrawController` 只承认 ID 1～7；重复 ID 返回 Busy 并保持原槽位；每次注册生成新 generation，撤回将 generation 归零，旧句柄的查询、门控、依赖检查统一返回 NotReady。注册关系与资源来源分离保存，不建立每 clock 一个 IOService。
- **C1-03**：设备用 `sweet,clock-names`（NUL 结尾名字表，单名 1～31 字节、最多 16 项（0233 起；上限与诊断遍历表 `kIOQcomWalkMax=16` 相等））与 `sweet,clocks`（每项 2 个 LE32：controller／local ID，顺序与名字表一致）声明绑定；属性缺失返回 NotFound，类型错误、长度／尾 NUL／项数／ID 越界、重复名字返回 BadArgument。句柄持有 provider 与 consumer 引用，按（controller、local ID、generation）三元组固定身份，provider 撤回或停止后不再解析到新后端；相同 local ID 的不同 CC 不串线。
- **C1-04**：`IOQcomClockInfo` 分别记录 possible／selected 父源、`accessDeps`（寄存器访问依赖）、`domain`、`cornerMaxHz[3]`、pinned／inheritedOn；`checkDependencies` 与门控前置的 `admit` 区分 Ready、ControllerAbsent、ParentController、ParentClock、Domain、Access、UnknownParent、Cycle、Depth、Malformed，深度上限 8、遍历上限 16，环与自引用返回 BadArgument。不注册 IOPM 策略、不自动供电、不写任何寄存器。
- **能力拒绝**：头文件把硬件能力建模为 `kIOQcomCapEnable/Disable/SetRate/Reset` 位；本候选全部后端的这些操作返回 `kIOReturnUnsupported`，`disable` 额外拒绝 pinned 时钟。admission 先于能力判定，因此“依赖未满足”不会被误报成“后端不支持”。
- **入口行如实计数**：`phase=start` 行末尾的 `bindings=%u` 报本节点 `sweet,clock-names` 实际声明的条数（无该属性即 `0`），不再打印 `kIOQcomBindingMax` 容量常量。0122 实机日志出现 `bindings=8`，即该字段误用了容量常量；0231 修正后同一份 ADT 应打出 `bindings=0`。
- **诊断入口（默认关闭，双通道）**：设备树属性 `sweet,clock-diag=<u32 1>` 或启动参数 `sweetclock` 任一存在即触发一次性 `sweetclock-diag` 控制器的只读查询，日志带 `planID=C1 resource=... consumer=... token=... generation=... phase=... result=...`，结束行标记 `detail=zero-writes`。属性存在但不足 4 字节或值不为 `1` 时记一条 `detail=bad-diag-property` 的 WARN 且不开启诊断，不把配置错误当成静默关闭。本机固件对启动参数是 **607 字节硬上限**（`boot args rejected at 607`，超限时内核根本没进入），现役参数已占 605 字节且每个 token 都有语义，因此实机只能走设备树通道；0122 的 `sweetclock=1` 镜像（618 字节）就是被这条上限拒绝的，不是诊断代码的问题。
- **拒绝只报一次（补丁 0232）**：一次失败的使能曾经被报两遍——`checkDependencies` 的遍历打出 `phase=admit` 的 WARN，句柄 `enable` 内部再走同一条遍历又打一遍，0123 诊断日志里 `cc1:0x30` 与 `cc7:0x4` 各出现两条**逐字节相同**的 `phase=admit` 行。0232 给遍历加了一个“是否记账”的开关：`admitWalk`／`admitInternal`／`checkDependenciesInternal` 的 `log` 参数为假时不打印任何拒绝行，只把原因写回 `reason`；`checkDependencies` 与 `admit` 两个公共入口照旧记账，`IOQcomClockHandle::enable` 改为直接调静音遍历并在自己名下打出唯一一条 `phase=enable result=... detail=...`，诊断段每个句柄的探测同样走静音遍历（原因进 `depReason` 字段），因此一台设备上每个失败句柄只留一条拒绝行，且带的是它自己的 phase。成功路径与能力拒绝的 INFO 行不变。
- **证据**：`tools/tests/test-mh2lm-clock-provider.py` 通过（0235 起含账本用例，见 C1-05 段；覆盖生命周期、重复／越界注册、generation 失效、13 类畸形绑定拒绝、admission 九种原因、admission 先于能力、pinned 拒绝、后端传播、重入与自撤回、token 回绕、幂等 stop、参数与设备树两路诊断开关、畸形属性拒绝、`bindings=` 如实计数、写计数为零）；`tools/tests/test-mh2lm-mdss-adt.py` 通过（C1 节点可选、无窗口、可字节还原、CLI 可选、`--clock-diag` 只增加一个属性且去掉后字节还原）；ARM64 单 TU 编译 rc=0（工作区 `scratch/clock-c1/edited-IOQcomClock.compile.txt`，仅既有宏与 `-Wunsafe-buffer-usage` 噪声）；补丁 `0230`（SHA256 `3fc2536659099605f98366ea05e4eab092c7c5a2decf5a90eaa88a32102e428f`）与 `0231`（SHA256 `261a300173249d0642021913d3a0a9be6d769572a319c18467d6663e65a1790c`）在基线 `4148ed3` 上依次严格 `patch -p1 --fuzz=0` 应用后四份文件与工作树逐字节一致、无 `.rej`／`.orig`。C0 基线回归 18 组重跑仍为 17 通过＋同一既有 `panel-profile-lifecycle` ESC0 停钟失败（与 C0 签名一致），新测试日志见 `scratch/clock-c1/logs/`。补丁 `0232`（SHA256 `0d3fdf8814312413db556e714048169842e70c1daf88f644d090ab20e20c2db9`）在同一基线上按 `0230`→`0231`→`0232` 顺序严格应用后四份文件同样逐字节一致。主机测试新增两组断言：一次域依赖失败只让 `phase=admit` 计数不变而 `phase=enable` 加一且带 `detail=domain-absent`，随后一次显式 `checkDependencies` 恰好加一条 `phase=admit`（说明记账权在调用者手里，静音不是丢账）。
- **边界**：无 MMIO 映射、无寄存器读写、无热插拔硬件验证；实机只验证“匹配、Ready、绑定、写计数为零”，真实后端留给 C2-01／02，首个硬件写仍在 C2-08（账本随 0235 一起落地，见上）。本候选不改动显示时钟账本、父类源码、频率／电源／reset／GDSC 路径，也不关闭任何既有资源。

## 实机证据（0123 内核，两档各一次）

0123 内核（`Payloads/kernel.macho` SHA256 `4a054ef0efc9a5bd8d3380a53848961fddbf6cc682c8c9b5cffbff714a25bace`，源码树与提交 `4190f83` 的 5302 个文件逐字节一致）由同一份固件卷与同一份 605 字节启动参数打出三份镜像，实机各刷一次；日志在设备端落盘后取回，`total < 4194304`（未绕环）才当作证据。

- **默认档**（`boot-16k-0123-gpioirq-panel-pipeline.img`，SHA256 `ce0101d0ede54d637d36622cbf716bd377ea814acc187f4ed6116efa5e37122d`；日志 `sweetlog-0123-0b.bin`，4198400 字节，`total=1090524`，flush 9，SHA256 `a9ec2d73f233c61f4b09bead1e1a54c00946fa9d4abb46e16aef60e03a6d4a7c`）：时钟日志只有一条 `[INFO] SWEET CLOCK: planID=C1 phase=start result=0 detail=entry-ready bindings=0`——0231 的如实计数在真机上兑现（0122 同位置曾打 `bindings=8`）；无 bind／enable／disable／admit／register／withdraw／diag 行，时钟 WARN／ERROR 均为 0。节点无 `reg`、无控制器注册，本档不存在 MMIO 写路径。非时钟 WARN／ERROR 清单与 b32 完全一致（同五条 WARN、零 ERROR），`handoff`／`scanout` 各 1，触摸 token 2。
- **诊断档**（`boot-16k-0123-gpioirq-panel-pipeline-clockdiag.img`，SHA256 `54771c2e83afa3df1898f815fb1b29565a995851f3893c2dc793c89ae0874c38`；日志 `sweetlog-0123-1b.bin`，`total=1376435`，flush 12，SHA256 `b8764571aa79e9e7f7f3b0d6313c2c05ec51a1d726f6abb01e12ad562978b3b5`）：18 条时钟行，`phase=start`（`bindings=0`）→ `consumer=provider phase=register result=0 generation=1`（`cc7:0x0`）→ `phase=diag result=0 detail=backend-registered` → 四个句柄各一条 `phase=bind ... bindings=4` → 每个句柄一条 `phase=diag` 账本行 → 收尾 `phase=diag-done result=0 detail=zero-writes`。账本行显示查询本身可用而写路径全拒绝：`diag-core`（`caps=0x3`，无 Enable 位）与 `diag-child`（`caps=0x103`）`deps=0 depReason=ready`、`enable=-536870201`（Unsupported，前者带 `detail=capability-absent`，后者由后端拒绝）；`diag-orphan` `deps=-536870184 depReason=parent-controller-absent`、`diag-domain`（`domain=0x1`）`deps=-536870184 depReason=domain-absent`，两者 `enable` 等于 `deps`，即“依赖未满足”没有被误报成“后端不支持”；四个句柄 `info=0 state=0`。非时钟基线同样与 b32 零差异。
- **本轮唯一缺陷**：两个被拒句柄的 `phase=admit` WARN 各出现两条逐字节相同的副本（遍历被 `checkDependencies` 与 `enable` 各走一遍），已由补丁 0232 收敛为“每个失败句柄一条、归 enable 所有”；该缺陷只影响日志行数，不影响判定内容。
- 审计脚本 `scratch/artifact-0123/audit-clock-log.py` 两种形态都接受（重复行只被记录为 `duplicate_refusal_lines`，不被当成通过条件），输出 `scratch/artifact-0123/device-log-review.json` 与 `device-log-review-diag.json`。
- 0123 之前的两次短日志（`sweetlog-0123-0.bin`／`-1.bin`，`total` 分别 10745838／7825416）因绕环丢掉了引导段时钟行，不作为证据；此后每次取证都在开机后不久落盘。

### 0232 复验（0124 内核，仅诊断档）

0124 内核（`kernel.development.mh2lm` SHA256 `4dd10add94b2cf1e861c23b41d4e79f74ee31f7f581c842897d66cd05016aad8`，17911248 字节，构建树 5302 文件与提交 `d4c5710` 一致、154 marker 命中）的诊断镜像 `boot-16k-0124-gpioirq-panel-pipeline-clockdiag.img`（SHA256 `24ab375d6132d11569e079028ba9cd3982e5226eb7ee03bcee311e3b61b43fe6`，参数 605 字节）实机启动一次，日志 `logs/sweetlog-0124-0.bin`（4198400 字节，`total=999833`，flush 8，未绕环，SHA256 `981e29ef35bf1ebb2fb16bc5f75a63470776cc0f0464ffcb8cdc16cea6971eee`）：

- 时钟行 18→**16** 条，正是少了那两条重复的 `phase=admit`：`phase=start`／`register`／`diag`（backend-registered 1 条＋四句柄 4 条）／`bind` 4／`enable` 4／`diag-done` 1，`admit` 计数为 0。
- 两个被拒句柄各只剩**一条** WARN，phase 都是 `enable`：`resource=cc7:0x3 … result=-536870184 detail=parent-controller-absent`、`resource=cc7:0x4 … result=-536870184 detail=domain-absent`；审计脚本记为 `owner_phases=["enable"] lines_per_owner={"enable":1} duplicated=false`。
- 四行账本与 0123 逐字段相同（`info=0 state=0`、`deps`／`depReason` 一致、`caps` 0x3／0x103／0x101／0x303、`enable` 全为 Unsupported 或等于 `deps`），收尾仍是 `phase=diag-done result=0 detail=zero-writes`，零 `[ERROR]`。
- 非时钟基线与 b32 一致（同五条 WARN、零 ERROR、`handoff`／`scanout` 各 1；短会话一轮触摸 token=2）。审计 `scratch/artifact-0124/device-log-review-diag.json`，rc=0。

据此 0232 的“谁报告谁记账”在真机兑现，C1 日志形态与卡片一致；默认档不含任何拒绝路径，无需再测。
