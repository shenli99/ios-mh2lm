# C2：GCC 的最小本地时钟与 reset 能力

[返回总计划](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)。各卡默认遵守[总计划 3.5 共享约束](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)，卡内只写特有边界。状态：C2-01／C2-02 已在 0126 默认档实机验收通过（补丁 `0233`／`0234`，零写入）；**C2-08 已实现（补丁 `0235`，与 C1-05 合并），待 0127 实机**；C2-03 起未开始。

## 阶段目标与退出条件

按初始化、只读查询、debugcc 测频（C2-08）、普通门控、引用共享、voted、reset 分别增加能力。每卡只操作已批准的一个样本；尚不实现的 PLL／RCG／GDSC 保持 capability 关闭。统一寄存器访问模块随首次实际读写建立，不预分配 DMA 缓冲。

### C2-01 初始化 GCC 后端并取得基础资源

- **目标**：使 IOQcomGCC 完成匹配、映射和只读资源注册。
- **前置**：C0-02、C1-02。
- **需求与边界**：核对实际 nub 的窗口、长度、SoC 变体和可访问性；建立带平台所需屏障的窄寄存器访问与锁。初始化不重配 PLL、reset 或 GDSC，不接管现有 writer 的寄存器。
- **本地检验**：错误窗口／长度／版本、映射失败和注册失败清理；越界读写拒绝；源代码确认无初始化硬件写入。
- **实机检验**：后端 Ready、指定已知寄存器只读样本有效、写计数为零，原有显示／存储／USB 路径仍工作。
- **预期结果**：GCC 已建立且可提供最小描述；其余操作明确未支持。
- **失败归属**：C2-01 map／variant／access／register。

### C2-02 查询一个固定配置时钟的实际选择与计算频率

- **目标**：只读解码一种已知固定父源／分频配置。
- **前置**：C2-01、C1-03，样本已具备访问条件。
- **需求与边界**：仅支持样本对应的解码类型；无可信父频率时报告 Unknown，不以请求值代替硬件配置。不实现 setRate 或父源切换。
- **本地检验**：参考配置、边界编码、未知父源／非法字段；计算值与独立参考公式一致。
- **实机检验**：读取样本配置并留原始字段及计算依据；C2-08 通过后再与 debugcc 实测值比对，此前只验“配置解码”，不宣称物理频率实测。
- **预期结果**：能区分 configured rate 与 measured rate，零写入完成查询。
- **失败归属**：C2-02 decode／parent-rate；无法访问寄存器时 blockedBy=C2-01 或访问依赖。

### C2-08 通过 debugcc 测量一个时钟的实际频率

> 编号沿用不改；执行顺序在 C2-02 之后、C2-03 之前，使后续门控／频率卡可取得实测证据。

- **目标**：用 GCC debug mux 与 CXO 计数器测得一个正在运行的已知时钟的频率，并能识别“时钟未运行”。
- **前置**：C2-01、C2-02；C0-05 确认 debugcc 适用；debug mux 寄存器所有权在 C0-02 无其他 writer。
- **需求与边界**：参照 LG `drivers/clk/qcom/clk-debug.c` 与 `debugcc-sm8150.c`（GCC `debug_offset 0x62000`、`post_div_offset 0x62004`、`xo_div4_cbcr 0x43008`），仅支持 GCC 本地 mux 的一个测量源；跨 CC 的 debug mux 另立 C9-T 卡。测量会写 debug mux 选择、post-div、计数控制及 XO/4 branch，属于首次硬件写，须独立候选；测量后恢复 mux 和 XO/4 原值。CXO 需求由现有保留票提供，不新增 RPMh 请求。短测与长测计数相同时报告 NotRunning，而不是 0 Hz。
- **本地检验**：计数换算公式与参考一致（含 post-div／multiplier）；短测＝长测判定未运行；计数超时有界；寄存器原值保存与恢复；只读运行时钟（例如已知固定频率的源）作为参考样本。
- **实机检验**：测量一个已知固定频率的运行时钟，实测值与 C2-02 解码值在容差内；再测一个已知关闭的时钟得到 NotRunning；前后公共回归无退化，mux 已恢复。
- **预期结果**：后续卡可以用“实测频率／运行状态”作为验收证据；不代表任何时钟可被控制。
- **失败归属**：C2-08 mux-select／count／restore；测量值不符时先区分解码错误（C2-02）与测量链路错误。

### C2-03 开启一个普通 branch gate

- **目标**：通过时钟句柄开启一个原本关闭且允许接管的普通 branch。
- **前置**：C2-01、C1-03／04、C0-03 合格 gate 样本；父源／电源已独立建立或有明确保留凭据。
- **需求与边界**：只设置本资源允许字段并有界确认运行；不改父频率、不顺带自动上电，不实现通用关钟。本卡首次增加相应写能力及必要诊断入口。
- **本地检验**：原状态、邻位保留、posted write／halt 语义、超时保留状态；无前置凭据时无写拒绝。
- **实机检验**：`request→write-issued→running→completed` 事件链命中同一资源／token；C2-08 可用时附实测运行证据；验证邻位未改变和公共回归。样本可暂保留至重启，不用未验的关钟代码收尾。
- **预期结果**：一个普通 branch 的 enable 可用；不能据此打开所有 GCC 门控。
- **失败归属**：C2-03 admission／write／running-wait；父源失效单独标明前置失败。

