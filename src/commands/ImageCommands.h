#pragma once
#include "domain/Domain.h"
namespace lingya
{
class CommandService;
class ProjectDocument;
class ImageCommands
{
  public:
    ImageCommands(CommandService *commands, const ProjectDocument *doc)
        : m_commands(commands), m_doc(doc)
    {
    }
    std::variant<bool, AppError> setNodeImage(const ID &sheetId, const ID &nodeId,
                                              const QString &png);
    std::variant<ID, AppError> add(const ID &sheetId, const QString &png, const QPointF &position);
    std::variant<bool, AppError> move(const ID &sheetId, const ID &imageId, const QPointF &position,
                                      const std::optional<QSizeF> &size = std::nullopt);
    std::variant<bool, AppError> remove(const ID &sheetId, const ID &imageId);

  private:
    CommandService *m_commands;
    const ProjectDocument *m_doc;
};
} // namespace lingya
