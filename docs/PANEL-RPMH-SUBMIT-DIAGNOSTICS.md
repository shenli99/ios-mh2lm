# RPMh 提交检查：状态差值、IRQ enable 和 trigger

2026-11-12，核对当前0208（`190c558`）。本页提供诊断代码及插入点，**未修改生产驱动或新增刷机档位**。

## 当前流程核对

1. `begin()`/ARC stage/CN0 queue：选择空闲 ACTIVE TCS，屏蔽所选 IRQ source，W1C其旧完成位；
   写 MSGID/ADDR/DATA、WAIT、CMD_ENABLE、CONTROL=0、CONTROL=AMC，最后开启所选 source。
   操作队列逐项读回，未确认时异步等待原1ms期限，不越过读回直接 trigger。
2. `advance()` 按当前 IRQ_ENABLE 做 read/modify/write，program首项清bit、末项置bit，保留其他来源。
   `addOperation(IRQEnable,0)` 是排队占位值，执行时会重算，**不是最后写入零禁用中断**。
3. source开启后、trigger前要求所选 IRQ_STATUS=0；然后 Armed→CONTROL=AMC|Trigger→GIC enable。
   此硬件所谓doorbell就是所选TCS的CONTROL bit24，不另找未知doorbell地址。
4. 原先没有提交前/后的CMD_STATUS差值；已有IRQ/期限诊断只有当时快照。
   最终trigger写有MMIO屏障，但不在posted操作队列里，没有单独记录该写入后的CONTROL读值。

LG `drivers/mailbox/qcom-rpmh-mailbox.c::__tcs_buffer_write()` 同样先写payload和WAIT/ENABLE，
再清trigger、清AMC、设AMC、最后设trigger。当前选择的TCS先要求CONTROL=0，因此队列从0开始。

| 字段 | 相对 `fTCS=0x18220d00` 的偏移 | 预期 |
|---|---|---|
| RSC_DRV_IRQ_ENABLE | 0x00，不加TCS stride | 所选TCS bit为1，1是enable |
| RSC_DRV_IRQ_STATUS | 0x04，不加TCS stride | trigger前所选bit为0；本轮IRQ时为1 |
| TCS_CONTROL | t×0x2a0+0x14 | trigger前AMC=0x10000；最后写0x1010000 |
| TCS_STATUS | t×0x2a0+0x18 | 完成时非零且不是0xffffffff |
| CMD_ENABLE / WAIT | t×0x2a0+0x1c / +0x10 | 三命令=0x7，单命令=0x1 |
| CMD_STATUS[c] | t×0x2a0+0x3c+c×0x14 | ISSUED bit8、COMPLETE bit16 |

TCS0前三条CMD_STATUS实际地址为0x18220d3c、0x18220d50、0x18220d64。
IRQ_ENABLE看的是TCS位，CMD_ENABLE看的是命令位；两种mask不能混用。
GIC INTID37对应GICD_ISENABLER1的bit5，是另一层enable。

## 诊断代码

以下只放进SweetRPMh内部，使用现有MMIO封装；全部调用在provider gate/workloop内，
不放进GIC顶半部，不让consumer读寄存器，不写CMD_STATUS，不轮询等待变化。
正常采样输出使用INFO，受现有日志等级过滤；原失败ERROR日志继续保留。
这里只比较**命令编程已完成、trigger之前**与**trigger之后/IRQ时/超时时**的状态。
若要另取“第一次payload写之前”的状态，应单独标记该phase，避免把编程清状态误当执行完成。

SweetRPMh.h私有区追加：

```cpp
struct SubmitDiag {
 uint64_t token;
 unsigned tcs, stage, count;
 uint32_t irqEnable, irqStatus, control, idle, enable, wait, command[3];
};
SubmitDiag fSubmitBefore;
bool fSubmitBeforeValid;
SubmitDiag sampleSubmitDiag(void);
void dumpSubmitDiag(const char *, const SubmitDiag &, const SubmitDiag &);
```

SweetRPMh.cpp中实现：

```cpp
SweetRPMh::SubmitDiag SweetRPMh::sampleSubmitDiag(void)
{
 SubmitDiag s={};
 s.token=fArcRequest ? fClients[fClientIndex].displayStatus.token :
         (fRegisterRequest ? fClients[fClientIndex].registerStatus.token :
                             fClients[fClientIndex].status.token);
 s.tcs=fTCSIndex; s.stage=fArcRequest ? fArcStage : 0;
 s.count=fCommandCount;
 const uint32_t b=s.tcs*Stride;
 s.irqEnable=read(fTCS,IRQEnable); s.irqStatus=read(fTCS,IRQStatus);
 s.control=read(fTCS,b+Control); s.idle=read(fTCS,b+Status);
 s.enable=read(fTCS,b+Enable); s.wait=read(fTCS,b+Wait);
 for (unsigned c=0;c<s.count && c<3;++c)
  s.command[c]=read(fTCS,b+CommandStatus+c*CommandStride);
 return s;
}
void SweetRPMh::dumpSubmitDiag(const char *phase,
 const SubmitDiag &before,const SubmitDiag &after)
{
 if (before.token!=after.token || before.tcs!=after.tcs ||
     before.stage!=after.stage || before.count!=after.count) {
  SWEET_LOG_WARN("SWEET RPMH SUBMIT: mismatched samples; no delta\n"); return;
 }
 SWEET_LOG_INFO("SWEET RPMH SUBMIT: phase=%s token=%llu tcs=%u stage=%u n=%u irq-enable=%#x->%#x irq-status=%#x->%#x control=%#x->%#x idle=%#x->%#x enable=%#x->%#x wait=%#x->%#x\n",
  phase,after.token,after.tcs,after.stage,after.count,
  before.irqEnable,after.irqEnable,before.irqStatus,after.irqStatus,
  before.control,after.control,before.idle,after.idle,
  before.enable,after.enable,before.wait,after.wait);
 for (unsigned c=0;c<after.count && c<3;++c) {
  const uint32_t a=before.command[c],z=after.command[c];
  SWEET_LOG_INFO("SWEET RPMH SUBMIT: cmd=%u before=%#x after=%#x xor=%#x rose=%#x fell=%#x valid=%u\n",
   c,a,z,a^z,z&~a,a&~z,(unsigned)(a!=~0u && z!=~0u));
 }
}
```

