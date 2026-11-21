#pragma once

#include "domain/Domain.h"
#include <QByteArray>
#include <QFile>
#include <QHash>

namespace lingya
{
// 只在内存中读取指定条目。源文件由调用方以只读方式打开。
class XMindArchive
{
  public:
    explicit XMindArchive(QFile &file) : m_file(file) {}
    std::optional<AppError> scan();
    bool contains(const QByteArray &name) const;
    std::variant<QByteArray, AppError> read(const QByteArray &name, qint64 limit) const;

  private:
    struct Entry
    {
        quint32 crc, compressedSize, size, offset;
        quint16 flags, method;
    };

    QFile &m_file;
    QHash<QByteArray, Entry> m_entries;
    qint64 m_directoryOffset = 0;
};
} // namespace lingya
