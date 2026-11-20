# GPIO 中断分批实施计划

本计划以真正实机通过的 **b22 / 830fa52** 为唯一代码基线。用户于 2026-11-20 授权固定计划并逐项实施。不得直接合入 b23–b26 的整批改动。

## 执行规则

- 每批一个行为主题，单独提交；本地测试通过后打包，实机验收后才开始下一批生产改动。
- 只读审查、编写后续测试方案可以提前；不得把后续修正夹进当前待验镜像。
- 保存源码提交、内核与镜像 SHA256、模式、日志 header/flush seq、验收与未证结论。
- 设备日志统一使用 `tools/sweet-log.py`，4096 字节头块，64 位 total/seq。落盘末尾不是故障 PC。
- 不修改 CI workflow、固件、ADT 或显示启动参数。用户负责 push、下载和实机；agent 负责实现、验证、提交及打包。
- 如果发现前置依赖，先写明依赖并调整当前批次内部步骤；不默默混入另一个主题。
- 中断注册必须持有对应 AGPIO；island/只读 pad 不允许写。stray 不是越权清理的授权。

## 固定顺序与门禁

| 批次 | 范围 | 不包含 | 通过条件 |
|---|---|---|---|
| 1 基线锁定 | b22 源码、镜像、现有回归、所有权现状 | 生产逻辑变更 | 5300 源码匹配构建树、镜像/日志核验、现有测试结果留档 |
| 2 所有权收口 | 触摸 delegated 模式剩余 INTR_CFG/INTR_STATUS 操作迁入句柄；注册/释放生命周期 | 叶子掩码、claim 开线时机、stray 普查/清扫、显示流程 | 所有路径含故障/暂停/恢复单写者；真实 worker 回归；保持 b22 整线门控与启动时序；实机桌面、触摸和原有显示生命周期通过 |
| 3 首包竞态 | 在批次2代码上复现并修正武装后立即到包的读回判断 | summary 策略或显示降级 | 已 deferred 的首包正常处理、锁存不丢；无回调但 ENABLE=0 仍拒绝；实机通过 |
| 4 状态写序 | mask/resume 软件记账与 MMIO 写序、必要同步 | 叶子化、异常清扫 | 强制交错下状态与硬件一致；不引入 IRQ 阻塞、provider 锁重入或无界等待；实机通过 |
| 5 叶子掩码 | 仅改变客户端屏蔽的范围，去掉其它 pad 的无关等待 | claim 启动策略变化、全 pad 普查/清扫 | A deferred 时 B 仍派发；A 锁存保留且恢复正常；真实 GIC/触摸实机验证，无持续重投递 |
| 6 异常路径 | 单独定义 stray 可见性、隔离、恢复及所有权边界 | 擅自操作未持有 pad | 无贡献者/持续电平的测试有明确结果；限制中断而非只限日志；恢复不依赖已经死亡的 worker；实机通过 |

批次2如果因已有首包竞态无法通过，不添加无条件容错：保留失败证据，先单独提交批次2，再将批次3作为明确依赖处理；不得声称批次2已经实机通过。不得只用 ENABLE=1 判定初始化成功，也不得无条件接受 ENABLE=0。

## 独立后续任务

显示降级单独实施、提交和验收：模拟触摸启动失败，驱动整个首次接管直到最终 complete，而非只到 scanout active；解决前段绕过失败触摸、后段却强制 resume 的矛盾。正常触摸 fence 不放宽。此任务不与叶子掩码同版。

总线层 SweetI2CController / SweetI2CDevice / SweetTouchComm 和触摸迁 C++ 在中断阶段稳定后继续，不夹带进本计划的中断批次。

## 批次1记录

- 基线 HEAD：830fa52（其驱动实现来自 9e3616e）。不能以 f9e515a 代称实际 b22。
- 下载的 b22 buildtree 与该提交全部 5300 个 XNU 源码文件一致（回退时已逐文件核验）。
- 已验 b22 gpioirq：用户确认正常；日志记录桌面/触摸接管，15 down/15 up、hits319、late0。
- 当前模式3仍含触摸直接 INTR_CFG 写入：arm、fail、park 等，不能把文档里的“句柄独占”当作全部实现。
- 当前 AGPIO mask 会关 pad 和整条 summary；任一 client mask 未清零就不 unmask，尚无独立叶子掩码。
- 回归与镜像核验明细见 `docs/GPIO-IRQ-BASELINE-B22.md`。

## 当前进度

批次1已完成。批次2a–2e本地完成、b27独立实机失败已封存；单独批次3修正后的b28组合已实机通过桌面/基本触摸和两轮普通OFF/ON、醒后触摸，满足下一批门槛（不将b27改写为独立通过）。当前以999989b生产代码/b28镜像为已验回退基线。第4批b29已实机通过桌面、46对触点/十指峰值及三轮普通OFF/ON（ERROR0、末轮醒后触摸继续），0312a41生产逻辑为新回退基线，详见[第4批记录](GPIO-IRQ-STAGE4-MMIO-ORDER.md)。第5批双GPIO回归及客户端leaf mask已本地完成，候选0121b30待CI/实机；保留dispatch短门控和claim/prepare时机，详见[第5批记录](GPIO-IRQ-STAGE5-LEAF-MASK.md)。第6批和独立显示降级尚未实施。证据见[第2批](GPIO-IRQ-OWNERSHIP-STAGE2.md)及[第3批](GPIO-IRQ-STAGE3-EARLY-CALLBACK.md)。

### 批次2a：注册所有权必须持续到注销

发现：b22 的 `releasePin()` 可以把仍注册着 IRQ 的 pad 交给新 owner；`freePinIRQ()` 删除 slot 前未清 pad ENABLE，可能留下没有 slot 服务的贡献者。

本地修正：direct `releasePin()` 拒绝 live slot；`AGPIO::release/put/free` 在归还前调用注销；注销先走原有 maskPinIRQ，再移除槽位，不 ACK 待处理锁存。保持 b22 的整线门控，未改正常 claim/dispatch/resume、worker、显示或 firmware。

验证：新增 live release 拒绝、注销后 ENABLE=0 且 status 保留、后续 dispatch 不再调用已删 handler、直接 drop handle 后可重新注册的用例；同一新增测试在原始 b22 的 live release 断言处失败，修正后 PASS。TLMM、真实触摸 worker、panel-lifecycle 回归及 ARM64 TLMM TU 通过；CI marker151/0。生命周期由 owner 串行化；本用例不证明已在执行的并发回调已经排空，也未修复 mask/MMIO 状态写序。

2a 是批次2内部的独立提交，不要求现在构建或实机；待2b及整批审查完成，再交付批次2单一候选，验收后才进入批次3。
