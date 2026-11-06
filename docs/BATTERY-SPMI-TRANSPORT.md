# 电池共享 SPMI 访问接口（0150）

2026-11-06：第1段的 transport 实现与只读预检已完成本地验证，尚未云编译或
实机验收。没有调用充电配置写接口，没有取得充电控制权，也没有修复物理充电。
第2段数据清单见 [BATTERY-TELEMETRY-CHECKLIST.md](BATTERY-TELEMETRY-CHECKLIST.md)。

## 分层与调用

```text
电池自己的 IOWorkLoop
  ├─ 现有 IRQ 采样 → sweet_spmi_read → observer（旧边界）
  ├─ 可选一次预检 → charger_get_route / charger_read（新增精确表）
  └─ 后续控制组件 → charger_update（本轮无运行期调用者）
                                ↓
共享 transport：sp_lock / APID 权限 / 有界事务 / 超时失效
  ├─ observer lane：原有读与新增读共用 observer poison
  └─ RW lane：充电写与 mux IRQ 写共用 RW poison
                                ↑
SweetSpmiInterruptMux：IRQ 配置和 ack，仍只承担中断职责
```

没有新增服务、全局 consumer 桥接或充电策略单例。mux 的 IRQ 写入口没有扩大。
新接口在 MH2LM 编译条件下开放；旧 observer API 的签名、返回0/-1、白名单、
初始化和正常 IRQ 采样保持原有行为。Type-C 的旧 API 仍拒绝访问；只有新接口
精确允许0x1506/1509/150b，且先查唯一 EE0 writer APID，再用其 observer。
预检不会借用外部执行环境的 Type-C APID。

## API 与权限

声明在 `bsd/dev/sweet_spmi.h`，实现随
`xnu/patches/0150-mh2lm-charger-transport.patch` 交付。

| API | 语义 |
| --- | --- |
| `sweet_spmi_charger_get_route(sid, addr, out)` | 只读 arbiter 元数据，不提交 PMIC 命令；返回APID/write-ee/IRQ-owner |
| `sweet_spmi_charger_read(sid, addr, out)` | 精确寄存器表内单字节读取；成功才写出值 |
| `sweet_spmi_charger_update(sid, addr, mask, value, verified)` | 同锁内读、掩码更新、写、回读；成功才写出实际字节 |
| `sweet_spmi_rw_poisoned_locked(apid)` / `...poison_locked(apid)` | mux与transport共用的RW通道失效状态；必须已持有控制器锁 |

所有新 charger API 固定 USID2、PMIC-arb v5，拒绝未知地址、空输出、无写权限
或多个EE0 writer。查write-ee与IRQ-owner分开：写接口不要求IRQ-owner置位，
也不因为某一路IRQ成功就跳过写权核对。元数据匹配**不能证明实际写命令获准**。

可写表只允许稳定配置，所有访问为1字节：

| 地址 | 允许mask | 含义 |
| --- | --- | --- |
| 0x1042 / 0x1043 | 0x01 / 0x01 | 充电允许 / pause，持久控制位 |
| 0x1061 / 0x1070 | 0xff / 0xff | FCC / FV原始码 |
| 0x1340 / 0x1342 | 0x01 / 0x01 | USB suspend / ICL override持久模式 |
| 0x1365 / 0x1366 | 0x10 / 0x07 | APSD后override / ICL模式 |
| 0x1370 / 0x1380 | 0xff / 0x9c | ICL原始码 / AICL选项 |

依据为LG `smb5-reg.h`、`smb5-lib.c:smblib_icl_override` 与
`smblib_chg_disable_vote_callback`。名字含CMD不必然是脉冲；上表使用Linux
已经按持久状态读取/掩码更新的控制位。watchdog pet(0x1643)、AICL触发(0x1644)、
IRQ/W1C、secure unlock、SRAM和SMB并联override都不开放写入；将来确需脉冲/
W1C命令须新增独立语义，不能挤进RMW。完整只读表以patch内
`sp_charger_registers`为准，不允许任意整片扫描。

mask必须非零且在允许表内，value不得含mask以外位。保留其余位；值无变化
不提交RW命令、不创建临时映射。回读核对请求mask；未请求位可能被硬件更新，
不把其他执行环境的变化解释成本地原子性失效。物理单位范围、温度、电池上限
和输入能力由未来策略层验证，本接口不能代替这些条件。

