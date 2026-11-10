# F4：自有扫描缓冲、硬件裁剪与双缓冲翻页

2026-11-10。F3正常使用路径已由0121a11验收，用户授权开始F4。
**当前0181首段已由替换后的0121a11实测：池分配、四份快照及基本回归通过；SMMU原始值需保留解释，尚未验证新缓冲DMA。**
尚未切换扫描地址或完成翻页。本轮不改CircleCI，不push，不操作设备。

**0182第二段已完成本地实现和验证，待用户push、云构建及实机。** 新增独立`own2`档，
由SweetMDSS在有界timer中暂停TE/autorefresh、提交A并核对新传输，再由FB将CPU目标切入A。
这是单缓冲/30Hz实验，提交后的整个池保留到重启，不计最终回收或双缓冲验收。
own0/1语义不变；完整设计、失败边界和本轮上机步骤见[受控接管记录](FRAMEBUFFER-HANDOFF-F4.md)。

**2026-11-10新增目标：** F4是[完整面板驱动PD1](PANEL-FULL-DRIVER-PLAN.md)的主体，
但不等于完整硬件接管；还须补无ABL像素的首帧及自有控制台/故障缓冲，再推进PD2–PD5
独立初始化和断电恢复，之后才进入F5。0181仍只是预检，own1语义不变；首段实测见第9节。
PD0[资源核对](PANEL-RESOURCES-PD0.md)已区分F4预检所需E2与后续冷初始化门槛；不要求
先确认全部厂商表和电源权限才能测试当前扫描池，但不能越过DMA可达性/完成关联接管扫描。

## 1. 先纠正完成确认依据

重新核对设备实际使用的LG DRM/SDE源码，而非树中未启用的fbdev驱动：

| 项目 | 核对结果 | 本地参考源码 |
|---|---|---|
| SSPP `+0x70` | `SSPP_SRC_ADDR_SW_STATUS`；`setup_secure()`读改写低4位，表示各plane安全属性，**不是扫描地址或翻页完成状态** | `ref/android_kernel_lge_sm8150/drivers/gpu/drm/msm/sde/sde_hw_sspp.c:82,431–463` |
| SSPP `+0x14` | `SRC0_ADDR`；`get_source_addr()`直接读此配置寄存器，读回并不能单独证明旧硬件读者已退出 | 同文件735–776 |
| CTL `+0x18` | `CTL_FLUSH`；只写VIG0目标bit不能代替完成事件，flush值也不是最终release fence | `sde_hw_ctl.c:299–324` |
| RD_PTR | LG用它通知vsync；retire处理还关联CTL_START和pending计数 | `sde_encoder_phys_cmd.c:268–360` |
| PP TX_DONE | LG在此发送FRAME_DONE / RELEASE_FENCE并维护pending kickoff计数 | 同文件195–240 |
| autorefresh | LG有独立autorefresh_done处理，启用时不直接发普通CTL_START | 同文件243–265,1687–1694 |

因此原计划“下一RD_PTR读ADDR_SW_STATUS确认并释放旧缓冲”已删除。
F3实测约60Hz和下一次RD_PTR都不能独立证明某一次新地址提交已经生效。
不能把上述Linux fence代码直接移入XNU；需要按同一硬件事实实现typed接口和IOKit事件状态机。
现阶段只记录真实寄存器与内存信息，不以无故障读数自动授权新DMA地址。

## 2. F4分段与结束条件

| 分段 | 内容 | 结束条件/当前进度 |
|---|---|---|
| 首段：池和live预检 | 两个自有缓冲、ABL有界复制准备、typed只读快照 | 0121a11分配/启动/四快照/输入与唤醒通过；CB2原始FSR保留复核，新DMA未测；扫描仍ABL |
| 单缓冲接管 | 0182/own2本地实现和验证通过，待新云产物及实机；暂停TE/auto、有界提交/观察，再切CPU目标 | 待证明真实画面、裁剪及OFF/ON接管；提交池保留到重启，安全停止/回收未完成，不计整段验收 |
| 完成事件和双缓冲 | 确认本机autorefresh下的提交关联；typed提交/事件反馈；前台、后台、待确认独立管理 | A/B正常轮换，旧读者退出后才复用；超时不伪装完成；OFF/ON/停止不踩空 |
| 等待和性能收口 | 完整SwapWait/pending/display语义，CPU复制独立执行，移除新路径30Hz限制 | 原F4验收矩阵通过，复制耗时/呈现频率有实测，压力及资源平衡通过 |

