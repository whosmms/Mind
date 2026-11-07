#include "services/OnlineAuthService.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

namespace lingya
{
namespace
{
QString fingerprint(const QString &url)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString safeError(int status, const QByteArray &body)
{
    const auto code = QJsonDocument::fromJson(body).object().value("error_code").toString();
    if (status == 429 || code == "over_email_send_rate_limit")
        return QStringLiteral("操作过于频繁，请稍后再试；邮件投递额度由服务端控制。");
    if (code == "email_not_confirmed")
        return QStringLiteral("邮箱尚未验证，请输入邮件中的验证码或重新发送。");
    if (code == "otp_expired")
        return QStringLiteral("验证码无效或已过期，请重新获取。");
    if (code == "weak_password")
        return QStringLiteral("密码未满足服务端强度要求，请使用更长且不常见的密码。");
    if (status == 400 || status == 401 || status == 403 || status == 422)
        return QStringLiteral("认证未通过，请检查邮箱、密码或验证码；也可重新登录或找回密码。");
    if (status >= 300 && status < 400)
        return QStringLiteral("服务返回了重定向，已停止发送凭据，请核对项目地址。");
    if (status >= 500)
        return QStringLiteral("账号服务暂时不可用，请稍后再试；工程仍保存在本地。");
    return QStringLiteral("连接失败、超时或响应无效，请检查网络和账号服务配置。");
}
bool validSession(const QJsonObject &data)
{
    const auto user = data.value("user").toObject();
    const auto id = user.value("id").toString();
    const int seconds = data.value("expires_in").toInt();
    return !QUuid(id).isNull() && OnlineAuthService::validEmail(user.value("email").toString()) &&
           !user.value("email_confirmed_at").toString().isEmpty() &&
           data.value("token_type").toString().compare("bearer", Qt::CaseInsensitive) == 0 &&
           QRegularExpression("^[A-Za-z0-9._~-]{1,16384}$")
               .match(data.value("access_token").toString())
               .hasMatch() &&
           !data.value("refresh_token").toString().isEmpty() &&
           data.value("refresh_token").toString().size() <= 2048 && seconds >= 60 &&
           seconds <= 604800;
}
} // namespace
OnlineAuthService::OnlineAuthService(const QString &root, QObject *parent,
                                     QNetworkAccessManager *transport,
                                     std::unique_ptr<SecretStore> store, int timeoutMs)
    : QObject(parent), m_root(root),
      m_network(transport ? transport : new QNetworkAccessManager(this)), m_store(std::move(store)),
      m_injectedStore(bool(m_store)), m_timeoutMs(qBound(20, timeoutMs, 60000))
{
    setObjectName("onlineAuthService");
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &OnlineAuthService::restore);
}
OnlineAuthService::~OnlineAuthService()
{
    cancel();
}
bool OnlineAuthService::validConfig(const Config &config)
{
    // 只允许官方托管项目的HTTPS源站；公开标识不是服务端管理密钥。
    static const QRegularExpression host("^[a-z0-9]{20}\\.supabase\\.co$");
    static const QRegularExpression key("^sb_publishable_[A-Za-z0-9_-]{16,200}$");
    const QUrl url(config.projectUrl);
    return url.isValid() && url.scheme() == "https" && host.match(url.host()).hasMatch() &&
           url.port() == -1 && url.userInfo().isEmpty() && url.query().isEmpty() &&
           url.fragment().isEmpty() && (url.path().isEmpty() || url.path() == "/") &&
           key.match(config.publishableKey).hasMatch();
}
bool OnlineAuthService::validEmail(const QString &email)
{
    static const QRegularExpression expression("^[^\\s@<>]+@[^\\s@<>]+\\.[^\\s@<>]+$");
    return email.size() <= 254 && expression.match(email).hasMatch();
}
bool OnlineAuthService::validPassword(const QString &password)
{
    return password.size() >= 12 && password.size() <= 128 && !password.contains(QChar(0));
}
bool OnlineAuthService::configure(const Config &config, bool save)
{
    if (busy() || loggedIn())
    {
        emit failed(QStringLiteral("请先退出账号并等待当前操作结束，再修改服务配置。"));
        return false;
    }
    if (!validConfig(config))
    {
        emit failed(QStringLiteral("请输入有效的Supabase项目HTTPS地址和publishable公开标识；不能使"
                                   "用secret或service_role密钥。"));
        return false;
    }
    Config normalized{QUrl(config.projectUrl).adjusted(QUrl::StripTrailingSlash).toString(),
                      config.publishableKey};
    if (save)
    {
        if (!QDir().mkpath(m_root))
        {
            emit failed(QStringLiteral("无法创建本机配置目录。"));
            return false;
        }
        QSaveFile file(m_root + "/online-auth.json");
        file.setDirectWriteFallback(false);
        const auto bytes = QJsonDocument(QJsonObject{{"projectUrl", normalized.projectUrl},
                                                     {"publishableKey", normalized.publishableKey}})
                               .toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        {
            emit failed(QStringLiteral("无法保存账号服务的公开配置。"));
            return false;
        }
    }
    m_config = normalized;
    if (!m_injectedStore)
        m_store = createSecretStore("LingyaMind/Auth/" + fingerprint(m_config.projectUrl));
    return true;
}
void OnlineAuthService::loadConfiguration(const QString &bundledFile)
{
    const QString local = m_root + "/online-auth.json";
    QFile file(QFile::exists(local) ? local : bundledFile);
    if (file.fileName().isEmpty() || !file.exists())
        return;
    if (file.size() > 8192 || !file.open(QIODevice::ReadOnly))
    {
        emit failed(QStringLiteral("账号服务配置不可读。"));
        return;
    }
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.size() != 2 || !json.value("projectUrl").isString() ||
        !json.value("publishableKey").isString())
    {
        emit failed(QStringLiteral("账号配置格式无效，请重新配置。"));
        return;
    }
    configure({json.value("projectUrl").toString(), json.value("publishableKey").toString()},
              false);
}
QString OnlineAuthService::localUserId() const
{
    QString id = m_userId;
    return id.remove('-');
}
QString OnlineAuthService::localFilesRoot() const
{
    return m_root + "/online/" + fingerprint(m_config.projectUrl);
}
bool OnlineAuthService::checkInput(const QString &email, const QString &password,
                                   bool checkPassword)
{
    if (!validEmail(email.trimmed()) || (checkPassword && !validPassword(password)))
    {
        emit failed(QStringLiteral("请输入有效邮箱；新密码须为12至128个字符。"));
        return false;
    }
    return true;
}
void OnlineAuthService::request(const QString &path, const QJsonObject &body, Completion done,
                                const QString &bearer, bool put)
{
    if (!configured())
    {
        emit failed(QStringLiteral("尚未配置联网账号服务，请先打开“服务配置”。"));
        return;
    }
    if (busy())
    {
        emit failed(QStringLiteral("正在处理上一项操作，请稍候。"));
        return;
    }
    QNetworkRequest req(QUrl(m_config.projectUrl + "/auth/v1/" + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", m_config.publishableKey.toUtf8());
    if (!bearer.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setTransferTimeout(qMin(15000, m_timeoutMs));
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto *reply = put ? m_network->put(req, payload) : m_network->post(req, payload);
    m_reply = reply;
    emit busyChanged(true);
    // 总时限独立于传输活动；限制体积，禁止信任服务端任意响应文本。
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply] { reply->abort(); });
    timeout->start(m_timeoutMs);
    connect(reply, &QIODevice::readyRead, this,
            [reply]
            {
                if (reply->bytesAvailable() > 65536)
                    reply->abort();
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timeout, done]
            {
                timeout->stop();
                reply->deleteLater();
                if (m_reply != reply)
                    return;
                m_reply.clear();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool transportOk = reply->error() == QNetworkReply::NoError;
                const auto body = reply->read(65537);
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(body, &error);
                const bool jsonOk =
                    (body.isEmpty() && status == 204) ||
                    (error.error == QJsonParseError::NoError && document.isObject());
                emit busyChanged(false);
                if (!transportOk || status < 200 || status >= 300 || body.size() > 65536 || !jsonOk)
                {
                    emit failed(safeError(status, body));
                    done({}, status >= 300 ? status : -1);
                    return;
                }
                done(document.object(), 0);
            });
}
void OnlineAuthService::cancel()
{
    if (m_reply)
    {
        auto reply = m_reply;
        m_reply.clear();
        reply->abort();
        reply->deleteLater();
        emit busyChanged(false);
    }
    if (loggedIn() && !m_refreshTimer.isActive())
        m_refreshTimer.start(1000);
}
bool OnlineAuthService::acceptSession(const QJsonObject &data, bool remember,
                                      const QString &expectedEmail)
{
    const auto user = data.value("user").toObject();
    if (!validSession(data) ||
        (!expectedEmail.isEmpty() &&
         expectedEmail.compare(user.value("email").toString(), Qt::CaseInsensitive) != 0))
    {
        emit failed(QStringLiteral("认证响应缺少有效且已验证的账号信息，未建立登录状态。"));
        return false;
    }
    m_access = data.value("access_token").toString();
    m_refresh = data.value("refresh_token").toString();
    m_userId = QUuid(user.value("id").toString()).toString(QUuid::WithoutBraces);
    m_email = user.value("email").toString();
    m_remember = remember;
    bool stored = true;
    if (remember)
    {
        stored = m_store->setApiKey(m_refresh, true);
        if (!stored)
        {
            m_store->clearApiKey();
            m_remember = false;
        }
    }
    else
        m_store->clearApiKey();
    m_refreshTimer.start(qMax(1000, (data.value("expires_in").toInt() - 60) * 1000));
    emit signedIn();
    if (!stored)
        emit failed(QStringLiteral("登录成功，但系统凭据保存失败，本次仅在内存中保持登录。"));
    return true;
}
void OnlineAuthService::clearSession(bool forget)
{
    m_refreshTimer.stop();
    m_access.clear();
    m_refresh.clear();
    m_userId.clear();
    m_email.clear();
    m_remember = false;
    if (forget && m_store)
        m_store->clearApiKey();
    emit signedOut();
}
void OnlineAuthService::login(const QString &email, const QString &password, bool remember)
{
    const QString address = email.trimmed();
    if (!checkInput(address) || password.isEmpty() || password.size() > 128)
    {
        emit failed(QStringLiteral("请填写邮箱和密码。"));
        return;
    }
    request("token?grant_type=password", {{"email", address}, {"password", password}},
            [this, remember, address](const QJsonObject &data, int code)
            {
                if (!code)
                    acceptSession(data, remember, address);
            });
}
void OnlineAuthService::signUp(const QString &email, const QString &password)
{
    const QString address = email.trimmed();
    if (!checkInput(address, password, true))
        return;
    request("signup", {{"email", address}, {"password", password}},
            [this](const QJsonObject &, int code)
            {
                if (!code)
                    emit completed(QStringLiteral(
                        "如果该邮箱可以注册，验证邮件已发送；请填写邮件中的验证码。"));
            });
}
void OnlineAuthService::confirmSignUp(const QString &email, const QString &code, bool remember)
{
    const QString address = email.trimmed();
    if (!checkInput(address) ||
        !QRegularExpression("^[0-9]{6,10}$").match(code.trimmed()).hasMatch())
    {
        emit failed(QStringLiteral("请输入邮件中的数字验证码。"));
        return;
    }
    request("verify", {{"email", address}, {"token", code.trimmed()}, {"type", "email"}},
            [this, remember, address](const QJsonObject &data, int status)
            {
                if (!status)
                    acceptSession(data, remember, address);
            });
}
void OnlineAuthService::resendConfirmation(const QString &email)
{
    if (!checkInput(email.trimmed()))
        return;
    request("resend", {{"email", email.trimmed()}, {"type", "signup"}},
            [this](const QJsonObject &, int code)
            {
                if (!code)
                    emit completed(
                        QStringLiteral("如果该邮箱需要验证，验证码已重新发送；请稍候查看邮件。"));
            });
}
void OnlineAuthService::requestReset(const QString &email)
{
    if (!checkInput(email.trimmed()))
        return;
    request("recover", {{"email", email.trimmed()}},
            [this](const QJsonObject &, int code)
            {
                if (!code)
                    emit completed(QStringLiteral(
                        "如果该邮箱已注册，找回密码邮件已发送，请填写验证码和新密码。"));
            });
}
void OnlineAuthService::resetPassword(const QString &email, const QString &code,
                                      const QString &password)
{
    const QString address = email.trimmed();
    if (!checkInput(address, password, true) ||
        !QRegularExpression("^[0-9]{6,10}$").match(code.trimmed()).hasMatch())
    {
        emit failed(QStringLiteral("请填写邮箱、邮件验证码和12至128个字符的新密码。"));
        return;
    }
    request(
        "verify", {{"email", address}, {"token", code.trimmed()}, {"type", "recovery"}},
        [this, address, password](const QJsonObject &data, int status)
        {
            if (status)
                return;
            if (!validSession(data) ||
                data.value("user").toObject().value("email").toString().compare(
                    address, Qt::CaseInsensitive) != 0)
            {
                emit failed(QStringLiteral("找回密码的认证响应无效，未修改密码。"));
                return;
            }
            const auto token = data.value("access_token").toString();
            request(
                "user", {{"password", password}},
                [this, token](const QJsonObject &, int code)
                {
                    if (code)
                        return;
                    clearSession(true);
                    request(
                        "logout?scope=global", {},
                        [this](const QJsonObject &, int revoked)
                        {
                            emit completed(
                                revoked
                                    ? QStringLiteral(
                                          "密码已更新，请重新登录；其他会话撤销未获服务端确认。")
                                    : QStringLiteral(
                                          "密码已更新，旧刷新会话已撤销，请用新密码登录。"));
                        },
                        token);
                },
                token, true);
        });
}
void OnlineAuthService::restore()
{
    if (!configured())
        return;
    if (busy())
    {
        if (loggedIn())
            m_refreshTimer.start(1000);
        return;
    }
    const auto refresh = m_refresh.isEmpty() ? m_store->apiKey() : m_refresh;
    if (refresh.isEmpty())
        return;
    const bool remember = m_refresh.isEmpty() || m_remember;
    const QString previous = m_userId;
    request("token?grant_type=refresh_token", {{"refresh_token", refresh}},
            [this, remember, previous](const QJsonObject &data, int code)
            {
                if (code)
                {
                    clearSession(code == 400 || code == 401 || code == 403);
                    return;
                }
                if (!previous.isEmpty() &&
                    QUuid(data.value("user").toObject().value("id").toString())
                            .toString(QUuid::WithoutBraces) != previous)
                {
                    clearSession(true);
                    emit failed(QStringLiteral("刷新后的账号不一致，已退出登录。"));
                    return;
                }
                if (!acceptSession(data, remember))
                    clearSession(false);
            });
}
void OnlineAuthService::logout()
{
    cancel();
    const QString access = m_access;
    clearSession(true);
    if (access.isEmpty())
        return;
    request(
        "logout?scope=local", {},
        [this](const QJsonObject &, int code)
        {
            emit completed(code ? QStringLiteral("本机已退出；网络异常，服务器会话撤销未确认。")
                                : QStringLiteral("已退出联网账号，工程保留在本地。"));
        },
        access);
}
} // namespace lingya
