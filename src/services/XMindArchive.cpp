#include "services/XMindArchive.h"

#include <QtEndian>
#include <zlib.h>

namespace lingya
{
namespace
{
constexpr int kEndSize = 22;
constexpr int kCentralSize = 46;
constexpr int kLocalSize = 30;
constexpr qint64 kMaxDirectorySize = 16 * 1024 * 1024;
constexpr quint32 kEndSignature = 0x06054b50;
constexpr quint32 kCentralSignature = 0x02014b50;
constexpr quint32 kLocalSignature = 0x04034b50;
constexpr quint16 kEncryptionFlags = 0x2041;

quint16 u16(const char *bytes, int offset)
{
    return qFromLittleEndian<quint16>(bytes + offset);
}

quint32 u32(const char *bytes, int offset)
{
    return qFromLittleEndian<quint32>(bytes + offset);
}

AppError archiveError()
{
    return {"INVALID_PROJECT", QStringLiteral("XMind 压缩包不完整或格式无效")};
}

std::variant<QByteArray, AppError> inflateEntry(QByteArray compressed, qsizetype expected)
{
    QByteArray output(expected + 1, Qt::Uninitialized);
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
        return archiveError();
    }
    const int result = inflate(&stream, Z_FINISH);
    const bool complete = result == Z_STREAM_END && stream.avail_in == 0 &&
                          stream.total_out == static_cast<uLong>(expected);
    inflateEnd(&stream);
    if (!complete)
    {
        return archiveError();
    }
    output.truncate(expected);
    return output;
}
} // namespace

std::optional<AppError> XMindArchive::scan()
{
    const qint64 fileSize = m_file.size();
    const qint64 tailSize = qMin<qint64>(fileSize, kEndSize + 65535);
    if (tailSize < kEndSize || !m_file.seek(fileSize - tailSize))
    {
        return archiveError();
    }
    const QByteArray tail = m_file.read(tailSize);
    if (tail.size() != tailSize)
    {
        return archiveError();
    }
    qsizetype end = tail.size() - kEndSize;
    while (end >= 0)
    {
        const auto *header = tail.constData() + end;
        if (u32(header, 0) == kEndSignature && end + kEndSize + u16(header, 20) == tail.size())
        {
            break;
        }
        --end;
    }
    if (end < 0)
    {
        return archiveError();
    }
    const auto *header = tail.constData() + end;
    const auto count = u16(header, 10);
    if (u16(header, 4) != 0 || u16(header, 6) != 0 || u16(header, 8) != count || count == 65535)
    {
        return AppError{"INVALID_PROJECT",
                        QStringLiteral("暂不支持分卷或 ZIP64 格式的 XMind 压缩包")};
    }
    const qint64 size = u32(header, 12);
    m_directoryOffset = u32(header, 16);
    if (size > kMaxDirectorySize || size < qint64(count) * kCentralSize ||
        m_directoryOffset + size != fileSize - tailSize + end || !m_file.seek(m_directoryOffset))
    {
        return archiveError();
    }
    const QByteArray directory = m_file.read(size);
    if (directory.size() != size)
    {
        return archiveError();
    }
    qsizetype offset = 0;
    for (int i = 0; i < count; ++i)
    {
        if (offset + kCentralSize > directory.size())
        {
            return archiveError();
        }
        const auto *entry = directory.constData() + offset;
        const auto length = u16(entry, 28);
        const qsizetype next = offset + kCentralSize + length + u16(entry, 30) + u16(entry, 32);
        if (u32(entry, 0) != kCentralSignature || next > directory.size() || u16(entry, 34) != 0)
        {
            return archiveError();
        }
        const QByteArray name(entry + kCentralSize, length);
        if (name.isEmpty() || m_entries.contains(name))
        {
            return archiveError();
        }
        m_entries.insert(name, {u32(entry, 16), u32(entry, 20), u32(entry, 24), u32(entry, 42),
                                u16(entry, 8), u16(entry, 10)});
        offset = next;
    }
    return offset == directory.size() ? std::optional<AppError>{} : archiveError();
}

bool XMindArchive::contains(const QByteArray &name) const
{
    return m_entries.contains(name);
}

std::variant<QByteArray, AppError> XMindArchive::read(const QByteArray &name, qint64 limit) const
{
    const auto entry = m_entries.constFind(name);
    if (entry == m_entries.cend())
    {
        return AppError{"FILE_NOT_FOUND", QStringLiteral("XMind 文件缺少所需条目")};
    }
    if (entry->flags & kEncryptionFlags)
    {
        return AppError{"INVALID_PROJECT", QStringLiteral("请用 XMind 解除文件密码后再导入")};
    }
    if (entry->size > limit || entry->compressedSize > limit + 1024 * 1024)
    {
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("XMind 文档内容超过导入大小上限")};
    }
    if (qint64(entry->offset) + kLocalSize > m_directoryOffset || !m_file.seek(entry->offset))
    {
        return archiveError();
    }
    const QByteArray header = m_file.read(kLocalSize);
    const auto *bytes = header.constData();
    if (header.size() != kLocalSize || u32(bytes, 0) != kLocalSignature ||
        u16(bytes, 6) != entry->flags || u16(bytes, 8) != entry->method ||
        u16(bytes, 26) != name.size() || m_file.read(name.size()) != name)
    {
        return archiveError();
    }
    const qint64 start = m_file.pos() + u16(bytes, 28);
    if (start + entry->compressedSize > m_directoryOffset || !m_file.seek(start))
    {
        return archiveError();
    }
    QByteArray content = m_file.read(entry->compressedSize);
    if (content.size() != entry->compressedSize)
    {
        return archiveError();
    }
    if (entry->method == Z_DEFLATED)
    {
        auto decoded = inflateEntry(std::move(content), entry->size);
        if (const auto *error = std::get_if<AppError>(&decoded))
        {
            return *error;
        }
        content = std::get<QByteArray>(std::move(decoded));
    }
    else if (entry->method != 0 || entry->size != entry->compressedSize)
    {
        return AppError{"INVALID_PROJECT", QStringLiteral("XMind 文件使用了暂不支持的压缩方式")};
    }
    if (crc32(0, reinterpret_cast<const Bytef *>(content.constData()),
              static_cast<uInt>(content.size())) != entry->crc)
    {
        return archiveError();
    }
    return content;
}
} // namespace lingya
