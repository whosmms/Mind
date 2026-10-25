#include <QtTest>

#include "charts/GanttView.h"
#include "commands/CommandService.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include <QAbstractButton>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>

#include <cstdio>
#include <cstdlib>

#include "app/MainWindow.h"
#include "app/RecoveryDialog.h"
#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "services/FileService.h"
#include "services/RecoveryService.h"
#include "services/SettingsService.h"

using namespace lingya;

// UI层测试（规范§3 tst_ui规划、§19.1）：进程内验证快捷键绑定与文件打开行为。
// QTest::keyClick走Qt事件系统（ShortcutMap），不经Windows输入队列，
// 因此不受真实环境IME与前台焦点干扰，可直接判定快捷键绑定正确性。
class TstUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void undoRedoViaShortcuts();
    void undoRedoDisabledBeforeAnyEdit();
    void openFileRestoresContent();
    void openFileFailureKeepsProject();
    void recentFilesMenuOpensEntry();
    void recentFilesMissingEntryRemoved();
    void ganttHostSwitchAndAttach();
    void recentFilesCancelKeepsUnsavedContentAndEntry();
    void outlineEditingRoundTrip();
    void outlineCancelAndUndoSelection();
    void rootRenameUsesSharedRulesAndTaskLinkage();
    void ganttLazyLoadRefreshAndUndo();
    void previewZoomAndLongTitle();
    void treeCanvasNodesEdgesSelectionAndRename();
    void treeCanvasHundredNodesRoundTrip();
    void treeCanvasCollapsedSelectionIsViewOnly();
    void multilineTitleInputMethodAndCancel();
    // 恢复快照（规范§11.3、原设计§8）——放最后：前序测试在隔离空恢复根目录下不受影响
    void recoverySnapshotWrittenOnlyForDirtyChangedContent();
    void recoveryAdoptionGoesThroughCommandTransaction();
    void recoveryOfferDialogAdoptAndIgnoreAll();
    void recoveryAdoptionFromOpenedFileRefreshesTitle();
    void recoveryAdoptionSurvivesRestartAndFailedSave();
    void recoveryOpeningAnotherProjectClearsSnapshotStatus();
    void recoveryProcessCrashEndToEnd();

private:
    QTemporaryDir m_settingsDir;   // QSettings重定向目录（initTestCase设置，成员保活）
    QTemporaryDir m_recoveryDir;   // 恢复快照重定向目录（initTestCase设置，成员保活）

    static QMap<ID, NodeItem *> canvasNodes(QGraphicsView *view)
    {
        QMap<ID, NodeItem *> result;
        for (QGraphicsItem *item : view->scene()->items())
            if (auto *node = dynamic_cast<NodeItem *>(item))
                result.insert(node->nodeId(), node);
        return result;
    }

    static Project canvasProject(int groups = 2, int children = 2)
    {
        Project project = ProjectFactory::createMindmap(QStringLiteral("整树画布验证"));
        auto &sheet = std::get<TreeSheet>(project.sheets.first());
        sheet.nodes[sheet.rootId].title = QStringLiteral("课程项目");
        for (int group = 0; group < groups; ++group)
        {
            TreeNode branch;
            branch.id = canvasId(QStringLiteral("branch-%1").arg(group));
            branch.title = QStringLiteral("阶段%1").arg(group + 1);
            branch.parentId = sheet.rootId;
            branch.order = group;
            sheet.nodes.insert(branch.id, branch);
            for (int child = 0; child < children; ++child)
            {
                TreeNode node;
                node.id = canvasId(QStringLiteral("leaf-%1-%2").arg(group).arg(child));
                node.title = QStringLiteral("任务%1-%2").arg(group + 1).arg(child + 1);
                node.parentId = branch.id;
                node.order = child;
                sheet.nodes.insert(node.id, node);
            }
        }
        return project;
    }

    static ID canvasId(const QString &name)
    {
        // 测试标签映射为稳定UUID，满足工程文件严格读入规则。
        const QUuid space(QStringLiteral("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
        return QUuid::createUuidV5(space, name.toUtf8()).toString(QUuid::WithoutBraces);
    }

    static void answerTitleDialog(const QString &title, bool accept = true)
    {
        // 鼠标事件可能先处理定时器再打开对话框，等待可见实例后再应答。
        auto *timer = new QTimer(qApp);
        timer->setInterval(1);
        connect(timer, &QTimer::timeout, timer,
                [timer, title, accept]
                {
                    for (QWidget *widget : QApplication::topLevelWidgets())
                    {
                        if (auto *dialog = qobject_cast<QInputDialog *>(widget);
                            dialog != nullptr && dialog->isVisible())
                        {
                            timer->stop();
                            dialog->setTextValue(title);
                            if (accept)
                                dialog->accept();
                            else
                                dialog->reject();
                            timer->deleteLater();
                            return;
                        }
                    }
                });
        timer->start();
        QTimer::singleShot(3000, timer, &QObject::deleteLater);
    }

    // 进程级崩溃E2E子进程阶段（initTestCase识别LINGYA_E2E_PHASE=1进入）：
    // 写快照后不带任何清理地硬退出，模拟真实崩溃；失败以退出码2报告
    void runCrashPhaseChildAndExit();

    static void autoCloseMessageBoxes() {
        // openFile失败路径会弹模态错误框，测试中定时自动关闭（close对QMessageBox等同取消）
        QTimer::singleShot(0, [] {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(top)) box->close();
        });
    }
    // 找到“最近文件”子菜单（标题含该文字即可，不依赖快捷键助记符细节）
    QMenu* findRecentMenu(const MainWindow& window) const {
        const auto menus = window.findChildren<QMenu*>();
        for (QMenu* menu : menus)
            if (menu->title().contains(QStringLiteral("最近文件"))) return menu;
        return nullptr;
    }
    // 当前顶层恢复预览对话框（offerRecoverySnapshots用open()显示，非阻塞）。
    // 只认可见实例：已finish但deleteLater尚未执行的旧对话框保持隐藏，不应误认
    static RecoveryDialog* findRecoveryDialog() {
        for (QWidget* top : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<RecoveryDialog*>(top); dialog != nullptr && dialog->isVisible())
                return dialog;
        return nullptr;
    }
    // 按文字前缀找按钮（如“采用(&A)”）
    static QAbstractButton* findButton(const QWidget& parent, const QString& prefix) {
        const auto buttons = parent.findChildren<QAbstractButton*>();
        for (QAbstractButton* button : buttons)
            if (button->text().startsWith(prefix)) return button;
        return nullptr;
    }
    // 生成一份快照内容：工厂工程+根主题改名（测试夹具直接构造值，非运行时行为）
    static Project makeRecoveredProject(const QString& projectTitle, const QString& rootTitle) {
        Project project = ProjectFactory::createMindmap(projectTitle);
        for (Sheet& sheet : project.sheets)
            if (auto* tree = std::get_if<TreeSheet>(&sheet)) {
                tree->nodes[tree->rootId].title = rootTitle;
                ++tree->nodes[tree->rootId].contentRevision;
            }
        return project;
    }
};

