#pragma once
#include "domain/Domain.h"
#include <QFont>
#include <QGraphicsItem>
#include <QIcon>
#include <QPainterPath>
#include <QRectF>
#include <QString>

namespace lingya {

// 通用节点图元：完整标题换行和尺寸测量；测量结果可供乙的布局使用。
class NodeItem : public QGraphicsItem {
public:
  NodeItem(const QString &title, const QFont &font, QGraphicsItem *parent = nullptr,
           const QString &iconId = QString(), const NodeStyle &style = NodeStyle{});

  // 渲染副本中合并默认值与局部覆盖，不回写领域数据。
  static NodeStyle resolvedStyle(const NodeStyle &defaults, const NodeStyleOverride &override);
  static QFont styledFont(const QFont &fallback, const NodeStyle &style);

  // 仅识别内置ID，未知值不作为文件/网络路径读取。
  static QIcon iconForId(const QString &id);

  QRectF boundingRect() const override;
  QPainterPath shape() const override;
  QPointF horizontalAnchor(bool right) const;
  QRectF textRect() const;
  NodeStyle nodeStyle() const;
  void setCutPending(bool pending);
  // 稳定ID用于画布事件定位；可见边框用于连线端点，均不持有领域对象指针。
  void setNodeId(const QString &id);
  QString nodeId() const;
  QRectF nodeRect() const;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QString m_title;
    QString m_nodeId;
    QFont m_font;
    QRectF m_rect;
    QRectF m_textRect;
    QIcon m_icon;
    NodeStyle m_style;
    bool m_cutPending = false;
    QPainterPath m_shape;
    QRectF m_iconRect;
};

}  // namespace lingya
