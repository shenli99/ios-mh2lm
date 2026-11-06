# 0120 FG 读取失败与 0152 路由修正

2026-11-06。0120实机遥测失败，0152完成本地修正及验证；0120a1原生云产物
已核对并打包，待实机验证，路径与校验见文末。
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
