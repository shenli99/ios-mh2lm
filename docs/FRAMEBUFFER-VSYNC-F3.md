# F3：硬件 vsync 接入记录

2026-11-09。当前完成 **0179 只读预检实现及本地验证，待云编译和实机日志**。
尚未注册 MDSS IRQ handler、使能 RD_PTR 或替换软件 vsync，不能称 F3 验收通过。
本步接续已验证正常使用路径的 F2，不重开 BUG-003；BUG-002 随日常使用观察。
完整系统/设备低功耗已排到大目标四，当前只推进既定显示路线。

## 1. 已核对的硬件链

参考本地 `ref/android_kernel_lge_sm8150` 的 LG SM8150 实现，按硬件定义独立实现小型预检，
未搬入 Linux DRM/SDE 驱动栈。主要依据：

| 项目 | 已知事实 | 参考源码 |
|---|---|---|
| 父中断 | DT `interrupts=<0 83 0>`；SPI 编号 83 加 32 得 **GIC INTID 115** | `arch/arm64/boot/dts/qcom/sm8150-sde.dtsi:44` |
| 显示类型 | 本板是 command mode；SDE 5.x catalog 声明 INTF TE 能力，command encoder 选择 `SDE_IRQ_TYPE_INTF_TEAR_RD_PTR` | `drivers/gpu/drm/msm/sde/sde_hw_catalog.c:1864`、`sde_encoder_phys_cmd.c:1784,1814` |
| 顶层状态 | MDSS 基址 `0x0ae00000`，`HW_INTR_STATUS=+0x10`；MDP 汇总为 bit0，另有 DSI 等来源 | `sde_hw_interrupts.c:25`、`sde_hw_interrupts.h:23`、`sde_irq.c:55` |
| INTF1 TE IRQ | **`0x0ae6e800`**：ENABLE +0、STATUS +4、W1C CLEAR +8；RD_PTR 是 **bit2** | `sde_hw_interrupts.c:37–41,194–198,1437–1443` |
| INTF2 TE IRQ | `0x0ae6e900`，布局相同；仅观察，当前未选择它作为刷新源 | 同文件 |
| INTF1 tear-check 配置 | `0x0ae6b800`：EN +0x284、CFG +0x288、RD_PTR 阈值 +0x2a4、LINE +0x2b0、AUTOREFRESH +0x2b4 | `sde_hw_intf.c` |
| MDP 主 IRQ | MDSS+0x1000：EN/STATUS +0x10/+0x14，EN2/STATUS2 +8/+0xc | `sde_hwio.h:22–27`、`sde_hw_interrupts.c:1357` |
| GICD | 物理地址 `0x17a00000`；本项目现有按键/USB/mux 已使用此控制器 | 既有驱动及 ADT |

INTF1 **配置窗口**与 **TE 中断窗口**是两个地址，不能在现有 INTF1 映射上直接加 0/4/8
当作中断寄存器。LG 表里的 INTF1 RD_PTR 逻辑索引 322 也不是 GIC INTID。
主 MDP 中的 video INTF1 VSYNC bit27、PINGPONG RD_PTR bit12 等与这里的 TE RD_PTR bit2 不同。
DT 触发字段为 0，不能把它直接当作已证明的 GIC edge/level 配置；本轮读取实际 ICFGR。

0121a9 已有探针观察到 INTF1 tear-check=1、CFG=0x180088、RD_PTR=0x925、
AUTOREFRESH=0x80000001，INTF2 配置为零。这支持选取 INTF1 作为候选；
旧 frame counter 为零，**不证明 60 Hz**。GIC115 的实际投递、使能后的 RD_PTR 频率、
父线上是否还存在其他 enabled source，均需后续证据。

## 2. 0179 的架构与边界

- 只改内核 `SweetMDSS.cpp`；AppleCLCD、背光接口及 F2 DCS28/29 路径保持既有实现。
- 新参数 `sweetmdssirqprobe=0/1/2`，默认 0。0 不映射、不读取新增窗口；
  1 仅读 GICD；2 再读取 MDSS/MDP/两组 TE 状态和 INTF1 配置。
- 新窗口由 `/arm-io/mdss` 的 ADT `reg` **追加**提供，provider 映射并校验实际物理地址及长度。
  原九窗口顺序不变，不增加 consumer MMIO 路径；不发布标准 `interrupts` 属性。
- 任一新增窗口缺失、地址/长度不符，或新 timer 分配/加入失败：ERROR，释放新增资源，
  仅停用预检，保留工作中的显示服务。`SweetDisplayIRQPreflightReady=false`。
