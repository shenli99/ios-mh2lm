# C3：apps RSC 公共事务通道

[返回总计划](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)。各卡默认遵守[总计划 3.5 共享约束](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)，卡内只写特有边界。状态：C3-01 已在设备上两次读到同一块并**验收通过**（0140 读到、抓一处读错；0141 带上补丁 `0247` 复验严格 rc=0）；C3-02～C3-08 未开始。

## 阶段目标与退出条件

先只读初始化，再原样提取执行器，再以启动时选择的方式转交唯一硬件所有权（在线交接为可选的 C3-04b）；队列、事务生命周期和批次分别增加。既有 apps IRQ 已存在，首批保留 IRQ 完成及期限 timer，不退回轮询 completion。VRM／ARC／CN0 聚合算法在本阶段不变。

### C3-01 建立 apps RSC channel 的只读实例

- **目标**：使 IOQcomRSCChannel 识别 apps DRV 并发布尚不可提交的通道。
- **前置**：C0-02；原 SweetRPMh 继续独占写入。
- **需求与边界**：取得经过核对的 DRV／TCS 窗口及 IRQ 描述，读取版本、TCS 分组、NCPT；此时只保存 IRQ 资源，不注册第二个物理 handler，不修改 IRQ mask／TCS。
- **本地检验**：错误 DRV／版本／布局／长度、NCPT 越界、缺 IRQ 拒绝；submit 明确 NotReady；退出释放只读资源。
- **实机检验**：新实例读到与原 owner 一致的 apps 配置，写计数零，原 owner 的自然投票及 IRQ 仍完成。
- **预期结果**：通道可初始化但没有第二个 writer；不同 RSC 的描述不串用。
- **失败归属**：C3-01 identity／layout／map；原票失败且缺新写证据时先查基线与访问条件。

#### 实机证据：0140 通道档（C3-01 第一次上机，2026-11-27）

默认档树＝0139 默认树 + 一个 `sweet,rsc-channel` 节点（六条属性，`reg` 只列 owner 的三段，窗口里没有 GIC），一次 `fastboot boot` 进桌面，
环 `logs/sweetlog-0140-0.bin` 里有 9 条 `SWEET RSC:` 行：`phase=irq intid=37 detail=described-not-armed`、三次 `phase=map`
（`apps-drv` `0x100`／`apps-tcs` `0x3000`／`apps-db-locator` `0x8`）、`phase=identity id=0x10700 major=1 solver=0x4010100 tcs=8 ncpt=16`、
`phase=command-db address=0x85f20000 bytes=4424 version=0x1`、`phase=tcs irq-enable=0x1 irq-status=0 detail=observed-not-owned`、
`phase=start … windows=4 ranges=3 reads=10 writes=0 id=0x10700 tcs=8 ncpt=16 irq=37 submit=not-ready`。
身份五个字段与 owner 那一行（`pre-v3 RSC id=0x10700 config=0x800c8104 ncpt=16 ACTIVE=2`）逐字段相等，读账 10 次载入、零次存储，
`submit_attempts=0`（没有调用者），`phase=stop` 不出现（驱动在本次启动里不卸载）；时钟侧 26 行、`reads=19`／`writes=0` 与 0139 默认档逐值相同。

**这张环抓到 driver 一处读错，也纠正了本卡对启动顺序的假设。**

- flag 字读在 `kIOQcomRscDbHeaderBytes`（144，header 的**长度**）而不是 header 的最后一个字（140），于是把命令条目首字节当 flags
  打印成 `flags=0x6c2e7863`（小端即 `"cx.l"`），并对一次健康启动报了 `version-flags-differ` WARN。owner 在同一次启动里一条 version/flags
  警告都没有，所以错的只能是新读者。修法是 `kIOQcomRscDbFlagsOffset=140`（补丁 `0247`），读账仍是 10 次载入；宿主测试用两侧用例钉住这个偏移。
- 通道描述 INTID37 时，owner 只**映射并读过**这一块，还没有武装它自己的线：0140 的顺序是 owner 身份行（136）→ 通道 146..154 →
  owner 的 `INTID37 configured` 在 2112 行。所以"通道必须在 owner 武装中断之后"是错的判据，"在 owner 读过块之后"才是对的，
  中断那一行只能要求出现一次。

