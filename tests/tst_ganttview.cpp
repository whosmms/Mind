#include <QtTest>

#include "charts/GanttLayout.h"
#include "charts/GanttView.h"
#include "domain/Domain.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTextDocument>

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
  void showsTaskTitlesTodayAndWeekends();
  void rowForTaskMatchesTableOrder();
  void unscheduledListShowsTasks();
  void dragFilledBarMovesWholeBar();
  void dragStartEdgeResizesStart();
  void resizingPreviewsDatesWithoutEditing_data();
  void resizingPreviewsDatesWithoutEditing();
  void rebuildDuringDragDropsState();
  void shiftTaskDatesMove();
  void shiftTaskDatesResizeClamps();
  void shiftTaskDatesInvalidInput();
  void freeRouteXAvoidsBlockingBars();
  void shortCrossMonthShowsSingleMonthLabel();
  void monthBoundaryLineDoesNotCrossLabel();
  void monthBoundaryLineVisibleInAxisBand();
  void monthLineTopFollowsMonthFont();
  void longRangeShowsBothMonthLabels();
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

QGraphicsItem* barItemFor(GanttView& view, const ID& taskId) {
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->data(GanttView::kBarDataRole).toString() == taskId) {
            return item;
        }
    }
    return nullptr;
}

// 月份标签按几何位置识别：它在时间轴最上一行（y≈0）；日号在 kAxisHeight-16，
// 未排期说明在任务区下方。不再用 "yyyy-MM" 的字符形状去猜（§52.3 遗留）。
QVector<QGraphicsTextItem*> monthLabelItems(GanttView& view) {
    QVector<QGraphicsTextItem*> labels;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->type() != QGraphicsTextItem::Type) {
            continue;
        }
        auto* text = static_cast<QGraphicsTextItem*>(item);
        if (text->pos().y() < 8.0) {
            labels.append(text);
        }
    }
    return labels;
}

// 月界主线：主色竖线。日号分隔线是 borderColor，依赖连线不竖直，都不参与。
QVector<QGraphicsLineItem*> monthLineItems(GanttView& view) {
    QVector<QGraphicsLineItem*> lines;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->type() != QGraphicsLineItem::Type) {
            continue;
        }
        auto* line = static_cast<QGraphicsLineItem*>(item);
        const QLineF segment = line->line();
        if (!qFuzzyCompare(segment.x1() + 1.0, segment.x2() + 1.0)) {
            continue;
        }
        if (line->pen().color() != QColor(0x24, 0x7A, 0x62)) {
            continue;
        }
        lines.append(line);
    }
    return lines;
}

// 场景坐标 → 视口坐标（QTest 的鼠标事件按视口坐标投递）
QPoint viewportPointFor(GanttView& view, const QPointF& scenePoint) {
    return view.viewport()->mapFrom(&view, view.mapFromScene(scenePoint));
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

    // 日期占 120px，末尾任务名也必须在可滚动范围内；行高不随标题改变。
    const QRectF rect = view.scene()->sceneRect();
    QVERIFY(rect.width() > 120.0);
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
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 2), false, 50)});
    // 任务条本体 + 进度填充 = 2 个矩形（刻度是线与文字，不计）
    QCOMPARE(barItemCount(view), 1);
    QCOMPARE(typeCount(view, QGraphicsRectItem::Type), 2);

    GanttView zero;
    zero.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 1, 1), QDate(2026, 1, 2), false, 0)});
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

