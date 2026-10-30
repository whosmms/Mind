#include "ai/DeepSeekClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace lingya {

namespace {

const char* const kDefaultBaseUrl = "https://api.deepseek.com/chat/completions";
constexpr int kDefaultTimeoutMs = 60000;
const char* const kTimeoutProperty = "lingyaTimeout";

}  // namespace

DeepSeekClient::DeepSeekClient(const QString& apiKey, QObject* parent)
    : IAiProvider(parent), m_apiKey(apiKey), m_baseUrl(QString::fromLatin1(kDefaultBaseUrl)) {}

DeepSeekClient::~DeepSeekClient() {
    for (QNetworkReply* reply : m_active) {
        reply->abort();
    }
}

void DeepSeekClient::setBaseUrl(const QString& url) {
    if (!url.isEmpty()) {
        m_baseUrl = url;
    }
}

void DeepSeekClient::setTimeoutMs(int ms) {
    m_timeoutMs = ms > 0 ? ms : kDefaultTimeoutMs;
}

void DeepSeekClient::start(const QString& requestId, const QJsonObject& requestBody) {
    if (m_active.contains(requestId)) {
        return;   // 同一 requestId 不重复发送（规范§10.6）
    }
    QNetworkRequest request{QUrl(m_baseUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    QNetworkReply* reply =
        m_network.post(request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
    m_active.insert(requestId, reply);

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] {
        reply->setProperty(kTimeoutProperty, true);
        reply->abort();
    });
    timer->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this,
            [this, requestId, reply] { onFinished(requestId, reply); });
}

void DeepSeekClient::cancel(const QString& requestId) {
    QNetworkReply* reply = m_active.value(requestId, nullptr);
    if (reply != nullptr) {
        reply->abort();
    }
}

void DeepSeekClient::onFinished(const QString& requestId, QNetworkReply* reply) {
    if (m_active.value(requestId) != reply) {
        reply->deleteLater();
        return;
    }
    m_active.remove(requestId);
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (reply->property(kTimeoutProperty).toBool()) {
        AiError error;
        error.code = QStringLiteral("TIMEOUT");
        error.message = QStringLiteral("请求超时");
        error.requestId = requestId;
        error.retryable = true;
        emit failed(error);
        return;
    }
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        AiError error;
        error.code = QStringLiteral("CANCELED");
        error.message = QStringLiteral("请求已取消");
        error.requestId = requestId;
        emit failed(error);
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 200 && status < 300 && document.isObject()) {
        emit completed(requestId, document.object());
        return;
    }
    emit failed(mapError(requestId, reply, payload));
}

AiError DeepSeekClient::mapError(const QString& requestId, QNetworkReply* reply,
                                 const QByteArray& payload) {
    AiError error;
    error.requestId = requestId;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    switch (status) {
    case 400:
        error.code = QStringLiteral("INVALID_REQUEST");
        break;
    case 401:
    case 403:
        error.code = QStringLiteral("AUTH_FAILED");
        break;
    case 402:
        error.code = QStringLiteral("BALANCE_LOW");
        break;
    case 429:
        error.code = QStringLiteral("RATE_LIMITED");
        error.retryable = true;
        break;
    default:
        break;
    }
    if (error.code.isEmpty()) {
        if (status == 0 && reply->error() != QNetworkReply::NoError) {
            error.code = QStringLiteral("NETWORK_ERROR");
            error.retryable = true;
        } else {
            error.code = QStringLiteral("UPSTREAM_ERROR");
            error.retryable = status >= 500;
        }
    }

    // 只取上游 error.message，绝不回显请求头（含密钥）
    const QJsonObject body = QJsonDocument::fromJson(payload).object();
    const QString upstreamMessage =
        body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    error.message = upstreamMessage.isEmpty() ? reply->errorString() : upstreamMessage;
    return error;
}

}  // namespace lingya