## 故障与生命周期

- 新返回码分开：INVALID、NOT_READY、UNSUPPORTED、NO_OWNER、AMBIGUOUS、
  MAP_FAILED、DENIED、DROPPED、IO、TIMEOUT、POISONED、VERIFY，成功为0。
  旧`read`仍返回0/-1。失败时所有输出保持原值。
- 每笔状态等待最多1000次、每次delay(1us)，无自动重试/回滚。DONE错误与
  未完成超时分开。读/写后失败可能已有硬件效果，调用者必须进入控制故障路径，
  不得解释成“恢复旧值”；无法读回时不能保证已停止充电。
- observer超时禁用该observer lane；RW超时在解锁前标记共享失效。mux先在同锁
  内检查RW状态，timeout也在同锁内通知transport。两边都不能在同一启动中复用
  那条lane。RW失败不顺带禁止独立observer读；IRQ ack因失效而失败时沿用现有
  mux隔离/错误通知，不静默切回轮询。
- 新RW映射由`ml_io_map_unmappable`按需创建，同步完成或失败后正确按16K页
  对齐调用`ml_io_unmap`；所有出口平衡。旧boot期共享锁/observer映射仍为已有
  boot lifetime，不把永久`ml_io_map`映射交给可释放资源路径。
- 锁顺序：consumer自己的loop/gate → 单笔transport锁；mux自己的loop/gate →
  单笔transport锁。transport不反调服务、不等其他loop，不允许已持锁者再次调
  内部取锁的read/update，也不在持有mux gate时同步等待consumer loop。
  本轮无新的timer/thread/异步回调或长期控制handle，无新增terminated清理。
- 控制所有权不是write-ee：本轮不实现策略接管。后续在电池loop管理准备、取得
  控制、停止/取消监测、故障、禁充确认及服务退出；不能只凭此API就认为已有
  这套生命周期。SRAM会话须另设串行会话锁，按request/grant/read/release有界
  推进，等待grant时不占用共享事务锁。

读值失败由预检WARN报告地址/返回码/unknown；超时及写故障ERROR始终输出；
正常快照INFO受现有日志等级过滤，不修改USB日志传输。

## 可选只读档与验证范围

新增` sweetbatttransport=1 `：默认关闭，仅在真实SID2服务已启动自己的workloop
后，用runAction执行一次。位于注册mux consumer前，不在matched回调中做操作。
输出`SWEET CHARGER PREFLIGHT`的路由、原始字节和完成时刻at-ns；每字段独立，
失败继续。它是逐笔快照，不是全部寄存器同时刻、持续监测或充电控制许可。

后续同批构建可用：

```sh
tools/mk-mh2lm-event-matrix.sh KERNEL_ARTIFACT LABEL --battery-transport
```

生成`battery-observe`（现有diag，无新访问）和`battery-transport`（多一次只读
预检），两者均保留mode4、三键、USB ACM、触摸。脚本核对内核预检标记，拒绝
旧产物。可与第2～3段合并一次构建；**当前不要求为0150单独push/云编译**。
有新产物后由本地打包，用户fastboot；不重复0119 no-usb对照。

本地已通过：

- 新host测试运行实际完整transport、实际mux写方法及实际电池预检，ASAN/UBSAN。
  覆盖精确权限/掩码/版本/owner歧义、无变化、并发不同位及mux写并行、初始化/
  映射失败、DONE错误、回读不符/失败、双向共享timeout、输出和映射生命周期；
  预检not-ready/逐项失败及无策略写入。
- 现有SPMI锁/初始化/旧Type-C拒绝测试与额外10次并发测试；mux/consumer完整
  套件与额外80次生命周期竞争；0149逐字段诊断回归。
- 严格apply后23个选定源文件逐字节一致；3个修改编译单元ARM64单文件编译通过。
  Linux clang19使用已有CI头和本地builtin兼容定义，不能代替原生完整构建/链接。
- CircleCI YAML及source gate验证：接受0150、拒绝缺少0150的0149源码，实际
  `bash build.sh`命令未改；打包脚本bash语法校验通过。

工作底稿：工作区`scratch/charge-transport-0119/{base,edited,verify,compile.py}`。
尚缺实机正常路径回归、实际路由/配置快照和真正受约束的写事务验收；不能把
本地模型对写命令的模拟成功当作硬件充电成功。
