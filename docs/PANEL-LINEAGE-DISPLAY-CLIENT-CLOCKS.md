# 0229：面板 DSI 与显示 MDP 外层时钟引用

2026-11-15，实现提交 **`b9b3305`**，前置 `9e7df2f`，用户授权完成 C08 第3、4项。**本地实现与验证完成；a39两轮普通mode7/PV OFF/ON及外层引用已实机通过，成为最新基线。** 本批给面板流程和扫描引擎各增加一个真实时钟使用者，明确两者何时接手和归还。完整 C08／PD4 尚未完成；[实机证据](PANEL-D1-0121A39.md)。

## C08 固定子项与本次边界

沿用用户在 `codex-session-24.md` 中确认的六项拆分，避免把相关 PD4 缺口混成一个未完成项：

| 子项 | 当前状态 | 剩余工作 |
|---|---|---|
| 1. 每条命令的时钟引用 | 0228，a38普通成功路径实测通过 | 不确定完成仍保留引用；故障注入仅本地证据 |
| 2. 低速 ESC 时钟停启 | 0227，a37普通OFF/ON实测通过 | 不是共享core关闭或节电验收 |
| 3. 面板流程的外层 DSI 引用 | **0229本地完成，a39普通mode7/PV两轮实机通过** | 本批普通路径收口；故障注入仍仅本地覆盖 |
| 4. 显示引擎的 MDP 引用 | **0229本地完成，a39普通mode7/PV两轮实机通过**；自动刷新/临时调光持有，真正OFF排空才归还 | 本批普通路径收口；未增加非自动刷新的idle策略或完整stop/free释放 |
| 5. 多使用者汇总与物理开关 | 新角色已接入同一账本，额外使用者和故障保护本地通过 | 保留BOOT/managed/core/config底座，最后使用者退出后的完整物理转换未完成 |
| 6. 命令与扫描协调 | 现有XNU排空/暂停/恢复策略可用，新外层引用回归通过 | 结合后续提交路径核对Lineage差异，不改作每帧关钟 |

共享core、SMMU、RSC、QoS等**状态丢失后的独立重建属于关联PD4**，与C08使用者申请/归还边界分开记录。第3/4项普通路径已通过，下一步推进第5项等剩余范围。

## 两项具体完成了什么

| 时段 | 面板 DSI 引用 | MDP 引用 | 完成条件 |
|---|---|---|---|
| 首次接管准备 | managed 时钟完成后取得 | 扫描提交前取得 | 原资源、触摸、IRQ 和 DMA 门槛 |
| 正常开屏 | 在新帧完成后归还 | 持有 | 唤醒还须色彩、首帧、HS亮度及第二帧完成 |
| 自动刷新／同步调光 | 命令另取逐包引用 | 持有，调光临时停扫也不释放 | 不为每次自动刷新反复申请 |
| 关屏开始 | pre-disable 先重取 | 排空前继续持有 | 触摸暂停、IRQ与扫描真正排空 |
| 发送 OFF 表及停链路 | 持有 | 已归还 | 原 OFF 表、reset、HS→ULPS→ESC→PHY 顺序 |
| host／PHY 停止完成 | unprepare 归还 | 无 | 真实链路完成后才继续 DATA／供电停放 |
| 关屏后准备唤醒 | 先取得标为硬件暂停的引用 | 无 | 该引用不能授权命令；仍须原供电／NoC／链路恢复 |

第3项实现 `prepare/post-enable/pre-disable/unprepare` 外层 DSI 持票周期。第4项实现扫描前取得、正常显示持续持有、真正 OFF 排空后归还的 MDP 周期。取得／归还进入原 `fClockLeases`、`fClockReferences` 账本；槽位4→6，新增的是角色标签和token，没有第二套影子计数。

所有新引用都覆盖既有 DSI0 mask `0x7f`。已有 BOOT、managed、core/config底座继续保留，因此本批外层引用归还不会触发最后使用者的共享 core 关断；原普通 OFF/ON 仍执行已经实现的 HS/ESC/PHY 物理停启。暂停期间的 `hardwareSuspended` 是“持票但硬件未可用”，不等于时钟已运行。

