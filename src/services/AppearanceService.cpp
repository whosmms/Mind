#include "services/AppearanceService.h"
#include <QFile>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
namespace lingya::AppearanceService
{
namespace
{
std::optional<AppError> validate(const Preferences &preferences)
{
    if (!QStringList{"mint", "lavender", "projection"}.contains(preferences.theme))
        return AppError{"INVALID_ARGUMENT", QStringLiteral("请选择有效界面配色")};
    QSet<QString> used;
    for (auto it = preferences.shortcuts.cbegin(); it != preferences.shortcuts.cend(); ++it)
    {
        if (!QRegularExpression("^[A-Za-z][A-Za-z0-9]{0,79}$").match(it.key()).hasMatch())
            return AppError{"INVALID_ARGUMENT", QStringLiteral("快捷键动作标识无效")};
        if (it.value().isEmpty())
            continue;
        const auto sequence = QKeySequence::fromString(it.value(), QKeySequence::PortableText);
        if (sequence.isEmpty() || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown)
            return AppError{"INVALID_ARGUMENT", QStringLiteral("每个快捷键请设置为一个组合")};
        const auto key = sequence[0].key();
        const auto mods = sequence[0].keyboardModifiers();
        if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde)
            if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
                return AppError{"INVALID_ARGUMENT",
                                QStringLiteral("字母或数字快捷键请加Ctrl或Alt，避免输入时触发")};
        if ((key == Qt::Key_F4 && mods == Qt::AltModifier) ||
            (key == Qt::Key_Delete && mods == (Qt::ControlModifier | Qt::AltModifier)) ||
            (mods & Qt::MetaModifier))
            return AppError{"INVALID_ARGUMENT",
                            QStringLiteral("该组合由系统使用，请选择其他快捷键")};
        const QString normalized = sequence.toString(QKeySequence::PortableText);
        if (used.contains(normalized))
            return AppError{"INVALID_ARGUMENT", QStringLiteral("快捷键重复：") + normalized};
        used.insert(normalized);
    }
    return {};
}
} // namespace
Preferences read(const QSettings &settings)
{
    Preferences preferences;
    preferences.theme = settings.value("appearance/theme", "mint").toString();
    const auto map = settings.value("appearance/shortcuts").toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        preferences.shortcuts.insert(it.key(), it.value().toString());
    if (validate(preferences))
    {
        preferences = {};
    }
    return preferences;
}
Preferences read()
{
    QSettings settings;
    return read(settings);
}
std::variant<bool, AppError> write(QSettings &settings, const Preferences &preferences)
{
    if (const auto error = validate(preferences))
        return *error;
    QVariantMap map;
    for (auto it = preferences.shortcuts.cbegin(); it != preferences.shortcuts.cend(); ++it)
        map.insert(it.key(), it.value());
    settings.setValue("appearance/theme", preferences.theme);
    settings.setValue("appearance/shortcuts", map);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return AppError{"WRITE_FAILED", QStringLiteral("本机偏好保存失败，请检查可写权限")};
    return true;
}
std::variant<bool, AppError> write(const Preferences &preferences)
{
    QSettings settings;
    return write(settings, preferences);
}
QString styleSheet(const QString &theme)
{
    QFile resource(":/lingya/styles/light.qss");
    if (!resource.open(QIODevice::ReadOnly))
        return {};
    QString style = QString::fromUtf8(resource.readAll());
    if (theme == "lavender")
    {
        style.replace("#247A62", "#675087")
            .replace("#DDF2E7", "#EDE6F5")
            .replace("#F0F7F2", "#F3EFF8")
            .replace("#EDF6F0", "#F3EFF8")
            .replace("#E8F4EA", "#EEE8F5");
    }
    else if (theme == "projection")
    {
        style.replace("#243A34", "#101D16")
            .replace("#6E8175", "#34483B")
            .replace("#F6F8F5", "#FFFFFF")
            .replace("#DDE6DF", "#88998E")
            .replace("#C7D4CB", "#88998E");
    }
    return style;
}
} // namespace lingya::AppearanceService
