# H057 节点边框与中文配色验证

被测源码为 `d84596188348bdc7933ae5db6bfb2beb9cbdb7cf`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [Debug](Debug.json) | 31 | 732 | 0 | 0 |
| [Release](Release.json) | 31 | 732 | 0 | 0 |
| [Release 离屏 1 倍](离屏-1.json) | 1 | 14 | 0 | 0 |
| [Release 离屏 1.5 倍](离屏-1.5.json) | 1 | 14 | 0 | 0 |
| [Release 原生 Windows 1 倍](Windows-1.json) | 1 | 14 | 0 | 0 |
| [Release 原生 Windows 1.5 倍](Windows-1.5.json) | 1 | 14 | 0 | 0 |

专项计数包含 QtTest 初始化和清理。覆盖导图、鱼骨图、流程图的新增形状、长标题、端口贴边、撤回、恢复默认及保存重开。验证工作表默认样式的优先级、已保存自定义色精确保留、中文色名、滑块取消和确认，以及样式窗口和外框概要的色彩编辑。

按配置执行 `cmake --preset debug`、`cmake --build --preset debug --parallel 4`、准备运行依赖及 `ctest --preset debug --parallel 2 --no-tests=error --output-on-failure`，Release 同样验证。逐套 QtTest XML 和构建日志位于 `D:/mind/.worktrees/gantt-review/build/h057-final`。两个配置各记录 `tests/tst_nodecommands.cpp` 其他行的 5 条既有告警。改动范围通过 clang-format 19.1.7 检查。

Release 严格读取 13 份旧工程样例。候选的 18 个运行文件通过哈希校验，在精简 PATH 和隔离资料目录中原生启动 8 秒，加载候选自带 Windows 平台插件，错误输出 0 字节。[样例读取](样例读取.json) · [候选启动](候选启动.json)

- [流程图边框和中文配色](01-流程边框和中文配色.png)
- [自定义选色滑块](02-自定义选色滑块.png)
