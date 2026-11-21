#include "commands/DraftCommands.h"
#include "ai/AiMaterial.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "services/ProjectJson.h"
#include <algorithm>
namespace lingya
{
namespace
{
std::optional<AppError> validateParagraph(const QPair<QString, QString> &paragraph)
{
    if (paragraph.first.trimmed().isEmpty() || paragraph.second.trimmed().isEmpty() ||
        paragraph.first.toUcs4().size() > Limits::kTitleMaxChars ||
        paragraph.second.toUcs4().size() > Limits::kNoteMaxChars)
        return AppError{"INVALID_ARGUMENT", QStringLiteral("段落标题和正文不能为空或超限")};
    return std::nullopt;
}
} // namespace

DraftCommands::DraftCommands(CommandService *commands, const ProjectDocument *document)
    : m_commands(commands), m_document(document)
{
}
std::variant<bool, AppError>
DraftCommands::updateText(const ID &id, const QString &title,
                          const QVector<QPair<QString, QString>> &paragraphs)
{
    const auto &drafts = m_document->project().drafts;
    auto it = std::find_if(drafts.cbegin(), drafts.cend(),
                           [&](const TextDraft &d) { return d.id == id; });
    if (it == drafts.cend())
        return AppError{"NOT_FOUND", QStringLiteral("文字稿已不存在")};
    if (title.trimmed().isEmpty() || title.toUcs4().size() > Limits::kTitleMaxChars ||
        paragraphs.size() != it->sections.size() || paragraphs.isEmpty())
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("文稿标题不能为空，段落数量须与原稿一致")};
    TextDraft next = *it;
    next.title = title;
    bool changed = next.title != it->title;
    for (int i = 0; i < paragraphs.size(); ++i)
    {
        const auto &p = paragraphs[i];
        if (const auto error = validateParagraph(p))
            return *error;
        changed |= next.sections[i].heading != p.first || next.sections[i].text != p.second;
        next.sections[i].heading = p.first;
        next.sections[i].text = p.second;
    }
    if (!changed)
        return true;
    return m_commands->applyEdit(QStringLiteral("编辑文字稿"),
                                 [next](Project &project)
                                 {
                                     for (auto &draft : project.drafts)
                                         if (draft.id == next.id)
                                         {
                                             draft = next;
                                             break;
                                         }
                                 });
}

std::variant<bool, AppError>
DraftCommands::reconcileSources(const ID &id, qsizetype sectionIndex, qint64 expectedRevision,
                                const std::optional<QPair<QString, QString>> &replacement)
{
    if (m_document->sessionRevision() != expectedRevision)
        return AppError{"STALE_SOURCE", QStringLiteral("工程在对照期间已变化，请重新核对来源")};
    const auto &project = m_document->project();
    const auto &drafts = project.drafts;
    const auto it = std::find_if(drafts.cbegin(), drafts.cend(),
                                 [&id](const TextDraft &draft) { return draft.id == id; });
    if (it == drafts.cend() || sectionIndex < 0 || sectionIndex >= it->sections.size())
        return AppError{"NOT_FOUND", QStringLiteral("文字稿或段落已不存在")};
    const auto &section = it->sections.at(sectionIndex);
    if (section.sourceNodeIds.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("本段没有关联的来源主题")};

    DraftSection next = section;
    const auto *tree = AiMaterial::sourceTree(project, it->sourceSheetId);
    next.sourceSnapshot = tree ? AiMaterial::draftSourceSnapshot(*tree, section.sourceNodeIds)
                               : DraftSourceSnapshot{};
    if (replacement)
    {
        if (const auto error = validateParagraph(*replacement))
            return *error;
        next.heading = replacement->first;
        next.text = replacement->second;
    }
    if (next.sourceSnapshot == section.sourceSnapshot && next.heading == section.heading &&
        next.text == section.text)
        return true;

    const qsizetype draftIndex = it - drafts.cbegin();
    Project candidate = project;
    candidate.drafts[draftIndex].sections[sectionIndex] = next;
    if (ProjectJson::toBytes(candidate).size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("更新后的来源快照超出工程文件大小上限")};
    return m_commands->applyEdit(replacement ? QStringLiteral("更新文字稿段落")
                                             : QStringLiteral("确认文字稿段落来源"),
                                 [draftIndex, sectionIndex, next](Project &target)
                                 { target.drafts[draftIndex].sections[sectionIndex] = next; });
}

std::variant<bool, AppError> DraftCommands::remove(const ID &id)
{
    const auto &drafts = m_document->project().drafts;
    if (std::none_of(drafts.cbegin(), drafts.cend(),
                     [&](const TextDraft &d) { return d.id == id; }))
        return AppError{"NOT_FOUND", QStringLiteral("文字稿已不存在")};
    return m_commands->applyEdit(
        QStringLiteral("删除文字稿"),
        [id](Project &project)
        {
            project.drafts.erase(std::remove_if(project.drafts.begin(), project.drafts.end(),
                                                [&](const TextDraft &d) { return d.id == id; }),
                                 project.drafts.end());
        });
}
} // namespace lingya
