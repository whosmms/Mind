#include <QtTest>

#include "charts/GanttView.h"
#include "commands/CommandService.h"
#include "commands/NodeCommands.h"
#include "commands/TaskCommands.h"
#include "document/ProjectDocument.h"
#include "features/ReviewService.h"
#include "graphics/NodeItem.h"
#include "services/ProjectJson.h"
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QUrl>

#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include "app/MainWindow.h"
#include "app/RecoveryDialog.h"
#include "app/UserFilesDialog.h"
#include "app/LoginDialog.h"
#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "services/FileService.h"
#include "services/RecoveryService.h"
#include "services/SessionService.h"
#include "services/SettingsService.h"
#include "services/UserFileService.h"

using namespace lingya;

// 测试拦截系统URL派发，不启动浏览器或发出网络请求。
class LinkReceiver : public QObject
{
    Q_OBJECT
public:
    QUrl last;
    int count = 0;
public slots:
    void open(const QUrl &url) { last = url; ++count; }
};

// UI层测试（规范§3 tst_ui规划、§19.1）：进程内验证快捷键绑定与文件打开行为。
// QTest::keyClick走Qt事件系统（ShortcutMap），不经Windows输入队列，
// 因此不受真实环境IME与前台焦点干扰，可直接判定快捷键绑定正确性。
class TstUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void undoRedoViaShortcuts();
    void redoWithEditorFocus_data();
    void redoWithEditorFocus();
    void saveAsDialogRoundTripAndCancel();
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
    void nodeMetadataSurvivesEditorAndFileRoundTrip();
    void nodeMetadataNoOpKeepsWindowClean();
    void nodePropertiesEditClearUndoAndRoundTrip();
    void nodePropertiesCancelInvalidAndUnknownIcon();
    void nodeIconResourcesAndMeasurement();
    void nodeStyleShapes_data();
    void nodeStyleShapes();
    void nodeStyleEditUndoAndRoundTrip();
    void nodeStyleCancelInvalidResetAndNoOp();
    void nodeStyleMixedHundredNodes();
    void ganttLazyLoadRefreshAndUndo();
    void previewZoomAndLongTitle();
    void treeCanvasNodesEdgesSelectionAndRename();
    void treeCanvasHundredNodesRoundTrip();
    void treeCanvasCollapsedSelectionIsViewOnly();
    void multilineTitleInputMethodAndCancel();
    void siblingKeyboardInsertionRoundTrip();
    void deletionCancelAndRootProtection();
    void deletionPoliciesAndUndo_data();
    void deletionPoliciesAndUndo();
    void exportManualFixtures();
    void unsavedPromptButtonsAreChineseAndCancelKeepsContent();
    // 恢复快照（规范§11.3、原设计§8）——放最后：前序测试在隔离空恢复根目录下不受影响
    void recoverySnapshotWrittenOnlyForDirtyChangedContent();
    void recoveryAdoptionGoesThroughCommandTransaction();
    void recoveryOfferDialogAdoptAndIgnoreAll();
    void recoveryAdoptionFromOpenedFileRefreshesTitle();
    void recoveryAdoptionSurvivesRestartAndFailedSave();
    void recoveryOpeningAnotherProjectClearsSnapshotStatus();
    void recoveryProcessCrashEndToEnd();
    // 登录会话（D-M2-02）——放最后：前序测试在未登录现状下不受影响
    void loggedInSaveNamesIntoOwnedFolderOnly();
    void loggedInOpenRejectsOthersButAllowsPublicAndOwn();
    void loginSessionRejectsWrongPassword();
    void loggedOutOpensAnyFileUnchanged();
    void loggedInOpensFileLooseInUsersRoot();
    void ownedOpenCancelKeepsUnsavedProject();
    void ownedSaveProtectsExternalChanges_data();
    void ownedSaveProtectsExternalChanges();
    void ownedListUsesStableFileIdentity_data();
    void ownedListUsesStableFileIdentity();
    void loginDialogValidationRegistrationAndCancel();
    void ownedListPreservesRepeatedExtension();
    void ownedSaveRejectsEmptyNameForDotFile();

private:
  static void answerNodeStyle(MainWindow &window, std::function<void(QDialog *)> answer)
  {
      QTimer::singleShot(0, &window,
                         [&window, answer]
                         {
                             auto *dialog =
                                 window.findChild<QDialog *>(QStringLiteral("nodeStyleDialog"));
                             if (dialog == nullptr || !dialog->isVisible())
                             {
                                 QTest::qFail("未显示节点样式对话框", __FILE__, __LINE__);
                                 return;
                             }
                             answer(dialog);
                             if (dialog->isVisible())
                                 dialog->reject();
                         });
  }

    static void answerNodeProperties(MainWindow &window, std::function<void(QDialog *)> answer)
    {
        QTimer::singleShot(0, &window, [&window, answer]
        {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("nodePropertiesDialog"));
            if (dialog == nullptr || !dialog->isVisible())
            {
                QTest::qFail("未显示主题属性对话框", __FILE__, __LINE__);
                return;
            }
            answer(dialog);
            // 测试断言失败或预检拒绝时仍关闭模态窗口，避免测试挂起。
            if (dialog->isVisible())
                dialog->reject();
        });
    }

    QTemporaryDir m_settingsDir;   // QSettings重定向目录（initTestCase设置，成员保活）
    QTemporaryDir m_recoveryDir;   // 恢复快照重定向目录（initTestCase设置，成员保活）
    QTemporaryDir m_sessionDir;    // 会话根重定向目录（D-M2-02登录功能隔离）

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

    static Project deletionProject()
    {
        Project project = canvasProject(3, 1);
        auto &sheet = std::get<TreeSheet>(project.sheets.first());
        const ID branch = canvasId(QStringLiteral("branch-1"));
        const ID leaf = canvasId(QStringLiteral("leaf-1-0"));
        Task linked;
        linked.id = canvasId(QStringLiteral("linked-task"));
        linked.title = sheet.nodes.value(branch).title;
        linked.source = TaskSource{sheet.id, branch};
        linked.startDate = QDate(2026, 10, 25);
        linked.endDate = QDate(2026, 10, 28);
        project.tasks.append(linked);
        Task dependent;
        dependent.id = canvasId(QStringLiteral("dependent-task"));
        dependent.title = QStringLiteral("后续独立任务");
        dependent.predecessorIds.append(linked.id);
        project.tasks.append(dependent);
        Relation relation;
        relation.id = canvasId(QStringLiteral("relation"));
        relation.sourceId = leaf;
        relation.targetId = canvasId(QStringLiteral("branch-2"));
        relation.style = sheet.defaultEdgeStyle;
        sheet.relations.append(relation);
        GroupAnnotation annotation;
        annotation.id = canvasId(QStringLiteral("annotation"));
        annotation.parentId = sheet.rootId;
        annotation.kind = QStringLiteral("boundary");
        annotation.memberIds = {branch, canvasId(QStringLiteral("branch-2"))};
        annotation.text = QStringLiteral("删除影响验证");
        annotation.color = QStringLiteral("#247A62");
        sheet.annotations.append(annotation);
        Comment comment;
        comment.id = canvasId(QStringLiteral("comment"));
        comment.nodeId = leaf;
        comment.author = QStringLiteral("验收样例");
        comment.body = QStringLiteral("删除时一起清理此批注");
        comment.createdAt = comment.updatedAt = QDateTime::currentDateTimeUtc();
        sheet.comments.append(comment);
        const auto card = ReviewService::createCard(project, sheet.id, branch);
        if (card.has_value())
            project.cards.append(*card);
        PresentationRoute route;
        route.id = canvasId(QStringLiteral("route"));
        route.sheetId = sheet.id;
        route.title = QStringLiteral("保留的讲解路线");
        route.steps.append(
            {canvasId(QStringLiteral("step")), leaf, QStringLiteral("来源可能被删除")});
        project.routes.append(route);
        TextDraft draft;
        draft.id = canvasId(QStringLiteral("draft"));
        draft.sourceSheetId = sheet.id;
        draft.title = QStringLiteral("保留的文稿");
        draft.sourceRevision = project.revision;
        draft.createdAt = QDateTime::currentDateTimeUtc();
        draft.sections.append(
            {QStringLiteral("内容"), QStringLiteral("保持原文"), {leaf}, QStringLiteral("source")});
        project.drafts.append(draft);
        return project;
    }

    static void answerDeletion(const QString &button, QString *message = nullptr)
    {
        QTimer::singleShot(0,
                           [button, message]
                           {
                               for (QWidget *top : QApplication::topLevelWidgets())
                               {
                                   if (auto *box = qobject_cast<QMessageBox *>(top))
                                   {
                                       if (message != nullptr)
                                           *message = box->text();
                                       const QString capture =
                                           qEnvironmentVariable("LINGYA_DELETE_CAPTURE");
                                       if (!capture.isEmpty())
                                           box->grab().save(capture);
                                       if (auto *choice = findButton(*box, button))
                                           choice->click();
                                       else
                                           box->reject();
                                   }
                               }
                           });
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
    QVERIFY(m_sessionDir.isValid());
    MainWindow::setSessionRootForTest(m_sessionDir.path());
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

void TstUi::redoWithEditorFocus_data()
{
    QTest::addColumn<QString>("focusName");
    QTest::addColumn<bool>("shiftZ");
    QTest::newRow("大纲-CtrlY") << QStringLiteral("topicOutline") << false;
    QTest::newRow("大纲-CtrlShiftZ") << QStringLiteral("topicOutline") << true;
    QTest::newRow("画布-CtrlY") << QStringLiteral("topicPreview") << false;
    QTest::newRow("画布-CtrlShiftZ") << QStringLiteral("topicPreview") << true;
}

void TstUi::redoWithEditorFocus()
{
    QFETCH(QString, focusName);
    QFETCH(bool, shiftZ);
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *focus = window.findChild<QWidget *>(focusName);
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QVERIFY(focus && document && commands && outline && view);
    const ID rootId = outline->currentItem()->data(0, Qt::UserRole).toString();
    const QString before = window.currentRootTitle();
    QVERIFY(window.renameRootTo(QStringLiteral("焦点内快捷键改名")));
    focus->setFocus();
    QTRY_VERIFY(focus->hasFocus());
    QSignalSpy changed(document, &ProjectDocument::changed);
    QTest::keyClick(window.windowHandle(), Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(window.currentRootTitle(), before);
    QVERIFY(commands->canRedo());
    QTRY_VERIFY(focus->hasFocus());
    QTest::keyClick(window.windowHandle(), shiftZ ? Qt::Key_Z : Qt::Key_Y,
                    shiftZ ? Qt::ControlModifier | Qt::ShiftModifier : Qt::ControlModifier);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(window.currentRootTitle(), QStringLiteral("焦点内快捷键改名"));
    QVERIFY(!commands->canRedo());
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), rootId);
    QCOMPARE(outline->currentItem()->text(0), window.currentRootTitle());
    QVERIFY(canvasNodes(view).contains(rootId));
    QCOMPARE(canvasNodes(view).value(rootId)->toolTip(), window.currentRootTitle());
}

