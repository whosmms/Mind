#include "commands/TaskCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"

#include <QUuid>

namespace lingya {

namespace {

const Task* findTask(const Project& project, const ID& taskId) {
    for (const Task& task : project.tasks) {
        if (task.id == taskId) {
            return &task;
        }
    }
    return nullptr;
}

}  // namespace

TaskCommands::TaskCommands(CommandService* commands, const ProjectDocument* document)
    : m_commands(commands), m_document(document) {
}

std::variant<ID, AppError> TaskCommands::addTask(const QString& title) {
    const ID newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto result = m_commands->applyEdit(QStringLiteral("新增任务"),
                                              [newId, title](Project& project) {
                                                  Task task;
                                                  task.id = newId;
                                                  task.title = title;
                                                  project.tasks.push_back(task);
                                              });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return newId;
}

std::variant<bool, AppError> TaskCommands::updateTask(const Task& updated) {
    if (findTask(m_document->project(), updated.id) == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("任务不存在：%1").arg(updated.id)};
    }
    return m_commands->applyEdit(QStringLiteral("修改任务"), [updated](Project& project) {
        for (Task& task : project.tasks) {
            if (task.id == updated.id) {
                task = updated;
                return;
            }
        }
    });
}

std::variant<bool, AppError> TaskCommands::removeTask(const ID& taskId) {
    if (findTask(m_document->project(), taskId) == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("任务不存在：%1").arg(taskId)};
    }
    return m_commands->applyEdit(QStringLiteral("删除任务"), [taskId](Project& project) {
        // 先清除其他任务对它的前置依赖，避免校验报 TASK_PREDECESSOR
        for (Task& task : project.tasks) {
            task.predecessorIds.removeAll(taskId);
        }
        for (int i = 0; i < project.tasks.size(); ++i) {
            if (project.tasks[i].id == taskId) {
                project.tasks.removeAt(i);
                return;
            }
        }
    });
}

std::variant<ID, AppError> TaskCommands::linkTask(const ID& sheetId, const ID& nodeId) {
    const TreeSheet* tree = nullptr;
    for (const Sheet& sheet : m_document->project().sheets) {
        if (const auto* candidate = std::get_if<TreeSheet>(&sheet);
            candidate != nullptr && candidate->id == sheetId) {
            tree = candidate;
            break;
        }
    }
    if (tree == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("树图工作表不存在：%1").arg(sheetId)};
    }
    if (!tree->nodes.contains(nodeId)) {
        return AppError{"NOT_FOUND", QStringLiteral("节点不存在：%1").arg(nodeId)};
    }
    for (const Task& task : m_document->project().tasks) {
        if (task.source.has_value() && task.source->sheetId == sheetId &&
            task.source->nodeId == nodeId) {
            return AppError{"CONFLICT", QStringLiteral("该节点已关联任务")};
        }
    }

    const ID newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString title = tree->nodes.value(nodeId).title;
    const auto result = m_commands->applyEdit(QStringLiteral("设为任务"),
                                              [newId, sheetId, nodeId, title](Project& project) {
                                                  Task task;
                                                  task.id = newId;
                                                  task.title = title;
                                                  task.source = TaskSource{sheetId, nodeId};
                                                  project.tasks.push_back(task);
                                              });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return newId;
}

std::variant<bool, AppError> TaskCommands::unlinkTask(const ID& taskId) {
    if (findTask(m_document->project(), taskId) == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("任务不存在：%1").arg(taskId)};
    }
    return m_commands->applyEdit(QStringLiteral("解除任务关联"), [taskId](Project& project) {
        for (Task& task : project.tasks) {
            if (task.id == taskId) {
                task.source = std::nullopt;
                return;
            }
        }
    });
}

}  // namespace lingya
