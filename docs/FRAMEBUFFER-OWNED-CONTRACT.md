# CPU framebuffer 自有快照契约实验（0173）

2026-11-08。状态：0121a3产物核对、两档boot打包完成，**尚未上机或验收视觉效果**。
接续[原版帧契约核对](FRAMEBUFFER-SWAP-CONTRACT.md)和[0121a2 实测](FRAMEBUFFER-F0-0121A2.md)。
F0 尚未验收；这是独立行为实验，不代表 F1/F3/F4 已完成。

## 目的与边界

旧路径的 SwapWait 返回成功后，timer 仍会读 CA 的源 surface；retain 保对象但不保像素。
本实验先修正 CPU 后端的源读取所有权，检验它与唤醒缺时间、取消关机后缺时间是否相关。
同时实现持久 layer0、背景和合法 blank。关机滑条的症状是区域半透明颜色未正常渲染，
不是几何重叠；本轮不预设它与源复用同因，也不擅自加入 alpha 混合。

开关为 `sweetfbcontract=1`，只在 MH2LM 解析，默认关闭；`0` 完全走原有提交/定时拷贝路径。
初始化失败明确拒绝启动实验后端，不静默 fallback。
不改 CI、用户态二进制、MDP/DSI/SMMU 配置、按键/触摸/USB/电池驱动。

## 实际架构

```text
AppleCLCD : IOMobileFramebuffer
 ├─ IOMobileFramebufferUserClient
 │   ├─ SwapBegin / End / Wait / Cancel / CancelAll
 │   └─ 外部调用线程：执行源快照、CPU 组帧和扫描内存拷贝
 ├─ SweetFBContract : OSObject（provider 持有，不是独立 IOService）
 │   ├─ 原 AppleCLCD IOWorkLoop + 新 IOCommandGate：串行化状态/所有权
 │   ├─ 64 项记录：client、64 位序号/时间、32 位 ID、状态、waiter pin
 │   ├─ fLayer：当前 layer0 自有像素
 │   ├─ fScratch：下一提交的暂存像素，验证/复制成功才替换 fLayer
 │   └─ fFrame：背景与 layer0 组成的完整面板画面
 ├─ 原 60Hz timer：实验档仅发送原有软件 vsync 通知，不读源、不写像素
 └─ 原 ABL framebuffer 映射：最终 CPU 拷贝目标，MDP 继续扫描
```

没有新 worker；提交线程是与 workloop 独立的执行者。gate 内只校验、切换所有权和发布状态；
`kernelAddress()`、源复制、整帧组装和写扫描内存均在 gate 外。一个 active 作业，后续提交
用 commandSleep 有界等待，不覆盖进行中的读取。用户客户端在实验档显式设置
DefaultLocking=true、SingleThreadExternalMethod=false、SetProperties=false；XNU8796
`IOUserClient::callExternalMethod()` 因而取共享读锁，等待者不独占挡住提交者。
来自本 workloop 线程的同步调用返回 CannotLock，避免等待只有同 loop 才能生成的事件。

输入先复制到自有内存，成功后 gate 内 commit；释放源引用后，只读自有像素写 panel，
最后才发布 source-released。这个节点比纯源复制结束更保守；仍然**不是硬件整帧显示完成**。
源归还后，息屏恢复/背景变化/未更新图层重放均不会重新读 CA surface。

## 支持的输入及等待

- 支持单层 layer0、BGRA/4字节/1×1元素、无缩放、严格源范围和目标裁剪。
  支持其它层的 disable；其它启用层、缩放、非 BGRA 等返回 Unsupported，保持旧画面。
- update 只改所指图层；没有更新的 layer0 和背景保持。disable 撤去图层，bit31 更新背景。
  背景必须不透明；全 disable + 黑背景真实输出 blank，不重放旧源。
- 64项中 reading、当前缓存场景、display ID、waiter pin 不能回收；可回收终态选最旧序号。
  ID 分配跳过0和仍保留的同 ID。容量耗尽返回 NoResources，重复 End 不当成新帧。
- options0：既不 reading，也不是当前 display ID；options1：不 reading；options2：整个
  active swap 队列没有 reading，不按目标 ID。最高位 noBlock 条件未满足返回 Busy。
