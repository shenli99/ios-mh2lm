# PMIC mux 回滚失败隔离与日志等级

2026-11-05，新增 `0148-mh2lm-mux-group-isolation.patch`，接在0147之后。
0119原生内核及buildtree已核对：23个最终源文件与0148树逐字节一致，
新符号存在、无未定义符号；随后keys+USBIN组合实机通过，未触发隔离路径。
0148异常隔离仍待硬件取证；0118镜像不含0146～0148。

## 故障范围与恢复规则

原实现将 `removeGroup()` 的任意失败升级为整个 mux 停止。现在区分“恢复旧
PMIC配置失败”与“无法关闭共享中断源”：优先隔离失败组，保留其它组。

1. `beginRouteUpdate()` 先屏蔽共享 GIC；原有 maintenance/pending 状态交接保留。
2. `removeGroup()` 仍尝试禁用该组 IRQ、清 latch、恢复触发/极性和适用的
   APID oldAccess。成功时沿用原正常清理流程。
3. 失败时输出 ERROR，在同一 mux gate 中调用 `isolateGroup(group)`。
4. 按 APID 去重，仅对该组已解析、映射的 EE0 IRQ owner channel 操作。
   若同一 APID 仍有其它活动组，拒绝直接关闭该 APID，以免误伤其它 consumer。
5. 对可独立关闭的 APID 写本地 `ACC_ENABLE=0`，清该组注册 bit 的本地 IRQ，
   读回要求 access=0 且 IRQ_STATUS=0。这里不发 SPMI 外设命令，poisoned RW
   lane 也不复用。会尝试关闭其它受影响 APID，即使某一个隔离检查已失败。
6. 全部检查成功：该组按本次启动永久隔离，清绑定；设置
   `SweetMuxIsolatedGroups` 位图（bit0按键、bit1电池），生命周期标为 degraded，
   输出 WARN。`endRouteUpdate()`/已排队 IRQ worker 仅恢复其它活动路由。
   后续该组 attach 明确拒绝；其它组也不能经新路由重新开启已隔离的APID，
   不做静默重试或轮询回退。
7. 跨组共享活动 APID、关门读回失败或 pending 未清空：输出 ERROR，调用原有
   `disableHardware()`，保持共享 GIC/PDC 关闭并报告相关 consumer FAILED。

适用范围是 attach 失败回滚和 consumer detach 清理。当前运行期 ack 耗尽、
未知/卡住中断、storm、GIC/PDC 本身失败等共享故障仍保留原全局停止处理；
本补丁没有把这些来源不明的故障改成盲目继续运行。

该隔离提供的是“中断路径封闭”，不是“PMIC已完全恢复原状态”。未恢复的
触发配置或 enable 可能留在 PMIC，但经过验证的本地 APID 门保持关闭，直到
重启。既有 GPIO 模式校验仍只读；本次不增加 GPIO 配置或充电策略写入。
当前按键和电池 APID 不交叉；跨组共享 APID 分支是面向路由扩展的保守处理。

## 日志等级

现有0140已经定义 ERROR/WARN/INFO/DEBUG。本轮完成以下补充：

- `SWEET_LOG_ERROR` 直接输出 `[ERROR]`，不再调用等级过滤函数。
- WARN 是异常但仍能继续或已安全隔离；INFO 是启动、采样、状态更新等运行信息。
- `SweetBattery.cpp`、`sweet_spmi.c` 原来的直接 IOLog/printf 全部迁入等级宏。
  transport映射/版本/超时、静态回退等失败标为 ERROR；只读诊断不可用标为 WARN；
  正常采样/启动/诊断内容标为 INFO。mux、连接、按键此前已使用相同等级宏。

| sweetloglevel | 输出 |
|---:|---|
| 0 | ERROR |
| 1 | ERROR + WARN |
| 2（默认） | ERROR + WARN + INFO |
| 3 | 上述运行日志 + 原有 DEBUG 详细调试 |

运行日志优先级为 ERROR > WARN > INFO；DEBUG 仅保留作显式启用的详细诊断档。
ERROR 不受该阈值过滤。不同 transport 的启用、持久日志开关仍由原配置决定。
这是现有 Sweet 宏及 PMIC相关调用的整理，未声称覆盖整个 XNU/userland；
后续日志大目标中的按目标开启、独立过滤与集中管理仍未完成。