void TstUi::saveAsDialogRoundTripAndCancel()
{
    // 自动化仅驱动Qt文件对话框，不据此声称覆盖Windows原生对话框/真实键盘。
    const bool previousNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restoreNative = qScopeGuard(
        [previousNative]
        { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previousNative); });
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("首次另存.lingya"));
    const QString secondPath = dir.filePath(QStringLiteral("改名后另存.lingya"));
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *document = window.findChild<ProjectDocument *>();
    QVERIFY(document);
    QAction *saveAs = nullptr;
    for (QAction *action : window.findChildren<QAction *>())
        if (action->text().startsWith(QStringLiteral("另存为")))
            saveAs = action;
    QVERIFY(saveAs);
    QVERIFY(window.renameRootTo(QStringLiteral("首次保存内容")));
    int answered = 0;
    bool accepted = false;
    QString destination = firstPath;
    bool cancel = false;
    QTimer responder;
    responder.setInterval(1);
    connect(&responder, &QTimer::timeout, &window,
            [&]
            {
                auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (dialog == nullptr || !dialog->isVisible())
                    return;
                responder.stop();
                ++answered;
                if (cancel)
                    dialog->reject();
                else
                {
                    dialog->selectFile(destination);
                    accepted = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                    if (dialog->isVisible())
                        dialog->reject();
                }
            });
    responder.start();
    QTest::keyClick(window.windowHandle(), Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(answered, 1);
    QVERIFY(accepted);
    QVERIFY(!document->isModified());
    const auto first = FileService::load(firstPath);
    QVERIFY(std::holds_alternative<Project>(first));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(first)),
             ProjectJson::toJson(document->project()));

    QVERIFY(window.renameRootTo(QStringLiteral("取消另存后保留的修改")));
    const auto beforeCancel = ProjectJson::toJson(document->project());
    const qint64 revision = document->sessionRevision();
    const QString title = window.windowTitle();
    cancel = true;
    responder.start();
    saveAs->trigger();
    QCOMPARE(answered, 2);
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(ProjectJson::toJson(document->project()), beforeCancel);
    QCOMPARE(window.windowTitle(), title);
    QVERIFY(document->isModified());

    cancel = false;
    destination = secondPath;
    responder.start();
    saveAs->trigger();
    QCOMPARE(answered, 3);
    QVERIFY(!document->isModified());
    QCOMPARE(window.windowTitle(), QStringLiteral("改名后另存 - 灵芽思维"));
    QVERIFY(window.renameRootTo(QStringLiteral("继续保存只更新新文件")));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::keyClick(window.windowHandle(), Qt::Key_S, Qt::ControlModifier);
    QCOMPARE(answered, 3);
    QVERIFY(!document->isModified());
    const auto firstAfter = FileService::load(firstPath);
    const auto second = FileService::load(secondPath);
    QVERIFY(std::holds_alternative<Project>(firstAfter));
    QVERIFY(std::holds_alternative<Project>(second));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(firstAfter)),
             ProjectJson::toJson(std::get<Project>(first)));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(second)),
             ProjectJson::toJson(document->project()));
    MainWindow reopened;
    QVERIFY(reopened.openFile(secondPath));
    QCOMPARE(reopened.currentRootTitle(), QStringLiteral("继续保存只更新新文件"));
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

void TstUi::nodeMetadataSurvivesEditorAndFileRoundTrip()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *add = window.findChild<QAction *>(QStringLiteral("addChildTopic"));
    QVERIFY(document && commands && outline && add);
    const auto initialSheet = std::get<TreeSheet>(document->project().sheets.first());
    const ID sheetId = initialSheet.id;
    const ID rootId = initialSheet.rootId;
    NodeCommands nodes(commands, document);
    NodeMetaUpdate metadata;
    metadata.note = QStringLiteral("编辑后仍需保留的多行备注\n第二行中文");
    metadata.iconId = QStringLiteral("idea");
    metadata.link = QStringLiteral("https://example.org/course");
    metadata.tags = QStringList{QStringLiteral("课程"), QStringLiteral("待复习")};
    QVERIFY(std::holds_alternative<bool>(nodes.updateNodeMeta(sheetId, rootId, metadata)));
    NodeStyleOverride style;
    style.fill = QStringLiteral("#247A62");
    style.fontSize = 24;
    style.bold = true;
    QVERIFY(std::holds_alternative<bool>(nodes.setStyle(sheetId, rootId, style)));

    // 通过既有窗口入口改名、撤销重做及新增撤销，验证消费者不重建或丢弃其它字段。
    const QString originalTitle = window.currentRootTitle();
    QVERIFY(window.renameRootTo(QStringLiteral("带元数据的主题")));
    commands->undo();
    QCOMPARE(window.currentRootTitle(), originalTitle);
    commands->redo();
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("带元数据的主题"));
    answerTitleDialog(QStringLiteral("临时子主题"));
    add->trigger();
    QCOMPARE(outline->currentItem()->parent()->data(0, Qt::UserRole).toString(), rootId);
    commands->undo();
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), rootId);
    QCOMPARE(outline->topLevelItem(0)->childCount(), 0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("元数据兼容.lingya"));
    QVERIFY(window.saveProjectTo(path));
    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(loaded)),
             ProjectJson::toJson(document->project()));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    auto *reopenedDocument = reopened.findChild<ProjectDocument *>();
    auto *view = reopened.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QVERIFY(reopenedDocument && view);
    QCOMPARE(ProjectJson::toJson(reopenedDocument->project()),
             ProjectJson::toJson(document->project()));
    const auto root =
        std::get<TreeSheet>(reopenedDocument->project().sheets.first()).nodes.value(rootId);
    QCOMPARE(root.title, QStringLiteral("带元数据的主题"));
    QCOMPARE(root.note, *metadata.note);
    QVERIFY(root.iconId == metadata.iconId);
    QVERIFY(root.link == metadata.link);
    QCOMPARE(root.tags, *metadata.tags);
    QVERIFY(root.style.fill == style.fill);
    QVERIFY(root.style.fontSize == style.fontSize);
    QVERIFY(root.style.bold == style.bold);
    QCOMPARE(canvasNodes(view).size(), 1);
    QVERIFY(canvasNodes(view).contains(rootId));
    // 此处仅验证数据保留与M1画布仍可用，不断言尚未接线的M2样式绘制。
}