- options3/4 对应固定 boot canvas 配置 ready 的正/反条件，不等同背光或物理面板电源。
  这是 CPU 后端的配置状态映射，不声称恢复 Apple 完整配置状态机；失败返回 BadArgument。
- ID0由 options 决定。未知/已回收 ID 的0/1按未命中处理；保留的 FAILED 返回原错误，
  CANCELLED 返回 Aborted，跨 client 查有效 ID 返回 BadArgument。这些错误策略比原生正常
  未命中分支更严格，是实验兼容边界。
- 普通第三 scalar=0，实验默认等1秒；非0暂按毫秒解释，上限10000。
  原生非零 timeout 的完整单位/恢复策略尚未核实，不声称完全 ABI 兼容。

CPU 后端在 panel 拷贝结束后发布 display ID；新帧完成、off 或取消时退出旧 display ID。
显示中的快照与源释放是两种状态；options0可以 Busy 而 options1已经成功。
本实验没有真实 MDSS/vsync fence，不能证明消除撕裂。

## 取消、关闭、停止

同版 framework 新核对入口：

| 入口 | wrapper 地址 | selector / scalar |
|---|---|---|
| kern_SwapCancel | 0x1af227a14 | 52 / 一个 swap ID |
| kern_SwapCancelAll | 0x1af227a7c | 81 / cutoff 的 high32、low32 |
| kern_GetCurrentAbsoluteTime | 0x1af229264 | 77 / 一个 scalar output |
| kern_SwapSignal | 0x1af227924 | 20 / 两个 scalar，实验明确 Unsupported |

原生 dispatch table=0xfffffff006decb58，每项24字节。
52项=0xfffffff006ded038，入口0xfffffff005bffa9c；81项=0xfffffff006ded2f0，
入口0xfffffff005bffad8，能观察到 high32/low32 拼接后走 provider 虚表+0x6c8。
77项=0xfffffff006ded290，入口0xfffffff005c01228，经0xfffffff005bff620到 provider
虚表+0x778，写回64位输出。这两条 provider 的完整虚表/原生 cutoff 生成语义仍未恢复。
底稿 `scratch/framebuffer-abi-0121a2/{cancel,cancel-all,signal,cancel-all-ca,kernel-cancel-52,kernel-cancel-81}.txt`。

实验77返回 mach_absolute_time，Begin 使用同一时基 stamp；81仅取消该 client 在 cutoff
之前或当时 Begin 的记录。这是显式实验策略，不宣称完全复现原生时间语义。不能保持旧77
恒返回0却给81制造假的有效截止时间。

取消 reading 仅标记，然后 commandSleep 释放 gate 等读者退出；超时返回错误，绝不假称源已归还。
commit 之前取消不改变画面；commit 之后取消不会回滚已写入/正在输出的自有像素，但撤销该 ID，
仍等读者和输出退出才能取消成功。关闭只取消所属 client，不影响其它 client。
客户端 close 标记后，已进入调用经 gate 检查并退出；IOUserClient 调用生命周期仍由 XNU 管理。
stop 先移除 timer、撤掉可取得的新 helper 指针，随后 gate 内 stopping/ready=false，取消 active，
唤醒等待者，等所有已进入调用退出后释放 helper。每次调用独立 retain helper；源、自有三缓冲、
gate及 helper 对 workloop 的引用都在安全退出后释放。setup 未挂入 gate 时的失败也能清理。
原 provider 的其它旧资源生命周期未在这轮整体重构。

## 性能与日志

1080×2340×4×3=30,326,400 字节，约28.9 MiB普通缓存内存；不是三块新硬件扫描缓冲。
每帧多次 CPU 复制且提交同步，可能降低帧率/增加延迟。本轮不受 sweetfb_hz=30 的旧 timer
拷贝节流控制，不能用它衡量最终 F4 零拷贝方案性能。

sweetfbpreserveoff=1 时保留 panel 像素；唤醒重放自有快照。未开启 preserveoff 时实验档
立即擦黑，不模拟旧7tick fade，因此正式对照两档都设置 preserveoff=1。
实验档强制关闭 live-source trace，避免源归还后又由诊断读它；不启用每秒约79ms的重采样。
新增日志使用现有等级：错误始终输出；INFO=2。常规 reading/source-released/wait 日志前96条，
power/blank/cancel另有64条预算，避免早期提交耗尽所有唤醒记录。ERROR无此次数限制。

