#include <QtTest>

#include <algorithm>

#include <QUuid>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "commands/CommandService.h"
#include "commands/NodeCommands.h"
#include "commands/TaskCommands.h"
#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"
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

// 根 + 三个同级 a/b/c + a 的两个子节点 a-1/a-2，供移动、关系、外框、批注与片段用例复用
struct RichTree {
    Project project;
    ID sheetId;
    ID rootId;
};

RichTree makeRichTree() {
    RichTree result;
    result.project = ProjectFactory::createMindmap(QStringLiteral("命令族测试"));
    const TreeSheet& tree = treeOf(result.project);
    result.sheetId = tree.id;
    result.rootId = tree.rootId;
    addRawNode(result.project, result.rootId, 0, QStringLiteral("a"), QStringLiteral("A"));
    addRawNode(result.project, result.rootId, 1, QStringLiteral("b"), QStringLiteral("B"));
    addRawNode(result.project, result.rootId, 2, QStringLiteral("c"), QStringLiteral("C"));
    addRawNode(result.project, QStringLiteral("a"), 0, QStringLiteral("a-1"),
               QStringLiteral("A1"));
    addRawNode(result.project, QStringLiteral("a"), 1, QStringLiteral("a-2"),
               QStringLiteral("A2"));
    return result;
}

QString orderOfChildren(const Project& project, const ID& parentId) {
    const TreeSheet& tree = treeOf(project);
    QVector<QPair<int, ID>> pairs;
    for (auto it = tree.nodes.constBegin(); it != tree.nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == parentId)
            pairs.append({it.value().order, it.key()});
    }
    std::sort(pairs.begin(), pairs.end());
    QStringList ids;
    for (const auto& pair : pairs) ids.append(pair.second);
    return ids.join(QLatin1Char(','));
}

}  // namespace

class TstNodeCommands : public QObject {
    Q_OBJECT

private slots:
    void relationStyleIsAtomicAndValidated();
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

    void moveSubtreeReordersSiblingsAcrossParents();
    void moveSubtreeRejectsRootCycleAndBadReference();
    void moveSubtreeNoOpShortCircuits();
    void moveSubtreeOutOfFrameRemovesAnnotation();
    void moveSubtreeIntoFrameJoinsAnnotation();
    void moveSubtreeReordersInsideFrameKeepsAnnotation();
    void addSiblingInsideFrameJoinsAnnotation();
    void setSheetStyleAppliesAndShortCircuits();
    void addRelationCreatesAndRejectsInvalid();
    void updateRelationKeepsUntouchedFields();
    void removeRelationDeletesAndReportsMissing();
    void createAnnotationRequiresContiguousSiblings();
    void createAnnotationStoresMembersInTreeOrder();
    void updateAnnotationRevalidatesMembers();
    void removeAnnotationDeletesAndReportsMissing();
    void addCommentNormalizesTimestamps();
    void resolveCommentShortCircuitsWhenUnchanged();
    void copyFragmentCollapsesAncestorsAndKeepsInternalRefs();
    void copyFragmentKeepsOnlyInternalReferences();
    void pasteFragmentRemapsIdsRelationsAndTasks();
    void fragmentJsonRoundTrips();
    void fragmentJsonRejectsBadEnvelope();
    void pasteFragmentRejectsEmptyOrMissingParent();
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
    addRawNode(project, QStringLiteral("victim"), 1, QStringLiteral("child2"), QStringLiteral("子二"));
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

    GroupAnnotation parentIn;   // 父级在子树内 → 整框移除
    parentIn.id = QStringLiteral("ga-parent");
    parentIn.parentId = QStringLiteral("victim");
    parentIn.kind = QStringLiteral("boundary");
    parentIn.memberIds = {QStringLiteral("child"), QStringLiteral("child2")};
    tree.annotations.append(parentIn);
    GroupAnnotation memberIn;   // 3个成员里删掉1个，剩下2个仍同父连续 → 保留
    memberIn.id = QStringLiteral("ga-member");
    memberIn.parentId = rootId;
    memberIn.kind = QStringLiteral("boundary");
    memberIn.memberIds = {QStringLiteral("victim"), QStringLiteral("keeper"),
                          QStringLiteral("outside")};
    tree.annotations.append(memberIn);
    GroupAnnotation intactGa;   // 完全不涉及子树 → 原样保留
    intactGa.id = QStringLiteral("ga-ok");
    intactGa.parentId = rootId;
    intactGa.kind = QStringLiteral("boundary");
    intactGa.memberIds = {QStringLiteral("keeper"), QStringLiteral("outside")};
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
    // BUG-019 回归：删掉3个成员里的1个，剩下2个仍同父连续，外框必须保留（规范规则7只要求
    // “成员少于2移除”）；父级被删的整框移除；不涉及子树的框原样保留。
    QCOMPARE(after.annotations.size(), 2);
    QCOMPARE(after.annotations.at(0).id, QStringLiteral("ga-member"));
    QCOMPARE(after.annotations.at(0).memberIds,
             QStringList({QStringLiteral("keeper"), QStringLiteral("outside")}));
    QCOMPARE(after.annotations.at(1).id, QStringLiteral("ga-ok"));
    QCOMPARE(after.annotations.at(1).memberIds,
             QStringList({QStringLiteral("keeper"), QStringLiteral("outside")}));
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

void TstNodeCommands::moveSubtreeReordersSiblingsAcrossParents() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const qint64 aRevision = treeOf(doc.project()).nodes.value(QStringLiteral("a")).contentRevision;

