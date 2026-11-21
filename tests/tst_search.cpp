#include "commands/CommandService.h"
#include "commands/SearchReplaceCommands.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "features/SearchService.h"
#include "services/ProjectJson.h"
#include <QtTest>
using namespace lingya;
class TstSearch : public QObject
{
    Q_OBJECT
  private slots:
    void findsCollapsedNotesAndTasks();
    void reportsLimitAndCaseFolding();
    void emptyIsReadOnly();
    void batchReplacementIsOneTransaction();
    void replacementRejectsOversizeAndStalePlans();
    void selectedTaskSynchronizesLegacyTitles();
};
void TstSearch::findsCollapsedNotesAndTasks()
{
    auto project = ProjectFactory::createMindmap("检索");
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    tree.nodes[tree.rootId].collapsed = true;
    TreeNode child;
    child.id = "child";
    child.parentId = tree.rootId;
    child.title = "主题";
    child.note = "隐藏的中文检索关键字";
    tree.nodes.insert(child.id, child);
    Task task;
    task.id = "task";
    task.title = "检索关键字任务";
    project.tasks.append(task);
    const auto result = SearchService::find(project, "检索关键字");
    QCOMPARE(result.total, 2);
    QCOMPARE(result.matches.size(), 2);
    QCOMPARE(result.matches[0].targetId, child.id);
    QCOMPARE(result.matches[1].kind, "task");
    QVERIFY(result.matches[0].excerpt.contains("关键字"));
}
void TstSearch::reportsLimitAndCaseFolding()
{
    auto project = ProjectFactory::createFishbone("大纲");
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    for (auto &node : tree.nodes)
        node.title = "MiXeD";
    const auto result = SearchService::find(project, " mixed ", 2);
    QCOMPARE(result.total, tree.nodes.size());
    QCOMPARE(result.matches.size(), 2);
}
void TstSearch::emptyIsReadOnly()
{
    const auto project = ProjectFactory::createFishbone("只读");
    const auto json = ProjectJson::toJson(project);
    QCOMPARE(SearchService::find(project, " ").total, 0);
    SearchService::find(project, "骨");
    QCOMPARE(ProjectJson::toJson(project), json);
}
void TstSearch::batchReplacementIsOneTransaction()
{
    ProjectDocument document;
    auto project = ProjectFactory::createMindmap(QStringLiteral("版本 Alpha"));
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    const auto key = tree.rootId;
    const auto sheetId = tree.id;
    const auto nodeRevision = tree.nodes.value(key).contentRevision;
    tree.nodes[key].title = QStringLiteral("Alpha 定义");
    tree.nodes[key].note = QStringLiteral("说明 alpha，复习 ALPHA。");
    tree.nodes[key].collapsed = true;
    auto diagram = ProjectFactory::createFlowchart(QStringLiteral("结构"));
    auto &flow = std::get<FlowSheet>(diagram.sheets.first());
    const auto flowId = flow.nodes.firstKey();
    const auto flowSheetId = flow.id;
    flow.nodes[flowId].title = QStringLiteral("Alpha 过程");
    project.sheets.append(std::move(diagram.sheets.first()));
    document.replaceProject(project);
    CommandService service(&document);
    TaskCommands tasks(&service, &document);
    const auto task = tasks.linkTask(sheetId, key);
    QVERIFY(std::holds_alternative<ID>(task));
    QVERIFY(std::holds_alternative<ID>(tasks.addTask(QStringLiteral("独立 alpha 任务"))));
    service.clearHistory();
    document.setClean();

    SearchReplaceCommands command(&service, &document);
    const auto result = command.preview(QStringLiteral(" alpha "), QStringLiteral("乙版"));
    QVERIFY(std::holds_alternative<SearchReplaceCommands::Plan>(result));
    const auto plan = std::get<SearchReplaceCommands::Plan>(result);
    QCOMPARE(plan.topics(), 2);
    QCOMPARE(plan.tasks(), 2);
    QVERIFY(!plan.examples().isEmpty());
    QVERIFY(!document.isModified());
    const auto revision = document.sessionRevision();
    QVERIFY(document.setActiveSheet(flowSheetId));
    QCOMPARE(document.sessionRevision(), revision);
    const auto before = ProjectJson::toJson(document.project());
    QSignalSpy changed(&document, &ProjectDocument::changed);
    QVERIFY(std::holds_alternative<bool>(command.apply(plan)));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(document.sessionRevision(), revision + 1);
    QCOMPARE(document.project().activeSheetId, flowSheetId);
    const auto root = std::get<TreeSheet>(document.project().sheets[0]).nodes.value(key);
    QCOMPARE(root.title, QStringLiteral("乙版 定义"));
    QCOMPARE(root.note, QStringLiteral("说明 乙版，复习 乙版。"));
    QCOMPARE(root.contentRevision, nodeRevision + 1);
    QCOMPARE(std::get<FlowSheet>(document.project().sheets[1]).nodes.value(flowId).title,
             QStringLiteral("乙版 过程"));
    QCOMPARE(document.project().tasks[0].title, root.title);
    QCOMPARE(document.project().tasks[1].title, QStringLiteral("独立 乙版 任务"));
    const auto after = ProjectJson::toJson(document.project());
    service.undo();
    QCOMPARE(ProjectJson::toJson(document.project()), before);
    QVERIFY(!service.canUndo());
    service.redo();
    QCOMPARE(ProjectJson::toJson(document.project()), after);
}

