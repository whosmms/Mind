#include "charts/GanttView.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QStringList>

namespace lingya {

namespace {

constexpr int kAxisHeight = 34;    // 时间轴高度（容纳月份+日号两行）
// 月界竖线起点：压在月份标签下沿（标签高约 16px），既保证轴带内仍看得到月份分界，
// 又不会穿过标签文字；从 kAxisHeight 起画会让轴带内完全看不到分界（P3 52.2）
constexpr int kMonthLineTop = 22;
constexpr int kRowHeight = 28;
constexpr int kBarMargin = 4;
constexpr int kMilestoneRadius = 8;
constexpr int kArrowSize = 5;
constexpr int kUnscheduledHeight = 26;

// 统一规范§14 色值（与 resources/styles/light.qss 一致，不另造颜色）
QColor pageBackground() {
    return QColor(0xF6, 0xF8, 0xF5);  // 页面底色
}

QColor primaryColor() {
    return QColor(0x24, 0x7A, 0x62);  // 主色
}

QColor mintAccent() {
    return QColor(0xDD, 0xF2, 0xE7);  // 薄荷辅助（任务条填充）
}

QColor borderColor() {
    return QColor(0xDD, 0xE6, 0xDF);  // 边框
}

QColor textColor() {
    return QColor(0x24, 0x3A, 0x34);  // 主文字
}

QColor secondaryTextColor() {
    return QColor(0x63, 0x76, 0x6E);  // 次文字
}

qreal rowTop(int row) {
    return kAxisHeight + row * kRowHeight + kBarMargin;
}

qreal rowCenter(int row) {
    return kAxisHeight + row * kRowHeight + kRowHeight / 2.0;
}

const GanttBar* findBar(const QVector<GanttBar>& bars, const ID& taskId) {
    for (const GanttBar& bar : bars) {
        if (bar.taskId == taskId) {
            return &bar;
        }
    }
    return nullptr;
}

// FS 依赖：前置结束点 → 后继开始点右拐折线 + 箭头；中转竖线避开中间行任务条（L-020）
void addDependency(QGraphicsScene* scene, const QVector<GanttBar>& bars, const GanttBar& pred,
                   const GanttBar& succ, int pixelsPerDay) {
    const qreal x1 = pred.milestone ? pred.startX + pred.width / 2.0 + kMilestoneRadius
                                    : pred.startX + pred.width;
    const qreal y1 = rowCenter(pred.row);
    const qreal x2 =
        succ.milestone ? succ.startX + succ.width / 2.0 - kMilestoneRadius : succ.startX;
    const qreal y2 = rowCenter(succ.row);
    const int routeX = freeRouteX(bars, static_cast<int>((x1 + x2) / 2.0), pred.row, succ.row,
                                  pixelsPerDay);

    QPainterPath path;
    path.moveTo(x1, y1);
    path.lineTo(routeX, y1);
    path.lineTo(routeX, y2);
    path.lineTo(x2, y2);
    scene->addPath(path, QPen(primaryColor(), 1.0, Qt::DashLine));

    const qreal direction = (x2 >= routeX) ? 1.0 : -1.0;
    QPolygonF head;
    head << QPointF(x2, y2) << QPointF(x2 - direction * kArrowSize, y2 - kArrowSize * 0.6)
         << QPointF(x2 - direction * kArrowSize, y2 + kArrowSize * 0.6);
    scene->addPolygon(head, Qt::NoPen, QBrush(primaryColor()));
}

}  // namespace

GanttView::GanttView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setBackgroundBrush(pageBackground());
}

void GanttView::setTasks(const QVector<Task>& tasks) {
    m_tasks = tasks;
    rebuild();
}

const QVector<Task>& GanttView::tasks() const {
    return m_tasks;
}

void GanttView::setPixelsPerDay(int pixelsPerDay) {
    m_pixelsPerDay = pixelsPerDay > 0 ? pixelsPerDay : GanttLayout::kDefaultPixelsPerDay;
    rebuild();
}

int GanttView::pixelsPerDay() const {
    return m_pixelsPerDay;
}

int GanttView::rowForTask(const ID& taskId) const {
    for (int row = 0; row < m_tasks.size(); ++row) {
        if (m_tasks.at(row).id == taskId) {
            return row;
        }
    }
    return -1;
}