### C2-04 关闭自己取得的一个普通 branch

- **目标**：撤回本驱动独占取得的普通 branch 并确认其 halt 条件。
- **前置**：C2-03；样本无其他使用者且外设已停止，无 inherited／pinned 保护。
- **需求与边界**：只增加该类 disable；关闭前校验身份及停机凭据。未知在途操作保留资源，不因超时强行复位。
- **本地检验**：非法 owner、继承资源、未停止消费者拒绝；有效 disable 保留邻位；超时不丢失所有权账本。
- **实机检验**：使用 C2-03 已验 enable，单独触发 disable，观察本卡 write／halt／complete；再用已验 enable 恢复并检查回归。
- **预期结果**：普通 branch 的一次完整撤回可验；没有公共自动关钟策略。
- **失败归属**：C2-04 permission／disable-write／halt-wait；enable 前置未成立不算本卡执行失败。

### C2-05 增加同一 branch 的多消费者引用

- **目标**：使两个 XNU 消费者共享同一个已验普通 branch 而不会提前关钟。
- **前置**：C2-04、C1-05。
- **需求与边界**：公共层保存每消费者请求，后端执行已验门控；先不处理不同频率约束、跨 CC 或固件票回收。
- **本地检验**：A/B 取得、A 释放、B 最后释放；重复／过期句柄拒绝；确定性交错下物理转换只发生在有效边界。
- **实机检验**：两个诊断消费者通过真实接口保持样本，A 释放后仍运行且无 disable；B 最后释放才触发已验 disable，日志能分别关联两者。
- **预期结果**：共享引用可独立验证；初始引用为零仍不能关闭 inherited clock。
- **失败归属**：C2-05 ledger／transition；底层门控已验合同破坏则记录对应后端阶段。

### C2-06 支持一个 GCC voted branch 的本方票

- **目标**：按硬件 voted 语义取得和撤回 AP 自身的一个投票位。
- **前置**：C2-05；补齐 voted 样本卡，明确本方位、状态位和共享者观测条件。
- **需求与边界**：新加一种 voted branch 描述／操作；释放遵守已核实 settle，不强求 aggregate OFF，不向 RSC 发此票。
- **本地检验**：模拟其他票仍在时撤回本方票；开启状态等待、邻位隔离及不可信 enable echo 等按具体类型检查。
- **实机检验**：确认写到本方 vote 寄存器及类型特定完成。能独立保持第二票时验证 aggregate 继续开；没有第二投票者时只验本方撤票，保留共享硬件场景未覆盖。
- **预期结果**：本样本 voted 能力通过；整体 OFF 和本方票撤回分别报告。
- **失败归属**：C2-06 vote-write／enable-completion／release-settle；缺第二票证据不冒充聚合关断故障。

### C2-07 提供一个已停止外设的 reset 脉冲

- **目标**：通过独立 reset 句柄完成一个获准外设的 assert／deassert 周期。
- **前置**：C2-01、C1-03 的独立 reset 绑定；已填 reset 样本卡，外设停止及访问／供电凭据成立。
- **需求与边界**：仅一个 BCR 类型／reset line；明确独占权限、最小脉宽和退出状态。不因 clock 引用为零自动复位，不改存储、USB 或运行中的显示。
- **本地检验**：无权限／未停止时拒绝；assert／delay／deassert 顺序及失败后状态；计时有界。
- **实机检验**：独立诊断模式对样本执行一次周期，记录真实 assert／deassert 和外设允许的恢复／状态证据；无安全样本时保留受阻，不猜测 reset 生效。
- **预期结果**：一个 reset 类型可用；无消费者功能观察时，明确只验 reset 控制状态。
- **失败归属**：C2-07 ownership／assert／timing／deassert；外设功能恢复若超出样本合同另立计划。

## 本候选实现（C2-01／C2-02，补丁 0233＋0234，零写入）

实现文件：[IOQcomGCC.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.h)、[IOQcomGCC.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.cpp)；接入 [files.arm64](../../dxb/xnu/iokit/conf/files.arm64)（`optional iokitcpp`）与 [KernelConfigTables.cpp](../../dxb/xnu/iokit/KernelConfigTables.cpp)（MH2LM 段，`IONameMatch='sweet,gcc'`，provider 为 `IOPlatformDevice`，与 C1 节点并列，不匹配 `sweet,clock-provider`）。ADT 侧由 [mk-mh2lm-mdss-adt.py](../../tools/mk-mh2lm-mdss-adt.py) 的 `--gcc` 追加独立节点 `gcc`（`name='gcc'`、`compatible='sweet,gcc'`、`sweet,gcc-abi=<u32 1>`、`sweet,gcc-variant=<u32 0x8150>`、`reg` 四个窗口），`--gcc` 隐含 `--clock-provider`，只去掉 `gcc` 节点即逐字节回到 C1 字节、两个都去掉回到原始树；`gcc` 节点恒排在 `clock` 之后。

