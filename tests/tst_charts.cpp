#include <QtTest>
#include <QSignalSpy>
#include <QUuid>

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

QTEST_GUILESS_MAIN(TstCharts)
#include "tst_charts.moc"
