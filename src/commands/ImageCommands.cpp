#include "commands/ImageCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "services/ImageService.h"
#include "services/ProjectJson.h"
#include <QUuid>
namespace lingya
{
namespace
{
SheetBase *find(Project &p, const ID &id)
{
    for (auto &sheet : p.sheets)
    {
        auto *base = std::visit([](auto &s) -> SheetBase * { return &s; }, sheet);
        if (base->id == id)
            return base;
    }
    return nullptr;
}
BaseNode *node(Project &p, const ID &sheetId, const ID &id)
{
    for (auto &sheet : p.sheets)
        if (std::visit([](auto &s) { return s.id; }, sheet) == sheetId)
        {
            if (auto *tree = std::get_if<TreeSheet>(&sheet); tree && tree->nodes.contains(id))
                return &tree->nodes[id];
            if (auto *flow = std::get_if<FlowSheet>(&sheet); flow && flow->nodes.contains(id))
                return &flow->nodes[id];
        }
    return nullptr;
}
AppError missing()
{
    return {"NOT_FOUND", QStringLiteral("图片、节点或工作表不存在")};
}
} // namespace
std::variant<bool, AppError> ImageCommands::setNodeImage(const ID &sheetId, const ID &id,
                                                         const QString &png)
{
    auto p = m_doc->snapshot();
    auto *target = node(p, sheetId, id);
    if (!target)
        return missing();
    if (target->imagePng == png)
        return true;
    if (!png.isEmpty() && ImageService::decode(png).isNull())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("图片数据无效")};
    target->imagePng = png;
    if (ProjectJson::toBytes(p).size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("工程超过10MiB，请移除部分图片或缩小原图")};
    return m_commands->applyEdit(QStringLiteral("设置节点图片"),
                                 [sheetId, id, png](Project &value)
                                 {
                                     auto *n = node(value, sheetId, id);
                                     n->imagePng = png;
                                     ++n->contentRevision;
                                 });
}
std::variant<ID, AppError> ImageCommands::add(const ID &sheetId, const QString &png,
                                              const QPointF &position)
{
    const auto image = ImageService::decode(png);
    if (image.isNull())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("图片数据无效")};
    auto p = m_doc->snapshot();
    auto *sheet = find(p, sheetId);
    if (!sheet)
        return missing();
    CanvasImage item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.position = position;
    item.size = image.size().scaled(320, 220, Qt::KeepAspectRatio);
    item.imagePng = png;
    sheet->images.append(item);
    if (ProjectJson::toBytes(p).size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("工程超过10MiB，请移除部分图片或缩小原图")};
    auto result =
        m_commands->applyEdit(QStringLiteral("插入画布图片"), [sheetId, item](Project &value)
                              { find(value, sheetId)->images.append(item); });
    if (const auto *error = std::get_if<AppError>(&result))
        return *error;
    return item.id;
}
std::variant<bool, AppError> ImageCommands::move(const ID &sheetId, const ID &id,
                                                 const QPointF &position,
                                                 const std::optional<QSizeF> &size)
{
    auto p = m_doc->snapshot();
    auto *sheet = find(p, sheetId);
    if (!sheet)
        return missing();
    for (const auto &img : sheet->images)
        if (img.id == id)
        {
            if (img.position == position && (!size || img.size == *size))
                return true;
            return m_commands->applyEdit(QStringLiteral("调整图片"),
                                         [sheetId, id, position, size](Project &value)
                                         {
                                             for (auto &image : find(value, sheetId)->images)
                                                 if (image.id == id)
                                                 {
                                                     image.position = position;
                                                     if (size)
                                                         image.size = *size;
                                                 }
                                         });
        }
    return missing();
}
std::variant<bool, AppError> ImageCommands::remove(const ID &sheetId, const ID &id)
{
    auto p = m_doc->snapshot();
    auto *sheet = find(p, sheetId);
    if (!sheet)
        return missing();
    bool found = false;
    for (const auto &img : sheet->images)
        found |= img.id == id;
    if (!found)
        return missing();
    return m_commands->applyEdit(QStringLiteral("移除画布图片"),
                                 [sheetId, id](Project &value)
                                 {
                                     auto &images = find(value, sheetId)->images;
                                     for (int i = 0; i < images.size(); ++i)
                                         if (images[i].id == id)
                                         {
                                             images.removeAt(i);
                                             break;
                                         }
                                 });
}
} // namespace lingya
