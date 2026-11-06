# 0120a5 后的电池身份与输入能力离线核对

2026-11-07后续：下文规划的固定profile读取、输入raw/error/独立时间与拒绝条件
已实现为0157，尚待云构建/实机。当前接口、检查及单镜像采集见
[profile/输入证据实现](BATTERY-PROFILE-INPUT-EVIDENCE.md)。下文保留离线核对依据。

2026-11-06。本轮只核对 LG 源码、DTS、现有 XNU 源码和已导出的日志，没有改
内核、CI 或 boot。结论：**实测 BAT_ID 与 Tocad 91kΩ profile 一致，4027mAh
容量提供第二项相符证据；当前 FG profile 前缀及输入端电流许可仍待补证。**
身份候选已缩小，不必重测 ADC 转换。`batteryVerified/inputVerified` 尚未成立，
SMB1355 隔离、watchdog 维护也未完成，不能据此启充。

## 证据版本与复算

- LG 参考树：`ref/android_kernel_lge_sm8150`，HEAD
  `d4f335e13d73c4aa2af85581a6ec11b670ee5b94`。以下 LG 路径相对这棵树。
- 现有 XNU：0156，0120a5 云树的 33 个选定文件与本地 verify 一致。
  代码快照在工作区 `scratch/artifact-0120a5/xnu`；完整接口核对采用
  `scratch/adc-consumer-0120a4/verify/xnu`。
- 本轮测量：`logs/sweetlog-0120a5-battery-adc5-consumer.bin`，SHA256
  `f009090b73162c63ecb16122789013d4ab08b90e0b4bfb1dda12ca242c431ddb`。
  SWEETLOG v1/total1511002/flush144，按有效长度解码，未回卷。
- 旧输入配置：`logs/sweetlog-0120a3-battery-telemetry.bin`，仅作历史证据，
  不能代表 0120a5 的每次插拔。0119 两份 charge-diag 也已有输入诊断，但旧的
  Type-C 访问/有效性与新 transport 不同，不能把日志中的零直接认作无设备。

仓库根目录可离线复算，无设备访问：

```bash
python3 tools/audit-mh2lm-battery-evidence.py \
  --lg-source ../android_kernel_lge_sm8150 \
  --log ../../logs/sweetlog-0120a5-battery-adc5-consumer.bin \
  --input-log ../../logs/sweetlog-0120a3-battery-telemetry.bin
```

工具校验所用 LG 公式/常量，解析两份原始 profile，输出来源 SHA256、数值、
候选及明确缺口；没有改变运行期授权。它是本次源码快照的复算工具，不是通用
C/DTS 解释器，也不把全局 profile-integrity 位当作匹配证明。本轮输出保存于
工作区 `scratch/artifact-0120a5/battery-evidence-audit.json`。

## BAT_ID：LG 使用 1825mV 偏置

`drivers/power/supply/lge/extension-fg-gen4.c:898` 的
`extension_get_batt_id()` 先取 `unified_bootmode_batteryid()`；无值才使用 ADC。
后者实际由 `lge/unified-bootmode.c:231` 返回 bootarg
`androidboot.vendor.lge.battid=` 解析值，不能因日志写着 shared memory 就说
已读取 SMEM。备份 `lgg8x_backup/boot_a.bin` 和 `boot_b.bin` 的 v2 静态 header
均未含此参数；这不能排除 ABL 运行时另行追加，XNU 当前也没有该项运行值证据。

LG 的 ADC fallback 按以下整数顺序计算电阻：

```text
mv = floor(BAT_ID_microvolts / 1000)
ohms = floor(100000000 / (floor(1825000 / mv) - 1000))
profile_id_kohm = floor(ohms / 1000)
```

1825mV 是该 LG 电阻公式的偏置；ADC raw→微伏的 1875mV 满量程参考是另一项
参数，两者不能互换。连续分压公式可作交叉检查，复现 LG 选择行为须保留截断顺序。

