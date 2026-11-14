# 0224：普通 OFF/ON 的 HS 时钟与 ULPS 顺序

2026-11-14。在 a33/0223 两次普通唤醒成功基线上，按全流程矩阵推进 D03/D04，并审计 C08。**本地实现与验证完成，待新 CircleCI 内核及实机；a33 仍是成功实机基线。** 没有改亮度/色彩、固件、ADT、启动参数或日志环。

## 实现范围

补丁：`xnu/patches/0224-mh2lm-lineage-clock-ulps-order.patch`。
SHA256：`282162e246c92c316354e5221c74ecebd64aef9e64701ff4582afdf5309c16a4`。
生产源码仅改 `SweetMDSS.cpp` 的内部 link 状态枚举及 `SweetMDSSLink.inc`。

原 0223 普通 OFF 在 HS 还运行时进入 ULPS，然后才关 PHY 动态门控、HS 分支与 PLL。0224 改为：

1. 沿用 panel OFF 真完成、reset-low、host shutdown、停扫/IRQ/资源检查及首次 lane-idle 检查。
2. 关闭 PHY0 wrapper 动态门控，设置 HS suspended mask；依次关闭 byte_intf (`DISPCC+202c`)、pixel (`+2004`)、byte (`+2028`)。
3. 等三个分支真实 `CBCR_OFF`，且 RCG 没有 UPDATE/dirty；关闭 global clock、发 PLL stop、关闭 resync FIFO、关闭 PLL bias，等待至少 250ns。
4. 新 `LinkULPSIdle` 重新检查 `PHY+14c` 的五条 lane idle，期限 100µs。通过后才请求 `PHY+a4=1f`，等待至少 100µs、关闭 LPRX/CDRX，并检查真实 ULPS status。
5. 新 `LinkPHYOff` 执行已有 PHY shutdown、reset mask、Dormant。后续协调器才 park DATA/撤 panel votes。

ON 保留原来的 controller/PHY setup、ULPS 重入、PLL lock/roots；HS 开启顺序改为 byte→pixel→byte_intf，之后 resync→ULPS exit→PHY gating。共享的 warm rebuild 也使用此 HS 分支顺序及 PLL disable 的 FIFO stop；旧非 Lineage 路径不会增加 ULPS 请求。

PLL unlock 和配置回显仍只是诊断，不能当作硬件完成证据。每个异步步骤仍重新检查资源、参与者、时限、停扫和 DMA 状态；失败保持不确定状态及所需 rails/pins，不在失败后继续写。a33 的预先 prepared 4KiB 缓冲、短 FIFO/长物理 DMA、真实完成检查、display/transfer engine 引用、generation/新帧凭据均保持。

## Lineage 依据及 C08 审计

