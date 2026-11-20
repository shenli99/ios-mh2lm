# 批次4：软件状态与 MMIO 写序（0121b29 候选）

基础：已验 b28 / 999989b 生产代码，桌面、触摸及两轮普通OFF/ON证据见[批次3](GPIO-IRQ-STAGE3-EARLY-CALLBACK.md)。本批按[固定计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)只处理同步与写序，不实施叶子掩码。

## 已复现的问题

旧setSummaryMasked在fIrqLock内更新fIrqMasked，解锁后才写GICD。A记账后暂停，B完成相反操作，A再补写旧操作，就能得到软件认为masked但硬件enabled，或软件认为unmasked但硬件disabled。

maskPinIRQ/resumePinIRQ也把client mask、pad CFG/STATUS和summary写分开；同类交错会留下“client已恢复、pad却被晚到mask关掉”，或“client已masked、pad却被晚到resume打开”。prepare检查masked后解锁再配置，以及resume已开始但注销先完成，也存在同类窗口。

新的确定性双线程测试在真实控制器适配层的第一次IRQ unlock处暂停A，让B完整执行相反操作后再放行。不是概率压力测试，不用sleep。旧b28在gicdLineEnabled与最终软件状态一致性的断言失败；相同硬件W1S/W1C别名适配下仍复现，不依赖普通内存数组的假行为。

## 修正范围

- 增加setSummaryMaskedLocked：软件门控与对应GICD写在同一fIrqLock事务完成。handler仍在锁外，原整线门控规则不变。
- mask/resume的slot检查、client mask、pad ENABLE/ACK及summary操作共用同一IRQ锁。resume不递归调用会取同锁的ack入口；ACK读改写和prepare验证/配置也纳入该锁。
- dispatch在handler返回后的deferred/retiring判断与非deferred ACK同锁提交；观察到island接管时不写其status。
- claim仍保持原provider enable一次→mask顺序，仅将状态和MMIO纳入同锁；release的provider调用在锁外。GICD映射失败时在注册provider handler前拒绝TLMM句柄中断路径，由客户端保留旧source；不在IRQ锁内执行provider fallback。
- ISENABLER/ICENABLER只写INTID240本位，不再读改写整个共享字，避免恢复其它驱动刚关闭的邻位。设备MMIO屏障保留；测试在gicdWrite边界独立模拟write-one别名，不为fixture向生产加入多余寄存器写。
- 原低层unmask内的每包ERROR不再在新锁区间打印。读回观察单独在线程上下文、锁下采样且锁外至多WARN一次，不poll RWP，不关闭客户端，不以单次0作为未投递判据。真实probe/arm失败验证保持b28代码。

## 验证

真实TLMM控制器测试：

1. summary mask→unmask与unmask→mask双向交错；
2. pad mask/resume双向交错，最终client mask、pad ENABLE和summary一致；
3. prepare与resume、resume与注销交错，不能留下晚到ENABLE写；
4. 映射失败不注册、不enable/disable provider；
5. 相邻INTID242已使能时，TLMM所有GICD写只能含INTID240本位，邻位保持；
6. 线程读回0只发一次WARN；IRQ上下文不打印、不消费once标记，也不关停正常pad。

fixture在provider入口、日志输出、thread_block处断言未持IRQ锁。prepare/dispatch/IRQ退役、service钩子排空等既有回归保留。fixture使用设备默认INFO阈值2，明确断言诊断WARN实际出现一次。

本地结果：TLMM、真实C worker+service、panel-lifecycle、display-service四套回归PASS；最终TLMM ARM64 TU成功；CI marker151/failing0，diff --check通过。生产差异仅TLMMGPIOController.{cpp,h}；C触摸验证、service、GenericInterruptController和显示生命周期执行代码未变。并发测试模拟OS/MMIO，不等于真实硬件压力或长期稳定证明。

## 交付与未完成事项

工作区out-bs/mk16k-0121b29.sh已就绪，固定原ABI608/ADT/参数，并检查locked helper/诊断符号与新文案。实际b28内核在新helper门禁处被拒，未生成伪b29镜像；下载后仍需核对全量构建源码与SHA，不靠符号证明锁语义。

候选尚未CI/实机；用户push full-source、下载b29 kernel/buildtree后agent核验打包。验收同b28：桌面、连续触摸/抬起/滑动、两轮普通OFF/ON和醒后触摸。旧逐包ERROR预期不再出现；若有一次snapshot WARN只当线索，probe/arm/setup失败仍须定位，不强行通过。

本批仍保持一颗pad deferred时整条summary关闭，**尚未叶子化**。第5批叶子策略、第6批stray隔离、失败触摸的显示降级仍未实施。b28原镜像保留为回退；b29通过实机之前不推进下一批生产改动。


## b29 下载及镜像交付（2026-11-20）

kernel/buildtree已到，5300个XNU源码文件与0312a41逐文件一致（0缺失/0差异）。内核17879760字节，SHA256 05b9fce017aa37379144d9ff770517e929033ae6d9e12f16d771cf82b3daa836，与包内SHA256SUMS匹配。未另行核验CI run ID或全量云端build日志。

两镜像均35545088字节。重新解析最终Android v1头并抽取FAT中的kernel/ADT/TOML：内核和派生ADT摘要匹配，ABI608固件SHA256 27e7a1d1a3f9421af18d02f216e4e7d651137d42f2955be775f9142f118ea480和面板MHPANEL1钩子通过；ADT SHA256 a9e4f37151162be9244b2e701b2e60063d85d4c424e8842c5bbf9b8d62391875。每档启动参数与对应b28逐词相同，两档只差touch mode。

| 镜像 | SHA256 |
|---|---|
| out-bs/boot-16k-0121b29-gpioirq-panel-pipeline.img | 31cd1bf007bb7d90303aad063a68f9f0743f75352c4b678013265f0231afe139 |
| out-bs/boot-16k-0121b29-baseline-panel-pipeline.img | 1be49cf3f7a7d4ce1fec787f7a1e65328f295fb37e37af3defe7f498929be4d0 |

新locked-helper符号与诊断文案在二进制中通过门禁；最终FAT内核也含probe/arm和新snapshot标记，旧逐包`summary INTID %u did not enable`字面量已不存在。核验记录：工作区scratch/artifact-0121b29/packaging-verification.json。

下一步用户临时启动gpioirq，确认桌面、连续点击/抬起/滑动、两轮OFF/ON和醒后触摸，导出sweetlog-0121b29.bin。无需再跑CI，当前未实机验收；b28镜像保持回退。本次只打包/核验/记录，未修改生产代码或推进第5批。
