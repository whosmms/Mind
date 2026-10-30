#include <QtTest>

#include <QUuid>

#include "commands/CommandService.h"
#include "commands/ReviewCommands.h"
#include "document/ProjectDocument.h"
#include "domain/Domain.h"
#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"
#include "features/ReviewService.h"
#include "features/ReviewSession.h"
#include "services/ProjectJson.h"

using namespace lingya;

namespace {

TreeSheet& treeOf(Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

const TreeSheet& treeOf(const Project& project) {
    return std::get<TreeSheet>(project.sheets.first());
}

ID addRawNode(Project& project, const ID& parentId, int order, const QString& title,
              const QString& note = QString()) {
    TreeNode node;
    node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node.parentId = parentId;
    node.order = order;
    node.title = title;
    node.note = note;
    node.contentRevision = 1;
    treeOf(project).nodes.insert(node.id, node);
    return node.id;
}

}  // namespace

// 复习卡片命令（X01；L-017 自评写回）。所有写入经CommandService::applyEdit，
// 因此这里同时验证“失败不改数据/单步撤销/无操作不入历史”三条既有语义。
class TstReviewCommands : public QObject {
    Q_OBJECT

private slots:
    // 自评写回
    void gradeMasteredWritesStatusAndTimestamp();
    void gradeReviewedWritesReviewStatus();
    void gradeAgainChangesNothingAndSkipsHistory();
    void gradeUnknownCardReturnsNotFound();
    void gradeIsSingleUndoStep();
    // 卡片增删
    void addCardExtractsFrontAndBack();
    void addCardRejectsDuplicateNode();
    void addCardRejectsNodeWithoutContent();
    void removeCardAndUndoRestores();
    // 源变化处置
    void refreshFromSourceKeepsIdStatusAndReviewedAt();
    void keepCustomAndMakeStandaloneChangeMode();
    // 存盘往返
    void gradedCardSurvivesSaveAndReload();
};

void TstReviewCommands::gradeMasteredWritesStatusAndTimestamp() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const auto added = review.addCard(document.project().activeSheetId, rootId);
    QVERIFY(std::holds_alternative<ID>(added));
    const ID cardId = std::get<ID>(added);
    QVERIFY(!document.project().cards.first().lastReviewedAt.has_value());

    const auto graded = review.gradeCard(cardId, ReviewMark::Mastered);
    QVERIFY(std::holds_alternative<bool>(graded));
    const ReviewCard& card = document.project().cards.first();
    QCOMPARE(card.status, QStringLiteral("mastered"));
    QVERIFY(card.lastReviewedAt.has_value());
    QVERIFY(card.lastReviewedAt->isValid());
    // 时间按UTC记录（避免跨时区出现“未来时间”或时区漂移）
    QCOMPARE(card.lastReviewedAt->timeSpec(), Qt::UTC);
    QVERIFY(card.lastReviewedAt->secsTo(QDateTime::currentDateTimeUtc()) <= 5);
}

void TstReviewCommands::gradeReviewedWritesReviewStatus() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));
    QVERIFY(std::holds_alternative<bool>(review.gradeCard(cardId, ReviewMark::Reviewed)));
    QCOMPARE(document.project().cards.first().status, QStringLiteral("review"));
}

void TstReviewCommands::gradeAgainChangesNothingAndSkipsHistory() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));
    service.clearHistory();

    // Again：回队尾、状态保持 new、不写时间，且不应产生一条撤销记录
    const qint64 revisionBefore = document.sessionRevision();
    const auto again = review.gradeCard(cardId, ReviewMark::Again);
    QVERIFY(std::holds_alternative<bool>(again));
    QCOMPARE(document.project().cards.first().status, QStringLiteral("new"));
    QVERIFY(!document.project().cards.first().lastReviewedAt.has_value());
    QCOMPARE(document.sessionRevision(), revisionBefore);
    QVERIFY(!service.canUndo());
}