void TstUi::initTestCase() {
#ifdef Q_OS_WIN
    // 无屏幕平台不自动枚举Windows字体；显式加载仅影响测试进程。
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
    {
        const QString fontPath =
            QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
        if (QFile::exists(fontPath))
        {
            QFontDatabase::addApplicationFont(fontPath);
            QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
        }
    }
#endif
    // 测试隔离：QSettings重定向到临时目录（IniFormat），不读写真实用户设置
    QVERIFY(m_settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    qApp->setOrganizationName(QStringLiteral("lingya-selftest"));
    qApp->setApplicationName(QStringLiteral("lingya-tst-ui"));
    // 恢复快照同样重定向到空临时目录：真实AppLocalData中的遗留快照不会弹预览对话框
    QVERIFY(m_recoveryDir.isValid());
    MainWindow::setRecoveryRootForTest(m_recoveryDir.path());
    // 进程级崩溃E2E的子进程分支：完成快照写入后硬退出，不再执行任何测试槽
    if (qEnvironmentVariable("LINGYA_E2E_PHASE") == QStringLiteral("1"))
        runCrashPhaseChildAndExit();
}

void TstUi::runCrashPhaseChildAndExit() {
    const QString root = qEnvironmentVariable("LINGYA_E2E_ROOT");
    if (root.isEmpty()) _exit(2);
    MainWindow::setRecoveryRootForTest(root);
    MainWindow window;
    if (!window.renameRootTo(QStringLiteral("崩溃链路内容"))) _exit(2);
    // 30秒定时回调直接调用：脏文档且内容变化，应写一份快照
    if (!QMetaObject::invokeMethod(&window, "writeRecoverySnapshot")) _exit(2);
    const QVector<RecoveryService::SnapshotInfo> snapshots =
        RecoveryService::list(root, window.currentProjectId());
    if (snapshots.size() != 1) _exit(2);
    fprintf(stdout, "LINGYA_E2E_PROJECT=%s\n", qPrintable(window.currentProjectId()));
    fflush(stdout);
    // 模拟崩溃：不执行析构、不清理快照、不做任何保存，直接终止进程
    _exit(1);
}

void TstUi::undoRedoDisabledBeforeAnyEdit() {
    // 规范§5.1：未做任何编辑时撤销/重做栈为空；重启或打开新工程后历史清空
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowActive(&w));   // WindowShortcut要求窗口处于active状态
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));

    // 无编辑历史时按Ctrl+Z/Ctrl+Y不应有任何效果
    QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));
}

void TstUi::undoRedoViaShortcuts() {
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowActive(&w));   // WindowShortcut要求窗口处于active状态
    QVERIFY(w.renameRootTo(QStringLiteral("第一次修改")));
    QCOMPARE(w.currentRootTitle(), QStringLiteral("第一次修改"));

    // 注意：QTest::keyClick(QWindow*,...)走窗口系统事件流（QShortcutMap），
    // 与真实键盘一致；QWidget重载会绕过快捷键匹配导致误报
    QWindow* win = w.windowHandle();
    QVERIFY(win != nullptr);

    // Ctrl+Z撤销：回“中心主题”
    QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));

    // Ctrl+Y重做
    QTest::keyClick(win, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("第一次修改"));

    // Ctrl+Z撤销后再用Ctrl+Shift+Z重做（统一规范§14.1的别名）
    QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));
    QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("第一次修改"));

    // 连续多次编辑后逐步撤销到最初
    QVERIFY(w.renameRootTo(QStringLiteral("第二次修改")));
    QVERIFY(w.renameRootTo(QStringLiteral("第三次修改")));
    QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("第二次修改"));
    QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("第一次修改"));
    QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));
}

void TstUi::openFileRestoresContent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui保存.lingya"));

    MainWindow w1;
    QVERIFY(w1.renameRootTo(QStringLiteral("保存内容验证")));
    QVERIFY(w1.saveProjectTo(path));

    MainWindow w2;
    QVERIFY(w2.openFile(path));
    QCOMPARE(w2.currentRootTitle(), QStringLiteral("保存内容验证"));
    // 工程标题来自文件：M0无修改工程标题的入口，打开文件后标题栏显示文件基名（D-M0-12）
    QVERIFY(w2.windowTitle().contains(QStringLiteral("ui保存")));
}

void TstUi::openFileFailureKeepsProject() {
    MainWindow w;
    QVERIFY(w.renameRootTo(QStringLiteral("保留验证")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bad.lingya"));
    QFile bad(path);
    QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Text));
    bad.write("{ 这是坏JSON");
    bad.close();

    autoCloseMessageBoxes();
    QVERIFY(!w.openFile(path));   // 加载失败
    QCOMPARE(w.currentRootTitle(), QStringLiteral("保留验证"));   // 原工程保持不变（R04）
}