    const auto moved = nodes.moveSubtree(fixture.sheetId, QStringLiteral("c"), fixture.rootId,
                                         QStringLiteral("a"));
    QVERIFY(std::holds_alternative<bool>(moved));
    QVERIFY(std::get<bool>(moved));
    QCOMPARE(orderOfChildren(doc.project(), fixture.rootId), QStringLiteral("c,a,b"));
    QCOMPARE(childOrders(doc.project(), fixture.rootId), QVector<int>({0, 1, 2}));

    // 跨父级：a-1 移到 b 之下，旧父级与新父级的contentRevision各+1
    QVERIFY(std::get<bool>(
        nodes.moveSubtree(fixture.sheetId, QStringLiteral("a-1"), QStringLiteral("b"))));
    QCOMPARE(treeOf(doc.project()).nodes.value(QStringLiteral("a-1")).parentId.value_or(QString()),
             QStringLiteral("b"));
    QCOMPARE(orderOfChildren(doc.project(), QStringLiteral("b")), QStringLiteral("a-1"));
    QCOMPARE(treeOf(doc.project()).nodes.value(QStringLiteral("a")).contentRevision, aRevision + 1);

    service.undo();
    service.undo();
    QCOMPARE(orderOfChildren(doc.project(), fixture.rootId), QStringLiteral("a,b,c"));
    QCOMPARE(treeOf(doc.project()).nodes.value(QStringLiteral("a-1")).parentId.value_or(QString()),
             QStringLiteral("a"));
    QCOMPARE(treeOf(doc.project()).nodes.value(QStringLiteral("a")).contentRevision, aRevision);
}

void TstNodeCommands::moveSubtreeRejectsRootCycleAndBadReference() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);
    const qint64 revision = doc.sessionRevision();

    const auto rootMove = nodes.moveSubtree(fixture.sheetId, fixture.rootId, QStringLiteral("a"));
    QVERIFY(std::holds_alternative<AppError>(rootMove));
    QCOMPARE(std::get<AppError>(rootMove).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto cycle = nodes.moveSubtree(fixture.sheetId, QStringLiteral("a"),
                                         QStringLiteral("a-2"));
    QVERIFY(std::holds_alternative<AppError>(cycle));
    QCOMPARE(std::get<AppError>(cycle).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto missingParent = nodes.moveSubtree(fixture.sheetId, QStringLiteral("a"),
                                                 QStringLiteral("missing"));
    QVERIFY(std::holds_alternative<AppError>(missingParent));
    QCOMPARE(std::get<AppError>(missingParent).code, QStringLiteral("NOT_FOUND"));

    // a-1 是 a 的子节点，不是根的子节点，不能作为“插入到根下某兄弟之前”的参照
    const auto badBefore = nodes.moveSubtree(fixture.sheetId, QStringLiteral("b"), fixture.rootId,
                                             QStringLiteral("a-1"));
    QVERIFY(std::holds_alternative<AppError>(badBefore));
    QCOMPARE(std::get<AppError>(badBefore).code, QStringLiteral("INVALID_ARGUMENT"));

    QCOMPARE(doc.sessionRevision(), revision);
    QVERIFY(!service.canUndo());
}

void TstNodeCommands::moveSubtreeNoOpShortCircuits() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    // a 已在首位，插到 b 之前即当前状态
    QVERIFY(std::get<bool>(
        nodes.moveSubtree(fixture.sheetId, QStringLiteral("a"), fixture.rootId,
                          QStringLiteral("b"))));
    QVERIFY(!service.canUndo());

    // c 已是末尾，beforeNodeId为空表示追加末尾
    QVERIFY(std::get<bool>(
        nodes.moveSubtree(fixture.sheetId, QStringLiteral("c"), fixture.rootId)));
    QVERIFY(!service.canUndo());
}

void TstNodeCommands::setSheetStyleAppliesAndShortCircuits() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const TreeSheet& before = treeOf(doc.project());
    const int originalSize = before.defaultNodeStyle.fontSize;
    NodeStyle style = before.defaultNodeStyle;
    style.fontSize = 20;
    style.fill = QStringLiteral("#112233");
    EdgeStyle edge = before.defaultEdgeStyle;
    edge.width = 3.0;
    edge.dashed = true;

    QVERIFY(std::get<bool>(nodes.setSheetStyle(fixture.sheetId, style, edge)));
    const TreeSheet& applied = treeOf(doc.project());
    QCOMPARE(applied.defaultNodeStyle.fontSize, 20);
    QCOMPARE(applied.defaultNodeStyle.fill, QStringLiteral("#112233"));
    QCOMPARE(applied.defaultEdgeStyle.width, 3.0);
    QVERIFY(applied.defaultEdgeStyle.dashed);
    QVERIFY(service.canUndo());

    const qint64 revision = doc.sessionRevision();
    QVERIFY(std::get<bool>(nodes.setSheetStyle(fixture.sheetId, style, edge)));   // 同值短路
    QCOMPARE(doc.sessionRevision(), revision);

    service.undo();
    QCOMPARE(treeOf(doc.project()).defaultNodeStyle.fontSize, originalSize);

    NodeStyle badColor = style;
    badColor.fill = QStringLiteral("red");
    QCOMPARE(std::get<AppError>(nodes.setSheetStyle(fixture.sheetId, badColor, edge)).code,
             QStringLiteral("INVALID_ARGUMENT"));

    NodeStyle badShape = style;
    badShape.shape = QStringLiteral("hexagon");
    QCOMPARE(std::get<AppError>(nodes.setSheetStyle(fixture.sheetId, badShape, edge)).code,
             QStringLiteral("INVALID_ARGUMENT"));

    NodeStyle badSize = style;
    badSize.fontSize = Limits::kFontSizeMax + 1;
    QCOMPARE(std::get<AppError>(nodes.setSheetStyle(fixture.sheetId, badSize, edge)).code,
             QStringLiteral("LIMIT_EXCEEDED"));

    NodeStyle badWidth = style;
    badWidth.borderWidth = 0.0;
    QCOMPARE(std::get<AppError>(nodes.setSheetStyle(fixture.sheetId, badWidth, edge)).code,
             QStringLiteral("INVALID_ARGUMENT"));

    EdgeStyle badEdge = edge;
    badEdge.width = 0.0;
    QCOMPARE(std::get<AppError>(nodes.setSheetStyle(fixture.sheetId, style, badEdge)).code,
             QStringLiteral("INVALID_ARGUMENT"));
}