void TstReviewCommands::gradeUnknownCardReturnsNotFound() {
    ProjectDocument document;
    document.replaceProject(ProjectFactory::createMindmap(QStringLiteral("复习")));
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const auto graded = review.gradeCard(QStringLiteral("no-such-card"), ReviewMark::Mastered);
    QVERIFY(std::holds_alternative<AppError>(graded));
    QCOMPARE(std::get<AppError>(graded).code, QStringLiteral("NOT_FOUND"));
    QVERIFY(document.project().cards.isEmpty());
    QVERIFY(!service.canUndo());
}

void TstReviewCommands::gradeIsSingleUndoStep() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));
    service.clearHistory();

    QVERIFY(std::holds_alternative<bool>(review.gradeCard(cardId, ReviewMark::Mastered)));
    const QDateTime gradedAt = *document.project().cards.first().lastReviewedAt;
    QVERIFY(service.canUndo());

    service.undo();
    QCOMPARE(document.project().cards.first().status, QStringLiteral("new"));
    QVERIFY(!document.project().cards.first().lastReviewedAt.has_value());
    QVERIFY(!service.canUndo());

    service.redo();
    QCOMPARE(document.project().cards.first().status, QStringLiteral("mastered"));
    QCOMPARE(*document.project().cards.first().lastReviewedAt, gradedAt);
}

void TstReviewCommands::addCardExtractsFrontAndBack() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));
    addRawNode(project, rootId, 1, QStringLiteral("子主题二"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const auto added = review.addCard(document.project().activeSheetId, rootId);
    QVERIFY(std::holds_alternative<ID>(added));

    const ReviewCard& card = document.project().cards.first();
    QCOMPARE(card.id, std::get<ID>(added));
    QCOMPARE(card.sheetId, document.project().activeSheetId);
    QCOMPARE(card.nodeId, rootId);
    QCOMPARE(card.front, QStringLiteral("中心主题"));
    // 备注为空时背面取直属子节点按order拼接
    QCOMPARE(card.back, QStringLiteral("子主题一\n子主题二"));
    QCOMPARE(card.status, QStringLiteral("new"));
    QCOMPARE(card.mode, QStringLiteral("linked"));
    QCOMPARE(card.sourceState, QStringLiteral("ok"));
    QVERIFY(!card.sourceHash.isEmpty());
    QCOMPARE(ReviewService::detectSourceState(card, document.project()), QStringLiteral("ok"));
}

void TstReviewCommands::addCardRejectsDuplicateNode() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);
    const ID sheetId = document.project().activeSheetId;

    QVERIFY(std::holds_alternative<ID>(review.addCard(sheetId, rootId)));
    const auto again = review.addCard(sheetId, rootId);
    QVERIFY(std::holds_alternative<AppError>(again));
    QCOMPARE(std::get<AppError>(again).code, QStringLiteral("CARD_EXISTS"));
    QCOMPARE(document.project().cards.size(), 1);
}

void TstReviewCommands::addCardRejectsNodeWithoutContent() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID leafId = addRawNode(project, treeOf(project).rootId, 0, QStringLiteral("叶子"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    // 叶子节点：无备注、无子节点 → 背面为空，不生成空卡
    const auto added = review.addCard(document.project().activeSheetId, leafId);
    QVERIFY(std::holds_alternative<AppError>(added));
    QCOMPARE(std::get<AppError>(added).code, QStringLiteral("NO_CONTENT"));
    QVERIFY(document.project().cards.isEmpty());
}

void TstReviewCommands::removeCardAndUndoRestores() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));
    service.clearHistory();

    QVERIFY(std::holds_alternative<bool>(review.removeCard(cardId)));
    QVERIFY(document.project().cards.isEmpty());
    service.undo();
    QCOMPARE(document.project().cards.size(), 1);
    QCOMPARE(document.project().cards.first().id, cardId);

    const auto missing = review.removeCard(QStringLiteral("no-such-card"));
    QVERIFY(std::holds_alternative<AppError>(missing));
    QCOMPARE(std::get<AppError>(missing).code, QStringLiteral("NOT_FOUND"));
}

