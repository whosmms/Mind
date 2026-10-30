#pragma once
#include "domain/Domain.h"
#include "layout/FlowLayout.h"

#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// 流程节点补丁（R12）：外层optional=是否修改该字段。
// title按标题规则规范化；role只允许start/end/process/decision/io；position须有限数。
struct FlowNodePatch {
    std::optional<QString> title;
    std::optional<QString> note;
    std::optional<QString> role;
    std::optional<QPointF> position;
    std::optional<QStringList> tags;
    std::optional<NodeStyleOverride> style;
};

// 流程连线补丁：标签/箭头/端口整值替换；端口空串=自动，非空只允许left/right/top/bottom。
struct FlowEdgePatch {
    std::optional<QString> label;
    std::optional<QString> arrow;
    std::optional<QString> sourcePort;
    std::optional<QString> targetPort;
};

// 流程图类型化命令（R12，规范§4.3.7、§5.1、§6.3）。
// 流程图是独立图模型：允许回路、多个起止、暂时未连接的节点，不按树约束校验（规则3）。
// 所有写入统一经CommandService::applyEdit（候选副本→ProjectValidator校验→单次快照入栈），
// 失败不改变工程，无操作短路不占撤销历史。
// 自动布局的几何计算在layout/FlowLayout（纯数据服务），本类只负责把结果写入一次可撤销命令。
class FlowCommands {
public:
    FlowCommands(CommandService* commands, const ProjectDocument* document);

    // 新增流程节点。role须为start/end/process/decision/io之一，title空白转“未命名主题”，
    // position须为有限数（自由坐标，网格吸附由界面决定）。成功返回新节点ID。
    std::variant<ID, AppError> addNode(const ID& sheetId, const QString& role,
                                       const QString& title, const QPointF& position);

    // 按补丁更新节点：标题/备注/类型/位置/标签/局部样式；contentRevision+1；可撤销。
    // 与当前值完全相同时短路，不入历史（D-M1-02显式例外）。
    std::variant<bool, AppError> updateNode(const ID& sheetId, const ID& nodeId,
                                            const FlowNodePatch& patch);

    // 移动节点（拖动松手一次提交）：等价于只改position的updateNode。
    std::variant<bool, AppError> moveNode(const ID& sheetId, const ID& nodeId,
                                          const QPointF& position);

    // 删除节点，同一事务内清理：以它为任一端点的连线、挂在它上面的批注。
    // 关联任务保留为独立任务（与树图deleteSubtree的默认策略一致，D-M1-05）。
    std::variant<bool, AppError> removeNode(const ID& sheetId, const ID& nodeId);

    // 新增有向连线。端点必须存在且同一工作表；允许自环与回路（R12“回路”要求）；
    // 完全相同的连接（同向+同端口）拒绝并提示，不同端口允许（表达不同含义）。
    std::variant<ID, AppError> addEdge(const ID& sheetId, const ID& sourceId, const ID& targetId,
                                       const QString& label,
                                       const QString& sourcePort = QString(),
                                       const QString& targetPort = QString());

    std::variant<bool, AppError> updateEdge(const ID& sheetId, const ID& edgeId,
                                            const FlowEdgePatch& patch);
    std::variant<bool, AppError> removeEdge(const ID& sheetId, const ID& edgeId);

    // 自动布局（规范§6.3）：使用FlowLayout算出的位置整表写入，单事务一次撤销。
    // 尺寸由界面按实际字体测量提供；缺尺寸或尺寸非正数返回错误且不改数据。
    // 结果与当前位置完全相同时短路，不入历史。
    std::variant<bool, AppError> applyLayout(const ID& sheetId,
                                             const QVector<FlowLayout::NodeSize>& sizes);

    // R09批注在流程图上的对应命令：语义与树图一致（正文去空白、≤2000码点、UTC时间、状态相同短路）。
    std::variant<ID, AppError> addComment(const ID& sheetId, const ID& nodeId,
                                          const QString& author, const QString& body);
    std::variant<bool, AppError> resolveComment(const ID& sheetId, const ID& commentId,
                                                bool resolved = true);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
