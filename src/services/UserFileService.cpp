#include "services/UserFileService.h"
#include "services/FileService.h"
#include "services/LocalNameRules.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace lingya
{

namespace UserFileService
{

namespace
{

// 去首尾空白后检查Windows名称，在任何文件操作前拒绝路径/设备/流名。
bool validFileName(const QString &raw, QString *cleaned)
{
    const QString name = raw.trimmed();
    if (!LocalNameRules::validComponent(name))
        return false;
    if (cleaned != nullptr)
        *cleaned = name;
    return true;
}

QString ownedPath(const QString &root, const QString &userName, const QString &rawName)
{
    QString name = rawName.trimmed();
    if (!name.endsWith(QStringLiteral(".lingya"), Qt::CaseInsensitive))
        name += QStringLiteral(".lingya");
    return userDir(root, userName) + QLatin1Char('/') + name;
}

} // namespace

QString userDir(const QString &root, const QString &userName)
{
    return root + QStringLiteral("/users/") + userName.trimmed();
}

std::variant<QString, AppError> saveOwned(const QString &root, const QString &userName,
                                          const QString &name, const Project &project)
{
    if (root.isEmpty() || !LocalNameRules::validComponent(userName.trimmed()))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("保存需要有效的账号与目录")};
    QString cleaned;
    if (!validFileName(name, &cleaned))
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("文件名不能为空，且不能使用Windows保留名称或非法字符")};
    if (!QDir().mkpath(userDir(root, userName)))
        return AppError{"FILE_WRITE_FAILED", QStringLiteral("无法创建用户文件目录")};
    const QString path = ownedPath(root, userName, cleaned);
    const auto saved = FileService::save(path, project);
    if (std::holds_alternative<AppError>(saved))
        return std::get<AppError>(saved);
    return path;
}

QVector<OwnedFileInfo> listOwned(const QString &root, const QString &userName)
{
    QVector<OwnedFileInfo> result;
    if (root.isEmpty() || !LocalNameRules::validComponent(userName.trimmed()))
        return result;
    const QDir dir(userDir(root, userName));
    if (!dir.exists())
        return result;
    const QFileInfoList entries =
        dir.entryInfoList(QStringList{QStringLiteral("*.lingya")}, QDir::Files);
    for (const QFileInfo &entry : entries)
    {
        OwnedFileInfo info;
        info.name = entry.completeBaseName();
        info.modified = entry.lastModified();
        info.bytes = entry.size();
        result.append(info);
    }
    std::sort(result.begin(), result.end(),
              [](const OwnedFileInfo &a, const OwnedFileInfo &b)
              {
                  if (a.modified != b.modified)
                      return a.modified > b.modified; // 新→旧
                  return a.name < b.name;
              });
    return result;
}

std::variant<Project, AppError> openOwned(const QString &root, const QString &userName,
                                          const QString &name)
{
    if (root.isEmpty() || !LocalNameRules::validComponent(userName.trimmed()))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("打开需要有效的账号与目录")};
    QString cleaned;
    if (!validFileName(name, &cleaned))
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("文件名不能为空，且不能使用Windows保留名称或非法字符")};
    const QString path = ownedPath(root, userName, cleaned);
    if (!QFileInfo::exists(path))
        return AppError{"FILE_NOT_FOUND", QStringLiteral("文件不存在：%1").arg(cleaned)};
    return FileService::load(path);
}

UserFileService::Access classify(const QString &root, const QString &userName, const QString &path)
{
    const QString usersRoot = QDir::cleanPath(QDir(root).absoluteFilePath(QStringLiteral("users")));
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString me = userName.trimmed();
    if (absolute.size() <= usersRoot.size() + 1 ||
        !absolute.startsWith(usersRoot + QLatin1Char('/'), Qt::CaseInsensitive))
        return Access::Public;
    const QString relative = absolute.mid(usersRoot.size() + 1);
    // users根下直接散置的文件（不在任何用户子文件夹内）不属于任何用户：
    // 按D-M2-02边界澄清视为公有（BUG-013：未登录存入users根的文件登录后须可打开）
    if (!relative.contains(QLatin1Char('/')))
        return Access::Public;
    const QString firstSegment = relative.section(QLatin1Char('/'), 0, 0); // 首段=用户目录名
    if (firstSegment.compare(me, Qt::CaseInsensitive) == 0)
        return Access::Own;
    return Access::Others;
}

} // namespace UserFileService

} // namespace lingya