后续行为切换另设明确档位；0181的`sweetfbown=1`固定表示首段预检。
原F4目标及整体验收仍在[面板计划](FRAMEBUFFER-PANEL-PLAN.md)。不提前宣称F4全部完成。
F4/PD1后先完成PD2–PD5的完整面板驱动；F5才是可扫描surface直接输出/零CPU拷贝；
F7负责默认切换及最终旧路径清理，不再把翻回ABL作为最终panic/恢复方案。

## 3. 0181架构和内存所有权

```text
AppleCLCD : IOMobileFramebuffer
 ├─ SweetScanoutPool : OSObject（本次新建，属于framebuffer）
 │   ├─ IOBufferMemoryDescriptor A：32位物理范围、连续、WC
 │   └─ IOBufferMemoryDescriptor B：32位物理范围、连续、WC
 ├─ 已有IOWorkLoop/IOCommandGate、硬件vsync IES、SweetVsyncChannel
 └─ 持有的SweetDisplayBackend* ── inspectScanout(stack snapshot)
                                  │ provider gate串行，只读
                             SweetMDSS : IOService
                              └─ 已验证ADT资源：SSPP/CTL/INTF/MDP/TE/SMMU/CB2

当前实际扫描仍由ABL 0x9d200000提供；A/B都未提交给MDP。
```

补丁：`xnu/patches/0181-mh2lm-scanout-pool-preflight.patch`，严格接在0180之后。
新实现位于`SweetScanoutPool.{h,inc}`、`SweetFBScanout.inc`、`SweetMDSSScanout.inc`；
include进入原有两个cpp，无新增全局单例、BSD桥接或编译单元，不改CI编译列表。

- 默认own0不分配扫描池、不调用新增寄存器预检。own1要求service1/mdss1/power1/irq2/vsync1/
  contract0；无效档位/组合明确ERROR并拒绝启动实验配置。
- 启动早期、GUI发布前申请两个1125×2436画布，行距4608；每个11225088字节（约10.705MiB），
  合计22450176字节；底层分配可能按内核页向上取整，不能据此宣称物理分配恰好为此字节数。
  使用`inTaskWithPhysicalMask`、`kIOMemoryPhysicallyContiguous`、`kIOMemoryMapperNone`、
  `kIOMapWriteCombineCache`；prepare后用`getPhysicalSegment(...MapperNone)`核对整个范围，
  检查非零、对齐、连续长度、末地址低于4GiB和A/B互不重叠。物理地址不是IOVA。
- 只在已知ABL物理地址/1080×2340/stride4320、逻辑1125×2436、offset22/48且源descriptor
  足够大时复制。先排除两缓冲与完整18MiB显示保留区的重叠，再由驱动清零并仅向A的(22,48)
  写入现有面板矩形；B及边缘/行padding保持零。不能先写像素、后检查是否覆盖ABL。
  不按大画布尺寸越界读取ABL，不写ABL。该副本是启动时准备样本，**不是未来接管时的最终帧**。
- 复制/分配发生在服务发布前，不加入vsync回调的像素拷贝；本轮保留原CPU30Hz输出策略。
- 任一分配/prepare/验证失败会释放部分资源，明确ERROR及registry失败属性，关闭首段实验，
  保留已验收F3路径；不能将“能进系统”替代pool ready/预检成功判断。
- 服务stop在gate里先关闭admission和硬件订阅，再释放尚未交给硬件的池；free覆盖部分启动失败。
  后续一旦MDP可能持有缓冲，必须先扩展硬件读者退出协议，不能照搬当前立即回收的条件。

## 4. typed快照和日志

`SweetDisplayBackend::inspectScanout(SweetScanoutSnapshot *)`由持有provider的consumer调用。
provider在自己的gate检查admission/时钟/必需资源，使用已有ADT映射读寄存器；输出不暴露MMIO指针。
没有provider→consumer gate反调，通知上下文不执行硬件预检，不增加常驻timer或轮询线程。

快照包含源尺寸/裁剪/输出/地址/行距/格式/unpack/op/安全属性，CTL flush/start/top，INTF配置/
autorefresh/行位置，MDP/TE的enable及原始锁存状态。SMMU ID匹配已知布局才扫描SMR；
报告所有匹配的数量，仅唯一TRANS/CB2匹配时读取已映射CB2的转换配置及fault/FAR，未知或
冲突路由只报告，不猜测新CB地址。fault只读、不清除；读值是当前可见寄存器视图。

