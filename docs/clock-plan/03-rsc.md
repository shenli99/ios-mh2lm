# C3：apps RSC 公共事务通道

[返回总计划](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)。各卡默认遵守[总计划 3.5 共享约束](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)，卡内只写特有边界。状态：C3-01 已在设备上两次读到同一块并**验收通过**（0140 读到、抓一处读错；0141 带上补丁 `0247` 复验严格 rc=0）；C3-02 已在 0142 上**验收通过**（查询与 owner 对同一资源给出同一份答案，十次载入／零存储不变）；C3-03 已在 0143 上**验收通过**（执行机制已搬进无状态模块，环里仍是旧服务持有硬件，自然 ACTIVE 完成照旧到达）；C3-04 已在 0147 的**双默认树**上**验收通过**（委派档 `executor=delegated … hardware-owner=IOQcomRSCChannel`、交接四行按序到齐；对照档 `executor=module … hardware-owner=SweetRPMh`、verdict 与 0143 逐字段相同）；C3-04b 暂缓（无需求）；C3-05 已在 0149 的**双默认树**上**验收通过**（候选档读到 `phase=selftest kind=register-bandwidth resource=0 bytes0=20000 bytes1=40000` 与配套的 direct／enqueue／queued 三行，两笔各自成飞、各自 ACK；对照档 45 条 RPMH／RSC 线缆行与 0147 逐字节相同，只多 7 条无条件直接派发行）；0148 那一轮在设备上证明的是**示意写错**（两条 rail 票被准入筛合法拒绝），不是队列有问题。C3-06 代码、宿主证据、审计器与包门禁已完成；0150 实机在示意**时序**上失败（两示意共用一个退休窗口，三笔全入队），已由 `0254` 定点修复；0151 实机把取消合同六行全部读到（撤、退出、账本、降票都对），但 `step=2` 那一行**被控制台的行缓冲截断**（331 字节只存 255，尾巴连换行一起丢），已由 `0255` 把该行改成字段在前、句子在后的形状，**0152 重抓**：排队中的请求可由持有者或退出客户端撤回，撤回既不是完成也不是硬件回滚，把 CN0 降回 0 由一笔真投票完成而不是撤掉任何人的账本。

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

补丁 `xnu/patches/0248-mh2lm-rsc-database.patch`（1 527 行），在基线上严格重放后与工作树逐字节相同。

#### 实机验收：0142 查询档（C3-02 收口，2026-11-27）

默认档一次启动（`boot-16k-0142-gpioirq-panel-pipeline.img`，内核 `0ddddf4a…`，17 988 008 字节；CI 构建树里的
`SweetRSCDatabase.cpp/.h`、`IOQcomRSC.cpp/.h` 与提交 `e623885` 逐字节相同），环 `sweetlog-0142-0.bin`
（4 198 400 字节，sha256 `998b0b6c…`，`total=318997`，`flush_sequence=10`，`wrapped=false`，3 958 行）。
`SWEET RSC:` **九行**，`db-query` 严格夹在 `command-db` 与 `tcs` 之间：

```
phase=irq intid=37 detail=described-not-armed
phase=map resource=apps-drv result=0 length=0x100
phase=map resource=apps-tcs result=0 length=0x3000
phase=map resource=apps-db-locator result=0 length=0x8
phase=identity id=0x10700 major=1 solver=0x4010100 tcs=8 ncpt=16
phase=command-db address=0x85f20000 bytes=4424 version=0x1 flags=0 detail=read-only-map
phase=db-query name=CN0 result=0 slave=5 address=0x500a8 entry=0x818 aux=0x9b0/8 unit=20000 width=4 vcd=5 matches=1 reads=292
phase=tcs irq-enable=0x1 irq-status=0 detail=observed-not-owned
phase=start result=0 windows=4 ranges=3 reads=10 query-reads=292 writes=0 id=0x10700 tcs=8 ncpt=16 db=0x85f20000 db-size=4424 irq=37 query=ok submit=not-ready
```

**两代读者给出同一份答案**：同一环第 1220 行是 owner 自己的
`SWEET RPMH BCM: name=CN0 metadata ready address=0x500a8 unit=20000 width=4 vcd=5 enable-mask=0`——
`address`／`unit`／`width`／`vcd` 四个字段逐字相等（`enable-mask` 不比，它是调用方策略），`slave=5`、`matches=1`。
辅助区间 `aux=0x9b0/8` 落在 BCM 八字节那条上，与 owner 只按 `range(table,8)` 取用一致。

**账目分开且没有漂**：`reads=10`／`writes=0` 与 0141 一模一样（C3-01 立下的那句话仍然成立），
查询的代价单列成 `query-reads=292`，与查询行自己的 `reads=292` 相等。292 比夹具钉的 26 大一个量级，
原因是设备上的 BCM 块有一百多个条目、`lookup()` 要逐条比 8 字节键才找到 `CN0`——**夹具钉的是走法，不是设备的条目数**，
这一点在写夹具时就该预期到；环里这个数只用于对账，没有门限依赖它。

**零新增 WARN**：非时钟 WARN／ERROR 清单与 b32 逐条相等（`SWEET FB SCANOUT` ×3、`SWEET USB` ×2），
`query-refused` 一条也没有——成功是 INFO，只有"树里点名了块里没有的资源"才 WARN，而这一版树点名的是真实存在的 `CN0`。
`audit-clock-log.py --c2-03 --c3-02` **严格 rc=0**（没有 `--allow-baseline-drift`）。

**旗标与环双向一致**：用 `--c3-01` 判这份环直接被拒（`AssertionError: the ring carries a query row: judge it with --c3-02`）。
**打包侧记一笔**：`IOQcomRSCChannel::queryCommandDB` 被内联进 `start()`，产物里**没有**它的符号，
所以符号门禁只钉跨翻译单元的 `SweetRSCDatabase::lookup`／`stageName`（`strings -a` 里实测各一次）——
手写一条 `__ZN16IOQcomRSCChannel13queryCommandDBEv` 会让这一版内核被自己的门禁拒掉。

**C3-02 由此收口**：资源驱动可共享只读元数据（同一份 DB、同一个判据、同一个顺序，两个读者给出同一份答案），
而普通外设拿到的只有 `SweetRSCResource` 这个纯结构（地址、entry、aux 区间、unit／width／vcd、电平表、匹配数与拒绝 stage），
没有任何"把原始映射交出去"的接口；投票仍走旧查询与旧执行器，块上一次写都没有。

### C3-03 原样提取现有 ACTIVE 执行器

- **目标**：把现有 TCS 执行代码提取为内部模块，同时保持原服务为唯一硬件 owner。
- **前置**：C0-01／02。
- **需求与边界**：只移动寄存器访问、现有 IRQ/event 配合、触发／完成／清理机制；保留原 gate、顺序、期限、Busy 和故障保留。原 VRM／ARC／CN0 调用通过内部适配层原样执行，不在这次改聚合或通用接口。
- **本地检验**：对已有命令形状比较提取前后 MMIO／结果轨迹；现有 stale IRQ、超时和 cleanup 回归不弱化；适用 ARM64 编译。
- **实机检验**：至少一个既有自然 ACTIVE 请求取得新 IRQ 完成，原消费者功能及普通路径保持；日志明确仍由旧服务持有硬件。
- **预期结果**：公共执行器已有可复用实现，硬件所有权尚未转交。
- **失败归属**：C3-03 extraction／sequence／completion；算法差异需撤出本卡另建修复。

