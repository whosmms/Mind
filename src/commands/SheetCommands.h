#pragma once
#include "domain/Domain.h"

#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// 工作表级命令（M3）：新建、改名、更换结构、删除。
// 规范§5.1要求changeSheetKind与deleteSheet必须可撤销、单事务；addSheet为M3编辑闭环的必要补充
// （此前仓库内没有任何创建流程图/鱼骨图/甘特图工作表的路径，R11/R12/R13无法被界面触达）。
// 所有写入经CommandService::applyEdit（候选副本→校验→单快照），失败不改数据。
class SheetCommands {
public:
    SheetCommands(CommandService* commands, const ProjectDocument* document);

    // 追加一张工作表并设为当前工作表。kind只允许mindmap/fishbone/flowchart/gantt；
    // name留空则按“工作表 N”自动命名；工程工作表数量上限Limits::kMaxSheets。成功返回新工作表ID。
    std::variant<ID, AppError> createSheet(const QString& kind, const QString& name = QString());

    // 重命名工作表：去首尾空白后不得为空，长度同标题上限；相同则短路。
    std::variant<bool, AppError> renameSheet(const ID& sheetId, const QString& name);

    // 更换结构（规范§6.2）：只支持导图↔鱼骨，二者共享树语义，保留标题/备注/标签/任务/关系。
    // 流程图与甘特图是独立模型，不做无损切换，返回INVALID_ARGUMENT（不做单向副本）。
    std::variant<bool, AppError> changeSheetKind(const ID& sheetId, const QString& kind);

    // 删除工作表，同一事务内把关联任务保留为独立任务（清除source）；拒绝删除最后一张工作表；
    // 删除当前工作表时激活第一张剩余工作表。
    // 该表上仍有“用户攒下来的东西”时拒绝删除并返回SHEET_IN_USE，提示先处理：
    // 文稿（独立创作内容）、复习卡片（学习状态，规范§7.1要求源删除时保留卡片而非销毁）、
    // 讲解路线（步骤带用户写的讲解备注，规范§7.2）。含义见BUG-020。
    std::variant<bool, AppError> deleteSheet(const ID& sheetId);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
