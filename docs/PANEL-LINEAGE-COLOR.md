# 0226：普通 SDR 默认色彩/锐度恢复

2026-11-14。实现提交 **`ba8a38a`**。D09 的普通 SDR Auto 默认分支已实现并通过本地验证，**[a36产物与镜像](PANEL-D1-0121A36.md)已核对打包，待实机**。最新实机基线仍是[a35/0225](PANEL-D1-0121A35.md)，带已知且暂缓处理的BUG-007。补丁 `xnu/patches/0226-mh2lm-lineage-sdr-color-restore.patch`，SHA256 `e3f37195b56fb0a5f6f10ab500b29f35929759419d76ba7fa77f883f6c608f59`。

## 本批行为与边界

此前mode2/7普通ON只恢复ON表及亮度，缺少Lineage在首次像素提交前执行的色彩/锐度恢复。本批顺序为：

```text
ON表（LP，原始字节与等待保持）→取得display command-engine引用
→SDR Auto色彩/锐度六包（HS，全部真实完成）
→重建/恢复本轮像素流→首帧
→0225排空/HS亮度→同generation第二帧→触摸/FB完成
```

当前XNU明确采用 `screen_mode=Auto(0)`、HDR关闭、RGB preset2/三个offset0、锐度关闭。尚无对应的iOS动态色彩客户端；本批不虚构Android用户态模式，也不宣称支持HDR/专家模式/动态RGB或锐度调节。四个profile独立选择；不根据未知身份默认选PV。原ON/OFF表、旧模式、首次接管保留ABL色彩的路径保持，第一次普通唤醒才执行此恢复；首次显示色彩与独立初始化仍属剩余范围。

六包依次为 `B0 00 → CE（67bytes）→ B0 03 → B0 00 → DD 32 06 07 65 → B0 03`，保留两组命令各自的bank选择/恢复。CE走现有物理DMA，其余五包走FIFO；只有PV在CE之后等待20ms，PP1/DV1/DV2无此等待。每份序列含头共122bytes。PP1/DV1/DV2序列SHA为`ca2913449c173cb111d6bd03190c544ec7141d2e108dfe7ec247f6c3c8971b82`，PV为`7edc6ea7d28b8506e3af4aaa6f0ad94ae308bf9769ff6070029afb9fa21d50dc`。

增加尾部枚举 `SweetPanelColorRestore`，阶段预算500ms，原16s总预算保持；公有状态结构、旧枚举值、ABI608、ADT和605byte参数不变。命令期间逐次检查participant、触摸quiesce、reset、资源票、display引擎引用、物理IRQ排空、TE/autorefresh及CTL状态。`SweetPanelLifecycleColorRestored`仅在同token的六包成功、无硬件不确定状态后发布；未得到该凭据不能开始像素流。命令完成不代表画面或锁屏内容就绪，原两次新帧/亮度/触摸门槛保持。

故障沿既有关闭路径保留资源、扫描池和不确定DMA pin，不继续发后续命令或自动恢复。Lineage的void色彩mapper忽略首组错误、仍尝试锐度；XNU把两组三包放进一次有期限的事务，任一失败即停，是显式的错误语义差异。继续复用启动prepared4KiB缓冲，活动路径不新增该缓冲分配；其余序列对象生命周期沿用原实现。

## 参考与验证

固定Lineage `2e897dddf49a4c3e85b2ff77fdf7c70f860398dd`。实际 `lge_dsi_panel_mode_set` 的 `!(backup ^ complete)` 在两者默认false时成立；四份DTS均启用color-manager且未启用 `lge,ddic-register-backup`。OFF→ON的set_nolp分支进入mode_set，r66456_ops绑定screen_mode→display_control。`cm_preset_step=2`来自实际初始化，其余默认字段来自kzalloc；本批采用该内核默认策略，不把它等同于Android用户态最终选择。

