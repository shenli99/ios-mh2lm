# SPMI 调用点、准备失败与 ready 同步核对

**0167更新：** 控制器仍单锁串行，mux IRQ写/ACK新增等待者优先入场，普通事务
统一通过可睡眠的入场检查；不在top half取锁。独立ADC/WD诊断读逐字段释放。
历史调用点/行号按原版本保留；当前仲裁、完整锁顺序和验证边界见
[SPMI-ACK-ARBITRATION.md](SPMI-ACK-ARBITRATION.md)。

**0148更新：下列行号/哈希保留为0147审计快照。回滚失败处理已改为先尝试按组
隔离，只有隔离无法确认安全才停整个mux；日志也补齐等级。当前策略见
[MUX-GROUP-ISOLATION.md](MUX-GROUP-ISOLATION.md)。SPMI事务入口/锁和ready同步
方式保持，下文“回滚失败停整个mux”描述的是0147旧策略。**

核对日期：2026-11-05。基准是提交 `232b8b3` 所对应的0142→0147补丁链
最终源码（精确0118提取源码 +0146 +0147），不是未更新的0118实机内核。
可读源码快照：工作区 `scratch/consumer-prepare-0118/verify/xnu/`。
下列行号均相对该快照；调用点是静态源码位置，循环/重试会执行多次。

## 结论与接口边界

- 当前没有 `SweetSpmiInterruptMux::spmiRead()/spmiWrite()`，也没有本范围内的
  `fReady` 字段。不能把“统一锁”描述成“每笔事务都经 mux 公共读写接口”。
- 电池通过 `sweet_spmi_read()`，内部锁住 `sp_lock`；按键通过 `keys_read()`，
  命令到结果全程调用 `sweet_spmi_irq_lock()/unlock()`，取同一 `sp_lock`。
- mux 私有 `readPeripheral()` 的 battery 分支调用 `sweet_spmi_read()`，key
  分支手工持同一锁；私有 `writePeripheral()` 对两组均手工持同一锁。
- GIC top half 不发 SPMI 外设事务，不取可睡眠 IOLock。它只切换原子状态、
  屏蔽 GIC 并唤醒软件事件。真正的 PMIC ack 在 mux workloop 中完成；按键
  consumer handler 只唤醒 worker，由 worker 读 PON/GPIO，再发 HID。
- PMIC-arb 的 IRQ_STATUS/IRQ_CLEAR/ACC_ENABLE、APID 元数据，以及 GIC/PDC
  寄存器是本地控制器 MMIO，不是发往 PMIC 的 SPMI 读写包，不走上述事务锁。
  mux 的路由/IRQ控制 MMIO 由自身 gate 串行；top half 只接触 GIC。
- 当前保证每笔事务互斥。RMW、多字节一致性读取不是跨多笔事务的全局原子操作；
  采样保留原有有界重复/影子比较逻辑。

## prepare 失败与中断路由回滚

`keys_gpio_input_ok()` 仅调用 `keys_read()` 读取 MODE_CTL、EN_CTL、SUBTYPE；
`sweet_keys_prepare()` 不修改 GPIO 模式、电平、拉电阻等配置。固件留下的 GPIO
配置校验失败时，清该键 APID，调用 `sweet_keys_stop()` 并返回 false（MH2LM
音量下走 PON，音量上走 GPIO）。stop 释放按键状态/等待 worker 并 unmap 自有
映射。此时还没调用 attach，没有“已配置的 GPIO”需要写回。

prepare 返回 false 后连接进入 terminal，发 FAILED，不调用 attachConsumer。
若准备成功而后续 attach 失败，则处理的是另一层的中断配置：
`configureRoute()` 保存 oldType/oldHigh/oldLow，写 SET_TYPE/POLARITY/enable；
`updateBinding()` 失败时调用 `removeGroup()`，禁用当前组 IRQ、ack、按 mask
恢复触发类型/极性，并在 APID 无其它活动路由时恢复 oldAccess。
回滚失败会停整个 PMIC mux 并通知相关 consumer。

