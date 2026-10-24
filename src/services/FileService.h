#pragma once
#include <QString>
#include <variant>
#include "domain/Domain.h"

namespace lingya {


// 文件保存与恢复（规范§8、§11）
// 纯文件操作：路径由控制器经QFileDialog获得后传入，本服务不弹任何对话框，
// 可由线程任务调用（规范§11）。取消文件对话框由控制器单独处理。
// 路径只存在本机会话，不进入Project、AI材料或日志。
namespace FileService {

// 保存：先校验 → 序列化为UTF-8 JSON → 10MiB上限 → 旧文件先备份.bak →
//       QSaveFile同目录临时文件+原子替换；显式关闭directWriteFallback。
// 成功返回savedRevision；任何失败保持调用方脏状态，返回AppError。
std::variant<qint64, AppError> save(const QString& path, const Project& project);

// 读取：体积检查 → QJsonDocument解析 → ProjectJson严格字段校验 →
//       ProjectValidator引用与业务规则校验；全部通过才返回Project（规范§8）。
// 坏文件不覆盖当前工作，由调用方保持原工程不变（R04）。
std::variant<Project, AppError> load(const QString& path);

}  // namespace FileService

}  // namespace lingya
