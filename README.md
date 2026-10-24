# 灵芽思维（LingyaMind）

课程项目：C++17 + Qt 6 Widgets + Graphics View + CMake 的 Windows 桌面思维导图软件。
**新成员与AI协助从这里开始：先读 [AGENTS.md](AGENTS.md)，再读
[docs/三人协作开发统一规范.md](docs/三人协作开发统一规范.md)。**

## 当前状态：M0骨架可构建，团队验收尚未完成

按原设计§12 M0与统一规范§4完成了纵向闭环代码（领域/文档/命令/服务/AI契约/主窗口/
4个测试套件/fixtures），并按统一规范完成配置、分层、命名与错误码对齐。
main已包含BUG-010/011修复。甲takagi（Takagi-gif）、乙LH（whosmms）、丙wbccaidapkc分别登记自己的环境和验证，不互相代签。
最新环境与验证见 [docs/environment.md](docs/environment.md)，交接和当前待办见
[docs/交接文档.md](docs/交接文档.md) 的 H-002。M0三机复现、人工交互及干净机器部署仍待验收。

仓库地址：[whosmms/Mind](https://github.com/whosmms/Mind)。甲使用 `feature/a/...`，
乙使用 `feature/b/...`，丙使用 `feature/c/...`；本次启动修复分支为 `feature/a/M0-ui-acceptance`。
甲的改动由非作者评审、乙负责合并；旧合并请求#1已关闭且未合并，旧错误分支已撤回。

本机工具按“仓库父目录/.tools/Qt”和“.tools/formatter”布局时，用PowerShell 7执行：

```powershell
. .\tools\进入开发环境.ps1
.\tools\验证环境.ps1
.\tools\打开QtCreator.ps1
# 构建、准备运行依赖并启动实际程序。
.\tools\打开灵芽思维.ps1
```

工具目录不同则给脚本传 `-ToolsRoot`，也可按下面命令手动设置Kit；配套MinGW/CMake/Ninja
必须优先于其他软件附带工具。脚本仅修改当前进程，不修改永久PATH；Qt Creator使用独立个人配置。
`D:/mind` 下CMD是甲电脑的便利入口，不作为其他成员的安装文件；仓库共享的是tools中的PS1脚本。
切换到不含这些脚本的分支前先备份入口依赖，避免本机双击入口失效。

环境安装后执行（统一规范§19.2合并前最低检查）：

```powershell
$env:QT_KIT_DIR = "<Qt套件目录>"
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --no-tests=error
```

## 目录说明（统一规范§6）

- `src/domain` Domain.h 领域值类型（原设计§4.2）、Limits.h 上限常量（k前缀）、工厂与校验器
- `src/document` ProjectDocument 唯一数据源　`src/commands` CommandService 快照命令与撤销
- `src/services` ProjectJson 严格序列化、FileService 原子保存与备份
- `src/ai` AiTypes 契约、IAiProvider 接口、FakeAiProvider 假响应（真实 DeepSeekClient 随 M5）
- `src/app`、`src/graphics` 主窗口与画布图元　`src/controllers|layout|charts|features|ui` 待 M1-M4 充实
- `tests/` tst_domain / tst_projectjson（链接lingya_core）、tst_commands（链接lingya_appcore）、tst_ui及fixtures
- `resources/` QSS 主题与 AI 提示词模板　`packaging/` Inno Setup占位（M6）　`tools/` 环境、验证和IDE启动脚本
- `docs/` 统一规范、设计规范、交接文档、开发日志（仓库主本）及 environment / decisions /
  acceptance / third-party 与 templates/ 三个模板

构建目标分层（统一规范§7.1）：`lingya_core` 仅链接 Qt Core（领域与序列化）；
`lingya_appcore` 承载文档与命令历史（QUndoStack，链接 Qt Widgets）；`lingya` 为窗口应用。

## 协作要点（统一规范）

- 三人角色：甲——界面图标、编辑器、讲解；乙——数据、文件、布局、构建集成；
  丙——任务甘特、复习检查、AI。文件归属见规范§2.2，main 合并由乙操作。
- 业务代码放 `namespace lingya`；常量 k+PascalCase；错误码用规范§10.3统一表。
- 每次合并前：`git diff --check` → Debug构建 → ctest 非零测试全过 → 非作者评审。
- 未运行的命令与测试一律写“未执行”及原因；验收证据引用提交号与命令，不只写“正常”。


## 启动提示缺少DLL或建议重新安装

先使用`tools/打开灵芽思维.ps1`（PowerShell 7），或本机`D:/mind/打开灵芽思维.cmd`。
入口按冻结Kit构建，调用Qt官方windeployqt准备依赖，再打开程序；已运行时保留现有窗口，不强制结束进程。
验证环境脚本也会在构建后准备依赖。已有产物只需补依赖时，可运行：

```powershell
.\tools\准备运行依赖.ps1 -Configuration Both
```

程序依赖同目录的Qt/MinGW DLL和platforms插件，不能只移动一个lingya.exe。
当前Kit中Debug构建也导入无d后缀的Qt库；部署脚本以PE导入为准，不根据Debug标签猜测插件类型。
这项修复不重装Qt、不改永久PATH；构建目录全部忽略，正式分发仍由乙按M6制作完整部署目录或安装包。
实际窗口证据与接收步骤见[H-004](docs/交接文档.md)。
