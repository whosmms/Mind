#pragma once
#include "domain/Domain.h"
#include <QPicture>
#include <QRectF>
#include <variant>
namespace lingya
{
// 只读画面快照导出；文件写入均QSaveFile，无直接覆盖回退。
namespace ExportService
{
constexpr qint64 kMaxPngPixels = 32000000;
constexpr int kMaxPdfPages = 200;
struct Snapshot
{
    QPicture picture;
    QRectF bounds;
};
std::variant<QSize, AppError> pngSize(const QRectF &bounds, qreal scale);
std::variant<int, AppError> pdfPageCount(const QRectF &bounds, qreal scale = 1);
std::variant<bool, AppError> writePng(const Snapshot &snapshot, const QString &path,
                                      qreal scale = 1);
std::variant<int, AppError> writePdf(const Snapshot &snapshot, const QString &path,
                                     qreal scale = 1);
std::variant<QString, AppError> outline(const Project &project, const ID &sheetId);
std::variant<bool, AppError> writeText(const QString &text, const QString &path);
} // namespace ExportService
} // namespace lingya
