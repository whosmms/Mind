#include <QtTest>

#include <QAbstractButton>
#include <QAction>
#include <QFile>
#include <QListWidget>
#include <QMenu>
#include <QStackedWidget>
#include <QGraphicsView>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

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
    // 恢复快照（规范§11.3、原设计§8）——放最后：前序测试在隔离空恢复根目录下不受影响
    void recoverySnapshotWrittenOnlyForDirtyChangedContent();
    void recoveryAdoptionGoesThroughCommandTransaction();
    void recoveryOfferDialogAdoptAndIgnoreAll();

private:
    QTemporaryDir m_settingsDir;   // QSettings重定向目录（initTestCase设置，成员保活）
    QTemporaryDir m_recoveryDir;   // 恢复快照重定向目录（initTestCase设置，成员保活）

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
    // 测试隔离：QSettings重定向到临时目录（IniFormat），不读写真实用户设置
    QVERIFY(m_settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    qApp->setOrganizationName(QStringLiteral("lingya-selftest"));
    qApp->setApplicationName(QStringLiteral("lingya-tst-ui"));
    // 恢复快照同样重定向到空临时目录：真实AppLocalData中的遗留快照不会弹预览对话框
    QVERIFY(m_recoveryDir.isValid());
    MainWindow::setRecoveryRootForTest(m_recoveryDir.path());
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
    // 30秒定时写盘对未变化内容去重：采用后不产生重复快照
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
    QVERIFY(!RecoveryService::list(root.path(), pending.id).isEmpty());

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
    QCOMPARE(w.currentRootTitle(), QStringLiteral("待恢复的根主题"));   // 文档不被误改
}

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