- **C2-01 窗口与校验**：四页按 ADT `reg` 顺序为 `0x0010b000/0x1000`（显示）、`0x00133000/0x1000`（PDM）、`0x00143000/0x1000`（XO/4）、`0x00162000/0x1000`（debug mux）。**地址模型**：`sm8150.dtsi` 把 GCC 声明成 `reg = <0x100000 0x1f0000>`，厂商表里的寄存器号（`0x33008`、`0x62000`…）是这个块内的**偏移**，所以物理地址 = `0x00100000 + 偏移`；本卡下文一律用厂商偏移引用寄存器，日志 `resource=` 用解析后的 PA。0125 曾把三个窗口写成裸偏移（`0x0033000`／`0x0043000`／`0x0062000`），设备上会映射并读取与时钟表无关的低地址页——0234 修正，并让每一行 `phase=map` 同时打出 `offset=%#llx`，使 `resource` 与厂商偏移在设备日志里可直接对账。每页 `mapDeviceMemoryWithIndex(i)` 后核对解析出的物理地址与长度，不符即拒绝并卸载：`resource=gcc:0x<真实 PA> phase=map result=… detail=address-mismatch observed=%#llx`／`length-short observed=%#llx required=%#llx`／`window-absent`／`no-virtual-base`（`resource` 就是按窗口基址算出的绝对 PA，与解码行同一命名）。`0x10b000` 页同时被 `arm-io/mdss` 只读映射（C0-02），本后端只读它，不改它的所有权。 **实机证据（0125 缺陷的隔离）**：`boot-16k-0125-kernel-c1only-adt.img` 用同一颗 0125 内核、同一份 ramdisk 与参数、但 ADT 不带 `gcc` 节点，实机进入桌面；带 `gcc` 节点（三个裸偏移窗口）的两份镜像都停在 `bsp-kernel`。两者唯一差别是该节点，故 0125 内核与 C1 无回归，问题只在这四个窗口地址。
- **C2-01 身份**：`sweet,gcc-abi` 缺失、长度非 4 或值非 1 → `kIOReturnBadArgument detail=missing-gcc-abi`；`sweet,gcc-variant` 缺失 → `BadArgument detail=missing-variant`，不为 `0x8150` → `kIOReturnUnsupported detail=variant-mismatch observed=%#x required=%#x`。版本与变体只认数值，不信商品字符串。
- **C2-01 寄存器访问**：`readRegister(window,offset,&v)` 要求已映射、偏移 4 字节对齐、`offset+4<=length`，取一次 `*(volatile uint32_t *)` 后立即 `dmb oshld`，全程在一把 `IOLock` 内；越界、非对齐、未映射一律返回 false 且不发生读取。**本文件没有任何写路径**，`registerWrites()` 返回常量 `0`（不是会漂移的计数器），主机测试对源码文本断言：只有一处 `*(volatile uint32_t *)`、该处即取值、无 `writeRegister`／`IOSyncWrite`、`dmb oshld` 一处、整段 `#if defined(ARM64_BOARD_CONFIG_MH2LM)` 包裹。
- **C2-01 启动快照**：`start` 内先冻结 11 个已知寄存器（显示 `0x10b00c`／`0x10b038`／`0x10b048`，PDM `0x33008`／`0x3300c`／`0x33010`／`0x33014`，XO/4 `0x43008`，debug mux `0x62000`／`0x62004`／`0x62008`），供后续卡比对；快照只读，失败只记 `fReadFailures`，不改配置。
- **C2-01 provider 顺序**：不假设 IOKit 的 start 顺序；`IOQcomClockProvider::publishedProvider()` 为空时以 `200×5 ms` 有界轮询，超时记 `phase=start detail=provider-absent` 并卸载退出，不留半状态。注册成功后 INFO：`planID=C2 resource=cc%u phase=start result=0 windows=4 snapshot=11 reads=18 writes=0 variant=%#x`（`cc1` 是 `kIOQcomCCGCC`，`reads=18`＝11 快照＋6 条描述的 7 次读取）。失败路径沿用控制器名 `resource=gcc`：`missing-gcc-abi`／`missing-variant`／`variant-mismatch observed=%#x required=%#x`／`already-started`／`no-lock`／`provider-absent`／`registration-refused`。
- **C2-01 stop／重启**：`stop` 先撤注册（已撤则容忍并返回成功），再卸载映射、清 `fMapped`、释放锁，所以第二次 `start` 是真正的首次 start。该缺陷由主机测试发现：旧代码 `stop` 后仍留 `fMapped`／`fLock`，重启永远撞 `already-started`。
- **C2-02 描述表**：6 条（`gcc_disp_ahb_clk`／`gcc_disp_hf_axi_clk`／`gcc_disp_xo_clk`／`gcc_pdm_xo4_clk`／`gcc_pdm2_clk_src`／`gcc_pdm2_clk`），窗口、偏移、halt 类别、父源、`domain` 全部来自 LG `gcc-sm8150.c`。显示三条是既有 writer 的资源，声明为 pinned；`gcc_disp_ahb_clk`、`gcc_disp_xo_clk` 在厂商树里是 `CLK_IS_CRITICAL`，本候选永不关它们。
- **C2-02 只解码一个样本**：`gcc_pdm2_clk_src`（`cmd_rcgr=0x33010`、`hid_width=5`、`parent_map=gcc_parent_map_0`、`mnd_width=0`）。选择子按 map 解码：`0→bi_tcxo`（19.2 MHz，可信）、`1→gpll0`、`6→gpll0_out_even`（无可信父频 → `detail=parent-rate-unknown known=0`）；`7`（test SE）与 `2..5`（map 中未用）→ `detail=src-invalid sel=%u` 并返回 `BadArgument`。`mode!=0` → `mode-unsupported`（该样本 `mnd_width=0`，出现 mode 就不是本卡声明的那一种配置）。`CMD_UPDATE` 置位 → `detail=update-pending known=0`（配置未生效，报 Unknown 而不是拿旧值冒充）。
- **C2-02 分频语义**：照抄 `clk_rcg2_calc_rate` 的分支——`div` 字段非 0 时 `rate=parent*2/(div+1)`，为 0 时**不做除法**；内部用 `divX2=div?div+1:0` 作哨兵，`divX2==0` 表示“未编码分频”，`configuredHz` 就是父频本身。厂商频率表里 9.6 MHz 那一行的 pre_div 属于请求侧计算，不能当实测或回读依据。
- **C2-02 配置≠测得**：`state.raw[2]` 保留 CMD／CFG 原值，`divX2`／`selected`／`rateKnown` 保留推导依据，日志为 `phase=decode … sel=%u div=%u raw=%#x,%#x known=%u`，`start` 末尾再补一条 `phase=decode-sample name=%s hz=%llu known=%u`。RCG 没有 branch halt 状态，`rootOff` 只如实报出、不当“已停钟”证据，`haltTrusted` 恒 false；`gcc_pdm2_clk` 的父是本 CC 的 RCG，无法取到可信父频，报 Unknown 而不串联父值。
- **C2-02 branch**：`enableBit` 取 CBCR bit0，`clockOffBit` 取 bit31；`haltTrusted` 只在 `BRANCH_HALT` 类为真（`BRANCH_HALT_DELAY` 的显示 branch 不信 halt 读回）。
- **如实声明的两处保守**：`gcc_pdm_xo4_clk` 在厂商树里没有声明父源，本后端按名字与 C0-03 S2 实测把它标成 XO/4（4.8 MHz），并在描述表注释里记为“由观测声明”；各 branch 没有 `vdd_class`，因此 `domain=0`（未声明），不猜 CX／MX。
- **顺带修正 C1 表上限（`kIOQcomBindingMax` 8→16）**：诊断段会为每个已注册 clock 建一张绑定表，旧上限 8 会让 10 条探测全部 `phase=diag-bind result=2`（BadArgument），把后端本身藏起来；现在表上限与遍历上限 `kIOQcomWalkMax=16` 相等并有 `static_assert`，诊断两个循环也以 `kIOQcomBindingMax` 为界。0124 时诊断段没有真实后端，这个缺陷没有在实机上显形，是主机测试先发现的。C1 的九条边界用例相应改成“16 条通过／17 条拒绝”。

