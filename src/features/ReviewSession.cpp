#include "features/ReviewSession.h"

namespace lingya {

QString reviewStatusForMark(ReviewMark mark) {
    switch (mark) {
    case ReviewMark::Reviewed:
        return QStringLiteral("review");
    case ReviewMark::Mastered:
        return QStringLiteral("mastered");
    case ReviewMark::Again:
        return QString();  // 调用方保持原值
    }
    return QString();
}

ReviewSession::ReviewSession(const QVector<ReviewCard>& cards)
    : m_queue(cards), m_total(cards.size()) {
}

bool ReviewSession::isFinished() const {
    return m_queue.isEmpty();
}

bool ReviewSession::hasCurrent() const {
    return !m_queue.isEmpty();
}

const ReviewCard* ReviewSession::currentCard() const {
    return m_queue.isEmpty() ? nullptr : &m_queue.first();
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

std::variant<GradeOutcome, AppError> ReviewSession::grade(ReviewMark mark) {
    if (m_queue.isEmpty()) {
        return AppError{QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("没有可自评的卡片")};
    }
    if (!m_revealed) {
        return AppError{QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("请先翻面再自评")};
    }

    const ReviewCard card = m_queue.takeFirst();
    m_revealed = false;

    if (mark == ReviewMark::Again) {
        m_queue.append(card);  // 回队尾，status 不变、不计完成
        return GradeOutcome{card.id, mark};
    }

    if (!m_completedIds.contains(card.id)) {
        m_completedIds.insert(card.id);
        ++m_completed;
    }
    return GradeOutcome{card.id, mark};
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
