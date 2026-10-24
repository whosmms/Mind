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

QColor barFill() {
    return QColor(0xD6, 0xE4, 0xFF);
}

QColor barStroke() {
    return QColor(0x2F, 0x6F, 0xED);
}

}  // namespace

GanttView::GanttView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setBackgroundBrush(QColor(0xFF, 0xFF, 0xFF));
}

void GanttView::setTasks(const QVector<Task>& tasks) {
    m_tasks = tasks;
    rebuild();
}

const QVector<Task>& GanttView::tasks() const {
    return m_tasks;
}

void GanttView::setPixelsPerDay(int pixelsPerDay) {
    m_pixelsPerDay = pixelsPerDay > 0 ? pixelsPerDay : 24;
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
    const QPen stroke(barStroke());

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
            target->addPolygon(diamond, stroke, QBrush(barStroke()));
        } else {
            target->addRect(QRectF(bar.startX, top, bar.width, kRowHeight - 2 * kBarMargin),
                            stroke, QBrush(barFill()));
        }
    }
    target->setSceneRect(0, 0, result.totalWidth, result.rowCount * kRowHeight);
}

}  // namespace lingya
