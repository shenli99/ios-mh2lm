# C2：GCC 的最小本地时钟与 reset 能力

[返回总计划](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)。各卡默认遵守[总计划 3.5 共享约束](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)，卡内只写特有边界。状态：C2-01／C2-02 已在 0126 默认档实机验收通过（补丁 `0233`／`0234`，零写入）；**C2-08 已实机收口：0127 测量档验证测量链路（23 次写、19.2 MHz、关闭样本 NotRunning、账本完整往返），暴露的唯一缺陷是恢复核验把分支的 halt 状态位当成要写回的值；补丁 `0236` 把判据收窄为“只比配置位”，0128 测量档复验 `mismatches=0`（同一次运行里 `0x162008` 读回 `0`、`0x143008` 读回 `0x80000000`，两条同一谓词都通过），19.2 MHz 与解码值差 +1 ppm；0128 默认档用同一内核、只去掉门，复验零写入（25 行、`writes=0`、无 `gate`／`write-issued`／`measure`／`restore`／`acquire`／`release`）**。**C2-08 整卡收口**。**C2-03 分支门控已在 0130 门控档实机验收通过**：补丁 `0237` 用一条 store 打开 `gcc_pdm_xo4_clk` 分支，判据是硬件 halt 位而不是写进去的值，邻位前后读回，需求由账本单独持有、样本留到重启（关钟是 C2-04、共享是 C2-05）；实测该分支第一次读回即 `enable=1 off=0`（`polls=0`）、四个邻位零变化、屏幕与 0126／0129 基线无差别。顺带把 `0x143008` 的身份更正为 `gcc_xo_div4_clk`（localID 7，C2-08 计数参考），描述表由 6 条变 7 条。**0130-1 门控＋测频档把 `gcc_pdm_xo4_clk` 接成计数器第三源：门控侧全部判据照旧通过，但该 branch 的实测频率是声明父源的 ×4——0130 把它按名字标成 XO/4（4.8 MHz），计数器当场反驳（19200036，3000007 ppm，`BadArgument`）。补丁 `0238` 把父源改成 XO（19.2 MHz），并把这条 `MUX_SRC_LIST`＋无 `parent_names` 的证据写进补丁正文。C2-03 的门控行为因此定案。**0131 门控＋测频档结账（补丁 `0238`）**：第三源在改正后的描述下测得 `19199963` Hz、`decoded-hz=19200000`、`delta-ppm=-1`、`result=0`，审计器 `verdict=pass`（且不需要任何 `--allow-*`），门控侧行与 0130／0130-1 逐字相同——**描述与计数器现在交叉验证了两个不同的时钟，C2-03 全卡收口**。

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
- **状态**：实机通过（0132 门控＋关钟档，补丁 `0239`，见本文件末节）。

### C2-05 增加同一 branch 的多消费者引用

- **目标**：使两个 XNU 消费者共享同一个已验普通 branch 而不会提前关钟。
- **前置**：C2-04、C1-05。
- **需求与边界**：公共层保存每消费者请求，后端执行已验门控；先不处理不同频率约束、跨 CC 或固件票回收。
- **本地检验**：A/B 取得、A 释放、B 最后释放；重复／过期句柄拒绝；确定性交错下物理转换只发生在有效边界。
- **实机检验**：两个诊断消费者通过真实接口保持样本，A 释放后仍运行且无 disable；B 最后释放才触发已验 disable，日志能分别关联两者。
- **预期结果**：共享引用可独立验证；初始引用为零仍不能关闭 inherited clock。
- **失败归属**：C2-05 ledger／transition；底层门控已验合同破坏则记录对应后端阶段。
- **状态**：候选已实现（0133 共享档待实机，补丁 `0240`，见本文件末节）。

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
- **如实声明的两处保守**：`gcc_pdm_xo4_clk` 在厂商树里没有声明父源。0233 起按名字把它标成 XO/4（4.8 MHz）；0238 依 0130-1 的计数与厂商 `MUX_SRC_LIST`（`0x99`→`gcc_pdm_xo4_clk`，`clk_branch2_ops`、无 `.parent_names`、无可分频字段）改正为 **XO（19.2 MHz）：名字里的 `xo4` 不是分频，全系统唯一的 /4 是计数器时基 `0x43008`**。名字推断必须由 `debugcc-*.c` 的 mux 表或一次计数器实测确认，否则就只是猜测。各 branch 没有 `vdd_class`，因此 `domain=0`（未声明），不猜 CX／MX。
- **顺带修正 C1 表上限（`kIOQcomBindingMax` 8→16）**：诊断段会为每个已注册 clock 建一张绑定表，旧上限 8 会让 10 条探测全部 `phase=diag-bind result=2`（BadArgument），把后端本身藏起来；现在表上限与遍历上限 `kIOQcomWalkMax=16` 相等并有 `static_assert`，诊断两个循环也以 `kIOQcomBindingMax` 为界。0124 时诊断段没有真实后端，这个缺陷没有在实机上显形，是主机测试先发现的。C1 的九条边界用例相应改成“16 条通过／17 条拒绝”。

**证据**：`tools/tests/test-mh2lm-gcc-backend.py` 通过（身份与四类窗口拒绝、映射与 11 项快照、对齐／越界只读访问、`clk_rcg2` 解码向量含 `div=0` 哨兵与 `src-invalid`／`mode-unsupported`／`update-pending`、halt 类别、provider 绑定与 domain admission、诊断段列出真实后端且收尾 `writes=0`、零写入）；`tools/tests/test-mh2lm-clock-provider.py` 通过（含新的 16／17 表上限边界）；`tools/tests/test-mh2lm-mdss-adt.py` 通过（`gcc` 节点可选、`reg` 走 `LOW_ALIAS` 编码、`--gcc` 隐含 `--clock-provider`、可逐字节还原、CLI 与默认字节不变）；ARM64 单 TU 编译两份均 rc=0、零 `error:`（`scratch/clock-c2/logs/compile-IOQcomClock.txt`、`compile-IOQcomGCC.txt`）；补丁 `0233`（SHA256 `b92d455222eb80bbe12f67b1940a7565f41123815b1cada8f8a1f77acb487649`）在基线 `4148ed3` 上按 `0230`→`0231`→`0232`→`0233` 以 `patch -p1 --fuzz=0` 应用，六份文件与工作区 `cmp` 逐字节一致、无 `.rej`。

## 本候选实现（C2-08 测频＋C1-05 需求账本，补丁 0235）

实现文件：[IOQcomGCC.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.h)、[IOQcomGCC.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.cpp)、[IOQcomClock.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.h)、[IOQcomClock.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomClock.cpp)。两颗文件在同一候选中分别落地两张卡：C1-05 是 provider 侧的公共合同，C2-08 是这个合同的第一台**真实执行者**（XO/4 参考源的需求由账本持有，而不是只写在分支里）。ADT 侧新增 `--clock-measure`（隐含 `--gcc`），只在 `gcc` 节点追加一条 4 字节属性 `sweet,clock-measure=1`：默认树逐字节仍为 `081f63dc…`，加门后为 `0fc24851…`（`+40` 字节），去掉该属性又回到默认字节。

