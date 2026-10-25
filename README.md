# 灵芽思维（LingyaMind）

课程项目：C++17 + Qt 6 Widgets + Graphics View + CMake 的 Windows 桌面思维导图软件。
**新成员与AI协助从这里开始：先读 [AGENTS.md](AGENTS.md)，再读
[docs/三人协作开发统一规范.md](docs/三人协作开发统一规范.md)。**

## 当前状态：甲M1与按钮API清理已集成，M1操作核查继续

乙已将甲M1编辑器合入main（7b3a27f），并接收H-016两项元数据兼容测试（282c2af）；按钮API清理由乙普通合并df18ded进入main。GitHub [PR17](https://github.com/whosmms/Mind/pull/17)已合并；[PR18](https://github.com/whosmms/Mind/pull/18)已关闭，其代码已集成。五条旧远程分支已清理，不再以它们作为当前同步入口。

乙INT-20261025-006/007登记合后Debug/Release各15套件266项通过；丙评审与授权代验结果均为乙回执来源。最新main=033441c追加甲本人Ctrl+Y复核通过（乙转达）、H-011～H-016门槛闭合；旧三项不再作为待办。本轮甲另补焦点/别名与另存为链路自动核查，见[H-017说明](docs/reviews/H-017-M1集成回执与操作核查.md)及[草稿PR19](https://github.com/whosmms/Mind/pull/19)。M2属性面板和样式绘制尚未交付。

甲本轮H-017被测b24fdd2：Debug/Release各15套件271项通过、0失败0跳过（UI39，含初始化/清理及数据行）。大纲/画布的两种重做快捷键及Qt非原生另存为链路未复现故障，未改业务实现；本轮自动结果不覆盖Windows原生对话框，团队验收状态按乙最新闭环回执承接，不要求用户重复整套旧验收。

## 历史过程记录

以下保留原批次状态、待办和数字，最新集成情况以上段、AGENTS与交接的最新回执为准；不要据此重复创建PR或要求整套重测。

乙已对甲85f0a1b代码/文档及排序补丁复核通过（用户转交结论，双配置各15套件246项全过）；[回执与边界](docs/reviews/H-013-乙复核回执.md)。
当前PR #17源码保持该待验基线，等待丙独立复核与甲本人[三项增量补验](docs/甲M1增量真人验收_三项.md)，之后由乙合入。原八项清单不再要求全量重做。

甲分支本轮H-013已完成已有M1接口接线（代码977b03b）：整树/大纲联动、子级/同级新增、改名/多行、删除影响确认和关联任务处置、撤销重做、保存重开、缩放定位。
代码已上传至[草稿评审PR #17](https://github.com/whosmms/Mind/pull/17)，等待真人验收与乙丙复核，尚未合并。
Debug/Release各15套件246项通过，0失败0跳过；修正删除后同级顺序未连续，并补齐当前主窗口提示按钮中文。
请按[当前人工验收步骤](docs/甲M1收尾_人工验收步骤.md)完成真实输入法、鼠标及DPI；非作者评审待完成，完整M1验收未关闭。上传与评审回执见H-013。
以下H-011/H-012内容保留为历史交付说明，最新同级/删除能力以本段及H-013为准。

本地甲分支已在乙main=e7bb883基础上完成整树画布接线（代码51736ca）：
所有可见节点与父子连线、大纲/画布单选联动、双击/F2改名、多行标题、适应全部与所选定位。
上轮H-011单主题预览已被本轮H-012接入替代，详见[当前操作与验收](docs/甲M1整树画布接入与验收.md)。
本批待非作者复核及新版本人工验收，未上传/未合入main；同级新增和删除仍待乙，完整M1/R10未通过。

历史H-011：本地工作分支 `feature/a/M1-editor-integration` 第一批界面接入（代码 `4d134d9`）：
左侧大纲新增多级子主题、F2/双击改名、统一撤销与保存重开，右侧显示所选主题完整文字预览，
支持25%—200%缩放与空白拖动；甘特视图按H-007懒加载并随文档刷新。
本批待乙丙非作者复核，尚未上传或合入main。右侧是主题预览，完整树形布局与同级新增/子树删除待乙接口。
操作、证据和验收边界见 [甲M1界面接入与验收](docs/甲M1界面接入与验收.md) 与 H-011。

按原设计§12 M0与统一规范§4完成了纵向闭环代码（领域/文档/命令/服务/AI契约/主窗口/
12个测试套件/fixtures），并按统一规范完成配置、分层、命名与错误码对齐。
已集成R01第一批、甘特视图、复习会话基础件与DEV-030恢复快照甲复核修正（D-REC-001：采用后保留磁盘副本
直至正式保存或明确放弃）。甲复核双配置12套件156项通过、本人真实崩溃链路确认通过（五图、INT-20261025-003）；
乙复核通过并合入main，合并后双配置14套件199项通过；甲乙丙评审链已按H-010闭合（丙结论由乙转达登记），见[评审与结果](docs/reviews/DEV-030-甲复核.md)。
标签[v0.1.0]（提交aef9432）为M0基线。甲takagi（Takagi-gif）、乙LH（whosmms）、丙wbccaidapkc
分别登记自己的环境和验证，不互相代签。最新环境与验证见 [docs/environment.md](docs/environment.md)，
交接和当前待办见 [docs/交接文档.md](docs/交接文档.md)（本分支最新H-010；M0人工证据H-006）。

M0门禁状态：三机同提交自动签收已闭合（4713a79，甲INT-023/乙INT-009/丙INT-010，各9套件105用例）；
人工验收中输入法预编辑、DPI三档、完整人工保存重开由甲通过（H-006），坏文件拒绝与外部修改拒绝由乙通过；
无Qt电脑跨机验证经团队决定暂缓（IF-005），如实标注未验收。团队验收（acceptance最终汇总与复核）尚未完成。

仓库地址：[whosmms/Mind](https://github.com/whosmms/Mind)。甲使用 `feature/a/...`，
乙使用 `feature/b/...`，丙使用 `feature/c/...`；合并前非作者评审、main由乙操作合入，
评审可走合并请求或团队直接协调（结论登记开发日志）。

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
- `src/services` ProjectJson 严格序列化、FileService 原子保存与备份、RecoveryService 恢复快照
- `src/ai` AiTypes 契约、IAiProvider 接口、FakeAiProvider 假响应（真实 DeepSeekClient 随 M5）
- `src/app`、`src/graphics` 主窗口与画布图元　`src/controllers|layout|charts|features|ui` 待 M1-M4 充实
- `tests/` tst_domain / tst_projectjson（链接lingya_core）、tst_commands/tst_tasks（链接lingya_appcore）、tst_ui、tst_check及fixtures
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
