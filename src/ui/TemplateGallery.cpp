#include "ui/TemplateGallery.h"
#include "layout/FishboneLayout.h"
#include "layout/TreeLayout.h"
#include "services/TemplateLibrary.h"
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
namespace lingya
{
QIcon TemplateGallery::preview(const QString &id)
{
    QPixmap pixmap(240, 112);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto result = TemplateLibrary::create(id);
    if (!std::holds_alternative<Project>(result))
        return QIcon(pixmap);
    const auto p = std::get<Project>(result);
    QMap<ID, QPointF> points;
    QVector<QPair<ID, ID>> edges;
    ID root;
    if (const auto *tree = std::get_if<TreeSheet>(&p.sheets.first()))
    {
        root = tree->rootId;
        if (tree->kind == "fishbone")
        {
            QVector<FishboneLayout::NodeSize> sizes;
            for (const auto &n : tree->nodes)
                sizes.append({n.id, 90, 34});
            const auto layout = FishboneLayout::layout(*tree, sizes);
            if (const auto *result = std::get_if<FishboneLayout::Result>(&layout))
                for (const auto &n : result->positions)
                    points[n.id] = {n.x, n.y};
        }
        else
        {
            QVector<TreeLayout::NodeSize> sizes;
            for (const auto &n : tree->nodes)
                sizes.append({n.id, 90, 34});
            const auto layout = TreeLayout::layout(*tree, sizes);
            if (const auto *positions = std::get_if<QVector<TreeLayout::NodePosition>>(&layout))
                for (const auto &n : *positions)
                    points[n.id] = {n.x, n.y};
        }
        for (const auto &n : tree->nodes)
            if (n.parentId)
                edges.append({*n.parentId, n.id});
    }
    else if (const auto *flow = std::get_if<FlowSheet>(&p.sheets.first()))
    {
        for (const auto &n : flow->nodes)
            points[n.id] = n.position;
        for (const auto &e : flow->edges)
            edges.append({e.sourceId, e.targetId});
    }
    else
    {
        painter.setPen(QPen(QColor("#DDE6DF"), 1));
        for (int x = 20; x < 230; x += 35)
            painter.drawLine(x, 8, x, 102);
        for (int i = 0; i < p.tasks.size(); ++i)
        {
            painter.setBrush(QColor(i % 2 ? "#DDF2E7" : "#247A62"));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRectF(15 + i * 34, 12 + i * 18, 70, 12), 3, 3);
        }
        return QIcon(pixmap);
    }
    QRectF bounds;
    for (const auto &point : points)
        bounds = bounds.united(QRectF(point - QPointF(45, 17), QSizeF(90, 34)));
    const auto scale = qMin(220.0 / qMax(1.0, bounds.width()), 92.0 / qMax(1.0, bounds.height()));
    painter.translate(120, 56);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());
    painter.setPen(QPen(QColor("#91B7A8"), qMax(2.0, 1.0 / scale)));
    for (const auto &e : edges)
        painter.drawLine(points[e.first], points[e.second]);
    for (auto it = points.cbegin(); it != points.cend(); ++it)
    {
        painter.setBrush(QColor(it.key() == root ? "#247A62" : "#DDF2E7"));
        painter.setPen(QPen(QColor("#247A62"), 1.0 / scale));
        painter.drawRoundedRect(QRectF(it.value() - QPointF(40, 14), QSizeF(80, 28)), 6, 6);
    }
    return QIcon(pixmap);
}
TemplateGallery::TemplateGallery(bool allowStructure, QWidget *parent) : QDialog(parent)
{
    setObjectName("templateGallery");
    setWindowTitle(QStringLiteral("结构与模板"));
    resize(760, 650);
    auto *layout = new QVBoxLayout(this);
    if (allowStructure)
    {
        auto *title = new QLabel(
            QStringLiteral("当前导图结构：保持内容，重新排布；手动位置将清除，可后退撤回"), this);
        title->setWordWrap(true);
        layout->addWidget(title);
        auto *row = new QHBoxLayout;
        const QStringList codes{"bilateral", "right", "left", "down", "up"},
            names{"双向", "向右", "向左", "向下", "向上"};
        for (int i = 0; i < codes.size(); ++i)
        {
            auto *button = new QPushButton(names[i], this);
            button->setObjectName("applyStructure_" + codes[i]);
            row->addWidget(button);
            connect(button, &QPushButton::clicked, this,
                    [this, code = codes[i]]
                    {
                        layoutCode = code;
                        accept();
                    });
        }
        layout->addLayout(row);
    }
    auto *caption =
        new QLabel(allowStructure ? QStringLiteral("或选择完整模板，插入新工作表，保留原有内容")
                                  : QStringLiteral("选择可编辑模板创建工程。所有模板均可使用"),
                   this);
    caption->setWordWrap(true);
    layout->addWidget(caption);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("templateKinds");
    layout->addWidget(tabs, 1);
    const QStringList kinds{"mindmap", "fishbone", "flowchart", "gantt"},
        names{"思维导图", "鱼骨图", "流程图", "甘特图"};
    for (int type = 0; type < kinds.size(); ++type)
    {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        auto *page = new QWidget;
        auto *grid = new QGridLayout(page);
        grid->setAlignment(Qt::AlignTop);
        int at = 0;
        for (const auto &info : TemplateLibrary::entries())
            if (info.kind == kinds[type])
            {
                auto *button = new QToolButton(page);
                button->setObjectName("template_" + info.id);
                button->setProperty("templateCard", true);
                button->setText(info.title);
                button->setToolTip(info.description);
                button->setIcon(preview(info.id));
                button->setIconSize(QSize(180, 84));
                button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                button->setMinimumHeight(124);
                button->setMinimumWidth(200);
                button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                grid->addWidget(button, at / 2, at % 2);
                ++at;
                connect(button, &QToolButton::clicked, this,
                        [this, id = info.id]
                        {
                            templateId = id;
                            accept();
                        });
            }
        scroll->setWidget(page);
        tabs->addTab(scroll, names[type]);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
} // namespace lingya