**0143 验收（C3-03 收口）**：环 `logs/sweetlog-0143-0.bin`（4 198 400 字节，sha256 `1be482ae…`，
`total=347298`，`flush_sequence=28`，`wrapped=false`，54 619 行），默认档一次启动。自证句只有一条，且正好压在
这次开机第一条自然完成行上方——中间没有任何别的行：

```
2110 [INFO] SWEET RPMH IRQ: INTID37 configured; ACTIVE completion on provider workloop, no status polling
2111 [INFO] SWEET RPMH: executor=module tcs=0 commands=3 open=0 hardware-owner=SweetRPMh
2112 [INFO] SWEET RPMH IRQ: fresh ACTIVE completion tcs=0 commands=3 entries=1; cleanup pending
2113 [INFO] SWEET RPMH: token=1 ldoa14 ACTIVE ACK enable=1 mv=1800 mode=7; physical state unverified
```

两行描述同一笔事务（`tcs=0`／`commands=3`），`open=0` 表示交给模块的每个命令槽都拿到了完成位——
这正是旧代码会以 `incomplete-irq` 退掉的形状，这里是零。**完成路径没有变**：`tcs=0` 的完成 10 条，`entries` 从 1 连到 10；前十行里 6 条的下一行就是它那笔的 `ACTIVE ACK`（token 1–6：`ldoa14`／`ldoa17`／`ldoc3`／`ldoa5`／`display ARC`／`CN0`，`enable=1`／`mv`／`mode=7` 与 0142 同形）；中间 5 条 `commands=1` 的完成行（2126–2130）连排，ACK 落在最后一次完成之后——**完成位与命令槽从来不一一对应**，这是旧行为而不是新模块的口味；第 7 条 `ACTIVE ACK` 出现在下一次提交之后。
另有 4 条 `SWEET MDSS NOC IRQ: fresh ACTIVE completion tcs=2 …` 属显示 NoC 自己的路径，审计正则不匹配它们，
文档里说"10 条"指的就是 RPMH 这条。整环 `[ERROR] … SWEET RPMH` **零条**，四条放弃路径
（stale／posted-write／incomplete-irq／超时）一条都没走到，WARN 清单与 b32 逐条相等。

**与 0142 的差只有开机前缀长度**：`channel` 判定的每个字段逐字相等（9 行、`reads=10`、`writes=0`、`query=ok`、
`query_reads=292`、`query_row=CN0`、`owner_agrees=true`、4 窗口／3 区间、`id=0x10700 tcs=8 ncpt=16 irq=37`）；
整份 verdict 里只有行号（154 对 164）与 workloop 唤醒条数（13 对 12）不同——这两项是抓环时刻的函数，不是契约。
完成条数同理：0142 是 11 条、0143 是 10 条，差别只在于拉环前设备跑了多久，钉住的是顺序与计数器单调，不是总数。

**"原样提取"钉在测试里，不在文档里**：`tools/tests/test-mh2lm-rpmh-executor.py`（478 行）Part B 内嵌
**提取之前**录下的轨迹字面量（839 条记录、六个场景：empty、rail-live、rail-blocked、rail-missing、rail-complete、rail-expired），
逐条比对 owner 改走 `arm/step/verify/stageCleanup` 之后实际发出的读写序列与结果；字面量是常量，不从模块常量重算，
所以模块改了顺序而测试跟着变这件事在结构上不可能。Part A 单独钉模块形状：四个 static 的判定表、
`nm` 只认四个 text 符号且零 data 符号（模块不记事务）、头文件不引 IOKit 以外的东西。
两条负向对照做过并留着：把 `verify()` 里 `IRQStatus` 与 `Status` 两次采样对调，Part A 立刻失败；
把 owner 收尾的 `IRQEnable`／`IRQClear` 顺序对调，轨迹部分在 `rail-complete` 上以
`assert [l for l in body if l[0]=='W'] == writes` 失败。15 个夹具串行 15/15 PASS。

**打包侧这次真踩到一个坑**：`step()` 的 mangle 里 `uint64_t` 在主机上是 `unsigned long`（`…PbmmPj`），
在产品上是 `unsigned long long`（`…PbyyPj`）。第一版门禁照主机目标文件抄，于是**正确的内核被自己的门禁拒掉**；
从 `llvm-nm-19 ci-0143/kernel.development.mh2lm` 重抄四个 mangle 后 `GATE-MATCHES-PRODUCT`，之后打包一次通过。
规矩照旧：符号门禁一律从产物抄，不从本机目标文件抄。

**旗标与环双向一致**：`--c3-03` 蕴含 `--c3-02` 与 `--c3-01`，判 0143 **严格 rc=0**（没有 `--allow-baseline-drift`）；
同一份环用 `--c3-02` 判会被拒（`the ring carries the executor row: judge it with --c3-03`），
0142 用 `--c3-03` 判也被拒（`the C3-03 ring must print the ownership row`）。

**C3-03 由此收口**：ACTIVE 路径的寄存器访问、既有 IRQ 配合、触发／完成／清理机制已经在一个**无状态**模块里，
owner 仍是唯一硬件所有者——TCS 选择、按类准入、面板栅栏、`IRQEnable` 值组成、GIC 布防、期限、Busy 判定、
失败保留与全部日志措辞都还在 `SweetRPMh.cpp`；模块拿不到 map、IRQ 源、定时器或锁，这一点由代码形状保证，
再由环里那条 `hardware-owner=SweetRPMh` 与它下面那条完成行共同作证。0143 的双默认树因此成为 C3-04 的起点。

**0144 不安排上机**：那是收口文档提交 `7dd55ed` 触发的 CI 构建，其构建树与 `ci-0143-buildtree/xnu` 逐字节相同，内核除 `__CTF`（类型元数据，614 243／617 906 字节）外逐段相同——同一份代码，"文档提交不改物理行为"由产物本身证明，
不必再费一次设备周期；0143 的环就是这一卡的实机证据。

### C3-04 启动时由 channel 作为 apps 执行器唯一 owner

- **目标**：通过启动时选择，使 apps DRV 的全部实际写入与 IRQ 生命周期从一开始就归属 channel，旧服务不再触碰 TCS。
- **前置**：C3-01、C3-03；C0-02 的 apps 交接条件全部闭合。
- **需求与边界**：owner 在 channel 首次写入前确定（编译期或显式启动参数），旧 SweetRPMh 执行路径在该模式下不初始化写能力、不注册 IRQ handler；既有 VRM／ARC／CN0 客户端经兼容层使用同一执行器，不能留两套 MMIO。不做运行中交接，不涉及排空旧在途状态。保留单笔在途及 Busy；不改 GIC 通用架构、不加队列／缓存。回退方式为选择旧 owner 的已验镜像／参数。
- **本地检验**：两种 owner 模式下仅一方可写、IRQ 仅一方注册；新模式下旧入口写尝试被拒绝；命令顺序与 C3-03 一致。
- **实机检验**：新 owner 模式启动，记录 owner 选择、旧写计数为零、channel 完成既有自然 VRM／ARC／CN0 请求的 IRQ／cleanup；USB／根卷、显示、触摸回归。
- **预期结果**：唯一 writer 和 IRQ owner 已迁移，资源语义不变。后续 C3-05 起与 C4 依赖本卡，不依赖 C3-04b。
- **失败归属**：C3-04 owner-select／IRQ-owner／dispatch；新算法问题不能靠兼容回退隐藏。

