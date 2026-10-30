#include <QtTest>

#include "ai/AiServiceFactory.h"
#include "ai/DeepSeekClient.h"
#include "ai/FakeAiProvider.h"
#include "ai/SecretStore.h"

#include <QHostAddress>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

using namespace lingya;

namespace {

// 本机桩服务器：只回一条固定 HTTP 响应，用于验证请求头与错误映射，不访问外网。
// per-socket 标志挡重复 readyRead；disconnected 后释放 socket。
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
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        auto handled = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, handled] {
            lastRequest += socket->readAll();
            if (*handled || !lastRequest.contains("\r\n\r\n")) {
                return;
            }
            *handled = true;
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

// DeepSeek 客户端测试（M5）：成功、错误码映射、有界重试、取消静默、超时与断网。
class TstDeepSeekClient : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void successEmitsCompleted();
    void authErrorMapsToAuthFailed();
    void balanceLowMapsToBalanceLow();
    void rateLimitRetriesOnceThenFails();
    void serverErrorRetriesOnceThenFails();
    void duplicateRequestIdIsIgnored();
    void cancelIsSilent();
    void cancelThenRetrySendsAgain();
    void timeoutMapsToTimeout();
    void networkErrorMapsToNetworkError();
    void nonJsonObjectIsUpstreamError();
    void factoryPicksProviderByKey();

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

    client.start(QStringLiteral("req-1"),
                 QJsonObject{{QStringLiteral("model"), QStringLiteral("m")}});

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(completed.first().at(0).toString(), QStringLiteral("req-1"));
    QCOMPARE(m_server.requestCount, 1);
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
    QVERIFY(!error.retryable);
    QCOMPARE(m_server.requestCount, 1);   // 401 不重试
}

void TstDeepSeekClient::balanceLowMapsToBalanceLow() {
    m_server.status = 402;
    m_server.body = R"({"error":{"message":"insufficient balance"}})";

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-402"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(failed.first().at(0).value<AiError>().code, QStringLiteral("BALANCE_LOW"));
}

void TstDeepSeekClient::rateLimitRetriesOnceThenFails() {
    m_server.status = 429;
    m_server.body = R"({"error":{"message":"rate limited"}})";

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    client.setRetryDelayMs(10);
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-3"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("RATE_LIMITED"));
    QVERIFY(error.retryable);
    QCOMPARE(m_server.requestCount, 2);   // 429 重试一次后放弃
}

void TstDeepSeekClient::serverErrorRetriesOnceThenFails() {
    m_server.status = 500;
    m_server.body = R"({"error":{"message":"boom"}})";

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    client.setRetryDelayMs(10);
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-4"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("UPSTREAM_ERROR"));
    QCOMPARE(error.message, QStringLiteral("boom"));
    QCOMPARE(m_server.requestCount, 2);
}

void TstDeepSeekClient::duplicateRequestIdIsIgnored() {
    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy completed(&client, &DeepSeekClient::completed);

    client.start(QStringLiteral("req-5"), QJsonObject{});
    client.start(QStringLiteral("req-5"), QJsonObject{});

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
    QCOMPARE(m_server.requestCount, 1);
}

void TstDeepSeekClient::cancelIsSilent() {
    m_server.delayMs = 3000;

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy completed(&client, &DeepSeekClient::completed);
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-6"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount, 1, 3000);
    client.cancel(QStringLiteral("req-6"));

    QTest::qWait(200);
    QCOMPARE(completed.count(), 0);   // 取消不发任何信号（canceled 由 AiService 发）
    QCOMPARE(failed.count(), 0);
}

void TstDeepSeekClient::cancelThenRetrySendsAgain() {
    m_server.delayMs = 500;

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());

    client.start(QStringLiteral("req-7"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount, 1, 3000);
    client.cancel(QStringLiteral("req-7"));

    QSignalSpy completed(&client, &DeepSeekClient::completed);
    client.start(QStringLiteral("req-7"), QJsonObject{});   // 同一 id 取消后可以重发
    QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount, 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
}

void TstDeepSeekClient::timeoutMapsToTimeout() {
    m_server.delayMs = 2000;

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    client.setTimeoutMs(50);
    client.setRetryDelayMs(10);
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-8"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(failed.first().at(0).value<AiError>().code, QStringLiteral("TIMEOUT"));
}

void TstDeepSeekClient::networkErrorMapsToNetworkError() {
    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(QStringLiteral("http://127.0.0.1:1/chat"));   // 无人监听
    client.setRetryDelayMs(10);
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-9"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
    QCOMPARE(failed.first().at(0).value<AiError>().code, QStringLiteral("NETWORK_ERROR"));
}

void TstDeepSeekClient::nonJsonObjectIsUpstreamError() {
    m_server.status = 200;
    m_server.body = QByteArray("not a json object");

    DeepSeekClient client(QStringLiteral("k"));
    client.setBaseUrl(url());
    QSignalSpy failed(&client, &DeepSeekClient::failed);

    client.start(QStringLiteral("req-10"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    const auto error = failed.first().at(0).value<AiError>();
    QCOMPARE(error.code, QStringLiteral("UPSTREAM_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("非 JSON 对象")));   // 不丢中文说明
}

void TstDeepSeekClient::factoryPicksProviderByKey() {
    SessionSecretStore empty;
    auto fake = createAiProvider(&empty);
    QVERIFY(dynamic_cast<FakeAiProvider*>(fake.get()) != nullptr);

    SessionSecretStore withKey;
    withKey.setApiKey(QStringLiteral("sk-x"), false);
    auto deepseek = createAiProvider(&withKey);
    QVERIFY(dynamic_cast<DeepSeekClient*>(deepseek.get()) != nullptr);
}

QTEST_MAIN(TstDeepSeekClient)
#include "tst_deepseekclient.moc"
