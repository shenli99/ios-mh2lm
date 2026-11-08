# 0121a2 F0 v2 实测：时钟区域、帧语义和硬件配置

2026-11-08。内核源码基线 `2bc0251`（0171+0172），产物记录提交 `e89754d`。
本轮只分析日志、对照源码并更新文档，没有修改驱动或 CI，没有 push。

**结论：preserveoff 没有解决用户观察到的显示异常。持续缺时间的形态在驱动当前读取的
source surface 和扫描内存中同时出现，不能只用息屏擦除或漏拷时钟解释。提交 ABI、
完成/复用协议和本版正常充电界面的区别仍未闭环，F0 不提前验收。**

## 1. 证据与用户观察

| 档位 | 原始文件（work1/logs 下） | SHA256 | 有效字节 / flush / 行数 |
|---|---|---|---|
| 普通 | sweetlog-0121a2-fb-trace.bin | 80a74ed551c571036fe7f1abc643b4b6904d45a631cb1750e52f9c445c0a6088 | 1046410 / 27 / 9898 |
| preserveoff | sweetlog-0121a2-fb-trace-preserveoff.bin | 402477844444ec1b21aa826b31b4220ef1fbcc2ad2fb98980d3532aa9a573d1a | 1034355 / 31 / 9578 |

均为 v1 / 4 MiB ring，未回卷，有效区无 NUL。两档各记录 8 次 display off、9 次 on
（含启动）；不能将电源请求次数直接当作插拔测试次数。持久刷写点之后可能仍有未保存事件。
内核 SHA256：`5bc262e74534dbc2952a09342a0d97348561226d58a16a5a7c937ebad10966b1`。
38 文件源码、114 项 marker 与 boot 回读核对见 [F0 v2](FRAMEBUFFER-F0-V2.md)。

用户确认：**两档都和之前一样**，即插电唤醒、大电池图标消失后缺时间，拔电唤醒有时间，
关机滑条重叠、取消后缺时间；没有观察到 preserveoff 带来的改善。黑色背景本身不算故障。
没有逐秒录像或操作时间标记，因此以下时间线是按电源/电池事件对应，不能标定每个用户动作。
本版正常 iPhone 在对应充电过渡状态下是否应立即出现大时间，仍未独立核验。

本地底稿：`work1/scratch/artifact-0121a2/` 中 `fb-trace.txt`、
`fb-trace-preserveoff.txt`、两份 `*.timeline.txt`、`log-analysis.json`、`analyze-logs.py`。
下文行号均指这些解码文本。原始日志和设备产物不入 git。

## 2. 稳定黑区域已经存在于当前 source

时钟 ROI 为 panel `(180,195)`、`720×585`，对应 source `(202,243)`；步长 8，6660 个样点。
两档实际 CPU copy 均为 source `(22,48)` → panel `(0,0)`、`1080×2340`。

| 统计 | 普通 | preserveoff |
|---|---:|---:|
| 有 source/panel 对照的周期时钟样本 | 183 | 186 |
| RGB 差异为 0 | 173 | 175 |
| 首次手动息屏后，有对照的周期时钟样本 | 107 | 87 |
| 其中两侧 RGB 全 0、alpha 非零样点 6660、差异 0 | 44 | 45 |

不是单次过渡采样：

- 普通第 6070–6086 行，t=189.029s，copied=294/source2，时钟两侧 `rgb=0 alpha=6660
  diff-rgb=0 diff-alpha=0`；upper/lower 仍有彩色样点。189–198.099s 连续 10 次同类观察，
  copied 从 294 推进到 766，说明期间有新帧拷贝，并非一直没有呈现。
- preserveoff 第 6198–6214 行，t=234.717s，copied=187/source3，同样时钟全黑且相等。
  234.717–241.771s 连续 8 次同类观察，copied 从 187 推进到 492。
- 拔线后对照：preserveoff 第 6508–6524 行，t=249.168s，copied=499/source2，时钟两侧
  `rgb=1044 alpha=6660 diff-rgb=0 diff-alpha=0`，中央区域恢复可见内容。

