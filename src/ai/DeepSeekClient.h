#pragma once
#include "ai/IAiProvider.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QString>

class QNetworkReply;

namespace lingya {

// DeepSeek 真实 HTTP 客户端（M5，规范§10.3/§10.5）：
// - 密钥由调用方从 SecretStore 取出后传入，本类不读盘、不写日志、不持久化；
// - baseUrl 默认官方端点，测试可指向本机桩服务器；
// - 超时/网络/HTTP/业务错误统一映射为 AiError（§10.3 错误码），不泄露密钥；
// - 同一 requestId 重复 start 不重复发送；cancel 中止对应请求。
class DeepSeekClient : public IAiProvider {
    Q_OBJECT
public:
    explicit DeepSeekClient(const QString& apiKey, QObject* parent = nullptr);
    ~DeepSeekClient() override;

    void setBaseUrl(const QString& url);
    void setTimeoutMs(int ms);

    void start(const QString& requestId, const QJsonObject& requestBody) override;
    void cancel(const QString& requestId) override;

private:
    void onFinished(const QString& requestId, QNetworkReply* reply);
    AiError mapError(const QString& requestId, QNetworkReply* reply, const QByteArray& payload);

    QString m_apiKey;
    QString m_baseUrl;
    int m_timeoutMs = 60000;
    QNetworkAccessManager m_network;
    QHash<QString, QNetworkReply*> m_active;
};

}  // namespace lingya
