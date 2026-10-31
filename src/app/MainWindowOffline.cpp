#include "app/MainWindow.h"

#include "charts/GanttView.h"
#include "charts/TaskTableModel.h"
#include "commands/CommandService.h"
#include "commands/NodeCommands.h"
#include "commands/ReviewCommands.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "features/CheckService.h"
#include "features/ReviewService.h"
#include "graphics/NodeItem.h"
#include "services/ProjectJson.h"
#include "ui/IssuePanel.h"
#include "ui/ReviewPage.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

namespace lingya
{
namespace
{
QVector<Issue> currentIssues(const Project &project)
{
    Project display = project;
    for (auto &card : display.cards)
        card.sourceState = card.mode == "custom" ? QStringLiteral("ok")
                                                 : ReviewService::detectSourceState(card, project);
    return CheckService::checkProject(display);
}
} // namespace
QVector<Task> MainWindow::visibleGanttTasks() const
{
    if (m_ganttTaskLocation)
        return m_doc->project().tasks;
    const GanttSheet *active = nullptr;
    for (const auto &sheet : m_doc->project().sheets)
        if (const auto *gantt = std::get_if<GanttSheet>(&sheet);
            gantt && gantt->id == m_doc->project().activeSheetId)
        {
            active = gantt;
            break;
        }
    // 从树图/流程图的“视图→甘特图”进入时，保持工程全部任务的既有行为。
    if (!active)
        return m_doc->project().tasks;
    QVector<Task> visible;
    for (const auto &task : m_doc->project().tasks)
        if (task.source ? (!active->filter.sourceSheetId ||
                           task.source->sheetId == *active->filter.sourceSheetId)
                        : active->filter.includeStandalone)
            visible.append(task);
    return visible;
}

void MainWindow::buildOfflineActions(QMenu *viewMenu)
{
    auto *dock = new QDockWidget(QStringLiteral("任务表（全部工作表）"), this);
    dock->setObjectName("taskDock");
    auto *panel = new QWidget(dock);
    auto *layout = new QVBoxLayout(panel);
    m_taskModel = new TaskTableModel(this);
    m_taskTable = new QTableView(panel);
    m_taskTable->setObjectName("taskTable");
    m_taskTable->setModel(m_taskModel);
    m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_taskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_taskTable);
    auto *edit = new QPushButton(QStringLiteral("编辑任务 / 里程碑…"), panel);
    edit->setObjectName("editTask");
    layout->addWidget(edit);
    auto *locate = new QPushButton(QStringLiteral("定位关联主题"), panel);
    locate->setObjectName("locateTaskSource");
    locate->setEnabled(false);
    layout->addWidget(locate);
    dock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->hide();
    viewMenu->addAction(dock->toggleViewAction());
    connect(m_taskTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &index)
            {
                if (index.isValid() && index.row() < m_taskModel->tasks().size())
                    selectTask(m_taskModel->tasks().at(index.row()).id,
                               m_centralStack->currentWidget() != m_ganttPage);
            });
    connect(m_taskTable, &QTableView::doubleClicked, this, [this] { editTask(m_selectedTaskId); });
    connect(edit, &QPushButton::clicked, this, [this] { editTask(m_selectedTaskId); });
    connect(locate, &QPushButton::clicked, this, [this] { selectTask(m_selectedTaskId, true); });
    auto *menu = menuBar()->addMenu(QStringLiteral("离线工具"));
    menu->addAction(QStringLiteral("编辑所选任务…"), this, [this] { editTask(m_selectedTaskId); })
        ->setObjectName("editTaskProperties");
    auto *newTask = menu->addAction(
        QStringLiteral("新增独立任务…"), this,
        [this]
        {
            bool ok = false;
            const QString title = QInputDialog::getMultiLineText(
                this, QStringLiteral("新增独立任务"), QStringLiteral("任务名称"), {}, &ok);
            if (!ok)
                return;
            const auto result = TaskCommands(m_commands, m_doc).addTask(title.trimmed());
            if (const auto *error = std::get_if<AppError>(&result))
                statusBar()->showMessage(error->message, 8000);
            else
            {
                selectTask(std::get<ID>(result));
                if (auto *dock = findChild<QDockWidget *>("taskDock"))
                    dock->show();
            }
        });
    newTask->setObjectName("newTask");
    auto *link = menu->addAction(
        QStringLiteral("所选主题设为任务"), this,
        [this]
        {
            const auto *tree = activeTreeSheet();
            if (!tree || !tree->nodes.contains(m_selectedNodeId))
                return;
            for (const auto &task : m_doc->project().tasks)
                if (task.source && task.source->sheetId == tree->id &&
                    task.source->nodeId == m_selectedNodeId)
                {
                    selectTask(task.id);
                    editTask(task.id);
                    return;
                }
            const auto result =
                TaskCommands(m_commands, m_doc).linkTask(tree->id, m_selectedNodeId);
            if (const auto *error = std::get_if<AppError>(&result))
                statusBar()->showMessage(error->message, 8000);
            else
            {
                selectTask(std::get<ID>(result));
                if (auto *dock = findChild<QDockWidget *>("taskDock"))
                    dock->show();
            }
        });
    link->setObjectName("linkSelectedTask");
    auto *addCard = menu->addAction(
        QStringLiteral("所选主题加入复习"), this,
        [this]
        {
            const auto *tree = activeTreeSheet();
            if (!tree || !tree->nodes.contains(m_selectedNodeId))
                return;
            const auto result =
                ReviewCommands(m_commands, m_doc).addCard(tree->id, m_selectedNodeId);
            if (const auto *error = std::get_if<AppError>(&result))
                statusBar()->showMessage(error->message, 8000);
            else
                statusBar()->showMessage(QStringLiteral("已加入复习，可从离线工具打开复习卡片。"),
                                         5000);
        });
    addCard->setObjectName("addReviewCard");
    menu->addAction(QStringLiteral("复习卡片…"), this, [this] { manageReviewCards(); })
        ->setObjectName("showReviewCards");

    auto *issuesDock = new QDockWidget(QStringLiteral("检查助手"), this);
    issuesDock->setObjectName("issueDock");
    m_issuePanel = new IssuePanel(issuesDock);
    issuesDock->setWidget(m_issuePanel);
    addDockWidget(Qt::RightDockWidgetArea, issuesDock);
    issuesDock->hide();
    auto *check = menu->addAction(QStringLiteral("检查当前工程"), this,
                                  [this, issuesDock]
                                  {
                                      m_issuePanel->setIssues(currentIssues(m_doc->project()));
                                      m_checksDirty = false;
                                      issuesDock->show();
                                  });
    check->setObjectName("checkProject");
    connect(issuesDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible)
            {
                if (visible && m_checksDirty)
                {
                    m_issuePanel->setIssues(currentIssues(m_doc->project()));
                    m_checksDirty = false;
                }
            });
    connect(m_issuePanel, &IssuePanel::locateRequested, this, &MainWindow::locateIssue);
    connect(
        m_issuePanel, &IssuePanel::actionRequested, this,
        [this](ID sheetId, ID targetId, QString action)
        {
            locateIssue(sheetId, targetId);
            if (action == "edit-node")
            {
                if ((activeTreeSheet() && activeTreeSheet()->nodes.contains(targetId)) ||
                    (activeFlowSheet() && activeFlowSheet()->nodes.contains(targetId)))
                    renameSelectedNode();
                else
                    statusBar()->showMessage(
                        QStringLiteral(
                            "该建议针对卡片答案，需在复习卡片中先更新来源；未改动其他主题。"),
                        8000);
            }
            else if (action == "edit-task")
                editTask(targetId);
            else if (action == "fix-source")
            {
                bool card = false;
                for (const auto &item : m_doc->project().cards)
                    card |= item.id == targetId;
                if (card)
                    manageReviewCards(targetId);
                else
                    statusBar()->showMessage(
                        QStringLiteral("讲解路线的缺失步骤需在路线编辑器处理，本轮未改写路线。"),
                        8000);
            }
            else if (action == "fix-contrast")
            {
                if (activeTreeSheet())
                    editNodeStyle(true);
                else
                    statusBar()->showMessage(
                        QStringLiteral("当前类型的主题色面板尚未接线，检查建议保留。"), 8000);
            }
        });
}