// 甲复核 PR27（BUG-018）：外框[a,b]（父=根），把 a 移到 c 之下。以前命令成功、框的父仍是根、
// 成员 a 的父已是 c，校验器又不查跨父，非法分组能一路存盘。
void TstNodeCommands::moveSubtreeOutOfFrameRemovesAnnotation() {
    RichTree fixture = makeRichTree();
    GroupAnnotation annotation;
    annotation.id = QStringLiteral("ga-move-out");
    annotation.kind = QStringLiteral("boundary");
    annotation.parentId = fixture.rootId;
    annotation.memberIds = {QStringLiteral("a"), QStringLiteral("b")};
    treeOf(fixture.project).annotations.append(annotation);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto moved = nodes.moveSubtree(fixture.sheetId, QStringLiteral("a"),
                                         QStringLiteral("c"), QString());
    QVERIFY(std::holds_alternative<bool>(moved));
    QVERIFY(std::get<bool>(moved));

    const TreeSheet& after = treeOf(doc.project());
    QCOMPARE(after.nodes.value(QStringLiteral("a")).parentId.value(), QStringLiteral("c"));
    // 剩 b 一个成员，按规则7整框移除；不能留下跨父的框
    QVERIFY(after.annotations.isEmpty());
    QVERIFY(ProjectValidator::validate(doc.project()).isEmpty());
}

