#pragma once
#include <QVector>
#include "domain/Domain.h"

namespace lingya {


// 引用与业务规则校验（规范§4.3不变量、§8加载顺序）
// 读取文件时在字段校验（ProjectJson）之后运行：字段类型合法但引用或结构非法的工程在此拒绝。
// M0已实现：schemaVersion、工程内ID唯一（规则1）、树不变量（规则1：单根、父级存在、无环、
//           全部可达）、kind与layout配对（规则2）、关系线端点（规则7）、分组注释成员与父级、
//           任务排期与进度（规则5）、任务依赖前驱存在且无环、标题长度（规则8）。
// 待M1-M4补充：任务标题与节点标题一致性修复预览（规则4）、甘特关联细节、
//           卡片与路线的运行时重校验（§7.1、§7.2在删除时处理，加载时不作为错误）。
namespace ProjectValidator {

// 返回空向量表示校验通过
QVector<AppError> validate(const Project& project);

}  // namespace ProjectValidator

}  // namespace lingya
