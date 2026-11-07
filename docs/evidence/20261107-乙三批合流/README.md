# 20261107-乙三批合流证据

乙对 PR48/PR49/PR50（H-037/H-038/H-039）合入 main 后的独立复跑证据。

- 合入前 main：`308a7ce`；合入后 HEAD：`f309622`；结果树：`496d47aa5dcdc0e5cff1af272f1fd0120768b5dd`
- 合并提交：`dfacdff`（PR48）、`ddca464`（PR49）、`f309622`（PR50）
- 环境：Qt 6.8.3 mingw_64 / MinGW 13.1.0 / Ninja，CMakePresets 的 debug、release 预设
- 用例口径：逐套件 XML 的 `Incident` 计数，与 `ctest` 的 24 个测试目标一致

## 目录

- `debug/*.xml`（24 份）、`debug/ctest.txt`、`debug/build.txt`
- `release/*.xml`（24 份）、`release/ctest.txt`、`release/build.txt`
- `validation.json`：合入顺序、树哈希、两配置逐套件计数、数字闭合与未合入说明

## 结果

| 配置 | 套件 | 通过 | 失败 | 跳过 | CTest |
| --- | --- | --- | --- | --- | --- |
| Debug | 24 | 540 | 0 | 0 | 24/24 |
| Release | 24 | 540 | 0 | 0 | 24/24 |

UI 103、nodecommands 69、ganttview 30。

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

只证明本机自动测试与构建。不代签真人鼠标/键盘/输入法、Windows DPI 高倍率、无 Qt 跨机、真实 API 联网；`QT_QPA_PLATFORM=offscreen` 下的无屏幕平台提示属正常噪声。编译告警 5 条为既存问题（`tst_nodecommands.cpp` 多字符常量、`QDateTime` 弃用），本批未引入。
