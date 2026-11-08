# 0121 F0 实测：显示异常与 PMIC 中断组隔离

2026-11-08。内核实现 d58e7fc / 0170。本轮分析日志和参考源码、修正文档，未修改驱动或 CI。
**结论：F0 尚未完成。普通档无法再唤醒的直接故障链已找到；缺时间和滑条重叠的像素根因未定。**

## 1. 原始证据

| 档位 | 原始文件（work1/logs 下） | SHA256 | 有效字节 / flush / 行数 |
|---|---|---|---|
| 普通档 | sweetlog-0121-fb-trace.bin | a6fb9e52dc3b55766918e6197f5109b3f0b99c222ed35014b32daea92a05e235 | 534424 / 132 / 6743 |
| 保留像素档 | sweetlog-0121-fb-trace-preserveoff.bin | fd2ac55aed53ef85aa81071b661b060e5edd92eb8e38bac1283930e8aa1687e6 | 467153 / 43 / 5983 |

均为 v1 / 4 MiB ring，未回卷、有效区无 NUL。解码、结构化分析和复算脚本位于
`work1/scratch/artifact-0121/`（`fb-trace*.txt`、`log-analysis.json`、`analyze-logs.py`）。
日志最后一个持久刷写点之后的事件可能未保存，不用日志缺项证明没有发生。

## 2. 用户观察及验收修正

- **当前设备未实现自动息屏**，本轮只测手动息屏插线/拔线。删除自动锁屏测试前置，
  不要求用户补测；日志中的 on/off 次数不等同于完整插拔测试次数。
- 息屏插电：亮屏，显示大电池图标，图标消失后可见状态栏电量、相机图标和闪动白色横条，
  中间没有大时间。黑色背景本身不作为故障判据。
- 息屏拔电：亮屏，除了上述组件，还有大时间，进入看起来完整的锁屏。
- 关机界面：滑条与后面的图形重叠，难以辨认；取消后也出现缺少时间的界面。
  不能把这些现象归因于主题，也不能仅凭描述断言是哪一层渲染错误。
- 普通档后段一次亮屏插线没有 UI 更新，随后手动息屏后插线/电源键均无法再亮屏，用户强制重启。
  保留像素档未观察到相同失效。一次对照不能证明 preserveoff 修复了 PMIC 故障。
- 关于 iPhone 正常行为：充电瞬间的临时充电界面与稳定锁屏要区分；本轮未取得同版原机对照。
  尝试访问 Apple iOS 16 用户指南时，本机代理 127.0.0.1:7897 不可用，直连 DNS 失败，
  因而**未完成独立核验“大电池消失后在此版本/状态是否立即恢复大时间”**。
  用户的预期作为待核验项，不把推测当正常行为标准。

## 3. 无法唤醒：直接故障链已证实

普通档 `fb-trace.txt`：

1. 第 6436–6439 行：`battery-usbin group=1 apid=269 bit=4 status=0x10` 在 drain 后仍 pending，
   mux 判 `unhandled/stuck`，隔离电池 group 1。
2. 第 6463–6467 行：consumer 收到失败后 detach；`op=0 value=0 error=0` 关闭充电；
   `SWEET BATT IRQ: FAILED ... no polling fallback`，撤回 external/charging，保留最后 99%。
3. 第 6618–6624 行：`power group=0 apid=248 bit=0 status=0x1` 同样触发隔离，
   按键 consumer detach，工作线程停止并释放已按 HID 键。
4. 第 6635 行收到 display off；此后仍有触摸、USB bus reset 和用户态 exec 日志，
   但电池插拔和电源键两组已不可用，吻合两种方式都无法再唤醒。持久日志无 panic 记录。

保留像素档无 `[ERROR]`；记录 8 次 display off、9 次 display on（含启动 on）。
普通档 8 次 off、8 次 on（含启动 on，最后停在 off）。

**已知机制，不等于底层根因已定：** `SweetSpmiInterruptMux::drainPending()` 只立即扫描/ACK 最多
4 遍，然后仍 pending 就永久隔离相关 group。日志没有 `ack failed`、write timeout 或 poison，
不能说 SPMI 命令失败；也没有逐遍 PMIC latch/RT 状态，不能区分新边沿、清除传播延迟和真实 stuck。
`ackRoute()` 的 channel IRQ_CLEAR → PMIC LATCHED_CLR 顺序与 LG
`drivers/spmi/spmi-pmic-arb.c:590` 的 `qpnpint_irq_ack()` 一致，没有证据支持直接反转该顺序。
0170 未修改 mux，但增加诊断开销可能改变调度，尚不能证明触发条件。

后续应独立处理 mux 的持续 pending 判定/有界重试及故障现场；保持 group 隔离、事务串行化和
无输入轮询回退，不用恢复轮询掩盖问题。显示取证阶段必须同时检查两组健康状态。

## 4. 99% → 97%：原始电量计值发生变化