void GanttView::rebuild() {
    QGraphicsScene* target = scene();
    // 重建会销毁全部图元：拖拽期间若数据被外部刷新（setTasks/撤销/另存），
    // 保留m_dragItem会成为野指针，下一次鼠标移动就会崩溃，故先复位拖拽状态。
    m_dragMode = DragMode::None;
    m_dragItem = nullptr;
    m_dragTaskId.clear();
    target->clear();

    GanttLayout layout(m_pixelsPerDay);
    const GanttLayoutResult result = layout.compute(m_tasks);
    m_rangeStart = result.rangeStart;

    // 未排期任务：不画条，单独列在底部
    QStringList unscheduled;
    for (const Task& task : m_tasks) {
        const bool hasDates = task.startDate.has_value() && task.startDate->isValid() &&
                              task.endDate.has_value() && task.endDate->isValid();
        if (!hasDates) {
            unscheduled << task.title;
        }
    }

    const int rows = result.rowCount;
    const int unscheduledHeight = unscheduled.isEmpty() ? 0 : kUnscheduledHeight;
    const qreal sceneWidth = qMax(result.totalWidth, 1);
    const qreal sceneHeight = kAxisHeight + rows * kRowHeight + unscheduledHeight;
    target->setSceneRect(0, 0, sceneWidth, sceneHeight);

    // 时间轴刻度：每月边界画主线+年月，按天画浅色分隔线与日号
    if (result.rangeStart.isValid() && result.totalWidth > 0) {
        QFont dayFont;
        dayFont.setPointSize(7);
        QFont monthFont;
        monthFont.setPointSize(8);
        monthFont.setBold(true);

        const int totalDays = result.totalWidth / m_pixelsPerDay;
        // 月界标签避让：短区间跨月时两个 yyyy-MM 只隔一天，会压在一起（OBS-A-20261031-001）。
        // 记录上一个已画标签的右边界，与它会重叠时只画竖线不画字。
        qreal lastMonthLabelRight = -1.0e9;
        for (int i = 0; i < totalDays; ++i) {
            const QDate day = result.rangeStart.addDays(i);
            const qreal x = i * m_pixelsPerDay;
            const bool monthLabel = i == 0 || day.day() == 1;
            if (monthLabel)
            {
                const QString monthText = day.toString(QStringLiteral("yyyy-MM"));
                const qreal width = QFontMetricsF(monthFont).horizontalAdvance(monthText);
                // 月界网格从标题下沿起画：不穿字，且轴带内仍保留月份分界（P3 52.2）
                target->addLine(x, kMonthLineTop, x, sceneHeight, QPen(primaryColor(), 1.0));
                if (x + 3 >= lastMonthLabelRight + 6)
                {
                    auto *label = target->addText(monthText, monthFont);
                    label->setDefaultTextColor(primaryColor());
                    label->setPos(x + 3, 0);
                    lastMonthLabelRight = x + 3 + width;
                }
            }
            if (day.day() != 1 && m_pixelsPerDay >= 16)
            {
                if (!monthLabel)
                    target->addLine(x, kAxisHeight - 4, x, sceneHeight, QPen(borderColor(), 0.6));
                auto* label = target->addText(QString::number(day.day()), dayFont);
                label->setDefaultTextColor(secondaryTextColor());
                label->setPos(x + 2, kAxisHeight - 16);
            }
        }
    }

    const QPen stroke(primaryColor());

    // 任务条 + 进度填充 + 里程碑
    for (const GanttBar& bar : result.bars) {
        if (!bar.hasDates) {
            continue;
        }
        const Task* task = nullptr;
        for (const Task& candidate : m_tasks) {
            if (candidate.id == bar.taskId) {
                task = &candidate;
                break;
            }
        }
        if (bar.milestone) {
            const qreal cx = bar.startX + bar.width / 2.0;
            const qreal cy = rowCenter(bar.row);
            QPolygonF diamond;
            diamond << QPointF(cx, cy - kMilestoneRadius) << QPointF(cx + kMilestoneRadius, cy)
                    << QPointF(cx, cy + kMilestoneRadius) << QPointF(cx - kMilestoneRadius, cy);
            auto* item = target->addPolygon(diamond, stroke, QBrush(primaryColor()));
            item->setData(kBarDataRole, bar.taskId);
            continue;
        }

        const QRectF barRect(bar.startX, rowTop(bar.row), bar.width,
                             kRowHeight - 2 * kBarMargin);
        auto* item = target->addRect(barRect, stroke, QBrush(mintAccent()));
        item->setData(kBarDataRole, bar.taskId);

        const int progress = task != nullptr ? qBound(0, task->progress, 100) : 0;
        if (progress > 0) {
            // 填充作为任务条的子图元：①整体平移预览时随条一起移动，不会留在原地；
            // ②命中测试可从填充回溯到带任务ID的父图元，进度越满也能抓住条。
            // 子图元坐标相对父图元的pos（本类中恒为原点），故沿用与父矩形相同的绝对坐标
            const QRectF fillRect(barRect.x(), barRect.y(),
                                  barRect.width() * progress / 100.0, barRect.height());
            auto* fill = target->addRect(fillRect, Qt::NoPen, QBrush(primaryColor()));
            fill->setParentItem(item);
            fill->setPos(0.0, 0.0);
        }
    }

    // FS 依赖连线：前置结束 → 后继开始
    for (const Task& task : m_tasks) {
        const GanttBar* successor = findBar(result.bars, task.id);
        if (successor == nullptr || !successor->hasDates) {
            continue;
        }
        for (const ID& predecessorId : task.predecessorIds) {
            const GanttBar* predecessor = findBar(result.bars, predecessorId);
            if (predecessor != nullptr && predecessor->hasDates) {
                addDependency(target, result.bars, *predecessor, *successor, m_pixelsPerDay);
            }
        }
    }

    // 未排期列表
    if (!unscheduled.isEmpty()) {
        QFont font;
        font.setPointSize(8);
        auto* label = target->addText(
            QStringLiteral("未排期：") + unscheduled.join(QStringLiteral("、")), font);
        label->setDefaultTextColor(secondaryTextColor());
        label->setPos(4, kAxisHeight + rows * kRowHeight + 4);
    }

    // 日期宽度不是文字宽度：把年月和未排期列表纳入滚动范围，保留日期坐标与行高。
    QRectF visibleBounds(0, 0, sceneWidth, sceneHeight);
    for (QGraphicsItem *item : target->items())
    {
        if (item->type() == QGraphicsTextItem::Type)
            visibleBounds = visibleBounds.united(item->sceneBoundingRect());
    }
    target->setSceneRect(visibleBounds);
}

