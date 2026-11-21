# 第6批 GPIO summary stray 隔离与恢复（0121b31）

基线是 b30 / `b76005f`。GPIO122 叶子掩码已通过真实启动、触摸和两轮普通 OFF/ON，见[第5批记录](GPIO-IRQ-STAGE5-LEAF-MASK.md)。

## 故障合同

- `stray` 表示一次已接纳的 summary dispatch 没有调用任何未屏蔽、已注册的 handler（`served == 0`）。原因可能是 client-masked 锁存、陈旧 pending、时序或外部贡献者。b31r2 将当前软件探针满足单次排空条件的投递单独分类，其他未服务投递仍隔离。计数没有提供具体 pad 的归因。
- IRQ 路径在既有短门控上保留 INTID 240 的 mask，在 IRQ 锁内记录计数和持久 hold，在 IRQ 中提交有界诊断快照，线程回调在开中断时输出 WARN（1、2、4……次，排队快照可合并）。hold 期间排队或重复的 callback 直接退出。provider 开线窗口没有 slot 时同样隔离。IRQ 路径只使用已注册 slot 的快照和本控制器的 GICD 位，无全 pad 普查、未持有或 island pad 写入、provider 调用及 RWP 等待。
- 普通 resume、晚注册和客户端注销均不能解除 hold。最后一个 slot 注销也保留已屏蔽的 provider claim，防止同一 SPI 被另一条路径重新启用。stop 释放 claim，下一次 start 重置状态。
- 显式恢复是内部 C++ 诊断入口。调用者必须持有控制器引用，在非 IRQ 上下文提供外部清除确认、当前 stray 序号和非零确认来源编号。控制器核对当前 claim 和映射、armed 与 masked 状态，并拒绝停止、在途 dispatch、retiring slot、client mask、island hold 或活动软件探针。只有注册的 AGPIO slot 参与检查，运行期转为只读或仍有锁存均拒绝恢复。检查和开线在同一 IRQ 锁事务内。恢复日志记录来源与序号。台账里的确认是调用者声明，硬件外部状态由诊断调用者验证。
- 目前生产路径没有接入恢复调用者。故障发生后，设备上的可用恢复手段是重启或控制器 stop/start。显示降级和总线迁移见[独立任务](GPIO-IRQ-IMPLEMENTATION-PLAN.md)。

## 本地证据

### b31r1 修复：诊断输出必须在线程上下文

b31 实机 panic 为 `console i/o from interrupt-disabled context @serial_console.c:168`。源码和 host 负对照确认一个与报错一致的缺陷：b31 的 stray 分支在硬件中断回调中释放 `fIrqLock` 后直接执行 `SWEET_LOG_WARN`。释放自旋锁会恢复锁前的状态，不能把硬中断回调变成线程上下文。`SWEET_LOG_WARN` 的 C++ sink 是 `IOLog`，而 `IOLog` 的 console 输出要求中断已开启，因此 panic 出现在 console 层。尚无这次设备的完整调用栈，不能确认最初的 stray 来源。

b31r1 将 thread call 在控制器启动时预分配。IRQ 只更新计数、保存 `dispatches/strays/pads` 快照并提交一次 thread call，线程回调再输出带 `resolution (deferred diagnostic)` 的 WARN。stop 先关闭接纳、取消并等待 thread call，再释放控制器对象。`reportSummaryReadback()`、`freePinIRQ()` 和 AGPIO 拒绝释放的诊断也拒绝在中断关闭时进入 console。

### 修复验证

host fixture 在日志 sink 前断言中断已开启。b31 原始源码在 summary stray WARN 处按预期触发该断言，b31r1 的 TLMM、触摸、面板生命周期、显示服务和日志等级回归以及 ARM64 单文件编译通过。旧 b31 镜像不能用于修复版复测。


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

### b31r1 镜像交付

2026-11-21 核对两份 CircleCI 下载归档：gzip 完整，5300 个 XNU 文件匹配修复提交 `edece98`，154 个源码标记命中。内核 17863640 字节，SHA256 `20c42e6df362c3f942a6578e539032ccdb89513f575381a48cab9e2e97095f19`，与包内清单一致，确认含 `resolution (deferred diagnostic)`。未取得 CI run ID 或完整构建日志。

两档最终镜像各 35545088 字节，回读 Android header v1、32 MiB FAT16 和其中的内核、ADT、TOML。固件、ADT 与启动参数均和对应 b30/b31 一致，命令行各 605 字节。证据在工作区 `scratch/artifact-0121b31r1/packaging-verification.json`。