**证据**：`tools/tests/test-mh2lm-gcc-backend.py` 通过（身份与四类窗口拒绝、映射与 11 项快照、对齐／越界只读访问、`clk_rcg2` 解码向量含 `div=0` 哨兵与 `src-invalid`／`mode-unsupported`／`update-pending`、halt 类别、provider 绑定与 domain admission、诊断段列出真实后端且收尾 `writes=0`、零写入）；`tools/tests/test-mh2lm-clock-provider.py` 通过（含新的 16／17 表上限边界）；`tools/tests/test-mh2lm-mdss-adt.py` 通过（`gcc` 节点可选、`reg` 走 `LOW_ALIAS` 编码、`--gcc` 隐含 `--clock-provider`、可逐字节还原、CLI 与默认字节不变）；ARM64 单 TU 编译两份均 rc=0、零 `error:`（`scratch/clock-c2/logs/compile-IOQcomClock.txt`、`compile-IOQcomGCC.txt`）；补丁 `0233`（SHA256 `b92d455222eb80bbe12f67b1940a7565f41123815b1cada8f8a1f77acb487649`）在基线 `4148ed3` 上按 `0230`→`0231`→`0232`→`0233` 以 `patch -p1 --fuzz=0` 应用，六份文件与工作区 `cmp` 逐字节一致、无 `.rej`。

## 本候选实现（C2-08 测频＋C1-05 需求账本，补丁 0235）

实现文件：[IOQcomGCC.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.h)、[IOQcomGCC.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.cpp)、[IOQcomClock.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.h)、[IOQcomClock.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.cpp)。两颗文件在同一候选中分别落地两张卡：C1-05 是 provider 侧的公共合同，C2-08 是这个合同的第一台**真实执行者**（XO/4 参考源的需求由账本持有，而不是只写在分支里）。ADT 侧新增 `--clock-measure`（隐含 `--gcc`），只在 `gcc` 节点追加一条 4 字节属性 `sweet,clock-measure=1`：默认树逐字节仍为 `081f63dc…`，加门后为 `0fc24851…`（`+40` 字节），去掉该属性又回到默认字节。

