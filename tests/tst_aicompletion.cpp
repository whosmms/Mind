#include "ai/AiMaterial.h"
#include "ai/AiService.h"
#include "ai/IAiProvider.h"
#include "ai/SecretStore.h"
#include "commands/AiCommands.h"
#include "commands/CommandService.h"
#include "commands/DraftCommands.h"
#include "document/ProjectDocument.h"
#include "domain/ProjectFactory.h"
#include "services/FileService.h"
#include "services/ProjectJson.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace lingya;
class CaptureProvider : public IAiProvider
{
  public:
    CaptureProvider() : IAiProvider(nullptr) {}
    int starts = 0;
    QString requestId;
    QJsonObject body;
    void start(const QString &id, const QJsonObject &request) override
    {
        ++starts;
        requestId = id;
        body = request;
    }
    void cancel(const QString &) override {}
    void reply(const QJsonObject &content)
    {
        const QJsonObject message{
            {"content", QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}};
        const QJsonArray choices{QJsonObject{{"message", message}}};
        emit completed(requestId, QJsonObject{{"model", "test-only"},
                                              {"choices", choices},
                                              {"usage", QJsonObject{{"prompt_tokens", 12},
                                                                    {"completion_tokens", 5},
                                                                    {"total_tokens", 17}}}});
    }
};
class TstAiCompletion : public QObject
{
    Q_OBJECT
  private slots:
    void mapMaterialIdsAndRequirements();
    void draftRequestCarriesStructuredSources();
    void rejectsInputBeforeProvider();
    void responseHonorsRequestedLimitsAndSources();
    void draftEditingKeepsProvenance();
    void persistentSourceHashAndLegacyFiles();
};
void TstAiCompletion::mapMaterialIdsAndRequirements()
{
    CaptureProvider provider;
    SessionSecretStore store;
    store.setApiKey("test-only", false);
    AiService service(&provider, &store);
    GenerateMapRequest request;
    request.requestId = "map";
    request.text = QStringLiteral("甲段\n\n乙段 \"数据\"");
    request.requirements = QStringLiteral("保留两段来源");
    QVERIFY(service.generateMap(request));
    QCOMPARE(provider.starts, 1);
    const auto text = provider.body["messages"].toArray()[1].toObject()["content"].toString();
    QVERIFY(text.contains("\"id\":\"p1\""));
    QVERIFY(text.contains("\"id\":\"p2\""));
    QVERIFY(text.contains(request.requirements));
    service.cancel(request.requestId);
}
void TstAiCompletion::draftRequestCarriesStructuredSources()
{
    const auto project = ProjectFactory::createFishbone("来源");
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    const auto built = AiMaterial::fromTree(project, tree.id);
    QVERIFY(std::holds_alternative<GenerateDraftRequest>(built));
    auto request = std::get<GenerateDraftRequest>(built);
    QCOMPARE(request.source.size(), tree.nodes.size());
    QCOMPARE(request.source.first().id, tree.rootId);
    QVERIFY(!request.source.first().parentId);
    request.requestId = "draft";
    request.requirements = "面向初学者";
    CaptureProvider provider;
    SessionSecretStore store;
    store.setApiKey("test-only", false);
    AiService service(&provider, &store);
    QVERIFY(service.generateDraft(request));
    const auto text = provider.body["messages"].toArray()[1].toObject()["content"].toString();
    for (const auto &source : request.source)
        QVERIFY(text.contains(source.id));
    QVERIFY(text.contains("parentId"));
    QVERIFY(text.contains("order"));
    QVERIFY(text.contains(request.requirements));
    const auto &firstChild = request.source[1];
    const auto sub = AiMaterial::fromTree(project, tree.id, firstChild.id);
    QVERIFY(std::holds_alternative<GenerateDraftRequest>(sub));
    QCOMPARE(std::get<GenerateDraftRequest>(sub).source.size(), 1);
    QVERIFY(!std::get<GenerateDraftRequest>(sub).source.first().parentId);
    service.cancel(request.requestId);
}
void TstAiCompletion::rejectsInputBeforeProvider()
{
    CaptureProvider provider;
    SessionSecretStore store;
    store.setApiKey("test-only", false);
    AiService service(&provider, &store);
    QSignalSpy errors(&service, &AiService::failed);
    GenerateMapRequest request;
    request.requestId = "too-long";
    request.text = QString(12001, QChar(0x4E2D));
    QVERIFY(!service.generateMap(request));
    QCOMPARE(provider.starts, 0);
    QCOMPARE(errors.last().first().value<AiError>().code, "LIMIT_EXCEEDED");
    request.requestId = "too-many";
    request.text = "材料";
    request.maxNodes = 51;
    QVERIFY(!service.generateMap(request));
    QCOMPARE(provider.starts, 0);
    GenerateDraftRequest draft;
    draft.requestId = "empty-source";
    QVERIFY(!service.generateDraft(draft));
    QCOMPARE(provider.starts, 0);
    for (int i = 0; i < 101; ++i)
        draft.source.append({QString::number(i), std::nullopt, i, "来源", {}});
    draft.requestId = "too-many-source";
    QVERIFY(!service.generateDraft(draft));
    QCOMPARE(provider.starts, 0);
    draft.source.resize(1);
    draft.source[0].note = QString(20001, QChar(0x4E2D));
    draft.requestId = "too-long-source";
    QVERIFY(!service.generateDraft(draft));
    QCOMPARE(provider.starts, 0);
}
void TstAiCompletion::responseHonorsRequestedLimitsAndSources()
{
    CaptureProvider provider;
    SessionSecretStore store;
    store.setApiKey("test-only", false);
    AiService service(&provider, &store);
    QSignalSpy errors(&service, &AiService::failed), ready(&service, &AiService::mapReady);
    GenerateMapRequest request;
    request.requestId = "one";
    request.text = "材料";
    request.maxNodes = 1;
    QVERIFY(service.generateMap(request));
    const auto node = [](QString title, QString source)
    {
        return QJsonObject{{"title", title},
                           {"note", ""},
                           {"origin", "source"},
                           {"sourceParagraphIds", QJsonArray{source}},
                           {"children", QJsonArray{}}};
    };
    auto root = node("根", "p1");
    root["children"] = QJsonArray{node("超额子主题", "p1")};
    provider.reply({{"title", "结果"}, {"root", root}, {"warnings", QJsonArray{}}});
    QCOMPARE(ready.count(), 0);
    QCOMPARE(errors.count(), 1);
    request.requestId = "source";
    QVERIFY(service.generateMap(request));
    provider.reply(
        {{"title", "结果"}, {"root", node("根", "不存在的段落")}, {"warnings", QJsonArray{}}});
    QCOMPARE(ready.count(), 0);
    QCOMPARE(errors.count(), 2);
    request.requestId = "valid";
    QVERIFY(service.generateMap(request));
    provider.reply({{"title", "结果"}, {"root", node("根", "p1")}, {"warnings", QJsonArray{}}});
    QCOMPARE(ready.count(), 1);
    QCOMPARE(service.lastMapProposal().model, QStringLiteral("test-only"));
    QCOMPARE(service.lastMapProposal().usage.inputTokens, qint64(12));
    QCOMPARE(service.lastMapProposal().usage.outputTokens, qint64(5));
    QCOMPARE(service.lastMapProposal().usage.totalTokens, qint64(17));
}
void TstAiCompletion::draftEditingKeepsProvenance()
{
    auto project = ProjectFactory::createMindmap("文稿来源");
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    TextDraft draft;
    draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    draft.sourceSheetId = tree.id;
    draft.title = "原稿";
    draft.sourceRevision = 7;
    draft.purpose = "speech";
    draft.createdAt = QDateTime::currentDateTimeUtc();
    draft.sections.append({"原段", "原文", {tree.rootId}, "source"});
    project.drafts.append(draft);
    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService commands(&doc);
    DraftCommands drafts(&commands, &doc);
    QSignalSpy changed(&doc, &ProjectDocument::changed);
    const auto initial = ProjectJson::toJson(doc.project());
    QVERIFY(std::holds_alternative<bool>(drafts.updateText(draft.id, "新稿", {{"新段", "新文"}})));
    QCOMPARE(changed.count(), 1);
    const auto updated = doc.project().drafts.first();
    QCOMPARE(updated.sourceRevision, draft.sourceRevision);
    QCOMPARE(updated.sections.first().sourceNodeIds, draft.sections.first().sourceNodeIds);
    QCOMPARE(updated.createdAt, draft.createdAt);
    QCOMPARE(updated.purpose, draft.purpose);
    commands.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), initial);
    commands.redo();
    const auto saved = ProjectJson::toJson(doc.project());
    changed.clear();
    QVERIFY(std::holds_alternative<bool>(drafts.updateText(draft.id, "新稿", {{"新段", "新文"}})));
    QCOMPARE(changed.count(), 0);
    QVERIFY(std::holds_alternative<AppError>(drafts.updateText(draft.id, "新稿", {{"", ""}})));
    QCOMPARE(ProjectJson::toJson(doc.project()), saved);
    QTemporaryDir dir;
    const QString path = dir.filePath("稿件.lingya");
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, doc.project())));
    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(loaded));
    QCOMPARE(std::get<Project>(loaded).drafts.first().sections.first().text, "新文");
    QVERIFY(std::holds_alternative<bool>(drafts.remove(draft.id)));
    QVERIFY(doc.project().drafts.isEmpty());
    commands.undo();
    QCOMPARE(ProjectJson::toJson(doc.project()), saved);
}
void TstAiCompletion::persistentSourceHashAndLegacyFiles()
{
    auto project = ProjectFactory::createMindmap(QStringLiteral("稳定来源"));
    ProjectDocument doc;
    doc.replaceProject(project);
    CommandService commands(&doc);
    const auto tree = std::get<TreeSheet>(project.sheets.first());
    DraftProposal proposal;
    proposal.title = QStringLiteral("稿件");
    proposal.sections.append(
        {QStringLiteral("段落"), QStringLiteral("正文"), {tree.rootId}, "source"});
    AiApplyOptions options;
    options.expectedRevision = doc.sessionRevision();
    QVERIFY(std::holds_alternative<ID>(
        AiCommands(&commands, &doc)
            .saveDraft(proposal, tree.id, "summary", doc.sessionRevision(), options)));
    const auto saved = doc.project();
    const auto hash = saved.drafts.first().sourceHash;
    QVERIFY(hash.has_value());
    QCOMPARE(*hash, AiMaterial::treeHash(tree));
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("带来源指纹.lingya"));
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, saved)));
    const auto loaded = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(loaded));
    auto reopened = std::get<Project>(loaded);
    QCOMPARE(reopened.drafts.first().sourceHash, hash);
    auto &currentTree = std::get<TreeSheet>(reopened.sheets.first());
    currentTree.viewport.zoom = 1.5;
    currentTree.nodes[currentTree.rootId].collapsed = true;
    QCOMPARE(AiMaterial::treeHash(currentTree), *hash);
    currentTree.nodes[currentTree.rootId].title = QStringLiteral("修改来源");
    QVERIFY(AiMaterial::treeHash(currentTree) != *hash);
    auto json = ProjectJson::toJson(saved);
    auto drafts = json["drafts"].toArray();
    auto legacy = drafts[0].toObject();
    legacy.remove("sourceHash");
    drafts[0] = legacy;
    json["drafts"] = drafts;
    const auto old = ProjectJson::fromObject(json);
    QVERIFY(std::holds_alternative<Project>(old));
    QVERIFY(!std::get<Project>(old).drafts.first().sourceHash);
    legacy["sourceHash"] = 123;
    drafts[0] = legacy;
    json["drafts"] = drafts;
    QVERIFY(std::holds_alternative<AppError>(ProjectJson::fromObject(json)));
    legacy["sourceHash"] = "invalid";
    drafts[0] = legacy;
    json["drafts"] = drafts;
    QVERIFY(std::holds_alternative<AppError>(ProjectJson::fromObject(json)));
}

QTEST_MAIN(TstAiCompletion)
#include "tst_aicompletion.moc"
