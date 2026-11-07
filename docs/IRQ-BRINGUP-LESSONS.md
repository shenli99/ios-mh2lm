# IRQ 适配复盘：按键 0114a → 0114a10

**用途：** 后续 DWC3 USB、触摸及 PMIC consumer 适配时的执行检查表。
本页只记录已发生的问题、证据边界和可复用做法；详细日志 SHA256、
阶段过程与硬件值见 `NOTES.md` 和对应硬件基线。

| 踩过的坑 | 当时的证据与结论 | 下一个 IRQ 驱动怎么做 |
| --- | --- | --- |
| 只凭文件名/镜像名判断测试内容 | 0114a3 的 `route-map` 镜像实际用了尚未包含 0131 的内核，仍执行 map 加 `ACC_ENABLE`；0114a6 与 0114a5 三份产物逐字节相同 | 每轮先核对已推送 commit、CI 应用补丁列表、buildtree 中的实际源码、内核 manifest 和镜像内启动参数。编号只能当索引，不能当版本证据 |
| 把旧持久日志当成本次失败现场 | 初次 0114a 和 0114a3 的部分导出文件与前一轮日志 SHA256 完全相同；失败发生在新日志 flush 前，旧内容不能定位出错 MMIO | 校验新旧日志 SHA256、ring total/flush 和本轮 boot args；USB 未枚举时仍保留持久日志，但没有新内容就明确记为“无现场日志” |
| 一次诊断触及太多寄存器 | 0114a 全量预检回 LG BSP，不能知道是哪一步；拆成 helper、route、channel map、channel read 后才收窄 | 首个会触碰新 MMIO 的镜像要有独立开关和停机点；一个云端内核可打包多个启动参数镜像，前一阶段失败就停止下一阶段。源码/历史实测已明确的值不要再重复做只读摸底 |
| `ml_io_map()` 成功就认为寄存器可读 | 0114a5 的纯 channel 映射成功；读取重复 PPID 对应的外部 EE channel 才高度疑似导致 LG BSP 返回。PON PPID `0x8` 同时映射 APID 1（EE1）、19（EE4）、248（EE0） | PMIC-arb 先查当前 EE 与 IRQ-owner，只读/写本 EE 拥有的 channel。任何 provider 都需区分地址可映射和实际可访问；没有旧失败现场时不要声称精确是 APID 1 或 19 |
| 混淆 DT SPI 号、XNU INTID 和中间路由 | 按键的 DTS `GIC_SPI 481` 是 INTID 513，且先经过 PDC pin 1；将它们当成同一个编号会配置错源 | 在设计图上完整写 `外设 → 中间控制器 → DTS SPI N → GIC INTID N+32 → XNU callback`。DWC3 子节点是 SPI 133 / INTID 165；wrapper 的 `pwr_event_irq` 是另一条线 |
| 直接照搬参考项目的注册 API | 0114a7 实际 XNU 使用内建 `SweetCPU`/`sleh_irq`，没有 UniXNU 的 `armcpu_spi_attach` | 在准备补丁前查**实际 buildtree 和符号**，再定唯一的 GIC callback、EOI 前分发、软件事件源与 worker。Linux `irqdomain` 和其他板的 helper 只作语义参考 |
| 将 GIC Group 位读回当作唯一健康检查 | 0114a9 的 SPI 513 `IGROUPR` 写后读回 0，驱动提前回退；0114a10 保持相同读回 0，却通过软件 pending 探针并获得真实按键 IRQ 命中 | 可记录 Group/优先级/路由读回，但非安全视图的 Group=0 不能单独否定投递。先在外设 IRQ 门关闭时对目标 INTID 做**有界软件 pending**，再用真实设备事件证明端到端线路。软件 pending 只证明 GIC 路径，不能代替设备事件测试 |
| 过早打开所有 IRQ gate 或把耗时操作放在上半部 | 0134/0135 的安全顺序使首次 Power 试验失败时 PMIC/PDC gate 仍关闭；实机成功后 GIC 上半部只屏蔽并通知 IOWorkLoop，由 worker 做 PMIC ack/HID 唤醒 | 先建立 callback、worker、状态机和回退，再配 GIC、清旧 pending、开单源设备 gate，最后逐步并发。上半部短小；确认顺序、风暴上限、卡住时屏蔽与恢复轮询必须在放开硬件前具备 |
| 以功能表象或节流日志代替 IRQ 证据 | 0114a9 的 Power 正常是 20 ms 轮询；0114a10 有 `hit route=` 才证明 IRQ。三键日志只逐条打印前 32 次，此后每 64 次打印一次，后面没有逐次 `hit` 不表示停止 | 同时看 GIC callback、worker、route hit、设备状态和 HID 按下/松开；记录日志节流规则和回退计数。保留的 200 ms 恢复轮询意味着单独的按键状态行不能证明每次输入都由 IRQ 唤醒 |
| 调试 USB 时未使用已验证的日志通道 | 0114a9 按键镜像继承 `sweetusb=0`，浪费了已调通的 CDC-ACM；0114a10 开启后实时日志与按键 IRQ 并行正常 | 后续把 USB 实时日志当作工作工具，开机前启动 host 读取脚本；同时保存持久 SWEETLOG，覆盖 USB 尚未枚举、链路故障或 USB 自身 IRQ 失效的阶段。无需单独重测 CDC-ACM 基本可用性 |

