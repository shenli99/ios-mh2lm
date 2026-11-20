# 批次2：所有权收口工作记录

基线：830fa52；批次1记录为 60e1906，2a释放边界为66b7f82。执行顺序与验收门禁见 [固定计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)。本记录只说明本地进展，尚未实机通过。

## 2b：worker 的 pad 中断寄存器写入

| 路径 | b22 原写入者 | 当前 delegated mode3 | 保持的时序 |
|---|---|---|---|
| claim/register | TLMM路由配置，worker后配触发 | 仍只由TLMM认领路由，记录detect | 不提前触发配置到claim，不加普查 |
| arm准备 | worker INTR_CFG低8位配置和清旧status | AGPIO::prepareInterrupt，控制器核验持有者、注册slot、masked、线程上下文、island | 在worker原arm位置执行，仍不开pad/summary |
| arm使能 | hook resume后worker再次写ENABLE | 仅句柄resume写ENABLE；worker通过句柄读回 | 原resume位置及b22整线门控保留；不再有第二个裸写者 |
| 正常ACK | delegated ack hook，缺hook则裸写 | 只允许句柄ACK；缺hook拒绝操作 | 不提前消费deferred锁存 |
| fail | worker直接清ENABLE | 仅句柄mask | 仍先发布失败状态，再mask |
| park/quiesce | worker直接清ENABLE并调用mask hook | 句柄mask、句柄CFG读回 | 原drain预算、GIC观测及显示fence不变 |
| resume验证 | worker读CFG、旧arm重配 | 句柄读回与prepare | 原TCM protocol和几何检查不变 |
| worker退出 | 退出时不总是mask | delegated退出时要求句柄mask | 不留下失去worker的可写pad |
| service stop/free | 清钩子后取消delegated标记 | 标记保持delegated直到重启，钩子撤销后不恢复裸写权限 | teardown并发排空尚未声称解决 |

prepare 的触发类型来自注册slot；支持high/low/rising/falling/both，低8位输出分别0x72/0x70/0x76/0x74/0x7c，保留高位。prepare禁止IRQ上下文、未持有/未注册、仍unmasked和island拥有的pad。mask/resume/ack在IRQ可用的检查中也拒绝运行期EGPIO接管，不拿睡眠锁；mask必要时仍关summary但不写island pad。

## 本地验证

- TLMM真实控制器测试覆盖prepare在claim之后仍不开线，五类触发编码、高位保留、锁存清理、未注册/实时island/IRQ上下文/live拒绝。
- 真实C worker + SweetTouchPanel测试在C MMIO边界加入断言：delegated worker若写gpio122 INTR_CFG/INTR_STATUS立即失败。覆盖arm/ACK/fail/park/resume和撤销服务后的取消。句柄模型走独立writeReg边界，不代替真实TLMM控制器测试。
- 测试重启帮助函数现在清理所有hook与delegated标记，避免上一用例遗留状态伪装后续mode2/3。
- 触摸、TLMM、panel-lifecycle、display-service回归通过；ARM64编译曾暴露IOPlatformExpert不完整类型，已加真实头文件并加fixture源码守卫。TLMM、SweetTouchPanel、sweet_touch_tcm三个TU均复编成功，CI marker151/0，diff --check通过。
- 对比2a提交逐函数验证claimSummary/dispatchSummary/setSummaryMasked/maskSummaryLine/unmaskSummaryLine字节不变；显示生命周期及GenericInterruptController执行代码未改。
- 本轮没有修改GIC enable读回策略、等待心跳、summary门控、claim开线时机、显示生命周期、固件或ADT。

## 明确保留的风险/未完成事项

1. delegated arm最后仍检查ENABLE读回，正常首包可能在读取前将其mask。没有把批次3的state2容错夹进本批。新句柄迁移不应因测试只跑静态时序而被称为实机稳定。
2. 软件探针pending写入已在2c收回summary owner（见下）；探针整体仍沿b22的enable/mask与读回顺序，不证明软件注入会制造某个pad的硬件status。
3. pad dispatch快照的注销排空已在2d实现并本地验证（见下）。service全局worker钩子已在2e独立排空，具体合同见下；不能据此宣称任意外部调用者都可无视owner序列化约束。
4. 新prepare与island检查不保证能与固件瞬时抢占进行跨域原子仲裁，仅尊重可观测所有权；不自动清扫其它pad。

以上事项须在批次2总审查中列明处置；2b为批次2内部提交，当前不要求CI/刷机，也不推进批次3生产代码。


## 2c：软件探针 pending 写入归 summary owner

