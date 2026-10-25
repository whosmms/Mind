#pragma once
#include <QGraphicsItem>
#include <QFont>
#include <QRectF>
#include <QString>

namespace lingya {

// 通用节点图元：完整标题换行和尺寸测量；测量结果可供乙的布局使用。
class NodeItem : public QGraphicsItem {
public:
    NodeItem(const QString& title, const QFont& font, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    QString m_title;
    QFont m_font;
    QRectF m_rect;
    QRectF m_textRect;
};

}  // namespace lingya