void TstUi::recentFilesMenuOpensEntry() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("最近验证.lingya"));

    MainWindow writer;
    QVERIFY(writer.renameRootTo(QStringLiteral("最近文件内容")));
    QVERIFY(writer.saveProjectTo(path));   // 另存成功即记录最近文件

    // 新窗口：另存后“最近文件”子菜单含该路径
    QMenu* recent = findRecentMenu(writer);
    QVERIFY(recent != nullptr);
    QVERIFY(!recent->actions().isEmpty());
    bool found = false;
    for (QAction* action : recent->actions())
        found = found || action->text() == QFileInfo(path).absoluteFilePath();
    QVERIFY(found);

    // 第二个窗口新建工程后，通过菜单项重新打开该文件
    MainWindow reader;
    QVERIFY(reader.renameRootTo(QStringLiteral("临时内容")));
    QVERIFY(reader.saveProjectTo(dir.filePath(QStringLiteral("另一工程.lingya"))));
    QMenu* readerRecent = findRecentMenu(reader);
    QVERIFY(readerRecent != nullptr);
    QAction* entry = nullptr;
    for (QAction* action : readerRecent->actions())
        if (action->text() == QFileInfo(path).absoluteFilePath()) entry = action;
    QVERIFY(entry != nullptr);

    entry->trigger();   // 无未保存修改时直接打开，不弹框
    QCOMPARE(reader.currentRootTitle(), QStringLiteral("最近文件内容"));
    QVERIFY(reader.windowTitle().contains(QStringLiteral("最近验证")));
}

void TstUi::recentFilesMissingEntryRemoved() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("会被删除.lingya"));

    MainWindow window;
    QVERIFY(window.renameRootTo(QStringLiteral("占位")));
    QVERIFY(window.saveProjectTo(path));
    QMenu* recent = findRecentMenu(window);
    QVERIFY(recent != nullptr);
    QVERIFY(QFile::remove(path));   // 文件在磁盘上被移除

    QAction* entry = nullptr;
    for (QAction* action : recent->actions())
        if (action->text() == QFileInfo(path).absoluteFilePath()) entry = action;
    QVERIFY(entry != nullptr);

    autoCloseMessageBoxes();
    entry->trigger();   // 文件缺失：提示后移除记录，原工程保持
    QCOMPARE(window.currentRootTitle(), QStringLiteral("占位"));

    bool stillListed = false;
    for (QAction* action : recent->actions())
        stillListed = stillListed || action->text() == QFileInfo(path).absoluteFilePath();
    QVERIFY(!stillListed);   // P01：文件缺失可移除记录
}

void TstUi::recentFilesCancelKeepsUnsavedContentAndEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("取消打开验证.lingya"));

    MainWindow window;
    QVERIFY(window.saveProjectTo(path));
    QVERIFY(window.renameRootTo(QStringLiteral("必须保留的未保存内容")));
    QVERIFY(QFile::remove(path)); // 仅删除受控临时文件，模拟最近文件已失效

    QMenu *recent = findRecentMenu(window);
    QVERIFY(recent != nullptr);
    QAction *entry = nullptr;
    for (QAction *action : recent->actions())
    {
        if (action->text() == QFileInfo(path).absoluteFilePath())
            entry = action;
    }
    QVERIFY(entry != nullptr);
    const QString beforeTitle = window.windowTitle();
    const auto beforeRecents = SettingsService::recentFiles();

    bool cancelDialogSeen = false;
    QTimer::singleShot(0, &window, [&cancelDialogSeen] {
        for (QWidget *top : QApplication::topLevelWidgets())
        {
            if (auto *box = qobject_cast<QMessageBox *>(top))
            {
                if (QAbstractButton *cancel = box->button(QMessageBox::Cancel))
                {
                    cancelDialogSeen = true;
                    cancel->click();
                }
                else
                {
                    box->close(); // 错误路径也结束模态框，使测试失败而非挂起
                }
            }
        }
    });
    entry->trigger();

    QVERIFY(cancelDialogSeen);
    QCOMPARE(window.currentRootTitle(), QStringLiteral("必须保留的未保存内容"));
    QCOMPARE(window.windowTitle(), beforeTitle);
    QVERIFY(window.windowTitle().contains(QLatin1Char('*')));
    QCOMPARE(SettingsService::recentFiles(), beforeRecents);
}

void TstUi::ganttHostSwitchAndAttach()
{
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    auto* stack = qobject_cast<QStackedWidget*>(w.centralWidget());
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->currentIndex(), 0);   // 默认思维导图页

    // 视图菜单：切到甘特图占位页，再切回思维导图
    QMenu* viewMenu = nullptr;
    for (QMenu* menu : w.findChildren<QMenu*>())
        if (menu->title().contains(QStringLiteral("视图"))) viewMenu = menu;
    QVERIFY(viewMenu != nullptr);
    QAction* ganttAction = nullptr;
    QAction* mapAction = nullptr;
    for (QAction* action : viewMenu->actions()) {
        if (action->text().contains(QStringLiteral("甘特图"))) ganttAction = action;
        if (action->text().contains(QStringLiteral("思维导图"))) mapAction = action;
    }
    QVERIFY(ganttAction != nullptr);
    QVERIFY(mapAction != nullptr);
    ganttAction->trigger();
    QCOMPARE(stack->currentIndex(), 1);
    mapAction->trigger();
    QCOMPARE(stack->currentIndex(), 0);

    // 丙的接入契约：setGanttView装入后占位页被替换并切到甘特页
    auto* ganttView = new QGraphicsView();
    w.setGanttView(ganttView);
    QCOMPARE(stack->currentWidget(), qobject_cast<QWidget*>(ganttView));
    mapAction->trigger();
    QCOMPARE(stack->currentIndex(), 0);   // 思维导图页不受影响
    ganttAction->trigger();
    QCOMPARE(stack->currentWidget(), qobject_cast<QWidget*>(ganttView));
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));   // 甘特页切换不改工程

    // 同一指针重复调用：无操作且无悬空（评审发现的缺陷回归）
    w.setGanttView(ganttView);
    w.setGanttView(ganttView);
    QCOMPARE(stack->currentWidget(), qobject_cast<QWidget*>(ganttView));
    ganttAction->trigger();
    QCOMPARE(stack->currentWidget(), qobject_cast<QWidget*>(ganttView));

    // 误传画布视图与nullptr：无操作，画布仍是页0
    w.setGanttView(nullptr);
    auto* canvas = stack->widget(0);
    w.setGanttView(qobject_cast<QGraphicsView*>(canvas));
    QCOMPARE(stack->currentWidget(), qobject_cast<QWidget*>(ganttView));
    QVERIFY(stack->indexOf(canvas) == 0);   // 画布未被移出
}

