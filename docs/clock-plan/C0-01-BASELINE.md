# C0-01 结果：时钟计划运行基线

[返回 C0](00-baseline.md)。日期：2026-11-22。等级：**调查完成**（引用既有 b32 实机证据，未新增镜像或设备操作）。

## 唯一基线条目

| 项 | 值 |
|---|---|
| 驱动源码 | `b3f735b`（full-source 分支；之后到 `f042fe2` 只有文档提交，`git diff b3f735b HEAD -- dxb xnu .github tools boot-support shim` 为空） |
| CI 归档 | `logs/xnu-kernel-0121b32.gz` `def476f9…879b`；`logs/xnu-buildtree-0121b32.gz` `dcda2823…aebd21`；5300 源文件与 `b3f735b` 逐一吻合（`scratch/artifact-0121b32/packaging-verification.json`）。CI run ID 及完整云端日志未取得 |
| 内核 | `kernel.development.mh2lm` 17863608 字节，`cc50524a…29856` |
| ADT | `out-bs/adt-16k-3g-panel-pipeline.adt` `a9e4f371…1875`，由 `tools/mk-mh2lm-mdss-adt.py` 派生 |
| 附带 LG DTB | `de479a45…befb`（从 b32 镜像 kernel 段 gzip 尾部提取，与打包记录一致） |
| 固件 | `kernel-panel.blob` `27e7a1d1…a480`（panel-uefi-0121a14-bootargs608） |
| 镜像 | mode3 `out-bs/boot-16k-0121b32-gpioirq-panel-pipeline.img` `5946904d…c4e99`；mode2 `…-baseline-…img` `ccb18497…4dc08` |
| 实机日志 | `logs/sweetlog-0121b32.bin` `7a365e0e…eacea`，2026-11-21 mode3 普通验收（[记录](../DISPLAY-TOUCH-FAILURE-FALLBACK.md#b32-普通实机验收2026-11-21)） |
| 回退 | b31r2、b30 镜像保留 |

以上 SHA 于 2026-11-22 重新计算，均与交付记录一致。

## 时钟相关现有回归（本地，基于 `dxb/xnu` @ `f042fe2`）

证据：工作区 `scratch/clock-c0/regressions/`（`summary.json` 及逐项日志）。

- 通过 17 项：rpmh-resources、rpmh-completion-irq、rsc-clock-votes、rsc-qtimer-init、rsc-local-reset-evidence、bcm-metadata、display-clocks、display-client-clocks、display-core、display-noc、clock-timeout-diagnostics、clock-write-readback、voted-halt-completion、command-clock、link-lp-clock、noc-completion-evidence、noc-admission-diagnostics。
- 已知失败 1 项：panel-profile-lifecycle，`ESC0 did not halt phase=27` 后 `untilLife` 断言；与 b32 本地记录中基线／修改版同位失败一致，不在本卡修复。后续时钟卡以“该失败位置不变”作为不退化判据。

## 结论

后续时钟候选以 b32 / `b3f735b` 为基础；公共实机回归沿用 b32 的启动／根卷、USB 日志、触摸、显示首次接管和两轮普通 OFF/ON。基线无未闭合来源项（CI 元数据缺失照旧记录，不作为来源证明）。
