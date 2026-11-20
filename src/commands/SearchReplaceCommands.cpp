#include "commands/SearchReplaceCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/TitleRules.h"
#include "services/ProjectJson.h"
#include <type_traits>

namespace lingya
{
namespace
{
// 先计算最坏长度，再分配替换结果；避免短关键字与长替换词放大输入。
bool replaceField(const QString &before, const QString &query, const QString &replacement,
                  bool title, std::optional<QString> &after)
{
    const auto first = before.indexOf(query, 0, Qt::CaseInsensitive);
    if (first < 0)
        return true;
    const int limit = title ? Limits::kTitleMaxChars : Limits::kNoteMaxChars;
    qint64 size = before.size();
    const qint64 growth = replacement.size() - query.size();
    if (growth > 0)
        for (auto pos = first; pos >= 0;
             pos = before.indexOf(query, pos + query.size(), Qt::CaseInsensitive))
        {
            size += growth;
            if (size > 2 * Limits::kNoteMaxChars)
                return false;
        }
    QString text = before;
    text.replace(query, replacement, Qt::CaseInsensitive);
    if (title)
        text = TitleRules::normalize(text);
    if (TitleRules::codePointCount(text) > limit)
        return false;
    if (text != before)
        after = std::move(text);
    return true;
}

AppError tooLong(const QString &field, int limit)
{
    return {"LIMIT_EXCEEDED",
            QStringLiteral("替换后%1超过%2个字符，请缩短替换文字。").arg(field).arg(limit)};
}

QString excerpt(const QString &text, qsizetype start)
{
    QString sample = text.mid(start, 64).simplified();
    if (start > 0)
        sample.prepend(QStringLiteral("…"));
    if (text.size() > start + 64)
        sample += QStringLiteral("…");
    return sample.isEmpty() ? QStringLiteral("（空白）") : sample;
}
} // namespace

SearchReplaceCommands::SearchReplaceCommands(CommandService *commands,
                                             const ProjectDocument *document)
    : m_commands(commands), m_document(document)
{
}

std::variant<SearchReplaceCommands::Plan, AppError>
SearchReplaceCommands::preview(const QString &input, const QString &replacement,
                               const SearchService::Match *selected) const
{
    const auto query = input.trimmed();
    if (query.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("请先输入要查找的文字。")};
    if (replacement.size() > 2 * Limits::kNoteMaxChars)
        return tooLong(QStringLiteral("文字"), Limits::kNoteMaxChars);

    Plan plan;
    plan.m_document = m_document;
    plan.m_revision = m_document->sessionRevision();
    const auto &project = m_document->project();
    QHash<ID, const TreeNode *> treeNodes;
    qint64 previewSize = 0;
    const auto record = [&](const QString &label, const QString &before, const QString &after)
    {
        previewSize += after.size();
        if (plan.m_examples.size() < 10)
        {
            qsizetype firstDifference = 0;
            while (firstDifference < qMin(before.size(), after.size()) &&
                   before[firstDifference] == after[firstDifference])
                ++firstDifference;
            const auto start = qMax<qsizetype>(0, firstDifference - 18);
            plan.m_examples.append(QStringLiteral("%1　%2 → %3")
                                       .arg(label, excerpt(before, start), excerpt(after, start)));
        }
        return previewSize <= Limits::kMaxFileBytes;
    };
    const AppError tooLarge{"LIMIT_EXCEEDED", QStringLiteral("替换后的工程超过 10 MiB。")};

    for (const auto &sheet : project.sheets)
    {
        const auto scan = [&](const auto &data) -> std::optional<AppError>
        {
            for (const auto &node : data.nodes)
            {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, TreeNode>)
                    treeNodes.insert(node.id, &node);
                if (selected && (selected->kind != "node" || selected->targetId != node.id ||
                                 selected->sheetId != data.id))
                    continue;
                Plan::Text change;
                if (!replaceField(node.title, query, replacement, true, change.title))
                    return tooLong(QStringLiteral("主题标题"), Limits::kTitleMaxChars);
                if (!replaceField(node.note, query, replacement, false, change.note))
                    return tooLong(QStringLiteral("主题备注"), Limits::kNoteMaxChars);
                if (change.title && !record(QStringLiteral("主题标题"), node.title, *change.title))
                    return tooLarge;
                if (change.note && !record(QStringLiteral("主题备注"), node.note, *change.note))
                    return tooLarge;
                if (change.title || change.note)
                    plan.m_nodes.insert(node.id, std::move(change));
            }
            return std::nullopt;
        };
        std::optional<AppError> error;
        if (const auto *tree = std::get_if<TreeSheet>(&sheet))
            error = scan(*tree);
        else if (const auto *flow = std::get_if<FlowSheet>(&sheet))
            error = scan(*flow);
        if (error)
            return *error;
    }