**0140 的判据（当时写下，0141 逐条对照）**：`--c2-03 --c3-01` 不加 `--allow-baseline-drift` 也 rc=0；通道 `reads=10`／`writes=0`；
`id`／`major`／`tcs`／`ncpt` 与 owner 行相等；`irq-enable=0x1` 只观察；无 `version-flags-differ`；owner 的 INTID37 行出现且仅一次。

#### 实机验收：0141 通道档（C3-01 收口，2026-11-27）

只有 `0247` 一个补丁落地（内核 `51cbf23e…`，17 987 456 B，与 0140 同尺寸），镜像 `boot-16k-0141-gpioirq-panel-pipeline.img`
＝`ec02cdcf…`（35 545 088 B，默认档树仍是 `bb7eb44e…`／23 884 B，十一棵门控树摘要与 605 字节参数、固件 `27e7a1d1…` 全部未变），
一次 `fastboot boot` 进桌面。环 `logs/sweetlog-0141-0.bin`（`6a2702cc…`、4 198 400 B、`total=319335`、`flush_sequence=9`、未回绕、4019 行）。

**同一块、同一个身份、同样十次载入，一条 WARN 也没多。** `SWEET RSC:` 行回到 **8 条**（`irq 1`／`map 3`／`identity 1`／
`command-db 1`／`tcs 1`／`start 1`），`phase=command-db … version=0x1 flags=0 detail=read-only-map`——140 处的那个字确实是 0，
`0x6c2e7863` 不再出现在任何一行；`--c2-03 --c3-01` **不加** `--allow-baseline-drift` 得 **rc=0**（第一个严格通过的通道档），
WARN 清单只剩 `SWEET FB SCANOUT`×3 与 `SWEET USB`×2，与 b32 相等。判据逐条：`reads=10 writes=0`；
`id=0x10700 major=1 tcs=8 ncpt=16` 与 owner 行 `pre-v3 RSC id=0x10700 config=0x800c8104 ncpt=16 ACTIVE=2` 逐字段相等；
`irq-enable=0x1 irq-status=0 detail=observed-not-owned`、`irq=37 detail=described-not-armed`、`submit=not-ready`、`submit_attempts=0`；
owner 的 `INTID37 configured` 出现且仅一次（2112 行，按修好的顺序判据）；时钟侧 26 行、`reads=19`／`writes=0`／`variant=0x8150`
与 0139／0140 默认档逐值相同，面板标记齐全。DB 仍是 `0x85f20000`／4424 B，与 0140 同址同长。

**内核增量这次是逐字节核对的**：0141 与 0140 同尺寸，整份差异只有四处——`LC_UUID` 16 B、加载命令区 2 B、
`0xb023b0` 上的一条 `movz w2, #144` → `#140`（上下文与 `bl` 位移全不变），以及 `__CTF` 段整体（该段 `initprot=1` 只读不可执行，
clang 侧每次构建都不同）。也就是说可执行代码只差这条立即数。顺带确认这套内核在字节层面不可重建（UUID 与 `__CTF` 每次都变），
所以"同一份代码给出同一哈希"不能当判据，判据只能是代码增量 + 符号/字符串门禁 + ADT 摘要，打包脚本现在做的正是这三样。

**C3-01 由此收口**：读侧成立（同一块、同一身份、十次载入）、零存储（结构上 `writeRegister()` 只回 `kIOReturnUnsupported`，
计数 `writes=0` 不是巧合）、不注册第二个 handler（节点连 GIC 段都没有）、不碰 IRQ mask／TCS，owner 的票与完成照旧。
未覆盖项留待后续卡：这条通道**没有任何提交能力**（`submit()` 恒 `kIOReturnNotReady`），也不与 owner 交接所有权。

### C3-02 提取只读 Command DB 查询

- **目标**：用一个公共查询模块返回已知资源的地址和辅助元数据。
- **前置**：C0-02；使用原有已验证 locator／映射来源。
- **需求与边界**：只迁移 DB 查询，保留边界／版本验证及资源类型信息。先以一个现有资源比对；不改变票值编码、TCS 或消费者。
- **本地检验**：正常、缺失、截断、越界、错误类型／辅助长度；新旧查询结果逐字段一致。
- **实机检验**：对同一已知资源比较新旧结果，现有投票仍走旧查询／执行器；无 DB 写操作。
- **预期结果**：资源驱动可共享只读元数据；没有原始地址面向普通外设的接口。
- **失败归属**：C3-02 locate／parse／lookup；资源不存在与传输失败分别记录。

