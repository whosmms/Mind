#include "graphics/NodeItem.h"
#include <QColor>
#include <QFontMetricsF>
#include <QPainter>

namespace lingya
{

namespace
{
// 设计变量（规范§9）：节点白底、边框灰绿1.5px、主文字深绿；圆角12px
constexpr qreal kNodeRadius = 12.0;
// 节点测量常量（规范§6.1）：内边距16px，常规宽度120至280px
constexpr qreal kNodePadding = 16.0;
constexpr qreal kWidthMin = 120.0;
constexpr qreal kWidthMax = 280.0;
constexpr qreal kIconSize = 20.0;
constexpr qreal kIconGap = 8.0;
} // namespace

QIcon NodeItem::iconForId(const QString &id)
{
    if (id == QStringLiteral("idea") || id == QStringLiteral("star") ||
        id == QStringLiteral("check"))
        return QIcon(QStringLiteral(":/lingya/icons/%1.png").arg(id));
    return QIcon();
}

NodeItem::NodeItem(const QString &title, const QFont &font, QGraphicsItem *parent,
                   const QString &iconId)
    : QGraphicsItem(parent), m_title(title), m_font(font), m_icon(iconForId(iconId))
{
    const QFontMetricsF fm(font);
    const qreal iconSpace = m_icon.isNull() ? 0 : kIconSize + kIconGap;
    const qreal width =
        qBound(kWidthMin, fm.horizontalAdvance(m_title) + kNodePadding * 2 + iconSpace, kWidthMax);
    const int flags = Qt::AlignCenter | Qt::TextWrapAnywhere;
    const QRectF text =
        fm.boundingRect(QRectF(0, 0, width - kNodePadding * 2 - iconSpace, 100000), flags, m_title);
    const qreal height =
        qMax(qMax(fm.height(), text.height()), m_icon.isNull() ? 0 : kIconSize) + kNodePadding * 2;
    m_rect = QRectF(-width / 2, -height / 2, width, height);
    m_textRect =
        m_rect.adjusted(kNodePadding + iconSpace, kNodePadding, -kNodePadding, -kNodePadding);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setToolTip(m_title);
}

QRectF NodeItem::boundingRect() const
{
    return m_rect.adjusted(-2, -2, 2, 2); // 包含选中边框，避免裁切
}

void NodeItem::setNodeId(const QString &id)
{
    m_nodeId = id;
}

QString NodeItem::nodeId() const
{
    return m_nodeId;
}

QRectF NodeItem::nodeRect() const
{
    return m_rect;
}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor(isSelected() ? "#247A62" : "#DDE6DF"), isSelected() ? 2.5 : 1.5));
    painter->setBrush(QColor("#FFFFFF"));
    painter->drawRoundedRect(m_rect, kNodeRadius, kNodeRadius);
    if (!m_icon.isNull())
        m_icon.paint(
            painter,
            QRectF(m_rect.left() + kNodePadding, -kIconSize / 2, kIconSize, kIconSize).toRect());
    painter->setPen(QColor("#243A34"));
    painter->setFont(m_font);
    painter->drawText(m_textRect, Qt::AlignCenter | Qt::TextWrapAnywhere, m_title);
}

} // namespace lingya
