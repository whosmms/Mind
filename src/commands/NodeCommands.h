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
// 第一批只交付addChild；addSibling、deleteSubtree在第二批按已冻结签名补充。
class NodeCommands {
public:
    NodeCommands(CommandService* commands, const ProjectDocument* document);

    // 在父节点子主题末尾新增；root可作为父节点。成功返回新节点ID。
    // 标题先统一规范化；空白转为“未命名主题”；超过Limits::kTitleMaxChars返回LIMIT_EXCEEDED。
    std::variant<ID, AppError> addChild(const ID& sheetId, const ID& parentId,
                                        const QString& title);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