这是尽力清理/恢复，不是完整状态快照还原：没有保存/恢复原 PMIC EN_SET 位，
退出后该组 IRQ 保持禁用；已清除的 latched 状态也不能还原；恢复写没有额外
读回校验；RW 超时后 APID poisoned 会拒绝后续写，从而导致回滚失败并停用。
不要把它描述成任何故障下都能恢复到原硬件状态。

关键源码位置：`sweet_keys.c:241,348,397,421,513`；
`SweetSpmiMuxClient.cpp:138`；`SweetSpmiInterruptMux.cpp:387,658,756`。

## ready/生命周期同步

| 字段/信号 | 类型与保护 | 位置 |
|---|---|---|
| `sp_ready` | 声明是 bool；成功发布用 `__atomic_store_n(..., RELEASE)`，无锁读用 `__atomic_load_n(..., ACQUIRE)`；init 内普通读受同一 IOLock 保护 | sweet_spmi.c:34,212,230,275 |
| mux `state`（1=ready/idle） | volatile unsigned，运行期用 `__atomic_load_n/store_n/compare_exchange_n`；并非靠 volatile 同步 | sweet_spmi_mux.h:79；mux cpp:516,542,548,605,614,629 |
| `SWEET_MUX_READY` | 事件位，不是 bool；owner callback 在 consumer workloop 调用 | client cpp:175 |
| client `pending` | unsigned + 原子 fetch_or/exchange/store，release/acquire | client cpp:109,123,200 |
| client `stopping/terminal/providerGone` | 普通 bool，连接 IOLock 保护 | client header:35；cpp:76,93,124,150,166,186,223 |
| client `attached` | 普通 bool，仅 consumer gate 内访问，stop 通过 runAction 同步 | client header:36；cpp:138,165,192,199,208 |
| keys `backendPrepared` | 普通 bool；consumer gate 写，shutdown 先 connection.stop/drain 再读并清 | keys cpp:27,89,109,112 |
| battery `fIrqAttached/fIrqFailed` | 普通 bool，启动发布前初始化；运行期由 battery loop 串行 | battery cpp:103,798,816,871,881 |
| `SweetKeysIRQHealthy` / `SweetMuxReady` | IORegistry 属性用于状态观察/启动检查，不代替上述同步 | keys cpp:65；mux cpp:876 |

未使用 OSAtomic API；跨 IRQ/线程发布的字段用编译器 `__atomic_*`，其余状态
通过锁/workloop/退出 drain 建立顺序，不能笼统归为“普通 bool 无同步”。

## 完整静态调用点

同时列出底层读写调用及采样/ack包装函数的调用，避免漏掉间接路径。
包含旧 SID0 对照路径、可选诊断和编译条件分支；不表示 MH2LM 运行时全部启用。

### iokit/Kernel/arm/SweetBattery.cpp

SHA256 `fea7458669d405c257fd971390b0a950777e3d874602ce588e78f1ae6e93b86e`

