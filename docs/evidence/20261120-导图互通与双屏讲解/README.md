# H059 导图互通、查找替换与双屏讲解

产品源码提交为 `c2cd4220717284bf0fe7ebf779f36f604cab96e0`。QtTest 项数包含数据行及初始化、清理。两配置和专项分别统计。

| 环境 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| [冻结 Debug](Debug.json) | 32 | 759 | 0 | 0 |
| [冻结 Release](Release.json) | 32 | 759 | 0 | 0 |
| [离屏](离屏-1.json) | 1 | 9 | 0 | 0 |
| [原生 Windows](Windows-1.json) | 1 | 9 | 0 | 0 |
| [原生 Windows 加 1.5 倍 Qt 缩放](Windows-1.5.json) | 1 | 9 | 0 | 0 |
| [虚拟双屏](双屏-离屏.json) | 1 | 3 | 0 | 0 |

13 份旧 .lingya 通过严格读取。运行清单包含 19 个文件，逐项核对 SHA256。精简 PATH 下本机原生启动存活 8 秒，从候选目录加载 Qt6Svg.dll 和 qwindows.dll，错误输出 0 字节。 [样例读取](样例读取.json) · [候选启动](候选启动.json)

界面截图见 [XMind](modern.xmind.png)、[OPML](outline.opml.png)、[FreeMind](mind.mm.png)、[SVG 导出](SVG导出.png)、[查找替换](查找和替换.png)、[演讲者屏](演讲者屏.png)、[观众屏](观众屏.png)。

## 支持范围

- 从「文件 → 导入导图…」读入 XMind、OPML、FreeMind，在保留未保存保护的情况下打开新工程。合成样例覆盖新版与旧版 XMind、中文、层级、工作表顺序、折叠、备注、关系线及受支持的图片。OPML 和 FreeMind 各有真实格式的合成文件，覆盖多层级、备注和网页链接。三种格式通过界面另存成 .lingya 并严格重开，源文件哈希与导入前相同。专有样式、附件、特殊链接和加密工作簿仍有兼容边界。
- 在「导出工作表」选择 SVG，导出可见画布。渲染测试逐项检查矢量路径、中文文字、缩放后的视框、内嵌 PNG、颜色像素和写入失败保留已有文件。界面导出前后工程和会话修订相同。
- Ctrl+H 打开查找和替换。检查替换所选、全部预览、取消、批量提交与一次撤销。命令测试覆盖树图、流程图、独立任务、关联任务、旧工程中的多任务关联、过期计划、超长文本和无变化短路。
- 讲解路线可以选择其他显示器作为观众屏。虚拟双屏使用不同尺寸和负横坐标，检查两窗各处于各自几何范围、全屏、翻页同步、私有备注和计时只出现在演讲者窗口、Esc 同时退出，原工程和编辑视口恢复。单屏模式另有播放与长备注滚动回归。

## 复验方法

在仓库根目录执行。两个配置均使用 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5、Ninja 1.12.1。

```powershell
. ./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools
$env:QT_QPA_PLATFORM = 'offscreen'
foreach ($configuration in @('debug','release')) {
    cmake --preset $configuration
    cmake --build --preset $configuration --parallel 4
    ./tools/准备运行依赖.ps1 -Configuration $configuration -ToolsRoot D:/mind/.tools
    ctest --preset $configuration --parallel 2 --no-tests=error --output-on-failure
}
$env:QT_QPA_PLATFORM = 'offscreen:configfile=tests/fixtures/presentation-screens.json'
./build/release/tests/tst_ui.exe presentationScreenSeparation -o presentation-dual.xml,xml
```

Qt 的虚拟屏平台参数使用仓库内相对路径。双屏截图读取本机已有的 Windows 字体，随包内容未新增字体文件。实机原生启动和 Qt 缩放下的界面操作已有本机证据，另一台无 Qt 的 Windows、真实显示器、第三方软件产生的各类真实文件统一进入最终人工验收。 [最终统一验收](../../最终统一验收.md)

主要格式和工具依据见 [导入与导出决定](../../decisions.md#d-io-20261120-001-导入新工程与矢量导出)、[Qt 6.8.3 虚拟屏实现](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/offscreen/qoffscreenintegration.cpp) 和 [第三方组件登记](../../third-party.md)。
