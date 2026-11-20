#pragma once

#include "features/SearchService.h"
#include <variant>

namespace lingya
{
class CommandService;
class ProjectDocument;

// 预览与提交共用一份变更清单，整批通过唯一的内容事务写回。
class SearchReplaceCommands
{
  public:
    struct Plan
    {
        int topics() const
        {
            return m_nodes.size();
        }
        int tasks() const
        {
            return m_tasks.size();
        }
        const QStringList &examples() const
        {
            return m_examples;
        }

      private:
        friend class SearchReplaceCommands;
        struct Text
        {
            std::optional<QString> title, note;
        };
        const ProjectDocument *m_document = nullptr;
        qint64 m_revision = -1;
        QMap<ID, Text> m_nodes;
        QMap<ID, QString> m_tasks;
        QStringList m_examples;
    };

    SearchReplaceCommands(CommandService *commands, const ProjectDocument *document);
    std::variant<Plan, AppError> preview(const QString &query, const QString &replacement,
                                         const SearchService::Match *selected = nullptr) const;
    std::variant<bool, AppError> apply(const Plan &plan) const;

  private:
    CommandService *m_commands;
    const ProjectDocument *m_document;
};
} // namespace lingya