void TstUi::recoverySnapshotWrittenOnlyForDirtyChangedContent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setRecoveryRootForTest(root.path());

    QTemporaryDir saveDir;   // 正式保存目标另放一处，不混入恢复根目录
    QVERIFY(saveDir.isValid());

    MainWindow w;
    QVERIFY(!w.currentProjectId().isEmpty());

    // 干净文档不写快照（快照保护的是未保存工作，§11.3）
    QVERIFY(QMetaObject::invokeMethod(&w, "writeRecoverySnapshot"));
    QVERIFY(RecoveryService::list(root.path(), w.currentProjectId()).isEmpty());

    // 编辑后30秒定时回调会写快照（测试直接调用同一私有槽验证行为）
    QVERIFY(w.renameRootTo(QStringLiteral("快照内容一")));
    QVERIFY(QMetaObject::invokeMethod(&w, "writeRecoverySnapshot"));
    QVector<RecoveryService::SnapshotInfo> snapshots =
        RecoveryService::list(root.path(), w.currentProjectId());
    QCOMPARE(snapshots.size(), 1);

    // 内容未变化不重复写一份（§11.3“仅当内容变化时写一份”）
    QVERIFY(QMetaObject::invokeMethod(&w, "writeRecoverySnapshot"));
    snapshots = RecoveryService::list(root.path(), w.currentProjectId());
    QCOMPARE(snapshots.size(), 1);

    // 快照可读回且内容一致
    const auto loaded =
        RecoveryService::load(root.path(), snapshots.first().projectId, snapshots.first().snapshotId);
    QVERIFY(std::holds_alternative<Project>(loaded));

    // 正式保存成功后清除该工程的自建快照（两种状态分离，§11.3）
    QVERIFY(w.saveProjectTo(saveDir.filePath(QStringLiteral("已保存.lingya"))));
    QCOMPARE(RecoveryService::list(root.path(), w.currentProjectId()).size(), 0);
}

void TstUi::recoveryAdoptionGoesThroughCommandTransaction()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setRecoveryRootForTest(root.path());

    const Project recovered =
        makeRecoveredProject(QStringLiteral("恢复工程"), QStringLiteral("恢复的根主题"));
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(
        RecoveryService::writeSnapshot(root.path(), recovered)));

    MainWindow w;
    QVector<RecoveryService::SnapshotInfo> snapshots =
        RecoveryService::list(root.path(), recovered.id);
    QCOMPARE(snapshots.size(), 1);

    // 采用：整工程经CommandService事务写入唯一文档，路径与外部戳清空
    QVERIFY(w.adoptRecoverySnapshot(snapshots.first()));
    QCOMPARE(w.currentRootTitle(), QStringLiteral("恢复的根主题"));
    QVERIFY(w.windowTitle().contains(QStringLiteral("恢复工程")));   // 无路径→标题栏显示工程标题
    QVERIFY(w.windowTitle().contains(QLatin1Char('*')));             // 保存前保持脏状态

    // 采用是可撤销历史：撤销回采用前的空白工程，重做再回恢复内容
    // （证明走了命令栈而非replaceProject，撤销与重做都作用在同一事务上）
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    for (QAction* action : w.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("撤销"))) undoAction = action;
        if (action->text().startsWith(QStringLiteral("重做"))) redoAction = action;
    }
    QVERIFY(undoAction != nullptr);
    QVERIFY(redoAction != nullptr);
    QVERIFY(undoAction->isEnabled());
    undoAction->trigger();
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));
    QVERIFY(redoAction->isEnabled());
    redoAction->trigger();
    QCOMPARE(w.currentRootTitle(), QStringLiteral("恢复的根主题"));

    // 采用后快照保留：采用的内容尚未落盘，此刻崩溃须可再次恢复（无保障间隙）
    snapshots = RecoveryService::list(root.path(), recovered.id);
    QCOMPARE(snapshots.size(), 1);
    // 采用不等于正式保存：撤销后原始磁盘副本仍保护恢复内容（30秒写盘对未变化内容去重）
    QVERIFY(QMetaObject::invokeMethod(&w, "writeRecoverySnapshot"));
    QCOMPARE(RecoveryService::list(root.path(), recovered.id).size(), 1);

    // 另存成功后快照才清理（两种状态分离，§11.3）
    QTemporaryDir saveDir;
    QVERIFY(saveDir.isValid());
    QVERIFY(w.saveProjectTo(saveDir.filePath(QStringLiteral("采用后另存.lingya"))));
    QVERIFY(RecoveryService::list(root.path(), recovered.id).isEmpty());
}

void TstUi::recoveryOfferDialogAdoptAndIgnoreAll()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setRecoveryRootForTest(root.path());

    MainWindow w;
    // 无快照：启动询问不出现任何界面
    w.offerRecoverySnapshots();
    QVERIFY(findRecoveryDialog() == nullptr);

    // 有快照：弹出预览对话框，未选中时采用按钮禁用
    const Project pending =
        makeRecoveredProject(QStringLiteral("待恢复工程"), QStringLiteral("待恢复的根主题"));
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(
        RecoveryService::writeSnapshot(root.path(), pending)));
    w.offerRecoverySnapshots();
    RecoveryDialog* dialog = findRecoveryDialog();
    QVERIFY(dialog != nullptr);   // open()模态但非阻塞，测试可继续驱动
    QAbstractButton* adoptButton = findButton(*dialog, QStringLiteral("采用"));
    QVERIFY(adoptButton != nullptr);
    QVERIFY(!adoptButton->isEnabled());

    // 选中第一条：预览加载通过校验，采用可用；点击采用→经事务写入当前文档
    auto* list = dialog->findChild<QListWidget*>();
    QVERIFY(list != nullptr);
    list->setCurrentRow(0);
    QVERIFY(adoptButton->isEnabled());
    adoptButton->click();
    QCOMPARE(w.currentRootTitle(), QStringLiteral("待恢复的根主题"));
    // 采用后快照保留：采用的内容尚未另存，崩溃时仍可再次恢复（另存/放弃时才清理）
    QCOMPARE(RecoveryService::list(root.path(), pending.id).size(), 1);   // 正式保存前保留

    // 另一份快照：选“全部不采用”→自建快照清除，当前文档不受影响
    const Project another =
        makeRecoveredProject(QStringLiteral("另一工程"), QStringLiteral("另一根主题"));
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(
        RecoveryService::writeSnapshot(root.path(), another)));
    w.offerRecoverySnapshots();
    dialog = findRecoveryDialog();
    QVERIFY(dialog != nullptr);
    QAbstractButton* ignoreButton = findButton(*dialog, QStringLiteral("全部不采用"));
    QVERIFY(ignoreButton != nullptr);
    ignoreButton->click();
    QVERIFY(RecoveryService::list(root.path(), another.id).isEmpty());
    QCOMPARE(w.currentRootTitle(), QStringLiteral("待恢复的根主题")); // 文档不被误改
}