void TstUi::nodeMetadataNoOpKeepsWindowClean()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QVERIFY(document && commands && outline && view);
    const auto initialSheet = std::get<TreeSheet>(document->project().sheets.first());
    const ID sheetId = initialSheet.id;
    const ID rootId = initialSheet.rootId;
    NodeCommands nodes(commands, document);
    NodeMetaUpdate metadata;
    metadata.note = QStringLiteral("原有备注");
    metadata.tags = QStringList{QStringLiteral("保留标签")};
    QVERIFY(std::holds_alternative<bool>(nodes.updateNodeMeta(sheetId, rootId, metadata)));
    NodeStyleOverride style;
    style.fill = QStringLiteral("#247A62");
    QVERIFY(std::holds_alternative<bool>(nodes.setStyle(sheetId, rootId, style)));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(window.saveProjectTo(dir.filePath(QStringLiteral("无操作基线.lingya"))));
    const auto saved = ProjectJson::toJson(document->project());
    const qint64 revision = document->sessionRevision();
    QSignalSpy changed(document, &ProjectDocument::changed);
    NodeItem *const visibleRoot = canvasNodes(view).value(rootId);
    QVERIFY(visibleRoot);
    QVERIFY(std::holds_alternative<bool>(nodes.updateNodeMeta(sheetId, rootId, metadata)));
    QVERIFY(std::holds_alternative<bool>(nodes.setStyle(sheetId, rootId, style)));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(document->sessionRevision(), revision);
    QVERIFY(!document->isModified());
    QCOMPARE(ProjectJson::toJson(document->project()), saved);
    QCOMPARE(canvasNodes(view).value(rootId), visibleRoot);

    NodeMetaUpdate patch;
    patch.note = QStringLiteral("仅改备注");
    QVERIFY(std::holds_alternative<bool>(nodes.updateNodeMeta(sheetId, rootId, patch)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(document->isModified());
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), rootId);
    QCOMPARE(view->scene()->selectedItems().size(), 1);
    const auto changedRoot =
        std::get<TreeSheet>(document->project().sheets.first()).nodes.value(rootId);
    QCOMPARE(changedRoot.note, *patch.note);
    QCOMPARE(changedRoot.tags, *metadata.tags);
    QVERIFY(changedRoot.style.fill == style.fill);
    commands->undo();
    QCOMPARE(changed.count(), 2);
    const auto restored =
        std::get<TreeSheet>(document->project().sheets.first()).nodes.value(rootId);
    QCOMPARE(restored.note, *metadata.note);
    QCOMPARE(restored.tags, *metadata.tags);
    QVERIFY(restored.style.fill == style.fill);
    // 一次撤销直接恢复真实补丁前的备注，证明两次无操作没有占据历史栈顶。
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), rootId);
    QVERIFY(canvasNodes(view).contains(rootId));
}

void TstUi::nodePropertiesEditClearUndoAndRoundTrip()
{
    MainWindow window;
    if (!qEnvironmentVariableIsEmpty("LINGYA_PROPERTIES_CAPTURE"))
    {
        QFile style(QFINDTESTDATA("../resources/styles/light.qss"));
        QVERIFY(style.open(QIODevice::ReadOnly));
        window.setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    auto *edit = window.findChild<QAction *>(QStringLiteral("editNodeProperties"));
    auto *open = window.findChild<QAction *>(QStringLiteral("openNodeLink"));
    QVERIFY(document && commands && outline && view && edit && open);
    Project project = canvasProject();
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    const ID branchId = canvasId(QStringLiteral("branch-0"));
    tree.nodes[branchId].tags = {QStringLiteral("保留原标签")};
    tree.nodes[branchId].style.fill = QStringLiteral("#247A62");
    document->replaceProject(project);
    commands->clearHistory();
    document->setClean();
    document->notifyChanged();
    outline->setCurrentItem(outline->topLevelItem(0)->child(0));
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), branchId);
    QVERIFY(!open->isEnabled());
    const qint64 revision = document->sessionRevision();
    QSignalSpy changed(document, &ProjectDocument::changed);
    answerNodeProperties(
        window,
        [](QDialog *dialog)
        {
            auto *icons = dialog->findChild<QComboBox *>(QStringLiteral("nodeIcon"));
            auto *note = dialog->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
            auto *link = dialog->findChild<QLineEdit *>(QStringLiteral("nodeLink"));
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(icons && note && link && buttons);
            icons->setCurrentIndex(icons->findData(QStringLiteral("star")));
            note->setPlainText(QStringLiteral("属性里的中文备注\n第二行"));
            link->setText(QStringLiteral("https://example.org/topic"));
            const QString capture = qEnvironmentVariable("LINGYA_PROPERTIES_CAPTURE");
            if (!capture.isEmpty())
                QVERIFY(dialog->grab().save(capture));
            buttons->button(QDialogButtonBox::Ok)->click();
        });
    edit->trigger();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(document->sessionRevision(), revision + 1);
    const auto edited =
        std::get<TreeSheet>(document->project().sheets.first()).nodes.value(branchId);
    QCOMPARE(edited.note, QStringLiteral("属性里的中文备注\n第二行"));
    QVERIFY(edited.iconId == std::optional<QString>(QStringLiteral("star")));
    QVERIFY(edited.link == std::optional<QString>(QStringLiteral("https://example.org/topic")));
    QCOMPARE(edited.tags, QStringList{QStringLiteral("保留原标签")});
    QVERIFY(edited.style.fill == tree.nodes.value(branchId).style.fill);
    QVERIFY(open->isEnabled());
    LinkReceiver receiver;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &receiver, "open");
    const auto restoreUrlHandler =
        qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); });
    open->trigger();
    QCOMPARE(receiver.count, 1);
    QCOMPARE(receiver.last, QUrl(*edited.link));

    // 三字段同一事务，撤销一次全部恢复；选择与未编辑字段保持。
    commands->undo();
    const auto undone =
        std::get<TreeSheet>(document->project().sheets.first()).nodes.value(branchId);
    QVERIFY(undone.note.isEmpty() && !undone.iconId && !undone.link);
    QVERIFY(!commands->canUndo());
    QVERIFY(!open->isEnabled());
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), branchId);
    commands->redo();
    const QString canvasCapture = qEnvironmentVariable("LINGYA_PROPERTIES_CANVAS_CAPTURE");
    if (!canvasCapture.isEmpty())
    {
        window.show();
        QTest::qWait(20);
        QVERIFY(window.grab().save(canvasCapture));
    }
    answerNodeProperties(
        window,
        [](QDialog *dialog)
        {
            dialog->findChild<QComboBox *>(QStringLiteral("nodeIcon"))->setCurrentIndex(0);
            dialog->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"))->clear();
            dialog->findChild<QLineEdit *>(QStringLiteral("nodeLink"))->clear();
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    edit->trigger();
    const auto cleared =
        std::get<TreeSheet>(document->project().sheets.first()).nodes.value(branchId);
    QVERIFY(cleared.note.isEmpty() && !cleared.iconId && !cleared.link);
    commands->undo();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("主题属性.lingya"));
    QVERIFY(window.saveProjectTo(path));
    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(loaded)),
             ProjectJson::toJson(document->project()));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    auto *reopenedDocument = reopened.findChild<ProjectDocument *>();
    QVERIFY(reopenedDocument);
    QCOMPARE(ProjectJson::toJson(reopenedDocument->project()),
             ProjectJson::toJson(document->project()));
    auto *reopenedView = reopened.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QVERIFY(reopenedView);
    QCOMPARE(canvasNodes(reopenedView).size(), canvasNodes(view).size());
    const auto originalItems = canvasNodes(view);
    const auto reopenedItems = canvasNodes(reopenedView);
    for (auto it = originalItems.constBegin(); it != originalItems.constEnd(); ++it)
    {
        // 保存前后按相同图标/字体测量，布局坐标和尺寸一致。
        QVERIFY(canvasNodes(reopenedView).contains(it.key()));
        QCOMPARE(canvasNodes(reopenedView).value(it.key())->boundingRect(),
                 it.value()->boundingRect());
        QCOMPARE(canvasNodes(reopenedView).value(it.key())->pos(), it.value()->pos());
    }
}

