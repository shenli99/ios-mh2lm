# C0-04 结果：消费者时钟需求清单

[返回 C0](00-baseline.md)。日期：2026-11-22。等级：**调查完成**。来源：[PLAN P4](../../../../docs/plan/PLAN.md)、[STATUS](../STATUS.md)、[C0-02](C0-02-OWNERSHIP.md)、[C0-05](C0-05-SCOPE.md)。

## 近期消费者

| 优先级 | 消费者 | 类别 | 所需资源 | 所需操作 | 现 writer |
|---|---|---|---|---|---|
| 1 | 显示 MDSS／DSI（PD4 进行中） | 现有迁移 | DISPCC 10 个 branch、GDSC、PLL0/1、RCG；display RSC BCM；apps ARC／CN0；VRM 面板供电 | query／enable／disable／rate（已有）／restore（PD4 需要） | SweetMDSS、SweetRPMh |
| 2 | 时钟计划自身的验收手段 | 基础设施 | GCC debug mux + XO/4 计数器 | measure | 无 |
| 3 | UFS／USB（继承） | 保护对象 | GCC UFS／USB branch 与 GDSC、VRM ldoc3／ldoa5 | **只读 query + inherited/pinned 登记**；不接管 | ABL（XNU 无 writer） |
| 4 | 显示依赖的 GCC 输入 | 保护对象 | GCC DISP_AHB 0x10b00c、DISP_HF_AXI 0x10b038 | 只读 query + pinned | ABL |
| — | 触摸（GPIO 模拟 I²C） | 无时钟需求 | 不使用 QUP 时钟 | — | — |
| 暂缓 | 音频 P4-4、GPU P4-6、Wi-Fi P4-7、相机、视频、NPU | 新外设 | 未定 | — | — |

## 对各阶段卡的影响

| 卡 | 结论 |
|---|---|
| C1 全部 | 实施（统一入口是迁移的前提） |
| C2-01／02 | 实施（GCC 只读，为 UFS／USB／DISP 输入登记 inherited） |
| C2-08 debugcc | 实施（首个硬件写候选） |
| C2-03／04／05 | 实施，但样本只能是测试用的空闲 branch，见 C0-03 |
| C2-06 voted | **仍无需求，但能力已验**：没有消费者要 GCC 本地 vote；样本（S4，NPU AXI 支）已在 0138 走完“投一票→开方向完成→撤回”一个来回，聚合 OFF 未覆盖 |
| C2-07 reset | **仍无需求，但能力已验**：没有消费者要复位；安全样本（S5，NPU BCR id 3）已在 0139 打过一次厂商脉宽，落点／脉宽／回原值／四邻不动均可对账，“复位生效”无代理、不宣称 |
| C3 全部、C4-07～09 | 实施（apps RSC 迁移，服务于显示） |
| C4-01～06 RPMhCC | 暂缓到出现需要 RPMh clock 的消费者（当前 CXO 等由 ABL 持有，显示不直接请求 RPMh clock） |
| C5 | 仅在 C6-07／08／09 迁移已有 GDSC／PLL／RCG 操作所需的范围内实施；C5-10～12 corner 协调在 PD4 需要动态调频时再定 |
| C6 | 实施，受[总计划第 4 节入口条件](../CLOCK-SYSTEM-IMPLEMENTATION-PLAN.md#4-阶段入口与依赖)约束 |
| C7 全部 | **暂缓（无需求）** |
| C8-01～06 | **暂缓（无需求）**：没有整机低功耗入口 |
| C8-07～09 | 随 PD4 状态丢失恢复的需要再定 |

近期主线：C1 → C2-01／02 → C2-08 → C3 → C6（PD4 收口或接口冻结后）；C2-03～05 作为公共门控能力的样本验证，可与 C3 并行准备。
