# 批次5：客户端叶子掩码（0121b30 候选）

基线为已验b29/0312a41，验收见[第4批](GPIO-IRQ-STAGE4-MMIO-ORDER.md)。遵循[固定计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)，不同时删除dispatch短门控，不移动claim/prepare时机，不新增stray全pad普查/清扫，不改显示流程。

## 范围

- 普通AGPIO::maskInterrupt只关本pad ENABLE、标记本slot，不在worker等待期间保持整条summary关闭。
- claim仍provider enable一次后mask；prepare仍在worker原arm位置，不开线；第一次真实resume设置summary armed。之后其它未resume/deferred的普通client不会阻塞peer。
- dispatch入口在取得快照引用的同一IRQ锁事务中增加dispatch depth并短暂mask；尾部减depth再尝试unmask。worker提前resume不能绕过尚未结束的dispatch gate。没有活跃slot或controller stopping时不会开线，最后slot注销不能被尾部重开。
- 新注册slot清自己的继承ENABLE，使硬件状态匹配client-masked，不让固件遗留ENABLE在peer开线时贡献无人服务的中断。保留触发位至原prepare位置，非提前触发配置或全pad清理。
- handler曾mask后，另CPU worker可能在handler返回前resume。新增每slot mask epoch；收尾若发现本回调期间发生过mask交接，就不替workerACK，避免清掉新包。此用例先失败后修正，保留deferred锁存。
- island/只读pad不能由AP强行关ENABLE，沿用原fail-closed整线安全行为，使用独立hold位与普通client mask分开。原owner重新可写并确认mask/resume后才可清hold；带hold的已注销slot不得复用，直到最后summary释放重置。peer或软件probe不能绕过hold。这不是新stray隔离/自动恢复机制。

## 验证

真实TLMM控制器用独立双设备aggregate模型（ENABLE && status贡献summary）：

1. A deferred、A锁存为1、ENABLE0时，B仍可派发且A不重复回调；A锁存未被B消费。
2. A/B都masked时summary开但模型无贡献，不重复投递；A恢复可继续，B锁存保留至B恢复。
3. 未resume的late slot不阻塞peer；包括固件继承ENABLE1的late注册，slot硬件被关且peer继续。
4. fast worker在callback返回前resume并模拟新status，dispatch期间line保持mask，新status不被尾部ACK，下一包正常派发。
5. deferred/unregister/replacement及既有两线程snapshot排空、summary/pad写序用例继续通过；普通leaf mask的最终summary期望更新，不能用旧“所有client都resume才开线”的断言冒充正确行为。
6. 运行期EGPIO接管时无pad寄存器写，peer保持不开线；owner恢复可解除hold；带hold注销后新slot/probe仍不能绕过。

同一leafCases替换b29生产实现，在A deferred后gicdLineEnabled断言失败。初次扩大fixture暴露OSObject分配未模拟内核零填充，已仅在host OSObject::operator new修正模型；ASan/UBSan诊断区分了该夹具问题与真实leaf阻塞，不往生产代码加入无关改动。

TLMM、真实C worker/service、panel-lifecycle、display-service四套回归PASS；TLMM ARM64 TU成功；marker151/0，diff --check通过。fixture的AGPIO模型同步为leaf mask；C worker执行代码、SweetTouchPanel、GenericInterruptController与MDSS显示执行源不变。头文件合同同步。host模型不模拟真实TLMM同步延时、GIC持续电平、固件抢占或长期负载，不能用host多pad通过等同实机多pad验证。

## 实机门槛与后续

候选b30尚无CI/设备结果。工作区out-bs/mk16k-0121b30.sh已准备，新增leaf模式标记门禁；真实b29被拒，未伪造新镜像。下载后须逐文件比较构建树、校验内核/固件/ADT和最终boot内容。

用户实机验收：mode3桌面、连续点击/滑动/多指与全部抬起、两轮普通OFF/ON及醒后触摸，导出原始sweetlog。保留b29已验镜像。当前只有一个硬件触摸pad消费者，因此普通设备测试证明gpio122在leaf mask下正常，并非第二颗GPIO的硬件并行证明；host双client结果独立记录。

stray仍是既有计数/DEBUG，持续未知贡献者可能在EOI后重复投递；本批不借异常处理增加全pad扫描。完整诊断/隔离/恢复留第6批。发生卡死/复位须先看本次正确解码日志，不把其自动归因为孤儿pad或handler重入。b30验收前不推进第6批或显示降级生产改动。