| 函数 | 行 | 调用 |
|---|---:|---|
| `SweetBattery::readWord` | 398 | `sweet_spmi_read(sid, address, &lo)` |
| `SweetBattery::readWord` | 399 | `sweet_spmi_read(sid, address + 1, &hi)` |
| `SweetBattery::readWordPair` | 420 | `sweet_spmi_read(sid, address, &lo)` |
| `SweetBattery::readWordPair` | 421 | `sweet_spmi_read(sid, address + 1, &hi)` |
| `SweetBattery::readWordPair` | 422 | `sweet_spmi_read(sid, copyAddress, &copyLo)` |
| `SweetBattery::readWordPair` | 423 | `sweet_spmi_read(sid, copyAddress + 1, &copyHi)` |
| `SweetBattery::readBytePair` | 445 | `sweet_spmi_read(sid, address, &sample)` |
| `SweetBattery::readBytePair` | 446 | `sweet_spmi_read(sid, copyAddress, &copy)` |
| `SweetBattery::logIrqLines` | 490 | `sweet_spmi_read(SWEET_BATT_SID, base + 0x10u, &rt)` |
| `SweetBattery::logIrqLines` | 491 | `sweet_spmi_read(SWEET_BATT_SID, base + 0x11u, &type)` |
| `SweetBattery::logIrqLines` | 492 | `sweet_spmi_read(SWEET_BATT_SID, base + 0x12u, &high)` |
| `SweetBattery::logIrqLines` | 493 | `sweet_spmi_read(SWEET_BATT_SID, base + 0x13u, &low)` |
| `SweetBattery::logIrqLines` | 494 | `sweet_spmi_read(SWEET_BATT_SID, base + 0x15u, &enabled)` |
| `SweetBattery::logIrqLines` | 495 | `sweet_spmi_read(SWEET_BATT_SID, base + 0x18u, &latched)` |
| `SweetBattery::logChargePolicy` | 515 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_DCDC_PATH, &path)` |
| `SweetBattery::logChargePolicy` | 516 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_USBIN_APSD, &apsd)` |
| `SweetBattery::logChargePolicy` | 517 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_USBIN_CMD_IL, &cmdIl)` |
| `SweetBattery::logChargePolicy` | 518 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_USBIN_ICL, &icl)` |
| `SweetBattery::logChargePolicy` | 519 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_USBIN_AICL, &aicl)` |
| `SweetBattery::logChargePolicy` | 520 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_TYPEC_SNK, &sink)` |
| `SweetBattery::logChargePolicy` | 521 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_TYPEC_STATE, &typec)` |
| `SweetBattery::logChargePolicy` | 522 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_TYPEC_MISC, &misc)` |
| `SweetBattery::logChargePolicy` | 523 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_SMB_EN, &smbEn)` |
| `SweetBattery::logChargePolicy` | 524 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_SMB_CFG, &smbCfg)` |
| `SweetBattery::poll` | 575 | `sweet_spmi_read(0, 0x4804, &type)` |
| `SweetBattery::poll` | 576 | `sweet_spmi_read(0, 0x4808, &present)` |
| `SweetBattery::poll` | 577 | `readWord(0, 0x48c0, &rawVoltage)` |
| `SweetBattery::poll` | 578 | `sweet_spmi_read(0, 0x1310, &usb)` |
| `SweetBattery::poll` | 579 | `sweet_spmi_read(0, 0x110b, &path)` |
| `SweetBattery::poll` | 580 | `sweet_spmi_read(0, 0x1006, &charger)` |
| `SweetBattery::poll` | 581 | `sweet_spmi_read(0, 0x1007, &error)` |
| `SweetBattery::poll` | 582 | `sweet_spmi_read(0, 0x100d, &temperature)` |
| `SweetBattery::poll` | 600 | `sweet_spmi_read(0, 0x4805, &subtype)` |
| `SweetBattery::poll` | 601 | `readWord(0, 0x48c2, &rawCurrent)` |
| `SweetBattery::pollSid2` | 633 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_FG_RT_STS, &socSts)` |
| `SweetBattery::pollSid2` | 635 | `readBytePair(SWEET_BATT_SID, SWEET_BATT_FG_SOC, SWEET_BATT_FG_SOC_CP, &soc)` |
| `SweetBattery::pollSid2` | 637 | `readWord(SWEET_BATT_SID, SWEET_BATT_VBATT, &rawVoltage)` |
| `SweetBattery::pollSid2` | 648 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_SYS_BATT, &sysBatt)` |
| `SweetBattery::pollSid2` | 650 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_STATUS, &chgr)` |
| `SweetBattery::pollSid2` | 651 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_USBIN_RT_STS, &usb)` |
| `SweetBattery::pollSid2` | 736 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_STATUS2, &status2)` |
| `SweetBattery::pollSid2` | 737 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_STATUS5, &status5)` |
| `SweetBattery::pollSid2` | 738 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_STATUS7, &status7)` |
| `SweetBattery::pollSid2` | 739 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_ENABLE, &enable)` |
| `SweetBattery::pollSid2` | 740 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_PAUSE, &pause)` |
| `SweetBattery::pollSid2` | 741 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_CHGR_CFG2, &cfg2)` |
| `SweetBattery::pollSid2` | 742 | `sweet_spmi_read(SWEET_BATT_SID, SWEET_BATT_SMB_EN, &smbEn)` |
| `SweetBattery::pollSid2` | 743 | `readWordPair(SWEET_BATT_SID, SWEET_BATT_IBATT, SWEET_BATT_IBATT_CP, &rawCurrent)` |
| `SweetBattery::preflightIrq` | 768 | `sweet_spmi_read(2, 0x100u + i, &rev[i])` |
| `SweetBattery::preflightIrq` | 773 | `sweet_spmi_read(2, 0x4005u, &socSubtype)` |
| `SweetBattery::preflightIrq` | 786 | `sweet_spmi_read(2, 0x4000u + offsets[i], &regs[i])` |

