#pragma once
#include "domain/Domain.h"
#include "features/ReviewSession.h"

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

namespace lingya {

// 复习页（X01，AI规范§7.1）：卡片正面 → 翻面 → 三档自评（重来/已复习/已掌握）。
// 纯展示与交互，不修改 Project；自评结果经 cardGraded 信号交给调用方写回卡片状态
// （调用方须先筛选 sourceState=="ok" 的卡片，源变化未处理的卡片不要塞进会话）。
class ReviewPage : public QWidget {
    Q_OBJECT
public:
    explicit ReviewPage(QWidget* parent = nullptr);

    // 载入卡片集合（新→旧或任意顺序，按传入顺序复习）
    void setCards(const QVector<ReviewCard>& cards);

    bool isFinished() const;
    int completedCount() const;
    int totalCount() const;

signals:
    // 自评结果：调用方据此经命令层写回卡片 status（Reviewed→review、Mastered→mastered；Again 保持原值）
    void cardGraded(const ID& cardId, ReviewMark mark);

private:
    void refresh();
    void onReveal();
    void onGrade(ReviewMark mark);

    ReviewSession m_session;
    QLabel* m_frontLabel = nullptr;
    QLabel* m_backLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_revealButton = nullptr;
    QPushButton* m_againButton = nullptr;
    QPushButton* m_reviewedButton = nullptr;
    QPushButton* m_masteredButton = nullptr;
};

}  // namespace lingya
