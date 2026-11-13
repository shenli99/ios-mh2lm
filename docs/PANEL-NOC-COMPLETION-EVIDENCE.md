# 0215：display NoC 完成等待取证

2026-11-13。a27 已越过 branch7 的额外 enable 门槛，RSC 时钟租约 Held，
新首错是 display NoC 的第一个 ACTIVE TCS2 请求超时。0215 补齐判断这个超时所需的证据；
它没有修复或证明硬件完成路径，仍需新产物实机验证。

## 已确认的差异与未知项

a27 的 `phase=3 cursor=0/5 group=1 tcs=2 executing=1 operation=0/0 posted=0`
表示程序已完成编程读回并发出 trigger，正在等待完成。它没有打印 IRQ_STATUS、TCS_STATUS、
CMD_STATUS 或时间，无法区分 100ms group deadline 与 1.5s overall deadline，
也无法区分命令未完成与缺失 fresh 证据。`executing=1` 不是硬件接受请求的证明。

核对的 Lineage commit 为 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`：

- [qcom-rpmh-mailbox.c](https://github.com/LineageOS/android_kernel_lge_sm8150/blob/2e897dddf49a4c3e85b2ff77fdf7c70f860398dd/drivers/mailbox/qcom-rpmh-mailbox.c)：
  `rsc_resume()` 将 ACTIVE mask 写入 IRQ_ENABLE；触发时已有 ACTIVE 完成中断。
  `tcs_irq_handler()` 检查本槽 IRQ 和每个命令的 ISSUED/所需 COMPLETE，清理后保持 ACTIVE IRQ。
- [sm8150.dtsi](https://github.com/LineageOS/android_kernel_lge_sm8150/blob/2e897dddf49a4c3e85b2ff77fdf7c70f860398dd/arch/arm64/boot/dts/qcom/sm8150.dtsi)：
  `disp_rsc` 配置 SLEEP1、WAKE1、ACTIVE2；SPI129 对应 GIC INTID161。
  apps 已接入的 INTID37 不能证明 display INTID161 已接入。

XNU 的 `noCQueueGroup()` 先屏蔽自己选中的槽 IRQ，在整个执行等待中保持屏蔽，
清理后才恢复。原完成判据要求 idle、所需 command status，以及本槽 IRQ 或此前观测到
busy/不完整。若 IRQ_STATUS 受 source mask 影响，又在第一次轮询前完成，则可能一直没有 fresh 证据。
独立模拟可复现这个路径，但尚无手册/实机证据证明 IRQ_STATUS 的门控语义或 a27 的实际原因。
旧通用 fixture 无条件注入 IRQ_STATUS，会掩盖这种情况；新专项独立控制 IRQ 和命令状态。

本轮先取证。直接启用 source IRQ 涉及 display INTID161 的处理与所有权，不能把 apps 的修复直接等同于 display。
也不能仅凭旧 CMD_STATUS 的 COMPLETE 放行新事务。

## 实现

[0215 补丁](../xnu/patches/0215-mh2lm-noc-completion-evidence.patch) 只改 SweetMDSS.cpp、SweetMDSSNoC.inc。
SHA256：`454174e71caf1631373c6deac1ce412e955e771cbb80ff2f5219479cddd40351`。

新增私有缓存不参与完成、准入或资源决策。每个请求清空全部证据；每组成功排队时清空组证据、
保留请求起始时间，并保存该组 cursor/TCS/命令计划。若下一组尚未成功排队，日志中的 group-cursor
仍指向最近一组，不能用当前 CONTEXT cursor 代替它。

| 日志 | 含义 |
|---|---|
| `NOC EVIDENCE` | overall/group/write-readback/invalid-status/provider 出口、当前时间、请求/组期限、trigger 软件写入后时间 |
| `NOC REGISTERS stage=trigger` | trigger 写返回后的 IRQ_ENABLE、CONTROL、CMD_ENABLE、WAIT 四次按序读取 |
| `NOC REGISTERS stage=group-timeout` | group deadline 出口、当次正常准入通过后的同组四读 |
| `NOC POLL` | 原轮询的首/末 IRQ_STATUS、idle、时间、次数，以及是否观察到 busy/不完整/本槽 IRQ |
| `NOC READBACK` | 原编程或清理读回的偏移、期望、实际值、时间和 write deadline；IRQ_CLEAR 的 want 是待清位 mask |
| `NOC COMMAND` | 每命令资源号、VCD、地址、计划 msg/data、required 状态位、首/末 CMD_STATUS |

资源号 1–5 依次为 MM1、MM0、SH0、MC0、ACV，0 为 apps 的 CN0。
命令日志的计划值不是额外硬件读回；`status-valid=0`、`valid=0`、`samples=0` 都表示没有样本。
`trigger-issued-ns` 只证明 MMIO 写和原屏障返回，不是硬件 ACK。
采样时间在相应一组读完成后记录，寄存器读取非原子；首/末样本及 seen 标记也不能排除采样间瞬变。

TCS base 是 `0x0af21c00`，stride `0x2a0`，IRQ_ENABLE/STATUS/CLEAR 是全局 +0/+4/+8。
对 ACTIVE2，CONTROL=`0x0af22154`、STATUS=`0x0af22158`、CMD_ENABLE=`0x0af2215c`、
WAIT=`0x0af22150`，第一个 CMD_STATUS=`0x0af2217c`，后续命令 stride `0x14`。

额外 MMIO 仅为每组 trigger 后四读和 group timeout 四读，均在既有 `noCAdmission()` 通过后。
overall 出口位于准入之前，仅打印缓存；权限丢失、stop/晚 timer 不新增读取。
没有新轮询或 IRQ_CLEAR；现有写入、100us timer、1ms读回/100ms组/1.5s请求期限、完成判据、故障保票保持。
日志函数不读 MMIO，成功不输出这些 ERROR，失败新增最多 10 行（5 个命令）；Aborted 不输出新取证。
采样和记时有执行开销，因此模拟时间/轨迹相同不代表实机耗时逐纳秒相同。

## 本地验证

- [新专项](../tools/tests/test-mh2lm-noc-completion-evidence.py)：真实 provider 代码、22 场景，ASan/UBSan 通过。
  覆盖受屏蔽的快速完成、持续 busy、缺 COMPLETE/非法值、独立 IRQ 或观测 busy 后完成、
  overall/group/readback 出口、访问条件丢失、stop/晚 timer、5 命令、ACTIVE3、跨组和跨请求缓存复位。
- 同样场景运行实际 a27 源码：总共只增加 38 组四寄存器读取，其余完整 MMIO 轨迹、返回状态、
  acknowledged/owned、timer 和模拟时间相同。模拟的快速完成无 fresh 证据仍失败，未放宽验收。
- 16 套既有回归通过，包含 Clock/Core/NoC/managed/lifecycle、资源/PHY/供电周期、
  既有取证和 voted halt completion；共 17 套。无需在本轮实机恢复这些扩展测试。
- 真实 KDK ARM64 MDSS 对象编译通过；warnings 1553→1561，新增 8 个有界数组访问
  `unsafe-buffer-usage` 提示，未屏蔽。它们来自最多 5 命令的缓存/表索引，5 命令 ASan/UBSan 已覆盖。
- 0185–0215 共 31 补丁严格重放，无 offset/fuzz；78 文件字节比较、114 原 CI source markers 通过。
- matrix 增加五种 NoC 日志 marker 门槛；实际旧 a27 因缺 `NOC EVIDENCE` 被拒绝，未生成新 boot。
  预计新内核 binary markers 为 191；新下载产物仍需重新验证。

底稿：`scratch/framebuffer-0215/` 的 base/edited/verify/replay、prepare-patch.py、compile.py、checks.py、
replay.py、verification.json、tests.json、新专项输出、warning-comparison.json、cumulative-replay.json、
matrix-rejection.json；a27 原始日志/二进制不入 git。
CircleCI workflow blob 保持 `da0b6c0947a66f03491b3a9023590b49665292df`。

## 云编译和实机判断

用户 push 后云编译，下载 kernel/buildtree（建议编号 0121a28）；agent 核对后沿用 ABI608 固件、ADT、
605 字节 retained 参数打包。现在没有包含 0215 的新 boot，不要重测旧 a27。
测试仍用 retained 的 `fastboot boot`，启动约 90 秒取完整 USB 日志。
a27 的 USB 已足够，补持久日志不能产生该版本未记录的 TCS 寄存器，因此目前不需补提。

先确认 branch7 halt complete 和 CLOCK Held/mask381，再按下列证据定位：

- group-deadline，polls>0，idle=1、所需命令状态齐、seen-busy/incomplete/irq 均0：
  支持缺失 fresh 证据路径。IRQ_ENABLE 本槽0支持屏蔽事实，但旧状态与本轮快速完成仍不能单靠这些值区分。
- idle=0 或所需命令位缺失：有实际未满足的完成条件，追对应资源/VCD与 trigger/配置读回。
- CONTROL trigger 位未保留：先核对该寄存器自清/读回语义，不能直接认定写失败。
- overall-deadline：看 last-ns 与 deadline 的间隔，缓存可能历史；没有 timeout sample 属预期。
- write-readback：按具体偏移和 want/got 分清编程、撤 trigger、清 IRQ 等失败。

无论前面的软件完成是否通过，最终仍以新 DMA 提交、transfer observed、HANDOFF active 验收接管。
完整接管前亮度、OFF/ON、长测及其他显示档继续暂停。
