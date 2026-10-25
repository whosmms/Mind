#include <QtTest>

#include "charts/GanttLayout.h"
#include "charts/GanttView.h"
#include "domain/Domain.h"

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>

using namespace lingya;

// 甘特视图测试（R13，AI规范§6.4）：刻度、任务条/进度、里程碑、依赖线、未排期与行同步。
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
    void progressFillsBar();
    void dependencyDrawsArrow();
    void timeAxisDrawsTicks();
    void rowForTaskMatchesTableOrder();
    void unscheduledListShowsTasks();
};

namespace {

Task makeTask(const QString& title, const QDate& start, const QDate& end,
              bool milestone = false, int progress = 0) {
    Task task;
    task.id = title;
    task.title = title;
    task.startDate = start;
    task.endDate = end;
    task.milestone = milestone;
    task.progress = progress;
    return task;
}

int barItemCount(GanttView& view) {
    int count = 0;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->data(GanttView::kBarDataRole).isValid()) {
            ++count;
        }
    }
    return count;
}

int typeCount(GanttView& view, int type) {
    int count = 0;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->type() == type) {
            ++count;
        }
    }
    return count;
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
    QCOMPARE(barItemCount(view), 2);
}

void TstGanttView::milestoneDrawsOneItem() {
    GanttView view;
    view.setTasks(
        {makeTask(QStringLiteral("M"), QDate(2026, 1, 1), QDate(2026, 1, 1), true)});
    QCOMPARE(barItemCount(view), 1);
}

void TstGanttView::skipsTaskWithInvalidDates() {
    GanttView view;
    // optional 内是无效 QDate：不参与范围、不画条，但进未排期列表
    view.setTasks({makeTask(QStringLiteral("X"), QDate(), QDate())});
    QCOMPARE(barItemCount(view), 0);
    QCOMPARE(typeCount(view, QGraphicsTextItem::Type), 1);
}

void TstGanttView::sceneRectTracksRowsAndRange() {
    GanttView view;
    view.setPixelsPerDay(10);
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 3)),
                   makeTask(QStringLiteral("B"), QDate(2026, 1, 10), QDate(2026, 1, 12))});

    // 日期范围 1/1..1/12 = 12 天 × 10px = 120；高度 = 时间轴 34 + 2 行 × 28 = 90
    const QRectF rect = view.scene()->sceneRect();
    QCOMPARE(rect.width(), 120.0);
    QCOMPARE(rect.height(), 90.0);
    QCOMPARE(barItemCount(view), 2);
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

void TstGanttView::progressFillsBar() {
    GanttView view;
    view.setTasks(
        {makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 3), false, 50)});
    // 任务条本体 + 进度填充 = 2 个矩形（刻度是线与文字，不计）
    QCOMPARE(barItemCount(view), 1);
    QCOMPARE(typeCount(view, QGraphicsRectItem::Type), 2);

    GanttView zero;
    zero.setTasks(
        {makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 3), false, 0)});
    QCOMPARE(typeCount(zero, QGraphicsRectItem::Type), 1);   // 进度为 0 不画填充
}

void TstGanttView::dependencyDrawsArrow() {
    Task pred = makeTask(QStringLiteral("P"), QDate(2026, 1, 1), QDate(2026, 1, 2));
    Task succ = makeTask(QStringLiteral("S"), QDate(2026, 1, 4), QDate(2026, 1, 5));
    succ.predecessorIds = {pred.id};

    GanttView view;
    view.setTasks({pred, succ});
    QCOMPARE(barItemCount(view), 2);
    QCOMPARE(typeCount(view, QGraphicsPathItem::Type), 1);   // 依赖折线
}

void TstGanttView::timeAxisDrawsTicks() {
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 3))});
    // 除任务条外，刻度会额外产生文字（月份/日号）
    QVERIFY(typeCount(view, QGraphicsTextItem::Type) > 0);
}

void TstGanttView::rowForTaskMatchesTableOrder() {
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 2)),
                   makeTask(QStringLiteral("B"), QDate(2026, 1, 3), QDate(2026, 1, 4))});
    QCOMPARE(view.rowForTask(QStringLiteral("A")), 0);
    QCOMPARE(view.rowForTask(QStringLiteral("B")), 1);
    QCOMPARE(view.rowForTask(QStringLiteral("missing")), -1);
}

void TstGanttView::unscheduledListShowsTasks() {
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("未排期任务"), QDate(), QDate())});
    QCOMPARE(barItemCount(view), 0);
    bool found = false;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->type() == QGraphicsTextItem::Type) {
            const auto* text = static_cast<QGraphicsTextItem*>(item);
            if (text->toPlainText().contains(QStringLiteral("未排期"))) {
                found = true;
            }
        }
    }
    QVERIFY(found);
}

QTEST_MAIN(TstGanttView)
#include "tst_ganttview.moc"
