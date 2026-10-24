#pragma once
#include <QGraphicsItem>
#include <QFont>
#include <QRectF>
#include <QString>

namespace lingya {


// 画布节点图元（M0简化版）：圆角矩形 + 标题，用于验证Graphics View技术选型
// （规范§2.1、§12 M0“图形框架至少做一次小验证”）。
// M1/M2将扩展：完整文本测量与折行（规范§6.1）、选中态2.5px轮廓（规范§9）、
// 图标、备注标记、样式覆盖与进度徽标。
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
};

}  // namespace lingya
