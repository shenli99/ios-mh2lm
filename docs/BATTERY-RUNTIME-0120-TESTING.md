# 0120 电池遥测与模拟策略测试

## 0120a3 / 0154 硬件接口与 ADC5 预检测试档

0120a3 已核对为0154构建，29个选定源码文件与本地verify逐字节一致，
内核SHA256为`22460d3e4056179e3ace39fc367c25c438dd9829a53327cf93a5a57a7afe91d3`。
已生成三个同核档，telemetry镜像为
`/home/shenli/work1/out-bs/boot-16k-0120a3-battery-telemetry.img`，SHA256
`de0e8c5474c323df44abaaa2ad1fa260d1c31190f40a7012250169dab672ec78`；observe
为`259dc12f3ea402f9ba731d3941635ce5207c4c2f994aef446c0b2fdf9d0bb09b`，dry
为`cb91931bdeb4946c66ed08afcec4093d7f2b5712990a7d1ef0001e6dae82c088`。
三档均回读kernel/ADT/TOML/firmware和无重复参数；只有telemetry启用
`sweetbattadcdiag=1`。实机结果见下节；以下保留打包与测试说明。

本轮只测试telemetry：稳定约60秒、插拔两轮、每状态15～30秒，导出持久日志。
须保留启动早期内容，核对HARDWARE、ADC5 PREFLIGHT/ROUTE/FIELD以及原FG
遥测和共享IRQ。ADC5字段是只读诊断，不能据此判断实际VBUS/电池ID或真实充电；
没有控制档，也不要用dry档替代本测试。

### 0120a3 实机结论

持久日志SHA256为`f414a6331edb147c6ab48e4203ee9ac6db2f9056b1e7bae03a7e5725f74f60fe`，
未回卷，1252次快照覆盖约1279秒。ADC5 13/13字段有效，user observer/writer/
IRQ均为APID280；calibration observer/writer为APID282且无独立IRQ owner，符合
DTS仅给user `0x31` EOC中断的描述。该轮未发起转换，data仍是diagnostic-only。

1252次V/I、温度及容量状态均有效；容量实际重读21次，余1231次为缓存。
19次电池mux刷新全部healthy；总快照由21次初始/事件与1231次保护timer构成。
power/volume-down各有完整按键边沿，
USB/触摸IRQ未见错误；10条既有USB endpoint WARN仍需单独处理。充电状态仍
为enable=0、CHGR=0x47、无控制记录，不能视为已充电。

2026-11-06，承接实现提交bde5519。用户下载产物，本轮仅核对和打包，
没有改驱动/CI、push或操作设备。telemetry持久日志结果已补于文末。

## 产物核对

| 文件 | SHA256 |
| --- | --- |
| logs/xnu-kernel-0120.gz | 75e474a007881709a72d7fc30f812f47712d98dda2367d15560de3641cbecb0e |
| logs/xnu-buildtree-0120.gz | 29fd142453947131056b58bf99376545bd319715b88d2b7580c37a4efb4b06ab |
| kernel.development.mh2lm | 628c3d8e2fe0baa8ba47a1ccd2cd792d2870cefbcb2a814fca8e2fa0c62801ac |

内核manifest通过；Mach-O plain arm64、segment地址/文件偏移16K对齐。
选定24个原生源码文件与scratch/battery-runtime-0119/verify/xnu逐字节一致，
包含0149诊断有效性、0150受限transport、0151遥测/策略及mux/client事件掩码。
内核含runtime/sample/policy、battery-source/fault、preflight字符串，
sweet_spmi_fg_read、charger get_route/read/update和PM通知定义符号存在。
未提供0120构建日志，不据此推断CI的每一步输出；原生源码和最终内核已核对。

机器记录及选定源码：`/home/shenli/work1/scratch/artifact-0120/verification.json`
和该目录下`xnu/`。源码比对基线24文件清单也在JSON中。

## 同核镜像