## 下一轮 DWC3 USB IRQ 的执行顺序

1. 核对当前内核源码/产物与 `docs/USB-IRQ-HARDWARE-BASELINE.md`：
   DWC3 event buffer 的候选中断是 SPI 133 / INTID 165；确认它没有被
   wrapper 的 PHY/电源事件或其他 XNU handler 占用。
2. 在现有 1 ms 轮询保持可用时建立独占 callback、短上半部、worker、
   `GEVNTCOUNT` drain/ack、计数器和明确的回退路径。先让软件 pending
   验证 INTID 165 到 XNU，不打开 DWC3 event interrupt mask。
3. 只在上述步骤成功后清除 `GEVNTSIZ.INTMASK`，用真实枚举、传输完成、
   bus reset 观察中断是否重复到达、是否正确消退；任何失败切回
   已验证的轮询路径，持久日志记录切换原因。
4. 同一云端编译保留当前轮询模式，并可选择 USB IRQ 或 USB＋
   `sweetkeysirq=3`；先测 IRQ，轮询镜像只在需要回退或定位问题时启动。
   每张实际启动的镜像提取 boot args 和 fresh log，
   不因 USB 实时输出正常就省略早期持久日志。

PMIC EE/APID 过滤是按键/电池共享 PMIC-arb 的专属规则，**DWC3 本身
不走 PMIC-arb**；不要把它机械套用到 USB 或触摸。USB、触摸各自的
设备侧 ack、mask 和可投递性仍需以本机真实事件验证。

## 0115 电池新增复盘：先核对初始化依赖

`sweetlog-0115-battery-irq-usbin.bin` 中，电池 `preflight=1`、初始采样
正常，但十秒后记录 mux not ready 并停用；随后 BSD 按键初始化才成功
启动 mux。电池路由一次也没接上，插拔不刷新、息屏不因电源事件唤醒，
不能据此诊断为 USBIN 极性、ack 或 framebuffer 问题。

早期 IOKit consumer 依赖晚期 BSD 初始化的共享服务时，应订阅就绪事件、
立即返回，让后续启动阶段推进；不要提前执行有阶段要求的 GIC/MMIO，也
不要以固定短超时猜测依赖何时存在。0142 使用单次 READY/FAILED 回调只排队，
真实工作仍在 consumer 自己的 workloop，取消订阅必须同步回调寿命。
等待必须可观察，实际初始化失败必须显式报告，均不能切换静默轮询。

