#pragma once
#include "domain/Domain.h"
#include <variant>
namespace lingya
{
class CommandService;
class ProjectDocument;
// 路线与讲解备注的唯一持久化入口；播放不调用本命令。
class PresentationCommands
{
  public:
    PresentationCommands(CommandService *commands, const ProjectDocument *document);
    std::variant<bool, AppError> saveRoute(const PresentationRoute &route);
    std::variant<bool, AppError> removeRoute(const ID &routeId);

  private:
    CommandService *m_commands;
    const ProjectDocument *m_document;
};
} // namespace lingya
