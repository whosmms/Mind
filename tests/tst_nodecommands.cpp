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

    void addSiblingInsertsAfterReferenceWithNormalizedOrders();
    void addSiblingAtEndKeepsAppendingOrder();
    void addSiblingRejectsRootReference();
    void addSiblingRejectsMissingReference();
    void addSiblingNormalizesTitle();
    void addSiblingSingleHistoryUndoRestores();
    void deleteSubtreeRemovesNodesAndReferences();
    void deleteSubtreeKeepAsStandaloneClearsTaskSource();
    void deleteSubtreeDeleteTogetherRemovesTaskAndDependencies();
    void deleteSubtreeMarksReviewCardsMissing();
    void deleteSubtreeKeepsRoutesAndDrafts();
    void deleteSubtreeRejectsRoot();
    void deleteSubtreeBumpsParentRevision();
    void deleteSubtreeUndoRestoresEverythingInOneStep();

    void updateMetaNoteSetsAndUndoes();
    void updateMetaNoteRejectsOverlong();
    void updateMetaIconSetsAndClears();
    void updateMetaLinkValidatesProtocol();
    void updateMetaTagsReplaceTrimAndLimits();
    void updateMetaNoOpShortCircuits();
    void updateMetaRejectsMissingNode();
    void setStyleAppliesAndUndoes();
    void setStyleClearsOverride();
    void setStyleRejectsInvalidValues();
    void setStyleNoOpShortCircuits();
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


void TstNodeCommands::addSiblingInsertsAfterReferenceWithNormalizedOrders() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("同级插入"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("a"), QStringLiteral("A"));
    addRawNode(project, rootId, 1, QStringLiteral("b"), QStringLiteral("B"));
    addRawNode(project, rootId, 2, QStringLiteral("c"), QStringLiteral("C"));

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    // 在B之后插入：顺序变为a,b,新节点,c（D-M1-03先规范化再插入）
    const auto created = nodes.addSibling(sheetId, QStringLiteral("b"), QStringLiteral("新同级"));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID newId = std::get<ID>(created);

    const TreeSheet& tree = treeOf(doc.project());
    QCOMPARE(tree.nodes.value(newId).parentId.value_or(QString()), rootId);
    QCOMPARE(tree.nodes.value(QStringLiteral("a")).order, 0);
    QCOMPARE(tree.nodes.value(QStringLiteral("b")).order, 1);
    QCOMPARE(tree.nodes.value(newId).order, 2);
    QCOMPARE(tree.nodes.value(QStringLiteral("c")).order, 3);
    QCOMPARE(tree.nodes.value(rootId).contentRevision, 2);   // 根修订+1（工厂初值1）
}

void TstNodeCommands::addSiblingAtEndKeepsAppendingOrder() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("末尾同级"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("only"), QStringLiteral("独子"));

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created = nodes.addSibling(sheetId, QStringLiteral("only"), QString());
    QVERIFY(std::holds_alternative<ID>(created));
    const ID newId = std::get<ID>(created);

    // 参照是最后一个：新节点追加在末尾；空标题转“未命名主题”
    const TreeSheet& tree = treeOf(doc.project());
    QCOMPARE(tree.nodes.value(QStringLiteral("only")).order, 0);
    QCOMPARE(tree.nodes.value(newId).order, 1);
    QCOMPARE(tree.nodes.value(newId).title, QStringLiteral("未命名主题"));
}

void TstNodeCommands::addSiblingRejectsRootReference() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("根无同级")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    const auto created = nodes.addSibling(tree.id, tree.rootId, QStringLiteral("非法"));
    QVERIFY(std::holds_alternative<AppError>(created));
    QCOMPARE(std::get<AppError>(created).code, QStringLiteral("INVALID_ARGUMENT"));
    QVERIFY(!service.canUndo());   // 失败不入历史
}

void TstNodeCommands::addSiblingRejectsMissingReference() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("参照缺失")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    const auto created = nodes.addSibling(tree.id, QStringLiteral("ghost"), QStringLiteral("x"));
    QVERIFY(std::holds_alternative<AppError>(created));
    QCOMPARE(std::get<AppError>(created).code, QStringLiteral("NOT_FOUND"));
}

void TstNodeCommands::addSiblingNormalizesTitle() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("标题规范化"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("a"), QStringLiteral("A"));

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created = nodes.addSibling(sheetId, QStringLiteral("a"), QStringLiteral("  同级  "));
    QVERIFY(std::holds_alternative<ID>(created));
    QCOMPARE(treeOf(doc.project())
                  .nodes.value(std::get<ID>(created))
                  .title,
              QStringLiteral("同级"));
}

