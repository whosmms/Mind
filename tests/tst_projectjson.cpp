#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QTimeZone>

#include "domain/Domain.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"
#include "services/FileService.h"
#include "services/ProjectJson.h"

using namespace lingya;

#ifndef FIXTURES_DIR
#define FIXTURES_DIR "fixtures"
#endif

// 序列化与文件服务测试（规范§4、§8、§12 M0）：
// M0纵向闭环（根节点→改名→保存→关闭→重开）、全类型往返幂等、严格拒绝场景。
class TstProjectJson : public QObject {
    Q_OBJECT

private slots:
    void m0VerticalSlice();
    void roundTripAllTypes();
    void loadsMinimalFixture();
    void rejectsUnknownFieldFixture();
    void loadsLongChineseFixture();
    void rejectsFutureVersion();
    void rejectsWrongType();
    void rejectsBadDate();
    void rejectsBadLink();
    void rejectsGarbage();
    void fileServiceSaveLoadRoundTrip();
    void fileServiceChineseAndSpacesPath();
    void fileServiceCreatesBackup();
    void fileServiceRejectsOversize();

private:
    static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
    static QString fixture(const QString& name) { return QDir(FIXTURES_DIR).filePath(name); }
    static ID addChild(TreeSheet& sheet, const ID& parentId, const QString& title) {
        TreeNode node;
        node.id = uuid();
        node.title = title;
        node.parentId = parentId;
        node.order = sheet.nodes.size();
        sheet.nodes.insert(node.id, node);
        return node.id;
    }
    // 覆盖全部领域类型的工程：三类工作表、任务、卡片、路线、文稿（规范§4.2）
    static Project richProject() {
        Project p = ProjectFactory::createMindmap(QStringLiteral("往返测试"));
        TreeSheet& tree = std::get<TreeSheet>(p.sheets[0]);
        const ID c1 = addChild(tree, tree.rootId, QStringLiteral("分支一"));
        const ID c2 = addChild(tree, tree.rootId, QStringLiteral("分支二"));
        tree.nodes[c1].note = QStringLiteral("备注内容");
        tree.nodes[c1].tags << QStringLiteral("重点");
        tree.nodes[c1].style.fill = "#DDF2E7";
        tree.nodes[c1].style.bold = true;
        tree.nodes[c1].link = "https://example.com/";
        tree.nodes[c1].provenance = Provenance{};
        tree.nodes[c1].provenance->requestId = uuid();
        tree.nodes[c1].provenance->model = "deepseek-flash";
        tree.nodes[c1].provenance->sourceParagraphIds << "p1";

        Relation relation;
        relation.id = uuid();
        relation.sourceId = c1;
        relation.targetId = c2;
        relation.label = QStringLiteral("关联");
        relation.style.color = "#247A62";   // 颜色必须为合法#RRGGBB（规范§4.3.10）
        relation.style.dashed = true;
        tree.relations.push_back(relation);

        GroupAnnotation annotation;
        annotation.id = uuid();
        annotation.parentId = tree.rootId;
        annotation.kind = "summary";
        annotation.memberIds << c1 << c2;
        annotation.text = QStringLiteral("两个分支的归纳");
        annotation.color = "#EEE8FA";
        tree.annotations.push_back(annotation);

        Comment comment;
        comment.id = uuid();
        comment.nodeId = c1;
        comment.author = QStringLiteral("甲");
        comment.body = QStringLiteral("这里需要补充例子");
        comment.createdAt = QDateTime(QDate(2026, 10, 24), QTime(9, 30, 0, 123), QTimeZone::utc());
        comment.updatedAt = comment.createdAt;
        tree.comments.push_back(comment);

        FlowSheet flow;
        flow.id = uuid();
        flow.name = QStringLiteral("流程");
        // 新建工作表的默认样式须填合法颜色（真实应用由工厂负责，测试同样遵守契约）
        flow.defaultNodeStyle.fill = "#FFFFFF";
        flow.defaultNodeStyle.color = "#243A34";
        flow.defaultNodeStyle.borderColor = "#DDE6DF";
        flow.defaultEdgeStyle.color = "#247A62";
        FlowNode start, decide;
        start.id = uuid();
        start.title = QStringLiteral("开始");
        start.role = "start";
        decide.id = uuid();
        decide.title = QStringLiteral("是否通过");
        decide.role = "decision";
        decide.position = QPointF(40, 80);
        flow.nodes.insert(start.id, start);
        flow.nodes.insert(decide.id, decide);
        FlowEdge edge;
        edge.id = uuid();
        edge.sourceId = start.id;
        edge.targetId = decide.id;
        edge.label = QStringLiteral("是");
        edge.style.color = "#247A62";   // 颜色必须为合法#RRGGBB（规范§4.3.10）
        edge.sourcePort = "right";
        edge.targetPort = "left";
        flow.edges.push_back(edge);
        // QVector扩容会使p.sheets[0]的旧引用失效，先保存ID副本再push_back
        const ID treeId = tree.id;
        const ID rootId = tree.rootId;
        p.sheets.push_back(flow);

        GanttSheet gantt;
        gantt.id = uuid();
        gantt.name = QStringLiteral("计划");
        gantt.defaultNodeStyle.fill = "#FFFFFF";
        gantt.defaultNodeStyle.color = "#243A34";
        gantt.defaultNodeStyle.borderColor = "#DDE6DF";
        gantt.defaultEdgeStyle.color = "#247A62";
        gantt.filter.sourceSheetId = treeId;
        gantt.scale = "month";
        p.sheets.push_back(gantt);

        Task first;
        first.id = uuid();
        first.title = QStringLiteral("任务一");
        first.source = TaskSource{treeId, c1};
        first.startDate = QDate(2024, 2, 29);   // 闰日
        first.endDate = QDate(2024, 3, 5);
        first.progress = 40;
        first.owner = QStringLiteral("乙");
        p.tasks.push_back(first);

        Task second;
        second.id = uuid();
        second.title = QStringLiteral("任务二（里程碑）");
        second.startDate = QDate(2024, 3, 6);
        second.endDate = QDate(2024, 3, 6);
        second.milestone = true;
        second.predecessorIds << first.id;
        p.tasks.push_back(second);

        ReviewCard card;
        card.id = uuid();
        card.sheetId = treeId;
        card.nodeId = c2;
        card.front = QStringLiteral("分支二的作用");
        card.back = QStringLiteral("用于验证关系线");
        card.status = "review";
        card.sourceHash = "abc123";
        card.lastReviewedAt = QDateTime(QDate(2026, 10, 24), QTime(8, 0, 0, 1), QTimeZone::utc());
        p.cards.push_back(card);

        PresentationRoute route;
        route.id = uuid();
        route.sheetId = treeId;
        route.title = QStringLiteral("讲解路线");
        PresentationStep step1, step2;
        step1.id = uuid();
        step1.nodeId = rootId;
        step1.speakerNote = QStringLiteral("先讲整体");
        step2.id = uuid();
        step2.nodeId = c1;
        route.steps.push_back(step1);
        route.steps.push_back(step2);
        p.routes.push_back(route);

        TextDraft draft;
        draft.id = uuid();
        draft.sourceSheetId = treeId;
        draft.title = QStringLiteral("摘要草稿");
        draft.sourceRevision = 3;
        draft.purpose = "explanation";
        draft.createdAt = QDateTime(QDate(2026, 10, 24), QTime(10, 0, 0, 0), QTimeZone::utc());
        DraftSection s1, s2;
        s1.heading = QStringLiteral("概述");
        s1.text = QStringLiteral("来自导图的内容");
        s1.sourceNodeIds << rootId;
        s2.heading = QStringLiteral("补充");
        s2.text = QStringLiteral("补充说明");
        s2.origin = "supplement";
        draft.sections.push_back(s1);
        draft.sections.push_back(s2);
        p.drafts.push_back(draft);
        return p;
    }
};

