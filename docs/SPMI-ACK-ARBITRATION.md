# SPMI ACK 与遥测竞争修复（0167）

2026-11-07，基于真实0120a15云源码及0166。实现补丁
`xnu/patches/0167-mh2lm-spmi-ack-arbitration.patch`，本地验证通过，待用户push、
云编译和新内核实测。本轮没有修改CI，没有push、操作设备或生成新boot。

## 对应证据与修复边界

0120a15 watchdog档ADC59第二阶段，在121.333559739秒已扫描到pending，
早于121.337562291秒deadline约4.003ms，但ACK直到121.346589479秒完成，
notify晚9.028ms。scan→ACK为13.030ms，其间超时诊断快照连续占锁6.718ms。
快照在超时后开始，会加重竞争，不能解释最初的超时原因。旧日志还不能把整个
ACK区间拆成等锁、事务和抢占，因此本轮同时修正竞争机制、补充直接计时。

ACK路径原先取普通`IOLock`，电池多笔读可能反复先取得锁；mux逐路ACK后立即
通知并打日志，普通Type-C事件可以在本轮后面的ADC ACK之前启动重采样。
public故障快照还会一次持锁读7或10字段。三者都存在软件排队与延迟来源。

本轮不宣称已经修复实机超时；不改变ADC 100ms、watchdog 23秒/180秒、IRQ-only、
owner校验、读写白名单、lane poison、既有三次幂等ACK尝试或故障撤销条件。
没有增加转换重试、轮询回退、自动启充或新的watchdog pet。

## 总线仲裁

仍只有一把控制器锁`sp_lock`，每个硬件事务从编程命令到读取结果始终互斥。

- 普通访问统一经过`sweet_spmi_irq_lock()`。取得锁后检查ACK等待计数；若非零，
  用`IOLockSleep(..., THREAD_UNINT)`原子入睡并让出锁，醒来重新检查。
- mux受限的IRQ写/ACK使用`sweet_spmi_irq_ack_lock()`，先原子登记等待者，再取
  同一把锁。这样普通采样下一笔事务不能反复插队到已登记的ACK前面。mux的有限
  路由配置/撤销写也使用此入口，普通consumer不获得此优先入口。
- 两类访问共用`sweet_spmi_irq_unlock()`；没有待入场ACK时，在同一锁保护下
  wakeup普通等待者并释放锁。没有忙等争锁、另开通道或丢失唤醒窗口。
- 已经开始的普通事务不会被打断。RMW、ADC协议状态与恢复、FG DMA准备仍保持
  原子区间；映射/权限检查也没有被不安全地移到锁外。

锁顺序保持：mux gate→控制器锁；FG会话锁→控制器锁。控制器持锁期间不等待
consumer gate/workloop。GIC top half不取睡眠锁、不做SPMI，只屏蔽/投递事件。
FG的ALG/grant `IOSleep()`仍释放控制器锁，只持FG会话锁。

这是事务入场优先级，不是实时调度保证。被抢占的持锁线程、当前事务内的映射/
多笔原子协议仍可能延迟ACK；不承诺所有负载下的硬性墙钟上限。已有每drain最多
4遍表、storm检测与stuck group隔离继续保留，各组按同一张表公平扫描，没有
只优先ADC而跳过按键/USB的分支。

## ACK 与通知分批

每遍最多扫描19条路由，先ACK本遍命中的路由，再按group合并token、投递一次
轻量通知。同组多个Type-C位合并为同一次输入刷新请求；各组均保留，后续遍次
仍会处理通知期间新到的pending。不为了合批绕过硬件ACK或延迟清锁存。

通知仍是原子OR + 软件`IOInterruptEventSource`，consumer重工作仍在自己的
workloop。没有同步调用完整电池采样。正常通知间隔最多一遍表的处理量，原有
表顺序不变；已成功ACK的事件在后续错误退出前也会投递，随后按原规则报告失败。

每条trace在实际通知前一次原子发布，包含本遍真实ACK和notify时刻，不暴露
notify=0的半成品。使用实例内固定4×19记录保存本次drain历史，避免运行期分配
或增大内核栈。日志统一在全部有限遍次完成及GIC重新使能/失败处理后输出，
不能阻挡本次drain后面路由的ACK。新一轮IRQ仍可能等待当前workloop剩余日志，
不能把日志开销当成完全消失。

