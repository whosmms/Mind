#include "app/MainWindow.h"
#include "charts/GanttView.h"
#include "commands/FlowCommands.h"
#include "commands/ImageCommands.h"
#include "commands/NodeCommands.h"
#include "commands/TaskCommands.h"
#include "document/Fragment.h"
#include "document/ProjectDocument.h"
#include "domain/TitleRules.h"
#include "graphics/NodeItem.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
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
} // namespace

QSet<ID> MainWindow::branchIds(const TreeSheet &sheet, const ID &id)
{
    QMultiHash<ID, ID> children;
    for (const auto &node : sheet.nodes)
        if (node.parentId)
            children.insert(*node.parentId, node.id);
    QSet<ID> ids{id};
    QVector<ID> pending{id};
    for (int at = 0; at < pending.size(); ++at)
    {
        const auto range = children.equal_range(pending.at(at));
        for (auto it = range.first; it != range.second; ++it)
            if (!ids.contains(it.value()))
            {
                ids.insert(it.value());
                pending.append(it.value());
            }
    }
    return ids;
}

void MainWindow::addBranchLearningActions(QMenu &menu, const ID &rootId)
{
    menu.addAction(QStringLiteral("复习此分支…"), this,
                   [this, rootId]
                   {
                       const auto *tree = activeTreeSheet();
                       if (tree && tree->nodes.contains(rootId))
                           manageReviewCards({}, ReviewScope{tree->id, branchIds(*tree, rootId),
                                                             tree->nodes.value(rootId).title});
                   })
        ->setObjectName("reviewBranch");
    menu.addAction(QStringLiteral("把分支整理为文字稿…"), this,
                   [this, rootId] { createDraftFromBranch(rootId); })
        ->setObjectName("draftFromBranch");
}

namespace
{
std::variant<Fragment, QString> outlineFragment(const QString &text, QTreeWidget *preview)
{
    preview->clear();
    if (text.size() > Limits::kMaxFileBytes)
        return QStringLiteral("大纲文本过长，请分批添加。");

    struct Line
    {
        int indent;
        ID id;
        QTreeWidgetItem *item;
    };
    static const QRegularExpression marker(QStringLiteral(R"(^(?:[-+*]|[0-9]+[.)])[ \t]+)"));
    QVector<Line> ancestors;
    QHash<ID, int> siblingCounts;
    Fragment fragment;
    const auto lines = QStringView(text).split(u'\n');
    for (qsizetype index = 0; index < lines.size(); ++index)
    {
        const auto line = lines.at(index);
        auto title = line.trimmed().toString();
        if (title.isEmpty())
            continue;
        const auto bullet = marker.match(title);
        if (bullet.hasMatch())
            title.remove(0, bullet.capturedLength());
        if (TitleRules::exceedsTitleLimit(title))
            return QStringLiteral("第 %1 行的主题超过 %2 个字符。")
                .arg(index + 1)
                .arg(Limits::kTitleMaxChars);
        if (fragment.nodes.size() >= Limits::kMaxNodes)
            return QStringLiteral("一次最多导入 %1 个主题。").arg(Limits::kMaxNodes);

        int indent = 0;
        for (const auto character : line)
        {
            if (character == u' ')
                ++indent;
            else if (character == u'\t')
                indent += 4 - indent % 4;
            else
                break;
        }
        while (!ancestors.isEmpty() && ancestors.back().indent >= indent)
            ancestors.removeLast();

        TreeNode node;
        node.id = QString::number(index);
        node.title = title;
        auto *row = new QTreeWidgetItem;
        if (ancestors.isEmpty())
        {
            fragment.rootIds.append(node.id);
            preview->addTopLevelItem(row);
        }
        else
        {
            node.parentId = ancestors.back().id;
            ancestors.back().item->addChild(row);
        }
        node.order = siblingCounts[node.parentId.value_or(ID{})]++;
        row->setText(0, title);
        row->setToolTip(0, title);
        ancestors.append({indent, node.id, row});
        fragment.nodes.insert(node.id, node);
    }
    preview->expandAll();
    return fragment;
}
} // namespace

void MainWindow::pasteOutlineAsChildren()
{
    const auto *tree = activeTreeSheet();
    if (!tree || !tree->nodes.contains(m_selectedNodeId))
        return;
    const auto sheetId = tree->id;
    const auto parentId = m_selectedNodeId;
    QDialog dialog(this);
    dialog.setObjectName("pasteOutlineDialog");
    dialog.setWindowTitle(QStringLiteral("粘贴大纲为子主题"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(
        QStringLiteral("将主题加入「%1」。每行一个主题，缩进表示子层级，也可粘贴 Markdown 列表。")
            .arg(tree->nodes.value(parentId).title),
        &dialog);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *input = new QPlainTextEdit(&dialog);
    input->setObjectName("outlinePasteText");
    input->setPlaceholderText(QStringLiteral("阶段一\n    准备材料\n    核对清单\n阶段二"));
    if (const auto *mime = QApplication::clipboard()->mimeData(); mime && mime->hasText())
        input->setPlainText(mime->text());
    layout->addWidget(input, 1);
    auto *preview = new QTreeWidget(&dialog);
    preview->setObjectName("outlinePastePreview");
    preview->setHeaderLabel(QStringLiteral("导入预览"));
    layout->addWidget(preview, 1);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *add = buttons->addButton(QStringLiteral("加入导图"), QDialogButtonBox::AcceptRole);
    add->setObjectName("applyOutlinePaste");
    layout->addWidget(buttons);
    Fragment fragment;
    const auto refresh = [&]
    {
        auto result = outlineFragment(input->toPlainText(), preview);
        if (const auto *error = std::get_if<QString>(&result))
        {
            preview->clear();
            add->setEnabled(false);
            status->setText(*error);
            return;
        }
        fragment = std::move(std::get<Fragment>(result));
        add->setEnabled(!fragment.isEmpty());
        status->setText(
            fragment.isEmpty()
                ? QStringLiteral("请粘贴或输入大纲。")
                : QStringLiteral("共 %1 个主题。可一次撤回。").arg(fragment.nodes.size()));
    };
    connect(input, &QPlainTextEdit::textChanged, &dialog, refresh);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&]
            {
                const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
                const auto result =
                    NodeCommands(m_commands, m_doc).pasteFragment(sheetId, parentId, fragment);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    status->setText(error->message);
                    return;
                }
                selectNewNode(std::get<ID>(result));
                dialog.accept();
            });
    refresh();
    dialog.resize(580, 540);
    dialog.exec();
}

