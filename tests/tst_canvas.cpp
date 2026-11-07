#include "commands/CommandService.h"
#include "commands/FlowCommands.h"
#include "commands/ImageCommands.h"
#include "commands/NodeCommands.h"
#include "document/ProjectDocument.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"
#include "layout/TreeLayout.h"
#include "services/ImageService.h"
#include "services/ProjectJson.h"
#include "services/TemplateLibrary.h"
#include <QBuffer>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace lingya;
class TstCanvas : public QObject
{
    Q_OBJECT
  private slots:
    void imageImportAndStrictRoundTrip();
    void allTemplates_data();
    void allTemplates();
    void flowPlusIsOneTransaction();
    void positionsAreAtomicAndPersisted();
    void manualDescendantsAndReset();
    void rejectsBadGeometry_data();
    void rejectsBadGeometry();
    void reconnectKeepsIdentityAndUndo();
    void optionalGeometryIsStrict_data();
    void optionalGeometryIsStrict();
    void copiedNodesUseTargetLayout();
};
void TstCanvas::positionsAreAtomicAndPersisted()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("位置"));
    CommandService history(&doc);
    NodeCommands commands(&history, &doc);
    auto sheet = std::get<TreeSheet>(doc.project().sheets.first());
    const ID root = sheet.rootId;
    const auto added = commands.addChild(sheet.id, root, "点击位置", QPointF(123.25, -66.5));
    QVERIFY(std::holds_alternative<ID>(added));
    const ID child = std::get<ID>(added);
    history.clearHistory();
    const auto before = ProjectJson::toJson(doc.project());
    const auto rev = doc.sessionRevision();
    QVERIFY(std::holds_alternative<bool>(
        commands.setPositions(sheet.id, {{root, {10, 20}}, {child, {240, 160}}})));
    QCOMPARE(doc.sessionRevision(), rev + 1);
    QVERIFY(!ProjectJson::toJson(doc.project()).isEmpty());
    auto loaded = ProjectJson::fromObject(ProjectJson::toJson(doc.project()));
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(std::get<TreeSheet>(std::get<Project>(loaded).sheets.first())
                 .nodes.value(child)
                 .manualPosition.value(),
             QPointF(240, 160));
    commands.setPositions(sheet.id, {{child, {240, 160}}});
    QCOMPARE(doc.sessionRevision(), rev + 1);
    const auto good = ProjectJson::toJson(doc.project());
    QVERIFY(std::holds_alternative<AppError>(
        commands.setPositions(sheet.id, {{child, {88, 90}}, {"missing", {1, 2}}})));
    QCOMPARE(ProjectJson::toJson(doc.project()), good);
    history.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    history.redo();
    QCOMPARE(ProjectJson::toJson(doc.project()), good);
}
void TstCanvas::manualDescendantsAndReset()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("继承"));
    CommandService history(&doc);
    NodeCommands commands(&history, &doc);
    const auto original = std::get<TreeSheet>(doc.project().sheets.first());
    const auto root = original.rootId;
    const ID child = std::get<ID>(commands.addChild(original.id, root, "子"));
    const ID leaf = std::get<ID>(commands.addChild(original.id, child, "孙"));
    commands.setPositions(original.id, {{child, {450, 100}}});
    auto tree = std::get<TreeSheet>(doc.project().sheets.first());
    QVector<TreeLayout::NodePosition> base{{root, 0, 0}, {child, 200, 20}, {leaf, 400, 40}};
    const auto positions = TreeLayout::withManualPositions(tree, base);
    QCOMPARE(QPointF(positions[2].x, positions[2].y), QPointF(650, 120));
    history.clearHistory();
    const auto before = ProjectJson::toJson(doc.project());
    commands.resetPositions(tree.id);
    QVERIFY(!std::get<TreeSheet>(doc.project().sheets.first()).nodes[child].manualPosition);
    history.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
}
void TstCanvas::rejectsBadGeometry_data()
{
    QTest::addColumn<double>("x");
    QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("infinite") << std::numeric_limits<double>::infinity();
    QTest::newRow("range") << 100001.0;
}
void TstCanvas::rejectsBadGeometry()
{
    QFETCH(double, x);
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("拒绝非法位置"));
    CommandService history(&doc);
    NodeCommands commands(&history, &doc);
    const auto tree = std::get<TreeSheet>(doc.project().sheets.first());
    const auto before = ProjectJson::toJson(doc.project());
    QVERIFY(
        std::holds_alternative<AppError>(commands.setPositions(tree.id, {{tree.rootId, {x, 0}}})));
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    QVERIFY(!history.canUndo());
}
void TstCanvas::reconnectKeepsIdentityAndUndo()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createFlowchart("重接"));
    CommandService history(&doc);
    FlowCommands commands(&history, &doc);
    auto flow = std::get<FlowSheet>(doc.project().sheets.first());
    const ID first = flow.nodes.firstKey();
    const ID second = std::get<ID>(commands.addNode(flow.id, "process", "二", {300, 0})),
             third = std::get<ID>(commands.addNode(flow.id, "decision", "三", {300, 200}));
    const ID edge = std::get<ID>(commands.addEdge(flow.id, first, second, "分支", "right", "left"));
    history.clearHistory();
    const auto before = ProjectJson::toJson(doc.project());
    FlowEdgePatch patch;
    patch.targetId = third;
    patch.targetPort = "top";
    patch.bendPoint = QPointF(150, -90);
    QVERIFY(std::holds_alternative<bool>(commands.updateEdge(flow.id, edge, patch)));
    auto changed = std::get<FlowSheet>(doc.project().sheets.first()).edges.first();
    QCOMPARE(changed.id, edge);
    QCOMPARE(changed.label, QString("分支"));
    QCOMPARE(changed.targetId, third);
    QCOMPARE(changed.bendPoint.value(), QPointF(150, -90));
    const auto after = ProjectJson::toJson(doc.project());
    QVERIFY(std::holds_alternative<Project>(ProjectJson::fromObject(after)));
    history.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    history.redo();
    patch.targetId = "missing";
    QVERIFY(std::holds_alternative<AppError>(commands.updateEdge(flow.id, edge, patch)));
    QCOMPARE(ProjectJson::toJson(doc.project()), after);
    FlowEdgePatch reset;
    reset.resetBend = true;
    commands.updateEdge(flow.id, edge, reset);
    QVERIFY(!std::get<FlowSheet>(doc.project().sheets.first()).edges.first().bendPoint);
    history.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), after);
    const auto duplicate = commands.addEdge(flow.id, first, second, {}, "right", "left");
    QVERIFY(std::holds_alternative<ID>(duplicate));
    patch = {};
    patch.targetId = second;
    patch.targetPort = "left";
    const auto unchanged = ProjectJson::toJson(doc.project());
    QVERIFY(std::holds_alternative<AppError>(commands.updateEdge(flow.id, edge, patch)));
    QCOMPARE(ProjectJson::toJson(doc.project()), unchanged);
}
void TstCanvas::optionalGeometryIsStrict_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto *k : {"old", "valid", "unknown", "wrong-type", "null", "range"})
        QTest::newRow(k) << QString::fromLatin1(k);
}
void TstCanvas::optionalGeometryIsStrict()
{
    QFETCH(QString, kind);
    auto p = ProjectFactory::createMindmap("兼容");
    auto json = ProjectJson::toJson(p);
    auto sheets = json["sheets"].toArray();
    auto sheet = sheets[0].toObject();
    auto nodes = sheet["nodes"].toObject();
    const auto id = nodes.keys().first();
    auto node = nodes[id].toObject();
    if (kind != "old")
        node["manualPosition"] = QJsonObject{{"x", 1.25}, {"y", 20}};
    if (kind == "unknown")
    {
        auto pos = node["manualPosition"].toObject();
        pos["extra"] = 1;
        node["manualPosition"] = pos;
    }
    if (kind == "wrong-type")
        node["manualPosition"] = "bad";
    if (kind == "null")
        node["manualPosition"] = QJsonValue::Null;
    if (kind == "range")
        node["manualPosition"] = QJsonObject{{"x", 100001}, {"y", 20}};
    nodes[id] = node;
    sheet["nodes"] = nodes;
    sheets[0] = sheet;
    json["sheets"] = sheets;
    const auto result = ProjectJson::fromObject(json);
    QCOMPARE(std::holds_alternative<Project>(result), kind == "old" || kind == "valid");
}
void TstCanvas::copiedNodesUseTargetLayout()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("复制"));
    CommandService history(&doc);
    NodeCommands commands(&history, &doc);
    const auto tree = std::get<TreeSheet>(doc.project().sheets.first());
    const ID child = std::get<ID>(commands.addChild(tree.id, tree.rootId, "原", QPointF(400, 50)));
    const auto fragment = commands.copyFragment(tree.id, {child});
    QVERIFY(std::holds_alternative<Fragment>(fragment));
    const auto pasted = commands.pasteFragment(tree.id, tree.rootId, std::get<Fragment>(fragment));
    QVERIFY(std::holds_alternative<ID>(pasted));
    QVERIFY(!std::get<TreeSheet>(doc.project().sheets.first())
                 .nodes.value(std::get<ID>(pasted))
                 .manualPosition);
}
void TstCanvas::imageImportAndStrictRoundTrip()
{
    QTemporaryDir files;
    QImage image(200, 90, QImage::Format_ARGB32);
    image.fill(QColor("#247A62"));
    const auto path = files.filePath("本地原图.png");
    QVERIFY(image.save(path));
    const auto imported = ImageService::importFile(path);
    QVERIFY(std::holds_alternative<QString>(imported));
    const auto png = std::get<QString>(imported);
    QVERIFY(!ImageService::decode(png).isNull());
    QVERIFY(ImageService::decode("https://example.com/picture.png").isNull());
    QVERIFY(ImageService::decode(QString(500000, 'x')).isNull());
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("图片"));
    CommandService history(&doc);
    ImageCommands commands(&history, &doc);
    const auto tree = std::get<TreeSheet>(doc.project().sheets.first());
    const auto rev = doc.sessionRevision();
    QVERIFY(std::holds_alternative<bool>(commands.setNodeImage(tree.id, tree.rootId, png)));
    QCOMPARE(doc.sessionRevision(), rev + 1);
    const auto valid = ProjectJson::toJson(doc.project());
    QVERIFY(std::holds_alternative<Project>(ProjectJson::fromObject(valid)));
    const auto added = commands.add(tree.id, png, {88, 99});
    QVERIFY(std::holds_alternative<ID>(added));
    const ID id = std::get<ID>(added);
    QVERIFY(std::holds_alternative<Project>(
        ProjectJson::fromObject(ProjectJson::toJson(doc.project()))));
    const auto before = ProjectJson::toJson(doc.project());
    QVERIFY(std::holds_alternative<AppError>(
        commands.move(tree.id, id, {std::numeric_limits<double>::infinity(), 0})));
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    QVERIFY(std::holds_alternative<AppError>(commands.setNodeImage(tree.id, tree.rootId, "bad")));
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    commands.remove(tree.id, id);
    QVERIFY(std::get<TreeSheet>(doc.project().sheets.first()).images.isEmpty());
    history.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    QVERIFY(QFile::exists(path));
    auto invalid = before;
    auto sheets = invalid["sheets"].toArray();
    auto sheet = sheets[0].toObject();
    auto pictures = sheet["images"].toArray();
    auto picture = pictures[0].toObject();
    picture["hiddenSecret"] = "bad";
    pictures[0] = picture;
    sheet["images"] = pictures;
    sheets[0] = sheet;
    invalid["sheets"] = sheets;
    QVERIFY(std::holds_alternative<AppError>(ProjectJson::fromObject(invalid)));
}
void TstCanvas::allTemplates_data()
{
    QTest::addColumn<QString>("id");
    const auto all = TemplateLibrary::entries();
    QCOMPARE(all.size(), 24);
    for (const auto &entry : all)
        QTest::newRow(entry.id.toLatin1().constData()) << entry.id;
}
void TstCanvas::allTemplates()
{
    QFETCH(QString, id);
    const auto result = TemplateLibrary::create(id);
    QVERIFY(std::holds_alternative<Project>(result));
    const auto project = std::get<Project>(result);
    const auto errors = ProjectValidator::validate(project);
    QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().message));
    const auto json = ProjectJson::toJson(project);
    const auto read = ProjectJson::fromObject(json);
    QVERIFY(std::holds_alternative<Project>(read));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(read)), json);
    if (const auto *tree = std::get_if<TreeSheet>(&project.sheets.first());
        tree && tree->kind == "mindmap")
    {
        QVector<TreeLayout::NodeSize> sizes;
        for (const auto &node : tree->nodes)
            sizes.append({node.id, 120, 40});
        const auto layout = TreeLayout::layout(*tree, sizes);
        QVERIFY(std::holds_alternative<QVector<TreeLayout::NodePosition>>(layout));
        const auto points = std::get<QVector<TreeLayout::NodePosition>>(layout);
        for (int a = 0; a < points.size(); ++a)
            for (int b = 0; b < a; ++b)
                QVERIFY(!QRectF(points[a].x - 60, points[a].y - 20, 120, 40)
                             .intersects(QRectF(points[b].x - 60, points[b].y - 20, 120, 40)));
    }
}
void TstCanvas::flowPlusIsOneTransaction()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createFlowchart("加号"));
    CommandService history(&doc);
    FlowCommands commands(&history, &doc);
    const auto flow = std::get<FlowSheet>(doc.project().sheets.first());
    const auto before = ProjectJson::toJson(doc.project());
    const auto rev = doc.sessionRevision();
    const auto added = commands.addSuccessor(flow.id, flow.nodes.firstKey(), {250, 0});
    QVERIFY(std::holds_alternative<ID>(added));
    QCOMPARE(doc.sessionRevision(), rev + 1);
    const auto after = std::get<FlowSheet>(doc.project().sheets.first());
    QCOMPARE(after.nodes.size(), 2);
    QCOMPARE(after.edges.size(), 1);
    QCOMPARE(after.edges.first().targetId, std::get<ID>(added));
    history.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), before);
    QVERIFY(!history.canUndo());
}

QTEST_GUILESS_MAIN(TstCanvas)
#include "tst_canvas.moc"