共 51 个静态调用点（含包装函数调用）。

### bsd/dev/sweet_keys.c

SHA256 `b1e6bb81041ba0f108a9f8a2443bdbce0e6ec392b3d479931011d7418bb62bc7`

| 函数 | 行 | 调用 |
|---|---:|---|
| `keys_irq_diag_line` | 218 | `keys_read(apid, rtAddr, &rt)` |
| `keys_irq_diag_line` | 219 | `keys_read(apid, (uint16_t)(rtAddr + 1u), &type)` |
| `keys_irq_diag_line` | 220 | `keys_read(apid, (uint16_t)(rtAddr + 2u), &high)` |
| `keys_irq_diag_line` | 221 | `keys_read(apid, (uint16_t)(rtAddr + 3u), &low)` |
| `keys_irq_diag_line` | 222 | `keys_read(apid, (uint16_t)(rtAddr + 5u), &enabled)` |
| `keys_irq_diag_line` | 223 | `keys_read(apid, (uint16_t)(rtAddr + 8u), &latched)` |
| `keys_gpio_input_ok` | 246 | `keys_read(apid, (uint16_t)(rt + 0x30u), &mode)` |
| `keys_gpio_input_ok` | 247 | `keys_read(apid, (uint16_t)(rt + 0x36u), &en)` |
| `keys_gpio_input_ok` | 248 | `keys_read(apid, (uint16_t)(rt - 0x0bu), &subtype)` |
| `keys_thread` | 297 | `keys_read(keys_pon_apid, KEYS_PON_RT, &pon)` |
| `keys_thread` | 300 | `keys_read(keys_gpio_apid, KEYS_GPIO_RT, &gpio)` |
| `keys_thread` | 303 | `keys_read(keys_gpio2_apid, KEYS_GPIO2_RT, &gpio2)` |
| `sweet_keys_prepare` | 386 | `keys_read(keys_pon_apid, KEYS_PON_RT, &pon)` |
| `sweet_keys_prepare` | 391 | `keys_read(keys_gpio_apid, KEYS_GPIO_RT, &gpio)` |
| `sweet_keys_prepare` | 392 | `keys_gpio_input_ok(keys_gpio_apid, KEYS_GPIO_RT, &mode, &en, &subtype)` |
| `sweet_keys_prepare` | 406 | `keys_read(keys_gpio2_apid, KEYS_GPIO2_RT, &gpio2)` |
| `sweet_keys_prepare` | 407 | `keys_gpio_input_ok(keys_gpio2_apid, KEYS_GPIO2_RT, &mode2, &en2, &subtype2)` |
| `sweet_keys_prepare` | 478 | `keys_irq_diag_line("pon", keys_pon_apid, KEYS_PON_RT)` |
| `sweet_keys_prepare` | 482 | `keys_irq_diag_line("volume-up", keys_gpio_apid, KEYS_GPIO_RT)` |

共 19 个静态调用点（含包装函数调用）。

