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


## b28 产物和镜像交付（2026-11-20）

收到kernel/buildtree，两包完整读取；5300个XNU源码文件与999989b逐文件一致，0缺失/0差异。内核17879616字节，SHA256 af987bd08d93e809bcf6fa42c94c3f133cab2c07bf0ef5c796ba4a4fb66e0305，与包内SHA256SUMS匹配。没有另行核验CI run ID，不能声称云端元数据已验证。

最终Android v1镜像均35545088字节，重新抽取FAT内核/ADT/TOML回读通过，新probe/arm四组标记存在；ABI608固件SHA256 27e7a1d1a3f9421af18d02f216e4e7d651137d42f2955be775f9142f118ea480及MHPANEL1钩子通过；ADT SHA256 a9e4f37151162be9244b2e701b2e60063d85d4c424e8842c5bbf9b8d62391875。两镜像参数只差touch mode，显示参数不变。

| 镜像 | SHA256 |
|---|---|
| out-bs/boot-16k-0121b28-gpioirq-panel-pipeline.img | 22fba647dec15d6b4e25fb342ba64b6d0a688f42ba0e0da7f979f3831f594701 |
| out-bs/boot-16k-0121b28-baseline-panel-pipeline.img | 8ff2fcd4244da5a645a813ec9aa9af0a34df6c33d9c8c33c8503b87d75f311ab |

核验记录在工作区scratch/artifact-0121b28/packaging-verification.json。下一步用户临时启动gpioirq，验证桌面和连续触摸，再原有OFF/ON，导出sweetlog-0121b28.bin。不要求新CI，当前无实机结果。若失败，以新增waited/enabled/probe-hits/cfg/state快照定位，不因字面量存在宣称问题已修复。
