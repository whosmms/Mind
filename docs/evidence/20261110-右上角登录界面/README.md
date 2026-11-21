# H051 右上角登录界面验证

被测源码为 `17ecb6574be859ef50421146ff03d1f18c722630`。冻结环境为 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。

| 验证 | 套件 | 通过 | 失败 | 跳过 |
| --- | ---: | ---: | ---: | ---: |
| Debug CTest 与 QtTest | 31 | 705 | 0 | 0 |
| Release CTest 与 QtTest | 31 | 705 | 0 | 0 |
| Release 离屏账号专项 | 2 | 10 | 0 | 0 |
| Release 原生 Windows 账号专项，Qt 缩放 1、1.5、2 倍各自 | 2 | 10 | 0 | 0 |

在冻结环境下使用 `cmake --preset debug`、`cmake --build --preset debug --parallel 2` 和 `ctest --preset debug --no-tests=error`。Release 将预设名换为 `release`。两个常规 CTest 结果均为 31/31。QtTest XML 计数包括初始化、清理及数据行。账号专项运行 `tst_ui.exe topRightAccountShowsPreview onlineIdentityKeepsLocalDocumentAndHistory homeNavigationKeepsEditorState` 与 `tst_onlineauth.exe dialogPreviewAndSettingsAreSeparate dialogValidationAndCancel`，分别预期 6 项及 4 项通过。

- [Debug 汇总](Debug.json) · [Release 汇总](Release.json) · [离屏专项](离屏专项.json) · [原生 1 倍](Windows-1.json) · [原生 1.5 倍](Windows-1.5.json) · [原生 2 倍](Windows-2.json)
- [宽窗首页](首页右上角-1536.png) · [窄窗首页](首页右上角-800.png) · [登录](登录预览.png) · [注册](注册预览.png) · [找回密码](找回密码预览.png)
- [候选启动校验](候选启动.json)。18 个运行文件逐一核验。精简 PATH 下原生启动 8 秒，加载本包 Windows 平台插件，错误输出为空。

测试使用隔离的临时资料目录与模拟账号传输。未配置时，预览入口发出 0 次账号请求。原生专项三档使用 Qt 附加缩放，实际服务与跨机操作继续按最终统一验收跟踪。

首轮 Release CTest 继承了专项环境的 2 倍缩放。以相同条件直跑离屏 UI，复现讲解播放 `presentationPlaybackRestoresEditor` 全屏断言。清理缩放环境后复跑常规 Release 全部通过。首轮 CTest 记录和离屏诊断原文保留在 `build/h051-final` 下。