void TstUi::recoveryAdoptionFromOpenedFileRefreshesTitle()
{
    QTemporaryDir root;
    QTemporaryDir files;
    QVERIFY(root.isValid());
    QVERIFY(files.isValid());
    MainWindow::setRecoveryRootForTest(root.path());
    const Project recovered =
        makeRecoveredProject(QStringLiteral("恢复后的工程"), QStringLiteral("恢复后的主题"));
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(
        RecoveryService::writeSnapshot(root.path(), recovered)));
    MainWindow window;
    QVERIFY(window.saveProjectTo(files.filePath(QStringLiteral("原先打开的文件.lingya"))));
    QVERIFY(window.windowTitle().contains(QStringLiteral("原先打开的文件")));
    QVERIFY(window.adoptRecoverySnapshot(RecoveryService::list(root.path(), recovered.id).first()));
    QCOMPARE(window.windowTitle(), QStringLiteral("恢复后的工程 * - 灵芽思维"));
    QCOMPARE(window.currentRootTitle(), QStringLiteral("恢复后的主题"));
}

void TstUi::recoveryAdoptionSurvivesRestartAndFailedSave()
{
    QTemporaryDir root;
    QTemporaryDir files;
    QVERIFY(root.isValid());
    QVERIFY(files.isValid());
    MainWindow::setRecoveryRootForTest(root.path());
    const Project recovered =
        makeRecoveredProject(QStringLiteral("再次启动恢复"), QStringLiteral("不能只保存在内存"));
    QVERIFY(std::holds_alternative<RecoveryService::SnapshotWrite>(
        RecoveryService::writeSnapshot(root.path(), recovered)));
    const auto snapshot = RecoveryService::list(root.path(), recovered.id).first();
    {
        MainWindow first;
        QVERIFY(first.adoptRecoverySnapshot(snapshot));
        // 不发送正常关闭事件、不触发下一次定时写盘，模拟内存实例直接消失后的磁盘状态。
        QCOMPARE(RecoveryService::list(root.path(), recovered.id).size(), 1);
    }
    MainWindow restarted;
    const auto pending = RecoveryService::list(root.path(), recovered.id);
    QCOMPARE(pending.size(), 1);
    QVERIFY(restarted.adoptRecoverySnapshot(pending.first()));
    QCOMPARE(restarted.currentRootTitle(), QStringLiteral("不能只保存在内存"));

    const QString blocked = files.filePath(QStringLiteral("同名目录.lingya"));
    QVERIFY(QDir().mkpath(blocked));
    autoCloseMessageBoxes();
    QVERIFY(!restarted.saveProjectTo(blocked));
    QCOMPARE(RecoveryService::list(root.path(), recovered.id).size(), 1);
    QVERIFY(restarted.windowTitle().contains(QLatin1Char('*')));

    const QString saved = files.filePath(QStringLiteral("正式保存.lingya"));
    QVERIFY(restarted.saveProjectTo(saved));
    QVERIFY(RecoveryService::list(root.path(), recovered.id).isEmpty());
    const auto loaded = FileService::load(saved);
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(std::get<Project>(loaded).id, recovered.id);
}

void TstUi::recoveryOpeningAnotherProjectClearsSnapshotStatus()
{
    QTemporaryDir root;
    QTemporaryDir files;
    QVERIFY(root.isValid());
    QVERIFY(files.isValid());
    MainWindow::setRecoveryRootForTest(root.path());
    MainWindow window;
    QVERIFY(window.renameRootTo(QStringLiteral("上一工程的未保存主题")));
    QVERIFY(QMetaObject::invokeMethod(&window, "writeRecoverySnapshot"));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("恢复快照")));
    const QString another = files.filePath(QStringLiteral("另一工程.lingya"));
    QVERIFY(std::holds_alternative<qint64>(
        FileService::save(another, ProjectFactory::createMindmap(QStringLiteral("另一工程")))));
    QVERIFY(window.openFile(another));
    QVERIFY(!window.statusBar()->currentMessage().contains(QStringLiteral("恢复快照")));
}

