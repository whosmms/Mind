#include "app/MainWindow.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "services/SettingsService.h"
#include "services/TemplateLibrary.h"
#include "ui/HomePage.h"
#include "ui/TemplateGallery.h"
#include <QAction>
#include <QDockWidget>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace lingya
{
void MainWindow::buildHomePage()
{
    m_homePage = new HomePage(this);
    m_centralStack->addWidget(m_homePage);
    auto *home = new QAction(QStringLiteral("返回首页"), this);
    home->setObjectName("showHomePage");
    home->setIconText(QStringLiteral("首页"));
    QPixmap picture(32, 32);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#247A62"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(4, 15), QPointF(16, 5));
    painter.drawLine(QPointF(16, 5), QPointF(28, 15));
    painter.drawRoundedRect(QRectF(8, 14, 16, 14), 2, 2);
    painter.end();
    home->setIcon(QIcon(picture));
    connect(home, &QAction::triggered, this, &MainWindow::showHomePage);
    if (auto *fileMenu = qobject_cast<QMenu *>(findChild<QAction *>("newProject")->parent()))
        fileMenu->insertAction(fileMenu->actions().first(), home);
    auto *rail = new QToolBar(QStringLiteral("导航"), this);
    rail->setObjectName("editorNavigation");
    rail->setMovable(false);
    rail->setFloatable(false);
    rail->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    rail->setIconSize(QSize(24, 24));
    rail->addAction(home);
    rail->addAction(findChild<QAction *>("toggleOutline"));
    rail->addAction(findChild<QAction *>("showReviewCards"));
    rail->addAction(findChild<QAction *>("checkProject"));
    rail->addAction(findChild<QAction *>("showAiWorkspace"));
    const QStringList labels{QStringLiteral("首页"), QStringLiteral("大纲"), QStringLiteral("复习"),
                             QStringLiteral("检查"), QStringLiteral("AI创作")};
    for (int index = 0; index < rail->actions().size(); ++index)
    {
        auto *action = rail->actions().at(index);
        auto *button = qobject_cast<QToolButton *>(rail->widgetForAction(action));
        const QString label = labels.at(index);
        button->setText(label);
        button->setAccessibleName(label);
        connect(action, &QAction::changed, button, [button, label] { button->setText(label); });
    }
    addToolBar(Qt::LeftToolBarArea, rail);
    connect(m_homePage, &HomePage::continueRequested, this, &MainWindow::returnFromHome);
    connect(m_homePage, &HomePage::preferencesRequested, this, &MainWindow::showPreferences);
    connect(m_homePage, &HomePage::templateRequested, this, &MainWindow::createFromHome);
    connect(m_homePage, &HomePage::templateGalleryRequested, this,
            [this] { showTemplates(false); });
    connect(m_homePage, &HomePage::openRequested, this, &MainWindow::openProject);
    connect(m_homePage, &HomePage::recentRequested, this, &MainWindow::openRecentFile);
    connect(m_homePage, &HomePage::removeRecentRequested, this,
            [this](const QString &path)
            {
                SettingsService::removeRecentFile(path);
                rebuildRecentMenu();
            });
}

void MainWindow::refreshHomePage()
{
    if (!m_homePage)
        return;
    m_homePage->setRecentFiles(SettingsService::recentFiles());
    m_homePage->setCurrentTitle(m_filePath.isEmpty() ? m_doc->project().title
                                                     : QFileInfo(m_filePath).completeBaseName());
}

void MainWindow::showHomePage()
{
    if (!m_homePage)
        return;
    refreshHomePage();
    if (!m_homeVisible)
    {
        m_pageBeforeHome = m_centralStack->currentIndex();
        m_editorWindowState = saveState();
        m_homeActionStates.clear();
        for (auto *action : findChildren<QAction *>())
        {
            const QString name = action->objectName();
            if (name.isEmpty() || name == "showHomePage" || name == "newProject" ||
                name == "openProject" || name == "saveProject" || name == "showPreferences" ||
                name == "onlineAccount" || name == "onlineAccountSettings")
                continue;
            m_homeActionStates.append({QPointer<QAction>(action), action->isEnabled()});
            action->setEnabled(false);
        }
        m_homeVisible = true;
        for (auto *dock : findChildren<QDockWidget *>())
            dock->hide();
        for (auto *bar : findChildren<QToolBar *>())
            bar->hide();
    }
    m_centralStack->setCurrentWidget(m_homePage);
    if (auto *zoom = findChild<QWidget *>("canvasZoomControls"))
        zoom->hide();
    statusBar()->showMessage(QStringLiteral("首页 · 选择模板或打开工程，当前内容保留在编辑器中"));
    updateInlineProperties();
}

void MainWindow::returnFromHome()
{
    if (!m_homeVisible)
        return;
    m_homeVisible = false;
    restoreState(m_editorWindowState);
    for (const auto &entry : m_homeActionStates)
        if (entry.first)
            entry.first->setEnabled(entry.second);
    m_homeActionStates.clear();
    m_centralStack->setCurrentIndex(m_pageBeforeHome == 1 ? 1 : 0);
    resizeEditorChrome();
    updateStatusLine();
}

void MainWindow::createFromHome(const QString &kind)
{
    Project next;
    if (kind == "mindmap")
        next = ProjectFactory::createMindmap(QStringLiteral("未命名导图"));
    else if (kind == "fishbone")
        next = ProjectFactory::createFishbone(QStringLiteral("未命名鱼骨图"));
    else if (kind == "flowchart")
        next = ProjectFactory::createFlowchart(QStringLiteral("未命名流程图"));
    else if (kind == "gantt")
        next = ProjectFactory::createGantt(QStringLiteral("未命名甘特图"));
    else
    {
        const auto result = TemplateLibrary::create(kind);
        if (const auto *error = std::get_if<AppError>(&result))
        {
            statusBar()->showMessage(error->message, 8000);
            return;
        }
        next = std::get<Project>(result);
    }
    if (!confirmDiscard())
        return;
    // 仅初始化尚未装入文档的新模板；已有工程样式按原文件保留。
    if (auto *tree = std::get_if<TreeSheet>(&next.sheets.first()))
    {
        auto &root = tree->nodes[tree->rootId];
        root.style.fill = "#247A62";
        root.style.color = "#FFFFFF";
        root.style.borderColor = "#247A62";
        root.style.bold = true;
        root.style.fontSize = 20;
        for (auto &node : tree->nodes)
            if (node.parentId && *node.parentId == tree->rootId)
            {
                node.style.fill = "#DDF2E7";
                node.style.bold = true;
            }
    }
    m_filePath.clear();
    m_fileStamp.reset();
    loadNewDocument(std::move(next));
    fitCanvas();
}
void MainWindow::showTemplates(bool insertSheet)
{
    const auto *tree = activeTreeSheet();
    TemplateGallery dialog(insertSheet && tree && tree->kind == "mindmap", this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!dialog.layoutCode.isEmpty() && tree)
    {
        const auto id = tree->id, layout = dialog.layoutCode;
        bool changed = tree->layout != layout;
        for (const auto &node : tree->nodes)
            changed |= node.manualPosition.has_value();
        if (!changed)
            return;
        const auto result = m_commands->applyEdit(
            QStringLiteral("切换导图结构"),
            [id, layout](Project &project)
            {
                for (auto &sheet : project.sheets)
                    if (auto *tree = std::get_if<TreeSheet>(&sheet); tree && tree->id == id)
                    {
                        tree->layout = layout;
                        for (auto &node : tree->nodes)
                            node.manualPosition.reset();
                    }
            });
        if (const auto *error = std::get_if<AppError>(&result))
            statusBar()->showMessage(error->message, 8000);
        else
            fitCanvas();
        return;
    }
    if (dialog.templateId.isEmpty())
        return;
    if (!insertSheet || m_homeVisible)
    {
        createFromHome(dialog.templateId);
        return;
    }
    if (m_doc->project().sheets.size() >= Limits::kMaxSheets)
    {
        statusBar()->showMessage(QStringLiteral("工作表数量已达上限"), 8000);
        return;
    }
    const auto result = TemplateLibrary::create(dialog.templateId);
    if (const auto *error = std::get_if<AppError>(&result))
    {
        statusBar()->showMessage(error->message, 8000);
        return;
    }
    const auto created = std::get<Project>(result);
    const auto applied = m_commands->applyEdit(QStringLiteral("插入模板工作表"),
                                               [created](Project &project)
                                               {
                                                   project.sheets.append(created.sheets.first());
                                                   project.tasks += created.tasks;
                                                   project.activeSheetId = created.activeSheetId;
                                               });
    if (const auto *error = std::get_if<AppError>(&applied))
        statusBar()->showMessage(error->message, 8000);
    else
        fitCanvas();
}

} // namespace lingya
