#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <variant>
#include "domain/Domain.h"

namespace lingya {


// .lingya 的显式序列化与反序列化（规范§4）
// 写出：UTF-8 JSON，schemaVersion=1；QMap→ID键对象，QVector/QStringList→数组，
//       QPointF→{x,y}，variant按kind分派，结构体继承字段平铺；
//       optional的parentId、iconId、link、source、日期和筛选来源无值时写null；
//       局部样式中未设置的字段及缺省provenance省略。
// 读取：严格类型检查（字符串isString、数字isDouble+有限+整数+范围），
//       枚举白名单、颜色#RRGGBB、链接仅http/https、日期yyyy-MM-dd往返比对、
//       时间戳UTC ISODateWithMs、revision限定JSON安全整数、每个已定义对象拒绝未知字段。
// 引用与业务规则（树环、任务依赖等）由ProjectValidator在本层之后校验（规范§8）。
namespace ProjectJson {

QJsonObject toJson(const Project& project);
QByteArray toBytes(const Project& project);   // 紧凑UTF-8，供FileService写盘

// 解析失败或字段校验失败返回AppError（code为INVALID_PROJECT）；文件版本高于1返回UNSUPPORTED_SCHEMA
// （错误码来源：三人协作开发统一规范§10.3）
std::variant<Project, AppError> fromBytes(const QByteArray& data);
std::variant<Project, AppError> fromObject(const QJsonObject& object);

}  // namespace ProjectJson

}  // namespace lingya