在consumer收到真实RD_PTR后取前三份、约隔60帧；第一次后续OFF→ON的真实帧可再取一份。
**整次启动最多四次，包括失败调用**；OFF不采样，幂等ON不追加，停止后不调用；无逐帧日志。
快照内各寄存器为顺序读取，非原子硬件快照；原始完成status可能是旧锁存，不能计算完成频率。

- INFO：pool ready/物理范围/字节数、一次seed耗时（含两缓冲清零和面板矩形复制）、各份硬件快照。
- WARN：配置、路由、安全/fault值需要复核；不自动改硬件或批准接管。
- ERROR：申请/准备/预检失败，不受等级过滤隐藏。沿用现有`sweetloglevel`，本轮不重构日志系统。
- registry：`SweetScanoutPreflight`、`SweetScanoutActive=false`、A/B/Bytes、Samples和失败属性。
  `Active=false`明确表示仍用ABL；CPU连续内存及干净fault读数都不证明DMA可达。

## 5. 已完成本地验证

- 以已核对0121a11源码为基线，0181 strict apply零offset/fuzz，应用树逐字节一致；原CI114条
  正向源码marker仍满足，CircleCI blob保持`da0b6c0947a66f03491b3a9023590b49665292df`。
- SweetMDSS/SweetFramebuffer的基线和补丁树均通过arm64-apple-darwin单文件对象编译。
  MDSS新增6条、FB新增23条有界数组/指针`unsafe-buffer-usage`诊断已按资源索引/两项循环/
  矩形边界检查审阅；FB新增4条OS元类宏既有类别告警，其余告警计数不变。
- 实际pool代码ASan/UBSan：12种分配/prepare/物理地址失败、prepare/complete配对、部分资源
  释放、零尺寸/溢出/行距/坐标/源长度拒绝、真实画布复制、源不变、B/边缘/padding清零及guard检查。
- 实际provider代码ASan/UBSan：新增接口MMIO轨迹全只读，安全字段按其原意输出；未知SMMU、
  多重匹配、其它CB、关闭时钟/停止admission及缺CB资源不穿透；原DCS/F2/F3测试仍通过。
- 实际FB代码ASan/UBSan：完整档前置条件、故障明确关闭首段、启动失败/stop释放池、typed调用
  预算、OFF不读/ON补样本、预检错误不停止F3；原dispatch/端口/客户端/硬件vsync回归通过。
  LSan仍受本机ptrace限制关闭，fixture对象计数/prepare平衡和guard另外断言。
- 打包shell语法、两档仅own差异、F3原四档参数保持；旧0121a11 kernel被F4 marker门拒绝，
  实现时尚未生成新实验boot；后续打包和实测分别见第8/9节。底稿`work1/scratch/framebuffer-0181/`含源码、编译和检查结果。

这不替代整内核云构建或真机分配/缓存/SMMU及显示验证。

## 6. 云编译后测试

