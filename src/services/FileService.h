#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <variant>
#include "domain/Domain.h"

namespace lingya {


// 文件保存与恢复（规范§8、§11）
// 纯文件操作：路径由控制器经QFileDialog获得后传入，本服务不弹任何对话框，
// 可由线程任务调用（规范§11）。取消文件对话框由控制器单独处理。
// 路径只存在本机会话，不进入Project、AI材料或日志。
namespace FileService {

// 文件外部状态戳（§11.3：比较mtime与摘要识别外部修改）。sha256为文件字节摘要，
// mtimeUtc为修改时间（仅作辅助展示，比对以摘要为准——mtime精度与改动检测都不可靠）。
struct FileStamp {
    QDateTime mtimeUtc;
    QByteArray sha256;
};

// 读取当前文件戳；文件不存在或不可读时返回AppError
std::variant<FileStamp, AppError> stamp(const QString& path);

// 保存：先校验 → 序列化为UTF-8 JSON → 10MiB上限 → 旧文件先备份到同目录"灵芽备份"专用子目录 →
//       QSaveFile同目录临时文件+原子替换；显式关闭directWriteFallback。
//       首次保存（目标不存在）无旧内容可备份；覆盖已有文件时必有备份（2026-10-24按乙指示统一规则）。
// 成功返回savedRevision；任何失败保持调用方脏状态，返回AppError。
std::variant<qint64, AppError> save(const QString& path, const Project& project);

// 带外部修改检测的保存（§11.3）：覆盖已加载文件前，将磁盘当前戳与expectedStamp比对，
// 不一致返回EXTERNAL_MODIFICATION、不写入、保留磁盘上的外部内容；一致则按上述流程保存。
// 另存到新路径不适用本重载（无外部覆盖风险）。
std::variant<qint64, AppError> save(const QString& path, const Project& project,
                                    const FileStamp& expectedStamp);

// 读取：体积检查 → QJsonDocument解析 → ProjectJson严格字段校验 →
//       ProjectValidator引用与业务规则校验；全部通过才返回Project（规范§8）。
// 坏文件不覆盖当前工作，由调用方保持原工程不变（R04）。
std::variant<Project, AppError> load(const QString& path);

}  // namespace FileService

}  // namespace lingya
