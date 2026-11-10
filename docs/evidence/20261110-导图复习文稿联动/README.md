# H053 导图、复习与文字稿联动验证

被测源码为 `006c52ed0463c6d2751f3d4a968bcbfda6cb9c78`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [Debug](Debug.json) | 31 | 720 | 0 | 0 |
| [Release](Release.json) | 31 | 720 | 0 | 0 |
| [Release 离屏 1 倍](离屏-1.json) | 1 | 5 | 0 | 0 |
| [Release 离屏 1.5 倍](离屏-1.5.json) | 1 | 5 | 0 | 0 |
| [Release 原生 Windows](Windows-1.json) | 1 | 5 | 0 | 0 |

专项计数包含 QtTest 初始化和清理。三条界面路径覆盖分支到期队列和建卡范围、卡片对照取消与确认和跨表定位，以及分支离线稿件预览、从稿件选题、保留未提交编辑和重开定位。另有 13 份旧工程样例经 Release 工程校验器严格读取通过。[样例结果](样例读取.json)

全量依次运行 `cmake --preset debug`、`cmake --build --preset debug --parallel 3`、运行依赖准备和 `ctest --preset debug`，再对 Release 重复。逐套 QtTest XML 与构建日志位于 `D:/mind/.worktrees/gantt-review/build/h053-final`。两份构建日志无编译告警，改动范围格式检查通过。

Release 候选核验了 18 个运行文件，并在精简 PATH 和独立测试资料目录中原生启动 8 秒，从候选加载 Windows 平台插件，错误输出 0 字节。[候选启动](候选启动.json)

- [分支复习](01-分支复习.png)
- [卡片来源对照](02-卡片来源对照.png)
- [分支文稿预览](03-分支文稿预览.png)
- [从文稿选题建卡](04-从文稿选题建卡.png)
