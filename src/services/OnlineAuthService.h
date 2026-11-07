#pragma once
#include "ai/SecretStore.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace lingya
{
// IF-A-20261107-009：仅处理身份，完全不读取或上传工程。
class OnlineAuthService : public QObject
{
    Q_OBJECT
  public:
    struct Config
    {
        QString projectUrl;
        QString publishableKey;
    };
    explicit OnlineAuthService(const QString &profileRoot, QObject *parent = nullptr,
                               QNetworkAccessManager *transport = nullptr,
                               std::unique_ptr<SecretStore> store = {}, int timeoutMs = 20000);
    ~OnlineAuthService() override;
    static bool validConfig(const Config &config);
    static bool validEmail(const QString &email);
    static bool validPassword(const QString &password);
    Config config() const
    {
        return m_config;
    }
    bool configure(const Config &config, bool save = true);
    void loadConfiguration(const QString &bundledFile = {});
    bool configured() const
    {
        return validConfig(m_config);
    }
    bool busy() const
    {
        return !m_reply.isNull();
    }
    bool loggedIn() const
    {
        return !m_userId.isEmpty();
    }
    QString email() const
    {
        return m_email;
    }
    QString localUserId() const;
    QString localFilesRoot() const;
    void login(const QString &email, const QString &password, bool remember);
    void signUp(const QString &email, const QString &password);
    void confirmSignUp(const QString &email, const QString &code, bool remember);
    void resendConfirmation(const QString &email);
    void requestReset(const QString &email);
    void resetPassword(const QString &email, const QString &code, const QString &password);
    void restore();
    void logout();
    void cancel();
  signals:
    void signedIn();
    void signedOut();
    void busyChanged(bool busy);
    void completed(const QString &message);
    void failed(const QString &message);

  private:
    using Completion = std::function<void(const QJsonObject &, int)>;
    void request(const QString &path, const QJsonObject &body, Completion done,
                 const QString &bearer = {}, bool put = false);
    bool checkInput(const QString &email, const QString &password = {}, bool checkPassword = false);
    bool acceptSession(const QJsonObject &data, bool remember, const QString &expectedEmail = {});
    void clearSession(bool forget);
    QString m_root;
    Config m_config;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<SecretStore> m_store;
    bool m_injectedStore = false;
    bool m_remember = false;
    QString m_access, m_refresh, m_userId, m_email;
    QTimer m_refreshTimer;
    int m_timeoutMs;
};
} // namespace lingya
