#include "ui/HomePage.h"
#include <QDateTime>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
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
    auto *preferences = new QPushButton(QStringLiteral("设置与帮助"), nav);
    preferences->setObjectName("homePreferences");
    navLayout->addWidget(preferences);
    connect(preferences, &QPushButton::clicked, this, &HomePage::preferencesRequested);
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
    auto *actions = new QHBoxLayout;
    auto *open = new QPushButton(QStringLiteral("打开工程"), hero);
    open->setObjectName("homeOpenProject");
    open->setProperty("primary", true);
    connect(open, &QPushButton::clicked, this, &HomePage::openRequested);
    actions->addWidget(open);
    actions->addStretch();
    heroLayout->addLayout(actions);
    content->addWidget(hero);
    auto *heading = new QLabel(QStringLiteral("从模板开始"), main);
    heading->setProperty("sectionTitle", true);
    content->addWidget(heading);
    auto *gallery = new QPushButton(QStringLiteral("浏览24种结构与场景模板…"), main);
    gallery->setObjectName("homeTemplateLibrary");
    content->addWidget(gallery, 0, Qt::AlignLeft);
    connect(gallery, &QPushButton::clicked, this, &HomePage::templateGalleryRequested);
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
    m_recents->setTextElideMode(Qt::ElideRight);
    m_recents->setIconSize(QSize(118, 55));
    m_recents->setContextMenuPolicy(Qt::CustomContextMenu);
    content->addWidget(m_recents, 1);
    content->addStretch(1);
    connect(m_recents, &QListWidget::itemActivated, this, [this](QListWidgetItem *item)
            { emit recentRequested(item->data(Qt::UserRole).toString()); });
    connect(m_recents, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &point)
            {
                auto *item = m_recents->itemAt(point);
                if (!item)
                    return;
                const auto path = item->data(Qt::UserRole).toString();
                const bool pinned = item->data(Qt::UserRole + 1).toBool();
                QMenu menu(m_recents);
                menu.addAction(QStringLiteral("打开工程"),
                               [this, path] { emit recentRequested(path); });
                menu.addAction(pinned ? QStringLiteral("取消置顶") : QStringLiteral("置顶"),
                               [this, path, pinned] { emit recentPinRequested(path, !pinned); })
                    ->setObjectName("pinRecentFile");
                menu.addAction(QStringLiteral("移除记录"),
                               [this, path] { emit removeRecentRequested(path); })
                    ->setObjectName("removeRecentFile");
                menu.exec(m_recents->viewport()->mapToGlobal(point));
            });
    connect(recentNav, &QPushButton::clicked, this,
            [this, scroll]
            {
                scroll->ensureWidgetVisible(m_recents);
                m_recents->setFocus();
            });
    scroll->setWidget(main);
    root->addWidget(scroll, 1);
}

void HomePage::setRecentFiles(const QVector<QString> &paths, const QStringList &pinned,
                              const std::function<QIcon(const QString &)> &thumbnail)
{
    const auto generation = ++m_recentGeneration;
    m_recents->clear();
    const QIcon placeholder = preview("mindmap");
    for (const QString &path : paths)
    {
        const QFileInfo info(path);
        const bool isPinned = pinned.contains(path, Qt::CaseInsensitive);
        auto *item = new QListWidgetItem(
            placeholder,
            (isPinned ? QStringLiteral("置顶 · ") : QString()) + info.completeBaseName() +
                QStringLiteral("\n") +
                (info.isFile() ? QStringLiteral("修改于 %1")
                                     .arg(info.lastModified().toString("yyyy-MM-dd HH:mm"))
                               : QStringLiteral("文件已移动或删除")),
            m_recents);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, isPinned);
        item->setToolTip(path);
        item->setSizeHint(QSize(0, 70));
        // 大工程保留通用缩略图。小工程逐项加载，首页打开后再刷新预览。
        if (info.isFile() && info.size() <= 512 * 1024)
        {
            QTimer::singleShot(m_recents->count() * 12, this,
                               [this, generation, path, thumbnail]
                               {
                                   if (generation != m_recentGeneration || !isVisible())
                                       return;
                                   const QIcon icon = thumbnail(path);
                                   if (icon.isNull())
                                       return;
                                   for (int i = 0; i < m_recents->count(); ++i)
                                   {
                                       auto *row = m_recents->item(i);
                                       if (row->data(Qt::UserRole).toString() == path)
                                           row->setIcon(icon);
                                   }
                               });
        }
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
