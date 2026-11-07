#include "commands/PresentationCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include <QSet>
#include <algorithm>
namespace lingya
{
PresentationCommands::PresentationCommands(CommandService *commands,
                                           const ProjectDocument *document)
    : m_commands(commands), m_document(document)
{
}
std::variant<bool, AppError> PresentationCommands::saveRoute(const PresentationRoute &route)
{
    const TreeSheet *tree = nullptr;
    for (const auto &sheet : m_document->project().sheets)
        if (const auto *candidate = std::get_if<TreeSheet>(&sheet);
            candidate && candidate->id == route.sheetId)
            tree = candidate;
    if (!tree)
        return AppError{"NOT_FOUND", QStringLiteral("讲解只支持仍存在的导图或鱼骨图")};
    if (route.id.isEmpty() || route.title.trimmed().isEmpty() ||
        route.title.toUcs4().size() > Limits::kTitleMaxChars ||
        route.steps.size() > Limits::kMaxNodes)
        return AppError{"INVALID_ARGUMENT", QStringLiteral("路线名称不能为空，步骤最多1000个")};
    QSet<ID> ids;
    for (const auto &step : route.steps)
    {
        if (step.id.isEmpty() || ids.contains(step.id) ||
            step.speakerNote.toUcs4().size() > Limits::kNoteMaxChars)
            return AppError{"INVALID_ARGUMENT", QStringLiteral("步骤标识重复或讲解备注超限")};
        ids.insert(step.id);
    }
    // 已删除节点的步骤保留，由播放明确提示跳过；与既有ROUTE校验和检查助手一致。
    for (const auto &existing : m_document->project().routes)
    {
        if (existing.id != route.id || existing.sheetId != route.sheetId ||
            existing.title != route.title || existing.steps.size() != route.steps.size())
            continue;
        bool same = true;
        for (int i = 0; i < route.steps.size(); ++i)
            same &= existing.steps[i].id == route.steps[i].id &&
                    existing.steps[i].nodeId == route.steps[i].nodeId &&
                    existing.steps[i].speakerNote == route.steps[i].speakerNote;
        if (same)
            return true;
    }
    return m_commands->applyEdit(QStringLiteral("保存讲解路线"),
                                 [route](Project &project)
                                 {
                                     for (auto &existing : project.routes)
                                         if (existing.id == route.id)
                                         {
                                             existing = route;
                                             return;
                                         }
                                     project.routes.append(route);
                                 });
}
std::variant<bool, AppError> PresentationCommands::removeRoute(const ID &routeId)
{
    const auto &routes = m_document->project().routes;
    if (std::none_of(routes.cbegin(), routes.cend(),
                     [&routeId](const PresentationRoute &r) { return r.id == routeId; }))
        return AppError{"NOT_FOUND", QStringLiteral("讲解路线已不存在")};
    return m_commands->applyEdit(
        QStringLiteral("删除讲解路线"),
        [routeId](Project &project)
        {
            project.routes.erase(std::remove_if(project.routes.begin(), project.routes.end(),
                                                [&routeId](const PresentationRoute &r)
                                                { return r.id == routeId; }),
                                 project.routes.end());
        });
}
} // namespace lingya
