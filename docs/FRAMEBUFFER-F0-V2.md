# F0 补充取证：0171

状态：0121a2 两档实测已分析，用户确认显示异常相同；详见 [0121a2 报告](FRAMEBUFFER-F0-0121A2.md)。
时钟区域 source/panel 同黑，模式/帧完成 ABI 仍待核对；0121a1 include 编译问题已修复。
当前仍是 F0，未进入 F1。
上一轮结果见 [0121 报告](FRAMEBUFFER-F0-0121.md)，总路线见 [显示计划](FRAMEBUFFER-PANEL-PLAN.md)。

## 本轮要回答什么

插电唤醒、大电池图标消失之后缺时间，以及关机滑条区域半透明效果异常/取消后缺时间，究竟对应什么提交记录和
扫描缓冲内容？0170 的单通道、中间区域 hash 无法回答。0171 补充证据，不改变原有呈现策略。

| 取证项 | 0171 实现 | 解读限制 |
|---|---|---|
| 最近提交 | 按时间顺序打印最近 4 份 swap：id、update(+0x104)、disable(+0x108)、4 个 surface id、layer0 目标矩形、+0x10c/+0x110 原始值 | 后两个字段先以原始偏移命名，未证明本版背景色 ABI 前不按猜测执行 |
| 实际拷贝 | 在 memcpy 完成分支记录 swap id、source id、时间戳和最终 sx/sy/dx/dy/w/h；另记尝试呈现的 swap | 指最近实际拷贝的图层；多图层时仅最后一层。不是硬件 latch 或 CA 完成 fence |
| 像素 | BGRA 四字节，横纵步长 8，排除行 padding；上/中/下三区、中央时钟 ROI、底部横条 ROI | ROI 是几何区域，不识别字形；稀疏采样不能证明所有未采到的像素为空 |
| 对应比较 | 在上次实际拷贝矩形与 ROI 的交集中，按裁剪偏移比较 source 和 panel 的同一目标像素；分开统计 RGB、alpha 差异 | panel 整 ROI 的 hash 与 pair 交集 hash 未必同范围，不能直接比；以 pair 的 diff 为准 |
| 稳定画面 | 沿用现有 vsync workloop，亮屏期间每秒一次，最多 600 次；保留 off-before/off-after/on-after | 不是新增硬件输入轮询。600 次用尽后周期采样停止，电源转换采样仍在 |
| 开销/竞争 | 每次采样记录 sample-us、copy-changed、live-sample=1 | copy-changed=0 也不能排除 CA 正在写源图。采样会增加内存读取和日志开销 |
| surface 属性 | trace 开启时，前 32 次 create 的属性最多各 64 键：键名、number/bool/string 值或对象类型；字符串限长 | 不打印任意数据 payload；不改变分配或映射属性。配合原 create 日志确定进程/几何 |
| selector | 0–127 各前 32 次参数日志，并汇总两个 64 位使用掩码 | 记录调用不等于已实现该 selector 的语义；范围外仍按原路径处理 |
| 硬件计数 | frame 改读 +0xac；打印真实 elapsed-ns、frame enable(+0xa8)、raster line(+0xb0)、tear line(+0x2b0) | 不将循环行计数相减称为 TE 频率；command mode 下 frame 计数也可能无进展 |
| SMMU | trace≥2，且 ID0/ID1、TRANS/CB2 匹配已知值时，只读 CB2 配置 | 不遍历页表，不改地址转换，不据 TTBR 值直接宣称可扫描任意新物理页 |

诊断不长时间 retain 源 surface：仅保存 id，采样时通过 surface 表取得临时引用，结束后 release。
对象已释放则打印 alive=0，保留上次实际拷贝元数据，避免为了取证改变源图生命周期。
读取前重新验证维度、stride、offset、descriptor length、ROI 和地址加法边界。

新增日志使用 `sweet_log_level.h` 的 INFO/WARN 等级；测试参数显式 `sweetloglevel=2`。
既有非 F0 日志尚未统一改造，日志大计划另行处理。

## SMMU 地址核对

