#include <QtTest>

#include <QAction>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include "app/MainWindow.h"
#include "services/FileService.h"

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

private:
    QTemporaryDir m_settingsDir;   // QSettings重定向目录（initTestCase设置，成员保活）

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
};

void TstUi::initTestCase() {
    // 测试隔离：QSettings重定向到临时目录（IniFormat），不读写真实用户设置
    QVERIFY(m_settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    qApp->setOrganizationName(QStringLiteral("lingya-selftest"));
    qApp->setApplicationName(QStringLiteral("lingya-tst-ui"));
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

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
