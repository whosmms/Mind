#include "app/MainWindow.h"

#include "layout/FishboneLayout.h"
#include "layout/TreeLayout.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>

#include "app/LoginDialog.h"
#include "app/RecoveryDialog.h"
#include "app/UserFilesDialog.h"
#include "charts/GanttView.h"
#include "charts/TaskTableModel.h"
#include "commands/CommandService.h"
#include "commands/NodeCommands.h"
#include "commands/SheetCommands.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "graphics/NodeItem.h"
#include "services/FileService.h"
#include "services/LocalNameRules.h"
#include "services/RecoveryService.h"
#include "services/SessionService.h"
#include "services/SettingsService.h"
#include "services/UserFileService.h"
#include <QItemSelectionModel>
#include <QTableView>

namespace lingya {


namespace {
NodeItem *owningNode(QGraphicsItem *item)
{
    for (; item; item = item->parentItem())
        if (auto *node = dynamic_cast<NodeItem *>(item))
            return node;
    return nullptr;
}
// 大纲拖放只发出移动意图，不调用Qt内部的模型搬移；命令提交后统一重建视图。
class TopicOutline final : public QTreeWidget
{
  public:
    using QTreeWidget::QTreeWidget;
    std::function<void(ID, ID, ID)> moveRequested;

  protected:
    void dropEvent(QDropEvent *event) override
    {
        auto *target = itemAt(event->position().toPoint());
        auto *source = currentItem();
        if (event->source() != this || !target || !source || target == source)
        {
            event->ignore();
            return;
        }
        QTreeWidgetItem *parent = target;
        ID before;
        if (dropIndicatorPosition() == AboveItem || dropIndicatorPosition() == BelowItem)
        {
            parent = target->parent();
            if (!parent)
            {
                event->ignore();
                return;
            }
            int index = parent->indexOfChild(target);
            if (dropIndicatorPosition() == BelowItem)
                ++index;
            if (index < parent->childCount())
                before = parent->child(index)->data(0, Qt::UserRole).toString();
        }
        const ID node = source->data(0, Qt::UserRole).toString();
        const ID parentId = parent->data(0, Qt::UserRole).toString();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        // 延后提交，避免在dropEvent栈内重建并销毁source/target。
        QTimer::singleShot(0, this,
                           [this, node, parentId, before]
                           {
                               if (moveRequested)
                                   moveRequested(node, parentId, before);
                           });
    }
};

// 管理窗口的“应用”每次提交一条命令；关闭不回滚已应用内容，撤销走主窗口历史。
struct EditorForm
{
    QDialog dialog;
    QVBoxLayout *layout;
    QListWidget *list;
    QFormLayout *fields;
    QLabel *error;
    QPushButton *fresh, *remove, *apply;
    EditorForm(QWidget *parent, const QString &name, const QString &title) : dialog(parent)
    {
        dialog.setObjectName(name);
        dialog.setWindowTitle(title);
        dialog.resize(560, qMin(620, parent->screen()->availableGeometry().height() - 80));
        layout = new QVBoxLayout(&dialog);
        auto *hint = new QLabel(
            QStringLiteral("每次应用立即保存到工程，可在主窗口撤销。关闭窗口保留已应用内容。"),
            &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        list = new QListWidget(&dialog);
        list->setObjectName("records");
        list->setMaximumHeight(150);
        layout->addWidget(list);
        auto *content = new QWidget(&dialog);
        fields = new QFormLayout(content);
        auto *scroll = new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        layout->addWidget(scroll, 1);
        error = new QLabel(&dialog);
        error->setObjectName("editorError");
        error->setTextFormat(Qt::PlainText);
        error->setWordWrap(true);
        layout->addWidget(error);
        auto *row = new QHBoxLayout;
        fresh = new QPushButton(QStringLiteral("新建"), &dialog);
        fresh->setObjectName("newRecord");
        remove = new QPushButton(QStringLiteral("删除所选"), &dialog);
        remove->setObjectName("removeRecord");
        apply = new QPushButton(QStringLiteral("应用"), &dialog);
        apply->setObjectName("applyRecord");
        auto *close = new QPushButton(QStringLiteral("关闭"), &dialog);
        for (auto *button : {fresh, remove, apply, close})
        {
            button->setAutoDefault(false);
            row->addWidget(button);
        }
        layout->addLayout(row);
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    }
};

void showNotice(QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text)
{
    QMessageBox box(icon, title, text, QMessageBox::Ok, parent);
    box.button(QMessageBox::Ok)->setText(QStringLiteral("确定"));
    box.exec();
}

// 标准Qt多行输入框：预编辑期间不把Enter当成提交，Shift+Enter保留换行。
class NodeTitleDialog final : public QInputDialog
{
  public:
    NodeTitleDialog(const QString &caption, const QString &initial, QWidget *parent)
        : QInputDialog(parent)
    {
        setWindowTitle(caption);
        setLabelText(QStringLiteral("主题标题（Enter确认，Shift+Enter换行）："));
        setOption(QInputDialog::UsePlainTextEditForTextInput);
        setTextValue(initial);
        setOkButtonText(QStringLiteral("确定"));
        setCancelButtonText(QStringLiteral("取消"));
        resize(440, 240);
        m_editor = findChild<QPlainTextEdit *>();
        if (m_editor != nullptr)
        {
            m_editor->installEventFilter(this);
            m_editor->selectAll();
        }
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_editor && event->type() == QEvent::InputMethod)
            m_preediting = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        if (watched == m_editor && event->type() == QEvent::KeyPress)
        {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            {
                if (m_preediting)
                    return true;
                if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
                {
                    accept();
                    return true;
                }
            }
        }
        return QInputDialog::eventFilter(watched, event);
    }

  private:
    QPlainTextEdit *m_editor = nullptr;
    bool m_preediting = false;
};

// 单标签输入保留真实输入法候选确认，预编辑期间不把Enter当成添加。
class TagNameDialog final : public QInputDialog
{
  public:
    explicit TagNameDialog(QWidget *parent) : QInputDialog(parent)
    {
        setWindowTitle(QStringLiteral("新增标签"));
        setLabelText(QStringLiteral("标签名称："));
        setTextValue(QString());
        setOkButtonText(QStringLiteral("添加"));
        setCancelButtonText(QStringLiteral("取消"));
        m_editor = findChild<QLineEdit *>();
        if (m_editor)
            m_editor->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_editor && event->type() == QEvent::InputMethod)
            m_preediting = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        if (watched == m_editor && event->type() == QEvent::KeyPress && m_preediting)
        {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                return true;
        }
        return QInputDialog::eventFilter(watched, event);
    }

  private:
    QLineEdit *m_editor = nullptr;
    bool m_preediting = false;
};

QFont canvasFont() {
    QFont font(QStringLiteral("Microsoft YaHei"));   // Windows优先微软雅黑（规范§9）
    font.setPixelSize(16);                           // 节点默认字号（规范§4.2）
    return font;
}
}  // namespace

QString MainWindow::s_recoveryRootForTest;
QString MainWindow::s_sessionRootForTest;

void MainWindow::setRecoveryRootForTest(const QString& path) {
    s_recoveryRootForTest = path;   // 空=恢复默认QStandardPaths路径（构造时生效）
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_doc(new ProjectDocument(this)),
      m_commands(new CommandService(m_doc, this)) {
    setWindowTitle(QStringLiteral("灵芽思维"));
    resize(1280, 800);   // 推荐最小窗口（规范§9.1）

    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setObjectName(QStringLiteral("topicPreview"));
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);   // 双击画布节点重命名
    // 中央区两页：页0思维导图画布，页1甘特图（M3丙接入，见setGanttView）
    m_centralStack = new QStackedWidget(this);
    m_centralStack->addWidget(m_view);
    auto *ganttPlaceholder = new QLabel(QStringLiteral("打开甘特图后显示工程中的排期任务"), this);
    ganttPlaceholder->setAlignment(Qt::AlignCenter);
    m_ganttPage = ganttPlaceholder;
    m_centralStack->addWidget(m_ganttPage);
    setCentralWidget(m_centralStack);

    auto *outlineDock = new QDockWidget(QStringLiteral("主题大纲"), this);
    outlineDock->setObjectName(QStringLiteral("outlineDock"));
    auto *outlinePanel = new QWidget(outlineDock);
    auto *outlineLayout = new QVBoxLayout(outlinePanel);
    outlineLayout->setContentsMargins(0, 0, 0, 0);
    m_tagFilter = new QComboBox(outlinePanel);
    m_tagFilter->setObjectName(QStringLiteral("outlineTagFilter"));
    m_tagFilter->setAccessibleName(QStringLiteral("标签筛选"));
    m_tagFilter->setToolTip(QStringLiteral(
        "仅筛选大纲，保留匹配主题及祖先；画布保持完整。选择筛选外的画布主题时恢复全部。"));
    outlineLayout->addWidget(m_tagFilter);
    auto *outline = new TopicOutline(outlinePanel);
    m_outline = outline;
    outline->setDragDropMode(QAbstractItemView::InternalMove);
    outline->setDefaultDropAction(Qt::MoveAction);
    outline->moveRequested = [this](const ID &node, const ID &parentId, const ID &before)
    { moveSelection(node, parentId, before); };
    m_outline->setObjectName(QStringLiteral("topicOutline"));
    m_outline->setHeaderLabel(QStringLiteral("主题（Tab新增子主题，F2改名）"));
    m_outline->setMinimumWidth(280);
    m_outline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outline->installEventFilter(this);
    outlineLayout->addWidget(m_outline);
    outlineDock->setWidget(outlinePanel);
    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    connect(m_outline, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *)
            {
                if (current == nullptr)
                    return;
                selectNode(current->data(0, Qt::UserRole).toString());
            });
    connect(m_outline, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { renameSelectedNode(); });
    connect(m_tagFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { applyOutlineTagFilter(); });

    buildMenus();
    connect(m_doc, &ProjectDocument::changed, this, &MainWindow::onDocumentChanged);

    // 恢复快照（规范§11.3、原设计§8）：30秒定时，仅脏文档且内容变化时写一份；
    // 每工程保留最近5份成功快照由RecoveryService写入后修剪
    m_recoveryRoot = s_recoveryRootForTest.isEmpty() ? RecoveryService::defaultRoot()
                                                     : s_recoveryRootForTest;
    m_sessionRoot = s_sessionRootForTest.isEmpty()
                        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        : s_sessionRootForTest;
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setInterval(Limits::kRecoverySnapshotIntervalMs);
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::writeRecoverySnapshot);
    m_recoveryTimer->start();

    loadNewDocument(ProjectFactory::createMindmap(QStringLiteral("未命名导图")));

    // 启动发现快照先预览，由用户决定采用（规范§11.3）；延后到事件循环，
    // 命令行打开（main.cpp在show前调用openFile）完成后窗口已装配再询问
    QTimer::singleShot(0, this, &MainWindow::offerRecoverySnapshots);
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(QStringLiteral("新建(&N)"), QKeySequence::New, this,
                        &MainWindow::newProject)->setObjectName(QStringLiteral("newProject"));
    fileMenu->addAction(QStringLiteral("打开(&O)..."), QKeySequence::Open, this,
                        &MainWindow::openProject);
    fileMenu->addAction(QStringLiteral("保存(&S)"), QKeySequence::Save, this,
                        [this] { saveProject(false); });
    fileMenu->addAction(QStringLiteral("另存为(&A)..."), QKeySequence::SaveAs, this,
                        [this] { saveProject(true); });
    m_loginAction = fileMenu->addAction(QStringLiteral("登录(&I)..."), this,
                                        &MainWindow::showLoginDialog);
    m_logoutAction = fileMenu->addAction(QStringLiteral("退出登录(&I)"), this,
                                         &MainWindow::logoutSession);
    m_ownedFilesAction = fileMenu->addAction(QStringLiteral("我的文件(&M)..."), this,
                                             &MainWindow::showOwnedFilesDialog);
    updateSessionActions();
    m_recentMenu = fileMenu->addMenu(QStringLiteral("最近文件(&R)"));
    rebuildRecentMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    m_undoAction = editMenu->addAction(QStringLiteral("撤销(&U)"), QKeySequence::Undo, this,
                                       [this] { m_commands->undo(); });
    m_redoAction = editMenu->addAction(QStringLiteral("重做(&R)"), this,
                                       [this] { m_commands->redo(); });
    // 统一规范§14.1：Ctrl+Shift+Z为主，Ctrl+Y为别名（Windows平台QKeySequence::Redo即Ctrl+Y）
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    editMenu->addSeparator();
    auto *renameAction = editMenu->addAction(QStringLiteral("重命名主题(&M)"), QKeySequence("F2"),
                                             this, &MainWindow::renameSelectedNode);
    renameAction->setObjectName(QStringLiteral("renameTopic"));
    auto *childAction = editMenu->addAction(QStringLiteral("新增子主题(&C)"), this,
                                            &MainWindow::addChildToSelection);
    childAction->setObjectName(QStringLiteral("addChildTopic"));
    m_siblingAction = editMenu->addAction(QStringLiteral("新增同级主题"), this,
                                          &MainWindow::addSiblingToSelection);
    m_siblingAction->setObjectName(QStringLiteral("addSiblingTopic"));
    m_deleteAction =
        editMenu->addAction(QStringLiteral("删除主题及子主题"), this, &MainWindow::deleteSelection);
    m_deleteAction->setObjectName(QStringLiteral("deleteTopic"));
    auto *toolbar = addToolBar(QStringLiteral("主题编辑"));
    toolbar->setObjectName(QStringLiteral("editorToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(childAction);
    toolbar->addAction(m_siblingAction);
    toolbar->addAction(renameAction);
    toolbar->addAction(m_deleteAction);
    m_propertiesAction = editMenu->addAction(QStringLiteral("主题属性…"), this,
                                              &MainWindow::editSelectedNodeProperties);
    m_propertiesAction->setObjectName(QStringLiteral("editNodeProperties"));
    m_styleAction =
        editMenu->addAction(QStringLiteral("节点样式…"), this, &MainWindow::editSelectedNodeStyle);
    m_styleAction->setObjectName(QStringLiteral("editNodeStyle"));
    m_openLinkAction = editMenu->addAction(QStringLiteral("打开主题链接"), this,
                                            &MainWindow::openSelectedNodeLink);
    m_openLinkAction->setObjectName(QStringLiteral("openNodeLink"));
    toolbar->addSeparator();
    toolbar->addAction(m_propertiesAction);
    toolbar->addAction(m_styleAction);
    m_tagsAction =
        editMenu->addAction(QStringLiteral("主题标签…"), this, &MainWindow::editSelectedNodeTags);
    m_tagsAction->setObjectName(QStringLiteral("editNodeTags"));
    toolbar->addAction(m_tagsAction);
    toolbar->addAction(m_openLinkAction);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(QStringLiteral("思维导图(&M)"), this, [this] { m_centralStack->setCurrentIndex(0); });
    auto *ganttAction =
        viewMenu->addAction(QStringLiteral("甘特图(&G)"), this, [this] { showGanttView(); });
    ganttAction->setObjectName(QStringLiteral("showGantt"));
    viewMenu->addAction(
        findChild<QDockWidget *>(QStringLiteral("outlineDock"))->toggleViewAction());
    viewMenu->addSeparator();
    auto *zoomIn = viewMenu->addAction(QStringLiteral("放大"), this,
                                       [this] { setCanvasZoom(m_view->transform().m11() * 1.2); });
    auto *zoomOut = viewMenu->addAction(QStringLiteral("缩小"), this,
                                        [this] { setCanvasZoom(m_view->transform().m11() / 1.2); });
    auto *fit = viewMenu->addAction(QStringLiteral("适应全部"), this, &MainWindow::fitCanvas);
    auto *locate =
        viewMenu->addAction(QStringLiteral("定位所选主题"), this, &MainWindow::focusSelectedNode);
    locate->setObjectName(QStringLiteral("locateTopic"));
    zoomIn->setObjectName(QStringLiteral("zoomIn"));
    zoomOut->setObjectName(QStringLiteral("zoomOut"));
    fit->setObjectName(QStringLiteral("fitTopic"));
    toolbar->addSeparator();
    toolbar->addAction(zoomIn);
    toolbar->addAction(zoomOut);
    toolbar->addAction(fit);
    toolbar->addAction(locate);
    buildEditorActions(editMenu);
    buildFlowActions(editMenu);
    buildOfflineActions(viewMenu);
}

void MainWindow::showGanttView(bool forTaskLocation)
{
    m_ganttTaskLocation = forTaskLocation;
    if (qobject_cast<QLabel *>(m_ganttPage) != nullptr)
    {
        auto *gantt = new GanttView(this);
        gantt->setTasks(visibleGanttTasks());
        setGanttView(gantt);
    }
    else
    {
        if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage))
            gantt->setTasks(visibleGanttTasks());
        m_centralStack->setCurrentIndex(1);
    }
    if (!m_selectedTaskId.isEmpty())
        selectTask(m_selectedTaskId);
    updateStatusLine();
    if (m_doc->project().tasks.isEmpty())
        statusBar()->showMessage(QStringLiteral("暂无任务。打开含排期任务的工程后可查看时间轴。"));
    else if (!hasVisibleGanttTasks())
        statusBar()->showMessage(
            QStringLiteral("当前筛选无任务（工程共 %1 项）").arg(m_doc->project().tasks.size()));
}

