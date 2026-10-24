#pragma once
#include <QString>
#include <QVector>
class QSettings;

namespace lingya
{

// 非敏感设置（原设计§8、协作规范§11.3：最近文件与非敏感设置放QSettings，凭据不放QSettings）
// 只存白名单键；磁盘路径属本机会话信息，不进入Project、AI材料或日志。
// 读写接受注入的QSettings便于测试隔离（规范§19.1）；便捷重载使用应用默认QSettings。
namespace SettingsService
{

// 最近文件列表（新→旧），至多kMaxRecentFiles条；容忍空项并去重，路径规范化为绝对路径
QVector<QString> recentFiles(const QSettings &settings);

// 成功打开/另存后记录：路径规范化为绝对路径，置顶、去重（Windows路径大小写不敏感）、
// 超出kMaxRecentFiles时从末尾淘汰
void addRecentFile(QSettings &settings, const QString &path);

// 移除一条记录（文件缺失时由界面调用）；列表中不存在时为无操作
void removeRecentFile(QSettings &settings, const QString &path);

// 便捷重载：使用应用默认QSettings（main已登记组织与应用名）
QVector<QString> recentFiles();
void addRecentFile(const QString &path);
void removeRecentFile(const QString &path);

} // namespace SettingsService

} // namespace lingya