void GanttView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_rangeStart.isValid()) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    // 命中点最上层可能是进度填充或文字，向上回溯到带任务ID的任务条本体
    QGraphicsItem* item = itemAt(event->pos());
    while (item != nullptr && !item->data(kBarDataRole).isValid()) {
        item = item->parentItem();
    }
    if (item == nullptr) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const ID taskId = item->data(kBarDataRole).toString();
    const Task* task = nullptr;
    for (const Task& candidate : m_tasks) {
        if (candidate.id == taskId) {
            task = &candidate;
            break;
        }
    }
    if (task == nullptr || !task->startDate.has_value() || !task->endDate.has_value()) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    m_dragTaskId = taskId;
    m_dragItem = item;
    m_dragOriginalStart = *task->startDate;
    m_dragOriginalEnd = *task->endDate;
    m_dragPressScenePos = mapToScene(event->pos());

    // 条两端 8px 内拖动改端，其余整体平移；里程碑只能整体移动
    const GanttLayout layout(m_pixelsPerDay);
    const int startX = layout.xForDate(m_dragOriginalStart, m_rangeStart);
    const int endX = layout.xForDate(m_dragOriginalEnd, m_rangeStart) + m_pixelsPerDay;
    const qreal x = m_dragPressScenePos.x();
    if (task->milestone) {
        m_dragMode = DragMode::Move;
    } else if (qAbs(x - startX) <= 8.0) {
        m_dragMode = DragMode::ResizeStart;
    } else if (qAbs(x - endX) <= 8.0) {
        m_dragMode = DragMode::ResizeEnd;
    } else {
        m_dragMode = DragMode::Move;
    }
    event->accept();
}

void GanttView::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragMode == DragMode::None) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const GanttLayout layout(m_pixelsPerDay);
    const QDate pressDate = layout.dateForX(qRound(m_dragPressScenePos.x()), m_rangeStart);
    const QDate currentDate = layout.dateForX(qRound(mapToScene(event->pos()).x()), m_rangeStart);
    const int deltaDays = pressDate.daysTo(currentDate);

    // 整体平移时给位移预览；改端只改一端，不做整条位移以免误导
    if (m_dragItem != nullptr) {
        m_dragItem->setPos(m_dragMode == DragMode::Move ? deltaDays * m_pixelsPerDay : 0.0, 0.0);
    }
    event->accept();
}

void GanttView::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragMode == DragMode::None) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    const GanttLayout layout(m_pixelsPerDay);
    const QDate pressDate = layout.dateForX(qRound(m_dragPressScenePos.x()), m_rangeStart);
    const QDate currentDate = layout.dateForX(qRound(mapToScene(event->pos()).x()), m_rangeStart);
    const int deltaDays = pressDate.daysTo(currentDate);

    GanttDragMode dragMode = GanttDragMode::Move;
    if (m_dragMode == DragMode::ResizeStart) {
        dragMode = GanttDragMode::ResizeStart;
    } else if (m_dragMode == DragMode::ResizeEnd) {
        dragMode = GanttDragMode::ResizeEnd;
    }
    const GanttDateRange shifted =
        shiftTaskDates(m_dragOriginalStart, m_dragOriginalEnd, deltaDays, dragMode);

    if (m_dragItem != nullptr) {
        m_dragItem->setPos(0.0, 0.0);
    }
    const ID taskId = m_dragTaskId;
    const bool changed =
        shifted.start != m_dragOriginalStart || shifted.end != m_dragOriginalEnd;

    m_dragMode = DragMode::None;
    m_dragItem = nullptr;
    m_dragTaskId.clear();

    rebuild();   // 先回到模型状态；调用方提交后由 setTasks 再刷新
    if (changed) {
        emit taskDatesEdited(taskId, shifted.start, shifted.end);
    }
    event->accept();
}

}  // namespace lingya
