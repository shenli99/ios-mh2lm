# SweetSpmiInterruptMux 的 IOService 化

2026-11-05，0144 本地实现；叠加 0142/0143，尚未通过新云端完整链接与实机测试。
用户要求进一步把 mux 变成 IOService，并评估 filter / interrupt controller。
电池验收继续暂停，0115 的硬件证据与未知项不变。

## 三种类型分别负责什么

| 类型 | 实际职责 | 本轮决定 |
| --- | --- | --- |
| `IOService` | 设备匹配、IORegistry、start/stop/free、实例资源和服务生命周期 | 实现：mux 继承 IOService，状态归实例。 |
| `IOFilterInterruptEventSource` | primary interrupt context 快速过滤；true 才调度 workloop；常用于可安全快速判断来源的硬件 | 本轮保留普通软件 `IOInterruptEventSource`。INTID513 已独占交给 mux；路由确认仍需下半部。 |
| `IOInterruptController` | 向 provider 的 client 提供 register/enable/disable/投递接口，将中断接入 IOKit 通用管理 | 尚未实现；未来由上游 GIC provider 承担，不能只把 mux 改个基类来代替。 |

这些类型不是互相替换的三种实现。拥有 `IOService` 生命周期不意味着已经具备
`IOInterruptController` 的中断 provider 能力。

核对本版 XNU 的 `IOInterruptEventSource.cpp`、`IOFilterInterruptEventSource.cpp`
和 `IOService.cpp` 后确认：

- 有 provider 时，事件源会通过 provider 的 `registerInterrupt` 等接口接到
  controller；当前平台没有这条 IOKit GIC provider 链，nub 也没有中断 specifier。
- provider=NULL 时可以手动调用事件源的 `interruptOccurred()`；若换成 filter
  子类，normalInterruptOccurred 仍可执行 filter。**不是技术上不能使用 filter**。
  但 GIC callback、屏蔽、重开和退出同步仍要自己管理，不会自动获得 controller
  管理能力，也不应把 `this` 当成不存在的中断 provider 传入。
- PMIC-arb 的 `IRQ_STATUS` 可直接读 MMIO 判断 pending；并非全部来源识别都要
  SPMI。耗时/有界等待主要在 SPMI ack、外设触发配置和状态读取，应留在 workloop。
- 当前每个已投递的共享 PMIC IRQ 都需要 drain/ack。未知 pending 必须显式停用并
  留日志；不能让 filter 简单返回 false 而留下 level IRQ 不处理。本轮无明确的
  可跳过 workloop 的正常事件类别，因此保留短 callback 加普通软件事件源。

未来若引入真正 GIC controller，先提供 provider 的注册、type、mask/unmask、
handler 与退出同步，再让 mux 的事件源绑定那个 provider。若确有安全的快速
筛选需求再选择 filter；普通 provider-bound IOInterruptEventSource 本身也可以
处理 level IRQ 的屏蔽/下半部交接。PMIC 路由表和按键/电池回调接口继续保留。
本次没有把 DWC3、触摸或定时器迁移到新 GIC controller。

## 匹配与启动

```mermaid
flowchart TD
  BOARD[BSD 晚期 sweet_board_init] --> PUB["sweet_spmi_mux_start：仅发布 nub"]
  PE[SweetPlatformExpert] --> NUB["SweetSpmiMuxDevice<br/>板级创建的晚期设备"]
  PUB --> NUB
  NUB --> MATCH[KernelConfigTables personality 同步匹配]
  MATCH --> SVC["SweetSpmiInterruptMux : IOService<br/>init / start / stop / free"]
  SVC --> OWN[实例拥有 routes / bindings / MMIO / workloop / 软件 IES]
  SVC -. READY / FAILED .-> BATT[SweetBattery 自己的 workloop]
  BOARD --> KEY[按键消费端]
  BATT --> API[attach / detach C 兼容接口]
  KEY --> API
  API --> OWN
```

`SweetSpmiMuxDevice` 是板级在已就绪的 platform expert 下创建的服务 nub，
不是新编造的固件 DT 节点，不是 GIC controller。`KernelConfigTables.cpp` 只在
MH2LM 配置增加 provider=SweetSpmiMuxDevice、driver=SweetSpmiInterruptMux 的
personality。nub 的 `registerService(kIOServiceSynchronous)` 让 IOKit 正常匹配，
不是手工调用驱动 `start()`。

保留 BSD 晚期阶段：IOBSD/autoconf 与 inittodr 后、root lookup 前。nub 同步匹配
返回 READY/FAILED 后，board 才继续 keys/touch/USB 初始化。早期 battery 不阻塞
此过程，继续用 0142 就绪订阅；它可以在匹配前或后启动。关闭 keys 不影响发布。
匹配失败或没有驱动时发布 FAILED，board 仍继续独立设备，无轮询兜底。