void TstUi::nodePropertiesCancelInvalidAndUnknownIcon()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *edit = window.findChild<QAction *>(QStringLiteral("editNodeProperties"));
    QVERIFY(document && commands && edit);
    Project project = canvasProject();
    auto &sheet = std::get<TreeSheet>(project.sheets.first());
    sheet.nodes[sheet.rootId].iconId = QStringLiteral("future-icon");
    sheet.nodes[sheet.rootId].note = QStringLiteral("原备注");
    document->replaceProject(project);
    commands->clearHistory();
    document->setClean();
    document->notifyChanged();
    const auto before = ProjectJson::toJson(document->project());
    const qint64 revision = document->sessionRevision();
    QSignalSpy changed(document, &ProjectDocument::changed);
    answerNodeProperties(
        window,
        [](QDialog *dialog)
        {
            dialog->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"))
                ->setPlainText(QStringLiteral("取消的修改"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        });
    edit->trigger();
    answerNodeProperties(
        window,
        [](QDialog *dialog)
        {
            auto *icons = dialog->findChild<QComboBox *>(QStringLiteral("nodeIcon"));
            QCOMPARE(icons->currentData().toString(), QStringLiteral("future-icon"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    edit->trigger();
    answerNodeProperties(
        window,
        [](QDialog *dialog)
        {
            dialog->findChild<QLineEdit *>(QStringLiteral("nodeLink"))
                ->setText(QStringLiteral("javascript:alert(1)"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QVERIFY(dialog->isVisible());
            QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("nodeMetadataError"))
                        ->text()
                        .contains(QStringLiteral("http")));
        });
    edit->trigger();
    answerNodeProperties(
        window,
        [](QDialog *dialog)
        {
            dialog->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"))
                ->setPlainText(QString(20001, QChar(0x4e2d)));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QVERIFY(dialog->isVisible());
            QVERIFY(!dialog->findChild<QLabel *>(QStringLiteral("nodeMetadataError"))
                         ->text()
                         .isEmpty());
        });
    edit->trigger();
    QCOMPARE(changed.count(), 0);
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(ProjectJson::toJson(document->project()), before);
    QVERIFY(!document->isModified());
    QVERIFY(!commands->canUndo());
}

void TstUi::nodeIconResourcesAndMeasurement()
{
    const QStringList ids{QStringLiteral("idea"), QStringLiteral("star"), QStringLiteral("check")};
    for (const QString &id : ids)
    {
        // 运行资源必须能用Qt内置PNG解码，不依赖团队未统一安装的Svg插件。
        QFile resource(QStringLiteral(":/lingya/icons/%1.png").arg(id));
        QVERIFY(resource.open(QIODevice::ReadOnly));
        const QByteArray bytes = resource.readAll();
        QVERIFY(bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        const QImage raster = QImage::fromData(bytes, "PNG");
        QCOMPARE(raster.size(), QSize(64, 64));
        const auto icon = NodeItem::iconForId(id);
        QVERIFY(!icon.isNull());
        const auto pixmap = icon.pixmap(20, 20);
        QVERIFY(!pixmap.isNull());
        bool visible = false;
        const auto image = pixmap.toImage();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                visible |= qAlpha(image.pixel(x, y)) != 0;
        QVERIFY(visible);
    }
    QVERIFY(NodeItem::iconForId(QStringLiteral("future-icon")).isNull());
    QVERIFY(NodeItem::iconForId(QStringLiteral("https://example.org/icon.svg")).isNull());
    QVERIFY(NodeItem::iconForId(QStringLiteral("D:/untrusted/icon.svg")).isNull());
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(16);
    const QString title = QStringLiteral("图标和长中文测量") + QString(200, QChar(0x4e2d));
    const NodeItem plain(title, font);
    const NodeItem decorated(title, font, nullptr, QStringLiteral("idea"));
    const QFontMetricsF metrics(font);
    const auto text =
        metrics.boundingRect(QRectF(0, 0, decorated.nodeRect().width() - 32 - 28, 100000),
                             Qt::AlignCenter | Qt::TextWrapAnywhere, title);
    QVERIFY(decorated.nodeRect().height() >= text.height() + 32);
    QVERIFY(decorated.nodeRect().height() >= plain.nodeRect().height());
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

void TstUi::siblingKeyboardInsertionRoundTrip()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    answerTitleDialog(QStringLiteral("首个分支"));
    QTest::keyClick(outline, Qt::Key_Return); // 根Enter新增子主题
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("首个分支"));
    const ID parent = outline->currentItem()->parent()->data(0, Qt::UserRole).toString();
    answerTitleDialog(QStringLiteral("第二个分支"));
    QTest::keyClick(outline, Qt::Key_Return);
    QCOMPARE(outline->currentItem()->text(0), QStringLiteral("第二个分支"));
    QCOMPARE(outline->currentItem()->parent()->data(0, Qt::UserRole).toString(), parent);
    outline->setCurrentItem(outline->topLevelItem(0)->child(0));
    answerTitleDialog(QStringLiteral("插入中间"));
    window.findChild<QAction *>(QStringLiteral("addSiblingTopic"))->trigger();
    QCOMPARE(outline->topLevelItem(0)->childCount(), 3);
    QCOMPARE(outline->topLevelItem(0)->child(1)->text(0), QStringLiteral("插入中间"));
    QCOMPARE(outline->topLevelItem(0)->child(2)->text(0), QStringLiteral("第二个分支"));
    auto *commands = window.findChild<CommandService *>();
    commands->undo();
    QCOMPARE(outline->topLevelItem(0)->childCount(), 2);
    commands->redo();
    QCOMPARE(outline->topLevelItem(0)->childCount(), 3);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("同级编辑.lingya"));
    QVERIFY(window.saveProjectTo(path));
    QVERIFY(window.openFile(path));
    QCOMPARE(outline->topLevelItem(0)->child(1)->text(0), QStringLiteral("插入中间"));
}

void TstUi::deletionCancelAndRootProtection()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *remove = window.findChild<QAction *>(QStringLiteral("deleteTopic"));
    QVERIFY(!remove->isEnabled());
    const qint64 rootRevision = document->sessionRevision();
    QTest::keyClick(outline, Qt::Key_Delete);
    QCOMPARE(document->sessionRevision(), rootRevision);
    QVERIFY(!commands->canUndo());
    answerTitleDialog(QStringLiteral("取消删除的主题"));
    window.findChild<QAction *>(QStringLiteral("addChildTopic"))->trigger();
    QVERIFY(remove->isEnabled());
    const qint64 revision = document->sessionRevision();
    const ID selected = outline->currentItem()->data(0, Qt::UserRole).toString();
    answerDeletion(QStringLiteral("取消"));
    QTest::keyClick(outline, Qt::Key_Delete);
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), selected);
    QCOMPARE(outline->topLevelItem(0)->childCount(), 1);
    QTimer::singleShot(0,
                       []
                       {
                           for (QWidget *top : QApplication::topLevelWidgets())
                               if (auto *box = qobject_cast<QMessageBox *>(top))
                                   QTest::keyClick(box, Qt::Key_Return);
                       });
    remove->trigger(); // 默认按钮是取消，Enter也不能误删
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(outline->topLevelItem(0)->childCount(), 1);
}

void TstUi::deletionPoliciesAndUndo_data()
{
    QTest::addColumn<bool>("deleteTasks");
    QTest::newRow("保留独立任务") << false;
    QTest::newRow("连同任务删除") << true;
}

void TstUi::deletionPoliciesAndUndo()
{
    QFETCH(bool, deleteTasks);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Project fixture = deletionProject();
    const QString path = dir.filePath(QStringLiteral("删除影响.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, fixture)));
    QVERIFY(std::holds_alternative<Project>(FileService::load(path)));
    MainWindow window;
    QVERIFY(window.openFile(path));
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("topicOutline"));
    const Project original = document->snapshot();
    outline->setCurrentItem(outline->topLevelItem(0)->child(1));
    QString impact;
    answerDeletion(deleteTasks ? QStringLiteral("连同任务一起删除")
                               : QStringLiteral("保留任务并删除主题"),
                   &impact);
    window.findChild<QAction *>(QStringLiteral("deleteTopic"))->trigger();
    QVERIFY(impact.contains(QStringLiteral("共2个主题")));
    QVERIFY(impact.contains(QStringLiteral("关联任务1项")));
    const auto &tree = std::get<TreeSheet>(document->project().sheets.first());
    QCOMPARE(tree.nodes.size(), 5);
    QVERIFY(!tree.nodes.contains(canvasId(QStringLiteral("branch-1"))));
    QVERIFY(tree.relations.isEmpty() && tree.annotations.isEmpty() && tree.comments.isEmpty());
    QCOMPARE(document->project().cards.first().sourceState, QStringLiteral("missing"));
    QCOMPARE(document->project().routes.size(), 1);
    QCOMPARE(document->project().drafts.size(), 1);
    QCOMPARE(outline->currentItem(), outline->topLevelItem(0));
    QCOMPARE(document->project().tasks.size(), deleteTasks ? 1 : 2);
    if (deleteTasks)
        QVERIFY(document->project().tasks.first().predecessorIds.isEmpty());
    else
        QVERIFY(!document->project().tasks.first().source.has_value());
    // 删除中间分支后，存活兄弟的order仍必须连续。
    QCOMPARE(tree.nodes.value(canvasId(QStringLiteral("branch-2"))).order, 1);
    QVERIFY(window.saveProjectTo(path));
    QVERIFY(std::holds_alternative<Project>(FileService::load(path)));
    commands->undo();
    QVERIFY(!commands->canUndo()); // 删除作为唯一的一次事务
    auto beforeJson = ProjectJson::toJson(original);
    auto afterJson = ProjectJson::toJson(document->project());
    for (const QString &key : {QStringLiteral("revision"), QStringLiteral("updatedAt")})
    {
        beforeJson.remove(key);
        afterJson.remove(key);
    }
    QCOMPARE(afterJson, beforeJson);
    commands->redo();
    QCOMPARE(std::get<TreeSheet>(document->project().sheets.first()).nodes.size(), 5);
}

void TstUi::exportManualFixtures()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString output = qEnvironmentVariable("LINGYA_MANUAL_FIXTURES", temporary.path());
    QVERIFY(QDir().mkpath(output));
    Project hundred = canvasProject(9, 10);
    auto &large = std::get<TreeSheet>(hundred.sheets.first());
    large.nodes[canvasId(QStringLiteral("leaf-0-0"))].title =
        QString(120, QChar(0x4e2d)) + QStringLiteral("\n末行");
    Project collapsed = canvasProject();
    std::get<TreeSheet>(collapsed.sheets.first())
        .nodes[canvasId(QStringLiteral("branch-0"))]
        .collapsed = true;
    const QVector<QPair<QString, Project>> samples = {
        {QStringLiteral("01-basic.lingya"), canvasProject()},
        {QStringLiteral("02-delete-impact.lingya"), deletionProject()},
        {QStringLiteral("03-hundred-nodes.lingya"), hundred},
        {QStringLiteral("04-collapsed.lingya"), collapsed}};
    for (const auto &sample : samples)
    {
        const QString path = QDir(output).filePath(sample.first);
        QVERIFY(!QFile::exists(path)); // 受控样例只新建，不覆盖用户文件
        QVERIFY(std::holds_alternative<qint64>(FileService::save(path, sample.second)));
        const auto reloaded = FileService::load(path);
        QVERIFY(std::holds_alternative<Project>(reloaded));
        QCOMPARE(ProjectJson::toJson(std::get<Project>(reloaded)),
                 ProjectJson::toJson(sample.second));
    }
}

void TstUi::unsavedPromptButtonsAreChineseAndCancelKeepsContent()
{
    MainWindow window;
    QVERIFY(window.renameRootTo(QStringLiteral("必须保留的内容")));
    bool chinese = false;
    QTimer::singleShot(
        0,
        [&]
        {
            for (QWidget *top : QApplication::topLevelWidgets())
            {
                if (auto *box = qobject_cast<QMessageBox *>(top))
                {
                    chinese =
                        box->button(QMessageBox::Save)->text() == QStringLiteral("保存") &&
                        box->button(QMessageBox::Discard)->text() == QStringLiteral("放弃修改") &&
                        box->button(QMessageBox::Cancel)->text() == QStringLiteral("取消");
                    box->button(QMessageBox::Cancel)->click();
                }
            }
        });
    QAction *create = nullptr;
    for (QAction *action : window.findChildren<QAction *>())
        if (action->text().startsWith(QStringLiteral("新建")))
            create = action;
    QVERIFY(create != nullptr);
    create->trigger();
    QVERIFY(chinese);
    QCOMPARE(window.currentRootTitle(), QStringLiteral("必须保留的内容"));
    QVERIFY(window.findChild<ProjectDocument *>()->isModified());
}


void TstUi::loggedInSaveNamesIntoOwnedFolderOnly()
{
    QTemporaryDir sessionRoot;
    QVERIFY(sessionRoot.isValid());
    MainWindow::setSessionRootForTest(sessionRoot.path());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        sessionRoot.path(), QStringLiteral("测试用户"), QStringLiteral("abc123"))));

    MainWindow w;
    QVERIFY(w.loginSession(QStringLiteral("测试用户"), QStringLiteral("abc123")));
    QVERIFY(w.isLoggedIn());
    QVERIFY(w.renameRootTo(QStringLiteral("登录态保存内容")));
    QVERIFY(w.saveProjectToOwned(QStringLiteral(" 我的工程 ")));   // 命名去空白

    const QString ownedPath = UserFileService::userDir(sessionRoot.path(), QStringLiteral("测试用户")) +
                              QStringLiteral("/我的工程.lingya");
    QVERIFY(QFile::exists(ownedPath));
    // 保存后状态干净（正式保存语义）
    QVERIFY(!w.windowTitle().contains(QLatin1Char('*')));

    // 同名再保存=原位覆盖（我的文件内的Ctrl+S不弹命名）
    QVERIFY(w.renameRootTo(QStringLiteral("第二版内容")));
    QVERIFY(w.saveProjectToOwned(QStringLiteral("我的工程")));
    MainWindow reader;
    QVERIFY(reader.openFile(ownedPath));
    QCOMPARE(reader.currentRootTitle(), QStringLiteral("第二版内容"));
}