// 甲复核 PR27 场景2：外框[a,b]，把同级 c 移到 a 与 b 之间。c 落在框覆盖范围内，
// 并入框、成员按树序排成 [a,c,b]；不能留下“成员被 c 隔开”的不连续分组。
void TstNodeCommands::moveSubtreeIntoFrameJoinsAnnotation() {
    RichTree fixture = makeRichTree();
    GroupAnnotation annotation;
    annotation.id = QStringLiteral("ga-move-in");
    annotation.kind = QStringLiteral("boundary");
    annotation.parentId = fixture.rootId;
    annotation.memberIds = {QStringLiteral("a"), QStringLiteral("b")};
    treeOf(fixture.project).annotations.append(annotation);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto moved = nodes.moveSubtree(fixture.sheetId, QStringLiteral("c"), fixture.rootId,
                                         QStringLiteral("b"));
    QVERIFY(std::holds_alternative<bool>(moved));
    QVERIFY(std::get<bool>(moved));

    const TreeSheet& after = treeOf(doc.project());
    QCOMPARE(orderOfChildren(doc.project(), fixture.rootId), QStringLiteral("a,c,b"));
    QCOMPARE(after.annotations.size(), 1);
    QCOMPARE(after.annotations.first().memberIds,
             QStringList({QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
    QVERIFY(ProjectValidator::validate(doc.project()).isEmpty());
}

// 成员在框内换序（b 移到 a 之前）不算破坏范围：框保留，成员表按树序重排成 [b,a]。
void TstNodeCommands::moveSubtreeReordersInsideFrameKeepsAnnotation() {
    RichTree fixture = makeRichTree();
    GroupAnnotation annotation;
    annotation.id = QStringLiteral("ga-reorder");
    annotation.kind = QStringLiteral("boundary");
    annotation.parentId = fixture.rootId;
    annotation.memberIds = {QStringLiteral("a"), QStringLiteral("b")};
    treeOf(fixture.project).annotations.append(annotation);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto moved = nodes.moveSubtree(fixture.sheetId, QStringLiteral("b"), fixture.rootId,
                                         QStringLiteral("a"));
    QVERIFY(std::holds_alternative<bool>(moved));
    QVERIFY(std::get<bool>(moved));

    const TreeSheet& after = treeOf(doc.project());
    QCOMPARE(orderOfChildren(doc.project(), fixture.rootId), QStringLiteral("b,a,c"));
    QCOMPARE(after.annotations.size(), 1);
    QCOMPARE(after.annotations.first().memberIds,
             QStringList({QStringLiteral("b"), QStringLiteral("a")}));
    QVERIFY(ProjectValidator::validate(doc.project()).isEmpty());
}

// 给框成员加同级主题：新节点插在两个成员之间，同样落在框内 → 并入，否则立刻变成不连续分组。
void TstNodeCommands::addSiblingInsideFrameJoinsAnnotation() {
    RichTree fixture = makeRichTree();
    GroupAnnotation annotation;
    annotation.id = QStringLiteral("ga-sibling");
    annotation.kind = QStringLiteral("boundary");
    annotation.parentId = fixture.rootId;
    annotation.memberIds = {QStringLiteral("a"), QStringLiteral("b")};
    treeOf(fixture.project).annotations.append(annotation);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto added = nodes.addSibling(fixture.sheetId, QStringLiteral("a"),
                                        QStringLiteral("新的同级"));
    QVERIFY(std::holds_alternative<ID>(added));
    const ID newId = std::get<ID>(added);

    const TreeSheet& after = treeOf(doc.project());
    QCOMPARE(orderOfChildren(doc.project(), fixture.rootId),
             QStringLiteral("a,") + newId + QStringLiteral(",b,c"));
    QCOMPARE(after.annotations.size(), 1);
    QCOMPARE(after.annotations.first().memberIds,
             QStringList({QStringLiteral("a"), newId, QStringLiteral("b")}));
    QVERIFY(ProjectValidator::validate(doc.project()).isEmpty());
}

void TstNodeCommands::addRelationCreatesAndRejectsInvalid() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created = nodes.addRelation(fixture.sheetId, QStringLiteral("a"),
                                           QStringLiteral("b"), QStringLiteral("  依赖  "),
                                           QStringLiteral("both"));
    QVERIFY(std::holds_alternative<ID>(created));
    const TreeSheet& tree = treeOf(doc.project());
    QCOMPARE(tree.relations.size(), 1);
    QCOMPARE(tree.relations.first().label, QStringLiteral("依赖"));
    QCOMPARE(tree.relations.first().arrow, QStringLiteral("both"));
    QCOMPARE(tree.relations.first().sourceId, QStringLiteral("a"));
    QVERIFY(service.canUndo());

    service.undo();
    QVERIFY(treeOf(doc.project()).relations.isEmpty());

    const auto self = nodes.addRelation(fixture.sheetId, QStringLiteral("a"), QStringLiteral("a"),
                                        QString());
    QCOMPARE(std::get<AppError>(self).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto missing = nodes.addRelation(fixture.sheetId, QStringLiteral("a"),
                                           QStringLiteral("missing"), QString());
    QCOMPARE(std::get<AppError>(missing).code, QStringLiteral("NOT_FOUND"));

    const auto badArrow = nodes.addRelation(fixture.sheetId, QStringLiteral("a"),
                                            QStringLiteral("b"), QString(),
                                            QStringLiteral("forward"));
    QCOMPARE(std::get<AppError>(badArrow).code, QStringLiteral("INVALID_ARGUMENT"));

    QVERIFY(std::holds_alternative<ID>(nodes.addRelation(fixture.sheetId, QStringLiteral("a"),
                                                         QStringLiteral("b"), QString())));
    const auto duplicate = nodes.addRelation(fixture.sheetId, QStringLiteral("a"),
                                             QStringLiteral("b"), QString());
    QCOMPARE(std::get<AppError>(duplicate).code, QStringLiteral("INVALID_ARGUMENT"));
    QCOMPARE(treeOf(doc.project()).relations.size(), 1);
}

void TstNodeCommands::updateRelationKeepsUntouchedFields() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const ID relationId = std::get<ID>(
        nodes.addRelation(fixture.sheetId, QStringLiteral("a"), QStringLiteral("b"),
                          QStringLiteral("原标签")));

    RelationUpdate labelOnly;
    labelOnly.label = QStringLiteral("  新标签  ");
    QVERIFY(std::get<bool>(nodes.updateRelation(fixture.sheetId, relationId, labelOnly)));
    QCOMPARE(treeOf(doc.project()).relations.first().label, QStringLiteral("新标签"));
    QCOMPARE(treeOf(doc.project()).relations.first().arrow, QStringLiteral("end"));

    const qint64 revision = doc.sessionRevision();
    QVERIFY(std::get<bool>(nodes.updateRelation(fixture.sheetId, relationId, labelOnly)));
    QCOMPARE(doc.sessionRevision(), revision);   // 无操作短路

    RelationUpdate arrowOnly;
    arrowOnly.arrow = QStringLiteral("none");
    QVERIFY(std::get<bool>(nodes.updateRelation(fixture.sheetId, relationId, arrowOnly)));
    QCOMPARE(treeOf(doc.project()).relations.first().arrow, QStringLiteral("none"));

    RelationUpdate badArrow;
    badArrow.arrow = QStringLiteral("both-ways");
    QCOMPARE(std::get<AppError>(nodes.updateRelation(fixture.sheetId, relationId, badArrow)).code,
             QStringLiteral("INVALID_ARGUMENT"));

    QCOMPARE(std::get<AppError>(
                 nodes.updateRelation(fixture.sheetId, QStringLiteral("missing"), labelOnly))
                 .code,
             QStringLiteral("NOT_FOUND"));
}

void TstNodeCommands::removeRelationDeletesAndReportsMissing() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const ID relationId = std::get<ID>(
        nodes.addRelation(fixture.sheetId, QStringLiteral("a"), QStringLiteral("b"), QString()));
    QVERIFY(std::get<bool>(nodes.removeRelation(fixture.sheetId, relationId)));
    QVERIFY(treeOf(doc.project()).relations.isEmpty());

    QCOMPARE(std::get<AppError>(nodes.removeRelation(fixture.sheetId, relationId)).code,
             QStringLiteral("NOT_FOUND"));

    service.undo();
    QCOMPARE(treeOf(doc.project()).relations.size(), 1);
}

void TstNodeCommands::createAnnotationRequiresContiguousSiblings() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto single = nodes.createAnnotation(fixture.sheetId, fixture.rootId,
                                               QStringLiteral("boundary"),
                                               {QStringLiteral("a")});
    QCOMPARE(std::get<AppError>(single).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto nonContiguous =
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("c")});
    QCOMPARE(std::get<AppError>(nonContiguous).code, QStringLiteral("INVALID_ARGUMENT"));

    // a 与 a-1 不是同一父级的兄弟
    const auto crossParent =
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("a-1")});
    QCOMPARE(std::get<AppError>(crossParent).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto duplicated =
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("a")});
    QCOMPARE(std::get<AppError>(duplicated).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto badKind =
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("frame"),
                               {QStringLiteral("a"), QStringLiteral("b")});
    QCOMPARE(std::get<AppError>(badKind).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto badColor =
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("b")}, QString(),
                               QStringLiteral("#12345"));
    QCOMPARE(std::get<AppError>(badColor).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto missingParent =
        nodes.createAnnotation(fixture.sheetId, QStringLiteral("missing"),
                               QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("b")});
    QCOMPARE(std::get<AppError>(missingParent).code, QStringLiteral("NOT_FOUND"));

    QVERIFY(!service.canUndo());
    QVERIFY(treeOf(doc.project()).annotations.isEmpty());
}