不能从旧摘要中的 `0x42000` 推算 CB2。此次已对照原机 DT 和 LG 驱动重新核对：

- 原机解码 DT：`scratch/mh2lm-dt/android-mh2lm.dts:20096`，apps-smmu base 为
  `0x15000000/0x100000`；LG `msm-arm-smmu-sm8150.dtsi:67` 相同。
- ID1=`0x60000038`：pgshift=12；NUMPAGENDXB=6；全局半区大小
  `(1 << (6+1)) << 12 = 0x80000`，完整寄存器资源 `0x100000`，与 DT 一致。
- LG `drivers/iommu/arm-smmu.c:5121`：cb_base=base+resource_size/2；
  `ARM_SMMU_CB(smmu,n)` 再加 n<<pgshift。所以 CB2 为 **0x15082000**。
- GR1 是 base+0x1000，复用已有 0x2000 映射，不重复映射同一页；CB2 仅映射一页。
- 只读 CBAR/CBA2R、SCTLR/ACTLR、TTBCR2/TTBCR、TTBR0/1、MAIR。未知 ID/route 跳过，
  映射失败只给 WARN，不影响既有亮度路径；沿用探针时钟门检查与 dp_lock 串行化。
- 0121a2 已获得读数：SCTLR=0xe0/M0，TTBR0/1、TTBCR、MAIR 均0；本次 CB2 stage1
  转换未使能。TRANS route 本身不证明活跃转换，新扫描地址可达性仍须在 F4 写入前核对。

不新增 DCS、CTL、SSPP 或 SMMU 写入；现有 DCS 写入函数保持原样。

## 验证记录

- 以 0121 / 0170 四文件为基线生成 `0171-mh2lm-framebuffer-f0-evidence.patch`（包含新 helper）；
  strict `git apply --check`、apply 后与 edited 四文件逐字节一致。
- SweetFramebuffer.cpp、SweetSurface.cpp、sweet_display_probe.c：使用 CI 参数和本地
  clang 19 进行 arm64-apple-darwin 单文件编译，三个 rc=0；对照基线无新增告警类别。
  新增边界已检查的数组访问仍触发既有 `-Wunsafe-buffer-usage`，不宣称零告警或完整链接通过。
- 新 host 测试在 C11/C++17 + ASan/UBSan 下通过：不同 stride/offset/裁剪、padding 排除、
  红/绿/alpha 变化、descriptor 截断、空/越界/整数溢出输入、稀疏采样的限制。
  workspace runner 不支持 LSan，测试关闭 leak detection；此测试无堆分配，地址/UB 检查仍启用。
- 打包脚本 `bash -n` 通过；实际传入旧 0121 内核包，因缺少 v2 marker 正确拒绝打包。
- 13 个 host 测试最终全部通过；原充电测试首跑也遇到同一 LSan/ptrace 限制，
  仅用 `ASAN_OPTIONS=detect_leaks=0` 重跑该项即通过，未改其源码。
  回归测试与详细结果见 `scratch/framebuffer-0171/{tests.json,compile-report.json}`。
  这些 host 检查不能证明上机效果。
- 按用户要求未改 `.github/workflows/circleci.yml`；补丁生效由下载后的 buildtree 逐字节核对、
  新内核四个 v2 marker 和打包门检查。本轮不加 CI 配置步骤。

## 一次编译，两档取证

