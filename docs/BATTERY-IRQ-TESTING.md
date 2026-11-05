# MH2LM 电池 IRQ：0141 首版实现与测试

**日期：** 2026-11-05。补丁 `0141-mh2lm-battery-event-irqs.patch` 基于
0114a15 最终 buildtree（0138→0139→0140 已生效）生成。完成本地检查；0115
云端产物的最终源码、内核符号与标记已核对，六个测试镜像已生成并回读验证。
mode1 的 live 与持久日志已核对，采样和既有三路 IRQ 正常，启动记录明确
`preflight=1`，只读预检通过。尚未开启电池路由，不能宣称电池 IRQ 成功。
已验证的真实电池基线仍是 `live-0114a15-batt-irq-only.txt`。

## 实现路径与边界

```text
PM8150B USBIN / CHGR / FG
  → PMIC-arb / PDC / 唯一 INTID513
  → 原有 GIC callback（屏蔽 SPI、排队）
  → SweetSpmiInterruptMux 的 IOWorkLoop（路由、确认、通知）
  → SweetBattery 自己的软件 IOInterruptEventSource / IOWorkLoop
  → 合并事件、observer 读真实状态、IOPMPowerSource::updateStatus()
```

mux 用路由表描述事件和触发边沿，绑定接口与物理 GIC callback 分离；没有
按功能写死的 switch，也没有新增第二个共享 SPI 所有者。目前仍是板级静态
route 表与两个 consumer group，不是完整的可枚举 PMIC 子驱动 provider。
按键三个 route 保留；DWC3、触摸和 ACM 日志路径保留。

新增 `sweet_spmi_mux_attach/detach()` 在 mux workloop 下串行执行。回调只向
电池事件源排队，不能等待电池 workloop；detach 返回后才允许释放 consumer
和事件源，避免交叉 workloop 死锁或释放后回调。电池先发布初始采样，再 arm，
arm 后补读一次关闭启动窗口；mux 未启动时最多等待约十秒，不在等待中采样。

新增路由先解析唯一 EE0 IRQ-owned APID，再访问该 channel。拒绝接管已经
使能的对应外设 IRQ 位；保存并恢复自己的触发位，保留其他位。电池 observer
读取与新增 IRQ 写入共用 transport lock；写入限于 IRQ 配置/确认寄存器
`+0x11..+0x16`，不写充电控制、限流、SRAM、profile 或算法启动寄存器。
RW 事务超时后该 APID 本轮启动禁止继续写入，避免复用可能晚完成的命令。

接入局部失败时尝试撤回电池路由；清理成功则按键继续工作。若清理失败或发生
共享 mux 确认失败、未知 pending、IRQ 风暴，屏蔽共享 SPI 并明确通知按键和
电池，**这类故障会影响按键**。因此本轮必须回归三键并发。顺带修正原 mux
累计 256 次 IRQ 后停用的问题，改为一秒窗口超过 256 次的风暴限制；本机
正常/异常速率仍待验证。

IRQ 档没有两秒状态扫描或低频遥测兜底。除启动采样、arm 后同步采样外，只有
硬件事件触发刷新；若既有“大幅 SOC 跳变需两次一致”保护拦住一次结果，允许
该事件最多一次 100ms 确认读取，单独标记 `source=bounded-confirm`。
不能把确认读取当作独立硬件 IRQ 的证据。

IRQ 档遇到 gauge/charger 无效或 pack missing，保留上次已发布状态、标记
`SweetBatteryIRQHealthy=false` 和 `SweetBatteryHardwareSampleValid=false`，
停止采样并撤回路由，输出 ERROR；不发布伪造满电，也不切回轮询。
初始预检或初始采样失败可导致服务无法启动，同样应检查明确错误。
电压、温度、电流等完整电池管理和充电策略不是本补丁的交付范围。

## 一次编译的测试档位

所有镜像保留 `sweetbattery=1 sweetbatt=1`、按键/USB/触摸 IRQ 和 USB ACM。
下面的后缀由 `tools/mk-mh2lm-event-matrix.sh ... --battery-irq` 生成。

| 镜像后缀 | sweetbattirq | 用途 | 周期采样 |
| --- | --- | --- | --- |
| battery-irq-baseline | 0 | 新内核的真实电池对照基线，不接电池 IRQ | 两秒 |
| battery-irq-preflight | 1 | 只读 REVID、FG 和既有 USBIN/CHGR 预检 | 两秒 |
| battery-irq-usbin | 2 | 仅接 USBIN_PLUGIN，验证插拔到 IOPM 链路 | 无 |
| battery-irq-charger | 3 | USBIN_PLUGIN + CHG_STATE_CHANGE | 无 |
| battery-irq-all | 4 | 上述事件 + MSOC_DELTA + SOC_READY | 无 |
| battery-irq-all-debug | 4 | 同一全事件配置，sweetloglevel=3 | 无 |

