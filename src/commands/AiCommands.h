#pragma once
#include "ai/AiTypes.h"
#include "domain/Domain.h"

#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// AI采用命令（规范§5.1清单中的applyAIMap与saveTextDraft，X05/X06的落库环节）。
// 分工边界：AiService负责请求、状态机、返回校验与预览；本类只把“已通过校验的提案”
// 写成可编辑数据，因此不发网络请求、不读密钥、不改预览状态、不写任何AI自报的用量。
// 采用是单一可撤销事务（CommandService::applyEdit）：校验或写盘失败时工程不变，
// 用户取消预览时根本不调用本类。

// 采用的公共选项（applyAIMap与saveTextDraft共用）。
// expectedRevision为“发起生成请求时”的ProjectDocument::sessionRevision()：
// >=0且与当前不一致时返回STALE_SOURCE，要求界面让用户重新确认目标后再采用，
// 不自动插入旧位置（规范§10.6）。用会话修订而非Project.revision：后者随文件保存，
// 单次编辑不会递增，无法反映“生成后又被改过”。不传（-1）表示调用方已确认接受当前状态。
struct AiApplyOptions {
    qint64 expectedRevision = -1;
    QString sheetName;   // 新建工作表名称；留空取提案标题
    QString requestId;   // 写入provenance，供来源追踪；可为空
};

class AiCommands {
public:
    AiCommands(CommandService* commands, const ProjectDocument* document);

    // X05采用方式一（默认）：新建工作表承载整棵提案树并设为当前工作表。成功返回新工作表ID。
    // 节点ID、父级、order与布局由程序生成；每个节点的来源写入BaseNode::provenance，
    // 手工节点不设置该字段（规范§10.6）。
    std::variant<ID, AppError> applyMapAsSheet(const MapProposal& proposal,
                                              const AiApplyOptions& options = {});

    // X05采用方式二：提案根作为目标节点的下一个新子节点，完整保留提案树。
    // 目标必须是树图且父节点仍存在，否则返回STALE_SOURCE。
    // 成功返回提案根的节点ID。
    std::variant<ID, AppError> applyMapAsBranch(const MapProposal& proposal, const ID& sheetId,
                                                const ID& parentId,
                                                const AiApplyOptions& options = {});

    // X06采用：创建TextDraft并记录sourceRevision；此后源图改变只提示“源导图已更新”，
    // 不静默重写草稿（规范§10.5）。首版只对导图/鱼骨图启用。
    // sourceRevision应传“生成时”的sessionRevision()，<0时取当前值；
    // 界面用draft.sourceRevision与当前sessionRevision比较即可判断是否过期。
    // expectedRevision规则同上。
    std::variant<ID, AppError> saveDraft(const DraftProposal& proposal, const ID& sourceSheetId,
                                         const QString& purpose, qint64 sourceRevision,
                                         const AiApplyOptions& options = {});

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
