# 第6批 GPIO summary stray 隔离与恢复（0121b31）

基线是 b30 / `b76005f`。GPIO122 叶子掩码已通过真实启动、触摸和两轮普通 OFF/ON，见[第5批记录](GPIO-IRQ-STAGE5-LEAF-MASK.md)。

## 故障合同

- `stray` 表示一次已接纳的 summary dispatch 没有调用任何未屏蔽、已注册的 handler（`served == 0`）。原因可能是 client-masked 锁存、陈旧 pending、时序或外部贡献者。计数没有提供具体 pad 的归因。
- IRQ 路径在既有短门控上保留 INTID 240 的 mask，在 IRQ 锁内记录计数和持久 hold，在锁外按 1、2、4……次输出 WARN。hold 期间排队或重复的 callback 直接退出。provider 开线窗口没有 slot 时同样隔离。IRQ 路径只使用已注册 slot 的快照和本控制器的 GICD 位，无全 pad 普查、未持有或 island pad 写入、provider 调用及 RWP 等待。
- 普通 resume、晚注册和客户端注销均不能解除 hold。最后一个 slot 注销也保留已屏蔽的 provider claim，防止同一 SPI 被另一条路径重新启用。stop 释放 claim，下一次 start 重置状态。
- 显式恢复是内部 C++ 诊断入口。调用者必须持有控制器引用，在非 IRQ 上下文提供外部清除确认、当前 stray 序号和非零确认来源编号。控制器核对当前 claim 和映射、armed 与 masked 状态，并拒绝停止、在途 dispatch、retiring slot、client mask、island hold 或活动软件探针。只有注册的 AGPIO slot 参与检查，运行期转为只读或仍有锁存均拒绝恢复。检查和开线在同一 IRQ 锁事务内。恢复日志记录来源与序号。台账里的确认是调用者声明，硬件外部状态由诊断调用者验证。
- 目前生产路径没有接入恢复调用者。故障发生后，设备上的可用恢复手段是重启或控制器 stop/start。显示降级和总线迁移见[独立任务](GPIO-IRQ-IMPLEMENTATION-PLAN.md)。

## 本地证据

- TLMM 确定性 fixture 对持续外部贡献者建模，验证首次无状态 dispatch 后 summary 保持屏蔽，重复请求无 pad 访问、无 unmask、无额外 handler 或 WARN。在设备 INFO 阈值可见 WARN，并校验 MMIO 访问只涉及已注册 slot。覆盖显式恢复成功、恢复拒绝、正常 owned 派发恢复、claim 窗口、最后注销及重注册。
- 同一 `--stray-isolation-only` 用例对 b30 原始 TLMM 源码运行，在 `gIrqHits==0&&!gicdLineEnabled()` 处按预期失败。证据在工作区 `scratch/gpio-irq-stage6/b30-negative.log`。
- TLMM GPIO、真实触摸 worker/service、panel-lifecycle、display-service 和 log-levels 串行回归通过。ARM64 TLMM 单文件编译通过，现有 CI 源码 marker 154/154 命中，`git diff --check` 通过。命令和日志在工作区 `scratch/gpio-irq-stage6/`。实机 GIC 持续电平、历史 b23/b24 复位根因仍无本批证据。

## CI 与实机验收

用户 push 后以现有 CircleCI full-source 配置构建，将两份包保存为 `logs/xnu-kernel-0121b31.gz` 和 `logs/xnu-buildtree-0121b31.gz`。逐文件核对构建树和内核 SHA，再用工作区 `out-bs/mk16k-0121b31.sh` 打包并回读 ABI608 两档最终镜像。脚本已用真实 b30 内核验证拒绝旧版，无 b31 镜像或实机结果。

先用 mode 3 临时启动，验证桌面、连续触摸、多指与全部抬起、两轮普通 OFF/ON 和醒后触摸。核对软件探针、武装、接管、IRQ 统计以及是否出现 stray WARN。若观察到异常，保存原始持久日志与设备表现，再审查隔离效果。持续异常电平和显式恢复仍需安全的设备证据。保留 b30 已验镜像回退。