void TstNodeCommands::addSiblingSingleHistoryUndoRestores() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("同级撤销"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("a"), QStringLiteral("A"));
    addRawNode(project, rootId, 1, QStringLiteral("b"), QStringLiteral("B"));

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created = nodes.addSibling(sheetId, QStringLiteral("a"), QStringLiteral("S"));
    QVERIFY(std::holds_alternative<ID>(created));
    const ID newId = std::get<ID>(created);

    service.undo();   // 单事务：一次撤销回到插入前
    const TreeSheet& tree = treeOf(doc.project());
    QVERIFY(!tree.nodes.contains(newId));
    QCOMPARE(tree.nodes.value(QStringLiteral("a")).order, 0);
    QCOMPARE(tree.nodes.value(QStringLiteral("b")).order, 1);
    QCOMPARE(tree.nodes.value(rootId).contentRevision, 1);

    service.redo();
    QVERIFY(treeOf(doc.project()).nodes.contains(newId));
}

void TstNodeCommands::deleteSubtreeRemovesNodesAndReferences() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("删除引用清理"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));
    addRawNode(project, rootId, 1, QStringLiteral("keeper"), QStringLiteral("保留"));
    addRawNode(project, QStringLiteral("victim"), 0, QStringLiteral("child"), QStringLiteral("子"));
    addRawNode(project, rootId, 2, QStringLiteral("outside"), QStringLiteral("外部"));

    TreeSheet& tree = treeOf(project);
    Relation inside;   // 两端都在子树内
    inside.id = QStringLiteral("rel-in");
    inside.sourceId = QStringLiteral("victim");
    inside.targetId = QStringLiteral("child");
    tree.relations.append(inside);
    Relation crossing;   // 一端在子树内一端在外
    crossing.id = QStringLiteral("rel-cross");
    crossing.sourceId = QStringLiteral("victim");
    crossing.targetId = QStringLiteral("keeper");
    tree.relations.append(crossing);
    Relation intact;   // 两端都在子树外
    intact.id = QStringLiteral("rel-ok");
    intact.sourceId = QStringLiteral("keeper");
    intact.targetId = QStringLiteral("outside");
    tree.relations.append(intact);

    GroupAnnotation parentIn;   // 父级在子树内
    parentIn.id = QStringLiteral("ga-parent");
    parentIn.parentId = QStringLiteral("victim");
    parentIn.memberIds = {QStringLiteral("child")};
    tree.annotations.append(parentIn);
    GroupAnnotation memberIn;   // 成员在子树内
    memberIn.id = QStringLiteral("ga-member");
    memberIn.parentId = rootId;
    memberIn.memberIds = {QStringLiteral("keeper"), QStringLiteral("victim")};
    tree.annotations.append(memberIn);
    GroupAnnotation intactGa;
    intactGa.id = QStringLiteral("ga-ok");
    intactGa.parentId = rootId;
    intactGa.memberIds = {QStringLiteral("keeper")};
    tree.annotations.append(intactGa);

    Comment onVictim;
    onVictim.id = QStringLiteral("c-victim");
    onVictim.nodeId = QStringLiteral("victim");
    tree.comments.append(onVictim);
    Comment onKeeper;
    onKeeper.id = QStringLiteral("c-keeper");
    onKeeper.nodeId = QStringLiteral("keeper");
    tree.comments.append(onKeeper);

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto removed = nodes.deleteSubtree(sheetId, QStringLiteral("victim"));
    QVERIFY(std::holds_alternative<bool>(removed));
    QVERIFY(std::get<bool>(removed));

    const TreeSheet& after = treeOf(doc.project());
    QVERIFY(!after.nodes.contains(QStringLiteral("victim")));
    QVERIFY(!after.nodes.contains(QStringLiteral("child")));
    QVERIFY(after.nodes.contains(QStringLiteral("keeper")));
    QCOMPARE(after.relations.size(), 1);
    QCOMPARE(after.relations.first().id, QStringLiteral("rel-ok"));
    QCOMPARE(after.annotations.size(), 1);
    QCOMPARE(after.annotations.first().id, QStringLiteral("ga-ok"));
    QCOMPARE(after.comments.size(), 1);
    QCOMPARE(after.comments.first().id, QStringLiteral("c-keeper"));
}