- AGPIO::setProbePending(bool) 请求summary控制器执行ISPENDR/ICPENDR写入；仅允许线程上下文、真实持有者、已注册slot、映射可用且只有一个已注册客户端。
- true建立探针预留，重复true拒绝；false仅允许同slot已建立的预留。预留期间新增客户端在任何pad寄存器写入前被拒绝；注册发布与预留检查通过fLock/fIrqLock串行化。
- 注销清理该slot遗留的预留并由owner清pending，不让新owner继承软件注入。
- worker mode3仅通过service/AGPIO钩子请求pending操作，缺hook报错或返回false；mode2保留原始写入。原探针enable/mask、等待预算和pending/active读回不变；不增加全pad扫描。
- C worker MMIO测试边界进一步禁止mode3直接写GICD ISENABLER/ICENABLER/ISPENDR/ICPENDR；通过钩子的单客户端操作通过，撤销后拒绝；并不代替真实GIC硬件探针验收。
- 真实控制器测试覆盖单客户端成功、IRQ上下文拒绝、无预留清除拒绝、重复注入拒绝、预留期间注册拒绝且pad不变、多pad拒绝，以及handle遗忘结束探针时注销清理。
- TLMM、触摸、panel-lifecycle、display-service四套回归通过；TLMM/SweetTouchPanel/C worker三个ARM64 TU通过；marker151/0与diff --check通过。

2c没有修改summary claim/dispatch门控或显示执行流程；只是确定共享pending写入者及诊断准入边界。批次2仍剩在途callback的注销排空协议。当前不要求CI/刷机、不启动批次3。


## 2d：注销等待 pad dispatch 快照排空

- 派发在第一次接触summary硬件前，于fIrqLock下为每个有效slot快照增加在途引用；引用保持到回调、ACK判断及summary尾部都结束。无可进入slot的dispatch直接返回。
- 线程注销先将slot标记retiring并置client mask，阻止新快照；mask pad后，在同一IRQ锁下检查在途引用与assert_wait。恢复中断、释放spinlock后才thread_block；dispatch尾部归零并唤醒，不丢block前到达的完成。
- 等待中保留handler/refcon、slot和pad所有权。retiring时resume拒绝；待全部快照完成后才能清slot和归还pad。无“超时即允许释放”分支；handler必须遵守IRQ中不阻塞的合同。
- 注册/注销经独立线程mutex串行，避免最后一slot注销summary与新注册交错。IRQ上下文注册/注销明确拒绝；typed AGPIO::put在拒绝时保留handle/reference，必须由线程重试。生命周期不得经OSObject基类强行释放绕过typed put合同。
- 控制器stop先关闭注册，按slot排空，再注销summary和失效handle。此证据针对已进入pad dispatch快照的回调；上游GenericInterruptController在对象销毁前的vector/target寿命仍是平台生命周期前提，不能把这里的测试当成完整GIC控制器热拔插证明。
- 测试用真实控制器和两个线程，分别hold正在执行回调、hold同一快照前面的回调以注销后续slot、在hold期间停止controller；每种再覆盖完成先于thread_block的早到wake。注销等待时旧owner保留、resume拒绝、锁存保留；解除hold后才允许新owner注册。无sleep轮询。
- 在2c原实现上移除IRQ拒绝用例以隔离排空检查，线程测试于“回调未结束却注销返回”的断言失败；2d通过同一交错。另测IRQ disable/put拒绝及引用保留。
- TLMM、touch、panel-lifecycle、display-service四套回归PASS；三个ARM64 TU通过；marker151/0；diff --check通过。summary的整线门控规则未改，dispatch多了生命周期引用记账，未开启叶子化或显示流程更改。

第2批总审查接下来需完成触摸service全局worker钩子的在途调用保护，并检查与stop/free的顺序。2d不单独要求CI或刷机；仍未启动第3批生产修正。


## 2e：service 的 worker 钩子绑定与在途调用

- 六个全局钩子mask/unmask/ACK/prepare/probe/config不再通过无锁gTouchPanel直接调用。每次从永久绑定锁下获取一次service访问额度，执行时不持spinlock，返回时减计数；closing期间新调用不进入。
- stop/free先执行原shutdown取消协议，再关闭绑定入口。仍有钩子调用时，在同锁下assert_wait，解锁恢复IRQ后thread_block，最后一个reader唤醒；排空后才允许注销IRQ和归还AGPIO。等待时不持worker会访问的fLock/command gate，不在IRQ上下文做生命周期等待。
- 钩子函数地址安装后保持稳定；worker用acquire原子快照调用，避免检查全局指针后重新加载。detach撤销的是绑定对象，不是并发移除函数地址；持有旧函数指针在释放后调用，返回无绑定默认值/不写寄存器。
- 绑定锁是kernel常驻单worker facade的永久对象，不随service释放。当前不支持模块卸载或任意多worker；初始化依赖sweet_touch_panel_open已有唯一session约束。service生命周期方法由IOKit线程上下文串行；禁止在钩子自身内部递归stop/free。
- 测试在真实service调用到AGPIO::getConfig时hold钩子，同时另线程stop或free，确认入口关闭、旧调用返回前不能释放、新调用拒绝、旧调用完成后才能结束；各覆盖early wake。旧2d的service在相同用例的waitArmed断言失败，2e通过；含ASan/UBSan。
- 完整touch、TLMM、panel-lifecycle、display-service回归通过；三个ARM64 TU成功；CI marker151/0；diff --check通过。显示MDSS/Lifecycle及GenericInterruptController文件与b22无执行差异。

