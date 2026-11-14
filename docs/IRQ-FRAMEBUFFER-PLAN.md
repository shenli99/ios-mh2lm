# MH2LM IRQ、Framebuffer、日志与电源管理路线图

## 0121a36 实机进展（2026-11-14，普通SDR唤醒功能已验）

[a36实机审计](PANEL-D1-0121A36.md)：USB确认mode7/PV四轮OFF/ON，generation1→2→3→4→5；每轮原17包LP/4DMA→display引用→六包HS色彩/1DMA完成→首帧→HS亮度→同generation第二kickoff→触摸/ON完成。用户反馈“我感觉没问题”，a36成为普通SDR唤醒最新实机基线。
第2轮五个完整60000–60100ms IRQ窗口、59.914–59.933Hz、dropped0；第1/3轮仅10188/7800ms，第4轮无完整窗口，不称两轮各60秒、全档亮度或色度一致性通过。USB7710行到SYSTEM_OFF，含3NUL；无a36持久对照。BUG-006/007继续保留，未确认旧画面闪现消失。
**下一步先审计C08完整命令/显示资源周期，再确定其余PD4实现批次。** D09动态HDR/专家/RGB、首次色彩、DSPP及LP/core/SMMU/QoS仍保留，完整显示驱动未完成；后续新批次可补足分轮观察时长。本轮只做日志审计/文档/本地提交，无驱动/CI/镜像修改、驱动测试重跑、push或设备操作。证据scratch/artifact-0121a36/audit-wake.py、wake-log-review.json、wake-user-input.json；实现ba8a38a、交付0d07ca2。以下为历史。

## 0121a36 当前进展（2026-11-14，默认SDR色彩镜像已交付，待实机）

[a36/0226镜像与验收](PANEL-D1-0121A36.md)已完成80份源码、144source/251kernel markers核对；相比a35三份修改和一份新增，匹配ba8a38a。沿原ABI608打包identity/pipeline并回读通过，ADT/605与477byte参数与a35一致；a31–a35旧镜像保持。
**下一步用户先开USB采集，再测试a36/mode7：首次接管/调光正常后，两轮OFF/ON各保持亮屏至少60秒，观察颜色/锐度、亮度、触摸和持续刷新。** 新顺序为ON表/display引用→六包HS色彩/锐度完成→首帧→原0225亮度/第二帧→触摸完成，每轮generation只增加一次。BUG-007只记录、继续暂缓。
a36尚无实机结果，a35仍最新实机基线；D09动态状态、首次色彩、完整C08/LP/core及其余PD4仍保留。证据scratch/artifact-0121a36/final-review.json。本轮无驱动/CI修改、驱动回归重跑、push/设备操作。以下为历史。

## 0226 当前进展（2026-11-14，默认SDR色彩/锐度本地通过，待新CI/实机）

[D09普通SDR Auto色彩/锐度恢复](PANEL-LINEAGE-COLOR.md)已实现并提交ba8a38a：mode2/7 ON表LP/display引用→六包HS色彩/锐度真实完成→管线/首帧→原0225亮度与第二帧→触摸/FB完成。四profile按实际LG函数结果选择，PV单独保留20ms；默认RGB preset2 offsets0、锐度off。动态HDR/专家/RGB策略和首次接管色彩仍未实现，BUG-007按用户要求暂缓。
14组驱动运行、九个固定LG函数、每模式120条MDSS→FB状态、ARM64告警1596→1596、42patch/80文件严格重放通过。CircleCI仅增加3条源码断言，工作流/资源保持；144markers、YAML/4参数模板/6shell段通过。旧a35被新kernel门槛拒绝，原镜像保留；证据scratch/framebuffer-0226/final-review.json。
**下一步用户push并下载新CircleCI kernel/buildtree（建议0121a36），agent沿原ABI608核对打包mode7，再实机验证色彩/锐度、亮度和两轮OFF/ON各60秒。** 当前无新kernel/boot或实机结果，a35仍为带BUG-007的最新实机基线。D09动态状态、完整C08/LP/core、首次/持续提交及其余PD4仍保留；本轮未push/触发CI/操作设备。以下为历史。

## 0121a35 当前进展（2026-11-14，两轮唤醒/调光留证，BUG-007暂缓）

[a35/0225实机持久日志](PANEL-D1-0121A35.md)确认两轮OFF/ON，generation1→2→3；每轮17包LP/4物理DMA后首帧→排空→1包HS亮度真实完成→同generation重启/新帧门槛→触摸/ON完成，后续输入与CPU呈现存在。用户确认滑条调光正常。第一轮33913ms，第二轮两个完整60秒IRQ窗口，均dropped0；不记为两轮各60秒或60Hz UI。
a35为普通唤醒/亮度最新验证基线，带已知[BUG-007](BUGS.md)：唤醒先短暂显示息屏前画面再切锁屏，用户明确要求暂缓修复。根因未知，fresh frame不证明锁屏内容就绪。USB忘开，只审持久日志，不记为USB失败、不为此补测；BUG-006仍未修。
**D07/D08本批mode7普通路径验证收口；后续计划D09色彩/锐度，再其余PD4。本轮未开始D09或修BUG-007。** 完整C08动态票/LP/core/SMMU/QoS、首次/持续提交差异仍保留。a34/a33/a31旧基线保持。证据scratch/artifact-0121a35/audit-wake.py、wake-log-review.json；本轮仅日志/文档，无驱动/CI/镜像修改、驱动测试重跑、push或设备操作。以下为历史。

## 0225 当前进展（2026-11-14，本地通过，待新CI/实机）

[D07/D08首帧后亮度恢复与HS](PANEL-LINEAGE-BRIGHTNESS.md)已实现：mode2/7先ON表/首帧，再限时排空IRQ/MDP、恢复最新typed亮度，复用同generation重启并确认新帧后恢复触摸。普通亮度HS、ON/OFF表/通用DCS仍LP；prepared缓冲、完成/pin/引用/资源保护保持。
12组驱动回归、固定参考9函数与caller静态审计、ARM641596→1596告警、41patch/79文件严格重放、141源码markers通过；旧a34被新打包门槛拒绝。证据scratch/framebuffer-0225/final-review.json。
**下一步用户push并下载新CircleCI kernel/buildtree（建议0121a35），agent沿原ABI608核对打包mode7，再实机验证亮度/普通唤醒。** a34仍最新成功基线，无本批新kernel/boot/实机结果。通过后继续D09及其余PD4；完整C08/LP/core、首次接管/持续刷新差异、BUG-006仍单列。

## 0224 / a34 当前进展（2026-11-14，三轮实机通过）

用户确认无异常；[a34三轮OFF/ON实机与日志](PANEL-D1-0121A34.md)确认HS停→ULPS entry→OFF完整完成，ON每次17命令/4物理DMA、generation1→2→3→4、触摸/新像素/后续呈现。前两轮各完整60秒硬件刷新统计、dropped0；第三轮无完整60秒窗口。
[0224 D03/D04顺序修正](PANEL-LINEAGE-CLOCK-ULPS.md)本批普通路径验证收口。a34/mode7成为最新成功基线，a33和a31保留；a33的启动prepared4KiB缓冲复用与真实完成/pin保护仍通过。完整C08动态资源周期、LP/core物理释放及PD4仍未完成。
**下一步D07/D08亮度恢复时机与HS传输，再D09色彩/锐度及其余PD4。** 不重编或重复同版普通唤醒测试；没有本轮新增代码待CI。日志环BUG-006保持单列。以下为历史基线记录。

## 0223 / a33 已验证基线（2026-11-14）

用户确认0121a33亮屏成功；[0223实现](PANEL-LINEAGE-COMMAND-LIFECYCLE.md)的[两次OFF/ON实机与日志审计](PANEL-D1-0121A33.md)通过，generation1→2→3，启动prepared缓冲跨唤醒复用。a33/mode7为当前成功基线；完整PD4及全流程一致仍未完成。
正常mode7不再CTL reset；mode2/7的TE在flush前连接、OFF关闭tearcheck、命令引擎按display/transfer引用启停。4KiB物理缓冲在服务发布前准备并复用，明确早于Lineage首次命令懒分配；短FIFO/长物理DMA和真实完成/pin保持。
mode2/7循环/缓冲引用与故障/实际MDSS→FB、ARM64对象、39patch/79文件/132项source markers通过；ABI608/ADT/参数不变，旧a32打新档被拒绝。
本轮唤醒症状在a33两次OFF/ON中消失；移出gate的首次分配是最直接改善解释，但CTL/TE/引擎引用同时变化，未唯一确定a32根因。下一步保留a33基线，按全流程矩阵推进HS/ULPS、亮度/色彩等余项及PD4；a31仍保留，不重复旧版验证。

以下0222/a32及更早记录均为历史阶段，不作为“没有新代码待编译”的当前结论。


## 当前总进度（2026-11-14，a32唤醒失败审计）

**当前在大目标二，0222固定管线重建首次接管已实测通过，但a32息屏后无法唤醒；完整PD4尚未验收。**
mode7沿Lineage配置VIG0/LM0/CTL0/PP0/INTF1，并以新配置代数及新帧完成恢复。
0121a32产物/镜像核对通过，USB显示OFF完成及ON供电/ULPS/host/DDIC reset完成，尾部停在首次DMA缓冲分配入口；未见generation2。
[日志与回归审计](PANEL-D1-0121A32.md)未发现旧Lineage生命周期逻辑回退，但采集时长不确定、持久日志仅到OFF前，根因未确认。下一步查命令缓冲准备及日志连续性；[实现边界](PANEL-PIPELINE-REBUILD.md)保持，无新修复产物待编译。
本节替代下方历史“当前执行点”和旧待编译/待测试安排；a31/0221为已验证基线，实测记录提交56e7b5c。
上层调用链补充确认：0222的每次无条件CTL reset及kickoff后才连接TE，与Lineage正常command-mode流程不同；旧tearcheck关闭/命令引擎生命周期也有差异。详见[a32上层流程审计](PANEL-D1-0121A32.md)，后续需同时审查这些策略，尚未证明黑屏因果。

