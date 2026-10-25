#include "app/MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QLabel>
#include <QTimer>


#include "app/RecoveryDialog.h"
#include "commands/CommandService.h"
#include "domain/ProjectFactory.h"
#include "domain/Limits.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include "services/FileService.h"
#include "services/RecoveryService.h"
#include "services/SettingsService.h"

namespace lingya {


namespace {
QFont canvasFont() {
    QFont font(QStringLiteral("Microsoft YaHei"));   // Windows优先微软雅黑（规范§9）
    font.setPixelSize(16);                           // 节点默认字号（规范§4.2）
    return font;
}
}  // namespace

QString MainWindow::s_recoveryRootForTest;

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
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->viewport()->installEventFilter(this);   // 双击画布节点重命名
    // 中央区两页：页0思维导图画布，页1甘特图（M3丙接入，见setGanttView）
    m_centralStack = new QStackedWidget(this);
    m_centralStack->addWidget(m_view);
    auto* ganttPlaceholder = new QLabel(QStringLiteral("甘特图视图待接入（M3）：由丙经 setGanttView 装入"), this);
    ganttPlaceholder->setAlignment(Qt::AlignCenter);
    m_ganttPage = ganttPlaceholder;
    m_centralStack->addWidget(m_ganttPage);
    setCentralWidget(m_centralStack);

    buildMenus();
    connect(m_doc, &ProjectDocument::changed, this, &MainWindow::onDocumentChanged);

    // 恢复快照（规范§11.3、原设计§8）：30秒定时，仅脏文档且内容变化时写一份；
    // 每工程保留最近5份成功快照由RecoveryService写入后修剪
    m_recoveryRoot = s_recoveryRootForTest.isEmpty() ? RecoveryService::defaultRoot()
                                                     : s_recoveryRootForTest;
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
    editMenu->addAction(QStringLiteral("重命名根节点(&M)"), QKeySequence("F2"), this,
                        &MainWindow::renameRoot);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(QStringLiteral("思维导图(&M)"), this, [this] { m_centralStack->setCurrentIndex(0); });
    viewMenu->addAction(QStringLiteral("甘特图(&G)"), this, [this] { m_centralStack->setCurrentIndex(1); });
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
        QMessageBox::critical(this, QStringLiteral("无法采用快照"),
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
        QMessageBox::warning(this, QStringLiteral("无法采用快照"),
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
    // 双击节点重命名（M0简化交互；完整编辑交互细则见规范§5.2，M1实现）
    if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const QPointF scenePos = m_view->mapToScene(mouse->position().toPoint());
        if (dynamic_cast<NodeItem*>(m_scene->itemAt(scenePos, QTransform())) != nullptr) {
            renameRoot();
            return true;
        }
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
    const auto result = FileService::load(path);
    if (std::holds_alternative<AppError>(result)) {
        // 坏文件不覆盖当前工作（R04）：原工程保持不变
        QMessageBox::critical(this, QStringLiteral("无法打开"),
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
        QMessageBox::critical(this, QStringLiteral("保存失败"),
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

void MainWindow::renameRoot() {
    const TreeSheet* sheet = activeTreeSheet();
    if (sheet == nullptr) return;
    const QString oldTitle = sheet->nodes.value(sheet->rootId).title;

    bool ok = false;
    const QString title =
        QInputDialog::getText(this, QStringLiteral("重命名根节点"), QStringLiteral("新的标题："),
                              QLineEdit::Normal, oldTitle, &ok);
    if (!ok) return;
    renameRootTo(title);
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
        QMessageBox::information(this, QStringLiteral("最近文件"),
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
    const auto result = m_commands->applyEdit(
        QStringLiteral("重命名根节点"), [sheetId, nodeId, title](Project& project) {
            for (Sheet& s : project.sheets) {
                if (auto* tree = std::get_if<TreeSheet>(&s);
                    tree != nullptr && tree->id == sheetId) {
                    TreeNode& node = tree->nodes[nodeId];
                    node.title = title;
                    ++node.contentRevision;
                }
            }
        });
    if (std::holds_alternative<AppError>(result)) {
        QMessageBox::warning(this, QStringLiteral("无法重命名"),
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
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, QStringLiteral("未保存的修改"), QStringLiteral("当前工程已修改，是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
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
    m_recoveryNote.clear();   // 新建/打开不能沿用上一工程的快照时间或失败状态
    m_doc->replaceProject(std::move(project));
    m_commands->clearHistory();   // 打开新工程清空撤销历史（规范§5.1）
    m_doc->setClean();            // 新建/打开后尚未编辑，应为干净状态（规范§8）
    m_doc->notifyChanged();       // 状态变更完成后统一通知（D-M0-13）
}

void MainWindow::rebuildCanvas() {
    m_scene->clear();
    const TreeSheet* sheet = activeTreeSheet();
    if (sheet == nullptr) return;
    const TreeNode& root = sheet->nodes.value(sheet->rootId);
    auto* item = new NodeItem(root.title, canvasFont());
    m_scene->addItem(item);
    m_scene->setSceneRect(item->boundingRect().adjusted(-80, -80, 80, 80));
}

void MainWindow::onDocumentChanged() {
    rebuildCanvas();
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
    QString line = QStringLiteral("阶段M0纵向验证 · 修订 %1 · %2")
                       .arg(m_doc->sessionRevision())
                       .arg(state);
    // 恢复快照与正式保存分别显示状态（原设计§8）：备注为最近一次快照写入时间或失败提示
    if (!m_recoveryNote.isEmpty()) line += QStringLiteral(" · ") + m_recoveryNote;
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

}  // namespace lingya
