# DSI0 PHY、PLL 与输出时钟重建（0191，PD2 / D1 后续）

2026-11-11，接0190 / `90c51d6`。新增 `SweetMDSSLink.inc`、数值配置表
`SweetMDSSLinkData.inc` 和纯计算 `SweetDSILink.h`，已接实际 managed 自有扫描接管。
**本地编译与回归通过，尚未上机。实际启动仍要求温态 splash，完整面板驱动未完成。**

## 架构与执行边界

```text
AppleCLCD / SweetFBScanout
 └─ SweetDisplayBackend typed begin/status
     └─ SweetMDSS : IOService（原 MMIO owner、workloop、gate）
         ├─ managed 协调器 / 原 scanout timer
         │   ├─ SweetRPMh：LDO、ARC
         │   ├─ core / 原 clock timer：CN0、GDSC、核心 PLL、AHB/MDP
         │   ├─ NoC / 原 NoC timer：display 带宽
         │   ├─ clock：独立 DSI0 lease、共享引用与父源账本
         │   ├─ link / 原 DSI timer：PHY0、7nm-v2 PLL、BYTE/PCLK roots
         │   └─ host / 同一 DSI timer：v2.3 host 软复位及公共配置
         └─ 原 IRQ、扫描池 pin、新像素传输完成
```

没有新增 provider 空壳、映射、线程、全局实例桥或第二时钟 writer。link 与 host、DSI
命令 executor 共用 timer，沿原 gate 串行；`fClockTouched` 复用原 writer 退休保护。
startup/matched 不复位硬件；资源获取、扫描暂停和 IRQ Idle 后才进入 `ManagedLink`。

显式 `sweetdsilink=2` 要求 `sweetdsihost=1`、`sweetdisplaycore=2`、
`sweetdisplaymanaged=1` 及原 own2/power1/IRQ2/resource2/clock1/NoC 前提。
默认0保留0190路径；无新镜像矩阵，不为此单段要求 push 或刷机。

profile2 表示按 LG compatible 选择 PHY v4.0 / PLL 7nm-v2，**不是自动识别硬件版本**。
写前检查原 PHY0 窗口物理地址 `0x0ae94400`、长度至少 `0x760`，记录原始四个 revision
byte，拒绝非 byte 值；没有臆造 revision 编码。还检查温态 PLL/REFGEN/lanes、DPHY、
standalone 源、lane map、PN swap、无 ULPS 控制和 DSI1 对 PHY0 的别名使用。
未知/不兼容状态拒绝写入，不能把选择参数或本地 fixture 当作新硬件身份测量。

## 真实重建与完成条件

1. typed rails/ARC/NoC/DSI0 lease 均完成，核心资源正常；TE/auto 停止、IRQ Idle。
   关闭 PHY0 动态门控和 host 低3位，使能关闭后独立等四条 data lane/clock lane stop
   state 及 DMA/MDP idle。只关闭 mixed INT_CTRL 的 enables，不随意 ACK W1C 状态。
2. 按 pixel→byte-intf→byte 关分支；独立确认三个 `CLK_OFF`、两个 `ROOT_OFF`，且
   RCG UPDATE/dirty 已清。软件 enable 清除或配置写回不能授权停 PLL/改父源。
3. 关全局时钟，写 PLL stop，**观察 lock 清除后**才关 bias/PHY、复位 PHY。
   PHY reset `DSI+0x12c=0x01000001` 至少1ms，退出后等待100µs，再等 REFGEN ready。
4. 明确配置 common、四数据 lane/clock lane、14项 timing；不回放 ABL 快照。
   LG v4 lane CFG 仅写前三字节，第四字节不是一个 CFG3 寄存器。
   PLL 初值每次 timer 最多8项，再写 v2 固定项、分数频率、DOWN-SSC 和输出分频。
   排除 RO status/debug；最大 PHY offset 为 `0x75c`。
5. bias 等待至少250ns，再 start PLL；**独立观察新 lock**才进行数字 reset/global clock。
   单独更新 BYTE/PCLK 两个 RCG，等 UPDATE/dirty 清；设置 byte-intf /2，按反序启分支。
   分别检查真实分支运行和根时钟运行，配置写回或 Held token 均不能代替。
6. resync FIFO 后核实配置、锁定和保留字段；只更新本次控制的父源 CFG/PCLK MND/INTF
   divider 快照，不整体重新采集后接受所有漂移。其他 MND、共享字段/父源漂移仍失败。
7. `LinkReady` 后才重建 host；host 完成后才重开原 IRQ、pin/提交自有 A，并要求原
   fresh PP_DONE、flush=0、RD_PTR 新帧。PLL lock/host ready 不能代替像素完成。

