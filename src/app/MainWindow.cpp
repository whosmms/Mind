#include "app/MainWindow.h"

#include "layout/TreeLayout.h"
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>
#include <algorithm>

#include "app/LoginDialog.h"
#include "app/RecoveryDialog.h"
#include "app/UserFilesDialog.h"
#include "charts/GanttView.h"
#include "commands/CommandService.h"
#include "commands/NodeCommands.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "graphics/NodeItem.h"
#include "services/FileService.h"
#include "services/RecoveryService.h"
#include "services/SessionService.h"
#include "services/SettingsService.h"
#include "services/UserFileService.h"

namespace lingya {


namespace {
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
    m_outline = new QTreeWidget(outlineDock);
    m_outline->setObjectName(QStringLiteral("topicOutline"));
    m_outline->setHeaderLabel(QStringLiteral("主题（Tab新增子主题，F2改名）"));
    m_outline->setMinimumWidth(280);
    m_outline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outline->installEventFilter(this);
    outlineDock->setWidget(m_outline);
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
                        &MainWindow::newProject);
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

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(QStringLiteral("思维导图(&M)"), this, [this] { m_centralStack->setCurrentIndex(0); });
    auto *ganttAction =
        viewMenu->addAction(QStringLiteral("甘特图(&G)"), this, &MainWindow::showGanttView);
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
}

void MainWindow::showGanttView()
{
    if (qobject_cast<QLabel *>(m_ganttPage) != nullptr)
    {
        auto *gantt = new GanttView(this);
        gantt->setTasks(m_doc->project().tasks);
        setGanttView(gantt);
    }
    else
    {
        m_centralStack->setCurrentIndex(1);
    }
    if (m_doc->project().tasks.isEmpty())
        statusBar()->showMessage(QStringLiteral("暂无任务。打开含排期任务的工程后可查看时间轴。"));
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
    if (watched == m_view->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick))
    {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const QPointF position = m_view->mapToScene(mouse->position().toPoint());
            if (const auto *item =
                    dynamic_cast<NodeItem *>(m_scene->itemAt(position, QTransform())))
            {
                const ID nodeId = item->nodeId();
                selectNode(nodeId);
                m_view->setFocus();
                if (event->type() == QEvent::MouseButtonDblClick)
                    renameSelectedNode();
                return true;
            }
        }
    }
    if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonRelease)
        syncCanvasSelection();
    // 只处理大纲/画布，文本对话框和输入法预编辑不经过此入口。
    if ((watched == m_outline || watched == m_view || watched == m_view->viewport()) &&
        event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
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
            name = QFileInfo(m_filePath).completeBaseName();   // 我的文件原文件名保存
        } else {
            const QString fallback = m_filePath.isEmpty()
                                         ? m_doc->project().title
                                         : QFileInfo(m_filePath).completeBaseName();
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
    selectNode(std::get<ID>(result));
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
    selectNode(std::get<ID>(result));
}

void MainWindow::deleteSelection()
{
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
                                     "同时清理：%3条关系、%4项外框/概要、%5条批注。\n"
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
    const QSignalBlocker blocker(m_outline);
    QSet<ID> collapsed;
    for (QTreeWidgetItemIterator it(m_outline); *it; ++it)
        if (!(*it)->isExpanded())
            collapsed.insert((*it)->data(0, Qt::UserRole).toString());
    m_outline->clear();
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr)
    {
        m_selectedNodeId.clear();
        return;
    }
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
}

void MainWindow::selectNode(const ID &nodeId)
{
    const TreeSheet *sheet = activeTreeSheet();
    if (sheet == nullptr || !sheet->nodes.contains(nodeId))
        return;
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
        it.value()->setSelected(it.key() == m_selectedNodeId);
    const TreeSheet *sheet = activeTreeSheet();
    const bool nonRoot = sheet != nullptr && sheet->nodes.contains(m_selectedNodeId) &&
                         m_selectedNodeId != sheet->rootId;
    m_siblingAction->setEnabled(nonRoot);
    m_deleteAction->setEnabled(nonRoot);
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
    if (sheet == nullptr) return;
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
        const NodeItem measured(it.value().title, canvasFont());
        const QRectF bounds = measured.boundingRect();
        sizes.append({it.key(), bounds.width(), bounds.height()});
    }
    const auto layout = TreeLayout::layout(display, sizes);
    if (const auto *error = std::get_if<AppError>(&layout))
    {
        m_canvasError = error->message;
        m_scene->addText(QStringLiteral("无法显示导图：%1").arg(error->message), canvasFont());
        m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
        return;
    }
    for (const auto &position : std::get<QVector<TreeLayout::NodePosition>>(layout))
    {
        auto *item = new NodeItem(display.nodes.value(position.id).title, canvasFont());
        item->setNodeId(position.id);
        item->setPos(position.x, position.y);
        m_scene->addItem(item);
        m_nodeItems.insert(position.id, item);
    }
    for (auto it = m_nodeItems.constBegin(); it != m_nodeItems.constEnd(); ++it)
    {
        const auto parentId = display.nodes.value(it.key()).parentId;
        if (!parentId.has_value() || !m_nodeItems.contains(*parentId))
            continue;
        const NodeItem *parent = m_nodeItems.value(*parentId);
        const NodeItem *child = it.value();
        const bool right = child->pos().x() >= parent->pos().x();
        const QPointF start = parent->mapToScene(
            QPointF(right ? parent->nodeRect().right() : parent->nodeRect().left(), 0));
        const QPointF end = child->mapToScene(
            QPointF(right ? child->nodeRect().left() : child->nodeRect().right(), 0));
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
    syncCanvasSelection();
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80));
    focusSelectedNode();
}

void MainWindow::onDocumentChanged() {
    rebuildOutline();
    rebuildCanvas();
    if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage))
        gantt->setTasks(m_doc->project().tasks);
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
    QString line = QStringLiteral("思维导图 · 大纲与画布同步 · 修订 %1 · %2")
                       .arg(m_doc->sessionRevision())
                       .arg(state);
    // 恢复快照与正式保存分别显示状态（原设计§8）：备注为最近一次快照写入时间或失败提示
    if (!m_recoveryNote.isEmpty()) line += QStringLiteral(" · ") + m_recoveryNote;
    if (!m_canvasError.isEmpty())
        line += QStringLiteral(" · 布局失败：%1").arg(m_canvasError);
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

}  // namespace lingya
