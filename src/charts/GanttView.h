#pragma once
#include "domain/Domain.h"

#include <QGraphicsView>
#include <QVector>

namespace lingya {

// 甘特时间轴视图（R13，AI规范§9.2）：用 GanttLayout 计算坐标并绘制任务条与里程碑。
// 甲通过 MainWindow::setGanttView 装入中央区；本类只读展示，不修改 Project。
class GanttView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GanttView(QWidget* parent = nullptr);

    void setTasks(const QVector<Task>& tasks);
    const QVector<Task>& tasks() const;

    void setPixelsPerDay(int pixelsPerDay);
    int pixelsPerDay() const;

private:
    void rebuild();

    QVector<Task> m_tasks;
    int m_pixelsPerDay = 24;
};

}  // namespace lingya
