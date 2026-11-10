# MH2LM 遗留与回归 Bug 台账

更新：2026-11-10。

这里记录已有功能基本实现后，在后续使用或其他阶段测试中暴露的问题。与当前大目标的实施计划
独立维护：发现 bug 不自动重开原任务，也不自动插入 framebuffer 的步骤或成为其验收前置。
需要安排修复时单独确定范围。若故障使当次测试失去有效输入，应标明该段证据的局限。

用户于2026-11-09决定暂缓插电后锁屏缺时间，登记为BUG-003，继续显示F1架构重构。
滑条区域半透明颜色异常仍由[显示计划](FRAMEBUFFER-PANEL-PLAN.md)保留；不要求先消除
这些已知画面差异才开始F1，也不把暂缓处理写成修复或F0完整验收。

## 总表

| 编号 | 模块 | 问题 | 状态 | 影响 |
|---|---|---|---|---|
| BUG-001 | DWC3 / USB CDC-ACM | 反复插拔后 Windows 偶发无法识别设备 | 未修复；原因待定 | USB 枚举、连接与实时日志不可用 |
| BUG-002 | SPMI interrupt mux / 电池 / 按键 | drain 收尾 pending 被判永久故障，隔离 group 后失去插线或按键唤醒 | 代码已修复（0178 / 41a0efa）；后续正常使用观察，不安排专项复测 | 电池事件停止、禁充、百分比停留；按键失效后息屏无法唤醒 |
| BUG-003 | 锁屏 / 充电过渡 / 显示接口 | 插电大充电图标消失后锁屏缺少时间，拔电恢复 | 未修复；按用户要求暂缓 | 锁屏中央时间显示；不阻塞F1架构重构 |
| BUG-004 | 电池输入采样 / runtime3 | 电脑USB输入波动时，USB快照校验失败与恢复引发反复停充/启充，充电图标闪烁 | 未修复；0121a5两档同链复现，新MDSS后端非必要条件 | 充电控制及UI稳定性 |
| BUG-005 | 用户态DriverKit / Jetsam | driverkitd启动时因单进程内存上限被终止 | 未修复；0121a10/0121a11同签名，具体上限及触发原因待查 | 用户态DriverKit服务未正常持续运行；当前可见功能影响未确认 |

## BUG-001：USB 多次插拔后无法识别

**背景与发现阶段**

USB gadget（设备侧 CDC-ACM 功能）和 IRQ 收发基本可用；此问题在旧轮询驱动中已经出现，
0114a11 IRQ 测试时再次报告。用户在 0120a6 也明确指出第二次插电脑识别失败是已知 bug。
不能仅凭重连失败将其归因于 IRQ 化。

**触发与现象**

- 手机启动后连接电脑，正常枚举并使用 ACM，然后反复拔插。
- 达到不固定次数后，Windows 提示“未识别的 USB 设备”，后续 ACM 日志连接可能不能恢复。
- 触发次数、线缆/主机依赖、是否必须重启手机才能恢复，尚未形成稳定复现矩阵。

**证据与已确认项**

- 历史日志：`work1/logs/sweetlog-0114a11-usb-irq.bin`，SHA256
  `eb2274f725285048fd27e1c2acde92e68ca052443d47e1a30c1e97ae4731de47`。
  既有分析记录：初期 ACM 配置成功两次，之后有 13 次额外 reset/connect，未再成功配置 ACM；
  真实 DWC3 IRQ 的 hit/ack/rearm 已有证据。
- 详见 [USB IRQ 基线](USB-IRQ-HARDWARE-BASELINE.md)及 `NOTES.md` 的 0114a11 记录。
- 0121 普通档尾部仍可见反复 bus reset/connect 和 `ep0 cmd 0x6 status 1`，
  不能单凭这些条目确定 USB 故障根因，也不能将它们与 PMIC 隔离视为同一 bug。

**待查方向（不是已证实根因）**

旧 reset 路径对端点/传输资源的清理不完整，是既有源码分析提出的候选原因。
`ep0 cmd 0x6 status 1` 也曾出现在可工作的路径中，不能独立作为故障判据。
安排修复时需核对当时版本的 disconnect/reset、EP0 setup 重挂、端点 transfer resource 和
ACM 配置生命周期，并结合主机枚举结果判断。

**当前处理与关闭条件**

