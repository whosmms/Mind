#include "ai/AiService.h"

#include "ai/AiMaterial.h"
#include "ai/AiValidator.h"
#include "ai/IAiProvider.h"
#include "ai/PromptLibrary.h"
#include "ai/SecretStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

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

AiUsage responseUsage(const QJsonObject &response)
{
    const auto usage = response.value("usage").toObject();
    const auto count = [&](const QString &key)
    {
        const auto value = usage.value(key);
        const auto number = value.toInteger(-1);
        return value.isDouble() && number >= 0 && number <= 9007199254740991LL &&
                       double(number) == value.toDouble()
                   ? number
                   : qint64(0);
    };
    return {count("prompt_tokens"), count("completion_tokens"), count("total_tokens")};
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

QJsonObject buildMapBody(const GenerateMapRequest &request, const QString &model)
{
    const QJsonObject system{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), PromptLibrary::mapPrompt()}};
    const QJsonObject user{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"),
         QStringLiteral("模式%1；maxNodes=%2；maxDepth=%3。附加要求：%4。材料JSON：%5")
             .arg(request.mode)
             .arg(request.maxNodes)
             .arg(request.maxDepth)
             .arg(request.requirements)
             .arg(QString::fromUtf8(QJsonDocument(AiMaterial::mapJson(request.text))
                                        .toJson(QJsonDocument::Compact)))}};
    return QJsonObject{{QStringLiteral("model"), model},
                       {QStringLiteral("messages"), QJsonArray{system, user}},
                       {QStringLiteral("max_tokens"), kMaxTokens},
                       {QStringLiteral("response_format"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}}};
}

QJsonObject buildDraftBody(const GenerateDraftRequest &request, const QString &model)
{
    const QJsonObject system{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), PromptLibrary::draftPrompt()}};
    const QJsonObject user{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"),
         QStringLiteral("purpose=%1；tone=%2；targetChars=%3；模式%4；附加要求：%"
                        "5。来源JSON（id用于sourceNodeIds）：%6")
             .arg(request.purpose, request.tone)
             .arg(request.targetChars)
             .arg(request.mode)
             .arg(request.requirements)
             .arg(QString::fromUtf8(
                 QJsonDocument(AiMaterial::draftJson(request)).toJson(QJsonDocument::Compact)))}};
    return QJsonObject{{QStringLiteral("model"), model},
                       {QStringLiteral("messages"), QJsonArray{system, user}},
                       {QStringLiteral("max_tokens"), kMaxTokens},
                       {QStringLiteral("response_format"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}}};
}

QJsonObject buildConnectionBody(const QString &model)
{
    const QJsonObject user{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QStringLiteral("ping")}};
    return QJsonObject{{QStringLiteral("model"), model},
                       {QStringLiteral("messages"), QJsonArray{user}},
                       {QStringLiteral("max_tokens"), 8}};
}

}  // namespace

AiService::AiService(IAiProvider *provider, SecretStore *secretStore, QObject *parent)
    : QObject(parent), m_provider(provider), m_secretStore(secretStore),
      m_model(QString::fromLatin1(kModel))
{
    if (m_provider != nullptr) {
        connect(m_provider, &IAiProvider::completed, this, &AiService::onProviderCompleted);
        connect(m_provider, &IAiProvider::failed, this, &AiService::onProviderFailed);
    }
}

AiStatus AiService::status() const {
    AiStatus result;
    result.configured = m_secretStore != nullptr && !m_secretStore->apiKey().isEmpty();
    result.busy = busy();
    result.model = m_model;
    return result;
}

bool AiService::setModel(const QString &model)
{
    const QString value = model.trimmed();
    if (busy() ||
        !QRegularExpression(QStringLiteral("^[A-Za-z0-9._:/-]{1,100}$")).match(value).hasMatch())
        return false;
    m_model = value;
    return true;
}

bool AiService::setKey(const QString& key, bool remember) {
    if (m_secretStore == nullptr) {
        return false;
    }
    const QString previous = m_secretStore->apiKey();
    const bool ok = m_secretStore->setApiKey(key, remember);
    // 密钥没变就不重建（避免每次保存都重建 provider、打断在途请求）
    if (ok && m_secretStore->apiKey() != previous) {
        refreshProvider();
    }
    return ok;
}

void AiService::clearKey() {
    if (m_secretStore != nullptr) {
        m_secretStore->clearApiKey();
        refreshProvider();
    }
}

void AiService::setProvider(IAiProvider* provider) {
    // 先摘后放：旧对象还活着时 cancel+disconnect，再释放，避免在已析构对象上调用
    const QString dropped = detachProvider();
    m_ownedProvider.reset();
    attachProvider(provider);
    // 最终状态已定（attachProvider 里 setState(Idle)），最后再告知被中止的请求
    notifyDropped(dropped);
}

void AiService::setProviderFactory(std::function<std::unique_ptr<IAiProvider>()> factory) {
    m_providerFactory = std::move(factory);
    refreshProvider();
}

