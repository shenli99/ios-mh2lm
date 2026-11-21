# MH2LM 文档入口

更新：2026-11-21。目标设备是LG G8X（mh2lm／SM8150）；sweet和guacamole是继承来源，不能直接套用其机型参数或刷写命令。

| 要查什么 | 入口 |
|---|---|
| 现在做到哪里、下一步是什么 | [当前状态](STATUS.md) → [有效交接](../NOTES.md) |
| a39镜像、日志和实机结论 | [0121a39验收记录](PANEL-D1-0121A39.md) |
| 构建、产物核对、打包和取日志 | [BUILD-GUIDE](BUILD-GUIDE.md) |
| 当前驱动职责与依赖 | [驱动架构](CURRENT-DRIVER-ARCHITECTURE.md) |
| 四大目标和执行顺序 | [IRQ／显示／日志／电源路线](IRQ-FRAMEBUFFER-PLAN.md) |
| GPIO 中断当前批次 | [分批计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md) → [第6批 stray 合同与验证](GPIO-IRQ-STAGE6-STRAY-CONTRACT.md) |
| 触摸终止后的首次显示降级 | [b32合同、本地证据与交付](DISPLAY-TOUCH-FAILURE-FALLBACK.md) |
| 显示F0–F7阶段 | [显示重构计划](FRAMEBUFFER-PANEL-PLAN.md) |
| 完整面板驱动PD0–PD5 | [独立初始化计划](PANEL-FULL-DRIVER-PLAN.md) |
| C08六项状态、DSI／MDP引用 | [外层引用与范围](PANEL-LINEAGE-DISPLAY-CLIENT-CLOCKS.md) |
| 与固定Lineage版本的差异 | [全流程审计矩阵](PANEL-LINEAGE-FULL-FLOW-AUDIT.md) |
| 已知问题 | [BUGS](BUGS.md) |
| 电池已验收能力和剩余覆盖 | [电池实现计划](BATTERY-IMPLEMENTATION-PLAN.md) → [runtime3充电](BATTERY-BASIC-CHARGE.md) |

## 按模块阅读

- 显示：从[全流程审计](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)进入资源、host、PHY、管线、面板表及完成契约。各 `PANEL-D1-0121A*.md` 是该版交付／实机记录，不代表最新版。
- 输入／IRQ：[按键](KEY-IRQ-HARDWARE-BASELINE.md)、[触摸](TOUCH-IRQ-HARDWARE-BASELINE.md)、[USB](USB-IRQ-HARDWARE-BASELINE.md)、[共享mux](MUX-IOSERVICE.md)与[consumer](MUX-CONSUMERS.md)。历史测试矩阵按各页日期解释。
- 电源：[RPMh完成IRQ](PANEL-RPMH-COMPLETION-IRQ.md)、[SPMI事务](SPMI-TRANSACTIONS.md)、[基础充电](BATTERY-BASIC-CHARGE.md)。显示RPMh不穿透电池SPMI白名单。
- 构建：[ABI608](BOOTARGS-ABI-0121A14.md)解释loader布局；[构建指南](BUILD-GUIDE.md)提供当前命令；[XNU说明](../xnu/README.md)说明补丁和产物。

## 如何维护

1. 新进度先更新[STATUS](STATUS.md)与NOTES有效交接，记录实现／交付／实机证据各自的提交和路径。
2. 计划中的完成状态随验收更新；不以增加“当前进展”段落代替修正过期的正文。
3. 原始失败、旧版本边界与参考来源保留为历史，不将旧失败改写成成功，也不因普通唤醒通过关闭未验证项目。
4. 新增入口同步本索引；移动章节时保留旧锚点或迁移链接。设备dump、序列号、内核／固件／Apple产物与原始日志不入git。