#### 实现与本地证据（C3-04 代码完成，0145／0146 两轮构建失败已修，待 0147 实机）

**切换点是设备树，不是启动参数**：固件对启动参数是 607 字节硬上限，现役已占 605 字节（见 C1-02），
所以这一卡的两个开关只能是属性——`rsc-channel` 上的 `sweet,rsc-executor`（4 字节，值 1）与
`display-rpmh` 上的 `sweet,rpmh-executor-delegated`（4 字节，值 1）。生成器 `--rsc-executor` 要求同时给出
`--rsc-channel` 与 `--rpmh-irq`，**拒绝半边树**：只让 channel 接管而没有 owner 退位，或者 owner 退位而没有 channel，
都会得到一次"两个都不能写"或"两个都在写"的启动，两种都不许输出。派生树与 C3-03 树只差两条属性加 channel 的第四个 range
（GIC 窗口 `0x17a00000/0x10000`，`23924 → 24020` 字节，`999e2705… → a81e8b56…`），
摘掉两属性并截回 range 后 `adt.serialize()` 与查询树逐字节相同——"切换只动一处"因此由产物自证。

**owner 在第一次写入之前就定了**：`SweetRPMh::start()` 先读自己的属性得到 `fDelegated`，再调
`sweet_apps_owner(delegated, haveIRQServices)` 求出唯一 owner（`Channel`／`Service`／`None`），随后所有写入口
（`ensureExecutor()`／`execute()`／`tcsWrite()`）都先问 `executorDoor()`：门没人发布就**打一条行、返回失败、绝不自己写**。
delegated 模式下 `SweetRPMh` 不 map TCS／DRV／GIC，只把 locator 与 command DB 以 `kIODirectionIn` 映射，
并用 channel 已读到的身份做 `adoptDelegatedIdentity()`——兼容层拿到的是同一个执行器，不是第二套 MMIO。
"不初始化写能力、不注册 IRQ handler"因此在代码形状上成立：`fRSC`／`fTCS` 为空、`fMode` 必须为 2 才可能走到旧路径、
IRQ 源只在 `!fDelegated && fMode==2` 时创建，`SweetAppsDoor` 的表也只在认领线路处（`setupIRQ()`）发布，
一个从不取线的启动不发布任何空表。

**门是唯一交汇处**：`SweetAppsDoor.h/.cpp` 是一个函数表加一对 `publish`／`withdraw`。发布走一条懒分配的
`lck_mtx` 做比较交换，**第二个发布者被拒**（`line table refused; another owner published first; no writes`），
撤回只接受"真的还挂着"的那个指针，所以退位的旧 owner 拆不掉继任者。半边表（缺 `context`／`name`／`read`／`write`／`irq`）
被拒并点名 `detail=incomplete-table`；锁分配失败是 `detail=lock-absent`，都不静默降级。

**线路是第二份实现，旧序列冻结**：`SweetAppsLine.h/.cpp` 提供 `geometry`／`claimable`／`program`／`retired`／
`verify`／`mask`／`unmask`／`admit` 八条静态判据，全部只读写 GIC 那一页。这里刻意**不**把 `SweetRPMh` 里那段内联序列统一进来：
那段有五个设备环作证，统一它就等于改一条本卡不该碰的路径（结论与理由写在补丁正文里）。

**channel 作为执行器**：`sweet,rsc-executor` 让节点给出第四段 range，`IOQcomRSC` 映射它、校验控制器几何
（`ctlr=0x12 typer=0x7b041d`）、确认该线当前不是活的、自建 `IOWorkLoop` 与一个 `IOInterruptEventSource`，
然后才发布门。上半部只做三件事——mask 线路、记一次进入、`interruptOccurred()` 叫醒线程——完成回调在
work loop 线程上跑，且**调用前已释放 `fLock`**，所以客户端可以立刻从门里回来投下一笔。写走 `tcsWrite()`：
窗口外拒绝、非执行器模式拒绝、记账每一次被接受的 store，顺序规则沿用通道原有那套；线路只在一次触发被准入之后才 unmask，
停止时先 quiet 再摘 event source。启动行因此是 `windows=5 ranges=4 … executor=channel`，
停止行是 `… executor=channel` 加一条 `phase=executor-stop … door=withdrawn`。

**旧 owner 变成客户端**：`phase=delegate result=0 owner=IOQcomRSCChannel stores-here=0 irq-handlers-here=0` 是这一侧的
唯一新句子，后面跟着原有的那笔自然完成；ownership 行改成 `executor=delegated … hardware-owner=IOQcomRSCChannel`。
两条**描述现实**的行按模式换词：`phase=irq … detail=described-for-executor` 与
`phase=tcs … detail=observed-before-owner`（旧树仍是 `described-not-armed`／`observed-not-owned`），
所以单看一份环就能回答"这一轮谁持有硬件"，不需要外挂上下文。
`SWEET APPS DOOR:` 前缀是新加的诊断面，旧路径的诊断措辞一个字节都没动。

**本地检验**（全绿，串行跑）：`test-mh2lm-rsc-executor-door.py`（新，驱动两种 owner 对同一份夹具：同一票产出同一串 TCS store 轨迹，
delegated 侧只 map locator+DB、不 map 数组、不布防、采纳 owner 身份、自写入计数为零，门被撤回后**带行、缺 token、零写**）；
`test-mh2lm-rsc-channel.py`（执行器接受档 + 三条拒绝档 + 两条模式措辞断言）；`test-mh2lm-mdss-adt.py`（组合、两处半边拒绝、
可字节还原、CLI 等价）；`test-mh2lm-display-service.py`／`test-mh2lm-rpmh-executor.py`／`test-mh2lm-rpmh-completion-irq.py`
跟着改动后仍 rc=0；`scratch/c3-03/run-suite.sh` 十六夹具串行仍全 PASS。

**包门禁**：`out-bs/mk16k-0145.sh` 在 0143 的全部钉子上加 13 个跨翻译单元符号（门 5 ＋ 线路 8）、23 条 C3-04 字面量，
以及从 0143 树派生 `adt-16k-3g-clock-executor.adt` 的断言组：两节点属性集、解码后的 range 等于
`RPMH_WINDOWS + IRQ_WINDOWS[-1:]`、arm-io 子节点顺序、摘除后逐字节还原成查询树、两条 CLI 等价 `cmp`、
半边 `--rsc-executor` 必须拒绝且不留文件。