void TstNodeCommands::deleteSubtreeKeepAsStandaloneClearsTaskSource() {
    LinkedProject linked = makeLinkedProject();   // 任务关联root
    const ID sheetId = linked.sheetId;
    addRawNode(linked.project, linked.rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));
    Task linkedToVictim;   // 关联将被删节点的任务
    linkedToVictim.id = QStringLiteral("task-2");
    linkedToVictim.title = QStringLiteral("被删任务");
    linkedToVictim.source = TaskSource{sheetId, QStringLiteral("victim")};
    linked.project.tasks.push_back(linkedToVictim);

    ProjectDocument doc;
    doc.replaceProject(linked.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto removed = nodes.deleteSubtree(sheetId, QStringLiteral("victim"));
    QVERIFY(std::holds_alternative<bool>(removed));
    QVERIFY(std::get<bool>(removed));

    // 默认策略：任务保留为独立任务（source清除），root关联任务不受影响
    QCOMPARE(doc.project().tasks.size(), 2);
    const Task* kept = nullptr;
    const Task* untouched = nullptr;
    for (const Task& task : doc.project().tasks) {
        if (task.id == QStringLiteral("task-2")) kept = &task;
        if (task.id == linked.taskId) untouched = &task;
    }
    QVERIFY(kept != nullptr);
    QVERIFY(!kept->source.has_value());
    QVERIFY(untouched != nullptr);
    QVERIFY(untouched->source.has_value());
}

void TstNodeCommands::deleteSubtreeDeleteTogetherRemovesTaskAndDependencies() {
    LinkedProject linked = makeLinkedProject();
    const ID sheetId = linked.sheetId;
    addRawNode(linked.project, linked.rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));
    Task doomed;
    doomed.id = QStringLiteral("task-2");
    doomed.title = QStringLiteral("连坐任务");
    doomed.source = TaskSource{sheetId, QStringLiteral("victim")};
    linked.project.tasks.push_back(doomed);
    Task dependent;   // 依赖被删任务的幸存任务
    dependent.id = QStringLiteral("task-3");
    dependent.title = QStringLiteral("幸存任务");
    dependent.predecessorIds = {QStringLiteral("task-2"), linked.taskId};
    linked.project.tasks.push_back(dependent);

    ProjectDocument doc;
    doc.replaceProject(linked.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto removed = nodes.deleteSubtree(
        sheetId, QStringLiteral("victim"), NodeCommands::TaskDeletePolicy::DeleteTogether);
    QVERIFY(std::holds_alternative<bool>(removed));
    QVERIFY(std::get<bool>(removed));

    QCOMPARE(doc.project().tasks.size(), 2);   // task-2被连坐删除，root任务与task-3保留
    bool hasDoomed = false;
    const Task* survivor = nullptr;
    for (const Task& task : doc.project().tasks) {
        if (task.id == QStringLiteral("task-2")) hasDoomed = true;
        if (task.id == QStringLiteral("task-3")) survivor = &task;
    }
    QVERIFY(!hasDoomed);
    QVERIFY(survivor != nullptr);
    QCOMPARE(survivor->predecessorIds,
             QVector<ID>{linked.taskId});   // 对被删任务的依赖已清理，其余依赖保留
}

void TstNodeCommands::deleteSubtreeMarksReviewCardsMissing() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("卡片missing"));
    const ID sheetId = treeOf(project).id;
    addRawNode(project, treeOf(project).rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));
    addRawNode(project, treeOf(project).rootId, 1, QStringLiteral("keeper"), QStringLiteral("保留"));

    ReviewCard onVictim;
    onVictim.id = QStringLiteral("card-1");
    onVictim.sheetId = sheetId;
    onVictim.nodeId = QStringLiteral("victim");
    onVictim.sourceState = QStringLiteral("ok");
    project.cards.push_back(onVictim);
    ReviewCard onKeeper;
    onKeeper.id = QStringLiteral("card-2");
    onKeeper.sheetId = sheetId;
    onKeeper.nodeId = QStringLiteral("keeper");
    onKeeper.sourceState = QStringLiteral("ok");
    project.cards.push_back(onKeeper);

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto removed = nodes.deleteSubtree(sheetId, QStringLiteral("victim"));
    QVERIFY(std::get<bool>(removed));

    // 卡片保留：源被删置missing，其他卡片不受影响（D-M1-05）
    QCOMPARE(doc.project().cards.size(), 2);
    for (const ReviewCard& card : doc.project().cards) {
        if (card.id == QStringLiteral("card-1"))
            QCOMPARE(card.sourceState, QStringLiteral("missing"));
        if (card.id == QStringLiteral("card-2"))
            QCOMPARE(card.sourceState, QStringLiteral("ok"));
    }
}