| 镜像 | SHA256 |
|---|---|
| `out-bs/boot-16k-0121b31r1-gpioirq-panel-pipeline.img`（mode 3） | `8e7b420c86a8205865befb9881a4900299f8a05a363becfd25293469f8d9f74a` |
| `out-bs/boot-16k-0121b31r1-baseline-panel-pipeline.img`（mode 2） | `505d4c00dfb0265a8b1efae4ae113d3007d6318700b2be9f1fdb420a2f7d1af4` |

先临时启动 mode 3。核对 console panic 是否重现；可进入桌面且触摸正常时，再测连续触摸、多指全部抬起和两轮普通 OFF/ON。请导出 `logs/sweetlog-0121b31r1.bin`，若发生 panic 保留完整 backtrace。若有 deferred stray WARN 但触摸不工作，保留日志，不把“没有 panic”当作功能通过。b30 为已验回退。b31r1 结果见下。

### b31r1 实机失败与 b31r2 候选

用户看到日志画面冻结超过两分钟，USB 启动成功。原始 `logs/sweetlog-0121b31r1.bin` SHA256 `8115d3d0fdf0664a7f7d8f57abe749830011b552f61286e6702547e323016b5b`；标准解码 version1/ring4194304/total459371/flush15，5825行。证据见工作区 `scratch/artifact-0121b31r1/device-log-review.json`。

- 行772 SpringBoard 已 exec；3129 USB ACM endpoints configured；3929 TouchComm ready。
- 行3930 TLMM deferred WARN：dispatches=2 / strays=1 / pads=1，INTID240 隔离；3931 软件探针 hits=1、clear=1、pending/active 均为零；3932–3933 正式武装读回失败，触摸停止。WARN 在诊断线程输出，不能据其文本次序确定第2次投递是否就是软件 pending 或陈旧电平。
- 行4470 显示已观察到硬件传输；4475 touch resume admission 失败，4480 FB HANDOFF 明确冻结画面；其后仍有 SpringBoard 与其他用户态事件。未捕获 panic；末行不代表卡死 PC。

b31r2 的确定性夹具模拟先命中后屏蔽 leaf、再注入软件 pending。真实 b31r1 代码错误地 hold；b31r2 允许在当前预留内对已屏蔽 sole client 排空一次未服务投递。该机会绑定到投递入口所见的预留序号；不调用第二次 handler、不写 pad、不清既有 stray hold，不扩大到未屏蔽、过期、晚注入或额外投递。清除软件 pending 时在正常线程输出 `one masked-client software probe delivery drained`，证明消费了软件预留，不证明硬件贡献者身份。五组回归、ARM64 单文件和 154 个源码标记通过；负对照和打包旧版拒绝证据见工作区 `scratch/gpio-irq-b31r2/`。未改显示降级或 C worker，总线不在本批。下一步验证 b31r2 的真实探针/武装、桌面、触摸和两轮普通 OFF/ON；如仍有 hold，先采集实证，不放宽隔离或去扫 pad。b31r2 到件及镜像回读见下。

### b31r2 镜像交付

2026-11-21 核对两份 CircleCI 下载归档：gzip 完整，5300 个 XNU 文件匹配修复提交 `5e1680b`，154 个源码标记命中。内核 17863640 字节，SHA256 `aab2d08b1b8b7f99fcee5216ecb7ee0c9bf1e0855874295fdc5cd6978021bb2d`，与包内清单一致；确认含 `one masked-client software probe delivery drained` 和延迟诊断标记。未取得 CI run ID 或完整构建日志。

两档最终镜像各 35545088 字节，回读 Android header v1、32 MiB FAT16 和其中的内核、ADT、TOML。固件、ADT 与启动参数均和对应 b30/b31r1 一致，命令行各 605 字节。证据在工作区 `scratch/artifact-0121b31r2/packaging-verification.json`。

| 镜像 | SHA256 |
|---|---|
| `out-bs/boot-16k-0121b31r2-gpioirq-panel-pipeline.img`（mode 3） | `5c016c23c0a2ce2f7df0aac2b2d6948547222b0aeeedccb50229db92af16e0a1` |
| `out-bs/boot-16k-0121b31r2-baseline-panel-pipeline.img`（mode 2） | `00ed9d326fd2056092bd7c802a7f01568bde6f7883f97f724c0c745b44245baf` |

先用 `fastboot boot` 临时启动 mode 3。核对软件探针后能否正式武装、显示接管是否完成；若进桌面且触摸正常，再测连续滑动、多指全部抬起、两轮普通 OFF/ON 及醒后触摸。请导出 `logs/sweetlog-0121b31r2.bin`；若再次停在日志画面也应保留原始日志。观察单次软件探针排空的 INFO 是否命中，以及是否还有 stray hold 或 arm/readback 失败；不因未观察到该 INFO 就认定设备已覆盖该交错。b30 是已验回退，b31r2 仍待实机。
