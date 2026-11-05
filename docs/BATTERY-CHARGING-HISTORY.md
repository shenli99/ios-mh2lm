# 过去为何能充电：原始日志、镜像与固件复核

2026-11-06。用户要求先解释过去成功充电的可能原因，本次只读分析，不改驱动、
启动镜像、充电寄存器，也不要求新编译或回刷旧内核。

**最有依据的解释：成功轮次进入XNU时充电使能已经开启，PMIC随后沿用配置
工作；XNU负责观察和上报。** 开启者可能处于ABL/UEFI或更早的启动环境，
目前没有足够记录指定到某个函数。这个结论与“驱动实现了完整充电”不同。

## 1. m19从第一笔就使能，不是插线后由XNU打开

当前 `logs/sweetlog-0112m19.bin` SHA256：
`8071ed68c6b3be36f5efc72f4f3dabec02275d479d7ef8e10d53a1404915da0e`。
头v1/ring4194304/total438739/flush47；未环绕，无解码NUL。
**旧文档所说“wrap顺序不能确定转换”不适用于这个文件**，它保留启动开头和
后续有序记录。以下行号对应工作区解码 `scratch/charge-history-0119/0112m19.txt`。

- 112行首次采样为external0/state7，114行独立DIAG已是en0x1，config/current均
  有效。此时没有外接电源，电池尚未开始充电，但允许充电的命令位已经是1。
- 248笔DIAG的en始终0x1，未记录0→1转换。247笔config-ok1，一笔3865行在
  后续配置读取失败、config-ok0；所有current-ok1。这笔en已经读成功，错误
  发生在后面的CFG2链中，不能把cfg2打印零当有效硬件零。
- 与相邻DIAG一一配对后，有序运行如下：

  | 采样行区间 | 数量 | external / state / enable | 解释 |
  | --- | --- | --- | --- |
  | 112～3722 | 121 | 0 / 7 / 1 | 未插线，已允许充电 |
  | 3730～4316 | 65 | 1 / 3 / 1 | PMIC进入FULLON，真实充电 |
  | 4325～4430 | 16 | 1 / 7 / 1 | 输入仍报告有效，但不再充电 |
  | 4439～5133 | 46 | 0 / 7 / 1 | 拔线后放电，命令位仍保持1 |

- 充电首笔电流约-27.8mA，下一笔3740行升至-2.328612A；后续总体约-2.2～
  -2.3A，MSOC63→66%，稳态电压4198→4304mV，足以确认真实充电。
- 成功充电时DIAG的SMB_EN0x1648也为0，因此它不是“并联硬件关闭”的充分
  证据；policy的SMB read-ok0更不能用来解释选择配置。详见0149诊断记录。

这还说明：**enable1不是足够条件，state7也不总意味着enable0。** m19中间
16笔external1/state7/en1的非充电样本仍为path0x95、未suspend、ICL配置1500mA。
它们可能涉及电源切换/输入能力、其他硬件门控或执行环境；没有操作时间线、
真实输入电流/ICL模式/故障全量证据，不能把原因指定为其中一项。

## 2. 使能为零早于电池IRQ/mux改造

`sweetlog-0113-battery-ui.bin` 首笔113/115行external0/state7/en0；实际参数
sweetusb0，仍是两秒观察模式，尚无后来的共享mux/电池IRQ。
124笔均未充电、DIAG en0。这说明此类禁充初态在IRQ阶段之前就存在，不能把
“IRQ后才不充”作为既定时间线。

当前0119主档及no-usb的首笔也已en0；no-usb十一笔均state7/en0。IRQ和USB
controller在首次禁充采样之后启动，因此它们不能解释这次首次采样的禁充。
这不排除之后的硬件影响，但没有证据把它们定为旧成功消失的根因。

可用的 `xnu-buildtree-0112k5.gz` 中选出的原生文件实际已包含0125短日志格式。
对该文件严格应用0126后，SweetBattery.cpp与原生0113逐字节相同；SPMI与USB
两个文件也相同。变化是UI/user-client日志，无充电控制写入。不能仅凭压缩包
文件名把它当成未应用0125的精确旧版本；此次按实际源码内容比较。
m19的bootargs没有sweetusb覆盖，旧源码默认sweetusb1仅探测，未运行ACM设备
栈；不是今天默认参数的等价运行，亦不能称当时USB硬件完全没有被访问。

## 3. 成功镜像与当前镜像用的是同一份固件

