#include <QtTest>

#include "charts/GanttLayout.h"
#include "charts/GanttView.h"
#include "domain/Domain.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QScrollBar>

using namespace lingya;

// 甘特视图测试（R13，AI规范§6.4）：刻度、任务条/进度、里程碑、依赖线、未排期与行同步。
class TstGanttView : public QObject {
    Q_OBJECT

private slots:
  void initTestCase();
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
  void shiftTaskDatesMove();
  void shiftTaskDatesResizeClamps();
  void shiftTaskDatesInvalidInput();
  void midMonthRangeShowsYearAndMonth();
  void longUnscheduledNamesRemainScrollable_data();
  void longUnscheduledNamesRemainScrollable();
  void milestoneDependenciesReachMarkers_data();
  void milestoneDependenciesReachMarkers();
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

void TstGanttView::initTestCase()
{
    // 与既有tst_ui一致：offscreen临时加载本机字体，避免无字体测量充当中文显示证据。
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
    {
        const QString path =
            QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
        if (QFile::exists(path))
        {
            QFontDatabase::addApplicationFont(path);
            QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
        }
    }
}

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

void TstGanttView::midMonthRangeShowsYearAndMonth()
{
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("月中任务"), QDate(2026, 10, 25), QDate(2026, 10, 27))});
    bool found = false;
    for (auto *item : view.scene()->items())
        if (auto *text = dynamic_cast<QGraphicsTextItem *>(item))
            found |= text->toPlainText() == QStringLiteral("2026-10");
    QVERIFY(found);
}

void TstGanttView::longUnscheduledNamesRemainScrollable_data()
{
    QTest::addColumn<bool>("mixed");
    QTest::newRow("only-unscheduled") << false;
    QTest::newRow("mixed") << true;
}

void TstGanttView::longUnscheduledNamesRemainScrollable()
{
    QFETCH(bool, mixed);
    QVector<Task> tasks;
    if (mixed)
        tasks.append(
            makeTask(QStringLiteral("有日期任务"), QDate(2026, 10, 25), QDate(2026, 10, 27)));
    Task pending;
    pending.id = QStringLiteral("pending");
    pending.title = QStringLiteral("待安排中文任务") + QString(100, QChar(0x4e2d));
    tasks.append(pending);
    GanttView view;
    view.resize(900, 300);
    view.setTasks(tasks);
    view.show();
    QCoreApplication::processEvents();
    bool found = false;
    for (auto *item : view.scene()->items())
        if (auto *text = dynamic_cast<QGraphicsTextItem *>(item))
            if (text->toPlainText().startsWith(QStringLiteral("未排期：")))
            {
                found = true;
                QVERIFY(view.sceneRect().contains(text->sceneBoundingRect()));
            }
    QVERIFY(found);
    QVERIFY(view.horizontalScrollBar()->maximum() > 0);
}

void TstGanttView::milestoneDependenciesReachMarkers_data()
{
    QTest::addColumn<int>("pixelsPerDay");
    QTest::newRow("default-day-width") << 24;
    QTest::newRow("wide-day") << 80;
}

void TstGanttView::milestoneDependenciesReachMarkers()
{
    QFETCH(int, pixelsPerDay);
    Task pred = makeTask(QStringLiteral("前置里程碑"), QDate(2026, 1, 1), QDate(2026, 1, 1), true);
    Task succ = makeTask(QStringLiteral("后继里程碑"), QDate(2026, 1, 3), QDate(2026, 1, 3), true);
    succ.predecessorIds = {pred.id};
    GanttView view;
    view.setPixelsPerDay(pixelsPerDay);
    view.setTasks({pred, succ});
    QGraphicsPolygonItem *predMarker = nullptr, *succMarker = nullptr;
    QGraphicsPathItem *dependency = nullptr;
    for (auto *item : view.scene()->items())
    {
        if (item->data(GanttView::kBarDataRole).toString() == pred.id)
            predMarker = dynamic_cast<QGraphicsPolygonItem *>(item);
        if (item->data(GanttView::kBarDataRole).toString() == succ.id)
            succMarker = dynamic_cast<QGraphicsPolygonItem *>(item);
        if (auto *path = dynamic_cast<QGraphicsPathItem *>(item))
            dependency = path;
    }
    QVERIFY(predMarker && succMarker && dependency);
    const auto path = dependency->path();
    const auto first = path.elementAt(0);
    const auto last = path.elementAt(path.elementCount() - 1);
    const QRectF predBounds = predMarker->polygon().boundingRect();
    const QRectF succBounds = succMarker->polygon().boundingRect();
    QCOMPARE(QPointF(first.x, first.y), QPointF(predBounds.right(), predBounds.center().y()));
    QCOMPARE(QPointF(last.x, last.y), QPointF(succBounds.left(), succBounds.center().y()));
}

void TstGanttView::shiftTaskDatesMove() {
    const GanttDateRange moved =
        shiftTaskDates(QDate(2026, 1, 10), QDate(2026, 1, 12), 3, GanttDragMode::Move);
    QCOMPARE(moved.start, QDate(2026, 1, 13));
    QCOMPARE(moved.end, QDate(2026, 1, 15));

    const GanttDateRange back =
        shiftTaskDates(QDate(2026, 1, 10), QDate(2026, 1, 12), -2, GanttDragMode::Move);
    QCOMPARE(back.start, QDate(2026, 1, 8));
    QCOMPARE(back.end, QDate(2026, 1, 10));
}

void TstGanttView::shiftTaskDatesResizeClamps() {
    // 改开始日越过结束日时夹到结束日
    const GanttDateRange start =
        shiftTaskDates(QDate(2026, 1, 10), QDate(2026, 1, 12), 5, GanttDragMode::ResizeStart);
    QCOMPARE(start.start, QDate(2026, 1, 12));
    QCOMPARE(start.end, QDate(2026, 1, 12));

    // 改结束日早于开始日时夹到开始日
    const GanttDateRange end =
        shiftTaskDates(QDate(2026, 1, 10), QDate(2026, 1, 12), -5, GanttDragMode::ResizeEnd);
    QCOMPARE(end.start, QDate(2026, 1, 10));
    QCOMPARE(end.end, QDate(2026, 1, 10));
}

void TstGanttView::shiftTaskDatesInvalidInput() {
    const GanttDateRange result = shiftTaskDates(QDate(), QDate(), 3, GanttDragMode::Move);
    QVERIFY(!result.start.isValid());
    QVERIFY(!result.end.isValid());
}

QTEST_MAIN(TstGanttView)
#include "tst_ganttview.moc"
