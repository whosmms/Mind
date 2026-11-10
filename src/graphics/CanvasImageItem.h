#pragma once
#include "domain/Domain.h"
#include "services/ImageService.h"
#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QPen>
namespace lingya
{
class CanvasImageItem : public QGraphicsPixmapItem
{
  public:
    explicit CanvasImageItem(const CanvasImage &image)
        : QGraphicsPixmapItem(QPixmap::fromImage(ImageService::decode(image.imagePng)))
    {
        setData(0x21C6, image.id);
        setOffset(-pixmap().width() / 2.0, -pixmap().height() / 2.0);
        setPos(image.position);
        setSize(image.size);
        setFlag(ItemIsSelectable);
        setZValue(4);
        setToolTip(QStringLiteral("拖动移动图片；选中后拖右下角缩放；右键可移除"));
        m_handle = new QGraphicsEllipseItem(QRectF(-6, -6, 12, 12), this);
        m_handle->setFlag(ItemIgnoresTransformations);
        m_handle->setData(0x21C7, true);
        m_handle->setPos(pixmap().width() / 2.0, pixmap().height() / 2.0);
        m_handle->setBrush(Qt::white);
        m_handle->setPen(QPen(QColor("#247A62"), 2));
        m_handle->setCursor(Qt::SizeFDiagCursor);
        m_handle->hide();
    }
    void setSize(const QSizeF &size)
    {
        if (pixmap().width() && pixmap().height())
            setTransform(QTransform::fromScale(size.width() / pixmap().width(),
                                               size.height() / pixmap().height()));
    }

  protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == ItemSelectedHasChanged && m_handle)
            m_handle->setVisible(value.toBool());
        return QGraphicsPixmapItem::itemChange(change, value);
    }

  private:
    QGraphicsEllipseItem *m_handle = nullptr;
};
} // namespace lingya
