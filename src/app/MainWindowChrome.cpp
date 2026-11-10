#include "app/MainWindow.h"

#include "charts/GanttView.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace lingya
{
namespace
{
enum class Glyph
{
    Leaf,
    File,
    Save,
    Add,
    Branch,
    Sibling,
    Edit,
    Delete,
    Undo,
    Redo,
    Copy,
    Cut,
    Paste,
    Link,
    Frame,
    Note,
    Tag,
    Style,
    Fit,
    Locate,
    ZoomIn,
    ZoomOut,
    Outline,
    Task,
    Review,
    Check,
    Flow,
    Account
};

// 用Qt画笔生成多倍率图标，不依赖QtSvg、字体图标或外部图标引擎。
QIcon chromeIcon(Glyph glyph)
{
    QIcon icon;
    for (int scale : {1, 2, 3})
        for (bool disabled : {false, true})
        {
            QPixmap pixmap(24 * scale, 24 * scale);
            pixmap.setDevicePixelRatio(scale);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(disabled ? "#A4B1A9" : "#247A62"), 1.7, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2)
            { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
            switch (glyph)
            {
                case Glyph::Account:
                {
                    p.drawEllipse(QRectF(8.5, 3, 7, 7));
                    QPainterPath shoulders;
                    shoulders.moveTo(4.5, 20);
                    shoulders.cubicTo(4.5, 10.5, 19.5, 10.5, 19.5, 20);
                    p.drawPath(shoulders);
                    break;
                }
                case Glyph::Leaf:
                {
                    QPainterPath leaf;
                    leaf.moveTo(12, 20);
                    leaf.cubicTo(2, 18, 2, 9, 3, 6);
                    leaf.cubicTo(12, 6, 14, 12, 12, 20);
                    leaf.moveTo(12, 15);
                    leaf.cubicTo(12, 6, 17, 3, 22, 3);
                    leaf.cubicTo(22, 11, 18, 15, 12, 15);
                    p.setBrush(QColor(disabled ? "#DDE6DF" : "#DDF2E7"));
                    p.drawPath(leaf);
                    line(12, 22, 12, 13);
                    break;
                }
                case Glyph::Save:
                    p.drawRoundedRect(QRectF(4, 3, 16, 18), 2, 2);
                    p.drawRect(QRectF(8, 3, 8, 6));
                    p.drawRect(QRectF(8, 14, 8, 7));
                    break;
                case Glyph::Add:
                    p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
                    line(12, 7, 12, 17);
                    line(7, 12, 17, 12);
                    break;
                case Glyph::Branch:
                case Glyph::Sibling:
                    p.drawRoundedRect(QRectF(2, 8, 7, 8), 2, 2);
                    line(9, 12, 13, 12);
                    line(13, 5, 13, 19);
                    line(13, 5, 16, 5);
                    line(13, 19, 16, 19);
                    p.drawRoundedRect(QRectF(16, 2, 6, 6), 1, 1);
                    if (glyph == Glyph::Sibling)
                        p.drawRoundedRect(QRectF(16, 16, 6, 6), 1, 1);
                    else
                    {
                        line(16, 19, 22, 19);
                        line(19, 16, 19, 22);
                    }
                    break;
                case Glyph::Edit:
                    line(5, 16, 16, 5);
                    line(16, 5, 20, 9);
                    line(20, 9, 9, 20);
                    line(9, 20, 4, 21);
                    line(4, 21, 5, 16);
                    line(13, 8, 17, 12);
                    break;
                case Glyph::Delete:
                    line(4, 6, 20, 6);
                    line(9, 3, 15, 3);
                    p.drawRoundedRect(QRectF(6, 6, 12, 15), 2, 2);
                    line(10, 10, 10, 17);
                    line(14, 10, 14, 17);
                    break;
                case Glyph::Undo:
                case Glyph::Redo:
                {
                    line(8, 4, 3, 9);
                    line(3, 9, 8, 14);
                    QPainterPath curve;
                    curve.moveTo(3, 9);
                    curve.lineTo(12, 9);
                    curve.cubicTo(23, 9, 23, 20, 12, 20);
                    p.drawPath(curve);
                    break;
                }
                case Glyph::Copy:
                    p.drawRoundedRect(QRectF(3, 3, 13, 14), 2, 2);
                    p.drawRoundedRect(QRectF(8, 8, 13, 14), 2, 2);
                    break;
                case Glyph::Cut:
                    p.drawEllipse(QRectF(3, 14, 6, 6));
                    p.drawEllipse(QRectF(15, 14, 6, 6));
                    line(7, 15, 19, 3);
                    line(17, 15, 5, 3);
                    break;
                case Glyph::Paste:
                    p.drawRoundedRect(QRectF(5, 5, 14, 17), 2, 2);
                    p.drawRoundedRect(QRectF(8, 2, 8, 6), 2, 2);
                    line(9, 13, 15, 13);
                    line(9, 17, 15, 17);
                    break;
                case Glyph::Link:
                    p.translate(12, 12);
                    p.rotate(-35);
                    p.drawRoundedRect(QRectF(-10, -3.5, 12, 7), 3.5, 3.5);
                    p.drawRoundedRect(QRectF(-2, -3.5, 12, 7), 3.5, 3.5);
                    break;
                case Glyph::Frame:
                    p.setPen(QPen(p.pen().color(), 1.7, Qt::DashLine));
                    p.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
                    break;
                case Glyph::Tag:
                    line(3, 3, 12, 3);
                    line(12, 3, 22, 13);
                    line(22, 13, 13, 22);
                    line(13, 22, 3, 12);
                    line(3, 12, 3, 3);
                    p.drawEllipse(QPointF(8, 8), 1.4, 1.4);
                    break;
                case Glyph::Style:
                    p.drawEllipse(QRectF(3, 3, 18, 18));
                    p.setBrush(p.pen().color());
                    for (const QPointF point : {QPointF(8, 8), QPointF(15, 7), QPointF(17, 13)})
                        p.drawEllipse(point, 1, 1);
                    break;
                case Glyph::Fit:
                case Glyph::Locate:
                    for (int i = 0; i < 4; ++i)
                    {
                        line(4, 9, 4, 4);
                        line(4, 4, 9, 4);
                        p.translate(12, 12);
                        p.rotate(90);
                        p.translate(-12, -12);
                    }
                    if (glyph == Glyph::Locate)
                        p.drawEllipse(QPointF(12, 12), 3, 3);
                    break;
                case Glyph::ZoomIn:
                case Glyph::ZoomOut:
                    p.drawEllipse(QRectF(3, 3, 13, 13));
                    line(15, 15, 21, 21);
                    line(6, 9.5, 13, 9.5);
                    if (glyph == Glyph::ZoomIn)
                        line(9.5, 6, 9.5, 13);
                    break;
                case Glyph::Outline:
                    for (int i = 0; i < 3; ++i)
                    {
                        line(3, 6 + i * 6, 5, 6 + i * 6);
                        line(9, 6 + i * 6, 21, 6 + i * 6);
                    }
                    break;
                case Glyph::Task:
                case Glyph::Check:
                    p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
                    line(7, 12, 11, 16);
                    line(11, 16, 18, 8);
                    break;
                case Glyph::Review:
                    p.drawRoundedRect(QRectF(4, 5, 16, 16), 2, 2);
                    line(8, 2, 8, 8);
                    line(16, 2, 16, 8);
                    line(4, 11, 20, 11);
                    line(9, 16, 15, 16);
                    break;
                case Glyph::Flow:
                    p.drawRoundedRect(QRectF(3, 3, 8, 6), 2, 2);
                    p.drawRoundedRect(QRectF(13, 15, 8, 6), 2, 2);
                    line(7, 9, 7, 18);
                    line(7, 18, 13, 18);
                    break;
                case Glyph::File:
                case Glyph::Note:
                    p.drawRoundedRect(QRectF(4, 2, 16, 20), 2, 2);
                    line(8, 8, 16, 8);
                    line(8, 12, 16, 12);
                    line(8, 16, 13, 16);
                    break;
            }
            p.end();
            if (glyph == Glyph::Redo)
                pixmap = QPixmap::fromImage(pixmap.toImage().mirrored(true, false));
            icon.addPixmap(pixmap, disabled ? QIcon::Disabled : QIcon::Normal);
        }
    return icon;
}

QToolButton *actionButton(QAction *action, const QString &label, Glyph glyph, QWidget *parent,
                          bool compact = false)
{
    if (!action)
        return nullptr;
    action->setIcon(chromeIcon(glyph));
    QString hint = action->text();
    hint.remove(QLatin1Char('&'));
    if (action->objectName() == "undoEdit")
        hint = QStringLiteral("后退：撤回上一步编辑");
    else if (action->objectName() == "redoEdit")
        hint = QStringLiteral("前进：恢复刚撤回的编辑");
    if (!action->shortcut().isEmpty())
        hint += QStringLiteral(" · ") + action->shortcut().toString(QKeySequence::NativeText);
    action->setToolTip(hint);
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("chrome_") + action->objectName());
    button->setDefaultAction(action);
    // 同一动作可出现在多个入口；短标签属于按钮，不回写共享QAction。
    button->setText(label);
    QObject::connect(action, &QAction::changed, button,
                     [button, label] { button->setText(label); });
    button->setAccessibleName(label);
    button->setToolButtonStyle(compact ? Qt::ToolButtonTextBesideIcon
                                       : Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(compact ? 18 : 24, compact ? 18 : 24));
    button->setProperty("compact", compact);
    button->setAutoRaise(true);
    return button;
}

struct ToolSpec
{
    const char *action;
    QString label;
    Glyph icon;
};

void installDockTitle(QDockWidget *dock)
{
    auto *title = new QWidget(dock);
    title->setObjectName("dockTitleControls");
    auto *row = new QHBoxLayout(title);
    row->setContentsMargins(10, 4, 6, 4);
    row->setSpacing(4);
    auto *caption = new QLabel(dock->windowTitle(), title);
    caption->setTextFormat(Qt::PlainText);
    row->addWidget(caption, 1);
    auto *floating = new QToolButton(title);
    floating->setObjectName("dockFloat");
    floating->setIcon(QIcon(":/lingya/icons/controls/float.png"));
    auto *close = new QToolButton(title);
    close->setObjectName("dockClose");
    close->setIcon(QIcon(":/lingya/icons/controls/close.png"));
    for (auto *button : {floating, close})
    {
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(28, 28);
        row->addWidget(button);
    }
    close->setToolTip(QStringLiteral("关闭面板"));
    close->setAccessibleName(close->toolTip());
    const auto update = [dock, floating, close]
    {
        floating->setToolTip(dock->isFloating() ? QStringLiteral("停靠面板")
                                                : QStringLiteral("浮动面板"));
        floating->setAccessibleName(floating->toolTip());
        floating->setVisible(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
        close->setVisible(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    };
    QObject::connect(floating, &QToolButton::clicked, dock,
                     [dock] { dock->setFloating(!dock->isFloating()); });
    QObject::connect(close, &QToolButton::clicked, dock, &QDockWidget::close);
    QObject::connect(dock, &QDockWidget::topLevelChanged, title, update);
    QObject::connect(dock, &QDockWidget::featuresChanged, title, update);
    QObject::connect(dock, &QDockWidget::windowTitleChanged, caption, &QLabel::setText);
    dock->setTitleBarWidget(title);
    update();
}
} // namespace

void MainWindow::buildEditorChrome()
{
    auto *account = new QToolButton(menuBar());
    account->setObjectName("accountCornerButton");
    m_onlineAccountAction->setIcon(chromeIcon(Glyph::Account));
    account->setDefaultAction(m_onlineAccountAction);
    account->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    account->setIconSize(QSize(18, 18));
    account->setFocusPolicy(Qt::StrongFocus);
    account->setCursor(Qt::PointingHandCursor);
    const auto updateAccountLabel = [account, action = m_onlineAccountAction]
    {
        account->setText(action->iconText());
        account->setAccessibleName(action->iconText());
    };
    connect(m_onlineAccountAction, &QAction::changed, account, updateAccountLabel);
    updateAccountLabel();
    menuBar()->setCornerWidget(account, Qt::TopRightCorner);

    auto *toolbar = findChild<QToolBar *>("editorToolbar");
    toolbar->clear();
    toolbar->setFloatable(false);
    auto *shell = new QWidget(toolbar);
    shell->setObjectName("editorRibbon");
    shell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *row = new QHBoxLayout(shell);
    row->setContentsMargins(12, 0, 8, 0);
    row->setSpacing(12);
    auto *brand = new QWidget(shell);
    auto *brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(8, 0, 8, 0);
    brandLayout->setSpacing(4);
    brandLayout->setAlignment(Qt::AlignCenter);
    auto *mark = new QLabel(brand);
    mark->setObjectName("brandMark");
    mark->setFixedHeight(36);
    mark->setPixmap(chromeIcon(Glyph::Leaf).pixmap(32, 32));
    mark->setAlignment(Qt::AlignCenter);
    auto *name = new QLabel(QStringLiteral("灵芽思维"), brand);
    name->setObjectName("brandName");
    name->setFixedHeight(26);
    name->setAlignment(Qt::AlignCenter);
    brandLayout->addWidget(mark);
    brandLayout->addWidget(name);
    row->addWidget(brand);
    auto *tabs = new QTabWidget(shell);
    tabs->setObjectName("ribbonTabs");
    tabs->setDocumentMode(true);
    tabs->setMinimumWidth(0);
    row->addWidget(tabs, 1);
    toolbar->addWidget(shell);

    const auto page = [tabs](const QString &title)
    {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget(scroll);
        content->setObjectName("ribbonPage");
        auto *layout = new QHBoxLayout(content);
        layout->setContentsMargins(0, 0, 0, 2);
        layout->setSpacing(8);
        layout->addStretch();
        scroll->setWidget(content);
        tabs->addTab(scroll, title);
        return layout;
    };
    const auto group =
        [this](QHBoxLayout *layout, const QString &title, std::initializer_list<ToolSpec> specs)
    {
        auto *box = new QFrame;
        box->setProperty("ribbonGroup", true);
        auto *column = new QVBoxLayout(box);
        column->setContentsMargins(8, 2, 8, 0);
        column->setSpacing(0);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(2);
        for (const auto &spec : specs)
            if (auto *button =
                    actionButton(findChild<QAction *>(spec.action), spec.label, spec.icon, box))
                buttons->addWidget(button);
        column->addLayout(buttons);
        auto *caption = new QLabel(title, box);
        caption->setProperty("secondary", true);
        caption->setAlignment(Qt::AlignCenter);
        column->addWidget(caption);
        layout->insertWidget(layout->count() - 1, box);
    };
    auto *start = page(QStringLiteral("开始"));
    group(start, QStringLiteral("文件"),
          {{"newProject", QStringLiteral("新建"), Glyph::File},
           {"openProject", QStringLiteral("打开"), Glyph::File},
           {"saveProject", QStringLiteral("保存"), Glyph::Save},
           {"exportSheet", QStringLiteral("导出"), Glyph::Save}});
    group(start, QStringLiteral("主题"),
          {{"addChildTopic", QStringLiteral("子主题"), Glyph::Branch},
           {"addSiblingTopic", QStringLiteral("同级"), Glyph::Sibling},
           {"renameTopic", QStringLiteral("改名"), Glyph::Edit},
           {"deleteTopic", QStringLiteral("删除"), Glyph::Delete}});
    group(start, QStringLiteral("剪贴板"),
          {{"copyTopic", QStringLiteral("复制"), Glyph::Copy},
           {"cutTopic", QStringLiteral("剪切"), Glyph::Cut},
           {"pasteTopic", QStringLiteral("粘贴"), Glyph::Paste}});
    group(start, QStringLiteral("历史"),
          {{"undoEdit", QStringLiteral("后退"), Glyph::Undo},
           {"redoEdit", QStringLiteral("前进"), Glyph::Redo}});
    group(start, QStringLiteral("结构"),
          {{"showTemplateGallery", QStringLiteral("模板"), Glyph::Frame}});
    group(start, QStringLiteral("查找"), {{"showSearch", QStringLiteral("搜索"), Glyph::Locate}});
    auto *insert = page(QStringLiteral("插入"));
    group(insert, QStringLiteral("主题信息"),
          {{"editNodeProperties", QStringLiteral("属性"), Glyph::Note},
           {"editNodeTags", QStringLiteral("标签"), Glyph::Tag},
           {"openNodeLink", QStringLiteral("链接"), Glyph::Link}});
    group(insert, QStringLiteral("关系与分组"),
          {{"editRelations", QStringLiteral("关系线"), Glyph::Link},
           {"editAnnotations", QStringLiteral("外框概要"), Glyph::Frame},
           {"editComments", QStringLiteral("批注"), Glyph::Note}});
    group(insert, QStringLiteral("图片"),
          {{"insertCanvasImage", QStringLiteral("画布图片"), Glyph::File},
           {"insertNodeImageAction", QStringLiteral("节点图片"), Glyph::File}});
    group(insert, QStringLiteral("任务与复习"),
          {{"linkSelectedTask", QStringLiteral("设为任务"), Glyph::Task},
           {"addReviewCard", QStringLiteral("复习卡片"), Glyph::Review}});

    auto *propertyDock = new QDockWidget(QStringLiteral("属性"), this);
    propertyDock->setObjectName("propertyDock");
    propertyDock->setMinimumWidth(256);
    propertyDock->setMaximumWidth(340);
    auto *property = new QWidget(propertyDock);
    property->setObjectName("propertyContent");
    auto *propertyLayout = new QVBoxLayout(property);
    propertyLayout->setContentsMargins(12, 12, 12, 12);
    auto *selection = new QLabel(property);
    selection->setObjectName("propertySelection");
    selection->setTextFormat(Qt::PlainText);
    selection->setWordWrap(true);
    selection->setMaximumHeight(64);
    propertyLayout->addWidget(selection);
    auto *propertyTabs = new QTabWidget(property);
    propertyTabs->setObjectName("propertyTabs");
    propertyLayout->addWidget(propertyTabs, 1);
    const auto propertyPage = [propertyTabs](const QString &title)
    {
        auto *scroll = new QScrollArea(propertyTabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *content = new QWidget(scroll);
        content->setObjectName("propertyPage");
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(8, 16, 8, 16);
        layout->setSpacing(12);
        layout->addStretch();
        scroll->setWidget(content);
        propertyTabs->addTab(scroll, title);
        return layout;
    };
    const auto section = [this](QVBoxLayout *layout, const QString &title, const char *summaryName,
                                std::initializer_list<ToolSpec> specs)
    {
        auto *box = new QWidget;
        auto *column = new QVBoxLayout(box);
        column->setContentsMargins(0, 0, 0, 8);
        auto *heading = new QLabel(title, box);
        heading->setProperty("sectionTitle", true);
        column->addWidget(heading);
        auto *summary = new QLabel(box);
        summary->setObjectName(QString::fromLatin1(summaryName));
        summary->setTextFormat(Qt::PlainText);
        summary->setWordWrap(true);
        summary->setMaximumHeight(104);
        summary->setProperty("secondary", true);
        column->addWidget(summary);
        for (const auto &spec : specs)
            if (auto *button = actionButton(findChild<QAction *>(spec.action), spec.label,
                                            spec.icon, box, true))
                column->addWidget(button, 0, Qt::AlignLeft);
        layout->insertWidget(layout->count() - 1, box);
    };
    auto *style = propertyPage(QStringLiteral("样式"));
    buildInlineStyle(style);
    section(style, QStringLiteral("工作表默认"), "sheetStyleSummary",
            {{"editSheetStyle", QStringLiteral("默认样式"), Glyph::Style}});
    auto *icons = propertyPage(QStringLiteral("图标"));
    buildInlineIcons(icons);
    auto *content = propertyPage(QStringLiteral("备注"));
    buildInlineNotes(content);
    section(content, QStringLiteral("其他属性"), "nodeContentSummary",
            {{"editFlowNode", QStringLiteral("流程节点"), Glyph::Flow}});
    section(content, QStringLiteral("标签与批注"), "nodeTagsSummary",
            {{"editNodeTags", QStringLiteral("编辑标签"), Glyph::Tag},
             {"editComments", QStringLiteral("查看批注"), Glyph::Note}});
    auto *tasks = propertyPage(QStringLiteral("任务"));
    section(tasks, QStringLiteral("所选任务"), "nodeTaskSummary",
            {{"linkSelectedTask", QStringLiteral("设为任务"), Glyph::Task},
             {"editTaskProperties", QStringLiteral("编辑任务"), Glyph::Edit}});
    propertyDock->setWidget(property);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock);
    auto *toggleProperty = propertyDock->toggleViewAction();
    toggleProperty->setObjectName("toggleProperties");
    auto *outlineDock = findChild<QDockWidget *>("outlineDock");
    outlineDock->toggleViewAction()->setObjectName("toggleOutline");
    auto *taskDock = findChild<QDockWidget *>("taskDock");
    taskDock->toggleViewAction()->setObjectName("toggleTasks");
    auto *view = page(QStringLiteral("视图"));
    group(view, QStringLiteral("画布"),
          {{"fitTopic", QStringLiteral("适应"), Glyph::Fit},
           {"locateTopic", QStringLiteral("定位"), Glyph::Locate},
           {"zoomIn", QStringLiteral("放大"), Glyph::ZoomIn},
           {"zoomOut", QStringLiteral("缩小"), Glyph::ZoomOut}});
    group(view, QStringLiteral("面板"),
          {{"toggleOutline", QStringLiteral("大纲"), Glyph::Outline},
           {"toggleProperties", QStringLiteral("属性"), Glyph::Style},
           {"toggleTasks", QStringLiteral("任务表"), Glyph::Task},
           {"checkProject", QStringLiteral("检查"), Glyph::Check}});
    group(view, QStringLiteral("学习"),
          {{"showReviewCards", QStringLiteral("复习"), Glyph::Review},
           {"managePresentation", QStringLiteral("讲解"), Glyph::Review}});
    auto *flow = page(QStringLiteral("流程"));
    group(flow, QStringLiteral("流程图"),
          {{"addFlowNode", QStringLiteral("新节点"), Glyph::Add},
           {"editFlowNode", QStringLiteral("节点属性"), Glyph::Edit},
           {"editFlowEdges", QStringLiteral("有向连线"), Glyph::Link},
           {"layoutFlow", QStringLiteral("自动布局"), Glyph::Flow},
           {"flowSnap", QStringLiteral("网格吸附"), Glyph::Frame}});
    auto *gantt = page(QStringLiteral("任务"));
    group(gantt, QStringLiteral("甘特图"),
          {{"newTask", QStringLiteral("新建任务"), Glyph::Add},
           {"editTaskProperties", QStringLiteral("任务属性"), Glyph::Edit},
           {"toggleTasks", QStringLiteral("任务表"), Glyph::Task},
           {"checkProject", QStringLiteral("检查"), Glyph::Check}});
    auto *creation = page(QStringLiteral("创作"));
    group(creation, QStringLiteral("辅助创作"),
          {{"showAiWorkspace", QStringLiteral("AI创作"), Glyph::Branch},
           {"manageDrafts", QStringLiteral("文字稿"), Glyph::Note},
           {"exportSheet", QStringLiteral("导出"), Glyph::Save}});
    connect(m_centralStack, &QStackedWidget::currentChanged, this,
            [this] { updateEditorChrome(); });

    auto *sheetBar = findChild<QToolBar *>("sheetToolbar");
    removeToolBar(sheetBar);
    addToolBar(Qt::BottomToolBarArea, sheetBar);
    sheetBar->show();
    sheetBar->setFloatable(false);
    sheetBar->setIconSize(QSize(18, 18));
    sheetBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *create = findChild<QAction *>("createSheet");
    create->setIcon(chromeIcon(Glyph::Add));
    create->setIconText(QStringLiteral("工作表"));
    m_sheetSelector->setMinimumContentsLength(12);
    m_sheetSelector->setMaximumWidth(300);
    auto *mode = new QLabel(sheetBar);
    mode->setObjectName("workspaceMode");
    mode->setMargin(8);
    sheetBar->addSeparator();
    sheetBar->addWidget(mode);
    auto *zoom = new QWidget(statusBar());
    zoom->setObjectName("canvasZoomControls");
    auto *zoomLayout = new QHBoxLayout(zoom);
    zoomLayout->setContentsMargins(4, 0, 4, 0);
    zoomLayout->setSpacing(6);
    zoomLayout->addWidget(actionButton(findChild<QAction *>("fitTopic"), QStringLiteral("适应"),
                                       Glyph::Fit, zoom, true));
    auto *slider = new QSlider(Qt::Horizontal, zoom);
    slider->setObjectName("canvasZoomSlider");
    slider->setAccessibleName(QStringLiteral("画布缩放"));
    slider->setFixedWidth(108);
    slider->setMinimumHeight(28);
    zoomLayout->addWidget(slider);
    auto *scale = new QLabel(zoom);
    scale->setObjectName("canvasZoomValue");
    scale->setMinimumWidth(66);
    zoomLayout->addWidget(scale);
    auto *exitFocus = new QPushButton(QStringLiteral("退出专注"), statusBar());
    exitFocus->setObjectName("exitBranchFocus");
    exitFocus->setToolTip(QStringLiteral("退出分支专注（Esc）并返回原来的视口"));
    exitFocus->hide();
    statusBar()->addPermanentWidget(exitFocus);
    connect(exitFocus, &QPushButton::clicked, this, &MainWindow::exitBranchFocus);
    statusBar()->addPermanentWidget(zoom);
    connect(slider, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (m_centralStack->currentIndex() == 1)
                {
                    if (auto *gantt = qobject_cast<GanttView *>(m_ganttPage))
                        gantt->setPixelsPerDay(value);
                    updateEditorChrome();
                }
                else
                    setCanvasZoom(value / 100.0);
            });
    resizeDocks({outlineDock, propertyDock}, {220, 284}, Qt::Horizontal);
    for (auto *dock : findChildren<QDockWidget *>())
        installDockTitle(dock);
    // 按实际字体与分组内容留足高度，包含窄窗口横向滚动条，避免固定高度截断。
    int pageHeight = 0;
    for (int index = 0; index < tabs->count(); ++index)
        if (auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(index)))
        {
            scroll->widget()->ensurePolished();
            pageHeight = qMax(pageHeight, scroll->widget()->sizeHint().height());
        }
    tabs->setMinimumHeight(pageHeight + tabs->tabBar()->sizeHint().height() + 20);
    resizeEditorChrome();
}

