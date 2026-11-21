# MH2LM 当前状态

更新：2026-11-21。具体证据见版本记录，操作见[构建指南](BUILD-GUIDE.md)，交接见[NOTES](../NOTES.md)。

## 当前交付

**最新触摸 IRQ 实机基线为 0121b30 / `b76005f`**，GPIO122 叶子掩码通过桌面、触摸和两轮普通 OFF/ON。[第5批实机证据](GPIO-IRQ-STAGE5-LEAF-MASK.md)

**GPIO 第6批 0121b31 本地验证通过**，包括 stray 的持久隔离、显式线程恢复、b30 负对照和 ARM64 单文件编译。b31 实机遇到关中断写 console panic；b31r1 将 stray WARN 延迟到线程，本地回归通过，待新 CircleCI 产物。下文 a39 为显示专项基线。[第6批合同与验证](GPIO-IRQ-STAGE6-STRAY-CONTRACT.md)

## 当前结论

LG G8X（mh2lm，SM8150）已经运行 iPhone X／D22AP 的 iOS 16.7.16 用户态，可进入桌面、操作应用、触摸、使用按键和调节亮度。使用16 KiB页XNU、UFS上的HFS+根卷；当前不是早期M3内核入口阻塞阶段。

**显示专项实机基线为0121a39／0229，mode7／PV。C08第3、4项普通显示路径已验收。** 主线仍是完整显示驱动：下一步C08第5项多使用者汇总后的物理资源转换，再结合第6项处理命令与扫描协调；共享core、SMMU、RSC、QoS状态丢失后重建单列PD4。之后PD1剩余ABL退出／PD5 → F5直扫 → F7收尾。

## 各部分状态

| 部分 | 已有能力与证据 | 尚未完成 |
|---|---|---|
| 启动与用户态 | LG ABL → Mu-Silicium → UniXNU／DarwinBoot → XNU → UFS／HFS+ → SpringBoard；8核、16K页、3 GiB配置窗口 | 完整手机功能；当前测试不是重新安装根卷 |
| 输入与USB | 按键、TouchComm触摸、DWC3 CDC-ACM日志使用IRQ；[基线与边界](IRQ-FRAMEBUFFER-PLAN.md) | 通用I²C／USB／中断控制器架构、完整设备终止与重匹配 |
| 电池 | runtime3已实际完成充电至100%、CC→CV→硬件截止，见[电池计划](BATTERY-IMPLEMENTATION-PLAN.md) | 0169温度路由归属/挂接已验、实际触发未观察到；回充与关机尾日志按用户决定暂缓，温区/上报及完整睡眠仍未全覆盖 |
| 显示缓冲与刷新 | 当前mode7扫描驱动持有的A缓冲，固定VIG0→LM0→PP0→INTF1管线、硬件RD_PTR约60Hz；CPU仍约30Hz原地拷贝 | F5零拷贝／surface直扫、逐帧提交和复用同步；60Hz扫描不等于60Hz UI |
| 面板普通OFF/ON | 供电票、reset、host／PHY／PLL、HS／ULPS／ESC、厂商表、SDR色彩及亮度恢复；[a39两轮证据](PANEL-D1-0121A39.md) | 全部共享状态掉电后的独立重建、无ABL首帧／console／panic和完整IOPM收口 |
| C08资源引用 | 每包命令引用、ESC停启、面板DSI外层引用及显示MDP引用的普通路径已验；[六项清单](PANEL-LINEAGE-DISPLAY-CLIENT-CLOCKS.md) | 第5项最后使用者的完整物理转换、第6项提交协调差异；BOOT／managed／core／config底座仍保留 |
| GPU与其它外设 | CPU软件渲染，输入及日志可用 | GPU、音频、Wi-Fi／蓝牙／蜂窝、相机等没有本项目已验收驱动 |
| 日志和电源管理 | USB实时日志及UFS持久环、PSCI关机／重启 | BUG-006日志发布竞态、日志结构整理、整机CPU suspend与低功耗 |

## a39验收与可复查证据

- 实现 `b9b3305`，镜像交付 `67da2bc`，实机记录提交 `7dedbc5`。
- 两轮完整OFF/ON、15个DSI／MDP引用状态事件；每轮24组命令取得→触发→DONE→归还。两轮分别1个、3个完整60秒IRQ窗口，均dropped0；用户反馈“感觉无异常”。
- USB从启动中段记录到SYSTEM_OFF，0NUL；没有本轮持久日志对照。普通功能通过不代表故障注入、长期耐久或全量硬件掉电验收。
- 工作区证据：`scratch/artifact-0121a39/wake-log-review.json`、`wake-final-review.json`；本地实现验证：`scratch/framebuffer-0229/final-review.json`。
- 测试镜像：`out-bs/boot-16k-0121a39-abi608-panel-pipeline.img`，SHA256 `14e9398fc3fbf7744f9fa20eae04f40c785fe838de0675638f37307b656f903f`。a38等旧镜像保留；a39是临时启动测试基线，不表示已刷入boot分区。

## 已知问题和范围

[BUGS](BUGS.md)为问题台账。BUG-006日志竞态、BUG-008首次亮度状态、BUG-009音量指示继续保留；BUG-007旧画面闪现按用户要求暂缓。其他既有Bug也未因本轮显示通过自动关闭。

首次仍有ABL像素／共享配置依赖；正常显示已使用自有A缓冲。这两个结论需要分开。当前mode7在自动刷新期间持有MDP资源，命令结束或DSI post-enable归还不等于全部时钟关闭。

## 构建和下一次测试

内核由CircleCI构建，配置位于 `.github/workflows/circleci.yml`；用户push并下载kernel／buildtree，agent核对源码、标记和SHA后沿ABI608固件打包，再回读镜像。GitHub Actions、旧sweet／guacamole shim及FV内512KiB载荷均非当前MH2LM操作路线。

显示专项 a39 验收已完成。当前待下载 GPIO 第6批修复版 b31r1 的新产物。所有 sudo、push 和 fastboot 操作按[构建指南](BUILD-GUIDE.md)分工执行。

## 文档使用规则

[文档索引](README.md)按用途组织资料。当前状态更新此页与NOTES有效交接段；阶段定义放计划，版本日志只记录当时结果。折叠的历史记录、旧日期和其它设备说明用于追溯，其中的“当前／下一步／待实机”不作为新的操作要求。