void MainWindow::setCanvasZoom(qreal zoom)
{
    zoom = qBound(0.25, zoom, 2.0);
    m_view->setTransform(QTransform::fromScale(zoom, zoom));
}

void MainWindow::fitCanvas()
{
    if (m_scene->items().isEmpty())
        return;
    m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-24, -24, 24, 24), Qt::KeepAspectRatio);
    setCanvasZoom(m_view->transform().m11());
    m_view->centerOn(m_scene->itemsBoundingRect().center());
}

void MainWindow::setGanttView(QGraphicsView* view) {
    // 同一指针重复调用或误传画布/空指针：无操作（评审指出重复调用会deleteLater后悬空）
    if (view == nullptr || view == m_ganttPage || view == m_view) return;
    view->viewport()->installEventFilter(this);
    if (auto *gantt = qobject_cast<GanttView *>(view))
        connect(gantt, &GanttView::taskDatesEdited, this,
                [this, gantt](const ID &id, const QDate &start, const QDate &end)
                {
                    for (const auto &task : m_doc->project().tasks)
                        if (task.id == id)
                        {
                            Task changed = task;
                            changed.startDate = start;
                            changed.endDate = end;
                            const auto result = TaskCommands(m_commands, m_doc).updateTask(changed);
                            if (const auto *error = std::get_if<AppError>(&result))
                            {
                                gantt->setTasks(visibleGanttTasks());
                                showNotice(this, QMessageBox::Warning,
                                           QStringLiteral("无法修改排期"), error->message);
                            }
                            return;
                        }
                    gantt->setTasks(visibleGanttTasks());
                });
    // 替换甘特页内容：移除旧页（占位或旧视图）后插入新视图，页序保持不变
    const int ganttIndex = m_centralStack->indexOf(m_ganttPage);
    m_centralStack->removeWidget(m_ganttPage);
    m_ganttPage->deleteLater();
    m_ganttPage = view;
    m_centralStack->insertWidget(ganttIndex, view);
    m_centralStack->setCurrentIndex(ganttIndex);   // 装入后直接切到甘特页
}

void MainWindow::offerRecoverySnapshots() {
    if (m_recoveryDialog != nullptr) return;   // 已在询问，不叠加对话框
    // 启动扫描全部工程的快照：未命名工程也有UUID，同样可恢复（原设计§8）
    const QVector<RecoveryService::SnapshotInfo> snapshots = RecoveryService::listAll(m_recoveryRoot);
    if (snapshots.isEmpty()) return;
    m_recoveryDialog = new RecoveryDialog(m_recoveryRoot, snapshots, this);
    connect(m_recoveryDialog, &QDialog::finished, this, [this](int) {
        // 只处理当前对话框的finished；deleteLater前仍存活的旧对话框再次发信号时忽略
        RecoveryDialog* dialog = m_recoveryDialog;
        m_recoveryDialog = nullptr;
        if (dialog == nullptr || dialog != sender()) return;
        const RecoveryDialog::Decision decision = dialog->decision();
        RecoveryService::SnapshotInfo info;
        const bool hasSelection = dialog->selectedSnapshot(&info);
        const QVector<RecoveryService::SnapshotInfo> listed = dialog->snapshots();
        dialog->deleteLater();
        if (decision == RecoveryDialog::Decision::Adopt && hasSelection) {
            adoptRecoverySnapshot(info);
        } else if (decision == RecoveryDialog::Decision::IgnoreAll) {
            // 用户明确放弃全部未保存副本：只清理应用自建的快照（规范§11.3）
            for (const RecoveryService::SnapshotInfo& snapshot : listed)
                RecoveryService::clear(m_recoveryRoot, snapshot.projectId);
        }
        // Cancel（稍后再问）：快照原样保留在磁盘，下次启动再询问
    });
    m_recoveryDialog->open();   // 模态但不阻塞启动事件流（区别于exec()）
}

bool MainWindow::adoptRecoverySnapshot(const RecoveryService::SnapshotInfo& info) {
    const auto loaded = RecoveryService::load(m_recoveryRoot, info.projectId, info.snapshotId);
    if (std::holds_alternative<AppError>(loaded)) {
        showNotice(this, QMessageBox::Critical, QStringLiteral("无法采用快照"),
                   std::get<AppError>(loaded).message);
        return false;
    }
    const Project recovered = std::get<Project>(loaded);
    // 路径等会话状态先于changed更新，避免采用时仍显示原文件名；失败则完整还原。
    const QString previousPath = m_filePath;
    const auto previousStamp = m_fileStamp;
    const QString previousNote = m_recoveryNote;
    m_filePath.clear();
    m_fileStamp.reset();
    m_recoveryNote.clear();
    // 恢复采用走命令事务（统一规范§7.1/§12）：整工程替换作为一条可撤销历史压入唯一文档，
    // 不绕过CommandService直接replaceProject；采用后可撤销回采用前状态
    const auto applied = m_commands->applyEdit(
        QStringLiteral("恢复快照"), [recovered](Project& project) { project = recovered; });
    if (std::holds_alternative<AppError>(applied)) {
        m_filePath = previousPath;
        m_fileStamp = previousStamp;
        m_recoveryNote = previousNote;
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法采用快照"),
                   std::get<AppError>(applied).message);
        return false;
    }
    // 采用只写入内存，不清理磁盘副本；否则另存前再次崩溃会失去恢复保障。
    // 采用后的内容尚未落盘（快照≠正式保存），此刻崩溃仍须可再次恢复；
    // 30秒定时写盘与最新快照字节比对去重，不会产生重复；另存成功或用户放弃时才清理
    // （规范§11.3快照保护未保存工作）；取消保存、保存失败和撤销均保留副本。
    return true;   // applyEdit内部已notifyChanged，画布与标题栏已按恢复内容刷新
}

void MainWindow::writeRecoverySnapshot() {
    // 干净状态无需快照：磁盘已有正式文件或工程刚打开未编辑（快照保护的是未保存工作）
    if (!m_doc->isModified() || m_doc->project().id.isEmpty()) return;
    const auto result = RecoveryService::writeSnapshot(m_recoveryRoot, m_doc->project());
    if (std::holds_alternative<RecoveryService::SnapshotWrite>(result)) {
        const auto& write = std::get<RecoveryService::SnapshotWrite>(result);
        if (write.written)   // 内容未变化被跳过时备注保持上一次的时间
            m_recoveryNote = QStringLiteral("恢复快照 %1")
                                 .arg(QDateTime::currentDateTime().toString(
                                     QStringLiteral("HH:mm:ss")));
    } else {
        // 自动保存失败不弹窗打断编辑，状态栏提示；下一周期内容变化时重试
        m_recoveryNote = QStringLiteral("恢复快照写入失败");
    }
    updateStatusLine();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage);
        gantt && watched == gantt->viewport() && event->type() == QEvent::MouseButtonPress)
    {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        for (auto *item = gantt->itemAt(mouse->position().toPoint()); item;
             item = item->parentItem())
            if (item->data(GanttView::kBarDataRole).isValid())
            {
                // 鼠标按下仍属于甘特拖动；此时切表会把接收后续移动/释放的视图隐藏。
                // 只同步选中，关联主题的跳转由任务表中的独立按钮触发。
                selectTask(item->data(GanttView::kBarDataRole).toString());
                break;
            }
    }
    if (handleFlowEvent(watched, event))
        return true;
    if (watched == m_view->viewport() && event->type() == QEvent::Leave)
        m_dragNodeId.clear(); // 离开画布即取消，不能让下次释放沿用旧的拖动源。
    if (watched == m_view->viewport() && event->type() == QEvent::MouseMove &&
        !m_dragNodeId.isEmpty())
    {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (!(mouse->buttons() & Qt::LeftButton))
        {
            m_dragNodeId.clear();
            return QMainWindow::eventFilter(watched, event);
        }
        if ((mouse->position().toPoint() - m_dragStart).manhattanLength() >=
            QApplication::startDragDistance())
            statusBar()->showMessage(
                QStringLiteral("松开到目标主题上，将作为其子主题；同级排序可在大纲拖到主题前后。"));
        return true;
    }
    if (watched == m_view->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick))
    {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const QPointF position = m_view->mapToScene(mouse->position().toPoint());
            auto *hit = m_scene->itemAt(position, QTransform());
            if (hit && hit->data(kTaskBadgeRole).isValid())
            {
                m_dragNodeId.clear();
                editTask(hit->data(kTaskBadgeRole).toString());
                return true;
            }
            if (const auto *item = owningNode(hit))
            {
                const ID nodeId = item->nodeId();
                m_dragNodeId = nodeId;
                m_dragStart = mouse->position().toPoint();
                selectNode(nodeId);
                m_view->setFocus();
                if (event->type() == QEvent::MouseButtonDblClick)
                {
                    m_dragNodeId.clear();
                    renameSelectedNode();
                }
                return true;
            }
        }
    }
    if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonRelease)
    {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const ID dragged = m_dragNodeId;
        m_dragNodeId.clear();
        if (mouse->button() == Qt::LeftButton && !dragged.isEmpty() &&
            (mouse->position().toPoint() - m_dragStart).manhattanLength() >=
                QApplication::startDragDistance())
        {
            if (const auto *target = owningNode(
                    m_scene->itemAt(m_view->mapToScene(mouse->position().toPoint()), QTransform())))
                if (target->nodeId() != dragged)
                {
                    moveSelection(dragged, target->nodeId());
                    return true;
                }
        }
        syncCanvasSelection();
    }
    // 只处理大纲/画布，文本对话框和输入法预编辑不经过此入口。
    if ((watched == m_outline || watched == m_view || watched == m_view->viewport()) &&
        event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && !m_cutNodeId.isEmpty())
        {
            m_cutNodeId.clear();
            m_cutToken.clear();
            syncCanvasSelection();
            return true;
        }
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier)
        {
            addChildToSelection();
            return true;
        }
        const TreeSheet *sheet = activeTreeSheet();
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && sheet != nullptr)
        {
            if (m_selectedNodeId == sheet->rootId)
                addChildToSelection();
            else
                addSiblingToSelection();
            return true;
        }
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier)
        {
            deleteSelection();
            return true;
        }
    }
    if (watched == m_view->viewport() && event->type() == QEvent::Wheel)
    {
        const auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->angleDelta().y() != 0)
            setCanvasZoom(m_view->transform().m11() *
                          (wheel->angleDelta().y() > 0 ? 1.2 : 1 / 1.2));
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::editSelectedNodeProperties()
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    const ID sheetId = sheet->id;
    const ID nodeId = m_selectedNodeId;
    const TreeNode node = sheet->nodes.value(nodeId);
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("nodePropertiesDialog"));
    dialog.setWindowTitle(QStringLiteral("主题属性"));
    dialog.resize(520, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(node.title, &dialog);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *form = new QFormLayout;
    auto *icons = new QComboBox(&dialog);
    icons->setObjectName(QStringLiteral("nodeIcon"));
    icons->addItem(QStringLiteral("无图标"), QString());
    const QStringList ids{QStringLiteral("idea"), QStringLiteral("star"), QStringLiteral("check")};
    const QStringList names{QStringLiteral("灵感"), QStringLiteral("星标"), QStringLiteral("完成")};
    for (int i = 0; i < ids.size(); ++i)
        icons->addItem(NodeItem::iconForId(ids.at(i)), names.at(i), ids.at(i));
    const QString currentIcon = node.iconId.value_or(QString());
    if (icons->findData(currentIcon) < 0)
        icons->addItem(QStringLiteral("当前图标（暂不支持预览）"), currentIcon);
    icons->setCurrentIndex(icons->findData(currentIcon));
    form->addRow(QStringLiteral("图标"), icons);
    auto *link = new QLineEdit(node.link.value_or(QString()), &dialog);
    link->setObjectName(QStringLiteral("nodeLink"));
    link->setPlaceholderText(QStringLiteral("https:// 或 http:// 开头；留空清除"));
    form->addRow(QStringLiteral("链接"), link);
    auto *note = new QPlainTextEdit(node.note, &dialog);
    note->setObjectName(QStringLiteral("nodeNote"));
    note->setPlaceholderText(QStringLiteral("独立备注，可输入多行；留空清除"));
    form->addRow(QStringLiteral("备注"), note);
    auto *styleContent = new QWidget(&dialog);
    styleContent->setLayout(form);
    auto *styleScroll = new QScrollArea(&dialog);
    styleScroll->setWidgetResizable(true);
    styleScroll->setWidget(styleContent);
    layout->addWidget(styleScroll, 1);
    auto *error = new QLabel(&dialog);
    error->setObjectName(QStringLiteral("nodeMetadataError"));
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&]
            {
                NodeMetaUpdate patch;
                patch.note = note->toPlainText();
                patch.link = link->text();
                patch.iconId = icons->currentData().toString();
                NodeCommands nodes(m_commands, m_doc);
                const auto result = nodes.updateNodeMeta(sheetId, nodeId, patch);
                if (const auto *failure = std::get_if<AppError>(&result))
                {
                    error->setText(failure->message);
                    return;
                }
                dialog.accept();
            });
    dialog.exec();
}

