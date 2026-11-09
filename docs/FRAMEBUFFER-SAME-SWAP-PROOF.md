# 同一 swap 的 CPU 像素取证（0174）

2026-11-09。状态：0121a4两轮实测已分析，见[结果](FRAMEBUFFER-PROOF-0121A4.md)；
已捕获新源ROI全黑的同ID证据，**不是视觉修复，F0 未验收**。
接续 [0121a3 实测](FRAMEBUFFER-CONTRACT-0121A3.md) 与
[自有快照实验](FRAMEBUFFER-OWNED-CONTRACT.md)。用户追问能否证明提交内容本来没有时间，
因此优先补同 ID 的像素证据。实测后用户决定暂缓，现按[BUG-003](BUGS.md)保留，
进入F1分层重构，不继续本专项模式元数据排查。

## 为什么补这一轮

0121a2 读的是采样时 source 的当前内容，source 可能已经复用；0121a3 关闭了像素采样。
两轮都不能将“当前 source 为黑”解释成“那次提交进驱动的像素为黑”。0174 在0173已经
复制到自有内存后，关联该次 swap 的请求、有效 layer、组帧和写屏回读，避免重新读取 CA source。

## 开关与执行位置

补丁为 `xnu/patches/0174-mh2lm-framebuffer-same-swap-proof.patch`，仅修改
`SweetFBContract.inc` 并新增 `SweetFBProof.h`。`sweetfbproof=1` 默认关闭，依赖
`sweetfbcontract=1`；旧路径没有新增采样。开启 INFO 日志需要 `sweetloglevel=2`。

提交线程先把源 rect 复制到自有 layer，释放源引用，再组帧、写原扫描内存、回读 ROI。
读回和统计在 command gate 外执行，但当前 job 在 Finish 前仍占据 active 槽，后续提交不能
复用这些像素。回读借用空闲 scratch，不新增缓冲分配；下一份 layer rect 在提交前完整覆盖。
日志元数据和统计保存到栈 job，Finish 后才输出，不再读取已解除保护的队列 entry。
stop 仍等待调用退出才释放缓冲，不修改既有源释放/等待语义。

每次真正 off→on（含首次 on 请求）打开30秒窗口，通常每秒最多一次，on 的缓存重放和窗口
首份 swap 可立即各采一次。每窗口最多32次、全程最多256次、最多12窗口。重复 on 请求也受
每窗口上限约束；没有新增 timer 或轮询线程，只有提交/电源调用触发观察，**没有新提交就没有新样本**。

ROI 为 `(width/6,height/12,width*2/3,height/4)`；1080×2340 时为
`(180,195,720,585)`，完整检查421200个像素，没有稀疏步长。该区域针对时间位置，
不单独覆盖或解释关机滑条区域的半透明异常。

## 日志字段与判读

启用标记为 `SWEET FB PROOF: v1 enabled`。每个 sample 输出三条 INFO：

1. 请求 ID/64位 seq、window、kind=swap/replay、scene/layer 的 ID与seq、surface、
   update/disable、fresh-layer、源复制起止时间、预约/观察时间、cost-us、最终 rc。
2. ROI、源/目标 rect、背景色，以及 layer 在 ROI 有效交集中的像素数、RGB非黑数、
   alpha=0/中间值/255 的数量和按32位像素计算的 FNV hash。
3. 完整 scene 和扫描内存回读 panel 的同类统计，以及逐像素 RGB/alpha 差异计数。

layer 仅统计有效 rect 交集，未覆盖区域不读旧数据；`layer pixels=0` 可能是禁用 layer 的
合法 blank，不能解释成 CA 没有画时间。`fresh-layer=1` 只表示本请求更新了 layer 状态，
还须结合 layer ID、disable、coverage 判断是否有新源像素。

| 观察 | 能支持的结论 |
|---|---|
| kind=swap、rc=0、fresh-layer=1、layer/scene ID与seq均对应本请求，layer 完整覆盖 ROI，layer/scene/panel 的 rgb 都为0、diff=0/0 | 本次源复制得到的该 ROI 的 RGB 全黑，组帧和扫描内存回读同黑；不能用这次 CPU 漏拷解释 |
| layer 与 scene 都有非黑像素，diff 非0 | 写入后的扫描内存回读与本次组帧有差异，需查写屏/内存路径；ERROR 始终输出 |
| kind=replay 或 layer 的 seq 旧于请求 | 证明自有缓存的重放/复用内容，不能当成新提交的源像素证据 |
| layer 未完整覆盖 ROI、blank、rc 非0 | 单独分析，不能用于上面的“新提交 ROI 全黑”结论 |

`rgb=0` 是全区域检查结果，hash 只辅助关联；`diff` 来自逐像素比较，不仅比较 hash。
非黑像素可能是别的控件，不能据其存在断言时间已画出；黑色文字也不能由 RGB 全黑识别。
即便符合第一行，结论仍只到**复制期间送入驱动的像素**，不能定位是 SpringBoard 还是 CA
决定了该内容，也不能直接认定 iPhone 正常充电过渡界面应该显示时间。

