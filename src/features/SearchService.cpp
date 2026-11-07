#include "features/SearchService.h"
#include <algorithm>
namespace lingya::SearchService
{
Results find(const Project &project, const QString &input, int limit)
{
    Results result;
    const QString query = input.trimmed();
    if (query.isEmpty() || limit <= 0)
        return result;
    const auto add = [&](const ID &sheetId, const BaseNode &node, const QString &context)
    {
        const bool titleMatch = node.title.contains(query, Qt::CaseInsensitive);
        const int noteIndex = node.note.indexOf(query, 0, Qt::CaseInsensitive);
        if (!titleMatch && noteIndex < 0)
            return;
        ++result.total;
        if (result.matches.size() >= limit)
            return;
        const QString excerpt =
            noteIndex < 0 ? QString()
                          : node.note.mid(qMax(0, noteIndex - 20), 140).replace('\n', " ");
        result.matches.append({sheetId, node.id, "node", node.title, context, excerpt});
    };
    for (const auto &sheet : project.sheets)
    {
        if (const auto *tree = std::get_if<TreeSheet>(&sheet))
            for (const auto &node : tree->nodes)
                add(tree->id, node, tree->name);
        else if (const auto *flow = std::get_if<FlowSheet>(&sheet))
            for (const auto &node : flow->nodes)
                add(flow->id, node, flow->name);
    }
    for (const auto &task : project.tasks)
        if (task.title.contains(query, Qt::CaseInsensitive))
        {
            ++result.total;
            if (result.matches.size() < limit)
                result.matches.append(
                    {{}, task.id, "task", task.title, QStringLiteral("任务"), {}});
        }
    return result;
}
} // namespace lingya::SearchService
