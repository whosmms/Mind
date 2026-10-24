#include "charts/GanttView.h"

#include "charts/GanttLayout.h"

#include <QBrush>
#include <QColor>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

namespace lingya {

namespace {

constexpr int kRowHeight = 28;
constexpr int kBarMargin = 4;
constexpr int kMilestoneRadius = 8;

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
    m_pixelsPerDay =
        pixelsPerDay > 0 ? pixelsPerDay : GanttLayout::kDefaultPixelsPerDay;
    rebuild();
}

int GanttView::pixelsPerDay() const {
    return m_pixelsPerDay;
}

void GanttView::rebuild() {
    QGraphicsScene* target = scene();
    target->clear();

    GanttLayout layout(m_pixelsPerDay);
    const GanttLayoutResult result = layout.compute(m_tasks);
    const QPen stroke(primaryColor());

    for (const GanttBar& bar : result.bars) {
        if (!bar.hasDates) {
            continue;  // 未排期任务不画条
        }
        const qreal top = bar.row * kRowHeight + kBarMargin;
        if (bar.milestone) {
            const qreal cx = bar.startX + bar.width / 2.0;
            const qreal cy = top + (kRowHeight - 2 * kBarMargin) / 2.0;
            QPolygonF diamond;
            diamond << QPointF(cx, cy - kMilestoneRadius)
                    << QPointF(cx + kMilestoneRadius, cy)
                    << QPointF(cx, cy + kMilestoneRadius)
                    << QPointF(cx - kMilestoneRadius, cy);
            target->addPolygon(diamond, stroke, QBrush(primaryColor()));
        } else {
            target->addRect(QRectF(bar.startX, top, bar.width, kRowHeight - 2 * kBarMargin),
                            stroke, QBrush(mintAccent()));
        }
    }
    target->setSceneRect(0, 0, result.totalWidth, result.rowCount * kRowHeight);
}

}  // namespace lingya
