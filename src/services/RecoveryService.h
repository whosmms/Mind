#pragma once
#include "domain/Domain.h"
#include <QDateTime>
#include <QString>
#include <QVector>
#include <variant>

namespace lingya
{

// 恢复快照服务（规范§11.2恢复快照上限、§11.3读写与恢复；原设计§8/§12 M1）。
// 布局：恢复根目录/<工程ID>/<毫秒时间戳>.lingya；快照内容即完整.lingya工程JSON，
// 加载走与FileService::load相同的严格校验，不引入第二种文件格式。
// 恢复快照与正式保存是不同状态（原设计§8）：目录内数字时间戳命名的.lingya文件
// 均为本服务创建，清理只删除这类文件，绝不触碰目录外用户的正式文件或其他名称的文件。
// 纯文件操作、无界面、无对话框；单份快照≤10MiB且每工程至多5份，GUI线程直接调用即可。
namespace RecoveryService
{

// 快照元信息（list/启动预览用）：snapshotId为文件基名（毫秒时间戳），恢复与清理以此定位
struct SnapshotInfo
{
    QString projectId;   // 所属工程UUID（目录名）
    QString snapshotId;  // 文件基名（毫秒时间戳）
    QString title;       // 工程标题（预览列表展示）
    QDateTime savedAt;   // 快照时间（取自文件名时间戳）
    qint64 revision = 0; // 快照内Project.revision
};

// 写入结果：written=false表示内容与该工程最新一份快照相同被跳过
// （规范§11.3“仅当内容变化时写一份”，比对以序列化字节为准）
struct SnapshotWrite
{
    QString snapshotId;
    bool written = false;
};

// 默认恢复根目录：QStandardPaths::AppLocalDataLocation/recovery（原设计§8）
QString defaultRoot();

// 某工程的快照列表（新→旧；损坏或非本服务命名规则的文件跳过，不报错）
QVector<SnapshotInfo> list(const QString &root, const QString &projectId);

// 全部工程的快照合并列表（新→旧）；启动预览用（未命名工程也有UUID，同样可恢复）
QVector<SnapshotInfo> listAll(const QString &root);

// 读取并完整校验一份快照：体积→JSON严格解析→ProjectValidator，全部通过才返回Project（规范§8）
std::variant<Project, AppError> load(const QString &root, const QString &projectId,
                                     const QString &snapshotId);

// 写入快照：工程校验→序列化→与最新一份快照字节比对（相同跳过）→QSaveFile原子写→
// 修剪为每工程最近kMaxRecoverySnapshots份成功快照（规范§11.2/§11.3）
std::variant<SnapshotWrite, AppError> writeSnapshot(const QString &root, const Project &project);

// 清除某工程的全部快照：只删除recovery/<projectId>/下本服务创建的快照文件（规范§11.3）。
// 正式保存成功或用户明确放弃修改后由界面调用；目录内其他名称的文件不动
void clear(const QString &root, const QString &projectId);

} // namespace RecoveryService

} // namespace lingya
