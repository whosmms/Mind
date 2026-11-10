# H055 流程连线自适应曲线验证

被测源码为 `be115179253ebc8804d2e6183da548d1a99224b6`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [Debug](Debug.json) | 31 | 725 | 0 | 0 |
| [Release](Release.json) | 31 | 725 | 0 | 0 |
| [Release 离屏 1 倍](离屏-1.json) | 1 | 14 | 0 | 0 |
| [Release 离屏 1.5 倍](离屏-1.5.json) | 1 | 14 | 0 | 0 |
| [Release 原生 Windows 1 倍](Windows-1.json) | 1 | 14 | 0 | 0 |
| [Release 原生 Windows 1.5 倍](Windows-1.5.json) | 1 | 14 | 0 | 0 |

专项计数包含 QtTest 初始化和清理。覆盖端口相向直连、错位和同侧曲线、绕行路径的可见笔画避开中间节点、拖动期间曲线跟随与工程不变、松手一次提交、后退和前进。还复验两端重接、无效落点和旧手动中点存盘兼容。旧显示在五组曲线场景和拖动预览检查上失败。[旧显示反证](旧显示反证.json)

全量执行 `cmake --preset debug`、`cmake --build --preset debug --parallel 4`、准备运行依赖、`ctest --preset debug --parallel 2 --no-tests=error --output-on-failure`，并对 Release 执行同等流程。逐套 QtTest XML 与构建日志位于 `D:/mind/.worktrees/gantt-review/build/h055-final`。改动范围通过 clang-format 19.1.7 检查。

Release 严格读取 13 份旧工程样例。候选的 18 个运行文件通过哈希校验，并在精简 PATH 和隔离资料目录中原生启动 8 秒，加载候选自带 Windows 平台插件，错误输出 0 字节。[样例读取](样例读取.json) · [候选启动](候选启动.json)

- [拖动节点时曲线跟随](拖动节点时曲线跟随.png)
- [流程圆滑避障](流程圆滑避障.png)
- [端口水平直连](端口水平直连.png)
- [端点重连](端点重连.png)
