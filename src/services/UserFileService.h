#pragma once
#include "domain/Domain.h"
#include <QDateTime>
#include <QString>
#include <QVector>
#include <variant>

namespace lingya
{

class Project;

// 用户专有文件区（登录功能，2026-10-25用户新增需求；设计见decisions D-M2-02）。
// 目录约定：<root>/users/<用户名>/ 存放该用户登录态保存的.lingya工程；
// users/之外的路径视为公有文件。保存复用FileService（校验/备份/原子写），
// 备份目录"灵芽备份"随之留在用户目录内，不越出专有区。
namespace UserFileService
{

// 列表项（我的文件对话框与测试共用）
struct OwnedFileInfo
{
    QString name; // 文件基名（不含.lingya）
    QDateTime modified;
    qint64 bytes = 0;
};

// 用户目录：<root>/users/<userName>（userName应为登录成功的账号名）
QString userDir(const QString &root, const QString &userName);

// 登录态保存：仅命名不选路径。name去首尾空白、禁路径分隔符与".."、自动补.lingya；
// 落盘经FileService::save（校验+备份+原子写），成功返回完整路径。
// 同名已有文件按正式保存语义覆盖（覆盖前FileService自动备份旧内容）
std::variant<QString, AppError> saveOwned(const QString &root, const QString &userName,
                                          const QString &name, const Project &project);

// 我的文件列表：该用户目录下的*.lingya，按修改时间新→旧（相同时间按名称）
QVector<OwnedFileInfo> listOwned(const QString &root, const QString &userName);

// 按名打开自己的文件（FileService::load同规则严格校验；坏文件不覆盖调用方当前工程）
std::variant<Project, AppError> openOwned(const QString &root, const QString &userName,
                                          const QString &name);

// 打开权限归属（登录态拦截依据；未登录不受限，维持现状）：
// Own=users/<userName>/内；Others=users/其他目录内；Public=users/之外任意路径
enum class Access
{
    Own,
    Others,
    Public
};
Access classify(const QString &root, const QString &userName, const QString &path);

} // namespace UserFileService

} // namespace lingya