### iokit/Kernel/arm/SweetSpmiInterruptMux.cpp

SHA256 `0d6f9bb13748ef69d3aedb03c4e92de80e9902080468dd06472cfc30c1817deb`

| 函数 | 行 | 调用 |
|---|---:|---|
| `SweetSpmiInterruptMux::readPeripheral` | 160 | `sweet_spmi_read(route.sid, (route.per << 8) \| reg, out)` |
| `SweetSpmiInterruptMux::ackRoute` | 236 | `writePeripheral(route, 0x14u, (uint16_t)mask, 1)` |
| `SweetSpmiInterruptMux::disableHardware` | 279 | `writePeripheral(route, 0x16u, (uint16_t)(1u << route.bit), 1)` |
| `SweetSpmiInterruptMux::configureRoute` | 390 | `readPeripheral(route, 0x11u, &type)` |
| `SweetSpmiInterruptMux::configureRoute` | 391 | `readPeripheral(route, 0x12u, &high)` |
| `SweetSpmiInterruptMux::configureRoute` | 392 | `readPeripheral(route, 0x13u, &low)` |
| `SweetSpmiInterruptMux::configureRoute` | 393 | `readPeripheral(route, 0x15u, &enabled)` |
| `SweetSpmiInterruptMux::configureRoute` | 407 | `writePeripheral(route, 0x11u, type \| mask, 1)` |
| `SweetSpmiInterruptMux::configureRoute` | 408 | `writePeripheral(route, 0x12u, wantHigh, 1)` |
| `SweetSpmiInterruptMux::configureRoute` | 409 | `writePeripheral(route, 0x13u, wantLow, 1)` |
| `SweetSpmiInterruptMux::configureRoute` | 410 | `ackRoute(route)` |
| `SweetSpmiInterruptMux::configureRoute` | 414 | `writePeripheral(route, 0x14u, (uint16_t)(mask \| (uint16_t)(mask << 8)), 2)` |
| `SweetSpmiInterruptMux::configureRoute` | 419 | `readPeripheral(route, 0x11u, &gotType)` |
| `SweetSpmiInterruptMux::configureRoute` | 420 | `readPeripheral(route, 0x12u, &gotHigh)` |
| `SweetSpmiInterruptMux::configureRoute` | 421 | `readPeripheral(route, 0x13u, &gotLow)` |
| `SweetSpmiInterruptMux::configureRoute` | 422 | `readPeripheral(route, 0x15u, &gotEnable)` |
| `SweetSpmiInterruptMux::drainInterrupt` | 479 | `ackRoute(route)` |
| `SweetSpmiInterruptMux::removeGroup` | 676 | `writePeripheral(route, 0x16u, mask, 1)` |
| `SweetSpmiInterruptMux::removeGroup` | 676 | `ackRoute(route)` |
| `SweetSpmiInterruptMux::removeGroup` | 678 | `readPeripheral(route, 0x11u, &type)` |
| `SweetSpmiInterruptMux::removeGroup` | 679 | `readPeripheral(route, 0x12u, &high)` |
| `SweetSpmiInterruptMux::removeGroup` | 679 | `readPeripheral(route, 0x13u, &low)` |
| `SweetSpmiInterruptMux::removeGroup` | 680 | `writePeripheral(route, 0x11u, (type & ~mask) \| (route.oldType & mask), 1)` |
| `SweetSpmiInterruptMux::removeGroup` | 681 | `writePeripheral(route, 0x12u, (high & ~mask) \| (route.oldHigh & mask), 1)` |
| `SweetSpmiInterruptMux::removeGroup` | 682 | `writePeripheral(route, 0x13u, (low & ~mask) \| (route.oldLow & mask), 1)` |

共 25 个静态调用点（含包装函数调用）。

## 本次核对范围

只读核对源码与此前0146/0147验证记录，没有改驱动行为、增加回滚写或改接口。
0118手机日志仍不能证明读回失败的具体硬件原因。新内核/组合实机测试仍待进行。
