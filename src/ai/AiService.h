#pragma once
#include "ai/AiTypes.h"

#include <QJsonObject>
#include <QObject>

namespace lingya {

class IAiProvider;
class SecretStore;

// AI 业务调度（AI规范§10.3、§10.6）：请求生命周期、响应校验与预览状态。
// 不直接修改 Project；采用由调用方在 preview 后经 CommandService 执行。
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

    AiStatus status() const;
    bool setKey(const QString& key, bool remember);
    void clearKey();

    // 均不阻塞 UI；重复提交（并发请求）返回 false（BUSY）
    bool generateMap(const GenerateMapRequest& request);
    bool generateDraft(const GenerateDraftRequest& request);
    void cancel(const QString& requestId);

    State state() const;
    const MapProposal& lastMapProposal() const;
    const DraftProposal& lastDraftProposal() const;

signals:
    void statusChanged(const AiStatus& status);
    void mapProposalReady(const MapProposal& proposal);
    void draftProposalReady(const DraftProposal& proposal);
    void failed(const AiError& error);
    void stateChanged(AiService::State state);

private:
    void onProviderCompleted(const QString& requestId, const QJsonObject& response);
    void onProviderFailed(const AiError& error);
    void handleMapResponse(const QJsonObject& response, const QString& requestId);
    void handleDraftResponse(const QJsonObject& response, const QString& requestId);
    void fail(const QString& code, const QString& message, const QString& requestId);
    void setState(State state);
    bool busy() const;
    bool beginRequest(const QString& requestId, bool isMap);

    IAiProvider* m_provider;
    SecretStore* m_secretStore;
    State m_state = State::Idle;
    QString m_currentRequestId;
    bool m_isMap = true;
    QVector<DraftSourceNode> m_draftSource;  // 本次稿件的来源集合，用于校验
    MapProposal m_lastMap;
    DraftProposal m_lastDraft;
};

}  // namespace lingya
