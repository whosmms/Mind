# H052 编辑与复习增强验证

被测源码为 `a8a9d358afa1d83ab8fc1596c1d0ef5d51c639db`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [Debug](Debug.json) | 31 | 717 | 0 | 0 |
| [Release](Release.json) | 31 | 717 | 0 | 0 |
| [Release 离屏 1 倍](离屏-1.json) | 1 | 10 | 0 | 0 |
| [Release 离屏 1.5 倍](离屏-1.5.json) | 1 | 10 | 0 | 0 |
| [Release 原生 Windows](Windows-1.json) | 1 | 5 | 0 | 0 |

逐套统计包含 QtTest 的初始化、清理和数据行。离屏专项覆盖大纲预览与撤回、四类画布拖放与粘贴、分支专注退出、今日到期队列、历史副本拒绝覆盖。原生 Windows 专项覆盖分支专注、今日复习和历史副本。13 份已有工程样例均由发布配置的工程校验器读取通过。[样例结果](样例读取.json)

全量验证依次运行 `cmake --preset debug`、`cmake --build --preset debug --parallel 2`、`tools/准备运行依赖.ps1` 和 `ctest --preset debug`，再对 Release 重复。逐套 QtTest XML 和初轮视口偏移的诊断留在 `D:/mind/.worktrees/gantt-review/build/h052-final` 与 `build/h052-dev`。最终双配置构建日志没有编译告警，改动范围通过格式检查。

Release 候选已校验 18 个运行文件。清除 Qt 插件路径并缩减 PATH 后，程序在独立测试资料目录下原生启动并存活 8 秒，从候选目录加载 Windows 平台插件，错误输出为 0 字节。[候选启动记录](候选启动.json)

- [大纲预览](01-大纲预览.png)
- [画布粘贴截图](02-画布粘贴截图.png)
- [分支专注](03-分支专注.png)
- [今日待复习](04-今日待复习.png)
- [本机版本历史](05-本机版本历史.png)

系统级拖放、真实截图剪贴板和无 Qt 的其他电脑统一记录在最终验收。
