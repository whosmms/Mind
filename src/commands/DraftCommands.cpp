#include "commands/DraftCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include <algorithm>
namespace lingya
{
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
        if (p.first.trimmed().isEmpty() || p.second.trimmed().isEmpty() ||
            p.first.toUcs4().size() > Limits::kTitleMaxChars ||
            p.second.toUcs4().size() > Limits::kNoteMaxChars)
            return AppError{"INVALID_ARGUMENT", QStringLiteral("段落标题和正文不能为空或超限")};
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
