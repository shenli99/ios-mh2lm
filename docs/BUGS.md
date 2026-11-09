# MH2LM 遗留与回归 Bug 台账

更新：2026-11-09。

这里记录已有功能基本实现后，在后续使用或其他阶段测试中暴露的问题。与当前大目标的实施计划
独立维护：发现 bug 不自动重开原任务，也不自动插入 framebuffer 的步骤或成为其验收前置。
需要安排修复时单独确定范围。若故障使当次测试失去有效输入，应标明该段证据的局限。

当前显示阶段的缺时间、滑条区域半透明效果异常，以及 F0 取证不足，继续由
[显示计划](FRAMEBUFFER-PANEL-PLAN.md)和 [0121 F0 报告](FRAMEBUFFER-F0-0121.md)跟踪。

## 总表

| 编号 | 模块 | 问题 | 状态 | 影响 |
|---|---|---|---|---|
| BUG-001 | DWC3 / USB CDC-ACM | 反复插拔后 Windows 偶发无法识别设备 | 未修复；原因待定 | USB 枚举、连接与实时日志不可用 |
| BUG-002 | SPMI interrupt mux / 电池 / 按键 | drain 后持续 pending 触发 group 隔离，先后失去插线及按键唤醒 | 未修复；直接故障链已确认，pending 原因待定 | 电池事件停止、禁充、百分比停留；按键失效后息屏无法唤醒 |

## BUG-001：USB 多次插拔后无法识别

**背景与发现阶段**

USB gadget（设备侧 CDC-ACM 功能）和 IRQ 收发基本可用；此问题在旧轮询驱动中已经出现，
0114a11 IRQ 测试时再次报告。用户在 0120a6 也明确指出第二次插电脑识别失败是已知 bug。
不能仅凭重连失败将其归因于 IRQ 化。

**触发与现象**

- 手机启动后连接电脑，正常枚举并使用 ACM，然后反复拔插。
- 达到不固定次数后，Windows 提示“未识别的 USB 设备”，后续 ACM 日志连接可能不能恢复。
- 触发次数、线缆/主机依赖、是否必须重启手机才能恢复，尚未形成稳定复现矩阵。

**证据与已确认项**

- 历史日志：`work1/logs/sweetlog-0114a11-usb-irq.bin`，SHA256
  `eb2274f725285048fd27e1c2acde92e68ca052443d47e1a30c1e97ae4731de47`。
  既有分析记录：初期 ACM 配置成功两次，之后有 13 次额外 reset/connect，未再成功配置 ACM；
  真实 DWC3 IRQ 的 hit/ack/rearm 已有证据。
- 详见 [USB IRQ 基线](USB-IRQ-HARDWARE-BASELINE.md)及 `NOTES.md` 的 0114a11 记录。
- 0121 普通档尾部仍可见反复 bus reset/connect 和 `ep0 cmd 0x6 status 1`，
  不能单凭这些条目确定 USB 故障根因，也不能将它们与 PMIC 隔离视为同一 bug。

**待查方向（不是已证实根因）**

旧 reset 路径对端点/传输资源的清理不完整，是既有源码分析提出的候选原因。
`ep0 cmd 0x6 status 1` 也曾出现在可工作的路径中，不能独立作为故障判据。
安排修复时需核对当时版本的 disconnect/reset、EP0 setup 重挂、端点 transfer resource 和
ACM 配置生命周期，并结合主机枚举结果判断。

**当前处理与关闭条件**

不为此重做已经通过的轮询/IRQ A/B；复现时可用持久日志继续取证，不能假定实时 ACM 可用。
关闭前应给出修复提交及实机证据，记录主机/线缆、插拔次数和每次枚举结果，覆盖首次连接、
多次重连、ACM 实际收发；确认没有引入 IRQ、按键、触摸或电池事件退化。
目前无已确认的修复或通用恢复办法。

## BUG-002：PMIC 持续 pending 后电池/按键组被隔离

**背景与发现阶段**

按键 IRQ、电池 IRQ 和 runtime3 基础充电已分别取得功能成功证据。
在后续 framebuffer F0 的 0121 普通档中，用户报告一次插线 UI 未更新，之后息屏再无法通过
插线或电源键亮屏，只能强制重启。作为已实现功能的后续故障独立跟踪，
不据此推翻此前功能成功记录；目前也没有证据证明由显示补丁直接引入。

**证据**

- 普通档：`work1/logs/sweetlog-0121-fb-trace.bin`，SHA256
  `a6fb9e52dc3b55766918e6197f5109b3f0b99c222ed35014b32daea92a05e235`。