void TstGanttView::showsTaskTitlesTodayAndWeekends()
{
    GanttView view;
    const auto today = QDate::currentDate();
    const auto title = QStringLiteral("检查任务名称");
    view.setTasks({makeTask(title, today.addDays(-4), today.addDays(4))});
    const auto items = view.scene()->items();
    const auto label = std::find_if(
        items.cbegin(), items.cend(),
        [&](QGraphicsItem *item)
        {
            const auto *text = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item);
            return text && text->text() == title && text->parentItem() == barItemFor(view, title);
        });
    QVERIFY(label != items.cend());
    QVERIFY(view.sceneRect().contains((*label)->sceneBoundingRect()));
    QVERIFY(std::any_of(items.cbegin(), items.cend(),
                        [](QGraphicsItem *item)
                        {
                            return item->type() == QGraphicsLineItem::Type &&
                                   item->toolTip() == QStringLiteral("今天");
                        }));
    QVERIFY(
        std::any_of(items.cbegin(), items.cend(), [](QGraphicsItem *item)
                    { return item->type() == QGraphicsRectItem::Type && item->zValue() == -2; }));
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

void TstGanttView::dragFilledBarMovesWholeBar() {
    // 进度100%时整条都被填充覆盖：命中点最上层是填充矩形，不能因此判定“没点到任务条”
    GanttView view;
    view.resize(1000, 400);
    view.setTasks(
        {makeTask(QStringLiteral("已完成"), QDate(2026, 1, 5), QDate(2026, 1, 9), false, 100)});
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    GanttLayout layout(view.pixelsPerDay());
    const auto result = layout.compute(view.tasks());
    const int startX = layout.xForDate(QDate(2026, 1, 5), result.rangeStart);
    const int endX = layout.xForDate(QDate(2026, 1, 9), result.rangeStart) + view.pixelsPerDay();
    const QPointF sceneCenter((startX + endX) / 2.0, 34 + 4 + 10);

    QSignalSpy spy(&view, &GanttView::taskDatesEdited);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      viewportPointFor(view, sceneCenter));
    // 平移预览：任务条与作为子图元的进度填充一起移动
    const int shiftPixels = 3 * view.pixelsPerDay();
    QTest::mouseMove(view.viewport(),
                     viewportPointFor(view, sceneCenter + QPointF(shiftPixels, 0)));
    QGraphicsItem* bar = barItemFor(view, QStringLiteral("已完成"));
    QVERIFY(bar != nullptr);
    QCOMPARE(bar->pos().x(), qreal(shiftPixels));
    QVERIFY(!bar->childItems().isEmpty());   // 填充挂在条下，随条移动

    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        viewportPointFor(view, sceneCenter + QPointF(shiftPixels, 0)));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("已完成"));
    QCOMPARE(spy.at(0).at(1).toDate(), QDate(2026, 1, 8));
    QCOMPARE(spy.at(0).at(2).toDate(), QDate(2026, 1, 12));
    QCOMPARE(barItemFor(view, QStringLiteral("已完成"))->pos().x(), qreal(0));   // 松开后回到模型状态
}

void TstGanttView::dragStartEdgeResizesStart() {
    GanttView view;
    view.resize(1000, 400);
    view.setTasks(
        {makeTask(QStringLiteral("任务"), QDate(2026, 1, 5), QDate(2026, 1, 9), false, 40)});
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    GanttLayout layout(view.pixelsPerDay());
    const auto result = layout.compute(view.tasks());
    const int startX = layout.xForDate(QDate(2026, 1, 5), result.rangeStart);
    const QPointF sceneEdge(startX + 4, 34 + 4 + 10);   // 起点侧 8px 内

    QSignalSpy spy(&view, &GanttView::taskDatesEdited);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      viewportPointFor(view, sceneEdge));
    QTest::mouseMove(view.viewport(),
                     viewportPointFor(view, sceneEdge - QPointF(2 * view.pixelsPerDay(), 0)));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        viewportPointFor(view, sceneEdge - QPointF(2 * view.pixelsPerDay(), 0)));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toDate(), QDate(2026, 1, 3));   // 只改起点
    QCOMPARE(spy.at(0).at(2).toDate(), QDate(2026, 1, 9));   // 终点不动
}

