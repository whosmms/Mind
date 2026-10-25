#pragma once
#include "charts/GanttLayout.h"
#include "domain/Domain.h"

#include <QGraphicsView>
#include <QVector>

namespace lingya {

// 甘特时间轴视图（R13，AI规范§6.4）：用 GanttLayout 计算坐标并绘制
// 时间轴刻度、任务条（含进度）、里程碑、FS 依赖连线与未排期列表。
// 甲通过 MainWindow::setGanttView 装入中央区；本类只读展示，不修改 Project。
class GanttView : public QGraphicsView {
    Q_OBJECT
public:
    // 任务条/里程碑图元上的数据键，供测试与外部按任务取图元
    static constexpr int kBarDataRole = 0x10B0;

    explicit GanttView(QWidget* parent = nullptr);

    void setTasks(const QVector<Task>& tasks);
    const QVector<Task>& tasks() const;

    void setPixelsPerDay(int pixelsPerDay);
    int pixelsPerDay() const;

    // 与 TaskTableModel 行同步：任务在场景中的行号（不存在返回 -1）
    int rowForTask(const ID& taskId) const;

private:
    void rebuild();

    QVector<Task> m_tasks;
    int m_pixelsPerDay = GanttLayout::kDefaultPixelsPerDay;
};

}  // namespace lingya
