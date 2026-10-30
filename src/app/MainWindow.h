#pragma once
#include <QMainWindow>
#include <optional>
#include "domain/Domain.h"
#include "services/FileService.h"
#include "services/RecoveryService.h"

// Qt类前向声明必须位于全局作用域，不能进入lingya命名空间
class QAction;
class QMenu;
class QGraphicsScene;
class QGraphicsView;
class QStackedWidget;
class QCloseEvent;
class QEvent;
class QObject;
class QTimer;
class QTreeWidget;
class QComboBox;

namespace lingya {

// 项目内类型前向声明
class CommandService;
class NodeItem;
class ProjectDocument;
class RecoveryDialog;
class LoginDialog;
class UserFilesDialog;

// 主窗口（原设计§3 src/app/MainWindow：装配页面、服务与控制器）
// M0范围：中文界面 + 纵向闭环“根节点→改名→保存→关闭→重开”（原设计§12 M0），
// 覆盖R01根主题编辑与R04文件管理的最小路径；完整编辑器为M1+。
// 交互细则按原设计§5.2：Ctrl+Z/Y撤销重做、Ctrl+S保存、Ctrl+Shift+S另存、F2重命名。
// M1补充：恢复快照（规范§11.3、原设计§8）——30秒定时写脏文档快照、启动预览对话框、
// 采用走CommandService事务；快照与正式保存状态分离，清理只针对应用自建快照。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 打开指定路径的工程（命令行参数/文件关联入口）；成功返回true，失败弹中文提示并保持原工程
    bool openFile(const QString& path);

    // 甘特图接入点（M3/R13，丙调用）：把甘特视图装入中央区第2页并切换过去；
    // 重复调用替换旧视图；传nullptr为无操作
    void setGanttView(QGraphicsView* view);

    // 恢复快照启动预览（规范§11.3）：列出全部工程快照供用户预览决定采用；
    // 无快照时无任何界面。已打开预览对话框时重复调用为无操作
    void offerRecoverySnapshots();

    // 采用一份快照：经CommandService事务写入唯一文档（统一规范§7.1/§12），可撤销；
    // 采用成功后保留磁盘快照直至正式保存或明确放弃，并清空文件路径与外部戳
    // （快照≠正式保存，原设计§8：不记录来源路径，首次保存走“另存为”）：
    // 采用后的内容尚未落盘，此刻崩溃仍须可再次恢复。失败返回false并保持原状
    bool adoptRecoverySnapshot(const RecoveryService::SnapshotInfo& info);

    // 测试注入恢复根目录（规范§19.1隔离，同QSettings重定向模式）；传空恢复默认QStandardPaths路径
    static void setRecoveryRootForTest(const QString& path);

    // ---- 登录会话（2026-10-25新增需求，D-M2-02；未登录时全部行为维持现状）----
    // 登录：经SessionService校验；成功建立会话并刷新菜单/状态栏，失败返回false
    bool loginSession(const QString& userName, const QString& password);
    // 退出登录：仅清除会话，不影响当前工程与文件路径
    void logoutSession();
    bool isLoggedIn() const;
    QString sessionUserName() const;

    // 登录态保存：仅命名，落当前用户专有文件夹（不能指定位置，D-M2-02）。
    // 对话框入口与测试共用；成功后走与正式保存相同的状态流（清快照/记录最近文件）
    bool saveProjectToOwned(const QString& name);

    // 打开“我的文件”中的指定文件（对话框入口与测试共用）
    bool openOwnedFile(const QString& name);

    // 测试注入会话根目录（默认AppLocalDataLocation；账号库与users/均在其下）
    static void setSessionRootForTest(const QString& path);

