// 部署专项工具：固定访问Qt公开页面，不发送密钥或调用模型接口，不计QtTest用例。
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <cstdio>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main(int argc, char **argv)
{
    static_assert(QT_VERSION == QT_VERSION_CHECK(6, 8, 3), "使用冻结Qt 6.8.3");
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3 || args[1] != "--report" || QFileInfo(args[2]).suffix() != "json")
        return 2;
    QJsonObject report{
        {"qtVersion", qVersion()},
        {"endpoint", "https://www.qt.io/"},
        {"usesApiKey", false},
        {"backendSelected", QSslSocket::setActiveBackend("schannel")},
        {"availableBackends", QJsonArray::fromStringList(QSslSocket::availableBackends())}};
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QStringLiteral("https://www.qt.io/"))};
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(15000);
    auto *reply = manager.get(request);
    int sslErrors = 0;
    bool timedOut = false;
    QObject::connect(reply, &QNetworkReply::sslErrors, &app,
                     [&](const QList<QSslError> &errors) { sslErrors += errors.size(); });
    QObject::connect(reply, &QIODevice::readyRead, &app, [reply] { reply->readAll(); });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app,
                     [&]
                     {
                         timedOut = true;
                         reply->abort();
                     });
    QObject::connect(reply, &QNetworkReply::finished, &app, &QCoreApplication::quit);
    timeout.start(16000);
    app.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString backend = QSslSocket::activeBackend();
    QString loaded;
#ifdef Q_OS_WIN
    wchar_t path[32768]{};
    if (const auto module = GetModuleHandleW(L"qschannelbackend.dll"))
        if (GetModuleFileNameW(module, path, DWORD(std::size(path))) != 0)
            loaded = QString::fromWCharArray(path);
#endif
    const bool success = !timedOut && sslErrors == 0 && reply->error() == QNetworkReply::NoError &&
                         status >= 200 && status < 400 && backend == "schannel" &&
                         !loaded.isEmpty();
    report["success"] = success;
    report["timedOut"] = timedOut;
    report["sslErrors"] = sslErrors;
    report["httpStatus"] = status;
    report["networkError"] = int(reply->error());
    report["backend"] = backend;
    report["loadedTlsPlugin"] = loaded;
    QSaveFile output(args[2]);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
        return 2;
    std::printf("success=%s http=%d backend=%s\n", success ? "true" : "false", status,
                backend.toUtf8().constData());
    return success ? 0 : 1;
}
