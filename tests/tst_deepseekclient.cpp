#include <QtTest>

#include "ai/DeepSeekClient.h"

#include <QHostAddress>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

using namespace lingya;

namespace {

// 本机桩服务器：只回一条固定 HTTP 响应，用于验证请求头与错误映射，不访问外网。
class StubServer : public QTcpServer {
public:
    QByteArray body;
    int status = 200;
    int delayMs = 0;
    int requestCount = 0;
    QByteArray lastRequest;

    void incomingConnection(qintptr socketDescriptor) override {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            lastRequest += socket->readAll();
            if (!lastRequest.contains("\r\n\r\n")) {
                return;
            }
            ++requestCount;
            const auto respond = [this, socket] {
                const QByteArray payload = body;
                const QByteArray response =
                    "HTTP/1.1 " + QByteArray::number(status) + " X\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + payload;
                socket->write(response);
                socket->disconnectFromHost();
            };
            if (delayMs > 0) {
                QTimer::singleShot(delayMs, socket, respond);
            } else {
                respond();
            }
        });
    }
};

}  // namespace

// DeepSeek 客户端测试（M5）：成功、错误码映射、重复请求去重、取消。
class TstDeepSeekClient : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void successEmitsCompleted();
    void authErrorMapsToAuthFailed();
    void rateLimitIsRetryable();
    void serverErrorMapsToUpstream();
    void duplicateRequestIdIsIgnored();
    void cancelEmitsCanceled();

private:
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/chat").arg(m_port); }

    StubServer m_server;
    quint16 m_port = 0;
};

void TstDeepSeekClient::init() {
    m_server.body = R"({"choices":[{"message":{"content":"{}"}}]})";
    m_server.status = 200;
    m_server.delayMs = 0;
    m_server.requestCount = 0;
    m_server.lastRequest.clear();
    QVERIFY(m_server.listen(QHostAddress::LocalHost, 0));
    m_port = m_server.serverPort();
}

void TstDeepSeekClient::cleanup() {
    m_server.close();
}

void TstDeepSeekClient::successEmitsCompleted() {
    DeepSeekClient client(QStringLiteral("test-key"));
    client.setBaseUrl(url());
    QSignalSpy completed(&client, &DeepSeekClient::completed);
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-1"), QJsonObject{{QStringLiteral("model"), QStringLiteral("m")}});

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(completed.first().at(0).toString(), QStringLiteral("req-1"));
    QCOMPARE(m_server.requestCount, 1);
    // 密钥只放在 Authorization 头里
    QVERIFY(m_server.lastRequest.contains("Bearer test-key"));
}

void TstDeepSeekClient::authErrorMapsToAuthFailed() {
    m_server.status = 401;
    m_server.body = R"({"error":{"message":"invalid api key"}})";

    DeepSeekClient client(QStringLiteral("bad-key"));
    client.setBaseUrl(url());
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-2"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("AUTH_FAILED"));
    QCOMPARE(error.requestId, QStringLiteral("req-2"));
    QVERIFY(!error.retryable);
}

void TstDeepSeekClient::rateLimitIsRetryable() {
    m_server.status = 429;
    m_server.body = R"({"error":{"message":"rate limited"}})";

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-3"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("RATE_LIMITED"));
    QVERIFY(error.retryable);
}

void TstDeepSeekClient::serverErrorMapsToUpstream() {
    m_server.status = 500;
    m_server.body = R"({"error":{"message":"boom"}})";

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-4"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("UPSTREAM_ERROR"));
    QVERIFY(error.retryable);
    QCOMPARE(error.message, QStringLiteral("boom"));
}

void TstDeepSeekClient::duplicateRequestIdIsIgnored() {
    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy completed(&client, &DeepSeekClient::completed);

    client.start(QStringLiteral("req-5"), QJsonObject{});
    client.start(QStringLiteral("req-5"), QJsonObject{});   // 同一 requestId 不重复发送

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
    QCOMPARE(m_server.requestCount, 1);
}

void TstDeepSeekClient::cancelEmitsCanceled() {
    m_server.delayMs = 3000;   // 服务器不回，等客户端取消

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-6"), QJsonObject{});
    QTest::qWait(50);
    client.cancel(QStringLiteral("req-6"));

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("CANCELED"));
}

QTEST_MAIN(TstDeepSeekClient)
#include "tst_deepseekclient.moc"
