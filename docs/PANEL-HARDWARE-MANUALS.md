# 面板型号与当前时钟故障的手册线索

2026-11-13。按用户要求复核工作区`docs/panel.md`和Lineage源码。
Lineage基线为`2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。
面板公共DTS、PV命令表、DDIC匹配/实现及三个时钟文件共7份，与本地LG源码逐字节一致。

## 型号证据

| 对象 | 已核对信息 |
|---|---|
| 本机ABL选择 | `source=2 profile=4`，对应`dsi_r66456_tianmamd_pv_1080_2340_cmd_display` |
| 模组供应商/配置 | `TIANMAMD`，Tianma MD，PV配置；1080×2340、60Hz、RGB888、4-lane MIPI DSI command mode |
| 面板DDIC | `lge,ddic-name = "r66456a"`；R66456A，驱动家族R66456 |
| 软件panel-name | `R66456 MD cmd mode dsi panel`；它不是完整模组订货料号 |
| 完整模组料号 | 尚未从所查资料取得；不能拿R66456A代替模组料号 |
| 当前故障所在芯片 | Qualcomm SM8150（Snapdragon 855）内部DISPCC/RSCC时钟与复位逻辑 |

ABL选择依据是既有实测`identity-slot-control-persistent.txt:85`，不是仅凭DTS include顺序猜PV，
也不是物理DDIC ID读回。项目旧文档将R66456A标为Renesas，本次源码能直接确认的是型号字符串，
没有独立的DDIC厂商品牌字段；找手册优先按R66456A/R66456检索。

Lineage依据（相对`ref/lge-lineage-kernel/`）：

- `arch/arm64/boot/dts/lge/sm8150_common/sm8150_common-panel.dtsi:197`连接PV display与panel；`:533`块内`:541`为TIANMAMD，`:542`为r66456a。
- `arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-pv-tianmamd-fhd-cmd.dtsi:15`为软件名称；`:377`起为60Hz/1080/2340。
- `drivers/gpu/drm/msm/lge/lge_ddic_ops.c:27`同时将r66456/r66456a映射到r66456_ops，A版本注释为D-CUT(DV2)。
- `drivers/gpu/drm/msm/lge/lge_ddic_ops_r66456_tianma_md.c`为DDIC特定实现。

## 当前失败的准确含义

a25向SM8150的`0x0af04004` bit0发出置位后，立即读回及39次等待均未见enable。
实际store/屏障与物理翻译已核对；仍不能仅凭读零断言硬件拒写、写保护或物理时钟停止。
该地址属于SoC的`disp_cc_mdss_non_gdsc_ahb_clk`，不是R66456A的DSI命令寄存器。

Lineage `drivers/clk/qcom/dispcc-sm8150.c:1408`定义enable/halt偏移+0x4004，enable为bit0；
`clk-regmap.c:54`经regmap置位；`clk-branch.c:60`的开启等待主要验CLK_OFF(bit31)清除。
当前XNU还要求enable读回为1，因此CBCR0满足Lineage的halt判据，却会被我们的自身票检查拒绝。
这项差异不能掩盖，也不能仅据此判断删检查就能安全完成接管。

## 查找优先级

1. **当前故障最直接：SM8150 / msmnile 的Display Clock Controller、RSCC与reset寄存器手册。**
   检索`DISP_CC_MDSS_NON_GDSC_AHB_CBCR`、`DISP_CC_MDSS_RSCC_BCR`、`CLK_ENABLE`、`CLK_OFF`、
   software vote、write access；重点地址`0x0af04004`与`0x0af04000`。
   希望确认bit0读写语义、何时忽略写入、复位/访问前置条件及与RSCC的关系。
2. **完整面板驱动：R66456A / R66456 datasheet或programming guide，TIANMAMD 1080×2340模组规格。**
   重点为供电/复位时序、厂商命令页、Sleep/Display On顺序、TE和DSI读回。
   这些有助于后续面板初始化，不直接解释SoC时钟寄存器读零。

本轮仅核对资料和更新文档，没有新增驱动修改/编译要求；0213代码`7f4f55a`仍按原交接等待云编译。
底稿：`scratch/artifact-0121a25/hardware-manual-source-audit.json`。
