#pragma once
#include "ai/IAiProvider.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

namespace lingya {

// DeepSeek 真实 HTTP 客户端（M5，规范§10.3/§10.5/§10.7）：
// - 密钥由调用方从 SecretStore 取出后传入，本类不读盘、不写日志、不持久化；
// - baseUrl 默认官方端点，测试可指向本机桩服务器；
// - 取消是静默的：cancel 只 abort 并移除待办，不发任何信号（canceled 由 AiService 发）；
// - §10.7 有界重试：429/500/503 或连接前失败重试一次（2s+抖动，读 Retry-After），
//   全程最多 2 次请求、180s 总预算；超预算不再重试；
// - 超时 90s（§10.7）；/超时与错误统一映射为 AiError（§10.3 错误码），不泄露密钥。
class DeepSeekClient : public IAiProvider {
    Q_OBJECT
public:
    explicit DeepSeekClient(const QString& apiKey, QObject* parent = nullptr);
    ~DeepSeekClient() override;

    void setBaseUrl(const QString& url);
    void setTimeoutMs(int ms);
    void setRetryDelayMs(int ms);   // 测试用，默认 2000

    void start(const QString& requestId, const QJsonObject& requestBody) override;
    void cancel(const QString& requestId) override;

private:
    struct Pending {
        QJsonObject body;
        int attempt = 0;
        QDeadlineTimer budget;
    };

    void send(const QString& requestId);
    void onFinished(const QString& requestId, QNetworkReply* reply);
    AiError mapError(const QString& requestId, int status,
                     QNetworkReply::NetworkError networkError, const QByteArray& payload) const;

    QString m_apiKey;
    QString m_baseUrl;
    int m_timeoutMs = 90000;
    int m_retryDelayMs = 2000;
    bool m_shuttingDown = false;
    QNetworkAccessManager m_network;
    QHash<QString, Pending> m_pending;
    QHash<QString, QNetworkReply*> m_active;
};

}  // namespace lingya
