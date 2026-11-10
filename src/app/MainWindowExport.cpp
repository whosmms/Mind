#include "app/MainWindow.h"
#include "charts/GanttView.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOptionGraphicsItem>
#include <QVBoxLayout>

namespace lingya
{
void MainWindow::buildCompletionActions()
{
    auto *menu = menuBar()->addMenu(QStringLiteral("创作与输出"));
    menu->addAction(QStringLiteral("结构与模板…"), this, [this] { showTemplates(!m_homeVisible); })
        ->setObjectName("showTemplateGallery");
    menu->addAction(QStringLiteral("插入画布图片…"), this,
                    [this]
                    {
                        auto *view = m_centralStack->currentIndex() == 1
                                         ? qobject_cast<QGraphicsView *>(m_ganttPage)
                                         : m_view;
                        if (view)
                            insertCanvasImage(view->mapToScene(view->viewport()->rect().center()));
                    })
        ->setObjectName("insertCanvasImage");
    menu->addAction(QStringLiteral("插入节点图片…"), this,
                    [this]
                    {
                        if (m_nodeItems.contains(m_selectedNodeId))
                            chooseNodeImage(m_selectedNodeId);
                    })
        ->setObjectName("insertNodeImageAction");
    auto *search =
        menu->addAction(QStringLiteral("搜索主题与任务…"), this, &MainWindow::showSearch);
    search->setObjectName("showSearch");
    search->setShortcut(QKeySequence::Find);
    menu->addAction(QStringLiteral("设置与帮助…"), this, &MainWindow::showPreferences)
        ->setObjectName("showPreferences");
    menu->addSeparator();
    menu->addAction(QStringLiteral("AI创作与密钥设置…"), this, &MainWindow::showAiWorkspace)
        ->setObjectName("showAiWorkspace");
    menu->addAction(QStringLiteral("工程文字稿…"), this, &MainWindow::manageDrafts)
        ->setObjectName("manageDrafts");
    auto *presentation =
        menu->addAction(QStringLiteral("分支讲解…"), this, &MainWindow::managePresentation);
    presentation->setObjectName("managePresentation");
    presentation->setShortcut(QKeySequence(Qt::Key_F5));
    auto *exportAction =
        menu->addAction(QStringLiteral("导出工作表…"), this, &MainWindow::exportCurrentSheet);
    exportAction->setObjectName("exportSheet");
}

ExportService::Snapshot MainWindow::captureCanvas() const
{
    ExportService::Snapshot result;
    auto *view =
        m_centralStack->currentIndex() == 1 ? qobject_cast<QGraphicsView *>(m_ganttPage) : m_view;
    if (!view || !view->scene())
        return result;
    QPainter painter(&result.picture);
    painter.setRenderHint(QPainter::Antialiasing);
    // 共用图元绘制，记录独立QPicture，不切换选择、不触发文档/场景信号。
    for (auto *item : view->scene()->items(Qt::AscendingOrder))
    {
        bool handle = false;
        for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem())
            handle |= ancestor->data(0x21C3).isValid() || ancestor->data(0x21C4).isValid() ||
                      ancestor->data(0x21C5).isValid() || ancestor->data(0x21C7).isValid();
        if (handle)
            continue;
        if (!item->isVisible() || item->data(0x21C0).isValid() ||
            item->data(GanttView::kPreviewDataRole).isValid() || item->effectiveOpacity() == 0)
            continue;
        result.bounds = result.bounds.united(item->sceneBoundingRect());
        painter.save();
        painter.setWorldTransform(item->sceneTransform(), true);
        painter.setOpacity(item->effectiveOpacity());
        if (auto *node = dynamic_cast<NodeItem *>(item))
            node->paintSnapshot(&painter);
        else
        {
            QStyleOptionGraphicsItem option;
            option.exposedRect = item->boundingRect();
            option.state = QStyle::State_Enabled;
            // 流程边的选择加宽是宿主显示状态，用领域原始画笔画副本。
            auto *line = dynamic_cast<QGraphicsPathItem *>(item);
            ID edgeId = item->data(0x21C1).toString();
            if (edgeId.isEmpty() && item->parentItem())
                edgeId = item->parentItem()->data(0x21C1).toString();
            bool copied = false;
            if (line && !edgeId.isEmpty())
                if (const auto *sheet = activeFlowSheet())
                    for (const auto &edge : sheet->edges)
                        if (edge.id == edgeId)
                        {
                            QGraphicsPathItem clean(line->path());
                            clean.setPen(QPen(
                                QColor(edge.style.color.isEmpty() ? "#247A62" : edge.style.color),
                                edge.style.width,
                                edge.style.dashed ? Qt::DashLine : Qt::SolidLine));
                            clean.setBrush(line->brush());
                            clean.paint(&painter, &option, nullptr);
                            copied = true;
                            break;
                        }
            if (!copied)
                item->paint(&painter, &option, nullptr);
        }
        painter.restore();
    }
    painter.end();
    result.bounds.adjust(-24, -24, 24, 24);
    return result;
}

