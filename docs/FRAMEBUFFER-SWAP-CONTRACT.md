# 显示帧完成契约：原版内核核对与实现设计

2026-11-08，接续[用户态 ABI 核对](FRAMEBUFFER-USERLAND-ABI.md)。
本轮完成离线调查和设计，尚未实现新驱动行为或验收 F0。

## 1. 新证据的来源与边界

从本地 `iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw` 提取
`kernelcache.release.iphone10b` 并解压，只读分析其中
`com.apple.iokit.IOMobileGraphicsFamily`（343.0.0）的 legacy 实现。
内核 UUID `28E24CE2-BA1C-38B1-AC56-C0BE08A077BC`，解压后 SHA256：
`2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`。

底稿在 `work1/scratch/framebuffer-abi-0121a2/`：`kernel_read.py`、
`kernel-swap-wait-{start,context}.txt`、`kernel-selector6.txt`、
`kernel-wait-user-wrapper.txt`、`kernel-display-id.txt`、`kernel-ready-state.txt`、
`verify-kernel-wait.py`、`kernel-wait-verification.json`。28项原始指令/分发表记录核对通过。
Apple 二进制和底稿不入 git；以下为 unslid VA，不能跨版本套地址。

原始用户客户端的 dispatch 表起点 `0xfffffff006decb58`：第5项结构输入0x46c；
第6项 `0xfffffff006decbe8` 为3 scalar输入、无输出，入口 `0xfffffff005bffa40`，
调用 `0xfffffff005bfdf0c`，后者经 provider 虚表+0x580转发。
legacy gated wait通过字符串 `swap_wait_gated` 及三个独立诊断字符串交叉定位到
`0xfffffff005c11160–0xfffffff005c11c04`。本轮没有完整恢复provider虚表/继承链，
不声称仅凭调用地址已直接连通该虚调用与gated函数；后者参数和行为另有函数体证据。
这是本机同版原生实现的语义参考，不代表SM8150拥有Apple显示硬件能力。

## 2. flags 的实际区别

gated函数将w2保存为swap ID、w3为options、w4为timeout；将options最高位单独取出为
noBlock（`0xfffffff005c111e4–ec`），再清除最高位进行低位比较（`0xfffffff005c11210–18`）。
未满足条件且noBlock非0时，`0xfffffff005c1180c–1c`直接返回
`0xe00002d5`（`kIOReturnBusy`），不进入后面的sleep。

用函数体中可直接观察的字段定义：

- `D`：请求ID等于`fDisplaySwapID`（object+0x1464）；此名称由原始诊断字符串明确给出。
- `H`：在pending（object+0x1308）或queue（object+0x1310，link+0x968）找到该ID。
  各swap记录的ID位于+0x314。
- `Q`：上述pending/queue至少有一个非空。
- `E`：配置字段object+0x1478非0；由配置更新代码将输入结构+0xc写入。
  不将其直接等同“背光亮”“面板物理上电”或“没有排队帧”。

下表限定于正常运行分支；原生还包含abort、断链等提前返回，不能据此实现全部硬件故障恢复。

| options低31位 | 原生成功条件 | 条件未满足时 | 主要证据地址 |
|---|---|---|---|
| 0 | `!D && !H` | noBlock则Busy，否则等待 | `0xfffffff005c1165c–6d0` |
| 1 | `!H`，即不必等`D`清除 | 同上 | `0xfffffff005c116d4–173c` |
| 2 | `!Q`，清空整个pending/queue | 同上；不按某个ID判断 | `0xfffffff005c11740–1788` |
| 3 | `E` | 立即BadArgument，无sleep | `0xfffffff005c1126c–12b0` |
| 4 | `!E` | 立即BadArgument，无sleep | `0xfffffff005c112b4–12f8` |

options3/4失败码均为`0xe00002c2`。不能将其擅自写成Busy/NotReady。
本机CA的id0/options0x80000003是配置状态查询；id0/options2是整队列等待，
**ID0没有统一的“等最新帧”含义**。不能用`id <= lastCompletedID`通吃这些请求。
未找到的ID在原生正常options0/1分支中可满足条件（0还要求不是当前display ID）；
因此本项目若对未知/过期ID采用更严格错误，属于显式策略差异，不能说这是原生要求。

options0与1可以返回不同结果：某ID仍是当前显示ID，却已从pending/queue移除时，
0仍Busy，1已成功。原始代码把pending swap ID写入display ID的指令在
`0xfffffff005c10634–3c`，不能把“仍在显示”与“仍在pending”压成一个bool。

第三scalar在用户框架普通SwapWait中为0；原生gated路径还支持timeout参数，非0先转换为deadline。
无显式timeout的阻塞路径可见5000ms默认界限、sleep及故障恢复；不照搬其5秒panic/清队列策略。
本项目要按自身后端设有界等待，超时记录错误且保留实际未完成状态，不能标成成功。

## 3. 本项目应维护的状态

需要两类状态，而不是一个“完成序号”：

| 状态 | 持有内容 | 允许的操作 |
|---|---|---|
| begun | 客户端与内部64位序号、返回的32位ID | 提交或取消；尚无源像素可读 |
| queued | 已校验的提交、源surface引用、等待copy/提交后端 | 可以被显式取消/替换；不得假成功后再读源 |
| reading | 执行中的源读取 | 不允许从另一线程直接释放/取消读者；取消须等待本次读停止 |
| sourceReleased | 源像素已不再被任何执行者读取，引用已归还 | 可允许生产者复用；不能再依赖该surface重放旧画面 |
| displayCurrent | 后端当前使用的帧/扫描缓冲，独立于源是否释放 | 下一帧替换或合法blank后才退出该状态 |
| retired / cancelled / failed | 终态及原因，按策略保留查询记录 | 重复查询稳定；不凭新ID掩盖旧失败 |