`tools/tests/test-mh2lm-lineage-color-reference.py`提取并执行九个未改写的LG函数，使用固定源码的宏、preset表、mode表及四份DTS：mode_set、screen_mode、display_control、色彩表/RGB/色调/饱和度/锐度mapper和实际vendor transfer。MMIO/DRM/CCF及iOS色彩策略未执行；caller、初始化和DTS条件为静态审计。实际输出作为 `tools/tests/fixtures/mh2lm-panel-sdr-color.json` 的独立wire期望，再与XNU头文件及实际驱动发包逐字节比较。

| 验证 | 结果 |
|---|---|
| 参考执行 | 四profile默认六包HS及各自等待；color-manager/backup条件；HDR、专家模式、RGB调整会改变输出；LG忽略错误行为另记 |
| mode2/7实际驱动 | 四profile、三轮普通OFF/ON及原HS/ULPS/管线/完成回归；色彩序列在ON表之后、首帧之前，亮度请求期间更新仍保留 |
| 新阶段错误出口 | 每模式20个生命周期故障点组合；FIFO/DMA各四种完成故障；stale DONE及六种child准入条件；未提前报告色彩/像素/触摸完成，无晚到写入 |
| 跨层完成 | 每模式120份实际MDSS状态经真实FB消费，包含色彩pending；18份坏凭据及mode7额外6种generation/capability错误被拒绝 |
| 兼容与编译 | 共14组驱动运行，包括旧mode6、host、sequence、同步DCS/亮度与缓冲/引用；ARM64基线与修改均成功，1596→1596且warning类别数量一致 |
| 补丁与识别 | 0185–0226共42patch严格重放，无offset/fuzz，80文件逐字节一致；144项source markers；新对象含两条色彩marker；真实旧a35打包被拒绝且无镜像输出 |

CircleCI仅追加三条源码断言，执行器、参数、资源及工作流保持；本地YAML、四处合法参数模板、六段shell语法和全部源码断言通过，没有新增裸 `<<`。打包工具增加两条kernel识别条件。没有运行CircleCI服务器验证、触发CI或push；本地检查不等于云端已成功构建。

证据目录 `scratch/framebuffer-0226/`：`color-reference.json/.c`、`test-color-reference.log`、14份 `test-*.log`、`fb*/mdss-status.json`、`verification.json`、`cumulative-replay.json`、`ci-check.json`、`final-review.json`及对应脚本。a35镜像SHA保持 `34a35e7d1bc4e39fccf05502a4472aee800c1712c19126a2b3675a8260454280`。

## 0121a36 产物交付

[a36](PANEL-D1-0121A36.md)的80份受检源码与本批最终SHA一致，144source/251kernel markers通过；两档ABI608镜像与内部kernel/ADT/TOML回读通过，a35参数和旧镜像保持。下一步按该页先USB采集，再临时启动pipeline档验证；当前仍无本批实机结果。以下新CI安排为交付前历史。

## 原CI与实机安排（历史，当前命令见a36）

用户push包含`ba8a38a`的main并触发CircleCI，下载新kernel/buildtree，建议 **0121a36**；agent核对0226及内核marker后沿原ABI608打包mode7。当前没有新内核或新boot镜像，不重复测试同版a35。

实机先检查首次接管、桌面和持续更新，再两轮息屏/唤醒，观察颜色/锐度、亮度和触摸是否出现新异常，每轮正常后保留至少60秒。ON表仍是PV的17包/4物理DMA；之后新增六包HS/1物理DMA，再generation2/3和原首帧后亮度流程。应有 `SDR Auto color/sharpness HS admitted ... six commands before pixels`、`... HS complete; pixels pending`，每轮generation只增加一次。保存USB和持久日志；若未开USB，按实际可用证据记录，不自动判USB失败。

BUG-007唤醒闪现旧画面按用户要求暂缓，本批不以它的消失为验收条件，也不承诺修复。D09动态状态/用户接口、首次接管色彩、DSPP/HDR、完整C08动态票/共享LP/core/SMMU/QoS及其余PD4仍待推进；具体边界见[全流程矩阵](PANEL-LINEAGE-FULL-FLOW-AUDIT.md)。