| 大目标 | 当前状态 | 剩余范围 |
|---|---|---|
| 一：既有驱动IRQ化 | 按键、USB、触摸IRQ已投入使用；电池事件驱动及基础充电有实测 | 电池计划仍保留回充、温度IRQ、部分停止/上报/组合验收，不能将整个目标写为全部完成；不因此重开显示前置测试 |
| 二：完整显示/面板驱动 | **当前主线**。F1分层、F2基础电源、F3硬件vsync正常使用通过；F4自有A扫描与裁剪、PD2链路和PD3普通OFF/ON已有实测 | PD4独立重建；PD1无ABL首帧/console/panic；PD5生命周期；F5直扫及提交/等待/复用；F7默认与旧路径收尾 |
| 三：日志结构与管理 | 已有分级日志基础，专项未展开 | 按驱动/事件独立开关、统一过滤与通道规则、输出量及故障保留 |
| 四：整机电源管理与低功耗 | 仅登记目标，尚未细化实施 | CPU/系统休眠、其他设备低功耗及全系统唤醒协调；仍排最后 |

a31已证明panel票撤销/恢复、ULPS退出、host soft reset、DDIC/长包命令、自有source/flush和触摸恢复，
用户确认画面/按键/触摸/亮度正常。硬件帧统计约60Hz，当前仍为自有A单缓冲/30Hz CPU拷贝，不能称零拷贝或60Hz UI。
只有一次普通OFF/ON，不能替代完整MDP/INTF/SMMU状态丢失后的重建及多轮/故障验收。

既定后续顺序：**补齐PD2管线配置并推进PD4独立重建 → PD1剩余ABL依赖/PD5必要生命周期 → F5直扫 → F7收尾**；
F6按帧kickoff为按需优化，CPU拷贝双缓冲与60Hz拷贝优化继续延期，不作为下一步前置。
详细阶段对照见[完整面板子计划](PANEL-FULL-DRIVER-PLAN.md)，完整显示目标见[显示计划](FRAMEBUFFER-PANEL-PLAN.md)，
已验证基线见[a31实测](PANEL-D1-0121A31.md)。0222未改命令FIFO/物理DMA，SMMU/core/QoS/色彩等保留依赖继续在显示目标内补齐；不转给整机低功耗。

## 历史执行记录

