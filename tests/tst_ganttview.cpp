#include <QtTest>

#include "charts/GanttLayout.h"
#include "charts/GanttView.h"
#include "domain/Domain.h"

using namespace lingya;

// 甘特视图测试（R13，AI规范§6.4）：任务条/里程碑绘制、几何、空场景与无效日期。
class TstGanttView : public QObject {
    Q_OBJECT

private slots:
    void emptySceneHasNoItems();
    void drawsBarsForDatedTasks();
    void milestoneDrawsOneItem();
    void skipsTaskWithInvalidDates();
    void sceneRectTracksRowsAndRange();
    void dateForXHandlesNegativeX();
    void setPixelsPerDayFallsBackToDefault();
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

void TstGanttView::skipsTaskWithInvalidDates() {
    GanttView view;
    // optional 内是无效 QDate：不参与范围、不画条
    view.setTasks({makeTask(QStringLiteral("X"), QDate(), QDate())});
    QCOMPARE(view.scene()->items().size(), 0);
}

void TstGanttView::sceneRectTracksRowsAndRange() {
    GanttView view;
    view.setPixelsPerDay(10);
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 3)),
                   makeTask(QStringLiteral("B"), QDate(2026, 1, 10), QDate(2026, 1, 12))});

    // 行数 2 × 行高 28 = 56；日期范围 1/1..1/12 = 12 天 × 10px = 120
    const QRectF rect = view.scene()->sceneRect();
    QCOMPARE(rect.width(), 120.0);
    QCOMPARE(rect.height(), 56.0);
    QCOMPARE(view.scene()->items().size(), 2);
    QCOMPARE(view.pixelsPerDay(), 10);
}

void TstGanttView::dateForXHandlesNegativeX() {
    GanttLayout layout(24);
    const QDate start(2026, 1, 1);
    QCOMPARE(layout.dateForX(-1, start), QDate(2025, 12, 31));   // 不足一天向左回退一天
    QCOMPARE(layout.dateForX(-24, start), QDate(2025, 12, 31));
    QCOMPARE(layout.dateForX(-25, start), QDate(2025, 12, 30));
}

void TstGanttView::setPixelsPerDayFallsBackToDefault() {
    GanttView view;
    view.setPixelsPerDay(0);   // ≤0 回退默认值
    QCOMPARE(view.pixelsPerDay(), GanttLayout::kDefaultPixelsPerDay);
}

QTEST_MAIN(TstGanttView)
#include "tst_ganttview.moc"
