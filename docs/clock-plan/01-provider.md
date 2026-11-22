# C1：统一时钟入口与只读资源接口

[返回总计划](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)。各卡默认遵守[总计划 3.5 共享约束](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)，卡内只写特有边界。状态：C1-01～C1-04 已实现为只读候选（补丁 `0230`，写计数为零）；C1-05 未开始。

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

## 本候选实现（C1-01～C1-04，补丁 0230）

实现文件：[IOQcomClock.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.h)、[IOQcomClock.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.cpp)；接入 [files.arm64](../../dxb/xnu/iokit/conf/files.arm64)（`optional iokitcpp`）与 [KernelConfigTables.cpp](../../dxb/xnu/iokit/KernelConfigTables.cpp)（MH2LM 段，`compatible` 匹配 `sweet,clock-provider`）；ADT 节点由 [mk-mh2lm-mdss-adt.py](../../tools/mk-mh2lm-mdss-adt.py) 的 `--clock-provider` 追加，节点无 `reg`、无子节点，只有 `name='clock'`、`compatible` 与 `sweet,clock-abi=<u32 1>`。默认派生字节不变，只有显式开关才出现该节点。

- **C1-01**：`start` 先校验自身 `sweet,clock-abi`（4 字节 `1`），再 `IOLockAlloc`、清空槽位、分配诊断 `thread_call`，最后置 `fReady`；重复 start 直接返回已启动结果，`stop` 幂等（第二次不重复 `super::stop`），失败路径不调用 `super::stop`，释放全部持有控制器后才交回父类。不映射任何控制器窗口，不等待任何 CC。
- **C1-02**：`registerController`／`withdrawController` 只承认 ID 1～7；重复 ID 返回 Busy 并保持原槽位；每次注册生成新 generation，撤回将 generation 归零，旧句柄的查询、门控、依赖检查统一返回 NotReady。注册关系与资源来源分离保存，不建立每 clock 一个 IOService。
- **C1-03**：设备用 `sweet,clock-names`（NUL 结尾名字表，单名 1～31 字节、最多 8 项）与 `sweet,clocks`（每项 2 个 LE32：controller／local ID，顺序与名字表一致）声明绑定；属性缺失返回 NotFound，类型错误、长度／尾 NUL／项数／ID 越界、重复名字返回 BadArgument。句柄持有 provider 与 consumer 引用，按（controller、local ID、generation）三元组固定身份，provider 撤回或停止后不再解析到新后端；相同 local ID 的不同 CC 不串线。
- **C1-04**：`IOQcomClockInfo` 分别记录 possible／selected 父源、`accessDeps`（寄存器访问依赖）、`domain`、`cornerMaxHz[3]`、pinned／inheritedOn；`checkDependencies` 与门控前置的 `admit` 区分 Ready、ControllerAbsent、ParentController、ParentClock、Domain、Access、UnknownParent、Cycle、Depth、Malformed，深度上限 8、遍历上限 16，环与自引用返回 BadArgument。不注册 IOPM 策略、不自动供电、不写任何寄存器。
- **能力拒绝**：头文件把硬件能力建模为 `kIOQcomCapEnable/Disable/SetRate/Reset` 位；本候选全部后端的这些操作返回 `kIOReturnUnsupported`，`disable` 额外拒绝 pinned 时钟。admission 先于能力判定，因此“依赖未满足”不会被误报成“后端不支持”。
- **入口行如实计数**：`phase=start` 行末尾的 `bindings=%u` 报本节点 `sweet,clock-names` 实际声明的条数（无该属性即 `0`），不再打印 `kIOQcomBindingMax` 容量常量。0122 实机日志出现 `bindings=8`，即该字段误用了容量常量；0231 修正后同一份 ADT 应打出 `bindings=0`。
- **诊断入口（默认关闭，双通道）**：设备树属性 `sweet,clock-diag=<u32 1>` 或启动参数 `sweetclock` 任一存在即触发一次性 `sweetclock-diag` 控制器的只读查询，日志带 `planID=C1 resource=... consumer=... token=... generation=... phase=... result=...`，结束行标记 `detail=zero-writes`。属性存在但不足 4 字节或值不为 `1` 时记一条 `detail=bad-diag-property` 的 WARN 且不开启诊断，不把配置错误当成静默关闭。本机固件对启动参数是 **607 字节硬上限**（`boot args rejected at 607`，超限时内核根本没进入），现役参数已占 605 字节且每个 token 都有语义，因此实机只能走设备树通道；0122 的 `sweetclock=1` 镜像（618 字节）就是被这条上限拒绝的，不是诊断代码的问题。
- **证据**：`tools/tests/test-mh2lm-clock-provider.py` 通过（覆盖生命周期、重复／越界注册、generation 失效、13 类畸形绑定拒绝、admission 九种原因、admission 先于能力、pinned 拒绝、后端传播、重入与自撤回、token 回绕、幂等 stop、参数与设备树两路诊断开关、畸形属性拒绝、`bindings=` 如实计数、写计数为零）；`tools/tests/test-mh2lm-mdss-adt.py` 通过（C1 节点可选、无窗口、可字节还原、CLI 可选、`--clock-diag` 只增加一个属性且去掉后字节还原）；ARM64 单 TU 编译 rc=0（工作区 `scratch/clock-c1/edited-IOQcomClock.compile.txt`，仅既有宏与 `-Wunsafe-buffer-usage` 噪声）；补丁 `0230`（SHA256 `3fc2536659099605f98366ea05e4eab092c7c5a2decf5a90eaa88a32102e428f`）与 `0231`（SHA256 `261a300173249d0642021913d3a0a9be6d769572a319c18467d6663e65a1790c`）在基线 `4148ed3` 上依次严格 `patch -p1 --fuzz=0` 应用后四份文件与工作树逐字节一致、无 `.rej`／`.orig`。C0 基线回归 18 组重跑仍为 17 通过＋同一既有 `panel-profile-lifecycle` ESC0 停钟失败（与 C0 签名一致），新测试日志见 `scratch/clock-c1/logs/`。
- **边界**：C1-05 公共需求账本未实现；无 MMIO 映射、无寄存器读写、无热插拔硬件验证；实机只验证“匹配、Ready、绑定、写计数为零”，真实后端留给 C2-01／02，首个硬件写仍在 C2-08。本候选不改动显示时钟账本、父类源码、频率／电源／reset／GDSC 路径，也不关闭任何既有资源。