void MainWindow::updateEditorChrome()
{
    auto *tabs = findChild<QTabWidget *>("ribbonTabs");
    if (!tabs || m_chromeUpdating)
        return;
    const bool focused = m_branchFocus && !m_homeVisible && m_centralStack->currentIndex() == 0;
    if (auto *exitFocus = findChild<QPushButton *>("exitBranchFocus"))
        exitFocus->setVisible(focused);
    if (auto *action = findChild<QAction *>("exitBranchFocusAction"))
        action->setEnabled(focused);
    if (m_homeVisible)
    {
        for (const auto &entry : m_homeActionStates)
            if (entry.first)
                entry.first->setEnabled(false);
        updateInlineProperties();
        return;
    }
    const QScopedValueRollback<bool> updating(m_chromeUpdating, true);
    const bool gantt = m_centralStack->currentIndex() == 1;
    const TreeSheet *tree = activeTreeSheet();
    if (auto *action = findChild<QAction *>("focusBranch"))
        action->setEnabled(!gantt && tree && tree->nodes.contains(m_selectedNodeId));
    if (auto *action = findChild<QAction *>("managePresentation"))
        action->setEnabled(tree != nullptr && !gantt);
    const FlowSheet *flow = activeFlowSheet();
    const QString mode = gantt                              ? QStringLiteral("甘特图")
                         : flow                             ? QStringLiteral("流程图")
                         : tree && tree->kind == "fishbone" ? QStringLiteral("鱼骨图")
                                                            : QStringLiteral("思维导图");
    const bool modeChanged = tabs->property("mode").toString() != mode;
    tabs->setProperty("mode", mode);
    tabs->setTabVisible(3, flow && !gantt);
    tabs->setTabVisible(4, gantt);
    if (modeChanged)
    {
        tabs->setCurrentIndex(gantt ? 4 : flow ? 3 : 0);
        if (auto *property = findChild<QTabWidget *>("propertyTabs"))
            property->setCurrentIndex(gantt ? 3 : flow ? 2 : 0);
    }
    const auto text = [this](const char *name, const QString &value)
    {
        if (auto *label = findChild<QLabel *>(name))
            label->setText(value);
    };
    text("workspaceMode", mode);
    for (const char *name : {"fitTopic", "locateTopic", "zoomIn", "zoomOut"})
        if (auto *action = findChild<QAction *>(name))
            action->setEnabled(!gantt);
    const BaseNode *node = nullptr;
    if (tree && tree->nodes.contains(m_selectedNodeId))
        node = &tree->nodes.constFind(m_selectedNodeId).value();
    else if (flow && flow->nodes.contains(m_selectedNodeId))
        node = &flow->nodes.constFind(m_selectedNodeId).value();
    const Task *task = nullptr;
    for (const auto &candidate : m_doc->project().tasks)
        if (candidate.id == m_selectedTaskId)
        {
            task = &candidate;
            break;
        }
    if (!task && !gantt && node)
        for (const auto &candidate : m_doc->project().tasks)
            if (candidate.source && candidate.source->sheetId == m_doc->project().activeSheetId &&
                candidate.source->nodeId == node->id)
            {
                task = &candidate;
                break;
            }
    const auto preview = [](const QString &value)
    { return value.size() > 140 ? value.left(140) + QStringLiteral("…") : value; };
    text("propertySelection", gantt  ? task ? preview(task->title) : QStringLiteral("选择一个任务")
                               : node ? preview(node->title)
                                     : QStringLiteral("选择一个主题"));
    if (node && !gantt)
    {
        const NodeStyle resolved = NodeItem::resolvedStyle(
            tree ? tree->defaultNodeStyle : flow->defaultNodeStyle, node->style);
        text("nodeStyleSummary",
             QStringLiteral("%1 · %2px%3\n%4 / %5")
                 .arg(resolved.fontFamily == "system" ? QStringLiteral("系统字体")
                                                      : resolved.fontFamily)
                 .arg(resolved.fontSize)
                 .arg(resolved.bold ? QStringLiteral(" · 加粗") : QString())
                 .arg(resolved.fill, resolved.color));
        text("nodeContentSummary",
             preview(node->note.isEmpty() ? QStringLiteral("尚无备注") : node->note) +
                 QStringLiteral("\n") + preview(node->link.value_or(QStringLiteral("尚无链接"))));
        text("nodeTagsSummary", node->tags.isEmpty()
                                    ? QStringLiteral("尚无标签")
                                    : preview(node->tags.join(QStringLiteral(" · "))));
    }
    else
    {
        text("nodeStyleSummary", QStringLiteral("选中主题后编辑形状、字体与颜色"));
        text("nodeContentSummary", QStringLiteral("选中主题后查看图标、备注与链接"));
        text("nodeTagsSummary", QStringLiteral("选中主题后查看标签与批注"));
    }
    text("sheetStyleSummary", tree ? QStringLiteral("局部样式优先，未覆盖字段使用本表默认值")
                                   : QStringLiteral("当前工作表类型由专用工具编辑"));
    text("nodeTaskSummary",
         task ? QStringLiteral("%1\n%2 · %3%")
                    .arg(preview(task->title), task->startDate && task->endDate
                                                   ? task->startDate->toString("yyyy-MM-dd") +
                                                         QStringLiteral(" — ") +
                                                         task->endDate->toString("yyyy-MM-dd")
                                                   : QStringLiteral("未排期"))
                    .arg(task->progress)
              : QStringLiteral("将主题设为任务，或在任务表中选择已有任务"));
    if (auto *edit = findChild<QAction *>("editTaskProperties"))
        edit->setEnabled(task != nullptr);
    updateInlineProperties();
    auto *slider = findChild<QSlider *>("canvasZoomSlider");
    if (slider)
    {
        const QSignalBlocker blocker(slider);
        if (auto *view = gantt ? qobject_cast<GanttView *>(m_ganttPage) : nullptr)
        {
            slider->setRange(8, 120);
            slider->setValue(view->pixelsPerDay());
            text("canvasZoomValue", QStringLiteral("%1px/天").arg(view->pixelsPerDay()));
        }
        else
        {
            slider->setRange(25, 200);
            slider->setValue(qRound(m_view->transform().m11() * 100));
            text("canvasZoomValue", QStringLiteral("%1%").arg(slider->value()));
        }
        slider->setEnabled(!gantt || qobject_cast<GanttView *>(m_ganttPage));
    }
}

void MainWindow::resizeEditorChrome()
{
    if (m_homeVisible)
        return;
    auto *dock = findChild<QDockWidget *>("propertyDock");
    if (!dock)
        return;
    const bool compact = width() < 1400;
    if (compact != m_compactChrome)
    {
        m_compactChrome = compact;
        if (compact)
        {
            m_restorePropertyDock = !dock->isHidden();
            dock->hide();
        }
        else if (m_restorePropertyDock)
        {
            m_restorePropertyDock = false;
            dock->show();
        }
    }
    if (auto *zoom = findChild<QWidget *>("canvasZoomControls"))
        zoom->setVisible(width() >= 1000);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    resizeEditorChrome();
}
} // namespace lingya
