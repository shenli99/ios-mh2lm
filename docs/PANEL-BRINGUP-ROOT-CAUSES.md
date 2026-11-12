# 面板接管问题归因汇总（截至 0121a23）

2026-11-12。当前在完整驱动计划 PD 阶段的 retained 首次接管，还没有完成新 DMA 接管。
相同的“屏幕停在启动文字、USB和按键仍有活动”，先后由不同的依赖项失败引起。
不能把几轮静帧认成一个硬件故障反复出现，也不能把修复某项前置说成整个驱动已完成。

## 已定位的问题与证据

| 问题 | 原因及修正 | 已有验证与边界 |
|---|---|---|
| a14 HFS NULL 写 panic | DarwinBoot 命令行数组1024字节，实际XNU ABI是608。739字节参数覆盖内核所读的 bootFlags / memSizeActual，后者读成 `playcore`；实际HFS默认事务缓冲计算溢出。loader统一ABI608并强制长度检查 | 结构错位和计算溢出离线复现，修正后的identity实机进入系统；未捕获原崩溃分配返回值，不能称完整panic重现。不是“面板调用了HFS”的证据 |
| identity ADT 与模式不一致 | 声明了NoC资源，模式却是resource1/clock0，与NoC入口要求不符；修正metadata档声明 | 与ABI错误分开处理，不能认作原HFS panic的已证实原因 |
| a15 ESC0 时钟准入拒绝 | 旧父时钟检查只认XO CFG/HID=1，漏掉LG支持的HID=0旁路。0203接受合法0/1并保留原始配置漂移检查 | a16七路boot clock准入通过；没有普遍放行任意父源 |
| a16–a17 ACV metadata失败 | 把ACV当普通带宽缩放资源，统一要求width非零。真实Command DB为unit1/width0/VCD11；ACV是开关资源。0205保留width0，按明确mask8编码X0/Y8 | a18元数据准入通过；不是解析偏移错误。直到a23仍没走到display TCS，不声称实际ACV新投票ACK已完成 |
| a18触摸初始准入失败 | IOService已发布早于TCM worker ready，显示首次接管请求暂停触摸过早。0206通过异步等待就绪后再quiesce，恢复正常操作期限 | a19 waiting→worker-ready→quiesce通过。是服务发布与工作线程生命周期竞态，不是触摸和显示共用IRQ造成的冲突 |
| a19–a20首笔ldoa14 RPMh超时 | 原完成路径没有取得本轮fresh完成证据。a20看到IRQ_ENABLE=0、saved=1、IRQ_STATUS=0、progress=0；idle/CMD_STATUS旧值不能当新ACK。0208接入apps RSC SPI5/INTID37真实完成IRQ及清理 | a21及后续10次fresh IRQ、四LDO/ARC/CN0成功，core2通过；这证明相关事务闭合，不代替物理电压测量或后续display RSC验收 |
| a21通用NoC NotReady | 原日志没有细分clock lease、分支、wrapper、solver准入。0209只增加具体失败分支与软件上下文 | a22将首错缩小到branch7；诊断本身不是硬件修复 |
| a22 RSC branch7被拒 | NON_GDSC_AHB是BRANCH_VOTED，原策略按独占halt及关闭态处理，且没对接core2已建立的PLL1 AHB父源。0210明确voted/halt语义，在实际coreReady和typed ARC/CN0凭据约束下取得自身票 | a23越过原准入并发出enable RMW；尚无Held，不能称RSC时钟获取成功 |
| a23 RSC branch7开启等待超时 | **具体原因未定。** 只知道CLOCK token2 Timeout、cursor7/waiting1。原日志没区分overall/branch-wait，也没记录使能读回与CLK_OFF样本 | 0211增加有界超时取证，待新编译/实机。不能从owned0x80推断写后读回成功，不能断言硬件持续OFF 10ms |