push 后沿用 CircleCI。用户下载新 kernel/buildtree（最好同时下载 build log）并给出编号，
agent 核对、打包、回读后提供真实镜像路径。当前没有本轮可刷的新内核，不能复用 0121 当新版本。

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-<编号>.gz <编号> --framebuffer-f0
```

两档 `fb-trace` / `fb-trace-preserveoff` 都使用 `sweetfbtrace=2 sweetloglevel=2`，
保留已验证的三键、USB ACM、触摸和电池 IRQ / runtime3 充电。第二档额外
`sweetfbpreserveoff=1`；只比较是否保留息屏像素，其他驱动参数相同。
若需区分新 CB 配置读取的影响，同一内核可用 `sweetfbtrace=1` 跳过 CB2 扩展读取，
仍保留 v2 像素和提交诊断，不需要重新云编译。

每档在周期采样窗口内做以下手动操作，并各留持久日志：

1. 启动后留亮屏约 35 秒，让只读硬件探针完成；普通亮屏/锁屏稳定画面停 5–10 秒。
2. 无充电线时，用电源键息屏，等 3 秒，插充电器；大电池 logo 消失后停 5–10 秒，记录有无时间。
3. 保持接电，用电源键息屏，等 3 秒，拔线；亮屏后停 5–10 秒，记录有无时间。
4. 唤出关机滑条，停 5–10 秒观察滑条区域半透明效果，再取消，停 5–10 秒观察时间。此项不执行关机。
5. 按显示计划累计插/拔各 5 次；每次保留稳定画面至少 5 秒，尽量记下异常发生的操作次序。
   当前没有自动息屏，不安排自动锁屏测试。

无需新增 USB 重连压力测试。USB 日志可照常使用，失败时导出持久日志即可。
若电池/按键 group 再被隔离，把发生点及其后的观察记为 [BUG-002](BUGS.md) 影响；
不要把输入已经失效后的无法唤醒当作 framebuffer 根因。本轮不改这两个独立 bug。

下轮分析需把用户描述的稳定状态与 ns 时间、copied/attempt swap、ROI 差异对齐。
F0 未验收前不进入 F1，不直接切扫描缓冲或修改 panel 序列。

## 0121a1 编译失败与 0172 修复

构建日志确认 0171 已应用；停止原因是三个新增 include 无法解析：

| 日志行 | 源码 | 错误 |
|---|---|---|
| 16504 | sweet_display_probe.c:36 | bsd/dev/sweet_log_level.h not found |
| 22940 | SweetSurface.cpp:47 | bsd/dev/sweet_log_level.h not found |
| 23034 | SweetFramebuffer.cpp:54 | bsd/dev/sweet_fb_trace.h not found |

CI 的 BSD 搜索根是 xnu/bsd，IOKit 也没有将整个 xnu 根加入搜索路径。
因此文件虽存在，`<bsd/dev/...>` 仍找不到。上轮本地编译额外加入了 edited 根目录的 `-I`，
掩盖了该问题；原“三源码通过”记录只能说明该额外环境下通过，不代表等价 CI 的 include 验证。
日志中的 TrustCache、Monterey/Sonoma 等跳过警告并非本次停止原因。

修复补丁 `0172-mh2lm-framebuffer-f0-includes.patch` 只改四条 include：
BSD 用同目录的 `"sweet_log_level.h"`，IOKit 用
`"../../../bsd/dev/sweet_log_level.h"` / `"../../../bsd/dev/sweet_fb_trace.h"`，
沿用现有 PMIC 驱动的做法。不扩大 CI 搜索路径，不修改 circleci.yml，不改诊断运行行为。

验证：移除本地额外源码根目录/arm目录搜索路径，旧三源码全部复现云端相同 fatal；
修复后三源码 arm64 交叉编译全部 rc=0。0171→0172 strict apply，四文件逐字节一致。
详细记录在 `scratch/framebuffer-0172/`；本轮无新状态机或像素算法修改，不重复无关 host 测试。
完整云编译及实机仍待完成，不宣称云端已通过。

日志 SHA256：`902da91ad88582b5c4118dd7e600d7b4689729d358a2be07b5e045580c7cb090`。
无需额外下载本次失败构建的产物；下一步是 push 修复提交后重新编译。

## 0121a2 产物与镜像（实测完成，以下保留原操作）

预期源码提交 `2bc0251`（0171+0172）。38 个核对文件与本地预期逐字节一致，
包含显示补丁涉及的全部四文件；现有 114 个 CI require_marker 通过，kernel manifest 及 v2 / IRQ /
充电标记通过。新内核 SHA 与 0121 不同。未提供本轮构建日志和 CI run ID，未查全量告警。

内核 SHA256：`5bc262e74534dbc2952a09342a0d97348561226d58a16a5a7c937ebad10966b1`。

- `xnu-buildtree-0121a2.gz`：`b309ea46c0601419c7490bd896a47ae715f2716f590d2adea4946d83e10d4ba2`。
- `xnu-kernel-0121a2.gz`：`e6cdee52f2013b4d222502e2aad9a3e23ea49ee1a635e4e0540ab5a4cd1b5c7d`。

两档镜像均在 `/home/shenli/work1/out-bs/`，各 35,667,968 字节，header v1/page 2048。

- `boot-16k-0121a2-fb-trace.img`：`172075a5072304c319e57d2a2482cbf01b035da1b4fb3a76f480750e26a5a7cc`。
- `boot-16k-0121a2-fb-trace-preserveoff.img`：`4b51c5050dfdff3b61c398a1cf72b9778a28147eea80aba974a9e358af9ba232`。

镜像整体回读：kernel blob、FAT ramdisk、内嵌 XNU、ADT、TOML 均一致；启动参数无重复。
两档同内核、同 ADT、同 IRQ/runtime3/USB ACM 参数；仅 preserveoff 档多
`sweetfbpreserveoff=1`。共同启用 `sweetfbtrace=2 sweetdispprobe=1 sweetloglevel=2`，
未启用温度 IRQ 扩展。不再需要 push 来制作本轮测试镜像。

先 `fastboot boot` 普通 `fb-trace` 档，按上文手动息屏插电/拔电、滑条及取消流程测试；
启动后先留 35 秒，异常画面停留 5–10 秒，结束后留至少 15 秒再导出持久日志。
再 boot `fb-trace-preserveoff` 档重复；分别命名
`sweetlog-0121a2-fb-trace.bin` / `sweetlog-0121a2-fb-trace-preserveoff.bin`。
没有自动息屏测试，没有新增 USB 重连压力测试。需记录稳定画面的时间显示和滑条区域半透明效果异常情况。

核对底稿：`scratch/artifact-0121a2/{verify-artifacts.py,verification.json,source-compare.json,
verify-boots.py,boot-verification.json}`。实测结果见下节，F0 未验收。

## 0121a2 日志分析结果

两档已完成，用户确认插电缺时间、拔电有时间及关机滑条/取消异常均同之前，保留像素无改善。
详见 [完整报告](FRAMEBUFFER-F0-0121A2.md)，包含日志 SHA、行号、属性表和硬件配置。

- 首次 off 后，普通44次/preserveoff45次周期时钟对照两侧RGB全0且相等，期间copy持续推进。
  不能仅归因于息屏擦除/漏拷，也不能宣布CA自身bug；source为实时样本，有复用证据。
- 普通首次 off 擦黑、on 遇空记录未copy；保留档留像素成功，后续却仍源图/扫描同黑。
  update/disable/background 的合法blank语义待核对，不直接过滤空记录。
- selector集合21种、surface11键已得到；每次亮屏selector79/block0x40当前返回全0，是模式
  ABI的具体核对线索。SwapWait/完成通知/缓冲复用需一起核对，retain不能替代所有权协议。
- CB2 SCTLR.M=0；INTF frame-en=0，TE留给F3 IRQ时间戳，不重复同类寄存器差值取证。
- 本轮未复现BUG-002；采样中位约79ms，不以诊断档评价默认动画性能。

NEXT：先离线核对本版框架ABI与帧语义，再做针对性修复。无新驱动待push/云编译；F0未验收。

## 后续本版 ABI 核对

见 [用户态 ABI 报告](FRAMEBUFFER-USERLAND-ABI.md)。关机症状已按用户澄清改为滑条区域半透明
颜色未正常渲染。原始框架确认+0x10c背景、bit31以及三层null的合法blank；CA在SwapWait成功后
推进页回收，当前假完成是实际契约缺口，修复优先于单补模式ID。模式+4/+8布局已确认，fallback
有效ID尚未闭环。SWContext有模糊实现，不能直接按缺GPU定因。尚无新的驱动/云编译版本。