void TstNodeCommands::deleteSubtreeKeepsRoutesAndDrafts() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("路线文稿不动"));
    const ID sheetId = treeOf(project).id;
    addRawNode(project, treeOf(project).rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));

    PresentationRoute route;
    route.id = QStringLiteral("route-1");
    route.sheetId = sheetId;
    route.title = QStringLiteral("讲解");
    PresentationStep step;
    step.id = QStringLiteral("step-1");
    step.nodeId = QStringLiteral("victim");
    route.steps.push_back(step);
    project.routes.push_back(route);

    TextDraft draft;
    draft.id = QStringLiteral("draft-1");
    draft.sourceSheetId = sheetId;
    draft.title = QStringLiteral("文稿");
    project.drafts.push_back(draft);

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto removed = nodes.deleteSubtree(sheetId, QStringLiteral("victim"));
    QVERIFY(std::get<bool>(removed));

    // 讲解路线与文稿不自动改写（D-M1-05：运行时重校验提示跳过）
    QCOMPARE(doc.project().routes.size(), 1);
    QCOMPARE(doc.project().routes.first().steps.first().nodeId, QStringLiteral("victim"));
    QCOMPARE(doc.project().drafts.size(), 1);
}

void TstNodeCommands::deleteSubtreeRejectsRoot() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("根不可删")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    const auto removed = nodes.deleteSubtree(tree.id, tree.rootId);
    QVERIFY(std::holds_alternative<AppError>(removed));
    QCOMPARE(std::get<AppError>(removed).code, QStringLiteral("INVALID_ARGUMENT"));
    QVERIFY(treeOf(doc.project()).nodes.contains(tree.rootId));
    QVERIFY(!service.canUndo());
}

void TstNodeCommands::deleteSubtreeBumpsParentRevision() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("父修订"));
    const ID sheetId = treeOf(project).id;
    addRawNode(project, treeOf(project).rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));
    const qint64 before = treeOf(project).nodes.value(treeOf(project).rootId).contentRevision;

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    QVERIFY(std::get<bool>(nodes.deleteSubtree(sheetId, QStringLiteral("victim"))));
    QCOMPARE(treeOf(doc.project()).nodes.value(treeOf(project).rootId).contentRevision,
             before + 1);
}

void TstNodeCommands::deleteSubtreeUndoRestoresEverythingInOneStep() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("删除单事务撤销"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("victim"), QStringLiteral("被删"));
    addRawNode(project, QStringLiteral("victim"), 0, QStringLiteral("child"), QStringLiteral("子"));
    TreeSheet& tree = treeOf(project);
    Relation rel;
    rel.id = QStringLiteral("rel-1");
    rel.sourceId = QStringLiteral("victim");
    rel.targetId = QStringLiteral("child");
    tree.relations.append(rel);
    Comment comment;
    comment.id = QStringLiteral("c-1");
    comment.nodeId = QStringLiteral("child");
    tree.comments.append(comment);
    Task task;
    task.id = QStringLiteral("task-1");
    task.title = QStringLiteral("联动任务");
    task.source = TaskSource{sheetId, QStringLiteral("victim")};
    project.tasks.push_back(task);
    ReviewCard card;
    card.id = QStringLiteral("card-1");
    card.sheetId = sheetId;
    card.nodeId = QStringLiteral("victim");
    card.sourceState = QStringLiteral("ok");
    project.cards.push_back(card);

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    QVERIFY(std::get<bool>(nodes.deleteSubtree(sheetId, QStringLiteral("victim"))));
    QVERIFY(!treeOf(doc.project()).nodes.contains(QStringLiteral("victim")));
    QVERIFY(doc.project().tasks.first().source.has_value() == false);

    // 单事务：一次undo完整恢复节点、关系、批注、任务source与卡片状态（§12）
    service.undo();
    const TreeSheet& restored = treeOf(doc.project());
    QVERIFY(restored.nodes.contains(QStringLiteral("victim")));
    QVERIFY(restored.nodes.contains(QStringLiteral("child")));
    QCOMPARE(restored.relations.size(), 1);
    QCOMPARE(restored.comments.size(), 1);
    QCOMPARE(doc.project().tasks.size(), 1);
    QVERIFY(doc.project().tasks.first().source.has_value());
    QCOMPARE(doc.project().cards.first().sourceState, QStringLiteral("ok"));
}

