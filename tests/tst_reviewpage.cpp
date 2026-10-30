#include <QtTest>

#include "domain/Domain.h"
#include "features/ReviewSession.h"
#include "ui/ReviewPage.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

using namespace lingya;

// 复习页测试（X01，AI规范§7.1）：正面/翻面/三档自评信号/结束状态。
class TstReviewPage : public QObject {
    Q_OBJECT

private slots:
    void startsWithFrontAndHiddenBack();
    void revealShowsBackAndEnablesGrades();
    void gradingEmitsCardIdAndMark();
    void finishesAfterAllGraded();
};

namespace {

ReviewCard makeCard(const QString& front, const QString& back) {
    ReviewCard card;
    card.id = front;
    card.front = front;
    card.back = back;
    return card;
}

QPushButton* button(QWidget* page, const char* objectName) {
    return page->findChild<QPushButton*>(QString::fromLatin1(objectName));
}

}  // namespace

void TstReviewPage::startsWithFrontAndHiddenBack() {
    ReviewPage page;
    page.setCards({makeCard(QStringLiteral("Q1"), QStringLiteral("A1")),
                   makeCard(QStringLiteral("Q2"), QStringLiteral("A2"))});

    auto* front = page.findChild<QLabel*>(QStringLiteral("reviewFront"));
    auto* back = page.findChild<QLabel*>(QStringLiteral("reviewBack"));
    QVERIFY(front != nullptr);
    QVERIFY(back != nullptr);
    QCOMPARE(front->text(), QStringLiteral("Q1"));
    QVERIFY(back->isHidden());
    QCOMPARE(page.totalCount(), 2);
    QCOMPARE(page.completedCount(), 0);
    QVERIFY(!page.isFinished());
}

void TstReviewPage::revealShowsBackAndEnablesGrades() {
    ReviewPage page;
    page.setCards({makeCard(QStringLiteral("Q1"), QStringLiteral("A1"))});

    auto* reveal = button(&page, "reviewReveal");
    auto* reviewed = button(&page, "reviewReviewed");
    QVERIFY(reveal != nullptr);
    QVERIFY(reviewed != nullptr);
    QVERIFY(!reviewed->isEnabled());   // 未翻面不能自评

    reveal->click();
    auto* back = page.findChild<QLabel*>(QStringLiteral("reviewBack"));
    QVERIFY(back != nullptr);
    QVERIFY(!back->isHidden());
    QCOMPARE(back->text(), QStringLiteral("A1"));
    QVERIFY(reviewed->isEnabled());
}

void TstReviewPage::gradingEmitsCardIdAndMark() {
    ReviewPage page;
    page.setCards({makeCard(QStringLiteral("Q1"), QStringLiteral("A1")),
                   makeCard(QStringLiteral("Q2"), QStringLiteral("A2"))});
    QSignalSpy spy(&page, &ReviewPage::cardGraded);

    button(&page, "reviewReveal")->click();
    button(&page, "reviewReviewed")->click();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("Q1"));
    QVERIFY(spy.first().at(1).value<ReviewMark>() == ReviewMark::Reviewed);
    QCOMPARE(page.completedCount(), 1);
}

void TstReviewPage::finishesAfterAllGraded() {
    ReviewPage page;
    page.setCards({makeCard(QStringLiteral("Q1"), QStringLiteral("A1"))});

    button(&page, "reviewReveal")->click();
    button(&page, "reviewMastered")->click();

    QVERIFY(page.isFinished());
    auto* front = page.findChild<QLabel*>(QStringLiteral("reviewFront"));
    QCOMPARE(front->text(), QStringLiteral("复习完成"));
    QVERIFY(!button(&page, "reviewReveal")->isEnabled());
}

QTEST_MAIN(TstReviewPage)
#include "tst_reviewpage.moc"
