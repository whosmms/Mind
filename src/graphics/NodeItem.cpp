#include "graphics/NodeItem.h"
#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <cmath>

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
                   const QString &iconId, const NodeStyle &style)
    : QGraphicsItem(parent), m_title(title), m_font(font), m_icon(iconForId(iconId)), m_style(style)
{
    if (m_style.fill.isEmpty())
        m_style.fill = QStringLiteral("#FFFFFF");
    if (m_style.color.isEmpty())
        m_style.color = QStringLiteral("#243A34");
    if (m_style.borderColor.isEmpty())
        m_style.borderColor = QStringLiteral("#DDE6DF");
    const QFontMetricsF fm(font);
    const qreal iconSpace = m_icon.isNull() ? 0 : kIconSize + kIconGap;
    // 粗体墨迹边界可能宽于字距；向上取整并留一个逻辑像素，避免短标题的尾字意外换行。
    const qreal titleWidth =
        std::ceil(qMax(fm.horizontalAdvance(m_title), fm.boundingRect(m_title).width())) + 1;
    const qreal width = qBound(kWidthMin, titleWidth + kNodePadding * 2 + iconSpace, kWidthMax);
    const int flags = Qt::AlignCenter | Qt::TextWrapAnywhere;
    const QRectF text =
        fm.boundingRect(QRectF(0, 0, width - kNodePadding * 2 - iconSpace, 100000), flags, m_title);
    const qreal height =
        qMax(qMax(fm.height(), text.height()), m_icon.isNull() ? 0 : kIconSize) + kNodePadding * 2;
    m_rect = QRectF(-width / 2, -height / 2, width, height);
    m_textRect =
        m_rect.adjusted(kNodePadding + iconSpace, kNodePadding, -kNodePadding, -kNodePadding);
    m_iconRect = QRectF(-width / 2 + kNodePadding, -kIconSize / 2, kIconSize, kIconSize);
    // 扩大斜角形状的外轮廓，完整内容矩形保持居中，避免长标题和图标被切入。
    if (m_style.shape == QStringLiteral("ellipse"))
        m_rect = QRectF(-width / std::sqrt(2.0), -height / std::sqrt(2.0), width * std::sqrt(2.0),
                        height * std::sqrt(2.0));
    else if (m_style.shape == QStringLiteral("diamond"))
        m_rect = QRectF(-width, -height, width * 2, height * 2);
    else if (m_style.shape == QStringLiteral("parallelogram"))
        m_rect.adjust(-24, 0, 24, 0);
    if (m_style.shape == QStringLiteral("ellipse"))
        m_shape.addEllipse(m_rect);
    else if (m_style.shape == QStringLiteral("diamond"))
    {
        m_shape.moveTo(0, m_rect.top());
        m_shape.lineTo(m_rect.right(), 0);
        m_shape.lineTo(0, m_rect.bottom());
        m_shape.lineTo(m_rect.left(), 0);
        m_shape.closeSubpath();
    }
    else if (m_style.shape == QStringLiteral("parallelogram"))
    {
        m_shape.moveTo(m_rect.topLeft() + QPointF(24, 0));
        m_shape.lineTo(m_rect.topRight());
        m_shape.lineTo(m_rect.bottomRight() - QPointF(24, 0));
        m_shape.lineTo(m_rect.bottomLeft());
        m_shape.closeSubpath();
    }
    else if (m_style.shape == QStringLiteral("rect"))
        m_shape.addRect(m_rect);
    else
        m_shape.addRoundedRect(m_rect, kNodeRadius, kNodeRadius);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setToolTip(m_title);
}

QRectF NodeItem::boundingRect() const
{
    const qreal margin = qMax(2.0, m_style.borderWidth / 2 + 0.75);
    return m_rect.adjusted(-margin, -margin, margin, margin);
}

NodeStyle NodeItem::resolvedStyle(const NodeStyle &defaults, const NodeStyleOverride &override)
{
    NodeStyle result = defaults;
    result.fill = override.fill.value_or(defaults.fill);
    result.color = override.color.value_or(defaults.color);
    result.borderColor = override.borderColor.value_or(defaults.borderColor);
    result.fontSize = override.fontSize.value_or(defaults.fontSize);
    result.fontFamily = override.fontFamily.value_or(defaults.fontFamily);
    result.bold = override.bold.value_or(defaults.bold);
    result.shape = override.shape.value_or(defaults.shape);
    result.borderWidth = override.borderWidth.value_or(defaults.borderWidth);
    return result;
}

QFont NodeItem::styledFont(const QFont &fallback, const NodeStyle &style)
{
    QFont result = fallback;
    if (!style.fontFamily.isEmpty() && style.fontFamily != QStringLiteral("system"))
        result.setFamily(style.fontFamily);
    result.setPixelSize(style.fontSize);
    result.setBold(style.bold);
    return result;
}

QPainterPath NodeItem::shape() const
{
    return m_shape;
}
QRectF NodeItem::textRect() const
{
    return m_textRect;
}
NodeStyle NodeItem::nodeStyle() const
{
    return m_style;
}
QPointF NodeItem::horizontalAnchor(bool right) const
{
    const qreal inset = m_style.shape == QStringLiteral("parallelogram") ? 12 : 0;
    return QPointF(right ? m_rect.right() - inset : m_rect.left() + inset, 0);
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
    painter->setPen(QPen(QColor(isSelected() ? QStringLiteral("#247A62") : m_style.borderColor),
                         isSelected() ? qMax(2.5, m_style.borderWidth) : m_style.borderWidth));
    painter->setBrush(QColor(m_style.fill));
    painter->drawPath(m_shape);
    if (!m_icon.isNull())
        m_icon.paint(painter, m_iconRect.toRect());
    painter->setPen(QColor(m_style.color));
    painter->setFont(m_font);
    painter->drawText(m_textRect, Qt::AlignCenter | Qt::TextWrapAnywhere, m_title);
}

} // namespace lingya
