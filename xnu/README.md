# XNU for MH2LM

更新：2026-11-15。LG G8X 使用 xnu-8796 世代、16 KiB 页的单体内核，运行 iPhone X iOS 16.7.16 用户态。当前补丁序列到0229，已验收产物为0121a39；状态见[统一入口](../docs/STATUS.md)。

- 活跃 CI 为 **CircleCI**，项目配置指向 [`.github/workflows/circleci.yml`](../.github/workflows/circleci.yml)。目标 `machine_config=MH2LM`、`kernel_config=DEVELOPMENT`；GitHub Actions 的 sweet/guacamole 命令是历史流程。
- CI 使用固定版本的 [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build)，顺序应用 `patches/*.patch` 并检查源码标记。当前本地修改从上一已验证结果树生成补丁，严格重放、比较源码并做相关验证。
- 产物为 `kernel.development.mh2lm` 与 SHA256 清单、`xnu-buildtree` 源码/编译上下文，以及构建日志。工作区命名为 `logs/xnu-kernel-<LABEL>.gz` 和 `logs/xnu-buildtree-<LABEL>.gz`；扩展名 `.gz` 的文件实际是 gzip 压缩 tar 包。
- 产物要核对包完整性、源码与预期补丁结果、源码/内核标记及 SHA。仅凭文件名或 CI 成功不足以证明拿到了新版本；a39 曾误下载成与 a38 相同的包，重下后已解决。
- 本地单文件 ARM64 交叉编译用于提前发现错误，须沿用真实组件 include 路径并解释兼容调整；不能代替 CI 和实机验收。

详细步骤、ABI608 镜像打包、回读与日志采集见[构建指南](../docs/BUILD-GUIDE.md)。用户负责 push、下载和设备操作；agent 负责补丁、验证、打包及本地提交。原始 Apple 文件、编译树和内核产物不入 git。
