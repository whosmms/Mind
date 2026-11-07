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
    std::variant<bool, AppError> remove(const ID &draftId);

  private:
    CommandService *m_commands;
    const ProjectDocument *m_document;
};
} // namespace lingya
