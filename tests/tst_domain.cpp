#include <QtTest>

#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"

using namespace lingya;

// 领域层测试：上限常量、工厂合法性、校验器不变量（规范§4.3规则1/2/5/7/8）
class TstDomain : public QObject {
    Q_OBJECT

private slots:
    void limitsMatchSpec();
    void factoryProducesValidProject();
    void rejectsTwoRoots();
    void rejectsOrphanParent();
    void rejectsCycle();
    void rejectsDuplicateIdsAcrossSheets();
    void rejectsEmptyOrOverlongTitle();
    void rejectsBadLayoutKind();
    void rejectsTaskDateOrder();
    void rejectsSingleDateOnly();
    void rejectsMilestoneSpanningDays();
    void rejectsTaskDependencyCycle();
    void rejectsTaskSourceMissing();

private:
    static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
    static ID addChild(TreeSheet& sheet, const ID& parentId, const QString& title) {
        TreeNode node;
        node.id = uuid();
        node.title = title;
        node.parentId = parentId;
        int order = 0;
        for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
            if (it.value().parentId == parentId) ++order;
        node.order = order;
        sheet.nodes.insert(node.id, node);
        return node.id;
    }
    static bool hasCode(const QVector<AppError>& errors, const char* code) {
        for (const AppError& e : errors)
            if (e.code == QLatin1String(code)) return true;
        return false;
    }
};

void TstDomain::limitsMatchSpec() {
    // 规范§4.3、§5.1、§5.2的关键上限，防止后续阶段被悄悄改动
    QCOMPARE(Limits::kSchemaVersion, 1);
    QCOMPARE(Limits::kMaxNodes, 1000);
    QCOMPARE(Limits::kMaxSheets, 20);
    QCOMPARE(Limits::kMaxFileBytes, qint64(10) * 1024 * 1024);
    QCOMPARE(Limits::kTitleMaxChars, 2000);
    QCOMPARE(Limits::kNoteMaxChars, 20000);
    QCOMPARE(Limits::kMaxTags, 20);
    QCOMPARE(Limits::kTagMaxChars, 30);
    QCOMPARE(Limits::kCommentMaxChars, 2000);
    QCOMPARE(Limits::kFontSizeMin, 12);
    QCOMPARE(Limits::kFontSizeMax, 32);
    QCOMPARE(Limits::kHistoryMaxSteps, 50);
    QCOMPARE(Limits::kZoomMin, 0.25);
    QCOMPARE(Limits::kZoomMax, 2.0);
    QCOMPARE(Limits::kAiMapMaxNodes, 50);
    QCOMPARE(Limits::kAiMapMaxDepth, 4);
}

void TstDomain::factoryProducesValidProject() {
    const Project p = ProjectFactory::createMindmap(QStringLiteral("工厂测试"));
    QVERIFY(ProjectValidator::validate(p).isEmpty());
    QCOMPARE(p.schemaVersion, 1);
    QCOMPARE(p.sheets.size(), 1);
    const TreeSheet& sheet = std::get<TreeSheet>(p.sheets.first());
    QCOMPARE(sheet.kind, QStringLiteral("mindmap"));
    QCOMPARE(sheet.layout, QStringLiteral("bilateral"));
    QCOMPARE(sheet.themeId, QStringLiteral("mint"));
    QCOMPARE(sheet.nodes.size(), 1);
    QCOMPARE(sheet.nodes.value(sheet.rootId).title, QStringLiteral("中心主题"));
    QCOMPARE(p.activeSheetId, sheet.id);
}

void TstDomain::rejectsTwoRoots() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("多根"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    TreeNode extra;
    extra.id = uuid();
    extra.title = QStringLiteral("第二个根");
    extra.parentId = std::nullopt;   // 一图只能有一个根（规则1）
    sheet.nodes.insert(extra.id, extra);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TREE_MULTI_ROOT"));
}

void TstDomain::rejectsOrphanParent() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("孤儿"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    TreeNode orphan;
    orphan.id = uuid();
    orphan.title = QStringLiteral("孤儿节点");
    orphan.parentId = uuid();   // 父级不存在
    sheet.nodes.insert(orphan.id, orphan);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TREE_PARENT_MISSING"));
}

