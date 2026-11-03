# H-036甘特状态提示文案证据（甲）

被测产品`eec56150c7613ade7471dc80ee2d8a8c8eda7096`，基线main8596415。冻结Windows Qt6.8.3/MinGW13.1.0/CMake3.30.5/Ninja1.12.1/clang-format19.1.7。

| 检查 | Debug | Release |
|---|---|---|
| 构建/部署/CTest | 成功，24/24 | 成功，24/24 |
| 逐套件XML | 24套件526通过、0失败0跳过 | 24套件526通过、0失败0跳过 |
| UI/甘特/节点命令 | 97/28/63 | 97/28/63 |

48份全量XML在debug、release，validation.json记录源SHA、逐套退出码及统计；构建、部署、CTest文本同目录。只扩展原UI断言，未新增计数；本轮增量构建未出现编译告警，offscreen字体/布局插件提示保留原XML，不隐去。验证期间本工作区HEAD保持。

- `h036-targeted.xml`：四组专项加init/cleanup共6通过、0失败0跳过。
- `h036-candidate.xml`：样例/截图专项加init/cleanup共4通过、0失败0跳过；与全量不相加。
- 四张PNG分别为恢复筛选/临时全量的900/800窗口，均实际查看。默认全量也执行两档宽度断言，截图保存才由环境变量控制。
- QINFO保留每次实测文本/可用宽度、字体、逻辑DPI与完整提示。当前Microsoft YaHei 10、96逻辑DPI，文本446/548px；900档可用880px，800档780px。断言失败将输出同样信息，不通过增加5%余量隐藏真实溢出。
- 候选程序校验.json对应`D:/mind/人工验收/甘特状态提示_20261031`：19文件含qt.conf逐项SHA-256、7样例重建/严格重开；精简PATH Windows平台启动3秒通过，只终止本轮自己启动的进程。
- 日常主线验证.json/日常主线程序校验.json单独对应main8596415：本轮另独立双配置24/526/0/0，原始48XML在app/build/main-h035-sync-evidence；16运行依赖、两程序、入口及两配置精简PATH启动各3秒。与待审eec候选结果分组，不混为main已含本批文案。

复验时在对应提交先执行`./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools`，TEMP/TMP指向工程内`build/environment/temp`，设置`QT_QPA_PLATFORM=offscreen`和`QT_PLUGIN_PATH=build/debug`绝对路径，再执行：

```powershell
cmake --preset debug
cmake --build --preset debug --parallel 2
./tools/准备运行依赖.ps1 -Configuration debug -ToolsRoot D:/mind/.tools
ctest --preset debug --no-tests=error --output-on-failure
./build/debug/tests/tst_ui.exe ganttSheetFilterKeepsProjectAndHistory offlineTasksMilestoneAndSelection offlineCheckLocatesAndRefreshes ganttClickKeepsPageDuringDrag
```

Release将debug替换为release；全量数字来自逐个运行24个tst_*.exe导出XML。原始驱动/日志在build/验证H036状态文案.ps1、build/h036-status-copy-evidence、build/h036-validation-console.log；候选生成脚本build/归档H036状态文案.ps1。本批P3属文案改善，未把旧文案伪造为缺陷红测。

仍待乙数据与生命周期复核、丙体验接收，不代签真人/Win DPI/输入法/无Qt跨机或M3/M4；无新增本人步骤。H-035历史证据和候选全部保留。