## 第2批交付前总审查状态

本地已覆盖：pad写入者、共享probe pending写入者、slot注册/释放、dispatch快照寿命、service钩子寿命及服务撤销后禁止raw fallback。仍须在最终候选上跑汇总核验并确认build/打包门禁，然后才给用户一个第2批候选做实机验收。所有2a–2e仍属于同一主题的内部提交，不是要求用户五次刷机。

首包ENABLE读回竞态、mask/resume软件/硬件写序、多pad整线阻塞、stray隔离、失败触摸后的显示降级，仍按固定计划分别留到后续批次；没有借本轮通过宣称它们解决。正常GPIO mask/resume仍要求owner不与注销并发（退休拒绝不是该类任意交错的完整MMIO原子协议，写序由批次4处理）。第2批本地完成不等于设备通过，也不能据此推进第3批生产改动。


## 汇总交付候选：0121b27（仅第2批，本地完成，待CI/实机）

生产提交：2a 66b7f82、2b 3575577、2c bb37387、2d 3bb3632、2e 867eb71；基础为真正b22/830fa52。本批不含旧b23–b26整包、不含第3批首包容错，也不修第4批summary写序或第5批叶子策略。

最终本地证据：四套TLMM/真实C worker+service/生命周期/display-service回归PASS；三个ARM64 TU成功；marker151/failing0；diff --check通过。2d与2e的双线程用例均有旧实现失败对照；ASan/UBSan仅检查主机适配范围，不是完整硬件或数据竞态证明。

工作区 out-bs/mk16k-0121b27.sh 已就绪：固定ABI608固件及面板钩子、派生ADT、mode2/mode3参数；检查prepare/probe/排空相关符号和字面量。真实旧b22在prepareInterrupt符号门禁处被拒，未生成伪b27镜像。新CI下载后仍须逐文件比较构建树，不能靠符号/string门禁证明并发补丁完整存在。

用户推送full-source后运行CircleCI，保存新产物为logs/xnu-kernel-0121b27.gz与logs/xnu-buildtree-0121b27.gz；agent核对再打包。当前没有b27内核或镜像，未push、未触发CI、未操作设备。

实机验收顺序：gpioirq mode3优先，核对claim模式、TouchComm ready、probe结果、armed、touch worker ready、FB HANDOFF active、桌面、连续点击/抬起/滑动及原有OFF/ON。mode2镜像是同内核对照而非不使用240。保留本次原始sweetlog；按4096字节头块解码。

已知首包竞态可能在本批暴露：正常callback已经将pad ENABLE清0时，旧arm验证仍可能报handle enable readback。若出现，应保留第2批实测失败，不把它改名为本批已验；再以单独第3批提交处理明确依赖。不得为强行通过本批而关闭检查或恢复第二个裸寄存器写者。

第2批在收到实机结果前保持“本地完成、待验”，不推进第3批生产改动。后续仍包括真实硬件多pad及异常中断验证，不把单触摸桌面等同于叶子化完成。


## b27 下载及镜像核验（2026-11-20）

下载构建树的5300个XNU源码文件与92b633f逐文件匹配，0缺失/0差异。kernel.development.mh2lm为17863232字节，SHA256 e9371b6dba8698429b77a21cdefd37bd048731792092277fb6470c6e2bec9f70，与包内SHA256SUMS一致。没有独立CI run ID，源码核验不等于云端run元数据核验。

两镜像均35545088字节。固件SHA256 27e7a1d1a3f9421af18d02f216e4e7d651137d42f2955be775f9142f118ea480，面板钩子存在；ADT SHA256 a9e4f37151162be9244b2e701b2e60063d85d4c424e8842c5bbf9b8d62391875。最终Android v1镜像重新抽取FAT内核/ADT/TOML并核验，参数只差sweettouchpanel=3/2，显示参数保持。

| 镜像 | SHA256 |
|---|---|
| out-bs/boot-16k-0121b27-gpioirq-panel-pipeline.img | a791480f95814a4bbaddd20a3d1f242e4a61c9d3f519ebdb3556fa8fec339ad8 |
| out-bs/boot-16k-0121b27-baseline-panel-pipeline.img | e8bf056468c799fd68b903c215254073d2f9a4796bd742fc490250506b3cdf32 |

证据：工作区scratch/artifact-0121b27/packaging-verification.json。候选源码仍92b633f；本次没有生产代码变更。下一步用户临时启动gpioirq，确认桌面/触摸/原显示生命周期并导出sweetlog-0121b27.bin；当前尚无实机结果。

等待期间仓库外scratch/gpio-irq-staged/characterize-stage3.py复现了该候选仍保留的首包问题：正常arm成功state1/cfg0x71；CFG读取前callback已deferred则失败state3/cfg0x70/status1；没有callback但ENABLE0则仍拒绝。这是明确的后续批次依赖线索，不是通过测试；未将容错夹入本候选。
