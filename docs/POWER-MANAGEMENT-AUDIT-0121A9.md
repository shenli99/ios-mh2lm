# 0121a9：息屏耗电与低功耗实现核对

2026-11-09，基于0121a9实际源码与长测日志；只做分析，不修改驱动或启动系统sleep。

## 判断

用户观察到较大掉电，有遥测依据：整份日志93%→78%，息屏前最后一次发布91%，
息屏段内降到78%。这不是只有UI变动；FG电流持续为负，电压也下降。
**当前移植仍处于显示关闭、系统继续运行的状态；这种耗电符合尚未完成低功耗管理的
开发阶段，但不能当作正常手机待机功耗。** 显示长时唤醒通过不代表待机功耗已验收。

尚不能用这份总电流日志给CPU、MDSS、USB等逐项分摊功耗，也不能判断相对上一版增加了
多少；没有同条件功耗对照，不将它直接登记为0177新引入的回归或电池故障。

## 电量与电流数据

原始日志`work1/logs/sweetlog-0121a9-fb-power-dcs-long.bin`，SHA256：
`0ae824787b04471ec67b721c78e3ea5434ed924319ad3aff8bd333a9b6228335`；未回卷。
解码/计算底稿：`work1/scratch/artifact-0121a9-long/`
下`analyze.py`、`power-usage.py`、`analysis.json`、`power-usage.json`。

| 项目 | 结果与证据 |
|---|---|
| 全日志电量 | 126行93% / MSOC0xef；7651行最后一次发布78% / MSOC0xc8 |
| 息屏前电量 | 3418行91% / MSOC0xe9；3472行实际DCS off |
| 息屏内样本 | id14～100，共87个，首末采样内核时间相差4805.156秒，约80分钟 |
| 息屏电流 | -393～-289mA，中位-294mA；按采样间隔加权约-311mA |
| 息屏电压 | id14为4159mV，id100为4048mV |
| 温度 | 息屏内35～36.25℃；末样本35℃ |
| 放电估算 | 对离散电流作梯形积分，约415mAh、1.70Wh，平均约1.28W |
| 恢复 | 7958行DCS on rc0；用户确认画面、亮度和触摸正常 |

415mAh/1.28W是约每分钟一次瞬时采样的积分估计，不是硬件累计库仑或功率计读数；
采样可能遗漏短峰值，不能作为精确能耗标定。没有把profile里未通过有效性检查的3510mAh
当作已确认电池容量，也不强制用百分比反推mAh。4805秒是息屏段采样跨度，不是精确
off-on秒表测量；用户按半小时要求操作，日志覆盖了更长区间。

`SweetPmicPower.cpp:154–157`读FG一致的IBATT/副本，以488281nA/LSB换算，并转换为
本驱动“负值放电”的符号。电流/电压/温度样本均有效；电量来自FG，非模拟递减。
本轮未记录有效外部输入/启充，不把这段当成功息屏充电。

## 驱动实际具备什么

下表“未实现”指本移植驱动没有相应控制链；不能据此声称ABL遗留配置或硬件自主节能
完全不存在。IRQ线程阻塞、DCS的LP传输模式、IOPMPowerSource信息发布，都不等同整机休眠。

| 模块 | 已有处理 | 缺失或尚未验证 |
|---|---|---|
| CPU / 平台 | XNU通用WFE/WFI idle框架；8核CPU_ON和PSCI关机/重启 | SweetCPU的quiesceCPU/haltCPU仍直接panic；无本板已实现的CPU suspend/恢复链，浅idle驻留比例未测，未见自有DVFS策略 |
| 面板 / SweetMDSS | 亮度0、typed DCS28/29、可恢复状态机；息屏保留像素 | 不发10/11，不关闭面板电源轨/reset，不主动管理MDSS/DSI时钟和电源域的息屏关闭/恢复 |
| AppleCLCD framebuffer | OFF时不继续常规present，保留最新待显示内容 | 软件vsync定时器仍每16667us唤起并先通知clients；OFF分支继续重设timer。sweetfb_hz=30只降拷贝频率，不降这个60Hz timer |
| USB DWC3 / PHY | IRQ worker空闲时阻塞，避免持续扫描事件缓冲 | gadget启动后RUN_STOP及VBUS override常驻；未接电池USBIN控制启停，也未实现拔线runtime suspend和PHY/时钟恢复 |
| 触摸TouchComm | IRQ-only，无消息时线程阻塞；息屏后本轮无触摸report | 未将display/system PM接入TCM deep-sleep/低功耗扫描、gesture wake或resume；无触摸事件不代表控制器已睡眠 |
| 按键 / SPMI mux | IRQ事件驱动、无输入时线程/workloop等待 | 无整机睡眠时wake-source保留、PDC/GIC状态恢复的完整实现；按键PMIC事件本应作为必要唤醒源保留 |
| 电池 / 充电 / ADC5 | 电池IRQ、60/30/10s安全温控节拍；runtime3有WillSleep通知并保留PMIC充电；ADC5按请求转换并恢复配置 | WillSleep只确认通知，没有使整机睡眠；深睡中bark唤醒及恢复未验证，本轮未收到will-sleep日志 |
| UFS / 持久日志 | I/O等待可阻塞线程，避免长时间一直忙等 | 没有本驱动主动管理的runtime Hibern8/系统suspend/PHY-clock恢复；日志线程默认每10s检查并在有新增日志时写盘，息屏不暂停 |