这里的 panel 是 CPU 写后的扫描内存读回，**不是 MDP/DSI 硬件完成 fence，也不是玻璃上的像素**。
没有改变 MDP 地址、面板命令、SMMU、刷新中断或混色策略。采样有 CPU/读内存开销，实机
耗时待看 cost-us；不能承诺比旧版约79ms采样快，或把开取证时的性能当默认性能。

## 本地验证

- 从0121a3实际源码生成0174，严格应用并与编辑后两文件逐字节比对通过。
- 基线和修改后使用真实 CI 参数 arm64 单文件编译均 rc=0，无新增告警类别（并非零告警）：
  1326→1331，增加5条未注解指针/数组访问的 `-Wunsafe-buffer-usage`，format告警仍为2条；
  尺寸/rect/实际分配长度由入口校验，未通过屏蔽告警处理。
- 实际 helper/驱动 host 测试覆盖预算边界、完整 ROI 单像素变化、部分覆盖、源复制后被改写、
  重放不再读源、scratch复用、blank、单像素回读差异、stop等待读回与最终 Aborted 记录。
- 14项 host 回归通过，ASan/UBSan 通过；LSan 受本环境 ptrace 限制未通过，运行时关闭
  detect_leaks，fixture 对象和原始分配计数归零。
- 打包脚本语法检查通过；旧0121a3内核被新 proof marker 检查正确拒绝。

底稿：`scratch/framebuffer-0174/{verification.json,tests.json,*.compile.txt,test-*.txt}`。
这些检查不能替代完整云编译和实机验证。

## 上机：一个取证档

新 kernel/buildtree 下载后先核对补丁、marker及manifest，再由 agent 打包：

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-proof
```

仅生成 `fb-contract-proof`：保留既有IRQ/电池runtime3参数、preserveoff=1、trace/probe=0、
loglevel=2，附加 `sweetfbcontract=1 sweetfbproof=1`。0121a4镜像已生成，记录见下一节。
无需重测0121a3的对照档。

1. 开机正常锁屏先停5–10秒，记录是否显示时间，保留正常场景参照；随后手动息屏→插电，
   大电池图标消失后停5–10秒，观察时间。
2. 接电时手动息屏→拔电，停5–10秒，观察时间。插/拔各2–3次即可。
3. 最后一次唤醒30秒内唤出关机界面，观察滑条区域半透明颜色，取消后观察时间。
4. 导出 `sweetlog-编号-fb-contract-proof.bin`，同时反馈视觉表现与冻结/明显延迟。

本机无自动息屏，不测 auto-lock。BUG-001 USB重连和BUG-002 mux隔离继续独立记录。
若该窗口内无提交，日志缺样本本身不能证明用户态没有渲染，需结合调用链再判断。

## 0121a4 产物与镜像记录

预期实现提交 `d0f9555`。41个源码文件逐字节匹配，包含0174两文件；114个CI源码marker和
新内核proof marker通过，kernel manifest核验通过。kernel SHA256为
`692153135ed2286b9e39cddcbce7f2079cb323afadb069e767a255f0f20b71ca`，与0121a3不同。
本地没有本轮构建日志/CI run ID，不声明已查全量编译警告。

镜像 `/home/shenli/work1/out-bs/boot-16k-0121a4-fb-contract-proof.img`，35,667,968字节；
SHA256：`854a82bfc8f942f20f8145fc3e4f21b6a2adb38e94ec5644539c45b8cb48bc06`。
从最终boot回读Android v1 header/FAT，kernel blob、XNU、ADT、TOML逐字节匹配；启动参数
无重复，contract=1/proof=1及上节公共参数符合预期。底稿在
`scratch/artifact-0121a4/{verification.json,source-compare.json,boot-verification.json,verify-*.py}`。

实际收到 `sweetlog-0121a4-fb-contract-proof1.bin`、`sweetlog-0121a4-fb-contract-proof2.bin`，
已分析，见[实测报告](FRAMEBUFFER-PROOF-0121A4.md)。第二轮关键连续样本满足同ID/新layer/
完整覆盖/rc0且layer/scene/panel同黑、diff0；F0仍未验收。第一轮电池组隔离已补BUG-002。

用户追问正常时间是否捕获：0121a2首次off前普通118.052s/preserveoff145.307s已有ROI非黑样本
949/1007点，拔电249.168s为1044点，均源/扫描差异0；但没有像素图或逐秒视觉对应，不能认定
这些就是时间字形。ROI依据上方中央位置作几何估计，未实证确定实际时间控件边界；本轮正常
锁屏对照也只能增强关联，单凭非黑数量不能证明时间完整处于ROI。

实测限制：部分第一条长日志在255字符截断，缺rc不能补0；后续保留该取证时拆短并前置结果，
不单为此重编译重测。第二轮可用时间对采样中位45.568ms，不能把截断cost解读成4ms；
关机组合键发生在最后像素样本之后，本轮没有对应半透明/取消后的独立像素证据。
