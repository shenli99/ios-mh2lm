# 固定 FG profile 与输入快照证据（0157）

2026-11-07。本地实现和检查已完成，等待用户 push、云构建和实机日志。
依据是[0120a5 后的离线核对](BATTERY-IDENTITY-INPUT-AUDIT.md)，不重复已通过的
ADC5 转换/consumer bring-up。本批采集 profile 和电源识别数据，**不恢复真实充电**。

## 分层与固定 FG 接口

保持 SweetBattery 自己的 workloop → 内部 SweetPmicPower → 共享 SPMI/FG
transport。高层采样、策略和 IOPM 发布不传 SRAM/SPMI 地址，没有新增全局
IOService 单例桥接，也没有改变 mux、按键、触摸、USB 或 ADC 服务的生命周期。

新增 `sweet_spmi_fg_read_profile(struct sweet_fg_profile *)`，固定返回：

- 24 字节 profile 前缀：Gen4 word65、offset0，SID2 0x4522～0x4539；
- 同一 MEMIF grant 下读取的 integrity（word299）及 nominal mAh（word271）；
- 会话取得锁后的开始时间，以及恢复/释放成功后的完成时间。

无地址/长度参数。四字段 scalar FG API 保持原签名和范围，scalar field4 仍非法。
两种调用复用同一个私有 reader、`sp_fg_lock`、有界 request/grant、版本检查、
DMA 前后状态检查、初始一次清错及 restore/release/poison；等待时释放控制器锁。
读取 DMA1 不要求本机拥有其 writer，只通过选定 observer 访问；协议控制仍要求
唯一 EE0 writer。没有扩大 SRAM 写入权限或改变 profile/学习内容。

返回数据先保存在局部对象，所有读取和清理成功后才复制给调用者。错误保持
调用者输出不变；采样后端使用零初始化局部对象，失败撤销前缀/metadata 的有效性，
不会保留上一轮前缀充当新证据。清理失败沿用原来的 SRAM poison，不另造重试兜底。

事件/初始观察时，后端用该 tuple 代替原来的两次独立 integrity/nominal 会话；
learned capacity 仍单独读取。前缀读取失败时，本次 nominal/profile 也报未知，
IOPM 容量按已有规则退回百分比语义，不能把旧容量作为这次 tuple 成功的证据。
温度保护采样没有新增前缀读取，正常保护 tick 仍缓存这些元数据。

`sb_profile_match()` 仅做诊断比较：完整性标志满足 LG 白名单、nominal 相符、
24 字节逐字节匹配时返回 Tocad=1 或 LGC=2，否则0。它不验证全部416字节、不
验证物理 BAT_ID，也不设置 `batteryVerified` 或额定限值。当前 BAT_ID 电阻已
支持 Tocad 候选；本批补读回前缀后，再通过 typed 证据消费者闭合身份授权。

## 输入获取时间与策略边界

在 snapshot 增加 `inputEpoch/inputStartedNs/inputCompletedNs`。初始观察、真实
事件或原有有界确认触发输入刷新时，先撤销输入有效位，再递增 epoch，记录该次
寄存器读取区间。epoch 饱和后时间清零、保持未就绪，不回卷复用旧序号。

缓存输入字段为 USB plugin、APSD status/result、Type-C sink/attached/misc，以及
新增的固定只读 debug-accessory 0x1507、legacy 0x150d。只在原有 transport
白名单开放这两个确切地址，写入仍拒绝，普通 legacy observer API 保持原范围。
每个新读字段都带 raw/error；失败清本次 raw 与有效位，不能把旧 raw 当新读值。

保护 tick 不读这八个字段、不递增输入 epoch、不更新输入开始/结束时间；它仍
读取保护所需的实时 path、限流/使能等配置。INPUT TIME 覆盖本次寄存器快照
读取区间，整体 SAMPLE TIME 还包含随后电池遥测/FG 会话，不能用后者给缓存
输入续期。配置日志只在输入刷新时输出，row 的 sample id 对应同一采样调用。
这不是硬件原子快照；时间区间用于暴露顺序读取耗时。

