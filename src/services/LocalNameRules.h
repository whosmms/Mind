#pragma once
#include <QString>

namespace lingya
{
namespace LocalNameRules
{

// 本地文件/目录单段名称的Windows基础规则；调用方先去首尾空白。
// 不接受路径、流名称、尾点/空格或保留设备名，不触碰文件系统。
inline bool validComponent(const QString &name)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        return false;
    const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (const QChar &ch : name)
        if (ch.category() == QChar::Other_Control || forbidden.contains(ch))
            return false;
    const QString device = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (device == QStringLiteral("CON") || device == QStringLiteral("PRN") ||
        device == QStringLiteral("AUX") || device == QStringLiteral("NUL"))
        return false;
    if (device.size() == 4 &&
        (device.startsWith(QStringLiteral("COM")) || device.startsWith(QStringLiteral("LPT"))) &&
        QStringLiteral("123456789¹²³").contains(device.at(3)))
        return false;
    return true;
}

} // namespace LocalNameRules
} // namespace lingya