void MainWindow::editSelectedNodeTags()
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    const ID sheetId = sheet->id;
    const ID nodeId = m_selectedNodeId;
    const QStringList original = sheet->nodes.value(nodeId).tags;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("nodeTagsDialog"));
    dialog.setWindowTitle(QStringLiteral("主题标签"));
    dialog.resize(440, 340);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(
        QStringLiteral("每个主题最多%1个标签，每个标签最多%2个字符。应用后一起生效，取消不修改。")
            .arg(Limits::kMaxTags)
            .arg(Limits::kTagMaxChars),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *tags = new QListWidget(&dialog);
    tags->setObjectName(QStringLiteral("nodeTags"));
    tags->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const auto append = [tags](const QString &tag)
    {
        auto *item = new QListWidgetItem(tag, tags);
        item->setData(Qt::UserRole, tag); // 独立保存每项，不用逗号或换行拆解旧标签。
    };
    for (const QString &tag : original)
        append(tag);
    layout->addWidget(tags);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("新增标签"), &dialog);
    auto *remove = new QPushButton(QStringLiteral("移除所选"), &dialog);
    auto *clear = new QPushButton(QStringLiteral("清空标签"), &dialog);
    add->setObjectName(QStringLiteral("addNodeTag"));
    remove->setObjectName(QStringLiteral("removeNodeTag"));
    clear->setObjectName(QStringLiteral("clearNodeTags"));
    for (auto *button : {add, remove, clear})
    {
        button->setAutoDefault(false);
        actions->addWidget(button);
    }
    layout->addLayout(actions);
    auto *error = new QLabel(&dialog);
    error->setObjectName(QStringLiteral("nodeTagsError"));
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(add, &QPushButton::clicked, &dialog,
            [&]
            {
                TagNameDialog input(&dialog);
                if (input.exec() != QDialog::Accepted)
                    return;
                const QString text = input.textValue().trimmed();
                if (text.isEmpty())
                {
                    error->setText(QStringLiteral("标签不能为空白。"));
                    return;
                }
                for (int index = 0; index < tags->count(); ++index)
                    if (tags->item(index)->data(Qt::UserRole).toString().trimmed() == text)
                    {
                        error->setText(QStringLiteral("该标签已存在。"));
                        return;
                    }
                append(text);
                error->clear();
            });
    connect(remove, &QPushButton::clicked, &dialog,
            [=]
            {
                for (auto *item : tags->selectedItems())
                    delete tags->takeItem(tags->row(item));
                error->clear();
            });
    connect(clear, &QPushButton::clicked, &dialog,
            [=]
            {
                tags->clear();
                error->clear();
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&]
            {
                QStringList values;
                for (int index = 0; index < tags->count(); ++index)
                    values.append(tags->item(index)->data(Qt::UserRole).toString());
                NodeMetaUpdate update;
                if (values != original)
                    update.tags = values;
                const auto result =
                    NodeCommands(m_commands, m_doc).updateNodeMeta(sheetId, nodeId, update);
                if (const auto *failure = std::get_if<AppError>(&result))
                {
                    error->setText(failure->message);
                    return;
                }
                dialog.accept();
            });
    dialog.exec();
}

void MainWindow::editSelectedNodeStyle()
{
    editNodeStyle(false);
}

void MainWindow::editNodeStyle(bool global)
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    const ID sheetId = sheet->id;
    const ID nodeId = m_selectedNodeId;
    NodeStyleOverride original = sheet->nodes.value(nodeId).style;
    const NodeStyle defaults = sheet->defaultNodeStyle;
    const EdgeStyle edgeDefaults = sheet->defaultEdgeStyle;
    if (global)
    {
        original.fill = defaults.fill;
        original.color = defaults.color;
        original.borderColor = defaults.borderColor;
        original.fontFamily = defaults.fontFamily;
        original.fontSize = defaults.fontSize;
        original.bold = defaults.bold;
        original.shape = defaults.shape;
        original.borderWidth = defaults.borderWidth;
    }
    QDialog dialog(this);
    dialog.setObjectName(global ? QStringLiteral("sheetStyleDialog")
                                : QStringLiteral("nodeStyleDialog"));
    dialog.setWindowTitle(global ? QStringLiteral("工作表默认样式") : QStringLiteral("节点样式"));
    dialog.resize(430, 440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(QStringLiteral("留空或选择“继承”时，使用工作表样式。"), &dialog);
    hint->setWordWrap(true);
    if (global)
        hint->setText(QStringLiteral(
            "工作表默认值影响未覆盖的节点；节点局部覆盖优先。此处修改不清除局部覆盖。"));
    layout->addWidget(hint);
    auto *form = new QFormLayout;
    const auto textField = [&](const QString &name, const QString &label,
                               const std::optional<QString> &value, const QString &fallback)
    {
        auto *edit = new QLineEdit(value.value_or(QString()), &dialog);
        edit->setObjectName(name);
        edit->setPlaceholderText(QStringLiteral("继承：%1").arg(fallback));
        form->addRow(label, edit);
        return edit;
    };
    auto *fill = textField(QStringLiteral("styleFill"), QStringLiteral("填充色（#RRGGBB）"),
                           original.fill, defaults.fill);
    auto *color = textField(QStringLiteral("styleColor"), QStringLiteral("文字色（#RRGGBB）"),
                            original.color, defaults.color);
    auto *border =
        textField(QStringLiteral("styleBorderColor"), QStringLiteral("边框色（#RRGGBB）"),
                  original.borderColor, defaults.borderColor);
    auto *family = textField(
        QStringLiteral("styleFontFamily"), QStringLiteral("字体名称"), original.fontFamily,
        defaults.fontFamily == QStringLiteral("system") ? QStringLiteral("系统字体")
                                                        : defaults.fontFamily);
    const QString initialFamily = family->text();
    bool resetStyle = false;
    auto *size = new QComboBox(&dialog);
    size->setObjectName(QStringLiteral("styleFontSize"));
    size->addItem(QStringLiteral("继承（%1）").arg(defaults.fontSize));
    for (int value = Limits::kFontSizeMin; value <= Limits::kFontSizeMax; ++value)
        size->addItem(QString::number(value), value);
    if (original.fontSize)
        size->setCurrentIndex(size->findData(*original.fontSize));
    form->addRow(QStringLiteral("字号"), size);
    auto *bold = new QComboBox(&dialog);
    bold->setObjectName(QStringLiteral("styleBold"));
    bold->addItem(QStringLiteral("继承（%1）")
                      .arg(defaults.bold ? QStringLiteral("粗体") : QStringLiteral("常规")));
    bold->addItem(QStringLiteral("常规"), false);
    bold->addItem(QStringLiteral("粗体"), true);
    if (original.bold)
        bold->setCurrentIndex(bold->findData(*original.bold));
    form->addRow(QStringLiteral("字重"), bold);
    auto *shape = new QComboBox(&dialog);
    shape->setObjectName(QStringLiteral("styleShape"));
    const QStringList codes{QStringLiteral("roundRect"), QStringLiteral("rect"),
                            QStringLiteral("ellipse"), QStringLiteral("diamond"),
                            QStringLiteral("parallelogram")};
    const QStringList names{QStringLiteral("圆角矩形"), QStringLiteral("矩形"),
                            QStringLiteral("椭圆"), QStringLiteral("菱形"),
                            QStringLiteral("平行四边形")};
    shape->addItem(QStringLiteral("继承（%1）").arg(names.value(codes.indexOf(defaults.shape))));
    for (int index = 0; index < codes.size(); ++index)
        shape->addItem(names[index], codes[index]);
    if (original.shape)
        shape->setCurrentIndex(shape->findData(*original.shape));
    form->addRow(QStringLiteral("节点形状"), shape);
    auto *width =
        textField(QStringLiteral("styleBorderWidth"), QStringLiteral("边框宽度（0.1～20）"),
                  original.borderWidth
                      ? std::optional<QString>(QString::number(*original.borderWidth, 'g', 17))
                      : std::nullopt,
                  QString::number(defaults.borderWidth));
    auto *styleContent = new QWidget(&dialog);
    styleContent->setLayout(form);
    auto *styleScroll = new QScrollArea(&dialog);
    styleScroll->setWidgetResizable(true);
    styleScroll->setWidget(styleContent);
    layout->addWidget(styleScroll, 1);
    auto *edgeColor = new QLineEdit(edgeDefaults.color, &dialog);
    edgeColor->setObjectName(QStringLiteral("sheetEdgeColor"));
    auto *edgeWidth = new QDoubleSpinBox(&dialog);
    edgeWidth->setObjectName(QStringLiteral("sheetEdgeWidth"));
    edgeWidth->setRange(0.1, 20);
    edgeWidth->setDecimals(12);
    edgeWidth->setValue(edgeDefaults.width);
    const double initialEdgeWidth = edgeWidth->value();
    auto *edgeDashed = new QCheckBox(QStringLiteral("默认连线使用虚线"), &dialog);
    edgeDashed->setObjectName(QStringLiteral("sheetEdgeDashed"));
    edgeDashed->setChecked(edgeDefaults.dashed);
    if (global)
    {
        form->addRow(QStringLiteral("连线颜色（#RRGGBB）"), edgeColor);
        form->addRow(QStringLiteral("连线宽度"), edgeWidth);
        form->addRow(edgeDashed);
        // 默认样式无上级可继承，移除继承项。
        for (auto *combo : {size, bold, shape})
            combo->removeItem(0);
    }
    else
    {
        edgeColor->hide();
        edgeWidth->hide();
        edgeDashed->hide();
    }
    auto *reset = new QPushButton(QStringLiteral("恢复为工作表样式"), &dialog);
    reset->setObjectName(QStringLiteral("resetNodeStyle"));
    reset->setAutoDefault(false);
    layout->addWidget(reset);
    reset->setVisible(!global);
    auto *error = new QLabel(&dialog);
    error->setObjectName(QStringLiteral("nodeStyleError"));
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(reset, &QPushButton::clicked, &dialog,
            [=, &resetStyle]
            {
                resetStyle = true;
                for (auto *edit : {fill, color, border, family, width})
                    edit->clear();
                for (auto *combo : {size, bold, shape})
                    combo->setCurrentIndex(0);
                error->clear();
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&]
            {
                NodeStyleOverride style;
                const auto optionalText = [](QLineEdit *edit) -> std::optional<QString>
                {
                    const QString text = edit->text().trimmed();
                    return text.isEmpty() ? std::nullopt : std::optional<QString>(text);
                };
                style.fill = optionalText(fill);
                style.color = optionalText(color);
                style.borderColor = optionalText(border);
                // 旧文件允许显式空串和首尾空白；未编辑时保留optional及原字符串。
                // 恢复按钮则明确清除覆盖，不把“原来为空”误判为未操作。
                style.fontFamily = !resetStyle && family->text() == initialFamily
                                       ? original.fontFamily
                                       : optionalText(family);
                if (global || size->currentIndex() > 0)
                    style.fontSize = size->currentData().toInt();
                if (global || bold->currentIndex() > 0)
                    style.bold = bold->currentData().toBool();
                if (global || shape->currentIndex() > 0)
                    style.shape = shape->currentData().toString();
                if (!width->text().trimmed().isEmpty())
                {
                    bool ok = false;
                    const double value = width->text().toDouble(&ok);
                    if (!ok || !std::isfinite(value) || value < 0.1 || value > 20.0)
                    {
                        error->setText(QStringLiteral("边框宽度必须是0.1至20之间的数字。"));
                        return;
                    }
                    style.borderWidth = value;
                }
                std::variant<bool, AppError> result;
                if (global)
                {
                    NodeStyle next;
                    next.fill = style.fill.value_or(QString());
                    next.color = style.color.value_or(QString());
                    next.borderColor = style.borderColor.value_or(QString());
                    next.fontFamily = style.fontFamily.value_or(QString());
                    next.fontSize = style.fontSize.value_or(defaults.fontSize);
                    next.bold = style.bold.value_or(defaults.bold);
                    next.shape = style.shape.value_or(defaults.shape);
                    next.borderWidth = style.borderWidth.value_or(defaults.borderWidth);
                    EdgeStyle edge{edgeColor->text().trimmed(),
                                   edgeWidth->value() == initialEdgeWidth ? edgeDefaults.width
                                                                          : edgeWidth->value(),
                                   edgeDashed->isChecked()};
                    result = NodeCommands(m_commands, m_doc).setSheetStyle(sheetId, next, edge);
                }
                else
                {
                    result = NodeCommands(m_commands, m_doc).setStyle(sheetId, nodeId, style);
                }
                if (const auto *failure = std::get_if<AppError>(&result))
                {
                    error->setText(failure->message);
                    return;
                }
                dialog.accept();
            });
    dialog.exec();
}

void MainWindow::openSelectedNodeLink()
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    const auto link = sheet->nodes.value(m_selectedNodeId).link;
    if (!link.has_value())
        return;
    const QUrl url(*link);
    if (!url.isValid() ||
        (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
        return;
    if (!QDesktopServices::openUrl(url))
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法打开链接"),
                   QStringLiteral("未能启动浏览器，请检查系统的默认浏览器设置。"));
}

void MainWindow::newProject() {
    if (!confirmDiscard()) return;
    m_filePath.clear();
    m_fileStamp.reset();   // 新建工程无已加载文件戳（§11.3）
    loadNewDocument(ProjectFactory::createMindmap(QStringLiteral("未命名导图")));
}

void MainWindow::openProject() {
    if (!confirmDiscard()) return;
    // 取消文件对话框是正常结果（规范§8）
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开工程"), QString(),
        QStringLiteral("灵芽思维工程 (*.lingya)"));
    if (path.isEmpty()) return;
    openFile(path);
}

bool MainWindow::openFile(const QString& path) {
    // 登录态打开限制（D-M2-02）：公有文件与自己的文件可开，其他用户的文件拒绝
    if (!m_sessionUser.isEmpty() &&
        UserFileService::classify(m_sessionRoot, m_sessionUser, path) ==
            UserFileService::Access::Others) {
        showNotice(this, QMessageBox::Critical, QStringLiteral("无法打开"),
                   QStringLiteral("不能打开其他用户的文件"));
        return false;
    }
    const auto result = FileService::load(path);
    if (std::holds_alternative<AppError>(result)) {
        // 坏文件不覆盖当前工作（R04）：原工程保持不变
        showNotice(this, QMessageBox::Critical, QStringLiteral("无法打开"),
                   std::get<AppError>(result).message);
        return false;
    }
    m_filePath = path;
    loadNewDocument(std::get<Project>(result));
    // 记录加载时外部状态戳（§11.3）：保存覆盖前比对，检测外部修改
    if (const auto fileStamp = FileService::stamp(path);
        std::holds_alternative<FileService::FileStamp>(fileStamp)) {
        m_fileStamp = std::get<FileService::FileStamp>(fileStamp);
    } else {
        m_fileStamp.reset();
    }
    SettingsService::addRecentFile(path);
    rebuildRecentMenu();
    return true;
}