普通档最后 gauge 记录为 `msoc=0xfe ... raw=99% src=gauge held=0 -> 99%`（第 6433 行），
随即电池 group 失效，后续没有有效新样本。第二档首样本为
`msoc=0xfa ... raw=97% src=gauge held=0 -> 97%`（第 113 行），之后又读到 98%。
不是本轮 framebuffer 改写百分比，也不是从启动默认满电切换；但故障后到下次启动之间没有
有效采样，无法仅凭这些日志判断真实消耗、估算重整各占多少，也不能归因于刷下一个镜像。

## 5. 显示 A/B：息屏擦除确实存在，但不足以解释全部现象

- 普通档首次 off：panel hash 从 `92b2f8e159fa7514` 变为 `47fdad21bd6411d3`；
  首次 on-after 仍为后者（第 3523、3558、3695 行）。
- 保留像素档首次 off-before/off-after/on-after 均为 `92b2f8e159fa7514`
  （第 3381、3388、3535 行）。说明 preserveoff 参数确实阻止了这次息屏擦除。
- 之后两档多次均出现 `47fdad21bd6411d3`；用户仍描述插线缺时间、拔线有时间。
  因此不能继续将“息屏擦黑”写成这些显示异常的完整已证实根因。
- 最近四份记录反复包含 `mask=0x1` 的 surface 2/3，以及 surface 全 0 的
  `mask=0x1` / `mask=0x80000007`。现有 `swap()` 无条件覆盖 fPending；
  `present()` 遇到 id=0 跳过，不解析 +0x108 disable mask、背景色或持续图层状态。
  重放最新记录可能无任何有效像素可拷贝，这是源码可证的缺口；尚未证明它造成此处缺时间。
  UniXNU 的 `drivers/GopFramebuffer/NativeLegacyRequest.h` 将 update、disable 和背景伪图层
  分开解释，HakiOS 的 `MiniIOMobileFramebuffer.cpp` 在提交时保留 surface 引用，值得参考；
  这些参考不能替代本机记录中的字段核对。
- 关机滑条重叠、取消后缺时间也可能涉及用户态合成/alpha/帧复用。当前只有文字记录和
  稀疏校验，没有对应稳定画面的 source/scanout 像素证据，未定因。

### 0170 取证限制（下轮 F0 必须补齐）

1. hash 对行 900–1400 每 8 行、每 64 字节只读一个字节。对 BGRA 即仅采蓝色分量，
   还扫到 source 行 padding；没有覆盖上方时钟，不能证明整帧或时钟区域是黑的。
   复算全零采样值：stride 4320 → `47fdad21bd6411d3`，4608 → `df099a7c56173623`。
   两个 hash 采样几何不同，不能直接据其不相等判断拷贝错误。
2. source 从 ring 槽位 0→3 选择，未按时间遍历且 id=0 会覆盖前一 id，造成多数缺少 source
   日志；也未记录“真正被 present 的 surface”。应记录提交、实际呈现和稳定唤醒画面三个时点，
   做相同逻辑区域（考虑裁剪）的 BGRA 统计、时钟/中心/滑条区域采样及边界验证。
3. delta 的所谓 frame 取自 +0x2b4，实际上是 AUTOREFRESH_CONFIG；真正 FRAME_COUNT 是 +0xac。
   +0x2b0 为循环行计数，不能对 1 秒两次读数简单相减得到 TE 频率。此次 TE 频率**未测得**。
4. 不应先过滤 id=0 或猜测 `0x80000007` 的行为来“修图”；应补 update/disable/bg/flags
   字段，再决定是否需维护持续图层状态和正确 blank 语义。

## 6. 硬件取证及后续约束

两档探针一致：VIG0 size/out `0x9240438`，xy=0，addr=`0x9d200000`，stride=`0x10e0`，
format=`0x237ff`，unpack=`0x3020001`，op=`0x80000000`，addr-status=0。
INTF1 TE-en=1，autorefresh=`0x80000001`。这些是继承值，不是已验收的自有缓冲配置。

apps SMMU：ID0=`0x4c017e5e`、ID1=`0x60000038`，SMR index3=`0x84200800`，
S2CR=`0x2`，type=0、CB=2。LG `arm-smmu-regs.h` 定义 type0=TRANS、type1=BYPASS，
所以当前 stream 路由配置**不是 BYPASS 类型**。仍需读 CB2 SCTLR 等有效使能/页表配置，
才能确认实际转换状态及可访问范围；不能因 SRC0_ADDR 看似物理地址就假定任意新物理页可扫描。
DTS `iommus=<&apps_smmu 0x800 0x420>` 的 0x420 是 stream match mask，不是第二个 SID。

CA 的 surface 2/3 均为 1125×2436、BGRA、bpr4608、alloc11239424、cache 属性 0x400、offset0、
parent0；未带 `IOSurfaceIsDisplayable`/`IOSurfaceIsGPUAddressable` 键。仅创建日志未列完整 11 个键。
两档可见 selector 3/4/5/6/8/12/15/17/22/27/28/29/33/50/55/57，72 无调用；大于 63
的调用仍只有旧日志，0170 不提供完整参数追踪。不可用此宣称所有 selector 都已实现。

下一步先补 F0 证据和 mux pending 诊断/恢复设计；不进入 F1 或直接自有缓冲翻页。
F4 前必须明确 SMMU 映射路径；32 位物理分配约束无法替代设备地址映射。