| 0120a5 的 48 个被接受 BAT_ID 样本 | 最小 | 中位 | 最大 |
| --- | --- | --- | --- |
| 电压，µV | 888581 | 889489 | 889814 |
| LG 整数公式，Ω | 94786 | 95057 | 95057 |
| 送入 LG profile 匹配的 kΩ | 94 | 95 | 95 |

`drivers/of/of_batterydata.c:318` 的 `of_batterydata_get_best_profile()` 取
`limit=floor(nominal_kohm*range_pct/100)`，在范围内选最接近者。MH2LM common
PM DTS 的容差为 15%。LG 在无匹配时还会选择默认 Tocad，这个 fallback 不能
成为移植驱动的身份校验；本轮是实测电阻落入范围，并非依赖默认名称。

## 两份 profile 与实际容量交叉核对

板级来源：`arch/arm64/boot/dts/lge/sm8150-mh2lm/`
`sm8150-mh2lm_common/sm8150-mh2lm_common-pm.dtsi:76`。
profile 来源：`arch/arm64/boot/dts/lge/battery_profile/` 下两份
`LGE_BLT42_*_4000mAh.dtsi`。

| 项目 | Tocad | LGC |
| --- | --- | --- |
| 标称 ID / LG 整数容差区间 | 91kΩ / 78～104kΩ | 12kΩ / 11～13kΩ |
| profile 数据长度 | 416 字节 | 416 字节 |
| profile 内 nominal capacity | 4027mAh | 4073mAh |
| profile FV / FCC | 4430mV / 3300mA | 4430mV / 3300mA |
| 当前电阻与 nominal 是否均相符 | 是，候选 | 否 |
| 已读回当前 FG 的 24 字节前缀 | 尚未 | 尚未 |

nominal capacity 位于 profile offset `(271-65)*2=412`，为小端 16-bit mAh，
对应 LG `qpnp-fg-gen4.c` 的 `NOM_CAP_WORD=271`。0120a5 的有效 CAP 记录均为
4027mAh，与 Tocad 数据吻合，不能用两份 profile 文件名的“4000mAh”替代实际值。

**FCC 还有板级覆盖。** common PM DTS:118 的
`lge-battery-supplement/qcom,fastchg-current-ma=2800` 被
`extension_fg_load_dt_post():1139` 写入 `fg->bp.fastchg_curr_ma`，覆盖 profile 的
3300mA。未来身份限值应保留这个 2800mA 板级约束；它不是当前已经允许的电流。
已有首次受控策略仍限定 FCC/ICL 各最多 500mA、FV 最多 4200mV，并且还有其他互锁。
温度系数 Tocad=9、LGC=30；当前温度没有 LG 补偿，不能说已复现 LG 温控。

目标 common DTS、实测电阻和容量相符；最终运行的 Android DTBO/revision
未独立核对。`scratch/mh2lm-dt/android-mh2lm.dts` 是缺 LG 电池节点的 base DT，
不能充当最终 overlay 证据。身份候选也不等于已验证全部板级拓扑。

## 最小缺口：只读当前 FG 的 profile 前缀

`qcom/qpnp-fg-gen4.c:2179` 的 `is_profile_load_required()` 在 integrity 有效后
比对 `PROFILE_LOAD_WORD=65, offset=0, len=24`。当前 integrity=0x13 包含
load bit0、bootloader-load bit1、first-load bit4，符合该函数白名单；这说明
标志组合合法，不能单独证明 profile 数据相符。

两份预期前缀：

```text
Tocad: 0900ffe3f10796dab3d50000a1bda38af88784929e942380
LGC:   0900dfe38cdc11e3a8dc0000cabc3e8bf9878480b58d1f80
```

完整 416 字节 profile 的 SHA256：

```text
Tocad: ba834dc2c21899d5f33e6bd0d7079fce3754cf7fbb01a61d9a8dba508cd2fa5d
LGC:   eaa084c47c0cffcc1c4077770eb21f0eff418a50451e4c5bbbf10e4a73180ce8
```

