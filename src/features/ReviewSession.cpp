#include "features/ReviewSession.h"

namespace lingya {

ReviewSession::ReviewSession(const QVector<ReviewCard>& cards)
    : m_queue(cards), m_total(cards.size()) {
}

bool ReviewSession::isFinished() const {
    return m_queue.isEmpty();
}

bool ReviewSession::hasCurrent() const {
    return !m_queue.isEmpty();
}

const ReviewCard& ReviewSession::currentCard() const {
    return m_queue.first();
}

bool ReviewSession::isRevealed() const {
    return m_revealed;
}

bool ReviewSession::reveal() {
    if (m_queue.isEmpty()) {
        return false;
    }
    m_revealed = true;
    return true;
}

bool ReviewSession::grade(ReviewGrade grade) {
    if (m_queue.isEmpty() || !m_revealed) {
        return false;  // 必须先翻面，避免误判
    }
    const ReviewCard card = m_queue.takeFirst();
    m_revealed = false;
    if (grade == ReviewGrade::Again) {
        m_queue.append(card);  // 回到队尾，稍后重来
    } else {
        ++m_completed;
    }
    return true;
}

int ReviewSession::totalCount() const {
    return m_total;
}

int ReviewSession::completedCount() const {
    return m_completed;
}

int ReviewSession::remainingCount() const {
    return m_queue.size();
}

}  // namespace lingya
