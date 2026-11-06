# 电池第2段：测量与配置取证清单

2026-11-06，第1段0150之后的数据契约。0151现已实现SRAM、采样和策略的
本地代码，但表中硬件/身份/并联证据仍未实测闭合；最新状态见
[BATTERY-RUNTIME.md](BATTERY-RUNTIME.md)。下面保留实现前的核对清单。与[实现计划](BATTERY-IMPLEMENTATION-PLAN.md)配套。
0150只提供底层受限接口和一次只读预检，不会批准充电。

每项后续应携带原始值、来源/版本、单位、完成时刻、返回码、有效性；时效由
保护采样预算定义，不用“上次成功”永远有效。配置码与实际测量分开。

| 项目 | 已有依据及下一动作 | 当前是否足够用于控制 |
| --- | --- | --- |
| 芯片/版本 | 现有REVID已确认PM8150B；核对具体revision与Gen4 v1 DMA workaround | 型号已知，版本分支仍需接入SRAM会话 |
| SOC | 已验证0x4009/400a配对、READY、缩放和防跳变 | 已有UI数据；不能替代温度/电流监测 |
| 电压/电流 | 现有0x41a0/41a2及其copy配对，IBATT负为充电；加入独立有效性/时刻，核对直接值/影子一致性 | 0149诊断已有电流，标准IOPM仍未发布实测电流 |
| 真实电池温度 | `qpnp-fg-gen4.c:fg_gen4_get_battery_temp`读SRAM word328 offset0 len2；10-bit有符号码，每LSB0.25°C，Linux转deci°C；继续核对LG修正和单位 | 未实现；UI的25°C占位禁止用于保护放行 |
| SRAM访问 | `fg-memif.c:fg_direct_mem_request/release`：request/grant、Gen4 bit及v1 ALG_ACTIVE/等待workaround、DMA地址/窗口/收尾；选择直接DMA或IMA后完整实现并测试 | 未开放；充电RMW表不允许MEMIF写，须新增专用有界会话接口 |
| USB输入 | 0x1307/1308 APSD，0x1340 suspend，0x1342/1365/1366 ICL模式，0x1370 ICL，0x1380 AICL；0150预检可读 | raw与模式待实机；ICL设定不等于电源承诺 |
| 实际限流/路径 | 0x1107 ICL status、0x1108 AICL settled、0x110b power-path；追加0x1106 ICL max/110a AICL done前先核对地址/权限 | 0150只读部分已有；不是实际输入电流测量 |
| Type-C/高压 | 0x1506/1509/150b仅精确读取，要求唯一EE0写owner；另外核对实际VBUS电压来源和初始会话 | 不足，不能因没发PD/QC就认定5V |
| FCC/FV | 0x1061/1070；LG `qpnp-smb5.c:smb5_pm8150b_params`分别50mA/LSB，FV=3.6V+10mV×码，先验有效码范围 | 0150可读，尚未实际取值；寄存器可编码范围不是本机电池安全上限 |
| ITERM/recharge/硬件保护 | 0x1067..106c/1072/107d..107f、1051、1090、1007/100d；核对真实参数路径、符号与继承来源 | 精确只读表已备部分；预检未全输出，下一段补带有效性的配置快照 |
| watchdog | 0x1651/1653配置；确定是否插件启动/超时会禁充，定义pet责任、期限、停止 | 0150可读，无pet命令实现，不得开启充电后再补 |
| 电池ID/profile/容量 | 实际DTS有BLT42 Tocad/LGC 4000mAh，需ID/实物依据及profile状态；Gen4 nominal word271、integrity word299、learned容量有v1/v2地址分支 | 未取证；5020mAh仍占位，不能作为策略依据 |
| 主/并联拓扑 | 0x1648/1690解释SMB override/select；还需SMB1355实际状态/安全隔离途径，参考DT和LG并联配置 | 未闭合；override0不代表并联关闭；必要I²C依赖单列实现 |

实现顺序：先把现有IBATT与新增配置读数整理成带有效性的内部snapshot，再完成
Gen4 SRAM只读会话取得温度/必要身份依据，随后定义采样时效与缺失阻止原因。
SRAM访问虽为读数据，也要写请求/窗口寄存器；单独审查协议、权限、超时及释放，
不把它描述成完全无写。共享事务锁不跨10ms grant等待或完整会话长期占用。

第2段结束时应能输出“哪些证据有效、哪些缺失因此不得取得控制”，而不是只有
一串原始十六进制。真实温度、输入能力/高压、FCC/FV上限、并联状态、watchdog
或故障路径任一缺口未闭合，后续模拟决策必须给出禁止使能原因。先以主路径、
常规有线管理为目标，不重载profile、不改学习数据、不提前实现快充。

本清单源码入口相对工作区`ref/android_kernel_lge_sm8150`；资料索引为工作区
`docs/INDEX.md`及六章。0150预检参数和本地验证见
[BATTERY-SPMI-TRANSPORT.md](BATTERY-SPMI-TRANSPORT.md)。
