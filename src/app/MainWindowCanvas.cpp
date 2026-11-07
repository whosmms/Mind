#include "app/MainWindow.h"
#include "charts/GanttView.h"
#include "commands/FlowCommands.h"
#include "commands/ImageCommands.h"
#include "commands/NodeCommands.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <cmath>

namespace lingya
{
namespace
{
NodeItem *owner(QGraphicsItem *hit)
{
    for (auto *item = hit; item; item = item->parentItem())
        if (auto *node = dynamic_cast<NodeItem *>(item))
            return node;
    return nullptr;
}
QSet<ID> branchIds(const TreeSheet &sheet, const ID &id)
{
    QSet<ID> ids{id};
    QVector<ID> pending{id};
    for (int at = 0; at < pending.size(); ++at)
        for (const auto &node : sheet.nodes)
            if (node.parentId == pending[at] && !ids.contains(node.id))
            {
                ids.insert(node.id);
                pending.append(node.id);
            }
    return ids;
}
} // namespace
void MainWindow::drawTreeLinks(const TreeSheet &display)
{
    QList<QGraphicsItem *> old;
    for (auto *item : m_scene->items())
        if (!item->parentItem() && !dynamic_cast<NodeItem *>(item))
            old.append(item);
    qDeleteAll(old);
    const bool fishbone = display.kind == "fishbone";
    if (fishbone)
        addFishboneEdges(display);
    for (auto it = m_nodeItems.constBegin(); !fishbone && it != m_nodeItems.constEnd(); ++it)
    {
        const auto parentId = display.nodes.value(it.key()).parentId;
        if (!parentId.has_value() || !m_nodeItems.contains(*parentId))
            continue;
        const NodeItem *parent = m_nodeItems.value(*parentId);
        const NodeItem *child = it.value();
        const auto delta = child->pos() - parent->pos();
        const bool vertical = std::abs(delta.y()) > std::abs(delta.x());
        const bool right = delta.x() >= 0, down = delta.y() >= 0;
        const QPointF start = vertical
                                  ? parent->mapToScene(QPointF(0, down ? parent->nodeRect().bottom()
                                                                       : parent->nodeRect().top()))
                                  : parent->mapToScene(parent->horizontalAnchor(right));
        const QPointF end = vertical
                                ? child->mapToScene(QPointF(0, down ? child->nodeRect().top()
                                                                    : child->nodeRect().bottom()))
                                : child->mapToScene(child->horizontalAnchor(!right));
        QPainterPath path(start);
        if (vertical)
        {
            const auto middle = (start.y() + end.y()) / 2;
            path.cubicTo(QPointF(start.x(), middle), QPointF(end.x(), middle), end);
        }
        else
        {
            const auto middle = (start.x() + end.x()) / 2;
            path.cubicTo(QPointF(middle, start.y()), QPointF(middle, end.y()), end);
        }
        QPen edgePen(QColor(display.defaultEdgeStyle.color), display.defaultEdgeStyle.width);
        if (display.defaultEdgeStyle.dashed)
            edgePen.setStyle(Qt::DashLine);
        auto *edge = m_scene->addPath(path, edgePen);
        edge->setZValue(-1);
        edge->setAcceptedMouseButtons(Qt::NoButton);
    }
    addDecorations(display);
    drawCanvasImages();
}
void MainWindow::cancelTreeDrag()
{
    for (auto it = m_treeDragOrigins.cbegin(); it != m_treeDragOrigins.cend(); ++it)
        if (auto *item = m_nodeItems.value(it.key()))
            item->setPos(it.value());
    m_treeDragOrigins.clear();
    m_dragNodeId.clear();
    if (const auto *tree = activeTreeSheet())
        drawTreeLinks(*tree);
}
void MainWindow::resetTreeLayout()
{
    if (const auto *tree = activeTreeSheet())
    {
        const auto result = NodeCommands(m_commands, m_doc).resetPositions(tree->id);
        if (const auto *error = std::get_if<AppError>(&result))
            statusBar()->showMessage(error->message, 8000);
    }
}
bool MainWindow::handleTreeCanvasEvent(QObject *watched, QEvent *event)
{
    if (!activeTreeSheet())
        return false;
    if ((watched == m_view || watched == m_view->viewport()) && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && !m_pendingRelation.isEmpty())
    {
        m_pendingRelation.clear();
        statusBar()->showMessage(QStringLiteral("已取消关系线"));
        return true;
    }
    if ((watched == m_view || watched == m_view->viewport()) && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && !m_dragNodeId.isEmpty())
    {
        cancelTreeDrag();
        return true;
    }
    if (watched != m_view->viewport())
        return false;
    const auto *sheet = activeTreeSheet();
    if (event->type() == QEvent::Leave)
    {
        cancelTreeDrag();
        return false;
    }
    if (event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && !m_dragNodeId.isEmpty())
    {
        cancelTreeDrag();
        return true;
    }
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick &&
        type != QEvent::MouseMove && type != QEvent::MouseButtonRelease)
        return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const auto position = m_view->mapToScene(mouse->position().toPoint());
    auto *hit = m_view->itemAt(mouse->position().toPoint());
    if ((type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) &&
        mouse->button() == Qt::LeftButton)
    {
        cancelTreeDrag();
        if (hit && hit->data(kTaskBadgeRole).isValid())
        {
            editTask(hit->data(kTaskBadgeRole).toString());
            return true;
        }
        if (auto *node = owner(hit))
        {
            const auto id = node->nodeId();
            bool plus = false;
            for (auto *ancestor = hit; ancestor; ancestor = ancestor->parentItem())
                plus |= ancestor->data(0x21C5).isValid();
            if (plus)
            {
                m_pendingRelation.clear();
                quickAddNode(id);
                return true;
            }
            if (!m_pendingRelation.isEmpty())
            {
                const auto source = m_pendingRelation;
                m_pendingRelation.clear();
                const auto result =
                    NodeCommands(m_commands, m_doc).addRelation(sheet->id, source, id, {});
                if (const auto *error = std::get_if<AppError>(&result))
                    statusBar()->showMessage(error->message, 8000);
                return true;
            }
            if (mouse->modifiers().testFlag(Qt::ControlModifier))
            {
                if (m_multiSelection.isEmpty() && sheet->nodes.contains(m_selectedNodeId))
                    m_multiSelection.insert(m_selectedNodeId);
                if (m_multiSelection.contains(id))
                    m_multiSelection.remove(id);
                else
                    m_multiSelection.insert(id);
                m_selectedNodeId = m_multiSelection.isEmpty() ? ID{} : *m_multiSelection.cbegin();
                syncCanvasSelection();
                return true;
            }
            m_multiSelection.clear();
            {
                const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
                selectNode(id);
            }
            m_view->setFocus();
            if (type == QEvent::MouseButtonDblClick)
            {
                renameSelectedNode();
                return true;
            }
            m_dragNodeId = id;
            m_dragStart = mouse->position().toPoint();
            m_treePressScene = position;
            m_treeStructuralDrag =
                sheet->kind == "fishbone" || mouse->modifiers().testFlag(Qt::AltModifier);
            for (const auto &child : branchIds(*sheet, id))
                if (auto *item = m_nodeItems.value(child))
                    m_treeDragOrigins[child] = item->pos();
            return true;
        }
    }
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton && !owner(hit))
    {
        m_pendingRelation.clear();
        m_multiSelection.clear();
        syncCanvasSelection();
    }
    if (type == QEvent::MouseMove && !m_dragNodeId.isEmpty())
    {
        if (!(mouse->buttons() & Qt::LeftButton))
        {
            cancelTreeDrag();
            return true;
        }
        if ((mouse->position().toPoint() - m_dragStart).manhattanLength() <
            QApplication::startDragDistance())
            return true;
        if (!m_treeStructuralDrag)
        {
            const auto delta = position - m_treePressScene;
            for (auto it = m_treeDragOrigins.cbegin(); it != m_treeDragOrigins.cend(); ++it)
                if (auto *item = m_nodeItems.value(it.key()))
                    item->setPos(it.value() + delta);
            drawTreeLinks(*sheet);
        }
        statusBar()->showMessage(
            m_treeStructuralDrag
                ? QStringLiteral("松开到另一个主题上改变父级，Esc取消")
                : QStringLiteral("自由移动整支；松手保存位置，Esc取消，Alt拖动可改变父级"));
        return true;
    }
    if (type == QEvent::MouseButtonRelease && !m_dragNodeId.isEmpty())
    {
        const auto id = m_dragNodeId;
        const auto origins = m_treeDragOrigins;
        const bool moved = (mouse->position().toPoint() - m_dragStart).manhattanLength() >=
                           QApplication::startDragDistance();
        const bool structural = m_treeStructuralDrag;
        const auto delta = position - m_treePressScene;
        // 先恢复只读预览，事务失败也保持原连接与位置。
        cancelTreeDrag();
        if (mouse->button() != Qt::LeftButton || !moved)
            return true;
        const auto center = m_view->mapToScene(m_view->viewport()->rect().center());
        const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
        if (structural)
        {
            if (auto *target = owner(m_view->itemAt(mouse->position().toPoint()));
                target && target->nodeId() != id)
                moveSelection(id, target->nodeId());
        }
        else
        {
            QMap<ID, QPointF> positions;
            for (const auto &child : branchIds(*sheet, id))
            {
                if (origins.contains(child))
                    positions[child] = origins.value(child) + delta;
                else if (sheet->nodes.value(child).manualPosition)
                    positions[child] = *sheet->nodes.value(child).manualPosition + delta;
            }
            const auto result = NodeCommands(m_commands, m_doc).setPositions(sheet->id, positions);
            if (const auto *error = std::get_if<AppError>(&result))
                statusBar()->showMessage(error->message, 8000);
        }
        m_view->centerOn(center);
        return true;
    }
    return false;
}
bool MainWindow::handleCanvasContextMenu(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::ContextMenu)
        return false;
    auto *gantt = qobject_cast<QGraphicsView *>(m_ganttPage);
    auto *view = watched == m_view->viewport() ? m_view : gantt;
    if (!view || watched != view->viewport())
        return false;
    auto *context = static_cast<QContextMenuEvent *>(event);
    const auto position = view->mapToScene(context->pos());
    auto *hit = view->itemAt(context->pos());
    ID edgeId, taskId;
    for (auto *item = hit; item; item = item->parentItem())
    {
        if (item->data(0x21C1).isValid())
            edgeId = item->data(0x21C1).toString();
        if (item->data(GanttView::kBarDataRole).isValid())
            taskId = item->data(GanttView::kBarDataRole).toString();
    }
    auto *selected = view == m_view ? owner(hit) : nullptr;
    const ID nodeId = selected ? selected->nodeId() : ID{};
    if (!nodeId.isEmpty())
    {
        const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
        if (!m_multiSelection.contains(nodeId))
            m_multiSelection.clear();
        selectNode(nodeId);
    }
    cancelTreeDrag();
    QMenu menu(this);
    menu.setObjectName("canvasContextMenu");
    menu.setToolTipsVisible(true);
    const auto report = [this](const auto &result)
    {
        if (const auto *error = std::get_if<AppError>(&result))
            statusBar()->showMessage(error->message, 8000);
    };
    if (!nodeId.isEmpty())
    {
        menu.addAction(activeTreeSheet() ? QStringLiteral("添加子主题")
                                         : QStringLiteral("添加并连接新节点"),
                       [this, nodeId] { quickAddNode(nodeId); })
            ->setObjectName("quickAddNode");
        menu.addAction(QStringLiteral("重命名 / 编辑…"), this, &MainWindow::renameSelectedNode);
        if (const auto *tree = activeTreeSheet())
        {
            const ID sheetId = tree->id;
            if (nodeId != tree->rootId)
                menu.addAction(QStringLiteral("添加同级主题"),
                               [this, nodeId, sheetId, report]
                               {
                                   const auto result =
                                       NodeCommands(m_commands, m_doc)
                                           .addSibling(sheetId, nodeId,
                                                       QStringLiteral("未命名主题"));
                                   report(result);
                                   if (const auto *id = std::get_if<ID>(&result))
                                       selectNewNode(*id);
                               });
            menu.addAction(QStringLiteral("从此主题创建关系线"),
                           [this, nodeId]
                           {
                               m_pendingRelation = nodeId;
                               statusBar()->showMessage(
                                   QStringLiteral("请点击目标主题；Esc或点击空白取消"));
                           })
                ->setObjectName("quickRelation");
            const auto annotate = [this, nodeId, sheetId, report](const QString &kind)
            {
                const auto *tree = activeTreeSheet();
                if (!tree || !tree->nodes.value(nodeId).parentId)
                {
                    statusBar()->showMessage(
                        QStringLiteral("请按Ctrl点选至少两个同父且连续的子主题"), 8000);
                    return;
                }
                auto selected = m_multiSelection;
                selected.insert(nodeId);
                QStringList ids;
                for (const auto &id : selected)
                    ids.append(id);
                report(
                    NodeCommands(m_commands, m_doc)
                        .createAnnotation(sheetId, *tree->nodes.value(nodeId).parentId, kind, ids,
                                          kind == "summary" ? QStringLiteral("概要") : QString()));
            };
            menu.addAction(QStringLiteral("为所选主题添加外框"),
                           [annotate] { annotate("boundary"); })
                ->setObjectName("quickBoundary");
            menu.addAction(QStringLiteral("为所选主题添加概要"),
                           [annotate] { annotate("summary"); })
                ->setObjectName("quickSummary");
            menu.addAction(QStringLiteral("添加批注…"), this, &MainWindow::editComments);
            menu.addAction(m_propertiesAction);
            menu.addAction(m_tagsAction);
            if (auto *paste = findChild<QAction *>("pasteTopic"))
                menu.addAction(paste);
            if (auto *action = findChild<QAction *>("linkSelectedTask"))
                menu.addAction(action);
            if (auto *action = findChild<QAction *>("addReviewCard"))
                menu.addAction(action);
        }
        menu.addSeparator();
        menu.addAction(QStringLiteral("插入 / 更换节点图片…"),
                       [this, nodeId] { chooseNodeImage(nodeId); })
            ->setObjectName("insertNodeImage");
        menu.addAction(QStringLiteral("移除节点图片"),
                       [this, nodeId, report]
                       {
                           report(ImageCommands(m_commands, m_doc)
                                      .setNodeImage(m_doc->project().activeSheetId, nodeId, {}));
                       });
    }
    else if (!edgeId.isEmpty() && activeFlowSheet())
    {
        const ID sheetId = activeFlowSheet()->id;
        menu.addAction(QStringLiteral("编辑连线…"), [this, edgeId] { editFlowEdge(edgeId); });
        menu.addAction(QStringLiteral("恢复自动走线"),
                       [this, edgeId, sheetId, report]
                       {
                           FlowEdgePatch patch;
                           patch.resetBend = true;
                           report(
                               FlowCommands(m_commands, m_doc).updateEdge(sheetId, edgeId, patch));
                       })
            ->setObjectName("resetFlowCurve");
    }
    else if (view != m_view)
    {
        if (!taskId.isEmpty())
            menu.addAction(QStringLiteral("编辑任务…"), [this, taskId] { editTask(taskId); });
        menu.addAction(QStringLiteral("新建任务"),
                       [this, report]
                       {
                           const auto result =
                               TaskCommands(m_commands, m_doc).addTask(QStringLiteral("新任务"));
                           report(result);
                           if (const auto *id = std::get_if<ID>(&result))
                               selectTask(*id);
                       });
    }
    else if (const auto *tree = activeTreeSheet())
    {
        const ID sheetId = tree->id,
                 parent = tree->nodes.contains(m_selectedNodeId) ? m_selectedNodeId : tree->rootId;
        menu.addAction(QStringLiteral("在此新建子主题"),
                       [this, sheetId, parent, position, report]
                       {
                           const auto result =
                               NodeCommands(m_commands, m_doc)
                                   .addChild(sheetId, parent, QStringLiteral("未命名主题"),
                                             position);
                           report(result);
                           if (const auto *id = std::get_if<ID>(&result))
                               selectNewNode(*id);
                       })
            ->setObjectName("createTopicHere");
        menu.addAction(QStringLiteral("恢复自动布局"), this, &MainWindow::resetTreeLayout)
            ->setObjectName("resetTreePositions");
    }
    else if (const auto *flow = activeFlowSheet())
    {
        const ID sheetId = flow->id;
        const QStringList roles{"process", "decision", "start", "end", "io"};
        const QStringList labels{QStringLiteral("处理节点"), QStringLiteral("判断节点"),
                                 QStringLiteral("起点"), QStringLiteral("终点"),
                                 QStringLiteral("输入/输出")};
        for (int i = 0; i < roles.size(); ++i)
            menu.addAction(
                    QStringLiteral("新建%1").arg(labels[i]),
                    [this, sheetId, position, role = roles[i], title = labels[i], report]
                    {
                        const auto result =
                            FlowCommands(m_commands, m_doc).addNode(sheetId, role, title, position);
                        report(result);
                        if (const auto *id = std::get_if<ID>(&result))
                            selectNode(*id);
                    })
                ->setObjectName("createFlowHere_" + roles[i]);
        menu.addAction(QStringLiteral("自动布局"), this, &MainWindow::layoutFlow);
    }
    if (nodeId.isEmpty())
    {
        menu.addSeparator();
        menu.addAction(QStringLiteral("在此插入图片…"),
                       [this, position] { insertCanvasImage(position); })
            ->setObjectName("insertCanvasImageHere");
    }
    menu.exec(context->globalPos());
    return true;
}
} // namespace lingya
