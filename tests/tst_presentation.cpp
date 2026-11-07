#include "commands/CommandService.h"
#include "commands/PresentationCommands.h"
#include "document/ProjectDocument.h"
#include "domain/ProjectFactory.h"
#include "features/PresentationController.h"
#include "services/FileService.h"
#include "services/ProjectJson.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace lingya;
class TstPresentation : public QObject
{
    Q_OBJECT
  private slots:
    void defaultOrderAndMissingSteps();
    void routeTransactionAndRoundTrip();
    void rejectsInvalidRoute();
    void navigationAndPauseAreReadOnly();
};
void TstPresentation::defaultOrderAndMissingSteps()
{
    auto project = ProjectFactory::createMindmap("讲解");
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    for (const auto &id : QStringList{"b", "a"})
    {
        TreeNode node;
        node.id = id;
        node.title = id;
        node.parentId = tree.rootId;
        node.order = 0;
        tree.nodes.insert(id, node);
    }
    auto route = PresentationController::defaultRoute(tree);
    QCOMPARE(route.steps.size(), 3);
    QCOMPARE(route.steps[1].nodeId, "a");
    QCOMPARE(route.steps[2].nodeId, "b");
    route.steps.prepend({"missing", "deleted", "遗留"});
    PresentationController controller;
    QVERIFY(std::holds_alternative<int>(controller.start(tree, route)));
    QCOMPARE(controller.skipped(), 1);
    QCOMPARE(controller.current().nodeId, tree.rootId);
    route.steps = {{"missing", "deleted", {}}};
    QVERIFY(std::holds_alternative<AppError>(controller.start(tree, route)));
    QCOMPARE(controller.count(), 0);
    route.sheetId = "missing";
    QVERIFY(std::holds_alternative<AppError>(controller.start(tree, route)));
}
void TstPresentation::routeTransactionAndRoundTrip()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("中文讲解"));
    CommandService commands(&doc);
    PresentationCommands routes(&commands, &doc);
    auto route =
        PresentationController::defaultRoute(std::get<TreeSheet>(doc.project().sheets.first()));
    route.steps[0].speakerNote = "观众可见备注";
    const auto initial = ProjectJson::toJson(doc.project());
    QSignalSpy changed(&doc, &ProjectDocument::changed);
    QVERIFY(std::holds_alternative<bool>(routes.saveRoute(route)));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(doc.project().routes.size(), 1);
    const auto saved = ProjectJson::toJson(doc.project());
    QTemporaryDir dir;
    const QString path = dir.filePath("讲解 路线.lingya");
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, doc.project())));
    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(std::get<Project>(loaded).routes.first().steps.first().speakerNote,
             route.steps.first().speakerNote);
    commands.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), initial);
    commands.redo();
    QCOMPARE(ProjectJson::toJson(doc.project()), saved);
    QVERIFY(std::holds_alternative<bool>(routes.removeRoute(route.id)));
    QVERIFY(doc.project().routes.isEmpty());
    commands.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), saved);
    changed.clear();
    QVERIFY(std::holds_alternative<bool>(routes.saveRoute(route)));
    QCOMPARE(changed.count(), 0);
}
void TstPresentation::rejectsInvalidRoute()
{
    ProjectDocument doc;
    doc.replaceProject(ProjectFactory::createMindmap("讲解"));
    CommandService commands(&doc);
    PresentationCommands routes(&commands, &doc);
    auto route =
        PresentationController::defaultRoute(std::get<TreeSheet>(doc.project().sheets.first()));
    const auto initial = ProjectJson::toJson(doc.project());
    route.title = " ";
    QVERIFY(std::holds_alternative<AppError>(routes.saveRoute(route)));
    route.title = "合法";
    route.steps.append(route.steps.first());
    QVERIFY(std::holds_alternative<AppError>(routes.saveRoute(route)));
    route.steps.removeLast();
    route.steps[0].speakerNote = QString(20001, QChar(0x4E2D));
    QVERIFY(std::holds_alternative<AppError>(routes.saveRoute(route)));
    QVERIFY(std::holds_alternative<AppError>(routes.removeRoute("absent")));
    QCOMPARE(ProjectJson::toJson(doc.project()), initial);
    QVERIFY(!commands.canUndo());
}
void TstPresentation::navigationAndPauseAreReadOnly()
{
    const auto project = ProjectFactory::createFishbone("鱼骨讲解");
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    const auto json = ProjectJson::toJson(project);
    PresentationController controller;
    QVERIFY(std::holds_alternative<int>(
        controller.start(tree, PresentationController::defaultRoute(tree))));
    QVERIFY(!controller.previous());
    QVERIFY(controller.next());
    QVERIFY(controller.previous());
    QTest::qWait(25);
    controller.setPaused(true);
    const auto time = controller.elapsedMilliseconds();
    QTest::qWait(30);
    QCOMPARE(controller.elapsedMilliseconds(), time);
    controller.setPaused(false);
    QTest::qWait(25);
    QVERIFY(controller.elapsedMilliseconds() >= time + 15);
    QCOMPARE(ProjectJson::toJson(project), json);
}
QTEST_MAIN(TstPresentation)
#include "tst_presentation.moc"
