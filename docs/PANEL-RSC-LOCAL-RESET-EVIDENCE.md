# 0213：RSC 局部状态与复位取证

**a26取证已完成：** BCR三样本均0，core转换前后局部CBCR相同；仍被enable门槛拒绝。
见[a26实测](PANEL-D1-0121A26.md#实测复位位清除仍由enable门槛超时)。后续[0214](PANEL-VOTED-HALT-COMPLETION.md)
已修改自身voted请求完成判据并本地验证，待云编译；以下待a26实机为历史。

**0121a26已核对并打包，待实机：** 78源码及185 binary markers通过，包含本补丁；
原ABI608 retained镜像回读通过。下一步用户临时启动约90秒提取完整USB日志，
详见[0121a26交付](PANEL-D1-0121A26.md)。无需再次云编译，下文“待用户云编译”为历史。

2026-11-13。[a25实测](PANEL-D1-0121A25.md#实测写后及等待均未观察到使能)已确认CPU物理翻译正确，
写后及39次等待均未观察到branch7 enable。尚无RSCC_BCR或core转换前的局部状态样本，不能确定写入未读回的原因。
本补丁只补这两处证据，未修复或证明完整面板接管成功。

## 实现与范围

`0213-mh2lm-rsc-local-reset-evidence.patch`仅修改SweetMDSS.cpp、SweetMDSSClock.inc和SweetMDSSCore.inc。
保留原寄存器写入、完成判据、10ms/200ms期限、timer请求、失败保票和NoC/DMA准入。

| 阶段 | 采样条件与内容 |
|---|---|
| warm-entry | warm core请求通过原访问/域/idle检查后、core寄存器转换前；读取BCR、三路CBCR、AHB CMD/CFG |
| pre-write | NoC时钟阶段，branch7 retain已通过原写入前检查，实际enable写入前；同样读取六个寄存器 |
| timeout reset | 同一branch7请求已取得pre-write样本且原clockPrerequisites通过；两个原Timeout出口各只读取一次BCR |

六个寄存器依次为DISPCC+0x4000、+0x4004、+0x400c、+0x4008、+0x22bc、+0x22c0。
LG reset表和本机ClockDxe均将RSCC_BCR定义为+0x4000、bit0控制复位；其余均为已使用的时钟寄存器。
所有读取位于既有DISPCC映射内，不访问尚未取得时钟租约的display RSC/TCS/wrapper。
每组为有顺序的读取，时间戳在该组末尾；不是同一瞬间的原子快照。

warm样本单独保存，在RSC请求受理时复制入本次诊断缓存；每次请求按原规则清空其他缓存，release不继承旧样本。
cold core路径不采集warm上下文。未发生branch7实际写入、其他分支、release、前置访问检查失败或stop/晚回调，
都不会触发新的timeout BCR读取。超时出口仍保持原CLOCK SAMPLE的current/历史含义，不额外读取CBCR。

原六条超时日志保留，另外输出：

- `SWEET MDSS CLOCK LOCAL: stage=warm-entry`：valid、bcr、三路CBCR、AHB CMD/CFG、sampled-ns。
- `SWEET MDSS CLOCK LOCAL: stage=pre-write`：同样字段。
- `SWEET MDSS CLOCK RESET:`：valid、超时BCR及其sampled-ns。

正常成功路径不打印这些ERROR。记录无效时valid=0，其他字段不能当作硬件值。
logger本身只读缓存；所有诊断字段不参与控制。典型此次失败路径共新增12次上下文读取和1次BCR读取，
不写复位、不重发enable、不增加重试，也不放宽自身使能检查。新增读取会带来少量实机耗时，不能声称绝对零时序影响。

## 判断范围

- warm-entry已存在异常、pre-write没有变化：优先检查继承的局部状态，而非把变化归给core转换。
- warm-entry与pre-write不同：可将变化范围缩小到这两个阶段之间；仍不能单凭时间相关性指定写入者。
- BCR bit0为1：提供复位被请求的线索，不等于已证明这是enable读零的原因；必须核对具体复位语义后再改控制。
- BCR始终清除且enable始终未读回：排除所采样时刻的简单复位位解释，继续查写入语义及访问条件；不能直接认定secure world拦截。
- overall超时的RESET是该出口新采的BCR，CLOCK SAMPLE仍可能是历史CBCR；两个时间戳不可混为同一现场。

LG开启等待只验halt，当前XNU额外要求enable读回；该实现差异已明确记录。
CBCR0满足LG halt等待，不足以证明当前XNU生命周期已经取得稳定的时钟控制，也不足以证明物理时钟停止。
本补丁保持检查，以取得决定下一步控制修正所需的证据。

## 本地验证

- 27个真实provider场景通过ASan/UBSan：不同阶段BCR/局部时钟变化、branch/overall超时、
  早期无写入、release缓存清除、cold无读取、访问丢失、logger只读缓存、stop/晚回调。
- 与实际a25/0212基线比较完整MMIO轨迹，只允许44组完整上下文读取和16次超时BCR读取（全部场景合计）；
  去除这些精确序列后，其余MMIO、结果、所有权、timer状态及模拟时间完全一致。
- 共15套显示/时钟/NoC/生命周期回归通过。
- 真实XNU/KDK ARM64 SweetMDSS基线及修改版对象编译通过，告警1552→1552，无新增或移除告警。
- 0185–0213严格累计重放无offset/fuzz，78源码字节一致，114项原CI source markers通过。
- 打包入口新增LOCAL/RESET两项binary marker；实际旧a25被LOCAL门槛拒绝，无boot输出。bash语法通过。
- 补丁SHA256：`06bcc6dce23f4b376ea40422655c7894ee56cf301f1d328d054414bf28786083`。

证据保存在`scratch/framebuffer-0213/`：verification/tests/cumulative-replay/warning-comparison JSON、
test-local-baseline.txt、matrix-rejection.json及编译/回归输出。
没有重新全量编译内核；没有修改CircleCI，blob仍为`da0b6c0947a66f03491b3a9023590b49665292df`。

## 交付下一步

需要用户`git push origin main`并云编译，下载新kernel/buildtree，建议标签0121a26。
agent核对三份变化源码、旧183项及新2项binary markers、自带SHA后，沿原ABI608固件/ADT/605字节参数打retained。
启动约90秒保存完整USB日志，保留原六条及新增LOCAL/RESET。a25无需重测或补持久日志。
亮度/OFFON/长测和其他显示档继续暂停。当前尚无新boot镜像，未push、触发CI或操作设备。