普通release不能撤掉外层角色票，外层存在时不能撤 BOOT，MDP存在时不能撤managed。已标记的角色票、token、state/result/uncertain和分支总数必须一致；额外消费者仍阻止链路关闭。失败、取消、provider消失或stop保留尚存引用与独立硬件pin；晚到回调不能借故释放或重新提交。合法显示IRQ的mask/ACK仍允许用于停机，不能概括为“故障后所有MMIO零写”。

## 对齐依据与边界

固定 Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`：

- `drivers/gpu/drm/msm/dsi-staging/dsi_display.c` 的prepare、post_enable、pre_disable、unprepare；post_enable对command面板撤DSI ALL_CLKS，pre_disable先取回。
- `dsi_clk_manager.c` 的DSI/MDP独立客户端和聚合计数。
- `drivers/gpu/drm/msm/sde/sde_encoder.c` 的KICKOFF、FRAME_DONE、PRE_STOP、STOP；autorefresh开启时FRAME_DONE不安排常规延迟idle-off。

[新增参考测试](../tools/tests/test-mh2lm-display-client-clock-reference.py)提取19个原函数并编译，实际执行原post_enable/pre_disable及SDE资源调度的上述路径、重复kickoff/stop、错误STOP、EARLY_WAKEUP和pending-frame条件。SDE资源helper是接到真实clock-manager的测试桩；prepare/unprepare本身只核对源代码，未执行完整DRM、CCF、物理时钟或电源域。

XNU的post-enable归还放在既有新像素／亮度完成链之后、触摸恢复之前，比Linux的函数调用时机更保守。初始接管仍继承ABL已运行状态；本批不处理首次色彩／亮度接管、ABL像素退出或状态丢失后的共享core、SMMU、RSC、QoS重建，也没有新增闲置定时器或改变自动刷新策略。失败保留不确定资源是明确的XNU适配，不能照搬Linux错误出口继续释放。

## 验证与证据

生产补丁：[0229](../xnu/patches/0229-mh2lm-panel-dsi-mdp-clock-clients.patch)。五份生产文件为MDSS.cpp、Clock.inc、Lifecycle.inc、Resources.inc和Scanout.inc；公有Backend、DSI/Link/Pipeline实现、ABI608、ADT、启动参数、固件和面板表保持。

workspace证据入口：`scratch/framebuffer-0229/final-review.json`。2026-11-15接续 `codex-session-24.md` 时重新运行最终证据核对，确认21组通过日志、编译记录、源码/补丁/镜像SHA及回归版本与最终版本仅有注释差异；本次接手未重跑驱动回归或修改生产补丁。接手核对记录为同目录 `handoff-recheck.json`。

| 检查 | 结果 |
|---|---|
| 21组驱动回归 | 新外层引用、逐命令引用、命令生命周期、亮度、色彩、LP clock各mode2/7；两类完整面板、display-clocks、host、DSI sequence、旧mode6/mode4、FB两模式各120状态；全部通过 |
| [新外层引用测试](../tools/tests/test-mh2lm-display-client-clocks.py) | 两模式各四profile×三轮OFF/ON；真实wire/source/flush/link写时核对票据；自动刷新和调光不撤MDP；暂停DSI拒绝命令；延迟排空／新帧、额外租约、16组丢票、饱和／溢出、12组cancel/provider/stop阶段；ASan/UBSan通过 |
| 固定Lineage参考 | 原post/pre与SDE调度参考通过；0228真实host_transfer＋clock-manager参考也通过；提取函数数不是全部分支执行覆盖 |
| ARM64单文件编译 | 基线／最终成功；1613→1640告警，新增27条均为有界数组访问的 `unsafe buffer access`，无新增告警抑制；不是全内核云构建 |
| 补丁 | 单patch严格apply，0185–0229共45patch严格重放，80文件字节与SHA一致，无offset/fuzz |
| CircleCI | 仅新增5条源码断言，总154；YAML、4处合法参数模板、6段run shell、实际source markers通过；workflow/executor/参数/资源不变，未做服务器验证 |
| 打包 | 只增加4个kernel标记检查；真实旧a38被拒绝且无输出，原a38 pipeline SHA保持 |

首轮实现曾在ON供电仍停放时用Drain阶段取DSI票，正确被既有供电准入拒绝；修正为先进入原SupplyActivate阶段，再取票、再提交pin请求，未放宽供电检查。旧display-clocks测试写死4槽，更新为真实填满账本后验证饱和，保留初次失败记录。最后一次生产修改仅纠正“允许暂停的票据”注释，代码语义与回归版本相同。

证据还有 `verification.json`、`regressions.json`、`cumulative-replay.json`、`display-client-clock-reference.json`、`command-clock-reference.json`、`warning-review.json`、`ci-check.json`、`packaging-gate.json` 及测试日志；编译、重放、配置和最终汇总脚本在同目录。

## 交付下一步（历史交付过程，a39已完成验收）

当前无需再次下载或重测；以下保留初次提交、误下旧包和原验收步骤，最终结果见[a39实机记录](PANEL-D1-0121A39.md)。

**a39实机已通过（2026-11-15）：** USB确认15个连贯外层引用事件、generation1→2→3，每轮24组命令held/trigger/DONE/released，分别1个和3个完整60秒IRQ窗口，dropped0；用户感觉无异常。第3/4项本批普通mode7/PV收口，下一步第5项资源汇总后的物理转换及第6项协调，PD4重建分开处理。详见[a39实机记录](PANEL-D1-0121A39.md)，证据 `scratch/artifact-0121a39/wake-log-review.json`。无需为本轮重编或重测同版；以下为历史交付记录。

**2026-11-15重新下载已核对通过：[a39两档ABI608镜像已就绪](PANEL-D1-0121A39.md)，待mode7实机。** 80份源码匹配0229，154source/260kernel markers通过，两档回读确认内核/ADT/参数/固件正确。初次错误下载已解决，当前不再等待push或下载；下一步按交付文档采集日志、临时启动，首次接管/调光及两轮OFF/ON各60秒。证据 `scratch/artifact-0121a39/final-review.json`；a38保持最新实机基线。下方为原下载阶段记录。

**2026-11-15下载核对：收到的0121a39两包与a38逐字节相同，未包含0229，未生成新镜像。** 两包gzip/tar完整；80份源码中五份0229生产修改不匹配，新增5条源码断言和4个内核标记均缺失。证据 `scratch/artifact-0121a39/rejected-inputs.json`。需要从包含 `b9b3305`（例如 `dd3670f` 或其后继）的新CircleCI成功构建重新下载两包；文件名本身不能证明版本。尚无CI run ID，旧文件来源未确定；以下打包/实机步骤仍待正确产物。

按 [BUILD-GUIDE](BUILD-GUIDE.md) 第1节第7步及第2节，由用户push本地main：

```sh
git -C /home/shenli/work1/ref/ios-mh2lm push origin main
```

下载包含`b9b3305`/0229的新CircleCI `xnu-kernel` 和 `xnu-buildtree`（建议命名0121a39）。将两包保存为 `logs/xnu-kernel-0121a39.gz` 与 `logs/xnu-buildtree-0121a39.gz`，若可取得则一并保留构建日志和CI run ID。agent核对源码和内核标记后沿原ABI608打包mode7。当前无a39内核／镜像／CI run或实机结果，本轮未push、触发CI或操作设备。

新镜像验收：首次接管／桌面／触摸／按键／低中高亮度；两轮息屏约5秒再唤醒，每轮正常后保持亮屏至少60秒。日志应看到 prepare DSI→MDP held→post-enable DSI released，以及 pre-disable DSI→MDP released after drain→unprepare DSI；同时核对原命令held/trigger/DONE/released、HS/ULPS/ESC与新帧／亮度／触摸完成顺序。下次先确认USB采集显示Reading且文件增长，保留持久日志。

保留 `out-bs/boot-16k-0121a38-abi608-panel-pipeline.img`，SHA256 `c4941c89ac3863d6fa9a516dc809d58529f8687f1e4f3de4485b490dc04d775f`。BUG-006／008／009保持，BUG-007继续暂缓。新硬件验证通过后再推进剩余C08／PD4，然后PD1／PD5→F5→F7。
