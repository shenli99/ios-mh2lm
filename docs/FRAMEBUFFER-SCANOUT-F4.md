# F4：自有扫描缓冲、硬件裁剪与双缓冲翻页

2026-11-10。F3正常使用路径已由0121a11验收，用户授权开始F4。
**当前0181完成首段本地实现及检查，待用户push/云编译/实测；尚未切换扫描地址或完成翻页。**
本轮不改CircleCI，不push，不生成基于旧内核的新实验boot，不操作设备。

**2026-11-10新增目标：** F4是[完整面板驱动PD1](PANEL-FULL-DRIVER-PLAN.md)的主体，
但不等于完整硬件接管；还须补无ABL像素的首帧及自有控制台/故障缓冲，再推进PD2–PD5
独立初始化和断电恢复，之后才进入F5。0181仍只是预检，own1语义及待实测状态不变。

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
