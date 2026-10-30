#pragma once
#include "domain/Domain.h"

#include <variant>

namespace lingya {


// 新工程与模板（规范§11、§9.1）：仅在内存创建合法Project，不触碰磁盘。
// 首版不内嵌图片附件，只支持本地图标资源ID（规范§4.1）。
// 默认样式统一取规范§9设计变量：卡片白、主文字、边框灰绿、主色连线——ProjectJson
// 严格读取要求颜色为#RRGGBB，故任何新建工作表都必须带上完整默认样式。
namespace ProjectFactory {

// 空白导图模板：单张树图工作表 + 根节点。M0纵向闭环使用。
Project createMindmap(const QString& title);

// M2起按规范§9.1补充的模板：鱼骨图（问题+6类原因）、流程图（开始节点）、甘特图（空任务视图）。
Project createFishbone(const QString& title);
Project createFlowchart(const QString& title);
Project createGantt(const QString& title);

// 按kind构造一张空工作表（含合法默认样式与初始内容），供“新建工作表”命令使用。
// kind只允许mindmap/fishbone/flowchart/gantt；其他值返回INVALID_ARGUMENT。
// 鱼骨图初始建好问题根节点与6类原因（人员/机器/材料/方法/测量/环境），满足R11“创建6类原因”。
std::variant<Sheet, AppError> createSheet(const QString& kind, const QString& name);

}  // namespace ProjectFactory

}  // namespace lingya
