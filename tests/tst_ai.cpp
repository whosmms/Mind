#include <QtTest>

#include "ai/AiService.h"
#include "ai/AiTypes.h"
#include "ai/AiValidator.h"
#include "ai/FakeAiProvider.h"
#include "ai/SecretStore.h"

using namespace lingya;

// AI 响应校验与状态机测试（AI规范§10.6、§10.4）：
// 结构/数量/深度/origin/来源引用；信号契约与 BUSY/取消/KEY_MISSING。
class TstAi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void acceptsValidMap();
    void rejectsEmptyTitle();
    void rejectsTooManyNodes();
    void rejectsTooDeep();
    void rejectsBadOrigin();
    void acceptsValidDraft();
    void rejectsDraftWithoutSections();
    void rejectsUnknownSourceNode();
    void rejectsEmptySection();
    void serviceRequiresKey();
    void serviceEmitsProposalOnSuccess();
    void serviceRejectsConcurrentRequest();
    void serviceCancelMovesToCanceled();
    void serviceEmitsErrorOnProviderFailure();
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

void TstAi::initTestCase() {
    registerAiMetaTypes();
}

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
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(result.message.contains(QStringLiteral("标题为空")));
}

void TstAi::rejectsTooManyNodes() {
    GeneratedNode root = makeNode(QStringLiteral("根"));
    for (int i = 0; i < 60; ++i) {
        root.children.append(makeNode(QStringLiteral("c%1").arg(i)));
    }
    const auto result = AiValidator::validateMapProposal(makeMap(root), 50, 4);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(result.message.contains(QStringLiteral("节点数")));
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
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(result.message.contains(QStringLiteral("深度")));
}

void TstAi::rejectsBadOrigin() {
    const auto result = AiValidator::validateMapProposal(
        makeMap(makeNode(QStringLiteral("根"), QStringLiteral("inferred"))));
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(result.message.contains(QStringLiteral("origin")));
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
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(result.message.contains(QStringLiteral("来源节点")));
}

void TstAi::rejectsEmptySection() {
    DraftProposal proposal;
    proposal.title = QStringLiteral("稿件");
    proposal.sections = {DraftSection{}};
    const auto result = AiValidator::validateDraftProposal(proposal, {});
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("SCHEMA_INVALID"));
    QVERIFY(!result.message.isEmpty());
}

void TstAi::serviceRequiresKey() {
    FakeAiProvider provider;
    SessionSecretStore store;
    AiService service(&provider, &store);
    QSignalSpy failedSpy(&service, &AiService::failed);

    GenerateMapRequest request;
    request.requestId = QStringLiteral("req-nokey");
    request.text = QStringLiteral("材料");
    QVERIFY(!service.generateMap(request));   // 无密钥 → 失败
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<AiError>().code, QStringLiteral("KEY_MISSING"));
    QCOMPARE(service.state(), AiService::State::Error);
}

void TstAi::serviceEmitsProposalOnSuccess() {
    FakeAiProvider provider;
    provider.setLatencyMs(5);
    SessionSecretStore store;
    store.setApiKey(QStringLiteral("sk-test"), false);
    AiService service(&provider, &store);
    QSignalSpy readySpy(&service, &AiService::mapReady);

    GenerateMapRequest request;
    request.requestId = QStringLiteral("req-ok");
    request.text = QStringLiteral("软件工程包括需求分析、设计、编码和测试。");
    QVERIFY(service.generateMap(request));
    QVERIFY(readySpy.wait(2000));
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.first().at(0).toString(), QStringLiteral("req-ok"));   // 携带 requestId
    QCOMPARE(service.state(), AiService::State::Preview);
    QVERIFY(!service.lastMapProposal().root.title.isEmpty());
}

void TstAi::serviceRejectsConcurrentRequest() {
    FakeAiProvider provider;
    provider.setLatencyMs(50);
    SessionSecretStore store;
    store.setApiKey(QStringLiteral("sk-test"), false);
    AiService service(&provider, &store);

    GenerateMapRequest first;
    first.requestId = QStringLiteral("req-a");
    first.text = QStringLiteral("材料");
    QVERIFY(service.generateMap(first));

    GenerateMapRequest second;
    second.requestId = QStringLiteral("req-b");
    second.text = QStringLiteral("材料");
    QVERIFY(!service.generateMap(second));   // BUSY
}

void TstAi::serviceCancelMovesToCanceled() {
    FakeAiProvider provider;
    provider.setLatencyMs(200);
    SessionSecretStore store;
    store.setApiKey(QStringLiteral("sk-test"), false);
    AiService service(&provider, &store);
    QSignalSpy cancelSpy(&service, &AiService::canceled);

    GenerateMapRequest request;
    request.requestId = QStringLiteral("req-cancel");
    request.text = QStringLiteral("材料");
    QVERIFY(service.generateMap(request));
    service.cancel(QStringLiteral("req-cancel"));
    QCOMPARE(service.state(), AiService::State::Canceled);
    QCOMPARE(cancelSpy.count(), 1);   // 取消发终态信号
    QCOMPARE(cancelSpy.first().at(0).toString(), QStringLiteral("req-cancel"));
}

void TstAi::serviceEmitsErrorOnProviderFailure() {
    FakeAiProvider provider;
    provider.setLatencyMs(5);
    provider.setFailureMode(FakeAiProvider::FailureMode::AuthFailed);
    SessionSecretStore store;
    store.setApiKey(QStringLiteral("sk-test"), false);
    AiService service(&provider, &store);
    QSignalSpy failedSpy(&service, &AiService::failed);

    GenerateMapRequest request;
    request.requestId = QStringLiteral("req-authfail");
    request.text = QStringLiteral("材料");
    QVERIFY(service.generateMap(request));
    QVERIFY(failedSpy.wait(2000));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).value<AiError>().code, QStringLiteral("AUTH_FAILED"));
    QCOMPARE(service.state(), AiService::State::Error);
}

QTEST_GUILESS_MAIN(TstAi)
#include "tst_ai.moc"
