#pragma once
#include "domain/Domain.h"
#include <QDateTime>
#include <QString>
#include <variant>

namespace lingya
{

class Project;

// 本地账号与会话（登录功能，2026-10-25用户新增需求；设计见decisions D-M2-02）。
// 离线课程软件：账号库存AppLocalDataLocation/accounts.json（内部文件，非.lingya格式），
// 密码以每账号随机盐+SHA-256哈希存储，不存明文——这是课程级本地隔离，不是服务端安全。
// 会话仅存内存：重启后需重新登录；root由调用方注入（默认AppLocalDataLocation，测试可重定向）。
// 用户名规则：去首尾空白后2至32个Unicode码点，禁路径分隔符与Windows文件名非法字符。
namespace SessionService
{

struct AccountInfo
{
    QString userName;
    QDateTime createdAt;
};

// 注册账号：成功后即可login；用户名重复返回CONFLICT，规则不满足返回INVALID_ARGUMENT
std::variant<bool, AppError> createAccount(const QString &root, const QString &userName,
                                           const QString &password);

// 登录校验：用户名不存在或密码错误返回AUTH_FAILED（D-M2-02登记的新错误码）
std::variant<AccountInfo, AppError> login(const QString &root, const QString &userName,
                                          const QString &password);

// 用户名合法性（去空白后2..32码点，禁 /\:*?"<>| 与控制字符）
bool validUserName(const QString &userName);

} // namespace SessionService

} // namespace lingya