void TstGanttView::resizingPreviewsDatesWithoutEditing_data()
{
    QTest::addColumn<bool>("startEdge");
    QTest::addColumn<int>("deltaDays");
    QTest::addColumn<QDate>("expectedStart");
    QTest::addColumn<QDate>("expectedEnd");
    QTest::newRow("延长开始") << true << -2 << QDate(2026, 1, 3) << QDate(2026, 1, 9);
    QTest::newRow("结束夹到开始") << false << -5 << QDate(2026, 1, 5) << QDate(2026, 1, 5);
}

void TstGanttView::resizingPreviewsDatesWithoutEditing()
{
    QFETCH(bool, startEdge);
    QFETCH(int, deltaDays);
    QFETCH(QDate, expectedStart);
    QFETCH(QDate, expectedEnd);
    const Task task =
        makeTask(QStringLiteral("拖动中只预览"), QDate(2026, 1, 5), QDate(2026, 1, 9));
    GanttView view;
    view.resize(1000, 400);
    view.setTasks({task});
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    GanttLayout layout(view.pixelsPerDay());
    const auto result = layout.compute(view.tasks());
    const int edgeX = startEdge ? layout.xForDate(*task.startDate, result.rangeStart) + 4
                                : layout.xForDate(task.endDate->addDays(1), result.rangeStart) - 4;
    const QPointF sceneEdge(edgeX, 34 + 4 + 10);
    const QPointF destination = sceneEdge + QPointF(deltaDays * view.pixelsPerDay(), 0);
    QSignalSpy spy(&view, &GanttView::taskDatesEdited);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      viewportPointFor(view, sceneEdge));
    QTest::mouseMove(view.viewport(), viewportPointFor(view, destination));
    QGraphicsRectItem *preview = nullptr;
    for (auto *item : view.scene()->items())
        if (item->data(GanttView::kPreviewDataRole).isValid())
            if (auto *rect = dynamic_cast<QGraphicsRectItem *>(item))
                preview = rect;
    QVERIFY(preview);
    QCOMPARE(preview->rect().x(), layout.xForDate(expectedStart, result.rangeStart));
    QCOMPARE(preview->rect().width(),
             (expectedStart.daysTo(expectedEnd) + 1) * view.pixelsPerDay());
    QVERIFY(!preview->childItems().isEmpty());
    auto *dates = dynamic_cast<QGraphicsSimpleTextItem *>(preview->childItems().first());
    QVERIFY(dates);
    QVERIFY(dates->text().contains(expectedStart.toString("yyyy-MM-dd")));
    QVERIFY(dates->text().contains(expectedEnd.toString("yyyy-MM-dd")));
    QCOMPARE(view.tasks().first().startDate, task.startDate);
    QCOMPARE(view.tasks().first().endDate, task.endDate);
    QCOMPARE(spy.count(), 0);
    const QString capture = qEnvironmentVariable("LINGYA_GANTT_RESIZE_CAPTURE");
    if (startEdge && !capture.isEmpty())
        QVERIFY(view.grab().save(capture));
    view.setTasks({task});
    for (auto *item : view.scene()->items())
        QVERIFY(!item->data(GanttView::kPreviewDataRole).isValid());
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        viewportPointFor(view, destination));
    QCOMPARE(spy.count(), 0);
}

void TstGanttView::rebuildDuringDragDropsState() {
    // 拖拽期间数据被外部刷新（撤销/另存/表格编辑）会销毁图元，不能留下野指针
    GanttView view;
    view.resize(1000, 400);
    const Task task =
        makeTask(QStringLiteral("任务"), QDate(2026, 1, 5), QDate(2026, 1, 9), false, 60);
    view.setTasks({task});
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    GanttLayout layout(view.pixelsPerDay());
    const auto result = layout.compute(view.tasks());
    const QPointF sceneCenter(
        layout.xForDate(QDate(2026, 1, 7), result.rangeStart), 34 + 4 + 10);
    QSignalSpy spy(&view, &GanttView::taskDatesEdited);

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      viewportPointFor(view, sceneCenter));
    view.setTasks({task});   // 重建：拖拽状态必须被清掉
    QTest::mouseMove(view.viewport(),
                     viewportPointFor(view, sceneCenter + QPointF(4 * view.pixelsPerDay(), 0)));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        viewportPointFor(view, sceneCenter + QPointF(4 * view.pixelsPerDay(), 0)));
    QCOMPARE(spy.count(), 0);   // 重建已取消本次拖拽，不应提交
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

