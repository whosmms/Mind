#include "services/SessionService.h"
#include "domain/Limits.h"
#include "domain/TitleRules.h"
#include "services/LocalNameRules.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace lingya
{

namespace SessionService
{

namespace
{

// 账号记录（内存形态；磁盘为accounts.json内对象）
struct AccountRecord
{
    QString name;
    QString salt;
    QString hash;
    QDateTime createdAt;
};

QString accountsPath(const QString &root)
{
    return root + QStringLiteral("/accounts.json");
}

// 盐+口令SHA-256十六进制；不存明文（课程级本地隔离，见头文件说明）
QString hashPassword(const QString &salt, const QString &password)
{
    const QByteArray bytes = salt.toUtf8() + QStringLiteral("#").toUtf8() + password.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// 读账号库；文件不存在返回空库（首次注册时创建）；损坏返回错误（不静默重建覆盖既有账号）
std::variant<QVector<AccountRecord>, AppError> loadAccounts(const QString &root)
{
    QVector<AccountRecord> records;
    const QString path = accountsPath(root);
    if (!QFileInfo::exists(path))
        return records;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return AppError{"FILE_READ_FAILED",
                        QStringLiteral("无法读取账号库：%1").arg(file.errorString())};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return AppError{"INVALID_PROJECT", QStringLiteral("账号库格式损坏，拒绝加载")};
    const QJsonObject rootObject = doc.object();
    if (rootObject.value(QStringLiteral("schemaVersion")).toInt() != 1)
        return AppError{"UNSUPPORTED_SCHEMA", QStringLiteral("账号库版本不支持")};
    if (!rootObject.value(QStringLiteral("users")).isArray())
        return AppError{"INVALID_PROJECT", QStringLiteral("账号库用户列表损坏，拒绝加载")};
    const QJsonArray users = rootObject.value(QStringLiteral("users")).toArray();
    for (const auto &value : users)
    {
        const QJsonObject user = value.toObject();
        AccountRecord record;
        record.name = user.value(QStringLiteral("name")).toString();
        record.salt = user.value(QStringLiteral("salt")).toString();
        record.hash = user.value(QStringLiteral("hash")).toString();
        record.createdAt = QDateTime::fromString(user.value(QStringLiteral("createdAt")).toString(),
                                                 Qt::ISODateWithMs);
        if (!validUserName(record.name) || record.salt.isEmpty() || record.hash.isEmpty())
            return AppError{"INVALID_PROJECT", QStringLiteral("账号库存在不完整记录，拒绝加载")};
        for (const AccountRecord &existing : records)
            if (existing.name.compare(record.name, Qt::CaseInsensitive) == 0)
                return AppError{"INVALID_PROJECT",
                                QStringLiteral("账号库存在重复的文件目录身份，拒绝加载")};
        records.append(record);
    }
    return records;
}

bool saveAccounts(const QString &root, const QVector<AccountRecord> &records)
{
    QJsonArray users;
    for (const AccountRecord &record : records)
    {
        users.append(QJsonObject{
            {QStringLiteral("name"), record.name},
            {QStringLiteral("salt"), record.salt},
            {QStringLiteral("hash"), record.hash},
            {QStringLiteral("createdAt"), record.createdAt.toString(Qt::ISODateWithMs)}});
    }
    const QJsonObject rootObject{{QStringLiteral("schemaVersion"), 1},
                                 {QStringLiteral("users"), users}};
    if (!QDir().mkpath(root))
        return false;
    QSaveFile file(accountsPath(root));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = QJsonDocument(rootObject).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QString normalizeUserName(const QString &raw)
{
    return raw.trimmed();
}

} // namespace

bool validUserName(const QString &userName)
{
    const QString name = normalizeUserName(userName);
    if (TitleRules::codePointCount(name) < 2 || TitleRules::codePointCount(name) > 32)
        return false;
    return LocalNameRules::validComponent(name);
}

std::variant<bool, AppError> createAccount(const QString &root, const QString &userName,
                                           const QString &password)
{
    if (root.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("账号根目录为空")};
    const QString name = normalizeUserName(userName);
    if (!validUserName(name))
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("用户名需为2至32个字符，且不含路径或文件名非法字符")};
    const QString trimmedPassword = password.trimmed();
    if (TitleRules::codePointCount(trimmedPassword) < 6 ||
        TitleRules::codePointCount(trimmedPassword) > 64)
        return AppError{"INVALID_ARGUMENT", QStringLiteral("密码需为6至64个字符")};

    auto loaded = loadAccounts(root);
    if (std::holds_alternative<AppError>(loaded))
        return std::get<AppError>(loaded);
    QVector<AccountRecord> records = std::get<QVector<AccountRecord>>(loaded);
    for (const AccountRecord &record : records)
        if (record.name.compare(name, Qt::CaseInsensitive) == 0)
            return AppError{"CONFLICT", QStringLiteral("用户名已被注册：%1").arg(name)};

    AccountRecord record;
    record.name = name;
    record.salt = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.hash = hashPassword(record.salt, trimmedPassword);
    record.createdAt = QDateTime::currentDateTimeUtc();
    records.append(record);
    if (!saveAccounts(root, records))
        return AppError{"FILE_WRITE_FAILED", QStringLiteral("账号库写入失败")};
    return true;
}

std::variant<AccountInfo, AppError> login(const QString &root, const QString &userName,
                                          const QString &password)
{
    if (root.isEmpty())
        return AppError{"INVALID_ARGUMENT", QStringLiteral("账号根目录为空")};
    const QString name = normalizeUserName(userName);
    auto loaded = loadAccounts(root);
    if (std::holds_alternative<AppError>(loaded))
        return std::get<AppError>(loaded);
    const QVector<AccountRecord> records = std::get<QVector<AccountRecord>>(loaded);
    for (const AccountRecord &record : records)
    {
        if (record.name != name)
            continue;
        if (record.hash != hashPassword(record.salt, password.trimmed()))
            return AppError{"AUTH_FAILED", QStringLiteral("用户名或密码错误")};
        AccountInfo info;
        info.userName = record.name;
        info.createdAt = record.createdAt;
        return info;
    }
    return AppError{"AUTH_FAILED", QStringLiteral("用户名或密码错误")};
}

} // namespace SessionService

} // namespace lingya
