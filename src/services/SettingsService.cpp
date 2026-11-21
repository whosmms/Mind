#include "services/SettingsService.h"

#include "domain/Limits.h"
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <algorithm>

namespace lingya
{

namespace
{
constexpr auto kRecentKey = "recentFiles";
constexpr auto kPinnedKey = "pinnedRecentFiles";

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

bool includesPath(const QStringList &items, const QString &path)
{
    return items.contains(path, Qt::CaseInsensitive);
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
    const auto pins = pinnedRecentFiles(settings);
    std::stable_partition(result.begin(), result.end(),
                          [&pins](const QString &path) { return includesPath(pins, path); });
    while (result.size() > Limits::kMaxRecentFiles)
        result.removeLast();
    return result;
}

QStringList pinnedRecentFiles(const QSettings &settings)
{
    return settings.value(QLatin1String(kPinnedKey)).toStringList();
}

void setRecentFilePinned(QSettings &settings, const QString &path, bool pinned)
{
    if (path.trimmed().isEmpty())
        return;
    const auto normalized = normalize(path);
    auto pins = pinnedRecentFiles(settings);
    pins.removeIf([&](const QString &item) { return samePath(item, normalized); });
    if (pinned && includesPath(readRaw(settings), normalized))
        pins.append(normalized);
    settings.setValue(QLatin1String(kPinnedKey), pins);
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
    const auto pins = pinnedRecentFiles(settings);
    while (items.size() > Limits::kMaxRecentFiles)
    {
        auto victim = std::find_if(items.crbegin(), items.crend(), [&pins](const QString &item)
                                   { return !includesPath(pins, item); });
        if (victim == items.crend())
            break;
        items.removeAt(qsizetype(items.crend() - victim - 1));
    }
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
    setRecentFilePinned(settings, path, false);
}

QVector<QString> recentFiles()
{
    QSettings settings;
    return recentFiles(settings);
}

QStringList pinnedRecentFiles()
{
    QSettings settings;
    return pinnedRecentFiles(settings);
}

void setRecentFilePinned(const QString &path, bool pinned)
{
    QSettings settings;
    setRecentFilePinned(settings, path, pinned);
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
