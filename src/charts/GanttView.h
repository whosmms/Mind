#pragma once
#include "charts/GanttLayout.h"
#include "domain/Domain.h"

#include <QDate>
#include <QGraphicsView>
#include <QVector>

class QGraphicsItem;

namespace lingya {

// 甘特时间轴视图（R13，AI规范§6.4）：用 GanttLayout 计算坐标并绘制
// 时间轴刻度、任务条（含进度）、里程碑、FS 依赖连线与未排期列表。
// 甲通过 MainWindow::setGanttView 装入中央区；本类只读展示 + 拖拽改期，
// 拖拽结束时发信号，由调用方经 TaskCommands::updateTask 提交（可撤销），视图不改 Project。
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

signals:
    // 拖拽结束后请求调用方提交新起止日期（经 TaskCommands::updateTask）；视图本身不修改 Project
    void taskDatesEdited(const ID& taskId, const QDate& start, const QDate& end);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragMode { None, Move, ResizeStart, ResizeEnd };

    void rebuild();

    QVector<Task> m_tasks;
    int m_pixelsPerDay = GanttLayout::kDefaultPixelsPerDay;

    QDate m_rangeStart;   // 当前时间轴起点（拖拽换算日期用）
    DragMode m_dragMode = DragMode::None;
    QGraphicsItem* m_dragItem = nullptr;   // 正在拖拽的图元（预览用）
    ID m_dragTaskId;
    QDate m_dragOriginalStart;
    QDate m_dragOriginalEnd;
    QPointF m_dragPressScenePos;
};

}  // namespace lingya
