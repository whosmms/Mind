#include <QtTest>
#include <QUuid>

#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "features/ReviewService.h"

using namespace lingya;

// 复习卡片服务测试（X01，AI规范§7.1）：正反面提取、空卡拒绝、来源哈希与状态、刷新保留 ID/状态。
class TstReview : public QObject {
    Q_OBJECT

private slots:
    void createCardFromNote();
    void createCardFromChildren();
    void noContentNoCard();
    void sourceStateOkThenChanged();
    void sourceStateMissingAfterRemove();
    void refreshKeepsIdAndStatus();
    void keepCustomStopsSync();
    void makeStandaloneAfterRemove();
};

namespace {

ID addChild(TreeSheet& sheet, const ID& parentId, const QString& title) {
    TreeNode node;
    node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.title = title;
    node.parentId = parentId;
    int order = 0;
    for (auto it = sheet.nodes.constBegin(); it != sheet.nodes.constEnd(); ++it) {
        if (it.value().parentId == parentId) {
            ++order;
        }
    }
    node.order = order;
    sheet.nodes.insert(node.id, node);
    return node.id;
}

}  // namespace

void TstReview::createCardFromNote() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("复习"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].note = QStringLiteral("这是备注答案");

    const auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());
    QCOMPARE(card->front, QStringLiteral("中心主题"));
    QCOMPARE(card->back, QStringLiteral("这是备注答案"));
    QCOMPARE(card->status, QStringLiteral("new"));
    QCOMPARE(card->mode, QStringLiteral("linked"));
    QCOMPARE(card->sourceState, QStringLiteral("ok"));
}

void TstReview::createCardFromChildren() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("复习"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    addChild(sheet, sheet.rootId, QStringLiteral("子一"));
    addChild(sheet, sheet.rootId, QStringLiteral("子二"));

    const auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());
    QCOMPARE(card->back, QStringLiteral("子一\n子二"));   // 按 order 排列
}

void TstReview::noContentNoCard() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("空"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    const auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(!card.has_value());   // 无备注、无子节点 → 不生成空卡
}

void TstReview::sourceStateOkThenChanged() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("哈希"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].note = QStringLiteral("答案A");

    const auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());
    QCOMPARE(ReviewService::detectSourceState(*card, p), QStringLiteral("ok"));

    sheet.nodes[sheet.rootId].note = QStringLiteral("答案B");
    QCOMPARE(ReviewService::detectSourceState(*card, p), QStringLiteral("changed"));
}

void TstReview::sourceStateMissingAfterRemove() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("删除"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].note = QStringLiteral("答案");

    const auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());

    sheet.nodes.remove(sheet.rootId);   // 删除源节点（仅用于验证缺失）
    QCOMPARE(ReviewService::detectSourceState(*card, p), QStringLiteral("missing"));
}

void TstReview::refreshKeepsIdAndStatus() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("刷新"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].note = QStringLiteral("旧答案");

    auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());
    card->status = QStringLiteral("review");

    sheet.nodes[sheet.rootId].note = QStringLiteral("新答案");
    const auto refreshed = ReviewService::refreshCard(*card, p);
    QVERIFY(refreshed.has_value());
    QCOMPARE(refreshed->id, card->id);            // 保留卡片 ID
    QCOMPARE(refreshed->status, QStringLiteral("review"));   // 保留学习状态
    QCOMPARE(refreshed->back, QStringLiteral("新答案"));     // 内容已刷新
}

void TstReview::keepCustomStopsSync() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("保留自定义"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].note = QStringLiteral("原答案");

    auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());

    sheet.nodes[sheet.rootId].note = QStringLiteral("新答案");
    QCOMPARE(ReviewService::detectSourceState(*card, p), QStringLiteral("changed"));

    ReviewService::keepCustom(*card);
    QCOMPARE(card->mode, QStringLiteral("custom"));    // 不再跟随源
    QCOMPARE(card->back, QStringLiteral("原答案"));      // 保留用户内容
}

void TstReview::makeStandaloneAfterRemove() {
    Project p = ProjectFactory::createMindmap(QStringLiteral("转独立"));
    TreeSheet& sheet = std::get<TreeSheet>(p.sheets[0]);
    sheet.nodes[sheet.rootId].note = QStringLiteral("答案");

    auto card = ReviewService::createCard(p, sheet.id, sheet.rootId);
    QVERIFY(card.has_value());
    const ID originalSheetId = card->sheetId;

    sheet.nodes.remove(sheet.rootId);
    QCOMPARE(ReviewService::detectSourceState(*card, p), QStringLiteral("missing"));

    ReviewService::makeStandalone(*card);
    QCOMPARE(card->mode, QStringLiteral("custom"));
    QCOMPARE(card->sourceState, QStringLiteral("missing"));
    QCOMPARE(card->sheetId, originalSheetId);   // 来源记录保留
}

QTEST_GUILESS_MAIN(TstReview)
#include "tst_review.moc"