用户push新提交并下载kernel/buildtree/build-log后，先核对0181源码与F4 marker，再运行：

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-scanout
```

两档均保留已验收的硬件vsync、DCS28/29、按键/触摸/USB IRQ、ACM和电池runtime3：

| 镜像后缀 | 差异 | 用法 |
|---|---|---|
| `fb-scanout-baseline` | own0，无新池/预检 | 故障对照备用，不要求重新验收F3 |
| `fb-scanout-preflight` | own1，两个池+一次复制准备+有界快照 | 本轮先测这一档；扫描仍ABL |

仅`fastboot boot`。进系统亮屏45–60秒，简测亮度/三键/触摸；手动息屏20–30秒，唤醒后
亮屏10–20秒并导出`sweetlog-编号-fb-scanout-preflight.bin`。没有自动息屏要求，不专项重复
充电插拔、BUG-002或半小时待机。现阶段不预期画面或流畅度改善。

检查pool ready、两个不重叠低32位物理范围和一次seed完成、三个初始快照及ON后补样本；
扫描仍0x9d200000、secure字段含义正确、SMMU/故障视图完整，F3硬件激活/输入/唤醒未新增退化。
如果pool setup failed或inspect失败，即使画面正常也不能验收本首段；按明确日志继续定位。
首段通过后结合live数据设计单缓冲接管与真实完成事件，不无意义重复同类只读测试。

## 7. 本次重复编号0121a10产物核对：不是0181

2026-11-10用户报编译完成，logs中两归档修改时间确为当日，但解包kernel仍与历史0121a10
一致：SHA256 `b6a0732a419f28276b6b6f8fba7c15f711d66394e66ee200043066a0aeda397e`，
17503376bytes；历史44份源码SHA也全部相同。归档字节不同不能证明其中驱动已更新。
新包没有0180硬件vsync的两个inc或0181扫描池/预检四文件，内核也无F3v2及F4 marker。
本次未打包boot，不能拿它验收0181；原历史预检测试证据不被覆盖。
原因须由正确任务产物/本次build log区分，本地尚无这份新构建日志，不据此判CircleCI损坏。
核对底稿：`work1/scratch/framebuffer-0181/incoming-0121a10-verification.json`。

## 8. 更正编号0121a11：0181产物核对及预检镜像

2026-11-10用户更正编号；本日18:53替换的0121a11归档确已包含0181。
**与历史同编号F3内核不同**：本次kernel为17525432bytes，SHA256
`b605b12db6e6f8a5edaae1182560b50589f099bb9d4f10fce1fcf5a5e5469234`；
历史F3内核为17523360bytes、SHA256
`5c7a21c1b077ccfae427547d9cb350b070749c1f0a5a8208485503bbf1d9dd5a`。
本次52份源码逐字节匹配0181验证树，包含9份补丁变化文件；归档manifest、114条原CI源码
marker及F3/F4内核marker均通过，对应实现42047d3。未取得本次完整build log/CI run ID，
不据产物核对宣称审完云端告警；也不把历史F3实测套用为0181实测。

两份归档SHA256：

- kernel：`afd78ed2eddebd2f12e64742ba2641557146c211d42062d014117625095f365d`
- buildtree：`65cb7ab5d97e8bd6f3d7087b22cbf77ba157c5573fa1c8bdd20e85a42b9de6a7`

已打包两个新后缀镜像，各35667968bytes，不覆盖历史F3镜像：

| 路径（相对work1） | SHA256 |
|---|---|
| `out-bs/boot-16k-0121a11-fb-scanout-baseline.img` | `9ed8681e1fe29bfaa9048ee9bb89ceeda976546eb3b0ee652ef70786d86bfba4` |
| `out-bs/boot-16k-0121a11-fb-scanout-preflight.img` | `5823bdbc9917292f578239b032ec25558b17c0e0c9a07aa1c1b62551414864d2` |

独立重解析最终Android boot v1和FAT内容，核对UEFI、ramdisk、实际内核、TOML和扩展ADT。
两档内核一致、参数只差sweetfbown=0/1；irq2/vsync1/F2/ACM/按键/触摸/电池runtime3保持，
无重复参数。打包时本机缺file(1)，只在scratch包装脚本用Python检查boot头，未改仓库打包器。

先执行：

```sh
fastboot boot /home/shenli/work1/out-bs/boot-16k-0121a11-fb-scanout-preflight.img
```

按第6节亮屏45–60秒/简测输入与亮度/手动OFF20–30秒/ON后10–20秒，导出
`sweetlog-0121a11-fb-scanout-preflight.bin`。baseline留作故障对照，无需先刷。
打包时尚无本次实机证据（后续实测见第9节）；仍只分配池、准备复制样本并读live快照，实际扫描保持ABL，
不能计作PD1已接管或承诺画面改善。通过后再依据快照推进受控接管与完成关联。

本次底稿：`work1/scratch/artifact-0121a11-f4/`的`verification.json`、`source-compare.json`、
`boot-verification.json`、两个verify脚本及`packaging.txt`；历史`artifact-0121a11/`不覆盖。
CircleCI blob仍`da0b6c0947a66f03491b3a9023590b49665292df`，无驱动改动、push或设备操作。

## 9. 0121a11扫描池预检实测：首段运行通过，新DMA尚未验证

日志`work1/logs/sweetlog-0121a11-fb-scanout-preflight.bin`，SHA256
`8cc34d8a15178179031ef8f155e348ce84d3d06649a08d63cb5bb5675ddd20cc`。
有效343538bytes、flush76、未回卷、4561行；371行确认own1/irq2/vsync1。
用户确认画面、亮度、三键、触摸、手动息屏唤醒“都正常，未见新增问题”。

### 9.1 池及快照

171–172行pool ready和seed成功。两缓冲逻辑范围如下，均16KiB对齐、低于4GiB、互不重叠，
且不覆盖ABL保留区`[0x9d200000,0x9e400000)`；末地址是逻辑字节范围，不代表页取整后的分配末端。

| 缓冲 | 起始物理地址 | 逻辑末端（不含） | 逻辑字节数 |
|---|---|---|---|
| A | `0xc3b30000` | `0xc45e4800` | 11225088 |
| B | `0xc4668000` | `0xc511c800` | 11225088 |

行距4608、画布1125×2436、crop22,48；合计22450176bytes，约21.41MiB。
一次清零/复制准备耗时167265us（167.265ms），发生在启动阶段，不能当逐帧复制耗时。
日志证明seed函数成功返回，不是对新缓冲显示质量的肉眼验证。

866/972/1079/3853行四份快照分别来自frame1/61/121/17929；最后一份在OFF→ON首帧。
四次地址均`0x9d200000`、stride4320、xy/outxy0、size/out1080×2340、secure0；
format/unpack/op仍为0x237ff/0x03020001/0x80000000。符合本轮不切扫描的设计。
INTF1 autorefresh仍`0x80000001`，MDP status0x100/tear status0xb为原始锁存视图，
不能据这些值证明新提交完成或计算传输完成频率。

### 9.2 SMMU原始非零值的含义

四份SMMU/CB2记录一致：已知ID，SID0x800唯一匹配SMR3=0x84200800，S2CR=0x2，
TRANS/CB2；CR0=0x2b0406、全局fault0。CBAR=0x1f000、CBA2R=1、SCTLR=0xe0/M0，
TTBCR/TTBR/MAIR均0。本轮stage1转换未使能，不据TRANS路由名直接要求建立页表。

**CB2 FSR原始值为0x400，FAR为0x0001fdfe:ba6e8cf7。** 0181当前按`cbFault != 0`
提示WARN，因此有四条`live configuration needs review; no handoff authorized`，不是
pool/inspect失败。复核LG `drivers/iommu/arm-smmu-regs.h:237–251`：其`FSR_FAULT`
为0xc00001fe，不含bit10，`0x400 & FSR_FAULT == 0`；`arm-smmu.c`的context handler
也先按此mask判定是否有fault。故不能将0x400描述为已证实的translation/permission/
external/stall故障，或把此FAR当有效故障地址；bit10具体含义仍未确认，不能反向声称
raw状态全零、已清故障或新DMA可达。没有写FSR/RESUME/页表，也没有硬件访问A/B。
后续接管实现须区分已知fault位和未解释原始位，并明确接管条件；本轮保留WARN和原值，
不通过悄悄清状态/忽略真实fault来推进。

### 9.3 基本回归与范围

- GIC probe hits1，FB首帧及ON后均hardware active，无FAILED/显式软件回退。
  九个完整窗口共540.191秒/32358帧，59.871–59.916Hz，加权59.901Hz，dropped全0；
  OFF前四个、ON后五个完整窗口。这是IRQ/vsync频率，不是新缓冲呈现频率。
- 3627行OFF暂停frames17928/entries17929，3848行ON重启计数相同；3852行恢复frame17929。
  OFF内七份电池完成记录跨度至少98.294秒，期间无MDSS计数日志；DCS OFF/ON均rc0，
  backend分别9/7ms，FB分别10/7ms。无息屏期间新增快照，整启动限四份符合预期。
- 16条按键事件完整配对：power3/up1/down4对；12条触摸事件，亮度请求含2775→0→2775。
  用户已确认亮度和输入正常；没有更多亮度档位的日志证据，不补造压力/终止测试结论。
- 无驱动ERROR、mux隔离、poison、SPMI timeout或kernel panic。47份电池样本、46次refresh
  全healthy1；末完成796.659秒。全部gauge charging0、电流-132..0mA，启动一次ENABLE；
  供电来源本轮未另确认，不将采样健康当真实充电，不重开电池任务。
- USB两次reset均见ACM配置，三条端点WARN，VERIFY(-12)为0；不关闭BUG-001/004。
  780–783行driverkitd仍sig9/ns1/code7，BUG-005仍存在。用户态既有BindFS缺目录等信息
  与驱动ERROR分别统计，不能写成整份日志没有error。

**决定：** 首段池分配/四快照采集及正常输入与OFF/ON回归通过，无需补刷own0或重测F3。
E2已获得实机物理范围与live配置；新缓冲DMA可达性、真实提交/完成、旧读者退出仍未验证。
下一步以本次证据设计受控单缓冲接管，先核对缓存/格式/裁剪及autorefresh提交与完成关系，
明确硬件持有内存的生命周期、失败时的停机/保留策略；另设接管档，不改变own1语义。
随后才实现双缓冲/SwapWait和PD1无ABL首帧/控制台，不能把本次预检视为PD1完成。

底稿`work1/scratch/artifact-0121a11-f4/`新增`analyze-preflight.py`、
`preflight-log-analysis.json`、`preflight-analysis-summary.json`、`preflight-selected.txt`及解码文本。
本轮仅分析和记录，无内核/boot参数/CI修改，无新编译、push或设备操作。