void TstNodeCommands::updateMetaNoteSetsAndUndoes() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("备注命令")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());
    const qint64 revision = tree.nodes.value(tree.rootId).contentRevision;

    NodeMetaUpdate update;
    update.note = QStringLiteral("第一条备注");
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, update)));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).note, QStringLiteral("第一条备注"));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).contentRevision, revision + 1);

    // 空串=清空备注（当前非空，是真实修改，可撤销）
    NodeMetaUpdate clear;
    clear.note = QString();
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, clear)));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).note, QString());
    QVERIFY(service.canUndo());

    service.undo();   // 撤销清空→回到第一条备注
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).note, QStringLiteral("第一条备注"));
}

void TstNodeCommands::updateMetaNoteRejectsOverlong() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("备注超长")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeMetaUpdate update;
    update.note = QString(Limits::kNoteMaxChars + 1, QLatin1Char('备'));
    const auto result = nodes.updateNodeMeta(tree.id, tree.rootId, update);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("LIMIT_EXCEEDED"));
    QVERIFY(treeOf(doc.project()).nodes.value(tree.rootId).note.isEmpty());
    QVERIFY(!service.canUndo());
}

void TstNodeCommands::updateMetaIconSetsAndClears() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("图标命令")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeMetaUpdate set;
    set.iconId = QStringLiteral(" sprout ");
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, set)));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).iconId.value_or(QString()),
             QStringLiteral("sprout"));   // 去首尾空白

    NodeMetaUpdate clear;
    clear.iconId = QStringLiteral("   ");
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, clear)));
    QVERIFY(!treeOf(doc.project()).nodes.value(tree.rootId).iconId.has_value());   // 空白=清除

    service.undo();   // 撤销清除→恢复图标
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).iconId.value_or(QString()),
             QStringLiteral("sprout"));
}

void TstNodeCommands::updateMetaLinkValidatesProtocol() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("链接命令")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeMetaUpdate set;
    set.link = QStringLiteral("https://example.org/中文 页面");
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, set)));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).link.value_or(QString()),
             QStringLiteral("https://example.org/中文 页面"));

    NodeMetaUpdate bad;
    bad.link = QStringLiteral("javascript:alert(1)");
    const auto rejected = nodes.updateNodeMeta(tree.id, tree.rootId, bad);
    QVERIFY(std::holds_alternative<AppError>(rejected));
    QCOMPARE(std::get<AppError>(rejected).code, QStringLiteral("INVALID_ARGUMENT"));

    NodeMetaUpdate clear;
    clear.link = QString();
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, clear)));
    QVERIFY(!treeOf(doc.project()).nodes.value(tree.rootId).link.has_value());
}

void TstNodeCommands::updateMetaTagsReplaceTrimAndLimits() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("标签命令")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeMetaUpdate set;
    set.tags = QStringList{QStringLiteral(" 重点 "), QStringLiteral("M2")};
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, set)));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).tags,
             QStringList({QStringLiteral("重点"), QStringLiteral("M2")}));   // 每项去空白

    NodeMetaUpdate tooMany;
    tooMany.tags = QStringList();
    for (int i = 0; i <= Limits::kMaxTags; ++i) tooMany.tags->append(QString::number(i));
    const auto rejected = nodes.updateNodeMeta(tree.id, tree.rootId, tooMany);
    QVERIFY(std::holds_alternative<AppError>(rejected));
    QCOMPARE(std::get<AppError>(rejected).code, QStringLiteral("LIMIT_EXCEEDED"));

    NodeMetaUpdate tooLong;
    tooLong.tags = QStringList{QString(Limits::kTagMaxChars + 1, QLatin1Char('标'))};
    QVERIFY(std::holds_alternative<AppError>(nodes.updateNodeMeta(tree.id, tree.rootId, tooLong)));

    NodeMetaUpdate blank;
    blank.tags = QStringList{QStringLiteral("  ")};
    const auto blankRejected = nodes.updateNodeMeta(tree.id, tree.rootId, blank);
    QVERIFY(std::holds_alternative<AppError>(blankRejected));
    QCOMPARE(std::get<AppError>(blankRejected).code, QStringLiteral("INVALID_ARGUMENT"));
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).tags.size(), 2);   // 原值保持
}

void TstNodeCommands::updateMetaNoOpShortCircuits() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("无操作短路")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeMetaUpdate same;
    same.note = QString();   // 当前备注即为空
    QVERIFY(std::get<bool>(nodes.updateNodeMeta(tree.id, tree.rootId, same)));
    QVERIFY(!service.canUndo());   // 无修改不入历史（D-M1-02）
}

