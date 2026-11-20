# 批次3：早到 probe / arm 回调验证（0121b28 候选）

基础：第2批已交付源码92b633f。b27实机未通过，首错在GIC probe enable；依照[固定计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)的依赖条款单独修正。第2批不改写成已通过，完整失败证据见[批次2记录](GPIO-IRQ-OWNERSHIP-STAGE2.md)。

## 问题与证据边界

b27已到TouchComm ready，但在检查probe使能时失败，未到arm检查。日志缺少此刻probe-hits，不能唯一归因为早到回调；硬件使能确实失败或超时仍须考虑。旧b22的GICD读回ERROR只是诊断，本次C worker的失败检查才直接令worker退出。

真实C worker + SweetTouchPanel测试可稳定产生两种相同硬件读回：

- resume已经投递callback，callback按合同mask：ENABLE=0、probe-hits=1；
- resume没有产生callback，确实没有使能：ENABLE=0、probe-hits=0。

旧probe guard都拒绝；旧arm guard同样把state2已deferred的ENABLE0当失败。本批同时修正该主题的两个窗口，不改summary策略/所有权/显示。

## 修正

probe：先执行原RWP等待、读使能，再acquire回调发布的probe-hits。ENABLE为0时，仅恰好一次已完成callback允许继续。RWP等待失败仍拒绝；不直接以callback宣告probe成功，原owner pending写入/清理、最终hits==1及pending/active排空检查全部保留。失败诊断加入waited/enabled/probe-hits/cfg，early callback有单独INFO。

arm delegated分支：先读CFG，再acquire当前IRQ state。允许state1且ENABLE1，或state2已deferred的包；~0配置、其它状态以及state1/ENABLE0仍拒绝。不补写ENABLE、不ACK、不清掉pending packet；state2 INFO指明交由worker正常drain。原先较早的GIC state快照不能代替此处的新state。

## 验证

- 新probe用例在ISENABLER读取前通过真实service callback投递：正常early callback通过，未投递且ENABLE0拒绝；active持续为1或RWP持续busy仍拒绝。
- 新arm用例在配置读取处投递callback：state2/ENABLE0通过且status1保留；无callback/ENABLE0拒绝；终止state即使ENABLE1也拒绝。
- 同测试仅换回b27旧C worker，在early callback应通过的断言处失败；保留了负对照。
- TLMM、真实worker/service、panel-lifecycle、display-service四套回归PASS，C worker ARM64 TU成功，CI marker151/0，diff --check通过。
- 本批生产差异仅sweet_touch_tcm.c的两个验证分支与日志；TLMMGPIOController、SweetTouchPanel、GenericInterruptController、MDSS执行逻辑不变。

## 交付与验收

工作区out-bs/mk16k-0121b28.sh已准备，固定原ABI608固件/ADT/参数，增加probe/arm标记门禁；真实b27内核在early callback标记处被拒绝，不会被包装成b28。还必须核验新CI构建树，不靠字面量证明全部逻辑。

用户push full-source下载b28 kernel/buildtree后，agent核对打包再验：TouchComm ready → probe成功/排空 → armed（若有deferred日志cfg可为0x70）→ touch worker ready → FB HANDOFF active → 桌面及连续按下/抬起/滑动。若再次probe失败，新增快照用于区分无callback/真超时，不强行忽略错误。当前无b28内核/镜像/实机结果，不声称修复所有历史故障。

批次4软件/MMIO写序、批次5叶子掩码、批次6异常路径及独立显示降级保持未实施；等本批依赖实机结果后再推进。
