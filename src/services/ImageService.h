#pragma once
#include "domain/Domain.h"
#include <QImage>
namespace lingya::ImageService
{
constexpr int kMaxPngBytes = 256 * 1024;
QImage decode(const QString &encoded);
std::variant<QString, AppError> importFile(const QString &path);
} // namespace lingya::ImageService