新增测试不仅模拟“mux 先启动”，还覆盖 consumer 先启动且 mux 晚于原超时、
订阅与发布竞争、订阅前后初始化失败、等待期间退出和通知排队后退出。
本地测试通过仍不证明实际外设 IRQ 投递，下一轮必须先看到 attach/armed，
再用真实插拔证明 route → worker → IOPM 全链路。


0143 继续处理就绪机制背后的归属错误：公共 mux 不能让某个 consumer 启动，
无存储依赖的外设也不能让 UFS root 初始化启动。改成独立晚期 board 入口，
consumer 自己 attach/detach；无 consumer 时不 arm，最后退出后不重开 SPI。
测试需覆盖 keys=0、按键预检/接入失败、最后一个 pending detach、上游 arm
失败，以及 root 重试不重复启动。调用链与限制见
[`DRIVER-STARTUP.md`](DRIVER-STARTUP.md)。就绪回调解决阶段交接，不能代替
初始化归属清理；二者都不等于真实硬件 IRQ 已通过。


0144 IOService 化新增约束：stop 必须先取得 mux gate，在 gate 内断开 IRQ
指针并同步上半部，再清理 route/context；先断开指针再等 gate，可能让并发的
attach/drain 在窗口内重新开 IRQ。C 兼容 API 需先取得服务引用，workloop 保留
到 free，才能支持并发 detach/stop。当前 XNU 的 ml_io_map 是永久分配；需要
释放时用 unmappable 路径，按 16K 页对齐包含地址偏移的完整范围。详见
[`MUX-IOSERVICE.md`](MUX-IOSERVICE.md)。

## 0116 编译复盘：保持编译单元自己的参数环境

0144 补丁正常应用，但 mux 直接包含 ARM memory_types，未引入通用
`VM_MEM_*`，导致 `VM_WIMG_IO` 展开失败。使用 `<vm/memory_types.h>`
可同时获得通用位定义和机器相关定义，无需硬编码数值或改变 MMIO 属性。

旧本地检查把 BSD 的参数/头文件路径叠加到 IOKit，掩盖了缺失依赖。
以后单文件交叉编译优先从实际 CI 对应编译单元提取完整命令，保留该子系统
的 include 顺序和 private 宏；本机适配单独记录。此次同一 IOKit 命令下
修正前复现、修正后通过，才构成编译修复的证据。剥离 include 的 mock
运行测试不能验证真实头文件依赖；本地交叉编译也不能替代原生完整链接。

## 0120a9 / 0161：新IRQ路由必须核对真实transport权限链

0160添加Type-C路由后，mux解析出APID271，但配置第一条route就失败。原因是
`readPeripheral`仍用旧`sweet_spmi_read`，其软件白名单拒绝0x15；状态采样的
另一个typed接口能读Type-C，不代表IRQ配置也能读。第一笔0x1511在软件校验
阶段被拒绝，没有硬件事务，不能按极性/IRQ owner/充电器协议去试错。

新增route前逐项核对：调用接口、SID/peripheral/offset、observer/writer/IRQ
权限、锁和错误输出；实际transport与mux至少有一条贯通测试。旧mux模拟读取
允许所有外设，掩盖了白名单限制。0161用固定SID2/外设/配置offset只读接口，
不扩大通用原始访问范围；新增真实readPeripheral+完整SPMI代码联测，模拟层
也恢复旧接口对Type-C的拒绝。

consumer attach失败仅说明该订阅不可用，不能自动把日志写成共享mux或所有
按键已停止。这轮失败后keys/ADC/touch仍工作。失败停止事件生成时，还必须
通过正常IOPM发布撤销旧external/charging及legacy标志并标记input-invalid，
不能只停线程、保留用户可见的有效旧状态。保留上次百分比不等于伪造新测量。
完整证据与修复见[CC记录](BATTERY-TYPEC-CC.md)。