- **C2-08 契约与来源**：照抄 LG `clk-debug.c`／`debugcc-sm8150.c` 的 `struct measure_clk_data { cxo; ctl_reg=0x62038; status_reg=0x6203c; xo_div4_cbcr=0x43008 }`，debug mux 三寄存器 `.debug_offset=0x62000`／`.post_div_offset=0x62004`／`.cbcr_offset=0x62008`，`src_sel_mask=0x3ff shift 0`、`post_div_mask=0xf shift 0`。本后端把这三页写成**具名寄存器**：`kIOQcomGccDebugMuxOffset`／`DebugPostDivOffset`／`DebugGateOffset`（同页 `0x162000` 的 `+0x8` 是 debug 分支 CBCR），计数器用 `MeasureCtlOffset=0x038`／`MeasureStatusOffset=0x03c`（`0x62000` 页内），XO/4 CBCR 用已有的 `0x143000` 页 `+0x8`。全部偏移以厂商偏移引用，物理地址仍由 C2-01 的 `0x00100000+偏移` 模型给出。
- **C2-08 首次写路径**：该文件第一次出现写：`writeRegister(window,offset,value)` 检查已映射、4 字节对齐、`offset+4<=length`（越界记 `phase=write-issued result=%d stage=%s detail=bounds` 且**不写**），随后按[总计划 3.5](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)的设备内存顺序 `dmb oshst` → 单次 `*(volatile uint32_t *)` → `dsb osh`，成功行 `phase=write-issued result=0 stage=%s write=%u value=%#x`，`stage` 取 `ref-gate`／`mux-select`／`post-div`／`debug-gate`／`counter`／`restore`。主机测试对源码文本断言：仍然只有一处 `*(volatile uint32_t *)` 取值、一处同样写形态，屏障三条各出现一次，`registerWrites()` 不再是常量 0 而是 `fWrites`。
- **C2-08 测量序列**：`measureSave` 先读存五个寄存器（XO/4 CBCR、debug mux sel、post-div、debug 分支 CBCR、计数控制，任一次读失败即 `detail=save-failed`）；向账本取得 `cc1:0x4`（`gcc_pdm_xo4_clk`）的需求——**只有 `opened` 的那个持有者写 `ref-gate`**，共享持有者继承已开的分支而不撤销他人；读回判定 `enable=1 off=0` 才算参考源在跑，否则 `detail=reference-not-running` 并 `ledgerComplete(token,false)`；随后按厂商顺序写 mux（`0x53`＝`gcc_disp_xo_clk`）、post-div（`1-1=0`）、debug branch enable；`runMeasurement` 做两次计数：先写 `ctl=ticks` 并**有界**等 `XO_DIV4_CNT_DONE(bit25)` 清零（`kIOQcomGccMeasurePollTries=2500 × 10 µs`），再写 `ctl=CNT_EN(bit20)|ticks` 等 bit25 置位，读 `status & GENMASK(24,0)`，最后写回 `ctl=ticks`；超时分别 `detail=idle-timeout`／`done-timeout polls=%u status=%#x`，超时后仍走恢复而不留半写。
- **C2-08 换算**：短测 `0x1000`、长测 `0x10000`，两者相等即 `NotRunning`（`running=0`，不是 0 Hz）；否则 `hz=((full*10+15)*4800000)/((0x10000*10)+35)`，即除以 `655395`，再乘 `multiplier=1`。`post-div` 按 `post_div_div-1` 编码后原样记在行里，恢复时用保存的原值而非重算。运行样本 mux `0x53`、关闭样本 mux `0x9A`（`gcc_pdm2_clk`；`gcc_pdm2_clk_src` 在厂商 mux 表里没有索引，不能当“关闭样本”）。
- **C2-08 恢复**：`measureRestore` 以**逆序**写回五个保存值，每写一条都立刻读回比对（`phase=restore result=0 expected=%#x value=%#x`／`detail=mismatch` 计入 `fRestoreMismatches`），最后再读一次 XO/4 判定 `enable==0` 才算“硬件已停”，据此调用 `ledgerRelease(token,stopped,&lastHolder)`；收尾行 `phase=measure-done result=%d writes=%u restores=5 mismatches=%u hold=%u`（`hold=1` 表示本次不是最后一个持有者，账本仍有未清的 `pending`）。
- **C2-08 门控**：`sweet,clock-measure` 缺失 → `measureClocks` 返回 `Unsupported` 且只打 DEBUG（设备日志级别 0，环里不出现），**默认镜像因此仍是 `writes=0`、行数与 0126 一致**；长度≠4 或值>1 → `ERROR detail=bad-clock-measure-gate length=%u`／`value=%#x`；值 1 → `INFO phase=gate result=0 clock-measure=1`。测量失败（非 `Success`／`Unsupported`）时在 `start` 收尾前补一条 `WARN phase=start result=0 detail=measure-incomplete measure-result=%d writes=%u`，后端照常 Ready——失败的测量不应把只读后端一起拖下水。
- **C1-05 账本语义**：`IOQcomClockProvider` 新增固定表（`kIOQcomLedgerEntries=8` 条、每条 `kIOQcomLedgerHolders=4` 个 token），键为 `(controller,localID)`，值含 `holders／desired／pending／confirmed` 与 `generation／transitions／confirmations`。`ledgerAcquire` 首次打开记 `desired=1,pending=1,transitions++` 并返回 `openedTransition=true`（日志 `transition=new`），同一资源的第二次取得只增 `holders`（`transition=shared`），**不再转换**；`ledgerComplete(token,true)` 置 `pending=0,confirmed=1,confirmations++`，`ledgerComplete(token,false)` 同样清 `pending`、记 `WARN detail=not-confirmed` 并返回 `NotReady`；最后一次 `ledgerRelease(token,stopped)` 在 `stopped=true` 时清 `pending／confirmed` 并 `confirmations++`，`stopped=false` 时**保留** `pending=1` 并记 `ERROR detail=hardware-still-on`（`IOError`）——不谎报硬件已停。
- **C1-05 拒绝与失效**：`controller` 越界（`0`／`>=kIOQcomCCMax`）、空 consumer、空 token → `BadArgument`；未挂载／未 Ready／正在 stop／后端未注册 → `NotReady`；表满或 token 位满 → `NoResources`；未知 token → `WARN phase=confirm|release result=10 detail=unknown-token`（`kIOReturnNotFound`）。`generation` 变化（后端 `start` 重开）时清 token／holders／desired／pending／confirmed 但**保留** `transitions／confirmations` 并更新 `generation`，记 `WARN detail=stale-generation entry-generation=%llu generation=%llu`；`withdrawController` 调 `ledgerForget` 使条目失活，随后按 key 或旧 token 查询都是 `NotFound`。`token=0` 从不签发（回绕时跳过在用的 token）。

