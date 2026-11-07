#include "app/MainWindow.h"
#include "commands/FlowCommands.h"
#include "commands/ImageCommands.h"
#include "commands/NodeCommands.h"
#include "document/ProjectDocument.h"
#include "graphics/CanvasImageItem.h"
#include "graphics/NodeItem.h"
#include "services/ImageService.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QStatusBar>
namespace lingya
{
namespace
{
QString imagePath(QWidget *parent)
{
    return QFileDialog::getOpenFileName(
        parent, QStringLiteral("插入本地图片"), {},
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.gif);;所有文件 (*)"));
}
} // namespace
QVector<CanvasImage> MainWindow::activeImages() const
{
    for (const auto &sheet : m_doc->project().sheets)
        if (std::visit([](const auto &s) { return s.id; }, sheet) == m_doc->project().activeSheetId)
            return std::visit([](const auto &s) { return s.images; }, sheet);
    return {};
}
void MainWindow::drawCanvasImages()
{
    for (const auto &image : activeImages())
    {
        auto *item = new CanvasImageItem(image);
        m_scene->addItem(item);
        item->setSelected(image.id == m_selectedImageId);
    }
}
void MainWindow::chooseNodeImage(const ID &id)
{
    const auto path = imagePath(this);
    if (path.isEmpty())
        return;
    const auto imported = ImageService::importFile(path);
    if (const auto *error = std::get_if<AppError>(&imported))
    {
        statusBar()->showMessage(error->message, 8000);
        return;
    }
    const auto result =
        ImageCommands(m_commands, m_doc)
            .setNodeImage(m_doc->project().activeSheetId, id, std::get<QString>(imported));
    if (const auto *error = std::get_if<AppError>(&result))
        statusBar()->showMessage(error->message, 8000);
}
void MainWindow::insertCanvasImage(const QPointF &position)
{
    const auto path = imagePath(this);
    if (path.isEmpty())
        return;
    const auto imported = ImageService::importFile(path);
    if (const auto *error = std::get_if<AppError>(&imported))
    {
        statusBar()->showMessage(error->message, 8000);
        return;
    }
    const auto result =
        ImageCommands(m_commands, m_doc)
            .add(m_doc->project().activeSheetId, std::get<QString>(imported), position);
    if (const auto *error = std::get_if<AppError>(&result))
        statusBar()->showMessage(error->message, 8000);
    else
    {
        m_selectedImageId = std::get<ID>(result);
        syncCanvasSelection();
    }
}
void MainWindow::quickAddNode(const ID &id)
{
    const auto center = m_view->mapToScene(m_view->viewport()->rect().center());
    const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
    if (const auto *tree = activeTreeSheet())
    {
        std::optional<QPointF> position;
        bool manual = false;
        for (const auto &node : tree->nodes)
            manual |= node.manualPosition.has_value();
        if (manual && tree->kind == "mindmap")
            if (const auto *parent = m_nodeItems.value(id))
            {
                const NodeItem measured(QStringLiteral("未命名主题"),
                                        NodeItem::styledFont(canvasFont(), tree->defaultNodeStyle),
                                        nullptr, {}, tree->defaultNodeStyle);
                const auto box = measured.boundingRect();
                const bool vertical = tree->layout == "down" || tree->layout == "up";
                const bool negative =
                    tree->layout == "left" || tree->layout == "up" ||
                    (tree->layout == "bilateral" && m_nodeItems.contains(tree->rootId) &&
                     parent->pos().x() < m_nodeItems.value(tree->rootId)->pos().x());
                const auto offset =
                    vertical ? QPointF(0, (parent->nodeRect().height() + box.height()) / 2 + 80)
                             : QPointF((parent->nodeRect().width() + box.width()) / 2 + 80, 0);
                const auto preferred = parent->pos() + (negative ? -offset : offset);
                for (int step = 0; step <= m_nodeItems.size() * 2 + 2; ++step)
                {
                    const int row = step == 0 ? 0 : ((step + 1) / 2) * (step % 2 ? 1 : -1);
                    const auto at = preferred + (vertical ? QPointF(row * (box.width() + 32), 0)
                                                          : QPointF(0, row * (box.height() + 32)));
                    if (qAbs(at.x()) > 100000 || qAbs(at.y()) > 100000)
                        continue;
                    bool overlap = false;
                    for (const auto *existing : m_nodeItems)
                        overlap |= existing->sceneBoundingRect().intersects(
                            box.translated(at).adjusted(-12, -12, 12, 12));
                    if (!overlap)
                    {
                        position = at;
                        break;
                    }
                }
                if (!position)
                {
                    statusBar()->showMessage(
                        QStringLiteral("附近没有足够空位，请在空白处右键指定位置"), 8000);
                    return;
                }
            }
        const auto result = NodeCommands(m_commands, m_doc)
                                .addChild(tree->id, id, QStringLiteral("未命名主题"), position);
        if (const auto *error = std::get_if<AppError>(&result))
            statusBar()->showMessage(error->message, 8000);
        else
            selectNewNode(std::get<ID>(result));
    }
    else if (const auto *flow = activeFlowSheet(); flow && flow->nodes.contains(id))
    {
        QPointF at = flow->nodes.value(id).position + QPointF(260, 0);
        for (int step = 0; step < 1000; ++step)
        {
            bool overlap = false;
            for (auto *node : m_nodeItems)
                overlap |= node->sceneBoundingRect().intersects(
                    QRectF(at - QPointF(90, 45), QSizeF(180, 90)));
            if (!overlap)
                break;
            at.ry() += 110;
        }
        const auto result = FlowCommands(m_commands, m_doc).addSuccessor(flow->id, id, at);
        if (const auto *error = std::get_if<AppError>(&result))
            statusBar()->showMessage(error->message, 8000);
        else
            selectNode(std::get<ID>(result));
    }
    m_view->centerOn(center);
    if (auto *item = m_nodeItems.value(m_selectedNodeId))
        m_view->ensureVisible(item, 40, 40);
}
bool MainWindow::handleImageEvent(QObject *watched, QEvent *event)
{
    auto *gantt = qobject_cast<QGraphicsView *>(m_ganttPage);
    auto *view = watched == m_view || watched == m_view->viewport() ? m_view : gantt;
    if (!view || (watched != view && watched != view->viewport()))
        return false;
    const auto restore = [this, view]
    {
        for (auto *item : view->scene()->items())
            if (auto *image = dynamic_cast<CanvasImageItem *>(item);
                image && image->data(0x21C6).toString() == m_dragImageId)
            {
                image->setPos(m_imageDragOrigin);
                image->setSize(m_imageSizeOrigin);
            }
        m_dragImageId.clear();
    };
    if (event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && !m_dragImageId.isEmpty())
        {
            restore();
            return true;
        }
        if (key->key() == Qt::Key_Delete && !m_selectedImageId.isEmpty())
        {
            const auto id = m_selectedImageId;
            m_selectedImageId.clear();
            ImageCommands(m_commands, m_doc).remove(m_doc->project().activeSheetId, id);
            return true;
        }
    }
    if (event->type() == QEvent::Leave)
    {
        if (!m_dragImageId.isEmpty())
            restore();
        return false;
    }
    if (watched != view->viewport())
        return false;
    if (event->type() == QEvent::ContextMenu)
    {
        auto *context = static_cast<QContextMenuEvent *>(event);
        ID id;
        for (auto *item = view->itemAt(context->pos()); item; item = item->parentItem())
            if (item->data(0x21C6).isValid())
                id = item->data(0x21C6).toString();
        if (id.isEmpty())
            return false;
        QMenu menu(this);
        menu.setObjectName("imageContextMenu");
        menu.addAction(
                QStringLiteral("移除图片"), [this, id]
                { ImageCommands(m_commands, m_doc).remove(m_doc->project().activeSheetId, id); })
            ->setObjectName("removeCanvasImage");
        menu.exec(context->globalPos());
        return true;
    }
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove &&
        type != QEvent::MouseButtonRelease)
        return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const auto position = view->mapToScene(mouse->position().toPoint());
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton)
    {
        CanvasImageItem *selected = nullptr;
        bool resize = false;
        for (auto *item = view->itemAt(mouse->position().toPoint()); item;
             item = item->parentItem())
        {
            resize |= item->data(0x21C7).isValid();
            if ((selected = dynamic_cast<CanvasImageItem *>(item)))
                break;
        }
        if (!selected)
        {
            m_selectedImageId.clear();
            return false;
        }
        m_selectedImageId = selected->data(0x21C6).toString();
        m_dragImageId = m_selectedImageId;
        m_imageResize = resize;
        m_imageMoved = false;
        m_imagePress = position;
        m_imageDragOrigin = selected->pos();
        m_imageSizeOrigin = selected->sceneBoundingRect().size();
        m_selectedNodeId.clear();
        m_selectedFlowEdge.clear();
        m_multiSelection.clear();
        m_pendingRelation.clear();
        syncCanvasSelection();
        for (auto *item : view->scene()->items())
            if (auto *image = dynamic_cast<CanvasImageItem *>(item))
                image->setSelected(image == selected);
        view->setFocus();
        return true;
    }
    if (!m_dragImageId.isEmpty())
    {
        CanvasImageItem *item = nullptr;
        for (auto *candidate : view->scene()->items())
            if (auto *image = dynamic_cast<CanvasImageItem *>(candidate);
                image && image->data(0x21C6).toString() == m_dragImageId)
            {
                item = image;
                break;
            }
        if (!item)
        {
            m_dragImageId.clear();
            return true;
        }
        const auto sizeAt = [&]
        {
            const auto delta = position - m_imageDragOrigin;
            double factor = qMax(delta.x() * 2 / m_imageSizeOrigin.width(),
                                 delta.y() * 2 / m_imageSizeOrigin.height());
            factor = qBound(0.1, factor,
                            2000.0 / qMax(m_imageSizeOrigin.width(), m_imageSizeOrigin.height()));
            return m_imageSizeOrigin * factor;
        };
        if (type == QEvent::MouseMove)
        {
            if (!(mouse->buttons() & Qt::LeftButton))
            {
                restore();
                return true;
            }
            m_imageMoved |= QLineF(position, m_imagePress).length() * view->transform().m11() >=
                            QApplication::startDragDistance();
            if (!m_imageMoved)
                return true;
            if (m_imageResize)
                item->setSize(sizeAt());
            else
                item->setPos(m_imageDragOrigin + position - m_imagePress);
            return true;
        }
        if (type == QEvent::MouseButtonRelease)
        {
            if (!m_imageMoved)
            {
                restore();
                return true;
            }
            const auto id = m_dragImageId;
            const bool resized = m_imageResize;
            const auto target =
                resized ? m_imageDragOrigin : m_imageDragOrigin + position - m_imagePress;
            const auto size = resized ? sizeAt() : m_imageSizeOrigin;
            restore();
            const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
            const auto result = ImageCommands(m_commands, m_doc)
                                    .move(m_doc->project().activeSheetId, id, target, size);
            if (const auto *error = std::get_if<AppError>(&result))
                statusBar()->showMessage(error->message, 8000);
            return true;
        }
    }
    return false;
}
} // namespace lingya
