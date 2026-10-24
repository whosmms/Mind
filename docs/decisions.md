# 决策记录（规范§18：出现不明确的普通实现细节按最小可行和可维护原则决策，写入本文件）

## D-M0-01 lingya_core 依赖 Qt Widgets（QUndoStack所在模块）

- 日期：2026-10-24
- 背景：规范§3要求“测试可单独链接lingya_core，不能把窗口代码塞进核心库”，
  而规范§2.1指定命令与历史使用QUndoStack，该类位于Qt Widgets模块。
- 决策：lingya_core链接Qt6::Core与Qt6::Widgets，但只使用QUndoStack/QUndoCommand，
  不包含任何QWidget、QGraphicsItem或页面代码；测试目标只链接lingya_core与Qt6::Test。
- 影响：核心库可脱离界面运行测试；M5接入Qt Network时在核心库追加Qt6::Network。

## D-M0-02 M0提供通用编辑事务，类型化命令清单留给M1

- 日期：2026-10-24
- 背景：规范§5.1列出23个必须覆盖的命令；M0只要求纵向闭环（根节点→改名→保存→重开）。
- 决策：CommandService::applyEdit(label, edit)提供“候选副本→校验→快照入栈”的通用事务，
  M0的重命名走该通道；addChild等类型化命令在M1按清单逐个实现，接口仍收口于CommandService。
- 影响：类型化命令实现后，通用接口可降级为内部实现细节，对外行为不变。

## D-M0-03 快照命令采用整工程值拷贝

- 日期：2026-10-24
- 背景：规范§5.1允许“初版使用值类型快照便于确保多对象事务一致”，并给出64MiB预算与50步上限。
- 决策：SnapshotCommand持有before/after两份Project值拷贝；QUndoStack::setUndoLimit(50)已启用；
  64MiB累计内存预算检查在M1实现（当前工程规模远小于预算）。

## D-M0-04 画布内编辑暂缓，M0重命名走对话框

- 日期：2026-10-24
- 背景：规范§5.2定义了双击/F2进入编辑、中文输入法处理等完整交互，属M1工作。
- 决策：M0用QInputDialog完成“改名”这最后一步纵向闭环，双击画布节点也触发该对话框；
  画布内QGraphicsTextItem编辑、输入法预编辑处理在M1按§5.2实现。

## D-M0-05 JSON读取端对未知字段一律拒绝

- 日期：2026-10-24
- 背景：规范§4.2要求“每个已定义对象拒绝未知字段；升级需要明确迁移”。
- 决策：Reader对每层对象执行unknownKeys检查（工程、工作表、节点、样式、任务、卡片、
  路线、文稿及position/viewport/filter子对象），报错信息含“未知字段”与字段名；
  fixtures/bad-unknown-field.lingya覆盖该路径。

## D-M0-06 FakeAiProvider只按IAiProvider契约提供假响应

- 日期：2026-10-24
- 背景：规范§12 M0要求“共享类型及假AI响应”，规范§10.3规定FakeAiProvider只在测试或
  明显标识的开发模式使用，禁止伪装真实调用。
- 决策：FakeAiProvider返回规范§10.4的合法X05响应外形（finish_reason=stop、usage、model），
  另提供TIMEOUT/AUTH_FAILED/EMPTY_OUTPUT三种失败模式；M0不实现AiService与预览界面，
  真实DeepSeekClient随M5接入。

## D-M0-07 CMakePresets通过环境变量QT_KIT_DIR注入套件路径

- 日期：2026-10-24
- 背景：规范§2.2要求CMakeLists统一依赖、通用构建配置可入库、含本机绝对路径的
  CMakeUserPresets.json不提交；规范§14要求不把另一台电脑的绝对路径写死。
- 决策：CMakePresets.json的CMAKE_PREFIX_PATH取`$env{QT_KIT_DIR}`，各成员自设环境变量；
  团队如需固化本机路径，写入各自不提交的CMakeUserPresets.json。

## D-M0-08 配置文件按统一规范第9章逐字采用

- 日期：2026-10-24
- 背景：《三人协作开发统一规范》§9给出了.clang-format、.editorconfig、.gitattributes、
  .gitignore的完整内容，要求M0建立；并明确“不要全局忽略*.lingya，因为受控的虚构测试样例
  必须进入tests/fixtures”。
- 决策：四个文件按规范内容逐字替换此前自建版本；CMakePresets.json按§5.3的base隐藏预设
  结构重建（binaryDir=build/${presetName}，BUILD_TESTING=ON，debug/release继承）。
- 影响：LF换行规范化由.gitattributes在下次文件更新时生效；构建命令保持
  `cmake --preset debug`不变。

## D-M0-09 构建目标拆分为lingya_core与lingya_appcore

- 日期：2026-10-24
- 背景：统一规范§7.1要求核心库lingya_core只包含不依赖界面的领域、序列化和纯规则代码、
  按需链接Qt Core；“命令历史和文档服务放应用支持层，不为了测试把QWidget塞入核心库，
  应用支持层可组织为lingya_appcore”。
- 决策：lingya_core（domain、ProjectJson、FileService、ProjectValidator）仅链接Qt6::Core；
  lingya_appcore（ProjectDocument、CommandService、SnapshotCommand、ai契约与假实现）
  链接Qt6::Core与Qt6::Widgets；可执行目标lingya链接appcore。
