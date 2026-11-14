# 0225：普通唤醒首帧后的亮度恢复与 HS 传输

2026-11-14。实现提交 `005e2ed`。D07/D08已在mode2/7实现并通过本地验证；**[0121a35](PANEL-D1-0121A35.md)的mode7两轮普通路径取得实机证据，用户确认滑条调光正常，唤醒闪现旧画面另记BUG-007暂缓修复。** 补丁：`xnu/patches/0225-mh2lm-lineage-post-frame-brightness.patch`。

## 0121a35 实机结果

[0121a35持久日志](PANEL-D1-0121A35.md)确认generation1→2→3，两轮均首帧→排空→1包typed HS亮度真实完成→同generation重新kickoff/新帧门槛→触摸/ON完成。第一轮约33.9秒，第二轮两个完整60秒IRQ窗口，dropped0；不能写两轮各60秒。两个恢复target均为1，滑条响应由用户观察确认，未逐档对齐或实机验证发送中target变化。USB未开是用户遗漏；本轮不补测。a35为带[BUG-007](BUGS.md)的普通唤醒/亮度最新基线，a34等旧基线保留；fresh frame不证明像素已是锁屏，视觉问题原因未知。D09及其余PD4仍待后续。下方CI启动修正和原实机安排为历史。

## CircleCI 启动修正（2026-11-14）

用户报告编译未启动。实现提交005e2ed在CircleCI YAML的源码检查字符串中加入 `1u<<26`，未转义的 `<<` 与平台模板语法冲突；该解析发生在shell/编译之前，shell单引号不能避免。现将marker改为仍存在于源文件的唯一前缀 `fDsiHS ? 0u :`。驱动、执行器、资源规格、工作流和参数均不变，141项检查保留。

上轮141项source markers及ARM64通过的结论不涵盖CircleCI配置解析，这是遗漏。本轮本地验证旧配置1处非法模板起始符、修复后仅4个合法参数标签；YAML与6个run步骤的bash语法通过，141条实际grep全部通过。证据 `scratch/framebuffer-0225/ci-template-fix/verification.json`。未取得云端错误原文/run ID，未运行CircleCI CLI/服务器验证，待用户push修复后确认云端启动。

## 行为与范围

原顺序为 ON 表→亮度→管线/首帧。现在为：

```text
ON 表（LP，原始字节不变）→显示引擎持有引用
→mode7 重建本轮管线 / mode2 恢复自有源
→flush 清零 + 新 PP_DONE + RD_PTR 推进（mode7 同时验证 generation）
→暂停 TE/autorefresh/IRQ，限时排空
→最新 typed 亮度 51 high low（HS）及真实 DMA_DONE/idle/ACK 检查
→复用本轮管线，清旧 PP_DONE、重开 IRQ/刷新
→再次验证新帧→恢复触摸→FB 唤醒完成
```

`SweetPanelBrightnessDrain` 追加在原枚举尾部，旧枚举值和状态结构布局保持。排空预算50ms，检查 GIC RWP/enable/active、RD_PTR source、DSI DMA/MDP idle、CTL flush/start 和两个 idle-line 样本。亮度阶段500ms，首帧/重启帧各100ms，原16s总预算不变。未放宽 sequencer 的 IRQ Idle 准入。

第二次刷新复用同一 `pipelineGeneration`，不会再跑管线重建或 CTL reset。发送期间目标变化会在真实完成后补发最新值，不延长500ms预算；完成后、第二帧前又收到目标变化，则在新帧后再排空发送，总预算仍有效。触摸恢复期间收到的目标由原 Apple backlight property-interest 在生命周期完成后重试；`brightnessRestored` 表示本轮恢复命令已完成，不表示此后所有客户端请求已处理。

普通 `setBrightness` 同步路径及生命周期异步恢复明确选择 HS，清 DMA 控制 bit26；使用 long DCS 的 `51 high low` 字节保持。只有 mode2/7 的 typed 入口启用此行为。通用 `dcsWrite`、公开命令序列、ON/OFF 表及旧模式仍为 LP，即使通用调用发送同样的51字节也不自动推断为 HS。每次事务成功后恢复原 DMA 配置。

启动 prepared4096 缓冲复用、FIFO/物理 DMA 分流、真实完成、display/transfer 引用、独立 DMA pin、SMMU/资源/IRQ 所有权均保持。新增等待取消、stop、provider丢失、资源失败或超时仍使该生命周期失败，保留必要资源/pin，禁止晚到写入和自动恢复。未知初始 typed 亮度、iOS 亮度刻度/0夹到1等既有限制未在本批修改。

仅四份生产源码变化：`SweetDisplayBackend.h`、`SweetMDSS.cpp`、`SweetMDSSDSI.inc`、`SweetMDSSLifecycle.inc`。无固件、ABI608、ADT、启动参数或现有成功镜像改动。

## 固定 Lineage 依据与适配边界

参考固定为 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。源码与该提交逐份核对。

- `msm_atomic.c:complete_commit` 的普通顺序为 modeset enable→wait_for_commit_done→cleanup→KMS complete_commit；`sde_kms_complete_commit` 调 connector post_kickoff 后才释放功率资源；`dsi_conn_post_kickoff` 转到 `dsi_display_post_kickoff`。
- LG `lge_backlight_device_update_status` 在不允许更新时保存最新 `bl_lvl_unset`。ON + pending>=0 + !allow 才进普通 post_kickoff 亮度分支；`lge_update_backlight` 还要求 host/backlight 存在、interactive power。原参考在发送失败后也设置 allow=true 并清 pending；XNU 保留更严格的失败保护。
- panel 由 kzalloc 创建，目标 MSM/LG 路径不写 `mipi_device.mode_flags`。通用 `mipi_dsi_device_transfer` 仅在 MODE_LPM 时增加 USE_LPM；因此普通亮度默认为 HS。目标 common-panel 为四个 profile 设置 big-endian，long DCS 帮助函数最终发送51 high low。
- 四个 profile 的 ON/OFF 表为 LP；未启用 brightness short/extension、额外 bl-update-flag 或 dim control。IRC/ACE **已启用**，但 post_kickoff 分支另需 `irc_pending` 和回调，不能把这些功能写成不存在。本批不实现 IRC/ACE 或 D09 色彩/锐度状态。

