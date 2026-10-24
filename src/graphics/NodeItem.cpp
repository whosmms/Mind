#include "graphics/NodeItem.h"
#include <QColor>
#include <QFontMetricsF>
#include <QPainter>

namespace lingya {


namespace {
// 设计变量（规范§9）：节点白底、边框灰绿1.5px、主文字深绿；圆角12px
constexpr qreal kNodeRadius = 12.0;
// 节点测量常量（规范§6.1）：内边距16px，常规宽度120至280px
constexpr qreal kNodePadding = 16.0;
constexpr qreal kWidthMin = 120.0;
constexpr qreal kWidthMax = 280.0;
}  // namespace

NodeItem::NodeItem(const QString& title, const QFont& font, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_title(title), m_font(font) {
    const QFontMetricsF fm(font);
    const qreal textWidth = fm.horizontalAdvance(m_title);
    // M0简化：单行显示，超宽截断；完整文本测量与折行为M1（规范§6.1）
    if (textWidth + kNodePadding * 2 > kWidthMax)
        m_title = fm.elidedText(m_title, Qt::ElideRight, kWidthMax - kNodePadding * 2);
    const qreal width = qBound(kWidthMin, fm.horizontalAdvance(m_title) + kNodePadding * 2, kWidthMax);
    const qreal height = fm.height() + kNodePadding * 1.5;
    m_rect = QRectF(-width / 2, -height / 2, width, height);
}

QRectF NodeItem::boundingRect() const {
    return m_rect;
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor("#DDE6DF"), 1.5));   // 节点正常边框1.5px（规范§9）
    painter->setBrush(QColor("#FFFFFF"));
    painter->drawRoundedRect(m_rect, kNodeRadius, kNodeRadius);
    painter->setPen(QColor("#243A34"));
    painter->setFont(m_font);
    painter->drawText(m_rect, Qt::AlignCenter, m_title);
}

}  // namespace lingya