#### 实现与本地证据（C3-02 代码完成，待 0142 实机）

**查询搬成一个独立翻译单元**：`SweetRSCDatabase.h/.cpp`（248／91 行）里没有任何 IOKit——`lookup()` 收字节数、资源名、
辅助位掩码和一个读字回调，走 chunk 描述符、比 8 字节键、按生效的 slave 类施加判据，返回一个纯结构
`SweetRSCResource{found,slave,matches,address,entry,aux,auxBytes,unit,width,vcd,levels[16],levelCount,reserved}`
加一个 `SweetRSCStage` 说明**在哪一步拒绝**。一个只读模块不该有写助手的可能：这个类里根本没有 store。

**判据抄的是 owner 的顺序，不是它的实现**（`SweetRPMh.cpp`）：公共 chunk 门（slave 3..5、entry 表字对齐、entry 表在块内、
aux 基在块内）→ rail：地址高字 4、16 字节槽、辅助窗口在块内；ARC：地址高字 3、地址字对齐、电平表半字对齐、4..32 偶数字节、
表与补齐都在块内、再校电平本身（首级为 0、严格递增、零只在末级之后、至少两级）；BCM：地址高字 5、地址字对齐、aux 恰 8 字节
且表字对齐、两字都在块内、再校 unit／width／vcd。载入顺序同样照抄（2 描述符字 → 每项 2 键字直到命中 → 地址＋aux 描述字 →
该类自己的字），`matches!=1` 即拒绝。

**两处刻意不搬**，都写进了补丁说明：① owner 的同类兄弟 alias 检查读的是它自己的 `fBCMAvailable` 表，那是 owner 的账本而非
DB 内容，公共模块表达不了，原样留在 owner；② 公共 chunk 门**不要求辅助基字对齐**——owner 只对 aux 做 `range(aux,0)`，
一个比 owner 更严的第二读者本身就是分歧。夹具因此专门造了一个 aux 基偏两个字节的块并要求其中的 rail 项解析成功，
把 `(aux&3)` 加回去这条立刻失败（双向已验证）。半字电平表按 `offset & ~3` 读下面那个字并缓存成对，和 owner 的 `db16()` 一致。

**通道侧接入是一处 opt-in 属性**：节点上的 `sweet,rsc-query`（4 字节，值 1）；长度或值不对各打一条 ERROR 并点名实际值与
期望值，没有这条属性就既不出行也不载入。`start()` 的顺序因此是 identity → command-db → **db-query** → tcs：
查询只能在这里跑，块已映射、几何已知。**账分开记**：`fReads` 仍是 0141 环钉住的十次身份载入，查询的载入由 lookup 自己数，
在总结行里单列 `query-reads=%u`，行里单列 `reads=%u`——"十次载入"继续是 C3-01 立下的那句话，查询的代价可见而不被摊进去。
两条路径共用同一个 `readWindow()`，所以整份驱动里 `*(volatile uint32_t *)` 仍然只有一处。

**两条失败分开记**：资源不存在是 `kIOReturnNotFound`（`stage=matches`），传输失败是 `kIOReturnNotReady`（其余 stage）；
两者都是 WARN 加 `detail=query-refused` 并点名 stage 与实际载入数，然后**照常启动**——通道的证据不依赖 DB 可读，
这一票由 owner 投。成功是一条 INFO：类、地址、entry、aux 地址／长度、unit／width／vcd、`matches` 与载入数。

**本地检验**（三条，全部绿）：`test-mh2lm-rsc-database.py` 拿夹具从高层描述造块再让模块解析回来，每个门都有双向档，
接受档钉死载入数（CN0 26 次、`ldoa14` 10 次、`mx` 13 次、八描述符全空 16 次），越界／截断／错误类型／辅助长度各归自己的 stage；
`test-mh2lm-rsc-channel.py` 加了四条属性档与一条查询环（9 行、`query-reads=18`、`fReads` 仍 10）；
`test-mh2lm-mdss-adt.py` 证明 `--rsc-query` 离不开 `--rsc-channel`（否则 `ValueError`）、不多加节点也不多加窗口、
且被门控的十一棵时钟树依旧完全没有通道节点。