事件 mask：USBIN `0x1`、CHGR `0x2`、MSOC `0x4`、SOC_READY `0x8`。
模式 2/3 故意不接电量事件，空闲电量不刷新符合该测试配置，不能用于完整验收。
旧 `--irq-only` 入口默认仍是 mode0；测试时必须核对实际 boot arguments。

预检读取 SID2 `0100..0105`，要求 type=`0x51`、subtype=`0x20`，FG
`4005` subtype=`0x10`。
0115 持久日志已实测确认上述 type/subtype 和 FG subtype，revision1..4 为
`0/0/0/0x2`，不是推测值。mode≥2 不匹配或配置读取失败就拒绝 arm。
FG 只读预检仅查看全局 APID/owner 元数据和 observer 寄存器，不读取新的
FG channel MMIO；FG channel 的核对和
使能留到 mode4。USBIN/CHGR 两条历史安全路径仍按当次状态重新核对。

MSOC delta 沿用固件现有配置，不访问 MEMIF/SRAM。LG 源码 0.3% 是静态参考，
**当前设备门限未知**。若真实电量已变化却无 FG 事件，保留失败证据，再单独
设计有授权/版本 workaround/释放的 SRAM 检查，不能靠后台轮询补齐验收。
容量 5020mAh 与目标 profile 的差异亦另案处理，本轮不改 SOC 缩放或容量。

## 产物打包与实机顺序

0115 的 `logs/xnu-kernel-0115.gz` 和 `logs/xnu-buildtree-0115.gz` 已核对：
五个 0141 文件与本地实现逐字节一致，按键/USB/触摸/日志等级源码保持既有版本，
最终内核包含电池 IRQ 符号与标记，manifest 校验通过。kernel SHA256 为
`a601cb597d38697832e4324fbb4341c75810034722ed95b139472bdb65dae6f2`。
本次未提供构建日志或 CI run ID，尚未审查完整构建警告。

六个临时镜像已生成于工作区 `out-bs/boot-16k-0115-battery-irq-*.img`；
最终 Android boot 镜像的 ramdisk、FAT 中的 kernel/ADT/启动参数均已回读核对，
完整镜像 SHA256 记录在 `NOTES.md` 和
工作区 `scratch/battery-irq-0115/boot-verification.json`。
用户已跳过 baseline 直接测试 preflight：运行段未见新增驱动故障，因此不要求
补测 baseline。持久日志补齐后预检明确通过，**当前使用现成的 usbin 镜像测试 mode2。**
无需再次 push 或云编译，真实中断接入仍待确认。

`live-0115-battery-irq-preflight.txt` 的 boot-args 确认 mode1；54 次真实采样
稳定 70%、4009mV，三键 12 次路由与成对 HID、触摸及 USB ACM 均继续运行，
未见驱动 ERROR/停用。插线但 CHGR state7 未充电，与前一真实采样基线一致。
该文件从 launchd 附近开始，未包含 REVID、FG 路由和 `selected mode/preflight`
输出。mode1 即使预检失败也会保留轮询，因此不能用成功启动代替预检结果。
随后 `sweetlog-0115-battery-irq-preflight.bin` 已补齐记录：REVID 确认
PM8150B，USBIN APID269、CHGR APID266、FG APID283 均为 EE0 IRQ owner。
FG 外设 edge/high/low/enable/latch 字节均为 0，RT=0x2；
`selected mode=1 preflight=1`。FG route-parts=0x3 没有访问 channel，
打印的 acc-enable/status=0 不能作为它们已测得为零的证据。详细值见硬件基线。

下一步命令（工作区根目录）：

```bash
fastboot boot out-bs/boot-16k-0115-battery-irq-usbin.img
```

插拔约三轮，每次停留 5–10 秒；同时回归三键/组合键和触摸，再覆盖一次
display-off 插拔。mode2 没有两秒电池采样，重点看插拔事件与状态通知；
导出 `sweetlog-0115-battery-irq-usbin.bin`，有 live ACM 日志也一并保留。
拔线中断 ACM 是正常现象，早期 arm/拔线段以持久日志补齐。未接 CHGR/MSOC，
所以本档不能验收充电状态和电量变化事件。

后续需要重打包时，在工作区根目录执行，`LABEL` 替换为实际产物编号：

```bash
ref/ios-mh2lm/tools/mk-mh2lm-event-matrix.sh logs/xnu-kernel-LABEL.gz LABEL --battery-irq
```

生成位置为 `out-bs/boot-16k-LABEL-battery-irq-*.img`。脚本检查 kernel manifest、
0141 标记、ramdisk 回读与 payload 一致性；旧 0114a15 内核会被明确拒绝。
所有测试仅 `fastboot boot`，不刷启动分区。

