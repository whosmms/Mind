#pragma once
#include "domain/Domain.h"
#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// 任务类型化命令（统一规范§5.1清单：addTask、updateTask、linkTask、unlinkTask）。
// 所有写入统一收口于CommandService::applyEdit（候选副本→ProjectValidator校验→快照入栈），
// 因此任务修改自动具备事务性、校验与撤销重做；删除任务同时清理其他任务对它的前置依赖。
// 只读当前工程用于存在性/唯一性预检，不直接改数据。
class TaskCommands {
public:
    TaskCommands(CommandService* commands, const ProjectDocument* document);

    // 新增任务，返回新任务ID；校验失败返回AppError
    std::variant<ID, AppError> addTask(const QString& title);

    // 按ID整体更新任务（标题、起止日、进度、负责人、里程碑、依赖）
    std::variant<bool, AppError> updateTask(const Task& updated);

    // 删除任务，并清除其他任务对它的前置依赖（§6.4：删除任务清除依赖并可撤销）
    std::variant<bool, AppError> removeTask(const ID& taskId);

    // 把节点设为任务（X03）：建立唯一Task，source指向节点，标题取节点当前标题
    std::variant<ID, AppError> linkTask(const ID& sheetId, const ID& nodeId);

    // 解除关联：保留两边当前标题，清除source
    std::variant<bool, AppError> unlinkTask(const ID& taskId);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