不为此重做已经通过的轮询/IRQ A/B；复现时可用持久日志继续取证，不能假定实时 ACM 可用。
关闭前应给出修复提交及实机证据，记录主机/线缆、插拔次数和每次枚举结果，覆盖首次连接、
多次重连、ACM 实际收发；确认没有引入 IRQ、按键、触摸或电池事件退化。
目前无已确认的修复或通用恢复办法。

## BUG-002：PMIC 持续 pending 后电池/按键组被隔离

**背景与发现阶段**

按键 IRQ、电池 IRQ 和 runtime3 基础充电已分别取得功能成功证据。
在后续 framebuffer F0 的 0121 普通档中，用户报告一次插线 UI 未更新，之后息屏再无法通过
插线或电源键亮屏，只能强制重启。作为已实现功能的后续故障独立跟踪，
不据此推翻此前功能成功记录；目前也没有证据证明由显示补丁直接引入。

**证据**

- 普通档：`work1/logs/sweetlog-0121-fb-trace.bin`，SHA256
  `a6fb9e52dc3b55766918e6197f5109b3f0b99c222ed35014b32daea92a05e235`。
- 对照档：`work1/logs/sweetlog-0121-fb-trace-preserveoff.bin`，SHA256
  `fd2ac55aed53ef85aa81071b661b060e5edd92eb8e38bac1283930e8aa1687e6`，此档未记录同类错误。
