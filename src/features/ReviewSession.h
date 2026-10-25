#pragma once
#include "domain/Domain.h"

#include <QSet>
#include <QString>
#include <QVector>
#include <variant>

namespace lingya {

// 复习标记（AI规范§7.1）：与 ReviewCard.status 的 new/review/mastered 三态一致，
// 不声称实现间隔重复算法（规范明确禁止）。
enum class ReviewMark {
    Again,     // 回队尾，status 保持原值，不计完成
    Reviewed,  // 移出队列，status = "review"，计完成
    Mastered   // 移出队列，status = "mastered"，计完成
};

// 自评结果：供界面/命令写回卡片 status（本类不修改 Project）
struct GradeOutcome {
    ID cardId;
    ReviewMark mark;
};

// 标记对应的卡片 status（规范§7.1）：Reviewed→review、Mastered→mastered；Again 由调用方保持原值
QString reviewStatusForMark(ReviewMark mark);

// 复习会话状态机（X01）：卡片队列、翻面、自评、结束判定。
// 纯逻辑，位于 lingya_core，不修改 Project。
// 调用方必须传入已筛选的卡片集合：sourceState 非 "ok" 的卡片应先经源变化处理
// （更新卡片/保留自定义/移除/转独立），不要把过期快照静默塞进会话。
class ReviewSession {
public:
    explicit ReviewSession(const QVector<ReviewCard>& cards = {});

    bool isFinished() const;
    bool hasCurrent() const;
    const ReviewCard* currentCard() const;  // 空队列返回 nullptr

    bool isRevealed() const;
    bool reveal();  // 翻面；空队列返回 false

    // 自评：须先翻面；空队列或未翻面返回 AppError(INVALID_ARGUMENT)
    std::variant<GradeOutcome, AppError> grade(ReviewMark mark);

    int totalCount() const;
    int completedCount() const;
    int remainingCount() const;

private:
    QVector<ReviewCard> m_queue;  // 待复习队列，front 为当前卡
    int m_total = 0;
    int m_completed = 0;
    bool m_revealed = false;
    QSet<ID> m_completedIds;  // 每张卡最多完成一次
};

}  // namespace lingya