这将持续异常的排查方向前移到了**驱动读取的源图为何如此、提交解释是否正确、何时允许复用**。
不能据此宣布“CoreAnimation 自身有 bug”：source 是事后实时读取，并非 SwapEnd 时的不可变
快照；接口兼容性可以影响用户态绘制，采样本身也不识别字形或所有未采到的像素。
关机滑条重叠还没有获得与具体动作一一对应的独立像素证据，不能直接套用缺时间的结论。

## 3. 擦除/空记录确有缺口，但不能解释全部异常

普通首次息屏：

- 第 5545 行 off-before，t=175.891s/copied260，clock source/panel 均有 4890 个彩色样点。
- 第 5607 行 off-after，t=179.515s，panel clock RGB/alpha 都变 0；source 仍为 4890/6660。
- 第 5767 行 on-after，t=185.081s，attempt262/copied260，最新 swap262 是全 disable、空
  surface 记录，没有 copy，panel 仍全 0。后续有效 surface 到来后恢复 copy。

preserveoff 第 5707、5726、5870 行，首次 off-before/off-after/on-after 时，clock 两侧均为
4883 个彩色样点且相等；attempt152/copied150，空记录152没有 copy，旧画面确实被保留。
但后续仍出现第 2 节的源图/扫描同黑，用户也确认肉眼异常相同。

两档捕获到的主要提交形态一致，仅 layer0 观察到非零 surface，未见 layer1–3 的非零 surface：

| update(+0x104) | disable(+0x108) | surface | raw10c / raw110 |
|---|---|---|---|
| 1 | 0 | layer0=2/3，全画布 | 0 / 0 |
| 1 | 1 | 全 0 | 0 / 0 |
| 0x80000007 | 0x80000007 | 全 0 | 0xff000000 / 0 |
| 0x80000007 | 0x80000006 | layer0=2/3，全画布 | 0xff000000 / 0 |

背景色候选位于 **+0x10c**，与 UniXNU 参考吻合；不能继续猜 +0x110。
当前呈现路径跳过空 surface，没有完整实现 update/disable/background 的持久图层状态。
但全 disable + 黑背景可能正是合法 blank 请求，**不能仅过滤空记录或强行恢复旧图作为修复**。
需先核对本版 ABI，再决定空记录如何改变呈现状态。

## 4. surface 会复用，retain 不是像素所有权协议

preserveoff 第 6675 行 off-before，t=258.096s/copied902/source3：panel clock 仍有 1064 个
彩色样点，当前 source clock 已全黑，diff-rgb=1064；第 6694、6812 行 off-after/on-after 仍
有同类差异。普通档第 6482、7440、8441 行也有类似现象。

这证明“上次实际拷贝所用 surface 的当前内容”可以不同于留在扫描缓冲的画面；不能再把
source id 当帧快照。也不能据此证明差异发生在 memcpy 内部，或断定 CA 非法写内存。
普通档仅 1 次 `copy-changed=1`，其余为 0；该字段只检测驱动 copy 元数据变化，检测不到所有
用户态写入。持有 IOSurface 对象引用只保证对象存活，不能阻止拥有映射的 CA 重用其像素。

下一步须核对 SwapEnd / SwapWait / 完成通知、被覆盖 pending 的处理及用户态等待条件。
F4/F5 只有 retain/release、没有对应完成/复用协议，仍不足以安全直扫。

## 5. selector 与 surface 属性：可离线核对的接口线索

两档使用的 selector 共 21 种：
`3,4,5,6,8,12,15,17,22,27,28,29,33,50,55,57,68,74,78,79,84`。
未见 72；这不等同于硬件不刷新，也不证明无需完成协议。

**明确候选缺口：selector79 / block0x40。** 每次亮屏后 backboardd 请求 `a=0x40`，输出结构
40 字节（普通第 2808、5720、6338 行等）。当前 `SweetFramebuffer.cpp` 没有相应处理，
走 default 后清零输出；SwapWait 也直接返回成功。

HakiOS `ref/hakios-xnu-kernel/xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp` 中，
selector79 为 GetBlock，第 1620–1633 行对 block0x40 在输出 +4 处返回 color/timing 两个 ID，
与第 403–490 行发布的 `TimingElements`、`ColorElements`、`IOMFBDisplayRefresh` 对应。
当前 AppleCLCD 没有发布这组模式属性。该参考为另一用户态版本，只能提供核对线索，
**不能直接证明本项目 iOS 16.7 的缺时间由此导致，也不直接搬整套实现**。

