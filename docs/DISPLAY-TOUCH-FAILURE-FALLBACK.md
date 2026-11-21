# 独立显示降级：触摸终止后完成首次接管（0121b32）

更新：2026-11-21。实现 `b3f735b`，基于 `9fb04a3`（b31r2 普通实机验收记录；驱动基线 `5e1680b`）。本地实现、CI归档核对与两档镜像回读完成，待实机结果。b31r2 仍是最新普通实机基线，b30 保留回退。

## 问题与范围

这是 [GPIO 计划中的独立后续任务](GPIO-IRQ-IMPLEMENTATION-PLAN.md#独立后续任务)，不再改变 TLMM / GIC 中断策略。旧代码在首次接管前段允许无可用触摸 worker 时继续显示，但取得真实像素完成后仍无条件调用触摸 `resume()`，导致生命周期失败。旧测试仅检查内部 `fScanoutState == SweetScanoutActive`，没有推动到最终 `SweetPanelComplete`，也没有确认 framebuffer 消费的公开 `scanoutStatus()`。

本次先只扩展完成断言，原始 b31r2 在该断言确定失败；随后实现修复。仅三份生产文件变化：

- `SweetMDSSLifecycle.inc`：无 token 的降级参与者复核、启动截止检查和最终完成分支。
- `SweetTouchPanel.cpp/.h`：追加私有 `OpFailed`，将终态查询放入现有 touch gate，与 session close/stop 串行。

C worker、TLMM、GIC、framebuffer、总线层、公有状态结构与 ABI608 均保持原实现；未修改 CI workflow、固件、ADT 或启动参数。`full-source` 分支直接维护完整源码，不生成历史补丁目录的新 patch。

## 降级合同

1. 仅首次 `SweetPanelTakeover`、没有已接纳的 touch token，且准入采样已看到同一 retained touch backend 的 `!available && SWEET_TOUCH_FAILED` 时允许降级。没有 ready、单纯离线、停止服务、失去 session 或仍在初始化均不够。
2. `touchQuiesced` 在此表示终止的 worker 没有未完成的触摸请求，不伪造 quiesce/resume token。每次生命周期及子资源推进仍复核参与者；有 token 的正常路径继续验证真实 fence。
3. 30 秒启动等待保持截止。终态查询可能等待 touch gate，故在查询返回后、任何显示续程请求前重新采时；已逾期则失败。仅截止前获准的降级续程获得正常 16 秒总体预算及 5 秒 takeover 阶段预算，接近启动截止不会继承所剩不足的等待预算。逾期时不打印降级继续日志。
4. 必须仍完成实际 scanout 的 flush/new PP_DONE/新帧检查、资源持有及适用的 DSI post-enable 交接，才跳过不存在的 touch resume 并进入 `SweetPanelComplete`。此后公开 `scanoutStatus()` 才返回 Active，而非 Coordinating/Failed。MDP/BOOT 等实际扫描资源仍保留。
5. 降级只保首次接管。正常触摸已有 token 后的 pause/resume 失败仍使显示生命周期失败；OFF/ON 仍需正常 worker 与真实 fence，不将本次扩展成无触摸的电源管理。

终态条件不记录 worker 的 ever-live 历史；准确范围是“首次接管前、未接纳 token、采样已确认终止”。若先采到 available，worker 在随后 quiesce 前退出，原有请求拒绝仍保持，不能声称覆盖整个竞态窗口。`sweettouch=0` 和线程创建失败不发布这里要求的终态，不能当作本修复的实机故障注入入口。首次接管后若真实 framebuffer 收到 OFF 准入拒绝，既有软件呈现冻结策略仍保持，不能宣称触摸失效后整套显示 OFF/ON 可用。

## 本地验证

证据目录：工作区 `scratch/display-touch-fallback-b32/`；汇总 `final-review.json` 记录9组正回归日志摘要、三份已提交源码摘要、文档链接检查和两档b31r2回退镜像摘要保持。

| 验证 | 结果与证据 |
|---|---|
| 旧版确定性负对照 | `b31r2-negative.log`：仅补最终 complete 断言，原始源码失败；`baseline-fallback-negative.log`：同一完整 mode2/7 降级测试对原始源码在 complete 断言失败 |
| 生命周期 | `panel-lifecycle.log`：完整首次接管；立即、延迟、启动截止前终态；逾期拒绝；新帧门槛；失去终态、取消、stop、backend 退出及真实 token 故障 |
| 实际面板降级 | `panel-touch-fallback.log`：mode2/mode7 × 四 profile 到 complete，公开 Active；资源和 post-enable 门槛；无触摸请求、reset/面板表请求；已知亮度后的 OFF 拒绝、合法 parked ON 的失败 fence 拒绝 |
| 查询同步与截止交错 | 同上：持 touch gate 的真实双线程 stop/query；查询必须等待并返回 stopped/NotReady；在查询边界推进时钟跨过 30 秒后无显示请求 |
| 相关回归 | `panel-touch.log`、`panel-lineage-wake.log`、`panel-pipeline.log`、`display-client-clocks.log`、`panel-lineage-fb-pipeline.log`、`display-service.log`、`log-levels.log` 均通过；加前两组共 9 组 |
| ARM64 单文件 | `compile.py` 沿既有 CI 参数；MDSS 与 SweetTouchPanel 基线/修改版均成功，告警类别和数量分别 1642→1642、1152→1152；无告警抑制，详见 `compile-comparison.json` |
| 源码门禁 | 现有 CI marker 154/154；三份生产文件变更范围已核对，见 `source-and-compile-checks.json`；`git diff --check` 通过 |
| 打包旧版拒绝 | `b31r2-packaging-negative.log`：真实 b31r2 内核因缺少 `takeover complete without touch; terminal worker failure retained` 被 b32 脚本拒绝，未产生镜像 |

回归以真实 MDSS、native touch service 和 C worker 源码运行，替代 OS 调度、MMIO 和外部设备响应；使用既有 ASan/UBSan 配置，串行执行。不将 host 完成当成真实 GIC、总线、面板电气或全内核并发验收。

### 既有测试失败单列

额外执行的旧 `test-mh2lm-panel-profile-lifecycle.py` 在基线与修改版均于相同 ESC0 停钟阶段失败（`ESC0 did not halt`，随后同一 untilLife 断言），分别见 `baseline-profile-lifecycle.log` 和 `panel-profile-lifecycle.log`。旧 fixture 的物理模型没有后续 Lineage fixture 的 ESC0 完成反馈。本批不修改该既有测试，也不把它列为 PASS；带完整反馈的 `panel-lineage-wake`、`panel-pipeline` 和真实 MDSS→FB pipeline 合同回归均通过。现有测试问题仍保留。

## 本地完成时的交付计划（归档与打包已完成）

以下保留本地完成时的交付步骤；本轮已完成归档核对与打包，当前下一步见[镜像交付与实机验收](#b32-镜像交付2026-11-21)。

本地准备好工作区 `out-bs/mk16k-0121b32.sh`，沿 b31r2 的两档 ABI608 布局、相同固件/ADT/启动参数，只新增 b32 内核字符串门禁；shell 语法和旧内核拒绝已验证。字符串门禁不能代替三份生产源码及整个 buildtree 的逐文件核对。

由用户执行：

```sh
git -C /home/shenli/work1/ref/ios-mh2lm push origin full-source
```

待 CircleCI 成功，下载为工作区 `logs/xnu-kernel-0121b32.gz`、`logs/xnu-buildtree-0121b32.gz`，并保留 CI run ID/构建日志。agent 核对归档完整性、5300 源码与 `b3f735b`、内核 SHA 和 b32 标记后，用已核验的 digest 打包，再从最终镜像回读固件、FAT 内核、ADT、TOML 与参数。

普通实机先临时启动 mode3，验首次桌面、触摸、多指全部抬起、两轮普通 OFF/ON 和醒后触摸，导出 `logs/sweetlog-0121b32.bin`；正常 worker 不应命中无触摸降级 WARN。真正终态故障时的显示降级尚无 b32 实机证据，也没有本批新增的实机强制失败开关；不能为了触发它放宽 fence 或擅自操作 pad。受控故障实机验证另行记录，不伪造已验结论。

本批未 push、下载、执行 fastboot 或写设备。总线与触摸 C++ 迁移待本批验收后继续；第6批持续异常电平、显式恢复、多 GPIO 硬件并行和长期稳定性等原边界保留。


## b32 镜像交付（2026-11-21）

收到两份 CircleCI 归档并完整读取 gzip/tar，CRC/归档检查通过。5300 个 XNU 源文件与 `b3f735b` 的 Git blob 逐一吻合，无缺失、差异或额外源路径；相比 b31r2 仅三份预期生产源码变化。现有154项源码标记全部命中。内核17863608字节，SHA256 `cc50524ad13a191f354b4927627a1e618e56fd46325f370235a944dc38b29856`，与包内清单一致；b32完成降级及既有b31r2探针/隔离标记存在。未提供CI run ID或完整云端构建日志，不声称核查了云端元数据或全量告警。

| 下载归档 | 字节 | SHA256 |
|---|---:|---|
| `logs/xnu-kernel-0121b32.gz` | 60230474 | `def476f93f7d6c38885956f0e78a36f258c4de1fd0cfd526e332bdfc9418879b` |
| `logs/xnu-buildtree-0121b32.gz` | 23432572 | `dcda28230665934a6290baaba687be53cc8ae566f78e40a292db8d00aaaebd21` |

沿既有脚本打包，两份镜像各35545088字节。对最终Android v1镜像按实际头结构拆出固件与32MiB FAT16，再回读FAT里的内核、ADT与TOML；内核逐字节等于已核验输入。固件与对应b31r2完全一致，解压为112字节BootShim及3145728字节FD，追加DTB保持；固件嵌套卷的MHPANEL1检查通过。ADT为23140字节、摘要 `a9e4f37151162be9244b2e701b2e60063d85d4c424e8842c5bbf9b8d62391875`。两档参数均605字节，逐词与对应b31r2相同，无重复键；两档之间只差 `sweettouchpanel=3/2`，显示生命周期仍为mode7。TOML仅版本标题变化。

| 最终镜像 | 触摸模式 | SHA256 |
|---|---:|---|
| `out-bs/boot-16k-0121b32-gpioirq-panel-pipeline.img` | 3（本次先测） | `5946904dff1b20a26e52f4f0d5041e4ea4cc59d7b813542d7ed6e0ccabbc4e99` |
| `out-bs/boot-16k-0121b32-baseline-panel-pipeline.img` | 2（配套档） | `ccb184972caf7a4d2ded3f81d896fe7055e9b28d1adeceb1a808f4674244dc08` |

证据在工作区 `scratch/artifact-0121b32/`：`verify-artifacts.py`、`artifact-verification.json`、`packaging.log`、`verify-images.py` 和 `packaging-verification.json`。两份b31r2镜像SHA保持交付值。此次没有生产/CI/固件修改，没有重跑同版驱动回归，也未push或操作设备。

**下一步由用户临时启动触摸mode3：**

```powershell
fastboot boot "\\wsl.localhost\Debian\home\shenli\work1\out-bs\boot-16k-0121b32-gpioirq-panel-pipeline.img"
```

先验桌面、连续触摸/多指全部抬起，再两轮普通OFF/ON与醒后触摸；留存 `logs/sweetlog-0121b32.bin`，可用既有USB采集补充。核对探针、正式armed、首次接管complete与FB handoff active、两轮完整生命周期。正常触摸时不应出现无触摸降级WARN；若worker实际终止，保留原始日志，核对终态→显示真实传输→降级完成→FB active，而不期待故障触摸恢复。故障降级实机与受控故障注入仍未覆盖；无新强制失败开关，不以`sweettouch=0`替代终态。暂不进入总线/C++迁移。
