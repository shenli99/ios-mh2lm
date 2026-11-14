# 0222：按 Lineage 重建固定显示管线

2026-11-14，承接 a31/0221 的成功息屏唤醒基线。本轮推进计划“独立重建显示管线（PD4 及 PD2 余项）”的第一段：首次受控接管以及每次 mode7 唤醒，重新配置 VIG0 → LM0 → CTL0/PP0 → INTF1，然后提交自有 A 缓冲并等待新帧。已完成本地实现与验证，尚无新 CI 内核或实机结果，PD4 保持进行中。

补丁：`xnu/patches/0222-mh2lm-lineage-pipeline-rebuild.patch`。
参考仓：`ref/lge-lineage-kernel`，提交 `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。
生产修改八份文件；新增 `SweetMDSSPipeline.inc`，继续使用唯一 SweetMDSS owner/gate 和原有 timer、资源票、扫描缓冲区。

## 实际变化

| 部分 | 实现与 Lineage 依据 |
|---|---|
| CTL 复位 | `sde_hw_ctl_reset_control` 的 CTL0 `SW_RESET=1`，轮询 bit0 清除；2ms 硬件等待界限，XNU 用异步 timer，不在 gate 内睡眠或自旋。等待期间禁止撤回 boot clock lease。 |
| SSPP | `sde_hw_sspp` 的 BGRA format/unpack、单矩形、尺寸/裁剪/行距/四平面地址、软件 pixel-extension；无缩放时关闭 QSEED3、旁路 CSC10。仍使用 1125×2436 自有缓冲，1080×2340 输出、stride4608、crop22,48。 |
| Mixer/CTL 路由 | LM0 输出尺寸、单 base stage、无 split/alpha blend/dim layer；清除 CTL0 内其他 mixer stage，VIG0 仅接 LM0。ACTIVE_CFG command mode、INTF1 active，无 WB/CWB/CDM/merge3D/DSC。其他 CTL 不写入。 |
| PP/INTF | PP0 DSC bypass、INTF1 绑定 PP0，TE0 及完整 tearcheck 配置。四 R66456 DTS 都是 1080×2340、vtotal2360、60Hz；按 Lineage 公式 `19200000/(2360*60)=135`，不重放 ABL 的 0x88。 |
| 提交顺序 | 清旧 PP_DONE → rearm IRQ → INTF 子 flush mask2 → 主 flush `0x80020041`（VIG0/LM0/CTL/INTF）→ autorefresh frame_count1 → 连接 external TE。先 kickoff 再连接 TE，对应 Lineage 防止 premature autorefresh 的顺序。 |
| 完成协同 | 新配置生成单调递增 `pipelineGeneration`；仍须 flush0、新 PP_DONE、RD_PTR 前进，才恢复触摸。FB mode7 需要新 capability、完整 supply/link/data 凭据以及比 OFF 更新的配置代数，旧代数不能算唤醒完成。 |

SMMU domain 检查与像素配置检查拆开：已接管后的 mode7 可以从丢失的像素配置恢复，不能因旧尺寸/format/TE 不存在就提前拒绝；SMMU ID、唯一 SID 路由、CB2 属性及实际 fault 检查保留。第一次进入仍验证已知 ABL 管线并排空旧扫描，尚未实现无 ABL 冷启动。

复位前要求触摸 quiesce、资源 Held、IRQ drained、DSI idle、无 pending CTL flush/start、TE/autorefresh 已停和空闲扫描线。开始复位前独立 pin 自有缓冲。另一 active CTL 占用 INTF1、LM0 或 VIG0 会拒绝；不清除其他控制器的配置。取消、stop、provider 退出或资源/硬件完成失败后不重试，不用回显释放 DMA 内存。

短命令 FIFO、长命令 prepared 物理 DMA、面板命令表、ON/OFF 延时与亮度协议均未修改，保持用户指定的传输例外。mode2/a31 息屏流程保留。

## 明确的阶段边界

本轮重建的是上述固定单管线配置。共享 MDP core/clock、SMMU domain、VBIF/QoS、DSPP 色彩状态等仍保留；没有实现完整 MDP power-collapse、共享 SMMU 复位或全部显示状态丢失后的冷恢复。PP dither 等未纳入本次重新配置范围。首次自有帧仍由现有代码从 ABL 像素初始化；无 ABL 首帧/default surface/console/panic 仍属于后续 PD1/PD4 余项。

测试中的“清空配置”由 MMIO 夹具注入，仅清本轮重建的配置，不宣称真实 CTL_SW_RESET 会清掉全部这些寄存器，也不清共享资源/域。实机 mode7 每次执行真实 CTL0 reset + 重配，没有额外零填寄存器的故障注入。

保留 a31 成功基线，当前 UI 仍是自有 A 单缓冲、30Hz CPU 拷贝。未实现 F5 零拷贝、完整 PD5 或整机低功耗。显示自身剩余初始化/恢复依赖继续在目标二解决，不移交给末尾整机低功耗目标。

## 本地验证

- mode7 实际 MDSS/资源/DSI/Pin/TouchComm 代码，四 profile 各三轮清空配置后唤醒；初次接管和唤醒都重新配置，旧 TE/autorefresh 快照损坏不影响软件配置。验证 reset 等待、flush/kickoff/TE 顺序与每项新帧凭据；两处 reset 等待分别覆盖 timeout、cancel、stop、provider、SMMU、DSI、资源、IRQ、CTL 冲突，boot clock 撤票拒绝及晚完成安静退出；ABI/三映射缺失、误址、短长度拒绝并释放。
- 真实 mode7 MDSS 的 60 份 typed 状态经另一进程中的真实 FB 消费：四 profile 各三轮，含快速反向请求、pending pixels/touch，18 种通用损坏凭据和六种 pipeline/capability 情况。两进程回放不模拟真实跨 workloop 并发。
- 旧 mode2 的四 profile 各三轮，以及 mode2 的 60 份跨层状态回放、FB modes1–6、通用 display service/IRQ/scanout 回归通过。上述执行使用 ASan/UBSan；LSan 在本机受限环境禁用，夹具检查模拟 reboot 后对象/映射/分配计数。
- 从 Lineage 文件即时提取并运行 14 个未经改写的底层函数，生成 `tools/tests/fixtures/mh2lm-pipeline-lineage.json`；新驱动测试逐项比对稳态配置。涵盖 CTL reset 2ms 界限、ACTIVE_CFG/flush、LM、pixel-extension、PP、INTF TE/autorefresh；不是整个 DRM 调用栈移植验证。
- MDSS、Framebuffer 实际 ARM64 对象基线/修改编译通过。相同本机命令下 clang 汇总 MDSS 1585→1596、FB 1396→1396；增加 11 条既有类别 `-Wunsafe-buffer-usage`，来自固定有界映射数组访问，无新警告类别。不是完整 CircleCI 链接。
- 0185–0222 共 38 补丁严格无 offset/fuzz 重放，79 文件逐字节一致，125 项 CI source markers 通过。CI 工具链/构建环境不变。
- ADT 单测及原七开关的全部 128 种组合对比旧版本逐字节不变；mode7 append-only ABI/CLI 通过。参数605/607字节，bash/Python/YAML 检查通过；真实 a31 内核被新 marker 门拒绝，未生成任何 boot 输出。

通用 display service 的 scenario5 原来把 source echo 不符判为立即失败，已在未修改的 a31 源码上复现同样失败；0220 起它已是诊断。测试改为等待实际完成，并验证超时继续保留 pin；没有为迁就测试改变生产判据。

底稿：workspace `scratch/framebuffer-0222/`，含 base/edited/verify/replay、ARM64 对象日志、基线旧测试复现、各回归日志、两档 bridge 状态、严格重放、ADT/参数和旧内核拒绝记录。

## 下一步交付与验收

用户 push 最新 main 触发 CircleCI，下载新 kernel/buildtree（建议标签 `0121a32`）。Agent 核对 0222、79 份受检源码和 125 项 source markers，沿用 ABI608 固件；无需重新编固件。

新档使用 `--panel-pipeline`，`sweetpanellifecycle=7`，不会因旧文件名误用 mode2。mode7 新增 MDSS map16–18：LM0 `0x0ae45000/0x320`、PP0 `0x0ae71000/0xd4`、VIG0 CSC10 opmode `0x0ae06a00/4`，以及 `sweet,display-pipeline-abi=1`。原16映射及 identity元数据不变。

拿到并核对新内核后再打包：

```sh
SWEET_PANEL_UEFI_MANIFEST=/home/shenli/work1/out-bs/panel-uefi-0121a14-bootargs608/panel-uefi.json \
  tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-0121a32.gz 0121a32-abi608 --panel-pipeline
```

预计输出 `boot-16k-0121a32-abi608-panel-pipeline.img` 及 metadata-only identity；当前尚无这些新镜像。a31 retained保留。用户只需临时启动新 pipeline 档，先确认首次管线重建及桌面更新，再一次 OFF/ON 检查画面、触摸、按键和亮度。日志应见初次 generation1，唤醒 generation2，随后 fresh pixels、touch/ON complete、FB owned resume。成功后再决定剩余独立恢复范围和多轮验收，不重测旧版来替代新路径验证。
