#include "services/FileService.h"
#include "domain/Limits.h"
#include "domain/ProjectValidator.h"
#include "services/ProjectJson.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace lingya {


namespace FileService {

std::variant<FileStamp, AppError> stamp(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) return AppError{"FILE_NOT_FOUND", QStringLiteral("文件不存在：%1").arg(path)};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return AppError{"FILE_READ_FAILED", QStringLiteral("无法读取文件：%1").arg(file.errorString())};
    FileStamp result;
    result.sha256 =
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    result.mtimeUtc = info.lastModified().toUTC();
    return result;
}

namespace {

constexpr qsizetype kMaxSavedVersions = 10;
const QString kLegacyVersion = QStringLiteral("legacy");

bool redirected(const QFileInfo &info)
{
    return info.isSymLink() || info.isJunction();
}

QString backupDirectory(const QFileInfo &projectFile)
{
    return projectFile.dir().filePath(QStringLiteral("灵芽备份"));
}

QString historyDirectory(const QFileInfo &projectFile, const ID &projectId)
{
    auto name = projectFile.fileName();
#ifdef Q_OS_WIN
    name = name.toCaseFolded();
#endif
    const auto identity = name.toUtf8() + '\0' + projectId.toUtf8();
    const auto digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    return QDir(backupDirectory(projectFile))
        .filePath(QStringLiteral("版本历史-%1").arg(QString::fromLatin1(digest)));
}

QString legacyBackupPath(const QFileInfo &projectFile)
{
    return QDir(backupDirectory(projectFile)).filePath(projectFile.fileName() + ".bak");
}

bool safeDirectory(const QString &path)
{
    const QFileInfo info(path);
    return !redirected(info) && (!info.exists() || info.isDir());
}

bool isVersionId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\Asaved-[0-9]{13}\.lingya\z)"));
    return pattern.match(id).hasMatch();
}

QFileInfoList archivedVersions(const QDir &dir)
{
    QFileInfoList result;
    const auto entries = dir.entryInfoList({"saved-*.lingya"}, QDir::Files | QDir::NoSymLinks,
                                           QDir::Name | QDir::Reversed);
    for (const auto &entry : entries)
        if (isVersionId(entry.fileName()) && !redirected(entry))
            result.append(entry);
    return result;
}

bool sameBytes(const QFileInfo &file, const QByteArray &bytes)
{
    if (file.size() != bytes.size())
        return false;
    QFile input(file.absoluteFilePath());
    return input.open(QIODevice::ReadOnly) && input.read(bytes.size() + 1) == bytes;
}

// 外部修改检测（§11.3）：磁盘当前内容与加载时摘要比对；文件被外部删除同样视为冲突，
// 不静默重建覆盖用户在外部做的删除决定。
std::variant<bool, AppError> checkExternalModification(const QString& path,
                                                       const FileStamp& expectedStamp) {
    const auto current = stamp(path);
    if (std::holds_alternative<AppError>(current)) {
        return AppError{"EXTERNAL_MODIFICATION",
                        QStringLiteral("文件已在外部被删除或无法读取，请另存到其他位置：%1").arg(path)};
    }
    if (std::get<FileStamp>(current).sha256 != expectedStamp.sha256) {
        return AppError{"EXTERNAL_MODIFICATION",
                        QStringLiteral("文件已在外部被修改，为避免覆盖外部改动已取消保存，请另存或先处理外部修改：%1").arg(path)};
    }
    return true;
}

}  // namespace