下一实现应在现有有界 FG session 内增加固定的 profile-prefix typed 读取，
由内部 PMIC 组件采样，电池策略不传 SRAM word/SPMI 地址。沿用 `sp_fg_lock`、
request/grant、DMA 状态检查与恢复/poison 语义，成功完成并清理后才发布结果。
不重载 profile、不改学习数据；“只读 profile”仍需要已有内存访问协议的配置写入。

特别注意分区：`qcom/fg-memif.c:1169` 的 Gen4 map 将 word64～169 映射到
`0x4520+(word-64)*2`，因此 word65 的前缀窗口为 **SID2 0x4522～0x4539**。
现有四字段 FG reader 只处理 word≥170，不能把 65 填入其最后一个分支，造成
`word-170` 下溢。应增加明确固定分区与读取长度，不开放任意 SRAM 地址接口。
24 字节比较复用 LG 的既有匹配约定，不宣称它证明整个 416 字节逐字节相同。

## 输入能力：APSD、Type-C 与 USB 枚举需要分开判断

参考 `qcom/smb5-lib.c:552` 的 `smblib_get_apsd_result()`，先要求
0x1307 bit0 DONE，再对 0x1308 &0x7f 做精确枚举；不能只判断“没 QC 位”。

| APSD result | 原始值 |
| --- | --- |
| UNKNOWN / SDP / OCP | 0 / 0x01 / 0x02 |
| CDP / DCP / FLOAT | 0x04 / 0x08 / 0x10 |
| QC2 / QC3 | 0x28 / 0x48 |

0x1307 bit1 另表明 QC charger，LG 会据此修正结果；只排除 0x1308 的 0x60
不完整。当前 `sb_decide()` 正是较窄检查，但 `inputVerified` 一直为 false，
尚未因此放行。接入输入授权前必须一并补精确类型、角色与时效校验。

Type-C 源自 `smb5-reg.h:367`、`smblib_get_prop_ufp_mode():3868`：

- 0x1506 &0x7f：Rp default=0x08、1.5A=0x04、3A=0x02、short=0x01；
  0x40/20/10 属于 debug-accessory 路径，不是普通 Rp。
- 0x150b bit6 为 source/DFP 角色，bit5 VBUS、bit4 VBUS error、bit3 debounce、
  bit0 CC attached；0x1509 bit5 为 attach/detach 状态。
- LG 还先读 0x1507 debug-accessory，读 0x150d legacy/noncompliant；当前 snapshot
  不包含这两个字段。不能在缺数据时把 accessory/未连接状态当正常 sink。
- “SOURCE_DEFAULT”描述对端 Rp，不能自动理解成 SDP 已获 500mA。PD 使用独立
  软件策略状态及 PD voter；没有 XNU PD stack、VBUS 约 5V 均不能单独排除继承的
  5V PD 会话。首批输入支持应明确范围，未知状态保持未授权。

`smblib_get_charge_current():5076` 综合 PD vote、legacy、APSD、Rp 与 settled ICL。
不要直接移植其中 `max(current, settled)` 来证明电源承诺：ICL/FCC 是硬件配置，
settled ICL 是算法结果，都不能替代供电协议的许可。

### 已有日志足够说明什么

0120a3 启动 preflight（约 19.397～19.419s）的实际读值：

| 地址 / 含义 | 值 | 能确认的范围 |
| --- | --- | --- |
| 0x1307 / 0x1308 | 0x01 / 0x01 | 当次 APSD done，结果 SDP |
| 0x1061 FCC / 0x1070 FV | 0x34 / 0x53 | 继承配置 2600mA / 4430mV |
| 0x1370 ICL / 0x1108 settled | 0x1e / 0x3c | 设定 1500mA / settled 3000mA，非输入许可 |
| 0x1340 / 0x1342 / 0x1365 / 0x1366 | 0 / 0 / 0xa5 / 0x02 | 模式原始配置，不能只读 ICL 数字 |
| 0x1506 / 0x1509 / 0x150b | 0x40 / 0x2c / 0x2a | 不是普通 Rp default 且 CC attached 未置位；待解释 |
| 0x1042 / 0x1651 / 0x1653 | 0 / 0x62 / 0x8c | 禁充初态、仍有 watchdog 约束 |

