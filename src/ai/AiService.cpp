#include "ai/AiService.h"

#include "ai/AiValidator.h"
#include "ai/IAiProvider.h"
#include "ai/PromptLibrary.h"
#include "ai/SecretStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace lingya {

namespace {

constexpr char kModel[] = "deepseek-flash";
constexpr int kMaxTokens = 4096;

QString extractContent(const QJsonObject& response) {
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        return QString();
    }
    return choices.first()
        .toObject()
        .value(QStringLiteral("message"))
        .toObject()
        .value(QStringLiteral("content"))
        .toString();
}

GeneratedNode parseNode(const QJsonObject& object) {
    GeneratedNode node;
    node.title = object.value(QStringLiteral("title")).toString();
    node.note = object.value(QStringLiteral("note")).toString();
    node.origin = object.value(QStringLiteral("origin")).toString(QStringLiteral("source"));
    for (const QJsonValue& value : object.value(QStringLiteral("sourceParagraphIds")).toArray()) {
        node.sourceParagraphIds << value.toString();
    }
    for (const QJsonValue& value : object.value(QStringLiteral("children")).toArray()) {
        node.children.append(parseNode(value.toObject()));
    }
    return node;
}

MapProposal parseMapProposal(const QJsonObject& object) {
    MapProposal proposal;
    proposal.title = object.value(QStringLiteral("title")).toString();
    proposal.root = parseNode(object.value(QStringLiteral("root")).toObject());
    for (const QJsonValue& value : object.value(QStringLiteral("warnings")).toArray()) {
        proposal.warnings << value.toString();
    }
    return proposal;
}

DraftProposal parseDraftProposal(const QJsonObject& object) {
    DraftProposal proposal;
    proposal.title = object.value(QStringLiteral("title")).toString();
    for (const QJsonValue& value : object.value(QStringLiteral("sections")).toArray()) {
        const QJsonObject sectionObject = value.toObject();
        DraftSection section;
        section.heading = sectionObject.value(QStringLiteral("heading")).toString();
        section.text = sectionObject.value(QStringLiteral("text")).toString();
        section.origin =
            sectionObject.value(QStringLiteral("origin")).toString(QStringLiteral("source"));
        for (const QJsonValue& id : sectionObject.value(QStringLiteral("sourceNodeIds")).toArray()) {
            section.sourceNodeIds << id.toString();
        }
        proposal.sections.append(section);
    }
    for (const QJsonValue& value : object.value(QStringLiteral("warnings")).toArray()) {
        proposal.warnings << value.toString();
    }
    return proposal;
}

QJsonObject buildMapBody(const GenerateMapRequest& request) {
    const QJsonObject system{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), PromptLibrary::mapPrompt()}};
    const QJsonObject user{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"),
         QStringLiteral("模式%1；maxNodes=%2；maxDepth=%3。材料：%4")
             .arg(request.mode)
             .arg(request.maxNodes)
             .arg(request.maxDepth)
             .arg(request.text)}};
    return QJsonObject{
        {QStringLiteral("model"), QString::fromLatin1(kModel)},
        {QStringLiteral("messages"), QJsonArray{system, user}},
        {QStringLiteral("max_tokens"), kMaxTokens},
        {QStringLiteral("response_format"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}}};
}

QJsonObject buildDraftBody(const GenerateDraftRequest& request) {
    QStringList lines;
    for (const DraftSourceNode& node : request.source) {
        lines << QStringLiteral("%1：%2").arg(node.title, node.note);
    }
    const QJsonObject system{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), PromptLibrary::draftPrompt()}};
    const QJsonObject user{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"),
         QStringLiteral("purpose=%1；tone=%2；targetChars=%3；模式%4。来源：%5")
             .arg(request.purpose, request.tone)
             .arg(request.targetChars)
             .arg(request.mode)
             .arg(lines.join(QStringLiteral("；")))}};
    return QJsonObject{
        {QStringLiteral("model"), QString::fromLatin1(kModel)},
        {QStringLiteral("messages"), QJsonArray{system, user}},
        {QStringLiteral("max_tokens"), kMaxTokens},
        {QStringLiteral("response_format"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}}};
}

QJsonObject buildConnectionBody() {
    const QJsonObject user{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QStringLiteral("ping")}};
    return QJsonObject{
        {QStringLiteral("model"), QString::fromLatin1(kModel)},
        {QStringLiteral("messages"), QJsonArray{user}},
        {QStringLiteral("max_tokens"), 8}};
}

}  // namespace

AiService::AiService(IAiProvider* provider, SecretStore* secretStore, QObject* parent)
    : QObject(parent), m_provider(provider), m_secretStore(secretStore) {
    if (m_provider != nullptr) {
        connect(m_provider, &IAiProvider::completed, this, &AiService::onProviderCompleted);
        connect(m_provider, &IAiProvider::failed, this, &AiService::onProviderFailed);
    }
}

AiStatus AiService::status() const {
    AiStatus result;
    result.configured = m_secretStore != nullptr && !m_secretStore->apiKey().isEmpty();
    result.busy = busy();
    result.model = QString::fromLatin1(kModel);
    return result;
}

bool AiService::setKey(const QString& key, bool remember) {
    if (m_secretStore == nullptr) {
        return false;
    }
    return m_secretStore->setApiKey(key, remember);
}

void AiService::clearKey() {
    if (m_secretStore != nullptr) {
        m_secretStore->clearApiKey();
    }
}