输入 epoch 是 battery 后端的获取序号；ADC client 保留自己的 source epoch。
它们不是共享 token，也不是物理插拔次数，不能只因数字相等就认定证据属于同一
输入。未来输入许可 consumer 仍需明确关联 ADC token、来源撤销与时效。

同时补强既有 `sb_decide()` 的拒绝条件，避免日后接入授权字段时漏检：

- APSD 按 LG 精确 result 枚举，status QC bit1 可覆盖普通 result；未完成、混合
  未知码不视为 DCP。检测类型不等于已有 PD/USB 电流许可。
- 初始策略只接受 DCP/CDP 类型，并要求普通 sink 的已连接/VBUS/debounce、
  Rp 标准/1.5A/3A、无 source 角色/水检/过流/异常 VBUS。debug-accessory、legacy/
  noncompliant、SDP、FLOAT、OCP、QC 均不属于本批初始可接受路径。有效电流许可
  provider 尚缺，不能从这些条件通过推导出电源承诺。
- 输入字段必须齐全且独立获取时间不超过3秒；决策截止时间取遥测、limits 和
  输入期限的最小值，防止配置途中跨过旧输入期限。期限加法饱和避免溢出。
- LG 板级 FCC 上限校验从3300收紧为2800mA；首次受控执行器原有500mA/4200mV
  小范围约束保持。并联路径、温控和 watchdog 责任没有因此完成。

`batteryVerified/inputVerified/parallelVerified` 本批仍全部保持 false。没有
把 `sb_profile_match()`、检测类型、5V 电压、ICL 或 USB `configured` 直接当成授权。
稳定插线3秒后缓存输入不再满足“新鲜快照”，保护 tick 不会扫描续期；后续输入
provider 的事件契约/重验证尚需实现，本批不宣称已有可持续充电会话。

## 日志格式

新增成功诊断为 INFO，底层错误/恢复沿用现有 ERROR/WARN 分级。每条包含 severity
和换行均不超过255字节；按 sample id 拼接，不依赖被截断的大行。

| 记录 | 主要内容 |
| --- | --- |
| `SWEET BATT PROFILE: v1` | valid/error、match、integrity、nominal、48位十六进制前缀，明确 diagnostic-only |
| `SWEET BATT PROFILE TIME: v1` | 同一 FG 会话开始/清理完成时间 |
| `SWEET BATT INPUT: v4` | epoch、检测类型、sink-ok、fresh、APSD/result/plugin raw/error |
| `SWEET BATT INPUT TIME: v1` | 独立输入获取区间及 epoch |
| `SWEET BATT TYPEC: v4` | sink、attached、misc、debug、legacy raw/error |
| `SWEET BATT ICL: v1` | path、suspend、ICL、settled raw/error |
| `SWEET BATT ICL MODE: v1` | override、load、mode、AICL raw/error |
| `SWEET BATT LIMIT CONFIG: v1` | FCC、FV、cfg2、JEITA raw/error |

检测类型编号：UNKNOWN0、SDP1、CDP2、DCP3、OCP4、FLOAT5、QC2=6、QC3=7。
数值 `/error` 的右侧0才表示该字段此次读取成功，raw0与未知必须区分。
原 CONFIG v3、PARALLEL、CAP、SAMPLE、TIME 继续输出，普通保护 tick 不重印上述
事件记录。`match=1` 预期对应 Tocad 前缀及4027mAh，仍须结合真实 BAT_ID 分析。

## 本地验证与实际边界

底稿 `scratch/battery-evidence-0120a5/`：

- 0120a5 实际云树33文件与基线逐字节匹配，0157严格 apply 后与 verify 一致；
  `validation-results.json`、`cloud-apply.json`。
- SweetBattery、SweetPmicPower、sweet_spmi 三个 ARM64 单文件编译通过。使用
  既有 CI headers/Linux clang兼容参数，有旧headers警告，不替代完整云链接。
- 实际 transport ASAN/UBSAN 共118场景，新增37个固定profile场景：v1/v2、
  外部 DMA writer 的 observer 访问、无路由/写权、grant超时、完整性/容量及24字节
  逐个读取失败、清理/后置DMA错误、poison、输出不变和无SRAM数据写入。
