# FG 初始 DMA 错误恢复与短记录日志（0153）

2026-11-06，实现提交 `47fa07e`，承接原生 0120a1 / 0152。代码和本地验证
完成，待用户 push、CircleCI 构建及实机验证；没有新 boot，也未操作设备。
0120a1 证据见 [FG observer 与路由记录](BATTERY-FG-OBSERVER-ROUTES.md)。

## 本轮解决什么

0120a1 已通过 PMIC 身份读取，四个 FG 字段均在 `0x4370` DMA 错误检查返回
`-9`。旧日志没有原始状态字节，pre/post 共用操作名，不能确认具体错误位、
发生阶段或来源。现补第一次会话之前的有界清错，并保留新的事务错误证据。

硬件参考为本工作区 LG SM8150 内核：

- `ref/android_kernel_lge_sm8150/drivers/power/supply/qcom/fg-reg.h`：
  PM8150B SID2，`0x4370` bit1 WRITE_ERROR / bit2 READ_ERROR；
  `0x4371` bit0 CLEAR_LOG、bit1 ADDR_KIND。
- 同目录 `fg-memif.c:fg_clear_dma_errors_if_any()`：读错误位，再按是否有错
  置位或清除 CLEAR_LOG；`fg_dma_init()` 在初始化中调用它。

这不是移植全部 Linux FG 初始化。下面的显式 low/high/low、回读和最多五次
状态检查是本驱动的有界实现，实际硬件清除时序尚待确认。首个 pre 中已有错误
也不能单凭日志认定是启动固件遗留，外部 EE 的作用仍未证明。

## 执行顺序与失败行为

`sweet_spmi_fg_read()` 继续在 consumer 自己的工作上下文执行，持 FG 会话锁。
身份/revision、MEMIF 空闲以及所有协议 writer 的预检通过后，才进入新增准备：

1. 取得共享 SPMI 控制器锁；重新检查 `0x4371` 唯一 EE0 writer、共享 RW poison，
   再读 `0x4340/0x4350`，要求 request/interface 均空闲。
2. 读取原始 `0x4370/0x4371`。第一次无错误且 CLEAR_LOG 为低时直接完成准备。
3. 第一次发现错误位或 CLEAR_LOG 已高，记录一次恢复尝试，映射已确认的本地
   RW APID。只向 `0x4371` 发 low/high/low 固定命令，保留 bit1 和全部其他位。
   每步回读；high 允许硬件自清，low 必须验证为低，其他位必须保持。
4. 最多读五次 DMA_STS，显式间隔合计最多 40us；不重复 clear。错误位清除后
   才允许原有 request/grant/分区读取/释放流程继续。

固定命令不走“相同值则跳过”的稳定配置 RMW，也没有公开任意写寄存器接口。
清错期间共享控制器锁保持持有；正常 ALG/grant 等待仍释放该锁。它只串行化
XNU 本地访问，不能保证与其他执行环境互斥。

恢复失败会锁存 FG 会话失效，本次不返回数据，后续调用返回 POISONED，直到
重启。若已尝试 high 而 low 未确认，仅在 RW 未 poison 时额外尝试一次 low
清理，保留首个错误；RW 超时立即共享 poison，绝不再次使用超时通道。
无法确认 deassert 或恢复失败输出无条件 ERROR。MMIO 映射按 16K 页正确解除。

准备完成之后的新 pre 状态错误，以及读数据后的 post 状态错误，均锁存失败，
不会再次清错或静默重试。post 错误仍释放本会话持有的请求和临时选择；清理
失败保持原有 ERROR/poison 行为。初始化前的 BUSY/身份/权限失败不发清错命令。

## 日志格式

DMA 新增 `SWEET FG DMA: v3`：

- `phase=pre`：原始 sts/ctl/arb/cfg、是否已初始化、是否已用恢复机会。
- `phase=recovery`：before/after、after-ok、ctl/ctl-ok、low-confirmed、error。
  未成功读回的零值必须结合 `*-ok=0` 看待。