**证据**：`tools/tests/test-mh2lm-clock-provider.py` 通过（C1 入口＋真实声明绑定＋**多消费者账本**：一次转换对应一个新需求、共享取得不转换、verdict 门控的 confirmed、硬件未能停时的 owed demand、generation 失效、撤回清理、holder 与表上限；全程零 MMIO 写）；`tools/tests/test-mh2lm-gcc-backend.py dxb/xnu` 通过（默认门缺失：0 次 store、无 `phase=measure`；门在：23 次有序 store＝4 建立＋7×2 计数＋5 恢复、`hz=19199084`、`decoded-hz=19200000` 与 `delta-ppm=-47` 在容差内、5 次恢复且 `fRestoreMismatches==0`、账本 `transitions=2 confirmations=2 holders=0`；坏门值与坏长度 0 次 store；`reference-not-running`；计数卡死 2×`done-timeout` 且轮询次数有界；其余 5 项仍恢复）；`tools/tests/test-mh2lm-mdss-adt.py` 通过（`sweet,clock-measure` 为可选属性、去掉即回到 `gcc` 节点字节、`--clock-measure` 隐含 `--gcc`、任何早于 0235 的树都不含该属性）；补丁 `0235` 在基线按 `0230`→`0235` 严格重放逐字节一致。

### 0127 待验收（本次打包）

两档镜像由 [out-bs/mk16k-0127.sh](../../out-bs/mk16k-0127.sh) 产出：默认档与 0126 逐字节同树（`081f63dc…`，仍应 `writes=0`），测量档 `--clock-measure`（`0fc24851…`）。测量档的验收点是：

- 参考源：`phase=transition result=0 token=%u owner=1` → `phase=running result=0 enable=1 off=0 value=0x1`（`0x143008` 由 `0x80000000` 变为 `0x1` 再回到 `0x80000000`）。
- 运行样本：`phase=measure result=0 mux=0x53 … hz=19199084 running=1 post-div=1 multiplier=1`，紧随 `phase=measure-compare result=0 decoded-hz=19200000 measured-hz=19199084 delta-ppm=-47 tolerance-ppm=10000`——实测值与 C2-02 的解码值相差 `-47 ppm`，远在容差内，这就是 C2-02 “配置解码”与 C2-08 “实测”分离的证据。
- 关闭样本：`mux=0x9a`（`gcc_pdm2_clk`）的 `short==full` ⇒ `hz=0 running=0`，INFO 行不带 `detail=`；这是“未运行”的正常判定（`short==full` 即 NotRunning），不是 0 Hz 的测量值，因此**没有** `measure-compare` 行。若该 mux 反而计出非零数，同一行会变成 `WARN … detail=running-unexpected`——那是“已关时钟仍在跑”的发现，必须按 C2-08 的核对者身份报告而不是当噪声。
- 写入序列：18 条写入（`ref-gate`→`mux-select`/`post-div`/`debug-gate`→两个样本各 `mux-select`＋`count-ctl`/`count-start`/`count-stop`×2）＋5 条 `restore`，共 23 条 `phase=write-issued`，`write=%u` 单调 1…23；`measure-save` 是 DEBUG（设备级别 0 不落环），所以**保存值要靠 `restore` 行的 `expected=` 读回**：逆序依次为 `0x162038`（计数控制，保存值不预定）、`0x162008=0x80000000`、`0x162004=0x0`、`0x162000=0x14b`、`0x143008=0x80000000`——后四个正好等于 0126 快照里的同名寄存器，这就是“恢复到原值”的独立证据。
- 收尾：`phase=measure-done result=0 writes=23 restores=5 mismatches=0 hold=0`，五条 `phase=restore result=0 expected=… value=…` 的 `value` 必须与 `expected` 逐条相等，最后一行为 `phase=release result=0 value=0x80000000`（XO/4 的 `ENABLE` 已回 0），账本 `holders=0 pending=0`。
- 回归：C1／C2-01／C2-02 行数与 0126 相同（25 行）之外只多测量行；非时钟基线与 b32 一致；面板正常进桌面。
- 失败归属：`hz` 明显偏离而非 `0x53` 的 mux 行 → 测量链路（`0x62000` 页或 debug 分支）；`mux-select` 写后读回不符 → 该页无写权限或被他人抢走；`reference-not-running` → XO/4 分支（`0x143008`）不可开，属 C0-03 前置；`restore … detail=mismatch` → 本卡恢复失败，必须保留镜像并回退。

