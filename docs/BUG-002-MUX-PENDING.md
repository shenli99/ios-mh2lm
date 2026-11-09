# BUG-002：末尾新 pending 被误判为永久故障

2026-11-09；0121a9 F2 长测期间独立排查。后续0178 / `41a0efa`完成代码修复，本地回归通过；
用户决定后续随正常使用观察，不安排专项复测。尚无新内核实机修复证据。

## 结论

**已定位的软件缺陷：mux 把一次 drain 结束时仍有 pending，当作无法恢复的中断故障，
立即永久隔离相关 consumer group。它没有要求这个 pending 在本批次被 ACK 过。**
因此正常的新事件也能使整个电池组退出，随后禁充、撤回输入状态、停止电池更新。
0121a9 的 CC 故障就是“本批次没有 ACK CC，末尾才观察到 CC pending”的路径，
不是“CC 连续清了四次仍清不掉”。实际 mux 源码的 host 复现也证明该路径成立。

这定位了事件失效的软件原因。真机末尾 CC 位是新边沿还是状态传播延迟，仍不能从旧日志
区分；同样不能宣称已经排除所有 PMIC 硬件异常。历史所有 BUG-002 个案不能自动据此逐一
定因，但共享这一错误判定，先前“持续 pending”措辞不能作为硬件持续卡住的证据。

## 0121a9 的实机证据

原始文件 `work1/logs/sweetlog-0121a9-fb-power-dcs.bin`，SHA256：
`f02beab6ad0b0d9fbf2882ba0d73ae3a8205fbd49c22c1910ba120a7df4810ca`。
有效 372499 字节，未回卷；解码文件
`work1/scratch/artifact-0121a9/fb-power-dcs.txt`。

| 解码行 | 内容 | 含义 |
|---|---|---|
| 4689 | USBIN UV，irq/work=87，总 hits=123 | 上一批累计 ACK 成功次数 |
| 4690–4693 | CC/APID271/bit2/status4，handled1/pending1，隔离 group1 | 本批末尾判断并撤销电池事件 |
| 4694 | USBIN，irq/work=88，总 hits=125 | 本批处理过的第一条 route |
| 4695 | CHGR，irq/work=88，总 hits=125 | 本批处理过的第二条 route |
| 4722–4727 | consumer detach、disable actual0、输入撤回，保留93% | 永久失去更新的下游后果 |

**计数的关键点：** 源码仅在 `ackRoute()` 成功后增加 `routeCount`，123→125 只有两次；
本批有 USBIN/CHGR 两条 hit，没有 CC hit。电池 route 的 hit 不受按键日志预算抑制。
ACK 失败会走另一条明确报错的退出路径，本次未出现。
所以这次 drain 中 CC 没被 ACK；不是隐去了四次 CC ACK 日志。
两次成功 ACK 也意味着最多两遍非空扫描加一遍空扫描，不是耗尽四遍的案例。

`drainInterrupt()` 先运行 `drainPending()`，再打印所有 batch；所以隔离日志位于两条 hit
之前，不表示隔离后又恢复了 USBIN/CHGR 的 ACK。已有采样 id43 在途完成也不表示恢复。

## 源码位置与错误判定

按 0121a9 云产物的实际源码行号核对：
`work1/scratch/artifact-0121a9/actual/xnu/iokit/Kernel/arm/SweetSpmiInterruptMux.cpp`，
SHA256 `1ae1241cbba465316e5f7982a4c6d4c37e622dbf44136235d136e962f9073a68`。

- 551 行 `drainPending()`：每遍遍历 active route，最多四遍，空遍提前结束。
- 604 行：ACK 成功后才累加总 hits；527 行：只有按键 hit 受预算限制。
- 618–625 行：另一次全表扫描决定 `stillPending`。
- 626 行：`if ((!handled && !maintenance) || stillPending)` 直接进入故障路径。
- 628–643 行：再次扫描收集 group，调用 `quarantinePendingGroups()`。
- 923 行：将该 group 全部 route 标记 inactive，关闭 APID，然后通知 `SWEET_MUX_FAILED`。
- 543–547 行：batch 日志在上述决定之后打印。

