#include <QtTest>

#include "domain/ProjectFactory.h"
#include "features/CheckAssistant.h"
#include "features/CheckService.h"
#include "ui/IssuePanel.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>

#include <algorithm>
#include <variant>

using namespace lingya;

// 检查助手测试（X04，AI规范§7.4）：规则映射 + 侧栏列表与动作派发。
class TstCheckAssistant : public QObject {
    Q_OBJECT

private slots:
    void ruleTitleCoversAllRules();
    void actionForFixableRules();
    void structuralRulesAreLocateOnly();
    void unknownRuleFallsBackToEmpty();
    void panelListsIssuesWithCount();
    void panelEnablesActionOnlyForFixable();
    void panelDispatchesAction();
    void issueActionIdsAgreeWithAssistant();
};

namespace {

Issue makeIssue(const QString& ruleId, IssueSeverity severity = IssueSeverity::Warning) {
    Issue issue;
    issue.ruleId = ruleId;
    issue.severity = severity;
    issue.sheetId = QStringLiteral("sheet-1");
    issue.targetId = QStringLiteral("target-1");
    issue.message = QStringLiteral("测试消息");
    return issue;
}

}  // namespace

void TstCheckAssistant::ruleTitleCoversAllRules() {
    QCOMPARE(CheckAssistant::ruleTitle(QStringLiteral("C01")), QStringLiteral("未命名主题"));
    QCOMPARE(CheckAssistant::ruleTitle(QStringLiteral("C06")), QStringLiteral("任务日期"));
    QCOMPARE(CheckAssistant::ruleTitle(QStringLiteral("C09")), QStringLiteral("对比度"));
}

void TstCheckAssistant::actionForFixableRules() {
    QCOMPARE(CheckAssistant::actionFor(QStringLiteral("C01")).actionId,
             QStringLiteral("edit-node"));
    QCOMPARE(CheckAssistant::actionFor(QStringLiteral("C06")).actionId,
             QStringLiteral("edit-task"));
    QCOMPARE(CheckAssistant::actionFor(QStringLiteral("C07")).actionId,
             QStringLiteral("edit-task"));
    QCOMPARE(CheckAssistant::actionFor(QStringLiteral("C08")).actionId,
             QStringLiteral("fix-source"));
    QCOMPARE(CheckAssistant::actionFor(QStringLiteral("C09")).actionId,
             QStringLiteral("fix-contrast"));
}

void TstCheckAssistant::structuralRulesAreLocateOnly() {
    // 结构性错误只定位，不给自动修复
    QVERIFY(CheckAssistant::actionFor(QStringLiteral("C02")).actionId.isEmpty());
    QVERIFY(CheckAssistant::actionFor(QStringLiteral("C03")).actionId.isEmpty());
    QVERIFY(CheckAssistant::actionFor(QStringLiteral("C04")).actionId.isEmpty());
    QVERIFY(CheckAssistant::actionFor(QStringLiteral("C05")).actionId.isEmpty());
}

void TstCheckAssistant::unknownRuleFallsBackToEmpty() {
    QCOMPARE(CheckAssistant::ruleTitle(QStringLiteral("C99")), QStringLiteral("C99"));
    QVERIFY(CheckAssistant::actionFor(QStringLiteral("C99")).actionId.isEmpty());
}

void TstCheckAssistant::panelListsIssuesWithCount() {
    IssuePanel panel;
    panel.setIssues({makeIssue(QStringLiteral("C01")), makeIssue(QStringLiteral("C03"))});

    auto* list = panel.findChild<QListWidget*>(QStringLiteral("issueList"));
    auto* header = panel.findChild<QLabel*>(QStringLiteral("issueHeader"));
    QVERIFY(list != nullptr);
    QVERIFY(header != nullptr);
    QCOMPARE(list->count(), 2);
    QCOMPARE(header->text(), QStringLiteral("问题（2）"));
    QCOMPARE(panel.issues().size(), 2);
}

void TstCheckAssistant::panelEnablesActionOnlyForFixable() {
    IssuePanel panel;
    panel.setIssues({makeIssue(QStringLiteral("C01")), makeIssue(QStringLiteral("C03"))});

    auto* list = panel.findChild<QListWidget*>(QStringLiteral("issueList"));
    auto* action = panel.findChild<QPushButton*>(QStringLiteral("issueAction"));
    QVERIFY(list != nullptr);
    QVERIFY(action != nullptr);
    QVERIFY(!action->isEnabled());   // 未选中

    list->setCurrentRow(0);   // C01 可修复
    QVERIFY(action->isEnabled());
    QCOMPARE(action->text(), QStringLiteral("编辑主题"));

    list->setCurrentRow(1);   // C03 只能定位
    QVERIFY(!action->isEnabled());
    QCOMPARE(action->text(), QStringLiteral("仅定位"));
}

void TstCheckAssistant::panelDispatchesAction() {
    IssuePanel panel;
    panel.setIssues({makeIssue(QStringLiteral("C01"))});
    QSignalSpy spy(&panel, &IssuePanel::actionRequested);

    auto* list = panel.findChild<QListWidget*>(QStringLiteral("issueList"));
    auto* action = panel.findChild<QPushButton*>(QStringLiteral("issueAction"));
    list->setCurrentRow(0);
    action->click();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("sheet-1"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("target-1"));
    QCOMPARE(spy.first().at(2).toString(), QStringLiteral("edit-node"));
}

void TstCheckAssistant::issueActionIdsAgreeWithAssistant() {
    // CheckService 产生 Issue.actionId，CheckAssistant 另有一份 ruleId→action 映射。
    // 两处若不一致，侧栏按钮会把“编辑任务”派发成别的动作，故逐条交叉核对。
    Project project = ProjectFactory::createMindmap(QStringLiteral("检查"));
    auto& tree = std::get<TreeSheet>(project.sheets.first());
    tree.nodes[tree.rootId].title = QString();
    tree.defaultNodeStyle.color = QStringLiteral("#FFFFFF");
    tree.defaultNodeStyle.fill = QStringLiteral("#FFFFFF");   // 低对比度，触发 C09

    const QVector<Issue> issues = CheckService::checkProject(project);
    QVERIFY(!issues.isEmpty());
    QVERIFY(std::any_of(issues.cbegin(), issues.cend(), [](const Issue& issue) {
        return issue.ruleId == QStringLiteral("C01");
    }));
    for (const Issue& issue : issues) {
        if (issue.actionId.has_value()) {
            QCOMPARE(CheckAssistant::actionFor(issue.ruleId).actionId, *issue.actionId);
        }
    }
}

QTEST_MAIN(TstCheckAssistant)
#include "tst_checkassistant.moc"
