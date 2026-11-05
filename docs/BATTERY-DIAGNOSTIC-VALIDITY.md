# 0149：电池充电诊断的读取有效性

2026-11-05，基于精确原生0119源码。充电主档及no-usb对照仍state7/en0，
这次仅修复诊断可信度，不宣称恢复充电。补丁为
`xnu/patches/0149-mh2lm-battery-diagnostic-validity.patch`。

## 原因与边界

`sweet_spmi_read()`白名单不包含Type-C外设0x15，所以0x1506/0x1509/0x150b
在软件里返回-1，没有进入事务锁或MMIO。原来的布尔短路链在首个Type-C失败后
不再尝试SMB_EN/SMB_CFG，却将初始化零值打印出来。DIAG另有独立config-ok，
旧日志中它为1，因此其中的SMB_EN0x1648=0是有效证据。

0149逐字段读取，不扩大白名单、不增加充电寄存器写入、不更改标准电源属性、
SOC换算、consumer/mux/IRQ路径或事件采样时机。SPMI继续使用现有事务锁。
原先被短路的后续寄存器现在会实际尝试读取；只在sweetbattdiag开启时运行。

## 日志判读

失败以WARN记录sid/address/error；原始值打印unknown，解码位或ICL打印-1。
有效的零仍打印零，不能与unknown混淆。掩码置1表示对应读取成功：

| 日志 | 从bit0起的字段顺序 | 全部有效 |
| --- | --- | --- |
| DIAG config-valid | STATUS2、STATUS5、STATUS7、ENABLE、PAUSE、CFG2、SMB_EN | 0x7f |
| INPUT read-ok | PATH、APSD、CMD_IL、ICL | 0xf |
| TYPEC read-ok | AICL、SNK、STATE、MISC | 0xf |
| SMB read-ok | ENABLE、CFG | 0x3 |

TYPEC标签里的AICL实际来自USBIN。当前白名单下预期TYPEC mask为0x1，三个
Type-C字段unknown；这是已知软件访问边界，不是新测得的硬件故障。
SMB_CFG现在独立读取，不能预先猜它的值。DIAG保留config-ok/current-ok，
IBATT配对失败时raw/signed/µA均打印unknown；有效时保留原符号和单位。

## 本地证据与待验证项

工作区 `scratch/battery-diag-validity-0119/` 保存精确基线、修改源、全新补丁
应用核对、ARM64对象和编译命令/日志。严格git apply检查通过，核对23个文件
与修改树逐字节一致。单文件编译使用现有CI头文件/参数与Linux clang19本地
兼容宏；不是Apple工具链完整构建/链接。snprintf已经在原生0119内核导出。

- 新测试提取实际诊断函数，在ASAN/UBSAN下逐一注入字段失败，验证后续读取
  不短路、掩码/unknown及IBATT正负/零/无效输出。测试不提供写入/发布API。
- SPMI串行化测试新增：即使模拟提供Type-C APID映射，白名单仍在锁/MMIO前
  拒绝三个地址；其余现有初始化/串行化/故障场景及10次并发重复均通过。
- 实际mux/consumer测试通过，包括按键/电池接入、隔离、超时、600次正常IRQ、
  生命周期故障场景和80次并发重复。主机模拟不能证明硬件时序或真实充电。
- CircleCI YAML解析通过；源码门禁接受0149、拒绝旧0119。仅增加门禁标记，
  原编译命令未变。

下一步用户push后一次云编译；下载新产物再打包多档诊断，优先补SMB_CFG证据，
调查禁充继承状态及安全初始化。CHGR自然事件、物理充电和实机回归尚待验证。