窗口可以小至：一次空遍刚读完 CC=0 → 新 CC 到达 → 末尾复查 CC=1 → 永久隔离。
这个 CC 没有得到第一次 ACK。单纯增加扫描次数无法消除“最后一次扫描之后”的窗口。
此外，四遍都 ACK 成功而第五个新事件又到达，也会被同一条件误判为 stuck。

历史来源：0134 / `e393921` 已有四遍后仍 pending 即故障的策略；0139 固定 IRQ 后取消
轮询回退；0158 / `1023575` 增加按 group 隔离以保护其他消费者；0167 / `357b26f`
调整 ACK 优先级和 batch 通知，仍保留此末尾判定。不是 F2 的 DCS28/29 新引入的条件。
这也不推翻 ACK 优先级对遥测竞争的改善：本次失败位根本未被尝试 ACK。

## LG 参考核对

参考 `work1/ref/android_kernel_lge_sm8150`，当前 commit `d4f335e13`：

- `drivers/spmi/spmi-pmic-arb.c:590`，`qpnpint_irq_ack()` 同样先清 arbiter IRQ_CLEAR，
  再写 PMIC INT_LATCHED_CLR；没有发现本次故障能归因于两者顺序颠倒。
- 505 行 `periph_interrupt()` 按当次 status 快照分发；526 行 chained handler 完成后
  退出上级中断处理，没有“收尾还有 pending 就永久删除 consumer”的对应策略。
  新事件由中断机制继续处理。这是行为参考，不要求引入 Linux irq_domain。
- `arch/arm64/boot/dts/qcom/pm8150b.dtsi:295`：Type-C bit2 是 rising edge，
  bit0/4/5 rising，bit3/6 both；与当前 mux 路由表一致。
- `drivers/power/supply/qcom/smb5-lib.c:6732`，CC handler 更新 Type-C 模式并通知
  power_supply；没有额外手工清 PMIC IRQ latch。不能据此要求电池 consumer 绕过 mux
  再写一次清除寄存器，ACK 本来就在中断控制层。

## 可重复的 host 证明

仓库内执行：

```bash
python3 tools/tests/repro-mh2lm-mux-pending.py \
  /home/shenli/work1/scratch/artifact-0121a9/actual/xnu
```

复用现有 consumer 测试框架，编译实际 mux；只替换 host MMIO 和事件到达时刻。
不修改被测 kernel 源码；带 ASan/UBSan，环境限制下 LSan 关闭，保留已有对象/映射清理检查。
本工具是**已知缺陷特征复现**，输出 REPRODUCED 不是驱动健康验收；修复后应改成健康/恢复
断言，不为维持此复现而保留错误行为。

| 情景 | 同批 CC ACK | 结果 |
|---|---:|---|
| 对照：5个 CC 分五次正常分发 | 共5次 | 无隔离，按键继续工作 |
| USBIN/CHGR 已处理，空遍最后一次 Type-C 读取后注入单个 CC | 0 | 立即隔离 group1，按键仍工作 |
| 每遍 ACK 成功后再到一个 CC，四遍后剩一个新 CC | 4 | 立即隔离 group1，按键仍工作 |

两种缺陷场景都只有一次 GIC dispatch，未触发256次/秒风暴阈值；没有模拟超时、
poisoned lane 或锁故障。输出底稿：`work1/scratch/bug002/committed-reproducer-result.txt`。
它证明了软件会错误隔离可处理的新事件，不模拟真机 CC 电气时序，也不证明硬件没有其他问题。

## 原修复规划（历史；实际范围以下节0178为准）

1. 已注册 route 的末尾 pending 应作为待继续处理的工作；保留有界执行，允许重新排队/
   重新进入中断，不能仅凭该次 pending 就撤销 consumer。不能只把四遍改成更大的数。