void TstReviewCommands::refreshFromSourceKeepsIdStatusAndReviewedAt() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));
    QVERIFY(std::holds_alternative<bool>(review.gradeCard(cardId, ReviewMark::Mastered)));
    const QDateTime reviewedAt = *document.project().cards.first().lastReviewedAt;

    // 改源节点标题后，卡片应判定为changed
    Project changed = document.project();
    TreeNode& root = treeOf(changed).nodes[rootId];
    root.title = QStringLiteral("中心主题（改）");
    document.replaceProject(changed);
    QCOMPARE(ReviewService::detectSourceState(document.project().cards.first(), document.project()),
             QStringLiteral("changed"));

    QVERIFY(std::holds_alternative<bool>(review.refreshFromSource(cardId)));
    const ReviewCard& card = document.project().cards.first();
    QCOMPARE(card.id, cardId);                                   // ID保留
    QCOMPARE(card.front, QStringLiteral("中心主题（改）"));        // 正反面跟随源
    QCOMPARE(card.status, QStringLiteral("mastered"));            // 学习状态保留
    QVERIFY(card.lastReviewedAt.has_value());                     // BUG-017：最近复习时间不能丢
    QCOMPARE(*card.lastReviewedAt, reviewedAt);
    QCOMPARE(card.sourceState, QStringLiteral("ok"));
    QCOMPARE(ReviewService::detectSourceState(card, document.project()), QStringLiteral("ok"));
}

void TstReviewCommands::keepCustomAndMakeStandaloneChangeMode() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));

    QVERIFY(std::holds_alternative<bool>(review.keepCustom(cardId)));
    QCOMPARE(document.project().cards.first().mode, QStringLiteral("custom"));
    QCOMPARE(document.project().cards.first().sourceState, QStringLiteral("ok"));

    service.undo();
    QCOMPARE(document.project().cards.first().mode, QStringLiteral("linked"));

    QVERIFY(std::holds_alternative<bool>(review.makeStandalone(cardId)));
    QCOMPARE(document.project().cards.first().mode, QStringLiteral("custom"));
    QCOMPARE(document.project().cards.first().sourceState, QStringLiteral("missing"));

    // 未知卡片：三条处置命令统一返回NOT_FOUND
    const auto unknown = review.makeStandalone(QStringLiteral("no-such-card"));
    QVERIFY(std::holds_alternative<AppError>(unknown));
    QCOMPARE(std::get<AppError>(unknown).code, QStringLiteral("NOT_FOUND"));
}

void TstReviewCommands::gradedCardSurvivesSaveAndReload() {
    Project project = ProjectFactory::createMindmap(QStringLiteral("复习往返"));
    const ID rootId = treeOf(project).rootId;
    addRawNode(project, rootId, 0, QStringLiteral("子主题一"));

    ProjectDocument document;
    document.replaceProject(project);
    CommandService service(&document);
    ReviewCommands review(&service, &document);

    const ID cardId = std::get<ID>(review.addCard(document.project().activeSheetId, rootId));
    QVERIFY(std::holds_alternative<bool>(review.gradeCard(cardId, ReviewMark::Mastered)));
    const QDateTime reviewedAt = *document.project().cards.first().lastReviewedAt;

    const QByteArray bytes = ProjectJson::toBytes(document.project());
    const auto reloaded = ProjectJson::fromBytes(bytes);
    QVERIFY(std::holds_alternative<Project>(reloaded));
    const Project& loaded = std::get<Project>(reloaded);
    QVERIFY(ProjectValidator::validate(loaded).isEmpty());

    QCOMPARE(loaded.cards.size(), 1);
    const ReviewCard& card = loaded.cards.first();
    QCOMPARE(card.status, QStringLiteral("mastered"));
    QVERIFY(card.lastReviewedAt.has_value());
    QCOMPARE(card.lastReviewedAt->toUTC(), reviewedAt);
}

QTEST_APPLESS_MAIN(TstReviewCommands)
#include "tst_reviewcommands.moc"
