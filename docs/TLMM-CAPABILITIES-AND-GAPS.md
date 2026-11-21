# TLMM 已实现能力、功能缺口与待修项

更新：2026-11-21。本文记录现状，不实施修复或新增功能。代码快照为 `4ba6922`，普通实机基线为 b32 / `b3f735b`；TLMM 当前实现承接 b31r2 / `5e1680b`。总线层与触摸 C++ 迁移是独立后续，不计入 TLMM 本身的未实现能力，也不因本清单自动启动。

结论：普通 GPIO、独占句柄及共享 pad IRQ 的主体已具备，当前消费者的普通运行路径已有实机证据；完整 pinmux、运行期所有权防护及通用低功耗等能力尚不能全部标为完成。两处现有源码待修项见 TLMM-C01、TLMM-C02，均未修复，且没有对应实机故障证据。

## 依据与范围

- [AGPIO 与控制器接口](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.h)、[控制器实现](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp)、[寄存器与 C 接口](../dxb/xnu/pexpert/pexpert/arm64/tlmm_gpio.h)。
- 固定 LG Lineage 参考版本 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`：[SM8150 分组/位定义](../../lge-lineage-kernel/drivers/pinctrl/qcom/pinctrl-sm8150.c#L39)、[功能选择写入](../../lge-lineage-kernel/drivers/pinctrl/qcom/pinctrl-msm.c#L203)。不依据旧调试输出的三位解码推定真实 MUX 字段宽度。
- 普通功能证据：[b32 验收](DISPLAY-TOUCH-FAILURE-FALLBACK.md#b32-普通实机验收2026-11-21)；IRQ 的合同与分批证据见 [GPIO 计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)、[第5批叶子掩码](GPIO-IRQ-STAGE5-LEAF-MASK.md)、[第6批异常隔离](GPIO-IRQ-STAGE6-STRAY-CONTRACT.md)。
- 下表的“已实现”表示存在相应生产路径，不等于所有175个GPIO、全部触发/复用模式或所有硬件交错均已验收。软件接口缺失也不等于硬件一定没有该能力。

## 已实现能力

| 能力 | 当前实现 | 使用与验证边界 |
|---|---|---|
| 普通 GPIO 寻址 | SM8150 175个普通GPIO，四个tile窗口，按全局pin编号定位；引脚范围检查 | 不等同已覆盖SDC/UFS reset等专用pad组；不是175个引脚逐一实测 |
| 输入/输出 | 读取实际pad输入，设置输出锁存，切换输入/输出方向 | 实际输入与输出锁存分别读取；电气特性并非仅靠软件读回证明 |
| 引脚配置 | 数字MUX、无上下拉/下拉/keeper/上拉、驱动强度2–16mA编码；支持逐字段及组合configure | MUX目前仅0–7，存在C01；驱动强度是设置编码，不是本项目完成电流测量 |
| 输出配置顺序 | 组合配置先写目标锁存，再开启输出 | 单字段setDirection使用既有锁存；不承诺任意复用切换或多引脚切换都无电气毛刺 |
| 所有权 | AGPIO独占申请、拒绝重复占用；plain写接口拒绝占用中的pad；允许共享诊断读取 | 合同约束本控制器API，不能拦截绕过API的旧裸MMIO；不等于所有消费者迁移已经完成 |
| island只读检查 | 申请句柄、plain写接口及相关IRQ路径检查ADT只读声明/实时EGPIO enable | 已持有AGPIO的普通配置/输出快路径仍有C02；不能笼统写成“每次写均已防护” |
| 配置与原值读回 | 获取解码字段及原始CFG、IO、INTR_CFG；查询pin owner | owner查询是诊断线索；解码MUX有C01；读回不是所有硬件完成/输出电压的证明 |
| pad IRQ | 高电平、低电平、上升沿、下降沿、双边沿；注册、prepare、mask、resume、ACK、注销 | 必须通过持有该pad的AGPIO注册；五种模式有源码实现，不等于全部模式均已实机验证 |
| 共享summary | 控制器持有INTID240，按已注册pad状态派发；首次真实resume才开线；短dispatch门控 | 软件注册表最多同时8个pad IRQ客户端，与普通GPIO数量175是两回事 |
| 独立叶子屏蔽 | 一个客户端deferred时只屏蔽自己的pad，保留其它已武装pad服务能力 | 多客户端交错有host证据；设备普通验证使用单个GPIO122 IRQ消费者 |
| 生命周期 | 注销时关闭入口并排空在途dispatch快照；stop排空IRQ/诊断并失效句柄 | owner仍须串行化自身普通方法与teardown；不等于完整上游GIC provider热拔插寿命证明 |
| stray异常处理 | 无服务投递持久hold summary；线程输出诊断；普通resume/换slot不清hold；最后slot退出仍保留claim | 受控持续异常电平及显式恢复尚未实机验证，不把日志限流当隔离 |
| 显式恢复与探针 | 线程诊断恢复入口，带外部清除确认/序号/来源；sole-client软件pending探针及有界单次排空 | 恢复入口目前没有生产调用者；不是自动恢复，设备当前可用恢复方式仍包括重启 |

主要接口位置：[普通GPIO与配置](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.h#L55)、[中断及生命周期](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.h#L82)、[控制器恢复入口](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.h#L245)。

接口上下文也需要区分：plain C接口持可睡眠状态锁，不可在硬IRQ使用；注册、prepare、软件探针与注销是线程上下文操作，mask/resume/ACK有IRQ安全路径。当前回调仍是硬IRQ上半部，由消费者投递其worker，控制器没有自动线程化回调层。

## 尚未实现的能力

这些属于功能扩展，不与下面两项现有正确性问题混记，也不默认全部成为当前消费者的前置。

| 能力缺口 | 当前情况 | 需要它的场景/后续边界 |
|---|---|---|
| 控制器系统PM与保存/恢复 | 尚无完整TLMM suspend/resume、丢失状态后的配置重建合同 | 整机睡眠/掉电恢复；普通屏幕OFF/ON不等于控制器深睡周期 |
| GPIO作为系统唤醒源 | 没有通用唤醒使能、唤醒路由和恢复后的事件交接接口 | 需要配合上游中断/唤醒控制器及平台低功耗路径，不是只加一个标志 |
| 命名pinctrl状态/组 | 没有通用default/active/sleep状态、成组申请与应用机制 | 设备驱动不再自行散布逐pin配置；组操作也不能无依据承诺电气同时翻转或硬件事务回滚 |
| 逻辑active-low描述 | 值接口表达物理高/低，尚无统一assert/deassert反转语义 | reset、enable等消费者；与已支持的低电平IRQ触发不同 |
| 统一开漏/开源接口 | 没有统一声明及驱动/释放操作 | 现有输出锁存+输入高阻可作为组合基础，不代表已有完整开漏封装或已确认原生硬件模式 |
| 去抖 | 没有公共软/硬去抖API | TLMM上的机械输入等；硬件是否支持及适用范围需另查，不把软件缺接口写成芯片不支持 |
| 完整功能名称与合法组合表 | 现有MUX接口使用数字，缺少按pad验证function/NA槽和命名组映射 | 通用外设复用；同时须先处理C01 |
| 批量/通用消费者接入 | 无通用多GPIO批量接口；IRQ主要采用自定义AGPIO回调，未提供每pad通用IOKit interrupt-provider接入层 | 随消费者增加按需扩展；当前8个IRQ槽位是软件上限，不是硬件只有8路 |

## 已发现的源码待修项

### TLMM-C01

**MUX字段宽度与选择范围不完整。状态：源码和静态位运算已核实；未修复，未实机复现。**

- 当前[字段定义](../dxb/xnu/pexpert/pexpert/arm64/tlmm_gpio.h#L85)为`MUX_SHIFT=2`、`MUX_MASK=0x7`，`setMux()`拒绝大于7的值，[getConfig()](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp#L247)也只解码三位。
- 固定SM8150参考的PINGROUP有10个功能选择槽，`mux_bit=2`、`drv_bit=6`；实际[写入函数](../../lge-lineage-kernel/drivers/pinctrl/qcom/pinctrl-msm.c#L203)按`order_base_2(nfuncs)`产生四位掩码。参考表确有有效高槽：GPIO81/selector8 emac_pps、GPIO89/selector8 tgu_ch0、GPIO90/selector9 wlan1_adc1、GPIO145/selector8 ddr_pxi0。
- [setMux()](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp#L320)只清除bits[4:2]，未清bit5。静态算例：初值CFG=0x20（选择8），调用setMux(0)后按现有表达式仍为0x20，实际选择仍是8；三位解码却会报告func0。这是字段处理问题，不只是“尚未支持少数新功能”。
- `configure()`会清整个CFG低10位，因此与单字段setMux并非相同后果，但仍不能表达selector8/9；不能把所有设置路径概括成同一个未清位行为。
- 修复前应增加独立于现有三位常量的字段测试：从selector8/9切换到0/1/7，读回完整字段，并验证pull/drive/OE/EGPIO无意外修改。扩大字段时还要明确每pad合法选择和NA槽，不能简单开放所有0–15值。

该项不推翻b32已通过的普通实机记录，也没有证据把过去的触摸复位、stray或显示冻结归因于此。

### TLMM-C02

**AGPIO普通写快路径缺少运行期island接管后的统一防护。状态：源码已核实；未修复，未做对应实机移交/故障注入。**

- [acquirePin()](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp#L809)在申请时检查只读声明及当前EGPIO enable；plain写入和相关IRQ路径也有相应检查。
- 已有句柄的[setValue/setDirection/setMux/setPull/setDrive/configure](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp#L273)主要检查`fValid`，经缓存的CFG/IO指针直接读写；[writeCfg/writeIo](../dxb/xnu/iokit/Kernel/arm/TLMMGPIOController.cpp#L137)没有重新检查运行期EGPIO所有权。
- 因此若申请后pad被固件转为island-owned，普通快路径没有与其它入口一致的拒写门禁。保留EGPIO位本身不等于禁止应用处理器继续写其它配置或输出。
- 后续应先明确运行期所有权移交合同，再统一拒写与错误返回。仅在store前采样一次不能证明与外部固件移交具有原子性；也不能为加保护把睡眠锁引入需要IRQ安全的快路径。
- 建议的确定性测试：取得句柄后由独立MMIO模型置EGPIO enable，逐个普通写API验证拒绝且CFG/IO无写入，读操作仍可用；同时回归已有IRQ安全hold、注销排空及stop。外部移交的真实硬件安全仍需独立证据。

## 验证边界与收口顺序

- b32普通实机：首次接管、76 down/76 up、峰值五指、两轮OFF/ON完整；第一轮醒后无HID接触记录，第二轮醒后1对。该记录验证当前消费者普通路径，不证明所有pad/配置模式。
- 多pad叶子并行、确定性交错、stray持续隔离/显式恢复等有相应host证据；多GPIO硬件并行、受控持续异常电平/显式恢复、物理rail-off、完整控制器PM和长稳仍未验。
- b32的真实触摸终态显示降级未在设备上触发，只保留其已有host证明；它属于显示与触摸协调，不是TLMM新增功能。
- 建议先将C01/C02作为独立正确性修复批次，再按消费者需要补逻辑极性、开漏和命名状态；PM/系统唤醒在平台低功耗阶段单独推进。这里只记录建议与验收标准，不是本轮已经实施或自动授权的修复。
- 本次只更新记录/索引/问题入口，不修改驱动、测试、CI、固件、ADT或镜像，不重跑同版构建和实机。
- 本次文档核对底稿：工作区 `scratch/tlmm-capabilities-20261121/verification.json`，记录固定源码摘要、四个高selector参考条目、C01静态算例、122个本地链接检查及生产代码未变。
