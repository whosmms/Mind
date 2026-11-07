#include "app/MainWindow.h"

#include "commands/CommandService.h"
#include "commands/FlowCommands.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPathStroker>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace lingya
{
namespace
{
constexpr int kPortRole = 0x21C0;
constexpr int kEdgeRole = 0x21C1;
constexpr int kEdgeHandleRole = 0x21C3;
constexpr int kPendingRole = 0x21C4;
// 扩大细线命中区，避免必须点中单个像素才能拖动。
class FlowConnectionItem : public QGraphicsPathItem
{
  public:
    using QGraphicsPathItem::QGraphicsPathItem;
    QPainterPath shape() const override
    {
        QPainterPathStroker stroke;
        stroke.setWidth(14);
        return stroke.createStroke(path());
    }
    QRectF boundingRect() const override
    {
        return QGraphicsPathItem::boundingRect().adjusted(-7, -7, 7, 7);
    }
};

NodeStyle flowStyle(const FlowSheet &sheet, const FlowNode &node)
{
    NodeStyle style = NodeItem::resolvedStyle(sheet.defaultNodeStyle, node.style);
    // 类型形状只作用于渲染副本；不把capsule这个显示值写入领域样式枚举。
    style.shape = node.role == "decision"                      ? "diamond"
                  : node.role == "io"                          ? "parallelogram"
                  : node.role == "start" || node.role == "end" ? "capsule"
                                                               : "rect";
    return style;
}

QFont flowFont(const NodeStyle &style)
{
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(16);
    return NodeItem::styledFont(font, style);
}

QPointF direction(const QString &port)
{
    if (port == "left")
        return {-1, 0};
    if (port == "right")
        return {1, 0};
    if (port == "top")
        return {0, -1};
    return {0, 1};
}

QPointF anchor(const NodeItem *item, const QString &port)
{
    const auto r = item->nodeRect();
    if (port == "left" || port == "right")
        return item->mapToScene(item->horizontalAnchor(port == "right"));
    return item->mapToScene(QPointF(0, port == "top" ? r.top() : r.bottom()));
}

void addPorts(QComboBox *combo)
{
    combo->addItem(QStringLiteral("自动"), QString());
    combo->addItem(QStringLiteral("左"), "left");
    combo->addItem(QStringLiteral("右"), "right");
    combo->addItem(QStringLiteral("上"), "top");
    combo->addItem(QStringLiteral("下"), "bottom");
}

void reportError(QLabel *label, const AppError &error)
{
    label->setText(error.message);
}

// 以节点包围盒的边界作正交网格，A*只在可见通道前进；不改变手工坐标。
// 起终点是端口向外伸出的短线端点，短线本身允许经过所属节点的安全边距。
QVector<QPointF> routeBetween(QPointF from, QPointF to, const QVector<QRectF> &obstacles)
{
    QVector<qreal> xs{from.x(), to.x()}, ys{from.y(), to.y()};
    for (const auto &rect : obstacles)
    {
        xs << rect.left() << rect.right();
        ys << rect.top() << rect.bottom();
    }
    const auto sort = [](QVector<qreal> &values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    sort(xs);
    sort(ys);
    // 极大手工图不分配无界二维网格；保留可见外通道，后续压力优化不改领域数据。
    if (qint64(xs.size()) * ys.size() > 250000)
    {
        const qreal outside = qMax(xs.last(), qMax(from.x(), to.x())) + 40;
        return {from, {outside, from.y()}, {outside, to.y()}, to};
    }
    const int width = xs.size(), count = width * ys.size();
    const auto point = [&](int i) { return QPointF(xs.at(i % width), ys.at(i / width)); };
    const int start = ys.indexOf(from.y()) * width + xs.indexOf(from.x());
    const int finish = ys.indexOf(to.y()) * width + xs.indexOf(to.x());
    QVector<qreal> distance(count, std::numeric_limits<qreal>::infinity());
    QVector<int> previous(count, -1);
    using Entry = std::pair<qreal, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pending;
    const auto manhattan = [](QPointF a, QPointF b)
    { return std::abs(a.x() - b.x()) + std::abs(a.y() - b.y()); };
    distance[start] = 0;
    pending.push({manhattan(from, to), start});
    QVector<bool> visited(count, false);
    while (!pending.empty())
    {
        const int current = pending.top().second;
        pending.pop();
        if (visited[current])
            continue;
        visited[current] = true;
        if (current == finish)
            break;
        const auto a = point(current);
        const int x = current % width, y = current / width;
        QVector<int> next;
        if (x)
            next.append(current - 1);
        if (x + 1 < width)
            next.append(current + 1);
        if (y)
            next.append(current - width);
        if (y + 1 < ys.size())
            next.append(current + width);
        for (const int candidate : next)
        {
            const auto b = point(candidate), mid = (a + b) / 2;
            bool blocked = false;
            for (const auto &rect : obstacles)
            {
                const auto inside = [&](QPointF p)
                {
                    return p.x() > rect.left() && p.x() < rect.right() && p.y() > rect.top() &&
                           p.y() < rect.bottom();
                };
                if (inside(b) || inside(mid))
                {
                    blocked = true;
                    break;
                }
            }
            if (blocked)
                continue;
            const qreal cost = distance[current] + manhattan(a, b);
            if (cost < distance[candidate])
            {
                distance[candidate] = cost;
                previous[candidate] = current;
                pending.push({cost + manhattan(b, to), candidate});
            }
        }
    }
    if (!std::isfinite(distance.at(finish)))
        return {from, {to.x(), from.y()}, to};
    QVector<QPointF> result;
    for (int i = finish; i != -1; i = previous.at(i))
        result.prepend(point(i));
    return result;
}
} // namespace

const FlowSheet *MainWindow::activeFlowSheet() const
{
    for (const auto &sheet : m_doc->project().sheets)
        if (const auto *flow = std::get_if<FlowSheet>(&sheet);
            flow && flow->id == m_doc->project().activeSheetId)
            return flow;
    return nullptr;
}

void MainWindow::buildFlowActions(QMenu *editMenu)
{
    auto *menu = editMenu->addMenu(QStringLiteral("流程图"));
    const auto add = [menu](const QString &title, const char *name, auto callback)
    {
        auto *action = menu->addAction(title, callback);
        action->setObjectName(QString::fromLatin1(name));
        return action;
    };
    add(QStringLiteral("新增流程节点…"), "addFlowNode", [this] { editFlowNode(true); });
    add(QStringLiteral("编辑流程节点…"), "editFlowNode", [this] { editFlowNode(false); });
    add(QStringLiteral("有向连线…"), "editFlowEdges", [this] { editFlowEdge(); });
    add(QStringLiteral("自动布局（可撤回）"), "layoutFlow", [this] { layoutFlow(); });
    auto *snap = add(QStringLiteral("移动吸附到20像素网格"), "flowSnap", [] {});
    snap->setCheckable(true);
}

void MainWindow::rebuildFlowCanvas(const FlowSheet &sheet)
{
    if (m_displaySheetId != sheet.id || m_flowGestureRevision != m_doc->sessionRevision())
    {
        if (m_displaySheetId != sheet.id)
            m_selectedFlowEdge.clear();
        m_displaySheetId = sheet.id;
        m_flowDragNodeId.clear();
        m_flowPortNodeId.clear();
        m_flowPendingNodeId.clear();
        m_flowDragEdgeId.clear();
        m_flowPreviewBend.reset();
        m_flowDragMoved = false;
        m_flowGestureRevision = m_doc->sessionRevision();
    }
    if (!m_selectedFlowEdge.isEmpty())
    {
        bool exists = false;
        for (const auto &edge : sheet.edges)
            exists |= edge.id == m_selectedFlowEdge;
        if (!exists)
            m_selectedFlowEdge.clear();
    }
    for (const auto &node : sheet.nodes)
    {
        const auto style = flowStyle(sheet, node);
        auto *item = new NodeItem(NodeItem::displayTitle(node), flowFont(style), nullptr,
                                  node.iconId.value_or(QString()), style, node.imagePng);
        item->setNodeId(node.id);
        item->enableAddHandle();
        item->setFlag(QGraphicsItem::ItemIsSelectable);
        item->setPos(node.position);
        m_scene->addItem(item);
        m_nodeItems.insert(node.id, item);
        for (const auto &port : {"left", "right", "top", "bottom"})
        {
            const QPointF point = item->mapFromScene(anchor(item, port));
            auto *handle =
                new QGraphicsEllipseItem(QRectF(point - QPointF(4, 4), QSizeF(8, 8)), item);
            handle->setBrush(QColor("#247A62"));
            handle->setPen(QPen(Qt::white, 1));
            handle->setData(kPortRole, QString::fromLatin1(port));
            handle->setToolTip(QStringLiteral("点击两个端口直接连线；也可按住端口拖向另一个端口"));
            handle->setZValue(2);
        }
    }
    drawFlowEdges(sheet);
    drawCanvasImages();
    if (sheet.nodes.isEmpty())
        m_scene->addText(QStringLiteral("编辑 → 流程图 → 新增流程节点，建立第一步。"));
    m_editorNote = QStringLiteral(
        "流程图：点击两个端口连线；选中线后拖中点调弯、拖两端重接。Esc取消，右键空白新建。");
}

void MainWindow::drawFlowEdges(const FlowSheet &sheet)
{
    // 节点拖动期间仅重绘连线，不提交文档；松手才发一次moveNode命令。
    QList<QGraphicsItem *> oldEdges;
    for (auto *item : m_scene->items())
        if (!item->parentItem() &&
            (item->data(kEdgeRole).isValid() || item->data(kPendingRole).isValid()))
            oldEdges.append(item);
    qDeleteAll(oldEdges);
    QVector<QRectF> obstacles;
    for (const auto *node : m_nodeItems)
        obstacles.append(node->sceneBoundingRect().adjusted(-10, -10, 10, 10));
    int channel = 0;
    for (const auto &edge : sheet.edges)
    {
        const auto *source = m_nodeItems.value(edge.sourceId);
        const auto *target = m_nodeItems.value(edge.targetId);
        if (!source || !target)
            continue;
        const QString from = edge.sourcePort.isEmpty() ? QStringLiteral("bottom") : edge.sourcePort;
        const QString to = edge.targetPort.isEmpty() ? QStringLiteral("top") : edge.targetPort;
        const QPointF a =
            (edge.id == m_flowDragEdgeId && m_flowDragMoved && m_flowDragHandle == "source")
                ? m_flowPointer
                : anchor(source, from);
        const QPointF b =
            (edge.id == m_flowDragEdgeId && m_flowDragMoved && m_flowDragHandle == "target")
                ? m_flowPointer
                : anchor(target, to);
        const QPointF a1 = a + direction(from) * 24, b1 = b + direction(to) * 24;
        QPainterPath path(a);
        path.lineTo(a1);
        const auto bend =
            (edge.id == m_flowDragEdgeId && m_flowPreviewBend) ? m_flowPreviewBend : edge.bendPoint;
        if (bend)
            path.quadTo(*bend * 2 - (a1 + b1) / 2, b1);
        else if (source == target || b1.y() <= a1.y())
        {
            // 自环/回边放在节点右侧的外通道；不会把合法回路当树环拒绝。
            qreal right = qMax(a1.x(), b1.x());
            for (const auto *node : m_nodeItems)
                right = qMax(right, node->sceneBoundingRect().right());
            right += 40 + 18 * channel++;
            const QPointF outside(right, (a1.y() + b1.y()) / 2);
            for (const auto &point : routeBetween(a1, outside, obstacles))
                path.lineTo(point);
            for (const auto &point : routeBetween(outside, b1, obstacles))
                path.lineTo(point);
        }
        else
        {
            for (const auto &point : routeBetween(a1, b1, obstacles))
                path.lineTo(point);
        }
        path.lineTo(b1);
        path.lineTo(b);
        auto *line = new FlowConnectionItem(path);
        line->setPen(QPen(QColor(edge.style.color.isEmpty() ? "#247A62" : edge.style.color),
                          edge.id == m_selectedFlowEdge ? 3 : edge.style.width,
                          edge.style.dashed ? Qt::DashLine : Qt::SolidLine));
        m_scene->addItem(line);
        line->setData(kEdgeRole, edge.id);
        line->setZValue(-1);
        line->setToolTip(edge.label +
                         QStringLiteral("（拖动调弯；选中后拖两端重接；双击编辑，Delete删除）"));
        const auto arrow = [line](QPointF tip, QPointF prior)
        {
            const auto d = tip - prior;
            const qreal length = std::hypot(d.x(), d.y());
            if (length == 0)
                return;
            const auto unit = d / length;
            const QPointF normal(-unit.y(), unit.x());
            QPainterPath head(tip - unit * 11 + normal * 5);
            head.lineTo(tip);
            head.lineTo(tip - unit * 11 - normal * 5);
            auto *item = new QGraphicsPathItem(head, line);
            item->setPen(line->pen());
        };
        if (edge.arrow == "end" || edge.arrow == "both")
            arrow(b, b1);
        if (edge.arrow == "both")
            arrow(a, a1);
        if (!edge.label.isEmpty())
        {
            auto *label = new QGraphicsTextItem(line);
            label->setHtml(QStringLiteral("<span style='background:white;color:#243A34'>%1</span>")
                               .arg(edge.label.toHtmlEscaped()));
            const auto center = path.pointAtPercent(0.5);
            label->setPos(center - QPointF(label->boundingRect().width() / 2,
                                           label->boundingRect().height()));
        }
        if (edge.id == m_selectedFlowEdge)
        {
            const auto handle = [&](const QPointF &point, const QString &kind)
            {
                auto *dot = m_scene->addEllipse(QRectF(point - QPointF(6, 6), QSizeF(12, 12)),
                                                QPen(QColor("#247A62"), 2), QBrush(Qt::white));
                dot->setData(kEdgeRole, edge.id);
                dot->setData(kEdgeHandleRole, kind);
                dot->setZValue(10);
                dot->setCursor(Qt::SizeAllCursor);
                dot->setToolTip(
                    kind == "bend"
                        ? QStringLiteral("拖动调整曲线；右键可恢复自动走线")
                        : QStringLiteral("拖到任意节点的端口，重新连接；无效落点保留原连接"));
            };
            handle(a, "source");
            handle(b, "target");
            handle(bend.value_or(path.pointAtPercent(0.5)), "bend");
        }
    }
    if (!m_flowPendingNodeId.isEmpty() && m_nodeItems.contains(m_flowPendingNodeId))
    {
        const auto start = anchor(m_nodeItems.value(m_flowPendingNodeId), m_flowPendingPort);
        QPainterPath pending(start);
        pending.lineTo(m_flowPointer);
        auto *line = m_scene->addPath(pending, QPen(QColor("#247A62"), 1.5, Qt::DashLine));
        line->setData(kPendingRole, true);
        line->setAcceptedMouseButtons(Qt::NoButton);
        line->setZValue(3);
    }
}

void MainWindow::editFlowNode(bool create)
{
    const auto *sheet = activeFlowSheet();
    if (!sheet || (!create && !sheet->nodes.contains(m_selectedNodeId)))
        return;
    const ID sheetId = sheet->id, nodeId = create ? ID{} : m_selectedNodeId;
    const FlowNode node = create ? FlowNode{} : sheet->nodes.value(nodeId);
    QDialog dialog(this);
    dialog.setObjectName("flowNodeDialog");
    dialog.setWindowTitle(create ? QStringLiteral("新增流程节点") : QStringLiteral("编辑流程节点"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *title = new QPlainTextEdit(node.title, &dialog);
    title->setObjectName("flowTitle");
    title->setMaximumHeight(110);
    auto *role = new QComboBox(&dialog);
    role->setObjectName("flowRole");
    const QStringList roles{"start", "end", "process", "decision", "io"};
    const QStringList labels{QStringLiteral("起点"), QStringLiteral("终点"), QStringLiteral("处理"),
                             QStringLiteral("判断"), QStringLiteral("输入/输出")};
    for (int i = 0; i < roles.size(); ++i)
        role->addItem(labels.at(i), roles.at(i));
    role->setCurrentIndex(role->findData(node.role));
    auto *note = new QPlainTextEdit(node.note, &dialog);
    note->setObjectName("flowNote");
    note->setMaximumHeight(100);
    auto *x = new QDoubleSpinBox(&dialog), *y = new QDoubleSpinBox(&dialog);
    x->setObjectName("flowX");
    y->setObjectName("flowY");
    for (auto *spin : {x, y})
    {
        spin->setRange(-10000000, 10000000);
        spin->setDecimals(3);
    }
    const QPointF initial =
        create ? m_view->mapToScene(m_view->viewport()->rect().center()) : node.position;
    x->setValue(initial.x());
    y->setValue(initial.y());
    const QPointF displayed(x->value(), y->value());
    form->addRow(QStringLiteral("标题（支持多行）"), title);
    form->addRow(QStringLiteral("类型"), role);
    form->addRow(QStringLiteral("备注"), note);
    form->addRow(QStringLiteral("横坐标"), x);
    form->addRow(QStringLiteral("纵坐标"), y);
    layout->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setObjectName("flowError");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&, this]
            {
                FlowCommands commands(m_commands, m_doc);
                if (create)
                {
                    const auto result =
                        commands.addNode(sheetId, role->currentData().toString(),
                                         title->toPlainText(), {x->value(), y->value()});
                    if (const auto *failure = std::get_if<AppError>(&result))
                    {
                        reportError(error, *failure);
                        return;
                    }
                    // 新增命令没有备注参数：首次备注随下一次“编辑节点”提交，避免新增分成两条历史。
                    selectNode(std::get<ID>(result));
                }
                else
                {
                    FlowNodePatch patch;
                    patch.title = title->toPlainText();
                    patch.role = role->currentData().toString();
                    patch.note = note->toPlainText();
                    if (x->value() != displayed.x() || y->value() != displayed.y())
                        patch.position = QPointF(x->value(), y->value());
                    const auto result = commands.updateNode(sheetId, nodeId, patch);
                    if (const auto *failure = std::get_if<AppError>(&result))
                    {
                        reportError(error, *failure);
                        return;
                    }
                }
                dialog.accept();
            });
    if (create)
    {
        note->setEnabled(false);
        note->setPlaceholderText(QStringLiteral("新增后可在编辑节点时填写备注"));
    }
    dialog.resize(480, 480);
    dialog.exec();
}

void MainWindow::editFlowEdge(const ID &edgeId, const ID &sourceId, const QString &sourcePort,
                              const ID &targetId, const QString &targetPort)
{
    const auto *sheet = activeFlowSheet();
    if (!sheet)
        return;
    const FlowSheet snapshot = *sheet;
    QDialog dialog(this);
    dialog.setObjectName("flowEdgeDialog");
    dialog.setWindowTitle(QStringLiteral("有向连线（应用立即生效，关闭保留）"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *record = new QComboBox(&dialog), *source = new QComboBox(&dialog),
         *target = new QComboBox(&dialog);
    auto *from = new QComboBox(&dialog), *to = new QComboBox(&dialog),
         *arrow = new QComboBox(&dialog);
    record->setObjectName("flowEdgeRecord");
    source->setObjectName("flowSource");
    target->setObjectName("flowTarget");
    from->setObjectName("flowSourcePort");
    to->setObjectName("flowTargetPort");
    arrow->setObjectName("flowArrow");
    auto *label = new QLineEdit(&dialog);
    label->setObjectName("flowEdgeLabel");
    addPorts(from);
    addPorts(to);
    arrow->addItem(QStringLiteral("终点箭头"), "end");
    arrow->addItem(QStringLiteral("双向箭头"), "both");
    arrow->addItem(QStringLiteral("无箭头"), "none");
    for (const auto &node : snapshot.nodes)
    {
        source->addItem(node.title, node.id);
        target->addItem(node.title, node.id);
    }
    form->addRow(QStringLiteral("连线"), record);
    form->addRow(QStringLiteral("起点"), source);
    form->addRow(QStringLiteral("起点端口"), from);
    form->addRow(QStringLiteral("终点"), target);
    form->addRow(QStringLiteral("终点端口"), to);
    form->addRow(QStringLiteral("标签（如“是”“否”）"), label);
    form->addRow(QStringLiteral("方向"), arrow);
    layout->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setObjectName("flowError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, &dialog);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    apply->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    auto *remove =
        buttons->addButton(QStringLiteral("删除所选连线"), QDialogButtonBox::DestructiveRole);
    remove->setObjectName("removeFlowEdge");
    layout->addWidget(buttons);
    const auto reload = [&, this](const ID &selected)
    {
        const QSignalBlocker blocker(record);
        record->clear();
        record->addItem(QStringLiteral("新建连线"), ID{});
        if (const auto *current = activeFlowSheet(); current && current->id == snapshot.id)
            for (const auto &edge : current->edges)
                record->addItem(QStringLiteral("%1 → %2：%3")
                                    .arg(current->nodes.value(edge.sourceId).title,
                                         current->nodes.value(edge.targetId).title, edge.label),
                                edge.id);
        record->setCurrentIndex(qMax(0, record->findData(selected)));
    };
    const auto fill = [&, this]
    {
        const ID id = record->currentData().toString();
        const bool editing = !id.isEmpty();
        source->setEnabled(true);
        target->setEnabled(true);
        remove->setEnabled(editing);
        label->clear();
        arrow->setCurrentIndex(0);
        from->setCurrentIndex(0);
        to->setCurrentIndex(0);
        if (const auto *current = activeFlowSheet())
            for (const auto &edge : current->edges)
                if (edge.id == id)
                {
                    source->setCurrentIndex(source->findData(edge.sourceId));
                    target->setCurrentIndex(target->findData(edge.targetId));
                    from->setCurrentIndex(from->findData(edge.sourcePort));
                    to->setCurrentIndex(to->findData(edge.targetPort));
                    arrow->setCurrentIndex(arrow->findData(edge.arrow));
                    label->setText(edge.label);
                }
    };
    connect(record, &QComboBox::currentIndexChanged, &dialog, fill);
    reload(edgeId);
    fill();
    if (!sourceId.isEmpty())
        source->setCurrentIndex(source->findData(sourceId));
    if (!targetId.isEmpty())
        target->setCurrentIndex(target->findData(targetId));
    if (!sourcePort.isEmpty())
        from->setCurrentIndex(from->findData(sourcePort));
    if (!targetPort.isEmpty())
        to->setCurrentIndex(to->findData(targetPort));
    connect(apply, &QPushButton::clicked, &dialog,
            [&, this]
            {
                const ID id = record->currentData().toString();
                FlowCommands commands(m_commands, m_doc);
                ID selected = id;
                if (id.isEmpty())
                {
                    const auto result = commands.addEdge(
                        snapshot.id, source->currentData().toString(),
                        target->currentData().toString(), label->text(),
                        from->currentData().toString(), to->currentData().toString());
                    if (const auto *failure = std::get_if<AppError>(&result))
                    {
                        reportError(error, *failure);
                        return;
                    }
                    selected = std::get<ID>(result);
                }
                else
                {
                    FlowEdgePatch patch;
                    patch.sourceId = source->currentData().toString();
                    patch.targetId = target->currentData().toString();
                    patch.label = label->text();
                    patch.arrow = arrow->currentData().toString();
                    patch.sourcePort = from->currentData().toString();
                    patch.targetPort = to->currentData().toString();
                    const auto result = commands.updateEdge(snapshot.id, id, patch);
                    if (const auto *failure = std::get_if<AppError>(&result))
                    {
                        reportError(error, *failure);
                        return;
                    }
                }
                error->clear();
                reload(selected);
                fill();
                arrow->setEnabled(true);
            });
    // addEdge固定终点箭头：新建时不提供一个会被静默忽略的方向选择。
    connect(record, &QComboBox::currentIndexChanged, &dialog,
            [=] { arrow->setEnabled(!record->currentData().toString().isEmpty()); });
    arrow->setEnabled(!edgeId.isEmpty());
    connect(remove, &QPushButton::clicked, &dialog,
            [&, this]
            {
                const auto result = FlowCommands(m_commands, m_doc)
                                        .removeEdge(snapshot.id, record->currentData().toString());
                if (const auto *failure = std::get_if<AppError>(&result))
                {
                    reportError(error, *failure);
                    return;
                }
                error->clear();
                reload({});
                fill();
                arrow->setEnabled(false);
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(530, 450);
    dialog.exec();
}

void MainWindow::deleteFlowSelection()
{
    const auto *flow = activeFlowSheet();
    if (!flow)
        return;
    const ID sheetId = flow->id, nodeId = m_selectedNodeId, edgeId = m_selectedFlowEdge;
    if (edgeId.isEmpty() && !flow->nodes.contains(nodeId))
        return;
    QMessageBox box(
        QMessageBox::Warning, QStringLiteral("删除流程内容"),
        edgeId.isEmpty()
            ? QStringLiteral(
                  "删除所选节点及其连线、批注；现有卡片快照保留，来源缺失时不可跳转。可撤回。")
            : QStringLiteral("删除所选有向连线，可撤回。"),
        QMessageBox::NoButton, this);
    box.addButton(QStringLiteral("删除"), QMessageBox::AcceptRole);
    auto *cancel = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == cancel || box.clickedButton() == nullptr)
        return;
    FlowCommands commands(m_commands, m_doc);
    const auto result = edgeId.isEmpty() ? commands.removeNode(sheetId, nodeId)
                                         : commands.removeEdge(sheetId, edgeId);
    if (const auto *error = std::get_if<AppError>(&result))
        statusBar()->showMessage(error->message, 8000);
}

void MainWindow::layoutFlow()
{
    const auto *flow = activeFlowSheet();
    if (!flow)
        return;
    QVector<FlowLayout::NodeSize> sizes;
    for (const auto &node : flow->nodes)
    {
        const auto style = flowStyle(*flow, node);
        const NodeItem measured(NodeItem::displayTitle(node), flowFont(style), nullptr,
                                node.iconId.value_or(QString()), style, node.imagePng);
        const auto rect = measured.boundingRect();
        // 四个端口各延伸4px，纳入自动布局间距计算。
        sizes.append({node.id, rect.width() + 8, rect.height() + 8});
    }
    const auto result = FlowCommands(m_commands, m_doc).applyLayout(flow->id, sizes);
    if (const auto *error = std::get_if<AppError>(&result))
        statusBar()->showMessage(error->message, 8000);
    else
        fitCanvas();
}

void MainWindow::cancelFlowGesture()
{
    if (auto *node = m_nodeItems.value(m_flowDragNodeId))
        node->setPos(m_flowDragOrigin);
    m_flowDragNodeId.clear();
    m_flowPortNodeId.clear();
    m_flowPendingNodeId.clear();
    m_flowDragEdgeId.clear();
    m_flowPreviewBend.reset();
    m_flowDragMoved = false;
    if (const auto *sheet = activeFlowSheet())
        drawFlowEdges(*sheet);
}
bool MainWindow::handleFlowEvent(QObject *watched, QEvent *event)
{
    if (!activeFlowSheet())
        return false;
    if ((watched == m_view || watched == m_view->viewport()) && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
    {
        if (!m_flowDragNodeId.isEmpty() || !m_flowPortNodeId.isEmpty() ||
            !m_flowPendingNodeId.isEmpty() || !m_flowDragEdgeId.isEmpty())
        {
            cancelFlowGesture();
            return true;
        }
    }
    if (watched != m_view->viewport())
        return false;
    if (event->type() == QEvent::Leave)
    {
        cancelFlowGesture();
        return false;
    }
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick &&
        type != QEvent::MouseMove && type != QEvent::MouseButtonRelease)
        return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const auto position = m_view->mapToScene(mouse->position().toPoint());
    auto *hit = m_view->itemAt(mouse->position().toPoint());
    ID edgeId;
    QString handle;
    for (auto *ancestor = hit; ancestor; ancestor = ancestor->parentItem())
    {
        if (ancestor->data(kEdgeRole).isValid())
            edgeId = ancestor->data(kEdgeRole).toString();
        if (ancestor->data(kEdgeHandleRole).isValid())
            handle = ancestor->data(kEdgeHandleRole).toString();
    }
    NodeItem *node = nullptr;
    for (auto *ancestor = hit; ancestor; ancestor = ancestor->parentItem())
        if ((node = dynamic_cast<NodeItem *>(ancestor)))
            break;
    // 端点手柄覆盖端口时，仍按几何寻找真正落点，避免命中层级改变重接语义。
    ID portNode;
    QString port;
    double nearest = 12.0 / qMax(0.25, m_view->transform().m11());
    for (auto *item : m_nodeItems)
        for (const auto *name : {"left", "right", "top", "bottom"})
        {
            const auto distance = QLineF(anchor(item, name), position).length();
            if (distance < nearest)
            {
                nearest = distance;
                portNode = item->nodeId();
                port = QString::fromLatin1(name);
            }
        }
    if (type == QEvent::MouseButtonDblClick && mouse->button() == Qt::LeftButton)
    {
        const ID id = node ? node->nodeId() : ID{};
        cancelFlowGesture();
        if (!id.isEmpty())
        {
            selectNode(id);
            editFlowNode(false);
            return true;
        }
        if (!edgeId.isEmpty())
        {
            editFlowEdge(edgeId);
            return true;
        }
    }
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton)
    {
        m_flowDragMoved = false;
        m_dragStart = mouse->position().toPoint();
        m_flowPointer = position;
        m_flowGestureRevision = m_doc->sessionRevision();
        m_view->setFocus();
        if (!handle.isEmpty() || (!edgeId.isEmpty() && port.isEmpty()))
        {
            m_flowPendingNodeId.clear();
            m_selectedFlowEdge = edgeId;
            m_selectedNodeId.clear();
            m_flowDragEdgeId = edgeId;
            m_flowDragHandle = handle.isEmpty() ? QStringLiteral("bend") : handle;
            m_flowPreviewBend.reset();
            m_flowBendOrigin = position;
            for (auto *item : m_scene->items())
                if (auto *line = dynamic_cast<QGraphicsPathItem *>(item);
                    line && !item->parentItem() && item->data(kEdgeRole).toString() == edgeId)
                {
                    m_flowBendOrigin = line->path().pointAtPercent(0.5);
                    break;
                }
            for (const auto &edge : activeFlowSheet()->edges)
                if (edge.id == edgeId && edge.bendPoint)
                    m_flowBendOrigin = *edge.bendPoint;
            syncCanvasSelection();
            drawFlowEdges(*activeFlowSheet());
            return true;
        }
        bool addHandle = false;
        for (auto *ancestor = hit; ancestor; ancestor = ancestor->parentItem())
            addHandle |= ancestor->data(0x21C5).isValid();
        if (addHandle && node)
        {
            quickAddNode(node->nodeId());
            return true;
        }
        if (!port.isEmpty())
        {
            m_flowPortNodeId = portNode;
            m_flowPort = port;
            {
                const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
                selectNode(portNode);
            }
            drawFlowEdges(*activeFlowSheet());
            return true;
        }
        if (node)
        {
            const ID id = node->nodeId();
            m_flowPendingNodeId.clear();
            {
                const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
                selectNode(id);
            }
            m_flowDragNodeId = id;
            m_flowDragOrigin = node->pos();
            drawFlowEdges(*activeFlowSheet());
            return true;
        }
        if (!m_flowPendingNodeId.isEmpty())
        {
            cancelFlowGesture();
            return true;
        }
        m_selectedFlowEdge.clear();
        drawFlowEdges(*activeFlowSheet());
    }
    if (type == QEvent::MouseMove)
    {
        m_flowPointer = position;
        if (!m_flowPendingNodeId.isEmpty() && m_flowPortNodeId.isEmpty() &&
            m_flowDragNodeId.isEmpty() && m_flowDragEdgeId.isEmpty())
        {
            drawFlowEdges(*activeFlowSheet());
            return false;
        }
        if (m_flowDragNodeId.isEmpty() && m_flowPortNodeId.isEmpty() && m_flowDragEdgeId.isEmpty())
            return false;
        if (!(mouse->buttons() & Qt::LeftButton))
        {
            cancelFlowGesture();
            return true;
        }
        m_flowDragMoved |= (mouse->position().toPoint() - m_dragStart).manhattanLength() >=
                           QApplication::startDragDistance();
        if (!m_flowDragMoved)
            return true;
        if (auto *moving = m_nodeItems.value(m_flowDragNodeId))
            moving->setPos(m_flowDragOrigin + position - m_view->mapToScene(m_dragStart));
        if (!m_flowDragEdgeId.isEmpty() && m_flowDragHandle == "bend")
            m_flowPreviewBend = m_flowBendOrigin + position - m_view->mapToScene(m_dragStart);
        if (!m_flowPortNodeId.isEmpty())
        {
            m_flowPendingNodeId = m_flowPortNodeId;
            m_flowPendingPort = m_flowPort;
        }
        drawFlowEdges(*activeFlowSheet());
        return true;
    }
    if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)
    {
        const ID moving = m_flowDragNodeId, source = m_flowPortNodeId, editing = m_flowDragEdgeId;
        const auto from = m_flowPort, kind = m_flowDragHandle;
        const bool moved =
            m_flowDragMoved || (mouse->position().toPoint() - m_dragStart).manhattanLength() >=
                                   QApplication::startDragDistance();
        const auto bend = m_flowPreviewBend;
        QPointF next = m_nodeItems.contains(moving) ? m_nodeItems.value(moving)->pos() : QPointF();
        m_flowDragNodeId.clear();
        m_flowPortNodeId.clear();
        m_flowDragEdgeId.clear();
        m_flowPreviewBend.reset();
        m_flowDragMoved = false;
        const auto center = m_view->mapToScene(m_view->viewport()->rect().center());
        const QScopedValueRollback<bool> preserve(m_keepCanvasView, true);
        const auto report = [this](const auto &result)
        {
            if (const auto *error = std::get_if<AppError>(&result))
                statusBar()->showMessage(error->message, 8000);
        };
        if (!moving.isEmpty())
        {
            if (auto *item = m_nodeItems.value(moving))
                item->setPos(m_flowDragOrigin);
            if (moved && next != m_flowDragOrigin)
            {
                if (auto *snap = findChild<QAction *>("flowSnap"); snap && snap->isChecked())
                    next = {std::round(next.x() / 20) * 20, std::round(next.y() / 20) * 20};
                report(
                    FlowCommands(m_commands, m_doc).moveNode(activeFlowSheet()->id, moving, next));
            }
            drawFlowEdges(*activeFlowSheet());
            m_view->centerOn(center);
            return true;
        }
        if (!editing.isEmpty())
        {
            if (moved)
            {
                FlowEdgePatch patch;
                if (kind == "bend")
                    patch.bendPoint = bend.value_or(m_flowBendOrigin + position -
                                                    m_view->mapToScene(m_dragStart));
                else if (!port.isEmpty())
                {
                    if (kind == "source")
                    {
                        patch.sourceId = portNode;
                        patch.sourcePort = port;
                    }
                    else
                    {
                        patch.targetId = portNode;
                        patch.targetPort = port;
                    }
                }
                if (kind == "bend" || !port.isEmpty())
                    report(FlowCommands(m_commands, m_doc)
                               .updateEdge(activeFlowSheet()->id, editing, patch));
            }
            drawFlowEdges(*activeFlowSheet());
            m_view->centerOn(center);
            return true;
        }
        if (!source.isEmpty())
        {
            ID start = source;
            QString startPort = from;
            if (!moved && !m_flowPendingNodeId.isEmpty())
            {
                start = m_flowPendingNodeId;
                startPort = m_flowPendingPort;
            }
            const bool complete = !port.isEmpty() && (moved || !m_flowPendingNodeId.isEmpty()) &&
                                  (start != portNode || startPort != port);
            if (complete)
            {
                m_flowPendingNodeId.clear();
                report(FlowCommands(m_commands, m_doc)
                           .addEdge(activeFlowSheet()->id, start, portNode, {}, startPort, port));
            }
            else if (!moved && m_flowPendingNodeId.isEmpty())
            {
                m_flowPendingNodeId = source;
                m_flowPendingPort = from;
                statusBar()->showMessage(
                    QStringLiteral("已选起点，请点击目标端口；Esc或点击空白取消"));
            }
            else
                m_flowPendingNodeId.clear();
            drawFlowEdges(*activeFlowSheet());
            m_view->centerOn(center);
            return true;
        }
    }
    return false;
}
} // namespace lingya
