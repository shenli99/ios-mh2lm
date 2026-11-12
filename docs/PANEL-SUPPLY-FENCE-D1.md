# PD3：面板供电事务的实时依赖校验（0200）

2026-11-12，接0199。本轮完成 Pins 的实时 reset-low 查询，以及带 Pins/Touch 依赖的
RPMh panel vote 和取消接口。**尚未接入统一协调器的供电释放/恢复阶段，未新增运行档位。**
原 mode0–5 的正常路径没有改动，启动、match、metadata 不请求供电或操作引脚。
交付 `xnu/patches/0200-mh2lm-panel-supply-fences.patch`，仅改四份 Pins/RPMh 源码。

## 为什么不能只查一次缓存

MDSS 与 RPMh 各有自己的 workloop。父协调器读到成功的 Parked 后，RPMh 子定时器才执行
是合法的调度顺序；两个事件之间可能发生取消、服务停止、token 更新或引脚配置漂移。
`SweetPanelPins::copyStatus()` 原本只复制缓存，不能据其结果继续撤票。

`copyLowStatus(consumer, token, status)` 在 Pins 自己的 gate 内重新读取 CTL、IRQ、输出锁存
和独立输入位。只接受本 consumer 的成功 Parked，或从自己 Parked 恢复的 Active-low；
前者已有 >=5ms 的实际低保持，后者 activate 不改变低输出。普通 warm Active-high、ResetComplete、
Pending、取消、poison、stopped、inactive、旧 ID/token 均不能通过。

无效 ID/token、Pending 或 high 状态在读 MMIO 前拒绝，输出清零；已经有成功低保持的租约若出现
配置/IRQ/latch/input 漂移，标 Failed/uncertain/poison，不写引脚、不重试、不回滚。
此时恢复寄存器原值也不能复用旧成功状态。原 `copyStatus()` 仍是缓存查询。

## 接口与所有权

`SweetPanelSupplyFence` 包含实际 `SweetPanelPins*`、`SweetTouchPanel*` 及各自的 consumer/token。
调用者在 admission 期间持有 provider 引用；RPMh 对接受的在途请求分别 retain，复制结构，
不保存调用者栈地址。请求成功、失败、拒绝、no-op、取消或 RPMh shutdown 都释放这两份引用。
不 retain consumer，没有反向回调、全局实例桥、通用函数指针或新增 MMIO owner。

| 接口 | 行为 |
|---|---|
| `requestPanelVote()` | 只接受 VDDI/VPNL；沿原 gate、聚合、唯一 TCS writer、timer 和 fresh ACK 完成路径 |
| `copyLowStatus()` | Pins owner 内实时验证成功低保持租约，不改变引脚 |
| Touch `copyStatus()` | 沿现有服务刷新真实 worker mailbox，核对同 token、quiesced、非 ready、非 uncertain |
| `cancelPanelVote()` | 仅取消本 consumer/token 当前在途的 panel 请求；不取消别人的票或普通 LDO 请求 |

admission 在 begin 之前验证两路依赖；no-op 同样验证。Pending 请求在每次 timer 入口、
每条 TCS 写入前、trigger 前，以及最终清理完成提交前重新核对。不会因为父 timer 尚未运行
就沿旧缓存继续推进，也不能在最后一次清理写入后丢失 fence 却报告成功。

锁顺序为 **consumer -> RPMh -> Pins/Touch**。两个 leaf 服务不调用 RPMh 或 MDSS；
不等待请求完成、不 busy-wait，继续使用有界的原操作队列及原期限。
普通 `requestVote()`、ARC 和 CN0 接口不变；0200 不是把所有普通票自动升级为新接口。
后续面板释放/恢复应明确选用新接口，不能调用普通票绕过依赖校验。

## 失败与完成的准确含义

- fence admission 失败：不发 token，不改变旧 client vote/status，不写 TCS。
- begin 内约束冲突、token 耗尽或 TCS 不可用：不返回请求 token、释放依赖引用；原 begin
  的内部序号消耗语义保留，不宣称所有这类拒绝都没有内部序号变化。