void MainWindow::exportCurrentSheet()
{
    if (m_homeVisible)
        return;
    QDialog dialog(this);
    dialog.setObjectName("exportDialog");
    dialog.setWindowTitle(QStringLiteral("导出工作表"));
    auto *column = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *format = new QComboBox(&dialog);
    format->setObjectName("exportFormat");
    format->addItems({QStringLiteral("PNG 图片"), QStringLiteral("PDF 分页文档"),
                      QStringLiteral("Markdown 大纲"), QStringLiteral("TXT 大纲")});
    auto *scale = new QDoubleSpinBox(&dialog);
    scale->setObjectName("exportScale");
    scale->setRange(5, 400);
    scale->setValue(100);
    scale->setSuffix(" %");
    form->addRow(QStringLiteral("格式"), format);
    form->addRow(QStringLiteral("图像比例"), scale);
    column->addLayout(form);
    auto *info = new QLabel(&dialog);
    info->setObjectName("exportInfo");
    info->setTextFormat(Qt::PlainText);
    info->setWordWrap(true);
    column->addWidget(info);
    const auto snapshot = captureCanvas();
    const auto update = [format, scale, info, &snapshot]
    {
        scale->setEnabled(format->currentIndex() < 2);
        if (format->currentIndex() == 0)
        {
            const auto size = ExportService::pngSize(snapshot.bounds, scale->value() / 100);
            if (const auto *error = std::get_if<AppError>(&size))
                info->setText(error->message);
            else
                info->setText(
                    QStringLiteral(
                        "%1 × %2 像素；当前可见内容，不含选择框和编辑控件。折叠内容请先展开。")
                        .arg(std::get<QSize>(size).width())
                        .arg(std::get<QSize>(size).height()));
        }
        else if (format->currentIndex() == 1)
        {
            const auto count = ExportService::pdfPageCount(snapshot.bounds, scale->value() / 100);
            info->setText(
                std::holds_alternative<AppError>(count)
                    ? std::get<AppError>(count).message
                    : QStringLiteral(
                          "A4横向，四边12毫米，按当前比例分页，共%1页；从左到右、从上到下。")
                          .arg(std::get<int>(count)));
        }
        else
            info->setText(QStringLiteral(
                "UTF-8文字，包含当前工作表完整内容，折叠主题也会导出；甘特按工作表筛选。"));
    };
    connect(format, &QComboBox::currentIndexChanged, &dialog, update);
    connect(scale, &QDoubleSpinBox::valueChanged, &dialog, update);
    update();
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("导出…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [&, this]
            {
                const QString suffix =
                    QStringList{"png", "pdf", "md", "txt"}.at(format->currentIndex());
                const QString path = QFileDialog::getSaveFileName(
                    &dialog, QStringLiteral("导出工作表"), QStringLiteral("工作表.") + suffix,
                    QStringLiteral("导出文件 (*.%1)").arg(suffix));
                if (path.isEmpty())
                    return;
                const QFileInfo target(path), source(m_filePath);
                if (!m_filePath.isEmpty() &&
                    (target.absoluteFilePath().compare(source.absoluteFilePath(),
                                                       Qt::CaseInsensitive) == 0 ||
                     (!target.canonicalFilePath().isEmpty() &&
                      target.canonicalFilePath().compare(source.canonicalFilePath(),
                                                         Qt::CaseInsensitive) == 0)))
                {
                    info->setText(QStringLiteral("不能覆盖当前工程，请选择其他导出路径。"));
                    return;
                }
                std::optional<AppError> error;
                if (format->currentIndex() == 0)
                {
                    const auto r = ExportService::writePng(snapshot, path, scale->value() / 100);
                    if (const auto *e = std::get_if<AppError>(&r))
                        error = *e;
                }
                else if (format->currentIndex() == 1)
                {
                    const auto r = ExportService::writePdf(snapshot, path, scale->value() / 100);
                    if (const auto *e = std::get_if<AppError>(&r))
                        error = *e;
                }
                else
                {
                    const auto text =
                        ExportService::outline(m_doc->project(), m_doc->project().activeSheetId);
                    if (const auto *e = std::get_if<AppError>(&text))
                        error = *e;
                    else
                    {
                        const auto r = ExportService::writeText(std::get<QString>(text), path);
                        if (const auto *e = std::get_if<AppError>(&r))
                            error = *e;
                    }
                }
                if (error)
                {
                    info->setText(error->message);
                    return;
                }
                statusBar()->showMessage(QStringLiteral("已导出：") + path, 8000);
                dialog.accept();
            });
    dialog.resize(500, 260);
    dialog.exec();
}
} // namespace lingya