XNU 的真实首帧门槛、额外排空与第二帧确认是现有 transport/IRQ 架构的适配；Linux 的 wait helper 并不在本测试中提供硬件成功凭据。不能将“post-frame顺序已修正”扩大为整个 DRM、动态时钟票、用户亮度政策或全部错误语义等价。

## 本地验证

完整记录：workspace `scratch/framebuffer-0225/final-review.json`。

| 验证 | 结果与范围 |
|---|---|
| 固定参考执行 | `test-mh2lm-lineage-brightness-reference.py` 直接提取执行9个未改写的 LG/MSM/MIPI 函数，包括真实 `complete_commit`、pending更新、post_kickoff及通用MIPI链。检查最新target、顺序、guard、HS/LP、字节序、原生错误清理及IRC pending。DRM/KMS/OS/host叶子为夹具；中间KMS/connector绑定、初始化、DTS为静态审计，明确记录在 `brightness-reference.json`。 |
| mode2/7 普通路径 | 四profile各三轮，命令表完整FIFO/物理DMA、HS/ULPS、引擎引用/缓冲复用，首帧前无额外typed亮度，mode7每轮只增加一次generation。 |
| 亮度专属回归（两模式） | 最新target在首帧前、排空、发送中及发送后变化；实际 Apple backlight OFF/ON与触摸恢复期间延迟重试；同字节typed HS/generic LP；第二帧独立凭据；六种排空阻碍；五阶段×五种cancel/stop/provider/deadline/resource失败；五种DONE/idle/ACK/IRQ/stale-DONE失败，资源与引用保留且无晚到写。 |
| 实际 MDSS→实际 FB | 两模式各108个真实生命周期状态，四profile三轮；新增排空/亮度/第二帧/触摸中间状态保持pending。拒绝18种坏凭据，mode7另6种generation/pixel/capability问题。 |
| 其他回归 | 命令引用/分配故障（mode2/7）、mode6供电、host、DSI sequencer、display-service旧DCS精确trace通过；共12组驱动运行，ASan/UBSan无报错，泄漏检测因运行环境ptrace限制关闭，保留夹具对象计数。 |
| ARM64 编译 | 真实历史CI参数，Linux clang19替换Darwin绝对路径/SDK路径及XNU类型内建宏；基线和修改版 MDSS 对象均成功，汇总告警1596→1596。不是完整云端内核构建。 |
| 补丁/识别门槛 | 0225严格apply；0185–0225共41patch无offset/fuzz，79份源码与edited/verify/replay逐字节一致；141项CI源码markers。新增2个内核marker，真实旧a34被拒绝且未生成镜像。 |

补丁 SHA256：`2a9a0242d7530c2b413222da62508b7c7cb4975fd8f40e18a0dba95ca5e2a698`。a34成功镜像 SHA仍为 `f8554f0a21c2d3f1ae14d8c45e2ac3837c381a3d883145324b7736ec8533fadd`。

关键复现：

```sh
cd /home/shenli/work1/ref/ios-mh2lm
python3 tools/tests/test-mh2lm-lineage-brightness-reference.py ../lge-lineage-kernel /home/shenli/work1/scratch/framebuffer-0225
python3 tools/tests/test-mh2lm-brightness-lifecycle.py /home/shenli/work1/scratch/framebuffer-0225/verify
python3 tools/tests/test-mh2lm-brightness-lifecycle.py /home/shenli/work1/scratch/framebuffer-0225/verify --pipeline
python3 tools/tests/test-mh2lm-panel-lineage-fb.py /home/shenli/work1/scratch/framebuffer-0225/verify --pipeline
```

其余命令使用 `final-review.json` 对应的tests脚本与同一verify目录；`prepare-patch.py`、`replay.py`、`compile.py`、`final-review.py` 位于该scratch目录。

## 原实机安排（历史，结果见顶部）

用户 push main 触发 CircleCI，下载新 kernel/buildtree，建议命名 **0121a35**。agent 核对源码/marker后沿原 ABI608 打包mode7；没有新kernel时不能提供新boot镜像。a34保留，不重复同版验证。

新版本重点：首次generation1/桌面正常；低、中、高亮度调节；两轮普通息屏5秒/唤醒，观察是否有闪亮、亮度跳变、停帧或触摸异常，正常后各留至少60秒日志。每次ON应先ON表完成与generation2/3，再出现：

```text
fresh frame before brightness; draining IRQ/MDP target=...
... commands=1 memory-dma=0; HS/timer sequence
post-frame HS brightness complete target=...; stream restart pending
... 同一 generation 再次 flush/kickoff、新帧、touch ready、ON complete
```

profile表仍显示LP；PV ON表17包/4物理DMA，typed亮度是另外的FIFO包。统计不要把额外亮度当作第18条ON表命令。第一次普通恢复应有两次IRQ rearm/像素确认，第二次不应增加generation。日志记录比最终亮屏更重要；发生异常先保存USB/持久现场，不放宽完成条件。

本批通过实机后再推进 [D09色彩/锐度](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)，随后其余PD4。完整C08动态时钟/MDP票周期、共享LP/core/SMMU/QoS及BUG-006日志环竞态继续单列，整个显示驱动尚未完成。
