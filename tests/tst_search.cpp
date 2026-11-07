#include "domain/ProjectFactory.h"
#include "features/SearchService.h"
#include "services/ProjectJson.h"
#include <QtTest>
using namespace lingya;
class TstSearch : public QObject
{
    Q_OBJECT
  private slots:
    void findsCollapsedNotesAndTasks();
    void reportsLimitAndCaseFolding();
    void emptyIsReadOnly();
};
void TstSearch::findsCollapsedNotesAndTasks()
{
    auto project = ProjectFactory::createMindmap("检索");
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    tree.nodes[tree.rootId].collapsed = true;
    TreeNode child;
    child.id = "child";
    child.parentId = tree.rootId;
    child.title = "主题";
    child.note = "隐藏的中文检索关键字";
    tree.nodes.insert(child.id, child);
    Task task;
    task.id = "task";
    task.title = "检索关键字任务";
    project.tasks.append(task);
    const auto result = SearchService::find(project, "检索关键字");
    QCOMPARE(result.total, 2);
    QCOMPARE(result.matches.size(), 2);
    QCOMPARE(result.matches[0].targetId, child.id);
    QCOMPARE(result.matches[1].kind, "task");
    QVERIFY(result.matches[0].excerpt.contains("关键字"));
}
void TstSearch::reportsLimitAndCaseFolding()
{
    auto project = ProjectFactory::createFishbone("大纲");
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    for (auto &node : tree.nodes)
        node.title = "MiXeD";
    const auto result = SearchService::find(project, " mixed ", 2);
    QCOMPARE(result.total, tree.nodes.size());
    QCOMPARE(result.matches.size(), 2);
}
void TstSearch::emptyIsReadOnly()
{
    const auto project = ProjectFactory::createFishbone("只读");
    const auto json = ProjectJson::toJson(project);
    QCOMPARE(SearchService::find(project, " ").total, 0);
    SearchService::find(project, "骨");
    QCOMPARE(ProjectJson::toJson(project), json);
}
QTEST_GUILESS_MAIN(TstSearch)
#include "tst_search.moc"
