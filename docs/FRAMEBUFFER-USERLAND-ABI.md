# F0：iOS 16.7 显示接口离线核对

2026-11-08，接续 [0121a2 实测](FRAMEBUFFER-F0-0121A2.md)。本轮使用本地原始共享缓存核对，
没有修改用户态二进制、内核、CI 或设备。F0 尚未验收。

## 症状描述校正

用户澄清：关机界面的问题是**滑条所在位置的半透明颜色未正常渲染**。此前记录的
“滑条重叠”不准确，不能据此诊断控件几何或层级错位。相关报告/计划已校正。
插电唤醒缺时间、取消关机后缺时间仍是另外的观察，不能预设它们与半透明问题同因。

当前送屏路径是单层像素拷贝，不实现图层 alpha 混合。但 0121a2 只观察到 layer0 非零
surface；若透明效果已由 CA 合成进最终 BGRA，内核不需要再次混合。
因此不能仅凭症状给 memcpy 加 alpha 混合，或将历史 CATransformLayer 的
allowsGroupBlending 提示当根因。现有 ROI 只统计 alpha 非零样点，没有滑条专属 ROI、
alpha 分布或操作时刻标记，尚不足以定位半透明效果在哪一层丢失。

## 原始框架与复核底稿

- 缓存：`work1/scratch/dsc/dyld_shared_cache_arm64` 及42个子缓存、`.symbols`。
- `ipsw dyld info` 标识 iOS 16.7、arm64、2710 images。
- cache UUID：`8f970452c9e33e88b114feb1864bc9e0`。
- local-symbol UUID：`51ccba451fc23f19aed047050bdf009a`，与主缓存字段一致。
- IOMobileFramebuffer image：`0x1af222000`；QuartzCore：`0x188284000`。
- 底稿：`work1/scratch/framebuffer-abi-0121a2/`，含符号表、选定函数反汇编、
  `cache_read.py`、`verify-abi.py`、`abi-verification.json`。17项原始指令核对通过。
  地址均为 unslid VA；换缓存版本必须重新定位，不能复用绝对地址。

工具全量符号/ObjC 自动分析在本机 ipsw 版本上尝试异常大分配并失败；后改用仅指定函数的
`dyld disass --quiet` 与只读 mmap 解析，无需加载整套 ObjC 元数据。
本节是本地静态证据，不是新的实机测试，也未读取手机当前 root 来逐字节核验运行框架。

## 1. swap 记录字段已由本版框架确认

| 原始函数 | 关键指令地址 | 可确认的语义 |
|---|---|---|
| kern_SwapEnd `0x1af223530` | `0x1af223548–55c` | object+0x18 为提交记录，selector5，结构大小0x46c |
| kern_SwapSetLayer `0x1af2238a4` | `0x1af2239ec–a30` | null surface 将图层bit同时置入update/disable；非null置update、清disable |
| IOMobileFramebufferSwapSetBackgroundColor `0x1af2242f4` | `0x1af224358–378` | 写object+0x124，即record+0x10c；将RGB量化为字节并加0xff000000；update/disable均置bit31 |
| CA::IOMFBDisplay::set_all_layers_to_null `0x188534b6c` | `0x188534bac–c30` | SwapBegin → 背景黑 → layer0/1/2置null → SwapEnd |

因此 0121a2 的 `update=disable=0x80000007`、surface全0、raw10c=0xff000000
有明确的合法来源：**CA请求清空三个图层并使用黑背景**，不是坏记录或无意义空帧。
当前驱动跳过surface ID为0的图层、忽略disable/background，没有执行这个完整状态变化，
这是已确认的实现缺口。
不能通过丢弃空记录、保留旧source或一律补旧画面修复。

同时，update掩码表达此次修改的图层，不意味着每次提交重建全部图层；重构需维护图层状态。
bit31是背景更新标记，不能当第32个普通surface图层。背景接口此处生成不透明RGB，不能把它
当作关机滑条的局部半透明颜色字段。

## 2. SwapWait 是实际影响资源回收的接口

`kern_SwapWait`（`0x1af223648`）通过 IOConnectCallScalarMethod 调用selector6，
传入三个64位scalar：32位swap ID、32位flags、0。并非纯计时/日志接口。

实际调用链证据：

- `CA::IOMFBDisplay::can_update_()`，`0x1882beb2c–b40`：读取该页swap ID，调用SwapWait；
  返回非零走另一分支，**返回0则清除该页swap ID**，继续更新已完成帧记账及资源回收。
  同路径 `0x1882bebd4–be8` 调用 Surface::unref 并清指针。
