# 0120 电池遥测与模拟策略测试

2026-11-06，承接实现提交bde5519。用户下载产物，本轮仅核对和打包，
没有改驱动/CI、push或操作设备。实机结果待补。

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
