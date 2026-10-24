#include "services/FileService.h"
#include "domain/ProjectValidator.h"
#include "domain/Limits.h"
#include "services/ProjectJson.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace lingya {


namespace FileService {

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

    // 旧文件存在时先创建独立.bak副本；备份失败提示另存或取消，不移动或删除唯一原文件（规范§8）
    if (info.exists()) {
        const QString backup = path + ".bak";
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

}  // namespace FileService

}  // namespace lingya