目标 bit clock 1,056,619,200Hz；VCO /2→bit，bit /8→byte，byte /2→byte-intf，bit /6→pixel。
19.2MHz XO 内部倍频、18-bit fraction，DEC=0x37、FRAC=0x2106；计算实际 VCO
2,113,238,378Hz、byte 132,077,398Hz、byte-intf 66,038,699Hz、pixel 176,103,198Hz。
SSC 默认31.5kHz/5000ppm DOWN，period302/step714。以上是编码计算值，**不是测频结果**。
LG 四个 R66456 PP1/DV1/DV2/PV 源表的目标 clockrate 和14项 timing 一致；不因此认定
本机 DDIC revision，也不授权发送任何版本的厂商表。

整段250ms预算；lane idle100µs、halt/run10ms、unlock/lock5ms、REFGEN1ms、RCG500µs。
所有等待由现有 timer 推进，不在 gate 长忙等。每步重新核对资源、非受控时钟、别名和 idle。
重建期间亮度/DCS/电源写不准入，clock/NoC 修改 Busy，原 token 查询可用；快照/probe
避开过渡/故障。已保留的 clock lease 不会在物理停钟期间谎报 `localClocksReady()`。

失败保留票和部分硬件状态；已触及硬件则 poison DSI，停止推进且禁止新提交。
不自动重试、回放旧寄存器或恢复 TE，不盲目撤电；stop/晚 timer 不访问硬件，也不偷偷
释放处于保持中的 reset。正常 DCS OFF/ON 保留资源，不重复此重建。

## 来源与验证

LG 主源 HEAD `d4f335e13d73c4aa2af85581a6ec11b670ee5b94`：

- `dsi_phy_hw_v4_0.c`：PHY enable/disable、REFGEN、lane/timing、stop state、resync。
- `mdss-dsi-pll-7nm.c`：v2 数值表、DEC/FRAC/SSC、bias、数字 reset、PLL/global clock 和分频。
- `dsi_ctrl_hw_2_2.c` / `dsi_ctrl_hw_cmn.c`：PHY reset、DISPCC wrapper mask/dynamic gating。
- `dispcc-sm8150.c` / `clk-rcg2.c`：BYTE/PCLK 分支、父源、divider 与 UPDATE/ROOT_OFF。
- `sm8150-sde{,-pll}.dtsi`、`sm8150-v2.dtsi` 和四个 R66456 源表：硬件描述与公共目标。

逐文件 SHA、表项和公式核对存于 `scratch/framebuffer-0191/reference-audit.json`、
`pll-table-audit.json`。这些是源码证据，没有新增实机身份/写入/锁定结果。

- 0191基线逐字节等于0190验证树；补丁严格应用无 offset/fuzz，10份变化源码一致，
  原114个 CI source marker 保留，CircleCI blob 仍为 `da0b6c0947a66f03491b3a9023590b49665292df`。
- 真实 XNU/KDK 头文件、arm64 MDSS 基线/最终对象编译通过。warning 1417→1475，
  新增58条均为有界数组 usage 提示；已按 map12、clock0..9、root0..1、lane0..4、
  timing0..13、固定表和最终 parents4..6 的边界逐项审阅，无 warning 抑制。
- 12套检查通过：新增 link 专项及原11套；11套实际源码 ASan/UBSan，另1套 Python ADT。
  UBSan halt_on_error，无 sanitizer 报告；LSan 因 ptrace 环境限制关闭，另断言对象、
  映射、通知与分配归零。
- link 专项沿真实 managed/IRQ/资源/host/扫描链，模拟 PHY reset 销毁旧配置；覆盖
  分支与 root 独立停稳/运行、旧 lock 不清/新 lock 不到、REFGEN/RCG 超时、>=1ms reset、
  分数频率/SSC oracle、丢写/非受控漂移/别名、token 互斥、provider 丢失、各阶段 stop、
  晚 timer 无 MMIO、未就绪不 pin/提交、host 阶段 PLL 丢锁，以及最终 fresh 像素完成。
- 底稿 `scratch/framebuffer-0191/{base,edited,verify,prepare-patch.py,compile.py,verification.json,
  native-results.json,warning-counts.json,array-bound-review.json,reference-audit.json,tests.json}`。

## 未完成与下一步

本段实现的是温态资源链内的 PHY/PLL/host 破坏后重建，不是整屏冷启动。
面板身份确认、GPIO reset/pinctrl、完整 ON/OFF、资源反向释放、NoC 冷 solver/QoS/端口/
sleepwake、MDP 全部冷初始化、自有首帧/default surface/console/panic 与 ABL 完全退出仍缺。
接下来推进面板完整生命周期和必要 provider 接口，再做状态丢失后的独立重建及零拷贝。
A 单缓冲/B 未提交/30Hz CPU 拷贝未变；未宣称旧卡顿修复。USB/按键/触摸/电池/mux未改，
未 push、触发 CI 或操作设备；无新 boot 或硬件验收，不重测旧 BUG-002/USB充电/F3/半小时。
