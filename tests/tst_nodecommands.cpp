#include <QtTest>

#include <algorithm>

#include "commands/CommandService.h"
#include "commands/NodeCommands.h"
#include "commands/TaskCommands.h"
#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "document/ProjectDocument.h"

using namespace lingya;

namespace {

TreeSheet& treeOf(Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

const TreeSheet& treeOf(const Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

ID addRawNode(Project& project, const ID& parentId, int order, const ID& id,
              const QString& title) {
    TreeNode node;
    node.id = id;
    node.parentId = parentId;
    node.order = order;
    node.title = title;
    node.contentRevision = 1;
    treeOf(project).nodes.insert(id, node);
    return id;
}

struct LinkedProject {
    Project project;
    ID sheetId;
    ID rootId;
    ID taskId;
};

LinkedProject makeLinkedProject(const QString& taskTitle = QStringLiteral("中心主题")) {
    LinkedProject result;
    result.project = ProjectFactory::createMindmap(QStringLiteral("节点命令测试"));
    const TreeSheet& tree = treeOf(result.project);
    result.sheetId = tree.id;
    result.rootId = tree.rootId;

    Task task;
    task.id = QStringLiteral("task-1");
    task.title = taskTitle;
    task.source = TaskSource{result.sheetId, result.rootId};
    result.project.tasks.push_back(task);
    result.taskId = task.id;
    return result;
}

QString emojiTitle(int count) {
    return QString::fromUtf8("\xF0\x9F\x98\x80").repeated(count);
}

QVector<int> childOrders(const Project& project, const ID& parentId) {
    QVector<int> orders;
    const TreeSheet& tree = treeOf(project);
    for (auto it = tree.nodes.constBegin(); it != tree.nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == parentId) {
            orders.append(it.value().order);
        }
    }
    std::sort(orders.begin(), orders.end());
    return orders;
}

}  // namespace

class TstNodeCommands : public QObject {
    Q_OBJECT

private slots:
    void addChildToRootCreatesAndUndoes();
    void addChildNormalizesSiblingOrder();
    void addChildRejectsInvalidInput();
    void addChildNormalizesTitle();
    void addChildEnforcesTitleLimit();
    void addChildEnforcesNodeLimit();
    void renameNodeSyncsTaskAndRevision();
    void renameNodeNoOpKeepsHistoryClean();
    void renameNodeRepairsMismatchedTask();
    void renameTaskSyncsNodeAndRevision();
    void renameTaskNoOpKeepsHistoryClean();
    void renameRejectsOverlongTitle();
};

void TstNodeCommands::addChildToRootCreatesAndUndoes() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("节点命令测试")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const TreeSheet& before = treeOf(doc.project());
    const ID sheetId = before.id;
    const ID rootId = before.rootId;
    const qint64 rootRevision = before.nodes.value(rootId).contentRevision;

    const auto created = nodes.addChild(sheetId, rootId, QStringLiteral("  子主题  "));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID childId = std::get<ID>(created);

    const TreeSheet& after = treeOf(doc.project());
    QVERIFY(after.nodes.contains(childId));
    QCOMPARE(after.nodes.value(childId).title, QStringLiteral("子主题"));
    QCOMPARE(after.nodes.value(childId).parentId.value_or(QString()), rootId);
    QCOMPARE(after.nodes.value(childId).order, 0);
    QCOMPARE(after.nodes.value(childId).contentRevision, 1);
    QCOMPARE(after.nodes.value(rootId).contentRevision, rootRevision + 1);
    QVERIFY(service.canUndo());

    service.undo();
    QVERIFY(!treeOf(doc.project()).nodes.contains(childId));
    QCOMPARE(treeOf(doc.project()).nodes.value(rootId).contentRevision, rootRevision);

    service.redo();
    QVERIFY(treeOf(doc.project()).nodes.contains(childId));
}