源码依据：0121a9展开文件位于`work1/scratch/artifact-0121a9/actual/xnu/`。

- `iokit/Kernel/arm/SweetFramebuffer.cpp:79,587,651–738`：60Hz定时与先通知后判断OFF；
  `SweetMDSS.cpp:246–299`：显示电源仅DCS开关。
- `bsd/dev/sweet_usb.c:571,1075,1219–1275`：IRQ等待、override和RUN_STOP。
- `bsd/dev/sweet_touch_tcm.c:286,906–940,1194`：等待/报告配置，无PM sleep/resume命令。
- `bsd/dev/sweet_keys.c:261,343`：IRQ等待；mux没有PM电源状态转换接口。
- `iokit/Kernel/arm/SweetBattery.cpp:1835–1875,2085`：实际priority sleep/wake interest；
  `SweetPmicAdc5.cpp:226–260`：请求结束恢复/失败abort。
- `bsd/dev/sweet_ufs.c:208–231,674,755–790`：可阻塞I/O与10s日志周期。

另从同份云源码归档提取CPU/idle文件至
`work1/scratch/artifact-0121a9-long/pm-source/xnu/`：

- `iokit/Kernel/arm/SweetCPU.cpp:255,261`明确panic未实现quiesce/halt-suspend。
  文件SHA256=`73e4e1e0dfb0af5b82581d44d34ec0130b84b7bbcc86198a5e63540df148086d`。
- `osfmk/arm64/machine_routines.c:343`保留通用WFE/WFI入口。不能因为有后台日志就推断
  CPU从未执行WFI，更不能把8核启动成功解释为8核全程满载。
- `SweetPlatformExpert.cpp:57`的SYSTEM_OFF/RESET是关机/重启，不是低功耗suspend。

## 其他明显活动与证据边界

关屏和开屏之间约2290条`exec`，其中fairplayd.H2 481次、bluetoothd 480次、
xpcproxy 1145次，电池userclient日志250条。说明后台启动活动很多；缺少退出状态、
线程CPU时间和PM驻留统计，不能仅凭exec断定每次都是崩溃，或认定这是全部耗电来源。
不把battery每60s安全采样未经测量地定为主要耗电原因。

这份日志没有WillSleep确认、mux隔离或USB reset；前者结合未完成的本板CPU睡眠接口，
支持“没有完成整机休眠”的判断。定时采样/用户态日志本身只能证明周期性活动，不能
单独排除其间发生WFI或短暂低功耗。BUG-002仍按用户决定随正常使用观察，不安排专项复测。

## 建议推进顺序（尚未实现）

1. 显示路线继续处理息屏vsync通知/timer和MDSS电源，保留typed backend和可靠恢复；
   当前F3计划已要求OFF时不产生显示IRQ，需同时明确OFF时不维持60Hz软件通知。
2. 清理反复启动的后台服务和运行日志/持久写盘开销，先记录原因与唤醒来源，避免靠
   屏蔽错误日志掩盖启动失败。沿日志大计划收敛输出，不撤掉当前必需的调试证据。
3. USB、触摸、UFS分别建立真正的设备低功耗/恢复和唤醒源策略，按XNU IOService PM树
   注册电源状态，通过typed provider协作，避免从上层直接改寄存器。
4. 最后连接IOPMrootDomain与SM8150/固件支持的CPU、cluster、系统低功耗入口和恢复。
   本板睡眠接口尚未实现，不宜只强行发system sleep；PSCI CPU_SUSPEND或平台固件所需
   的具体状态/资源投票仍待核对。保持按键、必要USB/PMIC事件可唤醒，先浅后深。

以上为核对与建议，不自动插入新云编译或专项长测，不改变当前显示任务范围。