- 独立 `IOTimerEventSource` 在服务启动约 30、31、32 秒各取一次样本，然后停止。
  不依赖 `sweetdispprobe`，不做持续轮询。与亮度、电源、stop 共用 MDSS workloop/gate。
- 先读 GICD；仅在既有 GCC/DISPCC AHB 条件满足时读 MDSS。时钟条件不满足则 WARN 并跳过
  该次显示寄存器读取；不自行开启时钟。
- 新探针只读 EN/STATUS，不读 CLEAR、不 ACK、不写使能/路由/配置，不做软件 pending 注入。
  系统原有亮度和电源操作仍按正常路径执行，不能把整台设备称作“零 MMIO 写入”。
- stop 关闭 admission、Ready=false、取消 timer，再移除事件源；映射保留到 free，
  保证已取得引用的旧调用和在途回调不会使用释放后的资源。

| ADT reg 下标 | 物理窗口 | 用途 |
|---|---|---|
| 0–8 | 原 F1a 九窗口 | 不变 |
| 9 | `0x0ae00000 / 0x2000` | MDSS revision、汇总状态、MDP IRQ EN/STATUS |
| 10 | `0x0ae6e800 / 0x200` | INTF1/2 TE IRQ EN/STATUS |
| 11 | `0x17a00000 / 0x10000` | GICD 只读快照 |

GIC115 对应 word3 bit19；ICFGR7 bits7:6；优先级在 +0x470 的最高字节，
IROUTER115 位于 +0x6398/+0x639c。记录 CTLR/TYPER/IIDR/PIDR2、GROUP/GROUPMOD 原字、
enable/pending/active、config、priority、route。Nonsecure GROUP/GROUPMOD 可能 RAZ，
**不得仅凭 group=0 判 secure 或不能投递**。记录的 enable/pending 是瞬时观察。

输出为 `[INFO] SWEET MDSS IRQ PREFLIGHT: ...`，拒绝资源为 ERROR，时钟不可用为 WARN。
属性 `SweetDisplayIRQPreflightMode/Ready/Samples` 分别表示已准备的模式、资源就绪、
采样尝试次数；Samples 包含 MDSS 因时钟未读的尝试，不等于有效 MDSS 样本或 IRQ 次数。

STATUS 是锁存位，三次相同值不能解释成“中断卡死”，也无法计算帧率。
读取 LINE 的变化只作辅助；本步不证明 RD_PTR 已送达 CPU。
`SweetDisplayInterruptEnabled` 仍为 false，60 Hz 软件 timer 仍运行，OFF 通知行为尚未改变。

## 3. 本地验证

基线：0121a9 实际云源码（0177）严格应用 0178，再应用 0179；只有 SweetMDSS.cpp 改变。

- `git apply --check`、零 fuzz/零 offset 重放、生成结果逐字节匹配；原 CI 114 项源码 marker 通过。
- SweetMDSS baseline/新源码均完成 arm64-apple-darwin 对象编译。clang19 的
  `-Wunsafe-buffer-usage` 新增 15 条，均为新增数组访问：常量下标 9/10/11，或有界
  i/j=9..11、i-9=0..2；逐项审阅，host 故障注入验证边界。其他告警类别和数量不变。
- 实际 provider/client/backlight 的 ASan/UBSan suite 通过，含默认 DCS 完整 MMIO 轨迹对照，
  F2 电源/恢复路径；新增预检默认零访问、GIC 解码、三次后停止、完整只读且不清锁存位、
  缺失/错位/过短资源、timer 失败、时钟关闭、inactive、stop 与采样并发、释放归零。
- 实际 framebuffer service suite、ADT 测试通过；ADT 新扩展 12 窗口翻译及既有树逐字节保留，
  原 9 窗口默认输出不变；真实基础 ADT 经 CLI 派生通过。
- 打包 shell 语法、三档参数一致性（仅 mode 不同）、旧 0121a9 内核缺少 F3 marker 时拒绝通过。
  此检查未生成假测试 boot。

本地环境无法使用 LSan（ptrace 限制），禁用 leak sanitizer；ASan/UBSan 和对象/映射归零断言仍运行。
底稿：`work1/scratch/framebuffer-0179/{base,edited,verify,verification.json,warning-counts.json,
tests.json,test-*.txt,packaging-checks.json,old-kernel-rejection.txt}`。
CircleCI 配置未改，blob 仍 `da0b6c0947a66f03491b3a9023590b49665292df`。
无云编译或实机成功结论。

