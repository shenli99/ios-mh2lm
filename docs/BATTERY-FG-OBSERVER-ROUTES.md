# 0120 FG 读取失败与 0152 路由修正

最新：0120a2 已验证0153初始DMA WRITE_ERROR清除、温度/容量/profile读取和短日志。
结果与下一步见 [FG DMA 恢复](BATTERY-FG-DMA-RECOVERY.md)。下文保留0152设计与
0120a1失败阶段的历史证据，不再把DMA错误当作当前阻塞。

2026-11-06。0120实机遥测失败，0152完成本地修正及验证；0120a1原生云产物
已核对并打包，首次实机日志已分析，结果与下一步见文末。
本轮不开放主动充电。原始结果见
[0120测试记录](BATTERY-RUNTIME-0120-TESTING.md)。

## 已知问题与证据边界

0120的617次温度读取均返回SWEET_SPMI_NO_OWNER(-4)，容量/profile无效；
30个配置字段、V/I、USBIN和source-change事件可用。旧日志不含FG失败地址，
因此不能断定这台机器一定在0x0105、MEMIF或DMA窗口的哪一步失败。

代码问题已确认：0151的sp_fg_byte复用sp_charger_route，要求每个只读地址
存在唯一EE0 writer。LG Linux v5的observer与RW规则不同：

| 路径 | APID选择与权限 |
| --- | --- |
| observer | 第一条PPID映射，有本地EE映射时优先本地；observer窗口属于调用EE，不要求APID的writer也是本地 |
| RW | 目标APID的write-ee必须是本地EE；本项目进一步拒绝多个本地writer的歧义 |
| IRQ | 独立IRQ owner，不能用其是否置位判断全部读写权限 |

依据：工作区`ref/android_kernel_lge_sm8150/drivers/spmi/spmi-pmic-arb.c`
的`pmic_arb_read_apid_map_v5`、`pmic_arb_offset_v5`。后者只在
PMIC_ARB_CHANNEL_RW分支检查write_ee。现有sp_find_apid已与该observer选择
方式一致，旧电池REVID预检也使用它。本轮修正明确的权限分层问题，实际
失败地址与MEMIF是否可完成会话，仍由下一份实机日志核对。

## 0152的改动

只改`bsd/dev/sweet_spmi.c/.h`和`SweetBattery.cpp`，以新增
`xnu/patches/0152-mh2lm-fg-observer-routes.patch`交付。

- 内部FG observer读取使用sp_find_apid和现有EE0 observer窗口，保留v5检查、
  单笔控制器锁、有界等待、独立observer poison。不以读取失败为理由切换
  另一APID重试，不访问外部EE的RW channel。
- 在任何会话写入前预检0x4340/4350/4371所需writer，v1额外预检0x41ee。
  缺失、歧义或RW已失效则停止，不先写request再发现缺少另一必需权限。
  实际每笔协议写仍经sp_update_owned再次核对唯一EE0 writer及RW poison。
- Gen4身份/revision、busy检查、grant/ALG有界等待、模式恢复和双request释放
  保持原流程；失败不返回数据，释放失败保持本启动session poison。
- 不扩大旧sweet_spmi_read白名单，也不修改charger_read/update的权限边界。
  没有公开任意SRAM地址接口，没有FG profile内容写、充电策略写或权限强制。
- 启动首次FG访问输出固定PPID的路由元数据，只读已映射core/cfg，不额外映射
  interrupt bank，不探测通道IRQ寄存器。匹配回调中没有新增SPMI工作。

模式仍为telemetry1/policy-dry2，三个真实证据provider仍缺，控制档不打包。
按键、USB、触摸IRQ及mux/consumer接口未改；共享transport正常路径已本地回归，
完整硬件并行回归仍待新内核实机。MEMIF协议控制确有写入，不能称全总线只读。

## 下一份日志将包含什么

| 标记 | 内容与频率 |
| --- | --- |
| SWEET FG ROUTE: v2 | 固定PPID的候选APID、write-ee、IRQ-owner及observer选中项；首次一次 |
| SWEET FG ACCESS: v2 | 字段、操作、SID/地址/PPID、首个错误和最终错误；首次失败/错误变化WARN，首次成功/恢复INFO |
| SWEET BATT SAMPLE: v1 | 增加temp-raw、n-error/l-error/p-error；与原采样日志同频 |
| SWEET BATT CONFIG: v2 | enable/pause、1651 watchdog/1653时间、1648/1690并联配置及每字节错误；初始和真实事件路径 |