bool MainWindow::saveProject(bool saveAs) {
    // 登录态：只命名、不选路径，保存到当前用户专有文件夹（D-M2-02）
    if (!m_sessionUser.isEmpty()) {
        QString name;
        if (!saveAs && !m_filePath.isEmpty() &&
            UserFileService::classify(m_sessionRoot, m_sessionUser, m_filePath) ==
                UserFileService::Access::Own) {
            return saveProjectTo(m_filePath);   // 原位保存确切路径，保留完整文件名与文件戳
        } else {
            const QString fallback = m_filePath.isEmpty()
                                         ? m_doc->project().title
                                         : QFileInfo(m_filePath).fileName();
            bool ok = false;
            name = QInputDialog::getText(this, QStringLiteral("保存到我的文件"),
                                         QStringLiteral("文件名称："), QLineEdit::Normal,
                                         fallback, &ok);
            if (!ok) return false;   // 取消是正常结果
        }
        return saveProjectToOwned(name);
    }
    QString path = m_filePath;
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("另存为"), QString(),
                                            QStringLiteral("灵芽思维工程 (*.lingya)"));
        if (path.isEmpty()) return false;   // 取消是正常结果，不算失败
    }
    return saveProjectTo(path);
}

bool MainWindow::saveProjectTo(const QString& path) {
    if (path.isEmpty()) return false;
    QString target = path;
    if (!target.endsWith(".lingya", Qt::CaseInsensitive)) target += ".lingya";
    // 覆盖已加载文件时带外部修改检测（§11.3）；另存新路径无外部覆盖风险
    const auto result =
        (target == m_filePath && m_fileStamp.has_value())
            ? FileService::save(target, m_doc->project(), *m_fileStamp)
            : FileService::save(target, m_doc->project());
    if (std::holds_alternative<AppError>(result)) {
        // 保存失败保持脏状态并显示中文提示（规范§8）
        showNotice(this, QMessageBox::Critical, QStringLiteral("保存失败"),
                   std::get<AppError>(result).message);
        return false;
    }
    m_filePath = target;
    m_doc->setClean();
    // 正式保存成功后，快照捕获的未保存状态已落盘：清除该工程的应用自建快照
    // （恢复快照与正式保存是不同状态，规范§11.3）；保存失败时快照保留继续保护
    RecoveryService::clear(m_recoveryRoot, m_doc->project().id);
    m_recoveryNote.clear();
    m_doc->notifyChanged();       // 状态变更完成后统一通知（D-M0-13）
    if (const auto fileStamp = FileService::stamp(target);
        std::holds_alternative<FileService::FileStamp>(fileStamp)) {
        m_fileStamp = std::get<FileService::FileStamp>(fileStamp);
    } else {
        m_fileStamp.reset();
    }
    SettingsService::addRecentFile(target);
    rebuildRecentMenu();
    return true;
}

void MainWindow::renameSelectedNode()
{
    if (activeFlowSheet())
    {
        editFlowNode(false);
        return;
    }
    const TreeSheet* sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    // 稳定ID先复制，命令触发changed重建后不继续持有文档内部引用。
    const ID sheetId = sheet->id;
    const ID nodeId = m_selectedNodeId;
    const QString oldTitle = sheet->nodes.value(nodeId).title;
    NodeTitleDialog dialog(QStringLiteral("重命名主题"), oldTitle, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString title = dialog.textValue();
    TaskCommands tasks(m_commands, m_doc);
    const auto result = tasks.renameNode(sheetId, nodeId, title);
    if (const auto *error = std::get_if<AppError>(&result))
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法重命名"), error->message);
}

void MainWindow::addChildToSelection()
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    const ID sheetId = sheet->id;
    const ID parentId = m_selectedNodeId;
    NodeTitleDialog dialog(QStringLiteral("新增子主题"), QStringLiteral("未命名主题"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString title = dialog.textValue();
    NodeCommands nodes(m_commands, m_doc);
    const auto result = nodes.addChild(sheetId, parentId, title);
    if (const auto *error = std::get_if<AppError>(&result))
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法新增"), error->message);
        return;
    }
    selectNewNode(std::get<ID>(result));
}

void MainWindow::addSiblingToSelection()
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    if (m_selectedNodeId == sheet->rootId)
        return;
    const ID sheetId = sheet->id;
    const ID referenceId = m_selectedNodeId;
    NodeTitleDialog dialog(QStringLiteral("新增同级主题"), QStringLiteral("未命名主题"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    NodeCommands nodes(m_commands, m_doc);
    const auto result = nodes.addSibling(sheetId, referenceId, dialog.textValue());
    if (const auto *error = std::get_if<AppError>(&result))
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法新增同级主题"), error->message);
        return;
    }
    selectNewNode(std::get<ID>(result));
}

void MainWindow::deleteSelection()
{
    if (activeFlowSheet())
    {
        deleteFlowSelection();
        return;
    }
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(m_selectedNodeId))
        return;
    if (m_selectedNodeId == sheet->rootId)
    {
        statusBar()->showMessage(QStringLiteral("根主题不能删除。请选择要删除的分支主题。"));
        return;
    }
    const ID sheetId = sheet->id;
    const ID nodeId = m_selectedNodeId;
    const ID parentId = sheet->nodes.value(nodeId).parentId.value_or(sheet->rootId);
    // 这里只读统计影响；实际清理统一交给乙的deleteSubtree事务。
    QSet<ID> subtree;
    QVector<ID> pending{nodeId};
    while (!pending.isEmpty())
    {
        const ID current = pending.takeLast();
        if (subtree.contains(current))
            continue;
        subtree.insert(current);
        for (const TreeNode &node : sheet->nodes)
            if (node.parentId.has_value() && *node.parentId == current)
                pending.append(node.id);
    }
    int taskCount = 0, relationCount = 0, annotationCount = 0, commentCount = 0, cardCount = 0;
    for (const Task &task : m_doc->project().tasks)
        if (task.source.has_value() && task.source->sheetId == sheetId &&
            subtree.contains(task.source->nodeId))
            ++taskCount;
    for (const Relation &relation : sheet->relations)
        if (subtree.contains(relation.sourceId) || subtree.contains(relation.targetId))
            ++relationCount;
    for (const GroupAnnotation &annotation : sheet->annotations)
    {
        bool affected = subtree.contains(annotation.parentId);
        for (const ID &member : annotation.memberIds)
            affected |= subtree.contains(member);
        if (affected)
            ++annotationCount;
    }
    for (const Comment &comment : sheet->comments)
        if (subtree.contains(comment.nodeId))
            ++commentCount;
    for (const ReviewCard &card : m_doc->project().cards)
        if (card.sheetId == sheetId && subtree.contains(card.nodeId))
            ++cardCount;
    QString summary = QStringLiteral("将删除“%1”及其后代，共%2个主题。可用Ctrl+Z撤销。\n\n"
                                     "同时清理：%3条关系、%5条批注。涉及%4项外框/"
                                     "概要，剔除成员后不足两个才移除；父级被删则移除。\n"
                                     "%6张复习卡片将保留并标记来源缺失。\n"
                                     "讲解路线和文稿保留，使用时需重新检查来源。")
                          .arg(sheet->nodes.value(nodeId).title.simplified().left(60))
                          .arg(subtree.size())
                          .arg(relationCount)
                          .arg(annotationCount)
                          .arg(commentCount)
                          .arg(cardCount);
    if (taskCount > 0)
        summary += QStringLiteral("\n\n另有关联任务%1项，请选择任务处置方式。").arg(taskCount);
    QMessageBox confirmation(QMessageBox::Warning, QStringLiteral("确认删除子树"), summary,
                             QMessageBox::NoButton, this);
    confirmation.setObjectName(QStringLiteral("deleteSubtreeConfirmation"));
    auto *keep = confirmation.addButton(taskCount > 0 ? QStringLiteral("保留任务并删除主题")
                                                      : QStringLiteral("删除主题"),
                                        QMessageBox::DestructiveRole);
    QPushButton *together = nullptr;
    if (taskCount > 0)
        together = confirmation.addButton(QStringLiteral("连同任务一起删除"),
                                          QMessageBox::DestructiveRole);
    auto *cancel = confirmation.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    confirmation.setDefaultButton(cancel);
    confirmation.setEscapeButton(cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != keep &&
        (together == nullptr || confirmation.clickedButton() != together))
        return;
    const auto policy = confirmation.clickedButton() == together
                            ? NodeCommands::TaskDeletePolicy::DeleteTogether
                            : NodeCommands::TaskDeletePolicy::KeepAsStandalone;
    m_selectedNodeId = parentId;
    NodeCommands nodes(m_commands, m_doc);
    const auto result = nodes.deleteSubtree(sheetId, nodeId, policy);
    if (const auto *error = std::get_if<AppError>(&result))
    {
        selectNode(nodeId);
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法删除"), error->message);
        return;
    }
    focusSelectedNode();
}

void MainWindow::rebuildRecentMenu() {
    if (m_recentMenu == nullptr) return;
    m_recentMenu->clear();
    const QVector<QString> recents = SettingsService::recentFiles();
    if (recents.isEmpty()) {
        // 空状态给出文字说明而非仅禁用（统一规范§14）
        QAction* empty = m_recentMenu->addAction(QStringLiteral("（暂无最近文件）"));
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : recents)
        m_recentMenu->addAction(path, this, [this, path] { openRecentFile(path); });
}

void MainWindow::openRecentFile(const QString& path) {
    if (!confirmDiscard()) return;
    if (!QFileInfo::exists(path)) {
        // P01验收：文件缺失可移除记录，提示后不静默失败
        SettingsService::removeRecentFile(path);
        rebuildRecentMenu();
        showNotice(this, QMessageBox::Information, QStringLiteral("最近文件"),
                   QStringLiteral("文件不存在，已从最近文件中移除：\n%1").arg(path));
        return;
    }
    openFile(path);
}

bool MainWindow::renameRootTo(const QString& title) {
    const TreeSheet* sheet = activeTreeSheet();
    if (sheet == nullptr) return false;
    const ID sheetId = sheet->id;
    const ID nodeId = sheet->rootId;
    TaskCommands tasks(m_commands, m_doc);
    const auto result = tasks.renameNode(sheetId, nodeId, title);
    if (std::holds_alternative<AppError>(result)) {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法重命名"),
                   std::get<AppError>(result).message);
        return false;
    }
    return true;
}

QString MainWindow::currentRootTitle() const {
    const TreeSheet* sheet = activeTreeSheet();
    return sheet == nullptr ? QString() : sheet->nodes.value(sheet->rootId).title;
}

bool MainWindow::confirmDiscard() {
    if (!m_doc->isModified()) return true;
    QMessageBox question(QMessageBox::Warning, QStringLiteral("未保存的修改"),
                         QStringLiteral("当前工程已修改，是否保存？"),
                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    question.button(QMessageBox::Save)->setText(QStringLiteral("保存"));
    question.button(QMessageBox::Discard)->setText(QStringLiteral("放弃修改"));
    question.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
    question.setDefaultButton(QMessageBox::Cancel);
    question.setEscapeButton(QMessageBox::Cancel);
    question.exec();
    const auto choice = question.standardButton(question.clickedButton());
    if (choice == QMessageBox::Save) return saveProject(false);
    if (choice == QMessageBox::Discard) {
        // 用户明确放弃未保存修改：清除该工程的应用自建恢复快照（规范§11.3），
        // 快照目录里只删本服务创建的文件，不触碰用户任何正式文件
        RecoveryService::clear(m_recoveryRoot, m_doc->project().id);
        return true;
    }
    return false;   // 取消：不换工程、不清理快照
}

void MainWindow::loadNewDocument(Project project) {
    m_annotationMembers.clear();
    m_cutNodeId.clear();
    m_cutToken.clear();
    m_unfilteredCollapsed.clear();
    m_filterWasActive = false;
    m_ganttTaskLocation = false;
    m_outlineSheetId.clear();
    m_displaySheetId.clear();
    {
        const QSignalBlocker blocker(m_tagFilter);
        m_tagFilter->setCurrentIndex(0);
    }
    m_selectedNodeId.clear();
    m_outline->clear();
    m_recoveryNote.clear();   // 新建/打开不能沿用上一工程的快照时间或失败状态
    m_doc->replaceProject(std::move(project));
    m_commands->clearHistory();   // 打开新工程清空撤销历史（规范§5.1）
    m_doc->setClean();            // 新建/打开后尚未编辑，应为干净状态（规范§8）
    m_doc->notifyChanged();       // 状态变更完成后统一通知（D-M0-13）
}

void MainWindow::rebuildOutline()
{
    if (m_outlineSheetId != m_doc->project().activeSheetId)
    {
        m_outlineSheetId = m_doc->project().activeSheetId;
        m_unfilteredCollapsed.clear();
        m_filterWasActive = false;
        const QSignalBlocker filterBlocker(m_tagFilter);
        m_tagFilter->setCurrentIndex(0);
        m_outline->clear();
    }
    rebuildTagFilter();
    const QSignalBlocker blocker(m_outline);
    QSet<ID> collapsed;
    for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
        if (!(*it)->isExpanded())
            collapsed.insert((*it)->data(0, Qt::UserRole).toString());
    if (m_filterWasActive)
        collapsed = m_unfilteredCollapsed;
    m_outline->clear();
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr)
    {
        if (const auto *flow = activeFlowSheet())
        {
            m_outline->setDragDropMode(QAbstractItemView::NoDragDrop);
            if (!flow->nodes.contains(m_selectedNodeId))
                m_selectedNodeId = flow->nodes.isEmpty() ? ID{} : flow->nodes.firstKey();
            for (const auto &node : flow->nodes)
            {
                auto *item = new QTreeWidgetItem(m_outline);
                item->setText(0, node.title);
                item->setToolTip(0, node.title);
                item->setData(0, Qt::UserRole, node.id);
                if (node.id == m_selectedNodeId)
                    m_outline->setCurrentItem(item);
            }
            return;
        }
        m_selectedNodeId.clear();
        return;
    }
    m_outline->setDragDropMode(QAbstractItemView::InternalMove);
    if (!sheet->nodes.contains(m_selectedNodeId))
        m_selectedNodeId = sheet->rootId;
    QMap<ID, QTreeWidgetItem *> items;
    QVector<ID> ids;
    for (auto it = sheet->nodes.constBegin(); it != sheet->nodes.constEnd(); ++it)
    {
        auto *item = new QTreeWidgetItem;
        item->setText(0, it.value().title);
        item->setToolTip(0, it.value().title);
        item->setData(0, Qt::UserRole, it.key());
        items.insert(it.key(), item);
        ids.append(it.key());
    }
    std::sort(ids.begin(), ids.end(),
              [sheet](const ID &a, const ID &b)
              {
                  const int left = sheet->nodes.value(a).order;
                  const int right = sheet->nodes.value(b).order;
                  return left == right ? a < b : left < right;
              });
    for (const ID &id : ids)
    {
        const auto parent = sheet->nodes.value(id).parentId;
        if (parent.has_value())
            items.value(*parent)->addChild(items.value(id));
        else
            m_outline->addTopLevelItem(items.value(id));
    }
    for (const ID &id : ids)
        items.value(id)->setExpanded(!collapsed.contains(id));
    m_outline->setCurrentItem(items.value(m_selectedNodeId));
    m_outline->scrollToItem(items.value(m_selectedNodeId));
    applyOutlineTagFilter();
}

