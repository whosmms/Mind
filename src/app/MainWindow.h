#pragma once
#include <QMainWindow>
#include <optional>
#include "domain/Domain.h"
#include "services/FileService.h"

// Qt类前向声明必须位于全局作用域，不能进入lingya命名空间
class QAction;
class QMenu;
class QGraphicsScene;
class QGraphicsView;
class QCloseEvent;
class QEvent;
class QObject;

namespace lingya {

// 项目内类型前向声明
class CommandService;
class NodeItem;
class ProjectDocument;

// 主窗口（原设计§3 src/app/MainWindow：装配页面、服务与控制器）
// M0范围：中文界面 + 纵向闭环“根节点→改名→保存→关闭→重开”（原设计§12 M0），
// 覆盖R01根主题编辑与R04文件管理的最小路径；完整编辑器为M1+。
// 交互细则按原设计§5.2：Ctrl+Z/Y撤销重做、Ctrl+S保存、Ctrl+Shift+S另存、F2重命名。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 打开指定路径的工程（命令行参数/文件关联入口）；成功返回true，失败弹中文提示并保持原工程
    bool openFile(const QString& path);

    // 将当前活动工作表根节点改名为title（对话框入口与测试共用）；失败返回false并保持原值
    bool renameRootTo(const QString& title);
    // 保存到指定路径（另存为对话框入口与测试共用）；失败返回false并保持脏状态
    bool saveProjectTo(const QString& path);
    // 当前活动工作表根节点标题（测试与后续大纲视图共用）
    QString currentRootTitle() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenus();
    void rebuildRecentMenu();       // 重建“最近文件”子菜单（R04，数据来自SettingsService）
    void openRecentFile(const QString& path);
    void newProject();
    void openProject();
    bool saveProject(bool saveAs);
    void renameRoot();
    bool confirmDiscard();          // 换工程/关闭时提供保存、放弃、取消（原设计§8）
    void loadNewDocument(Project project);
    void rebuildCanvas();
    void onDocumentChanged();
    const TreeSheet* activeTreeSheet() const;

    ProjectDocument* m_doc = nullptr;
    CommandService* m_commands = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    QString m_filePath;             // 磁盘路径只存在本机会话（原设计§8）
    // 已加载文件的外部状态戳（§11.3外部修改检测）；新建未命名或另存新路径时为空
    std::optional<FileService::FileStamp> m_fileStamp;
    QMenu* m_recentMenu = nullptr;  // 文件菜单内“最近文件”子菜单（R04）
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
};

}  // namespace lingya