void TstGanttView::freeRouteXAvoidsBlockingBars() {
    QVector<GanttBar> bars;
    bars.append(GanttBar{ID(QStringLiteral("A")), 0, 0, 50, false, true});
    bars.append(GanttBar{ID(QStringLiteral("B")), 1, 0, 100, false, true});
    bars.append(GanttBar{ID(QStringLiteral("C")), 2, 0, 50, false, true});

    // 25 被中间行 B(0..100) 挡住，向右挪到 105
    QCOMPARE(freeRouteX(bars, 25, 0, 2, 10), 105);
    // 本来就在外面，直接返回
    QCOMPARE(freeRouteX(bars, 150, 0, 2, 10), 150);
    // 只看 row2：C 只有 0..50
    QCOMPARE(freeRouteX(bars, 60, 2, 2, 10), 60);
    // 只看 row0：A 是 0..50，25 被挡 → 55
    QCOMPARE(freeRouteX(bars, 25, 0, 0, 10), 55);
}

void TstGanttView::shortCrossMonthShowsSingleMonthLabel()
{
    // OBS-A-20261031-001：10-31..11-02 只有 3 天，两个 yyyy-MM 只隔一天会重叠
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 10, 31), QDate(2026, 11, 2))});

    QCOMPARE(monthLabelItems(view).size(), 1); // 第二个月份文字被避让，月界仍保留。
    for (int pixelsPerDay : {24, 48})
    {
        view.setPixelsPerDay(pixelsPerDay);
        for (QGraphicsItem *item : view.scene()->items())
        {
            auto *label = dynamic_cast<QGraphicsTextItem *>(item);
            if (!label || label->pos().y() != 0 ||
                label->toPlainText() != QStringLiteral("2026-10"))
                continue;
            const qreal margin = label->document()->documentMargin();
            const QRectF text = label->mapRectToScene(
                label->boundingRect().adjusted(margin, margin, -margin, -margin));
            for (QGraphicsItem *candidate : view.scene()->items())
            {
                auto *grid = dynamic_cast<QGraphicsLineItem *>(candidate);
                if (!grid)
                    continue;
                const QPointF from = grid->mapToScene(grid->line().p1());
                const QPointF to = grid->mapToScene(grid->line().p2());
                const bool cutsText = qFuzzyCompare(from.x(), to.x()) && from.x() > text.left() &&
                                      from.x() < text.right() &&
                                      qMin(from.y(), to.y()) < text.bottom() &&
                                      qMax(from.y(), to.y()) > text.top();
                QVERIFY2(!cutsText, "月界竖线不应穿过月份标题");
            }
        }
    }
}

void TstGanttView::longRangeShowsBothMonthLabels()
{
    // 区间足够宽时三个标签不该被误删
    GanttView view;
    view.setPixelsPerDay(60);
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 10, 1), QDate(2026, 12, 1))});

    QCOMPARE(monthLabelItems(view).size(), 3); // 10、11、12 三个月界
}

