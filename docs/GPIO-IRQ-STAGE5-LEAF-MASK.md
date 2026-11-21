

## b30 实机验收（2026-11-21）

用户反馈桌面正常、未发现异常，提供 `logs/sweetlog-0121b30.bin`。header version1/ring4194304/total1549334/flush12，正确解码19527行；日志SHA256 `48668be19ae0230e61c84bc93a0a372f82adfb760494b263256ec8253350f6bd`。机器可读报告见 `scratch/artifact-0121b30/device-log-review.json`。

- 初次接管 token1/operation1 complete（4481行）。
- 第一轮 OFF token2/operation2 complete（15712），ON token3/operation3 complete（16518），包含触摸重新armed/app-format恢复。
- 第二轮 OFF token4/operation2 complete（16938），ON token5/operation3 complete（17443）。
- 全日志 35 down/35 up，触点顺序合法、无残留，峰值5指；最后 IRQ hits459/empty0/late0。
- ERROR=0；probe/arm/setup/resume/HANDOFF/deadline/panic失败均为0；snapshot WARN=0，旧summary读回ERROR=0。5条WARN均是启动期scanout review/USB ep0，生命周期活动期无WARN。

这证明 b30 的单个 GPIO122 客户端在叶子mask代码下完成真实启动、触摸和两轮普通OFF/ON；由于设备没有第二个TLMM GPIO客户端，不等同于多GPIO硬件并行验收。双客户端、锁存和fast-worker证据仍来自host aggregate测试。长期稳定、物理rail-off和异常持续电平仍未验。

结论：第5批普通实机门槛通过；b30/b76005f成为下一阶段回退基线，b29保留。第6批只处理stray异常隔离/恢复，不夹带显示降级或总线迁移。
