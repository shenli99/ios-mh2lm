# 完整面板驱动子计划：从 ABL 交接到 XNU 独立初始化

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


## 0223之前阶段对照（2026-11-14，a32唤醒失败审计）

**0222固定单管线重建本地通过，a32首次接管实测通过、息屏后无法唤醒；PD4仍进行中。**
mode7首次接管及每次唤醒按Lineage执行CTL0 reset、VIG0/LM0/CTL/PP/INTF重配与新帧提交，保留a31/mode2基线。
见[实现与边界](PANEL-PIPELINE-REBUILD.md)。[a32日志/回归审计](PANEL-D1-0121A32.md)显示OFF完成及ON供电/ULPS/host/DDIC reset完成，最后为首次DMA分配入口；未见ON表受理或generation2。
未发现旧Lineage流程回退，分配阻塞尚未确认；下一步聚焦命令缓冲准备和采集连续性，无新代码待编译。
上层调用链补充确认：0222的每次无条件CTL reset及kickoff后才连接TE，与Lineage正常command-mode流程不同；旧tearcheck关闭/命令引擎生命周期也有差异。详见[a32上层流程审计](PANEL-D1-0121A32.md)，后续需同时审查这些策略，尚未证明黑屏因果。
基线为a31/0221（实现5cf36dd、实测记录56e7b5c），[本次证据](PANEL-D1-0121A31.md)已包含用户画面/输入/亮度反馈。

| 阶段 | 已取得的结果 | 仍未完成 |
|---|---|---|
| PD0 资料与接口 | 静态资源/分层/锁顺序设计已交付，实际面板profile已用于ON/OFF | 共享供电物理状态等证据继续按具体写入范围核实，不是全部未知项清零 |
| PD1 摆脱ABL缓冲 | 正常扫描及本次OFF/ON均使用自有A缓冲 | 不读ABL像素的首帧、default surface、console/panic和相应生命周期 |
| PD2 控制器与链路 | 链路/ULPS已有实测；0222补固定VIG0/LM0/CTL0/PP0/INTF1的软件配置，本地通过 | 新配置实机；SMMU/core/VBIF/QoS/DSPP/部分PP保留状态的独立初始化与恢复 |
| PD3 面板生命周期 | 完整ON/OFF表、reset/pinctrl、panel票周期、长短命令、亮度/触摸和FB配合；a31一次普通OFF/ON通过 | 多轮/长时及故障释放边界未全实机覆盖，共享核心资源仍保留，不将撤票当物理断电证明 |
| PD4 独立重建 | 0222每次CTL0 reset后重配；四profile各三轮清空本轮配置后恢复、FB新代数/新帧配合本地通过 | mode7实机及其余控制器/域状态失效后重建；首帧仍读取ABL像素，不能算完整PD4通过 |
| PD5 IOPM/故障收口 | 已有事务取消、超时和部分资源保护 | 完整IOPM、provider终止/回滚、晚IRQ/完成与缓冲释放、故障显示的统一验收 |

执行顺序仍为PD2/PD3必要基础 → PD4 → PD1剩余项/PD5 → F5直扫 → F7。
按D1–D4批次看，资源/命令与面板正常路径已走通，下一主段是D3独立重建与ABL退出；D4直扫尚未实现。
F4拷贝双缓冲/60Hz CPU拷贝继续按需延期，不要求为此先重复长测。整机低功耗仍在总路线大目标四。
以下历史版本的“待云编译/无新DMA/先identity”不再是当前动作；现在a32已测且唤醒失败，按顶部审计定位，尚不能验收mode7唤醒；不重复旧a31首次验证。

## 历史执行记录