void MainWindow::refreshOfflineViews()
{
    if (!m_taskModel)
        return;
    const QSignalBlocker blocker(m_taskTable->selectionModel());
    const int scrollX = m_taskTable->horizontalScrollBar()->value();
    const int scrollY = m_taskTable->verticalScrollBar()->value();
    m_taskModel->setTasks(m_doc->project().tasks);
    bool found = false;
    for (int row = 0; row < m_taskModel->tasks().size(); ++row)
        if (m_taskModel->tasks().at(row).id == m_selectedTaskId)
        {
            m_taskTable->setCurrentIndex(m_taskModel->index(row, 0));
            found = true;
            break;
        }
    if (!found)
    {
        m_selectedTaskId.clear();
        if (auto *locate = findChild<QPushButton *>("locateTaskSource"))
            locate->setEnabled(false);
    }
    else
        selectTask(m_selectedTaskId);
    m_taskTable->horizontalScrollBar()->setValue(scrollX);
    m_taskTable->verticalScrollBar()->setValue(scrollY);
    m_checksDirty = true;
    if (auto *dock = findChild<QDockWidget *>("issueDock");
        m_issuePanel && dock && !dock->isHidden())
    {
        m_issuePanel->setIssues(currentIssues(m_doc->project()));
        m_checksDirty = false;
    }
    const auto *tree = activeTreeSheet();
    const bool node = tree && tree->nodes.contains(m_selectedNodeId);
    for (const auto &name : {"linkSelectedTask", "addReviewCard"})
        if (auto *action = findChild<QAction *>(name))
            action->setEnabled(node);
}