void TstNodeCommands::addChildNormalizesSiblingOrder() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("顺序规范化"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 3, QStringLiteral("child-a"), QStringLiteral("A"));
    addRawNode(project, rootId, 3, QStringLiteral("child-b"), QStringLiteral("B"));

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created = nodes.addChild(sheetId, rootId, QStringLiteral("C"));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID childId = std::get<ID>(created);

    // 先按(order, ID)稳定排序，再写回连续order；新节点追加在最后。
    QCOMPARE(treeOf(doc.project()).nodes.value(QStringLiteral("child-a")).order, 0);
    QCOMPARE(treeOf(doc.project()).nodes.value(QStringLiteral("child-b")).order, 1);
    QCOMPARE(treeOf(doc.project()).nodes.value(childId).order, 2);
    QCOMPARE(childOrders(doc.project(), rootId), QVector<int>({0, 1, 2}));
}

void TstNodeCommands::addChildRejectsInvalidInput() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("失败测试")));
    doc.setClean();
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const TreeSheet& tree = treeOf(doc.project());
    const qint64 revision = doc.sessionRevision();

    const auto empty = nodes.addChild(QString(), tree.rootId, QStringLiteral("子"));
    QVERIFY(std::holds_alternative<AppError>(empty));
    QCOMPARE(std::get<AppError>(empty).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto noSheet = nodes.addChild(QStringLiteral("missing"), tree.rootId,
                                        QStringLiteral("子"));
    QVERIFY(std::holds_alternative<AppError>(noSheet));
    QCOMPARE(std::get<AppError>(noSheet).code, QStringLiteral("NOT_FOUND"));

    const auto noParent = nodes.addChild(tree.id, QStringLiteral("missing"),
                                         QStringLiteral("子"));
    QVERIFY(std::holds_alternative<AppError>(noParent));
    QCOMPARE(std::get<AppError>(noParent).code, QStringLiteral("NOT_FOUND"));

    QVERIFY(!service.canUndo());
    QVERIFY(!doc.isModified());
    QCOMPARE(doc.sessionRevision(), revision);
    QCOMPARE(treeOf(doc.project()).nodes.size(), 1);
}

void TstNodeCommands::addChildNormalizesTitle() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("标题规范化")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const TreeSheet& tree = treeOf(doc.project());
    const auto created = nodes.addChild(tree.id, tree.rootId, QStringLiteral(" \t "));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID childId = std::get<ID>(created);
    QCOMPARE(treeOf(doc.project()).nodes.value(childId).title, QStringLiteral("未命名主题"));
}

void TstNodeCommands::addChildEnforcesTitleLimit() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("长度上限")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    const auto accepted = nodes.addChild(tree.id, tree.rootId, emojiTitle(Limits::kTitleMaxChars));
    QVERIFY(std::holds_alternative<ID>(accepted));

    const auto rejected = nodes.addChild(tree.id, tree.rootId,
                                         emojiTitle(Limits::kTitleMaxChars + 1));
    QVERIFY(std::holds_alternative<AppError>(rejected));
    QCOMPARE(std::get<AppError>(rejected).code, QStringLiteral("LIMIT_EXCEEDED"));
}

void TstNodeCommands::addChildEnforcesNodeLimit() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("节点上限"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    for (int i = 0; i < Limits::kMaxNodes - 1; ++i) {
        addRawNode(project, rootId, i, QStringLiteral("child-%1").arg(i),
                   QStringLiteral("节点%1").arg(i));
    }

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    QCOMPARE(treeOf(doc.project()).nodes.size(), Limits::kMaxNodes);
    const auto result = nodes.addChild(sheetId, rootId, QStringLiteral("超限"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("LIMIT_EXCEEDED"));
    QVERIFY(!service.canUndo());
}

void TstNodeCommands::renameNodeSyncsTaskAndRevision() {
    LinkedProject data = makeLinkedProject();
    ProjectDocument doc;
    doc.replaceProject(data.project);
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    QVERIFY(std::holds_alternative<bool>(
        tasks.renameNode(data.sheetId, data.rootId, QStringLiteral("  新标题  "))));

    const TreeSheet& tree = treeOf(doc.project());
    QCOMPARE(tree.nodes.value(data.rootId).title, QStringLiteral("新标题"));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("新标题"));
    QCOMPARE(tree.nodes.value(data.rootId).contentRevision, 2);
    QVERIFY(service.canUndo());

    service.undo();
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).title, QStringLiteral("中心主题"));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("中心主题"));
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).contentRevision, 1);
}

