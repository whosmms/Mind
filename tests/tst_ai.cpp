#include <QtTest>

#include "ai/AiTypes.h"
#include "ai/AiValidator.h"

using namespace lingya;

// AI 响应校验测试（AI规范§10.6）：结构、数量、深度、origin 枚举与来源引用。
class TstAi : public QObject {
    Q_OBJECT

private slots:
    void acceptsValidMap();
    void rejectsEmptyTitle();
    void rejectsTooManyNodes();
    void rejectsTooDeep();
    void rejectsBadOrigin();
    void acceptsValidDraft();
    void rejectsDraftWithoutSections();
    void rejectsUnknownSourceNode();
    void rejectsEmptySection();
};

namespace {

GeneratedNode makeNode(const QString& title, const QString& origin = QStringLiteral("source")) {
    GeneratedNode node;
    node.title = title;
    node.origin = origin;
    return node;
}

MapProposal makeMap(const GeneratedNode& root) {
    MapProposal proposal;
    proposal.title = QStringLiteral("测试导图");
    proposal.root = root;
    return proposal;
}

DraftSourceNode makeSourceNode(const ID& id) {
    DraftSourceNode node;
    node.id = id;
    node.title = QStringLiteral("来源");
    return node;
}

}  // namespace

void TstAi::acceptsValidMap() {
    GeneratedNode root = makeNode(QStringLiteral("根"));
    root.children = {makeNode(QStringLiteral("子1")), makeNode(QStringLiteral("子2"))};
    const auto result = AiValidator::validateMapProposal(makeMap(root));
    QVERIFY(result.ok);
    QVERIFY(result.code.isEmpty());
}

void TstAi::rejectsEmptyTitle() {
    const auto result = AiValidator::validateMapProposal(makeMap(makeNode(QString())));
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("EMPTY_TITLE"));
}

void TstAi::rejectsTooManyNodes() {
    GeneratedNode root = makeNode(QStringLiteral("根"));
    for (int i = 0; i < 60; ++i) {
        root.children.append(makeNode(QStringLiteral("c%1").arg(i)));
    }
    const auto result = AiValidator::validateMapProposal(makeMap(root), 50, 4);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("TOO_MANY_NODES"));
}

void TstAi::rejectsTooDeep() {
    GeneratedNode root = makeNode(QStringLiteral("根"));
    GeneratedNode* current = &root;
    for (int i = 0; i < 6; ++i) {
        current->children.append(makeNode(QStringLiteral("c%1").arg(i)));
        current = &current->children.last();
    }
    const auto result = AiValidator::validateMapProposal(makeMap(root), 50, 4);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("TOO_DEEP"));
}

void TstAi::rejectsBadOrigin() {
    const auto result = AiValidator::validateMapProposal(
        makeMap(makeNode(QStringLiteral("根"), QStringLiteral("inferred"))));
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("BAD_ORIGIN"));
}

void TstAi::acceptsValidDraft() {
    DraftProposal proposal;
    proposal.title = QStringLiteral("稿件");
    DraftSection section;
    section.heading = QStringLiteral("一");
    section.text = QStringLiteral("正文");
    section.sourceNodeIds = {QStringLiteral("n1")};
    proposal.sections = {section};

    const auto result =
        AiValidator::validateDraftProposal(proposal, {makeSourceNode(QStringLiteral("n1"))});
    QVERIFY(result.ok);
}

void TstAi::rejectsDraftWithoutSections() {
    DraftProposal proposal;
    proposal.title = QStringLiteral("稿件");
    const auto result = AiValidator::validateDraftProposal(proposal, {});
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
}

void TstAi::rejectsUnknownSourceNode() {
    DraftProposal proposal;
    proposal.title = QStringLiteral("稿件");
    DraftSection section;
    section.text = QStringLiteral("正文");
    section.sourceNodeIds = {QStringLiteral("nonexistent")};
    proposal.sections = {section};

    const auto result = AiValidator::validateDraftProposal(proposal, {});
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("UNKNOWN_SOURCE"));
}

void TstAi::rejectsEmptySection() {
    DraftProposal proposal;
    proposal.title = QStringLiteral("稿件");
    proposal.sections = {DraftSection{}};
    const auto result = AiValidator::validateDraftProposal(proposal, {});
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("EMPTY_SECTION"));
}

QTEST_GUILESS_MAIN(TstAi)
#include "tst_ai.moc"
