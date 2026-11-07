#include "ai/AiMaterial.h"
#include "domain/Limits.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <functional>
namespace lingya::AiMaterial
{
QString treeHash(const TreeSheet &sheet)
{
    GenerateDraftRequest request;
    for (const auto &node : sheet.nodes)
        request.source.append({node.id, node.parentId, node.order, node.title, node.note});
    for (const auto &relation : sheet.relations)
        request.relations.append({relation.sourceId, relation.targetId, relation.label});
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(draftJson(request)).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}
QJsonObject draftJson(const GenerateDraftRequest &request)
{
    QJsonArray nodes, relations;
    for (const auto &node : request.source)
        nodes.append(QJsonObject{
            {"id", node.id},
            {"parentId", node.parentId ? QJsonValue(*node.parentId) : QJsonValue(QJsonValue::Null)},
            {"order", node.order},
            {"title", node.title},
            {"note", node.note}});
    for (const auto &relation : request.relations)
        relations.append(QJsonObject{{"sourceId", relation.sourceId},
                                     {"targetId", relation.targetId},
                                     {"label", relation.label}});
    return {{"nodes", nodes}, {"relations", relations}};
}
QJsonObject mapJson(const QString &text)
{
    QJsonArray paragraphs;
    int index = 1;
    for (const auto &line : text.split('\n'))
        if (!line.trimmed().isEmpty())
            paragraphs.append(
                QJsonObject{{"id", QStringLiteral("p%1").arg(index++)}, {"text", line}});
    return {{"paragraphs", paragraphs}};
}
std::optional<AppError> validateMapInput(const GenerateMapRequest &request)
{
    if (request.text.trimmed().isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("请先输入材料")};
    if (request.text.toUcs4().size() > Limits::kAiTextMaxChars ||
        request.requirements.toUcs4().size() > Limits::kAiRequirementMaxChars)
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("材料最多12000字，附加要求最多1000字；请主动删减后重试")};
    if (request.maxNodes < 1 || request.maxNodes > Limits::kAiMapMaxNodes || request.maxDepth < 1 ||
        request.maxDepth > Limits::kAiMapMaxDepth ||
        (request.mode != "faithful" && request.mode != "supplement"))
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("节点上限为1–50，层级上限为1–4，请检查模式")};
    return {};
}
std::optional<AppError> validateDraftInput(const GenerateDraftRequest &request)
{
    if (request.source.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("请选择导图或鱼骨图的有效来源范围")};
    const QString text =
        QString::fromUtf8(QJsonDocument(draftJson(request)).toJson(QJsonDocument::Compact));
    if (request.source.size() > Limits::kAiDraftMaxNodes ||
        text.toUcs4().size() > Limits::kAiDraftMaterialMaxChars ||
        request.requirements.toUcs4().size() > Limits::kAiRequirementMaxChars)
        return AppError{
            "LIMIT_EXCEEDED",
            QStringLiteral("来源最多100个主题、序列化材料20000字；请选择较小分支，不会自动截断")};
    QSet<ID> ids;
    for (const auto &node : request.source)
    {
        if (node.id.isEmpty() || ids.contains(node.id))
            return AppError{"INVALID_ARGUMENT", QStringLiteral("来源主题标识无效或重复")};
        ids.insert(node.id);
    }
    if (request.targetChars < 100 || request.targetChars > 8000 ||
        (request.mode != "faithful" && request.mode != "supplement") ||
        (request.purpose != "summary" && request.purpose != "explanation" &&
         request.purpose != "speech"))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("请检查用途、篇幅与生成模式")};
    for (const auto &node : request.source)
        if (node.parentId && !ids.contains(*node.parentId))
            return AppError{"INVALID_ARGUMENT", QStringLiteral("来源范围缺少父主题")};
    for (const auto &relation : request.relations)
        if (!ids.contains(relation.sourceId) || !ids.contains(relation.targetId))
            return AppError{"INVALID_ARGUMENT", QStringLiteral("来源关系的端点不在选区内")};
    return {};
}
std::variant<GenerateDraftRequest, AppError> fromTree(const Project &project, const ID &sheetId,
                                                      const ID &rootId)
{
    const TreeSheet *tree = nullptr;
    for (const auto &sheet : project.sheets)
        if (const auto *t = std::get_if<TreeSheet>(&sheet); t && t->id == sheetId)
            tree = t;
    if (!tree)
        return AppError{"INVALID_ARGUMENT", QStringLiteral("文稿来源只支持导图或鱼骨图")};
    const ID root = rootId.isEmpty() ? tree->rootId : rootId;
    if (!tree->nodes.contains(root))
        return AppError{"STALE_SOURCE", QStringLiteral("来源主题已不存在，请重新选择")};
    GenerateDraftRequest request;
    request.sheetId = sheetId;
    QSet<ID> selected;
    std::function<void(const ID &)> visit = [&](const ID &id)
    {
        if (selected.contains(id))
            return;
        selected.insert(id);
        const auto node = tree->nodes.value(id);
        request.source.append({node.id, id == root ? std::nullopt : node.parentId, node.order,
                               node.title, node.note});
        QVector<TreeNode> children;
        for (const auto &child : tree->nodes)
            if (child.parentId && *child.parentId == id)
                children.append(child);
        std::sort(children.begin(), children.end(), [](const TreeNode &a, const TreeNode &b)
                  { return a.order == b.order ? a.id < b.id : a.order < b.order; });
        for (const auto &child : children)
            visit(child.id);
    };
    visit(root);
    for (const auto &relation : tree->relations)
        if (selected.contains(relation.sourceId) && selected.contains(relation.targetId))
            request.relations.append({relation.sourceId, relation.targetId, relation.label});
    if (const auto error = validateDraftInput(request))
        return *error;
    return request;
}
} // namespace lingya::AiMaterial