对以下三个最终Android boot镜像直接按header抽取kernel字段（BootShim、
压缩Mu固件及附加DTB），均2111336字节且逐字节相同：

- `out-bs/boot-16k-0112k6-battery-policy-log.img`
- `out-bs/boot-16k-0119-battery-charge-diag.img`
- `out-bs/boot-16k-0119-battery-charge-diag-no-usb.img`

该字段SHA256统一为
`80b40a0fd2dbcd79d87f4a7e45d9b93e8ef6ba9d661599f4ee6e4e09df68b1bd`。
所以“这三张镜像换了充电固件版本”没有依据；相同固件仍可能根据线缆、温度、
启动模式或保留状态走不同分支。原厂ABL及进入fastboot前的系统状态不在这份
镜像字段里，不能被此比较排除。

解开实际gzip成员及其LZMA FV，找到与以下本地二进制/配置完整匹配的内容：
`ref/Mu-Silicium/Binaries/mh2/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.efi`、
`.../ChargerExDxe/ChargerExDxe.efi`、`.../RawFiles/QcomChargerCfg.cfg`。
对应DXE/APRIORI也声明这些模块。由此确认它们确实被打入本次沿用的固件。

二进制含 `ChargerLib:: ... Enabling charging`、`Disabling charging`、
`ChargerLib_Exit`及watchdog-bite后重新使能等字符串，表明其包含相关控制分支，
**不是证明m19实机执行过哪一分支**。目前主要是闭源PE镜像，缺少当次UEFI
日志/调用记录；不能把“镜像中存在”升级成“已经证实它开启”。配置中的
`EnableChargerWdog=1`注释是启用看门狗、退出前关闭看门狗，不等同于退出前
关闭充电。`SuspendDCIn`是DC输入策略，也不能直接解释USBIN使能。

## 4. 候选原因的优先级与证据边界

| 候选解释 | 当前支持程度与尚缺信息 |
| --- | --- |
| 启动固件在旧轮次留下enable1，PMIC沿用 | 最有依据：m19首次读取已为1、XNU没有使能控制；固件实际包含充电模块。具体writer/分支未知 |
| 前一个系统/恢复环境或冷暖启动导致PMIC保留状态不同 | 可能；缺少每次启动前的环境/复位方式/使能快照，不证明PMIC一定跨该次复位保留 |
| 电脑、充电器或线缆/检测状态不同 | 可能影响是否实际充电；m19成功APSD0x1，近期有0x21；缺少来源确认，不能仅凭APSD反推电源身份 |
| 温度、并联路径、watchdog或其他硬件/执行环境门控 | 未排除；尚缺真实温度、ICL模式/settled、FCC/FV与完整故障状态；m19 en1仍停充提示enable并非全部 |
| 新IRQ/mux破坏充电、当前USB gadget是唯一原因 | 缺乏支持：0113 sweetusb0已有en0；0119首笔禁充早于两者启动、no-usb也未恢复 |
| 我们曾实现使能写入，后来删掉 | 此次检查没有发现支持证据；旧SweetBattery/SPMI是只读观察路径 |

现有证据足以解释“没有XNU充电控制，为何某轮还能充”：PMIC已经处于允许
状态，可自行完成充电阶段；无需软件每两秒重新开启。它不保证每次重启都成立，
也不等于已经补齐温控/限流。后续控制实现不能只把0x1042置1当作完整方案。

## 5. m20文件身份不符：不作为本次成功复核证据

历史NOTES记录m20为83笔充电、SHA256
`ef270d05c9d4f770299d679bd8c4e332edcacf21dba271d015dd2b4e3342414a`。
当前目录 `sweetlog-0112m20.bin` 实际SHA256却为
`6fffa51f4d791c9eb233fa82ad96a33024d4ace23ca641cb99a1ec0c47ea0c47`，
与 `sweetlog-0113-battery-ui.bin` **逐字节相同**，实际参数含sweetbattuidiag1，
124笔均en0/未充电。无法从当前文件验证历史83笔结论，亦不能据此反证旧结论。
不改名、不覆盖原始文件；历史结论保留为当时记录，旧m19单独足以证明真实充电。

本地解码、配对时间线、镜像元数据、原生源码比较与固件模块匹配结果保存在
工作区 `scratch/charge-history-0119/`，原始日志和固件不进git。当前没有新增
实机测试需求，亦不需要为本次历史分析编译内核。