void TstProjectJson::m0VerticalSlice() {
    // 规范§12 M0最短流程：一个根节点→改名→保存→关闭→重开
    Project p = ProjectFactory::createMindmap(QStringLiteral("纵向验证"));
    {
        TreeSheet& tree = std::get<TreeSheet>(p.sheets[0]);
        tree.nodes[tree.rootId].title = QStringLiteral("改名后的根节点");
        ++tree.nodes[tree.rootId].contentRevision;
        ++p.revision;
        p.updatedAt = QDateTime::currentDateTimeUtc();
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("slice.lingya"));

    const auto saved = FileService::save(path, p);
    QVERIFY(std::holds_alternative<qint64>(saved));
    QCOMPARE(std::get<qint64>(saved), p.revision);

    // “关闭”：不再引用原对象，从磁盘重新加载
    const auto reopened = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(reopened));
    const Project q = std::get<Project>(reopened);
    const TreeSheet& sheet = std::get<TreeSheet>(q.sheets[0]);
    QCOMPARE(sheet.nodes.value(sheet.rootId).title, QStringLiteral("改名后的根节点"));
    QCOMPARE(sheet.nodes.value(sheet.rootId).contentRevision, qint64(2));
    QCOMPARE(q.revision, p.revision);
    QVERIFY(ProjectValidator::validate(q).isEmpty());   // 重开一致且合法（R01验收口径）
}