- `finish_update()`，`0x1882d1118–1154`：逐页以flags0x80000000检查swap状态，结果影响
  可用页计数；并非只在亮屏时调用。
- `can_update_()`，`0x1882becc4`、`0x1882bed10` 还会用0x80000001；后者专门与
  `0xe00002d5`（XNU `kIOReturnBusy`）比较。
- `is_ready()`，`0x1882f9338–9348` 用id0/flags0x80000003检查ready，成功才推进显示状态。
- 实机日志普通档第2877、2911–2958、3762–3825行可见与具体swap ID相关的等待，
  包括flags1、0x80000000、0x80000001，当前均rc0。

对照当前驱动：SwapEnd只复制提交描述到fPending；timer稍后才取源像素，可能覆盖旧pending；
SwapWait直接成功。由此已确认：**驱动在未完成像素读取时，可以向CA报告等待成功，允许其
推进资源复用；对象retain不能补足这个协议**。这与0121a2捕获的source后续改写相容。

对应0121a2产物源码底稿为`work1/scratch/artifact-0121a2/actual/xnu/`：
`iokit/Kernel/arm/SweetFramebuffer.cpp` 第500行读取pending描述，第564行覆盖pending，
第720行跳过surface ID为0的图层，第941行SwapWait落入空操作分支。

但不能将它直接等同于所有可见异常的唯一根因。以下细节仍须实现前厘清：

- flags低位各完成阶段和高位非阻塞行为；现有证据不能把所有flags当成同一个copy-done。
- id0代表的ready/全队列条件，不能按“0小于最后完成id”直接成功。
- 已覆盖/取消/失败的swap、id回卷及重复等待；缺失记录不能凭大小比较假完成。
- 仅CPU拷贝完成可以释放源像素读取权，但不等同MDP已显示完整帧；硬件显示完成留给F3/F4。

## 3. 当前显示模式：本版输出位置确认，不能随意补常数

`IOMobileFramebufferGetDigitalOutMode`（`0x1af22438c`）：调用GetBlock，block0x40，
输出40字节；成功后将输出+4/+8两个u32交回调用者。
`kern_GetBlock`（`0x1af224cac`）最终使用selector79，scalar为block/offset及额外参数。
这与每次亮屏后实机日志中的请求一致，当前内核在default分支清零输出。

QuartzCore `fetch_current_iomfb_mode()`（`0x1883a7828`）通过动态取得的
IOMobileFramebufferGetDigitalOutMode读取两个ID，与已有mode条目匹配；失败/无匹配可返回NULL。
动态符号名由init_iomfb中的调用和缓存字符串`0x188599672`核对，非按HakiOS猜测。

因此输出结构的用途已确认，但**0是否在当前fallback mode集合中有效、该缺口是否触发本次
UI异常仍未证实**。不能只在+4/+8填1；需要模式属性/ID/画布/格式/刷新率形成一致契约，
并核对原生显示与digital mode的调用分支。这一项优先级低于已证实的假完成和blank语义缺口。

## 4. 半透明效果：不预设缺GPU或必须内核混色

本版QuartzCore有`CA::OGL::SWContext::blur_surface`（`0x188444c08`）、
`make_buffer_current`（`0x188445294`）及通用backdrop逻辑。说明软件路径存在模糊处理能力，
不能以“没有GPU”就断言所有半透明/模糊特效不可能工作。函数存在也不证明本机在该UI状态下
已执行或成功执行它。

需要区分普通alpha混合、backdrop/blur材质、颜色管理、源图复用四种路径。当前缺滑条区域的
源/目标像素证据，不能选择其中之一作为根因；单层送屏也不等同没有用户态子图层合成。

## 下一实施顺序

1. 先完成帧完成/复用契约设计：核对flags、提交/拷贝/退役/ready状态，在workloop/command gate
   内序列化；使用带超时、能释放gate/锁的等待，非阻塞查询按状态返回。不能在持锁等待中阻塞
   执行copy的workloop，不能通过永远成功或永远Busy糊接口。
2. 明确实现update/disable/background的持久状态和合法blank；提交覆盖与资源归还一同处理。
   不将新的呈现行为混入只读取证补丁。
3. 模式查询与注册属性成组核对后再补；独立开关与帧协议修复分开对照，避免一次改动混淆原因。
4. 半透明效果独立观察；若需新取证，在明确的关机滑条操作时采样源/目标局部BGRA与alpha分布，
   避免继续在vsync workloop每秒做约79ms的全套采样。

本轮完成离线ABI核对和症状校正，无新kernel补丁或待push测试版本。下一份驱动修改先按上述
状态/flags补齐设计与本地验证，再组织一次云编译的分档测试；当前不要求重复0121a2。