字段编号0温度、1nominal、2profile integrity、3learned。错误操作包括
observer-read、writer-preflight、protocol-update、identity-layout、session-busy、
dma-status、alg-wait、grant-wait和session-disabled。首个错误位置不会被后续
清理覆盖；清理若另有失败，final-error和原有ERROR日志仍会显示。
相同字段/位置/错误不每秒重复WARN；ERROR通道及原有总线/释放故障日志不压制。
INFO/WARN沿用sweetloglevel过滤。无效raw必须结合对应error/valid解释，不能
把缓存或初始零值当作测量。配置日志只使用已经取得的快照，不增加插拔轮询。

watchdog记录用于区分实际配置；本轮没有新增pet，也不关闭watchdog以放行。
取得FG温度/容量仍不能代替物理电池身份、实测VBUS或并联隔离证据。

## 验证与下次测试

基于原生0120选定源码严格应用0152（whitespace=error）成功，24文件与编译
输入逐字节一致。两个修改编译单元通过ARM64单文件编译，使用已有CI头和
Linux clang19兼容定义，不替代macOS完整构建/链接。

实际函数ASAN/UBSAN：transport/FG共26个场景通过，包括无本地writer仍可读
REVID/DMA页、v2无需v1 peek writer、本地优先/第一外部映射、MEMIF缺失或歧义
writer不产生会话写入、observer拒绝/超时、失败地址、错误去重和恢复；原有
grant/ALG/清理失败、共享poison及RMW并发继续通过。电池采样/发布测试验证
独立容量错误与raw配置/错误配对；运行层策略、停止与100次电源通知竞争通过。
mux/consumer完整回归与80次生命周期竞争、旧SPMI及10次并发回归通过。

CircleCI仅增加0152源码标记门禁，真实build.sh命令不变；YAML和门禁验证
接受0152源码、拒绝原0120，打包脚本bash语法通过。runtime打包要求新标记，
防止旧内核冒充新修正；已有0120镜像保留。底稿及完整测试结果在工作区
`scratch/fg-observer-0120/`（validation-results.json、verify/xnu、compile.py）。

0120a1已核对，并以同一`--battery-runtime`生成三档。
优先测telemetry，启动约一分钟、插拔两轮并导出持久日志即可；先检查四字段
及FG路由/失败地址，再看watchdog/enable/并联配置，不重复0120或no-USB测试。
若仍停在writer-preflight，可明确标记该协议写所需权限未满足，不能直接取消
写权校验。手机温度/容量尚未验证，不能宣称本轮恢复物理充电或结束IRQ阶段。

## 0120a1产物与可用镜像

用户输入编号“01120a1”，本地实际文件为0120a1，以下均沿用实际编号。
实现提交99189b1；本次只核对/打包/记录，无驱动或CI修改、push、设备操作。

| 产物 | SHA256 |
| --- | --- |
| logs/xnu-kernel-0120a1.gz | 10bb1374c6294ba31e314dfdc5d25d271c86003468c556f22cf26f82aa52ae62 |
| logs/xnu-buildtree-0120a1.gz | e86a392b692c36f017e687b0a8200b0b4ea643d8be4dba517934847ff0a2934d |
| kernel.development.mh2lm | 08d44a9394b0921d555aa00534b68a81ff55037b9f6b1916c84adb3c107dee10 |

原生24个选定源码文件与本地0152严格应用/编译输入逐字节相同；kernel manifest、
plain arm64、segment16K对齐、FG ROUTE/ACCESS v2、CONFIG v2及容量独立错误
字符串、FG/charger/PM通知定义符号通过。没有该轮build日志，不推断CI运行ID。
原生源码/机器记录/实际启动TOML在工作区`scratch/artifact-0120a1/`，详情
`verification.json`与`required-symbols.txt`。

镜像均在`/home/shenli/work1/out-bs/`，每档35667968字节：

| 文件名 | SHA256 |
| --- | --- |
| boot-16k-0120a1-battery-telemetry.img | 59ffcf2d4f35b9a49e7a769931540ee6f2349bd75a690092d221168a15828e78 |
| boot-16k-0120a1-battery-policy-dry.img | e219c7e76f8022b488d0d0c0774c1078dd914c41998114e12b92c17a9f2aaf5a |
| boot-16k-0120a1-battery-runtime-observe.img | 1df8a8e47b49d65b03b5ff05c2c4151a3ba2c07f34e6a9098d18e2d49cfd7cb2 |

