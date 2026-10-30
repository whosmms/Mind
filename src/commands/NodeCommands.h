#pragma once
#include "domain/Domain.h"
#include "document/Fragment.h"

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

// 关系线补丁（R07）：外层optional=是否修改该字段。箭头只允许none/end/both。
struct RelationUpdate {
    std::optional<QString> label;
    std::optional<QString> arrow;
};

// 外框/概要补丁（R08）：成员整表替换，仍须满足“同父连续兄弟、至少2个”的规则。
struct AnnotationUpdate {
    std::optional<QStringList> memberIds;
    std::optional<QString> text;
    std::optional<QString> color;   // 空串=使用主题色；非空须#RRGGBB
};

// 节点类型化命令（统一规范§5.1：addChild、addSibling、deleteSubtree、moveSubtree、
// updateNodeMeta、setStyle、addRelation、updateRelation、removeRelation、
// createAnnotation、updateAnnotation、addComment、resolveComment、pasteFragment，
// 以及工作表级默认样式的单一入口setSheetStyle）。
// 所有写入统一经CommandService::applyEdit（候选副本→ProjectValidator校验→单次快照入栈），
// 失败不改变工程、dirty与撤销历史。
// 改名统一使用TaskCommands::renameNode，本类不重复提供，避免两套行为。
// 本批命令作用于树图（mindmap/fishbone）；流程图节点与连线另见后续批次。
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

    // 把nodeId及其子树移动到newParentId下（R06 拖动改父级/顺序，规范§5.2）：
    // beforeNodeId为空=追加到末尾，否则插到该兄弟之前（必须是newParentId的子节点）。
    // 拒绝：根节点、新父级在自身子树内（环）、类型不符的参照节点；顺序写回连续order。
    // 节点、子树与任务ID保持不变（同工程内移动不重建ID，卡片不失联）。
    std::variant<bool, AppError> moveSubtree(const ID& sheetId, const ID& nodeId,
                                             const ID& newParentId,
                                             const ID& beforeNodeId = ID{});

    // 按补丁更新节点元数据（note/iconId/link/tags）：单事务、可撤销、contentRevision+1；
    // 补丁产生的值与当前值完全相同时无操作短路，不入历史（D-M1-02显式例外）。
    std::variant<bool, AppError> updateNodeMeta(const ID& sheetId, const ID& nodeId,
                                                const NodeMetaUpdate& update);

    // 整值替换节点局部样式override：空override=清除，回退工作表默认样式；可撤销。
    // 预检（基础码）：颜色#RRGGBB、shape五枚举、字号12至32、边宽正数；与当前相同则短路。
    std::variant<bool, AppError> setStyle(const ID& sheetId, const ID& nodeId,
                                          const NodeStyleOverride& style);

    // 工作表默认样式（R05 全局样式）：整值替换defaultNodeStyle/defaultEdgeStyle，
    // 未做局部覆盖的节点/连线随之变化；局部override优先级不变。与当前相同则短路。
    // 预检：颜色#RRGGBB、字号12至32、shape五枚举、边宽0.1至20且有限。
    std::variant<bool, AppError> setSheetStyle(const ID& sheetId, const NodeStyle& nodeStyle,
                                               const EdgeStyle& edgeStyle);

    // R07 关系线：端点必须是同一张树图内的不同节点；同一有序端点对不重复建线；
    // 标签先规范化并限制在Limits::kTitleMaxChars内；箭头只允许none/end/both。
    // 成功返回关系ID；删除节点时由deleteSubtree清理端点缺失的关系线（规范§4.3.7）。
    std::variant<ID, AppError> addRelation(const ID& sheetId, const ID& sourceId,
                                           const ID& targetId, const QString& label,
                                           const QString& arrow = QStringLiteral("end"));
    std::variant<bool, AppError> updateRelation(const ID& sheetId, const ID& relationId,
                                                const RelationUpdate& update);
    std::variant<bool, AppError> removeRelation(const ID& sheetId, const ID& relationId);

    // R08 外框/概要（规范§4.3.6）：只支持“同父连续兄弟”集合，成员按树顺序保存，
    // 至少2个、不可重复、不可包含父级自身；kind只允许boundary/summary。
    // 文本先去首尾空白并限制在Limits::kTitleMaxChars内；颜色空串=主题色。
    std::variant<ID, AppError> createAnnotation(const ID& sheetId, const ID& parentId,
                                                const QString& kind,
                                                const QStringList& memberIds,
                                                const QString& text = QString(),
                                                const QString& color = QString());
    // 成员整表替换时重新校验连续性与数量；与当前值相同则短路。
    std::variant<bool, AppError> updateAnnotation(const ID& sheetId, const ID& annotationId,
                                                  const AnnotationUpdate& update);
    std::variant<bool, AppError> removeAnnotation(const ID& sheetId, const ID& annotationId);

    // R09 批注（规范§4.3.8：单条最多2000码点）：正文去首尾空白后不得为空；
    // 作者去首尾空白；createdAt/updatedAt由命令层按UTC写入；resolve可反复调用，状态相同则短路。
    std::variant<ID, AppError> addComment(const ID& sheetId, const ID& nodeId,
                                          const QString& author, const QString& body);
    std::variant<bool, AppError> resolveComment(const ID& sheetId, const ID& commentId,
                                                bool resolved = true);

    // R03 复制：取给定节点中最高的子树（祖先与后代同选时只处理最高层一次），
    // 一并收集内部关系、内部外框/概要、挂在片段节点上的批注与关联任务副本；
    // 复习卡片与讲解路线不复制（规范§5.3）。不写数据、不入历史。
    std::variant<Fragment, AppError> copyFragment(const ID& sheetId,
                                                  const QVector<ID>& nodeIds) const;

    // R03 粘贴：为片段内全部对象重新生成ID并重映射内部引用（关系/外框成员/批注/任务source），
    // 顶层根挂到parentId下（beforeNodeId为空=追加末尾），单事务、一次撤销。
    // 任务作为独立副本继承日期与进度；指向片段外的前置依赖被清除（规范§5.3）。
    // 成功返回第一个顶层新节点ID。
    std::variant<ID, AppError> pasteFragment(const ID& sheetId, const ID& parentId,
                                             const Fragment& fragment,
                                             const ID& beforeNodeId = ID{});

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
