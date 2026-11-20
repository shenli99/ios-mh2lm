# 批次2：所有权收口工作记录

基线：830fa52；批次1记录为 60e1906，2a释放边界为66b7f82。执行顺序与验收门禁见 [固定计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)。本记录只说明本地进展，尚未实机通过。

## 2b：worker 的 pad 中断寄存器写入

| 路径 | b22 原写入者 | 当前 delegated mode3 | 保持的时序 |
|---|---|---|---|
| claim/register | TLMM路由配置，worker后配触发 | 仍只由TLMM认领路由，记录detect | 不提前触发配置到claim，不加普查 |
| arm准备 | worker INTR_CFG低8位配置和清旧status | AGPIO::prepareInterrupt，控制器核验持有者、注册slot、masked、线程上下文、island | 在worker原arm位置执行，仍不开pad/summary |
| arm使能 | hook resume后worker再次写ENABLE | 仅句柄resume写ENABLE；worker通过句柄读回 | 原resume位置及b22整线门控保留；不再有第二个裸写者 |
| 正常ACK | delegated ack hook，缺hook则裸写 | 只允许句柄ACK；缺hook拒绝操作 | 不提前消费deferred锁存 |
| fail | worker直接清ENABLE | 仅句柄mask | 仍先发布失败状态，再mask |
| park/quiesce | worker直接清ENABLE并调用mask hook | 句柄mask、句柄CFG读回 | 原drain预算、GIC观测及显示fence不变 |
| resume验证 | worker读CFG、旧arm重配 | 句柄读回与prepare | 原TCM protocol和几何检查不变 |
| worker退出 | 退出时不总是mask | delegated退出时要求句柄mask | 不留下失去worker的可写pad |
| service stop/free | 清钩子后取消delegated标记 | 标记保持delegated直到重启，钩子撤销后不恢复裸写权限 | teardown并发排空尚未声称解决 |

prepare 的触发类型来自注册slot；支持high/low/rising/falling/both，低8位输出分别0x72/0x70/0x76/0x74/0x7c，保留高位。prepare禁止IRQ上下文、未持有/未注册、仍unmasked和island拥有的pad。mask/resume/ack在IRQ可用的检查中也拒绝运行期EGPIO接管，不拿睡眠锁；mask必要时仍关summary但不写island pad。

## 本地验证

- TLMM真实控制器测试覆盖prepare在claim之后仍不开线，五类触发编码、高位保留、锁存清理、未注册/实时island/IRQ上下文/live拒绝。
- 真实C worker + SweetTouchPanel测试在C MMIO边界加入断言：delegated worker若写gpio122 INTR_CFG/INTR_STATUS立即失败。覆盖arm/ACK/fail/park/resume和撤销服务后的取消。句柄模型走独立writeReg边界，不代替真实TLMM控制器测试。
- 测试重启帮助函数现在清理所有hook与delegated标记，避免上一用例遗留状态伪装后续mode2/3。
- 触摸、TLMM、panel-lifecycle、display-service回归通过；ARM64编译曾暴露IOPlatformExpert不完整类型，已加真实头文件并加fixture源码守卫。TLMM、SweetTouchPanel、sweet_touch_tcm三个TU均复编成功，CI marker151/0，diff --check通过。
- 对比2a提交逐函数验证claimSummary/dispatchSummary/setSummaryMasked/maskSummaryLine/unmaskSummaryLine字节不变；显示生命周期及GenericInterruptController执行代码未改。
- 本轮没有修改GIC enable读回策略、等待心跳、summary门控、claim开线时机、显示生命周期、固件或ADT。

## 明确保留的风险/未完成事项

1. delegated arm最后仍检查ENABLE读回，正常首包可能在读取前将其mask。没有把批次3的state2容错夹进本批。新句柄迁移不应因测试只跑静态时序而被称为实机稳定。
2. 软件探针pending写入已在2c收回summary owner（见下）；探针整体仍沿b22的enable/mask与读回顺序，不证明软件注入会制造某个pad的硬件status。
3. 2a阻止归还live注册，却未为已在dispatch快照中的handler提供排空协议；服务撤销/并发调用的生命周期还要核对。钩子撤销后拒绝裸写，不等于已经消除了所有跨核生命周期竞态。
4. 新prepare与island检查不保证能与固件瞬时抢占进行跨域原子仲裁，仅尊重可观测所有权；不自动清扫其它pad。

以上事项须在批次2总审查中列明处置；2b为批次2内部提交，当前不要求CI/刷机，也不推进批次3生产代码。


## 2c：软件探针 pending 写入归 summary owner

- AGPIO::setProbePending(bool) 请求summary控制器执行ISPENDR/ICPENDR写入；仅允许线程上下文、真实持有者、已注册slot、映射可用且只有一个已注册客户端。
- true建立探针预留，重复true拒绝；false仅允许同slot已建立的预留。预留期间新增客户端在任何pad寄存器写入前被拒绝；注册发布与预留检查通过fLock/fIrqLock串行化。
- 注销清理该slot遗留的预留并由owner清pending，不让新owner继承软件注入。
- worker mode3仅通过service/AGPIO钩子请求pending操作，缺hook报错或返回false；mode2保留原始写入。原探针enable/mask、等待预算和pending/active读回不变；不增加全pad扫描。
- C worker MMIO测试边界进一步禁止mode3直接写GICD ISENABLER/ICENABLER/ISPENDR/ICPENDR；通过钩子的单客户端操作通过，撤销后拒绝；并不代替真实GIC硬件探针验收。
- 真实控制器测试覆盖单客户端成功、IRQ上下文拒绝、无预留清除拒绝、重复注入拒绝、预留期间注册拒绝且pad不变、多pad拒绝，以及handle遗忘结束探针时注销清理。
- TLMM、触摸、panel-lifecycle、display-service四套回归通过；TLMM/SweetTouchPanel/C worker三个ARM64 TU通过；marker151/0与diff --check通过。

2c没有修改summary claim/dispatch门控或显示执行流程；只是确定共享pending写入者及诊断准入边界。批次2仍剩在途callback的注销排空协议。当前不要求CI/刷机、不启动批次3。