原始材料：
[ABI及HFS](BOOTARGS-ABI-0121A14.md)、[XO旁路](PANEL-D1-0121A15.md)、
[ACV建模](PANEL-ACV-ENABLE-MASK.md)、[触摸就绪](PANEL-TOUCH-STARTUP-WAIT.md)、
[RPMh超时快照](PANEL-RPMH-FIRST-VOTE-TIMEOUT.md)、[真实完成IRQ](PANEL-RPMH-COMPLETION-IRQ.md)、
[NoC取证](PANEL-NOC-ADMISSION-DIAGNOSTICS.md)、[RSC投票](PANEL-RSC-CLOCK-VOTES.md)、
[a23实测](PANEL-D1-0121A23.md)、[下一轮超时取证](PANEL-CLOCK-TIMEOUT-DIAGNOSTICS.md)。
各文档保留原日志行号、哈希和离线复现底稿，不以本汇总替代原证据。

父链与halt的后续专项核对见[审计记录](PANEL-BRANCH7-PARENT-HALT-AUDIT.md)：
确认当前未漏掉PLL1 main/root的基本开启和物理检查；另有safe-config序列及RSC三路顺序差异待实证，
不能把它们直接写成已确定根因。

## 当前首错和连锁结果

a23的先后关系是：

1. apps RPMh fresh完成，四LDO/ARC/CN0、core2成功，当前CN0 token7确认。
2. RSC branch7发出enable，CLOCK token2在等待中Timeout，是本轮第一条ERROR。
3. NoC查询到clock失败，返回NotReady；尚未进入display TCS命令组。
4. managed/scanout/lifecycle/HANDOFF随之失败，保持资源不确定账本，拒绝新DMA；画面留在旧帧。
5. 后续按键触发生命周期准入失败，是失败状态的延续，不是新的独立首错。

这份日志没有kernel panic签名，后续仍有USB、按键和进程活动，因此不能称整核死锁。
CB2 FSR=0x400审计WARN仍需保留，但后续已经进入managed，不能把它当本轮已确定的停止点，
也不能因其没有阻止准入就认定无害。

## 为什么花了多轮

前几项是我们实现中的软件问题：ABI没有先核对实际构建头文件；时钟和BCM模型未覆盖本机
合法状态；服务发布被误当作worker就绪；RPMh完成路径没有闭合到真实IRQ。
这些不能笼统归给硬件或“厂商实现特殊”。受控接管按依赖顺序停在第一个失败项，
修复前置后才暴露下一个阻塞，所以表面都像静帧，实际首错在向后推进。

诊断粒度不足也增加了轮次：通用NotReady/Timeout无法区分具体gate和时序，只能另做一次
取证。0207/0209/0211分别补上事务快照、准入分支、超时样本；它们本身不应被算成修复验收。
后续以“首错＋原始样本＋本轮完成证据”为最小记录，区分映射成功、服务就绪、软件写入、
硬件读回、物理完成和新DMA，避免用上一层成功代替下一层验收。

## 与本次首错分开的旧问题

- 插电大图标后缺时间（BUG-003）与关机滑条区域半透明颜色异常仍未定因；前期实验没有
  证明具体时间字形一定在原始提交中缺失，也没有证明由面板寄存器配置导致。按用户决定暂缓。
- a12单缓冲接管后用户报告滑动更卡，a13有所改善；这不是双缓冲/零拷贝已经完成。
  拷贝双缓冲优化已延期，继续优先完成硬件驱动与必要生命周期。
- USB多次插拔识别失败（BUG-001）、mux收尾pending误隔离（BUG-002，代码已修复、正常使用观察）、
  电脑USB供电波动引发充电图标反复变化（BUG-004）分别由[BUGS](BUGS.md)跟踪，不并入面板首错。
- a23的USB WARN、FG profile mismatch / charging=0、HFS clonefile -34只是伴随记录，
  没有证据将它们归因到branch7 Timeout；HFS -34也不同于早期NULL写panic。
- 长时息屏掉电与整机低功耗缺失另属大目标四，不把显示DCS息屏等同于整机休眠。

目前没有完整接管成功的实机证据。下一步先用0211区分enable读回、CLK_OFF和超时出口，
再根据实际值决定硬件修正；不通过延长期限、跳过校验或回退ABL掩盖失败。