`sourceReleased`与`displayCurrent`可以同时成立：CPU拷贝后，MDP继续扫描驱动拥有的缓冲，
不再读取CA的源surface。这是两个所有权事实；不能从中推导“MDP已显示完整帧”。
对options0/1分别映射到后端的display/pending状态时，必须保持上述差别，并记录采用的完成节点。

32位swap ID对外兼容，内部用64位序号/客户端归属消歧。分配时跳过0，不能覆盖仍有效的同ID；
begin后未end、重复end、客户端退出、ID回卷、过期记录均需定义。不能用有符号差值单独判断所有状态。
表容量与保留期限必须有界；队列满时先做受控背压或返回错误，不能静默覆盖reading。

## 4. 持久图层与源像素所有权

update只修改对应图层，未更新图层维持已提交状态；disable撤去该图层，bit31处理背景。
提交必须先完整校验，再原子提交，失败不得留下半份新状态。

这里有一个必须与等待同步处理的问题：**长期retain surface不能维持旧像素内容**。
若发布sourceReleased，之后息屏恢复、背景变化、局部图层更新都不能再读取旧CA源来重组。
可选实现边界：

- 当前CPU拷贝后端：保存驱动拥有的像素快照或最终完整输出，重放只读自有内存。
  源尚未复制的排队提交必须维持读取所有权；息屏时也要处理/取消它们，不能无限挂起等待。
- 后续直接扫描后端：当前前台surface仍为硬件读者持有，下一扫描地址生效且旧读者停止后，
  才能归还其所有权；retain本身不能替代fence。

替换queued需要分开“丢弃该帧”和“归还源读取权”；reading帧不能直接当成已取消，
displayCurrent也不能因来了一个新pending就提前退役。
合法全disable+黑背景必须产生实际blank；不能过滤空surface记录或硬恢复旧source。
多图层/混合尚未实现时，先规定可支持形态并记录不支持请求，不能静默顺序memcpy冒充合成。
单层CA输出已经完成的半透明效果也不能由内核再次混合。

## 5. IOKit 的串行化和停止顺序

目标仍为AppleCLCD的IOWorkLoop+IOCommandGate拥有状态；后端只提供能力和完成事件。
不把帧接口修复变成BSD层穿透寄存器，也不在此轮提前改DSI/SMMU/MDP地址。

- SwapBegin/End/Cancel、功耗状态变化、后端完成事件均进同一gate。
- 非阻塞查询只读状态后立即返回；条件未满足返回与该options对应的状态码。
- 阻塞查询由外部调用线程进入gate，先检查条件，再`commandSleep`原子释放gate并等待；
  唤醒后重查条件和deadline。不能用IOSleep轮询，也不能在持锁状态阻塞copy/IRQ执行者。
- 若工作loop自身调用等待，禁止等待未来只有本loop才能生成的事件；改为异步继续执行。
- gate内不能跨耗时像素copy一直占用：锁定一个不可变reading作业交给独立执行者，
  完成后以事件回到gate。copy线程、定时器、唤醒都不能直接改所有权状态。
- stop先置stopping并拒绝新提交，唤醒所有等待者，再同步取消事件/收回copy作业；
  确认源读者和回调退出后释放surface/自有缓冲、移除event source/gate，最后super::stop。
  客户端close只取消自己的队列，不能误释放其它客户端或硬件正在读取的源。

这包含F1 gate、F3完成事件、F4缓冲所有权的设计约束；**并不表示这些阶段已经实现**。
F0只读诊断保留原职责；如需先做CPU后端行为对照，应另列独立boot-arg与验收，
不能悄悄让`sweetfbtrace`改变渲染策略。

## 6. 实施前后如何验收

先将上面的状态转换和options条件写成实际驱动复用的纯逻辑，host测试至少覆盖：

1. options0与1在displayCurrent但无pending时的差别；options2不使用目标ID；3/4配置正反条件。
2. noBlock不sleep；阻塞可由copy完成唤醒，超时不假完成，stop可打断等待，无丢失唤醒。
3. 替换queued、取消reading、取消当前显示、begin未end、重复查询和ID回卷/表容量。
4. copy后生产者立刻改写源，再触发重放，输出仍来自自有像素；归还后源读取计数为0。
5. 部分update、全disable/背景blank、校验失败回滚，copy错误不能伪装显示成功。

实现补丁需strict apply、arm64单文件交叉编译，并检查与CI一致的include路径；
不修改当前CircleCI。完成后一次编译打baseline和独立行为档；关掉每秒79ms的重采样，
改为有限生命周期日志（ID/options/排队/读完/源释放/显示替换/等待结果）。

实机仍测手动息屏插拔、关机滑条的半透明效果和取消后的时间显示，并回归按键、触摸、USB日志和充电。
帧协议正确与视觉症状消失分别验收，不能将任何一项代替另一项。
未完整恢复的取消入口/provider虚表、配置就绪状态来源及当前后端的display完成节点，
在接线前继续核对；本设计没有把这些未知用硬编码成功补齐。
