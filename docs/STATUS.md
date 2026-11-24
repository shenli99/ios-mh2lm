# MH2LM 当前状态

更新：2026-11-24。具体证据见版本记录，操作见[构建指南](BUILD-GUIDE.md)，交接见[NOTES](../NOTES.md)。

## 当前交付

**SM8150 时钟系统：C0 调查完成，C1-01～C1-04 只读候选已实机收口。** [总计划](CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md)及十个阶段覆盖统一入口、GCC、apps RSC、RPMh 客户端、频率／电源域、显示迁移、其他 CC、PM／恢复和覆盖扩展。补丁 `0230`／`0231`／`0232` 实现统一入口 `IOQcomClockProvider`、控制器注册／撤回与 generation、设备资源名到 `(controller, local ID)` 的只读绑定、父源与电源依赖描述，以及默认关闭的 `sweet,clock-diag` 诊断入口；所有硬件操作仍返回 Unsupported，写计数为零。0122 默认档、0123 默认档与诊断档实机通过（默认档 `phase=start … bindings=0` 且无其它时钟行，诊断档四只读句柄四行账本加 `detail=zero-writes` 收尾、依赖原因如实、两档非时钟基线与 b32 零差异）；0232 把同一失败被记两遍的问题收敛为“谁报告谁记账”一条。C1-05 需求账本按总计划 §3.4 并入下一个写候选；所有 MMIO 写仍留给后续。时段证据见 [阶段1](clock-plan/01-provider.md#实机证据0123-内核两档各一次) 与 [NOTES](../NOTES.md)。

**SM8150 时钟首个硬件写候选：C2-08 debugcc 测频＋C1-05 需求账本已实现（补丁 `0235`），待 0127 实机。** `IOQcomGCC` 出现第一条写路径（`dmb oshst`→单次 store→`dsb osh`，对齐／越界拒绝，`registerWrites()` 不再是常量 0），按 LG `clk-debug.c` 保存 5 个寄存器 → 经账本取得 XO/4 参考源（`cc1:0x4`，只有打开需求的持有者写 enable）→ 选 mux／写 post-div／开 debug 分支 → 短测 `0x1000`＋长测 `0x10000`（等待 bit25 均有界，超时分别 `idle-timeout`／`done-timeout`）→ 逆序恢复并逐条读回 → 以 XO/4 `ENABLE` 回 0 判决 `ledgerRelease(token,stopped)`。运行样本 `mux=0x53`（`gcc_disp_xo_clk`）预期 `hz=19199084`，与 C2-02 的 `19200000` 差 `-47 ppm`（容差 10000），这就是“配置解码”与“实测频率”的分界证据；关闭样本 `mux=0x9A`（`gcc_pdm2_clk`）短测＝长测即 `running=0`，不报 0 Hz 冒充测量值。C1-05 在 provider 内落地固定账本（8 条×4 持有者，`desired／pending／confirmed`＋`generation／transitions／confirmations`）：首个取得才推进转换、第二个消费者只加持有者、确认是 verdict、硬件没停则保留 `pending` 并 `ERROR detail=hardware-still-on`、generation 变化清 token 保计数、撤回使条目失活。测频由设备树 `sweet,clock-measure=<u32 1>` 门控（生成器 `--clock-measure`，隐含 `--gcc`）：缺失即 `Unsupported`＋DEBUG，**默认镜像仍 `writes=0`、行数与 0126 一致**；坏属性 `ERROR detail=bad-clock-measure-gate`；测量失败只补 `WARN detail=measure-incomplete`，后端照常 Ready。三份主机测试通过（账本多消费者／欠账／过期／上限；GCC 默认 0 次 store 与门在 23 次有序 store／5 次恢复 0 mismatch／`reference-not-running`／有界超时；ADT 门可选且逐字节还原），补丁 `0235` 在基线严格重放一致。下一步：0127 两档打包，默认档验零写入回归，测量档验上述测量行。[阶段2](clock-plan/02-gcc.md#本候选实现c2-08-测频c1-05-需求账本补丁-0235) · [阶段1](clock-plan/01-provider.md#c1-05-建立公共多消费者需求账本) · [NOTES](../NOTES.md)

**SM8150 时钟 C2-01／C2-02：只读 GCC 后端已实机收口（0126 默认档进入桌面）。** 补丁 `0233` 新增 `IOQcomGCC`：按 ADT `reg` 映射四页 GCC 窗口并在首次读取前核对解析地址与长度，只认 `sweet,gcc-abi=1`／`sweet,gcc-variant=0x8150`；寄存器访问是对齐／越界检查的窄只读助手（`dmb oshld`、单锁、文件内无写路径、`registerWrites()` 常量 0），start 先冻结 11 个已知寄存器。C2-02 只解码 `gcc_pdm2_clk_src` 一种固定父源／分频配置，`div=0` 按 `clk_rcg2_calc_rate` 语义视为“未编码分频”，无可信父频／`CMD_UPDATE`／非法选择子／非零 mode 分别报 `parent-rate-unknown`／`update-pending`／`src-invalid`／`mode-unsupported`。ADT 由 `--gcc` 追加独立 `gcc` 节点（隐含 `--clock-provider`，去掉即逐字节回到 C1 字节）。**0125 两档都停在 `bsp-kernel`**，导出的环与 0124 同 SHA256（挂在 UFS 可写前，环里是上次会话）；根因是厂商寄存器号属于 `reg = <0x100000 0x1f0000>` 块内偏移，生成器把三个窗口写成裸偏移，后端于是读了与时序无关的低地址页。`0234` 改为 `0x00133000`／`0x00143000`／`0x00162000` 并让每条 `phase=map` 打出 `offset=` 供对账。**0126 默认档实机进入桌面**：`logs/sweetlog-0126-0.bin`（`3d1cd662…`，`total=1366678`，未回绕）四条 map 与 `offset=` 自洽、11 条快照值、`windows=4 snapshot=11 reads=18 writes=0 variant=0x8150`、六条 `decode`（`cc1:0x1`…`0x6`）＋一条 `decode-sample`，样本解出 `sel=0 div=0 div2=0 hz=19200000 known=1`，零写入、零时钟 WARN／ERROR、非时钟基线与 b32 一致。下一候选按 §3.4 进入首个硬件写 C2-08（debugcc 测频），实现与待验行见上一段。[阶段2](clock-plan/02-gcc.md#实机验收0126-默认档通过) · [NOTES](../NOTES.md)

**独立显示降级 0121b32 / `b3f735b` 普通实机回归通过。** 首次接管、76对触点、峰值五指与两轮OFF/ON已验，第二轮醒后有触点、第一轮醒后无HID记录；ERROR/panic/stray hold均0。实际终态故障降级未触发，仍为host证据；9组相关回归与两份ARM64 TU的边界保持。[实机记录](DISPLAY-TOUCH-FAILURE-FALLBACK.md#b32-普通实机验收2026-11-21)

**最新触摸 IRQ 普通实机基线为 0121b32 / `b3f735b`**，保留b31r2及b30镜像回退。第6批持续异常电平与显式恢复仍未实机覆盖。[b32证据](DISPLAY-TOUCH-FAILURE-FALLBACK.md#b32-普通实机验收2026-11-21) · [第6批边界](GPIO-IRQ-STAGE6-STRAY-CONTRACT.md#b31r2-实机验收)

**GPIO 第6批 0121b31 本地验证通过**，包括 stray 的持久隔离、显式线程恢复、b30 负对照和 ARM64 单文件编译。b31 实机遇到关中断写 console panic；b31r1 将 WARN 延迟到线程，实机未记录原 panic，但在软件探针附近触发 hold，触摸武装失败，显示接管冻结。b31r2 的 mode 3 普通路径已实机通过；持续异常电平与显式恢复为 host 证据，未完成实机故障注入。下文 a39 为显示专项基线。[第6批合同与验证](GPIO-IRQ-STAGE6-STRAY-CONTRACT.md)

## 当前结论

LG G8X（mh2lm，SM8150）已经运行 iPhone X／D22AP 的 iOS 16.7.16 用户态，可进入桌面、操作应用、触摸、使用按键和调节亮度。使用16 KiB页XNU、UFS上的HFS+根卷；当前不是早期M3内核入口阻塞阶段。

**显示专项实机基线为0121a39／0229，mode7／PV。C08第3、4项普通显示路径已验收。** 主线仍是完整显示驱动：下一步C08第5项多使用者汇总后的物理资源转换，再结合第6项处理命令与扫描协调；共享core、SMMU、RSC、QoS状态丢失后重建单列PD4。之后PD1剩余ABL退出／PD5 → F5直扫 → F7收尾。

## 各部分状态

| 部分 | 已有能力与证据 | 尚未完成 |
|---|---|---|
| 启动与用户态 | LG ABL → Mu-Silicium → UniXNU／DarwinBoot → XNU → UFS／HFS+ → SpringBoard；8核、16K页、3 GiB配置窗口 | 完整手机功能；当前测试不是重新安装根卷 |
| TLMM GPIO控制器 | 普通GPIO、独占句柄、共享pad IRQ与叶子屏蔽主体已实现，当前消费者普通路径已验；[能力清单](TLMM-CAPABILITIES-AND-GAPS.md) | TLMM-C01 MUX字段与TLMM-C02运行期island写保护未修；PM/系统唤醒、命名状态、逻辑极性/开漏、去抖等未扩展 |
| 输入与USB | 按键、TouchComm触摸、DWC3 CDC-ACM日志使用IRQ；[基线与边界](IRQ-FRAMEBUFFER-PLAN.md) | 通用I²C／USB／中断控制器架构、完整设备终止与重匹配 |
| 电池 | runtime3已实际完成充电至100%、CC→CV→硬件截止，见[电池计划](BATTERY-IMPLEMENTATION-PLAN.md) | 0169温度路由归属/挂接已验、实际触发未观察到；回充与关机尾日志按用户决定暂缓，温区/上报及完整睡眠仍未全覆盖 |
| 显示缓冲与刷新 | 当前mode7扫描驱动持有的A缓冲，固定VIG0→LM0→PP0→INTF1管线、硬件RD_PTR约60Hz；CPU仍约30Hz原地拷贝 | F5零拷贝／surface直扫、逐帧提交和复用同步；60Hz扫描不等于60Hz UI |
| 面板普通OFF/ON | 供电票、reset、host／PHY／PLL、HS／ULPS／ESC、厂商表、SDR色彩及亮度恢复；[a39两轮证据](PANEL-D1-0121A39.md) | 全部共享状态掉电后的独立重建、无ABL首帧／console／panic和完整IOPM收口 |
| C08资源引用 | 每包命令引用、ESC停启、面板DSI外层引用及显示MDP引用的普通路径已验；[六项清单](PANEL-LINEAGE-DISPLAY-CLIENT-CLOCKS.md) | 第5项最后使用者的完整物理转换、第6项提交协调差异；BOOT／managed／core／config底座仍保留 |
| GPU与其它外设 | CPU软件渲染，输入及日志可用 | GPU、音频、Wi-Fi／蓝牙／蜂窝、相机等没有本项目已验收驱动 |
| 日志和电源管理 | USB实时日志及UFS持久环、PSCI关机／重启 | BUG-006日志发布竞态、日志结构整理、整机CPU suspend与低功耗 |

## a39验收与可复查证据

- 实现 `b9b3305`，镜像交付 `67da2bc`，实机记录提交 `7dedbc5`。
- 两轮完整OFF/ON、15个DSI／MDP引用状态事件；每轮24组命令取得→触发→DONE→归还。两轮分别1个、3个完整60秒IRQ窗口，均dropped0；用户反馈“感觉无异常”。
- USB从启动中段记录到SYSTEM_OFF，0NUL；没有本轮持久日志对照。普通功能通过不代表故障注入、长期耐久或全量硬件掉电验收。
- 工作区证据：`scratch/artifact-0121a39/wake-log-review.json`、`wake-final-review.json`；本地实现验证：`scratch/framebuffer-0229/final-review.json`。
- 测试镜像：`out-bs/boot-16k-0121a39-abi608-panel-pipeline.img`，SHA256 `14e9398fc3fbf7744f9fa20eae04f40c785fe838de0675638f37307b656f903f`。a38等旧镜像保留；a39是临时启动测试基线，不表示已刷入boot分区。

## 已知问题和范围

[BUGS](BUGS.md)为问题台账。BUG-006日志竞态、BUG-008首次亮度状态、BUG-009音量指示继续保留；BUG-007旧画面闪现按用户要求暂缓。其他既有Bug也未因本轮显示通过自动关闭。

首次仍有ABL像素／共享配置依赖；正常显示已使用自有A缓冲。这两个结论需要分开。当前mode7在自动刷新期间持有MDP资源，命令结束或DSI post-enable归还不等于全部时钟关闭。

## 构建和下一次测试

内核由CircleCI构建，配置位于 `.github/workflows/circleci.yml`；用户push并下载kernel／buildtree，agent核对源码、标记和SHA后沿ABI608固件打包，再回读镜像。GitHub Actions、旧sweet／guacamole shim及FV内512KiB载荷均非当前MH2LM操作路线。

显示专项 a39 验收已完成，GPIO 第6批 b31r2 普通验收通过，无需重测同版。b32普通实机验收已完成，无需新CI或重测同版。TLMM已实现/缺失与C01/C02源码待修项已单独记录；总线层及触摸C++迁移是独立后续，不与控制器收口混批。真正故障降级的实机未覆盖状态保持。所有 sudo、push 和 fastboot 操作按[构建指南](BUILD-GUIDE.md)分工执行。

## 文档使用规则

[文档索引](README.md)按用途组织资料。当前状态更新此页与NOTES有效交接段；阶段定义放计划，版本日志只记录当时结果。折叠的历史记录、旧日期和其它设备说明用于追溯，其中的“当前／下一步／待实机”不作为新的操作要求。