- `phase=post`：原始状态和字段号；明确不清错重试。

`SWEET FG ACCESS: v2` 保留，新增 `dma-pre-*`、`dma-clear-*` 和
`dma-post-status` 等操作名，便于区分失败阶段。原始诊断/恢复摘要为 INFO，
新 post 错误为 WARN，恢复/清理失败为 ERROR；沿用现有等级过滤规则。

旧 SAMPLE 约 255 字节后截断，连换行也丢失；此次只缩短驱动记录，不改全局
console。每次快照用相同 `id` 连接以下独立、带换行的记录：

| 标记 | 内容 | 输出时机 |
| --- | --- | --- |
| BATT SAMPLE v2 | source、有效位、电压/电流及有效性 | 每次采样 |
| BATT FG v3 | 温度、原始值和独立错误 | 每次采样 |
| BATT CAP v3 | nominal/learned/profile、各自有效性及错误 | 每次采样 |
| BATT TIME v3 | started-ns / completed-ns | 每次采样 |
| BATT CONFIG v3 | enable/pause/watchdog/time 原值及独立错误 | 初始/事件采样 |
| BATT PARALLEL v3 | SMB / SMB_CFG 原值及独立错误 | 初始/事件采样 |

保护 timer 不新增 USB/APSD/Type-C 状态轮询。采样、发布和 IRQ 路径未改。
按键/USB/触摸继续 IRQ-only，registry consumer 与共享 SPMI 锁结构保留。

## 本地验证与限制

- 在原生 0120a1 选定源码上严格应用 0153，24 个选定文件逐字节一致。
- 实际 transport/FG 函数的 50 个 ASAN/UBSAN 场景通过：READ/WRITE/双错误、
  自清/延迟/初始高、状态不清、三步写失败、map/readback/status 失败、超时
  poison、权限/忙/未知布局拒绝、后续及 post 错误不重清、原有会话释放等。
- 实际 runtime 采样/发布、策略、逐步写失败、停止和 100 次 PM 竞争通过。
  短记录检查包含 `[INFO] ` 和换行，覆盖最大 sample ID/64-bit 时间戳。
- mux/consumer 套件及额外 80 次生命周期竞争、旧 SPMI 套件及额外 10 次并发
  通过。`sweet_spmi.c` / `SweetBattery.cpp` 两个 ARM64 单文件编译通过，
  使用既有 CI headers 与 Linux clang 兼容参数，不代表完整原生构建/链接。
- CircleCI YAML/source gate 接受 0153、拒绝 0152；实际 `bash build.sh`
  前缀未变。打包脚本更新 marker，`bash -n` 通过。

完整底稿在 `/home/shenli/work1/scratch/fg-dma-0120a1/`，包括 base/edited/verify、
各测试输出、编译输出和 `validation-results.json`。host 模型不能证明真实 PMIC
时序、外部 EE 行为或实际充电。

## 下一轮只先验遥测

用户 push 后进行一次云编译。产物下载后先核对源码/符号/marker，再使用
`tools/mk-mh2lm-event-matrix.sh ARTIFACT LABEL --battery-runtime` 打包同核三档。
先运行 telemetry，约一分钟，USB 插拔两轮，每状态留 15～30 秒，导出持久日志
保留首次 DMA pre/recovery。无需先跑 observe/dry，亦无需重测旧 no-USB 档。

验收先看原始 sts 和清错回读，之后看四字段值/有效性/错误、完整 TIME，及
USBIN/source、按键/触摸/ACM 是否继续正常。若出现 post 错误或恢复失败，按
新阶段定位，不重复清错掩盖。温度仍未做 LG 补偿，容量/profile 不证明电池身份。

本轮不开放主动充电。实际电池身份/上限、实测 VBUS/输入能力、已证明的并联
隔离 provider 仍缺，`fLimits` 仍为空；watchdog/pet 未实现，不自动关 watchdog
绕过保护。先恢复测量，再补这些依赖和实际充电验收。