**审计器**：`--c3-02`（蕴含 `--c3-01`）认 `phase=db-query`，要求环与旗标双向一致（查询环不许当 C3-01 判、通道环不许当 C3-02 判），
并把它与**同一环里 owner 自己那条** `SWEET RPMH BCM: name=CN0 …` 逐字段比（`address`／`unit`／`width`／`vcd` 相等、`slave=5`、
`matches=1`、`aux` 以 `/8` 结尾）；`enable-mask` 不比，那是调用方策略不是 DB 内容。自测 `selftest-channel-auditor.py`
**PASS - 37 变体**（24 个 C3-01 ＋ 2 个合格查询环 ＋ 9 个应当被拒的查询环 ＋ 两个跨人格互拒）。

**待实机**：0142 档（节点加一条属性、ADT `999e2705…`／23 924 B、十三张镜像、605 字节参数与十一棵门控树摘要不变）。
判据：`SWEET RSC:` **9 行**（`db-query` 严格夹在 `command-db` 与 `tcs` 之间）、`reads=10`／`writes=0` 不动、
`query-reads` 等于行内 `reads`、`query=ok`、与 owner 的 CN0 行逐字段相等、**零新增 WARN**（成功是 INFO，失败才 WARN）。
补丁 `xnu/patches/0248-mh2lm-rsc-database.patch`（1 527 行），在基线上严格重放后与工作树逐字节相同。

### C3-03 原样提取现有 ACTIVE 执行器

- **目标**：把现有 TCS 执行代码提取为内部模块，同时保持原服务为唯一硬件 owner。
- **前置**：C0-01／02。
- **需求与边界**：只移动寄存器访问、现有 IRQ/event 配合、触发／完成／清理机制；保留原 gate、顺序、期限、Busy 和故障保留。原 VRM／ARC／CN0 调用通过内部适配层原样执行，不在这次改聚合或通用接口。
- **本地检验**：对已有命令形状比较提取前后 MMIO／结果轨迹；现有 stale IRQ、超时和 cleanup 回归不弱化；适用 ARM64 编译。
- **实机检验**：至少一个既有自然 ACTIVE 请求取得新 IRQ 完成，原消费者功能及普通路径保持；日志明确仍由旧服务持有硬件。
- **预期结果**：公共执行器已有可复用实现，硬件所有权尚未转交。
- **失败归属**：C3-03 extraction／sequence／completion；算法差异需撤出本卡另建修复。

### C3-04 启动时由 channel 作为 apps 执行器唯一 owner

- **目标**：通过启动时选择，使 apps DRV 的全部实际写入与 IRQ 生命周期从一开始就归属 channel，旧服务不再触碰 TCS。
- **前置**：C3-01、C3-03；C0-02 的 apps 交接条件全部闭合。
- **需求与边界**：owner 在 channel 首次写入前确定（编译期或显式启动参数），旧 SweetRPMh 执行路径在该模式下不初始化写能力、不注册 IRQ handler；既有 VRM／ARC／CN0 客户端经兼容层使用同一执行器，不能留两套 MMIO。不做运行中交接，不涉及排空旧在途状态。保留单笔在途及 Busy；不改 GIC 通用架构、不加队列／缓存。回退方式为选择旧 owner 的已验镜像／参数。
- **本地检验**：两种 owner 模式下仅一方可写、IRQ 仅一方注册；新模式下旧入口写尝试被拒绝；命令顺序与 C3-03 一致。
- **实机检验**：新 owner 模式启动，记录 owner 选择、旧写计数为零、channel 完成既有自然 VRM／ARC／CN0 请求的 IRQ／cleanup；USB／根卷、显示、触摸回归。
- **预期结果**：唯一 writer 和 IRQ owner 已迁移，资源语义不变。后续 C3-05 起与 C4 依赖本卡，不依赖 C3-04b。
- **失败归属**：C3-04 owner-select／IRQ-owner／dispatch；新算法问题不能靠兼容回退隐藏。

### C3-04b （可选）运行中将 apps 执行器从旧服务交接给 channel

