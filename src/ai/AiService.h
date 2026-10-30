#pragma once
#include "ai/AiTypes.h"

#include <QJsonObject>
#include <QObject>

#include <functional>
#include <memory>

namespace lingya {

class IAiProvider;
class SecretStore;

// AI 业务调度（AI规范§10.3、§10.6）：请求生命周期、响应校验与预览状态。
// 不直接修改 Project；采用由调用方在 preview 后经 CommandService 执行。
// 信号契约见规范§10.4：stateChanged / mapReady / draftReady / connectionReady / failed / canceled。
class AiService : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Validating,
        Requesting,
        ValidatingResponse,
        Preview,
        Applying,
        Done,
        Error,
        Canceled
    };
    Q_ENUM(State)

    AiService(IAiProvider* provider, SecretStore* secretStore, QObject* parent = nullptr);

    // 装配层注入：按当前密钥构造 provider。setKey/clearKey 成功后本服务自动重建并重连，
    // 不把“换供应方”的责任推给 UI（避免“存了密钥仍发给 Fake”）。
    void setProviderFactory(std::function<std::unique_ptr<IAiProvider>()> factory);

    // 直接更换 provider（测试/装配用）：取消在途请求、断开旧的、连接新的
    void setProvider(IAiProvider* provider);

    AiStatus status() const;
    bool setKey(const QString& key, bool remember);
    void clearKey();

    // 均不阻塞 UI；并发请求返回 false（BUSY）；无密钥返回 false（KEY_MISSING）
    bool generateMap(const GenerateMapRequest& request);
    bool generateDraft(const GenerateDraftRequest& request);
    bool testConnection(const QString& requestId);
    void cancel(const QString& requestId);

    State state() const;
    const MapProposal& lastMapProposal() const;
    const DraftProposal& lastDraftProposal() const;

signals:
    void stateChanged(const QString& requestId, AiService::State state);
    void mapReady(const QString& requestId, const MapProposal& proposal);
    void draftReady(const QString& requestId, const DraftProposal& proposal);
    void connectionReady(const QString& requestId, const QString& model);
    void failed(const AiError& error);
    void canceled(const QString& requestId);

private:
    enum class RequestKind { Map, Draft, Connection };

    void onProviderCompleted(const QString& requestId, const QJsonObject& response);
    void onProviderFailed(const AiError& error);
    // 断开旧供应方并返回被中止的 requestId（无则空）；failed 由调用方在最终状态确定后发
    QString detachProvider();
    void attachProvider(IAiProvider* provider);
    void notifyDropped(const QString& requestId);
    void refreshProvider();
    void handleMapResponse(const QJsonObject& response, const QString& requestId);
    void handleDraftResponse(const QJsonObject& response, const QString& requestId);
    void handleConnectionResponse(const QJsonObject& response, const QString& requestId);
    void fail(const QString& code, const QString& message, const QString& requestId);
    void setState(State state);
    bool busy() const;
    bool beginRequest(const QString& requestId, RequestKind kind);
    bool ensureKey(const QString& requestId);

    IAiProvider* m_provider;
    SecretStore* m_secretStore;
    std::unique_ptr<IAiProvider> m_ownedProvider;   // 由工厂创建时归本服务所有
    std::function<std::unique_ptr<IAiProvider>()> m_providerFactory;
    State m_state = State::Idle;
    QString m_currentRequestId;
    RequestKind m_kind = RequestKind::Map;
    QVector<DraftSourceNode> m_draftSource;
    MapProposal m_lastMap;
    DraftProposal m_lastDraft;
};

}  // namespace lingya
