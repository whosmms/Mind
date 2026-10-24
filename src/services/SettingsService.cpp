#include "services/SettingsService.h"

#include "domain/Limits.h"
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

namespace lingya
{

namespace
{
constexpr auto kRecentKey = "recentFiles";

QStringList readRaw(const QSettings &settings)
{
    return settings.value(QLatin1String(kRecentKey)).toStringList();
}

void writeRaw(QSettings &settings, const QStringList &items)
{
    settings.setValue(QLatin1String(kRecentKey), items);
}

// Windows路径大小写不敏感，去重与移除按不区分大小写比较，避免同一文件出现两条记录
bool samePath(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive) == 0;
}

// 规范化并过滤空项；不要求文件存在（记录可以先于文件存在性判断）
QString normalize(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}
} // namespace

namespace SettingsService
{

QVector<QString> recentFiles(const QSettings &settings)
{
    QVector<QString> result;
    for (const QString &item : readRaw(settings))
    {
        if (item.trimmed().isEmpty())
            continue;
        const QString path = normalize(item);
        bool duplicate = false;
        for (const QString &seen : result)
            duplicate = duplicate || samePath(seen, path);
        if (!duplicate)
            result.append(path);
    }
    while (result.size() > Limits::kMaxRecentFiles)
        result.removeLast();
    return result;
}

void addRecentFile(QSettings &settings, const QString &path)
{
    if (path.trimmed().isEmpty())
        return;
    const QString normalized = normalize(path);
    QStringList items{normalized};
    for (const QString &item : readRaw(settings))
    {
        if (item.trimmed().isEmpty())
            continue;
        const QString candidate = normalize(item);
        if (!samePath(candidate, normalized))
            items.append(candidate);
    }
    while (items.size() > Limits::kMaxRecentFiles)
        items.removeLast();
    writeRaw(settings, items);
}

void removeRecentFile(QSettings &settings, const QString &path)
{
    const QString normalized = normalize(path);
    QStringList items;
    for (const QString &item : readRaw(settings))
    {
        if (item.trimmed().isEmpty())
            continue;
        if (!samePath(normalize(item), normalized))
            items.append(item);
    }
    writeRaw(settings, items);
}

QVector<QString> recentFiles()
{
    QSettings settings;
    return recentFiles(settings);
}

void addRecentFile(const QString &path)
{
    QSettings settings;
    addRecentFile(settings, path);
}

void removeRecentFile(const QString &path)
{
    QSettings settings;
    removeRecentFile(settings, path);
}

} // namespace SettingsService

} // namespace lingya
