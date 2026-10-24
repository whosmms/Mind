#include <QtTest>
#include <QSignalSpy>
#include <QUuid>

#include "charts/GanttLayout.h"
#include "charts/TaskTableModel.h"
#include "domain/Domain.h"

using namespace lingya;

// 甘特任务表模型测试（AI规范§6.4、§9.2）：5 列、字段展示、日期/进度格式、编辑请求信号。
class TstCharts : public QObject {
    Q_OBJECT

private slots:
    void exposesFiveColumns();
    void displaysTaskFields();
    void formatsDatesAndProgress();
    void marksMilestone();
    void editRequestedCarriesTaskIdAndColumn();
    void computesBarForSingleTask();
    void handlesCrossMonthRange();
    void handlesCrossYearRange();
    void milestoneIsOneDay();
    void skipsTasksWithoutDates();
    void dateConversionsRoundByDay();
};

namespace {

Task makeTask(const QString& title, const QString& owner, QDate start, QDate end,
              int progress, bool milestone = false) {
    Task task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.title = title;
    task.owner = owner;
    task.startDate = start;
    task.endDate = end;
    task.progress = progress;
    task.milestone = milestone;
    return task;
}

}  // namespace

void TstCharts::exposesFiveColumns() {
    TaskTableModel model;
    QCOMPARE(model.columnCount(), 5);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("名称"));
    QCOMPARE(model.headerData(4, Qt::Horizontal).toString(), QStringLiteral("进度"));
}

void TstCharts::displaysTaskFields() {
    TaskTableModel model;
    model.setTasks({makeTask(QStringLiteral("写文档"), QStringLiteral("张三"),
                             QDate(2026, 1, 10), QDate(2026, 1, 15), 40)});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, TaskTableModel::TitleColumn)).toString(),
             QStringLiteral("写文档"));
    QCOMPARE(model.data(model.index(0, TaskTableModel::OwnerColumn)).toString(),
             QStringLiteral("张三"));
}

void TstCharts::formatsDatesAndProgress() {
    TaskTableModel model;
    model.setTasks({makeTask(QStringLiteral("做测试"), QString(),
                             QDate(2026, 2, 1), QDate(2026, 2, 5), 80)});
    QCOMPARE(model.data(model.index(0, TaskTableModel::StartColumn)).toString(),
             QStringLiteral("2026-02-01"));
    QCOMPARE(model.data(model.index(0, TaskTableModel::EndColumn)).toString(),
             QStringLiteral("2026-02-05"));
    QCOMPARE(model.data(model.index(0, TaskTableModel::ProgressColumn)).toString(),
             QStringLiteral("80%"));
}

void TstCharts::marksMilestone() {
    TaskTableModel model;
    model.setTasks({makeTask(QStringLiteral("发布"), QString(),
                             QDate(2026, 3, 1), QDate(2026, 3, 1), 100, true)});
    QVERIFY(model.data(model.index(0, TaskTableModel::TitleColumn)).toString().startsWith(
        QStringLiteral("◆")));
}

void TstCharts::editRequestedCarriesTaskIdAndColumn() {
    TaskTableModel model;
    const Task task = makeTask(QStringLiteral("任务"), QString(), QDate(), QDate(), 0);
    model.setTasks({task});

    QSignalSpy spy(&model, &TaskTableModel::editRequested);
    QVERIFY(model.setData(model.index(0, TaskTableModel::ProgressColumn), 50, Qt::EditRole));
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), task.id);
    QCOMPARE(args.at(1).toInt(), static_cast<int>(TaskTableModel::ProgressColumn));
    QCOMPARE(args.at(2).toInt(), 50);
}

void TstCharts::computesBarForSingleTask() {
    GanttLayout layout(20);
    const QVector<Task> tasks{
        makeTask(QStringLiteral("任务"), QString(), QDate(2026, 1, 1), QDate(2026, 1, 3), 0)};
    const GanttLayoutResult result = layout.compute(tasks);

    QCOMPARE(result.rowCount, 1);
    QCOMPARE(result.totalWidth, 3 * 20);   // 1/1 到 1/3 含首尾 3 天
    QCOMPARE(result.bars.size(), 1);
    QCOMPARE(result.bars.first().startX, 0);
    QCOMPARE(result.bars.first().width, 3 * 20);
    QVERIFY(result.bars.first().hasDates);
    QVERIFY(!result.bars.first().milestone);
}

void TstCharts::handlesCrossMonthRange() {
    GanttLayout layout(10);
    const QVector<Task> tasks{
        makeTask(QStringLiteral("A"), QString(), QDate(2026, 1, 30), QDate(2026, 2, 2), 0)};
    const GanttLayoutResult result = layout.compute(tasks);
    // 1/30、1/31、2/1、2/2 = 4 天
    QCOMPARE(result.totalWidth, 4 * 10);
    QCOMPARE(result.bars.first().width, 4 * 10);
}

void TstCharts::handlesCrossYearRange() {
    GanttLayout layout(10);
    const QVector<Task> tasks{
        makeTask(QStringLiteral("A"), QString(), QDate(2026, 12, 30), QDate(2027, 1, 2), 0)};
    const GanttLayoutResult result = layout.compute(tasks);
    // 2026/12/30、12/31、2027/1/1、1/2 = 4 天（跨年不断裂）
    QCOMPARE(result.bars.first().width, 4 * 10);
    QCOMPARE(result.totalWidth, 4 * 10);
}

void TstCharts::milestoneIsOneDay() {
    GanttLayout layout(20);
    const QVector<Task> tasks{makeTask(QStringLiteral("发布"), QString(), QDate(2026, 3, 1),
                                       QDate(2026, 3, 1), 100, true)};
    const GanttLayoutResult result = layout.compute(tasks);
    QCOMPARE(result.bars.first().width, 20);   // 里程碑占 1 天
    QVERIFY(result.bars.first().milestone);
}

void TstCharts::skipsTasksWithoutDates() {
    GanttLayout layout(20);
    const QVector<Task> tasks{
        makeTask(QStringLiteral("无日期"), QString(), QDate(), QDate(), 0),
        makeTask(QStringLiteral("有日期"), QString(), QDate(2026, 1, 1), QDate(2026, 1, 2), 0)};
    const GanttLayoutResult result = layout.compute(tasks);
    QCOMPARE(result.rowCount, 2);
    QCOMPARE(result.bars.size(), 2);
    QVERIFY(!result.bars.at(0).hasDates);
    QVERIFY(result.bars.at(1).hasDates);
    // 日期范围只取有效日期的任务
    QCOMPARE(result.totalWidth, 2 * 20);
    QCOMPARE(result.bars.at(1).startX, 0);
}

void TstCharts::dateConversionsRoundByDay() {
    GanttLayout layout(24);
    const QDate start(2026, 1, 1);
    QCOMPARE(layout.xForDate(QDate(2026, 1, 3), start), 2 * 24);
    QCOMPARE(layout.dateForX(0, start), QDate(2026, 1, 1));
    QCOMPARE(layout.dateForX(23, start), QDate(2026, 1, 1));   // 不足一天向下取整
    QCOMPARE(layout.dateForX(24, start), QDate(2026, 1, 2));
    QCOMPARE(layout.dateForX(48, start), QDate(2026, 1, 3));
}

QTEST_GUILESS_MAIN(TstCharts)
#include "tst_charts.moc"