- 事实澄清：QUndoStack/QUndoCommand在Qt 6.8中位于Qt Widgets模块（规范§7.1写作
  “属于Qt Gui”，按官方模块划分为Widgets）；appcore链接Widgets因此而来。

## D-M0-10 业务代码统一namespace lingya，常量k前缀，元类型特化留全局

- 日期：2026-10-24
- 背景：统一规范§8要求业务代码放`namespace lingya`、命名常量k+PascalCase、布尔带is/has/
  can前缀；同时“现有Domain字段、公开方法和信号已经有约定时，以已有契约为准”。
- 决策：src下27个文件与3个测试统一包namespace lingya（测试用using namespace lingya）；
  Limits.h常量36个全部改k前缀；m_modified改m_dirty。两处C++规则例外：Q_DECLARE_METATYPE
  特化（模板特化声明位置规则）与Qt类前向声明必须留在全局作用域。
- 对原设计§4.2的说明：Domain.h加外层namespace属于包装，字段、JSON键与枚举取值逐字不变，
  不构成“另造第二份领域模型”。

## D-M0-11 非AI错误码对齐统一规范第10.3节

- 日期：2026-10-24
- 背景：M0初版自造了部分错误码；统一规范§10.3给出非AI模块统一错误码表。
- 决策与映射：INVALID_PATH→INVALID_ARGUMENT；VALIDATION_FAILED→INVALID_PROJECT；
  FILE_TOO_LARGE→LIMIT_EXCEEDED；WRITE_FAILED→FILE_WRITE_FAILED；
  READ_FAILED→FILE_READ_FAILED；NOT_FOUND（文件场景）→FILE_NOT_FOUND；
  PARSE_INVALID与SCHEMA_INVALID（文件层）→INVALID_PROJECT；
  schemaVersion>1→UNSUPPORTED_SCHEMA（新实现于ProjectJson::fromObject）。
  BACKUP_FAILED与§10.3一致，保留。AI错误码清单不变（M5实现时沿用原设计§10.3）。
- 影响：对外错误code为契约，tests断言同步更新（rejectsFutureVersion断言
  UNSUPPORTED_SCHEMA、rejectsGarbage断言INVALID_PROJECT、oversize断言LIMIT_EXCEEDED）。

## D-M0-13 变更通知移出QUndoStack重放栈，由服务层完成后统一发出

- 日期：2026-10-24
- 背景：GUI实测发现改名成功后撤销/重做菜单仍禁用（BUG-010）。诊断证明：QUndoStack::push
  的首次redo()重放栈内emit changed时，槽读到的canUndo/canRedo尚未就绪（命令入栈/索引
  递增在redo返回后才对外可见），导致onDocumentChanged把撤销action设为禁用，快捷键失效。
- 决策：ProjectDocument.applySnapshot与setClean只变更数据、不发信号；新增public
  notifyChanged()作为变更通知唯一出口；CommandService在push/undo/redo完成后调用，
  MainWindow在loadNewDocument与saveProjectTo完成后调用。
- 影响：信号时序不再依赖QUndoStack内部实现；sessionRevision语义不变（仍每次数据变更递增）；
  调用点收敛于CommandService三处与MainWindow两处，遗漏调用=界面不更新，由tst_ui守护。


## D-M0-14 甲身份修正与环境记录同步（关联IF-001）

- 日期：2026-10-24；提出者：甲takagi（AI协助）；待复核：乙LH，丙参与环境接收。
- 问题：旧功能分支误把当前用户登记为乙，并套用“乙为作者时由甲合并”的例外。
- 权衡：整体重用旧提交会覆盖新main的缺陷修复和日志；整体交换角色会破坏原模块归属。
- 决策：保留原角色表，确认甲takagi、乙LH；从main=3650d47新建甲分支，逐项迁入本机环境证据。
  撤销旧分支中错误适用的合并例外；本批甲为作者，依统一规范§17.1由非作者评审、乙合并。
- 影响：仅文档、个人IDE忽略项和三个环境辅助脚本；无Domain/JSON/API/CMake/业务接口变化。
  这不是协作规则的新例外，不改统一规范；IF-001记录用于关闭此前流程冲突，不伪造成员已批准。
- 回退：本批未合并main时保留主分支不变；本机先保全启动脚本及入口再切换其他分支。


## D-M0-15 跨机与剩余人工验收暂缓，后续开发继续（IF-004）

- 日期：2026-10-24；决策人：用户明确指示；登记：甲takagi（AI协助）。
- 指示：用户要求暂且跳过当前讨论的验收，先在GitHub登记，再继续后续任务。
- 范围：staging-4713a79无Qt电脑的启动/编辑/中文/保存/重开，中文输入法预编辑、DPI三档、完整人工保存重开链路。
- 权衡：先推进课程功能开发，代价是上述实机环境和交互结果仍未知；如实保留待验状态，后续补验。
- 决定：四项当前暂缓，不阻碍M1及既定后续开发；不记为通过、不取消需求、不宣告M0完整验收完成。
  依统一规范§0.1的最新用户指示优先，本决定覆盖旧文档中必须先完成这四项才能继续开发的当前执行顺序。
- 保持：功能范围、开发次序、代码自动回归、密钥保护、非作者评审及其他未在本次指示中的验收状态。
- 恢复与责任：用户要求恢复或最终交付验收前按原规范补验；乙按届时最终版本制作包，甲实机验收，丙汇总记录。
- 实施影响：仅AGENTS、acceptance、总交接、日志和本决策记录；不改代码、工具链、构建或现有通过证据。