void MainWindow::rebuildTagFilter()
{
    const QSignalBlocker blocker(m_tagFilter);
    const QVariant previous = m_tagFilter->currentData();
    QSet<QString> uniqueTags;
    if (const TreeSheet *sheet = activeTreeSheet())
        for (const auto &node : sheet->nodes)
            for (const QString &tag : node.tags)
                uniqueTags.insert(tag);
    QStringList tags = uniqueTags.values();
    tags.sort(Qt::CaseSensitive);
    m_tagFilter->clear();
    m_tagFilter->addItem(QStringLiteral("全部标签（大纲）"));
    for (const QString &tag : tags)
        m_tagFilter->addItem(tag.isEmpty() ? QStringLiteral("（空标签）") : tag, tag);
    const int index = previous.isValid() ? m_tagFilter->findData(previous) : 0;
    m_tagFilter->setCurrentIndex(index < 0 ? 0 : index);
}

void MainWindow::applyOutlineTagFilter()
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr)
        return;
    const QVariant tag = m_tagFilter->currentData();
    const bool filtered = tag.isValid();
    if (filtered && !m_filterWasActive)
    {
        m_unfilteredCollapsed.clear();
        for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
            if (!(*it)->isExpanded())
                m_unfilteredCollapsed.insert((*it)->data(0, Qt::UserRole).toString());
    }
    if (!filtered && m_filterWasActive)
        for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
            (*it)->setExpanded(
                !m_unfilteredCollapsed.contains((*it)->data(0, Qt::UserRole).toString()));
    m_filterWasActive = filtered;
    QTreeWidgetItem *firstMatch = nullptr;
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item)
    {
        const bool match = !filtered || sheet->nodes.value(item->data(0, Qt::UserRole).toString())
                                            .tags.contains(tag.toString());
        if (match && firstMatch == nullptr)
            firstMatch = item;
        bool childVisible = false;
        for (int index = 0; index < item->childCount(); ++index)
            childVisible |= visit(item->child(index));
        const bool visible = match || childVisible;
        item->setHidden(!visible);
        if (filtered && childVisible)
            item->setExpanded(true);
        return visible;
    };
    for (int index = 0; index < m_outline->topLevelItemCount(); ++index)
        visit(m_outline->topLevelItem(index));
    if (filtered && firstMatch && m_outline->currentItem() && m_outline->currentItem()->isHidden())
        selectNode(firstMatch->data(0, Qt::UserRole).toString());
}

void MainWindow::selectNode(const ID &nodeId)
{
    const TreeSheet *sheet = activeTreeSheet();
    const FlowSheet *flow = activeFlowSheet();
    if ((!sheet || !sheet->nodes.contains(nodeId)) && (!flow || !flow->nodes.contains(nodeId)))
        return;
    m_selectedFlowEdge.clear();
    for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == nodeId && (*it)->isHidden())
        {
            const QSignalBlocker blocker(m_tagFilter);
            m_tagFilter->setCurrentIndex(0);
            applyOutlineTagFilter();
            break;
        }
    m_selectedNodeId = nodeId;
    {
        const QSignalBlocker blocker(m_outline);
        for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
        {
            if ((*it)->data(0, Qt::UserRole).toString() == nodeId)
            {
                m_outline->setCurrentItem(*it);
                m_outline->scrollToItem(*it);
                break;
            }
        }
    }
    // 选中持久化折叠分支内的节点时，仅临时展开其祖先供定位，不修改文档。
    if (!m_nodeItems.contains(nodeId))
        rebuildCanvas();
    syncCanvasSelection();
    focusSelectedNode();
}

void MainWindow::syncCanvasSelection()
{
    for (auto it = m_nodeItems.constBegin(); it != m_nodeItems.constEnd(); ++it)
    {
        it.value()->setSelected(it.key() == m_selectedNodeId);
        it.value()->setCutPending(it.key() == m_cutNodeId &&
                                  m_cutRevision == m_doc->sessionRevision());
    }
    const TreeSheet *sheet = activeTreeSheet();
    const bool nonRoot = sheet != nullptr && sheet->nodes.contains(m_selectedNodeId) &&
                         m_selectedNodeId != sheet->rootId;
    m_siblingAction->setEnabled(nonRoot);
    m_deleteAction->setEnabled(nonRoot);
    const bool valid = sheet != nullptr && sheet->nodes.contains(m_selectedNodeId);
    if (valid && m_taskModel)
        for (const auto &task : m_doc->project().tasks)
            if (task.source && task.source->sheetId == sheet->id &&
                task.source->nodeId == m_selectedNodeId)
            {
                selectTask(task.id);
                break;
            }
    m_propertiesAction->setEnabled(valid);
    m_styleAction->setEnabled(valid);
    m_tagsAction->setEnabled(valid);
    m_openLinkAction->setEnabled(valid && sheet->nodes.value(m_selectedNodeId).link.has_value());
    for (const auto &name :
         {"copyTopic", "cutTopic", "pasteTopic", "editSheetStyle", "editRelations",
          "editAnnotations", "editComments", "changeSheetKind", "renameTopic", "addChildTopic"})
        if (auto *action = findChild<QAction *>(QString::fromLatin1(name)))
            action->setEnabled(valid);
    const auto *flow = activeFlowSheet();
    const bool flowNode = flow && flow->nodes.contains(m_selectedNodeId);
    for (const auto &name : {"addFlowNode", "editFlowEdges", "layoutFlow", "flowSnap"})
        if (auto *action = findChild<QAction *>(QString::fromLatin1(name)))
            action->setEnabled(flow != nullptr);
    if (auto *action = findChild<QAction *>("editFlowNode"))
        action->setEnabled(flowNode);
    if (flow)
    {
        m_deleteAction->setEnabled(flowNode || !m_selectedFlowEdge.isEmpty());
        if (auto *rename = findChild<QAction *>("renameTopic"))
            rename->setEnabled(flowNode);
    }
}

void MainWindow::focusSelectedNode()
{
    if (NodeItem *item = m_nodeItems.value(m_selectedNodeId, nullptr))
        m_view->ensureVisible(item, 48, 48);
}

void MainWindow::rebuildCanvas() {
    m_nodeItems.clear();
    m_scene->clear();
    m_canvasError.clear();
    const TreeSheet *sheet = activeTreeSheet();
    m_editorNote.clear();
    if (sheet == nullptr)
    {
        if (const auto *flow = activeFlowSheet())
            rebuildFlowCanvas(*flow);
        syncCanvasSelection();
        m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
        return;
    }
    TreeSheet display = *sheet;
    ID ancestor = m_selectedNodeId;
    while (display.nodes.contains(ancestor))
    {
        const auto parent = display.nodes.value(ancestor).parentId;
        if (!parent.has_value())
            break;
        display.nodes[*parent].collapsed = false;
        ancestor = *parent;
    }
    QVector<TreeLayout::NodeSize> sizes;
    sizes.reserve(display.nodes.size());
    for (auto it = display.nodes.constBegin(); it != display.nodes.constEnd(); ++it)
    {
        const NodeStyle style = NodeItem::resolvedStyle(display.defaultNodeStyle, it.value().style);
        const NodeItem measured(it.value().title, NodeItem::styledFont(canvasFont(), style),
                                nullptr, it.value().iconId.value_or(QString()), style);
        const QRectF bounds = measured.boundingRect();
        sizes.append({it.key(), bounds.width(), bounds.height()});
    }
    // R11：鱼骨图使用乙的FishboneLayout（鱼头在原点、主骨向左、类别上下交替），
    // 其余树图仍用TreeLayout。两者都返回中心点坐标，图元创建方式相同。
    const bool fishbone = display.kind == QStringLiteral("fishbone");
    QVector<TreeLayout::NodePosition> positions;
    std::optional<AppError> layoutError;
    if (fishbone)
    {
        QVector<FishboneLayout::NodeSize> fishSizes;
        fishSizes.reserve(sizes.size());
        for (const auto &size : sizes)
            fishSizes.append({size.id, size.width, size.height});
        const auto fish = FishboneLayout::layout(display, fishSizes);
        if (const auto *error = std::get_if<AppError>(&fish))
            layoutError = *error;
        else
            for (const auto &position : std::get<FishboneLayout::Result>(fish).positions)
                positions.append({position.id, position.x, position.y});
    }
    else
    {
        const auto tree = TreeLayout::layout(display, sizes);
        if (const auto *error = std::get_if<AppError>(&tree))
            layoutError = *error;
        else
            positions = std::get<QVector<TreeLayout::NodePosition>>(tree);
    }
    if (layoutError.has_value())
    {
        m_canvasError = layoutError->message;
        m_scene->addText(QStringLiteral("%1：%2").arg(fishbone ? QStringLiteral("无法显示鱼骨图")
                                                               : QStringLiteral("无法显示导图"),
                                                      layoutError->message),
                         canvasFont());
        m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
        return;
    }
    for (const auto &position : positions)
    {
        const auto node = display.nodes.value(position.id);
        const NodeStyle style = NodeItem::resolvedStyle(display.defaultNodeStyle, node.style);
        auto *item = new NodeItem(node.title, NodeItem::styledFont(canvasFont(), style), nullptr,
                                  node.iconId.value_or(QString()), style);
        item->setNodeId(position.id);
        item->setPos(position.x, position.y);
        m_scene->addItem(item);
        m_nodeItems.insert(position.id, item);
    }
    if (fishbone)
        addFishboneEdges(display);
    for (auto it = m_nodeItems.constBegin(); !fishbone && it != m_nodeItems.constEnd(); ++it)
    {
        const auto parentId = display.nodes.value(it.key()).parentId;
        if (!parentId.has_value() || !m_nodeItems.contains(*parentId))
            continue;
        const NodeItem *parent = m_nodeItems.value(*parentId);
        const NodeItem *child = it.value();
        const bool right = child->pos().x() >= parent->pos().x();
        const QPointF start = parent->mapToScene(parent->horizontalAnchor(right));
        const QPointF end = child->mapToScene(child->horizontalAnchor(!right));
        const qreal middleX = (start.x() + end.x()) / 2;
        QPainterPath path(start);
        path.cubicTo(QPointF(middleX, start.y()), QPointF(middleX, end.y()), end);
        QPen edgePen(QColor(display.defaultEdgeStyle.color), display.defaultEdgeStyle.width);
        if (display.defaultEdgeStyle.dashed)
            edgePen.setStyle(Qt::DashLine);
        auto *edge = m_scene->addPath(path, edgePen);
        edge->setZValue(-1);
        edge->setAcceptedMouseButtons(Qt::NoButton);
    }
    addDecorations(display);
    addTaskBadges();
    syncCanvasSelection();
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
    focusSelectedNode();
}

// R11鱼骨连线（只读显示，不写业务数据）：
// 1. 主骨：从鱼头左缘水平向左，覆盖全部类别；
// 2. 类别斜骨：从类别靠主骨一侧的边缘中点按45°连到主骨；
// 3. 原因支骨：FishboneLayout把子节点向外逐个堆叠并左移一层，因此每个父节点在其全部后代
//    右缘之外取一条竖向“干线”，再水平接到每个子节点右缘。干线位于后代包围盒右侧，
//    只经过该父节点自己的子树所占的竖向区间，不穿过其他节点。
void MainWindow::addFishboneEdges(const TreeSheet &display)
{
    const NodeItem *head = m_nodeItems.value(display.rootId, nullptr);
    if (head == nullptr)
        return;
    QPen pen(QColor(display.defaultEdgeStyle.color), display.defaultEdgeStyle.width);
    if (display.defaultEdgeStyle.dashed)
        pen.setStyle(Qt::DashLine);
    QPen spinePen = pen;
    spinePen.setWidthF(qMax(2.0, display.defaultEdgeStyle.width * 2));
    spinePen.setStyle(Qt::SolidLine);
    const auto addEdge = [this](const QPainterPath &path, const QPen &edgePen)
    {
        auto *edge = m_scene->addPath(path, edgePen);
        edge->setZValue(-1);
        edge->setAcceptedMouseButtons(Qt::NoButton);
    };
    const auto visibleChildren = [this, &display](const ID &parentId)
    {
        QVector<QPair<int, ID>> ordered;
        for (auto it = display.nodes.constBegin(); it != display.nodes.constEnd(); ++it)
            if (it.value().parentId == parentId && m_nodeItems.contains(it.key()))
                ordered.append({it.value().order, it.key()});
        std::sort(ordered.begin(), ordered.end());
        QVector<ID> ids;
        for (const auto &pair : ordered)
            ids.append(pair.second);
        return ids;
    };

    const QPointF headLeft = head->mapToScene(head->horizontalAnchor(false));
    const qreal spineY = head->pos().y();
    const qreal maxAttachX = headLeft.x() - 12;
    qreal tailX = headLeft.x() - 96;
    QVector<ID> stack;
    for (const ID &categoryId : visibleChildren(display.rootId))
    {
        const NodeItem *category = m_nodeItems.value(categoryId);
        const QRectF rect = category->mapRectToScene(category->nodeRect());
        const bool upper = category->pos().y() < spineY;
        const QPointF inner(category->pos().x(), upper ? rect.bottom() : rect.top());
        const qreal attachX = qMin(maxAttachX, inner.x() + qAbs(inner.y() - spineY));
        QPainterPath bone(inner);
        bone.lineTo(attachX, spineY);
        addEdge(bone, pen);
        tailX = qMin(tailX, rect.left() - 24);
        stack.append(categoryId);
    }
    QPainterPath spine(headLeft);
    spine.lineTo(tailX, spineY);
    addEdge(spine, spinePen);

    while (!stack.isEmpty())
    {
        const ID parentId = stack.takeLast();
        const QVector<ID> children = visibleChildren(parentId);
        if (children.isEmpty())
            continue;
        const NodeItem *parent = m_nodeItems.value(parentId);
        const QRectF parentRect = parent->mapRectToScene(parent->nodeRect());
        qreal trunkX = -std::numeric_limits<qreal>::infinity();
        QVector<ID> descendants = children;
        for (int i = 0; i < descendants.size(); ++i)
        {
            const NodeItem *node = m_nodeItems.value(descendants.at(i));
            trunkX = qMax(trunkX, node->sceneBoundingRect().right() + 10);
            descendants += visibleChildren(descendants.at(i));
        }
        const bool upper = parent->pos().y() < spineY;
        QPointF start;
        if (trunkX >= parentRect.left() + 6 && trunkX <= parentRect.right() - 6)
            start = QPointF(trunkX, upper ? parentRect.top() : parentRect.bottom());
        else
            start = parent->mapToScene(parent->horizontalAnchor(trunkX > parentRect.right()));
        for (const ID &childId : children)
        {
            const NodeItem *child = m_nodeItems.value(childId);
            const QPointF end = child->mapToScene(child->horizontalAnchor(true));
            QPainterPath branch(start);
            if (start.x() != trunkX)
                branch.lineTo(trunkX, start.y());
            branch.lineTo(trunkX, end.y());
            branch.lineTo(end);
            addEdge(branch, pen);
            stack.append(childId);
        }
    }
}