这些是依次读取的旧启动片段，没有对应同一时段的 VBUS 或完整角色变化记录，
不能组合成当前有效输入 contract。特别是 SDP 与较大的 ICL/settled 数字并存，
说明“读到大限流值”不足以批准取电，不证明电脑真能供给相应电流。

0120a5 已接受 607 个 USBIN 结果，4610032～4994112µV、中位 4813488µV；
证明测量链路和本轮电压范围，没有提供承诺电流。30 位 valid bitmap 仅表示
snapshot 读取成功，CONFIG v3 没打印 APSD/Rp/ICL/FCC/FV 数值，不能从 bitmap
补出这轮实际类型与模式。

### 当前 USB gadget 还不能直接作为电流证据

0156 的 `bsd/dev/sweet_usb.c:700` 配置描述符为 bus-powered，bMaxPower=250，
USB2 单位 2mA，即声明最多 500mA。设备请求这个配置不等于 host 已接受；
`ud.configured` 是内部 endpoint 状态，没有对电池服务提供带 generation 的
枚举/配置/挂起/断连接口。

另有具体边界缺口：`SET_CONFIGURATION` 分支仅在 value1 时配置端点，value0
并不清 `ud.configured`（USB reset 会清）。因此不能把这个 bool 直接变成“可取
500mA”；必须先补配置撤销、reset/disconnect/suspend 及匹配生命周期语义。
本轮只记录，未修改 USB。首次受控充电可优先选已识别的常规充电器路径，电脑
SDP 的电流授权另按 USB provider 正确接入，不拿现有 USB 日志可用性代替电源许可。

## 下一实现与最小实机批次

离线核对已结束。以下是尚未实现的下一批任务，可同一构建完成，不要求现在
重刷 0120a5，也不新增“重复 ADC 正常路径”的测试轮次。

1. 固定 FG profile-prefix 读取，记录 24 字节、integrity、nominal、各项错误及
   时间；身份计算使用真实 BAT_ID、范围、前缀和板级限值，失败明确报未知。
2. 先输出已经采集的输入配置：APSD status/result、USB path/suspend、ICL
   value/override/load/mode/settled、Type-C sink/attached/misc、FCC/FV；按 sample id
   拆成短行，带 raw/error、输入 source epoch 和独立采样起止时间。
   若确需区分 accessory/legacy，再在现有后端开放固定 0x1507/0x150d typed 字段。
3. 输入证据由电池自己的 workloop 更新，事件先撤销、识别完成再重建；monitor
   刷新普通遥测时间不能延长缓存 APSD/Type-C 的有效期。不引入扫描发现插拔的
   轮询、不在匹配通知里做重操作、不让策略直接读寄存器。
4. 接输入 consumer 时同时补精确 APSD/QC/角色校验及输入独立时效；首批允许
   的类型明确列出。SDP、PD、accessory、unknown 不靠数值猜测授权。profile
   比对成功也不能跳过并联隔离与 watchdog。

新产物完成后用同一观察镜像取得 profile 及每次输入变化记录；电脑 USB 可留
live，普通已知 5V 充电器使用持久日志。两种输入可以分开启动/采集，不需为每种
电源重新编译。无需刻意重复 657 次 ADC 转换；当前仍不打包启充验收档。
具体镜像名与步骤等下一实现、本地检查及云产物核对后给出。

六段主计划第2～3段继续推进。当前真正剩余项是 profile/输入证据消费者、
SMB1355 隔离及 watchdog 契约；这些闭合后才进行禁充初态→受控使能→真实充电
与停止的第4段验收。
