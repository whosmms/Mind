#pragma once
#include "domain/Domain.h"
#include <QPair>
#include <variant>
namespace lingya
{
class CommandService;
class ProjectDocument;
class DraftCommands
{
  public:
    DraftCommands(CommandService *commands, const ProjectDocument *document);
    // 仅改标题及各段文字；来源、用途、原始修订和生成时间不由编辑器重写。
    std::variant<bool, AppError> updateText(const ID &draftId, const QString &title,
                                            const QVector<QPair<QString, QString>> &paragraphs);
    // 只确认所选段落的来源，可选地同时应用用户在对照窗口明确提交的标题和正文。
    std::variant<bool, AppError>
    reconcileSources(const ID &draftId, qsizetype sectionIndex, qint64 expectedRevision,
                     const std::optional<QPair<QString, QString>> &replacement);
    std::variant<bool, AppError> remove(const ID &draftId);

  private:
    CommandService *m_commands;
    const ProjectDocument *m_document;
};
} // namespace lingya
