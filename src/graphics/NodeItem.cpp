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
    const qreal width = qBound(kWidthMin, fm.horizontalAdvance(m_title) + kNodePadding * 2, kWidthMax);
    const int flags = Qt::AlignCenter | Qt::TextWrapAnywhere;
    const QRectF text =
        fm.boundingRect(QRectF(0, 0, width - kNodePadding * 2, 100000), flags, m_title);
    const qreal height = qMax(fm.height(), text.height()) + kNodePadding * 2;
    m_rect = QRectF(-width / 2, -height / 2, width, height);
    m_textRect = m_rect.adjusted(kNodePadding, kNodePadding, -kNodePadding, -kNodePadding);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setToolTip(m_title);
}

QRectF NodeItem::boundingRect() const {
    return m_rect.adjusted(-2, -2, 2, 2); // 包含选中边框，避免裁切
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor(isSelected() ? "#247A62" : "#DDE6DF"), isSelected() ? 2.5 : 1.5));
    painter->setBrush(QColor("#FFFFFF"));
    painter->drawRoundedRect(m_rect, kNodeRadius, kNodeRadius);
    painter->setPen(QColor("#243A34"));
    painter->setFont(m_font);
    painter->drawText(m_textRect, Qt::AlignCenter | Qt::TextWrapAnywhere, m_title);
}

}  // namespace lingya
