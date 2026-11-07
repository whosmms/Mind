#include "app/MainWindow.h"
#include "commands/PresentationCommands.h"
#include "document/ProjectDocument.h"
#include "features/PresentationController.h"
#include "graphics/NodeItem.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUuid>
#include <QVBoxLayout>

namespace lingya
{
void MainWindow::managePresentation()
{
    const auto *active = activeTreeSheet();
    if (!active || m_homeVisible)
    {
        statusBar()->showMessage(QStringLiteral("请在导图或鱼骨图中建立讲解路线"), 5000);
        return;
    }
    const TreeSheet tree = *active;
    QDialog dialog(this);
    dialog.setObjectName("presentationRoutes");
    dialog.setWindowTitle(QStringLiteral("分支讲解"));
    dialog.resize(780, 620);
    auto *column = new QVBoxLayout(&dialog);
    auto *routeCombo = new QComboBox(&dialog);
    routeCombo->setObjectName("presentationRouteChoice");
    routeCombo->addItem(QStringLiteral("新建：根主题与一级分支"));
    QVector<PresentationRoute> saved;
    for (const auto &route : m_doc->project().routes)
        if (route.sheetId == tree.id)
        {
            saved.append(route);
            routeCombo->addItem(route.title, route.id);
        }
    column->addWidget(routeCombo);
    auto *title = new QLineEdit(&dialog);
    title->setObjectName("presentationTitle");
    column->addWidget(title);
    auto *steps = new QListWidget(&dialog);
    steps->setObjectName("presentationSteps");
    column->addWidget(steps, 1);
    auto *row = new QHBoxLayout;
    auto *nodeChoice = new QComboBox(&dialog);
    nodeChoice->setObjectName("presentationNodeChoice");
    for (auto it = m_nodeItems.cbegin(); it != m_nodeItems.cend(); ++it)
        nodeChoice->addItem(tree.nodes.value(it.key()).title, it.key());
    row->addWidget(nodeChoice, 1);
    auto *add = new QPushButton(QStringLiteral("添加步骤"), &dialog);
    add->setObjectName("presentationAddStep");
    row->addWidget(add);
    auto *up = new QPushButton(QStringLiteral("上移"), &dialog),
         *down = new QPushButton(QStringLiteral("下移"), &dialog),
         *remove = new QPushButton(QStringLiteral("移除步骤"), &dialog);
    up->setObjectName("presentationStepUp");
    down->setObjectName("presentationStepDown");
    remove->setObjectName("presentationRemoveStep");
    row->addWidget(up);
    row->addWidget(down);
    row->addWidget(remove);
    column->addLayout(row);
    column->addWidget(new QLabel(QStringLiteral("所选步骤的讲解备注（播放时默认隐藏）"), &dialog));
    auto *note = new QPlainTextEdit(&dialog);
    note->setObjectName("presentationSpeakerNote");
    note->setMaximumHeight(125);
    column->addWidget(note);
    auto *info = new QLabel(&dialog);
    info->setWordWrap(true);
    info->setObjectName("presentationRouteInfo");
    column->addWidget(info);
    auto *actions = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("保存路线"), &dialog),
         *erase = new QPushButton(QStringLiteral("删除路线"), &dialog),
         *play = new QPushButton(QStringLiteral("开始讲解"), &dialog),
         *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    save->setObjectName("savePresentationRoute");
    erase->setObjectName("deletePresentationRoute");
    play->setObjectName("playPresentation");
    play->setProperty("primary", true);
    actions->addWidget(save);
    actions->addWidget(erase);
    actions->addStretch();
    actions->addWidget(play);
    actions->addWidget(close);
    column->addLayout(actions);
    PresentationRoute route;
    int editing = -1;
    const auto refresh = [&]
    {
        const QSignalBlocker blocker(steps);
        steps->clear();
        for (const auto &step : route.steps)
            steps->addItem(tree.nodes.contains(step.nodeId)
                               ? tree.nodes.value(step.nodeId).title
                               : QStringLiteral("[已删除主题，播放时跳过]"));
        steps->setCurrentRow(route.steps.isEmpty() ? -1 : 0);
        editing = steps->currentRow();
        note->setPlainText(editing < 0 ? QString() : route.steps.at(editing).speakerNote);
        note->setEnabled(editing >= 0);
        erase->setEnabled(routeCombo->currentIndex() > 0);
    };
    const auto load = [&]
    {
        route = routeCombo->currentIndex() > 0 ? saved.at(routeCombo->currentIndex() - 1)
                                               : PresentationController::defaultRoute(tree);
        title->setText(route.title);
        refresh();
        info->clear();
    };
    connect(routeCombo, &QComboBox::currentIndexChanged, &dialog, load);
    connect(steps, &QListWidget::currentRowChanged, &dialog,
            [&](int index)
            {
                editing = index;
                const QSignalBlocker blocker(note);
                note->setPlainText(index < 0 ? QString() : route.steps.at(index).speakerNote);
                note->setEnabled(index >= 0);
            });
    connect(note, &QPlainTextEdit::textChanged, &dialog,
            [&]
            {
                if (editing >= 0 && editing < route.steps.size())
                    route.steps[editing].speakerNote = note->toPlainText();
            });
    connect(add, &QPushButton::clicked, &dialog,
            [&]
            {
                if (nodeChoice->currentIndex() < 0)
                    return;
                route.steps.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                                    nodeChoice->currentData().toString(),
                                    {}});
                refresh();
                steps->setCurrentRow(route.steps.size() - 1);
            });
    const auto move = [&](int offset)
    {
        const int current = steps->currentRow(), next = current + offset;
        if (current < 0 || next < 0 || next >= route.steps.size())
            return;
        route.steps.swapItemsAt(current, next);
        refresh();
        steps->setCurrentRow(next);
    };
    connect(up, &QPushButton::clicked, &dialog, [&] { move(-1); });
    connect(down, &QPushButton::clicked, &dialog, [&] { move(1); });
    connect(remove, &QPushButton::clicked, &dialog,
            [&]
            {
                if (steps->currentRow() < 0)
                    return;
                route.steps.removeAt(steps->currentRow());
                refresh();
            });
    connect(save, &QPushButton::clicked, &dialog,
            [&]
            {
                route.title = title->text().trimmed();
                const auto result = PresentationCommands(m_commands, m_doc).saveRoute(route);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    info->setText(error->message);
                    return;
                }
                if (routeCombo->currentIndex() == 0)
                {
                    saved.append(route);
                    routeCombo->addItem(route.title, route.id);
                    routeCombo->setCurrentIndex(saved.size());
                }
                else
                {
                    saved[routeCombo->currentIndex() - 1] = route;
                    routeCombo->setItemText(routeCombo->currentIndex(), route.title);
                }
                info->setText(QStringLiteral("已保存；可在编辑器撤销"));
            });
    connect(erase, &QPushButton::clicked, &dialog,
            [&]
            {
                const auto result = PresentationCommands(m_commands, m_doc).removeRoute(route.id);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    info->setText(error->message);
                    return;
                }
                const int index = routeCombo->currentIndex();
                saved.removeAt(index - 1);
                {
                    const QSignalBlocker blocker(routeCombo);
                    routeCombo->removeItem(index);
                    routeCombo->setCurrentIndex(0);
                }
                load();
                info->setText(QStringLiteral("路线已删除，可撤销"));
            });
    connect(play, &QPushButton::clicked, &dialog,
            [&]
            {
                route.title = title->text();
                PresentationController probe;
                const auto result = probe.start(tree, route);
                if (const auto *error = std::get_if<AppError>(&result))
                {
                    info->setText(error->message);
                    return;
                }
                playPresentation(route);
            });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    load();
    dialog.exec();
}