void TstUi::loggedInOpenRejectsOthersButAllowsPublicAndOwn()
{
    QTemporaryDir sessionRoot;
    QVERIFY(sessionRoot.isValid());
    MainWindow::setSessionRootForTest(sessionRoot.path());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        sessionRoot.path(), QStringLiteral("甲同学"), QStringLiteral("abc123"))));

    // 其他用户目录里放一个文件（直接经FileService落盘，模拟既有他人文件）
    const QString othersPath = UserFileService::userDir(sessionRoot.path(), QStringLiteral("乙同学")) +
                               QStringLiteral("/她的工程.lingya");
    QVERIFY(std::holds_alternative<qint64>(FileService::save(
        othersPath, ProjectFactory::createMindmap(QStringLiteral("乙的内容")))));

    MainWindow w;
    QVERIFY(w.loginSession(QStringLiteral("甲同学"), QStringLiteral("abc123")));
    QVERIFY(w.renameRootTo(QStringLiteral("甲的当前内容")));

    // 他人的文件：拒绝打开，当前工程保持
    autoCloseMessageBoxes();
    QVERIFY(!w.openFile(othersPath));
    QCOMPARE(w.currentRootTitle(), QStringLiteral("甲的当前内容"));

    // 公有文件：照常打开
    QTemporaryDir publicDir;
    const QString publicPath = publicDir.filePath(QStringLiteral("公开工程.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(
        publicPath, ProjectFactory::createMindmap(QStringLiteral("公开内容")))));
    QVERIFY(w.openFile(publicPath));
    QCOMPARE(w.currentRootTitle(), QStringLiteral("中心主题"));   // 工厂默认根标题

    // 自己的文件：通过“我的文件”按名打开
    QVERIFY(w.saveProjectToOwned(QStringLiteral("甲的工程")));
    QVERIFY(w.openOwnedFile(QStringLiteral("甲的工程")));
}