## 诊断快照与计时

独立的ADC timer故障观察、watchdog终止观察按固定字段逐笔取锁/释放，下一字段
让出给等待ACK。快照本来就不是硬件原子图像，现在也允许本地其他事务穿插。
`sp_adc5_save_fault_locked()`用于协议错误的恢复前现场，仍在原协议锁内读取，
避免为优化诊断而拆开session归属和恢复；这条错误路径仍可能连续占锁。

`SWEET ADC5 SNAPSHOT: v2`和`SWEET BATT WD SNAPSHOT: v2`使用`first-lock`字段。
`done-first-lock`是整个观察的经过时间，不能再解读为连续持锁时间。ADC的`saved=1`
表示锁内保存，`saved=0`为独立观察。所有字段保留独立错误，均不能完成转换。

新增`SWEET MUX ACK: v1 route=... irq=... work=... wait=... hold=...`，在已有
ADC EOC和watchdog hit之后输出；单位均为纳秒，保留日志等级及255字节行上限。
`SweetMuxIrqTrace`同时提供`ackWaitNs/ackHoldNs`，供后续按接口读取。

- `wait`：进入受限写入口取锁前到取得锁的经过时间，包含等待和调度。
- `hold`：取得锁到释放前的经过时间，包含命令MMIO、DONE检查及期间抢占，
  不能等同纯硬件总线耗时。
- 若既有幂等ACK尝试多次，两个值分别累计；显式尝试间隔、通道IRQ_CLEAR等
  不计入hold。结合`scan/ack/notify`可分清大部分等待位置，仍不等于CPU抢占跟踪。

启动标记`SWEET MUX ARBITRATION: v1`，打包脚本的dependency档必须同时检测到
新仲裁和ACK计时标记；旧0120a15内核不能换标签冒充本轮修复。

## 本地验证

底稿：`/home/shenli/work1/scratch/spmi-ack-arbitration/`，不入git。

- 0167对真实0120a15相关33源码严格apply，最终33文件逐字节回读；原106源码
  gate通过，CI配置字节未改。依赖档仍为runtime1/deps0、1、2、4、7。
- 新仲裁测试执行真实三个锁函数，100次受控调度/1100线程；强制普通读者在
  ACK入场前睡眠，测试多ACK、伪唤醒、无插队和所有普通读者最终恢复。
- transport 134场景：验证成功/错误写的wait与hold独立累计；新增ADC/WD独立快照首字段期间登记ACK，确认第二字段
  前ACK已经完成；原RMW/owner/poison/失败输出/FG会话和协议恢复回归通过。
- mux/consumer 67场景 +80次竞争；新增按键、USB/source、全部Type-C、ADC和
  watchdog同时pending，验证通知前本遍ACK已完成、token无丢失、Type-C合并，
  通知中重触发会在下一遍交付。trace并发、group隔离、解绑/停止回归通过。
- 电池运行层回归、ADC client 19场景+100次竞争、SPMI初始化/按键并发及日志
  等级检查通过。上述宿主C/C++测试使用ASAN/UBSAN；不模拟原生XNU调度时延。
- 7个相关文件ARM64单文件编译通过；不代替完整云内核链接和实机验收。

## 下一轮

用户push后云编译一次；下载真实源码/内核后先核对0167，再用
`tools/mk-mh2lm-event-matrix.sh KERNEL LABEL --battery-dependencies`打包同核五档。
不重打旧0120a15，也不要求继续测旧baseline/identity/watchdog/inhibit。

优先新identity1：进入桌面尽快拔5秒→稳定电源保持5～10分钟→拔5秒，简单确认
三键/触摸，导出持久日志。关注至少覆盖旧59请求故障附近的持续USBIN转换、
deadline、ACK wait/hold及合批后分发耗时；出现故障即可导出，不强行凑时长。
看过身份档后再安排同核watchdog4/组合7，避免无目的重复刷机。watchdog诊断
180秒仍从准备开始，不在桌面先空等。

首bark硬件起算、长期有效输入许可、外部SMB1355 I²C隔离及最终managed-watchdog
授权仍未关闭；本轮全部runtime1、不主动启充，不能以本地并发测试替代物理充电验收。
