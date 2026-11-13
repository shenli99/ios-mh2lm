# 0216：按 Lineage 补齐当前 RSC / ACTIVE 接管路径

2026-11-13。实现提交 `eb72381`。基于 0215，参照 LG G8X Lineage
`2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。a28 实机在 qtimer 初始化 index4 的全值相等检查失败；
后续 [0217](PANEL-RSC-QTIMER-INIT.md) 将六项 CNTP 读回改为诊断，其他检查保持。
下文是 0216 实现与本地验证的原始记录，最新执行点见 NOTES。
a27 已经取得 RSC 时钟，但首组 display NoC ACTIVE2 超时；未提交新 DMA。
本补丁替代“继续只加诊断”的交接，云端应编译包含 0216 的最新提交。

## 已实现的流程

1. 保持既有 apps CN0、ARC、GDSC 和三路 RSC 时钟的权限检查。
   未取得时钟租约时不访问 RSC。启动只映射资源并注册软件中断事件，不初始化硬件。
2. 进入 CLK 工作状态：读真实 solver override、modes、STATUS0/1/2，清 wrapper
   override.bit0，使用 MMIO 屏障提交；不再把 `+0xd0` 叫作 solver status，它是
   `ERROR_IRQ_STATUS`。STATUS2 是选择的模式，不要求所有 solver 状态为零。
3. 按 `rsc_hw_tcs_wait()` 交接使用权：wrapper CTRL 写 0；若 SEQ_BUSY.bit1 为 1，
   写 1，让出 200 µs，再写 0，异步等待。等待上限采用 1 ms 时间预算，timer 请求 2 µs，
   不在 workloop 或自旋锁里忙等。与 Linux 500 次、每次 1–2 µs 的调度实现不同。
   wrapper 和 DRV sequencer 都空闲后才写初始化表/命令；交接超时且尚未改表/提交时，
   像 Lineage 一样写回 TCS_USE_OK=1。
4. 首次取得使用权后执行 RSC v2 的 65 项初始化写入：qtimer → wrapper →
   sequencer memory/branch addresses → solver → PDC 接口。逐项异步处理读回，
   防止部分初始化后仍继续提交。首次初始化完成后，同一实例后续请求不重复改表。
5. Display RSC 的 SPI129 接为 GIC INTID161。先注册 handler，再配置本 IRQ 的
   level/priority/route，随后启用所选 ACTIVE 槽的 source。独立于 apps37 和显示 TE115。
6. 每组按 VCD 提交，最后一条保留 COMMIT/response。顺序为清 trigger → 清 AMC →
   置 AMC → source enable → trigger → GIC enable。触发前拒绝已经 pending 的启用源；
   触发后不清 pending，因此极快完成也能经 level IRQ 送达。
7. 顶半部只屏蔽 INTID161、记账和排队事件。workloop 重验资源权限、所选 IRQ 源及
   ISSUED/必要的 COMPLETE；只有真实回调能启动完成确认。旧 CMD_STATUS、裸 IRQ_STATUS、
   曾经看到 busy，都不能替代本次中断。timer 负责等待期限/写读回/EOI 排空。
8. 成功后像 Lineage ACTIVE 路径一样只清 CMD_ENABLE、WAIT 和本槽 IRQ，保留 AMC/control
   及 source enable。槽位所有权账本只允许复用本实例成功清理的槽，不接管未知继承 AMC。
   接受自身槽 trigger 保持或硬件自行清除两种状态；复用前等待 GIC EOI/RWP。
9. 所有组完成后交还 TCS_USE_OK=1，再按既有引用账本保留/释放时钟和 CN0。
   失败后不清未知请求、不撤资源票；stop/失权只可屏蔽独立 GIC，不读写失去访问权限的 RSC。
   永久 dispatch lock 保证停用、移除事件和释放映射不会越过正在执行的物理回调。

## 初始化来源及边界

寄存器偏移和写入顺序来自 `sde_rsc_hw.c/.h`，计时来自
`sde_rsc_timer_calculate(NULL, IDLE)` 和 probe 的 v2 延时配置：

| 参数 | Lineage 默认初始化结果 |
|---|---:|
| FPS / vtotal / 限制后的 prefill | 60 / 505 / 35 |
| static wakeup | 298299 |
| mode threshold | 17938 |
| time slot 0 / 1 / 2 | 35771 / 320512 / 0xffffffff |
| RSC / PDC backoff | 17886 / 186020 |

保留源码整数除法顺序；PDC backoff 的单位与其他字段不同，不能统一除 52。
初始化模式使能表为 7，但 wrapper override.bit0 为 0，工作路径仍为 CLK/软件 ACTIVE；
该表的存在不代表已经实现 CMD solver 调度。

本轮补的是**当前电源和时钟已具备的首次资源接管**，不能宣布整个显示驱动已等价于 Lineage。
失电/继承 mode2 collapsed 状态仍拒绝准入，没有新增无条件唤醒或复位。
CMD/VID/IDLE 切换、VSYNC 协同、按实际面板重算 CMD timer、sleep/wake BCM 表及完整
mode2 进出仍待实现。RSC 的 PDC 接口初始化也不等于重建 SoC 外部 PDC 的全部内容。
后续 PHY/host 的 retained 重建与 Lineage continuous splash 跳过重建的差异，及 MDP/SMMU
继承管线与完整独立配置的差异，保留在对照文档中。本轮没有冒充已经修好这些后续阶段。

## ADT 与产物

`--display-noc` 现在必须同时声明：

- `sweet,display-noc-abi = 1`
- `sweet,display-noc-irq-abi = 1`
- `sweet,display-noc-intid = 161`

复用 MDSS 第 11 号 GICD 映射，16 个 MMIO 窗口保持不变。缺少/错误的 IRQ ABI 会明确失败，
不会退回 polling。metadata identity 档没有 NoC，不受影响。ABI608 固件和 605 字节 retained
bootargs 保持；硬件档 ADT **需要重新生成**，不能直接沿用 a27 ADT。
matrix 新增四条 RSC/IRQ marker；原 CircleCI workflow 不改。

## 本地验证

- 16 套既有 provider/时钟/core/NoC/managed/lifecycle 回归通过（ASan/UBSan）。
- 完成专项已更新为 37 个真实 provider 场景：初始化失败、握手忙/超时、ABI/映射拒绝、
  source/GIC 顺序、触发内快速完成、没有回调、外来/不完整中断、EOI/RWP、邻位保持、
  故障取证、停止与物理回调并发、迟到事件和对象释放。
- `audit-mh2lm-lineage-rsc.py` 动态提取并执行 Lineage 的实际计时及五个初始化函数，
  与生产表比较，65 项写入逐项逐序一致。测试不提供硬件仿真，也不证明真机已经接受写入。
- ADT 全组合及 CLI 回读通过；默认/identity 和其他 provider 的资源布局保持。
- SweetMDSS.cpp、sleh.c 的真实 KDK ARM64 单文件编译通过。SweetMDSS 告警记录
  1561→1573，增加 12 个固定表/数组访问的 unsafe-buffer-usage，未屏蔽；sleh.c 1 条既有告警。
- 0185–0216 共 32 个补丁严格重放，78 个源码文件字节一致，114 个原 CI source markers 通过。
- 实际旧 a27 产物被当前 matrix 拒绝，没有生成启动镜像。

NoC 的 `POLL` 日志名为保持取证工具兼容而保留，现在只记录 IRQ 后的完成检查。
没有中断时 samples=0、status-valid=0 是“未采样”，不是硬件状态为零。
group timeout 仍可在权限有效时采集四个配置寄存器；overall/失权/stop 不新增外围读取。

验证底稿：`scratch/framebuffer-0216/`。补丁：
`xnu/patches/0216-mh2lm-rsc-takeover.patch`，SHA256
`403fbaea29ca77d277052a075650387e7947f0e3fcc3eec1b1f2517222f10eb2`。

## 下一轮实机

用户 push 并云编译，下载 kernel/buildtree（可继续编号 0121a28）。agent 核对源码/内核，
重新生成带 INTID161 声明的 retained ADT 并打包，然后用户临时 `fastboot boot`。
完整接管前，亮度、OFF/ON、长测和 combined/supply 实机测试继续暂停。

USB 记录重点顺序：CLOCK Held → RSC enter CLK → v2 initialized → TCS ownership ready →
INTID161 configured → 每组 fresh ACTIVE completion → NoC ACK → 后续新 DMA/transfer/HANDOFF。
若失败，以新首错为准，不把模拟通过或时钟 Held 当作完整接管成功。