**审计器**：`--c3-04`（蕴含 `--c3-03`）判三项——通道（该模式 `windows=5`／`ranges=4`、map 顺序以 `apps-gic length=0x10000` 结尾、
两条模式措辞）、ownership（`executor=delegated`／`IOQcomRSCChannel`、仍是第一笔自然完成正上方）、交接（门只一条、在 start 之前、
`stores=0 writes-at-start=0 workloop=own`、`writes=0`、旧侧无 `INTID37 configured`、无 `executor=module`、无 ERROR）。
合成环由 `make-channel-ring.py … delegated` 从 0143 环改写而成；`selftest-channel-auditor.py`
**PASS - 71 变体**（原 48 ＋ 1 个合格交接环 ＋ 19 个应当被拒的交接环 ＋ 3 个跨人格互拒）。跨人格互拒措辞：
delegated 环用 `--c3-03` 判 → `the ring hands the hardware to the channel: judge it with --c3-04`；
0143｜0142 用 `--c3-04` 判 → `the ownership row still names SweetRPMh: this tree did not delegate`／
`the C3-04 ring must print the ownership row`。

**0145 编译失败：一处访问控制，与流水线无关**：`.github/workflows/circleci.yml` 没被改过（最后一次改动是
`1023fef`，远在本卡之前），失败是真编译错——整份 24 MB 日志里只有一条 `error:`：

```
IOQcomRSC.cpp:417:26: error: 'tcsRead' is a protected member of 'IOQcomRSCChannel'
IOQcomRSC.h:202:11: note: declared protected here
```

即准入时那个读状态字的 thunk 写成了文件作用域的自由函数，而 `tcsRead()` 是成员。修补办法不是放宽成员可见性
（那等于把这一卡费力守住的数组交给本翻译单元里任何调用者），而是把 thunk 变成 `IOQcomRSCChannel` 的静态成员，
和既有那排 `door*` 适配器并列——它们存在的理由本来就是"用函数指针的 ABI 走进私有状态"。类对外 API 一个符号都没多。
产物也自证失败：`xnu-kernel-0145.gz` 只有 175 400 B，解开是个 1 551 872 B 的 tar，里面只有 `kdk-libs/*.nm.txt`／
`*.members.txt`／`arch.txt` 与空 `SHA256SUMS`，**没有 `kernel.development.mh2lm`**，所以 0145 不可烧、也无法出环。

**0146 编译通过、链接失败：两个新单元没进构建清单**：`iokit/conf/files.arm64` 里没有
`SweetAppsDoor.cpp`／`SweetAppsLine.cpp` 这两行。文件照样能编——邻居 `#include` 了它们的头，编译器一句话都不会说——
然后链接器把清单整个倒出来：`sweet_apps_owner`／`sweet_apps_door`／`sweet_apps_door_publish`／
`sweet_apps_door_withdraw`、`sweet_rpmh_irq`（`sleh.c` 的引用，定义也搬进了那个单元）、以及
`SweetAppsLine` 的八个方法全部 undefined，收尾一条 `clang: error: linker command failed`。
方向是明确的：**新增翻译单元必须同时进构建清单**。现在补了 `test-mh2lm-build-file-list.py`——`iokit/Kernel/arm`
下每个 `.cpp` 都必须在 arm64 清单里、本卡这两个单元按名字钉住、重复登记算错，并且拿"删掉一个名字"的清单喂同一个
扫描器做反证（它必须只报那一个文件），因为一个永远不会失败的检查不算检查。那次日志里仅有的两条 warning 是
`-Wnewline-eof`（本卡新增文件末尾缺换行），也一并补上了。

**0147 通过**：编译、链接、打包全绿，14 个映像。委派档 `…-rscexecutor.img` 已上机取环（见下）；
对照档 `boot-16k-0147-gpioirq-panel-pipeline.img` 已就绪待启——同一个内核、树里没有那两条属性，
出来的环应当与 0143 的 `executor=module … hardware-owner=SweetRPMh` 无从分辨，这是本卡"双默认树"的另一半。

**为什么本地没先知**：五个夹具为了让用例能戳成员，把 `class X` 改写成 `struct X` 再编译，这一步把访问控制整个删掉了，
于是"别的作用域能不能碰到这个成员"在本地永远不成立。现在编译这两个受影响单元的夹具
（`test-mh2lm-rsc-channel.py`、`test-mh2lm-rsc-executor-door.py`）各自多走一遍**不改写**的语法检查：
用出厂原文（只摘 `#include`）加假 IOKit 前导跑 `clang++ -fsyntax-only`，再用同一份 TU 追加一个文件作用域辅助函数
作为反证，要求编译器**拒绝**它。反证那次必须看得见拒绝，否则门禁自己就是瞎的。

#### 实机验收：0147 委派档（C3-04 前半，2026-11-28）

环 `logs/sweetlog-0147-0.bin`（4 198 400 字节，sha256 `906d8c9b…`；头 `total=4194304 flush-seq=316228
capacity=9`，54 577 行，已回绕），内核 `a848acb8…`（`ci-0147/`，18 008 112 字节，13 个新符号全落在
`fffffe0007afb3xx`）。`audit-clock-log.py --c2-03 --c3-04` 严格 rc=0，无 `--allow-baseline-drift`。

启动顺序与设计逐行对上：145 `phase=irq intid=37 detail=described-for-executor` → 146–149 四段 range（末条
`apps-gic length=0x10000`）→ 150 `phase=identity id=0x10700 … tcs=8 ncpt=16` → 151 `phase=command-db …
detail=read-only-map` → 155 `db-query … CN0 … reads=292` → 156 `tcs irq-enable=0x1 irq-status=0
detail=observed-before-owner` → 162 `SWEET APPS DOOR: phase=publish result=0 owner=IOQcomRSCChannel intid=37
stores=0 irqs=0` → 163 `phase=executor … windows=5 ranges=4 stores=0 writes-at-start=0 workloop=own` →
167 `phase=start … reads=10 query-reads=292 writes=0 … irq=37 query=ok`。第一次飞行时交接四行按设计顺序到齐：
1778 `identity adopted from hardware owner id=0x10700 config=0x800c8104 ncpt=16`（旧侧不再自己读块）→
1779 `phase=install result=0 detail=completion-client` → 1780 `phase=delegate … stores-here=0
irq-handlers-here=0` → 1781 `phase=line … owner=channel; ACTIVE completion on own workloop, no status
polling`；1782 是 `executor=delegated tcs=0 commands=3 open=0 hardware-owner=IOQcomRSCChannel`，**正下方 1783
就是本次开机第一笔自然完成**。全环 10 条 `SWEET RPMH IRQ: fresh ACTIVE completion`（4 条 rail `commands=3` ＋
6 条 ARC `commands=1`）一笔未丢，7 条 `ACTIVE ACK`（token 1–7）照旧到达，LDO／ARC 电压与 `CN0` 请求全部落地，
USB／根卷、显示、触摸回归照旧。不该出现的三样东西一条没有：`INTID37 configured`、`executor=module`、任何旧入口
store 行（`stores-here=0`、`phase=start … writes=0`、`phase=executor … stores=0`）。时钟 verdict 不变
（`maps=4 reads=19 writes=0`、`measurement=absent`），非时钟 WARN/ERROR 与 b32 逐项相等、环内 `[ERROR]` 零条。
另 4 条 `SWEET MDSS NOC IRQ:` 完成（`tcs=2`、`entries` 1→4）属显示 NoC 自己的服务，与本卡无关，审计器按服务名分开。

