# 0121a3 CPU帧契约实验结果

2026-11-09。实现提交6c2e77d / 0173，产物核对与打包记录b3831f4。

**结论：自有快照实验已实际启用，初期等待/换帧和多次电源切换按预期工作；用户确认两档
视觉表现相同，未见新异常。0173没有消除缺时间或关机滑条区域半透明颜色异常，F0仍未验收。**

本轮分析日志并更新文档，没有修改驱动/CI，没有新镜像或待云编译的修复。

## 证据与用户观察

文件均在`work1/logs/`，v1 / 4 MiB ring，未回卷，有效区无NUL。

| 档位 | 文件 | SHA256 | 有效字节 / flush / 解码行数 |
|---|---|---|---|
| 对照 | sweetlog-0121a3-fb-contract-baseline.bin | 4285dcace17adcea7ccaba14b2184ec4b1eef00fbb2b9776ee34cad141ac900e | 451944 / 34 / 5666 |
| 自有快照 | sweetlog-0121a3-fb-contract-owned.bin | b3d6314db3d726de746d49d948125790f6c04a96001cb716544bbcf18e15a038 | 460493 / 18 / 5753 |

用户回答：“两档一样，未见新异常。”对应本次询问：插电唤醒缺时间、滑条区域半透明颜色、
取消关机后缺时间，以及冻结/明显变卡。没有新的逐动作录像或时间标记，不自行添加改善结论。

内核SHA256 `513d16fa31f746cac23573ba1ac87343a0ffe03c361cf7eb0835b4c44f04a8c8`。
40文件/114个CI源码marker和两档boot回读核验通过；仅sweetfbcontract=0/1不同。
底稿：`work1/scratch/artifact-0121a3/` 中两份`fb-contract-*.txt`及`*.timeline.txt`、
`analyze-logs.py`、`log-analysis.json`。以下行号均引用这些解码文本。

## 实验确实接入，并非仍在跑旧路径

实验档第75行明确输出：

```
SWEET FB CONTRACT: v1 enabled; synchronous owned snapshots bytes=30326400; no hardware fence
```

两档boot-args分别为sweetfbcontract=0/1；对照没有该启用标记。

实验初期有49条wait记录，其中26条成功、23条Busy；所有Busy均为
`options=0x80000000`且请求ID等于当时display ID，符合原版核对的非阻塞条件。
不是提交失败或超时，不能把Busy数量作为23帧丢失。

具体例子：

- 第2331–2335行：ID1 reading→source-released，display=1，随后options1成功。
- 第2352行：ID1/options0x80000000，display=1，返回Busy。
- 第2353–2354行：ID2 reading→source-released，display切到2。
- 第2363行：再次查ID1/options0x80000000，display=2，返回成功。
- 第2428行：ID4/options0x80000001，display=4仍成功，保留options0与1的差别。

这验证了实机上的状态分支，不只是host模拟。它仍不能证明MDP显示完整帧的时间，
也不能替代真实硬件fence/性能测试。

实验档记录了24条reading、23条source-released、15条blank、7次power-off、8次power-on
（含启动）。其中ID15以blank完成，故reading/source-released两项相差1，不是漏归还读者。
普通日志有96条预算，恰在ID24的等待处耗尽；ID24之后缺reading日志不是停止
提交或源未释放的证据。power/blank另有预算，后面继续到blank ID695并成功唤醒。
未记录helper的ERROR、reject、timeout或setup失败，未见panic签名；不能据此验收未被触发的
取消/关闭/stop全部并发分支。取消日志为0，本轮没有对应路径的实机验收证据。

## blank和唤醒有实际变化，视觉问题仍在

实验档第3305–3307行：blank ID60/61后power-off，display=0；第3430行power-on，
display=61。随后还有六轮off/on，最后blank ID694/695（5401–5403行），
第5449行power-on display=695，全部rc=0。

因此新路径有执行合法blank并使用缓存场景恢复；不能再把它概括成“空记录一律忽略”。
但power-on的成功不意味着锁屏时钟一定由用户态绘出，display ID也不能识别画面里的内容。

本轮两档都关闭了F0像素重采样，均无`TRACE: v2 phase=`样本。因此不能声称本轮已证明
时钟ROI仍是source/panel同黑，或滑条alpha已正确；那类像素证据只属于0121a2。
源快照/等待/blank改变之后，用户观察仍相同，说明**这些缺口不是当前视觉问题的充分解释**。
不能把这个负结果反过来当成旧假完成逻辑正确，或断言CoreAnimation自身有bug。

## 独立问题和回归边界

对照档后段复现[BUG-002](BUGS.md#bug-002pmic-持续-pending-后电池按键组被隔离)：

- 第5281–5284行，APID271的Type-C RID/CC/VBUS/attach，bit0/2/4/5，status=0x35持续pending。
- 第5285–5287行，隔离电池group1；第5314–5319行consumer失败、禁充并撤回external/charging，
  保留97%。此后插线不能正常驱动电池consumer更新。
- 此次未见按键group0被隔离。第5322行仍有display-on，后续触摸活动；无已记录的全内核死锁。

实验档未复现group隔离，最后电池refresh到notification/sample75，第5472行仍healthy=1。
两档操作次数/持续时间不同：对照9次off/10次on，实验7次off/8次on；不能据单次不同表现
宣称0173修复或抑制了mux问题，framebuffer补丁也没有修改mux。

实验档三条USB端点命令WARN（1433、1460、2385行），不能单凭它们认定USB枚举失败；
按[BUG-001](BUGS.md)已有边界处理。两档HID按键down/up均成对（11/11、9/9），触摸IRQ
有报告；实验档充电状态可随插拔改变。未由本轮进行长时充电/USB压力验收。

两档各7条`CATransformLayer ... allowsGroupBlending ... transform-only layer`警告，
是用户态属性无效提示，不能据其数量直接确定半透明问题的原因。
实验档末尾由SpringBoard发起正常关机并到PSCI SYSTEM_OFF，不是已记录的panic。

## 下一步

1. 保留0173为独立开关，不默认启用，不为同一视觉现象重复这组A/B。
2. 下一项先离线核对**显示模式、颜色/时序属性和亮屏后的状态查询**：现有selector79/block0x40
   仍默认清零返回成功，与注册的TimingElements/ColorElements/IOMFBDisplayRefresh如何一致，
   本版用户态无匹配mode的fallback分支究竟做什么。先恢复实际分支，再决定修复，不能仅填ID1。
3. 半透明异常保留独立合成路径调查，不能从“无GPU”或transform-only警告直接定因。
   若后续需要像素取证，应是明确操作时的局部快照，不恢复每秒高开销全套采样。
4. F0继续未验收；本轮未获得真实MDSS完成/翻页证据，不提前将F1/F3/F4记为完成。