插入点：

- VRM `begin()`、CN0 `beginRegisterBandwidth()`、ARC `queueArcStage()`建立每个实际硬件stage时，
  设置 `fSubmitBeforeValid=false`。不能复用前一个请求或ARC前一stage的baseline。
- 在`advance()`原有trigger前的IRQ_STATUS=0检查通过后，采样baseline；紧接保留原状态切换/
  Armed/trigger/GIC enable顺序。把这一小段改为下例，前面的fence与stale检查全部保留：

```cpp
fSubmitBefore=sampleSubmitDiag(); fSubmitBeforeValid=true;
fIRQObserved=false; fExecuting=true;
fOperation=fOperationCount=0;
__atomic_store_n(&fIRQArmed,1u,__ATOMIC_RELEASE);
write(fTCS,b+Control,AMC|Trigger); // 0x1010000，写后屏障在write()中
const SubmitDiag posted=sampleSubmitDiag(); // 含CONTROL显式读回；只采一次
write(fIRQMap->getVirtualAddress(),GICEnable,GICBit);
// 先启GIC再输出，避免日志把快速完成挡在enable之前。
dumpSubmitDiag("post-trigger",fSubmitBefore,posted);
waitCompletion(); return;
```

- 在`advance()`收到`fIRQObserved`并通过现有完成核验后、追加cleanup操作之前：

```cpp
if (fSubmitBeforeValid)
 dumpSubmitDiag("irq-before-cleanup",fSubmitBefore,sampleSubmitDiag());
```

- 原`diagnose()` lambda内，当`fExecuting && !fOperationCount && fSubmitBeforeValid`时，
  同样调用`dumpSubmitDiag("failure-before-quiesce",...)`；必须在`finish()/quietIRQ()`之前，
  否则读到的IRQ_ENABLE=0可能是失败清理自己屏蔽的，不能当作提交时未enable。
  不在stop、取消、依赖撤回或每个watchdog tick里读寄存器。

## 判断标准

| 观察 | 判断 |
|---|---|
| `before ^ after != 0`，有效读值 | 两次采样间状态发生变化；区分rose/fell和具体位，不能直接等同成功ACK |
| ISSUED或COMPLETE出现0→1 | 与执行/响应进展相符；仍需本轮IRQ和全部命令核验 |
| `before == after == 0x10101` | **无法用delta判断是否执行**；快速重新执行后可恢复相同值，旧值也可能一直残留 |
| 读到`0xffffffff` | 无效读值，不能把大delta当执行证据；不是正常状态 |
| trigger前`IRQ_ENABLE & (1u<<tcs) == 0` | 本TCS完成source未enable；先查提交队列，不等到超时再猜 |
| trigger前IRQ_ENABLE=1，IRQ_STATUS=0；写后所选IRQ_STATUS=1 | 支持本轮有新完成；还需callback、每条命令ACK及cleanup确认 |
| source已enable，trigger后CONTROL含AMC+Trigger | 支持触发写已可读，不能单独证明远端执行/PMIC成功 |
| trigger后CONTROL没有Trigger | 单次读回不足以判断漏写；必须结合已知寄存器语义、即时IRQ和完成状态，不盲目要求trigger始终保持1 |
| CONTROL=0x1010000，idle=1，命令均0x10101，未收到IRQ | 仍有完成证据缺口；若raw IRQ有bit查GIC/路由/event，若无bit查source/触发/硬件语义 |
| fresh callback + 所选IRQ_STATUS + idle + 全命令ISSUED/COMPLETE + cleanup读回 | 当前软件成功票据的完整条件；不是实际电压或整面板成功证明 |

这些顺序读取不是原子硬件快照；不因未采到busy、delta=0或瞬间GIC enable=0就报“没有执行”。
正常IRQ顶半部会主动mask SPI37，因此其后的GIC enable=0可以是正常现象，应结合采样phase。

0121a20旧日志在失败前IRQ_ENABLE=0/saved=1、IRQ_STATUS=0、CONTROL=0x1010000、命令均0x10101：
它确实显示当时source被屏蔽且trigger位可见，但没有前值和真实IRQ，不能从中证明本轮ACK或断言PMIC拒绝。
0208已改变该source/fresh证据路径，需新产物实测，旧日志不能代表0208运行结果。

## 示例校验范围

完整示例及上述插入点已在隔离0208源码树中通过实际XNU/KDK ARM64单文件编译。
诊断示例新增6条编译器unsafe-buffer-usage提示，来自现有client数组索引和有界三命令数组访问，
未屏蔽警告；没有新增编译错误。未进行硬件验证，未把该示例写入生产0208补丁。
底稿位于`scratch/rpmh-submit-review/`的源码、编译日志与warning-comparison.json。