void TstNodeCommands::createAnnotationStoresMembersInTreeOrder() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created =
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("summary"),
                               {QStringLiteral("b"), QStringLiteral("a")},
                               QStringLiteral("  概要  "));
    QVERIFY(std::holds_alternative<ID>(created));
    const TreeSheet& tree = treeOf(doc.project());
    QCOMPARE(tree.annotations.size(), 1);
    QCOMPARE(tree.annotations.first().kind, QStringLiteral("summary"));
    QCOMPARE(tree.annotations.first().memberIds,
             QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(tree.annotations.first().text, QStringLiteral("概要"));
    QVERIFY(tree.annotations.first().color.isEmpty());

    service.undo();
    QVERIFY(treeOf(doc.project()).annotations.isEmpty());
}

void TstNodeCommands::updateAnnotationRevalidatesMembers() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const ID annotationId = std::get<ID>(
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("b")},
                               QStringLiteral("组")));

    AnnotationUpdate members;
    members.memberIds = QStringList({QStringLiteral("c"), QStringLiteral("b")});
    QVERIFY(std::get<bool>(nodes.updateAnnotation(fixture.sheetId, annotationId, members)));
    QCOMPARE(treeOf(doc.project()).annotations.first().memberIds,
             QStringList({QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(treeOf(doc.project()).annotations.first().text, QStringLiteral("组"));

    const qint64 revision = doc.sessionRevision();
    QVERIFY(std::get<bool>(nodes.updateAnnotation(fixture.sheetId, annotationId, members)));
    QCOMPARE(doc.sessionRevision(), revision);   // 无操作短路

    AnnotationUpdate nonContiguous;
    nonContiguous.memberIds = QStringList({QStringLiteral("a"), QStringLiteral("c")});
    QCOMPARE(
        std::get<AppError>(nodes.updateAnnotation(fixture.sheetId, annotationId, nonContiguous))
            .code,
        QStringLiteral("INVALID_ARGUMENT"));

    AnnotationUpdate colorAndText;
    colorAndText.text = QStringLiteral("改过的概要");
    colorAndText.color = QStringLiteral("#247A62");
    QVERIFY(std::get<bool>(nodes.updateAnnotation(fixture.sheetId, annotationId, colorAndText)));
    QCOMPARE(treeOf(doc.project()).annotations.first().text, QStringLiteral("改过的概要"));
    QCOMPARE(treeOf(doc.project()).annotations.first().color, QStringLiteral("#247A62"));
}

void TstNodeCommands::removeAnnotationDeletesAndReportsMissing() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const ID annotationId = std::get<ID>(
        nodes.createAnnotation(fixture.sheetId, fixture.rootId, QStringLiteral("boundary"),
                               {QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY(std::get<bool>(nodes.removeAnnotation(fixture.sheetId, annotationId)));
    QVERIFY(treeOf(doc.project()).annotations.isEmpty());
    QCOMPARE(std::get<AppError>(nodes.removeAnnotation(fixture.sheetId, annotationId)).code,
             QStringLiteral("NOT_FOUND"));

    service.undo();
    QCOMPARE(treeOf(doc.project()).annotations.size(), 1);
}

void TstNodeCommands::addCommentNormalizesTimestamps() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto created = nodes.addComment(fixture.sheetId, QStringLiteral("a"),
                                          QStringLiteral("  张三  "),
                                          QStringLiteral("  这里需要补充  "));
    QVERIFY(std::holds_alternative<ID>(created));
    const TreeSheet& tree = treeOf(doc.project());
    QCOMPARE(tree.comments.size(), 1);
    QCOMPARE(tree.comments.first().author, QStringLiteral("张三"));
    QCOMPARE(tree.comments.first().body, QStringLiteral("这里需要补充"));
    QVERIFY(!tree.comments.first().resolved);
    QVERIFY(tree.comments.first().createdAt.isValid());
    QCOMPARE(tree.comments.first().createdAt.timeSpec(), Qt::UTC);
    QCOMPARE(tree.comments.first().createdAt, tree.comments.first().updatedAt);

    const auto blank = nodes.addComment(fixture.sheetId, QStringLiteral("a"), QString(),
                                        QStringLiteral("   "));
    QCOMPARE(std::get<AppError>(blank).code, QStringLiteral("INVALID_ARGUMENT"));

    const auto tooLong = nodes.addComment(fixture.sheetId, QStringLiteral("a"), QString(),
                                          emojiTitle(Limits::kCommentMaxChars + 1));
    QCOMPARE(std::get<AppError>(tooLong).code, QStringLiteral("LIMIT_EXCEEDED"));

    const auto missingNode =
        nodes.addComment(fixture.sheetId, QStringLiteral("missing"), QString(),
                         QStringLiteral("正文"));
    QCOMPARE(std::get<AppError>(missingNode).code, QStringLiteral("NOT_FOUND"));

    service.undo();
    QVERIFY(treeOf(doc.project()).comments.isEmpty());
}

void TstNodeCommands::resolveCommentShortCircuitsWhenUnchanged() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const ID commentId = std::get<ID>(nodes.addComment(fixture.sheetId, QStringLiteral("a"),
                                                       QStringLiteral("乙"),
                                                       QStringLiteral("待确认")));
    QVERIFY(std::get<bool>(nodes.resolveComment(fixture.sheetId, commentId, true)));
    QVERIFY(treeOf(doc.project()).comments.first().resolved);

    const qint64 revision = doc.sessionRevision();
    QVERIFY(std::get<bool>(nodes.resolveComment(fixture.sheetId, commentId, true)));
    QCOMPARE(doc.sessionRevision(), revision);   // 状态相同则短路

    QVERIFY(std::get<bool>(nodes.resolveComment(fixture.sheetId, commentId, false)));
    QVERIFY(!treeOf(doc.project()).comments.first().resolved);

    service.undo();   // 回到“已解决”
    QVERIFY(treeOf(doc.project()).comments.first().resolved);

    QCOMPARE(std::get<AppError>(nodes.resolveComment(fixture.sheetId, QStringLiteral("missing")))
                 .code,
             QStringLiteral("NOT_FOUND"));
}

void TstNodeCommands::copyFragmentCollapsesAncestorsAndKeepsInternalRefs() {
    RichTree fixture = makeRichTree();
    Relation relation;
    relation.id = QStringLiteral("rel-1");
    relation.sourceId = QStringLiteral("a-1");
    relation.targetId = QStringLiteral("a-2");
    treeOf(fixture.project).relations.append(relation);

    GroupAnnotation annotation;
    annotation.id = QStringLiteral("anno-1");
    annotation.parentId = QStringLiteral("a");
    annotation.kind = QStringLiteral("boundary");
    annotation.memberIds = QStringList({QStringLiteral("a-1"), QStringLiteral("a-2")});
    treeOf(fixture.project).annotations.append(annotation);

    Comment comment;
    comment.id = QStringLiteral("cmt-1");
    comment.nodeId = QStringLiteral("a-1");
    comment.author = QStringLiteral("甲");
    comment.body = QStringLiteral("检查");
    treeOf(fixture.project).comments.append(comment);

    Task task;
    task.id = QStringLiteral("t-1");
    task.title = QStringLiteral("A1");
    task.source = TaskSource{fixture.sheetId, QStringLiteral("a-1")};
    task.progress = 40;
    fixture.project.tasks.append(task);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    // 祖先与后代同选：只处理最高层子树一次（规范§5.3）
    const auto copied = nodes.copyFragment(
        fixture.sheetId, {QStringLiteral("a"), QStringLiteral("a-1")});
    QVERIFY(std::holds_alternative<Fragment>(copied));
    const Fragment& fragment = std::get<Fragment>(copied);
    QCOMPARE(fragment.rootIds, QVector<ID>({QStringLiteral("a")}));
    QCOMPARE(fragment.nodes.size(), 3);
    QVERIFY(fragment.nodes.contains(QStringLiteral("a-1")));
    QVERIFY(fragment.nodes.contains(QStringLiteral("a-2")));
    QVERIFY(!fragment.nodes.value(QStringLiteral("a")).parentId.has_value());
    QCOMPARE(fragment.relations.size(), 1);
    QCOMPARE(fragment.annotations.size(), 1);
    QCOMPARE(fragment.comments.size(), 1);
    QCOMPARE(fragment.tasks.size(), 1);
    QVERIFY(!service.canUndo());   // 复制不写数据、不入历史

    // 两个不相邻的根：按树顺序排列
    const auto twoRoots = nodes.copyFragment(
        fixture.sheetId, {QStringLiteral("b"), QStringLiteral("a")});
    QVERIFY(std::holds_alternative<Fragment>(twoRoots));
    QCOMPARE(std::get<Fragment>(twoRoots).rootIds,
             QVector<ID>({QStringLiteral("a"), QStringLiteral("b")}));
}

void TstNodeCommands::copyFragmentKeepsOnlyInternalReferences() {
    RichTree fixture = makeRichTree();
    Relation external;
    external.id = QStringLiteral("rel-out");
    external.sourceId = QStringLiteral("b");
    external.targetId = QStringLiteral("a");
    treeOf(fixture.project).relations.append(external);

    Comment outside;
    outside.id = QStringLiteral("cmt-out");
    outside.nodeId = QStringLiteral("b");
    outside.body = QStringLiteral("不在片段内");
    treeOf(fixture.project).comments.append(outside);

    Task outsideTask;
    outsideTask.id = QStringLiteral("t-out");
    outsideTask.title = QStringLiteral("B");
    outsideTask.source = TaskSource{fixture.sheetId, QStringLiteral("b")};
    fixture.project.tasks.append(outsideTask);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const Fragment fragment =
        std::get<Fragment>(nodes.copyFragment(fixture.sheetId, {QStringLiteral("a")}));
    QVERIFY(fragment.relations.isEmpty());   // 端点不全在片段内，不复制
    QVERIFY(fragment.comments.isEmpty());
    QVERIFY(fragment.tasks.isEmpty());
    QCOMPARE(fragment.nodes.size(), 3);
}

void TstNodeCommands::pasteFragmentRemapsIdsRelationsAndTasks() {
    RichTree fixture = makeRichTree();
    Relation relation;
    relation.id = QStringLiteral("rel-1");
    relation.sourceId = QStringLiteral("a-1");
    relation.targetId = QStringLiteral("a-2");
    relation.label = QStringLiteral("顺序");
    treeOf(fixture.project).relations.append(relation);

    GroupAnnotation annotation;
    annotation.id = QStringLiteral("anno-1");
    annotation.parentId = QStringLiteral("a");
    annotation.kind = QStringLiteral("boundary");
    annotation.memberIds = QStringList({QStringLiteral("a-1"), QStringLiteral("a-2")});
    treeOf(fixture.project).annotations.append(annotation);

    Comment comment;
    comment.id = QStringLiteral("cmt-1");
    comment.nodeId = QStringLiteral("a-1");
    comment.body = QStringLiteral("检查");
    treeOf(fixture.project).comments.append(comment);

    Task first;
    first.id = QStringLiteral("t-1");
    first.title = QStringLiteral("A1");
    first.source = TaskSource{fixture.sheetId, QStringLiteral("a-1")};
    first.startDate = QDate(2026, 10, 1);
    first.endDate = QDate(2026, 10, 3);
    first.progress = 60;
    Task second;
    second.id = QStringLiteral("t-2");
    second.title = QStringLiteral("A2");
    second.source = TaskSource{fixture.sheetId, QStringLiteral("a-2")};
    second.predecessorIds = QStringList({QStringLiteral("t-1"), QStringLiteral("t-ext")});
    Task external;
    external.id = QStringLiteral("t-ext");
    external.title = QStringLiteral("B外部任务");
    external.source = TaskSource{fixture.sheetId, QStringLiteral("b")};
    fixture.project.tasks.append(first);
    fixture.project.tasks.append(second);
    fixture.project.tasks.append(external);

    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const Fragment fragment =
        std::get<Fragment>(nodes.copyFragment(fixture.sheetId, {QStringLiteral("a")}));
    const auto pasted = nodes.pasteFragment(fixture.sheetId, QStringLiteral("b"), fragment);
    QVERIFY(std::holds_alternative<ID>(pasted));
    const ID newRoot = std::get<ID>(pasted);
    QVERIFY(newRoot != QStringLiteral("a"));

    const Project& after = doc.project();
    const TreeSheet& tree = treeOf(after);
    QCOMPARE(tree.nodes.size(), 9);   // 原6 + 粘贴3
    QCOMPARE(tree.nodes.value(newRoot).title, QStringLiteral("A"));
    QCOMPARE(tree.nodes.value(newRoot).parentId.value_or(QString()), QStringLiteral("b"));
    QCOMPARE(orderOfChildren(after, QStringLiteral("b")), newRoot);

    // 子节点ID全部重新生成，父子关系在片段内部重建
    QSet<ID> newChildren;
    for (auto it = tree.nodes.constBegin(); it != tree.nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == newRoot)
            newChildren.insert(it.key());
    }
    QCOMPARE(newChildren.size(), 2);
    QVERIFY(!newChildren.contains(QStringLiteral("a-1")));

    // 关系线：原1条 + 粘贴1条，端点指向新节点
    QCOMPARE(tree.relations.size(), 2);
    ID pastedRelationId;
    for (const Relation& item : tree.relations) {
        if (item.sourceId != QStringLiteral("a-1")) {
            pastedRelationId = item.id;
            QVERIFY(newChildren.contains(item.sourceId));
            QVERIFY(newChildren.contains(item.targetId));
        }
    }
    QVERIFY(!pastedRelationId.isEmpty());

    // 外框成员与批注节点同步重映射
    QCOMPARE(tree.annotations.size(), 2);
    for (const GroupAnnotation& item : tree.annotations) {
        if (item.parentId != QStringLiteral("a")) {
            QCOMPARE(item.parentId, newRoot);
            for (const ID& member : item.memberIds) QVERIFY(newChildren.contains(member));
        }
    }
    QCOMPARE(tree.comments.size(), 2);
    for (const Comment& item : tree.comments) {
        if (item.id != QStringLiteral("cmt-1")) QVERIFY(newChildren.contains(item.nodeId));
    }

    // 任务：独立副本，继承日期与进度；前置依赖仅保留片段内的映射结果
    QCOMPARE(after.tasks.size(), 5);
    ID pastedFirstTask;
    ID pastedSecondTask;
    for (const Task& item : after.tasks) {
        if (item.id == QStringLiteral("t-1") || item.id == QStringLiteral("t-2") ||
            item.id == QStringLiteral("t-ext"))
            continue;
        QVERIFY(item.source.has_value());
        QCOMPARE(item.source->sheetId, fixture.sheetId);
        QVERIFY(newChildren.contains(item.source->nodeId));
        if (item.title == QStringLiteral("A1")) {
            pastedFirstTask = item.id;
            QCOMPARE(item.progress, 60);
            QCOMPARE(item.startDate.value_or(QDate()), QDate(2026, 10, 1));
        } else {
            pastedSecondTask = item.id;
        }
    }
    QVERIFY(!pastedFirstTask.isEmpty());
    QVERIFY(!pastedSecondTask.isEmpty());
    for (const Task& item : after.tasks) {
        if (item.id != pastedSecondTask) continue;
        QCOMPARE(item.predecessorIds, QStringList({pastedFirstTask}));   // 外部前置被清除
    }

    service.undo();
    QCOMPARE(treeOf(doc.project()).nodes.size(), 6);
    QCOMPARE(doc.project().tasks.size(), 3);
    QVERIFY(treeOf(doc.project()).relations.size() == 1);
}

void TstNodeCommands::fragmentJsonRoundTrips() {
    // ProjectJson要求节点ID为UUID，故用真实ID形态构造片段
    Project project = ProjectFactory::createMindmap(QStringLiteral("片段序列化"));
    const ID sheetId = treeOf(project).id;
    const ID rootId = treeOf(project).rootId;
    const ID parentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const ID childId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    addRawNode(project, rootId, 0, parentId, QStringLiteral("父主题"));
    addRawNode(project, parentId, 0, childId, QStringLiteral("子主题"));
    Relation relation;
    relation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    relation.sourceId = childId;
    relation.targetId = parentId;
    relation.style.color = QStringLiteral("#247A62");
    treeOf(project).relations.append(relation);

    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const Fragment original =
        std::get<Fragment>(nodes.copyFragment(sheetId, {parentId}));
    const QJsonObject json = original.toJson();
    QCOMPARE(json.value(QStringLiteral("fragmentVersion")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("roots")).toArray().size(), 1);
    QVERIFY(json.value(QStringLiteral("payload")).isObject());

    const auto parsed = Fragment::fromJson(json);
    QVERIFY(std::holds_alternative<Fragment>(parsed));
    const Fragment& restored = std::get<Fragment>(parsed);
    QCOMPARE(restored.rootIds, original.rootIds);
    QCOMPARE(restored.nodes.size(), 2);
    QVERIFY(!restored.nodes.value(parentId).parentId.has_value());
    QCOMPARE(restored.nodes.value(childId).parentId.value_or(QString()), parentId);
    QCOMPARE(restored.relations.size(), 1);

    const auto pasted = nodes.pasteFragment(sheetId, rootId, restored);
    QVERIFY(std::holds_alternative<ID>(pasted));
    QCOMPARE(treeOf(doc.project()).nodes.size(), 5);   // 原3 + 粘贴2
}

void TstNodeCommands::fragmentJsonRejectsBadEnvelope() {
    QJsonObject empty;
    QCOMPARE(std::get<AppError>(Fragment::fromJson(empty)).code,
             QStringLiteral("INVALID_ARGUMENT"));

    QJsonObject future;
    future.insert(QStringLiteral("fragmentVersion"), 99);
    future.insert(QStringLiteral("roots"), QJsonArray({QStringLiteral("a")}));
    future.insert(QStringLiteral("payload"), QJsonObject());
    QCOMPARE(std::get<AppError>(Fragment::fromJson(future)).code,
             QStringLiteral("UNSUPPORTED_SCHEMA"));

    QJsonObject noRoots;
    noRoots.insert(QStringLiteral("fragmentVersion"), 1);
    noRoots.insert(QStringLiteral("payload"), QJsonObject());
    QCOMPARE(std::get<AppError>(Fragment::fromJson(noRoots)).code,
             QStringLiteral("INVALID_ARGUMENT"));
}

void TstNodeCommands::pasteFragmentRejectsEmptyOrMissingParent() {
    RichTree fixture = makeRichTree();
    ProjectDocument doc;
    doc.replaceProject(fixture.project);
    CommandService service(&doc);
    NodeCommands nodes(&service, &doc);

    const auto empty = nodes.pasteFragment(fixture.sheetId, fixture.rootId, Fragment{});
    QCOMPARE(std::get<AppError>(empty).code, QStringLiteral("INVALID_ARGUMENT"));

    const Fragment fragment =
        std::get<Fragment>(nodes.copyFragment(fixture.sheetId, {QStringLiteral("a")}));
    const auto missingParent =
        nodes.pasteFragment(fixture.sheetId, QStringLiteral("missing"), fragment);
    QCOMPARE(std::get<AppError>(missingParent).code, QStringLiteral("NOT_FOUND"));

    const auto badSheet = nodes.pasteFragment(QStringLiteral("missing"), fixture.rootId, fragment);
    QCOMPARE(std::get<AppError>(badSheet).code, QStringLiteral("NOT_FOUND"));
    QVERIFY(!service.canUndo());
}

void TstNodeCommands::relationStyleIsAtomicAndValidated()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap(QStringLiteral("关系样式")));
    CommandService commands(&doc);
    NodeCommands nodes(&commands, &doc);
    const ID sheet = treeOf(doc.project()).id, root = treeOf(doc.project()).rootId;
    const auto child = nodes.addChild(sheet, root, QStringLiteral("目标"));
    QVERIFY(std::holds_alternative<ID>(child));
    const auto relation =
        nodes.addRelation(sheet, root, std::get<ID>(child), QStringLiteral("原标签"));
    QVERIFY(std::holds_alternative<ID>(relation));
    commands.clearHistory();
    doc.setClean();
    const Relation before = treeOf(doc.project()).relations.first();
    RelationUpdate update;
    update.label = QStringLiteral("新标签");
    update.arrow = QStringLiteral("both");
    update.style = EdgeStyle{QStringLiteral("#406080"), 3.125, true};
    const qint64 revision = doc.sessionRevision();
    QSignalSpy changes(&doc, &ProjectDocument::changed);
    QVERIFY(
        std::holds_alternative<bool>(nodes.updateRelation(sheet, std::get<ID>(relation), update)));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(doc.sessionRevision(), revision + 1);
    QCOMPARE(treeOf(doc.project()).relations.first().style.width, 3.125);
    commands.undo();
    QCOMPARE(treeOf(doc.project()).relations.first().label, before.label);
    QCOMPARE(treeOf(doc.project()).relations.first().style.color, before.style.color);
    commands.redo();
    const qint64 after = doc.sessionRevision();
    QVERIFY(
        std::holds_alternative<bool>(nodes.updateRelation(sheet, std::get<ID>(relation), update)));
    QCOMPARE(doc.sessionRevision(), after);
    for (const EdgeStyle &invalid :
         {EdgeStyle{QStringLiteral("red"), 1, false},
          EdgeStyle{QStringLiteral("#406080"), 0, false},
          EdgeStyle{QStringLiteral("#406080"), std::numeric_limits<double>::infinity(), false}})
    {
        update.style = invalid;
        QVERIFY(std::holds_alternative<AppError>(
            nodes.updateRelation(sheet, std::get<ID>(relation), update)));
        QCOMPARE(doc.sessionRevision(), after);
        QCOMPARE(treeOf(doc.project()).relations.first().style.width, 3.125);
    }
}

QTEST_GUILESS_MAIN(TstNodeCommands)
#include "tst_nodecommands.moc"
