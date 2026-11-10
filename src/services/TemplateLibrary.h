#pragma once
#include "domain/Domain.h"
namespace lingya::TemplateLibrary
{
struct Entry
{
    QString id, title, kind, description;
};
QVector<Entry> entries();
std::variant<Project, AppError> create(const QString &id);
} // namespace lingya::TemplateLibrary