按顺序推进，每步保留原始日志和现象；有异常先停在该步分析，使用同一内核
的其他档位定位，无需为了换参数重复云编译：

1. **baseline：** 正常进桌面，真实电量、三键/组合键、触摸与 ACM 均能工作。
   记录启动时线缆状态；确认不是静态 100% 占位。
2. **preflight：** 获取 REVID revision/type/subtype、FG APID/ownership、
   edge/high/low/enable/latch。预检成功只说明允许继续，不等于 IRQ 投递成功。
3. **usbin：** 插拔几次，含 display-off 后插拔；要求 `battery-usbin` route
   命中、`refresh events=0x1 source=irq` 以及真实 external 变化/IOPM 通知。
   同时按三键、使用触摸，确认共享 mux 未影响按键。拔线时实时 ACM 会断开，
   重连后补取持久 SWEETLOG，不重复调试已经可用的日志通道。
4. **charger：** 保留上述操作，查 `battery-chgr` route 与包含 `0x2` 的事件。
   必须看真实 CHGR 状态变化，单看插线或 UI 闪动不算充电状态 IRQ 验证。
   没有自然状态转换就记录未覆盖，不为制造事件写充电控制寄存器。
5. **all：** 验证 FG 接入、MSOC route 与包含 `0x4` 的刷新；让真实 gauge
   电量发生变化并保存足够长的日志，不能规定短短几秒不变就判失败或成功。
   SOC_READY `0x8` 可能保持稳定而不触发，记录实际覆盖情况。回归启动已插线/
   未插线、息屏/亮屏、三键/组合键、触摸、ACM 和重复插拔。
6. **all-debug：** 仅需要细节时再测；保留 mode4 的事件配置。全程使用既有
   USB 日志工具；断连或启动异常使用持久 SWEETLOG 补充。

日志至少保留：`selected mode`、`REVID`、`attach route`、
`consumer attached`、`armed mode`、`hit route=battery-*`、
`refresh events`、`SWEET BATTERY SID2`、`SWEET BATT UI`、ERROR/WARN。
`route-to-publish-us` 从 mux 通知时刻近似计时，不包括硬件到 GIC/mux 的延迟，
事件合并时也不能解读成每个边沿的精确耗时。记录 IRQ 通知/采样次数、产物和
镜像 SHA256、boot arguments、线缆状态、操作顺序与原始日志名到 `NOTES.md`。
空闲 IRQ 模式不应继续出现每两秒采样；允许前述明确标记的启动与有界确认。

display-off 只指现有逻辑息屏，不代表深度休眠；已知锁屏局部黑屏仍归 framebuffer
后续目标。电池事件成功亦不等于真实充电控制已接管；旧 USB 多次插拔偶发枚举
失败要与本轮 IRQ 路由错误区分，不以历史现象替代本轮日志分析。

## 本地验证与剩余限制

- 0141 对精确 0114a15 基线 `git apply --check --whitespace=error-all` 通过；
  新目录应用后五个文件与编译源逐字节一致。
- 三个翻译单元生成 ARM64 Mach-O 对象，C/C++ 导出符号名称一致。
  Linux clang19 缺少 Apple XNU 类型内建函数，本地 C++ 检查使用仅存在于
  scratch 编译脚本的兼容宏；不能替代 Apple 工具链原生完整构建和链接。
- `tools/tests/test-mh2lm-battery-irq.py EXTRACTED_XNU_DIRECTORY` 对实际
  mux 源码替换 MMIO/IOKit 调度后，在 ASAN/UBSAN 下检查触发掩码、无关位
  保留、外部 EE 拒绝、部分接入失败回滚、带 queued IRQ detach、累计 600 次
  正常 IRQ 不停用、密集风暴显式停止、超时 APID 禁止复用及通知两个 consumer。
  模拟器不能证明真实 PMIC 时序、硬件投递或完整电池服务生命周期。
- 打包脚本语法、Python 语法、CircleCI YAML 解析通过；旧内核缺少标记时
  打包被拒绝且未生成镜像。CircleCI 实际使用的
  `.github/workflows/circleci.yml` 增加 0141 源码门禁，原编译命令未改。

源码工作目录为工作区 `scratch/battery-irq-0114a15/xnu/`；精确基线在
`base/xnu/`，补丁应用核对目录为 `verify/`，编译脚本/日志/对象在同级 scratch。
这些本地产物不进 git；仓库提交补丁、打包入口、测试脚本与文档。
0115 产物源码核对目录为工作区 `scratch/battery-irq-0115/xnu/`。
0115 preflight 持久记录已补齐；下一步使用已经生成的 USBIN 镜像验证真实投递。
