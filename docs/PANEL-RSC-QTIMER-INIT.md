# 0217：按 Lineage 修正 qtimer 初始化的读回条件

后续更新：0218 已按用户要求把审计范围扩展至后续配置路径，云编译应包含 0217+0218。
以 [0218 配置观察与验证](PANEL-LINEAGE-CONFIG-OBSERVATION.md) 和 NOTES 顶部交接为准。


2026-11-13。a28 实机首错是 RSC 初始化 index4：wrapper `+0x2020`
（物理地址 `0x0af32020`，Lineage 名称 `F0_QTMR_V1_CNTP_CVAL_LO`）
写入 `0xffffffff`，读回 `0x224cac6`。0216 的逐项全值相等检查在这里报 IOError。
此时尚无 display TCS payload/trigger、INTID161 配置或新 DMA；本次不能用于判断完成中断是否有效。
日志证据见 [a28 实测](PANEL-D1-0121A28.md)。

## 来源与修改范围

Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd` 的
`drivers/gpu/drm/msm/sde_rsc_hw.c:33` 起 `rsc_hw_qtimer_init()` 对十项寄存器按序写入后返回，
没有附加全值相等检查。0216 自行增加了该检查；现有实机证据不足以支持这项完成条件。

0217 仅对以下六项 CNTP 比较值/控制写入采用 Lineage 的提交行为：

| wrapper 偏移 | Lineage 名称 | 写入值 |
|---|---|---|
| `0x2020` / `0x2024` | F0 CNTP_CVAL_LO / HI | 均 `0xffffffff` |
| `0x3020` / `0x3024` | F1 CNTP_CVAL_LO / HI | 均 `0xffffffff` |
| `0x202c` / `0x302c` | F0 / F1 CNTP_CTL | 均 `1` |

仍执行每次 MMIO 写入及既有屏障，并各读一次作为诊断，打印
`SWEET MDSS RSC: qtimer write issued offset=... value=... observed=...`。
不再等待这六项与写值相等。初始化完成日志明确只代表软件写序列完成，不是 timer 硬件 ACK。
该行为仍只在现有显式硬件生命周期模式中启用，默认路径保持。

65 项写入数值及顺序不变；其余 59 项（包括 frame/access 配置、wrapper、序列、solver、PDC）
仍有原读回检查。初始化仅在原资源权限和 TCS 交接成立后执行，失败保票和 stop 行为保持。
NoC 完成仍必须有真实 INTID161 回调、本槽源及命令 ISSUED/必要 COMPLETE，不能用 qtimer 日志替代。

**硬件读回不同的原因仍待核实。** 不宣称 CVAL 是只读、硬件正在改写、写入被拒或权限异常。
通用 ARM timer 的 `CNTACR_RWPT` 定义见同一 Lineage 的
`drivers/clocksource/arm_arch_timer.c:49`，但不据此推断该 Qualcomm 专用集成的完整访问语义。
保留 Lineage 的 CNTACR bit0 写值，不擅自增加 bit5、复位 timer 或更改电源/时钟。
此修正去掉额外软件门槛，尚未证明 timer 硬件已接受配置或整个接管成功。

## 验证

- 新专项在实际 a28 源码上复现 7 组非相等读值导致的初始化失败，均在 TCS 提交前停止。
  第一组使用实测 `0x224cac6`；其余为故障模型，不是更多实机观察。
- 修改版 16 场景 ASan/UBSan：六项分别/同时不相等、其余配置损坏、权限丢失，
  及无 IRQ 不得 ACK；六项不重写、不轮询，仍按原顺序提交。
- 16 套既有 provider/clock/core/NoC/managed/lifecycle 回归及 37 场景完成中断专项通过。
- 动态执行 Lineage 实际初始化函数，65 项逐项逐序一致。
- SweetMDSS.cpp 真实 ARM64 单文件编译通过；告警 1573→1573，无新增类别。
- 0185–0217 共 33 个补丁严格重放，78 个文件字节一致，114 项既有 CI source markers 通过。
- matrix 增加 qtimer marker，实际旧 a28 已被拒绝且未生成 boot 输出；bash 语法检查通过。

底稿：`scratch/framebuffer-0217/`，包括 `a28-log-analysis.json`、`verification.json`、
`tests.json`、专项输出、ARM64 编译日志、`cumulative-replay.json`、`final-review.json`。
CI 配置、ADT、固件及启动参数未修改；未 push、触发 CI 或操作设备。

## 下一步

用户 push 最新 main 并云编译，下载新的 kernel/buildtree，可编号 `0121a29`。
agent 核对包含 0217 后，沿原 ABI608 固件、a28 hardware ADT 和 605 字节 retained 参数打包。
先查六条 qtimer write issued 的实际读值，再查初始化是否越过、INTID161/fresh ACTIVE、
NoC ACK 及新 DMA/transfer/HANDOFF；失败按新首错定位。
完整接管前继续暂停亮度、OFF/ON、长测、combined/supply。当前没有 0217 的新启动镜像。
