#pragma once
#include "domain/Domain.h"

#include <QVector>

namespace lingya {

// 复习会话状态机（X01）：卡片队列、翻面、自评、结束判定。
// 纯逻辑，不依赖界面；ReviewPage 只负责显示与按钮。
enum class ReviewGrade {
    Again,  // 重来：卡片回到队尾，稍后再说
    Hard,   // 困难
    Good,   // 良好
    Easy    // 简单
};

class ReviewSession {
public:
    explicit ReviewSession(const QVector<ReviewCard>& cards = {});

    bool isFinished() const;
    bool hasCurrent() const;
    const ReviewCard& currentCard() const;  // 调用前应 hasCurrent()

    bool isRevealed() const;
    bool reveal();  // 翻面；无当前卡返回 false

    // 自评：须先翻面；Again 回到队尾，其余记为完成
    bool grade(ReviewGrade grade);

    int totalCount() const;
    int completedCount() const;
    int remainingCount() const;

private:
    QVector<ReviewCard> m_queue;  // 待复习队列，front 为当前卡
    int m_total = 0;
    int m_completed = 0;
    bool m_revealed = false;
};

}  // namespace lingya
