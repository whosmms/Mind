# H050 复习页批量选取验证

被测源码为 `b91c246d549318a0ef011c28116e6fcee3c8fe20`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| Debug CTest 与 QtTest | 31 | 702 | 0 | 0 |
| Release CTest 与 QtTest | 31 | 702 | 0 | 0 |
| Release 离屏复习专项 | 1 | 5 | 0 | 0 |
| Release 原生 Windows 复习专项 | 1 | 5 | 0 | 0 |

在冻结环境下使用 `cmake --preset debug`、`cmake --build --preset debug --parallel 2` 和 `ctest --preset debug --no-tests=error`，Release 将预设名换为 `release`。两个 CTest 结果均为 31/31。逐套件的 QtTest XML 统计包含初始化、清理与数据行。复习专项运行 `build/release/tests/tst_ui.exe offlineReviewPicksTopics offlineReviewGradesAndSourceChanges -o 复习专项.xml,xml`，预期 5 项通过。

- [Debug 汇总](Debug.json) · [Release 汇总](Release.json) · [离屏复习专项](离屏复习专项.xml) · [原生 Windows 复习专项](原生Windows复习专项.xml)
- [主题选取截图](从导图选取复习主题.png) · [候选启动校验](候选启动.json)

实际鼠标回归覆盖按工作表分组、勾选思维导图及鱼骨图主题、已加入和缺答案提示、取消、保留进行中的翻面状态和空闲时开始复习新卡。命令回归覆盖选择顺序、重复和无答案拒绝、整批一次撤回、前进和保存重开。候选精简 PATH 启动 8 秒，加载随包 Windows 平台插件，错误输出为空。