- 在途依赖丢失：停止本请求并报失败，保留上次成功 ACK 的软件票及已写的部分 TCS。
  若本请求已编程/写入在途/已 trigger，则 uncertain 并沿原规则 poison，不清 TCS 或补电。
  consumer 显式取消沿相同处理；RPMh stop 保留原保守 uncertain 语义。
- 已 trigger 的事务可能稍后得到硬件 ACK。取消不能撤回它，不把旧软件票视为物理电压证明。
- 另一个 consumer 仍持 enabled 票时，本票撤销成功而 aggregate 仍 enabled 是正常结果。
  不把 aggregate enabled 当成撤票失败，也不把本票撤销当作电源实际已断。

这是**逐步复核依赖**，不是跨 provider 的硬件原子锁或电源隔离证明。leaf stop 或外部硬件变化
仍可能发生在最后一次读和实际写之间，已经发出的写不能收回；后续协调器失败收口须先同步
取消 RPMh 请求，再撤销 Pins/Touch token，不得把只取消父 timer 当成已取消子事务。
正常供电顺序、link/DMA 静止、同代账本及物理共享许可仍是协调器的前置责任。

0199 的 L3/L5 最后 aggregate disable 保护继续保留，新接口也不接受这两路。
Touch quiesced 只证明软件/IRQ/HID 静止，不是 deep sleep、供电隔离或断电后可恢复证明。
L14/L17 与其它器件的实际共享、外部偏压 E8、reset 后触摸 E7 仍未实机确认。

## 验证

严格应用至0199 verify，四源码逐字节核对，114原 CI source marker 保留，CircleCI blob
仍 `da0b6c0947a66f03491b3a9023590b49665292df`。22套本地检查通过（21实际源码
ASan/UBSan + Python ADT），无最终 sanitizer 报告；LSan 因 ptrace 环境限制禁用，另外核对
对象、映射、通知、分配及模拟 reboot 后 DMA pin 全部归零。

新检查执行真实 Pins、RPMh、MDSS 与 TouchComm worker，只有 OS、MMIO 和独立线级 peer
替身：Parked/Active-low、输入与 latch 分离、旧 token/取消/停止/漂移；四个在途阶段乘12种
子先于父异常；单次 timer 内写入之间丢失低保持，trigger/最后清理边界；fresh ACK/丢 ACK/
超时、no-op、其它 consumer 保电、同 owner 并发拒绝、begin 内失败和引用释放。
原0–5显示/资源/link/FB回归全部通过。

真实 XNU/KDK arm64 RPMh/Pins/MDSS/Framebuffer base+final 八个单文件对象编译通过；
关键新方法符号存在。RPMh warnings1325→1326，新增1处 `fClients[i]` 有界数组提示，
位于原 i<8 client 匹配循环内；Pins1171、MDSS1527、FB1397不变，未抑制警告。
不是整核链接、完整 FD 或实机结果。
底稿 `/home/shenli/work1/scratch/framebuffer-0200/` 保存严格应用、连续性、测试、对象、符号和警告审查。
无新 FD/boot、push/CI/设备操作，不为这组接口单开云编译。

## 下一步

把两路 panel vote 接到原统一协调器：完整 OFF -> reset 低保持 -> 原 mode5 link/DATA 停驻 ->
VPNL/VDDI 逆序撤本票；ON 保持 reset 低，VDDI ACK+20ms -> VPNL ACK -> 原 DATA/link/clock/
host/reset/完整 ON/brightness/pixel/touch。还需补 LG 的前后等待、精确部分释放账本、子请求
取消顺序及 FB 联合完成证据。保持 L3/L5、config floor、ARC、boot/RSC；不重跑一次性 takeover，
不猜 panel，不撤其它 consumer 的票。此序列是**待实现项**，不是0200运行路径。
