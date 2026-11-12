# BCM width来源：设备树、Lineage与0121a17实测

2026-11-12。问题：ACV `width=0`究竟来自哪里，能否用设备树里的4替换？

**结论：ACV的BCM width来自固件加载到共享内存的command DB，Lineage直接读取；
设备树的qcom,buswidth属于另一个字段。不能把设备树的4填进ACV的width。**

## 版本与资料范围

- `ref/lge-lineage-kernel` HEAD `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`（lineage-20合并记录）。
- `ref/android_kernel_lge_sm8150` HEAD `d4f335e13d73c4aa2af85581a6ec11b670ee5b94`。
- 两仓的cmd-db.c、msm_bus_fabric_rpmh.c、msm_bus_arb_rpmh.c、msm_bus_of_rpmh.c及
  sm8150-bus.dtsi五文件逐字一致，因此不是两份参考代码对width定义不一致。
- 已回看`/home/shenli/work1/docs/INDEX.md`、`docs/panel.md`及本仓PANEL-NOC-D1.md。
  INDEX为电源资料索引，panel.md的physical width/pixel width也不是BCM width。

以下源码行号以`/home/shenli/work1/ref/lge-lineage-kernel/`为根。

## 来源链条

1. `arch/arm64/boot/dts/qcom/sm8150-bus.dtsi:296`声明逻辑节点ACV_DISPLAY，
   `qcom,bcm-name="ACV"`，归属display RSC；此BCM节点没有指定width数值。
2. `drivers/soc/qcom/msm_bus/msm_bus_of_rpmh.c:115`读取该资源名用于查询。
3. `arch/arm64/boot/dts/qcom/sm8150.dtsi:3014`声明command DB locator
   `reg=<0xc3f000c 8>`，不是直接把整个数据库地址写死在这里。
4. `drivers/soc/qcom/cmd-db.c:348`映射locator，读取真正共享DB起始地址/大小，再映射DB。
   `Documentation/devicetree/bindings/arm/msm/cmd-db.txt`明确说明DB位于共享内存，
   由remote processor加载；这些公开驱动源码没有生成本机width0的固件数据表。
5. `cmd-db.c:96`按八字节resource key查条目，`:175`从该条目的aux位置复制数据。
6. `msm_bus_fabric_rpmh.c:52`定义aux布局；`:1202`读取它，`:1206`直接执行
   `bcmdev->width=(uint32_t)aux_data.width`。此初始化不将width0视为失败，也不从DTS补值。

```c
struct bcm_db {
    uint32_t unit_size;  // aux +0，4字节
    uint16_t width;      // aux +4，2字节
    uint8_t clk_domain;  // aux +6，1字节，即VCD
    uint8_t reserved;    // aux +7，1字节
};
```

当前XNU从aux前后两个word取unit和packed，低16位为width，中间8位为VCD，
高8位为reserved，与这份结构一致。0121a17读取unit1/packed0xb0000，解码为
width0/VCD11/reserved0；对应重建字节`01 00 00 00 00 00 0b 00`（由日志值重建，非独立dump）。
没有证据表明是把VCD错读成width，或读aux失败后返回全零的默认结构。

## 两种宽度不能混用

| 字段 | 来源/存储位置 | 用途 | 本次实例 |
|---|---|---|---|
| 节点buswidth | DTS qcom,buswidth → node_info.agg_params.buswidth | 节点带宽缩放的分母 | slv_ebi_display为4 |
| BCM width | 固件commandDB aux → bcmdev.width | BCM资源缩放系数的分子 | MC0为16，ACV为0 |
| agg-ports | DTS qcom,agg-ports → num_aggports | 平均带宽聚合端口数 | slv_ebi_display为4 |

`sm8150-bus.dtsi:2150`的EBI节点同时引用MC0_DISPLAY和ACV_DISPLAY，
但qcom,buswidth4不意味着这两个BCM的width都为4。
解析qcom,buswidth的函数在`msm_bus_of_rpmh.c:307`；
实际缩放在`msm_bus_arb_rpmh.c:540`及`:545`，明确分别使用两个字段。
BCM width是资源元数据，不能直接把ACV的0理解成物理DDR/面板总线没有宽度。

## width0对当前故障与后续修复的意义

0121a17 USB日志867行ACV项已通过地址、aux长度、对齐/范围检查，
当前XNU因统一的`!(packed&0xffffu)`规则拒绝继续，确实比Lineage初始化规则更严格。
修复须尊重实际元数据，不能用DTS4、MC0的16或测试fixture的4覆盖ACV的0。
这不表示其它BCM的width0都该放行，也不证明只删lookup检查就能完成扫描接管：
`SweetBCMInfo::scale()`同样拒绝width0，而ACV构票还需要表达正确的资源语义。

Lineage的ACV apps7038与display27006查询同一个DB key；只有apps节点进入bitmask8
特殊处理（`msm_bus_arb_rpmh.c:490`、`:571`）。display走普通vec分支，**不能因此就
假设它必须按width0算成零票**：max_ab/max_ib在BCM循环外初始化，MC0→ACV顺序
由DTS及phandle解析保留，前一项归一化累计值可传到后一项；详见0121a17诊断。
`tcs_cmd_gen()`（`msm_bus_fabric_rpmh.c:251`）再把vec打包、裁到14bit并交给按VCD
生成的命令列表；后端不会重新从DTS取一个width补上。

**已确认：width的来源、布局、实际值和软件拒绝条件。待核实：ACV最终应使用的投票策略及
跨资源累计行为的硬件意图。** 公开内核不包含remote固件生成该条目的源码，不能声称已经
解释了固件为什么选择0。后续以正确模型修复，不为本问题再要求额外实机取证。

本轮为静态源码/资料核对，无驱动、CI、固件改动，无push或云编译需求。
对照证据：`scratch/artifact-0121a17/bcm-width-source-review.json`。
