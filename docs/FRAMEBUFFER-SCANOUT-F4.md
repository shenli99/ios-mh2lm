# F4：自有扫描缓冲、硬件裁剪与双缓冲翻页

2026-11-10。F3正常使用路径已由0121a11验收，用户授权开始F4。
**当前0181首段已完成本地验证；替换后的0121a11云产物核对通过，预检boot已打包，待实机。**
尚未切换扫描地址或完成翻页。本轮不改CircleCI，不push，不操作设备。

**2026-11-10新增目标：** F4是[完整面板驱动PD1](PANEL-FULL-DRIVER-PLAN.md)的主体，
但不等于完整硬件接管；还须补无ABL像素的首帧及自有控制台/故障缓冲，再推进PD2–PD5
独立初始化和断电恢复，之后才进入F5。0181仍只是预检，own1语义及待实测状态不变。
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
| 首段：池和live预检 | 两个自有缓冲、ABL有界复制准备、typed只读快照 | 0181本地通过，待实测分配/启动/快照/输入与唤醒；扫描仍ABL |
| 单缓冲接管 | 核对内存可达性、缓存和格式；保留缓冲，受控切入一个自有扫描地址，定义恢复/退出 | 真实画面、裁剪位置和扫描接管成立；没有完成证据时不得释放可能在扫描的内存 |
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
  尚未生成新实验boot。底稿`work1/scratch/framebuffer-0181/`含源码、编译和检查结果。

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
当前尚无本次实机证据；仍只分配池、准备复制样本并读live快照，实际扫描保持ABL，
不能计作PD1已接管或承诺画面改善。通过后再依据快照推进受控接管与完成关联。

本次底稿：`work1/scratch/artifact-0121a11-f4/`的`verification.json`、`source-compare.json`、
`boot-verification.json`、两个verify脚本及`packaging.txt`；历史`artifact-0121a11/`不覆盖。
CircleCI blob仍`da0b6c0947a66f03491b3a9023590b49665292df`，无驱动改动、push或设备操作。
