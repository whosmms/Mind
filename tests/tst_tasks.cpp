#include <QtTest>

#include "commands/CommandService.h"
#include "commands/TaskCommands.h"
#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "document/ProjectDocument.h"

using namespace lingya;

// 任务命令测试（统一规范§5.1、§6.4、§7.3）：新增/修改/删除/关联/解除关联，
// 均经CommandService事务，校验失败不改数据，可撤销。
class TstTasks : public QObject {
    Q_OBJECT

private slots:
    void addTaskAppendsAndIsUndoable();
    void updateTaskRejectsInvalidDate();
    void removeTaskCleansDependencies();
    void linkTaskEnforcesOneNodeOneTask();
    void unlinkTaskKeepsTitle();
    void renameNodeSyncsTaskTitle();
    void renameTaskSyncsNodeTitle();
    void titleMismatchDetectsConflict();
};

void TstTasks::addTaskAppendsAndIsUndoable() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("任务测试")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const auto created = tasks.addTask(QStringLiteral("写文档"));
    QVERIFY(std::holds_alternative<ID>(created));
    QCOMPARE(doc.project().tasks.size(), 1);
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("写文档"));
    QVERIFY(service.canUndo());

    service.undo();
    QCOMPARE(doc.project().tasks.size(), 0);   // undo恢复before内容
}

void TstTasks::updateTaskRejectsInvalidDate() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("日期测试")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const ID id = std::get<ID>(tasks.addTask(QStringLiteral("任务")));
    Q_UNUSED(id);

    Task bad = doc.project().tasks.first();
    bad.startDate = QDate(2026, 10, 30);
    bad.endDate = QDate(2026, 10, 26);   // 结束早于开始
    const auto result = tasks.updateTask(bad);
    QVERIFY(std::holds_alternative<AppError>(result));
    QVERIFY(!doc.project().tasks.first().startDate.has_value());   // 校验失败原数据不变
}

void TstTasks::removeTaskCleansDependencies() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("依赖清理")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const ID firstId = std::get<ID>(tasks.addTask(QStringLiteral("前驱")));
    const ID secondId = std::get<ID>(tasks.addTask(QStringLiteral("后继")));
    Q_UNUSED(secondId);

    Task second = doc.project().tasks.last();
    second.predecessorIds << firstId;
    QVERIFY(std::holds_alternative<bool>(tasks.updateTask(second)));

    const auto removed = tasks.removeTask(firstId);
    QVERIFY(std::holds_alternative<bool>(removed));
    QCOMPARE(doc.project().tasks.size(), 1);
    QVERIFY(doc.project().tasks.first().predecessorIds.isEmpty());   // 依赖已清理
}

void TstTasks::linkTaskEnforcesOneNodeOneTask() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("关联任务")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
    const ID sheetId = sheet.id;
    const ID nodeId = sheet.rootId;

    const auto linked = tasks.linkTask(sheetId, nodeId);
    QVERIFY(std::holds_alternative<ID>(linked));
    QVERIFY(doc.project().tasks.first().source.has_value());
    QCOMPARE(doc.project().tasks.first().title,
             doc.project().tasks.first().source.has_value()
                 ? std::get<TreeSheet>(doc.project().sheets.first())
                       .nodes.value(nodeId)
                       .title
                 : QString());

    // 同一节点再次关联应被拒绝（规则4：一节点一任务）
    const auto again = tasks.linkTask(sheetId, nodeId);
    QVERIFY(std::holds_alternative<AppError>(again));
}

void TstTasks::unlinkTaskKeepsTitle() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("解除关联")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
    const ID taskId = std::get<ID>(tasks.linkTask(sheet.id, sheet.rootId));
    QVERIFY(std::holds_alternative<bool>(tasks.unlinkTask(taskId)));
    QVERIFY(!doc.project().tasks.first().source.has_value());
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("中心主题"));   // 保留标题
}

void TstTasks::renameNodeSyncsTaskTitle() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("双向改名")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
    const ID sheetId = sheet.id;
    const ID nodeId = sheet.rootId;
    QVERIFY(std::holds_alternative<ID>(tasks.linkTask(sheetId, nodeId)));

    QVERIFY(std::holds_alternative<bool>(
        tasks.renameNode(sheetId, nodeId, QStringLiteral("新标题"))));
    QCOMPARE(std::get<TreeSheet>(doc.project().sheets.first()).nodes.value(nodeId).title,
             QStringLiteral("新标题"));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("新标题"));

    service.undo();   // 一个事务同时复原节点与任务标题
    QCOMPARE(std::get<TreeSheet>(doc.project().sheets.first()).nodes.value(nodeId).title,
             QStringLiteral("中心主题"));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("中心主题"));
}

void TstTasks::renameTaskSyncsNodeTitle() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("任务改名")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
    const ID sheetId = sheet.id;
    const ID nodeId = sheet.rootId;
    const ID taskId = std::get<ID>(tasks.linkTask(sheetId, nodeId));

    QVERIFY(std::holds_alternative<bool>(tasks.renameTask(taskId, QStringLiteral("任务新名"))));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("任务新名"));
    QCOMPARE(std::get<TreeSheet>(doc.project().sheets.first()).nodes.value(nodeId).title,
             QStringLiteral("任务新名"));
}

void TstTasks::titleMismatchDetectsConflict() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("一致性")));
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const TreeSheet& sheet = std::get<TreeSheet>(doc.project().sheets.first());
    const ID sheetId = sheet.id;
    const ID nodeId = sheet.rootId;
    QVERIFY(std::holds_alternative<ID>(tasks.linkTask(sheetId, nodeId)));

    // 初始一致：无冲突
    QVERIFY(TaskCommands::titleMismatches(doc.project()).isEmpty());

    // 绕过命令直接改节点标题，模拟外部/旧文件造成的不一致
    Project broken = doc.project();
    std::get<TreeSheet>(broken.sheets.first()).nodes[nodeId].title = QStringLiteral("只改了节点");
    const auto mismatches = TaskCommands::titleMismatches(broken);
    QCOMPARE(mismatches.size(), 1);
    QCOMPARE(mismatches.first().second, nodeId);
}

QTEST_MAIN(TstTasks)
#include "tst_tasks.moc"