void TstUi::loginSessionRejectsWrongPassword()
{
    QTemporaryDir sessionRoot;
    QVERIFY(sessionRoot.isValid());
    MainWindow::setSessionRootForTest(sessionRoot.path());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        sessionRoot.path(), QStringLiteral("丙同学"), QStringLiteral("abc123"))));

    MainWindow w;
    autoCloseMessageBoxes();   // 失败提示自动关闭
    QVERIFY(!w.loginSession(QStringLiteral("丙同学"), QStringLiteral("错误口令")));
    QVERIFY(!w.isLoggedIn());
}

void TstUi::loggedOutOpensAnyFileUnchanged()
{
    QTemporaryDir sessionRoot;
    QVERIFY(sessionRoot.isValid());
    MainWindow::setSessionRootForTest(sessionRoot.path());
    const QString othersPath = UserFileService::userDir(sessionRoot.path(), QStringLiteral("乙同学")) +
                               QStringLiteral("/她的工程.lingya");
    QVERIFY(std::holds_alternative<qint64>(FileService::save(
        othersPath, ProjectFactory::createMindmap(QStringLiteral("乙的内容")))));

    MainWindow w;   // 未登录：现状不变，任何文件可开
    QVERIFY(w.openFile(othersPath));
    QVERIFY(!w.isLoggedIn());
}


void TstUi::loggedInOpensFileLooseInUsersRoot()
{
    // BUG-013复现：未登录把文件存进users根（不在任何用户子文件夹），登录后应能打开
    QTemporaryDir sessionRoot;
    QVERIFY(sessionRoot.isValid());
    MainWindow::setSessionRootForTest(sessionRoot.path());
    const QString loosePath = sessionRoot.filePath(QStringLiteral("users/散置工程.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(
        loosePath, ProjectFactory::createMindmap(QStringLiteral("散置内容")))));

    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        sessionRoot.path(), QStringLiteral("测试甲"), QStringLiteral("abc123"))));
    MainWindow w;
    QVERIFY(w.loginSession(QStringLiteral("测试甲"), QStringLiteral("abc123")));
    QVERIFY(w.openFile(loosePath)); // 修复前：被误判Others拒绝
}

void TstUi::ownedOpenCancelKeepsUnsavedProject()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setSessionRootForTest(root.path());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("复核同学"), QStringLiteral("test-only-one"))));
    MainWindow window;
    QVERIFY(window.loginSession(QStringLiteral("复核同学"), QStringLiteral("test-only-one")));
    QVERIFY(window.saveProjectToOwned(QStringLiteral("已存工程")));
    QVERIFY(window.renameRootTo(QStringLiteral("不能丢的未保存修改")));
    auto *document = window.findChild<ProjectDocument *>();
    QVERIFY(document);
    const auto before = ProjectJson::toJson(document->project());
    const qint64 revision = document->sessionRevision();
    autoCloseMessageBoxes();
    QVERIFY(!window.openOwnedFile(QStringLiteral("已存工程")));
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(ProjectJson::toJson(document->project()), before);
    QVERIFY(document->isModified());
}

void TstUi::ownedSaveProtectsExternalChanges_data()
{
    QTest::addColumn<bool>("removed");
    QTest::newRow("外部修改") << false;
    QTest::newRow("外部删除") << true;
}

void TstUi::ownedSaveProtectsExternalChanges()
{
    QFETCH(bool, removed);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setSessionRootForTest(root.path());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("复核同学"), QStringLiteral("test-only-one"))));
    MainWindow window;
    QVERIFY(window.loginSession(QStringLiteral("复核同学"), QStringLiteral("test-only-one")));
    QVERIFY(window.saveProjectToOwned(QStringLiteral("防覆盖工程")));
    const QString path = UserFileService::userDir(root.path(), QStringLiteral("复核同学")) +
                         QStringLiteral("/防覆盖工程.lingya");
    QVERIFY(window.renameRootTo(QStringLiteral("当前未保存修改")));
    auto *document = window.findChild<ProjectDocument *>();
    QVERIFY(document);
    const auto before = ProjectJson::toJson(document->project());
    if (removed)
        QVERIFY(QFile::remove(path));
    else
        QVERIFY(std::holds_alternative<qint64>(
            FileService::save(path, ProjectFactory::createMindmap(QStringLiteral("外部版本")))));
    QByteArray externalBytes;
    if (!removed)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        externalBytes = file.readAll();
    }
    autoCloseMessageBoxes();
    QVERIFY(!window.saveProjectToOwned(QStringLiteral("防覆盖工程")));
    QCOMPARE(ProjectJson::toJson(document->project()), before);
    QVERIFY(document->isModified());
    if (removed)
        QVERIFY(!QFile::exists(path));
    else
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), externalBytes);
    }
}

void TstUi::ownedListUsesStableFileIdentity_data()
{
    QTest::addColumn<bool>("removed");
    QTest::newRow("排序变化") << false;
    QTest::newRow("所选文件删除") << true;
}

void TstUi::ownedListUsesStableFileIdentity()
{
    QFETCH(bool, removed);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString user = QStringLiteral("复核同学");
    const auto savedA =
        UserFileService::saveOwned(root.path(), user, QStringLiteral("原选中"),
                                   ProjectFactory::createMindmap(QStringLiteral("原选中")));
    const auto savedB =
        UserFileService::saveOwned(root.path(), user, QStringLiteral("旁边文件"),
                                   ProjectFactory::createMindmap(QStringLiteral("旁边文件")));
    QVERIFY(std::holds_alternative<QString>(savedA));
    QVERIFY(std::holds_alternative<QString>(savedB));
    QFile fileA(std::get<QString>(savedA));
    QFile fileB(std::get<QString>(savedB));
    QVERIFY(fileA.open(QIODevice::ReadWrite));
    QVERIFY(fileB.open(QIODevice::ReadWrite));
    QVERIFY(fileA.setFileTime(
        QDateTime::fromString(QStringLiteral("2025-01-02T00:00:00Z"), Qt::ISODate),
        QFileDevice::FileModificationTime));
    QVERIFY(fileB.setFileTime(
        QDateTime::fromString(QStringLiteral("2025-01-01T00:00:00Z"), Qt::ISODate),
        QFileDevice::FileModificationTime));
    fileA.close();
    fileB.close();
    UserFilesDialog dialog(root.path(), user);
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list && list->count() == 2);
    QVERIFY(list->item(0)->text().startsWith(QStringLiteral("原选中")));
    list->setCurrentRow(0);
    if (removed)
        QVERIFY(QFile::remove(fileA.fileName()));
    else
    {
        QVERIFY(fileB.open(QIODevice::ReadWrite));
        QVERIFY(fileB.setFileTime(
            QDateTime::fromString(QStringLiteral("2025-02-01T00:00:00Z"), Qt::ISODate),
            QFileDevice::FileModificationTime));
        fileB.close();
    }
    auto *open = findButton(dialog, QStringLiteral("打开"));
    QVERIFY(open);
    open->click();
    if (removed)
    {
        QVERIFY(dialog.selectedFileName().isEmpty());
        QVERIFY(dialog.result() != QDialog::Accepted);
        const auto labels = dialog.findChildren<QLabel *>();
        QVERIFY(std::any_of(labels.begin(), labels.end(),
                            [](const QLabel *label) { return !label->text().isEmpty(); }));
    }
    else
        QCOMPARE(dialog.selectedFileName(), QStringLiteral("原选中.lingya"));
}

void TstUi::loginDialogValidationRegistrationAndCancel()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    LoginDialog dialog(root.path());
    if (!qEnvironmentVariableIsEmpty("LINGYA_LOGIN_CAPTURE"))
    {
        QFile style(QFINDTESTDATA("../resources/styles/light.qss"));
        QVERIFY(style.open(QIODevice::ReadOnly));
        dialog.setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    const auto edits = dialog.findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 2);
    QLineEdit *name = nullptr;
    QLineEdit *password = nullptr;
    for (auto *edit : edits)
        if (edit->echoMode() == QLineEdit::Password)
            password = edit;
        else
            name = edit;
    QVERIFY(name && password);
    name->setText(QStringLiteral("对话框同学"));
    password->setText(QStringLiteral("test-only-one"));
    auto *login = findButton(dialog, QStringLiteral("登录"));
    auto *signup = findButton(dialog, QStringLiteral("注册并登录"));
    QVERIFY(login && signup);
    dialog.show();
    login->click();
    QVERIFY(dialog.result() != QDialog::Accepted);
    const auto labels = dialog.findChildren<QLabel *>();
    QVERIFY(std::any_of(labels.begin(), labels.end(), [](const QLabel *label)
                        { return label->text() == QStringLiteral("用户名或密码错误"); }));
    const QString capture = qEnvironmentVariable("LINGYA_LOGIN_CAPTURE");
    if (!capture.isEmpty())
        QVERIFY(dialog.grab().save(capture));
    signup->click();
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(dialog.loggedInUserName(), QStringLiteral("对话框同学"));
    const QString accountPath = root.filePath(QStringLiteral("accounts.json"));
    QFile accounts(accountPath);
    QVERIFY(accounts.open(QIODevice::ReadOnly));
    const QByteArray before = accounts.readAll();
    accounts.close();
    LoginDialog cancelled(root.path());
    auto *cancel = findButton(cancelled, QStringLiteral("取消"));
    QVERIFY(cancel);
    cancelled.show();
    cancel->click();
    QVERIFY(cancelled.loggedInUserName().isEmpty());
    QVERIFY(accounts.open(QIODevice::ReadOnly));
    QCOMPARE(accounts.readAll(), before);
}