参考固定为 `ref/lge-lineage-kernel@2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。下列路径均相对该仓库。

| 项目 | 实际调用和边界 |
|---|---|
| 普通关钟 | `drivers/gpu/drm/msm/dsi-staging/dsi_clk_manager.c` 的 `dsi_update_clk_state` 调 HS OFF，再调 LP OFF；`dsi_clk_update_link_clk_state` 执行 pre callback→clock helper→post callback。不能按 callback 函数里两个 if 的书写顺序推断调用顺序。EARLY_GATE、force-update 另有路径，本次不声称覆盖。 |
| HS/ULPS | `dsi_display.c:dsi_pre_clkoff_cb` 在 HS pre-off 先停 PHY gating，LP pre-off 才按 panel_initialized/ULPS flags 进 ULPS。四个目标 DTS 都开 normal/suspend ULPS，未启用 force-clock-lane-hs；include 覆盖仍依既有全流程审计。 |
| PLL disable | `drivers/clk/qcom/mdss/mdss-dsi-pll-7nm.c:vco_7nm_unprepare→dsi_pll_disable`，global off→PLL control 0→RBUF 0→mux/bias off→250ns。HS disable 后 unprepare 可沿 CCF 父链停 PLL；此处是源码依据，参考夹具没有模拟完整 CCF 父树。 |
| lane idle | `dsi_phy.c:dsi_phy_enable_ulps` 在非 clamp 情况请求前检查；v4 `dsi_phy_hw_v4_0.c` 读 `LANE_STATUS1`，10µs poll/100µs timeout。重入带 clamp 时不作该 poll。XNU 新增的是关 HS 后的检查，原关 HS 前检查保留。 |
| 命令 clock/wake | `dsi_host_transfer`：ALL_CLKS ON→wake_up→engine ref→保留缓冲→transfer→engine release→ALL_CLKS OFF。**此固定版本的 `dsi_display_wake_up()` 仅 `return 0`。** 不能把这个函数名解释为缺失的唤醒硬件步骤。 |
| 引用聚合 | `dsi_clk_req_state` 对 client 的 core/link 分别计数，`dsi_recheck_clk_state` 合并 DSI/MDP client 状态。任一 client ON 就保持 ON；不是每个包都物理关钟。prepare 持有 core/link，ON 表嵌套请求；command post_enable 撤长期 DSI vote，pre_disable 重取，OFF 表嵌套，unprepare 在 host disable 后释放 link/core。 |
| 停扫/完成 | host→ctrl→message 普通命令链没有无条件停止 TE/autorefresh；实际 DMA kickoff 以 `cmd_dma_done` completion/中断 status 检查。`dsi_display_pre_kickoff` 在 clkrate_change_pending 分支锁 panel 并等 MDP idle；encoder 另管理帧完成/停扫。XNU 的整表及同步亮度停扫、IRQ drain、真实 idle/DONE、CTL/新像素与保 pin 是当前适配策略，不能混同为 Linux 的逐包流程。 |

**C08 的审计完成，但完整逐命令时钟管理尚未实现。** 本次保留 XNU 生命周期已持资源与 suspended-mask 拒绝准入，不添加只有形式的每包 clock get/put。post_enable/pre_disable 的完整资源周期、MDP/DSI 动态聚合和共享 LP/core/ESC 的物理释放仍有差异，要随显示资源工作继续处理；D03 本次只收口 HS→ULPS 的顺序，不代表全部 LP/core collapse 已完成。

## 本地验证

证据目录：workspace `scratch/framebuffer-0224/`；最终汇总 `final-review.json`。

- `test-mh2lm-lineage-clock-reference.py`：直接提取并执行 14 个未改写的真实 LG manager/callback/HS 函数。普通 OFF、ON、17 次嵌套命令引用、独立 MDP client 持票通过。OS/CCF、LP/core/PHY 叶子是夹具；不是完整 DRM 执行或硬件验证。另用 `mh2lm_lineage_clock_audit.py` 检查 C08 caller/guard、PLL 路径与四个 DTS，静态结果单列。
- mode2/mode7：四 profile 各三轮，验证 HS 内部顺序、PLL/bias→ULPS、重入/退出时序。五条 lane 各自关 HS 后不 idle 的延迟/超时、分支不停止、各新增/移动等待态的取消/stop/provider loss，均保持 pin/rails 且无 late writes；转换期间 DSI/时钟请求被拒绝。
- 命令生命周期 mode2/mode7：提前申请/prepare/unwind、跨 OFF/ON 复用、运行时分配失败注入、engine refs、FIFO/DMA fresh completion/idle/ACK/错误保留通过。
- 实际 MDSS→FB mode2/mode7：各 60 份状态，分别拒绝 18/24 种损坏凭据；成功不取消，待新帧与触摸后恢复。另通过 warm link、DSI host/sequence、旧 mode6 supply、display service 回归。
- ARM64 `SweetMDSS.cpp` 基线/修改版按同一份历史 CI 命令做本地单文件交叉编译，都通过；clang 汇总均 1596 warnings，没有新增。Linux 兼容调整见 `compile.py`；这不是整核 CI 编译。
- 0224 严格 apply 后与 edited 逐字节相同；0185–0224 共 40 个补丁严格重放，无 offset/fuzz，79 文件一致；136 项 CI source markers 通过。
- 打包器新增两个 0224 kernel markers。真实旧 a33 被拒绝且未创建输出，记录 `old-a33-rejected.txt`。ABI608、ADT、参数保持；没有新 kernel，尚未生成 a34 镜像。

复现（在 `ref/ios-mh2lm`）：

```sh
python3 tools/tests/test-mh2lm-lineage-clock-reference.py ../lge-lineage-kernel /home/shenli/work1/scratch/framebuffer-0224
python3 tools/tests/test-mh2lm-panel-lineage-wake.py /home/shenli/work1/scratch/framebuffer-0224/verify
python3 tools/tests/test-mh2lm-panel-pipeline.py /home/shenli/work1/scratch/framebuffer-0224/verify
```

## 下一步

用户 push 本地 main 触发 CircleCI，下载新 `kernel/buildtree`（建议命名 0121a34）。agent 核对 0224 和产物后，沿原 ABI608 打包 mode7；用户临时启动，先看首次 generation1，再验证两次普通 OFF/ON 和 generation2/3、触摸/持续更新。

OFF 预期先出现 `HS branches halted; PLL/bias stop issued; checking ULPS lane idle`，再出现 `ULPS entered after HS stop; PHY shutdown next`，最终 OFF complete。ON 沿原 true-completion/generation/pixels/touch 验收。若卡在新 idle 检查，保留完整 USB 与持久日志；不放宽判据绕过。

实机通过后继续 D07/D08 亮度时机及 HS、D09 色彩/锐度，再推进其余 PD4。完整 LP/core/SMMU/QoS、首次接管/持续帧与其他矩阵项保持；不把它们标成全流程已一致。
