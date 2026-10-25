#pragma once
#include "domain/Domain.h"

#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// 节点元数据补丁（M2：图标、备注、链接、标签）：外层optional=是否修改该字段；
// 文本字段先去首尾空白，空白视为清除（iconId/link置空、tags为空表）。链接仅允许http/https
// （与ProjectJson读写规则一致，规范§4.3.10）。
struct NodeMetaUpdate {
    std::optional<QString> note;      // 修改备注（可为空串=清空；≤20000码点）
    std::optional<QString> iconId;    // 修改图标资源ID（空白=清除）
    std::optional<QString> link;      // 修改链接（空白=清除；非空须http/https）
    std::optional<QStringList> tags;  // 整表替换（每项去空白；≤20项、每项≤30码点）
};

// 节点类型化命令（统一规范§5.1：addChild、addSibling、deleteSubtree、updateNodeMeta、setStyle）。
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

    // 按补丁更新节点元数据（note/iconId/link/tags）：单事务、可撤销、contentRevision+1；
    // 补丁产生的值与当前值完全相同时无操作短路，不入历史（D-M1-02显式例外）。
    std::variant<bool, AppError> updateNodeMeta(const ID& sheetId, const ID& nodeId,
                                                const NodeMetaUpdate& update);

    // 整值替换节点局部样式override：空override=清除，回退工作表默认样式；可撤销。
    // 预检（基础码）：颜色#RRGGBB、shape五枚举、字号12至32、边宽正数；与当前相同则短路。
    std::variant<bool, AppError> setStyle(const ID& sheetId, const ID& nodeId,
                                          const NodeStyleOverride& style);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
