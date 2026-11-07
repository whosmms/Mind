#include "services/ImageService.h"
#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
namespace lingya::ImageService
{
QImage decode(const QString &encoded)
{
    if (encoded.isEmpty() || encoded.size() > (kMaxPngBytes + 2) / 3 * 4)
        return {};
    const auto base64 = encoded.toLatin1();
    if (QString::fromLatin1(base64) != encoded)
        return {};
    const auto key =
        QString::fromLatin1(QCryptographicHash::hash(base64, QCryptographicHash::Sha256).toHex());
    static QMutex mutex;
    static QCache<QString, QImage> cache(16384);
    {
        QMutexLocker lock(&mutex);
        if (auto *found = cache.object(key))
            return *found;
    }
    const auto result =
        QByteArray::fromBase64Encoding(base64, QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.size() > kMaxPngBytes ||
        !result.decoded.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        return {};
    QBuffer buffer;
    buffer.setData(result.decoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png");
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 1024 || size.height() > 1024)
        return {};
    const auto image = reader.read();
    if (image.isNull())
        return {};
    {
        QMutexLocker lock(&mutex);
        cache.insert(key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
    }
    return image;
}
std::variant<QString, AppError> importFile(const QString &path)
{
    if (QFileInfo(path).size() > 20 * 1024 * 1024)
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("图片超过20MiB，请先缩小原图")};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64000000)
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("图片格式无效或像素过大，请使用较小的PNG/JPEG图片")};
    if (size.width() > 1024 || size.height() > 1024)
        reader.setScaledSize(size.scaled(1024, 1024, Qt::KeepAspectRatio));
    auto image = reader.read();
    if (image.isNull())
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("无法读取图片，请使用有效的PNG、JPEG、BMP或GIF首帧")};
    if (image.width() > 1024 || image.height() > 1024)
        image = image.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        png.clear();
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            break;
        if (png.size() <= kMaxPngBytes)
            return QString::fromLatin1(png.toBase64());
        image = image.scaled(qMax(1, image.width() * 3 / 4), qMax(1, image.height() * 3 / 4),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return AppError{"LIMIT_EXCEEDED", QStringLiteral("压缩后图片仍过大，请先缩小或裁剪图片")};
}
} // namespace lingya::ImageService