## 本地验证

- 精确0147最终源码叠加0148，严格 apply及逐字节比较；完整0142→0148重放
  与23个最终源文件一致。
- 八个 ARM64 编译单元通过，使用既有 IOKit/BSD 各自参数及本机适配；不等于
  原生 CI 完整链接通过。
- 实际 mux/client/keys C++ 与部分 battery 方法在 host harness 中运行：
  SET_TYPE保持错误且恢复写失败时仅隔离按键，电池仍能事件采样；电池 detach
  EN_CLR失败时隔离电池，按键继续；RW超时 poisoned 时仍能通过本地门隔离，
  不再发复用命令；覆盖已有 IRQ排队时 detach、门读回失败、pending卡住、
  合成跨组共享 APID拒绝隔离、其它组试图重开隔离APID被拒绝，以及原有
  生命周期/并发回归。ASAN/UBSAN及
  显式对象/notifier/MMIO计数、80次额外退出竞争通过。
- 实际 SPMI 初始化/加锁/错误路径测试通过，包含10次额外竞争。
- 实际日志宏和实际阈值函数分别用 C/C++ 编译，验证0～3、默认、非法参数，
  强制过滤函数全部拒绝时 ERROR仍输出、被过滤参数不求值；六个 PMIC相关
  模块无直接 printf/IOLog 调用。
- CircleCI源码 gate接受0148、拒绝0147；只更新 gate，不改构建命令。
  battery/board packer要求新隔离标记，防止用旧内核冒充新策略。

Scratch：`scratch/group-isolation-0118/`，包含base/edited/verify/full-chain、
compile-results、registry-tests、transport-tests、log-tests及CI gate结果。
这些模型不能证明真实 ACC_ENABLE/pending 的硬件行为；下一轮新内核直接测
keys+USBIN组合，保留三键、USB/触摸IRQ、ACM、息屏唤醒回归。不重跑用户已跳过
的 no-keys 阶段；无新内核前不生成声称包含0148的boot。

## 0119 原生产物与下一步

`logs/xnu-kernel-0119.gz`、`logs/xnu-buildtree-0119.gz`核对通过；内核SHA256：
`81074d86d567d0f4b7804fdf983f108aa7e50a53f6b6b91292a581466c3c3ae2`。
这证明0146共享锁、0147workloop准备、0148隔离/日志修改进入原生构建，
不证明0118的实机错误已解决，也不证明硬件隔离路径已触发/有效。

下一步直接使用 `out-bs/boot-16k-0119-board-startup-usbin.img`，
SHA256 `c79a86a52d7cbd04fdd5d198ac3244df8040aa87dfe4cc5ed574e324786b2d05`。
keys1、batteryIRQ2、真实gauge、ACM2/vbus1，固定USB/触摸IRQ，默认INFO。
测试三键/触摸、两轮拔插（每次等5秒）、独立电源键息屏/唤醒后输入。
导出 `live-0119-board-startup-usbin.txt` 和
`sweetlog-0119-board-startup-usbin.bin`。重点检查两组均attach成功、
三键HID成对、USBIN状态变化及并行输入；如触发隔离，必须区分“故障已隔离”
与“按键完整功能通过”。baseline仅作异常对照，不要求no-keys重测。

三个同内核profile及最终Android镜像内嵌kernel/ADT/TOML、参数唯一性已独立
解析核验；清单和核验脚本在 `scratch/artifact-0119/`。未提供本轮CI run ID或
build log；未执行push、云端操作或设备写入。

## 0119组合实测补充

两组接入、三键/触摸/USB、息屏唤醒及两轮USBIN拔插均通过，未出现相关驱动
ERROR/SET_TYPE读回失败。没有隔离事件，因此本轮不能作为故障隔离硬件路径
的验收。正常INFO与12条既有USB WARN可见，ERROR必出/阈值过滤仍以前述本地
宏测试及原生源码为证据。下一步同内核mode3，细节见
[BATTERY-IRQ-TESTING.md](BATTERY-IRQ-TESTING.md)。