## 4. 下一轮编译与测试

用户 push 后下载同轮 kernel/buildtree/build-log；先核对 0178/0179 源码及 marker，再打包：

```bash
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-irq-preflight
```

提供同核三档：`fb-irq-preflight`（mode2）、`fb-irq-gic`（mode1）、`fb-irq-baseline`（mode0）。
三档均保留 service1/mdss1/power1、按键/触摸/USB/电池 IRQ、ACM、runtime3；
使用同一扩展 ADT，关闭档不映射追加窗口。原 `--framebuffer-power` 的九窗口及两档行为不变。

**正常先只测完整只读档**：`fastboot boot`，进入系统后保持亮屏等待至少 35 秒，
确保取得三次快照；随后简测亮度、触摸、按键和手动息屏/唤醒，记录新增显示问题。
持久日志即可，建议 `sweetlog-编号-fb-irq-preflight.bin`，不要求实时 USB 日志或重复插拔压力测。
只有完整档出现新启动问题才用 GIC 档分离地址访问阶段；仍异常再用关闭档，不默认重跑三档。
尚无本轮云内核，**当前没有新 boot 可交付**，实际绝对路径在产物下载及回读核验后给出。

## 5. 预检后的 F3 剩余工作

结合实机 revision、GIC 及来源状态确认资源/路线，随后接既定 GIC callback → software
IOInterruptEventSource/IOWorkLoop：上半部屏蔽/时间戳，worker 清所选 RD_PTR/计帧/排队通知/再使能。
仍需定义 typed provider 的 vsync 订阅/撤回、终止同步、OFF 禁用与唤醒重启、风暴停用和
显式 timer watchdog 回退，不能在 provider gate 中反调 framebuffer gate 形成锁环。
最终以 60 秒 60±1 Hz、间隔 p50/p99、OFF 计数停止、输入/USB/电池并行健康验收。
这些尚未实现，不将本轮三次寄存器快照当作替代验收。

## 6. 0121a10 云产物与测试镜像（待实机）

已核对 kernel/buildtree：44 个已跟踪源码与本地 0179 验证树逐字节匹配，包含 0178 mux 修复与
0179 SweetMDSS；114 个既有 CI 源码 marker 和新增 F3 内核 marker 通过，内核归档 manifest
通过。未收到完整 build log/CI run ID，因此不声称完成全量云编译告警审阅。

内核 SHA256：`b6a0732a419f28276b6b6f8fba7c15f711d66394e66ee200043066a0aeda397e`，与 0121a9 不同。

三档均为 35,667,968 字节：

| 镜像（work1/out-bs/） | 预检模式 | SHA256 |
|---|---|---|
| `boot-16k-0121a10-fb-irq-preflight.img` | 2 | `641c1b0aefc1be781b71e146c176e060976e425bd339638b69fd5fd74511c9dd` |
| `boot-16k-0121a10-fb-irq-gic.img` | 1 | `0e9dd9ef014e7fad818f28e9777f112d00615cf8aba3bd38fc28d10740a12060` |
| `boot-16k-0121a10-fb-irq-baseline.img` | 0 | `9b79a1ad447470b30b374aef79eb7587cd78ab4e0c2609cbeb05dc15aed72614` |

最终 boot 回读 Android header、UEFI kernel blob、FAT ramdisk 内的 XNU/ADT/TOML 全部通过；
三档仅 sweetmdssirqprobe=2/1/0 不同，无重复参数；扩展 ADT 一致，原基础 ADT 保持不变。
ACM/原有 IRQ/runtime3/F2 power1 保留。打包环境缺少 `file` 命令，本轮用本地 Python
Android header 检测替代该条显示信息，随后独立完整回读核验，未修改项目打包脚本。
底稿：`work1/scratch/artifact-0121a10/{verification.json,source-compare.json,
boot-verification.json,verify-artifacts.py,verify-boots.py,packaging.txt,mdss.adt}`。

**下一步先仅 fastboot boot 完整 fb-irq-preflight 档**：进入系统保持亮屏至少 35 秒，
再简测亮度/触摸/按键和手动息屏唤醒；导出 `sweetlog-0121a10-fb-irq-preflight.bin`。
有新增启动问题才用 GIC/关闭档定位。不要求重复插拔压力测试或专项 BUG-002 测试。
本轮只完成产物和镜像核对，尚无 MDSS 寄存器实机快照或真实 vsync IRQ 证据。