void TstUi::recoveryProcessCrashEndToEnd()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setRecoveryRootForTest(root.path());

    // 子进程阶段：真实进程编辑→写快照→硬退出（模拟崩溃，无析构无保存）
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LINGYA_E2E_PHASE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("LINGYA_E2E_ROOT"), root.path());
    child.setProcessEnvironment(environment);
    child.start();
    QVERIFY(child.waitForStarted(10000));
    QVERIFY(child.waitForFinished(60000));
    QCOMPARE(child.exitCode(), 1);   // 约定的“崩溃”退出码；2=子阶段自检失败
    const QString output = QString::fromUtf8(child.readAllStandardOutput());
    const QString marker = QStringLiteral("LINGYA_E2E_PROJECT=");
    const int at = output.indexOf(marker);
    QVERIFY(at >= 0);
    const QString projectId =
        output.mid(at + marker.size()).section(QLatin1Char('\n'), 0, 0).trimmed();
    QVERIFY(!projectId.isEmpty());

    // 父阶段：重启后的新实例发现快照，预览并采用
    MainWindow window;
    QCOMPARE(RecoveryService::list(root.path(), projectId).size(), 1);
    window.offerRecoverySnapshots();
    RecoveryDialog* dialog = findRecoveryDialog();
    QVERIFY(dialog != nullptr);
    auto* list = dialog->findChild<QListWidget*>();
    QVERIFY(list != nullptr);
    list->setCurrentRow(0);
    QAbstractButton* adoptButton = findButton(*dialog, QStringLiteral("采用"));
    QVERIFY(adoptButton != nullptr);
    adoptButton->click();
    QCOMPARE(window.currentRootTitle(), QStringLiteral("崩溃链路内容"));
    QVERIFY(window.windowTitle().contains(QLatin1Char('*')));   // 保存前保持脏状态

    // 采用是可撤销事务：撤销回空白，重做回恢复内容（§4.4-C3补重做）
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("撤销"))) undoAction = action;
        if (action->text().startsWith(QStringLiteral("重做"))) redoAction = action;
    }
    QVERIFY(undoAction != nullptr && redoAction != nullptr);
    undoAction->trigger();
    QCOMPARE(window.currentRootTitle(), QStringLiteral("中心主题"));
    redoAction->trigger();
    QCOMPARE(window.currentRootTitle(), QStringLiteral("崩溃链路内容"));

    // C1：采用后未另存时快照保留，此刻崩溃仍可再次恢复
    QCOMPARE(RecoveryService::list(root.path(), projectId).size(), 1);

    // 另存为落盘后快照清除；关闭重开核对最终内容一致
    QTemporaryDir saveDir;
    QVERIFY(saveDir.isValid());
    QVERIFY(window.saveProjectTo(saveDir.filePath(QStringLiteral("崩溃恢复落盘.lingya"))));
    QVERIFY(RecoveryService::list(root.path(), projectId).isEmpty());
    MainWindow reopened;
    QVERIFY(reopened.openFile(saveDir.filePath(QStringLiteral("崩溃恢复落盘.lingya"))));
    QCOMPARE(reopened.currentRootTitle(), QStringLiteral("崩溃链路内容"));
}

void TstUi::outlineEditingRoundTrip()
{
    MainWindow window;
    if (!qEnvironmentVariableIsEmpty("LINGYA_UI_CAPTURE"))
    {
        QFile style(QFINDTESTDATA("../resources/styles/light.qss"));
        QVERIFY(style.open(QIODevice::ReadOnly));
        window.setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    QVERIFY(outline != nullptr);
    auto *add = window.findChild<QAction *>(QStringLiteral("addChildTopic"));
    auto *rename = window.findChild<QAction *>(QStringLiteral("renameTopic"));
    QVERIFY(add != nullptr && rename != nullptr);
    const ID rootId = outline->currentItem()->data(0, Qt::UserRole).toString();
    answerTitleDialog(QStringLiteral("第一层中文"));
    add->trigger();
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("第一层中文"));
    QCOMPARE(outline->currentItem()->parent()->data(0, Qt::UserRole).toString(), rootId);
    const ID childId = outline->currentItem()->data(0, Qt::UserRole).toString();
    answerTitleDialog(QStringLiteral("第二层中文"));
    QTest::keyClick(outline, Qt::Key_Tab);
    QCOMPARE(outline->currentItem()->parent()->data(0, Qt::UserRole).toString(), childId);
    answerTitleDialog(QStringLiteral("改名后的第二层"));
    rename->trigger();
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("改名后的第二层"));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("第二层中文"));
    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("改名后的第二层"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("多级编辑.lingya"));
    QVERIFY(window.saveProjectTo(path));
    const QString capture = qEnvironmentVariable("LINGYA_UI_CAPTURE");
    if (!capture.isEmpty())
        QVERIFY(window.grab().save(capture));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    auto *tree = reopened.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("第一层中文"));
    QCOMPARE(tree->topLevelItem(0)->child(0)->child(0)->text(0), QStringLiteral("改名后的第二层"));
}

void TstUi::outlineCancelAndUndoSelection()
{
    MainWindow window;
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *add = window.findChild<QAction *>(QStringLiteral("addChildTopic"));
    QVERIFY(outline && document && commands && add);
    const qint64 revision = document->sessionRevision();
    answerTitleDialog(QStringLiteral("取消的内容"), false);
    add->trigger();
    QCOMPARE(document->sessionRevision(), revision);
    QVERIFY(!document->isModified());
    QVERIFY(!commands->canUndo());
    answerTitleDialog(QStringLiteral("新增后撤销"));
    add->trigger();
    QVERIFY(outline->currentItem()->parent() != nullptr);
    commands->undo();
    QCOMPARE(outline->currentItem(), outline->topLevelItem(0));
    QCOMPARE(outline->topLevelItem(0)->childCount(), 0);
    commands->redo();
    QCOMPARE(outline->topLevelItem(0)->childCount(), 1);
    // 选择和展开不修改文档；预览必须对应新选中的节点。
    const qint64 beforeSelection = document->sessionRevision();
    outline->setCurrentItem(outline->topLevelItem(0)->child(0));
    outline->collapseAll();
    QCOMPARE(document->sessionRevision(), beforeSelection);
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QCOMPARE(view->scene()->selectedItems().size(), 1);
    QCOMPARE(view->scene()->selectedItems().first()->toolTip(), QStringLiteral("新增后撤销"));
}

void TstUi::rootRenameUsesSharedRulesAndTaskLinkage()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    QVERIFY(document && commands);
    const auto &sheet = std::get<TreeSheet>(document->project().sheets.first());
    TaskCommands tasks(commands, document);
    const auto linked = tasks.linkTask(sheet.id, sheet.rootId);
    QVERIFY(std::holds_alternative<ID>(linked));
    QVERIFY(window.renameRootTo(QStringLiteral("  同步中文标题  ")));
    QCOMPARE(window.currentRootTitle(), QStringLiteral("同步中文标题"));
    QCOMPARE(document->project().tasks.first().title, window.currentRootTitle());
    const qint64 revision = document->sessionRevision();
    QVERIFY(window.renameRootTo(QStringLiteral("同步中文标题")));
    QCOMPARE(document->sessionRevision(), revision);
    QVERIFY(window.renameRootTo(QStringLiteral("   ")));
    QCOMPARE(window.currentRootTitle(), QStringLiteral("未命名主题"));
    commands->undo();
    QCOMPARE(window.currentRootTitle(), QStringLiteral("同步中文标题"));
    QCOMPARE(document->project().tasks.first().title, window.currentRootTitle());
    autoCloseMessageBoxes();
    QVERIFY(!window.renameRootTo(QString(2001, QChar(0x4e2d))));
    QCOMPARE(document->project().tasks.first().title, QStringLiteral("同步中文标题"));
}