void TstNodeCommands::updateMetaRejectsMissingNode() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("缺节点")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeMetaUpdate update;
    update.note = QStringLiteral("x");
    const auto result = nodes.updateNodeMeta(tree.id, QStringLiteral("ghost"), update);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("NOT_FOUND"));
}

void TstNodeCommands::setStyleAppliesAndUndoes() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("样式命令")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());
    const qint64 revision = tree.nodes.value(tree.rootId).contentRevision;

    NodeStyleOverride first;
    first.fill = QStringLiteral("#DDF2E7");
    first.fontSize = 20;
    first.bold = true;
    QVERIFY(std::get<bool>(nodes.setStyle(tree.id, tree.rootId, first)));
    const TreeNode& applied = treeOf(doc.project()).nodes.value(tree.rootId);
    QCOMPARE(applied.style.fill.value_or(QString()), QStringLiteral("#DDF2E7"));
    QCOMPARE(applied.style.fontSize.value_or(0), 20);
    QVERIFY(applied.style.bold.value_or(false));
    QCOMPARE(applied.contentRevision, revision + 1);

    NodeStyleOverride second;   // 整值替换：未设置的字段清除
    second.color = QStringLiteral("#243A34");
    QVERIFY(std::get<bool>(nodes.setStyle(tree.id, tree.rootId, second)));
    const TreeNode& replaced = treeOf(doc.project()).nodes.value(tree.rootId);
    QVERIFY(!replaced.style.fill.has_value());
    QCOMPARE(replaced.style.color.value_or(QString()), QStringLiteral("#243A34"));

    service.undo();   // 回到first
    const TreeNode& restored = treeOf(doc.project()).nodes.value(tree.rootId);
    QCOMPARE(restored.style.fill.value_or(QString()), QStringLiteral("#DDF2E7"));
    QVERIFY(!restored.style.color.has_value());
    QCOMPARE(restored.contentRevision, revision + 1);   // 撤销恢复到第一次修改后的修订号
}

void TstNodeCommands::setStyleClearsOverride() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("清除样式")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeStyleOverride styled;
    styled.borderColor = QStringLiteral("#A83636");
    QVERIFY(std::get<bool>(nodes.setStyle(tree.id, tree.rootId, styled)));

    QVERIFY(std::get<bool>(nodes.setStyle(tree.id, tree.rootId, NodeStyleOverride{})));   // 空override=清除
    const TreeNode& cleared = treeOf(doc.project()).nodes.value(tree.rootId);
    QVERIFY(!cleared.style.borderColor.has_value());

    service.undo();
    QCOMPARE(treeOf(doc.project()).nodes.value(tree.rootId).style.borderColor.value_or(QString()),
             QStringLiteral("#A83636"));
}

void TstNodeCommands::setStyleRejectsInvalidValues() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("样式拒绝")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    NodeStyleOverride badColor;
    badColor.fill = QStringLiteral("red");
    QVERIFY(std::holds_alternative<AppError>(nodes.setStyle(tree.id, tree.rootId, badColor)));

    NodeStyleOverride badShape;
    badShape.shape = QStringLiteral("hexagon");
    QVERIFY(std::holds_alternative<AppError>(nodes.setStyle(tree.id, tree.rootId, badShape)));

    NodeStyleOverride badSize;
    badSize.fontSize = Limits::kFontSizeMax + 1;
    QVERIFY(std::holds_alternative<AppError>(nodes.setStyle(tree.id, tree.rootId, badSize)));

    NodeStyleOverride badWidth;
    badWidth.borderWidth = 0.0;
    QVERIFY(std::holds_alternative<AppError>(nodes.setStyle(tree.id, tree.rootId, badWidth)));

    QVERIFY(!service.canUndo());   // 全部失败不入历史
    QVERIFY(!treeOf(doc.project()).nodes.value(tree.rootId).style.fill.has_value());
}

void TstNodeCommands::setStyleNoOpShortCircuits() {
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("样式短路")));
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const TreeSheet& tree = treeOf(doc.project());

    QVERIFY(std::get<bool>(nodes.setStyle(tree.id, tree.rootId, NodeStyleOverride{})));   // 当前即为空
    QVERIFY(!service.canUndo());   // 无修改不入历史（D-M1-02）
}

QTEST_GUILESS_MAIN(TstNodeCommands)
#include "tst_nodecommands.moc"
