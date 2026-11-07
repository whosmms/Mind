#pragma once
#include "domain/Domain.h"
#include <QMap>
#include <variant>
class QSettings;
namespace lingya::AppearanceService
{
struct Preferences
{
    QString theme = "mint";
    QMap<QString, QString> shortcuts;
};
Preferences read(const QSettings &settings);
Preferences read();
std::variant<bool, AppError> write(QSettings &settings, const Preferences &preferences);
std::variant<bool, AppError> write(const Preferences &preferences);
QString styleSheet(const QString &theme);
} // namespace lingya::AppearanceService
