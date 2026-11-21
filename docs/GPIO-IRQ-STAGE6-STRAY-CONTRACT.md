# 第6批 GPIO summary stray 隔离与恢复（0121b31）

基线是 b30 / `b76005f`。GPIO122 叶子掩码已通过真实启动、触摸和两轮普通 OFF/ON，见[第5批记录](GPIO-IRQ-STAGE5-LEAF-MASK.md)。

## 故障合同

- `stray` 表示一次已接纳的 summary dispatch 没有调用任何未屏蔽、已注册的 handler（`served == 0`）。原因可能是 client-masked 锁存、陈旧 pending、时序或外部贡献者。计数没有提供具体 pad 的归因。
- IRQ 路径在既有短门控上保留 INTID 240 的 mask，在 IRQ 锁内记录计数和持久 hold，在 IRQ 中提交有界诊断快照，线程回调在开中断时输出 WARN（1、2、4……次，排队快照可合并）。hold 期间排队或重复的 callback 直接退出。provider 开线窗口没有 slot 时同样隔离。IRQ 路径只使用已注册 slot 的快照和本控制器的 GICD 位，无全 pad 普查、未持有或 island pad 写入、provider 调用及 RWP 等待。
- 普通 resume、晚注册和客户端注销均不能解除 hold。最后一个 slot 注销也保留已屏蔽的 provider claim，防止同一 SPI 被另一条路径重新启用。stop 释放 claim，下一次 start 重置状态。
- 显式恢复是内部 C++ 诊断入口。调用者必须持有控制器引用，在非 IRQ 上下文提供外部清除确认、当前 stray 序号和非零确认来源编号。控制器核对当前 claim 和映射、armed 与 masked 状态，并拒绝停止、在途 dispatch、retiring slot、client mask、island hold 或活动软件探针。只有注册的 AGPIO slot 参与检查，运行期转为只读或仍有锁存均拒绝恢复。检查和开线在同一 IRQ 锁事务内。恢复日志记录来源与序号。台账里的确认是调用者声明，硬件外部状态由诊断调用者验证。
- 目前生产路径没有接入恢复调用者。故障发生后，设备上的可用恢复手段是重启或控制器 stop/start。显示降级和总线迁移见[独立任务](GPIO-IRQ-IMPLEMENTATION-PLAN.md)。

## 本地证据

### b31r1 修复：诊断输出必须在线程上下文

b31 实机 panic 为 `console i/o from interrupt-disabled context @serial_console.c:168`。源码和 host 负对照确认一个与报错一致的缺陷：b31 的 stray 分支在硬件中断回调中释放 `fIrqLock` 后直接执行 `SWEET_LOG_WARN`。释放自旋锁会恢复锁前的状态，不能把硬中断回调变成线程上下文。`SWEET_LOG_WARN` 的 C++ sink 是 `IOLog`，而 `IOLog` 的 console 输出要求中断已开启，因此 panic 出现在 console 层。尚无这次设备的完整调用栈，不能确认最初的 stray 来源。

b31r1 将 thread call 在控制器启动时预分配。IRQ 只更新计数、保存 `dispatches/strays/pads` 快照并提交一次 thread call，线程回调再输出带 `resolution (deferred diagnostic)` 的 WARN。stop 先关闭接纳、取消并等待 thread call，再释放控制器对象。`reportSummaryReadback()`、`freePinIRQ()` 和 AGPIO 拒绝释放的诊断也拒绝在中断关闭时进入 console。

### 修复验证

host fixture 在日志 sink 前断言中断已开启。b31 原始源码在 summary stray WARN 处按预期触发该断言，b31r1 的 TLMM、触摸、面板生命周期、显示服务和日志等级回归以及 ARM64 单文件编译通过。b31r1 需要新的 CircleCI 构建，旧 b31 镜像不能用于这次实机复测。


- TLMM 确定性 fixture 对持续外部贡献者建模，验证首次无状态 dispatch 后 summary 保持屏蔽，重复请求无 pad 访问、无 unmask、无额外 handler 或 WARN。在设备 INFO 阈值可见 WARN，并校验 MMIO 访问只涉及已注册 slot。覆盖显式恢复成功、恢复拒绝、正常 owned 派发恢复、claim 窗口、最后注销及重注册。
- 同一 `--stray-isolation-only` 用例对 b30 原始 TLMM 源码运行，在 `gIrqHits==0&&!gicdLineEnabled()` 处按预期失败。证据在工作区 `scratch/gpio-irq-stage6/b30-negative.log`。
- TLMM GPIO、真实触摸 worker/service、panel-lifecycle、display-service 和 log-levels 串行回归通过。ARM64 TLMM 单文件编译通过，现有 CI 源码 marker 154/154 命中，`git diff --check` 通过。命令和日志在工作区 `scratch/gpio-irq-stage6/`。实机 GIC 持续电平、历史 b23/b24 复位根因仍无本批证据。

## CI 与实机验收

2026-11-21 收到 CircleCI full-source 的 kernel 和 buildtree。两个 gzip 归档完整，5300 个 XNU 文件与 `adddfb2` 逐一吻合，154 个源码标记命中。内核 17863520 字节，SHA256 `2aaa23b3c98b422bb83a2a0544b07c197d6f23ca14c5c3c7b5baf039ec84816d`，与归档清单一致，含本批隔离和恢复标记。CI run ID 与完整构建日志未提供。

两档 ABI608 镜像打包并回读 Android v1 header 和 FAT 内核、ADT、TOML。各档固件、ADT 和启动参数与对应 b30 镜像一致，参数 605 字节，符合 607 字节上限。每份镜像 35545088 字节。

| 镜像 | SHA256 |
|---|---|
| `out-bs/boot-16k-0121b31-gpioirq-panel-pipeline.img`（mode 3） | `217f04ff6c23b3af2b077af58058cb8536c20dbcef14718b9d2bcfdc5f7238f5` |
| `out-bs/boot-16k-0121b31-baseline-panel-pipeline.img`（mode 2） | `1231178b398a7c4c65bc618215e8570748dfcafb03dc8c2032c7f9c9ffd44769` |

工作区 `scratch/artifact-0121b31/artifact-verification.json` 和 `scratch/artifact-0121b31/packaging-verification.json` 保存归档与镜像核验结果。b31 实机报上述 panic，验收未通过；以下步骤用于修复版。

先用 mode 3 临时启动，验证桌面、连续触摸、多指与全部抬起、两轮普通 OFF/ON 和醒后触摸。核对软件探针、武装、接管、IRQ 统计以及是否出现 stray WARN。若观察到异常，保存原始持久日志与设备表现，再审查隔离效果。持续异常电平和显式恢复仍需安全的设备证据。保留 b30 已验镜像回退。