void MainWindow::onDocumentChanged() {
    const QScopedValueRollback<bool> refreshing(m_refreshingViews, true);
    const bool sameSheet = m_outlineSheetId == m_doc->project().activeSheetId;
    if (!sameSheet)
        m_ganttTaskLocation = false;
    const auto previousAnnotations = m_annotationMembers;
    rebuildSheets();
    rebuildOutline();
    rebuildCanvas();
    m_annotationMembers.clear();
    if (const auto *tree = activeTreeSheet())
        for (const auto &annotation : tree->annotations)
            m_annotationMembers.insert(annotation.id, annotation.memberIds);
    if (sameSheet)
        for (auto it = previousAnnotations.constBegin(); it != previousAnnotations.constEnd(); ++it)
            if (!m_annotationMembers.contains(it.key()) ||
                m_annotationMembers.value(it.key()) != it.value())
            {
                m_editorNote =
                    QStringLiteral("外框/概要已随结构变化调整，成员不足两个时移除；可撤销。");
                break;
            }
    if (!m_cutNodeId.isEmpty() && m_cutRevision != m_doc->sessionRevision())
        m_editorNote += QStringLiteral(" 剪切标记因源修订变化已失效，请重新剪切。");
    if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage))
        gantt->setTasks(visibleGanttTasks());
    refreshOfflineViews();
    updateStatusLine();
    // 标题栏显示文件基名（有路径时）或工程标题；打开文件后用户看到的是自己认识的文件名
    // （工程标题M0无修改入口，固定“未命名导图”曾造成“打开未生效”的误解，见D-M0-12）
    // 恢复快照采用后路径清空（快照≠正式保存），标题栏同样显示工程标题
    const QFileInfo info(m_filePath);
    const QString display =
        m_filePath.isEmpty() ? m_doc->project().title : info.completeBaseName();
    setWindowTitle(QStringLiteral("%1%2 - 灵芽思维")
                       .arg(display,
                            m_doc->isModified() ? QStringLiteral(" *") : QString()));
    m_undoAction->setEnabled(m_commands->canUndo());
    m_redoAction->setEnabled(m_commands->canRedo());
}

void MainWindow::updateStatusLine() {
    const QString state =
        m_doc->isModified() ? QStringLiteral("已修改") : QStringLiteral("已保存");
    const TreeSheet *sheet = activeTreeSheet();
    const bool ganttDisplayed = m_centralStack->currentIndex() == 1;
    const QString kindLabel =
        ganttDisplayed      ? QStringLiteral("甘特图")
        : sheet             ? (sheet->kind == QStringLiteral("fishbone") ? QStringLiteral("鱼骨图")
                                                                         : QStringLiteral("思维导图"))
        : activeFlowSheet() ? QStringLiteral("流程图")
                            : QStringLiteral("甘特图");
    QString line = QStringLiteral("%1 · 大纲与画布同步 · 修订 %2 · %3")
                       .arg(kindLabel)
                       .arg(m_doc->sessionRevision())
                       .arg(state);
    if (!sheet && !activeFlowSheet() && !m_doc->project().tasks.isEmpty() &&
        !hasVisibleGanttTasks())
        line +=
            QStringLiteral(" · 当前筛选无任务（工程共 %1 项）").arg(m_doc->project().tasks.size());
    if (ganttDisplayed)
    {
        if (m_ganttTaskLocation)
            line += QStringLiteral(" · 临时全量（点“视图→甘特图”恢复筛选）");
        else if (!m_selectedTaskId.isEmpty() && !isTaskVisibleInGantt(m_selectedTaskId))
            line += QStringLiteral(" · 所选任务在筛选外");
    }
    // 恢复快照与正式保存分别显示状态（原设计§8）：备注为最近一次快照写入时间或失败提示
    if (!m_recoveryNote.isEmpty()) line += QStringLiteral(" · ") + m_recoveryNote;
    if (!m_canvasError.isEmpty())
        line += QStringLiteral(" · 布局失败：%1").arg(m_canvasError);
    if (!m_editorNote.isEmpty())
        line += QStringLiteral(" · ") + m_editorNote;
    if (!m_sessionUser.isEmpty())
        line += QStringLiteral(" · 用户 %1").arg(m_sessionUser);
    statusBar()->showMessage(line);
}

QString MainWindow::currentProjectId() const {
    return m_doc->project().id;
}

const TreeSheet* MainWindow::activeTreeSheet() const {
    const Project& project = m_doc->project();
    for (const Sheet& s : project.sheets) {
        const TreeSheet* tree = std::get_if<TreeSheet>(&s);
        if (tree != nullptr && tree->id == project.activeSheetId) return tree;
    }
    return nullptr;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // 关闭时提供保存、放弃、取消（规范§8）
    if (confirmDiscard())
        event->accept();
    else
        event->ignore();
}


bool MainWindow::loginSession(const QString& userName, const QString& password) {
    const auto result = SessionService::login(m_sessionRoot, userName, password);
    if (std::holds_alternative<AppError>(result)) {
        showNotice(this, QMessageBox::Warning, QStringLiteral("登录失败"),
                   std::get<AppError>(result).message);
        return false;
    }
    m_sessionUser = std::get<SessionService::AccountInfo>(result).userName;
    updateSessionActions();
    updateStatusLine();
    return true;
}

void MainWindow::logoutSession() {
    m_sessionUser.clear();
    updateSessionActions();
    updateStatusLine();
}

bool MainWindow::isLoggedIn() const {
    return !m_sessionUser.isEmpty();
}

QString MainWindow::sessionUserName() const {
    return m_sessionUser;
}

bool MainWindow::saveProjectToOwned(const QString& name) {
    if (m_sessionUser.isEmpty()) {
        showNotice(this, QMessageBox::Warning, QStringLiteral("未登录"),
                   QStringLiteral("登录后才能保存到我的文件"));
        return false;
    }
    if (!LocalNameRules::validComponent(name.trimmed()))
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("文件名无效"),
                   QStringLiteral("文件名不能为空，且不能使用Windows保留名称或非法字符"));
        return false;
    }
    QString targetName = name.trimmed();
    if (!targetName.endsWith(QStringLiteral(".lingya"), Qt::CaseInsensitive))
        targetName += QStringLiteral(".lingya");
    if (!m_filePath.isEmpty() &&
        UserFileService::classify(m_sessionRoot, m_sessionUser, m_filePath) ==
            UserFileService::Access::Own &&
        targetName.compare(QFileInfo(m_filePath).fileName(), Qt::CaseInsensitive) == 0)
    {
        // 原位保存复用统一的已加载文件戳保护，不覆盖外部编辑或复活外部删除。
        return saveProjectTo(m_filePath);
    }
    const auto saved =
        UserFileService::saveOwned(m_sessionRoot, m_sessionUser, name, m_doc->project());
    if (std::holds_alternative<AppError>(saved)) {
        showNotice(this, QMessageBox::Critical, QStringLiteral("保存失败"),
                   std::get<AppError>(saved).message);
        return false;
    }
    const QString path = std::get<QString>(saved);
    m_filePath = path;
    m_doc->setClean();
    // 正式保存成功：清除该工程的应用自建恢复快照（与saveProjectTo同规则，§11.3）
    RecoveryService::clear(m_recoveryRoot, m_doc->project().id);
    m_recoveryNote.clear();
    m_doc->notifyChanged();
    if (const auto fileStamp = FileService::stamp(path);
        std::holds_alternative<FileService::FileStamp>(fileStamp)) {
        m_fileStamp = std::get<FileService::FileStamp>(fileStamp);
    } else {
        m_fileStamp.reset();
    }
    SettingsService::addRecentFile(path);
    rebuildRecentMenu();
    return true;
}

bool MainWindow::openOwnedFile(const QString& name) {
    if (!confirmDiscard()) return false;
    const auto opened = UserFileService::openOwned(m_sessionRoot, m_sessionUser, name);
    if (std::holds_alternative<AppError>(opened)) {
        showNotice(this, QMessageBox::Critical, QStringLiteral("无法打开"),
                   std::get<AppError>(opened).message);
        return false;
    }
    QString fileName = name.trimmed();
    if (!fileName.endsWith(QStringLiteral(".lingya"), Qt::CaseInsensitive))
        fileName += QStringLiteral(".lingya");
    return openFile(UserFileService::userDir(m_sessionRoot, m_sessionUser) + QLatin1Char('/') +
                    fileName);   // 归属Own，走统一的加载/戳/最近文件流程
}

void MainWindow::showLoginDialog() {
    LoginDialog dialog(m_sessionRoot, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.loggedInUserName().isEmpty()) {
        m_sessionUser = dialog.loggedInUserName();   // 对话框内已校验凭据
        updateSessionActions();
        updateStatusLine();
    }
}

void MainWindow::showOwnedFilesDialog() {
    if (m_sessionUser.isEmpty()) return;
    UserFilesDialog dialog(m_sessionRoot, m_sessionUser, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFileName().isEmpty())
        openOwnedFile(dialog.selectedFileName());
}

void MainWindow::updateSessionActions() {
    const bool loggedIn = !m_sessionUser.isEmpty();
    if (m_loginAction != nullptr) m_loginAction->setVisible(!loggedIn);
    if (m_logoutAction != nullptr) m_logoutAction->setVisible(loggedIn);
    if (m_ownedFilesAction != nullptr) {
        m_ownedFilesAction->setVisible(loggedIn);
        m_ownedFilesAction->setEnabled(loggedIn);
    }
}

void MainWindow::setSessionRootForTest(const QString& path) {
    s_sessionRootForTest = path;   // 空=恢复默认AppLocalDataLocation（构造时生效）
}

void MainWindow::buildEditorActions(QMenu *editMenu)
{
    auto *clipboard = editMenu->addMenu(QStringLiteral("剪贴板"));
    clipboard
        ->addAction(QStringLiteral("复制子树"), QKeySequence::Copy, this,
                    [this] { copySelection(false); })
        ->setObjectName(QStringLiteral("copyTopic"));
    clipboard
        ->addAction(QStringLiteral("剪切子树（粘贴后移动）"), QKeySequence::Cut, this,
                    [this] { copySelection(true); })
        ->setObjectName(QStringLiteral("cutTopic"));
    clipboard
        ->addAction(QStringLiteral("粘贴为子主题"), QKeySequence::Paste, this,
                    &MainWindow::pasteSelection)
        ->setObjectName(QStringLiteral("pasteTopic"));
    editMenu->addAction(QStringLiteral("工作表默认样式…"), this, [this] { editNodeStyle(true); })
        ->setObjectName(QStringLiteral("editSheetStyle"));
    editMenu->addAction(QStringLiteral("关系线…"), this, &MainWindow::editRelations)
        ->setObjectName(QStringLiteral("editRelations"));
    editMenu->addAction(QStringLiteral("外框与概要…"), this, &MainWindow::editAnnotations)
        ->setObjectName(QStringLiteral("editAnnotations"));
    editMenu->addAction(QStringLiteral("主题批注…"), this, &MainWindow::editComments)
        ->setObjectName(QStringLiteral("editComments"));
    auto *sheets = menuBar()->addMenu(QStringLiteral("工作表"));
    sheets->addAction(QStringLiteral("新建工作表…"), this, &MainWindow::createSheet)
        ->setObjectName(QStringLiteral("createSheet"));
    sheets->addAction(QStringLiteral("工作表改名…"), this, &MainWindow::renameSheet)
        ->setObjectName(QStringLiteral("renameSheet"));
    sheets
        ->addAction(QStringLiteral("导图／鱼骨切换"), this,
                    [this]
                    {
                        const auto *tree = activeTreeSheet();
                        if (!tree)
                            return;
                        const auto result =
                            SheetCommands(m_commands, m_doc)
                                .changeSheetKind(tree->id, tree->kind == QStringLiteral("fishbone")
                                                               ? QStringLiteral("mindmap")
                                                               : QStringLiteral("fishbone"));
                        if (const auto *error = std::get_if<AppError>(&result))
                            showNotice(this, QMessageBox::Warning, QStringLiteral("无法换结构"),
                                       error->message);
                    })
        ->setObjectName(QStringLiteral("changeSheetKind"));
    sheets->addAction(QStringLiteral("删除当前工作表…"), this, &MainWindow::deleteSheet)
        ->setObjectName(QStringLiteral("deleteSheet"));
    auto *bar = addToolBar(QStringLiteral("工作表"));
    bar->setObjectName(QStringLiteral("sheetToolbar"));
    bar->setMovable(false);
    m_sheetSelector = new QComboBox(bar);
    m_sheetSelector->setObjectName(QStringLiteral("sheetSelector"));
    m_sheetSelector->setAccessibleName(QStringLiteral("当前工作表"));
    m_sheetSelector->setMinimumContentsLength(16);
    bar->addWidget(m_sheetSelector);
    bar->addAction(findChild<QAction *>(QStringLiteral("createSheet")));
    connect(m_sheetSelector, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                if (index < 0)
                    return;
                const ID id = m_sheetSelector->itemData(index).toString();
                if (id == m_doc->project().activeSheetId)
                    return;
                // 切表是视图定位而非内容编辑：走ProjectDocument::setActiveSheet，不进撤销栈、
                // 不递增sessionRevision（否则Ctrl+Z会撤掉"切表"、剪切令牌还会被切表弄失效，见BUG-023/D-M1-08）。
                // setActiveSheet内部已notifyChanged，这里不要再补一次。
                if (!m_doc->setActiveSheet(id))
                {
                    // 失败时下拉框已经跳到目标项，先按文档真实状态重建，避免界面与实际不一致。
                    rebuildSheets();
                    statusBar()->showMessage(QStringLiteral("无法切换工作表：目标工作表不存在"),
                                             5000);
                }
            });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            [this]
            {
                if (!m_cutToken.isEmpty() && QApplication::clipboard()->mimeData()->data(
                                                 "application/x-lingya-cut-token") != m_cutToken)
                {
                    m_cutNodeId.clear();
                    m_cutToken.clear();
                    syncCanvasSelection();
                }
            });
}