**历史执行点（2026-11-13）：a25已分析；0213局部状态/复位取证已提交7f4f55a，待用户push云编译。**
a25首错仍为branch7/+4004 branch-wait，PA正确，立即及39次等待均未见enable，整体期限仍余188.3ms。
RSC准入为NON_GDSC0/RSCC_AHB1/RSCC_VSYNC1；十次apps IRQ和core2通过，未display TCS/新DMA。
实际store/load及ClockDxe静态直接CBCR路径已核对；LG halt等待与XNU额外enable验证须区分，根因未定。
0213补warm core入口/branch7写前各六项局部读取，超时仅再读BCR(+4000)；无新写入或控制条件修改。
27场景ASan/UBSan与0212基线仅差允许采样，其余MMIO/结果/所有权/timer/模拟时间相同；15套回归通过。
ARM64对象编译告警1552→1552；0185–0213严格重放/78源码/114原CI markers通过，旧a25被新LOCAL门槛拒绝。
见[a25实测](PANEL-D1-0121A25.md#实测写后及等待均未观察到使能)、[0213实现与交付](PANEL-RSC-LOCAL-RESET-EVIDENCE.md)。
新kernel/buildtree建议0121a26，核对后沿原ABI608 retained取完整日志；当前无新镜像。a25不用补日志或重测，其他显示测试暂停。


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


**0121a22产物已核对并打包，待实机：** 78源码匹配0209（a325219），114 source/173 binary markers通过。
固件ABI608、ADT和605字节retained参数保持0121a21；boot/FAT回读通过。
本轮只启动retained约90秒取首条CLOCK/NOC gate、CONTEXT和原NoC错误；仍属诊断，未证明修复。
镜像路径、校验值和USB命令见[0121a22交付](PANEL-D1-0121A22.md)。不需再次云编译；接管前其他显示测试继续暂停。


**0209已准备（代码`a325219`，本地通过、未实机）：** 为0121a21的display NoC NotReady补
RSC clock/lease/parent/branch、RSC layout及wrapper/seq/solver的具体拒绝日志，失败附软件CONTEXT。
原MMIO读写/顺序/保护/期限不变；新专项29场景+3失败路径与0208完整MMIO轨迹及结果一致，另10套回归/MDSS ARM64对象通过。
现在需要用户push云编译；新产物核对后沿用ABI608和0121a21 ADT/参数打retained，取首个gate及CONTEXT。
本轮用于取证，未宣称已修复；接管前OFFON/combined/supply仍暂停。
详见[实现、分支、验证和下一步](PANEL-NOC-ADMISSION-DIAGNOSTICS.md)。以下“无新编译要求”为历史。

**0121a21实测更新：** apps RPMh共10次真实完成IRQ，四LDO、ARC和CN0 ACK通过，core profile2成功。
新首错为display NoC token1/NotReady(0xe00002d8)/data ACK mask0；后续managed/HANDOFF失败是连锁结果，未提交新DMA。
现日志未分RSC clock lease与RSC/wrapper准入拒绝，不能认定具体寄存器或硬件拒票；下一步补NoC分支取证。
详见[逐项证据、边界与下一步](PANEL-D1-0121A21.md#实测结果apps-rpmh通过display-noc准入失败)。
本次仅分析记录，无新生产补丁/编译要求，不重刷旧镜像；接管前OFFON/combined/supply仍暂停。
以下0121a21“待实机”等为历史。

**0121a21已核对并打包：** 78源码匹配0208（代码190c558）、114 source/171 binary markers及镜像回读通过。
只启动新ABI608 retained档，等约90秒导完整启动日志，目标INTID37 configured→fresh ACTIVE completion→
ldoa14 ACK及后续首错/新DMA。固件/605字节参数同0121a20，hardware ADT新增显式RPMh IRQ扩展，identity不变。
接管前不测亮度/OFFON/长测/反复USB，combined/supply仍暂停；见[镜像路径与步骤](PANEL-D1-0121A21.md)。
以下0208“待push/待云编译”为历史，实机仍待验收；CMD_STATUS delta诊断示例未接入本轮生产内核。

**0208最新执行点（代码`190c558`，本地通过、未实机）：** 当前仍在面板D1。
0121a20证明首次ldoa14请求缺fresh完成证据；已接 apps RSC INTID37 的GIC callback/software event/
workloop完成路径，不放宽ACK或期限。16套回归及RPMh/sleh ARM64对象通过，待用户push云编译。
新hardware ADT追加IRQ资源，产物核对后重打retained，只先取启动日志；完整接管前OFFON/
combined/supply暂停。详见[完成IRQ实现及验证](PANEL-RPMH-COMPLETION-IRQ.md)。以下进展按历史保留。

**0121a14产物进展（2026-11-12）：** 已取得77c7631/0201的云端链接内核，78份源码与
本地verify逐字节一致，114 CI marker及7个新增关键方法的最终内核定义通过。
四档identity/retained/combined/supply已打包并回读，使用新Mu固件，同kernel/ADT，无重复参数。
**当前先测panel-identity，等待live身份与正常启动日志，尚无实机结果。**
不再需要为0201重复push/编译；后档在本档日志通过后逐级测试。详见
[本轮产物、路径、检查和测试步骤](PANEL-D1-0121A14.md)。以下0201“待云编译”按历史阶段保留。

**0201当前增量（2026-11-12，本地通过，未实机）：** 统一协调器新增显式
`sweetpanellifecycle=6`：完整OFF→reset实际低保持→原mode5 DATA/link停驻→VPNL/VDDI
逆序撤本panel票；ON先active-low→VDDI ACK+20ms→VPNL ACK+5ms，再恢复DATA/link/clock/
host/reset/完整ON/亮度/fresh像素/触摸。共享L3/L5、ARC、config floor和boot/RSC/core通路保留。
每ACK更新部分完成账本，失败先同步取消RPMh子事务再撤Pins/Touch token；FB核对联合证据。
原0–5保留，模式6默认关闭。撤本票不是物理rail-off证明，E7/E8仍待实机。
23套检查、8个真实XNU/KDK arm64单文件对象通过；**完整Mu mh2 RELEASE FD现已构建并核验**，
包含原0195身份adapter，旧blob未改。新`--panel-lifecycle`支持同核identity/retained/combined/
supply四档，先核对live身份，再逐级验收；缺新固件manifest或内核marker就拒绝打包。
现在需要用户push并云编译，下载后再打实际boot；CircleCI未改，未push/触发CI/操作设备。
详见[供电生命周期、架构、四档与证据](PANEL-SUPPLY-LIFECYCLE-D1.md)。
PD4冷NoC/MDP、ABL首帧/console/panic退出、故障后受控释放及完整IOPM仍缺；完整驱动未完成。
**以下0200及更早段落是各阶段历史记录，当前状态以本段及NOTES顶部为准。**

**0200供电依赖校验增量（2026-11-12，本地通过，未实机）：** Pins 新增实时 reset-low
查询，RPMh 新增仅 VDDI/VPNL 的 typed panel vote/取消接口，逐写核对同代 Pins/Touch token，
保留依赖引用，异常停止并保留部分事务；不撤共享 L3/L5。22套检查及八个真实 arm64
单文件对象通过。**尚未接入协调器供电释放/恢复，未新增档位，mode0–5正常路径不变。**
下一步接 LG 顺序、部分撤票账本及联合完成证据；E7/E8仍未实机。详见
[接口、锁顺序、失败边界及验证](PANEL-SUPPLY-FENCE-D1.md)。无新 FD/boot、push/CI/设备操作，
CircleCI 未改，暂不为接口单开云编译。以下旧阶段按各自范围保留，当前以此段及NOTES为准。

**0197当前增量（2026-11-12，本地通过，未实机）：** 协调器新增显式`sweetpanellifecycle=4`：
完整OFF后实际关闭host、BYTE/PCLK分支、PHY PLL/bias和PHY blocks，确认停稳；ON复用原执行层
重新reset/配置PHY、PLL、root和host，再执行面板reset/ON/亮度/fresh像素/触摸恢复。
全部资源票保留，clock查询另报hardwareSuspended，暂停期间普通接口拒绝操作；不是rail-off。
原0/1/2/3语义保留，无降档；额外link使用者/DSI1 alias在DDI副作用前拒绝，子timer重查fence和期限。
20套本地检查及MDSS/FB真实arm64单文件编译通过，严格重放/114原CI marker通过。
详见[硬件下电、重建与架构](PANEL-LINK-POWER-D1.md)。完整供电释放、冷NoC/MDP及ABL退出仍缺，
live profile/E7/E8未实机；无新FD/boot、push/CI/设备操作，CircleCI未改。暂不单开云编译/刷机。
下一步组合资源票释放与硬件下电，继续完整驱动。以下0196及更早段落保留当时范围，当前以0197为准。

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

**0191当前增量（本地通过，未实机）：** 实际 managed 接管已在资源完成后、host 重建前，
加入 PHY v4.0 / PLL 7nm-v2 复位重配和 BYTE/PCLK 时钟重建。沿原 MDSS owner/gate/DSI timer，
显式 `sweetdsilink=2` 要求 host1/core2/managed1；默认0保留0190行为。
先分别确认分支/root 停稳、旧 lock 清除，再配 PHY/分数频率/SSC/分频；新 lock、RCG 更新和
实际运行分别确认，最终仍需 host 重建及 fresh 像素完成。12套本地检查和arm64对象编译通过。
profile 为 LG 板级选择，原始 revision byte 记录不当作自动识别；尚无新实机写入证据。
面板 reset/vendor ON/OFF、反向释放、整屏冷启动及 ABL 退出仍缺，完整驱动未完成。
详见[PHY/PLL/输出时钟实现与验证](PANEL-LINK-D1.md)。下一步面板生命周期/独立重建，
完整驱动优先、之后零拷贝；暂不单独要求 push/刷机，A单缓冲/30Hz拷贝限制不变。
以下0190及更早段落按当时范围保留，当前以0191及NOTES顶部为准。

**0190当前增量（本地通过，未实机）：** 实际 managed 扫描接管在 NoC/DSI0 lease 完成后，
接入 host v2.3 软复位和明确的 RGB888 command-mode 配置重建，再继续原自有 A/fresh 像素完成。
复用 MDSS 唯一 owner/gate/DSI timer；默认关闭，显式 `sweetdsihost=1` 要求 managed1/core2
及原依赖。失败保留资源/部分状态、关闭后续写入，无自动恢复旧配置。PHY/PLL 仍继承，
未发送 vendor 表或操作面板 reset；没有整屏冷启动/独立重建证据，完整驱动仍未完成。
详见[host实现与验证](PANEL-HOST-D1.md)。下一步补 PHY/PLL 和完整面板生命周期；
完整驱动优先、之后零拷贝，暂不单独要求 push/刷机，旧性能限制不变。
以下旧段落按当时范围保留，当前增量以0190为准。

**0189当前增量（本地通过，未实机）：** 显式 core profile2 已将 GDSC 软件票、两颗核心
PLL、AHB75MHz/MDP460MHz 的配置与完成检查接到实际 managed 扫描资源链，复用 MDSS
唯一 owner/gate/clock timer；先实际 ARC/CN0 ACK，失败保票并关闭写入。工作 PLL 保留，
关闭 PLL 才初始化；不改 DSI PHY/PCLK/BYTE。冷态核心引擎已做组件测试，但真实 start 仍为
温态入口，尚无整屏冷启动/完整断电恢复或撤票。详见[核心资源控制](PANEL-CORE-D1.md)。
默认关闭，无新刷机轮次，继续补 DSI host/PHY/PLL 和面板完整 ON/OFF；D1 未整体完成。
以下旧段落按当时范围保留，当前增量以0189为准。

**0188当前显示进度（本地验证通过，未实机投票）：** 已把四路LDO、ARC、CN0/display NoC与
本地clock事务接到实际自有扫描接管，显式managed模式在暂停/idle后顺序获取资源，全部完成
才pin/提交A，并继续要求fresh传输完成。部分失败保留票并关闭写入，不自动恢复旧流。
默认关闭，DCS OFF暂不撤票；冷GDSC/时钟父源、PHY/PLL/host及完整面板ON/OFF仍缺，D1未完成。
见[资源获取与失败边界](PANEL-MANAGED-D1.md)、[NoC](PANEL-NOC-D1.md)、[ARC](PANEL-ARC-D1.md)。
下一段补冷域/时钟/链路，再完成资源反向释放及独立重建；暂不单独要求push或刷机。
下列0185/0184段落保留各自历史范围，当前增量以上述0188为准。


**0185本地时钟增量（本地验证通过，未实机启用）：** SweetMDSS内部已加入typed本地分支lease、
共享引用、token、timer完成/超时和停止保护；温态校验继承GDSC，默认关闭。
仅对已确认XO父源的关闭分支支持恢复/撤回，既有enable不关；没有MMCX/NoC票、GDSC冷上下电、
PLL/PHY/host初始化，不能验收完整D1。另修复OFF跨呈现间隔统计，pending年龄保留。
实现与本地验证见[时钟记录](PANEL-CLOCK-D1.md)；继续合并上游依赖/链路，不单独要求push或刷机。


**0184资源层增量（本地通过，未上机）：** 已新增真实匹配的SweetRPMh IOService、唯一
MH2LM apps-DRV writer、有界timer完成与typed consumer投票；MDSS自己的workloop绑定/查询，
没有匹配回调初始化、全局C实例桥或自动电源写入。v1 ADT扩完整DISPCC并追加单一PHY/PLL窗口，
所有权归MDSS；实际clock/GDSC获取、MMCX/NoC、PHY/PLL/host与面板完整初始化仍缺。
见[资源层实现、释放规则与验证](PANEL-POWER-D1.md)。D1未整体完成，继续合并下一驱动段，暂不要求push/刷机。

**2026-11-10优先级调整：用户要求完整面板驱动优先，随后零拷贝。** 不再以拷贝双缓冲、
60Hz CPU拷贝或完整F4性能收口作为PD2/PD3前置。共用DMA停止/完成与生命周期保护仍保留，
随资源、DSI/PHY/PLL和面板初始化实现一起推进；F4拷贝双缓冲改为兼容路径按需。
0121a12已被用户明确反馈滑动卡顿明显加重，只有接管/唤醒日志通过，不作体验验收。
具体D1–D4批次和合并计时项见[更新后的驱动交付顺序](PANEL-FULL-DRIVER-PLAN.md#7-加速后的交付批次与性能回归)。

**2026-11-10路线修订：大目标二增加完整面板驱动子计划PD0–PD5。**
用户要求完整接管，最低先脱离ABL缓冲，最终由驱动独立重建控制器/DSI/PHY/PLL与面板。
顺序改为已有PD0/0182 → PD2/PD3 → PD4 → PD1剩余项/PD5 → F5 → F7；详见
[完整面板驱动分步计划](PANEL-FULL-DRIVER-PLAN.md)。0181首段最新实测见下段。

**PD0推进：** [静态资源核对和接口设计](PANEL-RESOURCES-PD0.md)已完成；实际panel身份、
live链路/资源权限仍按E1–E8跟踪。0181已取得池和live快照；0182受控接管本地实现通过，0121a12接管/唤醒日志通过，待画面确认；PD1未验收。

**当前进度（2026-11-10）：F4首段0181已由0121a11实测，双池/seed/四快照和基本回归通过。**
用户确认画面/亮度/输入及手动唤醒无新增问题。CB2.M0，FSR原始0x400不命中LG已知故障位，
仍保留复核；新DMA可达/提交完成尚未由实机证明。0182新增独立`own2`受控单缓冲档，
经typed后端有界暂停/提交/观察后切CPU目标A；本地对象编译与ASan/UBSan通过，0121a12 single接管/唤醒日志通过，待画面反馈；安全回收与双缓冲仍缺。
30Hz原地更新仍在，提交后的池保留到重启；安全回收、双缓冲及SwapWait未完成。
见[接管设计与下一轮测试](FRAMEBUFFER-HANDOFF-F4.md)。
默认own0；own1申请双缓冲、准备ABL画面副本并获取有界快照，扫描仍ABL，无新扫描地址写入。
核对LG后纠正旧计划的`ADDR_SW_STATUS`语义：它是安全属性，不能用于翻页确认；先补完成事件
依据，再做接管/双缓冲/等待与性能收口。见[F4实施记录](FRAMEBUFFER-SCANOUT-F4.md)。
本轮不改CircleCI、不push；同核baseline/preflight档只差own参数，原驱动及F3行为保留。

**F3验收基线（2026-11-10）：0121a11完整硬件vsync档通过，F3正常使用路径验收通过。**
首真实帧及唤醒后FB进入hardware active，十个完整窗59.874–59.915Hz，OFF计数停止，
ON后三个完整窗稳定，无显式软件回退，用户未发现异常。见[F3第12节](FRAMEBUFFER-VSYNC-F3.md#12-0121a11完整硬件档f3正常使用路径验收通过)。
F4推进自有扫描缓冲、硬件裁剪和双缓冲翻页，写新地址前核对live SMMU/缓存/可达性。
默认参数/CPU拷贝30Hz尚未改变；旧bug及特殊生命周期覆盖边界保留，整机电源管理仍为大目标四。

以下为历史阶段记录，当前状态以上段及NOTES顶部为准。

**0180实现时进度（2026-11-09）：** 实际MDSS GIC115/RD_PTR IRQ及typed硬件vsync已实现、
通过本地arm64对象编译与ASan/UBSan验证，待云编译及实机验收。新行为默认关闭；完整档首次
真实帧后切硬件vsync、OFF停通知、失败明确回退软件。一次编译可分GIC/RD_PTR/完整档测试，
见[F3实现与测试](FRAMEBUFFER-VSYNC-F3.md#8-0180真实-irq-和硬件-vsync本地验证待实机)。
以下0179及更早描述保留对应阶段边界；不表示0180尚未实现，也不提前认定F3验收通过。

**2026-11-09 / F3实现补充：** 0121a10 / 0179完整只读预检实测通过，三次寄存器快照完整，用户确认无新增问题。下一步实际MDSS IRQ接管与vsync；尚未使能显示IRQ或替换软件timer，无需再测备用档，见[F3记录](FRAMEBUFFER-VSYNC-F3.md)。大目标四完整电源管理仍为末尾占位。

**0121a9当前实测：** F2三组实际DCS28/29全部成功，无电源降级/UNKNOWN；用户确认
息屏/唤醒正常、无新增显示问题。旧BUG-002电池group1隔离再次出现，BUG-004采样波动
也有记录，独立跟踪。隔离后的插拔唤醒无有效上游事件，不能据此判断面板。
同镜像长测已完成，显示/亮度/触摸正常；完整插拔唤醒次数未补齐，F3尚未开始实现。
BUG-002已由0178 / 41a0efa代码修复，按用户要求正常使用观察，不专项复测。
长测显示成功不代表低功耗验收：息屏段平均净放电约311mA，CPU/外设suspend仍缺，
详见[低功耗核对](POWER-MANAGEMENT-AUDIT-0121A9.md)；当前仅记录审计，暂不实施。
详见[F2实测](FRAMEBUFFER-POWER-F2.md)。用户决定继续显示路线，下一步F3硬件vsync，
先核对中断来源/映射与只读预检；F2未补齐次数作为覆盖边界保留，不追加旧bug专项测试。
完整电源管理列为日志之后的大目标四，仅占位，详细计划未来制定。下方本地完成/待产物状态为历史。

**F2当前实现（0177）：** 用户授权进入F2；面板DCS28/29、typed后端电源接口及状态机
已本地实现，strict apply/arm64单文件编译/17项host检查通过，待push云编译和实测。
新增sweetfbpower默认0，失败明确日志和UNKNOWN/保留像素降级，后续ON仍可恢复。
见[F2实施文档](FRAMEBUFFER-POWER-F2.md)。未做10/11或电源轨下电，未改CircleCI；
尚无0177新boot，F2未验收，独立BUG状态不变。

**0121a7当前进度：** F1b gated实测正常运行路径通过，用户未见新问题；新dispatch/gate及
后端绑定启用，亮度/4组off-on/三键/触摸/ACM有记录。结合F1a，F1分层重构正常使用范围
收口，下一步F2真显示电源；真实provider终止/重匹配未上机核验，默认开关仍关闭。
日志再次捕获BUG-004停充/启充链，独立跟踪，不视作充电完全无异常。
详见[F1b实测与边界](FRAMEBUFFER-SERVICE-F1B.md)。下方旧F1b待编译状态为历史。

**0176 / F1b本地实现：** AppleCLCD后端consumer、用户接口dispatch和状态gate已完成本地
编译/host验证，待push及新云产物实测。详见[F1b](FRAMEBUFFER-SERVICE-F1B.md)；默认关闭，
没有改变USB/电池/按键代码或CircleCI配置，F1整体仍未验收。

**0121a6最新进度：** F1a显示资源/背光服务迁移正常运行路径功能验收通过，亮度跟随及
3轮手动息屏唤醒正常；下一步F1b（AppleCLCD后端绑定、参数分发表/校验、状态gate）。
F1整体尚未完成；真实服务终止未上机验证，独立BUG状态不变。见[F1a实测](FRAMEBUFFER-SERVICE-F1A.md)。

**2026-11-09最新顺序调整：** 按用户要求，插电后锁屏缺时间归档为[BUG-003](BUGS.md)，
暂缓专项排查，进入[显示F1分层重构](FRAMEBUFFER-PANEL-PLAN.md)。F0有未闭环项，不记为
全部验收；此次进入F1由用户明确授权，覆盖下方历史“不跳到F1”的限制。新行为仍默认关闭。

**2026-11-08 / 0121 最新补充：显示 F0 两档实测已分析，尚未验收。** 普通档先隔离电池 USBIN
group1，后隔离电源键 group0，解释息屏后插线和按键均无法再唤醒；具体持续 pending 原因待查，
不能宣布 PMIC 事件长期稳定。保留像素档无该错误，但缺时间/滑条区域半透明效果异常仍需像素取证；
自动息屏未实现，移除 auto-lock 测试前置。详见 [0121 F0 报告](FRAMEBUFFER-F0-0121.md)
及 [显示分步计划](FRAMEBUFFER-PANEL-PLAN.md)。当前继续补 F0，不跳到 F1/F4。
USB 重连和 PMIC pending 统一由 [独立 Bug 台账](BUGS.md) 跟踪，修复另行安排，
不自动成为显示阶段新步骤或前置条件。
以下为历史阶段记录，以本段和 NOTES 顶部为当前状态。

**2026-11-08电池IRQ子目标：** 0168 runtime3（IRQ驱动、LG SMB5分工）在0120a17实机
充满，CHGR/USBIN/TYPEC/MSOC/WDOG路由均有自然命中，三键/触摸并行正常。剩余第5～6段
补强（回充、温度事件IRQ、停止留证、UI核对）完成后结案，再开始本文第2项framebuffer。
详见[电池实现计划](BATTERY-IMPLEMENTATION-PLAN.md)顶部。

**0120a16身份档实测：未通过，阻塞点为USBIN RESULT校验失败-12。**
只有3请求/2成功/5EOC，约53.744秒终止；失败这次GIC→notify61.979µs，ACK等锁
0.312µs/持锁12.188µs，callback距deadline仍98.522ms，属于及时交付后的校验错误。
快照status1/channel8均正常，但未保存原校验值和data lo/hi，不能断言具体是哪项。
实测ADC仅覆盖约1.255秒，不能用整档约700秒日志宣布旧超时/长期ADC已修复。
LG拒绝恰好0x8000，其他越界电压码按零处理；本实现拒绝所有raw>=0x8000，存在
具体语义差异，尚无被拒原始码证实因果。下一实现保留已读校验值/失败阶段并核对
零电压语义；本批不再重刷identity/watchdog/组合，无新驱动需push，继续禁充。
[完整时间线、已证实与未证实项](BATTERY-DEPENDENCY-TESTS.md#0120a16身份档实测及时交付后发生校验失败)。
以下旧待测/待编译动作均为历史，以本段为准。


**当前0120a16：真实云源码/内核核对通过，同核五档boot已打包并回读，待测identity1。**
实现357b26f/0167，相关33源码与本地验证树逐字节一致，原106源码gate、新仲裁/
ACK计时及snapshot v2标记通过；五档均runtime1、不主动启充，三键/USB/触摸IRQ
及USB日志保留。先用`out-bs/boot-16k-0120a16-battery-identity.img`：桌面后尽快
拔5秒→同一稳定电源5～10分钟→拔5秒，简测三键/触摸，导出
`sweetlog-0120a16-battery-identity.bin`，持久日志即可。目标核对持续USBIN转换与
ACK等锁/持锁/通知时序，是否消除旧超时尚待实机；之后按结果决定同核watchdog4/
组合7。无需新push/编译，不重测旧0120a15或baseline。
[镜像归档与验证边界](BATTERY-DEPENDENCY-TESTS.md#0120a16云产物与同内核测试镜像)。
以下旧待push/待编译/待测动作为历史记录，以本段为准。


**当前0167：ACK/遥测竞争修复已完成本地验证，待用户push/新云内核实测。**
等待中的mux IRQ写/ACK优先于下一笔普通SPMI事务；每遍先ACK、再合并通知，全部
有限遍次结束后输出日志；独立故障快照逐字段让锁，协议错误的恢复前快照仍保留
原子区间。新增ACK等锁/持锁计时，原期限/owner/poison/group隔离及runtime1禁充
不变。7文件ARM64单编译、并发/故障回归通过，CI未改，尚无实机修复结论。
下一步用户push，下载新产物后核对/打包同核五档，优先identity1；不再测旧0120a15。
[实现、锁顺序、计时边界和测试步骤](SPMI-ACK-ARBITRATION.md)。以下旧待测动作均按历史保留。


**0120a15-watchdog已分析：** 独立看门狗配置/9次自然bark维护/180秒有限停止通过
（含初始化10pet，结束后20bark不再pet），仍未主动启充。ADC59又RESULT超时，
这次mux在deadline前4.003ms已读到EOC pending，却到deadline后9.028ms才notify，
确认存在预截止事件交付过晚；ACK锁等待/抢占/总线耗时仍待拆分。下一实现处理
关键ACK/分发与重遥测竞争，当前不追加组合7或重复测试，无新驱动需push。
[完整实测与验收边界](BATTERY-DEPENDENCY-TESTS.md#0120a15看门狗档实测正常维护通过adc确认deadline前已有eoc)。
下方待watchdog等步骤按历史保留，以本段为当前动作。

**0120a15身份档已分析：** 5次身份转换正常；第6请求（首笔USBIN）RESULT超时。
恢复前读到转换完成/EOC锁存，第二mux scan晚于deadline78.134ms、notify晚109.106ms；
首EOC回调仅23.178µs、advance锁等待0.312µs，timer是真到期。已定位到存在分发
延迟，尚未证明硬件在deadline前完成，精确阻塞来源仍待核对。身份/限值撤销正确，
无有效VBUS或正许可。下一项同核watchdog4：桌面尽快拔5秒→接稳定电源≥60秒→拔5秒，
导出`sweetlog-0120a15-battery-watchdog.bin`。无新push/编译、不主动启充；不重测身份/
baseline/inhibit，暂不要求组合。[实测时间线与边界](BATTERY-DEPENDENCY-TESTS.md#0120a15身份档实测捕获result完成后的分发延迟)。
下方待identity等步骤按历史保留，以本段为当前动作。

**0120a15云产物已核对并打包，待实机：** 0166真实源码33文件与验证树一致，
原106源码检查、kernel manifest/新诊断marker和五档最终boot回读通过。先测
`boot-16k-0120a15-battery-identity.img`：进桌面尽快拔5秒→同一稳定电源约5分钟→
拔5秒，导出`sweetlog-0120a15-battery-identity.bin`，持久日志即可。之后按新证据
安排同核watchdog/组合档；当前不重测baseline/旧四档/独立inhibit，不主动启充。
[镜像、哈希与步骤](BATTERY-DEPENDENCY-TESTS.md#0120a15云产物与同内核测试镜像)。
下方待push/待云构建段落按历史保留，当前动作以本段为准。

**0166本地实现更新（待新云编译/实测）：** 已补ADC trigger→父GIC→mux扫描/ACK/
notify→consumer→SPMI锁的时间与一次性故障现场；timer先核对当前阶段的原截止
时间，未到时重挂，真超时仍撤销。watchdog补命令/软件plugin/故障取证，先禁充再
读现场，保留23秒/180秒，不额外pet。根因仍待新硬件证据，未宣布故障已修复。
本地回归和7文件ARM64编译通过；CI未改，可由用户push。下一次一次云编译、同核
分档，优先identity1；旧0120a13四档不重测。架构、字段、判读边界及步骤见
[IRQ时序取证](BATTERY-IRQ-TIMING.md)。以下均按历史时点保留，以本段为当前动作。

0120a4补充：0155 ADC5 provider实机已得到BAT_ID 889684uV、USBIN 4831136uV，
三次EOC及配置恢复成功，无ADC错误；下一步接入测量provider并核对充电证据。
充电仍互锁，不能把转换成功等同于真实充电。详细数据和回归边界见
[ADC5实机结果](BATTERY-ADC5-PROVIDER.md#0120a4-实机结果转换链路通过)。

0155进度：ADC5独立IOService/专用nub/固定转换/EOC/有界恢复已通过本地验证，
0120a4正常转换已通过，下一步接入测量与充电证据；0154预检在0120a3通过。
测试步骤见[ADC5 provider](BATTERY-ADC5-PROVIDER.md)。
0120a2遥测实机通过；实际ADC转换/输入能力/电池身份/并联及watchdog依赖
仍未闭环，不能进入首次受控充电或宣布电池IRQ阶段结束。详见
[硬件接口与下一轮](BATTERY-HARDWARE-INTERFACES.md)，后续大目标顺序不变。

**2026-11-06计划更新：** 已确认当前缺少主动充电控制，旧成功来自首次采样前
已经允许充电的状态，具体启动writer未定。按用户要求将电池实现划为六段，
详见 [`BATTERY-IMPLEMENTATION-PLAN.md`](BATTERY-IMPLEMENTATION-PLAN.md)。
先完成共享访问、真实测量与保护策略，再验证受控充电/CHGR事件及运行期恢复。
容量学习、profile重载、快充/无线等后续扩展不作为本轮IRQ验收前置。framebuffer
继续按原顺序在IRQ基本验收后推进。当前已开始第1段：0150受限SPMI接口、
与mux共享RW超时状态和可选只读预检已完成本地验证，尚待合批构建/实机。
接口边界见[BATTERY-SPMI-TRANSPORT.md](BATTERY-SPMI-TRANSPORT.md)，后续按
[BATTERY-TELEMETRY-CHECKLIST.md](BATTERY-TELEMETRY-CHECKLIST.md)补第2段；
当前没有实际充电使能。0151补了遥测、策略、控制执行器与运行层，
缺少电池/VBUS/并联证据provider，设备仍受互锁；状态与同核三档见
[BATTERY-RUNTIME.md](BATTERY-RUNTIME.md)，第4～6段实机验收尚未完成。

0120a2 补充：0153 初始DMA WRITE_ERROR清除成功，温度/容量/profile及短日志已
实测正常，USBIN/source-change连续健康。详见[FG DMA恢复](BATTERY-FG-DMA-RECOVERY.md)。
下一步补实际电池/输入/并联证据provider及watchdog/pet；未开放充电、未取得
CHGR正常充电转换证据，电池目标与第4～6段验收未完成，framebuffer顺序不变。

**当前进度：** 0119 mode4已实测三次独立MSOC中断，拔线期间电量由74%下降至
73/72/71%，与四次USBIN刷新区分，三键/触摸继续工作；之前mode3息屏插拔唤屏
已有证据。CHGR仍state7/charging0、没有独立命中，充电状态IRQ尚未验收。
按用户要求继续电池驱动：先用同0119内核的充电诊断镜像恢复物理充电/CHGR
证据链，再完善采样有效性、真实遥测及容量/profile，见
[`BATTERY-DRIVER-NEXT.md`](BATTERY-DRIVER-NEXT.md)。充电诊断主档九笔、no-usb
对照十一笔均state7/en0、无负IBATT，插线输入有效；禁充初态早于mux/USB初始化。
关闭当前启动的gadget未恢复充电。0149已修复逐字段诊断有效性，通过本地验证，
待用户push/云编译；保留SPMI白名单，不改充电使能或IRQ路径。
电池IRQ子目标仍未完成，framebuffer继续排在IRQ验收之后。证据与镜像见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。0148故障隔离仍只有本地
注入验证，未在手机触发；策略见 [`MUX-GROUP-ISOLATION.md`](MUX-GROUP-ISOLATION.md)。

**当前补充 0145：** 按用户要求，先完成 consumer 匹配通知、公开 C++ attach/
detach 和 terminated 清理，移除全局单例桥接。本地实现/验证完成；0118 原生
构建已核对，baseline 的新 registry 接入、三键/触摸/ACM 已有实机证据；
补充 wake live 已确认单独电源键息屏/唤醒及唤醒后输入，下一步测 no-keys/usbin，
电池 IRQ 子目标仍未验收。当前架构见
[`MUX-CONSUMERS.md`](MUX-CONSUMERS.md)，固定路由表与后续 framebuffer 顺序不变。

**状态：** 0114a11 实机验证按键三路与 DWC3 USB IRQ 并行；0114a13 实机验证
触摸 GPIO122 IRQ 与 USB 并行。0114a13 三路组合测试中音量上键的 PMIC
确认写入失败，mux 回退轮询，所以尚未完成固定 IRQ 验收。0138 增加有界
确认重试；0139 将 MH2LM 三路驱动固定为 IRQ、移除静默轮询回退，仍待
CircleCI 和实机验证。0140 已作为临时方案给当前 IRQ 驱动添加分级日志。
0114a14 构建日志确认 dxb 跳过了 0139 和 0140，该产物不得用于实机验证；
0139 已修正与 0138 的补丁冲突，CI 增加源码标记校验。0114a15 产物已确认包含
0139/0140，已打包普通与 DEBUG 两个临时镜像。0114a15 实机 ACM 日志中三键
PMIC mux 路由共 32 次命中、触摸 689 次、USB 1536 次，含息屏唤醒后的触摸和
并发活动，无驱动停止；固定 IRQ 的正常路径已通过本轮组合测试。0138 的异常
确认重试未被触发，仍未单独验证。日志文件名虽含 DEBUG，实际 boot-args 未含
`sweetloglevel=3`，分级日志的 DEBUG 档仍待核对。
随后 `live-e-0114a15-irq-only-debug1.txt` 明确带有 `sweetloglevel=3`，
记录 279 条 `[DEBUG]` 且 USB/触摸 IRQ 继续工作；正常 INFO 与 DEBUG
两档已分别验证。按键未在这份较短的 DEBUG 日志中操作，仍以前一份
组合测试为依据。
2026-11-05 补充：上述 0114a15 镜像漏掉真实电池启动参数，三路 IRQ
日志中的电池服务是静态占位。`d84ac7f` 已补回参数并用同一内核重打包
`0114a15-batt` 镜像。`live-0114a15-batt-irq-only.txt` 已确认真实电池
采样恢复：63 次 gauge 读数稳定 70%、4012–4013 mV，与 USB IRQ/ACM
及少量触摸事件并行；本次未操作按键、未覆盖插拔转换，所以完整组合回归
仍未覆盖。硬件报告 USB 存在但 state=7 未充电，未启用电流诊断。
2026-11-05 路线调整：按用户要求，在 IRQ 阶段追加子目标 4“电池 IRQ 化”，
在 framebuffer 之前实施。0141 已完成电池 IRQ 首版本地实现、交叉编译和模拟
故障检查；0115 云端产物的最终源码、符号与标记核对通过，六个临时镜像已生成。
0115 mode1 实机 live 日志显示真实采样与三键/USB/触摸正常，随后持久日志
补齐 REVID/FG 信息并明确 preflight=1。随后 mode2 因电池早于 mux 启动，
十秒等待耗尽而停用，电池路由未接入。0142 改为 mux 就绪事件交接，本地
编译与启动顺序模拟检查通过，待新云产物重测 USBIN；子目标仍未完成。
同一内核的分阶段测试入口见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。
2026-11-05 用户调整当前优先级：暂停电池验收，先清理启动归属。
0143 从 keys 移出 mux 启动、从 UFS 移出无存储依赖的外设启动，增加独立
BSD 晚期板级入口；keys/battery 各自 attach 路由。保留 0142 就绪交接，
UFS 只保留存储及分区持久日志。此项本地实现，仍待新 CI 与实机回归。
电池检查点、前后调用链、故障边界及同内核三档回归见
[`DRIVER-STARTUP.md`](DRIVER-STARTUP.md)。
随后按用户要求追加 0144：mux IOService 化，晚期 nub + personality 正常匹配，
实例拥有资源、consumer 与 stop/free；保留原 GIC callback/软件 IES。
filter 与 controller 的职责/选择、退出并发规则见
[`MUX-IOSERVICE.md`](MUX-IOSERVICE.md)。当前仍先验收启动及服务匹配，再继续电池。
Framebuffer 修复尚未启动。
0117 baseline 已确认新 board 入口先于根挂载、mux IOService 成功启动，
三键真实 IRQ/HID、触摸、USB 日志与电源键息屏唤醒回归通过。电池 IRQ=0，
子目标 4 仍未完成；下一步同内核 no-keys 独立 USBIN，再测 keys+USBIN。
**更新日期：** 2026-11-05
**适用范围：** `ref/ios-mh2lm` 的 MH2LM XNU 驱动；本文是实施和验收计划，
不替代 `NOTES.md` 中已经取得的硬件观察、日志结论和回归基线。

0114a15 当前 USB、按键、触摸、framebuffer、IOSurface 和背光的实际数据流、
与 XNU/IOKit 常规方式的差距，见
[`CURRENT-DRIVER-ARCHITECTURE.md`](CURRENT-DRIVER-ARCHITECTURE.md)。

## 总体顺序

1. 先完成一部分既有驱动的 IRQ 化：按键、DWC3 USB、触摸，再追加电池 IRQ 化。
2. 显示/面板详细计划已制定并成为当前主线；IRQ/电池的剩余验收独立保留，不将历史未收口项自动重新设为显示前置。
3. 将日志体系优化列为后续大目标；当前 0140 分级日志先用于 IRQ 验收。
4. 最后推进完整电源管理与低功耗适配；当前仅登记目标，详细计划未来制定。

### 0139 固定 IRQ 验收阶段

用户决定以功能显式停止和持久日志暴露 IRQ 故障。MH2LM 运行时按键、DWC3
事件、TouchComm 输入不再使用定时恢复读取，也不接受通过启动参数切回轮询。
按键 mux 失败时释放仍按下的键；触摸失败时释放触点；USB 事件故障时屏蔽
IRQ 并停止 gadget 工作线程和实时 ACM 日志。USB 日志发送线程由日志新增、
配置和发送完成事件唤醒。启动时有界等待、GIC 软件 pending 探针和诊断
计数仍保留；电池、UFS、framebuffer 的既有机制不在 0139 范围内。

0139 的实现与验收仅针对 MH2LM；共享按键和 USB 源文件已去掉旧轮询分支，
GUACAMOLE 与 SWEET 不在本轮支持和验证范围内。
这一步的源码和本地编译检查不等于实机通过。下一轮从 CircleCI 产物用
`tools/mk-mh2lm-event-matrix.sh ... --irq-only` 生成普通临时镜像；
包含 0140 的产物还会生成同内核的 DEBUG 日志镜像，
用 `fastboot boot` 验证三键、触摸、USB 枚举与日志并发、息屏/唤醒及重复
输入。特别检查 0114a13 音量上键确认失败是否被 0138 重试恢复；若三次
重试仍失败，应看到明确失败日志和按键失效。USB 故障时实时日志可能断开，
需提取新的持久 SWEETLOG，并以本轮 boot args 和日志计数确认新鲜度。

每一轮仍应沿用一个已验证的内核产物生成多个临时 `fastboot boot` 测试镜像的
方式。不要为了单一测试组合重复触发云端编译；不要在本路线中持久刷写启动分区。

按键的 GIC、DTS、PMIC 外设和 PMIC-arb 事实、未知项及预检步骤见
[`KEY-IRQ-HARDWARE-BASELINE.md`](KEY-IRQ-HARDWARE-BASELINE.md)。该基线是
`SweetSpmiInterruptMux` 的硬件输入，不以其他设备的 PDC/PMIC 常量替代 MH2LM 实测。

## 大目标一：既有驱动的 IRQ 化

### 架构约束

按键 0114a 系列的失败原因与可复用检查顺序见
[`IRQ-BRINGUP-LESSONS.md`](IRQ-BRINGUP-LESSONS.md)；USB、触摸适配前先核对
其中的产物来源、日志新鲜度、GIC 投递和真实设备事件四项。

本项目采用下列 XNU/IOKit 路径：

```text
PMIC 外设 / 控制器
  -> PMIC-arb / PDC 或控制器 GIC SPI
  -> XNU GIC callback
  -> SweetSpmiInterruptMux（仅共享 PMIC 源）
  -> consumer 的软件事件入口 / IOWorkLoop worker
  -> 既有的 HID、USB 或 TouchComm 工作路径
```

- `SweetSpmiInterruptMux` 是板级 PMIC 源解复用器，不是 Linux
  `irqdomain`，也不取代 XNU 的 GIC 或 IOKit 中断管理。
- 共享 PMIC SPI 的物理注册只能有一个拥有者，因此不能让按键、充电/电池等
  consumer 分别直接绑定同一 SPI。UniXNU 的 `armcpu_spi_attach(...)` 是
  参考实现；0114a7 实际内核使用内建 `SweetCPU`，不含这个符号，须先为当前
  XNU 的 GIC IRQ 分发建立唯一的 SPI 513 注册与回退机制。
- mux 的上游应是很短的 GIC callback：屏蔽对应 GIC SPI 并向软件
  `IOInterruptEventSource` 发信号。PMIC pending 扫描、latch 确认和重新使能
  由 `IOWorkLoop` worker 完成；不得在 GIC callback 执行 SPMI observer 读取、
  HID、DSI 或 UI 工作。
- mux 内部不得写死面向具体功能的 `switch`。采用静态路由表：每一项描述
  SID、PPID、PMIC bit mask、初始化后解析出的 APID、触发/极性/latch/确认策略、
  诊断及风暴保护参数、以及 consumer 回调和 context。第一阶段在启用 IRQ 前
  完成注册，运行期路由表保持不可变。
- 将来若 PMIC 子功能增加，路由表可抽象为注册接口或由 PMIC provider 注册子
  consumer；GIC callback 与上层 consumer handler 的接口不应因此改变。
- PMIC SPI 481 的共享对象是按键和 PMIC 充电/电池事件；它不是 DWC3 数据 IRQ。
  DWC3 使用控制器自己的 GIC IRQ，触摸使用其 TLMM GPIO/TouchComm IRQ 路径。

### 子目标 1：完成并验证 `SweetSpmiInterruptMux`

**目的：** 建立共享 PMIC IRQ 的可扩展分发和下半部交接机制，并以按键作为第一
个真实 consumer 验证。

**历史检查点（0114a7，只读路由预检完成）：** PON 的重复 PPID 映射包含
EE1/EE4 的 APID 1/19 和 EE0 的 APID 248。旧探针读取外部 EE channel 时
回 LG BSP；0132 跳过外部 EE 后，PON、GPIO6 的路由和外设配置单项/组合
预检全部进入 iOS。两条 EE0 channel 的 `ACC_ENABLE`、`IRQ_STATUS` 均为 0，
外设 `EN_SET` 也为 0。下一阶段是核对上游 PDC/GIC、latch/clear 和使能
顺序，再实现可回退的单 source Power IRQ 试验；当前 20 ms 轮询仍在工作。
详见硬件基线文档和 `NOTES.md` 的 0114a7 实测记录。

SM8150 PDC 源码已确认 pin 1 对应 PMIC EE0 IRQ / GIC INTID 513；Linux
PMIC-arb v5 确认 channel clear 在 `+0x108`。0114a8 实机读回 PDC pin 1
`enable-bank=0 config=0x4`，已知该 pin 当前关闭、配置为高电平有效。
GIC SPI 513 的现有空闲值无需再单独测试；当前 XNU 没有为它做专门配置，
新驱动会明确设置并读回 Group 1、level、priority、route 与 enable。
首轮可写试验应先具备当前内核的 SPI callback、mux 路由表、中断风暴保护
及轮询回退，然后才按单 Power source 开 gate。0133 的 GIC/组合只读镜像
保留作诊断备选，不作为进入实现的前置测试。

0134 首版已包含上述可按启动参数选择的 gate：`sweetkeysirq=1` 只验证
IOInterruptEventSource/IOWorkLoop 软件交接，`=2` 仅启用 Power IRQ，`=3`
启用三键 IRQ。默认参数不改硬件。其构建产物需依次临时启动 baseline、
软件交接、Power、三键镜像，每步提取独立日志；只有前一步正常才进入下一步。
0134 的语法与补丁应用已在本地验证；实机 IRQ 结果仍未知。
0114a9 已提供含 mux 符号的完整内核产物，四个测试镜像已从同一内核生成；
实机尚未报告 IRQ 命中，镜像路径与 SHA256 见 `NOTES.md`。

**实测更新：** 0114a9 Power 测试因非安全侧 GIC Group 位读回 0 而回退；
0135 先软件 pending SPI 513 并要求真实投递，再开 PMIC/PDC gate。
0114a10 的 Power 与三键模式均通过此探针，三条路由均有硬件 IRQ 命中，
按键状态配对正常，USB 实时日志同时工作。具体次数、日志 SHA256、
200 ms 恢复轮询的证据边界见 `NOTES.md`。

**实施边界：**

- 实现 `SweetSpmiInterruptMux`、静态路由表、pending 状态扫描、确认顺序、
  计数器和可诊断的异常/风暴防护。
- 在启用任何 PMIC IRQ gate 前，完成 MH2LM DTB/DSDT、PMIC-arb v5、APID、PDC
  与确认寄存器的只读核对；不得复用 UniXNU 其他设备的 PPID、PDC pin 或寄存器
  常量。
- 先仅将按键接入 mux：GIC 上半部调度 mux 的软件事件，mux worker 唤醒既有按键
  worker；按键 worker 继续读取电平并
  经既有 `sweet_hid_key()` 发布 HID 事件。IRQ 成功 arm 后，替换当前 20 ms
  轮询等待；保留安全的回退/诊断模式，直到硬件稳定。
- 暂不把电池/充电 PMIC gate 作为本子目标的硬件 consumer。它们将来可以复用 mux，
  但必须与各自的 latch 确认和 `IOWorkLoop` handoff 一同单独启用。

**验收条件：**

- `SweetSpmiInterruptMux` 的路由、ack、下半部调度和诊断在真实设备上运行，且不
  再依赖硬编码功能分支。
- Power、Volume Up、Volume Down 按键通过 IRQ 成功产生正确的按下/释放 HID 事件。
- 重复按键、组合键、display-off/唤醒及已建立的 USB/电池回归不出现中断风暴、
  漏释放、重复事件、GIC 注册冲突、kernel trap 或 panic。
- 日志保存 IRQ 路由命中、ack、worker 消费及异常计数，足以判定一次按键从物理源
  到 HID 的完整链路。

### 子目标 2：将 DWC3 USB 改为 IRQ 驱动

**0114a11 结果（2026-11-04）：** 已验证 INTID 165 软件 pending 与真实
DWC3 IRQ 投递、event buffer 处理、CDC-ACM 实时输出，以及与三键 IRQ
并行。USB IRQ 计数至少到 512，无空中断、漏 IRQ 回退或 mux 回退；
三键均有真实 mux 命中和 HID 按下/松开。一次音量上松开时 PON 读取失败，
GPIO 与 HID 松开正常，详见 `NOTES.md`。用户确认多次拔插后偶发的
Windows 未识别设备在旧轮询驱动中已存在，列为独立 gadget 重连缺陷。
当前转入子目标 3。

**目的：** 把当前已验证的 DWC3 CDC-ACM 轮询事件处理改为控制器 IRQ 驱动，同时
保持既有 USB 设备模式和日志能力。

**实施前的源码基线：** MH2LM 使用的 `sm8150-usb.dtsi` 中，主 USB
`dwc3@a600000` 子节点列出 GIC SPI 133（INTID 165）；USB wrapper 另有
`pwr_event_irq` 等中断，不能把它们当作 DWC3 事件缓冲区 IRQ。当前
`sweet_usb.c` 在 `GEVNTSIZ` bit 31 屏蔽事件中断，并由 `ud_thread` 每
1 ms 读取 `GEVNTCOUNT`。以上是当时的源码映射，实机投递结果见本节开头。
先核对目标 DTS 与 GIC 路由，再在现有 CDC-ACM 驱动上加入可由启动参数选择的
软件投递检查、DWC3 IRQ 上半部和既有事件处理 worker 的唤醒/回退。
同一内核应能选择当前轮询路径、USB IRQ 路径，以及同时开启
`sweetkeysirq=3` 的并发路径；USB 日志继续作为正常观测通道，早期或 USB
中断故障仍从持久 SWEETLOG 取证。
已知值、必须实测的 SPI 165 投递/确认步骤及无需重复测试的项目见
[`USB-IRQ-HARDWARE-BASELINE.md`](USB-IRQ-HARDWARE-BASELINE.md)。

**实施边界：**

- DWC3 不进入 `SweetSpmiInterruptMux`。它使用独立的 DWC3 GIC IRQ 与自己的短
  上半部/worker。
- 上半部仅标记并调度；worker 负责 drain event buffer、DMA cache 操作、
  `GEVNTCOUNT` 确认和 rearm 顺序。不得在 IRQ 上半部执行完整 gadget 状态机。
- 保留可选择的诊断/回退路径，直到 IRQ 模式在 CDC-ACM、DTR、TX/RX、拔插、bus
  reset 下稳定。测试镜像可通过 boot arguments 选择单项或组合测试。

**验收条件：**

- USB IRQ 模式下 CDC-ACM 可枚举、host DTR 可用、日志持续输出，TX/RX、重连和
  bus reset 无丢失或卡死。
- DWC3 IRQ 与按键 IRQ 可以并行工作，不共享/争用错误的物理 SPI，也没有 GIC
  注册、锁、DMA 或事件确认顺序冲突。
- 在 USB、按键以及现有电池轮询共同运行的回归中，无 kernel trap、panic、明显
  的事件风暴或功能退化。

### 子目标 3：将触摸改为 IRQ 驱动

**0137 本地进度：** 已定位正在使用的 TouchComm 驱动、GPIO122 和 TLMM 汇总
SPI 208（候选 INTID 240），并写好带 GIC 探针、真实 IRQ 与轮询回退的补丁。
本地编译与补丁应用通过，仍待 CircleCI 和手机验证。寄存器与测试门槛见
[`TOUCH-IRQ-HARDWARE-BASELINE.md`](TOUCH-IRQ-HARDWARE-BASELINE.md)。

**目的：** 用触摸的 TLMM GPIO/TouchComm 中断通知替代当前 10 ms ATTN 采样和周期性
强制传输的功能性回退。

**实施边界：**

- 触摸不进入 `SweetSpmiInterruptMux`；沿用其 TLMM GPIO/TouchComm 路径，并以短
  上半部唤醒/调度既有触摸读取与 HID 发布 worker。
- 逐步替换轮询，只在必要的恢复、去抖或硬件异常处理中保留受控回退，不把轮询
  当作常态数据通路。
- 验证单点、多点、拖动、锁屏、屏幕开关、长时间空闲后的首次触摸，以及与按键和
  USB 并发时的行为。

**验收条件：**

- 触摸 IRQ 模式在真实设备稳定工作，触摸报告和 UI 交互正常。
- 无持续轮询造成的固定空闲读取负担；没有漏触、重复触、卡死、I2C 错误回归或
  中断风暴。
- 与已完成的按键 IRQ、DWC3 USB IRQ 以及既有电池路径并行时无冲突。

### 子目标 4（临时追加）：电池 IRQ 化

**当前状态：** 0119已验证0145～0148的新启动与consumer架构，mode2/3的USBIN
和三键并行通过；mode4又实测三次独立MSOC命中与73/72/71%刷新，电量事件
正常路径已通过。CHGR/SOC_READY变化事件尚未覆盖，CHGR state7/charging0
不是实际充电证据。0119充电诊断主档/no-usb已完成；下一步按六段电池实现
计划补主动控制，之后验证CHGR，不重跑已跳过的no-keys。
完整历史、镜像和观测判据见
[`BATTERY-IRQ-TESTING.md`](BATTERY-IRQ-TESTING.md)。
PMIC 型号证据、事件位置、既有 APID 预检和待测项见
[`BATTERY-IRQ-HARDWARE-BASELINE.md`](BATTERY-IRQ-HARDWARE-BASELINE.md)。
工作区 `docs/INDEX.md` 的电源六章报告已补充 ack/unmask 顺序、LG 0.3%
MSOC delta 配置及 SRAM 事务依据；这些是源码参考，实际门限仍未知，但FG
channel接入与MSOC自然事件已有0119实测，无需为测门限访问SRAM。沿用固件现有
FG 状态验证事件，不为 IRQ 化重载 profile；
需要 SRAM 访问时单独设计授权、版本 workaround 与释放路径。

**目的：** 让插拔电源、充电状态和电量变化经 PMIC IRQ 触发状态刷新，继续通过
`IOPMPowerSource::updateStatus()` 通知 iOS，降低两秒定时采样带来的状态延迟。

**实施边界与顺序：**

1. 复用现有 `SweetSpmiInterruptMux` 与唯一 INTID 513 callback，以路由表新增
   battery consumer。按键 IRQ 保持启用；不单独注册共享 SPI，不写死功能 `switch`。
   明确 consumer 就绪、启动初始采样、路由 arm 及 stop/free 的顺序，防止回调
   访问未启动或已释放的电池实例。
2. 先接 USBIN_PLUGIN（SID2/peripheral 0x13/bit4）和 CHG_STATE_CHANGE
   （SID2/0x10/bit1），再核对并接入 FG MSOC_DELTA（SID2/0x40/bit3）；
   视需要接 SOC_READY。每个新外设先检查 EE0 ownership、APID、触发配置、
   latch/clear 和使能顺序，不重复读取已知危险的外部 EE channel。
3. GIC 上半部保持短小；mux workloop 完成源识别和确认，仅把事件交给电池
   consumer。电池在自己的 IOWorkLoop 上合并事件、读取硬件并发布，避免在
   mux worker 中进行整套电池诊断、长时间读取或 UI 操作。
4. IRQ 模式验收时停止现有两秒状态扫描。电压/电流等连续遥测若需低频定时
   采样，必须明确用途、周期和触发来源；不得用它静默补偿插拔、充电状态或
   MSOC IRQ 丢失。启动采样和有界事件后复查单独记录；IRQ 故障显式记录、
   屏蔽故障源，并说明是否影响共享按键路径，不能伪装为 IRQ 正常运行。
5. 本子目标只增加事件驱动和必要 IRQ 配置，不接管充电使能、限流、温控、
   电量计学习或完整 Apple manager ABI。IRQ 生效与真实充电分开取证。
   同一内核支持单源、组合及诊断参数测试镜像，减少云端编译次数；USB ACM
   继续作为正常日志通道，拔线或链路故障期间用持久 SWEETLOG 补充取证。

**验收条件：**

- 真实插入/拔出电源可见对应 route 命中、battery worker 处理、硬件状态读取
  和 IOPM 发布；充电状态变化也有独立证据，不能只看到 DWC3 IRQ 或图标变化。
- 电量确实跨过硬件配置的 MSOC delta 阈值时，由 FG IRQ 触发真实 gauge
  更新；记录阈值和事件行为，不用电压曲线或定时扫描冒充。短时间电量不变
  只能说明未覆盖此项，不能作为通过证据。
- 空闲期间没有两秒 PMIC 全量扫描；若保留低频遥测，日志能区分其来源。
- 与三键、USB ACM、触摸并发，含息屏/亮屏、重复插拔、启动时已插线与未插线，
  无确认失败、IRQ 风暴、键漏释放、死锁或功能停止。息屏测试指当前 display-off，
  不把它宣称为整机深度休眠唤醒已实现；既有 framebuffer 局部黑屏单独记录。
- 保存路由/worker/合并/错误计数、事件至发布耗时、boot args、产物 SHA256
  和原始日志；验收确认关键状态更新由硬件事件触发。

### 大目标一完成判定（追加电池后）

只有四个子目标均满足各自验收条件，且完成一次组合回归后，才视为此大目标完成。
组合回归至少覆盖：正常桌面启动、按键/组合键、触摸交互、USB CDC-ACM 日志、
插拔 USB 后的恢复、电池插电/充电状态与 MSOC 事件、display-off/唤醒，
以及持久日志中的 IRQ 计数和错误检查。
每个结果要记录 kernel/image SHA256、boot arguments、线缆状态、测试时间与原始
日志文件名到 `NOTES.md`。

## 大目标二：修复 framebuffer 并完成面板驱动

**目的：** 解决已记录的局部重绘/锁屏唤醒渲染问题，特别是 display-off 后插拔电源
会唤醒、状态栏和 home indicator 可见但中间时钟/锁屏区域保持黑色的现象。
2026-11-10扩充为完整显示/面板驱动：自有扫描内存、独立硬件初始化、供电/复位与断电
恢复均为必要目标；不以ABL交接状态作为最终运行前提。BUG-003仍单列，不提前声称修复。

**2026-11-08 详细计划已定：[FRAMEBUFFER-PANEL-PLAN.md](FRAMEBUFFER-PANEL-PLAN.md)。**
agent 实施时以该文档为准，按步骤顺序推进，每步通过验收后在该文档“进度记录”和
`NOTES.md` 记录，再开始下一步：

| 步骤 | 内容 | 修复 |
|---|---|---|
| F0 | 取证不改行为：选择子/surface 创建属性/off-on 校验和、preserveoff A/B、SSPP/TE/SMMU 状态 | 定位黑块根因，确认零拷贝前提 |
| F1 | 分层为 `SweetMDSS : IOService`（ADT reg/中断、mapDeviceMemory）+ `AppleCLCD` workloop/命令门/分发表 | 符合 IOKit 常规 |
| F2 | 真显示电源：DCS 28/29（后续 10/11），不再擦像素 | 息屏唤醒局部黑屏 |
| F3（0121a11正常使用验收通过） | MDSS INTF1 RD_PTR 中断做 vsync，定时器降为看门狗 | 假 vsync |
| F4（a31自有A接管及恢复通过，整体未完成） | 自有扫描池/裁剪及A接管已有证据；单缓冲30Hz CPU拷贝保留，拷贝双缓冲按需延期；提交/等待/复用仍需完善 | 正常扫描离开ABL缓冲；不等于零拷贝或性能整体验收 |
| PD0–PD5（当前位于PD2/PD3正常路径通过后） | PD0资料/接口已交付；a31链路及面板供电/reset/ON/OFF正常路径通过；剩PD2管线配置、PD4独立重建、PD1首帧/console/panic及PD5收口 | 完整接管面板，不依赖ABL配置 |
| F5 | CA surface 由扫描池分配、MDP 直接扫描（cache clean），不兼容时回退拷贝 | 每帧 CPU 拷贝 |
| F6 | （可选）关 autorefresh，按帧 kickoff | 功耗/残余撕裂 |
| F7 | 复核PD生命周期、关机/重启、使用自有缓冲的panic显示，切默认并删除旧路径 | 收尾 |

PD0已交付，利用0182证据先推进PD2/PD3；PD1余项和PD5按依赖补齐，再进入F5；各段结束条件以
[PD计划](PANEL-FULL-DRIVER-PLAN.md)为准。完整驱动不能仅凭缓冲分配、28/29或vsync成功验收。

新行为全部 opt-in（`sweetfbtrace/sweetmdss/sweetfbpower/sweetfbvsync/sweetfbown/
sweetfbdirect/sweetfbkickoff`），验收通过后再切默认。不要把 UniXNU、Hakios 或其他项目的整个
framebuffer 栈直接移植进来，也不要将电池事件或充电 UI 伪装成 framebuffer 问题的修复。

## 大目标三：优化日志结构与管理

**目的：** 建立统一的 Sweet 驱动日志结构、等级和管理方式。最终可以选择开启
哪些驱动或事件类别的日志（日志目标），并按等级过滤；在正常运行时控制输出量，
需要定位问题时再打开指定目标的详细记录。日志管理不能掩盖驱动故障。

**已有临时基础（0140起，已随当前镜像使用）：** 按键、DWC3 USB、
TouchComm、`SweetSpmiInterruptMux` 已改用 ERROR/WARN/INFO/DEBUG 前缀，
`sweetloglevel=0..3` 控制这四个驱动的全局输出上限，默认 INFO。高频细节在
DEBUG；节流后的硬件 IRQ 命中仍为 INFO。普通镜像和 DEBUG 镜像从同一内核
产物生成，持久 SWEETLOG 与 USB ACM 的字节流格式未改。
`sweetlogdump=1` 已识别新的 ERROR/WARN 前缀。具体范围和限制见
[`LOGGING-LEVELS.md`](LOGGING-LEVELS.md)。这只是过渡方案：其他 Sweet 驱动、
XNU/IOKit 与用户态输出尚未统一，当前也不能按驱动选择开关。

**后续设计方向（进入本目标时再细化子目标）：**

- 清点 Sweet 驱动及日志路径，为等级、目标/模块名、事件时间和必要上下文
  规定统一字段与调用接口；明确启动早期、IRQ 上半部及故障路径可用的接口。
- 为各日志目标配置独立的启用状态和等级阈值，并提供明确的正常、诊断等
  管理模式；配置方式须适合当前的 boot argument/临时镜像调试流程。
- 统一持久 SWEETLOG、USB 实时输出与控制台的过滤/保留规则。USB 本身异常时，
  ERROR 仍须能由持久日志取证；过滤策略不得静默丢弃所需的故障记录。
- 逐步迁移现有驱动，避免高频路径无条件格式化或输出；每次迁移核对日志
  量、启动/功能回归和提取工具兼容性。

**完成判定：** 可从一份内核产物通过配置分别开启指定目标及等级，默认模式
保留必要状态与故障而不过量输出；诊断模式能恢复目标的细节。各输出通道
对同一记录的等级和目标含义一致，故障场景可通过持久日志可靠追踪，
相关驱动和日志提取工具通过实机回归。

## 大目标四：完整电源管理与低功耗适配（待规划）

**目标：** 补齐整机休眠、设备低功耗与唤醒恢复，降低息屏/空闲耗电。

用户2026-11-09决定将本目标放在现有大计划最后，位于日志体系优化之后。
目前仅登记目标，子目标、实施顺序及验收条件均留待未来制定；当前继续显示/面板F4及PD子计划。
现状依据保留于[0121a9低功耗审计](POWER-MANAGEMENT-AUDIT-0121A9.md)，不将审计建议当作已定实施计划。
显示阶段自身既有的电源/生命周期工作仍按显示计划进行。

## 文档维护规则

- 每次阶段性实现、CI 结果、设备测试和回归结论都追加到 `NOTES.md`，明确区分观察
  与推测，并保留日志与 SHA256。
- 本文件只维护路线、边界和验收条件；已验证的硬件事实以 `NOTES.md` 为准。
- 不推送代码或文档；需要同步远端时明确告知由用户执行 push。
