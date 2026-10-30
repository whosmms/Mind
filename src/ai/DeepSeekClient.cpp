#include "ai/DeepSeekClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace lingya {

namespace {

const char* const kDefaultBaseUrl = "https://api.deepseek.com/chat/completions";
constexpr int kDefaultTimeoutMs = 90000;       // §10.7 单次 90 秒
constexpr int kDefaultRetryDelayMs = 2000;     // §10.7 2 秒 + 抖动
constexpr int kRetryJitterMs = 500;
constexpr int kMaxAttempts = 2;                // §10.7 全程最多 2 次请求
constexpr int kTotalBudgetMs = 180000;         // §10.7 180 秒总预算
const char* const kTimeoutProperty = "lingyaTimeout";

bool isRetryableStatus(int status) {
    return status == 429 || status == 500 || status == 503;
}

}  // namespace

DeepSeekClient::DeepSeekClient(const QString& apiKey, QObject* parent)
    : IAiProvider(parent), m_apiKey(apiKey), m_baseUrl(QString::fromLatin1(kDefaultBaseUrl)) {}

DeepSeekClient::~DeepSeekClient() {
    // abort 可能同步回调 finished()：先置标志，再逐个 take，避免遍历 QHash 时被增删
    m_shuttingDown = true;
    const QStringList ids = m_active.keys();
    for (const QString& requestId : ids) {
        QNetworkReply* reply = m_active.take(requestId);
        if (reply != nullptr) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_pending.clear();
}

void DeepSeekClient::setBaseUrl(const QString& url) {
    if (!url.isEmpty()) {
        m_baseUrl = url;
    }
}

void DeepSeekClient::setTimeoutMs(int ms) {
    m_timeoutMs = ms > 0 ? ms : kDefaultTimeoutMs;
}

void DeepSeekClient::setRetryDelayMs(int ms) {
    m_retryDelayMs = ms >= 0 ? ms : kDefaultRetryDelayMs;
}

void DeepSeekClient::start(const QString& requestId, const QJsonObject& requestBody) {
    if (m_pending.contains(requestId) || m_active.contains(requestId)) {
        return;   // 同一 requestId 进行中不重复发送；取消后 pending 已移除，可重发
    }
    Pending pending;
    pending.body = requestBody;
    pending.attempt = 1;
    pending.budget = QDeadlineTimer(kTotalBudgetMs);
    m_pending.insert(requestId, pending);
    send(requestId);
}

void DeepSeekClient::send(const QString& requestId) {
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) {
        return;
    }
    QNetworkRequest request{QUrl(m_baseUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    QNetworkReply* reply =
        m_network.post(request, QJsonDocument(it->body).toJson(QJsonDocument::Compact));
    m_active.insert(requestId, reply);

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [this, requestId, reply] {
        if (m_shuttingDown || m_active.value(requestId) != reply) {
            return;
        }
        reply->setProperty(kTimeoutProperty, true);
        reply->abort();
    });
    timer->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, requestId, reply] {
        if (m_shuttingDown) {
            return;
        }
        onFinished(requestId, reply);
    });
}

void DeepSeekClient::cancel(const QString& requestId) {
    // 先移除待办：onFinished 见不到 pending 就静默结束，不发 failed（canceled 由 AiService 发）
    m_pending.remove(requestId);
    QNetworkReply* reply = m_active.take(requestId);
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
    const bool timedOut = reply->property(kTimeoutProperty).toBool();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray retryAfterHeader = reply->rawHeader("Retry-After");
    reply->deleteLater();

    const auto it = m_pending.find(requestId);
    if (it == m_pending.end()) {
        return;   // 已取消：静默
    }

    if (timedOut) {
        m_pending.remove(requestId);
        AiError error;
        error.code = QStringLiteral("TIMEOUT");
        error.message = QStringLiteral("请求超时");
        error.requestId = requestId;
        emit failed(error);
        return;
    }

    // 2xx 且 body 是 JSON 对象：正常完成
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (status >= 200 && status < 300 && document.isObject()) {
        m_pending.remove(requestId);
        emit completed(requestId, document.object());
        return;
    }

    const bool preConnectionFailure = status == 0 && networkError != QNetworkReply::NoError;
    if ((isRetryableStatus(status) || preConnectionFailure) && it->attempt < kMaxAttempts) {
        int delayMs = m_retryDelayMs + static_cast<int>(QRandomGenerator::global()->bounded(kRetryJitterMs));
        bool overBudget = false;
        const int retryAfterSeconds = retryAfterHeader.toInt();
        if (retryAfterSeconds > 0) {
            const int requestedMs = retryAfterSeconds * 1000;
            if (it->budget.remainingTime() <= requestedMs) {
                overBudget = true;   // Retry-After 超过总预算：不再重试，直接提示
            } else {
                delayMs = qMax(delayMs, requestedMs);
            }
        }
        if (!overBudget && it->budget.remainingTime() > delayMs) {
            ++it->attempt;
            QTimer::singleShot(delayMs, this, [this, requestId] {
                if (m_shuttingDown || !m_pending.contains(requestId)) {
                    return;
                }
                send(requestId);
            });
            return;
        }
    }

    m_pending.remove(requestId);
    emit failed(mapError(requestId, status, networkError, payload));
}

AiError DeepSeekClient::mapError(const QString& requestId, int status,
                                 QNetworkReply::NetworkError networkError,
                                 const QByteArray& payload) const {
    AiError error;
    error.requestId = requestId;

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
        if (status == 0 && networkError != QNetworkReply::NoError) {
            error.code = QStringLiteral("NETWORK_ERROR");
            error.retryable = true;
        } else {
            error.code = QStringLiteral("UPSTREAM_ERROR");
            error.retryable = status >= 500;
        }
    }

    // 只取上游 error.message，绝不回显请求头（含密钥）
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    QString message;
    if (document.isObject()) {
        message = document.object()
                      .value(QStringLiteral("error"))
                      .toObject()
                      .value(QStringLiteral("message"))
                      .toString();
    }
    if (message.isEmpty()) {
        message = error.code == QStringLiteral("UPSTREAM_ERROR")
                      ? QStringLiteral("上游返回异常（HTTP %1，非 JSON 对象）").arg(status)
                      : QStringLiteral("请求失败（HTTP %1）").arg(status);
    }
    error.message = message;
    return error;
}

}  // namespace lingya
