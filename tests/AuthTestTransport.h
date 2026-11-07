#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
// 完全离线的HTTP协议传输桩：只使用固定假凭据，不会联系真实服务。
namespace auth_test
{
struct Reply
{
    int status = 200;
    QByteArray body = "{}";
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int delay = 0;
};
class Response : public QNetworkReply
{
  public:
    Response(const QNetworkRequest &req, const Reply &value, QObject *parent)
        : QNetworkReply(parent), m_bytes(value.body)
    {
        setRequest(req);
        setUrl(req.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, value.status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(value.delay, this,
                           [this, value]
                           {
                               if (isFinished())
                                   return;
                               if (value.error != NoError)
                                   setError(value.error, "受控网络错误");
                               emit readyRead();
                               if (isFinished())
                                   return;
                               setFinished(true);
                               emit finished();
                           });
    }
    void abort() override
    {
        if (isFinished())
            return;
        setError(OperationCanceledError, "已取消");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_bytes.size() - m_offset + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char *output, qint64 max) override
    {
        const auto count = qMin(max, qint64(m_bytes.size() - m_offset));
        if (count <= 0)
            return -1;
        std::memcpy(output, m_bytes.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_bytes;
    qint64 m_offset = 0;
};
class Network : public QNetworkAccessManager
{
  public:
    using QNetworkAccessManager::QNetworkAccessManager;
    struct Sent
    {
        Operation operation;
        QNetworkRequest request;
        QJsonObject body;
    };
    QList<Reply> replies;
    QList<Sent> sent;

  protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req,
                                 QIODevice *outgoing = nullptr) override
    {
        sent.append(
            {op, req,
             QJsonDocument::fromJson(outgoing ? outgoing->readAll() : QByteArray()).object()});
        Reply reply = replies.isEmpty() ? Reply{500, "{}"} : replies.takeFirst();
        return new Response(req, reply, this);
    }
};
inline QJsonObject session(QString id = "11111111-2222-3333-4444-555555555555",
                           QString email = "reader@example.com",
                           QString refresh = "fake-refresh-one")
{
    return {{"access_token", "fake.access.token"},
            {"refresh_token", refresh},
            {"token_type", "bearer"},
            {"expires_in", 3600},
            {"user", QJsonObject{{"id", id},
                                 {"email", email},
                                 {"email_confirmed_at", "2026-11-07T00:00:00Z"}}}};
}
inline Reply good(const QJsonObject &object = session())
{
    return {200, QJsonDocument(object).toJson(QJsonDocument::Compact)};
}
} // namespace auth_test
