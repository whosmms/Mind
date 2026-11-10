#include "graphics/NodeItem.h"
#include "domain/NodeShapes.h"
#include "services/ImageService.h"
#include <QColor>
#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPolygonF>
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
                   const QString &iconId, const NodeStyle &style, const QString &imagePng)
    : QGraphicsItem(parent), m_title(title), m_font(font), m_icon(iconForId(iconId)),
      m_image(ImageService::decode(imagePng)), m_style(style)
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
    const qreal width =
        qMax(qBound(kWidthMin, titleWidth + kNodePadding * 2 + iconSpace, kWidthMax),
             m_image.isNull() ? 0 : qMin(220, m_image.width()) + kNodePadding * 2);
    const int flags = Qt::AlignCenter | Qt::TextWrapAnywhere;
    const QRectF text =
        fm.boundingRect(QRectF(0, 0, width - kNodePadding * 2 - iconSpace, 100000), flags, m_title);
    const qreal textHeight =
        qMax(qMax(fm.height(), text.height()), m_icon.isNull() ? 0 : kIconSize);
    QSize picture = m_image.size();
    if (!m_image.isNull() && (picture.width() > width - kNodePadding * 2 || picture.height() > 140))
        picture = picture.scaled(int(width - kNodePadding * 2), 140, Qt::KeepAspectRatio);
    if (!m_image.isNull())
        picture = QSize(qMax(1, picture.width()), qMax(1, picture.height()));
    const qreal height =
        textHeight + kNodePadding * 2 + (m_image.isNull() ? 0 : picture.height() + 8);
    m_rect = QRectF(-width / 2, -height / 2, width, height);
    m_textRect = QRectF(-width / 2 + kNodePadding + iconSpace, -height / 2 + kNodePadding,
                        width - kNodePadding * 2 - iconSpace, textHeight);
    m_iconRect = QRectF(-width / 2 + kNodePadding, m_textRect.center().y() - kIconSize / 2,
                        kIconSize, kIconSize);
    if (!m_image.isNull())
        m_imageRect = QRectF(-picture.width() / 2.0, m_textRect.bottom() + 8, picture.width(),
                             picture.height());
    // 扩大斜角形状的外轮廓，完整内容矩形保持居中，避免长标题和图标被切入。
    const qreal slant = qMin(24.0, height / 3);
    if (m_style.shape == QStringLiteral("ellipse"))
        m_rect = QRectF(-width / std::sqrt(2.0), -height / std::sqrt(2.0), width * std::sqrt(2.0),
                        height * std::sqrt(2.0));
    else if (m_style.shape == QStringLiteral("diamond"))
        m_rect = QRectF(-width, -height, width * 2, height * 2);
    else if (m_style.shape == QStringLiteral("parallelogram"))
        m_rect.adjust(-24, 0, 24, 0);
    else if (m_style.shape == QLatin1StringView(NodeShapes::kHexagon))
        m_rect.adjust(-slant, 0, slant, 0);
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
    else if (m_style.shape == QLatin1StringView(NodeShapes::kCapsule))
    {
        const qreal radius = qMin(height / 2, 2 * kNodePadding);
        m_shape.addRoundedRect(m_rect, radius, radius);
    }
    else if (m_style.shape == QLatin1StringView(NodeShapes::kHexagon))
    {
        const QPointF inset(slant, 0);
        QPolygonF vertices;
        vertices << m_rect.topLeft() + inset << m_rect.topRight() - inset
                 << QPointF(m_rect.right(), 0) << m_rect.bottomRight() - inset
                 << m_rect.bottomLeft() + inset << QPointF(m_rect.left(), 0);
        m_shape.addPolygon(vertices);
        m_shape.closeSubpath();
    }
    else if (m_style.shape == QLatin1StringView(NodeShapes::kCutRectangle))
    {
        const qreal cut = qMin(14.0, height / 4);
        const QPointF horizontal(cut, 0), vertical(0, cut);
        QPolygonF vertices;
        vertices << m_rect.topLeft() + horizontal << m_rect.topRight() - horizontal
                 << m_rect.topRight() + vertical << m_rect.bottomRight() - vertical
                 << m_rect.bottomRight() - horizontal << m_rect.bottomLeft() + horizontal
                 << m_rect.bottomLeft() - vertical << m_rect.topLeft() + vertical;
        m_shape.addPolygon(vertices);
        m_shape.closeSubpath();
    }
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

QString NodeItem::displayTitle(const BaseNode &node)
{
    return node.provenance && node.provenance->origin == QStringLiteral("supplement")
               ? QStringLiteral("[AI补充] ") + node.title
               : node.title;
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

void NodeItem::setCutPending(bool pending)
{
    m_cutPending = pending;
    update();
}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    paintContent(painter, isSelected(), m_cutPending);
}

void NodeItem::paintSnapshot(QPainter *painter) const
{
    paintContent(painter, false, false);
}

void NodeItem::paintContent(QPainter *painter, bool selected, bool cutPending) const
{
    painter->setRenderHint(QPainter::Antialiasing);
    QPen border(QColor(selected ? QStringLiteral("#247A62") : m_style.borderColor),
                selected ? qMax(2.5, m_style.borderWidth) : m_style.borderWidth);
    if (cutPending)
        border.setStyle(Qt::DashLine);
    painter->setPen(border);
    painter->setBrush(QColor(m_style.fill));
    painter->drawPath(m_shape);
    if (!m_icon.isNull())
        m_icon.paint(painter, m_iconRect.toRect());
    if (!m_image.isNull())
        painter->drawImage(m_imageRect, m_image);
    painter->setPen(QColor(m_style.color));
    painter->setFont(m_font);
    painter->drawText(m_textRect, Qt::AlignCenter | Qt::TextWrapAnywhere, m_title);
}

void NodeItem::enableAddHandle()
{
    if (m_addHandle)
        return;
    setAcceptHoverEvents(true);
    auto *circle = new QGraphicsEllipseItem(QRectF(m_rect.right() + 9, -10, 20, 20), this);
    circle->setPen(QPen(QColor("#247A62"), 1));
    circle->setBrush(QColor("#F0F8F4"));
    circle->setData(0x21C5, true);
    circle->setZValue(8);
    circle->setToolTip(QStringLiteral("点击添加并连接新节点"));
    circle->setCursor(Qt::PointingHandCursor);
    auto *plus = new QGraphicsSimpleTextItem(QStringLiteral("+"), circle);
    QFont font;
    font.setPixelSize(17);
    plus->setFont(font);
    plus->setBrush(QColor("#247A62"));
    plus->setPos(circle->rect().center() - plus->boundingRect().center());
    plus->setData(0x21C5, true);
    m_addHandle = circle;
    circle->setVisible(isSelected() || m_hovered);
}
QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged && m_addHandle)
        m_addHandle->setVisible(value.toBool() || m_hovered);
    return QGraphicsItem::itemChange(change, value);
}
void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    m_hovered = true;
    if (m_addHandle)
        m_addHandle->show();
}
void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    m_hovered = false;
    if (m_addHandle)
        m_addHandle->setVisible(isSelected());
}

} // namespace lingya
