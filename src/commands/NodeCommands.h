#pragma once
#include "domain/Domain.h"

#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// 节点类型化命令（统一规范§5.1：addChild、addSibling、deleteSubtree）。
// 所有写入统一经CommandService::applyEdit（候选副本→ProjectValidator校验→单次快照入栈），
// 失败不改变工程、dirty与撤销历史。
// 改名统一使用TaskCommands::renameNode，本类不重复提供，避免两套行为。
class NodeCommands {
public:
    NodeCommands(CommandService* commands, const ProjectDocument* document);

    // 删除子树时对关联任务的处置策略（D-M1-05）：
    // KeepAsStandalone——默认，清除Task.source保留为独立任务；
    // DeleteTogether——同一事务内删除任务并清理其他任务对它的前置依赖。
    enum class TaskDeletePolicy { KeepAsStandalone, DeleteTogether };

    // 在父节点子主题末尾新增；root可作为父节点。成功返回新节点ID。
    // 标题先统一规范化；空白转为“未命名主题”；超过Limits::kTitleMaxChars返回LIMIT_EXCEEDED。
    std::variant<ID, AppError> addChild(const ID& sheetId, const ID& parentId,
                                        const QString& title);

    // 在参照节点之后新增同级主题（D-M1-03：先按(order,ID)规范化形成兄弟序列，
    // 再在参照之后插入，最后写回连续order）。参照不能是根节点（根无同级）。
    // 标题规则与addChild一致。成功返回新节点ID。
    std::variant<ID, AppError> addSibling(const ID& sheetId, const ID& referenceId,
                                          const QString& title);

    // 删除以nodeId为顶的整棵子树，同一事务内清理（规范§12删除必须单事务，D-M1-05）：
    // 子树节点、任一端点在子树内的关系线、父级或任一成员在子树内的分组注释、
    // 指向子树节点的批注；关联任务按policy处置；源节点被删的复习卡片保留并置
    // sourceState="missing"；讲解路线与文稿不自动改写。根节点拒绝删除。
    std::variant<bool, AppError> deleteSubtree(const ID& sheetId, const ID& nodeId,
                                               TaskDeletePolicy policy =
                                                   TaskDeletePolicy::KeepAsStandalone);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