void TstUi::ganttLazyLoadRefreshAndUndo()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *action = window.findChild<QAction *>(QStringLiteral("showGantt"));
    QVERIFY(document && commands && action);
    action->trigger();
    auto *view = window.findChild<GanttView *>();
    QVERIFY(view != nullptr);
    action->trigger();
    QCOMPARE(window.findChildren<GanttView *>().size(), 1);
    TaskCommands tasks(commands, document);
    QVERIFY(std::holds_alternative<ID>(tasks.addTask(QStringLiteral("排期任务"))));
    Task scheduled = document->project().tasks.first();
    scheduled.startDate = QDate(2026, 10, 25);
    scheduled.endDate = QDate(2026, 10, 28);
    QVERIFY(std::holds_alternative<bool>(tasks.updateTask(scheduled)));
    QCOMPARE(view->tasks().size(), 1);
    QCOMPARE(view->tasks().first().endDate, scheduled.endDate);
    QVERIFY(!view->scene()->items().isEmpty());
    commands->undo();
    QVERIFY(!view->tasks().first().startDate.has_value());
    commands->undo();
    QVERIFY(view->tasks().isEmpty());
    commands->redo();
    QCOMPARE(view->tasks().size(), 1);
}

void TstUi::previewZoomAndLongTitle()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    auto *zoomIn = window.findChild<QAction *>(QStringLiteral("zoomIn"));
    auto *zoomOut = window.findChild<QAction *>(QStringLiteral("zoomOut"));
    QVERIFY(document && view && zoomIn && zoomOut);
    const qint64 revision = document->sessionRevision();
    for (int i = 0; i < 30; ++i)
        zoomIn->trigger();
    QCOMPARE(view->transform().m11(), 2.0);
    for (int i = 0; i < 40; ++i)
        zoomOut->trigger();
    QCOMPARE(view->transform().m11(), 0.25);
    QCOMPARE(document->sessionRevision(), revision);
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(16);
    NodeItem shortNode(QStringLiteral("短标题"), font);
    const QString title = QString(200, QChar(0x4e2d)) + QStringLiteral("\n末行");
    NodeItem longNode(title, font);
    QCOMPARE(longNode.toolTip(), title);
    QVERIFY(longNode.boundingRect().height() > shortNode.boundingRect().height());
    QVERIFY(longNode.boundingRect().width() <= 284);
}

void TstUi::treeCanvasNodesEdgesSelectionAndRename()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = canvasProject();
    const auto &sheet = std::get<TreeSheet>(project.sheets.first());
    const ID rootId = sheet.rootId;
    const QString path = dir.filePath(QStringLiteral("双侧整树.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, project)));
    QVERIFY(std::holds_alternative<Project>(FileService::load(path)));
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(window.openFile(path));
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *document = window.findChild<ProjectDocument *>();
    auto nodes = canvasNodes(view);
    QCOMPARE(nodes.size(), 7);
    QCOMPARE(nodes.value(rootId)->pos(), QPointF(0, 0));
    QVERIFY(nodes.value(canvasId(QStringLiteral("branch-0")))->pos().x() > 0);
    QVERIFY(nodes.value(canvasId(QStringLiteral("branch-1")))->pos().x() < 0);
    int edgeCount = 0;
    for (QGraphicsItem *item : view->scene()->items())
    {
        if (auto *edge = dynamic_cast<QGraphicsPathItem *>(item))
        {
            ++edgeCount;
            QVERIFY(edge->zValue() < 0);
            QVERIFY(edge->acceptedMouseButtons() == Qt::NoButton);
            const QPainterPath line = edge->path();
            QVERIFY(!line.isEmpty());
            int endpoints = 0;
            for (NodeItem *node : nodes)
            {
                const auto first = line.elementAt(0);
                const auto last = line.elementAt(line.elementCount() - 1);
                const QRectF rect = node->sceneBoundingRect();
                if (rect.contains(QPointF(first.x, first.y)))
                    ++endpoints;
                if (rect.contains(QPointF(last.x, last.y)))
                    ++endpoints;
            }
            QCOMPARE(endpoints, 2);
        }
    }
    QCOMPARE(edgeCount, 6);
    const qint64 revision = document->sessionRevision();
    auto *leaf = outline->topLevelItem(0)->child(0)->child(1);
    outline->setCurrentItem(leaf);
    const ID leafId = leaf->data(0, Qt::UserRole).toString();
    QCOMPARE(view->scene()->selectedItems().size(), 1);
    QCOMPARE(dynamic_cast<NodeItem *>(view->scene()->selectedItems().first())->nodeId(), leafId);
    window.findChild<QAction *>(QStringLiteral("fitTopic"))->trigger();
    QCoreApplication::processEvents();
    const QPoint click =
        view->mapFromScene(nodes.value(canvasId(QStringLiteral("branch-1")))->pos());
    QVERIFY(view->viewport()->rect().contains(click));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, click);
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(),
             canvasId(QStringLiteral("branch-1")));
    QCOMPARE(document->sessionRevision(), revision);
    answerTitleDialog(QStringLiteral("画布双击改名"));
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, click);
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("画布双击改名"));
    auto *commands = window.findChild<CommandService *>();
    commands->undo();
    QCOMPARE(canvasNodes(view).value(canvasId(QStringLiteral("branch-1")))->toolTip(),
             QStringLiteral("阶段2"));
    commands->redo();
    QCOMPARE(canvasNodes(view).value(canvasId(QStringLiteral("branch-1")))->toolTip(),
             QStringLiteral("画布双击改名"));
    // 视图切换及窗口缩放后，定位仍指向所选节点。
    view->centerOn(10000, 10000);
    window.findChild<QAction *>(QStringLiteral("locateTopic"))->trigger();
    QVERIFY(view->viewport()->rect().contains(
        view->mapFromScene(canvasNodes(view).value(canvasId(QStringLiteral("branch-1")))->pos())));
    const QString capture = qEnvironmentVariable("LINGYA_TREE_CAPTURE");
    if (!capture.isEmpty())
    {
        QFile style(QFINDTESTDATA("../resources/styles/light.qss"));
        QVERIFY(style.open(QIODevice::ReadOnly));
        window.setStyleSheet(QString::fromUtf8(style.readAll()));
        window.findChild<QAction *>(QStringLiteral("fitTopic"))->trigger();
        QVERIFY(window.grab().save(capture));
    }
}

void TstUi::treeCanvasHundredNodesRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = canvasProject(9, 10);
    auto &sheet = std::get<TreeSheet>(project.sheets.first());
    sheet.nodes[canvasId(QStringLiteral("leaf-0-0"))].title =
        QString(120, QChar(0x4e2d)) + QStringLiteral("\n末行");
    const QString path = dir.filePath(QStringLiteral("一百节点.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, project)));
    QVERIFY(std::holds_alternative<Project>(FileService::load(path)));
    MainWindow window;
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(window.openFile(path));
    qInfo() << "一百节点加载及布局毫秒数：" << elapsed.elapsed();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    const auto before = canvasNodes(view);
    QCOMPARE(before.size(), 100);
    QCOMPARE(view->scene()->items().size(), 199);
    const auto values = before.values();
    QMap<ID, QPointF> positions;
    QMap<ID, QRectF> bounds;
    QMap<ID, QString> titles;
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
    {
        positions.insert(it.key(), it.value()->pos());
        bounds.insert(it.key(), it.value()->boundingRect());
        titles.insert(it.key(), it.value()->toolTip());
    }
    for (int i = 0; i < values.size(); ++i)
    {
        QVERIFY(qIsFinite(values.at(i)->pos().x()));
        QVERIFY(qIsFinite(values.at(i)->pos().y()));
        for (int j = i + 1; j < values.size(); ++j)
            QVERIFY2(
                !values.at(i)->sceneBoundingRect().intersects(values.at(j)->sceneBoundingRect()),
                qPrintable(values.at(i)->nodeId() + QStringLiteral(" 与 ") +
                           values.at(j)->nodeId()));
    }
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *document = window.findChild<ProjectDocument *>();
    const qint64 revision = document->sessionRevision();
    outline->setCurrentItem(outline->topLevelItem(0)->child(8)->child(9));
    QCOMPARE(document->sessionRevision(), revision);
    QVERIFY(window.saveProjectTo(path));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    const auto after =
        canvasNodes(reopened.findChild<QGraphicsView *>(QStringLiteral("topicPreview")));
    QCOMPARE(after.size(), 100);
    for (auto it = positions.constBegin(); it != positions.constEnd(); ++it)
    {
        QCOMPARE(after.value(it.key())->pos(), it.value());
        QCOMPARE(after.value(it.key())->boundingRect(), bounds.value(it.key()));
        QCOMPARE(after.value(it.key())->toolTip(), titles.value(it.key()));
    }
}

void TstUi::treeCanvasCollapsedSelectionIsViewOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project project = canvasProject();
    auto &sheet = std::get<TreeSheet>(project.sheets.first());
    sheet.nodes[canvasId(QStringLiteral("branch-0"))].collapsed = true;
    const QString path = dir.filePath(QStringLiteral("折叠定位.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, project)));
    QVERIFY(std::holds_alternative<Project>(FileService::load(path)));
    MainWindow window;
    QVERIFY(window.openFile(path));
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *document = window.findChild<ProjectDocument *>();
    QCOMPARE(canvasNodes(view).size(), 5);
    const qint64 revision = document->sessionRevision();
    outline->setCurrentItem(outline->topLevelItem(0)->child(0)->child(0));
    QCOMPARE(canvasNodes(view).size(), 7);
    QVERIFY(std::get<TreeSheet>(document->project().sheets.first())
                .nodes.value(canvasId(QStringLiteral("branch-0")))
                .collapsed);
    QCOMPARE(document->sessionRevision(), revision);
    QVERIFY(!document->isModified());
    QVERIFY(window.saveProjectTo(path));
    QVERIFY(window.openFile(path));
    QCOMPARE(canvasNodes(view).size(), 5);
}

void TstUi::multilineTitleInputMethodAndCancel()
{
    MainWindow window;
    bool editorFound = false;
    bool preeditKeptDialog = false;
    QTimer::singleShot(0,
                       [&]
                       {
                           for (QWidget *top : QApplication::topLevelWidgets())
                           {
                               if (auto *dialog = qobject_cast<QInputDialog *>(top))
                               {
                                   auto *editor = dialog->findChild<QPlainTextEdit *>();
                                   if (editor == nullptr)
                                   {
                                       dialog->reject();
                                       return;
                                   }
                                   editorFound = true;
                                   editor->setPlainText(QStringLiteral("首行"));
                                   editor->moveCursor(QTextCursor::End);
                                   QTest::keyClick(editor, Qt::Key_Return, Qt::ShiftModifier);
                                   editor->insertPlainText(QStringLiteral("次行"));
                                   QInputMethodEvent preedit(QStringLiteral("zhong"), {});
                                   QCoreApplication::sendEvent(editor, &preedit);
                                   QTest::keyClick(editor, Qt::Key_Return);
                                   preeditKeptDialog = dialog->isVisible();
                                   QInputMethodEvent commit;
                                   commit.setCommitString(QStringLiteral("中"));
                                   QCoreApplication::sendEvent(editor, &commit);
                                   QTest::keyClick(editor, Qt::Key_Return);
                               }
                           }
                       });
    window.findChild<QAction *>(QStringLiteral("renameTopic"))->trigger();
    QVERIFY(editorFound);
    QVERIFY(preeditKeptDialog);
    QCOMPARE(window.currentRootTitle(), QStringLiteral("首行\n次行中"));
    auto *document = window.findChild<ProjectDocument *>();
    const qint64 revision = document->sessionRevision();
    answerTitleDialog(QStringLiteral("取消多行\n不应写入"), false);
    window.findChild<QAction *>(QStringLiteral("renameTopic"))->trigger();
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(window.currentRootTitle(), QStringLiteral("首行\n次行中"));
}

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
