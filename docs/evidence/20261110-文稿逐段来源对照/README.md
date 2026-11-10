# H054 文稿逐段来源对照验证

被测源码为 `ca47eab3ad1b60e74e2e836b8ad66be2f37912d4`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [Debug](Debug.json) | 31 | 723 | 0 | 0 |
| [Release](Release.json) | 31 | 723 | 0 | 0 |
| [Release 离屏 1 倍](离屏-1.json) | 1 | 4 | 0 | 0 |
| [Release 离屏 1.5 倍](离屏-1.5.json) | 1 | 4 | 0 | 0 |
| [Release 原生 Windows](Windows-1.json) | 1 | 4 | 0 | 0 |

界面专项计数包含 QtTest 初始化和清理。覆盖逐段标记、同源段落独立确认、取消与保留未保存正文、提交单段和后退、主题删除后保存重开及旧稿待核对。来源无关的移动及非引用主题改动由回归覆盖。另有 13 份旧工程样例经 Release 工程校验器严格读取通过。[样例结果](样例读取.json)

全量依次运行 `cmake --preset debug`、`cmake --build --preset debug --parallel 4`、准备运行依赖及 `ctest --preset debug --parallel 2 --no-tests=error --output-on-failure`，再对 Release 重复。逐套 QtTest XML 与构建日志位于 `D:/mind/.worktrees/gantt-review/build/h054-final`。两份构建日志各记录原 `tests/tst_nodecommands.cpp` 的 5 条警告，改动范围格式检查通过。

Release 候选核验了 18 个运行文件，并在精简 PATH 和独立测试资料目录中原生启动 8 秒，从候选加载 Windows 平台插件，错误输出 0 字节。[候选启动](候选启动.json)

- [段落来源标记](01-段落来源标记.png)
- [原主题前后对照](02-原主题前后对照.png)
- [删除来源对照](03-删除来源对照.png)