- 实际 sampler/policy/controller：真实两份profile及逐字节失配、错误元数据、
  精确APSD/QC/角色、输入失效/期限、monitor不续期/不扫描、新读失败清旧raw、
  epoch饱和、最大id/时间与短行完整性；原控制写失败/退出及100轮停止竞态通过。
- mux/provider/按键/电池 suite 与80轮通知/准备/退出竞态、ADC client17场景与
  100轮通知/终止竞态、SPMI共享访问和board启动回归通过。
- CircleCI只增加源码gate，实际build命令/参数不变；完整gate接受0157、拒绝0156。
  `--battery-evidence` 打包检查拒绝真实0120a5旧kernel，无伪造内核标记。

host 模型不能证明 DMA1 实机可读、native IOKit 时序或物理电流许可。云产物和
本批实机日志尚未取得，不把本地通过写成设备已验证。

## 0120a6 实机结果

`logs/sweetlog-0120a6-battery-evidence.bin` 已解码到
`scratch/artifact-0120a6/battery-evidence-persistent.txt`。本次 11 次 FG
profile tuple 均为 `valid=1/error=0/match=1`，integrity 为 `0x13`、nominal
为 4027 mAh，24 字节前缀一致且为 Tocad 候选。输入快照字段也全部成功读取，
但电脑口只得到 SDP，普通充电器阶段得到 OCP/UNKNOWN，没有出现可接受的
DCP/CDP，因此策略保持 `sink-ok=0`，没有输出启充 CONTROL；这符合 0157 的
只读取证范围，不能证明真实充电失败或成功。

同一份日志还显示第二次电脑 USB 插入只出现反复 `bus reset`/`connected`，没有
再次出现 `ACM endpoints configured`，这是已知 DWC3 gadget 枚举问题，和输入
识别策略分开处理。日志末尾另有一次共享 PMIC mux 故障：第 373 次 ADC5 测量
已完成且 `restored=1`，随后出现 `handled=1 pending=1`，mux 按保护逻辑停用并
终止 keys、battery、ADC consumer。当前记录没有保存故障后的 per-route pending
mask，不能据此认定是 ADC、按键或电池 route；下一轮先增加 route/APID/status/
ack 取证，并尝试只隔离故障 group，避免全 mux 停止。

## 一次编译、同镜像的下一轮步骤

用户 push 并下载新编号 kernel/buildtree 后，先核对0158源文件和新函数符号，再打包：

```bash
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --battery-evidence
```

生成工作区 `out-bs/boot-16k-编号-battery-evidence.img`，参数 runtime1/IRQ4/ADCconv0，
保留既有三键/触摸/USB IRQ和USB日志。用户自行 fastboot，本地本轮没有新镜像。
无需先 baseline、dry 或启动有限ADC转换档。

建议同一次启动完成以下采集，避免为电源切换重复编译/重刷：

1. 接电脑USB启动，桌面停约30秒，确认按键/触摸正常；记录该段是电脑口。
2. 拔线等约10秒，接已知普通5V充电器等20～30秒；记录电源类型。此段可以没有
   USB live输出，直接靠持久日志。检测出QC/legacy/accessory也保留结果，不改码
   伪装普通sink，不要求本批一定得到 `sink-ok=1`。
3. 拔线等约10秒，再接电脑等约10秒，导出
   `sweetlog-编号-battery-evidence.bin`。若分开启动采集，分别注明电脑/充电器即可。

重点是固定tuple读回完整、Tocad候选是否相符，各电源的APSD/角色/模式可解释，
epoch与时序明确，且没有FG清理失败、mux隔离或错误启充CONTROL。无需刻意重复
数百次ADC转换。本批不充电符合范围，真实充电仍须后续身份/输入许可消费者、
SMB1355隔离与watchdog维护闭合后验收。

### 0159：APSD事件与时序修正

原“所有IRQ/有限确认更新输入epoch”的实现已由0159收窄为初始/plugin/source。
source bit6早在0151已接入；新增事件来源、观测会话、完成采集期限、一致性及
实际BC1.2 enable观察。旧0157数据和测试结果保留为历史。下一镜像仍用原档，
新增0159 kernel门禁；见[APSD事件修复](BATTERY-APSD-EVENTS.md)。
