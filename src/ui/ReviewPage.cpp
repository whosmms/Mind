#include "ui/ReviewPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <variant>

namespace lingya
{

// 只改变显示高度；原卡片、翻面及三档自评仍由 ReviewSession 管理。
class ReviewTextScroll : public QScrollArea
{
  public:
    ReviewTextScroll(QLabel *label, QWidget *parent) : QScrollArea(parent), m_label(label)
    {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setWidget(label);
    }

    void refreshHeight()
    {
        if (m_refreshing)
            return;
        const QScopedValueRollback<bool> guard(m_refreshing, true);
        const int width = qMax(40, viewport()->width());
        // QLabel 的 heightForWidth 会受旧 minimumHeight 约束，测量前先清除。
        m_label->setMinimumHeight(0);
        m_label->setMinimumHeight(qMax(96, m_label->heightForWidth(width)));
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QScrollArea::resizeEvent(event);
        refreshHeight();
    }

  private:
    QLabel *m_label;
    bool m_refreshing = false;
};

ReviewPage::ReviewPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("reviewPage"));

    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName(QStringLiteral("reviewProgressLabel"));

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("reviewProgress"));
    m_progress->setTextVisible(false);

    m_frontLabel = new QLabel(this);
    m_frontLabel->setObjectName(QStringLiteral("reviewFront"));
    m_frontLabel->setWordWrap(true);
    m_frontLabel->setTextFormat(Qt::PlainText);
    m_frontLabel->setAlignment(Qt::AlignCenter);
    m_frontLabel->setMinimumHeight(96);

    m_backLabel = new QLabel(this);
    m_backLabel->setObjectName(QStringLiteral("reviewBack"));
    m_backLabel->setWordWrap(true);
    m_backLabel->setTextFormat(Qt::PlainText);
    m_backLabel->setAlignment(Qt::AlignCenter);
    m_backLabel->setMinimumHeight(96);
    m_frontScroll = new ReviewTextScroll(m_frontLabel, this);
    m_frontScroll->setObjectName(QStringLiteral("reviewFrontScroll"));
    m_backScroll = new ReviewTextScroll(m_backLabel, this);
    m_backScroll->setObjectName(QStringLiteral("reviewBackScroll"));

    m_revealButton = new QPushButton(QStringLiteral("翻面"), this);
    m_revealButton->setObjectName(QStringLiteral("reviewReveal"));

    m_againButton = new QPushButton(QStringLiteral("重来"), this);
    m_againButton->setObjectName(QStringLiteral("reviewAgain"));
    m_reviewedButton = new QPushButton(QStringLiteral("已复习"), this);
    m_reviewedButton->setObjectName(QStringLiteral("reviewReviewed"));
    m_masteredButton = new QPushButton(QStringLiteral("已掌握"), this);
    m_masteredButton->setObjectName(QStringLiteral("reviewMastered"));

    auto *gradeRow = new QHBoxLayout;
    gradeRow->addWidget(m_againButton);
    gradeRow->addWidget(m_reviewedButton);
    gradeRow->addWidget(m_masteredButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_progressLabel);
    layout->addWidget(m_progress);
    layout->addWidget(m_frontScroll, 1);
    layout->addWidget(m_backScroll, 1);
    layout->addWidget(m_revealButton);
    layout->addLayout(gradeRow);

    connect(m_revealButton, &QPushButton::clicked, this, &ReviewPage::onReveal);
    connect(m_againButton, &QPushButton::clicked, this, [this] { onGrade(ReviewMark::Again); });
    connect(m_reviewedButton, &QPushButton::clicked, this,
            [this] { onGrade(ReviewMark::Reviewed); });
    connect(m_masteredButton, &QPushButton::clicked, this,
            [this] { onGrade(ReviewMark::Mastered); });

    refresh();
}

void ReviewPage::setCards(const QVector<ReviewCard> &cards)
{
    m_session = ReviewSession(cards);
    refresh();
}

bool ReviewPage::isFinished() const
{
    return m_session.isFinished();
}

int ReviewPage::completedCount() const
{
    return m_session.completedCount();
}

int ReviewPage::totalCount() const
{
    return m_session.totalCount();
}

void ReviewPage::onReveal()
{
    m_session.reveal();
    refresh();
}

void ReviewPage::onGrade(ReviewMark mark)
{
    const auto outcome = m_session.grade(mark);
    if (const auto *result = std::get_if<GradeOutcome>(&outcome))
    {
        emit cardGraded(result->cardId, result->mark);
    }
    refresh();
}

void ReviewPage::refresh()
{
    const ReviewCard *card = m_session.currentCard();
    const bool revealed = m_session.isRevealed();

    m_progress->setMaximum(qMax(m_session.totalCount(), 1));
    m_progress->setValue(m_session.completedCount());
    m_progressLabel->setText(QStringLiteral("已完成 %1 / %2")
                                 .arg(m_session.completedCount())
                                 .arg(m_session.totalCount()));

    if (card == nullptr)
    {
        m_frontLabel->setText(QStringLiteral("复习完成"));
        m_backLabel->setText(QString());
        m_backLabel->setVisible(false);
        m_backScroll->hide();
        m_frontScroll->refreshHeight();
        m_revealButton->setEnabled(false);
        m_againButton->setEnabled(false);
        m_reviewedButton->setEnabled(false);
        m_masteredButton->setEnabled(false);
        return;
    }

    m_frontLabel->setText(card->front);
    m_backLabel->setText(revealed ? card->back : QString());
    m_backLabel->setVisible(revealed);
    m_backScroll->setVisible(revealed);
    m_frontScroll->refreshHeight();
    m_backScroll->refreshHeight();
    QTimer::singleShot(0, this,
                       [this]
                       {
                           m_frontScroll->refreshHeight();
                           m_backScroll->refreshHeight();
                       });
    m_revealButton->setEnabled(!revealed);
    m_againButton->setEnabled(revealed);
    m_reviewedButton->setEnabled(revealed);
    m_masteredButton->setEnabled(revealed);
}

} // namespace lingya