void TstDomain::rejectsCycle() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("环"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    const ID a = addChild(sheet, sheet.rootId, QStringLiteral("甲"));
    const ID b = addChild(sheet, a, QStringLiteral("乙"));
    sheet.nodes[a].parentId = b;   // 甲→乙→甲成环（规则1：无环）
    QVERIFY(hasCode(ProjectValidator::validate(p), "TREE_CYCLE"));
}

void TstDomain::rejectsDuplicateIdsAcrossSheets() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("重复ID"));
    const TreeSheet& first = std::get<TreeSheet>(p.sheets[0]);
    TreeSheet second;
    second.id = uuid();
    second.name = QStringLiteral("工作表 2");
    second.rootId = first.rootId;   // 与第一张表共用节点ID（规则1：工程内ID唯一）
    TreeNode rootCopy;
    rootCopy.id = second.rootId;
    rootCopy.title = QStringLiteral("重复的根");
    second.nodes.insert(rootCopy.id, rootCopy);
    p.sheets.push_back(second);
    QVERIFY(hasCode(ProjectValidator::validate(p), "DUPLICATE_ID"));
}

void TstDomain::rejectsEmptyOrOverlongTitle() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("标题长度"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].title = QString();
    QVERIFY(hasCode(ProjectValidator::validate(p), "TITLE_LENGTH"));
    sheet.nodes[sheet.rootId].title = QString("长").repeated(2001);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TITLE_LENGTH"));
    sheet.nodes[sheet.rootId].title = QString("合").repeated(2000);
    QVERIFY(ProjectValidator::validate(p).isEmpty());
}

void TstDomain::rejectsBadLayoutKind() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("布局配对"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.layout = "fishbone";   // mindmap只允许bilateral/right（规则2）
    QVERIFY(hasCode(ProjectValidator::validate(p), "LAYOUT_KIND"));
    sheet.layout = "right";
    QVERIFY(ProjectValidator::validate(p).isEmpty());
}

void TstDomain::rejectsTaskDateOrder() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("日期顺序"));
    Task task;
    task.id = uuid();
    task.title = QStringLiteral("排期颠倒");
    task.startDate = QDate(2026, 10, 30);
    task.endDate = QDate(2026, 10, 26);
    p.tasks.push_back(task);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TASK_DATE"));
}

void TstDomain::rejectsSingleDateOnly() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("单边日期"));
    Task task;
    task.id = uuid();
    task.title = QStringLiteral("只有开始");
    task.startDate = QDate(2026, 10, 26);
    p.tasks.push_back(task);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TASK_DATE"));
}

void TstDomain::rejectsMilestoneSpanningDays() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("里程碑"));
    Task task;
    task.id = uuid();
    task.title = QStringLiteral("跨日里程碑");
    task.startDate = QDate(2026, 10, 26);
    task.endDate = QDate(2026, 10, 27);
    task.milestone = true;
    p.tasks.push_back(task);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TASK_MILESTONE"));
}

void TstDomain::rejectsTaskDependencyCycle() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("依赖环"));
    Task a, b;
    a.id = uuid();
    a.title = QStringLiteral("任务甲");
    b.id = uuid();
    b.title = QStringLiteral("任务乙");
    a.predecessorIds << b.id;
    b.predecessorIds << a.id;   // 互相依赖成环（规则5）
    p.tasks.push_back(a);
    p.tasks.push_back(b);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TASK_DEPENDENCY_CYCLE"));
}

void TstDomain::rejectsTaskSourceMissing() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("任务来源"));
    const TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    Task task;
    task.id = uuid();
    task.title = QStringLiteral("关联不存在节点");
    task.source = TaskSource{sheet.id, uuid()};
    p.tasks.push_back(task);
    QVERIFY(hasCode(ProjectValidator::validate(p), "TASK_SOURCE"));
}

QTEST_MAIN(TstDomain)
#include "tst_domain.moc"
