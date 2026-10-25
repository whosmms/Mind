#pragma once
#include <QGraphicsItem>
#include <QFont>
#include <QIcon>
#include <QRectF>
#include <QString>

namespace lingya {

// 通用节点图元：完整标题换行和尺寸测量；测量结果可供乙的布局使用。
class NodeItem : public QGraphicsItem {
public:
    NodeItem(const QString &title, const QFont &font, QGraphicsItem *parent = nullptr,
             const QString &iconId = QString());

    // 仅识别内置ID，未知值不作为文件/网络路径读取。
    static QIcon iconForId(const QString &id);

    QRectF boundingRect() const override;
    // 稳定ID用于画布事件定位；可见边框用于连线端点，均不持有领域对象指针。
    void setNodeId(const QString &id);
    QString nodeId() const;
    QRectF nodeRect() const;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    QString m_title;
    QString m_nodeId;
    QFont m_font;
    QRectF m_rect;
    QRectF m_textRect;
    QIcon m_icon;
};

}  // namespace lingya