## 已做验证和下一次实测

- 严格应用到0121a2产物的0172后基线，三文件与 edited 逐字节一致。
- 基线和修改后 SweetFramebuffer.cpp 均用 CI 参数的 arm64 单文件交叉编译通过；无新增告警
  类别，并非零告警。没有额外 XNU 根目录 include，也没有改 CircleCI。
- 14个 tools/tests 主机测试通过。新测试 include 真实 helper，覆盖像素归还后源改写与唤醒、
  部分 update、背景、blank、拒绝输入不改画面、等待/超时、两个提交背压、取消活跃读取、
  cutoff、close/stop、ID回卷/表容量、分配失败清理、stride/offset/padding/溢出。
- ASan/UBSan通过；LSan在本环境报 ptrace fatal，**未通过泄漏扫描**。detect_leaks=0 后运行，
  fixture 中引用对象/缓冲分配计数均归零；不能据此代替真实 XNU 调度或完整泄漏检查。
- 底稿：`scratch/framebuffer-0173/{verification.json,tests.json,*.compile.txt,*.test.txt}`。
- `bash -n`通过；打包脚本拒绝缺0173 marker的旧0121a2内核，尚无真实新内核/镜像。

用户 push 后下载新 kernel/buildtree，先核对三文件、marker和构建日志，再打包：

```sh
tools/mk-mh2lm-event-matrix.sh /home/shenli/work1/logs/xnu-kernel-编号.gz 编号 --framebuffer-contract
```

产出名字为 `boot-16k-编号-fb-contract-baseline.img` 和
`boot-16k-编号-fb-contract-owned.img`，在 work1/out-bs。0121a3已生成，核验记录见下节。
两档共用同内核和已验证 IRQ/电池 runtime3 参数，只差 sweetfbcontract=0/1，
均 preserveoff=1、trace/probe=0、loglevel=2。

先对照，再实验；每档留持久日志，USB日志可同时使用：

1. 正常进入桌面，检查按键、触摸、状态栏/充电变化。
2. 手动息屏→插电，等大电池图标消失，记录是否显示时间。
3. 接电时手动息屏→拔电，记录时间；插拔各做3次，每次等画面稳定。
4. 唤出关机界面，观察滑条区域半透明颜色；取消后观察时间。无需真正关机。

本机没有自动息屏，不安排 auto-lock。若发生已知 mux 隔离/USB 重连故障，按
[独立BUG台账](BUGS.md)分开判断。两档视觉无差别也有价值，不能因此静默判成功；
需结合失败/Unsupported/等待日志决定继续模式契约核对还是正式拆分显示后端。

## 0121a3 产物与镜像记录

预期实现提交6c2e77d。40个源码文件逐字节匹配，114个CI源码marker及实验内核marker通过；
内核manifest匹配，SHA256为`513d16fa31f746cac23573ba1ac87343a0ffe03c361cf7eb0835b4c44f04a8c8`。
本地未收到构建日志/CI run ID，不声明已查全量编译告警。

两档均在`/home/shenli/work1/out-bs/`，35,667,968字节：

| 文件 | SHA256 |
|---|---|
| boot-16k-0121a3-fb-contract-baseline.img | 33ee5f10df8230b6071cc4c9c5ab689334b239eaf56cf97685cc2398d88ef30c |
| boot-16k-0121a3-fb-contract-owned.img | 67e8a96ea29c57e8e0c59d63d5d999e508d010e15096fd8ad0cbe3f48a70c085 |

从最终boot回读Android v1 header/FAT；kernel blob、kernel payload、ADT、TOML逐字节核验通过。
启动参数没有重复，两档仅sweetfbcontract=0/1不同。底稿在
`scratch/artifact-0121a3/{verification.json,source-compare.json,boot-verification.json}`。
依上节顺序测baseline再owned，各留`sweetlog-0121a3-fb-contract-baseline.bin`、
`sweetlog-0121a3-fb-contract-owned.bin`。尚未上机，不将打包完成记为视觉修复验收。