两档 surface2/3 的 create 字典均捕获 11 键（普通第 2846–2858、2894–2906 行）：

| 键 | 值 |
|---|---|
| IOSurfaceWidth / Height | 1125 / 2436 |
| IOSurfaceBytesPerElement / PixelFormat | 4 / BGRA(0x42475241) |
| IOSurfaceBytesPerRow | 4608(0x1200) |
| IOSurfaceCacheMode | 0x400 |
| IOSurfaceAllocSize | 0xab5000（请求）；内核按 16 KiB 对齐后 11239424 |
| IOSurfaceMapCacheAttribute | 0 |
| IOSurfacePixelSizeCastingAllowed | false |
| IOSurfaceMemoryRegion / Name | PurpleGfxMem / QuartzCore |

未见 displayable/gpu 键；这些属性可供 F5 识别候选 surface，但名字/尺寸不保证物理连续或可扫描。

## 6. 硬件配置补证据

两档 VIG0 配置一致：地址 `0x9d200000`、stride4320、size1080×2340、format0x237ff、
unpack0x3020001。INTF1 FRAME_COUNT(+0xac)=0，同时计数使能(+0xa8)=0、raster line=0，
tear line 循环。本轮无法测出 TE 频率；无需再追加同类一秒寄存器差值实验，F3 用 RD_PTR IRQ
时间戳测实际间隔。

SMMU 普通第 2514–2516 行、preserveoff 第 984–986 行：

- SMR3=`0x84200800`，S2CR=`0x2`，route 类型 TRANS/CB2。
- CB2 PA=`0x15082000`；CBAR=`0x1f000`（stage1 translation/stage2 bypass），CBA2R=1。
- **SCTLR=`0xe0`，M bit0=0**；TTBR0/1、TTBCR、MAIR 均 0。
- TTBCR2 普通 `0x2c076`、preserveoff `0x4002c076`。

LG `drivers/iommu/arm-smmu-regs.h:222` 定义 SCTLR_M=1<<0。因此虽 route 为 TRANS，
本次采样 **CB2 stage1 转换没有使能**，不能仅凭 route 类型宣称正在走有效页表或必须先做 F4a。
这支持当前未启用转换的状态，但不证明任意新扫描地址都可用；F4 写新地址前仍重查 live 配置、
内存预留/归属和实际扫描可达性。本轮未写 SMMU，也未遍历页表。

## 7. 健康与诊断开销

两档未见 `[ERROR]` / `[WARN]`、真实 panic 签名、mux group isolation/pending 故障、SPMI
poison、DCS 失败或三键/触摸/USB IRQ FAILED。电池仍持续产生 gauge/插拔/充电变化。
**BUG-002 本轮未复现，不等于修复**；BUG-001/002 继续独立跟踪。

`sample-us` 包含读取、比较和日志，不是 memcpy 耗时：

| 档位 | 中位数 | p95 | 最大 |
|---|---:|---:|---:|
| 普通 | 78899 us | 81189 us | 86293 us |
| preserveoff | 78992 us | 80838 us | 85155 us |

存在少量约 6–9 ms 的样本，双峰原因未定。该诊断在 vsync workloop 上每秒执行，开销明显；
不能用本轮动画流畅度代表默认路径性能，常规镜像应关闭或降低取证开销。

## 8. 下一具体动作与验收状态

1. 优先离线核对本项目用户态的 GetBlock 当前模式、模式属性及 swap 完成/等待 ABI。
   本地 `work1/scratch/dsc/` 有共享缓存可继续调查，尚未定位/反汇编目标 framework。
2. 按 ABI 确认 update/disable/background 与持久图层状态，设计有针对性的修复和对照档。
   不把 preserveoff、retain 或硬恢复旧图当作完整修复。
3. 分开核验正常充电过渡 UI 和关机滑条重叠；目前不能声称两者同一根因。
4. F0 取证项已补齐时钟对照、属性、selector 集合、计数使能和 CB2 配置；**根因/接口解释仍未
   闭环，F0 尚未验收，不直接进入 F1**。无需立即重复两档或重新云编译；先完成离线核对。

本轮文档变更用 `git diff --check` 检查；无驱动改动，不重复运行无关交叉编译或硬件测试。
