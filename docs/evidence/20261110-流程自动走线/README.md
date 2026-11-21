# H049 流程图自动走线验证

被测提交为 `1dcd0f5512af9bcc161f6fdbf591a9aec73020a3`。使用冻结 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| Debug CTest 与 QtTest | 31 | 698 | 0 | 0 |
| Release CTest 与 QtTest | 31 | 698 | 0 | 0 |
| Release 离屏鼠标专项 | 1 | 13 | 0 | 0 |
| Release 原生 Windows 鼠标专项 | 1 | 13 | 0 | 0 |

CTest 两配置均为 31/31。用例数取自逐套件 QtTest XML，包含初始化、清理和数据行。鼠标专项是 `tst_ui` 子集，表内数字分别记录。

在启用冻结环境的源码目录分别运行 `cmake --preset debug`、`cmake --build --preset debug --parallel 2` 和 `ctest --preset debug --no-tests=error`。Release 将预设名换为 `release`。如需检查具体鼠标用例，在同一环境运行 `build/release/tests/tst_ui.exe flowAutoRoutes flowAutoRouteAndReconnect flowPortsCreateEdgeAndCancel -o 鼠标专项.xml,xml`，预期 13 项通过。

- [Debug 汇总](Debug.json) · [Release 汇总](Release.json) · [离屏专项](离屏鼠标专项.xml) · [原生 Windows 专项](原生Windows鼠标专项.xml)
- [水平直连](端口水平直连.png) · [自动避障](流程自动避障.png) · [端点重连](端点重连.png)
- [旧走线与拖弯的反证](旧走线反证.xml) · [加号盖住端口的反证](端口遮挡反证.xml)
- [本机候选校验](候选启动.json)。Release 候选逐个核验 18 个运行文件，精简 PATH 下原生启动 8 秒，加载本包 `qwindows.dll`，错误输出为空。

鼠标回归核对路径转折、连线两端的实际节点 ID、拖线身后工程修订、重接后退/前进及保存重开。节点重叠到堵住端口时，拉开节点会触发重新走线。真人操作和跨机检查仍在最终统一验收记录中跟踪。
