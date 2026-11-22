# C0-05 结果：时钟控制器适用范围

[返回 C0](00-baseline.md)。日期：2026-11-22。等级：**调查完成**（只读，无设备操作）。

## 来源

- XNU ADT 不是厂商树：`tools/mk-mh2lm-mdss-adt.py` 只派生 `mdss`、`display-rpmh` 等节点，GCC 页 `0x10b000`、DISPCC `0xaf00000`、apps RSC `0x18220000` 等窗口以常量写入（脚本 `WINDOWS`／`RPMH_WINDOWS`／`NOC_WINDOWS`）。ADT 中**没有独立的时钟控制器节点**，因此控制器存在性以 b32 镜像附带的 LG DTB 为准。
- LG DTB：b32 镜像 kernel 段 gzip 尾部，SHA `de479a45…befb`；反编译工作区 `scratch/clock-c0/dtb/appended.dts`。消费者统计按 `clocks` 属性中的控制器 phandle 计数。

## 结论

| 控制器 | DTB 节点／窗口 | 状态 | 主要消费者（DTB） | 结论 |
|---|---|---|---|---|
| GCC | `qcom,gcc-sm8150-v2` 0x100000/0x1f0000 | ok | 92 处引用（UFS、USB、QUP、PCIe…） | 首批（C2） |
| RPMhCC | apps RSC `mailbox@18220000` | ok | CXO 等 | 首批（C4） |
| DISPCC | `qcom,dispcc-sm8150-v2` 0xaf00000/0x20000 | ok | MDSS／DSI | 首批（C6，受入口条件约束） |
| debugcc | `qcom,debugcc-sm8150`，复用 GCC 等 regmap | ok | 无（测量用） | 首批（C2-08） |
| VIDEOCC | 0xab00000/0x10000 | ok | vidc（ok）、venus | 按需（C7），当前无视频驱动计划 |
| CAMCC | 0xad00000/0x10000 | ok | csiphy／cci／cpas 等 24 处 | 按需（C7），当前无相机驱动计划 |
| GPUCC | 0x2c90000/0x9000 | ok | kgsl-3d0、gmu、kgsl-smmu | 按需（C7），对应 PLAN P4-6 GPU，未开始 |
| NPUCC | 0x9910000/0x10000 | ok | msm_npu（ok）、tpdm；另两个 QUP 节点 disabled | 按需（C9），当前无 NPU 计划；**不是不适用** |
| SCC | 0x2b10000/0x30000 | **disabled** | 仅 SSC QUP3 的 i2c／spi，全部 disabled | **不适用（本设备）**：厂商 DTB 自身禁用且所有消费者均 disabled；若日后发现 sensor 子系统需要，重新评估 |
| CPUCC/OSM | `qcom,clk-cpu-osm` | ok | CPU 调频 | 不在本计划（CPU DVFS 另属电源计划） |

## 对计划的影响

- C7 三个控制器及 NPUCC 当前在 C0-04 中均无近期消费者，应标“暂缓（无需求）”（由 C0-04 正式落定）。
- XNU 侧新增 GCC／RPMhCC／DISPCC 后端时，需要在 ADT 派生脚本中新增对应节点或窗口；这属于 C1-03 所说的 ADT ABI 新增，应单独记录，不改现有 `mdss`／`display-rpmh` 节点含义。
