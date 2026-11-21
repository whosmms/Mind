#include "AuthTestTransport.h"
#include "app/OnlineAccountDialog.h"
#include "domain/ProjectFactory.h"
#include "services/OnlineAuthService.h"
#include "services/UserFileService.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSslSocket>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
using namespace lingya;
using namespace auth_test;
namespace
{
OnlineAuthService::Config config()
{
    return {"https://abcdefghijklmnopqrst.supabase.co", "sb_publishable_offline_test_only_123456"};
}
std::unique_ptr<SecretStore> memory()
{
    return std::make_unique<SessionSecretStore>();
}
class FailingStore : public SessionSecretStore
{
  public:
    bool setApiKey(const QString &key, bool remember) override
    {
        SessionSecretStore::setApiKey(key, remember);
        return false;
    }
};
} // namespace
class TstOnlineAuth : public QObject
{
    Q_OBJECT
  private slots:
    void configRejectsUnsafe_data();
    void configRejectsUnsafe();
    void configRoundTripHasNoAccountSecrets();
    void loginRememberRotateLogout();
    void registrationAndConfirmation();
    void recoveryChangesPasswordThenRevokes();
    void failuresAreRedacted_data();
    void failuresAreRedacted();
    void invalidSessionNeverAuthenticates_data();
    void invalidSessionNeverAuthenticates();
    void cancellationAndBusyDoNotCreateSession();
    void refreshFailureForgetsRejectedToken();
    void refreshCannotChangeIdentity();
    void rememberFailureDoesNotKeepOldCredential();
    void localFileNamespacesStaySeparate();
    void dialogValidationAndCancel();
    void timeoutAndTlsFailure();
    void recoveryRejectsInvalidIdentity();
    void unrememberedLoginClearsPreviousCredential();
    void persistedRefreshRestoresIdentity();
    void frozenRuntimeSupportsTls();
};
void TstOnlineAuth::configRejectsUnsafe_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("key");
    const auto key = config().publishableKey;
    QTest::newRow("http") << "http://abcdefghijklmnopqrst.supabase.co" << key;
    QTest::newRow("other-host") << "https://example.com" << key;
    QTest::newRow("credentials") << "https://user@abcdefghijklmnopqrst.supabase.co" << key;
    QTest::newRow("path") << "https://abcdefghijklmnopqrst.supabase.co/extra" << key;
    QTest::newRow("query") << "https://abcdefghijklmnopqrst.supabase.co?x=y" << key;
    QTest::newRow("port") << "https://abcdefghijklmnopqrst.supabase.co:443" << key;
    QTest::newRow("fragment") << "https://abcdefghijklmnopqrst.supabase.co#secret" << key;
    QTest::newRow("secret-key") << config().projectUrl << "sb_secret_fake_for_test_only";
    QTest::newRow("jwt-key") << config().projectUrl << "eyJ.service_role.test";
}
void TstOnlineAuth::configRejectsUnsafe()
{
    QFETCH(QString, url);
    QFETCH(QString, key);
    QVERIFY(!OnlineAuthService::validConfig({url, key}));
}
void TstOnlineAuth::configRoundTripHasNoAccountSecrets()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config()));
    QFile file(root.filePath("online-auth.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(json.size(), 2);
    QVERIFY(!json.contains("password"));
    QVERIFY(!json.contains("refresh_token"));
    OnlineAuthService loaded(root.path(), nullptr, &network, memory());
    loaded.loadConfiguration();
    QVERIFY(loaded.configured());
    QCOMPARE(loaded.config().projectUrl, config().projectUrl);
    QCOMPARE(network.sent.size(), 0);
}
void TstOnlineAuth::loginRememberRotateLogout()
{
    QTemporaryDir root;
    Network network;
    auto store = memory();
    auto *storage = store.get();
    OnlineAuthService service(root.path(), nullptr, &network, std::move(store));
    QVERIFY(service.configure(config(), false));
    network.replies << good();
    QSignalSpy signedIn(&service, &OnlineAuthService::signedIn);
    service.login("reader@example.com", "fake-password-123", true);
    QTRY_COMPARE(signedIn.size(), 1);
    QVERIFY(service.loggedIn());
    QVERIFY(!storage->apiKey().isEmpty());
    QCOMPARE(network.sent[0].request.url().path(), QString("/auth/v1/token"));
    QCOMPARE(network.sent[0].request.url().query(), QString("grant_type=password"));
    QVERIFY(!network.sent[0].request.hasRawHeader("Authorization"));
    QCOMPARE(network.sent[0].request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::ManualRedirectPolicy));
    network.replies << good(
        session("11111111-2222-3333-4444-555555555555", "reader@example.com", "fake-refresh-two"));
    service.restore();
    QTRY_COMPARE(signedIn.size(), 2);
    QVERIFY(storage->apiKey() == "fake-refresh-two");
    network.replies << Reply{204, {}};
    service.logout();
    QVERIFY(!service.loggedIn());
    QVERIFY(storage->apiKey().isEmpty());
    QTRY_VERIFY(!service.busy());
    QCOMPARE(network.sent.last().request.url().query(), QString("scope=local"));
}
void TstOnlineAuth::registrationAndConfirmation()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    QSignalSpy messages(&service, &OnlineAuthService::completed);
    network.replies << Reply{};
    service.signUp("reader@example.com", "fake-password-123");
    QTRY_COMPARE(messages.size(), 1);
    QVERIFY(!service.loggedIn());
    QCOMPARE(network.sent.last().request.url().path(), QString("/auth/v1/signup"));
    network.replies << Reply{};
    service.resendConfirmation("reader@example.com");
    QTRY_COMPARE(messages.size(), 2);
    QCOMPARE(network.sent.last().body.value("type").toString(), QString("signup"));
    network.replies << good();
    service.confirmSignUp("reader@example.com", "123456", false);
    QTRY_VERIFY(service.loggedIn());
    QCOMPARE(network.sent.last().body.value("type").toString(), QString("email"));
}
void TstOnlineAuth::recoveryChangesPasswordThenRevokes()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    QSignalSpy messages(&service, &OnlineAuthService::completed);
    network.replies << Reply{};
    service.requestReset("reader@example.com");
    QTRY_COMPARE(messages.size(), 1);
    network.replies << good() << good(session().value("user").toObject()) << Reply{204, {}};
    service.resetPassword("reader@example.com", "123456", "new-fake-password");
    QTRY_COMPARE(messages.size(), 2);
    QVERIFY(!service.loggedIn());
    QCOMPARE(network.sent.size(), 4);
    QCOMPARE(network.sent[1].body.value("type").toString(), QString("recovery"));
    QCOMPARE(network.sent[2].operation, QNetworkAccessManager::PutOperation);
    QVERIFY(network.sent[2].body.value("password").toString() == "new-fake-password");
    QCOMPARE(network.sent[3].request.url().query(), QString("scope=global"));
}
void TstOnlineAuth::failuresAreRedacted_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("bad-password")
        << 400
        << QByteArray("{\"error_code\":\"invalid_credentials\",\"message\":\"private-marker\"}");
    QTest::newRow("rate-limit") << 429 << QByteArray("{}");
    QTest::newRow("redirect") << 302 << QByteArray("{}");
    QTest::newRow("server") << 500 << QByteArray("private-marker");
    QTest::newRow("not-json") << 200 << QByteArray("private-marker");
    QTest::newRow("huge") << 200 << QByteArray(70000, 'x');
}
void TstOnlineAuth::failuresAreRedacted()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    QSignalSpy error(&service, &OnlineAuthService::failed);
    network.replies << Reply{status, body};
    service.login("reader@example.com", "fake-password-123", false);
    QTRY_COMPARE(error.size(), 1);
    QVERIFY(!service.loggedIn());
    QVERIFY(!error[0][0].toString().contains("private-marker"));
    QCOMPARE(network.sent.size(), 1);
}
void TstOnlineAuth::invalidSessionNeverAuthenticates_data()
{
    QTest::addColumn<QString>("field");
    for (const auto *name : {"access_token", "refresh_token", "token_type", "expires_in", "user",
                             "wrong-email", "unconfirmed", "header-injection"})
        QTest::newRow(name) << QString::fromLatin1(name);
}
void TstOnlineAuth::invalidSessionNeverAuthenticates()
{
    QFETCH(QString, field);
    auto data = session();
    if (field == "wrong-email")
        data["user"] = session("11111111-2222-3333-4444-555555555555", "other@example.com")["user"];
    else if (field == "unconfirmed")
    {
        auto user = data["user"].toObject();
        user.remove("email_confirmed_at");
        data["user"] = user;
    }
    else if (field == "header-injection")
        data["access_token"] = "bad\r\nheader";
    else
        data.remove(field);
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    QSignalSpy error(&service, &OnlineAuthService::failed);
    network.replies << good(data);
    service.login("reader@example.com", "fake-password-123", false);
    QTRY_COMPARE(error.size(), 1);
    QVERIFY(!service.loggedIn());
}
void TstOnlineAuth::cancellationAndBusyDoNotCreateSession()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    auto reply = good();
    reply.delay = 80;
    network.replies << reply;
    QSignalSpy error(&service, &OnlineAuthService::failed);
    service.login("reader@example.com", "fake-password-123", false);
    service.login("reader@example.com", "fake-password-123", false);
    QCOMPARE(network.sent.size(), 1);
    QCOMPARE(error.size(), 1);
    service.cancel();
    QTest::qWait(100);
    QVERIFY(!service.loggedIn());
    QVERIFY(!service.busy());
}
void TstOnlineAuth::refreshFailureForgetsRejectedToken()
{
    QTemporaryDir root;
    Network network;
    auto store = memory();
    auto *storage = store.get();
    storage->setApiKey("fake-expired", true);
    OnlineAuthService service(root.path(), nullptr, &network, std::move(store));
    QVERIFY(service.configure(config(), false));
    network.replies << Reply{400, "{}"};
    service.restore();
    QTRY_VERIFY(!service.busy());
    QVERIFY(storage->apiKey().isEmpty());
    QVERIFY(!service.loggedIn());
}
void TstOnlineAuth::refreshCannotChangeIdentity()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    network.replies << good();
    service.login("reader@example.com", "fake-password-123", true);
    QTRY_VERIFY(service.loggedIn());
    network.replies << good(session("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
    service.restore();
    QTRY_VERIFY(!service.busy());
    QVERIFY(!service.loggedIn());
}
void TstOnlineAuth::rememberFailureDoesNotKeepOldCredential()
{
    QTemporaryDir root;
    Network network;
    auto store = std::make_unique<FailingStore>();
    auto *storage = store.get();
    OnlineAuthService service(root.path(), nullptr, &network, std::move(store));
    QVERIFY(service.configure(config(), false));
    network.replies << good();
    QSignalSpy error(&service, &OnlineAuthService::failed);
    service.login("reader@example.com", "fake-password-123", true);
    QTRY_VERIFY(service.loggedIn());
    QCOMPARE(error.size(), 1);
    QVERIFY(storage->apiKey().isEmpty());
}
void TstOnlineAuth::localFileNamespacesStaySeparate()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService first(root.path(), nullptr, &network, memory());
    OnlineAuthService second(root.path(), nullptr, &network, memory());
    QVERIFY(first.configure(config(), false));
    auto other = config();
    other.projectUrl = "https://bcdefghijklmnopqrst0.supabase.co";
    QVERIFY(second.configure(other, false));
    network.replies << good();
    first.login("reader@example.com", "fake-password-123", false);
    QTRY_VERIFY(first.loggedIn());
    network.replies << good();
    second.login("reader@example.com", "fake-password-123", false);
    QTRY_VERIFY(second.loggedIn());
    QVERIFY(first.localFilesRoot() != second.localFilesRoot());
    const auto project = ProjectFactory::createMindmap("本地作品");
    QVERIFY(std::holds_alternative<QString>(
        UserFileService::saveOwned(first.localFilesRoot(), first.localUserId(), "作品", project)));
    QVERIFY(UserFileService::listOwned(second.localFilesRoot(), second.localUserId()).isEmpty());
    QVERIFY(!QFile::exists(root.filePath("accounts.json")));
    for (const auto &call : network.sent)
    {
        QVERIFY(!call.body.contains("project"));
        QVERIFY(!call.body.contains("sheets"));
    }
}
void TstOnlineAuth::dialogValidationAndCancel()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    OnlineAccountDialog unconfigured(&service);
    QCOMPARE(unconfigured.findChild<QTabWidget *>("onlineAuthTabs")->currentIndex(), 3);
    QVERIFY(!unconfigured.findChild<QPushButton *>("onlineLogin")->isEnabled());
    QCOMPARE(network.sent.size(), 0);
    QVERIFY(service.configure(config(), false));
    OnlineAccountDialog dialog(&service);
    QFile style(QFINDTESTDATA("../resources/styles/light.qss"));
    QVERIFY(style.open(QIODevice::ReadOnly));
    dialog.setStyleSheet(QString::fromUtf8(style.readAll()));
    dialog.show();
    auto *tabs = dialog.findChild<QTabWidget *>("onlineAuthTabs");
    tabs->setCurrentIndex(1);
    dialog.findChild<QLineEdit *>("onlineSignPassword")->setText("first-password");
    dialog.findChild<QLineEdit *>("onlineSignAgain")->setText("other-password");
    dialog.findChild<QPushButton *>("onlineSignUp")->click();
    QCOMPARE(network.sent.size(), 0);
    QVERIFY(dialog.findChild<QLabel *>("onlineAuthStatus")->text().contains("不一致"));
    tabs->setCurrentIndex(0);
    dialog.findChild<QLineEdit *>("onlineLoginEmail")->setText("reader@example.com");
    auto *password = dialog.findChild<QLineEdit *>("onlineLoginPassword");
    password->setText("fake-password-123");
    auto delayed = good();
    delayed.delay = 80;
    network.replies << delayed;
    dialog.findChild<QPushButton *>("onlineLogin")->click();
    QVERIFY(password->text().isEmpty());
    QVERIFY(!tabs->isEnabled());
    dialog.reject();
    QTest::qWait(100);
    QVERIFY(!service.loggedIn());
    const auto captures = qEnvironmentVariable("LINGYA_AUTH_CAPTURE_DIR");
    if (!captures.isEmpty())
    {
        QDir().mkpath(captures);
        OnlineAccountDialog preview(&service);
        preview.setStyleSheet(dialog.styleSheet());
        preview.show();
        auto *previewTabs = preview.findChild<QTabWidget *>("onlineAuthTabs");
        for (int index = 0; index < 4; ++index)
        {
            previewTabs->setCurrentIndex(index);
            QCoreApplication::processEvents();
            QVERIFY(preview.grab().save(captures + QString("/账号-%1.png").arg(index)));
        }
    }
}
void TstOnlineAuth::timeoutAndTlsFailure()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory(), 20);
    QVERIFY(service.configure(config(), false));
    QSignalSpy errors(&service, &OnlineAuthService::failed);
    auto delayed = good();
    delayed.delay = 100;
    network.replies << delayed;
    service.login("reader@example.com", "fake-password-123", false);
    QTRY_COMPARE(errors.size(), 1);
    QVERIFY(!service.busy());
    QVERIFY(!service.loggedIn());
    auto tls = good();
    tls.error = QNetworkReply::SslHandshakeFailedError;
    network.replies << tls;
    service.login("reader@example.com", "fake-password-123", false);
    QTRY_COMPARE(errors.size(), 2);
    QVERIFY(!service.loggedIn());
}
void TstOnlineAuth::recoveryRejectsInvalidIdentity()
{
    QTemporaryDir root;
    Network network;
    OnlineAuthService service(root.path(), nullptr, &network, memory());
    QVERIFY(service.configure(config(), false));
    network.replies << good(session("11111111-2222-3333-4444-555555555555", "other@example.com"));
    QSignalSpy errors(&service, &OnlineAuthService::failed);
    service.resetPassword("reader@example.com", "123456", "new-fake-password");
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(network.sent.size(), 1);
    QVERIFY(!service.loggedIn());
}
void TstOnlineAuth::unrememberedLoginClearsPreviousCredential()
{
    QTemporaryDir root;
    Network network;
    auto store = memory();
    auto *storage = store.get();
    storage->setApiKey("old-fake-refresh", true);
    OnlineAuthService service(root.path(), nullptr, &network, std::move(store));
    QVERIFY(service.configure(config(), false));
    network.replies << good();
    service.login("reader@example.com", "fake-password-123", false);
    QTRY_VERIFY(service.loggedIn());
    QVERIFY(storage->apiKey().isEmpty());
    network.replies << good();
    service.restore();
    QTRY_VERIFY(!service.busy());
    QVERIFY(service.loggedIn());
    QVERIFY(storage->apiKey().isEmpty());
}
void TstOnlineAuth::persistedRefreshRestoresIdentity()
{
    QTemporaryDir root;
    Network network;
    auto store = memory();
    store->setApiKey("fake-persisted-refresh", true);
    OnlineAuthService service(root.path(), nullptr, &network, std::move(store));
    QVERIFY(service.configure(config(), false));
    network.replies << good();
    service.restore();
    QTRY_VERIFY(service.loggedIn());
    QVERIFY(network.sent[0].body.value("refresh_token").toString() == "fake-persisted-refresh");
    QCOMPARE(service.email(), QString("reader@example.com"));
}
void TstOnlineAuth::frozenRuntimeSupportsTls()
{
    QVERIFY(QSslSocket::supportsSsl());
}
QTEST_MAIN(TstOnlineAuth)
#include "tst_onlineauth.moc"
