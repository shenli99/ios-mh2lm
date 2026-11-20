# 批次1：b22 基线锁定

日期：2026-11-20。执行计划：[固定分批计划](GPIO-IRQ-IMPLEMENTATION-PLAN.md)。

## 源码与产物

- 实际 b22 对应 830fa52，下载 buildtree 的全部 5300 个 XNU 源码文件与该提交一致；驱动实现提交为 9e3616e。
- 当前回归以 830fa52 的原代码运行，不合入此前 b23–b26 修改；本批只新增记录。
- 用户已经确认 b22 gpioirq 正常，不要求重新跑同一镜像。其余阶段的主机通过不代替新的设备验收。

| 产物 | SHA256 |
|---|---|
| b22 gpioirq boot image | `94b5574e81b56963e928a823ef43f62d24e98d9218f8ecc889fa04991a1fb155` |
| b22 baseline boot image | `e692aa604de5829938ab058b3768285ead1781a3d799da4da16081f2fd44b75d` |
| 两镜像 FAT 中 kernel.macho | `c8752d2c36f79e028a7bba4582021739f859619977e038853dc6b4ed7cad4a78` |
| 面板 ADT | `a9e4f37151162be9244b2e701b2e60063d85d4c424e8842c5bbf9b8d62391875` |
| BootShim/ABI608 固件槽 | `27e7a1d1a3f9421af18d02f216e4e7d651137d42f2955be775f9142f118ea480` |

上述 SHA 为从最终已有 boot 镜像重新抽取验证；gpioirq=mode3、baseline=mode2。工作区位置为 out-bs/boot-16k-0121b22-{gpioirq,baseline}-panel-pipeline.img。

## 已有实机证据

logs/sweetlog-0121b22.bin 的 header：version1 / ring4194304 / total3355655 / flush33，未绕环。以 tools/sweet-log.py 解码，不能从 offset20 截取。

已验里程碑：ABL selection source2/profile4；AGPIO claim gpio122；TouchComm ready；pad armed；touch worker ready；HANDOFF active；15 down/15 up、hits319、late0。保留 GICD 读回 ERROR 的原始事实：同一份日志中触摸仍正常，不能把 ENABLE 快照等同于没有 IRQ。

## 本次重新执行的回归

顺序运行，不并发复用固定临时对象：

```sh
python3 tools/tests/test-mh2lm-tlmm-gpio.py "$PWD/dxb/xnu"
python3 tools/tests/test-mh2lm-panel-touch.py "$PWD/dxb/xnu"
python3 tools/tests/test-mh2lm-panel-lifecycle.py "$PWD/dxb/xnu"
python3 ../../scratch/irq-gic-0131/check-ci-markers.py
```

结果：三套 PASS；markers151/failing0。测试中的非所有者释放、缺 specifier ERROR 是负例预期，不是设备日志。

## 所有权审计起点（批次2入口）

- SweetTouchPanel mode3 已持有 gpio122 的 AGPIO 再注册中断；不重复创建注册机制。
- 控制器 claim 后 mask；dispatch 入口 mask；maskPinIRQ 清 pad ENABLE 并 mask 整线；所有 client mask 清零后才允许 unmask。一个 pad deferred 会阻塞其它 pad，这是待解决目标。
- 触摸 arm 仍直接配置 INTR_CFG、清 INTR_STATUS、置 ENABLE；fail 和 park 等路径仍有直接 CFG 写入。文档的“一个写入者”目标尚未全实现。
- 只迁正常 arm 不够：批次2须覆盖失败、探针、暂停、恢复、取消、worker退出，逐项标明写入者，保留 mode2 原路径。
- 不在批次1改读回、清扫、claim开线、显示状态机或中断门控。

批次1完成；批次2可以开始本地审查、测试与实现，完成后须单独提交/实机验收。