void MainWindow::addTaskBadges()
{
    for (const auto &task : m_doc->project().tasks)
    {
        if (!task.source || task.source->sheetId != m_doc->project().activeSheetId)
            continue;
        auto *node = m_nodeItems.value(task.source->nodeId);
        if (!node)
            continue;
        QString text = QStringLiteral("任务 %1%").arg(task.progress);
        if (task.startDate && task.endDate)
            text += QStringLiteral(" %1–%2").arg(task.startDate->toString("MM/dd"),
                                                 task.endDate->toString("MM/dd"));
        auto *badge = new QGraphicsSimpleTextItem(text, node);
        QFont font = this->font();
        font.setPixelSize(10);
        badge->setFont(font);
        badge->setBrush(QColor("#247A62"));
        const QRectF rect = node->nodeRect();
        if (badge->boundingRect().width() > rect.width() - 8)
            badge->setScale((rect.width() - 8) / badge->boundingRect().width());
        badge->setPos(rect.left() + 4, rect.top() + 1);
        badge->setData(kTaskBadgeRole, task.id);
        badge->setToolTip(QStringLiteral("点击编辑任务：%1").arg(task.title));
    }
}

void MainWindow::selectTask(ID taskId, bool locateSource)
{
    if (!m_taskModel)
        return;
    for (int row = 0; row < m_taskModel->tasks().size(); ++row)
    {
        const Task task = m_taskModel->tasks().at(row);
        if (task.id != taskId)
            continue;
        m_selectedTaskId = taskId;
        if (auto *locate = findChild<QPushButton *>("locateTaskSource"))
            locate->setEnabled(task.source.has_value());
        {
            const QSignalBlocker blocker(m_taskTable->selectionModel());
            m_taskTable->setCurrentIndex(m_taskModel->index(row, 0));
            if (!m_refreshingViews)
                m_taskTable->scrollTo(m_taskModel->index(row, 0));
        }
        if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage))
            for (auto *item : gantt->scene()->items())
                if (item->data(GanttView::kBarDataRole).isValid())
                {
                    item->setFlag(QGraphicsItem::ItemIsSelectable);
                    item->setSelected(item->data(GanttView::kBarDataRole).toString() == taskId);
                }
        if (locateSource && task.source && m_doc->setActiveSheet(task.source->sheetId))
            selectNode(task.source->nodeId);
        return;
    }
}