通过以下命令生成，均位于`/home/shenli/work1/out-bs/`，每个35667968字节：

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-0120.gz 0120 --battery-runtime
```

| 镜像文件名 | 新运行层参数 | SHA256 |
| --- | --- | --- |
| boot-16k-0120-battery-telemetry.img | sweetbattruntime=1 | 3e5e102374cda4238d38b7c49463f949be931ad9856b0769bfa95af0ba1e7d3d |
| boot-16k-0120-battery-policy-dry.img | sweetbattruntime=2 | 5daac4bbbae84869b9d7483bc0436e0237f663df8d8a5ae0857b3af61472b5cd |
| boot-16k-0120-battery-runtime-observe.img | 省略runtime；sweetbattdiag=1 | fcf5a1c545c217d444a1c952c20e115d1715fa430273f6413584841f83eaedd2 |

三档保留三键/USB/触摸IRQ、真实SID2电池和sweetbattirq=4；USB ACM仍为
sweetusb=2 sweetusbstep=6 sweetvbus=1。cpus=8、16K ADT和已验证firmware相同。
脚本回读ramdisk后，再独立从最终Android boot提取ramdisk，回读其中FAT的
kernel/ADT/TOML并比较，确认参数无重复且每档runtime正确。

未生成控制档。遥测/模拟档会执行MEMIF协议控制及恢复，不写充电策略或FG
SRAM内容；三个真实证据provider仍缺，不能声称此次能主动开启实际充电。

## 测试顺序

1. 先由用户fastboot boot telemetry，无须先跑observe。使用现有USB日志读取
   工具保存`live-0120-battery-telemetry.txt`；同时导出
   `sweetlog-0120-battery-telemetry.bin`以保留ACM出现前的启动/MEMIF信息。
2. 启动后稳定约60秒，插拔两轮，每个状态停留15～30秒，记录电量/充电图标、
   按键/触摸是否异常。USB拔出时live会中断重连，持久日志用于补齐该段。
3. 先分析RUNTIME/SAMPLE的模式、温度错误/有效性、nominal/learned/profile、
   电压/电流及共享SPMI/IRQ错误。新source-change和fault路由已编译，但自然
   没有fault事件不算失败，也不通过制造过热等故障来触发它。
4. 遥测正常后再测policy-dry，保留同样操作和两类日志，后缀改为
   `battery-policy-dry`。检查缺失provider原因及allow=0，没有控制提交。
5. observe只在新运行层启动或遥测异常时作同核对照，避免无必要的重启。

温度是未经LG补偿的原始值；容量是FG字段，不能独自证明实际电池身份。
UI显示充电与策略允许、物理电流证据分开分析。本轮不验收真实充电恢复，
也不宣告六段计划第4～6段或电池IRQ整体完成。

## 首次遥测实机结果

用户仅导出`logs/sweetlog-0120-battery-telemetry.bin`，无需为了分析再补live。
SHA256 `7e06beb2344ddef2262d37dfcfe4742888e636392f410a0eb32203a1fe58fc86`。
v1、ring4194304、total472585、flush58，未回卷，解码5379行、无NUL。
解码和逐项分析保存在工作区`scratch/artifact-0120/`下
`battery-telemetry-persistent.txt`及`battery-telemetry-analysis.json`。

实际bootargs和RUNTIME均为mode1。617快照跨度609.888秒，其中23次初始/事件
路径、594次保护timer；30配置字段valid始终0x3fffffff，V/I均有效，
3873..3987mV，IOPM方向电流-684..+16mA。没有持续充电方向电流证据。
23次原电池发布均charging0、CHGR0x47/state7，SOC65/66%，不能以1%的变化
证明真实充电。15次策略记录均owned0/allow0，无CONTROL日志；这与mode1
不提交充电控制的设计一致。此次日志没有直接打印充电enable的raw值，不能
据历史日志代填当前0x1042。

**新的未通过项是FG遥测：温度617次均temp-error=-4、temp-ok=0，nominal/
learned/profile也始终无效。** -4在原生头文件中是SWEET_SPMI_NO_OWNER，
由sp_charger_route找不到目标PPID的EE0 owner返回，不是DMA超时，也不是
电池测出了0°C。FG路径先读0x0105/0103/4305，再进入MEMIF/DMA；由于失败
日志未含地址，尚不能区分身份读取阶段与MEMIF阶段，也不能断言硬件禁止
访问。容量/profile独立error未输出，不能把温度-4直接当作它们的错误码。
身份observer读取是否不必要地要求write owner，是下一步应核实的实现问题；
MEMIF实际控制写仍必须核对owner，不应直接放宽写权限。

策略原因0x7d1（8次）包含TELEMETRY/TEMPERATURE/BATTERY_ID/PARALLEL/INPUT/
WATCHDOG/LIMITS；0x7d9（7次）再含NO_INPUT。state4是缺温度后软件进入hold，
不证明实际过热；WATCHDOG位表示不满足当前“确认off”的策略，全部配置字段
有效时可确定0x1651的相应mask非零，但本日志没有raw字节，尚未建立pet机制。
因此即使修复温度读取，也不能宣称所有启充条件已经齐全。

21次IRQ刷新均healthy1，notifications/samples连续1..21；14次USBIN、7次
新增source-change，插拔通知工作正常。本次未自然观测到CHGR/MSOC/READY
hit；不撤销既有MSOC实机验证。未见驱动ERROR、panic或mux隔离；有4条既有
USB ep0/ep5 cmd0x6 status1警告，不能据此认定充电失败由USB造成。

**下一步先处理FG路由/读写权限分层及失败地址诊断，同时落实watchdog和三个
证据provider；暂不要求用户重启去测policy-dry或observe。** 不充电在本档
属预期边界，但不能把本次遥测当作验收通过。本轮没有修改驱动或CI。
