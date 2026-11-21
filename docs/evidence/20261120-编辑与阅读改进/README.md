# H058 编辑与阅读改进验证

被测产品提交 `480a8bc7efe35c9663bc91e7ad45a48dfa6d6337`。候选入口 `D:/mind/打开最新开发版.cmd`。

## 构建与自动回归

使用冻结的 Qt 6.8.3、MinGW 13.1.0、CMake 3.30.5 和 Ninja 1.12.1。Debug 和 Release 的 CTest 各 31/31，逐套 QtTest XML 各 737 项通过、0 失败、0 跳过，计数含初始化、清理和数据行。Release 构建记录包含既有的 `tst_nodecommands.cpp` 告警。[Debug](Debug.json) · [Release](Release.json)

在已加载冻结环境的仓库根目录执行以下命令可复跑两配置。

```powershell
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug --output-on-failure
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release --output-on-failure
```

首轮全量回归捕获导图及鱼骨图片手柄被框选拦截，以及分支专注测试仍按旧的即时创建判断。修复图片命中并在测试里完成标题确认后，两配置复跑均通过。窄窗截图复核随后捕获半截图标，改为按整组收纳，并增加边界检查。首轮记录保存在工作区的 `build/h058-work`。

## 界面专项和兼容性

| 模式 | 通过 | 失败 | 跳过 | 证据 |
|---|---:|---:|---:|---|
| 离屏 1 倍 | 16 | 0 | 0 | [结果](离屏-1.json) |
| 离屏 1.5 倍 | 15 | 0 | 0 | [结果](离屏-1.5.json) |
| 原生 Windows 1 倍 | 16 | 0 | 0 | [结果](Windows-1.json) |
| 原生 Windows 1.5 倍 | 16 | 0 | 0 | [结果](Windows-1.5.json) |

专项覆盖画布编辑、输入法事件、图片手柄、最近工程、复习、窄窗和单屏讲解。离屏 1.5 倍的首次全屏状态断言未通过，两档原生 Windows 均通过全屏项。原生 1.5 倍的固定宽窗属性栏断言未满足，该倍率用 800 像素边界用例验证缩放和功能区。原始输出留在 `build/h058-work/前一轮专项`。这些专项和 Qt 注入的输入法事件属于开发期验证，真人输入法、显示设置和无 Qt 跨机纳入最终集中验收。

13 份既有样例经严格读端验证。18 个运行文件逐项 SHA-256 一致，精简 PATH 的原生候选持续启动 8 秒，错误输出 0 字节，并从候选包加载 Windows 平台插件。[样例读取](样例读取.json) · [候选启动](候选启动.json)

## 实际画面

- [画布原地编辑](01-画布原地编辑.png)
- [框选后批量样式](02-框选批量样式.png)
- [800 像素功能区与缩放](03-主编辑器-800.png)
- [今日到期练习](04-今日待复习.png)
- [甘特任务名称与属性](05-甘特图.png)