- 对照档：`work1/logs/sweetlog-0121-fb-trace-preserveoff.bin`，SHA256
  `fd2ac55aed53ef85aa81071b661b060e5edd92eb8e38bac1283930e8aa1687e6`，此档未记录同类错误。
- 源码版本 d58e7fc / 0170；完整分析、解码行号及证据边界见
  [0121 F0 报告 §3–4](FRAMEBUFFER-F0-0121.md#3-无法唤醒直接故障链已证实)。

**已确认故障链**

1. 普通档解码第 6436 行，`battery-usbin group=1 apid=269 bit=4 status=0x10`：
   mux drain 后仍 pending，判为 stuck，隔离电池 group 1。
2. 第 6463–6467 行，电池 consumer detach；关闭充电命令成功，撤回 external/charging，
   停止有效电池事件更新，保留最后 99%。
3. 第 6618 行，`power group=0 apid=248 bit=0 status=0x1` 同样触发隔离；
   按键 consumer detach、工作线程停止，释放已按下的 HID 键。
4. 第 6635 行 display off；其后触摸、USB 和用户态仍活动，但插线和电源键的事件来源均已失效。
   持久日志未记录 panic，不能将此现象描述为已证实的全内核死锁。

**“pending 堵塞”的准确边界**

现有实现立即 drain 最多 4 遍，末尾仍 pending 就隔离相关 group。
本次没有 ACK 写失败、SPMI timeout 或 poisoned lane 日志；因此已确认的是
**pending 判定触发永久隔离与功能停止**，尚未证明某把锁堵塞或某笔 SPMI 命令卡死。
新边沿、清除传播延迟、持续真实 pending 等仍待区分。
保留像素档一次未复现，不能宣称 preserveoff 修好了这个 bug。

关联的 99%→97%：故障前最后原始 MSOC 为 0xfe（99%），下一启动首样本为 0xfa（97%）。
故障后没有有效遥测，真实消耗与电量计重估的贡献未知；暂不另立一个已定因的电量 bug。

**0121a3 后续复现（2026-11-09）**

`work1/logs/sweetlog-0121a3-fb-contract-baseline.bin`，SHA256
`4285dcace17adcea7ccaba14b2184ec4b1eef00fbb2b9776ee34cad141ac900e`。
第5281–5287行再次触发电池group1隔离，这次是APID271的Type-C RID/CC/VBUS/attach，
bit0/2/4/5、status=0x35；第5314–5319行consumer失败、禁充、撤回输入状态，保留97%。
此次未见按键group0隔离，之后仍有亮屏/触摸活动；没有据此证明全内核死锁。
实验档未复现，但两档时长/操作次数不同，不能把这项差异当0173的修复效果。
详见[0121a3显示实验报告](FRAMEBUFFER-CONTRACT-0121A3.md)。继续独立排期，不扩入显示修复。

**0121a4 第一轮再次复现（2026-11-09）**

`work1/logs/sweetlog-0121a4-fb-contract-proof1.bin`，SHA256
`d6f3989e8511b282ca546fdfe5cdf71a4bfdc2076d38a6fb0d4dbd9638f3d9ff`。
第3165行Type-C RID/group1/APID271/bit0 status=0x1仍pending，第3166–3168行隔离电池组；
第3205–3210行consumer退出、禁充并撤回输入，保留96%。之后无新电池refresh，按键group0
未隔离，后面仍有power IRQ/HID和显示提交。用户确认“插电/UI无反应，电源能唤醒”，
与组隔离边界一致，不认定全内核死锁。
第二轮无同类ERROR/WARN，不能当修复效果。详见[0121a4报告](FRAMEBUFFER-PROOF-0121A4.md)。

**待查方向与关闭条件**

独立安排修复时补逐遍 pending/PMIC 状态和 ACK 时序，区分瞬态与真正 stuck，核对有界
重试/恢复和 group 隔离策略。保持 mux 串行化接口，不用持续输入轮询静默回退。
关闭前需有修复提交及实机长时/重复插拔、按键与息屏唤醒证据，记录次数与时长；
证明电池和按键组持续健康，并验证真实不可恢复故障仍会明确报错及隔离。
当前无软件恢复方案得到实测确认；本次用户通过强制重启恢复。

## 维护约定

新问题追加固定编号，至少记录发现版本、现象、影响、证据、已确认项、待核实项与关闭条件。
状态按“未修复 / 修复待验证 / 已关闭”更新；提交修复不等于实机验证完成。
与已有条目同因时补充复现记录，避免在每一轮主计划中重复建立同一个任务。