脚本生成后独立从最终Android boot解出ramdisk，回读FAT内kernel/ADT/TOML，
确认firmware/kernel/ADT相同、参数无重复。三档保留realSID2、sweetbattirq4、
三键/USB/触摸IRQ及ACM；telemetry为runtime1，dry为2，observe无runtime。
未打包控制档，三项board证据provider仍缺，不能主动启充。

先测telemetry即可：用户fastboot boot后等待约一分钟，插拔两轮，每个状态
停留15～30秒，导出`sweetlog-0120a1-battery-telemetry.bin`；持久日志足够，
不额外要求live，不先测observe/dry。先看四字段有效性与FG失败位置，再看
enable/watchdog/SMB原始配置；实机结果待用户日志。

## 0120a1首次实机结果

日志`logs/sweetlog-0120a1-battery-telemetry.bin`，SHA256
`182dee0a02438f85393c06468538c09c1714c3750e363f15d3288e5a588a4862`。
v1/ring4194304/total515723/flush77，未回卷、无解码NUL；runtime1。
工作区`scratch/artifact-0120a1/battery-telemetry-persistent.txt`与
`battery-telemetry-analysis.json`保存原解码与按记录分析。

**0152权限分层修正已获得实机支持：** REVID实际映射PPID0x201、APID103，
write-ee=4，旧版要求唯一EE0 writer确实会在0x0105之前拒绝。新observer
已通过身份/revision读取，当前四字段一致停在`dma-status addr=0x4370`，
返回-9。另有下列实际路由，均write-ee=0：

| PPID | APID | 功能 |
| --- | --- | --- |
| 0x241 | 284 | FG BATT_INFO |
| 0x243 | 286 | FG MEMIF |
| 0x246 | 289 | DMA容量窗口 |
| 0x247 | 290 | DMA学习/profile窗口 |
| 0x248 | 291 | DMA温度窗口 |

793个快照（16初始/事件、777保护timer），开始时间跨度799.873秒；全部30配置
位有效，V/I有效，3895..3997mV、IOPM方向-588..+15mA。温度与nominal/learned/
profile错误码均-9，值无效。此-9由DMA_STS mask0x6非零返回，指DMA read/write
错误位，不是SPMI DENIED/超时，也不是新的owner不足。
本日志未打印DMA_STS raw，且两处pre/post检查同用dma-status，不能判定具体
哪个错误位、第一笔发生在哪一阶段，或断言错误来自旧启动/当前代码/外部EE。

16次CONFIG均独立读取成功，值一致：

| 寄存器 | 值 | 当前可下的结论 |
| --- | --- | --- |
| 0x1042/1043 | 0/0 | charging enable关闭，pause未置位 |
| 0x1651 | 0x62 | plugin timer bit1和BARK IRQ bit6启用；不满足当前确认watchdog-off策略 |
| 0x1653 | 0x8c | BITE配置为禁充；不能据配置证明已经发生超时 |
| 0x1648/1690 | 0/0x2 | 是实际配置，仍不能代替外部SMB1355隔离证据 |

watchdog依据LG smb5-reg.h与qpnp-smb5.c。未取得bark/bite IRQ或pet证据，
不能把enable0唯一归因于watchdog。11条policy均allow0/owned0，无CONTROL
提交，16条原发布均charging0、CHGR0x47。仍没有持续正充电电流证据。
14次IRQ刷新（10USBIN/4source-change）计数连续且healthy1，无驱动ERROR、
panic或mux隔离；有6条既有USB ep0/ep5命令WARN，不能拿它们解释FG错误。

另发现本轮新增SAMPLE过长：输出在约255字节处截断，结束时间与换行缺失，
与后继文本粘连。计数按记录标记而非物理行，开始时间/错误字段可用，结束
时间不可恢复。不是ring回卷证据；底层截断位置尚未定位，先缩短驱动日志，
无需为此修改全局console。上一轮host测试用2048字节接收未覆盖此边界，需要
补短记录长度检查。

下一步应一起补齐DMA状态raw及pre/post定位、受条件约束的DMA错误恢复，并
拆短采样日志。LG `fg-memif.c:fg_dma_init`会先调用
`fg_clear_dma_errors_if_any`，通过0x4371 bit0处理DMA错误日志；当前实现只有
遇错拒绝，尚无该恢复过程。实现须确认身份/idle/写路由、保留其他控制位、
验证错误已清除且有界，不把command放入稳定配置RMW，不在每次读失败后静默
清错重试，也不能重置FG/profile。仍需watchdog/pet及三个证据provider。
本轮只分析和记录，尚未新增恢复补丁；无需补live或重复当前镜像/dry测试。