    // 先处理任务中的命中；关联主题已经改名时沿用其标题，避免重复替换同一处。
    for (const auto &task : project.tasks)
    {
        if (selected && (selected->kind != "task" || selected->targetId != task.id))
            continue;
        const auto *source = task.source ? treeNodes.value(task.source->nodeId) : nullptr;
        if (source && plan.m_nodes.value(source->id).title)
            continue;
        std::optional<QString> title;
        if (!replaceField(task.title, query, replacement, true, title))
            return tooLong(QStringLiteral("任务名称"), Limits::kTitleMaxChars);
        if (!title)
            continue;
        if (source && source->title != *title)
        {
            if (!record(QStringLiteral("关联主题"), source->title, *title))
                return tooLarge;
            plan.m_nodes[source->id].title = std::move(title);
        }
        else
        {
            if (!record(QStringLiteral("任务名称"), task.title, *title))
                return tooLarge;
            plan.m_tasks.insert(task.id, std::move(*title));
        }
    }
    // 照顾旧工程里同一主题关联多个任务的情况。
    for (const auto &task : project.tasks)
        if (task.source)
        {
            const auto change = plan.m_nodes.constFind(task.source->nodeId);
            if (change != plan.m_nodes.cend() && change->title && task.title != *change->title)
            {
                if (!record(QStringLiteral("关联任务"), task.title, *change->title))
                    return tooLarge;
                plan.m_tasks.insert(task.id, *change->title);
            }
        }
    return plan;
}

std::variant<bool, AppError> SearchReplaceCommands::apply(const Plan &plan) const
{
    if (plan.m_document != m_document || plan.m_revision != m_document->sessionRevision())
        return AppError{"STALE_SOURCE", QStringLiteral("工程内容已变化，请重新查看替换预览。")};
    if (plan.m_nodes.isEmpty() && plan.m_tasks.isEmpty())
        return true;

    // 使用提交时的工作表与视图位置；切表不会使内容预览过期。
    auto candidate = m_document->snapshot();
    const auto update = [&](auto &sheet)
    {
        for (auto change = plan.m_nodes.cbegin(); change != plan.m_nodes.cend(); ++change)
        {
            auto node = sheet.nodes.find(change.key());
            if (node == sheet.nodes.end())
                continue;
            if (change->title)
                node->title = *change->title;
            if (change->note)
                node->note = *change->note;
            ++node->contentRevision;
        }
    };
    for (auto &sheet : candidate.sheets)
        if (auto *tree = std::get_if<TreeSheet>(&sheet))
            update(*tree);
        else if (auto *flow = std::get_if<FlowSheet>(&sheet))
            update(*flow);
    for (auto &task : candidate.tasks)
        if (const auto title = plan.m_tasks.constFind(task.id); title != plan.m_tasks.cend())
            task.title = title.value();
    if (ProjectJson::toBytes(candidate).size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("替换后的工程超过 10 MiB。")};
    return m_commands->applyEdit(QStringLiteral("替换工程文字"),
                                 [&candidate](Project &project)
                                 {
                                     project.sheets = std::move(candidate.sheets);
                                     project.tasks = std::move(candidate.tasks);
                                 });
}
} // namespace lingya