    // 将当前活动工作表根节点改名为title（对话框入口与测试共用）；失败返回false并保持原值
    bool renameRootTo(const QString& title);
    // 保存到指定路径（另存为对话框入口与测试共用）；失败返回false并保持脏状态
    bool saveProjectTo(const QString& path);
    // 当前活动工作表根节点标题（测试与后续大纲视图共用）
    QString currentRootTitle() const;
    // 当前工程ID（恢复快照按工程ID组织，测试与后续多工程特性共用）
    QString currentProjectId() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // 30秒定时回调（Limits::kRecoverySnapshotIntervalMs）：仅脏文档且内容变化时写快照（§11.2）
    void writeRecoverySnapshot();

private:
    void buildMenus();
    void rebuildRecentMenu();       // 重建“最近文件”子菜单（R04，数据来自SettingsService）
    void openRecentFile(const QString& path);
    void newProject();
    void openProject();
    bool saveProject(bool saveAs);
    void renameSelectedNode();
    void editSelectedNodeProperties();
    void editSelectedNodeTags();
    void editSelectedNodeStyle();
    void openSelectedNodeLink();
    void addChildToSelection();
    void addSiblingToSelection();
    void deleteSelection();
    void rebuildOutline();
    void rebuildTagFilter();
    void applyOutlineTagFilter();
    void showGanttView();
    void setCanvasZoom(qreal zoom);
    void fitCanvas();
    void selectNode(const ID &nodeId);
    void syncCanvasSelection();
    void focusSelectedNode();
    bool confirmDiscard();          // 换工程/关闭时提供保存、放弃、取消（原设计§8）
    void loadNewDocument(Project project);
    void rebuildCanvas();
    void addFishboneEdges(const TreeSheet &display);
    void onDocumentChanged();
    void updateStatusLine();        // 状态栏统一拼装：文档状态+恢复快照备注
    void showLoginDialog();
    void showOwnedFilesDialog();
    void updateSessionActions();    // 登录/退出登录/我的文件菜单项的可用性
    const TreeSheet* activeTreeSheet() const;

    ProjectDocument* m_doc = nullptr;
    CommandService* m_commands = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    QTreeWidget *m_outline = nullptr;
    QComboBox *m_tagFilter = nullptr;
    ID m_selectedNodeId;                       // 临时选择状态，不持久化、不进入撤销历史
    QMap<ID, NodeItem *> m_nodeItems;          // 非拥有指针；图元由m_scene销毁
    QString m_canvasError;
    QStackedWidget* m_centralStack = nullptr;  // 页0思维导图画布 / 页1甘特图（M3）
    QWidget* m_ganttPage = nullptr;            // 甘特页当前内容（占位或丙的视图）
    QString m_filePath;             // 磁盘路径只存在本机会话（原设计§8）
    // 已加载文件的外部状态戳（§11.3外部修改检测）；新建未命名或另存新路径时为空
    std::optional<FileService::FileStamp> m_fileStamp;
    QMenu* m_recentMenu = nullptr;  // 文件菜单内“最近文件”子菜单（R04）
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction *m_siblingAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_propertiesAction = nullptr;
    QAction *m_styleAction = nullptr;
    QAction *m_tagsAction = nullptr;
    QAction *m_openLinkAction = nullptr;
    QTimer* m_recoveryTimer = nullptr;     // 30秒恢复快照定时器（仅脏文档生效）
    QString m_recoveryRoot;                // 恢复快照根目录（测试可注入）
    QString m_recoveryNote;                // 状态栏恢复快照备注（最近写入时间/失败）
    RecoveryDialog* m_recoveryDialog = nullptr;   // 启动预览对话框（同一时刻至多一个）
    QString m_sessionRoot;                  // 会话根目录（账号库+users/；测试可注入）
    QString m_sessionUser;                  // 当前登录用户名；空=未登录（行为维持现状）
    QAction* m_loginAction = nullptr;
    QAction* m_logoutAction = nullptr;
    QAction* m_ownedFilesAction = nullptr;
    static QString s_recoveryRootForTest;  // 测试注入的恢复根目录覆盖
    static QString s_sessionRootForTest;   // 测试注入的会话根目录覆盖
};

}  // namespace lingya
