# RPMh ACTIVE 完成中断：0208

2026-11-12。本地实现及验证完成，尚未云编译或实机验收。

提交顺序复核、CMD_STATUS delta的证据边界、IRQ_ENABLE/doorbell诊断代码见
[RPMh提交诊断](PANEL-RPMH-SUBMIT-DIAGNOSTICS.md)。该示例尚未接入生产补丁，不改变本页0208实现。

0121a20 的第一笔 ldoa14 请求停在 completion：三条 CMD_STATUS 均为 0x10101，
但 IRQ_STATUS=0、progress=0。旧命令状态不能证明本轮 ACK；0208 将完成证据改为真实
apps RSC 中断，不删除 fresh 检查，不延长期限。实测依据见
[首次投票超时分析](PANEL-RPMH-FIRST-VOTE-TIMEOUT.md)。

## 依据及所有权

- LG `arch/arm64/boot/dts/qcom/sm8150.dtsi:2595`：apps RSC 为
  0x18220000/0x100、TCS 为 0x18220d00/0x3000，SPI5，即 GIC INTID37。
- LG `drivers/mailbox/qcom-rpmh-mailbox.c:1288` 使用 level-high IRQ；handler 检查
  IRQ_STATUS 和每条 ISSUED/COMPLETE 后清理、W1C。MSGID 为长度8、响应 bit8、写 bit16。
- 本项目现有 MH2LM callback 链中未找到其他 INTID37 使用者；服务启动及首次请求均检查
  GIC 版本、容量、Group1 使能及该 SPI 的 enabled/pending/active 状态，冲突则拒绝。
  首次请求还拒绝原始 RSC pending，不替未知来源 ACK。
- SweetRPMh 仍是 apps-DRV 唯一 writer。消费者通过原 typed IOService 接口和 gate 请求，
  无新增寄存器接口。GIC 映射与显示、输入等共享，但仅配置 INTID37 对应字段，保留邻位。
  INTID115 的 MDSS 显示 IRQ 与本 IRQ 分开；不经过 SweetSpmiInterruptMux。

## 架构和完成条件

```text
SweetMDSS 等 typed consumer
             │ IOService / IOCommandGate
        SweetRPMh
        ├─ apps RSC/TCS MMIO：编程、核验、清理及票据账本
        ├─ IOInterruptEventSource ─ provider IOWorkLoop
        └─ IOTimerEventSource：期限、posted读回、GIC RWP/EOI drain
                 ▲
    GIC SPI5 / INTID37 → sleh → sweet_rpmh_irq()
```

物理回调只屏蔽本 SPI、更新原子状态并排软件事件；不读 payload、不 ACK 外设、不调
消费者、不等待。`sleh` 保留 GIC EOI。永久 dispatch lock 仅保护物理入口的服务生命周期，
不是消费者查找服务的全局桥接。

每次事务在已认领的空闲 TCS 上依次屏蔽所选 source、W1C 其旧完成位、写命令和 WAIT/ENABLE/AMC、
启用所选 source，并在 trigger 前要求所选 IRQ_STATUS 仍为零。随后先 Armed、再 trigger、
再启 GIC。trigger 后不清 pending，允许完成快于 GIC enable，避免丢掉 level-high 完成。
所有 IRQ_ENABLE 修改保留其他 source 位。

真实回调排队后，workloop 要求当前事务、所选 IRQ_STATUS、idle 以及全部 ISSUED/COMPLETE
一致，再清 trigger/AMC、ENABLE、WAIT、所选完成位并恢复原 source bit；清理读回通过后
才发布成功票据。下一笔请求等待前次 GIC active/EOI 退出，不覆盖仍活动的代次。

等待完成期间 timer 只做 watchdog，不轮询 completion。原 VRM 100ms、ARC 分段100ms/总500ms、
posted 1ms 不变；GIC RWP 最多1ms，异步重入，不忙等。IRQ 丢失、foreign/partial IRQ、
期限或核验失败均显式失败，屏蔽自己启用的 source 和 SPI，保留未知 payload/完成状态，
不伪造成功、不自动重试或回退 ABL。原终止 SNAPSHOT 保留，字段改为 `irq-observed`。

停止先在 gate 关闭准入、屏蔽硬件、终止事务及 timer，再置 Online=0，在 dispatch lock 下
撤回物理入口，移除同步的软件事件，最后释放映射。晚到回调不访问已释放对象；写过硬件的
provider 保留退役账本，不允许同次启动新实例接管空账本。不擅自恢复给未知 GIC owner。

## ADT、打包和改动范围

`mk-mh2lm-mdss-adt.py --rpmh-irq` 显式追加 RPMh 第四资源 GIC
0x17a00000/0x10000、`sweet,rpmh-irq-abi=1`、`sweet,rpmh-intid=37`。
原三个 nub resource 索引不动；内部 `fMaps[3]` 仍为 command DB，GIC 使用独立映射。
不伪造标准 interrupt-parent/interrupts。

mode1 仍支持原 metadata-only ADT；mode2 必须提供新扩展，无 polling fallback。
event matrix 的 lifecycle hardware ADT 启用扩展，identity ADT 不变，并要求新 IRQ binary
markers，防止旧内核被当成新 IRQ 版打包。**下一轮 hardware ADT 与0121a20不同，必须重新生成。**
固件ABI608及原启动参数保持，无新增 boot args。

生产补丁仅修改 SweetRPMh.h、SweetRPMh.cpp、sleh.c；未改 USB、按键、触摸、电池、SPMI mux、
显示寄存器逻辑或 CircleCI。原 SCANOUT FSR0x400 WARN 保留，不据此宣称其无害。

## 本地验证及下一步

16套真实源码 ASan/UBSan 回归通过，另有 ADT/CLI 组合验证。新增专项覆盖快于 GIC enable 的完成、
命令状态前后不变的真实 IRQ、重复/丢失/foreign/partial IRQ、启 source 暴露旧 pending、
无 completion polling、邻位保留、RWP 期限、stop 与物理回调并发及晚事件。
旧 fence 测试仍禁止失效后改 payload/trigger/W1C/票据，只允许精确 IRQ 安全屏蔽。

RPMh/sleh 使用实际 XNU/KDK 头通过 ARM64 对象编译，warning 分别1326→1326和1→1，
无新增种类；callback 定义和引用符号一致。0185–0208严格累计应用无 offset/fuzz，78源码字节
比较和114现有 CI source markers通过。CircleCI blob 仍为
`da0b6c0947a66f03491b3a9023590b49665292df`。
本地底稿：`scratch/framebuffer-0208/` 下 verification/tests/cumulative-replay/warning-comparison JSON
及各编译和测试日志；这些不是硬件或整核链接验收。

下一步用户 push 并云编译，下载 kernel/buildtree 后核对0208源码及新 binary markers，
同ABI608重新打 retained 档。只先取启动完整日志，重点看：

1. `SWEET RPMH IRQ: INTID37 configured`。
2. `SWEET RPMH IRQ: fresh ACTIVE completion` 及相应 `ldoa14 ACTIVE ACK`。
3. 后续 rail/ARC/NoC/core/link 与新扫描地址；有失败则按首错定位。

ACK 只确认事务完成，不等同物理电压证明或完整面板驱动完成。接管前不安排 OFFON、亮度、
长测或 USB 反复插拔；identity/旧0121a20不重测，combined/supply继续暂停。