## 实机验收（0126 默认档，通过）

`logs/sweetlog-0126-0.bin`（sha256 `3d1cd662b6749ac037190c2c7ee30c0c82c277d8866ce4b1806dd2b0087193bc`，`total=1366678` 字节，`flush_sequence=12`，无回绕）来自 `boot-16k-0126-gpioirq-panel-pipeline.img`：设备进入桌面，`planID=C1 phase=start` 起 C1／C2 全链完整。审计脚本 `scratch/artifact-0126/audit-clock-log.py` 以默认档通过（零时钟 `[WARN]`／`[ERROR]`，非时钟 WARN／ERROR 清单与 b32 基线一致，面板标记不超基线）。

- **窗口**：四条 `phase=map result=0 length=0x1000`，`resource=offset=kIOQcomGccBase` 自洽：`gcc:0x10b000 offset=0xb000`、`gcc:0x133000 offset=0x33000`、`gcc:0x143000 offset=0x43000`、`gcc:0x162000 offset=0x62000`。0234 的地址模型在真机上成立。
- **快照实测值**（C2-01，11 个寄存器，`phase=snapshot result=0 value=…`）：

| 寄存器 | 实测值 | 读法 |
|---|---|---|
| `0x10b00c`（`gcc_disp_ahb_clk` CBCR） | `0x80000003` | enable=1，`CLK_OFF`=1（bit31）→ 门控开着但输出关 |
| `0x10b038`（`gcc_disp_hf_axi_clk` CBCR） | `0x4221` | enable=1，off=0 |
| `0x10b048`（`gcc_disp_xo_clk` CBCR） | `0x1` | enable=1，off=0 |
| `0x133008`／`0x13300c`／`0x133010` | `0x80000000` | 三处 `CLK_OFF`／`ROOT_OFF` 都置位 |
| `0x133014`（RCG CFG） | `0x100000` | `SRC_SEL=0`、`hid_div=0`、`mode=0` |
| `0x143008`（`gcc_pdm_xo4_clk` CBCR） | `0x80000000` | off=1，enable=0 |
| `0x162000`／`0x162004`／`0x162008`（debug mux） | `0x14b`／`0`／`0x80000000` | 测频 mux 当前选中 `0x14b` |

- **`start` 收尾行**：`planID=C2 resource=cc1 phase=start result=0 windows=4 snapshot=11 reads=18 writes=0 variant=0x8150`——读预算 11＋7 恰好用满，写计数为零。
- **C2-02 实测**：`cc1:0x5`＝`gcc_pdm2_clk_src` 解出 `detail=configured sel=0 div=0 div2=0 hz=19200000 known=1`，`decode-sample` 同一行给出 `hz=19200000 known=1`：CMD `0x80000000` 的 `ROOT_OFF` 置位但配置合法，CFG `0x100000` 选中 XO（19.2 MHz）且未编码分频，故不走分频公式。这是配置解码，**不是**物理频率实测。
- **已知限制**：`cc1:0x6`（`gcc_pdm2_clk`，父是本 CC 的 `cc1:0x5`）如实报 `parent-rate-unknown known=0`——本卡的 branch 解码只认 `gccFixedRate()` 的固定源，不做跨行速率解析。同一轮里 `cc1:0x5` 已经解出 19.2 MHz，所以后续卡若要闭合这条链，需要一次二遍解析（父频已解出后再补子行），本卡不做。
- **工具教训**：审计脚本曾把 `phase=snapshot` 当作“只应出现在失败时”，把 `phase=decode` 的 `resource` 当作物理地址；两者都与后端实际不符（快照成功也逐行打印、解码行用设备资源名 `cc1:0xN`）。修正后审计同时把每条 decode 的 `raw` 与对应快照值逐字节比对，读错寄存器会立刻暴露。

### 待实机验收（历史，0125 内核，已被 0126 取代）

以下为 0125 候选期的验收清单，保留作回归对照；其中“命名约定”与“五条 branch 期望”两条与后端实际不符，已在 0126 实测后改正（见上节）。

实机只验“后端建起来、只读样本有效、写计数为零、原有路径仍工作”，**不宣称任何频率实测**（测频是 C2-08）。

`gcc` 节点在每个 0125 镜像里都有，所以**默认档也会跑 C2 链**；`--clock-diag`（`sweet,clock-diag=<u32 1>`）只追加 C1 诊断段，把收尾行的控制器数从 1 变 2。