void TstNodeCommands::renameNodeNoOpKeepsHistoryClean() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("无操作改名")));
    doc.setClean();
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const TreeSheet& tree = treeOf(doc.project());
    QVERIFY(!service.canUndo());
    QVERIFY(!doc.isModified());
    const qint64 revision = doc.sessionRevision();
    const qint64 contentRevision = tree.nodes.value(tree.rootId).contentRevision;

    const auto result = tasks.renameNode(tree.id, tree.rootId, QStringLiteral("  中心主题 "));
    QVERIFY(std::holds_alternative<bool>(result));
    QVERIFY(std::get<bool>(result));
    QVERIFY(!service.canUndo());
    QVERIFY(!doc.isModified());
    QCOMPARE(doc.sessionRevision(), revision);
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).contentRevision, contentRevision);
}

void TstNodeCommands::renameNodeRepairsMismatchedTask() {
    LinkedProject data = makeLinkedProject(QStringLiteral("旧任务标题"));
    ProjectDocument doc;
    doc.replaceProject(data.project);
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    // 节点标题未变化，但关联任务标题不一致，属于需要修复的有效修改。
    const auto result = tasks.renameNode(data.sheetId, data.rootId, QStringLiteral("中心主题"));
    QVERIFY(std::holds_alternative<bool>(result));
    QVERIFY(std::get<bool>(result));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("中心主题"));
    QVERIFY(service.canUndo());
    // 节点标题未变化，只有任务标题被修复：节点contentRevision不增。
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).contentRevision, 1);
}

void TstNodeCommands::renameTaskSyncsNodeAndRevision() {
    LinkedProject data = makeLinkedProject();
    ProjectDocument doc;
    doc.replaceProject(data.project);
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    QVERIFY(std::holds_alternative<bool>(
        tasks.renameTask(data.taskId, QStringLiteral("  新任务  "))));

    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("新任务"));
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).title, QStringLiteral("新任务"));
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).contentRevision, 2);
    QVERIFY(service.canUndo());

    service.undo();
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("中心主题"));
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).title, QStringLiteral("中心主题"));
}

void TstNodeCommands::renameTaskNoOpKeepsHistoryClean() {
    LinkedProject data = makeLinkedProject();
    ProjectDocument doc;
    doc.replaceProject(data.project);
    doc.setClean();
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const qint64 revision = doc.sessionRevision();
    const auto result = tasks.renameTask(data.taskId, QStringLiteral("  中心主题  "));
    QVERIFY(std::holds_alternative<bool>(result));
    QVERIFY(std::get<bool>(result));
    QVERIFY(!service.canUndo());
    QVERIFY(!doc.isModified());
    QCOMPARE(doc.sessionRevision(), revision);
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).contentRevision, 1);
}

void TstNodeCommands::renameRejectsOverlongTitle() {
    LinkedProject data = makeLinkedProject();
    ProjectDocument doc;
    doc.replaceProject(data.project);
    CommandService service(&doc);
    TaskCommands tasks(&service, &doc);

    const QString tooLong = emojiTitle(Limits::kTitleMaxChars + 1);
    const auto nodeResult = tasks.renameNode(data.sheetId, data.rootId, tooLong);
    QVERIFY(std::holds_alternative<AppError>(nodeResult));
    QCOMPARE(std::get<AppError>(nodeResult).code, QStringLiteral("LIMIT_EXCEEDED"));

    const auto taskResult = tasks.renameTask(data.taskId, tooLong);
    QVERIFY(std::holds_alternative<AppError>(taskResult));
    QCOMPARE(std::get<AppError>(taskResult).code, QStringLiteral("LIMIT_EXCEEDED"));

    QVERIFY(!service.canUndo());
    QCOMPARE(treeOf(doc.project()).nodes.value(data.rootId).title, QStringLiteral("中心主题"));
    QCOMPARE(doc.project().tasks.first().title, QStringLiteral("中心主题"));
}

QTEST_GUILESS_MAIN(TstNodeCommands)
#include "tst_nodecommands.moc"