- **目标**：在不重启的情况下把执行器 owner 从旧服务转给 channel。仅在有实际需求（例如需要旧服务先行启动的早期供电票）时实施；无需求标“暂缓（无需求）”。
- **前置**：C3-04 实机通过；C3-07 的不确定／隔离合同。
- **需求与边界**：旧方关闭接收、排空或隔离旧在途状态，转交 IRQ/event、票据与保留状态，记录 ownership epoch。无法证明排空时拒绝交接，保持旧 owner，禁止在线强抢。
- **本地检验**：交接各阶段失败、未排空拒绝、旧入口交接后不能写、晚到 IRQ 生命周期。
- **实机检验**：记录 epoch、旧写计数冻结、新方接收一个既有自然请求并完成 IRQ／cleanup；原功能回归。
- **预期结果**：运行中交接可用；不作为其他卡的前置。
- **失败归属**：C3-04b drain／handoff／IRQ-owner。

### C3-05 增加有界串行提交队列

- **目标**：让两个合法请求可排队并按已定义顺序完成。
- **前置**：C3-04。
- **需求与边界**：固定且有界容量、显式 QueueFull；先保持硬件最多一笔在途。相同资源或同资源不同字段不能冲突并行；数据和持有对象的生命周期覆盖完成。
- **本地检验**：A 在途／B 排队、队满拒绝、不同调用者顺序、同资源冲突；A 失败时 B 不得擅自越过隔离状态。
- **实机检验**：两个已允许请求通过新接口产生独立 token，观察入队／发出／完成顺序；不借本卡增加新资源类型。
- **预期结果**：软件排队可用，仍不宣称多 TCS 并行吞吐。
- **失败归属**：C3-05 enqueue／ordering／capacity；具体事务执行错误标记 executor phase。

### C3-06 完成排队请求的取消与客户端退出合同

- **目标**：客户端退出后，不再执行其尚未发出的请求，也不访问已释放对象。
- **前置**：C3-05。
- **需求与边界**：区分 queued 与 triggered。只撤销尚未执行的请求；已触发请求继续由 provider 持有并终结／隔离，不把取消当成硬件撤销，不自动发送反向票。
- **本地检验**：取消队首／队尾、退出与完成交错、callback 排队后 consumer 退出；释放对象后无访问。
- **实机检验**：受控诊断客户端在 B 排队期间退出，B 无写事件；已允许的 A 正常完成。真实 provider 热终止若不可触发，注明仅 host 证据。
- **预期结果**：一个请求的取消不会撤掉其他客户端票，也不会提前回收在途存储。
- **失败归属**：C3-06 cancel／detach／lifetime。

### C3-07 对外提供超时、不确定与隔离状态

- **目标**：通用事务接口能准确暴露一次在途失败，且不会误归属晚到完成。
- **前置**：C3-05；原有超时保留机制自 C3-04 起持续生效。
- **需求与边界**：增加通用 token／generation 的状态查询及隔离标记；confirmed 不因超时前进，可能执行中的 TCS 不复用。不增加自动恢复或重试。
- **本地检验**：超时后 late IRQ、旧 token 查询、generation 重用、部分 cleanup 失败；隔离后新请求明确拒绝。
- **实机检验**：优先检验真实成功状态；若有受控软件诊断能屏蔽本请求完成消费，单独运行并证明隔离／后续拒绝。未实施故障注入时该分支只记 host 通过，不声称硬件超时已验。
- **预期结果**：调用者可区分 Pending／Complete／Failed／Uncertain；受损通道不会伪恢复。
- **失败归属**：C3-07 attribution／state／quarantine；故障注入本身未命中则为证据不足。

### C3-08 保持一组命令的原子边界与顺序

- **目标**：一个已知有序命令组能作为独立事务提交而不被拆散或重排。
- **前置**：C3-05、C3-07，命令组样本及 NCPT 限制已确认。
- **需求与边界**：先支持一组能放入单 TCS 的已有命令形状，保留 wait／response 要求；超长组明确拒绝。不实现任意自动跨 TCS 切分，不在此加去重。
- **本地检验**：组内顺序、容量边界、另一请求不能插入；批次接受／完成与每个命令状态区分。
- **实机检验**：用已有获准命令组比对寄存器编程顺序和本次完成，整个组对应一个 token；不能仅观察最后一个资源值。
- **预期结果**：为 VRM／BCM 的已知批次保留语义，不能吞掉控制／commit 命令。
- **失败归属**：C3-08 group-layout／ordering／group-completion。