2. 新事件/批次预算耗尽、ACK 传输失败、未知未注册位、真正的持续过载分开处理与记录。
   预算耗尽不是 ACK 失败；真正不可恢复的传输故障仍保留 fail-closed 和明确 ERROR。
3. 后续工作必须有调度公平性和每组速率/时间约束，防止持续活动令 mux workloop 独占；
   不能只依赖 GIC dispatch 计数约束软件续批。诊断补充批次数、逐遍 ACK mask、末尾 mask
   和时间；不得把“相同 bit 再出现”单独当硬件卡死证明。
4. 保持 typed IOService consumer 接口、mux 串行化及 IRQ ACK 优先级；不加输入轮询回退，
   不让 consumer 穿透读写 IRQ 寄存器。stop/detach 必须能取消所有续批。
5. 修复验收覆盖末尾新边沿、四遍后新事件、真实 ACK 超时/隔离、stop 与续批竞态、按键并行；
   上机验证重复插拔、息屏唤醒和长时组健康。不得把 host 复现视作修复已上机成功。

最初诊断阶段仅补复现工具和台账；当时无内核/CI/boot 改动。0121a9 F2 长测使用原镜像，
其显示验收与 BUG-002 的修复状态分别记录。


## 修复记录：0178 / `41a0efa`

按用户明确限定，仅改`drainPending()`收尾逻辑：

- 局部汇总`drainMasks[0..3]`为`everAcked`，末尾采集`pendingRoutes`（route索引位图，
  不是PMIC peripheral的bit位图）。
- `pendingRoutes & everAcked`与`pendingRoutes & ~everAcked`分别表示该批ACK过、未ACK过
  而收尾仍观察到的route。两类均不能证明不可恢复故障，正常rearm level GIC，留待下一次drain。
- 错误分支收窄为`!handled && !maintenance && !stillPending`；上述pending不会增加
  `faultCount`或进入group隔离。原空且未处理/非维护异常保留，隔离调用点保留。
- 四遍扫描、ACK返回语义、ACK失败`disableHardware()`行为完全不改；无新成员/全局变量，
  无软件续批/新timer/consumer接口调整，无CI/USB/显示代码变动。

`ACK成功+status置位`仅被视为可以继续处理，不能凭此独立证明物理新边沿。连续活动但
ACK成功的硬件边界按用户要求本轮暂不单独识别；原有256次/秒风暴保护仍在。

**本地结果：** strict apply及收尾之外字节一致；实际源码consumer suite（包含timeout
poison/disable）和80次生命周期竞态通过。专门的ASan/UBSan回归结果：

| 情景 | 首次drain后 | 下一次drain后 |
|---|---|---|
| USBIN/CHGR处理后CC晚到 | CC ACK0次，保留pending，无隔离 | CC ACK1次，pending清除 |
| 完全空扫描后CC晚到 | 无ACK，保留pending，无隔离 | CC ACK1次，pending清除 |
| 四遍各清一次后第五个CC到达 | CC ACK4次，保留pending，无隔离 | CC ACK5次，pending清除 |

这些场景的`faultCount`均0，按键继续运行。host不模拟enable后level信号的硬件递送，
测试显式发出下一次GIC callback；不把此结果记为已上机。LSan受环境限制关闭。
底稿`work1/scratch/bug002-fix/{make-patch.py,verify-pending.py,pending-regression.txt,consumers.txt}`；
仓库旧`repro-mh2lm-mux-pending.py`仍是针对修复前源码的缺陷特征复现，不应用其隔离预期验收0178。

**用户决定：** 不安排BUG-002专项复测，后续随正常使用观察；如复发由用户反馈再排查。
不再以本文历史专项测试清单阻塞显示路线，不宣称已取得0178的新内核实机修复证据。
同日0121a9长测无隔离，但该镜像不含0178，不能拿来证明修复部署或有效。
