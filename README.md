# 灵芽思维（LingyaMind）

课程项目：C++17 + Qt 6 Widgets + Graphics View + CMake 的 Windows 桌面思维导图软件。
**新成员与AI协助从这里开始：先读 [AGENTS.md](AGENTS.md)，再读
[docs/三人协作开发统一规范.md](docs/三人协作开发统一规范.md)。**

## 当前状态：M0代码骨架就绪，构建与测试未执行

按原设计§12 M0与统一规范§4完成了纵向闭环代码（领域/文档/命令/服务/AI契约/主窗口/
3个测试/fixtures），并按统一规范完成配置、分层、命名与错误码对齐。
**本机尚未安装Qt 6与CMake，构建和测试未执行**——见 [docs/environment.md](docs/environment.md)，
交接状态见 [docs/交接文档.md](docs/交接文档.md) 的 H-001（待接收验证）。

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
- `tests/` tst_domain / tst_projectjson（链接lingya_core）、tst_commands（链接lingya_appcore）及fixtures
- `resources/` QSS 主题与 AI 提示词模板　`packaging/` Inno Setup 脚本（M6）　`tools/` 验证脚本（M6前）
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