**审计器两个真发现，改的都是判据，不是驱动**：

1. `entries` 在委派档**必须是 0**。`fIRQEntries` 只在 `SweetRPMh::handleIRQ()` 里递增，而这一档根本没装那个
   handler——同一份环用 `irq-handlers-here=0` 自己作证。所以"entries 单调增长"是 C3-03 的判据，不是 C3-04 的；
   换成"恒为 0 才算唯一调度者"之后，`entries>0` 反而成了"有第二个调度者"的反证，正是本卡要防的那件事。
2. identity 与 irq 两行的**先后**不能钉死。模块档里旧侧自己读了块，channel 的独立读必须在它之后；委派档里旧侧
   根本不读，它印的是"采纳 channel 发布的那份身份"，两行谁先只由探测顺序决定（实环 150 在 1778 之前）。硬钉一个
   方向等于把偶然当契约，于是改成：委派档断言环里**没有 `pre-v3 RSC` 那一行**、并按 id／ncpt 值比对，顺序只在
   模块档断言。

合成环跟着换成设备形状（`delegated` 变体：完成行 `entries=0`、旧侧身份行换成 adopted 行、交接四行按 1778–1781
的顺序），门禁从 **71 变体扩到 76**，新增五个必须被拒的变体全 PASS：`delegateentries`（旧侧计数它并不拥有的中断
条目）、`delegatenoinstall`（线路布防却没有完成客户端）、`delegatelinefirst`（先拿线后装客户端）、`delegateprev3`
（委派的旧侧还在自己读块）、`execinstall`（模块环带着交接那两行）。0143 环用同一个审计器复判仍 rc=0
（`mode=module entries_max=10 handler=display-owner`），两个方向没有互相污染。

#### 实机验收：0147 对照档（C3-04 收口，2026-11-28）

同一个内核、树里没有那两条属性，环 `logs/sweetlog-0147-1.bin`（4 198 400 字节，sha256 `093735bf…`；
`total=4194304 flush-seq=307347 capacity=8`，54 535 行，已回绕）。`--c2-03 --c3-03` 严格 rc=0，
**verdict 与 0143 逐字段相同**——机器比对了 0143 那次判定的全部字段，只有四处数字不同，且都不是契约：
`executor.line` 2111 → 2108、`channel.line` 154 → 156、`workloop.rows` 13 → 12、`workloop.line_count`
4291 → 3834（后者是抓环时刻的函数，不是行为）。

模式由设备树决定，两个方向都在硬件上量到了：同一份驱动，136 行仍是旧侧自己读块（`pre-v3 RSC id=0x10700
config=0x800c8104 ncpt=16 ACTIVE=2`），140–141 行门由旧侧发布且 `owner=SweetRPMh`；委派树里同一行是
`owner=IOQcomRSCChannel` 且旧侧不再读块。通道侧回到只读形态：148 `phase=irq … detail=described-not-armed`、
149–151 三段 map（没有 gic 那段）、152 identity、153 `command-db … read-only-map`、154 `db-query … CN0 …
reads=292`、155 `tcs … detail=observed-not-owned`、156 `phase=start … windows=4 ranges=3 reads=10 writes=0 …
submit=not-ready`；2107 `INTID37 configured` 在场、2108 `executor=module tcs=0 commands=3 open=0
hardware-owner=SweetRPMh`、2109 就是本次开机第一笔自然完成（`entries=1`）。十笔 RPMH 完成＋4 条显示 NoC 完成、
无 ERROR；委派档那五行（`executor=delegated`／`phase=delegate`／`phase=install`／`phase=line`／`identity
adopted`）一条皆无。`--c3-04` 判这一环被拒：`the ownership row still names SweetRPMh: this tree did not delegate`。

**C3-04 由此收口**：执行器的 owner 在启动时由设备树选定，选完之后另一方碰不到硬件——委派档里旧侧零 store、
零 IRQ handler、连块都不读，对照档里通道只读三窗、`submit=not-ready`、`armed=false`；两种树在同一个内核上
各自产出与设计一致的环，且判据按模式分叉而不是靠旗标放宽。

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

#### 实现与证据（C3-05，0149 双默认树验收通过）

**队列只是存储，不是策略**：`SweetAppsQueue.h/.cpp` 固定八槽、一个头、一个计数，加上 accepted／dispatched／high-water
三个计数和入队时盖的序号；不分配、不加锁、不打印、不调 IOKit。`push()` 满了**拒绝**而不是丢最旧（同一资源的两票一旦折叠就没有
定义好的先后），拒绝不盖序号；记录自包含，因为调用方的栈和活对象在请求真正发出时早已不在：kind、owner 的客户端槽、资源标签、
载荷和**等待前就已发出的 token** 都随记录一起走。`find()` 只回答去重需要的那一问（同 kind、同 client、同 rail），
`waiting()` 回答 detach 要问的那一问（这个客户端还有没有东西在排）。

**准入被拆成两半**：`validate()` 是准入前的筛子，拿一份**还没存到任何地方**的提案，按同一批表和同一份已提交台账检查，
在任何 token 存在之前就拒掉，所以被拒的提交不动调用方上一次的完成、不动 token 序列，也没有写入、没有状态槽、没有队项。
它故意是同一套规则的**第二次读**而不是调进 launch：launch 需要一个可失败的记录，而先发 token 再筛会把一次笔误变成一次丢失的完成。
筛子判不了等待期间会变的东西（TCS、执行器门、模式、隔离），所以 dispatch 再判一次，并把它记在排队请求**已经持有的 token** 上。
直接（非排队）dispatch 失败时不发布状态、不写 Pending：调用方从返回值知道结果，这和本卡之前一样。

**派发与退休**：`retireFlight()` 释放数组、清 `fBusy`／`fCurrent`、取消超时，然后 `dispatchNext()` 取队首。
本卡最要紧的一处修补是 host 夹具逼出来的：**从队列里取出来的请求原先没有被设成“当前”请求**，于是它飞在天上的整段时间里，
同一资源的第二次提交只跟队列（已经不装它了）和 `fCurrent`（还描述着上一笔）比，去重筛正好在它该尽责的那条路径上变弱。
现在 `dispatchNext()` 在 launch 之前把要发的请求设为当前，`launch()` 在“这次派发不需要任何写入”时再清掉，
所以一笔没碰到数组的请求不会替它的后继占着筛子。隔离状态被设置和 `stop` 时 `drainQueue()` 逐条拒掉排队请求（`kIOReturnNotReady`／`kIOReturnAborted`）并抬 quarantined。

