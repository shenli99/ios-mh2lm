# 0211：RSC clock 超时取证

**2026-11-13：0211诊断目标已由a24 USB日志取得。** branch-wait、当前enable0/OFF0，
整体期限未到。无需补持久日志或重测a24；下一步[0212写后取证](PANEL-CLOCK-WRITE-READBACK.md)待云编译。
见[a24实测](PANEL-D1-0121A24.md#实测使能位未读回)。下文0211待push步骤为历史。

**父链/halt已再次核对：** LG与Lineage均为+0x4004/bit0 enable/bit31 OFF、voted开启仍验halt；
实际父链为PLL1 main→AHB root/8，当前实现有物理读回及typed权限检查。
RCG safe-config启用序列和三路RSC时钟取得顺序存在参考差异，尚未证明是a23根因；
见[父链/halt审计](PANEL-BRANCH7-PARENT-HALT-AUDIT.md)。本次未改内核，不新增编译要求。


2026-11-12，接[0121a23实测](PANEL-D1-0121A23.md)。本轮是诊断增量，**没有宣称修复超时**。
当前先补齐证据，再决定是否需要改硬件时序或其他前置。

## 修改范围与所有者

补丁`0211-mh2lm-clock-timeout-diagnostics.patch`只改实际源码SweetMDSS.cpp及SweetMDSSClock.inc。
仍由SweetMDSS的原gate/workloop/timer处理。每次受理retain/release清空本次诊断缓存；
worker保存其原本已读到的CBCR、已验证的parent CFG/source、实际RMW输入及目标值和时间。
缓存是诊断数据，不参与准入、完成、引用计数、所有权或失败处理。

两个原有Timeout出口各自输出三条ERROR，然后执行原finishLocalClocks路径：

| 日志 | 字段及含义 |
|---|---|
| `SWEET MDSS CLOCK TIMEOUT:` | reason=overall或branch-wait；token、branch、CBCR offset、release/waiting；本次判定now、整次/单分支期限、超过期限的纳秒数 |
| `SWEET MDSS CLOCK WRITE:` | valid、写分支、实际RMW before/want、写所在callback入口时间entry及发出后时间issued；**不是写后读回值** |
| `SWEET MDSS CLOCK SAMPLE:` | valid、current、样本分支、CBCR、bit0 enable和bit31 off、样本时间/年龄、parent-valid及原CFG/source |

`current=1`仅表示样本来自本次callback且分支相符，不代表各寄存器同时采样。
`now`沿用原决策时间，sample/issued在原读写后取时，可能略晚于now；age不足0时显示0。
valid=0表示未取得样本/尚未写入，其他零值不能当成硬件读数。
parent-valid只说明该样本所在callback的原parent检查通过，不是额外硬件探测或新的控制凭据。

overall出口在本次CBCR读取之前，固定current=0，可能只有带时间的历史样本，或valid=0；
不能将其当作超时瞬间的状态。branch-wait出口使用本callback已有样本，不额外读寄存器。
logger只读软件字段，不调用MMIO、provider、父时钟helper或timer。

未增加MMIO操作、未知地址扫描、重试、忙等、fallback或成功路径输出。
保留单分支10ms/整次200ms、100us异步等待以及原ready/timeout判断顺序。
例如单分支期限已到但本次ready且整体未到期，仍按原实现继续；整体期限先到则先Timeout，
不会为了取证在早退出路径追加CBCR读取。

## 下一份实机日志的判断标准

| 证据组合 | 能得出的结论 |
|---|---|
| branch-wait、current1、enable0 | 等待到本次样本时自身使能未读回；若off0，只说明共享时钟ON，不能代替自身票 |
| branch-wait、current1、enable1/off1 | 自身票已读回，但该样本仍表示CLK_OFF；应沿父源/域/分支依赖查，不能单凭它断定哪项依赖错误 |
| overall、current0 | 命中整体期限；结合write/sample时间和late值确定最后已知状态，不能断言硬件持续OFF或增加超时 |
| request/write/sample时间间隔很大 | 回调间隔或软件路径耗时是待查线索；不是硬件持续拒绝的证明，也不足以唯一归因到调度 |
| CLOCK Held且mask0x381 | RSC clock获取完成，随后追踪NoC wrapper/solver/TCS及首次后续ERROR/新DMA；INFO写入行本身仍不算完成 |

完整背景与已确认/未确认原因见[归因汇总](PANEL-BRINGUP-ROOT-CAUSES.md)。

## 验证与下一步

本地验证已完成：

- 新专项14场景通过ASan/UBSan：成功/晚到但ready、丢失自身票、CLK_OFF不清、两种同时发生、
  overall无样本/有历史样本、整体期限优先、请求缓存重置、普通halt/voted撤回失败、父源漂移及stop/晚事件静默。
  与0210基线逐项比较完整MMIO轨迹、返回值、所有权、timer状态和模拟时间，完全一致。
- 另12套显示、NoC、RSC投票及生命周期回归通过，共13套。
- 实际XNU/KDK ARM64 SweetMDSS对象基线/诊断版均编译通过；warnings1547→1549，
  新增2条unsafe-buffer-usage数组提示，未屏蔽，无新增格式告警。
- 0185–0211严格累计重放无offset/fuzz，78源码字节一致，114原CI source markers通过。
- bash语法检查通过；实际打包入口拒绝旧a23内核，明确缺少TIMEOUT marker，未输出boot。
- 补丁SHA256：`be780786c6bb91b6003fb06978297c81d76376fa651f9c01a1b954bc6f479b47`。
  CircleCI blob仍为`da0b6c0947a66f03491b3a9023590b49665292df`。

底稿在工作区`scratch/framebuffer-0211/`：verification/tests/cumulative-replay/warning-comparison JSON、
编译及测试日志、test-clock-timeout-diagnostics-baseline.txt、matrix-rejection.json。
专项为`tools/tests/test-mh2lm-clock-timeout-diagnostics.py`，执行实际provider而非复制算法。
组件测试和单文件ARM64对象不代替整核链接/实机。
打包入口要求上述三个新binary markers，拒绝以旧a23内核生成冒充诊断版本的镜像。
CircleCI配置、ABI608固件、ADT及605字节retained参数保持；不改USB/按键/触摸/电池或SPMI mux。

用户自行push并云编译，下载新kernel/buildtree后先核对再打retained，启动约90秒取完整USB或持久日志。
不安排旧a23重测；完整接管前亮度、OFFON、长测、反复USB及combined/supply继续暂停。