void MainWindow::rebuildSheets()
{
    const QSignalBlocker blocker(m_sheetSelector);
    m_sheetSelector->clear();
    bool gantt = false;
    for (const Sheet &sheet : m_doc->project().sheets)
        std::visit(
            [&](const auto &value)
            {
                m_sheetSelector->addItem(value.name, value.id);
                if (value.id == m_doc->project().activeSheetId)
                    gantt = value.kind == QStringLiteral("gantt");
            },
            sheet);
    m_sheetSelector->setCurrentIndex(m_sheetSelector->findData(m_doc->project().activeSheetId));
    if (m_displaySheetId != m_doc->project().activeSheetId)
    {
        m_displaySheetId = m_doc->project().activeSheetId;
        if (gantt)
            showGanttView();
        else
            m_centralStack->setCurrentIndex(0);
    }
}

void MainWindow::createSheet()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("createSheetDialog"));
    dialog.setWindowTitle(QStringLiteral("新建工作表"));
    auto *form = new QFormLayout(&dialog);
    auto *kind = new QComboBox(&dialog);
    kind->setObjectName(QStringLiteral("newSheetKind"));
    kind->addItem(QStringLiteral("思维导图"), "mindmap");
    kind->addItem(QStringLiteral("鱼骨图"), "fishbone");
    kind->addItem(QStringLiteral("流程图"), "flowchart");
    kind->addItem(QStringLiteral("甘特图"), "gantt");
    auto *name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("newSheetName"));
    form->addRow(QStringLiteral("类型"), kind);
    form->addRow(QStringLiteral("名称（留空自动命名）"), name);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    form->addRow(buttons);
    for (auto *button : buttons->buttons())
        if (auto *push = qobject_cast<QPushButton *>(button))
            push->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&]
            {
                const auto result = SheetCommands(m_commands, m_doc)
                                        .createSheet(kind->currentData().toString(), name->text());
                if (const auto *failure = std::get_if<AppError>(&result))
                    error->setText(failure->message);
                else
                {
                    dialog.accept();
                    fitCanvas();
                }
            });
    dialog.exec();
}

void MainWindow::renameSheet()
{
    TagNameDialog input(this);
    input.setWindowTitle(QStringLiteral("工作表改名"));
    input.setLabelText(QStringLiteral("工作表名称："));
    input.setOkButtonText(QStringLiteral("确定"));
    input.setTextValue(m_sheetSelector->currentText());
    if (input.exec() != QDialog::Accepted)
        return;
    const auto result = SheetCommands(m_commands, m_doc)
                            .renameSheet(m_doc->project().activeSheetId, input.textValue());
    if (const auto *error = std::get_if<AppError>(&result))
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法改名"), error->message);
}

void MainWindow::deleteSheet()
{
    const Project &project = m_doc->project();
    const ID id = project.activeSheetId;
    int cards = 0, routes = 0, drafts = 0, tasks = 0;
    for (const auto &card : project.cards)
        cards += card.sheetId == id;
    for (const auto &route : project.routes)
        routes += route.sheetId == id;
    for (const auto &draft : project.drafts)
        drafts += draft.sourceSheetId == id;
    for (const auto &task : project.tasks)
        tasks += task.source && task.source->sheetId == id;
    if (cards || routes || drafts || project.sheets.size() <= 1)
    {
        const auto result = SheetCommands(m_commands, m_doc).deleteSheet(id);
        if (const auto *error = std::get_if<AppError>(&result))
            showNotice(this, QMessageBox::Warning, QStringLiteral("无法删除工作表"),
                       error->message);
        return;
    }
    QMessageBox box(
        QMessageBox::Question, QStringLiteral("删除工作表"),
        QStringLiteral("删除“%1”？表中主题与连线将移除；%2个关联任务保留为独立任务。可以撤销。")
            .arg(m_sheetSelector->currentText())
            .arg(tasks),
        QMessageBox::Ok | QMessageBox::Cancel, this);
    box.button(QMessageBox::Ok)->setText(QStringLiteral("删除工作表"));
    box.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Ok)
        return;
    const auto result = SheetCommands(m_commands, m_doc).deleteSheet(id);
    if (const auto *error = std::get_if<AppError>(&result))
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法删除工作表"), error->message);
}

void MainWindow::copySelection(bool cut)
{
    const auto *tree = activeTreeSheet();
    if (!tree || !tree->nodes.contains(m_selectedNodeId))
        return;
    if (cut && m_selectedNodeId == tree->rootId)
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法剪切"),
                   QStringLiteral("根主题不能剪切；可以复制整棵树，或通过工作表菜单删除整张表。"));
        return;
    }
    const auto result = NodeCommands(m_commands, m_doc).copyFragment(tree->id, {m_selectedNodeId});
    if (const auto *error = std::get_if<AppError>(&result))
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法复制"), error->message);
        return;
    }
    const Fragment fragment = std::get<Fragment>(result);
    auto *mime = new QMimeData;
    mime->setData(Fragment::kMimeType,
                  QJsonDocument(fragment.toJson()).toJson(QJsonDocument::Compact));
    QStringList lines;
    std::function<void(ID, int)> append = [&](const ID &id, int depth)
    {
        lines.append(QString(depth * 2, QLatin1Char(' ')) + fragment.nodes.value(id).title);
        QVector<QPair<int, ID>> children;
        for (const auto &node : fragment.nodes)
            if (node.parentId == id)
                children.append({node.order, node.id});
        std::sort(children.begin(), children.end());
        for (const auto &child : children)
            append(child.second, depth + 1);
    };
    for (const auto &root : fragment.rootIds)
        append(root, 0);
    mime->setText(lines.join(QLatin1Char('\n')));
    m_cutNodeId.clear();
    m_cutToken.clear();
    if (cut)
    {
        m_cutNodeId = m_selectedNodeId;
        m_cutSheetId = tree->id;
        m_cutProjectId = m_doc->project().id;
        m_cutRevision = m_doc->sessionRevision();
        m_cutToken = QUuid::createUuid().toByteArray();
        mime->setData("application/x-lingya-cut-token", m_cutToken);
    }
    QApplication::clipboard()->setMimeData(mime);
    syncCanvasSelection();
    statusBar()->showMessage(
        cut ? QStringLiteral("已标记待剪切，粘贴成功后移动；按Esc取消。")
            : QStringLiteral("已复制子树；任务副本不带外部依赖，复习记录与讲解路线不复制。"),
        8000);
}

void MainWindow::pasteSelection()
{
    const auto *tree = activeTreeSheet();
    if (!tree || !tree->nodes.contains(m_selectedNodeId))
        return;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    const QByteArray token = mime->data("application/x-lingya-cut-token");
    if (!token.isEmpty())
    {
        if (token == m_cutToken && m_cutProjectId == m_doc->project().id &&
            m_cutSheetId != tree->id)
        {
            showNotice(
                this, QMessageBox::Warning, QStringLiteral("无法跨表剪切"),
                QStringLiteral(
                    "跨工作表移动的卡片和路线引用尚无统一迁移命令。请使用复制；源节点保留。"));
            return;
        }
        if (token != m_cutToken || m_cutProjectId != m_doc->project().id ||
            m_cutRevision != m_doc->sessionRevision())
        {
            showNotice(this, QMessageBox::Warning, QStringLiteral("剪切已失效"),
                       QStringLiteral("源工程、修订或剪贴板已变化，请重新剪切；源节点没有删除。跨工"
                                      "程传递请使用复制。"));
            return;
        }
        // 跨表剪切在上面的第一个分支里已经拦掉（token 一致且换表），这里不再重复判断。
        // applyEdit会发changed，先复制剪贴板字段，避免回调影响临时指针。
        const QByteArray bytes = mime->data(Fragment::kMimeType);
        const QString text = mime->text();
        const auto result =
            NodeCommands(m_commands, m_doc).moveSubtree(tree->id, m_cutNodeId, m_selectedNodeId);
        if (const auto *error = std::get_if<AppError>(&result))
        {
            showNotice(this, QMessageBox::Warning, QStringLiteral("无法移动"), error->message);
            return;
        }
        m_cutNodeId.clear();
        m_cutToken.clear();
        auto *copied = new QMimeData;
        copied->setData(Fragment::kMimeType, bytes);
        copied->setText(text);
        QApplication::clipboard()->setMimeData(copied);
        syncCanvasSelection();
        m_editorNote.clear();
        updateStatusLine();
        return;
    }
    const QByteArray bytes = mime->data(Fragment::kMimeType);
    if (bytes.isEmpty() || bytes.size() > Limits::kMaxFileBytes)
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法粘贴"),
                   QStringLiteral("剪贴板中没有可用的灵芽子树，或片段超过10MiB。请先复制主题。"));
        return;
    }
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(bytes, &parse);
    const auto parsed = Fragment::fromJson(json.object());
    if (parse.error != QJsonParseError::NoError || !json.isObject() ||
        std::holds_alternative<AppError>(parsed))
    {
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法粘贴"),
                   QStringLiteral("剪贴板片段格式不合法，原工程未改变。"));
        return;
    }
    const auto result = NodeCommands(m_commands, m_doc)
                            .pasteFragment(tree->id, m_selectedNodeId, std::get<Fragment>(parsed));
    if (const auto *error = std::get_if<AppError>(&result))
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法粘贴"), error->message);
    else
        selectNewNode(std::get<ID>(result));
}

void MainWindow::moveSelection(const ID &nodeId, const ID &parentId, const ID &beforeId)
{
    const auto *tree = activeTreeSheet();
    if (!tree)
        return;
    const auto result =
        NodeCommands(m_commands, m_doc).moveSubtree(tree->id, nodeId, parentId, beforeId);
    if (const auto *error = std::get_if<AppError>(&result))
        showNotice(this, QMessageBox::Warning, QStringLiteral("无法移动主题"), error->message);
    else
        selectNode(nodeId);
}

void MainWindow::selectNewNode(const ID &id)
{
    if (m_tagFilter->currentData().isValid())
    {
        statusBar()->showMessage(
            QStringLiteral("已新增主题；当前标签筛选保持，切回“全部标签”可查看新主题。"), 8000);
        return;
    }
    selectNode(id);
}

void MainWindow::editRelations()
{
    const auto *tree = activeTreeSheet();
    if (!tree)
        return;
    const ID sheetId = tree->id;
    EditorForm ui(this, "relationsDialog", QStringLiteral("关系线"));
    auto *source = new QComboBox(&ui.dialog), *target = new QComboBox(&ui.dialog);
    source->setObjectName("relationSource");
    target->setObjectName("relationTarget");
    for (const auto &node : tree->nodes)
    {
        source->addItem(node.title, node.id);
        target->addItem(node.title, node.id);
    }
    source->setCurrentIndex(source->findData(m_selectedNodeId));
    auto *label = new QLineEdit(&ui.dialog);
    label->setObjectName("relationLabel");
    auto *arrow = new QComboBox(&ui.dialog);
    arrow->setObjectName("relationArrow");
    arrow->addItem(QStringLiteral("无箭头"), "none");
    arrow->addItem(QStringLiteral("终点箭头"), "end");
    arrow->addItem(QStringLiteral("双向箭头"), "both");
    arrow->setCurrentIndex(1);
    auto *color = new QLineEdit(tree->defaultEdgeStyle.color, &ui.dialog);
    color->setObjectName("relationColor");
    auto *width = new QLineEdit(QString::number(tree->defaultEdgeStyle.width, 'g', 17), &ui.dialog);
    width->setObjectName("relationWidth");
    auto *dashed = new QCheckBox(QStringLiteral("虚线"), &ui.dialog);
    dashed->setObjectName("relationDashed");
    dashed->setChecked(tree->defaultEdgeStyle.dashed);
    ui.fields->addRow(QStringLiteral("起点"), source);
    ui.fields->addRow(QStringLiteral("终点"), target);
    ui.fields->addRow(QStringLiteral("标签"), label);
    ui.fields->addRow(QStringLiteral("箭头"), arrow);
    ui.fields->addRow(QStringLiteral("颜色（新建继承默认，选中后可改）"), color);
    ui.fields->addRow(QStringLiteral("宽度（0.1～20）"), width);
    ui.fields->addRow(dashed);
    ID selected;
    const auto refresh = [&]
    {
        ui.list->clear();
        for (const auto &relation : activeTreeSheet()->relations)
        {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1 → %2：%3")
                    .arg(activeTreeSheet()->nodes.value(relation.sourceId).title,
                         activeTreeSheet()->nodes.value(relation.targetId).title, relation.label),
                ui.list);
            item->setData(Qt::UserRole, relation.id);
        }
    };
    const auto fresh = [&]
    {
        selected.clear();
        ui.list->clearSelection();
        label->clear();
        source->setEnabled(true);
        target->setEnabled(true);
        color->setEnabled(false);
        width->setEnabled(false);
        dashed->setEnabled(false);
        ui.remove->setEnabled(false);
        ui.error->clear();
    };
    connect(ui.fresh, &QPushButton::clicked, &ui.dialog, fresh);
    connect(ui.list, &QListWidget::itemClicked, &ui.dialog,
            [&](QListWidgetItem *item)
            {
                selected = item->data(Qt::UserRole).toString();
                for (const auto &relation : activeTreeSheet()->relations)
                    if (relation.id == selected)
                    {
                        source->setCurrentIndex(source->findData(relation.sourceId));
                        target->setCurrentIndex(target->findData(relation.targetId));
                        source->setEnabled(false);
                        target->setEnabled(false);
                        label->setText(relation.label);
                        arrow->setCurrentIndex(arrow->findData(relation.arrow));
                        color->setText(relation.style.color);
                        width->setText(QString::number(relation.style.width, 'g', 17));
                        dashed->setChecked(relation.style.dashed);
                        color->setEnabled(true);
                        width->setEnabled(true);
                        dashed->setEnabled(true);
                        ui.remove->setEnabled(true);
                    }
            });
    connect(ui.apply, &QPushButton::clicked, &ui.dialog,
            [&]
            {
                NodeCommands nodes(m_commands, m_doc);
                if (selected.isEmpty())
                {
                    const auto result = nodes.addRelation(
                        sheetId, source->currentData().toString(), target->currentData().toString(),
                        label->text(), arrow->currentData().toString());
                    if (const auto *error = std::get_if<AppError>(&result))
                    {
                        ui.error->setText(error->message);
                        return;
                    }
                }
                else
                {
                    bool ok;
                    const double value = width->text().toDouble(&ok);
                    if (!ok)
                    {
                        ui.error->setText(QStringLiteral("关系线宽度必须为数字。"));
                        return;
                    }
                    RelationUpdate update;
                    update.label = label->text();
                    update.arrow = arrow->currentData().toString();
                    update.style = EdgeStyle{color->text().trimmed(), value, dashed->isChecked()};
                    const auto result = nodes.updateRelation(sheetId, selected, update);
                    if (const auto *error = std::get_if<AppError>(&result))
                    {
                        ui.error->setText(error->message);
                        return;
                    }
                }
                refresh();
                fresh();
            });
    connect(ui.remove, &QPushButton::clicked, &ui.dialog,
            [&]
            {
                const auto result =
                    NodeCommands(m_commands, m_doc).removeRelation(sheetId, selected);
                if (const auto *error = std::get_if<AppError>(&result))
                    ui.error->setText(error->message);
                else
                {
                    refresh();
                    fresh();
                }
            });
    refresh();
    fresh();
    ui.dialog.exec();
}