std::variant<qint64, AppError> save(const QString& path, const Project& project) {
    if (path.isEmpty()) return AppError{"INVALID_ARGUMENT", "保存路径为空"};

    // 先校验再写出，不把非法工程写盘（规范§8）
    const QVector<AppError> problems = ProjectValidator::validate(project);
    if (!problems.isEmpty())
        return AppError{"INVALID_PROJECT",
                        "工程未通过校验，已取消保存：" + problems.first().message};

    const QByteArray bytes = ProjectJson::toBytes(project);
    if (bytes.size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", "工程文件超过10MiB上限，请拆分工作表后再保存"};

    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    // 旧文件存在时先备份到专用子目录"灵芽备份"（不再在原目录留散落.bak）；
    // 首次保存（目标不存在）无旧内容可备份；备份失败提示另存或取消，不移动或删除唯一原文件（规范§8/§11.3）
    if (info.exists()) {
        const QString backupDir = backupDirectory(info);
        if (!safeDirectory(backupDir) || !QDir().mkpath(backupDir))
            return AppError{"BACKUP_FAILED", "无法创建备份目录，请另存到其他位置"};
        const QString backup = legacyBackupPath(info);
        if (redirected(QFileInfo(backup)))
            return AppError{"BACKUP_FAILED", "备份路径被重定向，请另存到其他位置"};
        QFile::remove(backup);
        if (!QFile::copy(path, backup))
            return AppError{"BACKUP_FAILED", "无法创建备份副本，请另存到其他位置"};
    }

    QSaveFile file(path);
    // 不为了写入成功直接截断原文件（规范§8 [Q3]）
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return AppError{"FILE_WRITE_FAILED", "无法写入文件：" + file.errorString()};
    const qint64 written = file.write(bytes);
    if (written != bytes.size()) {
        file.cancelWriting();
        return AppError{"FILE_WRITE_FAILED", "写入不完整，已取消替换原文件"};
    }
    if (!file.commit())
        return AppError{"FILE_WRITE_FAILED", "保存失败：" + file.errorString()};
    return project.revision;
}

std::variant<qint64, AppError> save(const QString& path, const Project& project,
                                    const FileStamp& expectedStamp) {
    // 覆盖已加载文件：先做外部修改检测，再走统一保存流程（§11.3）
    const auto unchanged = checkExternalModification(path, expectedStamp);
    if (std::holds_alternative<AppError>(unchanged)) return std::get<AppError>(unchanged);
    return save(path, project);
}

std::variant<Project, AppError> load(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) return AppError{"FILE_NOT_FOUND", "文件不存在：" + path};
    if (info.size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", "文件超过10MiB上限，已拒绝加载"};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return AppError{"FILE_READ_FAILED", "无法读取文件：" + file.errorString()};
    const QByteArray bytes = file.readAll();

    const auto parsed = ProjectJson::fromBytes(bytes);
    if (std::holds_alternative<AppError>(parsed)) return std::get<AppError>(parsed);

    const Project project = std::get<Project>(parsed);
    const QVector<AppError> problems = ProjectValidator::validate(project);
    if (!problems.isEmpty())
        return AppError{"INVALID_PROJECT", "工程校验未通过：" + problems.first().message};
    return project;
}

std::variant<bool, AppError> recordVersion(const QString &path, const Project &project)
{
    const QFileInfo projectFile(path);
    const auto history = historyDirectory(projectFile, project.id);
    if (!projectFile.isFile() || project.id.isEmpty() ||
        !safeDirectory(backupDirectory(projectFile)) || !safeDirectory(history) ||
        !QDir().mkpath(history))
        return AppError{"HISTORY_WRITE_FAILED", QStringLiteral("无法创建本机版本目录")};

    const QDir dir(history);
    const auto lockPath = dir.filePath(QStringLiteral("版本记录.lock"));
    if (redirected(QFileInfo(lockPath)))
        return AppError{"HISTORY_WRITE_FAILED", QStringLiteral("本机版本目录的锁路径异常")};
    QLockFile lock(lockPath);
    if (!lock.tryLock())
        return AppError{"HISTORY_BUSY", QStringLiteral("另一个进程正在写入本机版本历史")};

    const auto versions = archivedVersions(dir);
    const auto bytes = ProjectJson::toBytes(project);
    if (bytes.size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("本机版本超过工程大小上限")};

    const bool duplicate = !versions.isEmpty() && sameBytes(versions.front(), bytes);
    if (!duplicate)
    {
        auto time = QDateTime::currentMSecsSinceEpoch();
        if (!versions.isEmpty())
            time = qMax(time, versions.front().fileName().mid(6, 13).toLongLong() + 1);
        QString snapshot;
        do
        {
            const auto serial = QString::number(time++).rightJustified(13, u'0');
            snapshot = dir.filePath(QStringLiteral("saved-%1.lingya").arg(serial));
        } while (QFileInfo::exists(snapshot) || redirected(QFileInfo(snapshot)));
        QSaveFile file(snapshot);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return AppError{"HISTORY_WRITE_FAILED", QStringLiteral("无法写入本机版本副本")};
    }

    const auto retained = duplicate ? versions : archivedVersions(dir);
    bool cleaned = true;
    for (qsizetype index = kMaxSavedVersions; index < retained.size(); ++index)
    {
        const QFileInfo entry(retained.at(index).absoluteFilePath());
        if (entry.isFile() && !redirected(entry))
            cleaned = QFile::remove(entry.absoluteFilePath()) && cleaned;
    }
    if (!cleaned)
        return AppError{"HISTORY_CLEANUP_FAILED", QStringLiteral("无法清理超出保留数量的旧版本")};
    return !duplicate;
}

std::variant<QVector<SavedVersion>, AppError> listVersions(const QString &path, const ID &projectId)
{
    const QFileInfo projectFile(path);
    const auto history = historyDirectory(projectFile, projectId);
    if (path.isEmpty() || projectId.isEmpty() || !safeDirectory(backupDirectory(projectFile)) ||
        !safeDirectory(history))
        return AppError{"HISTORY_READ_FAILED", QStringLiteral("本机版本目录无效或已被重定向")};

    const auto files = archivedVersions(QDir(history));
    QVector<SavedVersion> versions;
    for (const auto &file : files)
    {
        const auto id = file.fileName();
        versions.append({id, QDateTime::fromMSecsSinceEpoch(id.mid(6, 13).toLongLong()), false});
    }

    const QFileInfo backup(legacyBackupPath(projectFile));
    if (std::holds_alternative<Project>(loadVersion(path, projectId, kLegacyVersion)))
    {
        QFile file(backup.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly))
        {
            const auto bytes = file.read(Limits::kMaxFileBytes + 1);
            if (bytes.size() <= Limits::kMaxFileBytes &&
                std::none_of(files.cbegin(), files.cend(),
                             [&bytes](const QFileInfo &entry) { return sameBytes(entry, bytes); }))
                versions.append({kLegacyVersion, backup.lastModified(), true});
        }
    }
    std::stable_sort(versions.begin(), versions.end(),
                     [](const auto &a, const auto &b) { return a.savedAt > b.savedAt; });
    return versions;
}

std::variant<Project, AppError> loadVersion(const QString &path, const ID &projectId,
                                            const QString &versionId)
{
    const QFileInfo projectFile(path);
    const auto history = historyDirectory(projectFile, projectId);
    const bool legacy = versionId == kLegacyVersion;
    if (path.isEmpty() || projectId.isEmpty() || (!legacy && !isVersionId(versionId)) ||
        !safeDirectory(backupDirectory(projectFile)) || (!legacy && !safeDirectory(history)))
        return AppError{"HISTORY_READ_FAILED", QStringLiteral("本机版本路径无效")};
    const QFileInfo version(legacy ? legacyBackupPath(projectFile)
                                   : QDir(history).filePath(versionId));
    if (!version.isFile() || redirected(version))
        return AppError{"HISTORY_READ_FAILED", QStringLiteral("本机版本已移除或不可读取")};
    auto loaded = load(version.absoluteFilePath());
    if (const auto *project = std::get_if<Project>(&loaded); project && project->id != projectId)
        return AppError{"HISTORY_READ_FAILED", QStringLiteral("该版本不属于当前工程")};
    return loaded;
}

}  // namespace FileService

}  // namespace lingya