void MainWindow::editTask(ID taskId)
{
    Task initial;
    bool found = false;
    for (const auto &task : m_doc->project().tasks)
        if (task.id == taskId)
        {
            initial = task;
            found = true;
            break;
        }
    if (!found)
    {
        statusBar()->showMessage(QStringLiteral("请先在任务表中选择一项任务。"), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName("taskDialog");
    dialog.setWindowTitle(QStringLiteral("任务属性"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *title = new QLineEdit(initial.title, &dialog);
    title->setObjectName("taskTitle");
    auto *rename = new QPushButton(QStringLiteral("单独应用名称（与关联主题同步）"), &dialog);
    rename->setObjectName("renameTask");
    auto *scheduled = new QCheckBox(QStringLiteral("已排期（取消则清空两日期）"), &dialog);
    scheduled->setObjectName("taskScheduled");
    scheduled->setChecked(initial.startDate.has_value());
    auto *start = new QDateEdit(initial.startDate.value_or(QDate::currentDate()), &dialog);
    auto *end = new QDateEdit(initial.endDate.value_or(QDate::currentDate()), &dialog);
    start->setObjectName("taskStart");
    end->setObjectName("taskEnd");
    for (auto *date : {start, end})
    {
        date->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
        date->setDisplayFormat("yyyy-MM-dd");
        date->setCalendarPopup(true);
        date->setEnabled(scheduled->isChecked());
    }
    start->setDate(initial.startDate.value_or(QDate::currentDate()));
    end->setDate(initial.endDate.value_or(QDate::currentDate()));
    auto *milestone = new QCheckBox(QStringLiteral("里程碑（起止须同日）"), &dialog);
    milestone->setObjectName("taskMilestone");
    milestone->setToolTip(
        QStringLiteral("未排期里程碑保留在任务表，设置同日起止日期后才显示在时间轴。"));
    milestone->setChecked(initial.milestone);
    auto *progress = new QSpinBox(&dialog);
    progress->setObjectName("taskProgress");
    progress->setRange(0, 100);
    progress->setValue(initial.progress);
    progress->setSuffix("%");
    auto *owner = new QLineEdit(initial.owner, &dialog);
    owner->setObjectName("taskOwner");
    auto *predecessors = new QListWidget(&dialog);
    predecessors->setObjectName("taskPredecessors");
    predecessors->setMaximumHeight(100);
    for (const auto &task : m_doc->project().tasks)
        if (task.id != taskId)
        {
            auto *item = new QListWidgetItem(task.title, predecessors);
            item->setData(Qt::UserRole, task.id);
            item->setCheckState(initial.predecessorIds.contains(task.id) ? Qt::Checked
                                                                         : Qt::Unchecked);
        }
    form->addRow(QStringLiteral("名称"), title);
    form->addRow(rename);
    form->addRow(scheduled);
    form->addRow(QStringLiteral("开始"), start);
    form->addRow(QStringLiteral("结束"), end);
    form->addRow(milestone);
    form->addRow(QStringLiteral("进度"), progress);
    form->addRow(QStringLiteral("负责人"), owner);
    form->addRow(QStringLiteral("前置任务（FS）"), predecessors);
    layout->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setObjectName("taskError");
    error->setWordWrap(true);
    layout->addWidget(error);
    connect(scheduled, &QCheckBox::toggled, &dialog,
            [=](bool checked)
            {
                start->setEnabled(checked);
                end->setEnabled(checked);
            });
    connect(rename, &QPushButton::clicked, &dialog,
            [&, this]
            {
                const auto result =
                    TaskCommands(m_commands, m_doc).renameTask(taskId, title->text());
                if (const auto *failure = std::get_if<AppError>(&result))
                    error->setText(failure->message);
                else
                {
                    for (const auto &task : m_doc->project().tasks)
                        if (task.id == taskId)
                        {
                            initial.title = task.title;
                            title->setText(task.title);
                        }
                    error->clear();
                }
            });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用属性"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("关闭"));
    auto *unlink = buttons->addButton(QStringLiteral("解除关联"), QDialogButtonBox::ActionRole);
    unlink->setObjectName("unlinkTask");
    unlink->setEnabled(initial.source.has_value());
    auto *remove = buttons->addButton(QStringLiteral("删除任务…"), QDialogButtonBox::ActionRole);
    remove->setObjectName("removeTask");
    connect(unlink, &QPushButton::clicked, &dialog,
            [&, this]
            {
                const auto result = TaskCommands(m_commands, m_doc).unlinkTask(taskId);
                if (const auto *failure = std::get_if<AppError>(&result))
                    error->setText(failure->message);
                else
                    dialog.accept();
            });
    connect(remove, &QPushButton::clicked, &dialog,
            [&, this]
            {
                QMessageBox box(
                    QMessageBox::Warning, QStringLiteral("删除任务"),
                    QStringLiteral("任务及其他任务对它的依赖将移除；关联主题保留。可撤销。"),
                    QMessageBox::NoButton, &dialog);
                auto *yes = box.addButton(QStringLiteral("删除"), QMessageBox::AcceptRole);
                auto *no = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
                box.setDefaultButton(no);
                box.exec();
                if (box.clickedButton() != yes)
                    return;
                const auto result = TaskCommands(m_commands, m_doc).removeTask(taskId);
                if (const auto *failure = std::get_if<AppError>(&result))
                    error->setText(failure->message);
                else
                    dialog.accept();
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&, this]
            {
                if (title->text() != initial.title)
                {
                    error->setText(
                        QStringLiteral("名称尚未应用，请先点击名称下方按钮，或恢复原名称。"));
                    return;
                }
                Task next = initial;
                next.owner = owner->text();
                next.progress = progress->value();
                next.milestone = milestone->isChecked();
                next.startDate =
                    scheduled->isChecked() ? std::optional<QDate>(start->date()) : std::nullopt;
                next.endDate =
                    scheduled->isChecked() ? std::optional<QDate>(end->date()) : std::nullopt;
                next.predecessorIds.clear();
                for (int i = 0; i < predecessors->count(); ++i)
                    if (predecessors->item(i)->checkState() == Qt::Checked)
                        next.predecessorIds.append(
                            predecessors->item(i)->data(Qt::UserRole).toString());
                if (QSet<ID>(next.predecessorIds.cbegin(), next.predecessorIds.cend()) ==
                    QSet<ID>(initial.predecessorIds.cbegin(), initial.predecessorIds.cend()))
                    next.predecessorIds = initial.predecessorIds;
                Project before, after;
                before.tasks = {initial};
                after.tasks = {next};
                if (ProjectJson::toBytes(before) == ProjectJson::toBytes(after))
                {
                    dialog.accept();
                    return;
                }
                const auto result = TaskCommands(m_commands, m_doc).updateTask(next);
                if (const auto *failure = std::get_if<AppError>(&result))
                {
                    error->setText(failure->message);
                    return;
                }
                dialog.accept();
            });
    layout->addWidget(buttons);
    dialog.resize(580, 600);
    dialog.exec();
}

void MainWindow::manageReviewCards(ID cardId)
{
    QDialog dialog(this);
    dialog.setObjectName("reviewCardsDialog");
    dialog.setWindowTitle(QStringLiteral("复习卡片（离线）"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *filter = new QComboBox(&dialog);
    filter->setObjectName("reviewStatusFilter");
    filter->addItem(QStringLiteral("全部状态"), ID{});
    filter->addItem(QStringLiteral("新卡片"), "new");
    filter->addItem(QStringLiteral("已复习"), "review");
    filter->addItem(QStringLiteral("已掌握"), "mastered");
    auto *list = new QListWidget(&dialog);
    list->setObjectName("reviewCards");
    list->setMaximumHeight(140);
    auto *notice = new QLabel(QStringLiteral("来源变化需先更新或保留自定义；来源缺失可转独立卡片。"
                                             "每个操作立即写入，可在主窗口撤销。"),
                              &dialog);
    notice->setWordWrap(true);
    auto *page = new ReviewPage(&dialog);
    auto *error = new QLabel(&dialog);
    error->setObjectName("reviewError");
    error->setWordWrap(true);
    layout->addWidget(notice);
    layout->addWidget(filter);
    layout->addWidget(list);
    auto *reviewScroll = new QScrollArea(&dialog);
    reviewScroll->setObjectName("reviewScroll");
    reviewScroll->setWidgetResizable(true);
    reviewScroll->setWidget(page);
    layout->addWidget(reviewScroll, 1);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    const auto button = [buttons](const QString &title, const char *name)
    {
        auto *item = buttons->addButton(title, QDialogButtonBox::ActionRole);
        item->setObjectName(name);
        return item;
    };
    auto *start = new QPushButton(QStringLiteral("开始 / 重新开始"), &dialog);
    start->setObjectName("startReview");
    layout->insertWidget(3, start);
    auto *refresh = button(QStringLiteral("更新卡片"), "refreshReviewCard");
    auto *custom = button(QStringLiteral("保留自定义"), "keepReviewCustom");
    auto *standalone = button(QStringLiteral("转独立卡片"), "standaloneReviewCard");
    auto *remove = button(QStringLiteral("移除卡片"), "removeReviewCard");
    layout->addWidget(buttons);
    bool grading = false;
    const auto reload = [&, this]
    {
        const ID selected =
            list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : cardId;
        list->clear();
        for (const auto &card : m_doc->project().cards)
        {
            if (!filter->currentData().toString().isEmpty() &&
                card.status != filter->currentData().toString())
                continue;
            const QString state = ReviewService::detectSourceState(card, m_doc->project());
            auto *item = new QListWidgetItem(
                QStringLiteral("%1 · %2 · %3")
                    .arg(card.front,
                         card.status == "new"        ? QStringLiteral("新卡片")
                         : card.status == "mastered" ? QStringLiteral("已掌握")
                                                     : QStringLiteral("已复习"),
                         card.mode == "custom" ? QStringLiteral("独立/自定义快照")
                         : state == "ok"       ? QStringLiteral("来源正常")
                         : state == "changed"  ? QStringLiteral("来源已变")
                                               : QStringLiteral("来源缺失")),
                list);
            item->setData(Qt::UserRole, card.id);
            if (card.id == selected)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
    };
    const auto begin = [&, this]
    {
        QVector<ReviewCard> cards;
        for (const auto &card : m_doc->project().cards)
            if ((filter->currentData().toString().isEmpty() ||
                 filter->currentData().toString() == card.status) &&
                (card.mode == "custom" ||
                 ReviewService::detectSourceState(card, m_doc->project()) == "ok"))
                cards.append(card);
        page->setCards(cards);
        error->setText(
            cards.isEmpty()
                ? QStringLiteral("没有可复习的卡片；来源变化的卡片需先处理，空答案不能直接建卡。")
                : QString());
    };
    connect(filter, &QComboBox::currentIndexChanged, &dialog,
            [&]
            {
                reload();
                page->setCards({});
            });
    connect(m_doc, &ProjectDocument::changed, &dialog,
            [&]
            {
                reload();
                if (!grading)
                    page->setCards({});
            });
    connect(start, &QPushButton::clicked, &dialog, begin);
    connect(page, &ReviewPage::cardGraded, &dialog,
            [&, this](const ID &id, ReviewMark mark)
            {
                for (const auto &card : m_doc->project().cards)
                    if (card.id == id)
                        if (card.mode != "custom" &&
                            ReviewService::detectSourceState(card, m_doc->project()) != "ok")
                        {
                            error->setText(QStringLiteral("来源已变化，请先处理后重新开始。"));
                            page->setCards({});
                            return;
                        }
                grading = true;
                const auto result = ReviewCommands(m_commands, m_doc).gradeCard(id, mark);
                grading = false;
                if (const auto *failure = std::get_if<AppError>(&result))
                {
                    error->setText(failure->message);
                    page->setCards({});
                }
            });
    const auto mutate = [&, this](int action)
    {
        if (!list->currentItem())
            return;
        const ID id = list->currentItem()->data(Qt::UserRole).toString();
        ReviewCommands commands(m_commands, m_doc);
        const auto result = action == 0   ? commands.refreshFromSource(id)
                            : action == 1 ? commands.keepCustom(id)
                            : action == 2 ? commands.makeStandalone(id)
                                          : commands.removeCard(id);
        if (const auto *failure = std::get_if<AppError>(&result))
            error->setText(failure->message);
        else
            error->clear();
    };
    connect(refresh, &QPushButton::clicked, &dialog, [=] { mutate(0); });
    connect(custom, &QPushButton::clicked, &dialog, [=] { mutate(1); });
    connect(standalone, &QPushButton::clicked, &dialog, [=] { mutate(2); });
    connect(remove, &QPushButton::clicked, &dialog, [=] { mutate(3); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    list->setMaximumHeight(100);
    reload();
    begin();
    dialog.resize(800, qMin(650, screen()->availableGeometry().height() - 70));
    dialog.exec();
}

void MainWindow::locateIssue(ID sheetId, ID targetId)
{
    for (const auto &task : m_doc->project().tasks)
        if (task.id == targetId)
        {
            const ID id = task.id;
            showGanttView(true);
            selectTask(id);
            if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage))
                for (auto *item : gantt->scene()->items())
                    if (item->data(GanttView::kBarDataRole).toString() == id)
                    {
                        gantt->ensureVisible(item, 20, 20);
                        break;
                    }
            if (auto *dock = findChild<QDockWidget *>("taskDock"))
                dock->show();
            return;
        }
    if (!sheetId.isEmpty() && !m_doc->setActiveSheet(sheetId))
    {
        statusBar()->showMessage(QStringLiteral("问题来源的工作表已不存在，请重新检查。"), 5000);
        return;
    }
    m_centralStack->setCurrentIndex(0);
    if (m_nodeItems.contains(targetId))
    {
        selectNode(targetId);
        return;
    }
    if (const auto *tree = activeTreeSheet(); tree && tree->nodes.contains(targetId))
    {
        selectNode(targetId);
        return;
    }
    for (const auto &card : m_doc->project().cards)
        if (card.id == targetId)
        {
            selectNode(card.nodeId);
            return;
        }
    statusBar()->showMessage(
        QStringLiteral("已定位工作表；目标为工作表级建议、卡片或已删除的来源，请按问题提示处理。"),
        8000);
}
} // namespace lingya
