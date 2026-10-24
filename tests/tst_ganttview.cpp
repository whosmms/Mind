#include <QtTest>

#include "charts/GanttView.h"
#include "domain/Domain.h"

using namespace lingya;

// 甘特视图测试（R13，AI规范§9.2）：任务条/里程碑绘制与空场景。
class TstGanttView : public QObject {
    Q_OBJECT

private slots:
    void emptySceneHasNoItems();
    void drawsBarsForDatedTasks();
    void milestoneDrawsOneItem();
};

namespace {

Task makeTask(const QString& title, const QDate& start, const QDate& end, bool milestone = false) {
    Task task;
    task.id = title;
    task.title = title;
    task.startDate = start;
    task.endDate = end;
    task.milestone = milestone;
    return task;
}

}  // namespace

void TstGanttView::emptySceneHasNoItems() {
    GanttView view;
    view.setTasks({});
    QVERIFY(view.scene() != nullptr);
    QCOMPARE(view.scene()->items().size(), 0);
}

void TstGanttView::drawsBarsForDatedTasks() {
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 3)),
                   makeTask(QStringLiteral("B"), QDate(2026, 1, 2), QDate(2026, 1, 4))});
    QCOMPARE(view.scene()->items().size(), 2);
}

void TstGanttView::milestoneDrawsOneItem() {
    GanttView view;
    view.setTasks(
        {makeTask(QStringLiteral("M"), QDate(2026, 1, 1), QDate(2026, 1, 1), true)});
    QCOMPARE(view.scene()->items().size(), 1);
}

QTEST_MAIN(TstGanttView)
#include "tst_ganttview.moc"
