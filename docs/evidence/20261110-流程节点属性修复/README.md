# H056 流程节点属性修复验证

被测源码为 `45651d4fa6e2c5aa7e301c8e9d59bf5dac9b5c43`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [Debug](Debug.json) | 31 | 728 | 0 | 0 |
| [Release](Release.json) | 31 | 728 | 0 | 0 |
| [Release 离屏 1 倍](离屏-1.json) | 1 | 9 | 0 | 0 |
| [Release 离屏 1.5 倍](离屏-1.5.json) | 1 | 9 | 0 | 0 |
| [Release 原生 Windows 1 倍](Windows-1.json) | 1 | 9 | 0 | 0 |
| [Release 原生 Windows 1.5 倍](Windows-1.5.json) | 1 | 9 | 0 | 0 |

专项计数包含 QtTest 初始化和清理。验证流程节点颜色和角色、点击图标时保留备注草稿、非法链接校验、备注与链接单次提交及后退前进、保存重开。验证标签和批注入口、连线选中时的默认样式，以及四类工作表切换时的属性目标。原生高缩放下先按实际操作打开自动收起的属性栏，再完成点击。[旧版复现](旧版复现.json)

按配置执行 `cmake --preset debug`、`cmake --build --preset debug --parallel 4`、准备运行依赖及 `ctest --preset debug --parallel 2 --no-tests=error --output-on-failure`，Release 同样验证。逐套 QtTest XML、复验与构建日志位于 `D:/mind/.worktrees/gantt-review/build/h056-final`。先前的构建日志 `prebuild.log` 各记录未改动的 `tests/tst_nodecommands.cpp` 的 5 条既有告警。改动范围通过 clang-format 19.1.7 检查。

Release 严格读取 13 份既有工程样例。候选的 18 个运行文件通过哈希校验，在精简 PATH 和隔离资料目录中原生启动 8 秒，加载候选自带的 Windows 平台插件，错误输出 0 字节。[样例读取](样例读取.json) · [候选启动](候选启动.json)

- [流程节点样式](01-流程节点样式.png)
- [流程节点备注与链接](02-流程节点备注链接.png)
