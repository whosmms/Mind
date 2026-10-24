#include "services/RecoveryService.h"
#include "domain/Limits.h"
#include "domain/ProjectValidator.h"
#include "services/ProjectJson.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace lingya
{

namespace RecoveryService
{

namespace
{

// 快照文件名 = 毫秒时间戳（当前位数固定，数值序即时间序）；只有这种名字是本服务创建的
QString snapshotFileName(qint64 msecs)
{
    return QString::number(msecs) + QStringLiteral(".lingya");
}

// 工程ID与快照基名必须是单一路径分量，拒绝分隔符与相对路径片段（防止目录逃逸）
bool isSafeComponent(const QString &value)
{
    return !value.isEmpty() && !value.contains(QLatin1Char('/')) &&
           !value.contains(QLatin1Char('\\')) && value != QStringLiteral(".") &&
           value != QStringLiteral("..");
}

QString projectDirPath(const QString &root, const QString &projectId)
{
    return root + QLatin1Char('/') + projectId;
}

QString snapshotPath(const QString &root, const QString &projectId, qint64 msecs)
{
    return projectDirPath(root, projectId) + QLatin1Char('/') + snapshotFileName(msecs);
}

// 目录内本服务创建的快照时间戳（数值，新→旧）；其他名称的文件一律不认、不删
QVector<qint64> snapshotStems(const QString &dirPath)
{
    const QDir dir(dirPath);
    QVector<qint64> stems;
    if (!dir.exists())
        return stems;
    const QStringList names =
        dir.entryList(QStringList{QStringLiteral("*.lingya")}, QDir::Files, QDir::Name);
    for (const QString &name : names)
    {
        const QString stem = name.left(name.size() - QStringLiteral(".lingya").size());
        bool ok = false;
        const qint64 value = stem.toLongLong(&ok);
        if (ok && QString::number(value) == stem)
            stems.append(value);
    }
    std::sort(stems.begin(), stems.end(), std::greater<qint64>());
    return stems;
}

QByteArray readAllBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    // 最多读取上限加1字节，超限交由parseSnapshot拒绝，避免先分配整个超大文件。
    return file.read(Limits::kMaxFileBytes + 1);
}

// 完整校验一份快照字节：体积→JSON严格解析→ProjectValidator（与FileService::load同序，规范§8）
std::variant<Project, AppError> parseSnapshot(const QByteArray &bytes)
{
    if (bytes.size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", "快照超过10MiB上限，已拒绝加载"};
    const auto parsed = ProjectJson::fromBytes(bytes);
    if (std::holds_alternative<AppError>(parsed))
        return std::get<AppError>(parsed);
    const Project project = std::get<Project>(parsed);
    const QVector<AppError> problems = ProjectValidator::validate(project);
    if (!problems.isEmpty())
        return AppError{"INVALID_PROJECT", "快照工程校验未通过：" + problems.first().message};
    return project;
}

// 每工程保留最近kMaxRecoverySnapshots份成功快照（规范§11.2）：
// 从新到旧数，通过校验的前5份保留；更旧的成功快照删除。
// 损坏/不可读的文件既不占用保留名额也不删除（可能是暂时读取失败，留给人工处置）。
void pruneSnapshots(const QString &root, const QString &projectId)
{
    const QString dirPath = projectDirPath(root, projectId);
    int kept = 0;
    const QVector<qint64> stems = snapshotStems(dirPath);
    for (const qint64 stem : stems)
    {
        // 损坏、不可读或工程ID不符的文件始终保留，不因已数满5份就误删。
        if (!std::holds_alternative<Project>(load(root, projectId, QString::number(stem))))
            continue;
        if (++kept > Limits::kMaxRecoverySnapshots)
            QFile::remove(snapshotPath(root, projectId, stem));
    }
}

} // namespace

QString defaultRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        return QString();
    return base + QStringLiteral("/recovery");
}

QVector<SnapshotInfo> list(const QString &root, const QString &projectId)
{
    QVector<SnapshotInfo> result;
    if (root.isEmpty() || !isSafeComponent(projectId))
        return result;
    for (const qint64 stem : snapshotStems(projectDirPath(root, projectId)))
    {
        const auto parsed = parseSnapshot(readAllBytes(snapshotPath(root, projectId, stem)));
        if (!std::holds_alternative<Project>(parsed))
            continue; // 损坏快照跳过
        const Project project = std::get<Project>(parsed);
        if (project.id != projectId)
            continue; // 目录名与内容不符的防御
        SnapshotInfo info;
        info.projectId = projectId;
        info.snapshotId = QString::number(stem);
        info.title = project.title;
        info.revision = project.revision;
        info.savedAt = QDateTime::fromMSecsSinceEpoch(stem);
        result.append(info);
    }
    return result;
}

QVector<SnapshotInfo> listAll(const QString &root)
{
    QVector<SnapshotInfo> result;
    const QDir rootDir(root);
    if (!rootDir.exists())
        return result;
    const QStringList projectIds = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &projectId : projectIds)
        result.append(list(root, projectId));
    std::sort(result.begin(), result.end(),
              [](const SnapshotInfo &a, const SnapshotInfo &b)
              {
                  if (a.savedAt != b.savedAt)
                      return a.savedAt > b.savedAt; // 新→旧
                  return a.snapshotId.toLongLong() > b.snapshotId.toLongLong();
              });
    return result;
}

