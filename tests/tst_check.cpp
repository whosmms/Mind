#include <QtTest>

#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "features/CheckService.h"

using namespace lingya;

// 规则检查助手测试（X04，AI规范§7.4）：纯函数、规则C01-C09、按严重度排序。
class TstCheck : public QObject {
    Q_OBJECT

private slots:
    void flagsUnnamedNode();
    void flagsLongTitle();
    void flagsTaskDateError();
    void flagsDependencyCycle();
    void flagsChangedCard();
    void flagsLowContrast();
    void sortedBySeverity();
    void flagsOrphanNode();
    void flagsRelationEndpoint();
    void flagsFlowIsolated();
    void flagsDecisionWithoutBranches();
};

namespace {

bool hasRule(const QVector<Issue>& issues, const char* ruleId) {
    for (const Issue& issue : issues) {
        if (issue.ruleId == QLatin1String(ruleId)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void TstCheck::flagsUnnamedNode() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("未命名检查"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].title = QString();
    QVERIFY(hasRule(CheckService::checkProject(p), "C01"));
}

void TstCheck::flagsLongTitle() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("长标题检查"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].title = QStringLiteral("长").repeated(81);
    QVERIFY(hasRule(CheckService::checkProject(p), "C02"));
}

void TstCheck::flagsTaskDateError() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("日期检查"));
    Task task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.title = QStringLiteral("坏日期");
    task.startDate = QDate(2026, 10, 30);
    task.endDate = QDate(2026, 10, 26);
    p.tasks.push_back(task);
    QVERIFY(hasRule(CheckService::checkProject(p), "C06"));
}

void TstCheck::flagsDependencyCycle() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("依赖环检查"));
    Task a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.title = QStringLiteral("甲");
    Task b;
    b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    b.title = QStringLiteral("乙");
    a.predecessorIds << b.id;
    b.predecessorIds << a.id;
    p.tasks.push_back(a);
    p.tasks.push_back(b);
    QVERIFY(hasRule(CheckService::checkProject(p), "C07"));
}

void TstCheck::flagsChangedCard() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("卡片检查"));
    const TreeSheet& sheet = std::get<TreeSheet>(p.sheets.first());
    ReviewCard card;
    card.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    card.sheetId = sheet.id;
    card.nodeId = sheet.rootId;
    card.front = QStringLiteral("问");
    card.back = QStringLiteral("答");
    card.sourceState = QStringLiteral("changed");
    p.cards.push_back(card);
    QVERIFY(hasRule(CheckService::checkProject(p), "C08"));
}

void TstCheck::flagsLowContrast() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("对比度检查"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.defaultNodeStyle.fill = QStringLiteral("#EEEEEE");
    sheet.defaultNodeStyle.color = QStringLiteral("#CCCCCC");
    QVERIFY(hasRule(CheckService::checkProject(p), "C09"));
}

void TstCheck::sortedBySeverity() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("排序检查"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    // 同时制造一个 suggestion（长标题，C02）和一个 error（多根，C03）
    sheet.nodes[sheet.rootId].title = QStringLiteral("长").repeated(81);
    TreeNode extra;
    extra.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    extra.title = QStringLiteral("第二个根");
    extra.parentId = std::nullopt;
    sheet.nodes.insert(extra.id, extra);

    const QVector<Issue> issues = CheckService::checkProject(p);
    QVERIFY(!issues.isEmpty());
    QCOMPARE(issues.first().severity, IssueSeverity::Error);   // 错误排在最前
}

void TstCheck::flagsOrphanNode() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("孤儿检查"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    TreeNode orphan;
    orphan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    orphan.title = QStringLiteral("孤儿");
    orphan.parentId = QUuid::createUuid().toString(QUuid::WithoutBraces);   // 父级不存在
    sheet.nodes.insert(orphan.id, orphan);
    QVERIFY(hasRule(CheckService::checkProject(p), "C03"));
}

void TstCheck::flagsRelationEndpoint() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("关系检查"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    Relation rel;
    rel.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rel.sourceId = sheet.rootId;
    rel.targetId = QUuid::createUuid().toString(QUuid::WithoutBraces);   // 目标节点不存在
    sheet.relations.append(rel);
    QVERIFY(hasRule(CheckService::checkProject(p), "C03"));
}

void TstCheck::flagsFlowIsolated() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("流程检查"));
    FlowSheet flow;
    flow.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    flow.name = QStringLiteral("流程");
    FlowNode node;
    node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.title = QStringLiteral("孤立节点");
    node.role = QStringLiteral("process");
    flow.nodes.insert(node.id, node);
    p.sheets.push_back(Sheet(flow));
    QVERIFY(hasRule(CheckService::checkProject(p), "C04"));
}

void TstCheck::flagsDecisionWithoutBranches() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("判断检查"));
    FlowSheet flow;
    flow.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    flow.name = QStringLiteral("流程");

    FlowNode decision;
    decision.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    decision.title = QStringLiteral("判断");
    decision.role = QStringLiteral("decision");
    flow.nodes.insert(decision.id, decision);

    FlowNode process;
    process.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    process.title = QStringLiteral("处理");
    process.role = QStringLiteral("process");
    flow.nodes.insert(process.id, process);

    FlowEdge edge;   // 判断节点只有一条出边
    edge.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    edge.sourceId = decision.id;
    edge.targetId = process.id;
    edge.label = QStringLiteral("是");
    flow.edges.append(edge);

    p.sheets.push_back(Sheet(flow));
    QVERIFY(hasRule(CheckService::checkProject(p), "C05"));
}

QTEST_GUILESS_MAIN(TstCheck)
#include "tst_check.moc"