bool AiService::generateMap(const GenerateMapRequest& request) {
    if (!beginRequest(request.requestId, RequestKind::Map)) {
        return false;
    }
    if (!ensureKey(request.requestId)) {
        return false;
    }
    setState(State::Requesting);
    m_provider->start(request.requestId, buildMapBody(request));
    return true;
}

bool AiService::generateDraft(const GenerateDraftRequest& request) {
    if (!beginRequest(request.requestId, RequestKind::Draft)) {
        return false;
    }
    if (!ensureKey(request.requestId)) {
        return false;
    }
    m_draftSource = request.source;
    setState(State::Requesting);
    m_provider->start(request.requestId, buildDraftBody(request));
    return true;
}

bool AiService::testConnection(const QString& requestId) {
    if (!beginRequest(requestId, RequestKind::Connection)) {
        return false;
    }
    if (!ensureKey(requestId)) {
        return false;
    }
    setState(State::Requesting);
    m_provider->start(requestId, buildConnectionBody());
    return true;
}

void AiService::cancel(const QString& requestId) {
    if (m_provider != nullptr) {
        m_provider->cancel(requestId);
    }
    if (requestId == m_currentRequestId) {
        setState(State::Canceled);  // stateChanged(requestId, Canceled)
        m_currentRequestId.clear();
        emit canceled(requestId);
    }
}

AiService::State AiService::state() const {
    return m_state;
}

const MapProposal& AiService::lastMapProposal() const {
    return m_lastMap;
}

const DraftProposal& AiService::lastDraftProposal() const {
    return m_lastDraft;
}

void AiService::onProviderCompleted(const QString& requestId, const QJsonObject& response) {
    if (requestId != m_currentRequestId) {
        return;  // 迟到响应丢弃
    }
    setState(State::ValidatingResponse);
    switch (m_kind) {
    case RequestKind::Map:
        handleMapResponse(response, requestId);
        break;
    case RequestKind::Draft:
        handleDraftResponse(response, requestId);
        break;
    case RequestKind::Connection:
        handleConnectionResponse(response, requestId);
        break;
    }
}

void AiService::onProviderFailed(const AiError& error) {
    if (!m_currentRequestId.isEmpty() && error.requestId != m_currentRequestId) {
        return;
    }
    setState(State::Error);
    m_currentRequestId.clear();
    emit failed(error);
}

void AiService::handleMapResponse(const QJsonObject& response, const QString& requestId) {
    const QString content = extractContent(response);
    if (content.trimmed().isEmpty()) {
        fail(QStringLiteral("EMPTY_OUTPUT"), QStringLiteral("AI返回为空"), requestId);
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("SCHEMA_INVALID"), QStringLiteral("AI返回不是合法JSON对象"), requestId);
        return;
    }
    const MapProposal proposal = parseMapProposal(document.object());
    const AiValidationResult validation = AiValidator::validateMapProposal(proposal);
    if (!validation.ok) {
        fail(validation.code, validation.message, requestId);
        return;
    }
    m_lastMap = proposal;
    setState(State::Preview);  // stateChanged(requestId, Preview)
    m_currentRequestId.clear();
    emit mapReady(requestId, proposal);
}

void AiService::handleDraftResponse(const QJsonObject& response, const QString& requestId) {
    const QString content = extractContent(response);
    if (content.trimmed().isEmpty()) {
        fail(QStringLiteral("EMPTY_OUTPUT"), QStringLiteral("AI返回为空"), requestId);
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("SCHEMA_INVALID"), QStringLiteral("AI返回不是合法JSON对象"), requestId);
        return;
    }
    const DraftProposal proposal = parseDraftProposal(document.object());
    const AiValidationResult validation = AiValidator::validateDraftProposal(proposal, m_draftSource);
    if (!validation.ok) {
        fail(validation.code, validation.message, requestId);
        return;
    }
    m_lastDraft = proposal;
    setState(State::Preview);
    m_currentRequestId.clear();
    emit draftReady(requestId, proposal);
}

void AiService::handleConnectionResponse(const QJsonObject& response, const QString& requestId) {
    const QString model =
        response.value(QStringLiteral("model")).toString(QString::fromLatin1(kModel));
    setState(State::Done);
    m_currentRequestId.clear();
    emit connectionReady(requestId, model);
}

void AiService::fail(const QString& code, const QString& message, const QString& requestId) {
    setState(State::Error);  // stateChanged(requestId, Error)
    m_currentRequestId.clear();
    emit failed(AiError{code, message, requestId, false});
}

void AiService::setState(State state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_currentRequestId, state);
}

bool AiService::busy() const {
    return m_state == State::Validating || m_state == State::Requesting ||
           m_state == State::ValidatingResponse || m_state == State::Applying;
}

bool AiService::beginRequest(const QString& requestId, RequestKind kind) {
    if (requestId.isEmpty() || busy() || requestId == m_currentRequestId) {
        return false;  // 非法 / 并发 / 同一 requestId 重复
    }
    m_currentRequestId = requestId;
    m_kind = kind;
    setState(State::Validating);
    return true;
}

bool AiService::ensureKey(const QString& requestId) {
    if (m_secretStore != nullptr && !m_secretStore->apiKey().isEmpty()) {
        return true;
    }
    fail(QStringLiteral("KEY_MISSING"), QStringLiteral("未配置密钥"), requestId);
    return false;
}

}  // namespace lingya
