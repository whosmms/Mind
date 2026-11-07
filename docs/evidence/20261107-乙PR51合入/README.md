# 20261107-乙PR51合入证据

乙将 PR51（`feature/a/UI-home-and-properties`，tip `cb7657f`）整支合入 main 后的独立复跑证据。

- 合入前 main：`0bee9b6`（PR48/49/50 三批已合）
- 合并提交：`6496dbe`，父 `0bee9b6` + `cb7657f`
- 合并结果树：`9e1ed95ee1bd4bc11c80d9387f14347da0a2265e`
- 环境：Qt 6.8.3 mingw_64 / MinGW 13.1.0 / Ninja，`CMakePresets` 的 debug、release 预设
- 用例口径：逐套件 XML 的 `Incident` 计数，与 `ctest` 的 29 个测试目标一致

## 目录

- `debug/*.xml`（29 份）、`debug/ctest.txt`、`debug/build.txt`、`debug/smoke-stderr.txt`
- `release/*.xml`（29 份）、`release/ctest.txt`、`release/build.txt`
- `validation.json`：合并参数、冲突处理、编号顺延、数字、回退方式

## 结果

| 配置 | 套件 | 通过 | 失败 | 跳过 | CTest |
| --- | --- | --- | --- | --- | --- |
| Debug | 29 | 595 | 0 | 0 | 29/29 |
| Release | 29 | 595 | 0 | 0 | 29/29 |

UI 122、nodecommands 69、ganttview 32。

相比合入前的 24 套件 540 项，净增 5 个测试目标、55 项：`tst_presentation`、`tst_export`、`tst_aicompletion`、`tst_appearance`、`tst_search`，且无套件消失。

## 启动冒烟

`QT_QPA_PLATFORM=offscreen ./lingya.exe` 运行 10 秒后被超时终止（退出码 124），stderr 为空，说明能正常进入事件循环、未在启动阶段崩溃。

## 复现

```bash
export PATH="<Qt>/6.8.3/mingw_64/bin:<Qt>/Tools/mingw1310_64/bin:<Qt>/Tools/Ninja:<Qt>/Tools/CMake_64/bin:$PATH"
export QT_KIT_DIR="<Qt>/6.8.3/mingw_64"
cmake --preset debug && cmake --build --preset debug --parallel 8
ctest --preset debug --output-on-failure
# 逐套件 XML：在 build/debug/tests 下对每个 tst_*.exe 执行
#   QT_QPA_PLATFORM=offscreen ./tst_x.exe -o tst_x.xml,xml
```

## 边界

只证明本机自动测试、构建与启动冒烟。不代签真人鼠标/键盘/输入法、Windows DPI 高倍率、无 Qt 跨机、真实 API 联网；`offscreen` 平台下的无屏幕提示属正常噪声。Debug 5 条告警为既存问题（`tst_nodecommands.cpp` 多字符常量、`QDateTime` 弃用），本批未引入，Release 0 告警。