void TstProjectJson::roundTripAllTypes() {
    const Project p = richProject();
    QVERIFY(ProjectValidator::validate(p).isEmpty());

    const QByteArray first = ProjectJson::toBytes(p);
    const auto parsed = ProjectJson::fromBytes(first);
    QVERIFY(std::holds_alternative<Project>(parsed));
    const Project back = std::get<Project>(parsed);
    QVERIFY(ProjectValidator::validate(back).isEmpty());

    // 幂等：再序列化必须逐字节一致
    const QByteArray second = ProjectJson::toBytes(back);
    QCOMPARE(first, second);

    // 关键字段抽查：variant分派、平铺继承、日期、枚举
    QCOMPARE(back.sheets.size(), 3);
    const TreeSheet& tree = std::get<TreeSheet>(back.sheets[0]);
    QCOMPARE(tree.kind, QStringLiteral("mindmap"));
    QCOMPARE(tree.nodes.size(), 3);
    QVERIFY(tree.nodes.value(tree.rootId).parentId == std::nullopt);
    const FlowSheet& flow = std::get<FlowSheet>(back.sheets[1]);
    QCOMPARE(flow.kind, QStringLiteral("flowchart"));
    // QMap按key排序，first()依赖UUID字典序不可靠；按role定位判断节点
    QPointF decidePos;
    for (auto it = flow.nodes.constBegin(); it != flow.nodes.constEnd(); ++it)
        if (it.value().role == "decision") decidePos = it.value().position;
    QCOMPARE(decidePos, QPointF(40, 80));
    const GanttSheet& gantt = std::get<GanttSheet>(back.sheets[2]);
    QCOMPARE(gantt.filter.sourceSheetId, std::optional<QString>(tree.id));
    QCOMPARE(gantt.scale, QStringLiteral("month"));
    QCOMPARE(back.tasks.first().startDate, std::optional<QDate>(QDate(2024, 2, 29)));
    QCOMPARE(back.tasks.at(1).milestone, true);
    QCOMPARE(back.cards.first().status, QStringLiteral("review"));
    QCOMPARE(back.routes.first().steps.size(), 2);
    QCOMPARE(back.drafts.first().sections.at(1).origin, QStringLiteral("supplement"));
}

void TstProjectJson::loadsMinimalFixture() {
    const auto result = FileService::load(fixture("minimal.lingya"));
    QVERIFY(std::holds_alternative<Project>(result));
    const Project p = std::get<Project>(result);
    QCOMPARE(p.title, QStringLiteral("最小样例"));
    QCOMPARE(p.revision, qint64(5));
    QVERIFY(ProjectValidator::validate(p).isEmpty());
}

void TstProjectJson::rejectsUnknownFieldFixture() {
    // 每个已定义对象拒绝未知字段（规范§4.2）
    const auto result = FileService::load(fixture("bad-unknown-field.lingya"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QVERIFY(std::get<AppError>(result).message.contains(QStringLiteral("未知字段")));
}

void TstProjectJson::loadsLongChineseFixture() {
    // 长中文、多行备注、标签（规范§3 fixtures“长中文”样本）
    const auto result = FileService::load(fixture("long-chinese.lingya"));
    QVERIFY(std::holds_alternative<Project>(result));
    const Project p = std::get<Project>(result);
    const TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    QCOMPARE(sheet.nodes.size(), 2);
    const QString title = sheet.nodes.value(sheet.rootId).title;
    QCOMPARE(title.size(), 180);
    QVERIFY(sheet.nodes.value(sheet.rootId).note.contains(QLatin1Char('\n')));
    int tagged = 0;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it)
        if (!it.value().tags.isEmpty()) tagged = it.value().tags.size();
    QCOMPARE(tagged, 2);
    QVERIFY(ProjectValidator::validate(p).isEmpty());
}

void TstProjectJson::rejectsFutureVersion() {
    // 未知更高版本拒绝（原设计§8），错误码按协作规范§10.3为UNSUPPORTED_SCHEMA
    Project p = ProjectFactory::createMindmap(QStringLiteral("未来版本"));
    QJsonObject j = ProjectJson::toJson(p);
    j["schemaVersion"] = 2;
    const auto result = ProjectJson::fromObject(j);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("UNSUPPORTED_SCHEMA"));
}

