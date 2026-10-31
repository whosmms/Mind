# H-035乙评审整改证据（甲，2026-10-31）

产品被测：`594b259c5317d7de1385ff1e9bd372eca3e2f7e3`；含普通同步main7f2cf05（ff4d179），未合main。冻结Qt6.8.3、MinGW13.1.0、CMake3.30.5、Ninja1.12.1、clang-format19.1.7。

## 真实结果

| 检查 | Debug | Release |
|---|---|---|
| 构建、依赖部署、CTest | 成功，24/24 | 成功，24/24 |
| 逐套XML | 24套件526通过、0失败0跳过 | 24套件526通过、0失败0跳过 |
| UI / 甘特 / 节点命令 | 97 / 28 / 63 | 97 / 28 / 63 |

全量48份XML与构建/部署/CTest输出在debug、release；validation.json记录源提交及逐套退出码。全量期间HEAD保持，只有既有断言扩展，没有新增用例计数。增量构建未出现编译告警，不代表未重编历史代码告警已清除。Qt offscreen插件的字体/布局提示保留在XML，不隐去。

- `h035-b-review-negative.xml`：有意把定位查询的false参数去掉，第二次筛选外定位误读临时全量，实际1项、期望3项，tst_ui.cpp:4712实红；2通过/1失败含init/cleanup。负对照不是待交付产品失败。
- `h035-b-review-targeted-final.xml`：恢复原始字节、哈希核对后重编，四组专项加init/cleanup，共6通过、0失败0跳过。
- `h035-b-review-candidate.xml`：样例/截图专项4通过、0失败0跳过，含init/cleanup，与全量526不相加。
- 候选程序校验.json：`D:/mind/人工验收/甘特定位体验_20261031_04`，19运行文件含qt.conf逐项SHA-256、7样例哈希；精简PATH、Windows平台启动3秒保持运行后，仅终止本轮自己启动的进程。
- 三张900×620截图实际查看；字体宽度断言已经移入默认全量，不再依赖截图环境变量。

## 复现命令

在对应提交工作区先执行`./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools`，TEMP/TMP指向工程内`build/environment/temp`。对debug、release分别执行：

```powershell
cmake --preset debug
cmake --build --preset debug --parallel 2
./tools/准备运行依赖.ps1 -Configuration debug -ToolsRoot D:/mind/.tools
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_PLUGIN_PATH=(Resolve-Path build/debug).Path
ctest --preset debug --no-tests=error --output-on-failure
./build/debug/tests/tst_ui.exe ganttSheetFilterKeepsProjectAndHistory offlineTasksMilestoneAndSelection offlineCheckLocatesAndRefreshes ganttClickKeepsPageDuringDrag
```

Release将debug替换为release；全量数字通过逐个运行24个tst_*.exe导出XML核对。原始脚本/日志位于`build/验证H035乙评审整改.ps1`、`build/h035-b-review-final-evidence`、`build/h035-b-review-validation-console.log`，反证脚本`build/验证H035连续定位反证.ps1`用finally精确恢复本轮自有源码，未reset用户改动。

旧9f27d02/526和候选03、4a64384/526和候选02均保留；乙原复核仅覆盖旧657c56d/4a64384，本次增量仍待乙接收及丙体验复核。未代签真人/DPI/跨机或M3/M4，无新增本人操作步骤。