- 期望顺序（默认档）：`planID=C1 … phase=start … bindings=0` → 四条 `planID=C2 resource=gcc:0x10b000|0x133000|0x143000|0x162000 phase=map result=0 length=0x1000 offset=0xb000|0x33000|0x43000|0x62000`（顺序按 ADT `reg`，`offset` 必须满足 `resource-0x100000`）→ `planID=C1 resource=cc1:0x0 consumer=provider phase=register result=0 generation=…`（这条属于 provider，由后端注册触发）→ `planID=C2 resource=cc1 phase=start result=0 windows=4 snapshot=11 reads=18 writes=0 variant=0x8150` → 六条 `phase=decode` → 一条 `phase=decode-sample`。
- 命名约定（0126 实测校正）：`map`／`snapshot` 行用**寄存器绝对地址** `resource=gcc:0x<PA>`；`start`／`stop` 用**控制器** `resource=cc1`；六条 `phase=decode` 与 `decode-sample` 用**设备资源名** `resource=cc1:0x<localID>`，`decode-sample` 另带 `name=<时钟名>`。`phase=decode` 按描述表顺序为 `cc1:0x1`…`cc1:0x6`，依次解码 `0x10b00c`、`0x10b038`、`0x10b048`、`0x143008`、`0x133010`＋`0x133014`、`0x13300c`。
- 五条 branch 的声明固定，解码结果与硬件状态无关，必须是：`cc1:0x1 detail=parent-rate-unknown known=0`（描述表未声明父源）、`cc1:0x2 detail=parent-rate-unknown known=0`（GPLL0 无可信频率）、`cc1:0x3 detail=configured known=1`（XO）、`cc1:0x4 detail=configured known=1`（XO/4＝4.8 MHz）、`cc1:0x6 detail=parent-rate-unknown known=0`（父是本 CC 的 RCG，单遍解析不闭合）。
- `cc1:0x5`（`gcc_pdm2_clk_src`，寄存器 `0x133010`／`0x133014`）有四种可接受结果，取决于硬件当时的选择子与分频：`detail=configured sel=0 div=… div2=… hz=… known=1`（XO 直通；`div=0` 时 `div2=0`、`hz=19200000`，否则 `hz=19200000*2/(div+1)`）、`detail=parent-rate-unknown sel=1|6 known=0`（GPLL0）、`detail=update-pending … known=0`（`CMD_UPDATE` 置位，配置未生效）、`detail=src-invalid sel=2|3|4|5|7` 或 `detail=mode-unsupported mode=…`（`[WARN]`＋`BadArgument`＝-536870206，如实拒绝而非缺陷）。
- `decode-sample` 只在 `cc1:0x5` 查询成功时出现一条：`planID=C2 resource=cc1:0x5 name=gcc_pdm2_clk_src phase=decode-sample result=0 hz=… known=…`；`known=0` 时 `hz=0`，不得出现任何替代值。被 `src-invalid`／`mode-unsupported` 拒绝时**没有**这一条。
- 零写入证据：`planID=C2` 不出现任何写相位或 `writes=1`；`phase=start` 与（重启时的）`phase=stop` 都印 `writes=0`；诊断档收尾 `phase=diag-done result=0 detail=zero-writes writes=0 controllers=2 probes=10`；`[ERROR]` 计数保持基线零。
- 诊断档的 C1 段必须遍历两个控制器、共 10 个句柄：4 条合成（`resource=cc7:0x1..0x4`，`consumer=diag-core|diag-child|diag-orphan|diag-domain`，`token=1..4`）加 6 条 GCC 描述（`resource=cc1:0x1..0x6`，`consumer` 是时钟名，`token=5..10`）；每条 bind 行印 `bindings=10`，收尾行印 `probes=10`。GCC 六条的 `caps` 都是 `0x3`（只有 QueryConfig／QueryRate），`parents` 依次 `0/0/1/1/3/1`，`domain` 只有 `cc1:0x5` 是 `0x1`：`cc1:0x1..0x4` 与 `cc1:0x6` 为 `deps=0 depReason=ready enable=-536870201`（`capability-absent`，INFO），`cc1:0x5` 为 `deps=-536870184 depReason=domain-absent enable=-536870184`（全档唯一的时钟 `[WARN]`）。任何一条 `enable=0` 都说明只读契约被破坏（该卡没有写路径）。
- 失败归属：`C2-01 map`（`window-absent`／`address-mismatch observed=%#llx`／`length-short observed=%#llx required=%#llx`／`no-virtual-base`）、`variant`（`missing-gcc-abi`／`missing-variant`／`variant-mismatch observed=%#x required=%#x`）、`access`（`phase=snapshot detail=read-failed`、`phase=query detail=register-read-failed`）、`register`（`provider-absent`／`registration-refused`）；`C2-02 decode`（`src-invalid`／`mode-unsupported`）与 `parent-rate`（`parent-rate-unknown`）。若默认档完全没有 `planID=C2 … phase=map` 行，先查 ADT 是否带 `gcc` 节点（`--gcc`）与 `IONameMatch='sweet,gcc'` 是否命中，再查窗口地址与 `sweet,gcc-variant`。
- 公共回归：显示／存储／USB 路径行为与 b32 一致（非时钟 WARN 清单不变、`handoff`／`scanout` 计数不变）；用短会话，避免 `SWEETLOG` 绕环（容量 4194304 字节）。