void MainWindow::playPresentation(const PresentationRoute &route)
{
    const auto *tree = activeTreeSheet();
    if (!tree)
        return;
    PresentationController controller;
    const auto started = controller.start(*tree, route);
    if (const auto *error = std::get_if<AppError>(&started))
    {
        statusBar()->showMessage(error->message, 8000);
        return;
    }
    const auto selected = m_selectedNodeId;
    const auto selectedTask = m_selectedTaskId;
    const auto transform = m_view->transform();
    const int h = m_view->horizontalScrollBar()->value(), v = m_view->verticalScrollBar()->value();
    const auto tag = m_tagFilter->currentData();
    const auto unfiltered = m_unfilteredCollapsed;
    const bool filterActive = m_filterWasActive;
    QSet<ID> expanded;
    for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
        if ((*it)->isExpanded())
            expanded.insert((*it)->data(0, Qt::UserRole).toString());
    const int outlineH = m_outline->horizontalScrollBar()->value(),
              outlineV = m_outline->verticalScrollBar()->value();
    const auto restore = qScopeGuard(
        [&]
        {
            m_selectedNodeId = selected;
            m_selectedTaskId = selectedTask;
            m_unfilteredCollapsed = unfiltered;
            m_filterWasActive = filterActive;
            {
                const QSignalBlocker blocker(m_tagFilter);
                m_tagFilter->setCurrentIndex(tag.isValid() ? m_tagFilter->findData(tag) : 0);
            }
            rebuildOutline();
            rebuildCanvas();
            {
                const QSignalBlocker blocker(m_outline);
                for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
                    (*it)->setExpanded(expanded.contains((*it)->data(0, Qt::UserRole).toString()));
            }
            m_view->setTransform(transform);
            m_view->horizontalScrollBar()->setValue(h);
            m_view->verticalScrollBar()->setValue(v);
            m_outline->horizontalScrollBar()->setValue(outlineH);
            m_outline->verticalScrollBar()->setValue(outlineV);
        });
    QDialog player(this);
    player.setObjectName("presentationPlayer");
    player.setWindowTitle(route.title);
    player.setWindowFlag(Qt::Window);
    auto *column = new QVBoxLayout(&player);
    auto *header = new QLabel(route.title, &player);
    header->setTextFormat(Qt::PlainText);
    header->setObjectName("presentationHeading");
    column->addWidget(header);
    auto *view = new QGraphicsView(m_scene, &player);
    view->setObjectName("presentationCanvas");
    view->setInteractive(false);
    view->setRenderHint(QPainter::Antialiasing);
    column->addWidget(view, 1);
    auto *note = new QLabel(&player);
    note->setObjectName("presentationVisibleNote");
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    note->hide();
    column->addWidget(note);
    auto *row = new QHBoxLayout;
    auto *previous = new QPushButton(QStringLiteral("← 上一步"), &player),
         *next = new QPushButton(QStringLiteral("下一步 →"), &player),
         *pause = new QPushButton(QStringLiteral("暂停计时"), &player),
         *exit = new QPushButton(QStringLiteral("退出 Esc"), &player);
    previous->setObjectName("presentationPrevious");
    next->setObjectName("presentationNext");
    pause->setObjectName("presentationPause");
    auto *showNote = new QCheckBox(QStringLiteral("显示备注（观众可见）"), &player);
    showNote->setObjectName("presentationShowNote");
    auto *time = new QLabel(&player);
    time->setObjectName("presentationTime");
    row->addWidget(previous);
    row->addWidget(next);
    row->addWidget(pause);
    row->addWidget(showNote);
    row->addStretch();
    row->addWidget(time);
    row->addWidget(exit);
    column->addLayout(row);
    const auto update = [&]
    {
        const auto step = controller.current();
        selectNode(step.nodeId);
        QRectF bounds;
        for (auto it = m_nodeItems.cbegin(); it != m_nodeItems.cend(); ++it)
        {
            ID id = it.key();
            while (tree->nodes.contains(id))
            {
                if (id == step.nodeId)
                {
                    bounds = bounds.united(it.value()->sceneBoundingRect());
                    break;
                }
                const auto parent = tree->nodes.value(id).parentId;
                if (!parent)
                    break;
                id = *parent;
            }
        }
        view->fitInView(bounds.adjusted(-70, -70, 70, 70), Qt::KeepAspectRatio);
        header->setText(
            QStringLiteral("%1  ·  %2 / %3  ·  %4%5")
                .arg(route.title)
                .arg(controller.index() + 1)
                .arg(controller.count())
                .arg(tree->nodes.value(step.nodeId).title)
                .arg(controller.skipped()
                         ? QStringLiteral("（跳过%1个已删除步骤）").arg(controller.skipped())
                         : QString()));
        note->setText(step.speakerNote);
        previous->setEnabled(controller.index() > 0);
        next->setEnabled(controller.index() + 1 < controller.count());
    };
    const auto goNext = [&]
    {
        if (controller.next())
            update();
    };
    const auto goPrevious = [&]
    {
        if (controller.previous())
            update();
    };
    const auto togglePause = [&]
    {
        controller.setPaused(!controller.paused());
        pause->setText(controller.paused() ? QStringLiteral("继续计时")
                                           : QStringLiteral("暂停计时"));
    };
    connect(next, &QPushButton::clicked, &player, goNext);
    connect(previous, &QPushButton::clicked, &player, goPrevious);
    connect(pause, &QPushButton::clicked, &player, togglePause);
    connect(exit, &QPushButton::clicked, &player, &QDialog::reject);
    connect(showNote, &QCheckBox::toggled, note, &QWidget::setVisible);
    for (const auto key : {Qt::Key_Right, Qt::Key_Left, Qt::Key_Space})
    {
        auto *shortcut = new QShortcut(QKeySequence(key), &player);
        if (key == Qt::Key_Right)
            connect(shortcut, &QShortcut::activated, &player, goNext);
        else if (key == Qt::Key_Left)
            connect(shortcut, &QShortcut::activated, &player, goPrevious);
        else
            connect(shortcut, &QShortcut::activated, &player, togglePause);
    }
    QTimer tick(&player);
    tick.setInterval(200);
    connect(&tick, &QTimer::timeout, &player,
            [&]
            {
                const qint64 seconds = controller.elapsedMilliseconds() / 1000;
                time->setText(QStringLiteral("%1:%2")
                                  .arg(seconds / 60, 2, 10, QChar('0'))
                                  .arg(seconds % 60, 2, 10, QChar('0')));
            });
    tick.start();
    player.resize(screen()->geometry().size());
    player.showFullScreen();
    update();
    QTimer::singleShot(0, &player, update);
    player.exec();
}
} // namespace lingya