`init()` 建立实例初值；`start()` 建立自己的 IOWorkLoop、owner=this 的软件
事件源、公共映射和有界 GIC probe，注册服务后发布 READY。仍没有 consumer
时不 arm 外设。三键和电池继续通过旧 C API 接入，运行时处理路径不变。

IORegistry 层级为 `SweetPlatformExpert -> SweetSpmiMuxDevice ->
SweetSpmiInterruptMux`。服务发布 `SweetMuxLifecycle`、`SweetMuxIntID=513`、
`SweetMuxHardwareProvider=false`；后者明确上游仍是板级 callback。
按键仍是 BSD consumer，电池仍按原 IOResources personality 匹配，**两者不是
本次新建的 mux 子 IOService**。未来完整子驱动 provider 化需另行实施。

## 生命周期与内存约束

1. 可变 routes、bindings、channel、计数、状态机、workloop 和事件源归实例。
   全局只保留启动前 rendezvous、受锁保护的服务/IRQ 指针，以及不可变模板。
   READY 订阅锁与 IRQ 指针锁保留整个内建模块的启动寿命。
2. C API 在 readiness lock 下取得服务临时引用，释放锁后才等待 mux gate。
   workloop 到 `free()` 才释放，保证已取得引用的调用不会遇到 stop 提前释放 loop。
3. `stop()` 进入自己的 workloop gate。先阻止新 attach，再短暂取得 IRQ spin
   lock，屏蔽 SPI 并断开指针，等待正在执行的上半部离开。上半部不取 workloop
   gate、不做 SPMI、分配或日志，因此不会与 gate 反向等待。
4. 在同一 gate 下停止硬件、通知 consumer 失败、清理自己的 route 位和所有
   callback/context，禁用并移除事件源；随后才取消 C API 查找发布。并发 detach
   可在同一 gate 同步，不会因过早隐藏服务而错过仍在使用的 context。
5. `free()` 释放事件源、workloop 和所有实例映射。部分 start 失败也走清理；
   没有接触硬件配置前的失败不会擅自写 PDC。共享 fatal 仍影响所有 consumer。
   当前服务的停止/失败是本次启动的终态，不支持停止后自动重建或重新试写
   已超时的 APID；必须重启，不隐藏恢复到轮询。

**映射使用可释放的路径。** 本版 `ml_io_map()` 的 late 分配使用 io_submap +
KMA_PERMANENT，不能配对 `ml_io_unmap()`；0144 使用
`ml_io_map_unmappable(..., VM_WIMG_IO)`，保留相同 MMIO 属性和物理跨度。
释放必须 `trunc_page(VA)` 与 `round_page(size + page_offset)`，尤其 CFG
物理地址 `0x0c40a000` 在 16K 页中偏移 `0x2000`。不能只释放原始 0x1000
寄存器范围。每 route 缓存已映射 channel，重复 attach 不重复分配；每次仍先查
唯一 EE0 IRQ owner，拒绝缓存 APID 与新解析结果不一致，避免复用错误映射。

## 验证与后续实机

本地输入/基线/重放/编译证据放在工作区 `scratch/mux-service-0115/`。
ARM64 编译检查 mux、KernelConfigTables 和 board；Linux Apple type builtin
替身只存在编译脚本中，不能替代原生完整链接。

已有 board 和 battery/mux 测试继续使用；服务测试编译真实实例代码，仅替换
IOKit 匹配/调度、MMIO/时间和部分 battery 采样/property。新增覆盖：未匹配、
init/loop/event/map 失败、已有 pending 时 stop、battery stop 并发、IRQ/API
与服务 stop/free 并发，最后检查所有映射释放、无回调到已删除实例、无 IRQ
指针断开后重新开启 GIC。包含 16K 对齐/CFG 偏移释放与 channel 缓存检查。
ASAN/UBSAN 的模型不证明真实 IOKit 匹配/调度和手机 IRQ/清理时序。

用户 push 后仍一次云编译，用 `--board-startup` 打包三档：baseline -> no-keys
-> usbin，按 [`DRIVER-STARTUP.md`](DRIVER-STARTUP.md) 顺序检查。
新增必需启动记录为 `SWEET MUX: IOService started; owns workloop, routes and MMIO`，
再看 consumer attach/armed；未匹配先停在启动问题，不解释成 PMIC 极性故障。
主机日志与持久日志一起保留。暂不要求重复旧硬件预检或运行 CHGR/MSOC。

CircleCI 仅新增最终源码/匹配标记门禁，构建命令未改。打包拒绝缺少 IOService
标记的旧 board-startup 内核。正常手机回归不主动 terminate 服务；stop/free 的
真实硬件行为尚未验收，不以模拟结果冒充实测。