void TstUi::ownedListPreservesRepeatedExtension()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString user = QStringLiteral("复核同学");
    MainWindow::setSessionRootForTest(root.path());
    QVERIFY(std::holds_alternative<bool>(
        SessionService::createAccount(root.path(), user, QStringLiteral("test-only-one"))));
    QVERIFY(std::holds_alternative<QString>(UserFileService::saveOwned(
        root.path(), user, QStringLiteral("复验.lingya"),
        makeRecoveredProject(QStringLiteral("原文件"), QStringLiteral("原文件主题")))));
    QVERIFY(std::holds_alternative<QString>(UserFileService::saveOwned(
        root.path(), user, QStringLiteral("复验.lingya.lingya"),
        makeRecoveredProject(QStringLiteral("重复扩展"), QStringLiteral("应打开的主题")))));
    UserFilesDialog dialog(root.path(), user);
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    bool found = false;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text().startsWith(QStringLiteral("复验.lingya · ")))
        {
            list->setCurrentRow(i);
            found = true;
        }
    QVERIFY(found);
    auto *open = findButton(dialog, QStringLiteral("打开"));
    QVERIFY(open);
    open->click();
    QCOMPARE(dialog.selectedFileName(), QStringLiteral("复验.lingya.lingya"));
    const auto loaded = UserFileService::openOwned(root.path(), user, dialog.selectedFileName());
    QVERIFY(std::holds_alternative<Project>(loaded));
    const auto &sheet = std::get<TreeSheet>(std::get<Project>(loaded).sheets.first());
    QCOMPARE(sheet.nodes.value(sheet.rootId).title, QStringLiteral("应打开的主题"));
    MainWindow window;
    QVERIFY(window.loginSession(user, QStringLiteral("test-only-one")));
    QVERIFY(window.openOwnedFile(dialog.selectedFileName()));
    QVERIFY(window.renameRootTo(QStringLiteral("正确原位保存")));
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::keyClick(window.windowHandle(), Qt::Key_S, Qt::ControlModifier);
    QVERIFY(!window.findChild<ProjectDocument *>()->isModified());
    const auto original =
        UserFileService::openOwned(root.path(), user, QStringLiteral("复验.lingya"));
    const auto changed =
        UserFileService::openOwned(root.path(), user, QStringLiteral("复验.lingya.lingya"));
    QVERIFY(std::holds_alternative<Project>(original));
    QVERIFY(std::holds_alternative<Project>(changed));
    const auto &originalSheet = std::get<TreeSheet>(std::get<Project>(original).sheets.first());
    const auto &changedSheet = std::get<TreeSheet>(std::get<Project>(changed).sheets.first());
    QCOMPARE(originalSheet.nodes.value(originalSheet.rootId).title, QStringLiteral("原文件主题"));
    QCOMPARE(changedSheet.nodes.value(changedSheet.rootId).title, QStringLiteral("正确原位保存"));
}

void TstUi::ownedSaveRejectsEmptyNameForDotFile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow::setSessionRootForTest(root.path());
    QVERIFY(std::holds_alternative<bool>(SessionService::createAccount(
        root.path(), QStringLiteral("复核同学"), QStringLiteral("test-only-one"))));
    MainWindow window;
    QVERIFY(window.loginSession(QStringLiteral("复核同学"), QStringLiteral("test-only-one")));
    QVERIFY(window.saveProjectToOwned(QStringLiteral(".lingya")));
    QVERIFY(window.renameRootTo(QStringLiteral("不应保存的修改")));
    auto *document = window.findChild<ProjectDocument *>();
    QVERIFY(document);
    const auto before = ProjectJson::toJson(document->project());
    autoCloseMessageBoxes();
    QVERIFY(!window.saveProjectToOwned(QString()));
    QVERIFY(document->isModified());
    QCOMPARE(ProjectJson::toJson(document->project()), before);
}

void TstUi::nodeStyleShapes_data()
{
    QTest::addColumn<QString>("shapeCode");
    for (const QString &code :
         {QStringLiteral("roundRect"), QStringLiteral("rect"), QStringLiteral("ellipse"),
          QStringLiteral("diamond"), QStringLiteral("parallelogram")})
        QTest::newRow(qPrintable(code)) << code;
}

void TstUi::nodeStyleShapes()
{
    QFETCH(QString, shapeCode);
    NodeStyle defaults;
    defaults.fill = QStringLiteral("#F0E0C0");
    defaults.color = QStringLiteral("#183A72");
    defaults.borderColor = QStringLiteral("#772244");
    defaults.fontSize = 24;
    defaults.bold = true;
    NodeStyleOverride local;
    local.shape = shapeCode;
    local.fontSize = 32;
    local.bold = false;
    local.borderWidth = 20;
    const NodeStyle resolved = NodeItem::resolvedStyle(defaults, local);
    QCOMPARE(resolved.fill, defaults.fill);
    QCOMPARE(resolved.fontSize, 32);
    QVERIFY(!resolved.bold);
    QFont fallback(QStringLiteral("Microsoft YaHei"));
    const QFont font = NodeItem::styledFont(fallback, resolved);
    QCOMPARE(font.pixelSize(), 32);
    QVERIFY(!font.bold());
    NodeItem item(QStringLiteral("长标题与图标共同显示\n样式Aa中文第二行"), font, nullptr,
                  QStringLiteral("star"), resolved);
    const QRectF text = item.textRect();
    for (const QPointF &corner :
         {text.topLeft(), text.topRight(), text.bottomLeft(), text.bottomRight()})
        QVERIFY(item.shape().contains(corner));
    QVERIFY(item.boundingRect().contains(item.shape().boundingRect().adjusted(-10, -10, 10, 10)));
    QImage image(item.boundingRect().size().toSize() + QSize(4, 4), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(-item.boundingRect().topLeft() + QPointF(2, 2));
    item.paint(&painter, nullptr, nullptr);
    painter.end();
    int fillPixels = 0, textPixels = 0, borderPixels = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            fillPixels += pixel == QColor(resolved.fill);
            textPixels += pixel == QColor(resolved.color);
            borderPixels += pixel == QColor(resolved.borderColor);
        }
    QVERIFY(fillPixels > 10 && textPixels > 0 && borderPixels > 0);
    const qreal expected =
        item.nodeRect().right() - (shapeCode == QStringLiteral("parallelogram") ? 12 : 0);
    QCOMPARE(item.horizontalAnchor(true), QPointF(expected, 0));
    QCOMPARE(item.horizontalAnchor(false), QPointF(-expected, 0));
    // 短中文标题按实际粗体宽度留余量，不能因小数像素取整把最后一个字挤到第二行。
    NodeStyle shortStyle = resolved;
    shortStyle.fontSize = 28;
    shortStyle.bold = true;
    const QFont shortFont = NodeItem::styledFont(fallback, shortStyle);
    const NodeItem shortItem(QStringLiteral("中心主题"), shortFont, nullptr, QString(), shortStyle);
    const QFontMetricsF metrics(shortFont);
    const QRectF singleLine = metrics.boundingRect(QRectF(0, 0, 10000, 10000),
                                                    Qt::AlignCenter | Qt::TextWrapAnywhere,
                                                    QStringLiteral("中心主题"));
    QVERIFY(shortItem.textRect().height() <= singleLine.height() + 0.01);
}