void TstProjectJson::rejectsWrongType() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("类型错误"));
    QJsonObject j = ProjectJson::toJson(p);
    j["title"] = 123;   // 标题必须是字符串，不能用默认值掩盖（规范§4.2）
    const auto result = ProjectJson::fromObject(j);
    QVERIFY(std::holds_alternative<AppError>(result));
    QVERIFY(std::get<AppError>(result).message.contains(QStringLiteral("title")));
}

void TstProjectJson::rejectsBadDate() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("非法日期"));
    QJsonObject j = ProjectJson::toJson(p);
    QJsonObject task;
    task["id"] = uuid();
    task["title"] = QStringLiteral("坏日期");
    task["source"] = QJsonValue(QJsonValue::Null);
    task["startDate"] = "2026-02-30";   // 2月30日不存在，格式化比对必须失败
    task["endDate"] = "2026-03-01";
    task["progress"] = 0;
    task["owner"] = QString();
    task["milestone"] = false;
    task["predecessorIds"] = QJsonArray();
    j["tasks"] = QJsonArray{task};
    const auto result = ProjectJson::fromObject(j);
    QVERIFY(std::holds_alternative<AppError>(result));
}

void TstProjectJson::rejectsBadLink() {
    // 链接只允许http/https，禁止javascript/file/data协议（规范§4.3.10）
    Project p = ProjectFactory::createMindmap(QStringLiteral("非法链接"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].link = "javascript:alert(1)";
    const auto result = ProjectJson::fromBytes(ProjectJson::toBytes(p));
    QVERIFY(std::holds_alternative<AppError>(result));
    QVERIFY(std::get<AppError>(result).message.contains(QStringLiteral("http")));
}

void TstProjectJson::rejectsGarbage() {
    const auto result = ProjectJson::fromBytes(QByteArray("{\"schemaVersion\": 1,"));
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("INVALID_PROJECT"));
}

void TstProjectJson::fileServiceSaveLoadRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.lingya"));
    const Project p = richProject();
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, p)));
    const auto result = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(result));
    QCOMPARE(ProjectJson::toBytes(std::get<Project>(result)), ProjectJson::toBytes(p));
    // 保存后的文件不应再产生备份（首次保存无旧文件）
    QVERIFY(!QFile::exists(path + ".bak"));
}

void TstProjectJson::fileServiceChineseAndSpacesPath() {
    // 路径中文和含空格必须测试（规范§8）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir base(dir.path());
    QVERIFY(base.mkpath(QStringLiteral("中文 目录/子 目录")));
    const QString path = base.filePath(QStringLiteral("中文 目录/子 目录/样例 文件.lingya"));
    const Project p = ProjectFactory::createMindmap(QStringLiteral("中文路径工程"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, p)));
    const auto result = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(result));
    QCOMPARE(std::get<Project>(result).title, QStringLiteral("中文路径工程"));
}

void TstProjectJson::fileServiceCreatesBackup() {
    // 旧文件存在时先创建独立.bak副本（规范§8）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("backup.lingya"));
    Project p = ProjectFactory::createMindmap(QStringLiteral("第一版"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, p)));

    QFile firstFile(path);
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    const QByteArray first = firstFile.readAll();
    firstFile.close();

    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].title = QStringLiteral("第二版");
    ++p.revision;
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, p)));

    // 备份在专用子目录"灵芽备份"（2026-10-24统一规则，不再在原目录留.bak）
    QFile bak(path.section('/', 0, -2) + QStringLiteral("/灵芽备份/") +
              QFileInfo(path).fileName() + QStringLiteral(".bak"));
    QVERIFY(bak.exists());
    QVERIFY(bak.open(QIODevice::ReadOnly));
    QCOMPARE(bak.readAll(), first);   // 备份保留上一版内容
}

void TstProjectJson::fileServiceRejectsOversize() {
    // 超10MiB拒绝加载（规范§4.3.9、§8）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("oversize.lingya"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray filler(qsizetype(Limits::kMaxFileBytes + 1), ' ');
    QCOMPARE(file.write(filler), filler.size());
    file.close();
    const auto result = FileService::load(path);
    QVERIFY(std::holds_alternative<AppError>(result));
    QCOMPARE(std::get<AppError>(result).code, QStringLiteral("LIMIT_EXCEEDED"));
}

QTEST_MAIN(TstProjectJson)
#include "tst_projectjson.moc"
