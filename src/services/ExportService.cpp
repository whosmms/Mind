#include "services/ExportService.h"
#include "domain/ProjectValidator.h"
#include <QFileInfo>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <functional>
namespace lingya::ExportService
{
namespace
{
AppError failed()
{
    return {"EXPORT_FAILED", QStringLiteral("导出写入失败，请检查路径和可用空间；原工程保持不变")};
}
bool valid(const QRectF &r, qreal scale)
{
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) &&
           std::isfinite(r.height()) && r.width() > 0 && r.height() > 0 && std::isfinite(scale) &&
           scale >= 0.05 && scale <= 4;
}
QPageLayout layout()
{
    return QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF(12, 12, 12, 12));
}
bool extension(const QString &path, const QString &suffix)
{
    return QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) == 0;
}
void paint(QPainter &painter, const Snapshot &s, qreal scale, const QPointF &offset)
{
    painter.save();
    painter.translate(offset);
    painter.scale(scale, scale);
    painter.translate(-s.bounds.topLeft());
    painter.drawPicture(QPointF(0, 0), s.picture);
    painter.restore();
}
} // namespace
std::variant<QSize, AppError> pngSize(const QRectF &r, qreal scale)
{
    if (!valid(r, scale))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("导出尺寸或缩放无效")};
    const double w = std::ceil(r.width() * scale), h = std::ceil(r.height() * scale);
    if (w > kMaxPngPixels || h > kMaxPngPixels || w * h > kMaxPngPixels)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("PNG超过3200万像素，请降低缩放后重试")};
    return QSize(int(w), int(h));
}
std::variant<int, AppError> pdfPageCount(const QRectF &r, qreal scale)
{
    if (!valid(r, scale))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("导出尺寸或缩放无效")};
    const auto page = layout().paintRectPixels(96).size();
    const double count =
        std::ceil(r.width() * scale / page.width()) * std::ceil(r.height() * scale / page.height());
    if (count > kMaxPdfPages)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("PDF超过200页，请降低缩放或拆分工作表")};
    return int(count);
}
std::variant<bool, AppError> writePng(const Snapshot &s, const QString &path, qreal scale)
{
    const auto measured = pngSize(s.bounds, scale);
    if (const auto *error = std::get_if<AppError>(&measured))
        return *error;
    if (!extension(path, "png"))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("PNG路径必须以.png结尾")};
    QImage image(std::get<QSize>(measured), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return failed();
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    paint(painter, s, scale, {});
    painter.end();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit())
        return failed();
    return true;
}
std::variant<int, AppError> writePdf(const Snapshot &s, const QString &path, qreal scale)
{
    const auto count = pdfPageCount(s.bounds, scale);
    if (const auto *error = std::get_if<AppError>(&count))
        return *error;
    if (!extension(path, "pdf"))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("PDF路径必须以.pdf结尾")};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return failed();
    {
        QPdfWriter writer(&file);
        writer.setResolution(96);
        writer.setPageLayout(layout());
        writer.setTitle(QStringLiteral("灵芽思维工作表"));
        QPainter painter(&writer);
        if (!painter.isActive())
            return failed();
        const auto page = writer.pageLayout().paintRectPixels(96).size();
        const int cols = int(std::ceil(s.bounds.width() * scale / page.width()));
        for (int i = 0; i < std::get<int>(count); ++i)
        {
            if (i && !writer.newPage())
                return failed();
            painter.save();
            painter.setClipRect(QRect(QPoint(0, 0), page));
            paint(painter, s, scale,
                  QPointF(-(i % cols) * page.width(), -(i / cols) * page.height()));
            painter.restore();
        }
        if (!painter.end())
            return failed();
    }
    if (!file.commit())
        return failed();
    return std::get<int>(count);
}
std::variant<QString, AppError> outline(const Project &project, const ID &sheetId)
{
    const auto errors = ProjectValidator::validate(project);
    if (!errors.isEmpty())
        return errors.first();
    QString result;
    for (const auto &sheet : project.sheets)
    {
        if (std::visit([](const auto &s) { return s.id; }, sheet) != sheetId)
            continue;
        result =
            QStringLiteral("# ") + std::visit([](const auto &s) { return s.name; }, sheet) + "\n\n";
        if (const auto *tree = std::get_if<TreeSheet>(&sheet))
        {
            std::function<void(const ID &, int)> visit = [&](const ID &id, int depth)
            {
                const auto node = tree->nodes.value(id);
                QString title = node.title;
                if(node.provenance && node.provenance->origin=="supplement")title.prepend(QStringLiteral("[AI补充] "));
                title.replace('\n', " ");
                result += QString(depth * 2, ' ') + "- " + title + "\n";
                if (!node.note.isEmpty())
                    result += QString((depth + 1) * 2, ' ') + node.note + "\n";
                QVector<TreeNode> children;
                for (const auto &child : tree->nodes)
                    if (child.parentId && *child.parentId == id)
                        children.append(child);
                std::sort(children.begin(), children.end(), [](const TreeNode &a, const TreeNode &b)
                          { return a.order == b.order ? a.id < b.id : a.order < b.order; });
                for (const auto &child : children)
                    visit(child.id, depth + 1);
            };
            visit(tree->rootId, 0);
        }
        else if (const auto *flow = std::get_if<FlowSheet>(&sheet))
        {
            for (const auto &node : flow->nodes)
                result += "- " + node.title + "\n";
            result += QStringLiteral("\n## 连线\n");
            for (const auto &edge : flow->edges)
                result += "- " + flow->nodes.value(edge.sourceId).title + " → " +
                          flow->nodes.value(edge.targetId).title + " " + edge.label + "\n";
        }
        else if (const auto *gantt = std::get_if<GanttSheet>(&sheet))
        {
            for (const auto &task : project.tasks)
            {
                if (task.source ? (gantt->filter.sourceSheetId &&
                                   task.source->sheetId != *gantt->filter.sourceSheetId)
                                : !gantt->filter.includeStandalone)
                    continue;
                result += "- " + task.title + " " +
                          (task.startDate ? task.startDate->toString(Qt::ISODate)
                                          : QStringLiteral("未排期")) +
                          " – " + (task.endDate ? task.endDate->toString(Qt::ISODate) : QString()) +
                          "\n";
            }
        }
        return result;
    }
    return AppError{"NOT_FOUND", QStringLiteral("工作表已不存在")};
}
std::variant<bool, AppError> writeText(const QString &text, const QString &path)
{
    if (!extension(path, "md") && !extension(path, "txt"))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("文字导出路径必须为.md或.txt")};
    const auto bytes = text.toUtf8();
    if (bytes.size() > 16 * 1024 * 1024)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("文字导出超过16MiB")};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return failed();
    return true;
}
} // namespace lingya::ExportService
