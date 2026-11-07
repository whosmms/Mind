#include "ui/HomePage.h"
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

namespace lingya
{
namespace
{
QIcon preview(const QString &kind)
{
    QPixmap image(240, 112);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#247A62"), 2));
    p.setBrush(QColor("#DDF2E7"));
    const auto node = [&p](qreal x, qreal y, qreal w = 38)
    { p.drawRoundedRect(QRectF(x, y, w, 24), 7, 7); };
    const auto line = [&p](qreal x, qreal y, qreal a, qreal b)
    { p.drawLine(QPointF(x, y), QPointF(a, b)); };
    if (kind == "fishbone")
    {
        line(26, 56, 206, 56);
        for (int x : {64, 112, 160})
        {
            line(x, 56, x - 22, 24);
            line(x, 56, x - 22, 88);
        }
        node(190, 44, 42);
    }
    else if (kind == "flowchart")
    {
        node(8, 44);
        node(94, 44);
        node(180, 44);
        line(46, 56, 94, 56);
        line(132, 56, 180, 56);
        line(87, 51, 94, 56);
        line(87, 61, 94, 56);
        line(173, 51, 180, 56);
        line(173, 61, 180, 56);
    }
    else if (kind == "gantt")
    {
        p.setPen(QPen(QColor("#DDE6DF"), 1));
        for (int x = 30; x < 230; x += 36)
            line(x, 12, x, 102);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#247A62"));
        p.drawRoundedRect(QRectF(30, 18, 82, 18), 5, 5);
        p.setBrush(QColor("#91CEB3"));
        p.drawRoundedRect(QRectF(78, 46, 90, 18), 5, 5);
        p.setBrush(QColor("#DDF2E7"));
        p.drawRoundedRect(QRectF(142, 74, 60, 18), 5, 5);
    }
    else
    {
        for (const QPointF a :
             {QPointF(60, 24), QPointF(60, 86), QPointF(190, 24), QPointF(190, 86)})
        {
            line(120, 56, a.x(), a.y());
            node(a.x() - 26, a.y() - 12, 52);
        }
        p.setBrush(QColor("#247A62"));
        node(92, 43, 56);
    }
    return QIcon(image);
}
} // namespace

HomePage::HomePage(QWidget *parent) : QWidget(parent)
{
    setObjectName("homePage");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *nav = new QWidget(this);
    nav->setObjectName("homeNavigation");
    nav->setFixedWidth(164);
    auto *navLayout = new QVBoxLayout(nav);
    navLayout->setContentsMargins(20, 26, 20, 20);
    auto *brand = new QLabel(QStringLiteral("灵芽思维"), nav);
    brand->setObjectName("homeBrand");
    navLayout->addWidget(brand);
    navLayout->addSpacing(28);
    auto *active = new QLabel(QStringLiteral("  首页"), nav);
    active->setObjectName("homeActiveNavigation");
    navLayout->addWidget(active);
    auto *recentNav = new QPushButton(QStringLiteral("最近工程"), nav);
    navLayout->addWidget(recentNav);
    auto *continueNav = new QPushButton(QStringLiteral("返回编辑器"), nav);
    continueNav->setObjectName("homeContinue");
    navLayout->addWidget(continueNav);
    connect(continueNav, &QPushButton::clicked, this, &HomePage::continueRequested);
    navLayout->addStretch();
    navLayout->addWidget(new QLabel(QStringLiteral("本地工程 · .lingya"), nav));
    root->addWidget(nav);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *main = new QWidget(scroll);
    main->setObjectName("homeWorkspace");
    auto *content = new QVBoxLayout(main);
    content->setContentsMargins(36, 30, 36, 30);
    content->setSpacing(22);
    content->setAlignment(Qt::AlignTop);
    auto *hero = new QWidget(main);
    hero->setObjectName("homeHero");
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(24, 24, 24, 24);
    auto *title = new QLabel(QStringLiteral("欢迎使用 灵芽思维"), hero);
    title->setObjectName("homeTitle");
    title->setWordWrap(true);
    heroLayout->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("让思考更清晰，让想法自然生长。"), hero);
    subtitle->setWordWrap(true);
    heroLayout->addWidget(subtitle);
    auto *actions = new QHBoxLayout;
    auto *create = new QPushButton(QStringLiteral("＋ 新建导图"), hero);
    create->setObjectName("homeNewMindmap");
    create->setProperty("primary", true);
    connect(create, &QPushButton::clicked, this, [this] { emit templateRequested("mindmap"); });
    auto *open = new QPushButton(QStringLiteral("打开工程"), hero);
    open->setObjectName("homeOpenProject");
    connect(open, &QPushButton::clicked, this, &HomePage::openRequested);
    actions->addWidget(create);
    actions->addWidget(open);
    actions->addStretch();
    heroLayout->addLayout(actions);
    content->addWidget(hero);
    auto *heading = new QLabel(QStringLiteral("从模板开始"), main);
    heading->setProperty("sectionTitle", true);
    content->addWidget(heading);
    auto *cards = new QWidget(main);
    m_cardLayout = new QGridLayout(cards);
    m_cardLayout->setContentsMargins(0, 0, 0, 0);
    m_cardLayout->setSpacing(14);
    const QStringList kinds{"mindmap", "fishbone", "flowchart", "gantt"};
    const QStringList names{QStringLiteral("思维导图"), QStringLiteral("鱼骨图"),
                            QStringLiteral("流程图"), QStringLiteral("甘特图")};
    for (int i = 0; i < kinds.size(); ++i)
    {
        auto *button = new QPushButton(names.at(i), cards);
        button->setObjectName(QStringLiteral("homeTemplate_") + kinds.at(i));
        button->setProperty("templateCard", true);
        button->setIcon(preview(kinds.at(i)));
        button->setIconSize(QSize(100, 48));
        button->setFixedHeight(108);
        button->setMinimumWidth(0);
        connect(button, &QPushButton::clicked, this,
                [this, kind = kinds.at(i)] { emit templateRequested(kind); });
        m_cards.append(button);
    }
    arrangeCards();
    content->addWidget(cards);
    m_current = new QLabel(main);
    m_current->setObjectName("homeCurrentProject");
    m_current->setTextFormat(Qt::PlainText);
    m_current->setWordWrap(true);
    content->addWidget(m_current);
    auto *recentHeading = new QLabel(QStringLiteral("最近工程"), main);
    recentHeading->setProperty("sectionTitle", true);
    content->addWidget(recentHeading);
    m_empty =
        new QLabel(QStringLiteral("还没有最近工程。选择上方模板，或打开本地 .lingya 文件。"), main);
    m_empty->setObjectName("homeRecentEmpty");
    m_empty->setWordWrap(true);
    content->addWidget(m_empty);
    m_recents = new QListWidget(main);
    m_recents->setObjectName("homeRecentFiles");
    m_recents->setMinimumHeight(150);
    m_recents->setTextElideMode(Qt::ElideMiddle);
    content->addWidget(m_recents, 1);
    auto *recentActions = new QHBoxLayout;
    auto *openRecent = new QPushButton(QStringLiteral("打开所选"), main);
    openRecent->setObjectName("homeOpenRecent");
    auto *remove = new QPushButton(QStringLiteral("移除记录"), main);
    remove->setObjectName("homeRemoveRecent");
    remove->setToolTip(QStringLiteral("只移除最近列表中的记录，工程文件保留。"));
    recentActions->addWidget(openRecent);
    recentActions->addWidget(remove);
    recentActions->addStretch();
    content->addLayout(recentActions);
    content->addStretch(1);
    const auto selection = [this, openRecent, remove]
    {
        const bool selected = m_recents->currentItem();
        openRecent->setEnabled(selected);
        remove->setEnabled(selected);
    };
    connect(m_recents, &QListWidget::currentRowChanged, this, selection);
    connect(openRecent, &QPushButton::clicked, this,
            [this]
            {
                if (auto *item = m_recents->currentItem())
                    emit recentRequested(item->data(Qt::UserRole).toString());
            });
    connect(m_recents, &QListWidget::itemActivated, this, [this](QListWidgetItem *item)
            { emit recentRequested(item->data(Qt::UserRole).toString()); });
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                if (auto *item = m_recents->currentItem())
                    emit removeRecentRequested(item->data(Qt::UserRole).toString());
            });
    connect(recentNav, &QPushButton::clicked, this,
            [this, scroll]
            {
                scroll->ensureWidgetVisible(m_recents);
                m_recents->setFocus();
            });
    selection();
    scroll->setWidget(main);
    root->addWidget(scroll, 1);
}

void HomePage::setRecentFiles(const QVector<QString> &paths)
{
    m_recents->clear();
    for (const QString &path : paths)
    {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(
            info.completeBaseName() + QStringLiteral("\n") +
                (info.exists() ? info.absolutePath() : QStringLiteral("文件已移动或删除")),
            m_recents);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setSizeHint(QSize(0, 60));
    }
    m_empty->setVisible(paths.isEmpty());
    m_recents->setVisible(!paths.isEmpty());
}

void HomePage::setCurrentTitle(const QString &title)
{
    m_current->setText(QStringLiteral("当前工程：%1 · 可随时返回继续编辑").arg(title));
}

void HomePage::arrangeCards()
{
    const int columns = width() >= 1200 ? 4 : 2;
    if (columns == m_columns)
        return;
    m_columns = columns;
    for (int i = 0; i < m_cards.size(); ++i)
    {
        m_cardLayout->removeWidget(m_cards.at(i));
        m_cardLayout->addWidget(m_cards.at(i), i / columns, i % columns);
    }
}

void HomePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    arrangeCards();
}
} // namespace lingya