void MainWindow::editAnnotations()
{
    const auto *tree = activeTreeSheet();
    if (!tree)
        return;
    const ID sheetId = tree->id;
    EditorForm ui(this, "annotationsDialog", QStringLiteral("外框与概要"));
    auto *parent = new QComboBox(&ui.dialog);
    parent->setObjectName("annotationParent");
    for (const auto &node : tree->nodes)
        parent->addItem(node.title, node.id);
    parent->setCurrentIndex(
        parent->findData(tree->nodes.value(m_selectedNodeId).parentId.value_or(tree->rootId)));
    auto *kind = new QComboBox(&ui.dialog);
    kind->setObjectName("annotationKind");
    kind->addItem(QStringLiteral("外框"), "boundary");
    kind->addItem(QStringLiteral("概要"), "summary");
    auto *members = new QListWidget(&ui.dialog);
    members->setObjectName("annotationMembers");
    members->setSelectionMode(QAbstractItemView::MultiSelection);
    members->setMaximumHeight(140);
    auto *text = new QLineEdit(&ui.dialog);
    text->setObjectName("annotationText");
    auto *color = new QLineEdit(&ui.dialog);
    color->setObjectName("annotationColor");
    color->setPlaceholderText(QStringLiteral("留空使用主题色；或#RRGGBB"));
    ui.fields->addRow(QStringLiteral("父主题"), parent);
    ui.fields->addRow(QStringLiteral("类型"), kind);
    ui.fields->addRow(QStringLiteral("连续兄弟≥2（点击多选）"), members);
    ui.fields->addRow(QStringLiteral("文本"), text);
    ui.fields->addRow(QStringLiteral("颜色"), color);
    ID selected;
    const auto populate = [&]
    {
        members->clear();
        QVector<QPair<int, ID>> ordered;
        for (const auto &node : activeTreeSheet()->nodes)
            if (node.parentId == parent->currentData().toString())
                ordered.append({node.order, node.id});
        std::sort(ordered.begin(), ordered.end());
        for (const auto &entry : ordered)
        {
            auto *item =
                new QListWidgetItem(activeTreeSheet()->nodes.value(entry.second).title, members);
            item->setData(Qt::UserRole, entry.second);
        }
    };
    const auto refresh = [&]
    {
        ui.list->clear();
        for (const auto &annotation : activeTreeSheet()->annotations)
        {
            auto *item =
                new QListWidgetItem(QStringLiteral("%1（%2个分支）：%3")
                                        .arg(annotation.kind == "boundary" ? QStringLiteral("外框")
                                                                           : QStringLiteral("概要"))
                                        .arg(annotation.memberIds.size())
                                        .arg(annotation.text),
                                    ui.list);
            item->setData(Qt::UserRole, annotation.id);
        }
    };
    const auto fresh = [&]
    {
        selected.clear();
        ui.list->clearSelection();
        text->clear();
        color->clear();
        parent->setEnabled(true);
        kind->setEnabled(true);
        ui.remove->setEnabled(false);
        ui.error->clear();
        populate();
    };
    connect(parent, &QComboBox::currentIndexChanged, &ui.dialog, [&](int) { populate(); });
    connect(ui.fresh, &QPushButton::clicked, &ui.dialog, fresh);
    connect(ui.list, &QListWidget::itemClicked, &ui.dialog,
            [&](QListWidgetItem *item)
            {
                selected = item->data(Qt::UserRole).toString();
                for (const auto &annotation : activeTreeSheet()->annotations)
                    if (annotation.id == selected)
                    {
                        parent->setCurrentIndex(parent->findData(annotation.parentId));
                        populate();
                        kind->setCurrentIndex(kind->findData(annotation.kind));
                        parent->setEnabled(false);
                        kind->setEnabled(false);
                        text->setText(annotation.text);
                        color->setText(annotation.color);
                        for (int i = 0; i < members->count(); ++i)
                            members->item(i)->setSelected(annotation.memberIds.contains(
                                members->item(i)->data(Qt::UserRole).toString()));
                        ui.remove->setEnabled(true);
                    }
            });
    connect(ui.apply, &QPushButton::clicked, &ui.dialog,
            [&]
            {
                QStringList ids;
                for (auto *item : members->selectedItems())
                    ids.append(item->data(Qt::UserRole).toString());
                NodeCommands nodes(m_commands, m_doc);
                std::optional<AppError> error;
                if (selected.isEmpty())
                {
                    // 颜色留空即"跟随主题色"：写空串（NodeCommands.h 的颜色契约），读盘侧已允许空串，
                    // 渲染时回退到工作表主题色（BUG-022/§44），不再把当时的主题色字面值冻进批注。
                    const auto result = nodes.createAnnotation(
                        sheetId, parent->currentData().toString(), kind->currentData().toString(),
                        ids, text->text(), color->text().trimmed());
                    if (const auto *failure = std::get_if<AppError>(&result))
                        error = *failure;
                }
                else
                {
                    AnnotationUpdate update;
                    update.memberIds = ids;
                    update.text = text->text();
                    update.color = color->text().trimmed();
                    const auto result = nodes.updateAnnotation(sheetId, selected, update);
                    if (const auto *failure = std::get_if<AppError>(&result))
                        error = *failure;
                }
                if (error)
                {
                    ui.error->setText(error->message +
                                      QStringLiteral("。请选择同一父主题下至少两个连续兄弟。"));
                    return;
                }
                refresh();
                fresh();
            });
    connect(ui.remove, &QPushButton::clicked, &ui.dialog,
            [&]
            {
                const auto result =
                    NodeCommands(m_commands, m_doc).removeAnnotation(sheetId, selected);
                if (const auto *error = std::get_if<AppError>(&result))
                    ui.error->setText(error->message);
                else
                {
                    refresh();
                    fresh();
                }
            });
    refresh();
    fresh();
    ui.dialog.exec();
}

void MainWindow::editComments()
{
    const auto *tree = activeTreeSheet();
    if (!tree || !tree->nodes.contains(m_selectedNodeId))
        return;
    const ID sheetId = tree->id, nodeId = m_selectedNodeId;
    EditorForm ui(this, "commentsDialog", QStringLiteral("主题批注"));
    auto *author =
        new QLineEdit(m_sessionUser.isEmpty() ? QStringLiteral("匿名") : m_sessionUser, &ui.dialog);
    author->setObjectName("commentAuthor");
    auto *body = new QPlainTextEdit(&ui.dialog);
    body->setObjectName("commentBody");
    ui.fields->addRow(QStringLiteral("作者显示名"), author);
    ui.fields->addRow(QStringLiteral("正文（最多2000字符）"), body);
    ui.remove->setText(QStringLiteral("标记已解决／重新打开"));
    ui.remove->setEnabled(false);
    ID selected;
    bool resolved = false;
    const auto refresh = [&]
    {
        ui.list->clear();
        for (const auto &comment : activeTreeSheet()->comments)
            if (comment.nodeId == nodeId)
            {
                auto *item = new QListWidgetItem(
                    QStringLiteral("[%1] %2 · %3\n%4")
                        .arg(comment.resolved ? QStringLiteral("已解决") : QStringLiteral("待处理"),
                             comment.author,
                             comment.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm"),
                             comment.body),
                    ui.list);
                item->setData(Qt::UserRole, comment.id);
                item->setToolTip(QStringLiteral("更新于 %1")
                                     .arg(comment.updatedAt.toLocalTime().toString(Qt::ISODate)));
            }
    };
    const auto fresh = [&]
    {
        selected.clear();
        ui.list->clearSelection();
        body->clear();
        body->setReadOnly(false);
        author->setReadOnly(false);
        ui.apply->setEnabled(true);
        ui.remove->setEnabled(false);
        ui.error->clear();
    };
    connect(ui.fresh, &QPushButton::clicked, &ui.dialog, fresh);
    connect(ui.list, &QListWidget::itemClicked, &ui.dialog,
            [&](QListWidgetItem *item)
            {
                selected = item->data(Qt::UserRole).toString();
                for (const auto &comment : activeTreeSheet()->comments)
                    if (comment.id == selected)
                    {
                        author->setText(comment.author);
                        body->setPlainText(comment.body);
                        resolved = comment.resolved;
                        body->setReadOnly(true);
                        author->setReadOnly(true);
                        ui.apply->setEnabled(false);
                        ui.remove->setEnabled(true);
                    }
            });
    connect(ui.apply, &QPushButton::clicked, &ui.dialog,
            [&]
            {
                const auto result =
                    NodeCommands(m_commands, m_doc)
                        .addComment(sheetId, nodeId, author->text(), body->toPlainText());
                if (const auto *error = std::get_if<AppError>(&result))
                    ui.error->setText(error->message);
                else
                {
                    refresh();
                    fresh();
                }
            });
    connect(ui.remove, &QPushButton::clicked, &ui.dialog,
            [&]
            {
                const auto result =
                    NodeCommands(m_commands, m_doc).resolveComment(sheetId, selected, !resolved);
                if (const auto *error = std::get_if<AppError>(&result))
                    ui.error->setText(error->message);
                else
                {
                    refresh();
                    fresh();
                }
            });
    refresh();
    ui.dialog.exec();
}

void MainWindow::addDecorations(const TreeSheet &sheet)
{
    const auto addPath =
        [this](const QPainterPath &path, const QPen &pen, const QString &kind, const ID &id)
    {
        auto *item = m_scene->addPath(path, pen);
        item->setZValue(-0.5);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setData(0, kind);
        item->setData(1, id);
        return item;
    };
    const auto addText = [this](const QString &text, const QPointF &position, const QColor &color,
                                const QString &kind, const ID &id)
    {
        auto *item = m_scene->addText(QString(), canvasFont());
        item->setPlainText(text);
        item->setTextWidth(220);
        item->setDefaultTextColor(color);
        item->setPos(position);
        item->setZValue(-0.25);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setData(0, kind);
        item->setData(1, id);
        return item;
    };
    for (const auto &relation : sheet.relations)
    {
        const auto *source = m_nodeItems.value(relation.sourceId, nullptr);
        const auto *target = m_nodeItems.value(relation.targetId, nullptr);
        if (!source || !target)
            continue; // 端点折叠隐藏时仅隐藏连线，不改数据
        const bool right = target->pos().x() >= source->pos().x();
        const QPointF start = source->mapToScene(source->horizontalAnchor(right));
        const QPointF end = target->mapToScene(target->horizontalAnchor(!right));
        const qreal offset = qMax(60.0, qAbs(end.x() - start.x()) * 0.35);
        QPainterPath path(start);
        const QPointF controlStart = start + QPointF(right ? offset : -offset, -36);
        const QPointF controlEnd = end + QPointF(right ? -offset : offset, -36);
        path.cubicTo(controlStart, controlEnd, end);
        QPen pen(QColor(relation.style.color), relation.style.width);
        if (relation.style.dashed)
            pen.setStyle(Qt::DashLine);
        addPath(path, pen, "relation", relation.id);
        const auto arrow = [&](QPointF tip, QPointF previous)
        {
            QLineF direction(previous, tip);
            direction.setLength(11);
            const QPointF back = tip - (direction.p2() - direction.p1());
            const QPointF normal(-(direction.dy()), direction.dx());
            QPainterPath head(tip);
            head.lineTo(back + normal * 0.4);
            head.moveTo(tip);
            head.lineTo(back - normal * 0.4);
            addPath(head, pen, "relationArrow", relation.id);
        };
        if (relation.arrow != "none")
            arrow(end, controlEnd);
        if (relation.arrow == "both")
            arrow(start, controlStart);
        if (!relation.label.isEmpty())
            addText(relation.label, path.pointAtPercent(0.5) + QPointF(-110, -32), pen.color(),
                    "relationLabel", relation.id);
    }
    QRectF allNodes;
    for (const auto *node : m_nodeItems)
        allNodes = allNodes.united(node->sceneBoundingRect());
    qreal nextRightY = allNodes.top(), nextLeftY = allNodes.top();
    for (const auto &annotation : sheet.annotations)
    {
        QMap<int, QRectF> sections;
        int visibleMembers = 0;
        for (const auto &id : annotation.memberIds)
            visibleMembers += m_nodeItems.contains(id);
        if (!visibleMembers)
            continue;
        for (auto it = m_nodeItems.constBegin(); it != m_nodeItems.constEnd(); ++it)
        {
            ID id = it.key();
            while (sheet.nodes.contains(id))
            {
                if (annotation.memberIds.contains(id))
                {
                    const int side = m_nodeItems.value(id)->pos().x() < 0 ? -1 : 1;
                    sections[side] = sections.value(side).united(it.value()->sceneBoundingRect());
                    break;
                }
                const auto parent = sheet.nodes.value(id).parentId;
                if (!parent)
                    break;
                id = *parent;
            }
        }
        // 双侧导图的同一组可以横跨左右：分侧绘制，避免大矩形把根和无关分支也框入。
        for (QRectF bounds : sections)
        {
            bounds.adjust(-16, -16, 16, 16);
            // 空色=跟随主题色（NodeCommands.h 的颜色契约）：回退到本表主题色，而不是写死常量。
            const QColor color(annotation.color.isEmpty() ? sheet.defaultEdgeStyle.color
                                                          : annotation.color);
            QPainterPath path;
            const bool right = bounds.center().x() >= 0;
            const qreal x = right ? bounds.right() : bounds.left();
            if (annotation.kind == "boundary")
                path.addRoundedRect(bounds, 14, 14);
            else
            {
                path.moveTo(x, bounds.top());
                path.lineTo(x + (right ? 10 : -10), bounds.top());
                path.lineTo(x + (right ? 10 : -10), bounds.bottom());
                path.lineTo(x, bounds.bottom());
            }
            auto *frame = addPath(path, QPen(color, 1.5), annotation.kind, annotation.id);
            frame->setZValue(-2);
            if (!annotation.text.isEmpty())
            {
                // 独立侧边通道在全部节点之外；逐条向下避让文本，避免长概要覆盖无关分支。
                qreal &nextY = right ? nextRightY : nextLeftY;
                const qreal y = qMax(bounds.top(), nextY);
                const qreal textX = right ? allNodes.right() + 48 : allNodes.left() - 268;
                auto *label = addText(annotation.text, QPointF(textX, y), color, "annotationLabel",
                                      annotation.id);
                nextY = y + label->boundingRect().height() + 16;
                QPainterPath connector(QPointF(x, bounds.center().y()));
                connector.lineTo(right ? textX : textX + 220,
                                 y + label->boundingRect().height() / 2);
                auto *line =
                    addPath(connector, QPen(color, 1), "annotationConnector", annotation.id);
                line->setZValue(-2);
            }
        }
    }
}

}  // namespace lingya