std::variant<Project, AppError> load(const QString &root, const QString &projectId,
                                     const QString &snapshotId)
{
    if (root.isEmpty() || !isSafeComponent(projectId) || !isSafeComponent(snapshotId))
        return AppError{"INVALID_ARGUMENT", "快照标识无效"};
    const QByteArray bytes = readAllBytes(projectDirPath(root, projectId) + QLatin1Char('/') +
                                          snapshotId + QStringLiteral(".lingya"));
    if (bytes.isEmpty())
        return AppError{"FILE_NOT_FOUND", "快照不存在或无法读取"};
    const auto parsed = parseSnapshot(bytes);
    if (const auto *project = std::get_if<Project>(&parsed);
        project != nullptr && project->id != projectId)
        return AppError{"CONFLICT", "快照工程与所选工程不一致，请重新选择快照"};
    return parsed;
}

std::variant<SnapshotWrite, AppError> writeSnapshot(const QString &root, const Project &project)
{
    if (root.isEmpty() || !isSafeComponent(project.id))
        return AppError{"INVALID_ARGUMENT", "恢复快照需要有效的恢复根目录与工程ID"};

    // 与正式保存同规：先校验再写出，非法工程不留快照（写出的快照必须能通过load）
    const QVector<AppError> problems = ProjectValidator::validate(project);
    if (!problems.isEmpty())
        return AppError{"INVALID_PROJECT",
                        "工程未通过校验，已跳过恢复快照：" + problems.first().message};
    const QByteArray bytes = ProjectJson::toBytes(project);
    if (bytes.size() > Limits::kMaxFileBytes)
        return AppError{"LIMIT_EXCEEDED", "工程超过10MiB上限，无法写恢复快照"};

    const QString dirPath = projectDirPath(root, project.id);
    if (!QDir().mkpath(dirPath))
        return AppError{"FILE_WRITE_FAILED", "无法创建恢复快照目录"};

    // 仅当内容变化时写一份（规范§11.3）：与该工程最新一份快照按序列化字节比对
    const QVector<qint64> stems = snapshotStems(dirPath);
    if (!stems.isEmpty())
    {
        const QByteArray previous = readAllBytes(snapshotPath(root, project.id, stems.first()));
        if (previous == bytes)
            return SnapshotWrite{QString::number(stems.first()), false};
    }

    // 文件名取毫秒时间戳；同毫秒已存在时递增保证唯一（数值序即时间序）
    qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    while (QFileInfo::exists(snapshotPath(root, project.id, stamp)))
        ++stamp;

    QSaveFile file(snapshotPath(root, project.id, stamp));
    file.setDirectWriteFallback(false); // 与正式保存一致：不因写入失败截断既有快照（规范§8）
    if (!file.open(QIODevice::WriteOnly))
        return AppError{"FILE_WRITE_FAILED", "无法写入恢复快照：" + file.errorString()};
    if (file.write(bytes) != bytes.size())
    {
        file.cancelWriting();
        return AppError{"FILE_WRITE_FAILED", "恢复快照写入不完整，已取消"};
    }
    if (!file.commit())
        return AppError{"FILE_WRITE_FAILED", "恢复快照保存失败：" + file.errorString()};

    pruneSnapshots(root, project.id);
    return SnapshotWrite{QString::number(stamp), true};
}

void clear(const QString &root, const QString &projectId)
{
    if (root.isEmpty() || !isSafeComponent(projectId))
        return;
    const QString dirPath = projectDirPath(root, projectId);
    // 只删除本服务创建的快照文件；目录里其他名称的文件保留（规范§11.3不自伤用户文件）
    for (const qint64 stem : snapshotStems(dirPath))
        QFile::remove(snapshotPath(root, projectId, stem));
    QDir(root).rmdir(projectId); // 目录已空则一并移除；仍有他文件时保留目录
}

} // namespace RecoveryService

} // namespace lingya