void MainWindow::focusBranch()
{
    const auto *tree = activeTreeSheet();
    if (!tree || !tree->nodes.contains(m_selectedNodeId))
        return;
    if (!m_branchFocus)
        m_branchFocus = BranchFocus{
            tree->id,
            m_selectedNodeId,
            m_selectedNodeId,
            m_view->transform(),
            m_view->mapToScene(m_view->viewport()->rect().center()),
            m_scene->sceneRect(),
            {m_view->horizontalScrollBar()->value(), m_view->verticalScrollBar()->value()}};
    else
        m_branchFocus->nodeId = m_selectedNodeId;
    const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
    rebuildCanvas();
    fitCanvas();
    statusBar()->showMessage(QStringLiteral("正在专注分支。按 Esc 返回原视口。"), 5000);
}

void MainWindow::exitBranchFocus()
{
    if (!m_branchFocus)
        return;
    const auto focus = *m_branchFocus;
    m_branchFocus.reset();
    const auto *tree = activeTreeSheet();
    if (!tree || tree->id != focus.sheetId)
    {
        updateEditorChrome();
        return;
    }
    const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
    if (tree->nodes.contains(focus.originalSelection))
        selectNode(focus.originalSelection);
    rebuildCanvas();
    m_view->setTransform(focus.transform);
    const auto bounds =
        focus.sceneRect.united(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
    m_scene->setSceneRect(bounds);
    if (bounds == focus.sceneRect)
    {
        m_view->horizontalScrollBar()->setValue(focus.scrollBars.x());
        m_view->verticalScrollBar()->setValue(focus.scrollBars.y());
    }
    else
        m_view->centerOn(focus.center);
    updateEditorChrome();
}

QRectF MainWindow::focusedBranchBounds() const
{
    const auto *tree = activeTreeSheet();
    if (!m_branchFocus || !tree || tree->id != m_branchFocus->sheetId)
        return {};
    QRectF bounds;
    for (const auto &id : branchIds(*tree, m_branchFocus->nodeId))
        if (const auto *node = m_nodeItems.value(id))
            bounds = bounds.united(node->sceneBoundingRect());
    return bounds;
}

void MainWindow::applyBranchFocus(const TreeSheet &sheet)
{
    const bool focused = m_branchFocus && sheet.id == m_branchFocus->sheetId;
    const auto branch = focused ? branchIds(sheet, m_branchFocus->nodeId) : QSet<ID>{};
    QSet<ID> decorations;
    if (focused)
    {
        for (const auto &relation : sheet.relations)
            if (branch.contains(relation.sourceId) && branch.contains(relation.targetId))
                decorations.insert(relation.id);
        for (const auto &annotation : sheet.annotations)
            if (std::all_of(annotation.memberIds.cbegin(), annotation.memberIds.cend(),
                            [&branch](const ID &id) { return branch.contains(id); }))
                decorations.insert(annotation.id);
    }
    for (auto *item : m_scene->items())
    {
        if (item->parentItem())
            continue;
        const auto *node = dynamic_cast<NodeItem *>(item);
        const bool inBranch = node ? branch.contains(node->nodeId())
                                   : branch.contains(item->data(kTreeEdgeParentRole).toString()) ||
                                         decorations.contains(item->data(1).toString());
        item->setOpacity(!focused || inBranch ? 1.0 : 0.25);
    }
    auto muted = m_outline->palette().color(QPalette::Text);
    muted.setAlphaF(0.45);
    for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
        (*it)->setForeground(0, focused && !branch.contains((*it)->data(0, Qt::UserRole).toString())
                                    ? QBrush(muted)
                                    : QBrush());
}

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
        edge->setData(kTreeEdgeParentRole, *parentId);
    }
    addDecorations(display);
    drawCanvasImages();
    applyBranchFocus(display);
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
            if (auto *outline = findChild<QAction *>("pasteOutline"))
                menu.addAction(outline);
            if (auto *focus = findChild<QAction *>("focusBranch"))
                menu.addAction(focus);
            if (auto *action = findChild<QAction *>("linkSelectedTask"))
                menu.addAction(action);
            if (auto *action = findChild<QAction *>("addReviewCard"))
                menu.addAction(action);
            addBranchLearningActions(menu, nodeId);
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
        menu.addAction(QStringLiteral("编辑连线…"), [this, edgeId] { editFlowEdge(edgeId); });
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
        const bool automatic = tree->kind == QStringLiteral("fishbone");
        const std::optional<QPointF> placed =
            automatic ? std::nullopt : std::optional<QPointF>(position);
        menu.addAction(automatic ? QStringLiteral("新建子主题（自动排布）")
                                 : QStringLiteral("在此新建子主题"),
                       [this, sheetId, parent, placed, report]
                       {
                           const auto result =
                               NodeCommands(m_commands, m_doc)
                                   .addChild(sheetId, parent, QStringLiteral("未命名主题"), placed);
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