- 源码版本 d58e7fc / 0170；完整分析、解码行号及证据边界见
  [0121 F0 报告 §3–4](FRAMEBUFFER-F0-0121.md#3-无法唤醒直接故障链已证实)。

**已确认故障链**

1. 普通档解码第 6436 行，`battery-usbin group=1 apid=269 bit=4 status=0x10`：
   mux drain 后仍 pending，判为 stuck，隔离电池 group 1。
2. 第 6463–6467 行，电池 consumer detach；关闭充电命令成功，撤回 external/charging，
   停止有效电池事件更新，保留最后 99%。
3. 第 6618 行，`power group=0 apid=248 bit=0 status=0x1` 同样触发隔离；
   按键 consumer detach、工作线程停止，释放已按下的 HID 键。
4. 第 6635 行 display off；其后触摸、USB 和用户态仍活动，但插线和电源键的事件来源均已失效。
   持久日志未记录 panic，不能将此现象描述为已证实的全内核死锁。

**“pending 堵塞”的准确边界**

现有实现立即 drain 最多 4 遍，末尾仍 pending 就隔离相关 group。
本次没有 ACK 写失败、SPMI timeout 或 poisoned lane 日志；因此已确认的是
**pending 判定触发永久隔离与功能停止**，尚未证明某把锁堵塞或某笔 SPMI 命令卡死。
新边沿、清除传播延迟、持续真实 pending 等仍待区分。
保留像素档一次未复现，不能宣称 preserveoff 修好了这个 bug。

关联的 99%→97%：故障前最后原始 MSOC 为 0xfe（99%），下一启动首样本为 0xfa（97%）。
故障后没有有效遥测，真实消耗与电量计重估的贡献未知；暂不另立一个已定因的电量 bug。

**0121a3 后续复现（2026-11-09）**

`work1/logs/sweetlog-0121a3-fb-contract-baseline.bin`，SHA256
`4285dcace17adcea7ccaba14b2184ec4b1eef00fbb2b9776ee34cad141ac900e`。
第5281–5287行再次触发电池group1隔离，这次是APID271的Type-C RID/CC/VBUS/attach，
bit0/2/4/5、status=0x35；第5314–5319行consumer失败、禁充、撤回输入状态，保留97%。
此次未见按键group0隔离，之后仍有亮屏/触摸活动；没有据此证明全内核死锁。
实验档未复现，但两档时长/操作次数不同，不能把这项差异当0173的修复效果。
详见[0121a3显示实验报告](FRAMEBUFFER-CONTRACT-0121A3.md)。继续独立排期，不扩入显示修复。

**0121a4 第一轮再次复现（2026-11-09）**

`work1/logs/sweetlog-0121a4-fb-contract-proof1.bin`，SHA256
`d6f3989e8511b282ca546fdfe5cdf71a4bfdc2076d38a6fb0d4dbd9638f3d9ff`。
第3165行Type-C RID/group1/APID271/bit0 status=0x1仍pending，第3166–3168行隔离电池组；
第3205–3210行consumer退出、禁充并撤回输入，保留96%。之后无新电池refresh，按键group0
未隔离，后面仍有power IRQ/HID和显示提交。用户确认“插电/UI无反应，电源能唤醒”，
与组隔离边界一致，不认定全内核死锁。
第二轮无同类ERROR/WARN，不能当修复效果。详见[0121a4报告](FRAMEBUFFER-PROOF-0121A4.md)。

**0121a9 / F2再次复现（2026-11-09）**

用户报告旧充电bug再次出现。`work1/logs/sweetlog-0121a9-fb-power-dcs.bin`，SHA256
`f02beab6ad0b0d9fbf2882ba0d73ae3a8205fbd49c22c1910ba120a7df4810ca`，未回卷。
解码4690行`battery-typec-cc group1 APID271 bit2 status0x4`在drain后仍pending；
4691–4693行触发group1隔离。已有在途采样id43在其后完成；4722行电池连接撤回，
4723行disable命令回读actual0，4724–4727行consumer失败、撤回external/charging并
保留93%。之后无新电池refresh。不能把在途id43误记为隔离后已恢复电池事件。

按键group0未隔离，之后仍有power/volume-down HID和触摸；4738/4774、4896/4908、
4923/4937三组MDSS off/on均成功。该故障发生在第一条实际DCS关屏之前；启动2270行
只有幂等ON前端日志，没有对应硬件事务。不能归因于该关屏命令，也不据此完全排除负载/
配置的间接影响；无SPMI timeout或panic证据，不称全内核死锁。隔离后的插拔唤醒结果
无有效电池事件来源，不能拿来判定面板电源成功或失败。F2电源键路径证据仍有效。

此处只更新旧bug记录，不在F2里更改mux/充电策略；源码细节和显示结果见
[F2实测](FRAMEBUFFER-POWER-F2.md)。当时尚未确定pending原因；后续源码/计数诊断见下一节，BUG-002保持未修复。

**2026-11-09 独立诊断：确认软件误隔离缺陷**

用户在F2长测期间要求排查，详见[BUG-002根因报告](BUG-002-MUX-PENDING.md)。
0121a9 work88总ACK计数仅123→125，对应USBIN和CHGR；电池hit不受按键预算限制，
没有CC ACK。CC在扫描收尾才被观察到，随后直接隔离，不能称“CC清四次仍失败”。
当前`stillPending`分支不要求故障位曾被ACK，就永久撤回整个group。
实际mux源码host注入一个末尾新CC，同样0次CC ACK即隔离；正常分五批处理5个CC无隔离。
四遍均清成功后又到新事件也可触发误隔离；没有模拟锁故障、超时或poisoned lane。
因此已确定功能永久失效的软件源头；真实CC晚到是新边沿还是传播延迟仍未知，
历史每个实例不自动全部定因。ACK先arb后PMIC与LG一致，CC触发极性也匹配LG DTS。
仅新增诊断/复现工具，尚未修复内核，未改变0121a9镜像或F2长测。

**2026-11-09 修复记录：0178 / `41a0efa`**

补丁[0178-mh2lm-mux-pending-rearm.patch](../xnu/patches/0178-mh2lm-mux-pending-rearm.patch)
仅修改`drainPending()`收尾：汇总4个`drainMasks`为局部`everAcked`，将末尾pending按route
分为已ACK后再次观察到、该批次未ACK而新观察到两类；两类都不作为不可恢复故障，走既有
GIC rearm，由下一次drain处理。不会因这两类pending增加`faultCount`或隔离group。
保留四遍扫描、`ackRoute()`返回值语义、ACK失败的`disableHardware()`以及隔离调用点；
无新成员/全局状态。原有无处理、非维护、无pending的异常分支保留。

验证：0121a9源码strict apply、函数收尾以外字节完全一致；实际源码consumer suite及
80次生命周期竞态通过，包含ACK timeout仍poison并报告失败。ASan/UBSan修复回归覆盖
末尾CC、空遍后CC、四遍后的第5个CC：下一次drain处理成功、无隔离、`faultCount=0`，
按键仍可工作；host显式递送下一次IRQ，尚不能代替真机电平重触发证据。
底稿`work1/scratch/bug002-fix/{make-patch.py,verify-pending.py,pending-regression.txt,consumers.txt}`。
详细实现与限制见[根因/修复报告](BUG-002-MUX-PENDING.md#修复记录0178--41a0efa)。

**后续观察政策（用户2026-11-09决定）**

代码修复完成，后续随正常使用观察；不再为BUG-002安排专项复测、重复插拔或新一轮对照，
也不把它作为显示后续阶段的测试前置。复发时由用户反馈，再核对当时镜像版本和日志。
尚无包含0178的新内核实机证据，不标为“已实机验证修复”。真正持续活动但每次ACK成功
这一边界本轮不另做识别，保留既有风暴保护，不宣称所有硬件stuck情形均已解决。

同日0121a9长测日志未复现隔离，但运行的是修复前镜像，只计显示长测成功，不能作为
0178修复生效的证据；详见[F2长测](FRAMEBUFFER-POWER-F2.md#0121a9-长测手动息屏后正常唤醒)。

## BUG-003：插电充电过渡后锁屏缺时间（暂缓）

**决定与范围：** 用户2026-11-09明确要求“先不管他，进入后续计划，把这个异常记录”。
暂停围绕该现象的模式元数据/用户态fallback根因排查和重复像素测试，进入显示F1分层重构。
本条不与BUG-002的电池事件退出合并，也不把关机滑条半透明颜色异常认作同一根因。

**最新现象：** 0121a4第二轮中，息屏插电后大充电图标消失，后续锁屏缺时间；原本亮屏
插电也出现同样情况。息屏拔电后时间正常，取消关机后的锁屏本轮正常；不能继续将“取消
关机后必然缺时间”当固定症状。用户没有报告本轮滑条半透明颜色是否改善。

**证据：** `sweetlog-0121a4-fb-contract-proof2.bin`，SHA256
`722c8986be2b2aa8683baf90ffe5ac86dfc01fd29176f9a75cf13216639d2655`。
样本20–27、ID151→370、单调时间164.031–171.363s，源复制后的自有layer/组帧/扫描内存
回读均全黑，421200个ROI像素逐一比较、diff0，同请求ID/seq、完整覆盖、rc0。正常启动和
拔电阶段同一区域有非黑内容；不再要求为了证明该区域会有内容而重复上机。
详见[0121a4实测](FRAMEBUFFER-PROOF-0121A4.md)；早期证据见0121/0121a2/0121a3报告。

**已确认与未确认：** 这些提交的黑区域已存在于驱动收到的源快照，不能归因于后续CPU
组帧/写屏漏拷。仍未定位到SpringBoard、CA或显示接口返回值；ROI统计不识别时间文字，
不是硬件完成fence，未独立核对同版原生iPhone的完整充电过渡UI。保留证据，不宣布根因已定。

**后续处理：** F1测试将其作为现有基线，记录是否变化，不以它未消失判定架构迁移失败。
独立恢复排查时再核对用户态页面状态、显示模式/能力返回与合成选择；不强制重放旧时间。
关闭前需独立修复提交及实机插电/拔电/正常锁屏对照，证明显示恢复且输入/充电不退化。

## BUG-004：电脑USB充电状态反复闪烁

发现于0121a5 SweetMDSS档；用户确认连接电脑USB口，随后同核baseline也成功复现。
日志`sweetlog-0121a5-fb-service-mdss.bin`，SHA256
`770a8164c2d66ad75053919c4191da80344aff7df8735c23d4fd0bdf18e80b16`。
详见[实测与源码诊断](FRAMEBUFFER-SERVICE-0121A5.md)。

**已确认：** 349份snapshot，771次USBIN UV route命中；62份USB字段VERIFY(-12)，
61次因遥测无效停充，整份日志累计67次启充。id28（4238–4247行）USB0x15/-12→
ENABLE0，id29（4267–4284行）USB0x10/0→9步重新启充；原始CHGR状态亦反复变化。
不是单纯画面闪烁。策略将完整USB字节前后不一致变成SB_USB无效，并立即关闭充电；
下一次有效采样恢复启充，遥测路径不触发命令失败的5秒退避。

**baseline对照：** `sweetlog-0121a5-fb-service-baseline.bin`，SHA256
`05b5ae79eaa5e41b32e18e3a8b9b23bc65158d89dbe84d7666a0589191296e09`。
未回卷；第332行确认sweetmdss=0，旧显示路径探针和DCS工作。301份快照、61份USB
VERIFY(-12)、59次遥测失效停充、64次启充，698次UV/772次CHGR route命中；639条命令
error均0，无mux隔离/SPMI timeout/panic。样本22（3585–3594行）无效→停充，
样本23（3614–3631行）有效→重新启充，与mdss故障链一致。
因此新SweetMDSS后端不是触发故障的必要条件，不归为该后端专属回归；不证明旧内核已复现。

**0121a7复现补充：** `sweetlog-0121a7-fb-state-gated.bin`，SHA256
`6cb1c9f504fb4c67543e565dbcaec0d079e7add1b168941873b388ff83b3a449`。
未回卷，146份样本，30份USB VERIFY(-12)，30次step2/reasons0x1、32次step1，另1次
step2/reasons0x2；352次UV路由命中。解码4000行id10 usb0x14/-12→4009停充→4051恢复
启充。无mux隔离/SPMI timeout/panic；用户未察觉新问题，不能据此关闭本bug。
本轮F1b正常使用验收不包含充电完全正常的承诺；具体证据见[F1b实测](FRAMEBUFFER-SERVICE-F1B.md)。

**0121a9采样波动补充：** 同份F2日志（SHA见上方BUG-002记录）还捕获4份USB
VERIFY(-12)，其中3次在已启充时引发step2/reasons0x1，分别3890→3899、4025→4034、
4178→4187行；之后3936/4074/4225恢复启充，符合已有BUG-004链。整份日志6次启充，
另有3次step2/reasons0x2，不能把所有停充都归因于VERIFY。末段电池完全停止更新是
BUG-002组隔离，与此处反复停/启充分别记录。本轮USB源类型未确认，不假定仍为电脑口。

**未确认：** 输入UV最初原因、共用配置/负载是否参与触发、最终源端/限流/一致性策略修复。
七个电池/SPMI/mux核心文件与0121a4逐字节一致，但不能据此排除负载或时序影响。
没有mux隔离或SPMI超时，不合并BUG-002；不归因于单条USB endpoint WARN。

**下一步与关闭条件：** A/B对照已完成，无需重复；独立排期电池输入稳定性修复，
先核对输入限流/AICL与USB动态状态/协议一致性校验的边界，不阻塞显示后续计划。
修复须走charger provider接口，区分输入保护与协议快照，不隐藏UI状态、无条件忽略-12
或穿透读写寄存器。关闭需同电脑源稳定充电、真实拔插/OV/温度保护仍正常的实机证据。


## BUG-005：driverkitd启动时因单进程内存上限被Jetsam终止

**状态：未修复；2026-11-10复核日志时登记，已有旧版本证据，不是已确认0180回归。**
用户没有察觉本轮画面/输入异常，主动要求检查隐藏问题；发现daemon终止后独立记录，
不作为F3继续测试的前置修复任务。

**证据与确认原因**

- `sweetlog-0121a11-fb-vsync-gic.bin`，SHA256
  `2ecff7f99fdcdba61676b16be307c97307327daf8c50d77ad22746d9a3b6adcc`，解码769–772行：
  `CRASH driverkitd[66] sig 9`，`reason ns 1 code 0x7: (none)`。
- 上一轮`sweetlog-0121a10-fb-irq-preflight.bin`（SHA见上方伴随记录），792–795行同样
  `driverkitd[66] sig9/ns1/code7`；0180实际GIC接管不是该现象出现的必要条件。
- 已核对0121a11实际云XNU源码`bsd/sys/reason.h:88`：OS_REASON_JETSAM=1，
  `bsd/sys/kern_memorystatus.h:290`：JETSAM_REASON_MEMORY_PERPROCESSLIMIT=7；
  `bsd/kern/kern_memorystatus.c:4252`也使用该reason创建单进程上限终止原因。
  属于按单进程内存上限终止（SIGKILL），不能仅看到CRASH或FAR就判越界访问/内核panic。

**影响与未知**

该用户态服务本次未持续正常运行；当前显示/按键/触摸/USB是内核驱动，实测仍有有效事件。
具体哪些DriverKit用户态功能受影响未核实，不能据“屏幕正常”判无影响，也不能据单进程
limit原因判整机物理内存耗尽。实际内存上限、当时footprint、限制是否配置不匹配、是否存在
异常增长均未知。launchd还记录Jetsam opt-in EINVAL，但尚未证明两者的直接因果链。

**后续处理/关闭条件**

独立排期核对用户态crash/Jetsam报告、launchd job内存限制、进程footprint与本内核的memory
limit兼容路径；不直接全局关闭Jetsam或扩大所有进程限制掩盖原因。关闭需实机证明driverkitd
可持续运行及相关服务不退化。当前只记录，不增加新一轮设备测试或阻塞显示后续步骤。
底稿与源码定义位于`work1/scratch/artifact-0121a11/`，详见[F3 GIC实测](FRAMEBUFFER-VSYNC-F3.md#10-0121a11-gic档实测父线投递通过伴随旧异常另记)。


## 维护约定

新问题追加固定编号，至少记录发现版本、现象、影响、证据、已确认项、待核实项与关闭条件。
状态按“未修复 / 修复待验证 / 已关闭”更新；提交修复不等于实机验证完成。
与已有条目同因时补充复现记录，避免在每一轮主计划中重复建立同一个任务。

**0121a10 / F3预检伴随记录：** `sweetlog-0121a10-fb-irq-preflight.bin`，SHA256
`504d8167c635b24fbc8bea4385ba1fda9eb0c0692bf2805b58d12325bcba46b3`。
id10/17有两份USBIN VERIFY(-12)（3188/3471行），此前已分别step2/reasons0x2停充；
随后step0/reasons0x1保持禁用，3238/3521行重新启充。不是两次从启充态被遥测主动停充；
用户确认无新增问题，未报告充电图标闪烁，不能称完整UI闪烁复现或BUG-004已修。
另有10条USB端点命令WARN/10次reset/2次ACM配置，不据此认定BUG-001主机枚举失败。
本轮无mux隔离/SPMI timeout，旧问题独立记录，不追加充电或BUG-002专项测试。


**0121a11 / F3真实GIC档伴随记录（2026-11-10）：** 日志SHA见BUG-005。
7条USB WARN（ep0六/ep5一）、7次bus reset，1549/2471两次ACM配置；3215行起后续5次reset
未再见ACM配置，作为BUG-001相关观察保留，缺主机枚举反馈，不据此确证枚举失败。
id10/18 USBIN VERIFY(-12)（3202/3737行）仍出现；3109/3643先因reasons0x2无输入禁充，
3210/3745为step0/reasons0x1保持禁用，3252/3790重新启充；不是两次-12直接从启充态主动
停充，不称完整BUG-004图标闪烁复现或已修复。用户明确未察觉异常；37样本/36refresh均
healthy1，无mux隔离/poison/SPMI timeout/panic，不增BUG-002专项测试。


**0121a11 / RD_PTR计帧档伴随记录（2026-11-10）：**
`sweetlog-0121a11-fb-vsync-rdptr.bin`，SHA256
`6b1727bbe355831f7355658ef41a9da2191f2854985da98992aee6a87baf5a5f`。
无mux隔离/poison/SPMI timeout/panic；61样本/60refresh healthy1，用户确认无新增问题。
USB两次reset均配置ACM，3端点WARN，VERIFY(-12)本轮0；不能据此关闭BUG-001/004。
787–790行driverkitd仍sig9/ns1/code7，BUG-005未修复。另61份gauge均charging0，电流-132..0mA；
只见启动一次ENABLE成功，用户确认接电脑USB；不据healthy1/百分比81→82宣称真实充电。
这条观察与VERIFY导致反复停/启充有区别，当前不合并定因或判MDSS引入；记录后继续显示F3。

**0121a11 / 完整硬件vsync档伴随记录（2026-11-10）：**
`sweetlog-0121a11-fb-vsync-hardware.bin`，SHA256
`9839ade3d47d58ee5ca2396ad73f77729c4d66302a5013b127e77f0dcae06145`。
用户未发现异常，F3正常使用路径通过；无mux隔离/poison/SPMI timeout/panic，不追加BUG-002专项测试。
USB两次reset/两次ACM配置、3端点WARN、USBIN VERIFY(-12)为0，BUG-001/004仍未关闭。
785–788行driverkitd仍sig9/ns1/code7，BUG-005未修。52样本/51refresh healthy1，但全部gauge
charging0、电流-132..0mA，只见启动ENABLE一次；本档供电来源未单独确认，不据healthy1
宣称充电成功，不沿用上一档供电来源或判MDSS因果。采样健康与实际充电状态分别记录。
详见[F3完整档](FRAMEBUFFER-VSYNC-F3.md#12-0121a11完整硬件档f3正常使用路径验收通过)。