**历史执行点（2026-11-13）：a27已实测越过branch7，0215完成取证已提交2ffc1a4，待用户push云编译。**
CLOCK token2/state2/mask381已Held；NoC首组ACTIVE2处phase3/executing1超时，ACK0、无新DMA。
Lineage ACTIVE IRQ预先启用，XNU屏蔽本槽后轮询，存在快速完成无fresh证据的候选路径；实机原因未证实。
0215仅补NoC缓存/两处有界四读，原完成/期限/保票保持；overall/访问丢失/stop不新增MMIO。
22场景与a27仅差38组四读，其余轨迹/结果/资源账本/timer/模拟时间相同；16套既有回归、ARM64对象、31补丁重放通过。
新产物建议0121a28，预计MDSS.cpp/NoC.inc两文件变化、191 binary markers；沿原ABI608 retained取新首错。
详见[a27实测](PANEL-D1-0121A27.md#实测branch7-已越过首个-noc-tcs2-请求超时)及[0215实现](PANEL-NOC-COMPLETION-EVIDENCE.md)。
a27 USB足够，无需补持久日志/重测；完整接管前其他显示测试暂停。以下a27待实机为历史。

**当前执行点（2026-11-13）：0121a27包含0214已核对，retained启动镜像回读通过，待实机日志。**
78源码/114 source/186 binary markers通过；五份变化与55cab80一致，ABI608固件/ADT/605字节参数保持a26。
本轮先验证voted halt complete→CLOCK Held→NoC→新DMA/HANDOFF；尚未证明实机接管成功。
命令与校验见[0121a27交付](PANEL-D1-0121A27.md)，无需再次云编译；其他显示测试暂停，以下待云编译为历史。

**当前执行点（2026-11-13）：a26已分析，0214完成语义修正已提交55cab80，待用户push云编译。**
BCR三阶段均0，局部CBCR前后不变，AHB CFG1→40f；仍enable0/OFF0，直接拒绝为额外enable门槛。
0214对自身已发出的voted请求按halt完成，同步Clock/NoC/Link/Lifecycle；保留父权限、继承票、普通HALT及撤票检查。
18场景专项+15套既有回归、4个基线复现、34父链故障、ARM64对象及30补丁重放通过；未证明实机接管成功。
下一份产物建议0121a27，预计五文件变化/186 binary markers，核对后打原ABI608 retained。
见[a26实测](PANEL-D1-0121A26.md#实测复位位清除仍由enable门槛超时)和[0214实现](PANEL-VOTED-HALT-COMPLETION.md)。
a26日志完整，无需重测/补持久日志；其他显示测试暂停。以下a26待测及保留enable门槛为历史。

**当前执行点（2026-11-13）：0121a26包含0213已核对，retained镜像已打包并回读通过，待用户临时启动取日志。**
78源码、114 source/185 binary markers通过；ABI608固件/ADT/605字节参数保持a25。
若仍超时，比较新增LOCAL warm-entry/pre-write与RESET的BCR/局部状态及时间，原六条诊断保留。
本版没有修改额外enable门槛，尚未证明根因或接管成功；a25无需补日志，其他显示测试暂停。
启动命令及证据见[0121a26交付](PANEL-D1-0121A26.md)。无需为本轮文档提交重编译，以下待云编译为历史。

**最新语义核对：a25 OFF=0已满足Lineage的开启判据，直接阻塞为XNU额外enable读回门槛。**
不能称硬件halt握手未返回；VOTED开启仍本地RMW并验halt，关闭延时10us，不是等待voted位或改走RPMh。
LG/Lineage实际leaf调用链24场景ASan/UBSan通过；父链及准确寄存器地址、enable门槛影响范围见
[父链/halt审计](PANEL-BRANCH7-PARENT-HALT-AUDIT.md)。本轮保留已就绪0213取证，无新生产补丁或额外云编译轮次。


**当前执行点（2026-11-13）：a25已分析；0213局部状态/复位取证已提交7f4f55a，待用户push云编译。**
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

**0208当前执行点（代码`190c558`，本地通过、未实机）：** D1首次资源获取的RPMh完成路径
已改为真实INTID37→软件event/workloop，保留fresh/期限和唯一writer。16套回归及ARM64对象通过。
现在需要用户push云编译；核对新产物后用带IRQ扩展的新ADT重打ABI608 retained，先取启动日志
确认ldoa14 ACK及后续首错。完整接管前不测OFFON/亮度/长测，combined/supply仍暂停。
详见[实现、证据和下一步](PANEL-RPMH-COMPLETION-IRQ.md)。以下“无代码改动/无编译要求”按历史保留。

**0121a20实测：** SCANOUT审计WARN由原始CB2 FSR0x400触发，随后仍接受接管并进入managed。
新快照确定ldoa14卡在completion：TCS0/idle1/三cmd状态10101，但IRQ_STATUS0、progress0，fresh完成条件未满足。
不能单凭CMD_STATUS认定本轮新ACK；下一步核对provider完成取证/ACTIVE IRQ路径，保留fresh保护。
详见[RPMh实测分析](PANEL-RPMH-FIRST-VOTE-TIMEOUT.md#0121a20实测审计警告与完成证据缺失2026-11-12)。本轮无代码改动，无新编译要求。
以下0121a20待测步骤为历史，OFFON/combined/supply仍暂停。


**0121a20已核对打包：** 78源码匹配0207（代码ae78f99）、114 source/164 binary markers与镜像回读通过。
只测新ABI608 retained档，605字节参数/ADT同0121a19；等约90秒导启动日志，取首次RPMh SNAPSHOT。
这是定位ldoa14超时阶段的诊断版，仍可能静帧；接管前不做OFFON/亮度/长测，identity不重测，combined/supply暂停。
见[镜像路径与日志目标](PANEL-D1-0121A20.md)。以下待push/待云编译步骤已完成，实机仍待验证。


**0121a19实测 / 0207下一步：** 0206首次触摸启动等待与quiesce已实机通过，已进入managed资源链；
第一笔ldoa14 RPMh事务以Timeout失败，确认完成rails=0、未提交新DMA，画面仍冻结。
未定位到编程/硬件完成/清理的具体阶段，不将超时当成PMIC拒绝或ACV票失败。
0207只为唯一apps-RSC provider增加终止故障快照；保留100ms期限、fresh完成、原IRQ mask与故障保留策略。
需要push云编译后新retained启动日志；旧镜像不重测，OFFON/combined/supply仍暂停。
详见[本轮结果](PANEL-D1-0121A19.md#实测结果2026-11-12)和[RPMh超时与诊断](PANEL-RPMH-FIRST-VOTE-TIMEOUT.md)。
以下“0121a19待实机 / 0206待push”等为历史，以本段和NOTES为准。


**0121a19已核对打包：** 0206首次接管等待触摸worker修复已包含；78源码、114 source/160 binary
markers及镜像回读通过。只启动新的ABI608 retained档，等约90秒导启动日志，先不做OFFON/长测。
参数605字节及ADT同上一轮，旧镜像保留；见[本轮步骤](PANEL-D1-0121A19.md)。
触摸与面板的接口/独立reset/IRQ及软件依赖见[关系说明](PANEL-TOUCH-STARTUP-WAIT.md#为什么面板接管会依赖触摸用户追问核对)。
以下0206待push/0121a18待测步骤为已完成历史，实机修复结果仍待验证。

**0121a18实测/0206增量：** ACV width0/mask8元数据已通过；首次生命周期准入仍NotReady，
触摸worker稍后才ready，原framebuffer一次性请求永久失败。0206仅协调器接收首次Takeover后
异步等待worker，真实quiesce证明前不改显示/投票；超时/退出仍失败。待push云编译，不复测旧镜像。
见[日志、复现及修复边界](PANEL-TOUCH-STARTUP-WAIT.md)。以下0121a18待测步骤已完成。

**0121a18已核对并打包：** 78源码与0205一致、114 source/155 binary markers及镜像回读通过。
待启动 `boot-16k-0121a18-abi608-panel-retained.img`，先取一分钟启动日志及画面，确认ACV准入
和managed扫描接管。ABI608固件、605字节参数及ADT保持；不重测旧诊断，combined/supply暂停。
详见[镜像、命令和日志目标](PANEL-D1-0121A18.md)。以下push/编译步骤已完成，实机尚待验收。

**0205当前修复（2026-11-12）：** 按用户决定将display ACV建模为 `0x8` 开关资源；
保留真实width0/unit1/VCD11，EBI有需求发送X0/Y8，无需求撤票。普通BCM缩放/非零width检查保留。
本地验证通过，待push云编译及新retained镜像实测；不是再次只加诊断，也不代表面板接管已验收。
本地DTS无mask属性；LG apps有mask8/X0/Y8，本次显式应用到display，不搬跨BCM累计副作用。
详见[实现、依据、验证与下一步](PANEL-ACV-ENABLE-MASK.md)。
以下此前“不能套apps mask/继续查投票策略/无编译需求”等为历史判断，由本段取代。

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

**以下0201及更早内容按各阶段保留，当前执行点以上段为准。**

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

**0199供电归属增量（2026-11-12，本地通过，未实机）：** 已确认DSI host L3/PHY L5
与MH2LM UFS及USB PHY共用；这些驱动尚未在SweetRPMh登记依赖，不能按显示最后一票关电。
provider新增typed `lastDisableRequiresHandoff`及mask0xc，在任何写入/token/账本修改前拒绝
这两路最后aggregate disable，保留原票和完成状态；非最后撤票及panel两路原接口保留。
21套检查与RPMh/MDSS/FB六个arm64单文件对象通过。没有新增档位或实际撤电，0198 mode5仍为
当前组合休眠路径。详见[供电图、源码依据、保护与剩余项](PANEL-SUPPLY-OWNERSHIP-D1.md)。
下一步保留L3/L5，补LG顺序的reset-low及panel VDDI/VPNL受控撤票/恢复；E7触摸与E8物理
共享/偏压尚未验收。无新FD/boot、push/CI/设备操作，CircleCI未改，暂不单开云编译。
以下0198及更早描述按当时范围保留；完整驱动仍未完成。

**0198当前增量（2026-11-12，本地通过，未实机）：** 新增显式`sweetpanellifecycle=5`，
把数据票停驻与host/PHY下电接入同一事务。OFF：完整OFF→config floor Held→managed clock
Released→host/PHY/PLL真实关闭→managed DATA NoC Released；ON：DATA Held→PHY/PLL/root
重建→managed clock Held→host重建→面板reset/完整ON/亮度/fresh像素/触摸恢复。
boot/RSC clock、config floor和LDO/ARC/core配置通路保留，暂停mask仍区分票与物理就绪。
仅协调器当前指定的DATA请求可在Dormant沿保留配置通路改票，普通客户端仍拒绝；FB必须同时
核验数据与链路完成证据。原0–4语义保留，无降档。21套检查和MDSS/FB真实arm64单文件编译
通过，严格重放/114原CI marker通过；详见[组合事务、架构和边界](PANEL-COMBINED-SUSPEND-D1.md)。
完整驱动仍缺正常rail-off/故障释放、冷NoC/MDP、ABL退出与完整IOPM；下一步先厘清E8共享电/
偏压所有权并推进受控供电释放。未实机live profile/E7/E8；无新FD/boot、push/CI/设备操作，
CircleCI未改，暂不要求单开云编译/刷机。以下0197及更早段落保留当时范围，当前以0198为准。

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

2026-11-10，用户要求加入大目标二。PD0静态资源核对和接口设计已交付，见
[PD0资源与依赖清单](PANEL-RESOURCES-PD0.md)；硬件身份/状态证据仍按该表跟踪，
PD2–PD5完整初始化尚未实现；0183已补D1命令基础及性能计时，本地验证见
[本轮实现](PANEL-COMMAND-D1.md)。0182自有扫描接管已有日志，0121a12滑动性能出现用户报告的退化。
0121a13已验证0184四路资源元数据与RPMh consumer绑定、自有A接管/唤醒；用户反馈滑动
有所改善、无新增异常。没有实机电源投票或完整初始化证据；见[实测](PANEL-D1-0121A13.md)。
现有 F1–F3 正常使用验收和 0181/F4 首段状态不变。

## 1. 目标与执行位置

最终由 XNU 驱动拥有显示缓冲、显示控制器配置、DSI host/PHY/PLL、面板供电、复位、
DDIC 初始化及恢复。至少先做到正常扫描、息屏恢复、控制台及故障显示均不需要 ABL 缓冲。
最终还须证明显示链路在受控停止、复位及断电后，可以由驱动重建，而不是继续使用 ABL
留下的寄存器状态。“自有缓冲”和“完整硬件初始化”分别验收，不能相互代替。

**2026-11-10优先级调整（用户要求）：完整面板驱动提前，不再等待拷贝双缓冲/性能收口。**
执行顺序改为：已有PD0/0182 → PD2/PD3所需的安全停止与资源/链路/面板实现 →
PD4独立重建 → PD1剩余ABL依赖与PD5必要生命周期收口 → F5直扫及共用完成/等待机制 → F7。
PD1未完成项随硬件驱动补齐；不要求先完成整套CPU拷贝双缓冲、60Hz拷贝性能或重复长测，
才能开始PD2/PD3。F4拷贝双缓冲降为兼容路径按需工作，不再单独占用云编译轮次。

必要的DMA停止、资源所有权、真实完成与buffer持有规则仍是重配/直扫的前提；这些做成
共用基础，由单缓冲交接、面板重建和F5直扫复用，不维护两套提交/完成实现。
F5仍需硬件显示/恢复基础可用，不能把“完整面板驱动”和“零拷贝”互相替代；零拷贝能减少
CPU复制，但不自动保证用户态不早复用surface。F6刷新策略按需，重建必需的触发配置在PD2完成。

允许 ABL 显示开机画面，也允许读取固件提供的硬件描述或经验证的面板标识。
但驱动必须具有不复制 ABL 像素、从自己生成的首帧开始显示的路径；最终不要求 ABL
保留缓冲内容或有效的显示寄存器配置。不涉及替换 ABL 或整个设备的引导链。

## 2. 分层与接口约束

- `AppleCLCD : IOMobileFramebuffer`：用户态 ABI、surface/SwapWait、扫描池与帧所有权；
  通过 typed 后端请求模式、呈现和电源操作，不穿透读取 MMIO 或 PMIC。
- `SweetMDSS : IOService`：MDP/SSPP/CTL/INTF、DSI host、硬件提交/完成中断；拥有自己
  的映射与状态 gate。DSI PHY/PLL 和面板生命周期可分成内部模块或独立 provider，PD0
  根据资源共享决定，不把新增类名当作已实现。
- 面板生命周期层：已确认的设备配置、命令表、reset/TE pinctrl、prepare/enable/
  disable/unprepare。硬件资源通过时钟、电源域、regulator、GPIO 等明确的 provider
  接口申请；缺接口先补最小 provider，不在 framebuffer 内散落 PMIC/时钟寄存器操作。
- 真正的SPMI访问遵守现有共享事务锁和限定接口；RPMh电源投票通过独立provider协调
  唯一TCS所有者，不能穿透mux的充电白名单。共享电源/时钟有引用和释放规则，不能影响触摸
  或其他 consumer。匹配通知只绑定/排队，耗时初始化放在自身 workloop；跨 provider
  等待不持有对端需要的 gate，避免恢复过程死锁。
- 状态至少区分交接、准备中、可显示、关闭中、已断电和故障；具体类型在实现前确定。
  每次状态转换有完成/超时/撤销结果；部分失败按本次实际获得的资源逆序释放。

## 3. 分步实施和验收

| 阶段 | 实施内容 | 结束条件 |
|---|---|---|
| PD0：硬件资料与资源接口 | 核对实际面板版本；整理完整模式、命令、时钟树、PHY/PLL、电源域、regulator、GPIO/pinctrl、共享资源及访问接口，逐项注明已证实/候选/未知 | 能列出从关闭状态恢复显示所需的全部资源和依赖；未知项明确验证方法，未确认的供电/厂商配置阻塞相应写入；完成分层、锁顺序及回滚设计 |
| PD1：摆脱 ABL 缓冲 | 复用 F4 扫描池、可达性、格式/裁剪和共用完成关联；增加不读取 ABL 的首帧；控制台/panic 改用驱动缓冲或独立应急缓冲，定义其生命周期 | 正常显示和 OFF/ON 的实际扫描地址均属于驱动；禁止 ABL 像素读写的诊断路径仍可启动显示；控制台/故障显示不翻回 ABL；DMA退出前不释放缓冲；拷贝双缓冲不再是进入PD2/PD3或验收PD1的独立前置 |
| PD2：显示控制器和链路配置 | 分段实现并显式配置所需时钟/电源域、MDP 路由/格式/时序/刷新、DSI host、PHY/PLL/lanes、TE 和完成中断；按硬件依赖细分补丁 | 在可控停止并重新配置相关模块后，从自有缓冲恢复画面；PLL/PHY/host ready 与真实传输完成均有证据；不能仅靠写回 ABL 快照或 SRC0_ADDR 读回宣称完成 |
| PD3：面板生命周期 | 接入供电/共享资源、reset/pinctrl、有界 DCS 长短包和厂商命令表；实现完整 prepare/enable/disable/unprepare 与亮度恢复 | 已确认面板配置的完整 ON/OFF、Sleep In/Out、reset 和资源回滚有实现；按分档验证命令/睡眠/复位恢复，不将 28/29 当作完整上下电；依赖不齐时拒绝该档，不能猜电压或命令 |
| PD4：显示链路独立重建 | 先停止提交/等待 DMA 和传输退出，再逐级验证面板 reset、面板断电，以及相关 host/PHY/控制器状态丢失后的重建；首帧只用自有缓冲 | 显示已确认关闭、相关资源确实释放/状态失效后，不调用 ABL、不恢复 ABL 快照，也不读取其像素，驱动重新初始化并显示自有测试帧及正常 UI；随后多轮 OFF/ON 和输入/亮度正常 |
| PD5：IOPM、故障与生命周期收口 | 完整显示电源状态机接入 IOPM；统一手动息屏、恢复、终止和关机；明确 panic 的可用资源及不依赖 workloop 的处理策略 | 资源申请/释放配对，晚到 IRQ/完成不访问已释放对象；重复恢复无新增异常；诊断档验证亮/灭屏下故障显示，若需应急缓冲也由驱动拥有；无 ABL 隐式回退。满足这些条件后才标记完整驱动验收 |

PD2/PD3 会有依赖交叉：例如 DSI 发命令前必须具备时钟/PHY，reset 前必须供电正确。
表格是交付顺序，不是可越过硬件依赖的机械调用顺序；具体上/下电流程须在 PD0 设计中展开。
PD3 的“实现完成”不代替 PD4 的实际断电重建证明。

## 4. 当前证据与缺口

- 0181 只分配扫描池、复制有界 ABL 画面并做只读预检，实际仍扫描 ABL，PD1 未完成。
  `sweetfbown=1`的预检语义不变；0182新增`own2`受控单缓冲档，0121a12日志已确认接管及唤醒配置。
  own2仍用ABL作一次交接图像，console/panic及默认surface仍依赖ABL，不能算PD1完成。
  无ABL、reset/断电档后续另定义。
- F2 仅验证 ABL 已初始化/供电前提下的 DCS `28/29`；没有 `10/11`、reset、电源轨
  关闭以及 host/PHY/PLL 的重新初始化。F3 硬件 vsync 成功不证明上述能力。
- 主要资料是工作区 `docs/panel.md` 及 `ref/android_kernel_lge_sm8150` 的实际 DRM/SDE、
  `dsi-staging`、LG override 和 mh2lm DTS；参考索引为工作区 `docs/INDEX.md`。
  PV R66456A/TIANMAMD 是已收集的候选，写厂商初始化表前仍需确认本机所用版本。
- 已收集 GPIO6 reset、GPIO8 TE、L14/L17、LAB/IBB、PHY timing 和完整命令表等候选配置；
  LAB/IBB 精确电压、实际生效的 LG override/供电先后、共享关系与时钟复位依赖仍须核对。
  不从通用 DCS 示例替换厂商次序；资料中的 `29 → 11 + 150ms` 也须按选定版本复核。
- 自有物理页不等于 DMA 可达；PD1/F4 仍须核对实时 SMMU 路由、缓存、范围和完成语义。
  `ADDR_SW_STATUS` 是安全属性，RD_PTR 不是独立的释放 fence。

## 5. 测试、故障与范围

一次编译可提供多个独立档：保留已验收对照，按资源/链路配置、睡眠、reset、断电恢复逐级
打开；不把所有破坏性状态变化挤进首档。每档记录实际执行步骤、资源状态、完成事件和
用户画面观察。没有真正执行的复位/断电不能写为“冷启动通过”。只由用户 `fastboot boot`。

过渡期可保留显式选择的 ABL 对照路径，但它不计入 PD 验收；接管失败必须报告，不因
静默切回 ABL 而把测试判成功。最终默认驱动不能在失败时依赖 ABL 缓冲或旧寄存器快照。
DMA 尚未退出时宁可保留相关内存并报告故障，不能释放后重用。

对每个新增硬件状态机做适当的本地编译、补丁严格重放与超时/部分失败/退出验证，实机
核验画面、亮度、输入、USB 日志及既有驱动是否退化。日志沿用 ERROR/WARN/INFO 分级，
不修改 CircleCI，不顺带调整充电、USB 或触摸策略。

这是大目标二内部新增子计划，显示自身的断电恢复在本计划完成，不推迟到大目标四。
整机 CPU suspend、其他外设低功耗和全系统唤醒协调仍属于后续电源管理大目标；完成
本计划不等于整机低功耗已完成。GPU、复杂色彩/HDR、AOD/动态刷新等扩展另行规划。
BUG-003 继续独立跟踪，不能提前声称完整驱动必然解决锁屏时钟或半透明异常。

## 6. 当前执行点

当前以顶部a31阶段表为准：panel两票周期、ULPS/host恢复、完整命令、自有像素/触摸及FB恢复已实测通过。
下一主段是补齐管线独立配置并推进PD4状态失效后的重建，再补PD1剩余ABL退出和PD5；无需重测旧identity/保票档。
不能将ACK当物理断电，也不能把本次一次成功循环写成完整PD计划完成。

以下为0181/0182基础交接时的历史证据与缺口：

PD0静态清单和设计已完成，详见[资源/现有接口/锁顺序/回滚及E1–E8证据项](PANEL-RESOURCES-PD0.md)。
已确认DSI v2.3/PHY v4.0/PLL7nm系列、RPMh供电路径及现有64-byte DCS接口无法承载
PV最大149-byte payload；实际panel版本/电源状态不提前认定已实证。
0181已由替换后的0121a11实测：双池/seed/四快照、输入及OFF/ON基本回归通过；用户未见新异常。
CB2.M0，FSR原始0x400不命中LG已知故障位，保留解释；新DMA可达性与完成关联未证明。
详见[F4实测记录](FRAMEBUFFER-SCANOUT-F4.md#9-0121a11扫描池预检实测首段运行通过新dma尚未验证)。
0182受控单缓冲接管已本地实现和验证，新增own2及typed begin/status/abandon接口；
一次有界暂停/提交/新传输观察后切CPU目标，30Hz仍在，提交池保留至重启。
0121a12 single已取得新传输、源地址A/裁剪及首次ON配置证据，6组OFF/ON成功；用户明确报告滑动卡顿明显加重，
见[接管记录](FRAMEBUFFER-HANDOFF-F4.md)。不重测F3矩阵、半小时或旧bug专项。
PD0仍是资料/接口设计交付；安全回收/双缓冲、PD1首帧/console/panic及PD2–PD5完整初始化未完成。

## 7. 加速后的交付批次与性能回归

用户认为继续投入拷贝双缓冲的收益低，要求优先完整驱动，随后零拷贝。此调整覆盖旧版
“F4所有项目验收后才能进入PD2”的顺序约束，不将延期项写成已实现或已验收。
以下批次按依赖交付，不表示已经写完完整驱动。0183完成D1中的命令编码/传输/可撤销
序列及合并计时；0184新增资源ABI、真实RPMh provider/唯一writer/typed投票与显示绑定。
0185新增MDSS内部typed本地时钟lease/异步分支控制与温态GDSC准入；0186新增原RPMh内的
MMCX/MX/CX分阶段供电事务、共享票与部分失败保护。0187补apps CN0与MDSS独占的display
RSC/NoC温态ACTIVE带宽、实际RSC时钟lease及VCD完成/反向撤回。0188在显式managed模式下
将四路LDO/ARC/NoC/local clock实际接入自有扫描资源获取；默认关闭，DCS OFF暂不撤票。
0189已补GDSC及两颗核心PLL/AHB/MDP控制并接实际温态获取；冷核心组件通过，实际start
仍需温态。NoC冷配置及link/完整ONOFF仍缺，D1未整体完成。见[命令](PANEL-COMMAND-D1.md)、
[资源](PANEL-POWER-D1.md)、[本地时钟](PANEL-CLOCK-D1.md)、[ARC](PANEL-ARC-D1.md)、[NoC](PANEL-NOC-D1.md)。
见[资源生命周期首段](PANEL-MANAGED-D1.md)及[核心控制](PANEL-CORE-D1.md)。0190进一步接入host v2.3软复位/公共command配置重建，见[host](PANEL-HOST-D1.md)；
0191已接PHY/PLL/输出时钟重建，见[link](PANEL-LINK-D1.md)；
下一段接完整面板ON/OFF和资源反向释放；温态获取不等于独立重建。

| 批次 | 合并实现内容 | 提交/测试门槛 |
|---|---|---|
| D1：资源与命令基础（0183–0191资源/命令/clock/ARC/NoC/核心域及host/link重建本地通过） | 按PD0补完整且不重叠的资源ABI/映射与版本保护；统一现有RPMh TCS writer并提供显示资源lease，明确时钟/GDSC所有者；typed DSI消息编码、支持完整vendor长包的传输和可撤销命令序列执行 | 真实源码编译与超时/部分失败验证；硬件身份/电源权限未知时拒绝相应写入；不为孤立版本读数或空壳服务单独云编译 |
| D2：硬件/面板实现 | PD2最小pipeline与DSI host/PHY/PLL配置；PD3已确认profile的供电/reset/ON/OFF、亮度恢复；共用安全暂停/提交/完成基础 | 一次内核按参数逐级开链路重建、睡眠/复位与恢复；同核保留明确对照，错误显式报告；未知面板表不试写 |
| D3：独立重建与ABL退出 | PD4验证实际状态失效后的重建；PD1自有首帧、default surface/console/panic；PD5最低必要停止/资源回滚与IOPM状态对接 | 证明未读ABL像素仍可显示和恢复，晚完成不踩空；断电/重建确有证据，不能只回写ABL快照 |
| D4：直扫落地 | F5可扫描surface分配、DMA可达性/缓存、token关联提交/完成/SwapWait与复用；兼容surface用已有拷贝路径，只有确有需要才扩拷贝双缓冲 | CPU拷贝数/耗时、直扫占比、实际呈现节奏及交互表现共同验收；不凭60Hz IRQ宣称60Hz UI |

批次间仍按实际依赖推进，D1内部的软件解析、接口、错误路径尽量先做完；可以合并的代码
不拆成多次云构建。仅当已有运行证据不足以安全继续写入时，才安排针对性硬件取证；
不要把PD0整表未知项一次性变成前置测试清单。E1面板身份只阻塞相应vendor表/reset后的
初始化，E4版本/布局只阻塞相应link写入；不阻塞先实现可离线验证的接口与序列执行。

**0121a12性能回归：** 用户明确反馈“比较明显，滑动比较明显”。当前own2是单缓冲A，
B只分配未提交，原30Hz CPU复制仍在；不能归因于尚未实现的双缓冲。日志确认的是接管/
6组OFFON及约59.91Hz IRQ，不证明滑动流畅或复制频率；本轮不作体验验收，也不切默认。

核对0182热路径：接管refresh约10MiB只执行一次；Active后`scanoutStatus()`仍每次vsync
跨provider gate查询，逐层向WC缓冲复制后有写屏障。这些是待计时的候选开销，不是已证实
根因；尚无逐帧copy耗时或提交年龄数据，不能直接说是WC、屏障或该gate造成。
0183已合并实现CPU呈现间隔、copy bytes/耗时、pending年龄/合并和provider查询耗时，
可选低频汇总p50/p99桶上界及超预算次数，不逐帧打印；0121a13已采样1237次复制，均2.4ms/max4.8ms，
未定因旧回归，用户反馈有所改善。0185修复实际OFF转换的interval清零，保留pending年龄。
保留必要的内存屏障；不靠关闭日志、强开60Hz
或删除DMA保护来冒充修复。计时随D1/D2镜像合并，不另开编译/刷机轮次。

调试对照已有0121a12 own0/own1，现不要求用户为此重新刷机，亦不重做半小时/F3/旧bug专项。
回归保留在当前显示计划，不误归档为与当前任务无关的已完成旧bug。后续需要短对照时同核
只改变必要参数，并说明要区分的假设。硬件驱动优先不表示忽略已报告的卡顿。
