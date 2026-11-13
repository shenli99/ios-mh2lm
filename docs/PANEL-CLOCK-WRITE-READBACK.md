# 0212：branch7 写后读回与观测历史

**实测状态：** [0121a25日志](PANEL-D1-0121A25.md#实测写后及等待均未观察到使能)已分析：PA正确，立即读回及39次等待均未见enable。
六条诊断完整，无需补持久日志或重测；局部复位/写入语义仍待核对，未证明硬件修复。
后续[0213局部状态/复位取证](PANEL-RSC-LOCAL-RESET-EVIDENCE.md)已提交`7f4f55a`，本地通过、待云编译。

2026-11-13。a24 的 USB 日志已取得所需证据，无需补持久日志。首错为单分支等待超时，
当前 CBCR=0，尚未看到自身 enable；具体原因仍未确定。本轮继续定位写入未保留的原因，未宣称硬件修复。
实测时间及原始行号见 [a24](PANEL-D1-0121A24.md#实测使能位未读回)。

## 已核对的写入路径

- 实际 a24 内核 `SweetMDSS::dp_wr` 反汇编为 `dmb oshst` → `str w2,[x0,w1,uxtw]` → `dsb sy`。
  不是缺少 store 或写后屏障。
- ADT 的 DISPCC 窗口为 PA 0x0af00000、长度 0x20000，生产代码同时校验物理基址与 v1 长度。
  这证明描述符范围覆盖 +0x4004，不代替运行时 CPU 页表翻译证据。
- LG、Lineage 和小米源码均定义 NON_GDSC_AHB enable/halt 为 +0x4004、enable bit0；
  SM8150 v2 fixup 未改这些 CBCR 偏移。LG 的 `clk_is_enabled_regmap` 同样读取 enable_mask，
  没有找到 bit0 只写或自清的依据，不能把读零直接作为成功。
- 本机 Mu ClockDxe 二进制中的三个时钟名指针所在记录，也分别含地址
  0x0af04004 / 0x0af0400c / 0x0af04008。原文件 SHA 与记录偏移见本地审计 JSON。
  这里只核对表中地址，未反编译其完整启停策略。
- RSC VSYNC/AHB 开启顺序及 root safe-config 的参考差异仍是候选，未证明它们会清除此使能位。
  本轮没有混入顺序或父时钟修改。

源码依据：LG `drivers/clk/qcom/dispcc-sm8150.c:1408/:1642`、`clk-regmap.c:29/:58`、
`arch/arm64/boot/dts/qcom/sm8150.dtsi:1476`；父链说明见 [专项审计](PANEL-BRANCH7-PARENT-HALT-AUDIT.md)。

## 代码与记录

`0212-mh2lm-clock-write-readback-evidence.patch` 仅修改 SweetMDSS.cpp / SweetMDSSClock.inc。
在已有 voted + managedAHB 分支（当前仅 branch7）的实际写入及 DSB 后，增加一次同一 CBCR 的读取，
记录时间，并用 `ml_vtophys` 记录该访问地址的 CPU 物理翻译。取得或撤回该票都适用。
其他分支无额外寄存器访问；没有新增映射、寄存器扫描、重发或寄存器写入。

等待历史复用原 worker 每次已读取的值：次数、首次值/时间、是否曾读到 enable/OFF、最后读到 enable 的时间。
每次请求清空缓存，每次实际分支写入重置该分支的等待历史。RSC 三路准入值来自原 preflight，
只在 RSC retain 成功受理后标有效；release 不伪造一份新的准入样本。

两个原 Timeout 出口在原 TIMEOUT/WRITE/SAMPLE 后各增加三条 ERROR：

| 日志 | 含义 |
|---|---|
| `CLOCK READBACK` | valid、branch、pa、写后一次 CBCR/enable/OFF 及采样时间；pa 预期为 0x0af04004 |
| `CLOCK OBSERVED` | 同一写分支的 wait-samples、首次等待样本、seen-enable/seen-off、最后读到 enable 的时间 |
| `CLOCK RSC PREFLIGHT` | valid 及 NON_GDSC_AHB / RSCC_AHB / RSCC_VSYNC 的原准入值，明确不是失败现场的新读值 |

原单分支 10ms、整体 200ms、100us timer、ready 判定顺序、失败保票及 DMA 准入均保留。
诊断数据不参与控制，logger 不读硬件、不调用 provider、不改 timer；成功运行不增加这些日志。
新读回也不会直接变成 ACK 或 Held。

## 下一份日志如何判断

- 写后 enable=1、等待最终 enable=0：本次确实至少读到一次使能，随后丢失；仍不能仅凭它指定清除者。
- 写后 enable=0、等待 seen-enable=1：使能较晚出现过，结合首次/最后时间及最终值判断。
- 写后和等待均未观察到 enable：尚无软件票读回证据，优先沿访问映射、权限、局部域/复位及写入语义查；
  不能证明两个采样之间从未短暂置位，也不能直接归因 secure world。
- pa=0 表示未得到可用翻译；非预期 PA 是映射问题线索。正确 PA 只证明 CPU 翻译，不证明总线接受写入或拥有硬件权限。
- wait-samples=0 表示没有写后等待样本；valid=0 的字段不能当硬件读数。OBSERVED 不包含紧随写入的那一次读取。
- 若 CLOCK Held/state2/mask0x381，则继续沿 NoC 首错或新 DMA 判定接管进展。

## 验证与交付

- 新专项 19 场景通过 ASan/UBSan，含三种 enable 历史、两类超时、其他分支故障、撤回/缓存重置、
  翻译无效/异常、stop/晚事件。与真实0211基线比较完整 MMIO：只允许每次 branch7 写后多一条同地址读取；
  其余 MMIO、返回值、所有权、timer 状态和模拟时间一致。此比较不等于实机运行耗时完全不变。
- 共 14 套显示/时钟/NoC/生命周期检查通过。
- 真实 XNU/KDK ARM64 SweetMDSS 基线/修改版对象编译通过；告警1549→1552，新增3条 unsafe-buffer-access，未屏蔽。
- 0185–0212 严格累计重放无 offset/fuzz，78 份源码字节一致，114 项原 CI source markers 通过。
- 打包入口要求三个新 binary markers；实际旧 a24 被 READBACK 门槛拒绝，无镜像输出。bash 语法通过。
- 补丁 SHA256：`58c3076bb016b038ba7f4001f42f5735332b6898eadcba912233334e7a9ed326`。

本地底稿在 `scratch/framebuffer-0212/`：verification/tests/cumulative-replay/warning-comparison JSON、
test-clock-write-readback-baseline.txt、write-path-audit.json、a24-dp-wr-disassembly.txt、matrix-rejection.json。
USB 原始日志不入 git。本轮不修改 CircleCI、固件、ADT、boot args、USB/输入/电池及 apps RPMh IRQ。

0121a25 已确认包含本补丁：78源码字节匹配，114 source/183 binary markers通过，同参数retained已打包回读。
a25实测未通过接管，已取得所需写后/等待证据。后续取证范围及静态审计见[a25实测](PANEL-D1-0121A25.md#实测写后及等待均未观察到使能)。
a24/a25无需重测，完整接管前亮度/OFFON/长测及其他显示档继续暂停。下一份日志使用0213新编译产物。
