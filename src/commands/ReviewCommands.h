#pragma once
#include "domain/Domain.h"
#include "features/ReviewSession.h"

#include <variant>

namespace lingya {

class CommandService;
class ProjectDocument;

// 复习卡片命令（X01；补 L-017：复习页的自评结果必须落库）。
// 所有写入统一收口于CommandService::applyEdit（候选副本→ProjectValidator校验→快照入栈），
// 因此卡片修改自动具备事务性、校验与撤销重做；失败不改数据。
// 只读当前工程用于存在性/唯一性预检，不直接改数据。
class ReviewCommands {
public:
    ReviewCommands(CommandService* commands, const ProjectDocument* document);

    // 自评写回（规范§7.1）：Reviewed→status="review"、Mastered→status="mastered"，
    // 同时把lastReviewedAt置为当前UTC时间（卡片“最近复习”的唯一来源）。
    // Again表示“回队尾、稍后再看”，status与时间都不变：直接返回成功且不入历史（不是失败）。
    std::variant<bool, AppError> gradeCard(const ID& cardId, ReviewMark mark);

    // 由节点新建卡片：正面=节点标题，背面=备注或直属子节点标题，status="new"、mode="linked"。
    // 节点无可提取内容（标题与背面皆空）返回NO_CONTENT；同一节点已有卡片返回CARD_EXISTS。
    std::variant<ID, AppError> addCard(const ID& sheetId, const ID& nodeId);

    // 删除卡片；不存在返回NOT_FOUND
    std::variant<bool, AppError> removeCard(const ID& cardId);

    // 源变化后的三种处置（规范§7.1）：
    // 按当前源重算正反面与哈希（保留id/status/最近复习时间）；
    // 保留自定义（mode改为custom，不再跟随源）；转为独立卡片（custom + missing）。
    std::variant<bool, AppError> refreshFromSource(const ID& cardId);
    std::variant<bool, AppError> keepCustom(const ID& cardId);
    std::variant<bool, AppError> makeStandalone(const ID& cardId);

private:
    CommandService* m_commands;
    const ProjectDocument* m_document;
};

}  // namespace lingya