void TstUi::nodeStyleEditUndoAndRoundTrip()
{
    MainWindow window;
    if (!qEnvironmentVariableIsEmpty("LINGYA_STYLE_DIALOG_CAPTURE"))
    {
        QFile style(QFINDTESTDATA("../resources/styles/light.qss"));
        QVERIFY(style.open(QIODevice::ReadOnly));
        window.setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    auto *edit = window.findChild<QAction *>(QStringLiteral("editNodeStyle"));
    QVERIFY(document && commands && view && edit);
    const ID rootId = std::get<TreeSheet>(document->project().sheets.first()).rootId;
    const auto before = ProjectJson::toJson(document->project());
    const qint64 revision = document->sessionRevision();
    QSignalSpy changes(document, &ProjectDocument::changed);
    answerNodeStyle(
        window,
        [](QDialog *dialog)
        {
            dialog->findChild<QLineEdit *>(QStringLiteral("styleFill"))
                ->setText(QStringLiteral("#E7F0FF"));
            dialog->findChild<QLineEdit *>(QStringLiteral("styleColor"))
                ->setText(QStringLiteral("#102040"));
            dialog->findChild<QLineEdit *>(QStringLiteral("styleBorderColor"))
                ->setText(QStringLiteral("#406080"));
            dialog->findChild<QLineEdit *>(QStringLiteral("styleFontFamily"))
                ->setText(QStringLiteral("Microsoft YaHei"));
            dialog->findChild<QLineEdit *>(QStringLiteral("styleBorderWidth"))
                ->setText(QStringLiteral("3.125"));
            auto *size = dialog->findChild<QComboBox *>(QStringLiteral("styleFontSize"));
            size->setCurrentIndex(size->findData(28));
            auto *bold = dialog->findChild<QComboBox *>(QStringLiteral("styleBold"));
            bold->setCurrentIndex(bold->findData(true));
            auto *shape = dialog->findChild<QComboBox *>(QStringLiteral("styleShape"));
            shape->setCurrentIndex(shape->findData(QStringLiteral("diamond")));
            const QString capture = qEnvironmentVariable("LINGYA_STYLE_DIALOG_CAPTURE");
            if (!capture.isEmpty())
                QVERIFY(dialog->grab().save(capture));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    edit->trigger();
    QCOMPARE(changes.count(), 1);
    QCOMPARE(document->sessionRevision(), revision + 1);
    const auto styled = ProjectJson::toJson(document->project());
    const auto local =
        std::get<TreeSheet>(document->project().sheets.first()).nodes.value(rootId).style;
    QCOMPARE(local.fontSize.value(), 28);
    QCOMPARE(local.borderWidth.value(), 3.125);
    QCOMPARE(canvasNodes(view).value(rootId)->nodeStyle().shape, QStringLiteral("diamond"));
    const QRectF bounds = canvasNodes(view).value(rootId)->boundingRect();
    const QString canvasCapture = qEnvironmentVariable("LINGYA_STYLE_PREVIEW_CAPTURE");
    if (!canvasCapture.isEmpty())
    {
        window.show();
        window.findChild<QAction *>(QStringLiteral("fitTopic"))->trigger();
        QTest::qWait(20);
        QVERIFY(window.grab().save(canvasCapture));
    }
    commands->undo();
    QCOMPARE(ProjectJson::toJson(document->project()), before);
    QVERIFY(!commands->canUndo());
    commands->redo();
    QCOMPARE(ProjectJson::toJson(document->project()), styled);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("局部样式.lingya"));
    QVERIFY(window.saveProjectTo(file));
    MainWindow reopened;
    QVERIFY(reopened.openFile(file));
    auto *reopenedDocument = reopened.findChild<ProjectDocument *>();
    QCOMPARE(ProjectJson::toJson(reopenedDocument->project()), styled);
    auto *reopenedView = reopened.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QCOMPARE(canvasNodes(reopenedView).value(rootId)->boundingRect(), bounds);
    QCOMPARE(canvasNodes(reopenedView).value(rootId)->nodeStyle().fill, QStringLiteral("#E7F0FF"));
}

void TstUi::nodeStyleCancelInvalidResetAndNoOp()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *edit = window.findChild<QAction *>(QStringLiteral("editNodeStyle"));
    Project project = canvasProject();
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    tree.defaultNodeStyle.bold = true;
    tree.defaultNodeStyle.fontSize = 24;
    tree.defaultNodeStyle.fill = QStringLiteral("#FFF0D0");
    tree.nodes[tree.rootId].style.bold = false;
    tree.nodes[tree.rootId].style.borderWidth = 0.123456789;
    tree.nodes[tree.rootId].note = QStringLiteral("不修改备注");
    document->replaceProject(project);
    commands->clearHistory();
    document->setClean();
    document->notifyChanged();
    const auto before = ProjectJson::toJson(document->project());
    const qint64 revision = document->sessionRevision();
    answerNodeStyle(
        window,
        [](QDialog *dialog)
        {
            auto *bold = dialog->findChild<QComboBox *>(QStringLiteral("styleBold"));
            QCOMPARE(bold->currentIndex(), 1);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    edit->trigger();
    QCOMPARE(document->sessionRevision(), revision);
    QVERIFY(!commands->canUndo() && !document->isModified());
    answerNodeStyle(
        window,
        [](QDialog *dialog)
        {
            auto *fill = dialog->findChild<QLineEdit *>(QStringLiteral("styleFill"));
            auto *width = dialog->findChild<QLineEdit *>(QStringLiteral("styleBorderWidth"));
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            fill->setText(QStringLiteral("red"));
            buttons->button(QDialogButtonBox::Ok)->click();
            QVERIFY(dialog->isVisible());
            QVERIFY(
                !dialog->findChild<QLabel *>(QStringLiteral("nodeStyleError"))->text().isEmpty());
            fill->clear();
            width->setText(QStringLiteral("21"));
            buttons->button(QDialogButtonBox::Ok)->click();
            QVERIFY(dialog->isVisible());
            dialog->findChild<QPushButton *>(QStringLiteral("resetNodeStyle"))->click();
            buttons->button(QDialogButtonBox::Cancel)->click();
        });
    edit->trigger();
    QCOMPARE(ProjectJson::toJson(document->project()), before);
    QCOMPARE(document->sessionRevision(), revision);
    answerNodeStyle(
        window,
        [](QDialog *dialog)
        {
            dialog->findChild<QPushButton *>(QStringLiteral("resetNodeStyle"))->click();
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
    edit->trigger();
    QCOMPARE(document->sessionRevision(), revision + 1);
    const auto after = std::get<TreeSheet>(document->project().sheets.first());
    QVERIFY(!after.nodes.value(tree.rootId).style.bold.has_value());
    QCOMPARE(after.nodes.value(tree.rootId).note, QStringLiteral("不修改备注"));
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    QVERIFY(canvasNodes(view).value(tree.rootId)->nodeStyle().bold);
    QCOMPARE(canvasNodes(view).value(tree.rootId)->nodeStyle().fill, QStringLiteral("#FFF0D0"));
    QCOMPARE(canvasNodes(view).value(tree.rootId)->nodeStyle().fontSize, 24);
    commands->undo();
    QCOMPARE(ProjectJson::toJson(document->project()), before);
}

void TstUi::nodeStyleMixedHundredNodes()
{
    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    Project project = canvasProject(9, 10);
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    const QStringList shapes{QStringLiteral("roundRect"), QStringLiteral("rect"),
                             QStringLiteral("ellipse"), QStringLiteral("diamond"),
                             QStringLiteral("parallelogram")};
    int index = 0;
    for (auto &node : tree.nodes)
    {
        node.title = QStringLiteral("混合样式长标题中文") + QString(index % 25, QChar(0x4e2d));
        node.style.shape = shapes[index % shapes.size()];
        node.style.fontSize = 12 + index % 21;
        node.style.borderWidth = 0.1 + index % 20;
        node.iconId = QStringLiteral("star");
        ++index;
    }
    document->replaceProject(project);
    document->notifyChanged();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("topicPreview"));
    const auto items = canvasNodes(view).values();
    QCOMPARE(items.size(), 100);
    for (int i = 0; i < items.size(); ++i)
        for (int j = i + 1; j < items.size(); ++j)
            QVERIFY(!items[i]->sceneBoundingRect().intersects(items[j]->sceneBoundingRect()));
    const QString capture = qEnvironmentVariable("LINGYA_STYLE_CANVAS_CAPTURE");
    if (!capture.isEmpty())
    {
        window.show();
        window.findChild<QAction *>(QStringLiteral("fitTopic"))->trigger();
        QTest::qWait(20);
        QVERIFY(window.grab().save(capture));
    }
}

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