**0148 的实机结论是“示意写错”，不是“队列错了”**：那一轮把示意放在两条 rail 上（各抬 8 mV），环里先是两条
`phase=refuse kind=rail-vote … detail=invalid result=0xe00002c2`，再是 `phase=selftest result=failed detail=pair-did-not-serialize`。
原因在准入筛里：这棵树给每条 rail 的都是点票（min == max），聚合取地板的最大值和天花板的**最小值**，第二个 callers 一抬地板就非法——
保护第一个客户端的规则同时禁止了第二次投票。旧代码还把步长夹在 `maxMV` 上，那样算出来的票**等于已持有的票**，就算合法也是静默空操作，永远不会发一趟。
同一份环里 7 条普通票全部是 `seq=0 wait=direct`（去重筛与派发路径都在工作），没有一条 enqueue／queued 行——所以那一轮既没证明排队，也没弄坏原有路径。

**示意改到 CN0 寄存器带宽**：这是这台机器上唯一**可以合法搬动**的量。寄存器带宽按最大值聚合，所以“要得更多”永远合法、永远真的发一趟。
步长取 `scale()` 舍入到的 y 场一个整编码单位（本件 `unit=20000`、`width=4`，即 **20000 B/s**），绝不按百分比：
比一个 y 单位更小的抬升会舍入到同一个 y，聚合丝毫不动。两个目标都先用 `scale()` 验过才发 token，两个客户端都是新槽位（否则第二次会被合并而不是排队）；
本次启动在第一次 `retireFlight()` 时落地，那时 `fRegisterAggregate==0`、`fRegisterOwned==false`，所以环里应当读到
`bytes0=20000 bytes1=40000`（资源 0）、两个不同 client 与 token、`depth=1 high-water=1`，以及启动自己那笔 300 000 000 B/s 的 CN0 票把两者都涵括在内的既成事实。
两笔票按和其他票一样**留驻**（撤票是 C3-06）。

**本地证据**：19 个编译 `SweetRPMh.cpp` 的主机夹具加三个相邻夹具串行全过；`test-mh2lm-display-noc.py` 的 C3-05 段用 `step()`（arm + 两拍，不带队列断言）
走完“直接派发 → 入队 → 退休后从队里派发”，断言两笔都留在 `SweetRPMhPending`、第二个客户端在**等待中**和**飞行中**再提交都被 `already-outstanding` 拒（`kIOReturnBusy`）、
队里那条派发后 `fCurrentValid && fCurrent.client==2`，并按行序验证“直接 → 入队 → 队列派发”三段；不带属性的对照启动断言 `!fQueueSelfTest` 且没有 selftest 行。
`test-mh2lm-rpmh-resources.py` 用逐票的路径证明同一资源两个客户端按序合并。
审计侧 `audit-clock-log.py --c3-05` 重写成按**合同**判（每个 token 只派发一次、direct⇒seq=0、queued/enqueue⇒seq≠0、入队深度与在途数对账、
包装截断后重新对齐、任何时刻 `depth<=1`、隔离行之后不再派发、以及 CN0 那对数字），`selftest-queue-auditor.py` 17 个变体全过——
其中 15 个反例各自打掉一条断言，另外两个互相拒收对方的环（带队列行的环用 `--c3-04` 判被拒、0147 的环用 `--c3-05` 判被拒）。
0148 的对照环按 `--c3-04` 判**通过**：7 条 `seq=0` 直接派发、0 条 selftest／enqueue／queued／queue-full，即“不装属性的树只留无条件打印的直接派发行”。
同一份环里还有 51 条 `SWEET FG ACCESS … op=grant-wait`（燃料计整轮没等到授权位），它属于电池那个消费者，脚本只在显式
`--fg-grant-timeout-observed` 下接受并把它单独记进报告，不当作本卡的通过或失败。

**0149 的两张环**（同一内核、两张默认树，唯一差别是 ADT 是否带 `sweet,rpmh-queue-selftest`）。补丁 `0252` 收上面两处；
`mk16k-0149.sh` 的十五张镜像与 0148 同树同参数，候选镜像名改为 `rscqueuecn0`，文本门里那条 selftest 行换成新措辞，
用 0148 的内核空跑一遍恰好在“缺 C3-05 要求”那条门失败，说明其余门禁都已跑到并被收紧。

候选环（`sweetlog-0149-0.bin`）里，示意的三行按合同到齐：`phase=dispatch kind=register-bandwidth client=1 resource=0 token=2 seq=0
depth=0 wait=direct` → `phase=enqueue … client=2 … token=3 seq=1 depth=1` → `phase=selftest resource=0 bytes0=20000 bytes1=40000
client0=1 client1=2 token0=2 token1=3 result0=0 result1=0 depth=1 high-water=1`，随后 `token=2 CN0 ACTIVE ACK bytes/s=20000`，
排队那笔在第一笔 ACK **之后**才以 `seq=1 depth=0 wait=queued` 派发，并回来 `token=3 CN0 ACTIVE ACK bytes/s=40000`：
两趟都是真飞行（各自 `commands=1`、各自 fresh ACTIVE completion），间隔正好一个编码单位。**本次开机自己那串
`ldoa14 → ldoa17 → ldoc3 → ldoa5 → display ARC corner=4 → CN0 300000000 ×2` 的资源、数值、顺序一条没变**，只是被这对示意插在中间；
`executor=delegated tcs=0` 照旧。审计判决：11 条队列行、0 条 refuse、`truncated=0`、基线“非时钟 WARN/ERROR 与 b32 逐项相等”。

对照环（`sweetlog-0149-1.bin`，执行器树、无属性）判**通过**：7 条 `seq=0 wait=direct`、0 条 selftest／enqueue／queued／queue-full；
更硬的一条是**全部 45 条 `SWEET RPMH`／`SWEET RSC` 行与 0147 的候选环逐字节相同**——属性不装时，队列在线上一个字节也没改。
两个环都各有百条左右 `SWEET FG ACCESS … op=grant-wait`（本机燃料计整轮等不到授权位），单列进 `fg_grant_timeout`，记在电池消费者账上。

### C3-06 完成排队请求的取消与客户端退出合同

- **目标**：客户端退出后，不再执行其尚未发出的请求，也不访问已释放对象。
- **前置**：C3-05。
- **需求与边界**：区分 queued 与 triggered。只撤销尚未执行的请求；已触发请求继续由 provider 持有并终结／隔离，不把取消当成硬件撤销，不自动发送反向票。
- **本地检验**：取消队首／队尾、退出与完成交错、callback 排队后 consumer 退出；释放对象后无访问。
- **实机检验**：受控诊断客户端在 B 排队期间退出，B 无写事件；已允许的 A 正常完成。真实 provider 热终止若不可触发，注明仅 host 证据。
- **预期结果**：一个请求的取消不会撤掉其他客户端票，也不会提前回收在途存储。
- **失败归属**：C3-06 cancel／detach／lifetime。

#### 实现与本地证据（C3-06 代码与宿主证据完成，待 0150 实机）

