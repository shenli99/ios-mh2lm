# 0214：以 halt 状态完成本地 voted 时钟请求

2026-11-13。[a26日志](PANEL-D1-0121A26.md#实测复位位清除仍由enable门槛超时)显示
RSCC_BCR在warm-entry、pre-write和timeout均为0，三路局部CBCR在core转换前后不变；
AHB从XO配置0x1切到PLL1配置0x40f。branch7仍为enable0/OFF0，42次等待未见两位置位。
当前XNU额外的enable读回门槛直接触发Timeout；Lineage的开启halt检查已经满足。

0214修正这处软件判据差异，**尚未证明实机接管成功，也未解释硬件为什么读回enable0**。
源码基线是已验证的实际a26构建产物，补丁只改变MDSS.cpp、Clock.inc、NoC.inc、Link.inc、Lifecycle.inc。

## 请求、完成与使用

- 增加`fClockIssuedMask`：仅在本实例对voted分支实际执行enable写入、`dp_wr`屏障返回后记账。
  写前保守设置的`fClockOwnedMask`不再被混作已发出请求的证据。issued是软件提交状态，不是硬件ACK。
- 新的`clockBranchRequested/clockBranchRunning`统一判定：继承的enable票仍需bit0读回；
  本实例拥有且已发出的voted请求可以在bit0读零时，依据bit31 OFF清除完成。
  表中只有branch7 NON_GDSC_AHB为voted，普通HALT及PHY分支的enable要求不变。
- retain准入、worker、最终复查、NoC访问前检查以及Link/Lifecycle的knownMask检查使用一致语义。
  第二个消费者复用已发出的票，不因bit0读零而重复写入。BOOT的mask0x7f不含branch7，
  localClocksReady和managedRetainedResources中的BOOT检查保持原样。
- 本轮未修改父源检查、ARC/CN0成功凭据、域/总线/idle准入、10ms/200ms期限或NoC的Held租约要求。
  请求提交本身不能进入RSC/TCS；仍须完成整个时钟租约和最终检查。
- 最后一个自身引用释放时仍写clear，确认enable清除后异步settle至少10us；不等共享aggregate OFF。
  释放完成清除issued，重新获取必须再发请求。继承票不撤回；故障保留原账本并关闭后续写入；stop无MMIO。
  这里仍保留比Linux更严格的撤票读回条件，本补丁不是整个branch2开关路径的逐项移植。

`CLK_OFF=0`为状态报告，不能证明电气测量或写入一定被接受。参考Linux在regmap写成功后也依赖
这个状态；当前XNU MMIO接口没有设备写入返回码。若写入被丢弃但仍读到OFF0，两者无法仅靠halt区分。
本补丁明确采用参考开启判据，避免凭未经参考要求的bit0读回制造超时；硬件侧原因仍需后续证据。

成功等待新增：

```text
SWEET MDSS CLOCK: voted halt complete branch=7 cbcr=0 request-issued=1; enable readback diagnostic only
```

这是该分支的等待完成，不是整个租约或DMA成功。随后仍需`CLOCK state=2 mask=0x381`、
NoC的本轮完成，以及新DMA/HANDOFF证据。原0211–0213取证仍保留，成功路径不输出超时ERROR。

## 验证

- 新专项18场景ASan/UBSan通过：精确a26兄弟分支状态/未开兄弟分支、立即或延后OFF清除、
  OFF持续置位/FSM2、最终复查失败、NoC租约/父源/上游权限、继承票消失、共享引用/最后撤票/重新获取及stop。
- 同一专项在实际a26基线运行4个读零场景，均复现Timeout；修改版相应场景能进入模拟managed新帧接管。
  fixture独立控制读回状态，并没有证明实机总线或物理时钟已恢复。
- 15套既有回归通过，共16套。更新旧诊断用例，使失败由真实OFF1触发；enable0/OFF0移入新的成功回归。
  新增完整mode4生命周期用例，持续enable0经过真实Link/Lifecycle函数的休眠、PHY重建及新帧恢复。
- 34个真实provider父链/域/总线/typed权限故障分别注入写前和写后，仍返回NotReady/Busy，
  无额外分支写入或display TCS提交。
- 真实XNU/KDK ARM64 SweetMDSS基线和修改版对象编译通过；告警1552→1553，
  新增一条固定分支表索引的unsafe-buffer-usage提示，未屏蔽告警，无新增格式告警。
- 0185–0214共30份补丁严格累计重放，无offset/fuzz，78份源码字节一致，114原CI source markers通过。
- 打包门槛新增`voted halt complete`；实际a26旧产物被拒绝，无boot输出。bash语法检查通过。
- 补丁SHA256：`7ec98ca1d2015cb12719b436582d4cd0f53256b0b2897a0c3fc9a76cdd3ef25f`。

底稿位于`scratch/framebuffer-0214/`：verification.json、tests.json、test-voted-halt-completion.txt、
test-panel-link-power-cycle.txt、parent-check-output.txt、warning-comparison.json、cumulative-replay.json、
matrix-rejection.json及final-review.json；`base`来自实际a26，`verify`为补丁重放结果。

## 下一次实机

需要用户push包含0214的代码并云编译，下载kernel/buildtree，建议标签0121a27。
agent核对五份源码变化、预计186项binary markers和SHA后，沿原ABI608固件/ADT/605字节参数打retained。
启动约90秒取完整USB日志，先检查voted halt complete→CLOCK Held→NoC；若再失败按新首错定位。
a26 USB证据已完整，无需补持久日志或重测。完整接管前亮度/OFFON/长测和其他显示档仍暂停。

未修改CI、固件、父时钟序列或复位寄存器，未push、触发云编译或操作设备；当前没有新boot镜像。