void TstSearch::replacementRejectsOversizeAndStalePlans()
{
    ProjectDocument document;
    auto project = ProjectFactory::createMindmap(QStringLiteral("测试"));
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    tree.nodes[tree.rootId].title = QStringLiteral("替换");
    tree.nodes[tree.rootId].note = QString(Limits::kNoteMaxChars, QChar('a'));
    document.replaceProject(project);
    document.setClean();
    CommandService service(&document);
    SearchReplaceCommands command(&service, &document);
    const auto before = ProjectJson::toJson(document.project());
    const auto longNote = command.preview("a", QString(24000, QChar('b')));
    QCOMPARE(std::get<AppError>(longNote).code, QStringLiteral("LIMIT_EXCEEDED"));
    const auto longTitle = command.preview(QStringLiteral("替换"), QString(2500, QChar('b')));
    QCOMPARE(std::get<AppError>(longTitle).code, QStringLiteral("LIMIT_EXCEEDED"));
    QVERIFY(!service.canUndo());
    QVERIFY(!document.isModified());
    QCOMPARE(ProjectJson::toJson(document.project()), before);

    const auto empty = command.preview("不存在", "");
    QVERIFY(
        std::holds_alternative<bool>(command.apply(std::get<SearchReplaceCommands::Plan>(empty))));
    QVERIFY(!service.canUndo());
    const auto plan =
        std::get<SearchReplaceCommands::Plan>(command.preview(QStringLiteral("替换"), ""));
    QVERIFY(std::holds_alternative<bool>(command.apply(plan)));
    QCOMPARE(std::get<TreeSheet>(document.project().sheets.first()).nodes.value(tree.rootId).title,
             QStringLiteral("未命名主题"));
    const auto after = ProjectJson::toJson(document.project());
    QCOMPARE(std::get<AppError>(command.apply(plan)).code, QStringLiteral("STALE_SOURCE"));
    QCOMPARE(ProjectJson::toJson(document.project()), after);
}

void TstSearch::selectedTaskSynchronizesLegacyTitles()
{
    ProjectDocument document;
    auto project = ProjectFactory::createMindmap(QStringLiteral("旧存档"));
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    Task task;
    task.id = QStringLiteral("仅任务命中");
    task.title = QStringLiteral("旧计划");
    task.source = TaskSource{tree.id, tree.rootId};
    project.tasks.append(task);
    document.replaceProject(project);
    CommandService service(&document);
    SearchReplaceCommands command(&service, &document);
    const auto matches = SearchService::find(document.project(), QStringLiteral("计划"));
    QCOMPARE(matches.total, 1);
    const auto plan = std::get<SearchReplaceCommands::Plan>(
        command.preview(QStringLiteral("计划"), QStringLiteral("记录"), &matches.matches.first()));
    QCOMPARE(plan.topics(), 1);
    QCOMPARE(plan.tasks(), 1);
    QVERIFY(std::holds_alternative<bool>(command.apply(plan)));
    QCOMPARE(document.project().tasks.first().title, QStringLiteral("旧记录"));
    QCOMPARE(std::get<TreeSheet>(document.project().sheets.first()).nodes.value(tree.rootId).title,
             QStringLiteral("旧记录"));

    // 旧格式读端允许多个任务关联同一主题，全部替换应只预览最终将写入的值。
    auto legacy = project;
    std::get<TreeSheet>(legacy.sheets.first()).nodes[tree.rootId].title = QStringLiteral("旧方案");
    task.id = QStringLiteral("另一项关联任务");
    task.title = QStringLiteral("另一份计划");
    legacy.tasks.append(task);
    document.replaceProject(std::move(legacy));
    service.clearHistory();
    const auto batch = std::get<SearchReplaceCommands::Plan>(
        command.preview(QStringLiteral("计划"), QStringLiteral("方案")));
    QCOMPARE(batch.topics(), 1);
    QCOMPARE(batch.tasks(), 2);
    QCOMPARE(batch.examples().size(), 3);
    for (const auto &example : batch.examples())
        QVERIFY(example.endsWith(QStringLiteral("另一份方案")));
    QVERIFY(std::holds_alternative<bool>(command.apply(batch)));
    QCOMPARE(document.project().tasks[0].title, QStringLiteral("另一份方案"));
    QCOMPARE(document.project().tasks[1].title, QStringLiteral("另一份方案"));
}
QTEST_GUILESS_MAIN(TstSearch)
#include "tst_search.moc"