**取消只对尚未执行的请求成立。** 这半句决定了整张卡的形状：队列里的记录属于"还没跑"，`fCurrent` 属于"已经在跑"。
`cancelWaiting(client,token,why)` 先在记录还在队里时结算归属——别人的等待回 `detail=not-owner` 加 `kIOReturnNotPermitted`，
不属于自己的 token 不动队列顺序——再让记录出队、置 `Failed(3)`/`kIOReturnAborted`，打一行 `phase=cancel … result=ok why=%s`。
已经被取出的 token 走另一条：`detail=already-triggered` 加 `kIOReturnBusy`，**不**打断飞行、不撤回任何票，
因为"停掉半个事务"正是本设计没有的 rollback（共享约束 3.5）。

**退出把"还在等"的一并带走，但先结算全部拒绝理由。** `detachClient()` 的四个前检查（`in-flight`／`rail-vote-retained`／
`display-vote-retained`／`register-vote-retained`）全部排在任何撤销之前，回 `Busy` 时客户端与它的等待记录**一个字段都不变**；
只有确定要成功的退出才调 `cancelClientQueue(client,"exit")`，把该客户端的新→旧逐条取出、各自一行、各自 token，
再从 `fClients[]` 抹掉身份。`phase=detach client=%u cancelled=%u depth=%u result=ok` 里的 `cancelled` 就是这批条数。
顺序反过来的写法（先抹身份再撤记录）会让派发器摸到一个已释放的槽，这正是卡里"释放对象后无访问"要挡的那件事。

**把数组降下来是票，不是撤销。** `withdrawRegisterVote()` 只改账本：只有当**别的客户端**的生效票还等于被撤回的那个值
（`others==told`）时才允许，否则 `would-change-hardware` + `kIOReturnNotPermitted`，且什么都不写；
`held==0` 是 `detail=no-vote` + `kIOReturnNotFound`。成功行是 `previous=… aggregate=… others=… hardware=unchanged`。
所以"把 CN0 回到 0"必须由某一笔**真投票**来做——示意里那笔来自已完成请求自己的客户端，`told` 取调用前一刻的
`fRegisterAggregate`（它在 `finish()` 里才前进，不在 launch 时），因此 `told` 与三笔里第一笔的字节率相等。

**存储层**：`SweetAppsQueue` 新增 `cancel(token,&removed)`／`cancelClient(client,out,cap)`／`cancelled()`／`findToken()`；
两条路径都把记录从数组里移出（后面元素的顺序不变），并计入 `fCancelled`，与派发计数分开记账；
`cancelClient` 按新→旧顺序写调用者的缓冲、超过容量就只写到容量为止但**全部出队**（调用者拿到的条数以返回值为准，
`detach` 的 `cancelled` 即此值）。`kSweetAppsQueueCapacity=8` 不变。

**示意图是三个消费者，不是两个。** 三笔 CN0 票各差一个编码单位（`base+3*step`／`+4*step`／`+5*step`，`step=cn0Step()=20000`，
`base=fRegisterAggregate`），第一笔直接飞、后两笔排队（`depth=2`、`high-water=2`）。第一笔完成后数组由它的票持有，
后两笔的账本条目才**可以**被撤回（`others==told` 成立），于是两种撤销各走一条：`fSelfClients[0..1]` 的账本条目由示意自己撤回，
`b1` 由调用者取消、`b2` 由退出带走。为什么必须是三笔：两笔时第一笔完成即把数组交回给出示意的第二笔，撤回它自己那笔会立刻
变成 `would-change-hardware`；三笔让"已完成的票持有数组、后面两笔只是账本"成立，撤销因此是纯账本操作。
六行按序到齐：`phase=cancel-selftest kind=register-bandwidth … depth=2 high-water=2; first in flight, two waiting` →
两行 `phase=withdraw … hardware=unchanged` → `phase=cancel … why=caller` → `phase=detach … cancelled=1 … why=exit` 那一批
→ `phase=cancel-selftest step=2 result=ok …` → 真实的降票 `phase=dispatch … wait=direct` → `step=3 result=ok`（`aggregate=0 owned=1 depth=0 cancelled=2`）。
`owned=1` 是"这一趟启动拥有过数组"的意思（第一笔完成时置位、从不清理），它也是那笔降到 0 的请求**真的**会飞行的原因
（`noWrite` 只在 `!target && !fRegisterOwned` 或 `fRegisterOwned && target==fRegisterAggregate` 时成立）。

**宿主证据**：`tools/tests/test-mh2lm-apps-queue.py` 增补按 token 撤（队首／队尾／不存在／零／已派发）与按客户端撤
（只撤自己的、新→旧、缓冲不足仍全部出队、计数与派发分开）；`tools/tests/test-mh2lm-rpmh-resources.py` 增补三段——
取消合同（别人的等待被拒且队列顺序不变、在途与邻座被拒、`Failed/Aborted` 之后账本不变）、退出撤销（新→旧、先结算拒绝理由、
`Busy` 时字段全不变）、账本撤销（`others==told` 才允许、`would-change-hardware` 一次写入都没有、`no-vote`）。
`tools/tests/test-mh2lm-mdss-adt.py` 增补派生侧：`sweet,rpmh-queue-cancel` 只出现在 display owner 上、恰好一条、
删掉它逐字节回到 C3-05 树、`--rpmh-queue-cancel` 不带 `--rpmh-queue-selftest` 或没有 owner 节点时拒绝且不落盘。

**审计器**：`--c3-06`（蕴含 `--c3-05`）按合同判，不按固定行数：两个被撤 token 在**整个环里**从未被派发；
两条 `withdraw` 行都落在示意图行与 `step=2` 之间，且 `previous < aggregate == others`、`hardware=unchanged`；
恰好三条 `detach`（两条 `cancelled=0` 是示意自己的条目、一条 `cancelled=1 depth=0` 是退出）；七行严格按序；
`b1-state=3`／`b1-result=0xe00002eb`（设备上真值，宿主 fixture 里的 `7` 只是夹具常量）；`cancelled=2`；降票行 `depth=0` 且在 `step=2` 之后。
`selftest-cancel-auditor.py` 跑 21 个变体：`ok` 通过，20 个反例各自打掉一条断言（没装属性、三笔没串行、`step=2` 报失败、
账本没交回、撤回声称动了硬件、`told` 超过落地那笔、降票在飞行没清空时被放行、深度谎报、token 复用、
`b1` 完成而非中止、退出撤了 0 条、只交回一条账本、三笔不是等步长、被撤的那笔竟然派发、`cancelled=1`、
结束仍有等待、`step=2` 出现两次），外加两条跨人格互拒：C3-05 的环用 `--c3-06` 判被拒（没有示意），
C3-06 的环用 `--c3-05` 判被拒（它排三笔而那张卡说一笔）。

**包门禁**：`out-bs/mk16k-0150.sh` 在 0149 的钉子上加 18 条 C3-06 字面量与三条撤回路径的拒绝字面量，
派生第三棵树 `adt-16k-3g-clock-executor-queueselftest-cancel.adt`（sha256 `8b92b1a0…`，只比 C3-05 树多一条属性，删掉即逐字节回到 C3-05），
并双向证明"没有 pair 就不许有撤回"（生成器拒绝且不落盘）。十六张镜像出自同一内核、同一参数表、同一固件：
候选 `rsccancel`（两属性），对照 `rscqueuecn0`（只有 C3-05 属性，环里应有同一对示意图行而**没有**撤回行），
其余十四张是旧树的控制。

