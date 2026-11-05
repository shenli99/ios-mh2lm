# SPMI 事务串行化：0118 组合档按键接入失败

## 实机已确认的故障

`sweetlog-0118-board-startup-usbin.bin` 持久日志未环绕，实际参数为
`sweetkeys=1 sweetbattirq=2`。参数没有遗漏。按时间顺序：

1. 电池早期订阅 group1，board 发布 mux IOService。
2. 电池 USBIN APID269 接入并 arm；电池 workloop 开始 arm-sync 采样。
3. 按键 group0 配置 power/down APID248、up APID261。
4. volume-up 读回 `type=0x42 high=1 low=1 enable=1`，SET_TYPE 的 bit0
   没有置位，违反 edge 配置校验；channel access/read 成功情况未在旧日志完整输出。
5. group0 回滚，`attachConsumer` 返回 `0xe00002ca`，按键线程没有启动，
   因而三个键全部不可用。group1 没有退出，随后两次 USBIN IRQ 仍完成状态发布。

这是按键初始化失败，不是运行过程中整个 mux 停止。原日志没有写入前 SET_TYPE
和每笔 command status，不能仅凭读回值断言哪笔写入被丢弃、读回陈旧或谁覆盖了值。

## 代码缺口与修正范围

0118 中只有电池 observer 读与电池 IRQ 写使用 `sp_lock`；mux 的 key observer/RW
以及 C 按键采样绕过该锁。mux workloop 只能串行自身任务，无法排除电池 workloop
同时采样。不同 APID 仍共享 PMIC-arb 控制器，不能把 APID 分开当作控制器级同步。
LG 参考内核 `drivers/spmi/spmi-pmic-arb.c` 的 `pmic_arb_read_cmd`、
`pmic_arb_write_cmd` 对命令、完成状态及结果提取使用同一 controller lock。

新增 `0146-mh2lm-spmi-transaction-serialization.patch`：

- 所有 key observer、mux key/battery RW、battery observer 共用线程上下文 IOLock，
  从命令/数据写入一直覆盖完成等待和数据读取。GIC top half 不获取该睡眠锁。
- 原子发布唯一锁，初始化检查及映射在该锁下串行；晚期 mux/keys 与早期 battery
  无须依赖谁先初始化。底层 transport 的按启动周期资源归属未改变。
- 电池 observer API 内部取锁，调用者不嵌套持锁；锁内不调用 consumer、等待
  workloop 或发 HID。mux gate → transport lock，没有反向依赖。
- RW 超时后包括 key APID 在内均禁止复用，避免晚到完成覆盖后续命令。
- 增加 write/observer 错误状态与 route readback 的 expected/mask/access/逐项
  read-ok 日志。配置仍须严格读回成功；没有增加重试配置、轮询回退或绕过校验。

consumer registry、attach/detach、terminated 清理、固定路由表、GIC/PDC 归属、
USB/触摸实现及充电/SOC 策略均保持。共享底层事务锁不是全局 mux discovery 桥接。

**确定修复的是控制器事务未统一串行的代码缺口。它是否是本次实机 readback 失败的
全部原因，必须由新内核验证；本地模型不能证明硬件因果。**

## 本地验证与下一轮

- 精确 0118 + 0146 严格 apply、逐字节比较；0142→0146 完整链重放一致。
- 八个编译单元生成 ARM64 Mach-O，保留实际 IOKit/BSD 各自参数及此前本机适配边界。
- registry/mux 测试要求所有 key observer/RW 也持有 controller lock；保留原故障、
  回滚、超时、双 consumer 和退出竞争测试。新增 SET_TYPE 写后仍为0x42 的注入：
  必须拒绝 group0、回滚，group1 仍能接收 USBIN；不把真实 readback 错误当成功。
- `test-mh2lm-spmi-serialization.py` 执行实际初始化、锁、key/battery observer
  方法：16线程初始化仅产生一套映射；持锁时两类 observer 均不得发命令；并行读、
  错误/超时解锁、分配失败与晚到超时 lane 禁止复用均通过，另跑10次竞争回归。
  MMIO 是预置完成的 host 内存，不能证明真实命令完成次序。
- 原 key worker/HID/读取中退出、board 归属、ASAN/UBSAN 与计数检查通过。
- CircleCI 只更新最终源码 gate，编译命令不变；packer 检查新事务标记，避免
  用旧0118内核生成声称已修复的测试镜像。

下一步用户 push 后获取新原生内核及 buildtree，再用同一编译产物测试 keys+USBIN
组合档（三键/触摸与USB拔插并行、息屏/唤醒）。baseline 可留作异常对照，不要求
重跑用户已跳过的 no-keys 阶段。出现 readback 失败时用新诊断细分原因。
0146 尚无原生构建/实机结果，现有0118镜像不含修正。