- **C2-08 契约与来源**：照抄 LG `clk-debug.c`／`debugcc-sm8150.c` 的 `struct measure_clk_data { cxo; ctl_reg=0x62038; status_reg=0x6203c; xo_div4_cbcr=0x43008 }`，debug mux 三寄存器 `.debug_offset=0x62000`／`.post_div_offset=0x62004`／`.cbcr_offset=0x62008`，`src_sel_mask=0x3ff shift 0`、`post_div_mask=0xf shift 0`。本后端把这三页写成**具名寄存器**：`kIOQcomGccDebugMuxOffset`／`DebugPostDivOffset`／`DebugGateOffset`（同页 `0x162000` 的 `+0x8` 是 debug 分支 CBCR），计数器用 `MeasureCtlOffset=0x038`／`MeasureStatusOffset=0x03c`（`0x62000` 页内），XO/4 CBCR 用已有的 `0x143000` 页 `+0x8`。全部偏移以厂商偏移引用，物理地址仍由 C2-01 的 `0x00100000+偏移` 模型给出。
- **C2-08 首次写路径**：该文件第一次出现写：`writeRegister(window,offset,value)` 检查已映射、4 字节对齐、`offset+4<=length`（越界记 `phase=write-issued result=%d stage=%s detail=bounds` 且**不写**），随后按[总计划 3.5](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#35-所有卡共享的默认约束)的设备内存顺序 `dmb oshst` → 单次 `*(volatile uint32_t *)` → `dsb osh`，成功行 `phase=write-issued result=0 stage=%s write=%u value=%#x`，`stage` 取 `ref-gate`／`mux-select`／`post-div`／`debug-gate`／`counter`／`restore`。主机测试对源码文本断言：仍然只有一处 `*(volatile uint32_t *)` 取值、一处同样写形态，屏障三条各出现一次，`registerWrites()` 不再是常量 0 而是 `fWrites`。
- **C2-08 测量序列**：`measureSave` 先读存五个寄存器（XO/4 CBCR、debug mux sel、post-div、debug 分支 CBCR、计数控制，任一次读失败即 `detail=save-failed`）；向账本取得 `cc1:0x4`（`gcc_pdm_xo4_clk`）的需求——**只有 `opened` 的那个持有者写 `ref-gate`**，共享持有者继承已开的分支而不撤销他人；读回判定 `enable=1 off=0` 才算参考源在跑，否则 `detail=reference-not-running` 并 `ledgerComplete(token,false)`；随后按厂商顺序写 mux（`0x53`＝`gcc_disp_xo_clk`）、post-div（`1-1=0`）、debug branch enable；`runMeasurement` 做两次计数：先写 `ctl=ticks` 并**有界**等 `XO_DIV4_CNT_DONE(bit25)` 清零（`kIOQcomGccMeasurePollTries=2500 × 10 µs`），再写 `ctl=CNT_EN(bit20)|ticks` 等 bit25 置位，读 `status & GENMASK(24,0)`，最后写回 `ctl=ticks`；超时分别 `detail=idle-timeout`／`done-timeout polls=%u status=%#x`，超时后仍走恢复而不留半写。
- **C2-08 换算**：短测 `0x1000`、长测 `0x10000`，两者相等即 `NotRunning`（`running=0`，不是 0 Hz）；否则 `hz=((full*10+15)*4800000)/((0x10000*10)+35)`（`4800000` 是 XO/4 计数时基 `0x43008` 的常量，**不是**被测时钟的频率），即除以 `655395`，再乘 `multiplier=1`。`post-div` 按 `post_div_div-1` 编码后原样记在行里，恢复时用保存的原值而非重算。运行样本 mux `0x53`、关闭样本 mux `0x9A`（`gcc_pdm2_clk`；`gcc_pdm2_clk_src` 在厂商 mux 表里没有索引，不能当“关闭样本”）。
- **C2-08 恢复**：`measureRestore` 以**逆序**写回五个保存值，每写一条都立刻读回比对（`phase=restore result=0 expected=%#x value=%#x mask=%#x`／`detail=mismatch` 计入 `fRestoreMismatches`），最后再读一次 XO/4 判定 `enable==0` 才算“硬件已停”，据此调用 `ledgerRelease(token,stopped,&lastHolder)`；收尾行 `phase=measure-done result=%d writes=%u restores=5 mismatches=%u hold=%u`（`hold=1` 表示本次不是最后一个持有者，账本仍有未清的 `pending`）。比对**只覆盖配置位**：两个分支 CBCR（`0x143008`／`0x162008`）用 `kIOQcomGccBranchConfigMask=0x0fffffff`，mux／post-div／计数控制用整寄存器（`kIOQcomGccVerifyAll`）。理由见[0127 实机结果](#0127-实机结果测量链路通过恢复核验判据有缺陷2026-11-24)：bit31..28 是硬件自己维护的 OFF/NOC halt 状态，厂商只轮询、从不写回。
- **C2-08 门控**：`sweet,clock-measure` 缺失 → `measureClocks` 返回 `Unsupported` 且只打 DEBUG（设备日志级别 0，环里不出现），**默认镜像因此仍是 `writes=0`、行数与 0126 一致**；长度≠4 或值>1 → `ERROR detail=bad-clock-measure-gate length=%u`／`value=%#x`；值 1 → `INFO phase=gate result=0 clock-measure=1`。测量失败（非 `Success`／`Unsupported`）时在 `start` 收尾前补一条 `WARN phase=start result=0 detail=measure-incomplete measure-result=%d writes=%u`，后端照常 Ready——失败的测量不应把只读后端一起拖下水。
- **C1-05 账本语义**：`IOQcomClockProvider` 新增固定表（`kIOQcomLedgerEntries=8` 条、每条 `kIOQcomLedgerHolders=4` 个 token），键为 `(controller,localID)`，值含 `holders／desired／pending／confirmed` 与 `generation／transitions／confirmations`。`ledgerAcquire` 首次打开记 `desired=1,pending=1,transitions++` 并返回 `openedTransition=true`（日志 `transition=new`），同一资源的第二次取得只增 `holders`（`transition=shared`），**不再转换**；`ledgerComplete(token,true)` 置 `pending=0,confirmed=1,confirmations++`，`ledgerComplete(token,false)` 同样清 `pending`、记 `WARN detail=not-confirmed` 并返回 `NotReady`；最后一次 `ledgerRelease(token,stopped)` 在 `stopped=true` 时清 `pending／confirmed` 并 `confirmations++`，`stopped=false` 时**保留** `pending=1` 并记 `ERROR detail=hardware-still-on`（`IOError`）——不谎报硬件已停。
- **C1-05 拒绝与失效**：`controller` 越界（`0`／`>=kIOQcomCCMax`）、空 consumer、空 token → `BadArgument`；未挂载／未 Ready／正在 stop／后端未注册 → `NotReady`；表满或 token 位满 → `NoResources`；未知 token → `WARN phase=confirm|release result=10 detail=unknown-token`（`kIOReturnNotFound`）。`generation` 变化（后端 `start` 重开）时清 token／holders／desired／pending／confirmed 但**保留** `transitions／confirmations` 并更新 `generation`，记 `WARN detail=stale-generation entry-generation=%llu generation=%llu`；`withdrawController` 调 `ledgerForget` 使条目失活，随后按 key 或旧 token 查询都是 `NotFound`。`token=0` 从不签发（回绕时跳过在用的 token）。

**证据**：`tools/tests/test-mh2lm-clock-provider.py` 通过（C1 入口＋真实声明绑定＋**多消费者账本**：一次转换对应一个新需求、共享取得不转换、verdict 门控的 confirmed、硬件未能停时的 owed demand、generation 失效、撤回清理、holder 与表上限；全程零 MMIO 写）；`tools/tests/test-mh2lm-gcc-backend.py dxb/xnu` 通过（默认门缺失：0 次 store、无 `phase=measure`；门在：23 次有序 store＝4 建立＋7×2 计数＋5 恢复、`hz=19199084`、`decoded-hz=19200000` 与 `delta-ppm=-47` 在容差内、5 次恢复且 `fRestoreMismatches==0`、账本 `transitions=2 confirmations=2 holders=0`；坏门值与坏长度 0 次 store；`reference-not-running`；计数卡死 2×`done-timeout` 且轮询次数有界；其余 5 项仍恢复）；`tools/tests/test-mh2lm-mdss-adt.py` 通过（`sweet,clock-measure` 为可选属性、去掉即回到 `gcc` 节点字节、`--clock-measure` 隐含 `--gcc`、任何早于 0235 的树都不含该属性）；补丁 `0235` 与 `0236` 在基线 `d177551` 上按序严格重放，`IOQcomClock.{h,cpp}`／`IOQcomGCC.{h,cpp}`／`tools/tests/test-mh2lm-gcc-backend.py` 五个文件逐字节一致；两份补丁都带主机测试，所以路径相对仓库根，用 `git apply -p1`（更早的时钟补丁只动 `dxb/xnu`，用 `--directory=dxb/xnu`）。

### 0127 实机结果：测量链路通过，恢复核验判据有缺陷（2026-11-24）

`boot-16k-0127-gpioirq-panel-pipeline-clockmeasure.img`（`3db09549…`，内核 `030cf3b0…`）实机进入桌面。`logs/sweetlog-0127-0.bin`（`c4142395…`，`total=1098038`，`flush_sequence=9`）是**本次会话**的环（不是残留：SHA 与 0126 不同、`flush_sequence` 也前进），测量档共 65 条 `SWEET CLOCK` 行、2 条 `[WARN]`、0 条 `[ERROR]`。逐条对照 0127 的验收点：

- **参考源与账本（通过）**：`planID=C1 resource=cc1:0x4 consumer=sweetclock-measure phase=acquire result=0 token=1 holders=1 desired=1 pending=1 confirmed=0 transitions=1 transition=new` → `phase=transition result=0 token=1 owner=1` → `phase=running result=0 enable=1 off=0 value=0x1`（`0x143008` 的 `ENABLE` 真的置位、halt 位清零）→ `phase=confirm result=0 token=1 holders=1 pending=0 confirmed=1 confirmations=2` → 收尾 `phase=release result=0 token=1 holders=0 desired=0 pending=0 confirmed=0 transitions=2 confirmations=2`。`release_value=0x1` 意味着释放判决时读到的 XO/4 仍是“enable 且未 halt”，随后逆序恢复把它写回 `0x80000000`。这是一次完整的“取得—确认—释放”，`transitions=2 confirmations=2` 与主机预期一致；`confirm`／`release` 行不带 `consumer=` 字段（只有 `acquire` 带），审计按此解析。
- **运行样本（通过，−1 ppm）**：`phase=measure result=0 mux=0x53 ticks=0xa/0x0 short=16398 full=262156 polls=9/671 hz=19199963 running=1 post-div=1 multiplier=1`，紧随 `phase=measure-compare result=0 decoded-hz=19200000 measured-hz=19199963 delta-ppm=-1 tolerance-ppm=10000`。实测与 C2-02 的解码值差 **−1 ppm**；主机 fixture 的 `19199084`（−47 ppm）只是另一组模型计数（`16384/262144`），本卡只要求“与解码同量级、在容差内、符号正确”，**不钉**具体计数。轮询 `9/671` 远低于 `2500` 次上限：运行样本没有把预算用满。
- **关闭样本（通过）**：`resource=cc1:0x6`（`gcc_pdm2_clk`，`mux=0x9a`）`short=262156 full=262156 hz=0 running=0 polls=45/747`，INFO 级、无 `detail=`，且**没有** `measure-compare` 行——短测＝长测即 `NotRunning`。`short==full` 且等于上一窗口的累计值，正是厂商注释里的“被测时钟未运行时计数器不归零”。同页 `0x133010=0x80000000`（`ROOT_OFF`）说明 `cc1:0x5` 本来就不能当运行样本，这个选择没有误伤。
- **写入与读回（通过）**：23 条 `phase=write-issued result=0`（`write=1…23` 单调）、5 条 `phase=restore`、收尾前的 C2 行 `windows=4 snapshot=11 reads=1515 writes=23 variant=0x8150`。`0x143008` 在 enable 之后读回 `0x1`、恢复时读到 `0x80000000`，说明“写—读—回写”这条链在硬件上是通的。
- **唯一缺陷：恢复核验比对了状态位**。`phase=restore result=-536870198 expected=0x80000000 value=0x0`（`gcc:0x162008`）**不是写失败**——`write=10 stage=debug-gate value=0x80000001` 已成功——而是分支 CBCR 的 bit31..28 是 FSM 的 OFF/NOC **状态**：厂商 `clk-branch.c` 把 `BRANCH_CLK_OFF=BIT(31)` 当状态**轮询**（`clk_branch_wait`），从不把它纳入“要写回的值”；门开之后硬件要几个周期才更新 halt 状态，所以写完立刻读回可以合法地读到 `0x0`，而写已经生效（`0x143008` 同时读回 `0x1` 就是反方向的例子：enable 生效、halt 未置位）。整寄存器比对把硬件拥有的状态位算进了“恢复到原值”，于是产生一条假 mismatch，并连带 `phase=measure-done result=-536870198 writes=23 restores=5 mismatches=1 hold=0` 与 `WARN phase=start … detail=measure-incomplete measure-result=-536870198 writes=23`。这两条 WARN 是**同一个原因的两个投影**，不是两次故障。
- **写路径本身未被削弱**：`writeRegister` 一如既往不做读回，唯一核对者是恢复时的比对，所以滞后一拍的 halt 位永远不会让一次成功 store 记成失败；修正只在核对谓词上（补丁 `0236`），不动写路径、不动顺序、不动屏障。审计脚本 `scratch/artifact-0127/audit-clock-log.py` 的 `--legacy-restore` 模式专门复现这一份修复前日志：它要求那一条假 mismatch 必须存在（否则就说明镜像不是修复前版本），同时要求其余全部通过。

### 0128 实机结果：掩码判据成立，C2-08 收口（2026-11-24）

`boot-16k-0128-gpioirq-panel-pipeline-clockmeasure.img`（`b5cabf20…`，内核 `21f99b5e…`）实机进入桌面，`logs/sweetlog-0128-0.bin`（`00570b16…`，`total=1008528`，`flush_sequence=8`）是本次会话的环：13058 行日志里的 64 条 `SWEET CLOCK`、**0 条时钟 WARN**、0 条 ERROR（0127 的两条 WARN 全部消失）。逐条对照 0128 的验收点：

- **恢复行按掩码判据落在两个方向上**（这是本次唯一的判据变化，也是最有价值的证据）：`resource=gcc:0x162008 phase=restore result=0 expected=0x80000000 value=0 mask=0xfffffff`——读回值**仍然是 0**（halt 位滞后，与 0127 完全一致的现象），但配置位相等，`result=0`；同一份环里 `resource=gcc:0x143008 phase=restore result=0 expected=0x80000000 value=0x80000000 mask=0xfffffff`——halt 已清，值整相同。**同一个谓词在“读到 0”和“读到 1”两侧都成立**，说明放行的是状态位而不是把整条核对放宽；另三条 `0x162038`／`0x162004`／`0x162000`（`expected=0 value=0`、`expected=0 value=0`、`expected=0x14b value=0x14b`）仍是 `mask=0xffffffff` 整寄存器比对。
- **收尾与失败消失**：`phase=measure-done result=0 writes=23 restores=5 mismatches=0 hold=0`；环里**没有** `detail=measure-incomplete`，`phase=start` 回到单条 INFO（`windows=4 snapshot=11 reads=1507 writes=23 variant=0x8150`，读数从 0127 的 1515 降到 1507，随轮询次数变化，掩码比较本身不加读）。
- **测量链路复现**：运行样本 `mux=0x53 short=16398 full=262157 polls=35/683 hz=19200036 running=1` → `measure-compare decoded-hz=19200000 measured-hz=19200036 delta-ppm=1 tolerance-ppm=10000`（0127 是 `19199963`／`-1`，两次都落在 1 ppm 内且符号相反，正好说明本卡只承诺“同量级、容差内、符号正确”，不钉计数）；关闭样本 `mux=0x9a short=262157 full=262157 hz=0 running=0`、无 `measure-compare`；账本 `transitions=2 confirmations=2 holders=0`、`release_value=0x1`，与 0127 一字不差。
- **回归**：无时钟 WARN／ERROR；非时钟清单仍是面板 `live configuration needs review`×3 与 USB `epN cmd N status N`×5（b32 为 2，同类同值 `ep0 cmd 0x6 status 1` 的枚举重试，随主机侧时序变化；审计以 `--allow-baseline-drift` 记录该次数漂移而不是当作新故障，`ERROR` 仍为零）。面板正常进桌面。
- **收口判定**：C2-08 的“首次硬件写”四件事——写路径可数（23 次有序 store）、测量真的发生（19.2 MHz／1 ppm）、恢复真的完成（5 次逆序写＋读回核对，`mismatches=0`）、共享需求真的归还（账本无欠账）——全部在真机上成立。

### 0128 默认档：门关闭时一个字节都不写（2026-11-24）

`boot-16k-0128-gpioirq-panel-pipeline.img`（`7f3a96bd…`）与测量档**共用同一内核** `21f99b5e…`，只把 ADT 换回 `081f63dc…`（23568 字节，不含 `sweet,clock-measure`）——这是该回归唯一干净的形态：写路径就在二进制里，差别只有一处设备树属性。实机进入桌面，`logs/sweetlog-0129-0.bin`（`860d1346…`，`total=1200369`，`flush_sequence=10`）：**25 条 `SWEET CLOCK`、0 条时钟 WARN、0 条 ERROR**，相位恰好是 `start 2`／`map 4`／`snapshot 11`／`register 1`／`decode 6`／`decode-sample 1`，`gate`／`write-issued`／`measure`／`measure-save`／`measure-compare`／`measure-done`／`restore`／`acquire`／`transition`／`confirm`／`release` **全部为零**；`phase=start … windows=4 snapshot=11 reads=18 writes=0 variant=0x8150`，没有任何一条 `writes=` 非零的行；`decode-sample` 仍是 `cc1:0x5 hz=19200000 known=1`（只读解码值，不是测量值）。非时钟清单与 b32 逐项相等（面板 ×3、USB ×2），无次数漂移。
- 这条与测量档互补：同一个二进制，门开则 23 次有序写并全部恢复、门关则零写；属性是唯一的开关，代码里没有第二条旁路。


## 本候选实现（C2-03 分支门控＋身份修正，补丁 0237）

实现文件：[IOQcomGCC.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.h)、[IOQcomGCC.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.cpp)、[mk-mh2lm-mdss-adt.py](../../tools/mk-mh2lm-mdss-adt.py)。两张卡的前提是**先分清楚两个分支属于谁**：0126 按厂商 debug 约定把 `0x143008` 叫成 PDM XO/4，但真机上 C2-08 的计数器读的是这一支（`0x143008` 写 enable 后读回 `0x1`），而 PDM 的 `gcc_pdm_xo4_clk` 是本地 CBCR `0x33008`。0237 把 `0x143008` 改成 localID 7 `gcc_xo_div4_clk`（`kIOQcomGccMeasureReferenceLocalID`），localID 4 `gcc_pdm_xo4_clk` 仍是门控样本（`0x133008`）；描述表因此多一条，默认档 `decode` 行由 6 变 7，其余行不变。

- **门控契约**：`gateClocks()` 在 C2-08 测量之前跑，只在 `sweet,clock-gate` 存在时进入。步骤固定为：读 CBCR（`phase=request`，打印 `enable`／`off`／`value`）→ 经账本取得 `(kIOQcomCCGCC, localID 4)` 的**独立 token**（consumer `sweetclock-gate`）→ 只在“持有者自己且未使能”时写一次 `value|ENABLE`（`stage=gate-enable`，走同一个 `writeRegister`：对齐／越界拒绝、`dmb oshst`→store→`dsb osh`）→ 以 `CLK_OFF`（bit31）清零作为判决（`2500×10 µs` 有界，`phase=running`，`polls` 可对账）→ 逐条读回四个邻位（`0x13300c`／`0x133010`／`0x133014` 与 `0x10b048`）与门开启前的捕获比较（`phase=gate-check changed=0`）→ 账本 `ledgerComplete(token,true)` → `phase=completed detail=sample-kept-until-reboot`（**不写回**，凭据保持 `hold=1`）→ `phase=gate-done writes=1 neighbours=4 changed=0 polls=%u hold=1`。
- **拒绝语义（无写入）**：已使能（`detail=already-enabled`）、halt 未置（`detail=halt-not-set`，即未使能却已“运行”，说明状态不可信）、账本拒绝（`detail=ledger-refused`）、读回失败（`phase=request detail=read-failed`）、store 失败（`detail=store-failed writes=%u`）——都在写之前或写失败处收手，账本不确认；halt 超时（`detail=halt-timeout`）保留已写状态并 `ledgerComplete(token,false)`（`pending=0`、`confirmations` 不动），`gate-done … hold=0 detail=halt-timeout`，`start` 收尾补 `WARN detail=gate-incomplete gate-result=%d writes=%u`——失败的写不把只读后端拖下水，也不谎报成功。
- **邻位为什么是这四个**：`0x13300c`／`0x133010`／`0x133014` 是同一页里 `gcc_pdm2_clk` 及其 RCG 的寄存器（0126 快照里 `0x80000000`／`0x80000000`／`0x100000`），`0x10b048` 是显示 CC 的 `gcc_disp_xo_clk` CBCR，它和 PDM 共用 XO/4 这一支；四者都是“被误伤就会被看见”的邻位，且全在已映射窗口内，不需要新页。
- **opt-in 与合成树**：`--clock-gate`（隐含 `--gcc`）只追加一条 4 字节属性 `sweet,clock-gate=1`：门控树 `e2996bda…`（23608 字节，与测量树同长但内容不同）、门控＋测频树 `c0ba7498…`（23648 字节）；默认树仍 `081f63dc…`、测量树仍 `0fc24851…`、诊断树仍 `8d6f2adb…` 逐字节不变。两条属性的关系已在生成器测试里锁死：各自单独去掉即回到原树，门控＋测频树去掉 `sweet,clock-gate` 即逐字节等于测量树。两者都在 `gcc` 节点上，因此 605 字节的参数表没有多一个字节。
- **证据（主机）**：`tools/tests/test-mh2lm-gcc-backend.py dxb/xnu` 通过——门只有 1 次 store（`0x80000001`，从快照值 `0x80000000` 只加 enable）、无 restore、`hold=1`、一个 token；`already-enabled`／`halt-not-set` 两次拒绝后 `ledgerInspect` 为 `NotFound`（条目从未建立，说明拒绝发生在账本之前）、零写入；halt 超时保留状态且 `pending=1 transitions=2 confirmations=0`；门控样本作为计数器第三源被测量（`mux=0x99`，模型计数 `16384/262144` → `measured-hz=19199084`，与 `decoded-hz=19200000` 差 −47 ppm，在 10000 ppm 容差内）；门控＋测频合成档 31 次 store、5 次恢复、`mismatches=0`；默认档仍是零写入。`tools/tests/test-mh2lm-mdss-adt.py` 通过（两条属性各自可选、合成树可还原）；补丁 `0237` 在基线严格重放逐字节一致（带主机测试与生成器，按 `git apply -p1`）。

### 0130 验收清单（C2-03）

镜像：`boot-16k-0130-gpioirq-panel-pipeline-clockgate.img`（门控档）、`…-clockgatemeasure.img`（门控＋测频档）、`…-clockmeasure.img`（只测频，回归用）、`…img`（默认档，回归用）。四档同一内核，唯一差别是 ADT 上那 1～2 条 4 字节属性。审计脚本 `scratch/artifact-0128/audit-clock-log.py --c2-03 [--gate]`（同一脚本兼容 0127／0128／0129 旧环）。

- **门控档（C2-03 的唯一证据）**：`phase=gate result=0 clock-measure=0 clock-gate=1` → `phase=request result=0 enable=0 off=1 value=0x80000000`（原状态，与 0126 快照同值）→ 恰好一条 `phase=write-issued … stage=gate-enable value=0x80000001 consumer=sweetclock-gate` → `phase=running result=0 enable=1 off=0 value=0x1 polls=<有界>` → `phase=gate-check result=0 neighbours=4 changed=0` → `phase=completed result=0 token=%u enable=1 off=0 detail=sample-kept-until-reboot` → `phase=gate-done result=0 writes=1 neighbours=4 changed=0 polls=%u hold=1`；账本一条 `acquire`（`resource=cc1:0x4`，`transition=new`）＋一条 `transition`＋一条 `confirm`（`confirmed=1`），**没有** `release`；`start` 收尾 `writes=1`（若出现 `detail=gate-incomplete` 则门卡失败）。全档无时钟 `[WARN]`／`[ERROR]`。
- **门控＋测频档（测量证据；速率的期望已被 0130-1 改正，见下节）**：在门控行之后仍有完整测量，且**多一个第三源**：`phase=measure mux=0x99`（`gcc_pdm_xo4_clk`，`0x133008` 已由本候选打开）在 `mux=0x53`（XO）与 `mux=0x9a`（`gcc_pdm2_clk`，关闭样本）之外；0130 时的期望是 `decoded-hz=4800000`（按名字把父源当成 XO/4），**0130-1 实测 19200036 把它反驳，正确期望是 19200000**；`writes=31`、`restores=5`、`mismatches=0`、`hold=0`（测量把自己的参考源还回去），两个 token、两条 `confirm`；账本对 `cc1:0x7`（测量参考）仍走完 `acquire→confirm→release`，对 `cc1:0x4`（门控）只 `acquire→confirm` 不 `release`——这正是“C2-03 的样本留到重启”的可观测形式。
- **默认档回归**：26 条 `SWEET CLOCK`（0129 的 25 条＋新增 `cc1:0x7` 一条 `decode`），`start … snapshot=11 reads=19 writes=0`，`gate`／`write-issued`／`measure`／`restore`／`acquire` 全零；`decode` 顺序为 `cc1:0x1…cc1:0x6`、`cc1:0x7`（`gcc_xo_div4_clk detail=configured known=1`，XO/4＝4.8 MHz），`decode-sample` 仍是 `cc1:0x5 hz=19200000`。
- **失败归属**：`C2-03 request/enable`（`read-failed`／`already-enabled`／`halt-not-set`／`store-failed`）、`running-wait`（`halt-timeout`，此时 `hold=0` 且 `pending` 保留）、`neighbour`（`neighbour-changed` 并带 `changed before=%#x after=%#x mask=%#x`）、`ledger`（`ledger-refused`／`not-owner`）；父源失效（XO/4 若被别的路径关掉）要看 `0x133008` 是否读出 `off=0 enable=0`——那是前置失败，不是本卡的写失败。若门控档出现 `writes=0` 但 `phase=gate` 有，先查 ADT 属性是否真的落进 ramdisk（`mcopy` 后 `cmp` 已在打包脚本里）。门控开启后 `0x133008` 保持 `enable=1 off=0` 直到重启，属预期：C2-04 才有经过验证的关钟路径。

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
| `0x143008`（`gcc_xo_div4_clk` CBCR，C2-08 计数参考） | `0x80000000` | off=1，enable=0 |
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
- 五条 branch 的声明固定，解码结果与硬件状态无关，必须是：`cc1:0x1 detail=parent-rate-unknown known=0`（描述表未声明父源）、`cc1:0x2 detail=parent-rate-unknown known=0`（GPLL0 无可信频率）、`cc1:0x3 detail=configured known=1`（XO）、`cc1:0x4 detail=configured known=1`（XO＝19.2 MHz，0238 起；0130 曾错标为 XO/4＝4.8 MHz）、`cc1:0x6 detail=parent-rate-unknown known=0`（父是本 CC 的 RCG，单遍解析不闭合）。
- `cc1:0x5`（`gcc_pdm2_clk_src`，寄存器 `0x133010`／`0x133014`）有四种可接受结果，取决于硬件当时的选择子与分频：`detail=configured sel=0 div=… div2=… hz=… known=1`（XO 直通；`div=0` 时 `div2=0`、`hz=19200000`，否则 `hz=19200000*2/(div+1)`）、`detail=parent-rate-unknown sel=1|6 known=0`（GPLL0）、`detail=update-pending … known=0`（`CMD_UPDATE` 置位，配置未生效）、`detail=src-invalid sel=2|3|4|5|7` 或 `detail=mode-unsupported mode=…`（`[WARN]`＋`BadArgument`＝-536870206，如实拒绝而非缺陷）。
- `decode-sample` 只在 `cc1:0x5` 查询成功时出现一条：`planID=C2 resource=cc1:0x5 name=gcc_pdm2_clk_src phase=decode-sample result=0 hz=… known=…`；`known=0` 时 `hz=0`，不得出现任何替代值。被 `src-invalid`／`mode-unsupported` 拒绝时**没有**这一条。
- 零写入证据：`planID=C2` 不出现任何写相位或 `writes=1`；`phase=start` 与（重启时的）`phase=stop` 都印 `writes=0`；诊断档收尾 `phase=diag-done result=0 detail=zero-writes writes=0 controllers=2 probes=10`；`[ERROR]` 计数保持基线零。
- 诊断档的 C1 段必须遍历两个控制器、共 10 个句柄：4 条合成（`resource=cc7:0x1..0x4`，`consumer=diag-core|diag-child|diag-orphan|diag-domain`，`token=1..4`）加 6 条 GCC 描述（`resource=cc1:0x1..0x6`，`consumer` 是时钟名，`token=5..10`）；每条 bind 行印 `bindings=10`，收尾行印 `probes=10`。GCC 六条的 `caps` 都是 `0x3`（只有 QueryConfig／QueryRate），`parents` 依次 `0/0/1/1/3/1`，`domain` 只有 `cc1:0x5` 是 `0x1`：`cc1:0x1..0x4` 与 `cc1:0x6` 为 `deps=0 depReason=ready enable=-536870201`（`capability-absent`，INFO），`cc1:0x5` 为 `deps=-536870184 depReason=domain-absent enable=-536870184`（全档唯一的时钟 `[WARN]`）。任何一条 `enable=0` 都说明只读契约被破坏（该卡没有写路径）。
- 失败归属：`C2-01 map`（`window-absent`／`address-mismatch observed=%#llx`／`length-short observed=%#llx required=%#llx`／`no-virtual-base`）、`variant`（`missing-gcc-abi`／`missing-variant`／`variant-mismatch observed=%#x required=%#x`）、`access`（`phase=snapshot detail=read-failed`、`phase=query detail=register-read-failed`）、`register`（`provider-absent`／`registration-refused`）；`C2-02 decode`（`src-invalid`／`mode-unsupported`）与 `parent-rate`（`parent-rate-unknown`）。若默认档完全没有 `planID=C2 … phase=map` 行，先查 ADT 是否带 `gcc` 节点（`--gcc`）与 `IONameMatch='sweet,gcc'` 是否命中，再查窗口地址与 `sweet,gcc-variant`。
- 公共回归：显示／存储／USB 路径行为与 b32 一致（非时钟 WARN 清单不变、`handoff`／`scanout` 计数不变）；用短会话，避免 `SWEETLOG` 绕环（容量 4194304 字节）。

## 实机验收（0130 门控档，通过）

`logs/sweetlog-0130-0.bin`（sha256 `e8f6870a88eebb2302d62d3bed16fe96de3661696c215010b4779694090744e8`，`total=1279649` 字节，`flush_sequence=11`，16624 行，无回绕）来自 `boot-16k-0130-gpioirq-panel-pipeline-clockgate.img`（sha256 `01d2edc0d75359c16020c0017ee5a486a0dc47cc61fbe6f3e87082e1e0180340`，35545088 字节）。屏幕正常进入桌面，与 0126／0129 基线看不出任何差别。

刷写后先核对“刷进去的到底是什么”：从镜像的 FAT16 ramdisk 里取出 `EFI/UniXNU/Payloads/kernel.macho.adt`（23608 字节，sha256 `e2996bda49155e7fc1f6d7c2eba4b4ea4fc2e0df64c454569e5ad8a8138c4b42`，与打包脚本钉死的 `ADT_GATE` 逐字节相同），按 Apple ADT 布局（32 字节名字槽 + `u32` 长度 + 值）读出属性：`sweet,clock-gate=1`，**没有** `sweet,clock-measure`／`sweet,clock-diag`。工具：`scratch/artifact-0128/read-adt-claims.py`（同时打印六张 0130 镜像的属性矩阵作对照）。

审计（rc=0）：`python3 scratch/artifact-0128/audit-clock-log.py --c2-03 --gate --allow-baseline-drift logs/sweetlog-0130-0.bin`。`clock_lines_total=36`，`clock_warn=0`，`clock_error=0`。逐行（行号为环内行号）：

| 行 | 实测行（节选） |
|---|---|
| 168 | `planID=C2 resource=gcc phase=gate result=0 clock-measure=0 clock-gate=1` |
| 181 | `resource=gcc:0x133008 phase=snapshot result=0 value=0x80000000`（存前：halted＋enable=0） |
| 198 | `… consumer=sweetclock-gate phase=request result=0 enable=0 off=1 value=0x80000000` |
| 199 | `planID=C1 resource=cc1:0x4 … phase=acquire result=0 token=1 holders=1 desired=1 pending=1 confirmed=0 transitions=1 transition=new` |
| 200 | `phase=transition result=0 token=1 owner=1` |
| 201 | `phase=write-issued result=0 stage=gate-enable write=1 value=0x80000001` |
| 202 | `phase=running result=0 enable=1 off=0 value=0x1 polls=0` |
| 203 | `planID=C1 resource=cc1:0x4 phase=confirm result=0 token=1 holders=1 pending=0 confirmed=1 confirmations=1` |
| 204 | `phase=gate-check result=0 neighbours=4 changed=0` |
| 205 | `phase=completed result=0 token=1 enable=1 off=0 detail=sample-kept-until-reboot` |
| 206 | `phase=gate-done result=0 writes=1 neighbours=4 changed=0 polls=0 hold=1` |
| 207 | `planID=C2 resource=cc1 phase=start result=0 windows=4 snapshot=11 reads=29 writes=1 variant=0x8150` |

- **C2-03 的三条判据全部成立**：一条 store（`writes=1`，`stage=gate-enable`）；判据是硬件 halt 位——`value` 从 `0x80000000` 写成 `0x80000001`，读回 `0x1`（`CLK_OFF` 清零）才算运行，本机**第一次读回就成立**（`polls=0`，一次 10 µs 延时都没用）；邻居 `neighbours=4 changed=0`。`hold=1`：样本留到重启，没有 `release`。
- **快照与 0126 逐值相同**（`0x10b00c=0x80000003`、`0x10b038=0x4221`、`0x10b048=0x1`、三处 `0x1330xx=0x80000000`、`0x133014=0x100000`、`0x143008=0x80000000`、`0x162000=0x14b`、`0x162004=0`、`0x162008=0x80000000`）：门控开之前那条分支的状态与 C2-01 首次观测完全一致，说明没有任何其他路径碰过它。
- **第 7 个描述符**：行 197 `resource=cc1:0x7 phase=decode result=0 detail=configured enable=0 off=1 raw=0x80000000 known=1`——`raw` 正是快照 `0x143008` 的值，`detail=configured` 而非 `parent-rate-unknown`（它声明了一个固定父源 XO/4）。这条行原先在宿主测试里被写成 `parent-rate-unknown known=0`，是本轮用编译出的真实行纠正的。
- **读次数恒等式**：`reads = 29 + polls`（19 步走查 + 1 次存前读 + 轮询自己的 `polls+1` 次读 + 8 次邻居读）。设备实测 `reads=29 polls=0` 恰好等于 29，而审计器原先写的是 `28 + polls`——被这台机器当场抓出（`polls` 是循环跳出时的下标，首读成功时 `polls=0` 但确实读了一次）。宿主 fixture 同步为 `polls=1 → reads=30`，8 个变体矩阵重跑仍 8/8 PASS。
- **账本**：`cc1:0x4` 一个 token（`token=1`），`acquire`（`transition=new`，`holders=1`）→ `transition`（`owner=1`）→ `confirm`（`confirmed=1`），**没有** `release`。这正是“C2-03 的需求由账本单独持有、硬件样本留到重启”的可观测形式。
- **基线**：非时钟 `WARN` 只有两类——`SWEET FB SCANOUT` ×3（与 b32 相同，稳定基线）与 `SWEET USB` ×13（b32 是 2；本次会话充电线＋调试线同时在线，属既定会话漂移，用 `--allow-baseline-drift` 记录进 `baseline_drift`，不视为回归）。面板标记 `scanout_transfer=1`、`fb_handoff=1`、`touch_mode3=1`、`probe_drained=1` 全在，`touch_armed=2`（b32 为 4，只允许少不允许多）。环内无 `panic`／`Debugger message`。
- **尚未做的**：门控＋测频档由 0130-1 完成（下节）；默认档回归与父源复验也已在 0131 结账。关钟（C2-04）与共享（C2-05）属新卡，见本文件末两节。每次验收只追加结果，不改上方清单。

## 实机验收（0130-1 门控＋测频档：门控成立，父源被计数器反驳）

镜像 `boot-16k-0130-gpioirq-panel-pipeline-clockgatemeasure.img`（`fce71e03…`，与 0130 门控档同一内核、只多 `sweet,clock-measure`）；环 `logs/sweetlog-0130-1.bin`（`80ebba44…`，`total=4194304` 即环已写满、`flush_sequence` 前进，无残留）。审计 `--measure --c2-03 --gate --gate-declared-xo4 --allow-baseline-drift` 通过，`verdict=gate-parent-refuted`：门控判据全部成立，唯一缺陷是第三源的**声明父源**，而这一条正是同一轮计数器给出的。

- **门控侧与 0130-0 逐条相同**：`phase=request value=0x80000000` → 一条 `stage=gate-enable value=0x80000001 consumer=sweetclock-gate` → `phase=running enable=1 off=0 value=0x1 polls=0` → `gate-check neighbours=4 changed=0` → `completed detail=sample-kept-until-reboot` → `gate-done writes=1 neighbours=4 changed=0 polls=0 hold=1`；账本 `cc1:0x4` 一个 token（`token=1`）、`acquire→transition→confirm`、**无 `release`**。屏幕与此前基线无差别。
- **第三源（反驳的那一行）**：`resource=cc1:0x4 name=gcc_pdm_xo4_clk consumer=sweetclock-measure mux=0x99 short=16396 full=262157 polls=21/642 hz=19200036 running=1` → `phase=measure-compare result=-536870198 decoded-hz=4800000 measured-hz=19200036 delta-ppm=3000007 tolerance-ppm=10000`。**实测是声明的 ×4.0000075**，不是噪声。
- **两个对照把“×4”钉在父源上**：同一轮 `mux=0x53`（`gcc_disp_xo_clk`）`short=16399 full=262156 hz=19199963` → `delta-ppm=-1`；`mux=0x9a`（`gcc_pdm2_clk`，关闭）`short=full=262156 hz=0 running=0`（无 compare 行）。计数器对同一条时基报 19.2 MHz 的源差 1 ppm，对“被声明成 4.8 MHz”的源差 3e6 ppm——错的只能是声明。
- **厂商证据**：`MUX_SRC_LIST` 把 debug index `0x99` 映射到 `gcc_pdm_xo4_clk`；`0x33008` 的 branch 只有 `enable_mask=BIT(0)` 与 `clk_branch2_ops`，**没有 `.parent_names`、没有分频字段**，无法对父源 /4。兄弟 `gcc_disp_xo_clk`（`0xb048`）同样是 parentless 且实测 19.2 MHz。全系统唯一的 XO/4 是计数器自己的时基 `0x43008`（localID 7），公式里的 `4800000` 只是这个时基的常量。
- **补丁 0238**：把 localID 4 的描述父源由 `kIOQcomSourceXODiv4` 改为 `kIOQcomSourceXO`、`kIOQcomGccGateHz=19200000`，宿主测试同步为“门控 branch 按运行样本计数（每拍 4 个）”、`reads=29` 门控恒等式与整行 compare 断言；并把 `MH2LM_GCC_DUMP_ROWS=1` 的输出接到环审计器，使 0130／0131 两种形态的期望都由**编译出的真实行**验证（`selftest-gate-auditor.py` 同时跑 fixed 与 refuted 两态）。
- **0131 的期望**：`decoded-hz=19200000`、`measured-hz` 仍是设备计数给出的量级（历史 19199963／19200036）、`delta-ppm` 在容差内、`result=0`、`measure-done result=0`、无 `detail=measure-incomplete`；门控侧的行一字不变。
- **其余收尾**：`writes=31`、`restores=5 mismatches=0`、`hold=0`、账本 `token=2 holders=1 transitions=2 confirmations=2 release_value=0x1`；非时钟 WARN 共 5 条（`SWEET FB SCANOUT` ×3、`SWEET USB` ×2，与 b32 逐项相等，本轮**不需要** `--allow-baseline-drift`），面板标记 `scanout_transfer=1`／`fb_handoff=1`／`touch_mode3=1`／`probe_drained=1` 全在。
- **教训**：`0130` 的错不在代码执行，而在“名字即事实”的推断；环里的 `[WARN]` 本该是设计的一部分（它正是第 0130-1 号证据），所以审计器的做法是**记录反驳**而不是放宽断言——`--gate-declared-xo4` 只允许这一段以 `gate-parent-refuted` 收尾，随后被 0238 消掉。

## 实机验收（0131 门控＋测频档：第三源站在描述这边，`pass`）

- **镜像与环**：`boot-16k-0131-gpioirq-panel-pipeline-clockgatemeasure.img`（构建期 `853c9dd5…`；注意 boot.img 内含 FAT 目录时间戳，重打包会改总摘要，不变的是载荷：`kernel.macho` `6a861360…`、`kernel.macho.adt` `c0ba7498…`）；环 `logs/sweetlog-0131-0.bin`（`53543ac0…`，`total=1113159`，未回绕，14400 行）。审计 `--measure --c2-03 --gate`：**rc=0，`verdict=pass`**，`clock_warn=0`、`clock_error=0`，非时钟 WARN（`SWEET FB SCANOUT`×3、`SWEET USB`×2）与 b32 完全相等，**不需要 `--allow-baseline-drift`**，也不需要 `--gate-declared-xo4`（该旗标只对 0130／0130-1 那种“声明与实测相左”的历史环有意义）。
- **第三源（本轮的主角）**：`cc1:0x4 name=gcc_pdm_xo4_clk mux=0x99 short=16396 full=262156 hz=19199963 running=1 polls=46/689` → `measure-compare result=0 decoded-hz=19200000 measured-hz=19199963 delta-ppm=-1 tolerance-ppm=10000`。0130-1 的 `3000007 ppm` 归零；`gate_declaration_refutation=null`、`verdict=pass`。
- **两个对照与 0130-1 同量级**：`0x53` `short=16399 full=262156 hz=19199963`（−1 ppm）、`0x9a` `short==full=262156` → `hz=0 running=0`。三轮（0128／0130-1／0131）里同一支 XO/4 时基对 XO 直通源都停在 ±1 ppm，说明 `19200000` 这个解码值本身是设备认可的。
- **门控行零漂移**：`gate`（`clock-measure=1 clock-gate=1`）→ `request value=0x80000000` → `acquire transition=new` → `transition owner=1` → `write-issued stage=gate-enable value=0x80000001`（全程唯一一条门控 store）→ `running enable=1 off=0 value=0x1 polls=0` → `confirm confirmations=1` → `gate-check neighbours=4 changed=0` → `completed detail=sample-kept-until-reboot` → `gate-done writes=1 neighbours=4 changed=0 polls=0 hold=1`。与 0130／0130-1 逐字相同：改的只是描述，硬件行为没有任何变化。
- **账本与收尾**：`token=2 holders=1 transitions=2 confirmations=2 release_value=0x1`（门控 token 仍不释放，测量 token 归还 `holders=0`）；`measure-done result=0 writes=31 restores=5 mismatches=0 hold=0`，五条 `restore` 行的 `value` 与 `expected` 全部相等。
- **读代价**：`reads=2147`（`29 + polls` 的门控段加上三源测量与五次恢复校验），远在轮询上限之内。
- **默认档回归（同一内核，去掉两个开关）**：`boot-16k-0131-gpioirq-panel-pipeline.img`（ADT `081f63dc…`，`sweet,clock-gate`／`sweet,clock-measure` 各 0 条）实机进桌面，环 `logs/sweetlog-0131-1.bin`（`13e8c739…`，`total=1092625`，未回绕）。审计 `--c2-03`：**rc=0、`verdict=pass`**（`--strict` 亦过），26 条时钟行、`writes=0`、`reads=19`、`maps=4`、`snapshot=11`、`decode=7`、`decode-sample 19200000`，非时钟 WARN 与面板标记均等于 b32，无 `gate`／`write-issued`／`measure`／`restore`／`acquire`／`release` 行。即 `0238` 只改了声明，无开关路径一次写都没多。
- **审计命令的一个细节**：`0237` 之后描述表是 7 条，默认档环也必须带 `--c2-03`（该旗标表示“这棵内核声明第 7 个描述符”，与是否开门控开关无关）；不带它，`artifact-0126` 的基线期望仍是 6 条，会在 `decode` 计数上直接断言失败——这不是设备问题。
- **结论**：C2-03 的两半——门控行为与父源身份——现在都有实机证据，且第二轮证据是**在修正之后独立取得**的。0237／0238 一并收口，下一张卡是 C2-04（关闭路径）与 C2-05（共享）。

## 本候选实现（C2-04 关钟＋恢复，补丁 0239）

实现文件：[IOQcomGCC.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.h)、[IOQcomGCC.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.cpp)、[mk-mh2lm-mdss-adt.py](../../tools/mk-mh2lm-mdss-adt.py)。C2-03 留下的样本是一条**只进不出**的需求：门开了、token 还挂着、`0x133008` 一直 `enable=1`，要靠重启才能回到原状。C2-04 把“退回这条需求”做成一条经过校验的路径——先确认这条 branch 确实是本驱动自己打开的，再只撤掉本驱动那一位，等硬件自己报 halt，最后把已验证的开钟路径再跑一遍，使开机终态与 C2-03 基线逐值相同。

- **关钟契约（`disableClocks()`，紧跟 `gateClocks()` 之后、`measureClocks()` 之前）**：守卫（有 provider、`fGateTokenHeld`、token 非零）→ 读 CBCR（`phase=disable-request`，打印 `enable`／`off`／`value`）→ 若已 halt（`detail=already-halted`）或未使能（`detail=not-enabled`）则不写、不释放 → 先抓四个邻位（`gateNeighbours(true, "sweetclock-disable")`）→ 只写 `saved & ~kGccBranchEnable`（`stage=disable`，走同一个 `writeRegister`：对齐／越界拒绝、`dmb oshst`→store→`dsb osh`）→ 以 bit31 `CLK_OFF` 置起作为 halt 判决（同一 `2500×10 µs` 上限，`phase=halt`，`polls` 可对账）→ 再读四邻位比对（`phase=disable-check changed=0`）→ 账本 `ledgerRelease(token, stopped=true)`：凭据的归还是**以 halt 为确认**的，而不是无条件的“我要还” → `phase=disable-done writes=2 … hold=0 last-holder=1 detail=sample-halted` → 随即 `gateClocks()` 恢复（新 token）并进入测量。
- **拒绝与失败归属**：`no-token`／`provider-absent`（`NotReady`，不写）、`already-halted`／`not-enabled`（不写不释放，也不掩盖异常）、`halt-read-failed`（轮询自身读失败）、`halt-timeout`（2500 次仍未见到 `CLK_OFF`：**保留已写的撤回状态，不写回、不释放**，`phase=disable-done … hold=1`，`start()` 收尾补 `WARN detail=disable-incomplete`）、`neighbour-changed`（四邻位在 halt 期间被人动过：保留而不“帮它修回去”，`IOError`）、`ledger-release-failed`。
- **“不撤销不记账的 store”是本卡的核心边界**：halt 超时或邻位漂移时，撤回位已经写出去了，但硬件没有按预期 halt、或周围状态已不可信；此时任何“写回去装作没发生”的动作都会把未知状态变成已知错误。本卡因此只保留、只报错，把修复留给重启，与 C2-03 超时语义同构。
- **为什么要恢复**：开机终态必须与 C2-03 基线一致（`0x133008` 为 `enable=1 off=0`），否则下一次开机的门控样本与测量第三源都不成立。恢复走的是同一个已验 `gateClocks()`，不是另一段手写 enable，因此账本会看到第二次 acquisition（`token=2`、`transitions=3 transition=new`）与第三次 confirmation。
- **opt-in 与合成树**：`--clock-disable` 隐含 `--clock-gate`（生成器与后端同时强制；只有 `sweet,clock-disable` 而缺 `sweet,clock-gate` 时后端打印 `WARN detail=clock-disable-needs-gate clock-gate=0 clock-disable=1` 且不进入关钟）。门控＋关钟树 `b1716968…`（23648 字节）、门控＋关钟＋测频树 `a043901b…`（23688 字节）；默认／测量／诊断／门控／门控＋测频五棵树逐字节不变，参数表仍 605 字节。
- **证据（主机）**：`tools/tests/test-mh2lm-gcc-backend.py dxb/xnu` 通过——happy path 断言 `fReads==49`、三次 store（`0x80000001`／`0x0`／`0x80000001`）、终态寄存器回到 `0x80000001` 且 `hold=1`；halt 超时断言 `polls=2500 writes=2 hold=1` 且寄存器停在 `0x0`；邻位漂移与 release 被拒各有一条。`tools/tests/test-mh2lm-mdss-adt.py` 增 4 项（两条属性的隐含关系、可加性、可逐条去掉、CLI 与库的字节等价）。补丁 `0239` 在基线 `93878eb` 上严格重放 5/5 `SAME`。
- **审计器**：`audit-clock-log.py --gate --c2-04 --c2-03` 新增第三人格，读账算成 `19 + (10+polls_g) + (10+polls_d) + (10+polls_r)`，即 **`49+polls`、三次 store**；`selftest-disable-auditor.py` 用 0131-1 真实环造的同形环跑 14 个变体（超时／读失败／已停／未使能／邻位漂移／release 被拒／恢复未完成，以及把故障行降成 `INFO` 的四个 `silent-*`），全部按语义拒绝。**设备纠正过一处模型**：恢复段 `acquire` 打印的 `transitions` 是 3——账本把每次 acquisition 记作一次 transition（门控、halt 那次归还、这次恢复），审计器与合成环已据此更正。

## 实机验收（0132 门控＋关钟档：一次开机里开、关、再开，`pass`）

- **镜像与环**：`boot-16k-0132-gpioirq-panel-pipeline-clockgatedisable.img`（`c2f4f6f4…`；载荷 `kernel.macho` `dfcf79e5…`、`kernel.macho.adt` `b1716968…`）；环 `logs/sweetlog-0132-1.bin`（`c24406ba…`，`total=1006585`，`flush_sequence=8`，未回绕）。审计 `--gate --c2-04 --c2-03 --strict`：**rc=0、`verdict=pass`**，51 条时钟行、`clock_warn=0`、`clock_error=0`。
- **一条完整的关钟链**：`gate clock-measure=0 clock-gate=1 clock-disable=1` → 门控段与 0130／0131 逐字相同（`request value=0x80000000`、`write=1 value=0x80000001`、`running value=0x1 polls=0`、`changed=0`、`hold=1`）→ `disable-request enable=1 off=0 value=0x1`（硬件在 enable 之后已清掉 `CLK_OFF`，所以请求读到的不是快照值）→ `write-issued stage=disable write=2 value=0` → `halt result=0 enable=0 off=1 value=0x80000000 polls=0`（**首读即 halt**，与开钟首读即运行为对称）→ `disable-check neighbours=4 changed=0` → `release token=1 holders=0 desired=0 pending=0 confirmed=0 transitions=2 confirmations=2` → `disable-done writes=2 neighbours=4 changed=0 polls=0 hold=0 last-holder=1 detail=sample-halted`。
- **同一开机内的恢复**：`request value=0x80000000`（又回到快照值）→ `acquire token=2 … transitions=3 transition=new` → `write-issued write=3 value=0x80000001` → `running value=0x1 polls=0` → `confirm confirmations=3` → `gate-check changed=0` → `completed token=2 detail=sample-kept-until-reboot` → `gate-done writes=3 hold=1`；收尾 `resource=cc1 phase=start … windows=4 snapshot=11 reads=49 writes=3 variant=0x8150`。读数正好是 C2-03 默认档的 19 加三段各 10：`19+10+10+10=49`，写入 `1+1+1=3`。
- **账本语义（本卡真正被验证的部分）**：释放只有一次，且带 halt 作确认（`stopped=true` → `transitions=2 confirmations=2`，不是 `IOError hardware-still-on`）；随后同一资源被重新取得（新 token、`transitions=3`），全天一条 `release` 行——这就是“需求被交还、而不是被丢弃”。
- **非时钟回归**：`SWEET FB SCANOUT`×3、`SWEET USB`×2，与 0127／0130 各环同形，无新 WARN 种类、无 ERROR；触摸标记这一轮是 `touch_armed` 2 对 b32 的 4、`touch_app_format` 1 对 3，与 0127／0130 各环同形的既有差异（默认档那一轮又与 b32 全等，说明这组标记随本次开机的触摸事件数走），不是本卡引入。
- **首次拉环作废的教训**：第一次的 `logs/sweetlog-0132-0.bin`（`04975515…`，`total=5089694`）已回绕——4 MiB 里有 **4105748 B／54023 行**是 `IOWorkLoop::threadMain()` 每次线程重入打的那条 `SWEET IOWORKLOOP: thread …`，占 98%，开机最早写入的时钟行全被挤掉（`SWEET CLOCK` 0 行）。该环仍证明设备健康（`SWEET MDSS IRQ … window-ms=60096 frames=3600 milli-hz=59903 dropped=0`、`SWEET FB PERF swaps=22`、`SWEET TOUCH IRQ reports=1`×17、`SpringBoard`／`backboardd` 均在跑）。结论：**环要在开机进桌面后几十秒内导**；那条打印应改成限频或只在首个 loop 打一次，留待下一轮镜像顺手处理，不影响本卡结论。
- **默认档回归（同内核，三条开关全去掉）**：`boot-16k-0132-gpioirq-panel-pipeline.img`（`38aa7134…`；ADT `081f63dc…` 23568 字节——`read-adt-claims.py` 直接从 ramdisk 读出核对，`sweet,clock-gate`／`sweet,clock-disable`／`sweet,clock-measure` 三条全不在）实机进入桌面，环 `logs/sweetlog-0132-2.bin`（`32fa2a89…`，`total=994381`，`flush_sequence=8`，未回绕）。审计 `--c2-03 --strict` **rc=0**：26 条时钟行、`reads=19`、`writes=0`，相位恰好 `start 2`／`map 4`／`snapshot 11`／`register 1`／`decode 7`／`decode-sample 19200000`，`gate`／`write-issued`／`disable-request`／`halt`／`acquire`／`release`／`measure` **一条都没有**，零时钟 WARN／ERROR，非时钟清单与面板标记都和 b32 全等（`marker_drift=null`）。即：关钟代码确实在这个内核里，属性不在时它一步也不走。
- **结论**：C2-04 的写到 halt、halt 到账本确认、确认到恢复三段，都在同一台设备同一次开机里完成，且恢复后的终态与 C2-03 基线逐值相同。下一张卡是 C2-05（同一 branch 的多消费者共享）。
## 本候选实现（C2-05 共享，补丁 0240）

实现文件：[IOQcomGCC.h](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.h)、[IOQcomGCC.cpp](../../dxb/xnu/iokit/Kernel/arm/IOQcomGCC.cpp)、[mk-mh2lm-mdss-adt.py](../../tools/mk-mh2lm-mdss-adt.py)。C2-04 撤回的是“本驱动唯一持有的那条需求”，任何第二个持有者都会让那条撤回变成“别人还在用就把钟关掉”。C2-05 让两个 XNU 消费者共享门控已经打开的那条 branch：同一账本条目上挂三个持有者（门控自己＋两个共享消费者），谁都不是所有者，谁都不在别人还持有的时候写硬件；只有**最后一笔归还落在一个仍在运行的 branch 上**时，才走 C2-04 那条已验撤回把它关掉，然后把 branch 再开回来，使开机终态仍是 C2-03 基线。

- **撤回体被参数化（`retractBranch(token, consumer, stage)`）**：0239 的关钟体从 `disableClocks()` 搬出来，判决与行文一字未改，只把 consumer／stage 变成参数；`disableClocks()` 缩成“守卫 + `retractBranch(fGateToken,"sweetclock-disable","disable")`”。因此 0132 的环在 0133 内核上仍逐字可读，0239 的宿主断言与 `--c2-04` 审计都不需要改。`fGateToken`／`fGateTokenHeld`／`fGateRunning` 只在撤回归还成功时清，任何失败路径一律保留凭据。
- **共享段（`shareClocks()`）**：`fShareEnabled` 才有；守卫 provider／`fGateTokenHeld`／token → 读一次 CBCR 作为**共享前**的证据（`phase=share-request` 打印 `enable`／`off`／`value`；已 halt 或未使能则拒绝，`hold=1`）→ 两个消费者各 `ledgerAcquire` 一次（`sweetclock-share-a`／`-b`，行里带 `opened=%u`）→ 每次取得立刻 `ledgerInspect`：`opened=1` 意味着这条需求已不是门控留下的那一笔（`WARN detail=ledger-lost-owner hold=1`，已取得的逐个归还后放弃，不写硬件）→ 两笔都到手后打一行总账（`phase=share result=0 consumers=2 holders=3 clock-share=1 clock-gate=1 clock-disable=1`）。取得阶段的缺陷被真实编译暴露过：`taken` 原先在循环末尾自增，`opened=1` 的拒绝路径会把发出去的 token 留在账本里，改为取得后立即自增。
- **非最后一笔归还零写入**：按**逆序**归还（先 `sweetclock-gate` 自己的 token，再 `sweetclock-share-a`），每笔之前都读 CBCR 并打印 `running=1`，然后 `ledgerRelease(token, stopped=false)`；行里带 `token`／`holders`／`last-holder` 与读到的 `value`。**这次读就是“共享释放不碰硬件”的全部证据**——三次 store 里没有一次属于这里。若某笔成了最后一笔（`last-holder=1`），说明第二个共享消费者的引用早被别人拿走过，`ERROR detail=unexpected-last-holder` 并且**不撤回**：宁可留一个开着的钟，也不在一个来路不明的归还上写硬件。
- **最后一笔归还走已验证的撤回**：剩下的 `sweetclock-share-b` 必须在归还前看见 `holders=1`（否则 `WARN detail=holders-mismatch`，不撤回）→ 打印 `phase=share-last result=0 token=%u holders=1 action=retract` → 交给 `retractBranch(token,"sweetclock-share-b","share-disable")`，即与 C2-04 完全相同的写入／halt 轮询／邻位比对／账本确认 → 成功则 `phase=share-done result=0 writes=3 consumers=2 retracted=1 last-holder=1 holders=0 detail=last-release-retracted`，同一 `start()` 里的恢复随即把 branch 再开起来（第三次 acquisition，`transitions=3 confirmations=3`），终态与 C2-04 逐值相同。失败则 `WARN phase=reenable … detail=reenable-incomplete`，不假装恢复过。
- **账本语义（宿主模型被真实语义纠正过一处）**：共享取得**不重开**条目——`pending`／`confirmed`／`transitions` 仍是门控那一次留下的值（`pending=0 confirmed=1 transitions=1`），只有 `holders` 走 1→2→3。原先的宿主断言写的是“共享取得会把条目重开成 `pending=1`”，已被改掉；真正有 `hardware-still-on` 风险的只有最后一笔，而本卡让它以 halt 作确认。
- **opt-in 与合成树**：`--clock-share` 同时隐含 `--clock-gate` 与 `--clock-disable`（生成器与后端双强制；缺任一条时 `WARN detail=clock-share-needs-gate-disable clock-gate=%u clock-disable=%u clock-share=%u`，不进共享）。共享树 `264b4a5c…`（23688 字节）= 关钟树 + 一条 `sweet,clock-share`；共享＋测频树 `3201cb02…`（23728 字节）= 关钟＋测频树 + 同一条。默认／测量／诊断／门控／门控＋测频／关钟／关钟＋测频七棵树逐字节不变，参数表仍 605 字节——属性是唯一通道，`--clock-share` 单独调用也必须得到同一棵树。
- **证据（主机）**：`tools/tests/test-mh2lm-gcc-backend.py dxb/xnu` 通过——happy path 断言 `fReads==52`、三次 store（`0x80000001`／`0x0`／`0x80000001`）、token 1..4（门控、a、b、恢复）、`phase=share*` 七行齐全且 `phase=confirm result=0` 恰好两次、终态 `fShareToken[0]==fShareToken[1]==0`、`fShareHolders==1`、账本 `transitions=3 confirmations=3`；拒绝路径（缺 gate 或缺 disable、`length=3`、`value=0x2`）零读零写；继承运行、`ledger-lost-owner`、halt 超时、`unexpected-last-holder`、`holders-mismatch` 各有断言。`tools/tests/test-mh2lm-mdss-adt.py` 增三项（三条属性的隐含关系、可加性、逐条去掉与 CLI 字节等价）。补丁 `0240` 在 `0239` 之上增量：基线 `a5b2112` + `0239` + `0240` 重放后五个文件与工作树逐字节相同。
- **审计器**：`audit-clock-log.py --c2-05` 新增第五人格，读账是 `19 + (19+polls_g) + (…d) + (…r)` 之外的**三笔**——`share-request` 一次加两笔非最后归还各一次 CBCR 读，合计 **`52+polls`、三次 store**；行序要求 `share-done` 落在恢复段 `gate-done` 之后。`make-share-ring.py` 从 `sweetlog-0131-1.bin` 真实环裁剪合成变体（`ok`／`lostowner`／`halted`／`notenabled`／`unexpected-last`／`stall`／`neighbour`／`norecovery`／`noshare` 与七个 `silent-*`），`selftest-share-auditor.py` 18/18 PASS。默认人格仍拒绝任何时钟 `WARN`／`ERROR`，`silent-*` 用同形 `INFO` 行证明逐字段判据真的在工作。
- **镜像（0133，尚未实机）**：`out-bs/mk16k-0133.sh` 在 0132 的八档之上加 `clockshare`（`adt-gate-share`）与 `clocksharemeasure`，其余七档照旧；脚本对 C2-05 的两个新符号（`retractBranch`／`shareClocks`）、全部 C2-05 行、两条新树的摘要逐条设卡，拿 0132 内核当 `$1` 会在第一条新符号处失败——刻意的负向验证。
- **C2-04 环在 0133 内核上仍必须可读**：`disableClocks()` 现在只是一个参数化的 `retractBranch`，行文法未变，故 `logs/sweetlog-0132-1.bin` 用 `--c2-04` 仍 rc=0；而把它当 C2-03 门控档审会 rc=1（环里有 `disable`／`release` 行），这正是 `--c2-04` 存在的理由。
- **尚未做的**：0133 两档实机验收（需先由 CI 产出含 C2-05 的内核）；`SWEET IOWORKLOOP` 限频**不并入**本轮——证据镜像一次只带一个变量，那条打印的限频留到 0134，单独一轮验。