void AiService::refreshProvider() {
    if (!m_providerFactory) {
        return;
    }
    // 顺序要紧：先摘（旧对象还活着时 cancel+disconnect），再释放，最后创建并挂接。
    // 若先执行 m_ownedProvider = factory()，旧对象会被 delete，m_provider 随即悬空，
    // 后续 cancel/disconnect 就是在已释放对象上调用（UB）。
    const QString dropped = detachProvider();
    m_ownedProvider.reset();
    m_ownedProvider = m_providerFactory();
    attachProvider(m_ownedProvider.get());
    notifyDropped(dropped);
}

QString AiService::detachProvider() {
    QString dropped;
    if (m_provider != nullptr) {
        // 取消在途请求并断开旧供应方，避免它继续回调本服务
        if (!m_currentRequestId.isEmpty()) {
            dropped = m_currentRequestId;
            m_provider->cancel(dropped);
        }
        disconnect(m_provider, nullptr, this, nullptr);
    }
    m_provider = nullptr;
    m_currentRequestId.clear();
    return dropped;
}

void AiService::attachProvider(IAiProvider* provider) {
    m_provider = provider;
    m_currentRequestId.clear();
    if (m_provider != nullptr) {
        connect(m_provider, &IAiProvider::completed, this, &AiService::onProviderCompleted);
        connect(m_provider, &IAiProvider::failed, this, &AiService::onProviderFailed);
    }
    setState(State::Idle);
}

void AiService::notifyDropped(const QString& requestId) {
    // 必须在最终 setState(Idle) 之后发：否则 stateChanged→面板写"就绪"会把中止提示盖掉。
    // 重建导致的在途请求不是"用户主动取消"，要显式告知 UI（§10.4 一次请求一个终态）。
    // 已知不一致：此处在 state()==Idle 时发 failed（终态信号与状态不同步）。面板按信号收尾，
    // 不读 state()，所以当前无影响；将来若有人按 state 判断终态，需先统一这两者。
    if (requestId.isEmpty()) {
        return;
    }
    AiError error;
    error.code = QStringLiteral("CANCELED");
    error.message = QStringLiteral("供应方已更换，请求已中止");
    error.requestId = requestId;
    emit failed(error);
}

bool AiService::generateMap(const GenerateMapRequest& request) {
    if (!beginRequest(request.requestId, RequestKind::Map)) {
        return false;
    }
    if (!ensureKey(request.requestId)) {
        return false;
    }
    if (const auto error = AiMaterial::validateMapInput(request))
    {
        fail(error->code, error->message, request.requestId);
        return false;
    }
    m_mapMaxNodes = request.maxNodes;
    m_mapMaxDepth = request.maxDepth;
    m_faithful = request.mode == "faithful";
    m_mapParagraphIds.clear();
    for (const auto &p : AiMaterial::mapJson(request.text).value("paragraphs").toArray())
        m_mapParagraphIds.append(p.toObject().value("id").toString());
    setState(State::Requesting);
    m_provider->start(request.requestId, buildMapBody(request, m_model));
    return true;
}

bool AiService::generateDraft(const GenerateDraftRequest& request) {
    if (!beginRequest(request.requestId, RequestKind::Draft)) {
        return false;
    }
    if (!ensureKey(request.requestId)) {
        return false;
    }
    if (const auto error = AiMaterial::validateDraftInput(request))
    {
        fail(error->code, error->message, request.requestId);
        return false;
    }
    m_faithful = request.mode == "faithful";
    m_draftSource = request.source;
    setState(State::Requesting);
    m_provider->start(request.requestId, buildDraftBody(request, m_model));
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
    m_provider->start(requestId, buildConnectionBody(m_model));
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
    MapProposal proposal = parseMapProposal(document.object());
    proposal.model = response.value("model").toString(QStringLiteral("未报告"));
    proposal.usage = responseUsage(response);
    const AiValidationResult validation =
        AiValidator::validateMapProposal(proposal, m_mapMaxNodes, m_mapMaxDepth);
    if (!validation.ok) {
        fail(validation.code, validation.message, requestId);
        return;
    }
    bool validSources = true;
    std::function<void(const GeneratedNode &)> check = [&](const GeneratedNode &node)
    {
        if (m_faithful && node.origin == "supplement")
            validSources = false;
        if (node.origin == "source" && node.sourceParagraphIds.isEmpty())
            validSources = false;
        for (const auto &id : node.sourceParagraphIds)
            if (!m_mapParagraphIds.contains(id))
                validSources = false;
        for (const auto &child : node.children)
            check(child);
    };
    check(proposal.root);
    if (!validSources)
    {
        fail("SCHEMA_INVALID", QStringLiteral("提案引用了未知材料分段或不符合忠实模式"), requestId);
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
    DraftProposal proposal = parseDraftProposal(document.object());
    proposal.model = response.value("model").toString(QStringLiteral("未报告"));
    proposal.usage = responseUsage(response);
    const AiValidationResult validation = AiValidator::validateDraftProposal(proposal, m_draftSource);
    if (!validation.ok) {
        fail(validation.code, validation.message, requestId);
        return;
    }
    if (m_faithful)
        for (const auto &section : proposal.sections)
            if (section.origin == "supplement")
            {
                fail("SCHEMA_INVALID",
                     QStringLiteral("忠实模式的文稿包含补充段落，请重新生成或明确选择允许补充"),
                     requestId);
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