#### 0150 实机：示意没在空窗里跑（已定点，`0254` 修，0151 待抓）

0150 候选环被判失败，但**取消合同本身没有被证伪**——坏的是示意驱动自己的时序：

- 环里只有三行错误：`phase=cancel-selftest step=1 result=failed detail=trio-did-not-serialize depth=4`；
  随后 `step=2 … detail=cancel-0xe00002eb b1-state=0 b1-result=0 ledger=0/0 exit-withdrew=0 lower=0xe00002eb depth=4`，
  `step=3 … detail=state-0 bytes=0 detach-0xe00002d5 aggregate=40000`。
- 起因：C3-05 的 pair 示意与 C3-06 的 step 1 挂在同一个 `retireFlight()` 上。pair 那一趟打印的是
  `dispatch … token=2 … wait=direct` ＋ `enqueue … token=3 depth=1` ＋ `phase=selftest … depth=1`，
  这一趟结束时**一笔在飞、一笔在等**，`fQueueTestRun` 同时置位，于是 step 1 在同一趟里被放行：
  三笔全部入队（`depth=2/3/4`），`trio-did-not-serialize depth=4`。
- `step=2`／`step=3` 是**失败即闭合**：`statusA.state` 不是 Complete，`cancelWaiting()` 根本没被调用，
  所以 `b1-state=0`／`b1-result=0`／`lower=0xe00002eb` 都是占位值，合同路径一行都没走到。
- 代价只有"多投票"：三笔示意请求事后照常飞行（`wait=queued`），三个示意客户端的 CN0 票留到这次启动结束；
  硬件没有被回滚、也没有被重复写。
- 修法（`0254`，只动 `SweetRPMh.cpp`）：两个示意都只在**开窗为空**（`!fBusy && fQueue.empty()`）时提交，
  否则静默返回、下一次退休再试；空窗判断放在 pair 的 `fQueueTestTries` 计数**之前**，等待不消耗次数。
  宿主 fixture 钉住两半：有飞行／有等待时零准入、零客户端、零行；空窗之后同一驱动真的排出一对并打印
  `depth=2 high-water=2`。审计器不需要改——它本来就要求 pair 行 `depth=1`、三笔 `depth=2 high-water=2`、
  `tail_dispatch == [a, lower]` 都 `direct/0/0`，0150 那种形状（深度 4、三笔真飞行）过不了其中任何一条。

**ARM64 单编译**：按真实 CI 参数（0149 buildtree 的 `.CFLAGS` 与保存的原生编译行，clang++-19＋内核头 shim）各编一遍改动前后的
`SweetRPMh.cpp`／`SweetAppsQueue.cpp`，两版都是 0 error，且**没有新告警类别**——只多出 `-Wunsafe-buffer-usage` 的实例
（213→270、7→15，该类对每处指针下标都报、全树普遍存在）。新代码没有绕过任何类型检查来通过编译。

**未上机的那一条边界**：`would-change-hardware` 故意不在设备上制造——它按设计打 ERROR，会落进审计的 `error_inventory`，
把一次健康的启动变成需要解释的启动；这条边界由宿主 fixture 与队列单测覆盖，实机只证明"能撤的都撤对了、降票是真票"。

#### 0151 实机：合同六行到齐，`step=2` 那一行被控制台截断（`0255` 修，0152 待抓）

0151 候选环 `sweetlog-0151-0.bin`（`743abe48…`、4 198 400 字节、54 610 行、未回卷）把取消合同读全了，逐字段与设计一致：

- `phase=selftest … token0=2 token1=3 result0=0 result1=0 depth=1 high-water=1`，两笔 `enqueue`（`client=4 token=5`、`client=5 token=6`）
  与 `phase=cancel-selftest … depth=2 high-water=2; first in flight, two waiting`；
- 两条 `phase=withdraw … previous=20000`／`previous=40000 aggregate=100000 others=100000 hardware=unchanged`；
- `phase=cancel … token=5 … result=ok why=caller` 与 `token=6 … why=exit`；`phase=detach … cancelled=0` ×2 与 `cancelled=1 depth=0`；
- `step=2 result=ok a=4 b1=5 b1-state=3 b1-result=0xe00002eb b2=6 exit-withdrew=1 lower=7 ledger=2/2 told=100000 depth=0`（见下）；
- 真实的降票 `phase=dispatch … wait=direct` 与 `step=3 result=ok exited=3 aggregate=0 owned=1 depth=0 cancelled=2`。

被撤的两笔全环从未出现在任何 `phase=dispatch`／`ACK` 行上，`executor=delegated` 与 45 条 RPMH／RSC 线缆行一条不变。
**但 `step=2` 那一行到 `b2=` 就断了**：xnu 的控制台把一行装进 `char str[SERIAL_CONS_BUF_SIZE]`
（`osfmk/console/serial_protos.h` 的 256 字节），`console_printbuf_putc` 只在 `pos < 255` 时存字节、换行也在这 255 之内，
超出的部分**静默丢弃、连换行一起丢**，于是 331 字节的那一行只剩 255 字节，尾巴粘到下一行上。这不是这一行独有的毛病：
同一环里 47 行被粘、45 行超 255（几乎都是用户态的 `[System Event]`），Sweet 只有这一行。

**判断**：这不是"容差"问题，是"生产者的行必须装得进缓冲区"。审计器加了与人格无关的一条 `console_rows`：任何 Sweet 行
不得出现第二个标记、最长不得超过 254 字节（255 字节里含换行，行尾不能丢）；0151 候选环正是**在这一条上被拒**，
其余人格因此没有被评估。对照环 `sweetlog-0151-1.bin`（`787a847b…`、54 643 行）用 `--c2-03 --c3-05` 判 rc=0、
`console_rows` 干净（1014 行、最长 236 字节），0 条 `phase=cancel-selftest`。

**修法（`0255`）**：只改这一行，不改控制台、不动其它子系统的行（那 47 行是别人的生产者在同一棵树上写出来的）。
字段在前、句子在后：本示意实测 203 字节、字段取满 253 字节，整行上限 254；机器字段一个不少，所以 0151 那种"只丢散文"的
截断仍然可区分——被截断的行依旧被 `console_rows` 拒。宿主 fixture 现在真的驱动第 2、3 步（pair 的两笔账本由公共调用造出，
示意本身由 C3-05 的 fixture 驱动），并断言两行都装得下。

**实机检验（0150，待用户抓环）**：候选环应读到上面六行，并满足：被撤的两笔在任何位置都没有派发出现在环里；
所有既有的 Rail／ARC／CN0 自然投票与完成行、`executor=delegated` 与 45 条 RPMH／RSC 线缆行一条不变；
对照环（`rscqueuecn0`）用 `--c3-05` 判通过、用 `--c3-06` 判被拒。真实 provider 热终止无法在本轮触发，
因此"退出发生在某笔正在飞的时候"只有宿主证据（`in-flight` 拒绝 + 字段全不变），实机这一半记为 host-only。

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