void TstGanttView::monthBoundaryLineDoesNotCrossLabel() {
    // P3 52.3：几何断言替代「长度 7 + 1 连字符」启发式——不许任何竖线落进标签文字矩形
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 10, 31), QDate(2026, 11, 2))});

    const auto labels = monthLabelItems(view);
    QVERIFY2(!labels.isEmpty(), "没有找到月份标签");
    auto* monthLabel = labels.first();
    const qreal margin = monthLabel->document()->documentMargin();
    const QRectF labelRect =
        monthLabel->sceneBoundingRect().adjusted(margin, margin, -margin, -margin);
    QVERIFY2(labelRect.isValid(), "月份标签矩形无效");

    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->type() != QGraphicsLineItem::Type) {
            continue;
        }
        auto* line = static_cast<QGraphicsLineItem*>(item);
        const QLineF segment = line->line();
        if (!qFuzzyCompare(segment.x1() + 1.0, segment.x2() + 1.0)) {
            continue;   // 只看竖线
        }
        const qreal x = segment.x1();
        const qreal top = qMin(segment.y1(), segment.y2());
        const qreal bottom = qMax(segment.y1(), segment.y2());
        const bool xInside = x >= labelRect.left() && x <= labelRect.right();
        const bool yOverlap = bottom >= labelRect.top() && top <= labelRect.bottom();
        QVERIFY2(!(xInside && yOverlap), "月界竖线穿过了月份标签文字");
    }
}

void TstGanttView::monthBoundaryLineVisibleInAxisBand() {
    // P3 52.2：竖线从标签下沿起画，轴带内（kAxisHeight=34 以上）仍能看到月份分界
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 10, 31), QDate(2026, 11, 2))});

    int monthLines = 0;
    int monthLinesVisibleInAxisBand = 0;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (item->type() != QGraphicsLineItem::Type) {
            continue;
        }
        auto* line = static_cast<QGraphicsLineItem*>(item);
        const QLineF segment = line->line();
        if (!qFuzzyCompare(segment.x1() + 1.0, segment.x2() + 1.0)) {
            continue;   // 只看竖线
        }
        // 只查月界主线（主色 #247A62）；日号浅色分隔线本来就从 kAxisHeight-4 起，会干扰判断
        if (line->pen().color() != QColor(0x24, 0x7A, 0x62)) {
            continue;
        }
        ++monthLines;
        if (qMin(segment.y1(), segment.y2()) < 34.0) {   // GanttView 的 kAxisHeight
            ++monthLinesVisibleInAxisBand;
        }
    }
    QVERIFY2(monthLines >= 2, "至少应有 10-31 与 11-01 两条月界主线");
    QVERIFY2(monthLinesVisibleInAxisBand >= 1, "月界主线应在轴带内可见");
}

void TstGanttView::monthLineTopFollowsMonthFont()
{
    // §54 乙反证：起点写死 22 时，月份字号调到 12pt 就穿字。竖线起点必须由字体推导。
    // 本用例锁死「线跟着字走」：线顶 == monthLineTop(标签字体)，字号变大时数值也变大。
    GanttView view;
    view.setTasks({makeTask(QStringLiteral("A"), QDate(2026, 10, 31), QDate(2026, 11, 2))});

    const auto labels = monthLabelItems(view);
    QVERIFY2(!labels.isEmpty(), "没有找到月份标签");
    auto* label = labels.first();
    const qreal margin = label->document()->documentMargin();
    const QRectF textRect =
        label->sceneBoundingRect().adjusted(margin, margin, -margin, -margin);

    const auto lines = monthLineItems(view);
    QVERIFY2(!lines.isEmpty(), "没有找到月界主线");
    const QLineF segment = lines.first()->line();
    const qreal lineTop = qMin(segment.y1(), segment.y2());

    QVERIFY2(lineTop > textRect.bottom(), "月界竖线压到了月份标签文字");
    QVERIFY2(lineTop <= textRect.bottom() + 8.0, "月界竖线离标签太远，轴带内会看着断开");
    QVERIFY2(qAbs(lineTop - GanttView::monthLineTop(label->font())) < 0.5,
             "月界竖线起点没有跟着月份标签字体走");

    QFont bigger = label->font();
    bigger.setPointSize(bigger.pointSize() + 4);
    QVERIFY2(GanttView::monthLineTop(bigger) > lineTop + 1.0,
             "字号变大时月界竖线起点没有下移（写死像素就会这样）");
}

QTEST_MAIN(TstGanttView)
#include "tst_ganttview.moc"
